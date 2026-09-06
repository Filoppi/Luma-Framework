#pragma once

// ResourceUpgradeManager
// ======================
// Owns all state and configuration related to resource format upgrades and resolution
// scaling of indirectly-upgraded (mirrored) resources. This centralizes:
//   - the upgrade/scale configuration (formats, size filters, LUTs, per-shader hashes, ...)
//   - the runtime mirror bookkeeping (original<->mirror resource and view maps, pending frees)
//   - the logic that decides when/how a resource or view gets upgraded/scaled
//
// The manager owns its own mutex(es) for its resources/config. Core.hpp (and the per-game
// code) orchestrate by passing the current frame's resolution / SR state as parameters,
// so the manager stays decoupled from DeviceData (no circular include). It can toggle each
// feature independently (upgrade on/off, scaling on/off, chain dependencies on/off).

#include <algorithm>
#include <bit>
#include <cfloat>
#include <cstdint>
#include <optional>
#include <shared_mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <include/reshade.hpp>

#include "debug.h"
#include "shader_types.h"
#include "utils/format.hpp"

// Forward-declared scale/SR state the manager needs to make scaling decisions. Passed in by
// core.hpp rather than read from DeviceData, to avoid a circular dependency.
struct ResourceUpgradeFrameState
{
   float2 render_resolution = { 1.f, 1.f };
   float2 output_resolution = { 1.f, 1.f };
   bool has_drawn_sr = false;
   bool sr_active = false; // SR configured and not suppressed
};

// Local copy of the IsMipOf helper (avoids pulling in utils/resource.hpp, which depends on DeviceData).
inline bool ResourceUpgradeIsMipOf(uint32_t base_w, uint32_t base_h, uint32_t w, uint32_t h)
{
   if (w == 0 || h == 0 || base_w == 0 || base_h == 0)
      return false;
   if (base_w < w || base_h < h)
      return false;
   bool valid_w = (base_w >> (std::countr_zero(base_w) - std::countr_zero(w))) == w;
   bool valid_h = (base_h >> (std::countr_zero(base_h) - std::countr_zero(h))) == h;
   return valid_w && valid_h;
}

class ResourceUpgradeManager
{
public:
   enum class TextureFormatUpgradesType : uint8_t
   {
      None,
      AllowedDisabled,
      AllowedEnabled
   };
   enum class ChainTextureFormatUpgradesType : uint
   {
      None,
      // Direct "copies" of the texture, like: CopyResource, CopySubresourceRegion, ResolveSubresource
      DirectDependencies,
      // Also indirect dependencies of the texture, like a SRV or UAV being read in a pixel or compute shader
      DirectAndIndirectDependencies
   };
   enum class SwapchainUpgradeType : uint8_t
   {
      // keep the original one, SDR or whatnot
      None,
      // scRGB linear HDR (16 bit float)
      scRGB,
      // BT.2020 PQ HDR (10 bit UNORM)
      HDR10
   };
   // Initialize freshly created mirrors with the original's current content (converted when needed).
   // TODO: Add a warning for textures we missed upgrading if the swapchain resolution changed later.
   enum class TextureFormatUpgrades2DSizeFilters : uint32_t
   {
      // If the flags are set to 0, we upgrade all textures independently of their size. This flag can't be added separately!
      All = 0,
      // The output resolution (usually matches the window resolution too).
      SwapchainResolution = 1 << 0,
      // The rendering resolution (e.g. for TAA and other types of upscaling).
      RenderResolution = 1 << 1,
      // The aspect ratio of the swapchain texture.
      // This can be useful for bloom or resolution scaling etc.
      // Ideally we'd also check the rendering resolution, but we can't really reliably determine it until rendering has started and textures have been created.
      SwapchainAspectRatio = 1 << 2,
      RenderAspectRatio = 1 << 3,
      // A custom aspect ratio (defaulted to 16:9, because that's the global standard).
      // It can be useful for games that don't support UltraWide or 4:3 resolutions and internally force 16:9 rendering, while having a fullscreen swapchain with black bars.
      CustomAspectRatio = 1 << 4,
      // All mip chain sizes based starting from the highest resolution between rendering and swapchain resolution (they should generally have the same aspect ratio anyway) to 1.
      // This can be useful for blur passes etc, if they used power of 2 mips, instead of simply halving the base resolution.
      Mips = 1 << 5,
      // Upgrade textures cubes (of all sizes), these are sometimes used by old games to do reflections (e.g. Burnout Revenge cars reflections)
      Cubes = 1 << 6,
      // Checks the swapchain/output resolution width only (e.g. used by games that add horizontal lines, like "Thumper" or "Beyond: Two Souls").
      // These are usually hard to match to an aspect ratio without using the "CustomAspectRatio" with a manually found aspect ratio,
      // and thus mips like bloom might be missing
      SwapchainResolutionWidth = 1 << 7,
      SwapchainResolutionHeight = 1 << 8,
      // Avoid upgrading 1x1 textures
      No1Px = 1 << 9,
      // Custom sizes (width/height pairs) to match for upgrades.
      CustomSize = 1 << 10,
      // "None" needs to be != 0, and specify all the negating flags
      None = No1Px,
   };
   enum class LUTDimensions
   {
      _1D,
      _2D,
      _3D
   };

   struct AutoTextureFormatUpgradeShaderHash
   {
      std::vector<uint8_t> rtv_slots;
      std::vector<uint8_t> uav_slots;
      bool scale = false; // When the hash-upgrade scale chain is active (SR upscaled early this frame), the mirror for this shader is created at output resolution instead of render resolution.
   };

   // Per-resource mirror bookkeeping.
   struct IndirectUpgradedResource
   {
      uint64_t mirror_handle = 0;
      bool is_scaled = false;
      uint32_t original_width = 0;
      uint32_t original_height = 0;
      uint32_t mirror_width = 0;
      uint32_t mirror_height = 0;
   };

   // ------------------------------------------------------------------
   // Configuration (source of truth). Games set these through the manager.
   // ------------------------------------------------------------------
   TextureFormatUpgradesType texture_format_upgrades_type = TextureFormatUpgradesType::None;
   // Whether texture upgrades (the ones that happen on resource creation) are done directly on the original resource, or on an upgraded mirrored version of it that we keep separately and live replace when the original resource is referenced.
   // Indirect upgrades might be safer, and can be made more selective, to avoid upgrading random textures, though they also keep the original texture so memory usage goes up.
   // Note: indirect upgrades will fail to replace references to resources if the game had DLSS/Streamline calls, as we can't intercept their calls to DX (at least in some cases?).
   // These are sometimes referred to as: indirect, mirrored, proxy, redirected, cloned, ...
   // See "FindOrCreateIndirectUpgradedResource()" for the main functionality.
   bool enable_indirect_texture_format_upgrades = false;
   // Automatically upgrade all textures that are used as target of an indirect upgraded resource, and their views.
   // Indirect texture mirrors might still be automatically created if "texture_format_upgrades_type" is enabled, in case the game tried to copy an upgraded resource into an incompatible one that wasn't upgraded etc.
   // This can work even without "enable_indirect_texture_format_upgrades", in case we upgraded textures through "auto_texture_format_upgrade_shader_hashes", or in case a texture wasn't a render target but was used as copy target of one.
   // It's generally suggested to true if "enable_indirect_texture_format_upgrades" is enabled, unless you are use it works fine without and want to maximize performance.
   ChainTextureFormatUpgradesType enable_chain_indirect_texture_format_upgrades = ChainTextureFormatUpgradesType::None;
   // Allows to temporarily ignore indirectly upgraded textures
   // In publishing mode, there's no need to ever forcefully ignore the indirectly upgraded textures,
   // given that the settings can't change live, hence they are not created if they are not enabled in the first place.
   bool ignore_indirect_upgraded_textures = false; // TODO: test why when this is turned off live in Lego City Undercover, the output breaks
   // List of render targets (and unordered access) textures that we upgrade to R16G16B16A16_FLOAT or other formats (depends on GetBestResourceUpgradeFormat()).
   // Most formats are supported but some might not act well when upgraded.
   std::unordered_set<reshade::api::format> texture_upgrade_formats;
   // Similar to "texture_upgrade_formats" but allows upgrading depth to R32_FLOAT/D32_FLOAT instead (e.g. useful in old games, especially when they allocated bits for stencil without using them)
   std::unordered_set<reshade::api::format> texture_depth_upgrade_formats;
   // Redirect incompatible copies between UNORM and FLOAT textures to a custom pixel shader that would do the same (not globally compatible).
   // This can happen if the game uses a temp texture that isn't either a render target nor is unordered access, so we don't upgrade it.
   bool enable_upgraded_texture_resource_copy_redirection = true; // TODO: delete given that we now have "enable_indirect_texture_format_upgrades"

   uint32_t texture_format_upgrades_2d_size_filters = 0 | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainResolution | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainAspectRatio;
   std::unordered_set<float> texture_format_upgrades_2d_custom_aspect_ratios = { 16.f / 9.f };
   // Custom sizes (width/height pairs) to match for upgrades.
   std::vector<uint2> texture_format_upgrades_2d_custom_sizes = {};
   // Most games do resolution scaling properly, with a maximum aspect ratio offset of 1 pixel, though occasionally it goes to 2 pixels of difference.
   // Set to 0 to only accept 100% matching aspect ratio.
   uint32_t texture_format_upgrades_2d_aspect_ratio_pixel_threshold = 1;
   // The size of the LUT we might want to upgrade, whether it's 1D, 2D or 3D.
   // LUTs in most games are 16x or 32x, though in some cases they might be 15x, 31x, 48x, 64x etc.
   uint32_t texture_format_upgrades_lut_size = -1;
   LUTDimensions texture_format_upgrades_lut_dimensions = LUTDimensions::_2D;

   // Automatically upgrade the formats of the textures this shader pass draws to. Generally best used on shaders that originally encoded from HDR (native rendering) to SDR. If the source textures were SDR too (UNORM), they'd need to be upgraded through other means.
   // "rtv_slots" are the RTV indexes to upgrade, "uav_slots" the UAVs (whether it's a pixel or compute shader).
   // This is meant to be used if "enable_indirect_texture_format_upgrades" is off, or if very specific custom upgrades are needed.
   // This assumes that when the upgraded texture is created (it could be at any time, if the target shader doesn't always run), the original texture values aren't relevant, because they won't be preserved.
   // Requires "enable_chain_indirect_texture_format_upgrades" to work, otherwise views from the new indirect upgraded textures don't ever get mirrored.
   std::unordered_map<uint32_t, AutoTextureFormatUpgradeShaderHash> auto_texture_format_upgrade_shader_hashes;

   // Optional pre-seed ordering gate: when enabled, chain resources are not redirected to
   // output-resolution mirrors until their origin seed has actually drawn this frame. Default OFF
   // to preserve existing behaviour.
   bool enable_pre_seed_ordering_gate = false;

   // ------------------------------------------------------------------
   // Runtime mirror state
   // ------------------------------------------------------------------
   std::unordered_set<uint64_t> upgraded_resources; // All the directly upgraded resources, excluding the swapchains backbuffers, as they are created internally by DX
#if DEVELOPMENT
   std::unordered_map<uint64_t, reshade::api::format> original_upgraded_resources_formats; // Maps the original resource to its direct upgraded format. These include the swapchain buffers too!
   std::unordered_map<uint64_t, std::pair<uint64_t, reshade::api::format>> original_upgraded_resource_views_formats; // All the views for direct upgraded resources, with the resource and the original resource view format
#endif
   std::unordered_map<uint64_t, IndirectUpgradedResource> original_resources_to_mirrored_upgraded_resources; // TODO: convert/copy the initial/current data from the source texture when created. Also rename to "indirect_upgraded"
   std::unordered_map<uint64_t, uint64_t> original_resource_views_to_mirrored_upgraded_resource_views;
   // Mirror views grouped by their mirror resource. Lets OnDestroyResource unlink a destroyed mirror's views by
   // iterating only that mirror's (small) set with plain hash lookups, instead of scanning the whole view map with
   // a device call (get_resource_from_view) per entry while holding the lock.
   std::unordered_map<uint64_t, std::unordered_set<uint64_t>> mirror_views_by_mirror_resource;
   // View handle -> resource handle caches, so the draw/descriptor/destroy paths never need a device call
   // (get_resource_from_view) while holding the luma mutex (lock-order inversion vs D3D11 runtime locks taken in
   // destruction-notifier callbacks). Populated in OnInitResourceView / at mirror-view insert sites.
   std::unordered_map<uint64_t, uint64_t> original_views_to_resources;      // game view -> its resource
   std::unordered_map<uint64_t, uint64_t> mirror_views_to_mirror_resources; // mirror view -> mirror resource
   // Mirrors freed at present (frame boundary) instead of on original destruction, so in-flight
   // hooks/game state can never dereference a freed mirror. Guarded by `mutex`.
   std::vector<reshade::api::resource> pending_mirror_resource_destructions;
   std::vector<reshade::api::resource_view> pending_mirror_view_destructions;

   // ------------------------------------------------------------------
   // Queries (all lock-free: the caller/core.hpp holds the single device mutex)
   // ------------------------------------------------------------------

   // View handle -> resource handle from our caches (no device call under the lock).
   uint64_t GetCachedResourceFromView(uint64_t view_handle) const
   {
      if (auto it = mirror_views_to_mirror_resources.find(view_handle); it != mirror_views_to_mirror_resources.end())
         return it->second;
      if (auto it = original_views_to_resources.find(view_handle); it != original_views_to_resources.end())
         return it->second;
      return 0;
   }

   bool IsUpgraded(uint64_t resource_handle) const
   {
      return upgraded_resources.contains(resource_handle);
   }

   bool HasMirror(uint64_t resource_handle) const
   {
      return original_resources_to_mirrored_upgraded_resources.contains(resource_handle);
   }

   // True when either resource is a scaled (output-resolution) indirect mirror.
   bool IsScaledMirrorResource(uint64_t resource_handle) const
   {
      if (resource_handle == 0)
         return false;
      if (auto it = original_resources_to_mirrored_upgraded_resources.find(resource_handle); it != original_resources_to_mirrored_upgraded_resources.end())
         return it->second.is_scaled;
      for (const auto& [orig, mirror] : original_resources_to_mirrored_upgraded_resources)
      {
         if (mirror.mirror_handle == resource_handle)
            return mirror.is_scaled;
      }
      return false;
   }

   uint64_t GetMirrorHandle(uint64_t resource_handle) const
   {
      if (auto it = original_resources_to_mirrored_upgraded_resources.find(resource_handle); it != original_resources_to_mirrored_upgraded_resources.end())
         return it->second.mirror_handle;
      return 0;
   }

   // True when the hash-upgrade scale chain is active this frame: a scale-enabled seed exists and SR
   // upscaled early. Everything scale-specific (the shader flag probe and scaled copies) is gated on this.
   bool IsScaleChainActive(const ResourceUpgradeFrameState& state) const
   {
      if (!state.has_drawn_sr)
         return false;
      for (const auto& entry : auto_texture_format_upgrade_shader_hashes)
      {
         if (entry.second.scale)
            return true;
      }
      return false;
   }

   // ------------------------------------------------------------------
   // Format helpers
   // ------------------------------------------------------------------

   reshade::api::format GetBestResourceUpgradeFormat(const reshade::api::resource_desc& desc) const
   {
      const bool is_depth = (desc.usage & reshade::api::resource_usage::depth_stencil) != 0;

      if (is_depth)
      {
         // Preserve the stencil if we can!
         if (desc.texture.format == reshade::api::format(DXGI_FORMAT_R24G8_TYPELESS)
            || desc.texture.format == reshade::api::format(DXGI_FORMAT_D24_UNORM_S8_UINT)
            || desc.texture.format == reshade::api::format(DXGI_FORMAT_R32G8X24_TYPELESS))
         {
            return reshade::api::format::r32_g8_typeless;
         }
         return reshade::api::format::r32_typeless; // Create it as typeless of the maximum depth, so we can also cast it as SRV
      }

      return reshade::api::format::r16g16b16a16_float;
   }

   reshade::api::format GetBestResourceViewUpgradeFormat(const reshade::api::resource_view_desc& original_view_desc, reshade::api::resource_usage usage_type, const reshade::api::resource_desc& original_desc, const reshade::api::resource_desc& upgraded_desc) const
   {
      // Straight forward upgrade (fast common path), couldn't really be otherwise
      if (upgraded_desc.texture.format == reshade::api::format::r16g16b16a16_float)
      {
         return reshade::api::format::r16g16b16a16_float;
      }
      // Depth
      else if (upgraded_desc.texture.format == reshade::api::format::r32_typeless || upgraded_desc.texture.format == reshade::api::format::r32_float || upgraded_desc.texture.format == reshade::api::format::d32_float)
      {
         if ((usage_type & reshade::api::resource_usage::depth_stencil) != 0)
         {
            return reshade::api::format::d32_float;
         }
         else
         {
            ASSERT_ONCE(IsFloatFormat(DXGI_FORMAT(original_view_desc.format))); // We might not want to use a float view in this case? We probably do anyway!
            return reshade::api::format::r32_float;
         }
      }
      // Depth + Stencil
      else if (upgraded_desc.texture.format == reshade::api::format::r32_g8_typeless || upgraded_desc.texture.format == reshade::api::format::d32_float_s8_uint)
      {
         if ((usage_type & reshade::api::resource_usage::depth_stencil) != 0)
         {
            return reshade::api::format::d32_float_s8_uint;
         }
         else
         {
            // If we got here, the game would have originally been using a depth buffer with a stencil, so preserve the right stencil/depth view
            return (original_view_desc.format == reshade::api::format::x24_unorm_g8_uint) ? reshade::api::format::x32_float_g8_uint : reshade::api::format::r32_float_x8_uint;
         }
      }
      // All other
      else
      {
         if (IsTypelessFormat(DXGI_FORMAT(original_desc.texture.format)))
         {
            ASSERT_ONCE(!IsTypelessFormat(DXGI_FORMAT(original_view_desc.format)));
            ASSERT_ONCE(GetTypelessFormat(DXGI_FORMAT(original_view_desc.format)) == DXGI_FORMAT(original_desc.texture.format));
            // TODO: return the format we upgraded the resource to, if it's not typeless, otherwise restore the most common one? FLOAT?
         }
         else
         {
            ASSERT_ONCE(GetTypelessFormat(DXGI_FORMAT(original_view_desc.format)) == GetTypelessFormat(DXGI_FORMAT(original_desc.texture.format)));
            return upgraded_desc.texture.format;
         }
      }

      return original_view_desc.format; // Unchanged
   }

   // Decides whether a resource should be upgraded at creation time. Uses the frame state for
   // resolution/aspect-ratio filters.
   std::optional<reshade::api::format> ShouldUpgradeResource(const reshade::api::resource_desc& desc, const ResourceUpgradeFrameState& state, bool has_initial_data = false) const;

   // ------------------------------------------------------------------
   // Mirror creation / lookup (migrated from core.hpp)
   // ------------------------------------------------------------------

   // Creates (or reuses) an indirectly-upgraded mirror for "in_resource". "in_source_resource" is the
   // resource the upgrade is being propagated from (0 for a seed). Returns true if a mirror was found or
   // created and "out_resource" was redirected. Lock-free: the caller (core.hpp) must hold the single
   // device mutex (passed as "lock_device_read") before calling.
   // "should_scale" is computed by core.hpp (seed vs chain policy) and passed in; the manager only
   // applies it with the render<output + aspect checks.
   bool FindOrCreateIndirectUpgradedResource(
      reshade::api::device* device,
      const uint64_t in_source_resource,
      const uint64_t in_resource,
      uint64_t& out_resource,
      bool allow_create,
      reshade::api::resource_usage initial_state,
      std::shared_lock<std::shared_mutex>& lock_device_read,
      const ResourceUpgradeFrameState& state,
      bool should_scale = false,
      bool leave_locked = true);

   bool FindOrCreateIndirectUpgradedResourceView(
      reshade::api::device* device,
      const uint64_t in_rv,
      uint64_t& out_rv,
      bool allow_create,
      reshade::api::resource_usage usage,
      std::shared_lock<std::shared_mutex>& lock_device_read);

   // ------------------------------------------------------------------
   // Synchronization (for runtime config modifications by game code)
   // ------------------------------------------------------------------

   // Only needed by "texture_format_upgrades_2d_custom_aspect_ratios" at the moment (if changed after initialization)
   mutable std::shared_mutex mutex;

   // ------------------------------------------------------------------
   // Lifecycle
   // ------------------------------------------------------------------

   // Unlinks + defers destruction of all indirect mirrors. Mirrors are queued into the pending lists and
   // freed at the next present boundary. Callers must not hold "mutex".
   void InvalidateAllIndirectUpgradedResources();

   // Flushes pending mirror/view destructions (call at present boundary).
   void FlushPendingDestructions(reshade::api::device* device);

   // Handles a resource being destroyed: unlink its mirror + mirror views, defer free to present.
   void OnResourceDestroyed(uint64_t resource_handle);

   // Handles a resource view being destroyed: unlink the mirror view.
   void OnResourceViewDestroyed(uint64_t view_handle);

private:
   void UnlinkMirror(uint64_t mirror_handle);
};

#include "resource_upgrades.inl"
