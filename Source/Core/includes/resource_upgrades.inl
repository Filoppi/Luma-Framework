#pragma once

// Implementation of ResourceUpgradeManager (see resource_upgrades.hpp).

std::optional<reshade::api::format> ResourceUpgradeManager::ShouldUpgradeResource(const reshade::api::resource_desc& desc, const ResourceUpgradeFrameState& state, bool has_initial_data) const
{
   if (texture_format_upgrades_type < TextureFormatUpgradesType::AllowedEnabled)
   {
      return std::nullopt;
   }

   const bool is_rt_or_ua = (desc.usage & (reshade::api::resource_usage::render_target | reshade::api::resource_usage::unordered_access)) != 0;
   // Convoluted check to test if the resource is "D3D11_USAGE_DEFAULT" (the only usage type that can be both used as SRV, and be the target of a CopyResource(), otherwise we'd never need to upgrade them).
   // We also check the initial data for extra safety, in case this was a "static" content texture that accidentally wasn't created as immutable.
   // This is needed by "Thumper", and possibly "Watch Dogs 2".
   const bool is_writable_sr = (desc.usage & reshade::api::resource_usage::shader_resource) != 0 && desc.heap == reshade::api::memory_heap::gpu_only && !has_initial_data && (desc.flags & reshade::api::resource_flags::immutable) == 0;

   const bool is_depth = (desc.usage & reshade::api::resource_usage::depth_stencil) != 0;

   if ((!(is_rt_or_ua || ((enable_chain_indirect_texture_format_upgrades >= ChainTextureFormatUpgradesType::DirectDependencies) ? is_writable_sr : false))
            || !texture_upgrade_formats.contains(desc.texture.format))
         && (!is_depth || !texture_depth_upgrade_formats.contains(desc.texture.format)))
   {
      return std::nullopt;
   }

   if (desc.heap != reshade::api::memory_heap::gpu_only)
   {
      // At least in DX11, any resource that isn't exclusively accessible by the GPU, can't be set as output (render target/unordered access).
      // These probably wouldn't have the RT/UA usage flags set anyway, or they'd fail on creation if they did.
      ASSERT_ONCE(desc.heap != reshade::api::memory_heap::unknown && desc.heap != reshade::api::memory_heap::custom); // Unexpected heap types
      return std::nullopt;
   }

   const bool is_cube = (desc.flags & reshade::api::resource_flags::cube_compatible) != 0 && (desc.texture.depth_or_layers % 6) == 0 && desc.texture.depth_or_layers != 0;

   // Note: we can't fully exclude texture 2D arrays here, because they might still have 1 layer
   bool type_and_size_filter = desc.type == reshade::api::resource_type::texture_2d && (desc.texture.depth_or_layers == 1 || is_cube);

   if (texture_format_upgrades_2d_size_filters != (uint32_t)TextureFormatUpgrades2DSizeFilters::All)
   {
      bool size_filter = false;

      if ((texture_format_upgrades_2d_size_filters & (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainResolution) != 0)
      {
         size_filter |= desc.texture.width == state.output_resolution.x && desc.texture.height == state.output_resolution.y;
      }
      if ((texture_format_upgrades_2d_size_filters & (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainResolutionWidth) != 0)
      {
         size_filter |= desc.texture.width == state.output_resolution.x;
      }
      if ((texture_format_upgrades_2d_size_filters & (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainResolutionHeight) != 0)
      {
         size_filter |= desc.texture.height == state.output_resolution.y;
      }
      if ((texture_format_upgrades_2d_size_filters & (uint32_t)TextureFormatUpgrades2DSizeFilters::RenderResolution) != 0)
      {
         size_filter |= desc.texture.width == state.render_resolution.x && desc.texture.height == state.render_resolution.y;
      }
      // Flipped condition, given we already allowed them above in "type_and_size_filter"
      if ((texture_format_upgrades_2d_size_filters & (uint32_t)TextureFormatUpgrades2DSizeFilters::Cubes) == 0)
      {
         size_filter &= !is_cube;
      }
      else
      {
         size_filter |= is_cube;
      }

      // Always scale from the smallest dimension, as that gives up more threshold, depending on how the devs scaled down textures (they can use multiple rounding models)
      float min_aspect_ratio = desc.texture.width <= desc.texture.height ? ((float)(desc.texture.width - texture_format_upgrades_2d_aspect_ratio_pixel_threshold) / (float)desc.texture.height) : ((float)desc.texture.width / (float)(desc.texture.height + texture_format_upgrades_2d_aspect_ratio_pixel_threshold));
      float max_aspect_ratio = desc.texture.width <= desc.texture.height ? ((float)(desc.texture.width + texture_format_upgrades_2d_aspect_ratio_pixel_threshold) / (float)desc.texture.height) : ((float)desc.texture.width / (float)(desc.texture.height - texture_format_upgrades_2d_aspect_ratio_pixel_threshold));
      bool generating_manual_mips = false;
#if DEVELOPMENT
      if ((texture_format_upgrades_2d_size_filters & (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainAspectRatio) != 0
         || (texture_format_upgrades_2d_size_filters & (uint32_t)TextureFormatUpgrades2DSizeFilters::RenderAspectRatio) != 0
         || (texture_format_upgrades_2d_size_filters & (uint32_t)TextureFormatUpgrades2DSizeFilters::CustomAspectRatio) != 0)
      {
         static thread_local UINT last_texture_width = desc.texture.width;
         static thread_local UINT last_texture_height = desc.texture.height;
         // If this was a chain of downscaling, don't send a warning! This is just a heuristics based check... The creation order might have been random, or inverted (from smaller to bigger mips).
         // Note that this isn't thread safe but whatever
         if (max(desc.texture.width, desc.texture.height) == 1)
         {
            generating_manual_mips = (last_texture_width / 2) == desc.texture.width && (last_texture_height / 2) == desc.texture.height;
         }
         last_texture_width = desc.texture.width;
         last_texture_height = desc.texture.height;
      }
#endif
      if ((texture_format_upgrades_2d_size_filters & (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainAspectRatio) != 0)
      {
         float target_aspect_ratio = (float)state.output_resolution.x / (float)state.output_resolution.y;
         bool aspect_ratio_filter = target_aspect_ratio >= (min_aspect_ratio - FLT_EPSILON) && target_aspect_ratio <= (max_aspect_ratio + FLT_EPSILON);
         size_filter |= aspect_ratio_filter;
#if DEVELOPMENT
         ASSERT_ONCE_MSG(!aspect_ratio_filter || max(desc.texture.width, desc.texture.height) > 1 || generating_manual_mips || ((texture_format_upgrades_2d_size_filters & (uint32_t)TextureFormatUpgrades2DSizeFilters::No1Px) != 0), "Upgrading 1x1 resource by aspect ratio, this is possibly unwanted"); // TODO: add a min size for upgrades? Like >1 or >32 on the smallest axis? Or ... scan if the allocations shrink in size over time
#endif
      }
      if ((texture_format_upgrades_2d_size_filters & (uint32_t)TextureFormatUpgrades2DSizeFilters::RenderAspectRatio) != 0)
      {
         float target_aspect_ratio = (float)state.render_resolution.x / (float)state.render_resolution.y;
         bool aspect_ratio_filter = target_aspect_ratio >= (min_aspect_ratio - FLT_EPSILON) && target_aspect_ratio <= (max_aspect_ratio + FLT_EPSILON);
         size_filter |= aspect_ratio_filter;
#if DEVELOPMENT
         ASSERT_ONCE_MSG(!aspect_ratio_filter || max(desc.texture.width, desc.texture.height) > 1 || generating_manual_mips || ((texture_format_upgrades_2d_size_filters & (uint32_t)TextureFormatUpgrades2DSizeFilters::No1Px) != 0), "Upgrading 1x1 resource by aspect ratio, this is possibly unwanted");
#endif
      }
      if ((texture_format_upgrades_2d_size_filters & (uint32_t)TextureFormatUpgrades2DSizeFilters::CustomAspectRatio) != 0)
      {
         // Lock-free: caller/core.hpp holds the single device mutex.
         for (auto texture_format_upgrades_2d_custom_aspect_ratio : texture_format_upgrades_2d_custom_aspect_ratios)
         {
            float target_aspect_ratio = texture_format_upgrades_2d_custom_aspect_ratio;
            bool aspect_ratio_filter = target_aspect_ratio >= (min_aspect_ratio - FLT_EPSILON) && target_aspect_ratio <= (max_aspect_ratio + FLT_EPSILON);
            size_filter |= aspect_ratio_filter;
#if DEVELOPMENT
            ASSERT_ONCE_MSG(!aspect_ratio_filter || max(desc.texture.width, desc.texture.height) > 1 || generating_manual_mips || ((texture_format_upgrades_2d_size_filters & (uint32_t)TextureFormatUpgrades2DSizeFilters::No1Px) != 0), "Upgrading 1x1 resource by aspect ratio, this is possibly unwanted");
#else
            if (size_filter) break;
#endif
         }
      }

      if ((texture_format_upgrades_2d_size_filters & (uint32_t)TextureFormatUpgrades2DSizeFilters::CustomSize) != 0)
      {
         for (auto texture_format_upgrades_2d_custom_size : texture_format_upgrades_2d_custom_sizes)
         {
            size_filter |= desc.texture.width == texture_format_upgrades_2d_custom_size.x && desc.texture.height == texture_format_upgrades_2d_custom_size.y;
            // We passed a very specific test, no need to do any more exclusion tests below
            if (size_filter) break;
         }
      }

      if ((texture_format_upgrades_2d_size_filters & (uint32_t)TextureFormatUpgrades2DSizeFilters::No1Px) != 0)
      {
         size_filter &= desc.texture.width != 1 || desc.texture.height != 1;
      }

      if ((texture_format_upgrades_2d_size_filters & (uint32_t)TextureFormatUpgrades2DSizeFilters::Mips) != 0)
      {
         float2 max_resolution = state.output_resolution.y >= state.render_resolution.y ? state.output_resolution : state.render_resolution;
         size_filter |= ResourceUpgradeIsMipOf(max_resolution.x, max_resolution.y, desc.texture.width, desc.texture.height);
      }

      type_and_size_filter &= size_filter;
   }

   if (is_depth)
   {
      if (type_and_size_filter)
      {
         return GetBestResourceUpgradeFormat(desc);
      }
      return std::nullopt;
   }

   switch (texture_format_upgrades_lut_dimensions)
   {
   case LUTDimensions::_1D:
   {
      // For 1D, "texture_format_upgrades_lut_size" is the whole width (usually they extend in width)
      type_and_size_filter |= desc.type == reshade::api::resource_type::texture_1d && desc.texture.width == texture_format_upgrades_lut_size && desc.texture.height == 1 && desc.texture.depth_or_layers == 1 && desc.texture.levels == 1;
      break;
   }
   default:
   case LUTDimensions::_2D:
   {
      // For 2D, "texture_format_upgrades_lut_size" is the height, usually they extend in width and that's squared
      type_and_size_filter |= desc.type == reshade::api::resource_type::texture_2d && desc.texture.width == (texture_format_upgrades_lut_size * texture_format_upgrades_lut_size) && desc.texture.height == texture_format_upgrades_lut_size && desc.texture.depth_or_layers == 1 && desc.texture.levels == 1;
      break;
   }
   case LUTDimensions::_3D:
   {
      // For 3D, all the dimensions usually match
      type_and_size_filter |= desc.type == reshade::api::resource_type::texture_3d && desc.texture.width == texture_format_upgrades_lut_size && desc.texture.height == texture_format_upgrades_lut_size && desc.texture.depth_or_layers == texture_format_upgrades_lut_size && desc.texture.levels == 1;
      break;
   }
   }

   if (type_and_size_filter)
   {
      return GetBestResourceUpgradeFormat(desc);
   }
   return std::nullopt;
}

bool ResourceUpgradeManager::FindOrCreateIndirectUpgradedResource(
   reshade::api::device* device,
   const uint64_t in_source_resource,
   const uint64_t in_resource,
   uint64_t& out_resource,
   bool allow_create,
   reshade::api::resource_usage initial_state,
   std::shared_lock<std::shared_mutex>& lock_device_read,
   const ResourceUpgradeFrameState& state,
   bool should_scale,
   bool leave_locked)
{
   bool replaced = false;

   auto original_resource_to_mirrored_upgraded_resource = original_resources_to_mirrored_upgraded_resources.find(in_resource);
   if (original_resource_to_mirrored_upgraded_resource != original_resources_to_mirrored_upgraded_resources.end())
   {
      out_resource = original_resource_to_mirrored_upgraded_resource->second.mirror_handle;
      replaced = true;
   }
   // Ignore resources that were already directly upgraded (their format is already changed in place, so
   // no mirror is needed). Swapchain backbuffers are excluded by the caller (core.hpp checks back_buffers
   // before invoking us) and never reach here with allow_create=true.
   else if (allow_create && in_resource != 0 && !upgraded_resources.contains(in_resource))
   {
      lock_device_read.unlock(); // Avoids deadlocks with the device

      reshade::api::resource mirrored_upgraded_resource;
      reshade::api::resource_desc source_desc = device->get_resource_desc({ in_resource });
      reshade::api::resource_desc target_desc = source_desc;
      bool needs_upgraded_resource;
      if (in_source_resource)
      {
         source_desc = device->get_resource_desc({ in_source_resource });

         float min_aspect_ratio = target_desc.texture.width <= target_desc.texture.height ? ((float)(target_desc.texture.width - texture_format_upgrades_2d_aspect_ratio_pixel_threshold) / (float)target_desc.texture.height) : ((float)target_desc.texture.width / (float)(target_desc.texture.height + texture_format_upgrades_2d_aspect_ratio_pixel_threshold));
         float max_aspect_ratio = target_desc.texture.width <= target_desc.texture.height ? ((float)(target_desc.texture.width + texture_format_upgrades_2d_aspect_ratio_pixel_threshold) / (float)target_desc.texture.height) : ((float)target_desc.texture.width / (float)(target_desc.texture.height - texture_format_upgrades_2d_aspect_ratio_pixel_threshold));
         float target_aspect_ratio = (float)source_desc.texture.width / (float)source_desc.texture.height;
         bool is_2x_square = target_desc.texture.width == 2 && target_desc.texture.height == 2;
         bool is_1x_square = target_desc.texture.width == 1 && target_desc.texture.height == 1;
         bool aspect_ratio_filter = source_desc.type == reshade::api::resource_type::texture_2d && !is_2x_square && !is_1x_square && target_aspect_ratio >= (min_aspect_ratio - FLT_EPSILON) && target_aspect_ratio <= (max_aspect_ratio + FLT_EPSILON); // Note: we don't check the aspect ratio on the depth, we only do it on 2D textures. We also ignore 1x1 and 2x2 for extra safety
         bool size_filter = source_desc.texture.width == target_desc.texture.width && source_desc.texture.height == target_desc.texture.height && source_desc.texture.depth_or_layers == target_desc.texture.depth_or_layers;
         size_filter |= aspect_ratio_filter;

         // Avoid upgrading textures that don't have the same number of channels (unless they'd now have more!), we wouldn't want to automatically turn 1 channel to 4 channel textures.
         // Also prevent upgrades if the size isn't compatible (aspect ratio matching).
         // And don't upgrade int formats for now, they could only cause troubles.
         // See "enable_chain_indirect_texture_format_upgrades" for more.
         needs_upgraded_resource = !AreFormatsCopyCompatible(DXGI_FORMAT(source_desc.texture.format), DXGI_FORMAT(target_desc.texture.format))
            && IsRGBAFormat(DXGI_FORMAT(source_desc.texture.format), true) == IsRGBAFormat(DXGI_FORMAT(target_desc.texture.format), true)
            && !IsIntFormat(DXGI_FORMAT(target_desc.texture.format))
            && size_filter;

         size_filter |= aspect_ratio_filter;
         // TODO: instead of checking the formats for compatibility, also check if the source was upgraded and in that case force the target to be upgraded (faster checks)
         if (needs_upgraded_resource)
         {
            target_desc.texture.format = source_desc.texture.format;
         }
      }
      else // Upgrade format
      {
         target_desc.texture.format = GetBestResourceUpgradeFormat(source_desc);
         needs_upgraded_resource = source_desc.texture.format != target_desc.texture.format;
      }
      needs_upgraded_resource &= target_desc.type == reshade::api::resource_type::texture_2d; // Filter out false positives (UAVs can be buffers)
      // TODO: optionally copy the content of "in_resource"?

      // Resolution scaling (upscale only): scale the mirror from render_resolution to output_resolution.
      // "should_scale" was computed by core.hpp (seed: scale set && sr_active; chain: SR has drawn).
      bool needs_scale = false;
      if (target_desc.type == reshade::api::resource_type::texture_2d
         && should_scale
         && state.render_resolution.x > 0 && state.render_resolution.y > 0
         && state.render_resolution.x < state.output_resolution.x
         && state.render_resolution.y < state.output_resolution.y)
      {
         const bool is_1x1 = target_desc.texture.width == 1 && target_desc.texture.height == 1;
         const bool is_2x2 = target_desc.texture.width == 2 && target_desc.texture.height == 2;
         const float min_aspect = target_desc.texture.width <= target_desc.texture.height
            ? ((float)(target_desc.texture.width - texture_format_upgrades_2d_aspect_ratio_pixel_threshold) / (float)target_desc.texture.height)
            : ((float)target_desc.texture.width / (float)(target_desc.texture.height + texture_format_upgrades_2d_aspect_ratio_pixel_threshold));
         const float max_aspect = target_desc.texture.width <= target_desc.texture.height
            ? ((float)(target_desc.texture.width + texture_format_upgrades_2d_aspect_ratio_pixel_threshold) / (float)target_desc.texture.height)
            : ((float)target_desc.texture.width / (float)(target_desc.texture.height - texture_format_upgrades_2d_aspect_ratio_pixel_threshold));
         const float render_aspect = state.render_resolution.x / state.render_resolution.y;
         const bool matches_render = !is_1x1 && !is_2x2 && render_aspect >= (min_aspect - FLT_EPSILON) && render_aspect <= (max_aspect + FLT_EPSILON);
         if (matches_render)
            needs_scale = true;
      }
      // Keep the original (render) size for the mirror bookkeeping, before the scale override below.
      const uint32_t original_width = target_desc.texture.width;
      const uint32_t original_height = target_desc.texture.height;
      if (needs_scale)
      {
         // Scale to full output resolution (matches FFXV's native upscale), so mirrors are the same
         // size as the swapchain and downstream copies to it stay size-matched.
         target_desc.texture.width  = (uint32_t)state.output_resolution.x;
         target_desc.texture.height = (uint32_t)state.output_resolution.y;
      }

      // Create a mirror when the resource needs a format upgrade AND/OR needs scaling.
      // Lock-free: caller/core.hpp holds the single device mutex.
      const bool should_create_mirror = needs_upgraded_resource || needs_scale;
      if (should_create_mirror && device->create_resource(target_desc, nullptr, initial_state, &mirrored_upgraded_resource))
      {
         if (!original_resources_to_mirrored_upgraded_resources.contains(in_resource))
         {
            original_resources_to_mirrored_upgraded_resources[in_resource] = { mirrored_upgraded_resource.handle, needs_scale, original_width, original_height, target_desc.texture.width, target_desc.texture.height };
            out_resource = mirrored_upgraded_resource.handle;
         }
         else // Destroy it if it was accidentally created at the same time by another thread
         {
            out_resource = original_resources_to_mirrored_upgraded_resources[in_resource].mirror_handle;
            device->destroy_resource(mirrored_upgraded_resource);
         }

         replaced = true;
      }
      else if (should_create_mirror)
      {
         ASSERT_ONCE_MSG(false, "Failed to create an indirect upgraded texture");
      }

      if (leave_locked)
         lock_device_read.lock();
   }

   // Let the upgrades happen above, but ignore the override
   if (ignore_indirect_upgraded_textures)
   {
      if (out_resource)
         out_resource = in_resource;
      return false;
   }

   return replaced;
}

bool ResourceUpgradeManager::FindOrCreateIndirectUpgradedResourceView(
   reshade::api::device* device,
   const uint64_t in_rv,
   uint64_t& out_rv,
   bool allow_create,
   reshade::api::resource_usage usage,
   std::shared_lock<std::shared_mutex>& lock_device_read)
{
   bool replaced = false;

   // See if we already have a indirect resource view mapped to this resource view
   auto original_resource_view_to_mirrored_upgraded_resource_view = original_resource_views_to_mirrored_upgraded_resource_views.find(in_rv);
   if (original_resource_view_to_mirrored_upgraded_resource_view != original_resource_views_to_mirrored_upgraded_resource_views.end())
   {
      replaced = true;
      out_rv = original_resource_view_to_mirrored_upgraded_resource_view->second;
   }
   // Otherwise, create it.
   // For example, sometimes we upgrade resources after creation and we can't know all the views that were previously created for the original resource (well, we could cache them on creation based on the list of formats we ever upgrade, if ever...),
   // so we need to create a mirrored upgraded view for every view it had.
   // TODO: just cache all the views for any resource we might ever upgrade later (e.g. through "auto_texture_format_upgrade_shader_hashes"), as mentioned above, so we could skip many of these checks.
   else if (allow_create && in_rv != 0 && !original_resources_to_mirrored_upgraded_resources.empty())
   {
      reshade::api::resource resource;
      resource.handle = GetCachedResourceFromView(in_rv);
      if (resource.handle == 0) // Unknown view (e.g. created before the addon loaded): query the device outside the lock
      {
         lock_device_read.unlock(); // Avoids deadlocks with the device
         resource = device->get_resource_from_view({ in_rv });
         lock_device_read.lock();
      }
      // The view may have been mapped by another thread while we were unlocked above: never create a duplicate
      auto recheck_it = original_resource_views_to_mirrored_upgraded_resource_views.find(in_rv);
      if (recheck_it != original_resource_views_to_mirrored_upgraded_resource_views.end())
      {
         replaced = true;
         out_rv = recheck_it->second;
      }
      else
      {
         auto original_resource_to_mirrored_upgraded_resource = original_resources_to_mirrored_upgraded_resources.find(resource.handle);
         if (original_resource_to_mirrored_upgraded_resource != original_resources_to_mirrored_upgraded_resources.end())
         {
            const auto original_resource_to_mirrored_upgraded_resource_ptr = original_resource_to_mirrored_upgraded_resource->second.mirror_handle;

            lock_device_read.unlock();

            reshade::api::resource_view_desc resource_view_desc = device->get_resource_view_desc({ in_rv });
            resource_view_desc.format = reshade::api::format::unknown; // Null the format so it's determined automatically. All the formats returned by "GetBestResourceUpgradeFormat()" are not typeless, so we can make views of (almost) all of them directly.

            reshade::api::resource_view mirrored_upgraded_resource_view;
            if (device->create_resource_view({ original_resource_to_mirrored_upgraded_resource_ptr }, usage, resource_view_desc, &mirrored_upgraded_resource_view))
            {
               // Lock-free: caller/core.hpp holds the single device mutex.
               if (!original_resource_views_to_mirrored_upgraded_resource_views.contains(in_rv))
               {
                  original_resource_views_to_mirrored_upgraded_resource_views[in_rv] = mirrored_upgraded_resource_view.handle;
                  mirror_views_by_mirror_resource[original_resource_to_mirrored_upgraded_resource_ptr].emplace(mirrored_upgraded_resource_view.handle);
                  mirror_views_to_mirror_resources[mirrored_upgraded_resource_view.handle] = original_resource_to_mirrored_upgraded_resource_ptr;
                  out_rv = mirrored_upgraded_resource_view.handle;
               }
               else // Destroy it if it was accidentally created at the same time by another thread
               {
                  out_rv = original_resource_views_to_mirrored_upgraded_resource_views[in_rv];
                  device->destroy_resource_view(mirrored_upgraded_resource_view);
               }
               replaced = true;
            }
            else
            {
               ASSERT_ONCE_MSG(false, "Failed to create an indirect upgraded texture view (maybe some format mismatch)");
            }

            lock_device_read.lock();
         }
      }
   }

   // Let the upgrades happen above, but ignore the override
   if (ignore_indirect_upgraded_textures)
   {
      if (out_rv)
         out_rv = in_rv;
      return false;
   }

   return replaced;
}

void ResourceUpgradeManager::UnlinkMirror(uint64_t mirror_handle)
{
   // Find and erase the original->mirror entry for this mirror.
   for (auto it = original_resources_to_mirrored_upgraded_resources.begin(); it != original_resources_to_mirrored_upgraded_resources.end(); ++it)
   {
      if (it->second.mirror_handle != mirror_handle)
         continue;

      // Invalidate stale view mappings for this mirror while the lock is held.
      std::vector<uint64_t> unlinked_mirror_views;
      if (auto mirror_views_it = mirror_views_by_mirror_resource.find(mirror_handle); mirror_views_it != mirror_views_by_mirror_resource.end())
      {
         const auto& mirror_views = mirror_views_it->second;
         for (auto view_map_it = original_resource_views_to_mirrored_upgraded_resource_views.begin(); view_map_it != original_resource_views_to_mirrored_upgraded_resource_views.end();)
         {
            if (mirror_views.contains(view_map_it->second))
            {
               unlinked_mirror_views.push_back(view_map_it->second);
               mirror_views_to_mirror_resources.erase(view_map_it->second);
               view_map_it = original_resource_views_to_mirrored_upgraded_resource_views.erase(view_map_it);
            }
            else
            {
               ++view_map_it;
            }
         }
         mirror_views_by_mirror_resource.erase(mirror_views_it);
      }

      original_resources_to_mirrored_upgraded_resources.erase(it);

      // Defer freeing to present: the mirror may still be in flight in hooks or bound on recorded lists.
      for (const uint64_t unlinked_mirror_view : unlinked_mirror_views)
      {
         pending_mirror_view_destructions.push_back({ unlinked_mirror_view });
      }
      pending_mirror_resource_destructions.push_back({ mirror_handle });
      break;
   }
}

void ResourceUpgradeManager::InvalidateAllIndirectUpgradedResources()
{
   // Lock-free: caller/core.hpp holds the single device mutex.

   std::vector<uint64_t> invalidated_mirrors;
   invalidated_mirrors.reserve(original_resources_to_mirrored_upgraded_resources.size());
   for (const auto& [orig, mirror] : original_resources_to_mirrored_upgraded_resources)
   {
      invalidated_mirrors.push_back(mirror.mirror_handle);
   }
   for (const uint64_t mirror_handle : invalidated_mirrors)
   {
      UnlinkMirror(mirror_handle);
   }
}

void ResourceUpgradeManager::FlushPendingDestructions(reshade::api::device* device)
{
   // Lock-free: caller/core.hpp holds the single device mutex.
   std::vector<reshade::api::resource_view> pending_views = std::move(pending_mirror_view_destructions);
   std::vector<reshade::api::resource> pending_resources = std::move(pending_mirror_resource_destructions);
   for (const reshade::api::resource_view view : pending_views)
   {
      device->destroy_resource_view(view);
   }
   for (const reshade::api::resource resource : pending_resources)
   {
      device->destroy_resource(resource);
   }
}

void ResourceUpgradeManager::OnResourceDestroyed(uint64_t resource_handle)
{
   // Lock-free: caller/core.hpp holds the single device mutex.
   auto original_resource_to_mirrored_upgraded_resource = original_resources_to_mirrored_upgraded_resources.find(resource_handle);
   if (original_resource_to_mirrored_upgraded_resource != original_resources_to_mirrored_upgraded_resources.end())
   {
      const auto mirror_handle = original_resource_to_mirrored_upgraded_resource->second.mirror_handle;
      original_resources_to_mirrored_upgraded_resources.erase(original_resource_to_mirrored_upgraded_resource);

      // Invalidate stale view mappings for this mirror while the lock is held.
      std::vector<uint64_t> unlinked_mirror_views;
      if (auto mirror_views_it = mirror_views_by_mirror_resource.find(mirror_handle); mirror_views_it != mirror_views_by_mirror_resource.end())
      {
         const auto& mirror_views = mirror_views_it->second;
         for (auto view_map_it = original_resource_views_to_mirrored_upgraded_resource_views.begin(); view_map_it != original_resource_views_to_mirrored_upgraded_resource_views.end();)
         {
            if (mirror_views.contains(view_map_it->second))
            {
               unlinked_mirror_views.push_back(view_map_it->second);
               mirror_views_to_mirror_resources.erase(view_map_it->second);
               view_map_it = original_resource_views_to_mirrored_upgraded_resource_views.erase(view_map_it);
            }
            else
            {
               ++view_map_it;
            }
         }
         mirror_views_by_mirror_resource.erase(mirror_views_it);
      }

      // Defer freeing to present: the mirror may still be in flight in hooks or bound on recorded lists.
      for (const uint64_t unlinked_mirror_view : unlinked_mirror_views)
      {
         pending_mirror_view_destructions.push_back({ unlinked_mirror_view });
      }
      pending_mirror_resource_destructions.push_back({ mirror_handle });
   }
   upgraded_resources.erase(resource_handle);
#if DEVELOPMENT
   original_upgraded_resources_formats.erase(resource_handle);
#endif
}

void ResourceUpgradeManager::OnResourceViewDestroyed(uint64_t view_handle)
{
   // Lock-free: caller/core.hpp holds the single device mutex.

#if DEVELOPMENT
   original_upgraded_resource_views_formats.erase(view_handle);
#endif
   original_views_to_resources.erase(view_handle);

   auto original_resource_view_to_mirrored_upgraded_resource_view = original_resource_views_to_mirrored_upgraded_resource_views.find(view_handle);
   if (original_resource_view_to_mirrored_upgraded_resource_view != original_resource_views_to_mirrored_upgraded_resource_views.end())
   {
      const auto mirrored_upgraded_resource_view = original_resource_view_to_mirrored_upgraded_resource_view->second;
      original_resource_views_to_mirrored_upgraded_resource_views.erase(original_resource_view_to_mirrored_upgraded_resource_view);
      reshade::api::resource mirror_resource;
      mirror_resource.handle = 0;
      if (auto mirror_res_it = mirror_views_to_mirror_resources.find(mirrored_upgraded_resource_view); mirror_res_it != mirror_views_to_mirror_resources.end())
      {
         mirror_resource.handle = mirror_res_it->second;
         mirror_views_to_mirror_resources.erase(mirror_res_it);
      }
      if (auto mirror_views_it = mirror_views_by_mirror_resource.find(mirror_resource.handle); mirror_views_it != mirror_views_by_mirror_resource.end())
      {
         mirror_views_it->second.erase(mirrored_upgraded_resource_view);
         if (mirror_views_it->second.empty())
            mirror_views_by_mirror_resource.erase(mirror_views_it);
      }
      // Defer freeing to present: the mirror view may still be in flight.
      pending_mirror_view_destructions.push_back({ mirrored_upgraded_resource_view });
   }
}
