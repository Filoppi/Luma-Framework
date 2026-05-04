#pragma once
#include <d3d11.h>
#include <deque>
#include <mutex>
#include <unordered_map>
#include "log.hpp"

// Per-resource upscaled replacement: output-resolution texture + views
struct UpscaledResource
{
   ComPtr<ID3D11Texture2D> texture;
   ComPtr<ID3D11ShaderResourceView> srv;
   ComPtr<ID3D11RenderTargetView> rtv;
   ComPtr<ID3D11UnorderedAccessView> uav;
   D3D11_TEXTURE2D_DESC original_desc{}; // cached for compatibility checks
   bool in_use_this_frame = false;
};

// Persistent output-resolution allocations + per-frame source links.
// Pool entries persist across frames; source links are cleared every frame.
// Keys are ID3D11Resource* pointers for stable identity across interface
// boundaries (SRV::GetResource, RTV::GetResource, ID3D11Texture2D all converge).
struct UpscaleTrackingState
{
   std::deque<UpscaledResource> pool;
   std::unordered_map<uintptr_t, uint32_t> frame_links; // source resource ptr -> pool index (frame-local)
   bool post_taa_upscale_active = false;                // gate: true between TAA and tonemap
   UINT pool_output_width = 0;
   UINT pool_output_height = 0;
   mutable std::recursive_mutex mutex;

   void ResetFrame()
   {
      std::lock_guard<std::recursive_mutex> lock(mutex);
      frame_links.clear();
      for (auto& entry : pool)
         entry.in_use_this_frame = false;
      post_taa_upscale_active = false;
   }

   void ClearPool()
   {
      std::lock_guard<std::recursive_mutex> lock(mutex);
      pool.clear();
      frame_links.clear();
      post_taa_upscale_active = false;
      pool_output_width = 0;
      pool_output_height = 0;
   }

   void InvalidatePoolIfOutputResolutionChanged(const float2& output_resolution)
   {
      std::lock_guard<std::recursive_mutex> lock(mutex);
      const UINT output_w = static_cast<UINT>(output_resolution.x);
      const UINT output_h = static_cast<UINT>(output_resolution.y);

      if (pool_output_width != 0 && pool_output_height != 0 &&
          (pool_output_width != output_w || pool_output_height != output_h))
      {
         ClearPool();
      }
   }
};

static constexpr uint32_t kInvalidPoolIndex = static_cast<uint32_t>(-1);

static uint32_t FindPoolIndexByPtr(const UpscaleTrackingState& tracking, const UpscaledResource* ptr)
{
   if (!ptr)
      return kInvalidPoolIndex;

   for (size_t i = 0; i < tracking.pool.size(); ++i)
   {
      if (&tracking.pool[i] == ptr)
         return static_cast<uint32_t>(i);
   }

   return kInvalidPoolIndex;
}

static void EnsureOutputPoolResolution(
   UpscaleTrackingState& tracking,
   const float2& output_resolution)
{
   std::lock_guard<std::recursive_mutex> lock(tracking.mutex);
   tracking.InvalidatePoolIfOutputResolutionChanged(output_resolution);
   tracking.pool_output_width = static_cast<UINT>(output_resolution.x);
   tracking.pool_output_height = static_cast<UINT>(output_resolution.y);
}

static UpscaledResource* AcquireUpscaledFromPool(
   ID3D11Device* device,
   ID3D11DeviceContext* device_context,
   UpscaleTrackingState& tracking,
   const D3D11_TEXTURE2D_DESC& original_desc,
   const float2& output_resolution,
   bool require_uav = false)
{
   std::lock_guard<std::recursive_mutex> lock(tracking.mutex);
   EnsureOutputPoolResolution(tracking, output_resolution);

   const UINT target_w = static_cast<UINT>(output_resolution.x);
   const UINT target_h = static_cast<UINT>(output_resolution.y);

   const UINT orig_w = original_desc.Width;
   const UINT orig_h = original_desc.Height;
   const UINT orig_fmt = static_cast<UINT>(original_desc.Format);

#if DEVELOPMENT || TEST
   char pool_diag[512];
   snprintf(pool_diag, sizeof(pool_diag),
      "[FFXV UpscaleChain] AcquireUpscaledFromPool orig=%ux%u fmt=%u target=%ux%u pool_size=%zu uav=%s",
      orig_w, orig_h, orig_fmt, target_w, target_h, tracking.pool.size(),
      require_uav ? "req" : "opt");
   Log_Debug(reshade::log::level::info, pool_diag);
#endif

   for (size_t pool_index = 0; pool_index < tracking.pool.size(); ++pool_index)
   {
      auto& entry = tracking.pool[pool_index];
      if (entry.in_use_this_frame || !entry.texture)
         continue;

      const UINT e_fmt = static_cast<UINT>(entry.original_desc.Format);
      const UINT e_w = entry.original_desc.Width;
      const UINT e_h = entry.original_desc.Height;
#if DEVELOPMENT || TEST
      snprintf(pool_diag, sizeof(pool_diag),
         "[FFXV UpscaleChain]   pool[%zu] check: orig=%ux%ufmt=%u in_use=%d -> %s",
         pool_index, e_w, e_h, e_fmt,
         entry.in_use_this_frame,
         ((e_fmt == orig_fmt && e_w == orig_w && e_h == orig_h) ? "MATCH" : "no-match"));
      Log_Debug(reshade::log::level::info, pool_diag);
#endif

      if ((!require_uav || entry.uav) &&
          entry.original_desc.Format == original_desc.Format &&
          entry.original_desc.Width == original_desc.Width &&
          entry.original_desc.Height == original_desc.Height)
      {
         D3D11_TEXTURE2D_DESC existing;
         entry.texture->GetDesc(&existing);
         if (existing.Width == target_w && existing.Height == target_h)
         {
#if DEVELOPMENT || TEST
            snprintf(pool_diag, sizeof(pool_diag),
               "[FFXV UpscaleChain]   pool[%zu] REUSED: tex=%ux%u",
               pool_index, existing.Width, existing.Height);
            Log_Debug(reshade::log::level::info, pool_diag);
#endif
            entry.in_use_this_frame = true;
            return &entry;
         }
      }
   }

   D3D11_TEXTURE2D_DESC desc = original_desc;
   desc.Width = target_w;
   desc.Height = target_h;

   // Pool textures are always single-frame targets: force 1 mip level so that
   // a mip chain is never accidentally inherited from the original descriptor.
   desc.MipLevels = 1;

   // Pooled textures must be DEFAULT usage so the GPU can use them as RTV/UAV.
   // DYNAMIC/STAGING usages are incompatible with those bind flags.
   desc.Usage = D3D11_USAGE_DEFAULT;

   // CPU access flags are meaningless (and illegal) for DEFAULT + RTV/UAV targets.
   desc.CPUAccessFlags = 0;

   // MSAA is incompatible with UAV binding and with SRV sampling in most post-
   // process shaders. Resolve to single-sample.
   desc.SampleDesc.Count = 1;
   desc.SampleDesc.Quality = 0;

   // Strip MiscFlags that conflict with the required bind flags:
   //  - GENERATE_MIPS requires a full mip chain (we forced 1 above)
   //  - SHARED / SHARED_KEYEDMUTEX / SHARED_NTHANDLE are irrelevant for pool textures
   // Keep TEXTURECUBE in case the original was a cube-map face (unusual but safe).
   desc.MiscFlags &= D3D11_RESOURCE_MISC_TEXTURECUBE;

   desc.BindFlags |= D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
   if (require_uav || (original_desc.BindFlags & D3D11_BIND_UNORDERED_ACCESS))
      desc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;

   UpscaledResource entry;
   entry.original_desc = original_desc;

#if DEVELOPMENT || TEST
   snprintf(pool_diag, sizeof(pool_diag),
      "[FFXV UpscaleChain] AcquireUpscaledFromPool CREATING new: desc=%ux%u fmt=%u target=%ux%u",
      desc.Width, desc.Height, static_cast<UINT>(desc.Format), target_w, target_h);
   Log_Debug(reshade::log::level::info, pool_diag);
#endif

   if (FAILED(device->CreateTexture2D(&desc, nullptr, entry.texture.put())))
      return nullptr;

   if (FAILED(device->CreateShaderResourceView(entry.texture.get(), nullptr, entry.srv.put())))
      return nullptr;

   if (FAILED(device->CreateRenderTargetView(entry.texture.get(), nullptr, entry.rtv.put())))
      return nullptr;

   if (desc.BindFlags & D3D11_BIND_UNORDERED_ACCESS)
   {
      if (FAILED(device->CreateUnorderedAccessView(entry.texture.get(), nullptr, entry.uav.put())))
         return nullptr;
   }

   entry.in_use_this_frame = true;

   tracking.pool.emplace_back(std::move(entry));
   return &tracking.pool.back();
}

static UpscaledResource* GetLinkedUpscaled(
   UpscaleTrackingState& tracking,
   uintptr_t source_key)
{
   std::lock_guard<std::recursive_mutex> lock(tracking.mutex);
   auto link_it = tracking.frame_links.find(source_key);
   if (link_it == tracking.frame_links.end())
      return nullptr;

   const uint32_t pool_index = link_it->second;
   if (pool_index >= tracking.pool.size())
   {
      tracking.frame_links.erase(link_it);
      return nullptr;
   }

   return &tracking.pool[pool_index];
}

static void UnlinkUpscaledResource(
   UpscaleTrackingState& tracking,
   uintptr_t source_key)
{
   std::lock_guard<std::recursive_mutex> lock(tracking.mutex);
   auto link_it = tracking.frame_links.find(source_key);
   if (link_it == tracking.frame_links.end())
      return;

   const uint32_t pool_index = link_it->second;
   if (pool_index < tracking.pool.size())
      tracking.pool[pool_index].in_use_this_frame = false;

   tracking.frame_links.erase(link_it);
}

// Link |original_resource| to an output-resolution pooled replacement for the current frame.
// The persistent pooled allocation is independent from original resource pointer identity.
static UpscaledResource* LinkUpscaledResource(
   ID3D11Device* device,
   ID3D11DeviceContext* device_context,
   ID3D11Resource* original_resource,
   UpscaleTrackingState& tracking,
   const float2& output_resolution,
   bool require_uav = false)
{
   std::lock_guard<std::recursive_mutex> lock(tracking.mutex);
   if (!original_resource)
   {
      Log_Debug(
         reshade::log::level::warning,
         "Cannot link upscaled resource for null original resource pointer");
      return nullptr;
   }

   const uintptr_t source_key = reinterpret_cast<uintptr_t>(original_resource);

#if DEVELOPMENT || TEST
   char link_diag[512];
   snprintf(link_diag, sizeof(link_diag),
      "[FFXV UpscaleChain] LinkUpscaledResource src=%p key=%#llx output=%.0fx%.0f uav=%s",
      original_resource, static_cast<unsigned long long>(source_key), output_resolution.x, output_resolution.y, require_uav ? "req" : "opt");
   Log_Debug(reshade::log::level::info, link_diag);
#endif

   if (UpscaledResource* linked = GetLinkedUpscaled(tracking, source_key))
   {
#if DEVELOPMENT || TEST
      const uint32_t pool_index = FindPoolIndexByPtr(tracking, linked);
      snprintf(link_diag, sizeof(link_diag),
         "[FFXV UpscaleChain] LinkUpscaledResource -> FOUND existing link pool_idx=%u rtv=%p uav=%p",
         pool_index,
         static_cast<void*>(linked->rtv.get()), static_cast<void*>(linked->uav.get()));
      Log_Debug(reshade::log::level::info, link_diag);
#endif
      if (!require_uav || linked->uav)
         return linked;

      ASSERT_ONCE(false); // an existing resource should never change UAV support without the pointer changing, since that would indicate a pool management bug
      linked->in_use_this_frame = false;
      tracking.frame_links.erase(source_key);
   }
   else
   {
#if DEVELOPMENT || TEST
      snprintf(link_diag, sizeof(link_diag),
         "[FFXV UpscaleChain] LinkUpscaledResource -> NO existing link, will create new pool entry");
      Log_Debug(reshade::log::level::info, link_diag);
#endif
   }

   ComPtr<ID3D11Texture2D> original_tex;
   if (FAILED(original_resource->QueryInterface(original_tex.put())))
   {
      Log_Debug(
         reshade::log::level::warning,
         "Failed to query original resource for texture2D interface - cannot link upscaled resource");
      return nullptr;
   }

   D3D11_TEXTURE2D_DESC original_desc;
   original_tex->GetDesc(&original_desc);

#if DEVELOPMENT || TEST
   snprintf(link_diag, sizeof(link_diag),
      "[FFXV UpscaleChain] LinkUpscaledResource original_desc=%ux%u fmt=%u",
      original_desc.Width, original_desc.Height, static_cast<uint32_t>(original_desc.Format));
   Log_Debug(reshade::log::level::info, link_diag);
#endif

   UpscaledResource* upscaled = AcquireUpscaledFromPool(device, device_context, tracking, original_desc, output_resolution, require_uav);
   if (!upscaled)
   {
      Log_Debug(
         reshade::log::level::warning,
         std::format("Failed to acquire upscaled resource for {}x{} fmt={} (UAV {})",
            original_desc.Width, original_desc.Height, static_cast<uint32_t>(original_desc.Format),
            require_uav ? "required" : "not required"));
      return nullptr;
   }

   const uint32_t pool_index = FindPoolIndexByPtr(tracking, upscaled);
   if (pool_index == kInvalidPoolIndex)
      return nullptr;
   tracking.frame_links[source_key] = pool_index;
   return upscaled;
}

#if DEVELOPMENT || TEST
// Per-resource detail captured during a Replace* call (for per-shader logging).
struct UpscaleSwapDetail
{
   UINT slot = 0;
   void* original = nullptr;    // pointer to the original (render-res) resource
   void* texture_ptr = nullptr; // pointer to the pool texture (canonical, use this to trace across passes)
   void* replacement = nullptr; // pointer to the view object (SRV or RTV) bound by the replacement
   UINT width = 0;
   UINT height = 0;
   uint32_t format = 0;
   bool is_rtv = false; // false = SRV, true = RTV
};
#endif

// Replace bound PS/CS SRVs that reference tracked resources. Returns count of
// active tracked hits.
// out_swapped is set to the number of SRVs actually replaced with upscaled versions.
static uint32_t ReplaceUpscaledInputs(
   ID3D11DeviceContext* context,
   UpscaleTrackingState& tracking,
   bool is_compute,
   uint32_t* out_swapped = nullptr
#if DEVELOPMENT || TEST
   ,
   std::vector<UpscaleSwapDetail>* out_details = nullptr
#endif
)
{
   constexpr UINT MAX_SLOTS = 16;
   ID3D11ShaderResourceView* srvs[MAX_SLOTS] = {};
   if (is_compute)
      context->CSGetShaderResources(0, MAX_SLOTS, srvs);
   else
      context->PSGetShaderResources(0, MAX_SLOTS, srvs);

   uint32_t chain_hits = 0;
   uint32_t swapped = 0;
   for (UINT i = 0; i < MAX_SLOTS; ++i)
   {
      if (!srvs[i])
         continue;

      ComPtr<ID3D11Resource> resource;
      srvs[i]->GetResource(resource.put());
      srvs[i]->Release(); // balance the Get

      if (!resource)
         continue;

      const uintptr_t source_key = reinterpret_cast<uintptr_t>(resource.get());
      UpscaledResource* linked = GetLinkedUpscaled(tracking, source_key);

      ID3D11ShaderResourceView* replacement = nullptr;
      if (linked && linked->srv)
      {
         ++chain_hits;
         replacement = linked->srv.get();
         if (is_compute)
            context->CSSetShaderResources(i, 1, &replacement);
         else
            context->PSSetShaderResources(i, 1, &replacement);
         ++swapped;
      }

#if DEVELOPMENT || TEST
      if (out_details)
      {
         UpscaleSwapDetail detail;
         detail.slot = i;
         detail.original = resource.get();
         if (linked && linked->texture)
         {
            detail.texture_ptr = linked->texture.get(); // canonical pool texture - same across SRV/RTV views
            ComPtr<ID3D11Texture2D> swap_tex;
            if (SUCCEEDED(linked->texture->QueryInterface(swap_tex.put())))
            {
               D3D11_TEXTURE2D_DESC sd;
               swap_tex->GetDesc(&sd);
               detail.width = sd.Width;
               detail.height = sd.Height;
               detail.format = static_cast<uint32_t>(sd.Format);
            }
         }
         else
         {
            ComPtr<ID3D11Texture2D> orig_tex;
            if (SUCCEEDED(resource->QueryInterface(orig_tex.put())))
            {
               D3D11_TEXTURE2D_DESC sd;
               orig_tex->GetDesc(&sd);
               detail.width = sd.Width;
               detail.height = sd.Height;
               detail.format = static_cast<uint32_t>(sd.Format);
            }
         }
         detail.replacement = replacement;
         out_details->push_back(detail);
      }
#endif
   }

   if (out_swapped)
      *out_swapped = swapped;
   return chain_hits;
}

// Replace bound RTVs whose underlying texture matches render_resolution.
// Only full-frame render targets are upscaled; bloom, reduction, and other
// non-full-frame targets are left untouched to avoid chain pollution.
// Returns count of RTVs replaced.
static uint32_t ReplaceUpscaledOutputs(
   ID3D11Device* device,
   ID3D11DeviceContext* context,
   UpscaleTrackingState& tracking,
   const float2& render_resolution,
   const float2& output_resolution
#if DEVELOPMENT || TEST
   ,
   std::vector<UpscaleSwapDetail>* out_details = nullptr
#endif
)
{
   // The game only binds a single RTV during post-processing passes.
   // Use single-RTV API to avoid array complexity.
   ComPtr<ID3D11RenderTargetView> orig_rtv;
   ComPtr<ID3D11DepthStencilView> dsv;
   context->OMGetRenderTargets(1, orig_rtv.put(), dsv.put());

   const UINT render_w = static_cast<UINT>(render_resolution.x);
   const UINT render_h = static_cast<UINT>(render_resolution.y);

   if (!orig_rtv)
      return 0;

   ComPtr<ID3D11Resource> resource;
   orig_rtv->GetResource(resource.put());
   if (!resource)
      return 0;

   // Only upscale RTVs that are at render_resolution (full-frame targets).
   // Skip bloom buffers, reduction targets, and other non-full-frame textures.
   ComPtr<ID3D11Texture2D> tex;
   if (FAILED(resource->QueryInterface(tex.put())))
      return 0;

   D3D11_TEXTURE2D_DESC tex_desc;
   tex->GetDesc(&tex_desc);

   if (tex_desc.Width != render_w || tex_desc.Height != render_h)
      return 0;

   const uintptr_t res_key = reinterpret_cast<uintptr_t>(resource.get());
   bool has_frame_link = false;
   {
      std::lock_guard<std::recursive_mutex> lock(tracking.mutex);
      has_frame_link = tracking.frame_links.find(res_key) != tracking.frame_links.end();
   }

#if DEVELOPMENT || TEST
   Log_Debug(
      reshade::log::level::info,
      std::format("[FFXV UpscaleChain] ReplaceUpscaledOutputs slot=0 res={:#x} {}x{} fmt={} render={}x{} has_link={}",
         res_key, tex_desc.Width, tex_desc.Height,
         static_cast<uint32_t>(tex_desc.Format), render_w, render_h, has_frame_link));
#endif

   UpscaledResource* upscaled = LinkUpscaledResource(device, context, resource.get(), tracking, output_resolution);
   if (!upscaled || !upscaled->rtv)
      return 0;

   ID3D11RenderTargetView* new_rtv = upscaled->rtv.get();

#if DEVELOPMENT || TEST
   if (out_details)
   {
      UpscaleSwapDetail detail;
      detail.slot = 0;
      detail.original = resource.get();
      detail.texture_ptr = upscaled->texture.get();
      detail.replacement = upscaled->rtv.get();
      detail.is_rtv = true;
      detail.width = static_cast<UINT>(output_resolution.x);
      detail.height = static_cast<UINT>(output_resolution.y);
      detail.format = static_cast<uint32_t>(tex_desc.Format);
      out_details->push_back(detail);
   }
#endif

   // DEBUG: Log RTVs BEFORE OMSetRenderTargets
#if DEVELOPMENT || TEST
   {
      char pre_log[512];
      int off = snprintf(pre_log, sizeof(pre_log), "[RTV REPLACE] BEFORE OMSet ctx=%p dsv=%p orig_rtv=%p(%p): ",
         context, dsv.get(), orig_rtv.get(), resource.get());
      ComPtr<ID3D11Texture2D> pre_tex;
      D3D11_TEXTURE2D_DESC pre_td = {};
      if (SUCCEEDED(resource->QueryInterface(pre_tex.put())))
         pre_tex->GetDesc(&pre_td);
      off += snprintf(pre_log + off, sizeof(pre_log) - off, "orig=%ux%u ", pre_td.Width, pre_td.Height);
      ComPtr<ID3D11Resource> new_r;
      new_rtv->GetResource(new_r.put());
      ComPtr<ID3D11Texture2D> new_t;
      D3D11_RESOURCE_DIMENSION rdim;
      if (new_r)
      {
         new_r->GetType(&rdim);
         if (rdim == D3D11_RESOURCE_DIMENSION_TEXTURE2D)
         {
            new_r->QueryInterface(new_t.put());
            if (new_t)
               new_t->GetDesc(&pre_td);
         }
      }
      off += snprintf(pre_log + off, sizeof(pre_log) - off, "new=%p %ux%u", new_rtv, pre_td.Width, pre_td.Height);
      Log_Debug(reshade::log::level::debug, pre_log);
   }
#endif

   // Discard the DSV from OMGetRenderTargets. It may have different dimensions
   // than the new upscaled RTV (e.g. DSV=640x360, RTV=1280x720), which causes
   // OMSetRenderTargets to silently fail in D3D11. Post-processing passes
   // never need depth/stencil anyway.
   context->OMSetRenderTargets(1, &new_rtv, nullptr);

   // DEBUG: Log RTVs AFTER OMSetRenderTargets
#if DEVELOPMENT || TEST
   {
      ComPtr<ID3D11RenderTargetView> verify_rtv;
      ComPtr<ID3D11DepthStencilView> verify_dsv;
      context->OMGetRenderTargets(1, verify_rtv.put(), verify_dsv.put());
      char post_log[512];
      int off = snprintf(post_log, sizeof(post_log), "[RTV REPLACE] AFTER OMSet dsv=%p: ", verify_dsv.get());
      ComPtr<ID3D11Resource> v_r;
      verify_rtv->GetResource(v_r.put());
      ComPtr<ID3D11Texture2D> v_t;
      D3D11_RESOURCE_DIMENSION vrdim;
      D3D11_TEXTURE2D_DESC v_td = {};
      if (v_r)
      {
         v_r->GetType(&vrdim);
         if (vrdim == D3D11_RESOURCE_DIMENSION_TEXTURE2D)
         {
            v_r->QueryInterface(v_t.put());
            if (v_t)
               v_t->GetDesc(&v_td);
         }
      }
      off += snprintf(post_log + off, sizeof(post_log) - off, "bound=%p(%p) %ux%u",
         verify_rtv.get(), v_r.get(), v_td.Width, v_td.Height);
      Log_Debug(reshade::log::level::debug, post_log);
   }
#endif

   return 1;
}

// Replace viewports that match render_resolution with output_resolution.
// Returns the count of viewports replaced.
// out_scissors_replaced receives the count of scissor rects replaced (if provided).
static uint32_t ReplaceViewports(
   ID3D11DeviceContext* context,
   const float2& render_resolution,
   const float2& output_resolution,
   uint32_t* out_scissors_replaced = nullptr)
{
   UINT num_viewports = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
   D3D11_VIEWPORT viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
   context->RSGetViewports(&num_viewports, viewports);

   if (num_viewports == 0)
      return 0;

   uint32_t replaced = 0;
   // Use a small tolerance for floating-point viewport dimensions
   constexpr float EPSILON = 0.5f;

   for (UINT i = 0; i < num_viewports; ++i)
   {
      if (std::abs(viewports[i].Width - render_resolution.x) < EPSILON &&
          std::abs(viewports[i].Height - render_resolution.y) < EPSILON)
      {
         viewports[i].Width = output_resolution.x;
         viewports[i].Height = output_resolution.y;
         ++replaced;
      }
   }

   if (replaced > 0)
   {
      context->RSSetViewports(num_viewports, viewports);
   }

   // Also replace Scissor Rects!
   UINT num_rects = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
   D3D11_RECT rects[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
   context->RSGetScissorRects(&num_rects, rects);

   uint32_t scissors_replaced = 0;
   if (num_rects > 0)
   {
      for (UINT i = 0; i < num_rects; ++i)
      {
         // D3D11_RECT uses LONG (left, top, right, bottom)
         LONG w = rects[i].right - rects[i].left;
         LONG h = rects[i].bottom - rects[i].top;

         if (std::abs((float)w - render_resolution.x) < EPSILON &&
             std::abs((float)h - render_resolution.y) < EPSILON)
         {
            // Log scissor rect before replacement
#if DEVELOPMENT || TEST
            char scissor_log_buf[256];
            snprintf(scissor_log_buf, sizeof(scissor_log_buf),
               "[FFXV] Scissor replaced #%u: [%d, %d, %d, %d] (%dx%d) -> [%d, %d, %d, %d] (%dx%d)",
               i, rects[i].left, rects[i].top, rects[i].right, rects[i].bottom, w, h,
               rects[i].left, rects[i].top, rects[i].left + static_cast<LONG>(output_resolution.x), rects[i].top + static_cast<LONG>(output_resolution.y),
               static_cast<UINT>(output_resolution.x), static_cast<UINT>(output_resolution.y));
            Log_Debug(reshade::log::level::info, scissor_log_buf);
#endif

            rects[i].right = rects[i].left + static_cast<LONG>(output_resolution.x);
            rects[i].bottom = rects[i].top + static_cast<LONG>(output_resolution.y);
            ++scissors_replaced;
         }
      }

      if (scissors_replaced > 0)
      {
         context->RSSetScissorRects(num_rects, rects);
      }
   }

   if (out_scissors_replaced)
      *out_scissors_replaced = scissors_replaced;

   return replaced;
}

#if DEVELOPMENT || TEST
// Dump all bound SRV/RTV state for a single draw call to identify chain gaps.
// Logs each slot's resource pointer, dimensions, and chain membership (link/pool entry).
static void DiagnosePassSRVs(
   ID3D11DeviceContext* context,
   const UpscaleTrackingState& tracking,
   bool is_compute,
   const char* pass_label)
{
   constexpr UINT MAX_SLOTS = 16;
   ID3D11ShaderResourceView* srvs[MAX_SLOTS] = {};
   if (is_compute)
      context->CSGetShaderResources(0, MAX_SLOTS, srvs);
   else
      context->PSGetShaderResources(0, MAX_SLOTS, srvs);

   char buf[384];
   size_t frame_link_count = 0;
   size_t pool_size = 0;
   {
      std::lock_guard<std::recursive_mutex> lock(tracking.mutex);
      frame_link_count = tracking.frame_links.size();
      pool_size = tracking.pool.size();
   }
   snprintf(buf, sizeof(buf),
      "[FFXV Diag] %s  frame_link_count=%zu pool_size=%zu",
      pass_label, frame_link_count, pool_size);
   Log_Debug(reshade::log::level::info, buf);

   for (UINT i = 0; i < MAX_SLOTS; ++i)
   {
      if (!srvs[i])
         continue;

      ComPtr<ID3D11Resource> resource;
      srvs[i]->GetResource(resource.put());
      srvs[i]->Release();

      if (!resource)
         continue;

      const uintptr_t key = reinterpret_cast<uintptr_t>(resource.get());
      bool in_link = false;
      bool has_pool_entry = false;
      {
         std::lock_guard<std::recursive_mutex> lock(tracking.mutex);
         const auto it = tracking.frame_links.find(key);
         in_link = it != tracking.frame_links.end();
         has_pool_entry = in_link && it->second < tracking.pool.size();
      }

      UINT w = 0, h = 0;
      uint32_t fmt = 0;
      ComPtr<ID3D11Texture2D> tex;
      if (SUCCEEDED(resource->QueryInterface(tex.put())))
      {
         D3D11_TEXTURE2D_DESC d;
         tex->GetDesc(&d);
         w = d.Width;
         h = d.Height;
         fmt = static_cast<uint32_t>(d.Format);
      }

      snprintf(buf, sizeof(buf),
         "[FFXV Diag] %s  slot=%u  res=%p  %ux%u fmt=%u  link=%d pool=%d",
         pass_label, i, reinterpret_cast<void*>(key), w, h, fmt,
         in_link ? 1 : 0, has_pool_entry ? 1 : 0);
      Log_Debug(reshade::log::level::info, buf);
   }

   // Also dump bound RTVs so we can see where the pass writes
   if (!is_compute)
   {
      ID3D11RenderTargetView* rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
      context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtvs, nullptr);
      for (UINT i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
      {
         if (!rtvs[i])
            continue;
         ComPtr<ID3D11Resource> resource;
         rtvs[i]->GetResource(resource.put());
         rtvs[i]->Release();
         if (!resource)
            continue;

         const uintptr_t key = reinterpret_cast<uintptr_t>(resource.get());
         UINT w = 0, h = 0;
         ComPtr<ID3D11Texture2D> tex;
         if (SUCCEEDED(resource->QueryInterface(tex.put())))
         {
            D3D11_TEXTURE2D_DESC d;
            tex->GetDesc(&d);
            w = d.Width;
            h = d.Height;
         }
         snprintf(buf, sizeof(buf),
            "[FFXV Diag] %s  RTV slot=%u  res=%p  %ux%u",
            pass_label, i, reinterpret_cast<void*>(key), w, h);
         Log_Debug(reshade::log::level::info, buf);
      }
   }
}
#endif
