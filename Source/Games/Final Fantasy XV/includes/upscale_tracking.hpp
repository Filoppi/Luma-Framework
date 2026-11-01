#pragma once
#include <d3d11.h>
#include <algorithm>
#include <deque>
#include <functional>
#include <mutex>
#include <unordered_map>

// Tolerance for aspect ratio comparisons used when deciding whether an RTV/viewport
// belongs to the render pipeline and should be proportionally upscaled.
static constexpr double kAspectRatioEpsilon = 0.02;

// Per-resource upscaled replacement: output-resolution texture + views
struct UpscaledResource
{
   ComPtr<ID3D11Texture2D> texture;
   ComPtr<ID3D11ShaderResourceView> srv;
   ComPtr<ID3D11RenderTargetView> rtv;
   ComPtr<ID3D11UnorderedAccessView> uav;
   D3D11_TEXTURE2D_DESC original_desc{}; // cached for compatibility checks
   DXGI_FORMAT view_format = DXGI_FORMAT_UNKNOWN; // typed view format when the pool texture is typeless; UNKNOWN = use texture format directly
   bool in_use_this_frame = false;
};

// Output-resolution stencil buffer standing in for a render-sized one, for passes that mask with stencil only.
struct UpscaledStencil
{
   ComPtr<ID3D11Texture2D> texture;
   ComPtr<ID3D11DepthStencilView> dsv;
   D3D11_TEXTURE2D_DESC original_desc{};
   D3D11_DEPTH_STENCIL_VIEW_DESC view_desc{};
   bool cleared_this_frame = false;
};

// Persistent output-resolution allocations + per-frame source links.
// Pool entries persist across frames; source links are cleared every frame.
// Keys are ID3D11Resource* pointers for stable identity across interface
// boundaries (SRV::GetResource, RTV::GetResource, ID3D11Texture2D all converge).
struct UpscaleTrackingState
{
   std::deque<UpscaledResource> pool;
   std::unordered_map<uintptr_t, uint32_t> frame_links; // source resource ptr -> pool index (frame-local)
   std::unordered_map<uintptr_t, UpscaledStencil> stencil_pool; // source DSV resource ptr -> output-resolution stencil
   bool post_taa_upscale_active = false;                // gate: true until the scene reaches an output-sized target
   UINT pool_output_width = 0;
   UINT pool_output_height = 0;
   UINT pool_render_width = 0;
   UINT pool_render_height = 0;
   mutable std::recursive_mutex mutex;

   void ResetFrame()
   {
      std::lock_guard<std::recursive_mutex> lock(mutex);
      frame_links.clear();
      for (auto& entry : pool)
         entry.in_use_this_frame = false;
      for (auto& [key, stencil] : stencil_pool)
         stencil.cleared_this_frame = false;
      post_taa_upscale_active = false;
   }

   void ClearPool()
   {
      std::lock_guard<std::recursive_mutex> lock(mutex);
      pool.clear();
      stencil_pool.clear();
      frame_links.clear();
      post_taa_upscale_active = false;
      pool_output_width = 0;
      pool_output_height = 0;
      pool_render_width = 0;
      pool_render_height = 0;
   }

   // Invalidate the pool whenever the render->output scale changes (either resolution changes).
   // All pooled entries are sized relative to the scale factor, so any change makes them stale.
   void InvalidatePoolIfScaleChanged(const float2& output_resolution, const float2& render_resolution)
   {
      std::lock_guard<std::recursive_mutex> lock(mutex);
      const UINT output_w = static_cast<UINT>(output_resolution.x);
      const UINT output_h = static_cast<UINT>(output_resolution.y);
      const UINT render_w = static_cast<UINT>(render_resolution.x);
      const UINT render_h = static_cast<UINT>(render_resolution.y);

      if (pool_output_width != 0 && pool_output_height != 0 &&
          (pool_output_width != output_w || pool_output_height != output_h ||
           pool_render_width != render_w || pool_render_height != render_h))
      {
         ClearPool();
      }

      pool_output_width = output_w;
      pool_output_height = output_h;
      pool_render_width = render_w;
      pool_render_height = render_h;
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

static UpscaledResource* AcquireUpscaledFromPool(
   ID3D11Device* device,
   ID3D11DeviceContext* device_context,
   UpscaleTrackingState& tracking,
   const D3D11_TEXTURE2D_DESC& original_desc,
   const float2& output_resolution,
   bool require_uav = false,
   DXGI_FORMAT view_format = DXGI_FORMAT_UNKNOWN)
{
   std::lock_guard<std::recursive_mutex> lock(tracking.mutex);

   const UINT target_w = static_cast<UINT>(output_resolution.x);
   const UINT target_h = static_cast<UINT>(output_resolution.y);

   for (size_t pool_index = 0; pool_index < tracking.pool.size(); ++pool_index)
   {
      auto& entry = tracking.pool[pool_index];
      if (entry.in_use_this_frame || !entry.texture)
         continue;

      if ((!require_uav || entry.uav) &&
          entry.view_format == view_format &&
          entry.original_desc.Format == original_desc.Format &&
          entry.original_desc.Width == original_desc.Width &&
          entry.original_desc.Height == original_desc.Height)
      {
         D3D11_TEXTURE2D_DESC existing;
         entry.texture->GetDesc(&existing);
         if (existing.Width == target_w && existing.Height == target_h)
         {
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
   entry.view_format = view_format;

   if (FAILED(device->CreateTexture2D(&desc, nullptr, entry.texture.put())))
      return nullptr;

   if (view_format != DXGI_FORMAT_UNKNOWN)
   {
      // Texture is typeless — create typed views explicitly to avoid D3D11 validation errors.
      D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc{};
      srv_desc.Format = view_format;
      srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
      srv_desc.Texture2D.MipLevels = 1;
      if (FAILED(device->CreateShaderResourceView(entry.texture.get(), &srv_desc, entry.srv.put())))
         return nullptr;

      D3D11_RENDER_TARGET_VIEW_DESC rtv_desc{};
      rtv_desc.Format = view_format;
      rtv_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
      if (FAILED(device->CreateRenderTargetView(entry.texture.get(), &rtv_desc, entry.rtv.put())))
         return nullptr;

      if (desc.BindFlags & D3D11_BIND_UNORDERED_ACCESS)
      {
         D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc{};
         uav_desc.Format = view_format;
         uav_desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
         if (FAILED(device->CreateUnorderedAccessView(entry.texture.get(), &uav_desc, entry.uav.put())))
            return nullptr;
      }
   }
   else
   {
      if (FAILED(device->CreateShaderResourceView(entry.texture.get(), nullptr, entry.srv.put())))
         return nullptr;

      if (FAILED(device->CreateRenderTargetView(entry.texture.get(), nullptr, entry.rtv.put())))
         return nullptr;

      if (desc.BindFlags & D3D11_BIND_UNORDERED_ACCESS)
      {
         if (FAILED(device->CreateUnorderedAccessView(entry.texture.get(), nullptr, entry.uav.put())))
            return nullptr;
      }
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

static UpscaledResource* LinkUpscaledResource(
   ID3D11Device* device, ID3D11DeviceContext* device_context, ID3D11Resource* original_resource,
   UpscaleTrackingState& tracking, const float2& output_resolution, bool require_uav, DXGI_FORMAT view_format);
static void UnlinkUpscaledResource(UpscaleTrackingState& tracking, uintptr_t source_key);

// Full clears of render-shaped RTVs during the active chain establish their
// scaled target immediately. This initializes pooled/discarded MRT regions
// before the following draw without retaining a clear that could go stale.
static void TrackAndClearUpscaleTarget(
   ID3D11Device* device, ID3D11DeviceContext* context, UpscaleTrackingState& tracking,
   ID3D11RenderTargetView* rtv, const float color[4], uint32_t rect_count,
   const float2& render_resolution, const float2& output_resolution)
{
   std::lock_guard<std::recursive_mutex> lock(tracking.mutex);
   if (!tracking.post_taa_upscale_active || !device || !context || !rtv || !color ||
       context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE || render_resolution.x <= 0.f ||
       render_resolution.y <= 0.f || output_resolution.x <= 0.f || output_resolution.y <= 0.f)
      return;
   ComPtr<ID3D11Resource> resource;
   ComPtr<ID3D11Texture2D> texture;
   rtv->GetResource(resource.put());
   D3D11_RENDER_TARGET_VIEW_DESC view{};
   rtv->GetDesc(&view);
   D3D11_TEXTURE2D_DESC desc{};
   if (!resource || FAILED(resource->QueryInterface(texture.put())))
      return;
   const uintptr_t key = reinterpret_cast<uintptr_t>(resource.get());
   if (rect_count != 0)
   {
      // Rect coordinates belong to the native grid. Drop any previous link so
      // a later promotion initializes from the updated native contents.
      UnlinkUpscaledResource(tracking, key);
      return;
   }
   texture->GetDesc(&desc);
   if (view.ViewDimension != D3D11_RTV_DIMENSION_TEXTURE2D || view.Texture2D.MipSlice != 0 ||
       desc.ArraySize != 1 || desc.SampleDesc.Count != 1)
   {
      return;
   }
   const double render_ar = double(render_resolution.x) / render_resolution.y;
   const double target_ar = double(desc.Width) / desc.Height;
   if (desc.Width >= static_cast<UINT>(output_resolution.x) || desc.Height >= static_cast<UINT>(output_resolution.y) ||
       std::abs(target_ar / render_ar - 1.0) > kAspectRatioEpsilon)
      return;
   for (const auto& entry : tracking.pool)
      if (entry.in_use_this_frame && entry.texture.get() == texture.get())
         return; // The normal clear already addresses the pool texture itself.
   const float2 scaled_size = {
      float((std::min)(static_cast<UINT>(std::round(desc.Width * double(output_resolution.x) / render_resolution.x)), static_cast<UINT>(output_resolution.x))),
      float((std::min)(static_cast<UINT>(std::round(desc.Height * double(output_resolution.y) / render_resolution.y)), static_cast<UINT>(output_resolution.y))) };
   UpscaledResource* scaled = LinkUpscaledResource(device, context, resource.get(), tracking, scaled_size, false,
      view.Format != desc.Format ? view.Format : DXGI_FORMAT_UNKNOWN);
   if (scaled && scaled->rtv)
      context->ClearRenderTargetView(scaled->rtv.get(), color);
}

// Native writers must not leave a previous scaled version authoritative.
static void ForgetNativeUpscaleOutputs(ID3D11DeviceContext* context, UpscaleTrackingState& tracking)
{
   ID3D11RenderTargetView* targets[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
   context->OMGetRenderTargets(ARRAYSIZE(targets), targets, nullptr);
   std::lock_guard<std::recursive_mutex> lock(tracking.mutex);
   for (auto* target : targets)
   {
      if (!target)
         continue;
      ComPtr<ID3D11Resource> resource;
      target->GetResource(resource.put());
      target->Release();
      tracking.frame_links.erase(reinterpret_cast<uintptr_t>(resource.get()));
   }
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
   bool require_uav = false,
   DXGI_FORMAT view_format = DXGI_FORMAT_UNKNOWN)
{
   std::lock_guard<std::recursive_mutex> lock(tracking.mutex);
   if (!original_resource)
      return nullptr;

   const uintptr_t source_key = reinterpret_cast<uintptr_t>(original_resource);

   if (UpscaledResource* linked = GetLinkedUpscaled(tracking, source_key))
   {
      // Validate the entry still matches the requested target size and view format.
      // The target size can legitimately differ between draws on the same source pointer
      // (e.g. render-res vs a sub-res effect buffer aliasing the same texture slot).
      bool entry_valid = linked->view_format == view_format;
      if (entry_valid && linked->texture)
      {
         D3D11_TEXTURE2D_DESC linked_desc;
         linked->texture->GetDesc(&linked_desc);
         entry_valid = linked_desc.Width  == static_cast<UINT>(output_resolution.x) &&
                       linked_desc.Height == static_cast<UINT>(output_resolution.y);
      }
      if (entry_valid)
      {
         if (!require_uav || linked->uav)
            return linked;
         ASSERT_ONCE(false); // UAV support changed on an existing entry — pool management bug
      }
      linked->in_use_this_frame = false;
      tracking.frame_links.erase(source_key);
   }

   ComPtr<ID3D11Texture2D> original_tex;
   if (FAILED(original_resource->QueryInterface(original_tex.put())))
      return nullptr;

   D3D11_TEXTURE2D_DESC original_desc;
   original_tex->GetDesc(&original_desc);

   UpscaledResource* upscaled = AcquireUpscaledFromPool(device, device_context, tracking, original_desc, output_resolution, require_uav, view_format);
   if (!upscaled)
      return nullptr;

   const uint32_t pool_index = FindPoolIndexByPtr(tracking, upscaled);
   if (pool_index == kInvalidPoolIndex)
      return nullptr;
   tracking.frame_links[source_key] = pool_index;
   return upscaled;
}

// Replace bound PS/CS SRVs that reference tracked resources. Return the number
// of active tracked inputs; unused slots beyond active_slots are left alone.
static uint32_t ReplaceUpscaledInputs(
   ID3D11DeviceContext* context,
   UpscaleTrackingState& tracking,
   bool is_compute,
   UINT active_slots = 16,
   bool replace = true)
{
   constexpr UINT MAX_SLOTS = 16;
   active_slots = (std::min)(active_slots, MAX_SLOTS);
   ID3D11ShaderResourceView* srvs[MAX_SLOTS] = {};
   if (is_compute)
      context->CSGetShaderResources(0, active_slots, srvs);
   else
      context->PSGetShaderResources(0, active_slots, srvs);

   uint32_t chain_hits = 0;
   for (UINT i = 0; i < active_slots; ++i)
   {
      if (!srvs[i])
         continue;
      ComPtr<ID3D11Resource> resource;
      srvs[i]->GetResource(resource.put());
      srvs[i]->Release(); // balance the Get
      if (!resource)
         continue;
      UpscaledResource* linked = GetLinkedUpscaled(tracking, reinterpret_cast<uintptr_t>(resource.get()));
      if (!linked || !linked->srv)
         continue;
      ++chain_hits;
      if (!replace)
         continue;
      ID3D11ShaderResourceView* replacement = linked->srv.get();
      if (is_compute)
         context->CSSetShaderResources(i, 1, &replacement);
      else
         context->PSSetShaderResources(i, 1, &replacement);
   }
   return chain_hits;
}

// Output Scaled also copies screen effects back into render-sized scene textures.
// Only a native output-sized destination is the final scene handoff.
static bool OutputScaledWritesOutputResolution(
   ID3D11DeviceContext* context,
   const UpscaleTrackingState& tracking,
   const float2& output_resolution)
{
   if (output_resolution.x <= 0.f || output_resolution.y <= 0.f)
      return false;

   ComPtr<ID3D11RenderTargetView> target;
   context->OMGetRenderTargets(1, target.put(), nullptr);
   ComPtr<ID3D11Resource> resource;
   ComPtr<ID3D11Texture2D> texture;
   if (target)
      target->GetResource(resource.put());
   if (!resource || FAILED(resource->QueryInterface(texture.put())))
      return false;

   D3D11_RENDER_TARGET_VIEW_DESC view_desc;
   target->GetDesc(&view_desc);
   if (view_desc.ViewDimension != D3D11_RTV_DIMENSION_TEXTURE2D || view_desc.Texture2D.MipSlice != 0)
      return false;

   // A pool texture can be output-sized while replacing a render-sized target.
   std::lock_guard<std::recursive_mutex> lock(tracking.mutex);
   for (const auto& entry : tracking.pool)
      if (entry.in_use_this_frame && entry.texture.get() == texture.get())
         return false;

   D3D11_TEXTURE2D_DESC desc;
   texture->GetDesc(&desc);
   return desc.Width >= static_cast<UINT>(output_resolution.x) &&
      desc.Height >= static_cast<UINT>(output_resolution.y);
}

static ID3D11DepthStencilView* AcquireUpscaledStencil(
   ID3D11Device* device,
   ID3D11DeviceContext* context,
   UpscaleTrackingState& tracking,
   ID3D11DepthStencilView* original_dsv,
   UINT target_w,
   UINT target_h)
{
   std::lock_guard<std::recursive_mutex> lock(tracking.mutex);
   ComPtr<ID3D11Resource> resource;
   ComPtr<ID3D11Texture2D> texture;
   original_dsv->GetResource(resource.put());
   if (!resource || FAILED(resource->QueryInterface(texture.put())))
      return nullptr;
   D3D11_TEXTURE2D_DESC desc{};
   texture->GetDesc(&desc);
   D3D11_DEPTH_STENCIL_VIEW_DESC view_desc{};
   original_dsv->GetDesc(&view_desc);
   if (view_desc.ViewDimension != D3D11_DSV_DIMENSION_TEXTURE2D || view_desc.Texture2D.MipSlice != 0 ||
       desc.ArraySize != 1 || desc.SampleDesc.Count != 1 ||
       (view_desc.Format != DXGI_FORMAT_D32_FLOAT_S8X24_UINT && view_desc.Format != DXGI_FORMAT_D24_UNORM_S8_UINT))
      return nullptr;
   view_desc.Flags = 0; // Never read only, the stencil is written by the passes using it

   UpscaledStencil& entry = tracking.stencil_pool[reinterpret_cast<uintptr_t>(resource.get())];
   if (entry.texture)
   {
      D3D11_TEXTURE2D_DESC existing{};
      entry.texture->GetDesc(&existing);
      if (existing.Width != target_w || existing.Height != target_h || existing.Format != desc.Format ||
          entry.view_desc.Format != view_desc.Format)
         entry = {};
   }
   if (!entry.texture)
   {
      D3D11_TEXTURE2D_DESC new_desc = desc;
      new_desc.Width = target_w;
      new_desc.Height = target_h;
      new_desc.MipLevels = 1;
      new_desc.Usage = D3D11_USAGE_DEFAULT;
      new_desc.CPUAccessFlags = 0;
      new_desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
      new_desc.MiscFlags = 0;
      if (FAILED(device->CreateTexture2D(&new_desc, nullptr, entry.texture.put())) ||
          FAILED(device->CreateDepthStencilView(entry.texture.get(), &view_desc, entry.dsv.put())))
      {
         entry = {};
         return nullptr;
      }
      entry.original_desc = desc;
      entry.view_desc = view_desc;
   }
   if (!entry.cleared_this_frame)
   {
      context->ClearDepthStencilView(entry.dsv.get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.f, 0);
      entry.cleared_this_frame = true;
   }
   return entry.dsv.get();
}

// Replace bound RTVs in the upscaled post-process chain.
// Linked effect targets are scaled proportionally, including rounded bloom levels.
// Returns count of bound RTVs using upscaled replacements (including already-bound pool views).
static uint32_t ReplaceUpscaledOutputs(
   ID3D11Device* device,
   ID3D11DeviceContext* context,
   UpscaleTrackingState& tracking,
   const float2& render_resolution,
   const float2& output_resolution,
   bool has_upscaled_input,
   float2& rtv_source_resolution,
   float2& rtv_target_resolution,
   const std::function<bool(ID3D11Texture2D*, DXGI_FORMAT, ID3D11RenderTargetView*)>& initialize_from_native,
   bool& preparation_failed)
{
   constexpr UINT max_targets = D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT;
   struct Output
   {
      ComPtr<ID3D11RenderTargetView> original_view;
      ComPtr<ID3D11Resource> resource;
      ComPtr<ID3D11Texture2D> texture;
      ComPtr<ID3D11Texture2D> initial_contents;
      D3D11_TEXTURE2D_DESC desc{};
      DXGI_FORMAT view_format = DXGI_FORMAT_UNKNOWN;
      DXGI_FORMAT typed_format = DXGI_FORMAT_UNKNOWN;
      uintptr_t source_key = 0;
      uint32_t pool_index = kInvalidPoolIndex;
      bool already_pooled = false;
      bool acquired = false;
   };
   Output outputs[max_targets];
   ID3D11RenderTargetView* bound[max_targets] = {};
   ComPtr<ID3D11DepthStencilView> dsv;
   context->OMGetRenderTargets(max_targets, bound, dsv.put());
   UINT target_count = 0;
   UINT primary = max_targets;
   for (UINT slot = 0; slot < max_targets; ++slot)
   {
      if (!bound[slot])
         continue;
      outputs[slot].original_view = bound[slot];
      bound[slot]->Release(); // balance OMGetRenderTargets
      target_count = slot + 1;
      if (primary == max_targets)
         primary = slot;
   }
   rtv_source_resolution = rtv_target_resolution = {};
   preparation_failed = false;
   // Hold the pool lock through preparation and commit. No source links change
   // until every bound output has a compatible replacement.
   std::lock_guard<std::recursive_mutex> lock(tracking.mutex);
   auto reject = [&]() -> uint32_t
   {
      preparation_failed = true;
      for (auto& output : outputs)
         if (output.acquired && output.pool_index < tracking.pool.size())
            tracking.pool[output.pool_index].in_use_this_frame = false;
      return 0;
   };
   if (primary == max_targets)
      return 0;
   if (render_resolution.x <= 0.f || render_resolution.y <= 0.f || output_resolution.x <= 0.f || output_resolution.y <= 0.f)
      return reject();
   for (UINT slot = 0; slot < target_count; ++slot)
   {
      auto& output = outputs[slot];
      if (!output.original_view)
         continue;
      output.original_view->GetResource(output.resource.put());
      if (!output.resource || FAILED(output.resource->QueryInterface(output.texture.put())))
         return reject();
      output.source_key = reinterpret_cast<uintptr_t>(output.resource.get());
      output.texture->GetDesc(&output.desc);
      D3D11_RENDER_TARGET_VIEW_DESC view_desc{};
      output.original_view->GetDesc(&view_desc);
      if (view_desc.ViewDimension != D3D11_RTV_DIMENSION_TEXTURE2D || view_desc.Texture2D.MipSlice != 0 ||
          output.desc.ArraySize != 1 || output.desc.SampleDesc.Count != 1)
         return reject();
      output.view_format = view_desc.Format != DXGI_FORMAT_UNKNOWN && view_desc.Format != output.desc.Format
         ? view_desc.Format : DXGI_FORMAT_UNKNOWN;
      output.typed_format = view_desc.Format;
      output.initial_contents = output.texture;
      for (uint32_t i = 0; i < tracking.pool.size(); ++i)
      {
         const auto& entry = tracking.pool[i];
         if (entry.in_use_this_frame && entry.texture.get() == output.texture.get())
         {
            output.pool_index = i;
            output.already_pooled = true;
            output.desc = entry.original_desc;
            break;
         }
      }
      if (slot != primary && (output.desc.Width != outputs[primary].desc.Width || output.desc.Height != outputs[primary].desc.Height))
         return reject();
   }
   auto& first = outputs[primary];
   if (!first.already_pooled && (first.desc.Width >= static_cast<UINT>(output_resolution.x) ||
       first.desc.Height >= static_cast<UINT>(output_resolution.y)))
      return 0;
   const double render_ar = double(render_resolution.x) / render_resolution.y;
   const double target_ar = double(first.desc.Width) / first.desc.Height;
   const bool linked = tracking.frame_links.find(first.source_key) != tracking.frame_links.end();
   if (!first.already_pooled && !has_upscaled_input && !linked && std::abs(target_ar / render_ar - 1.0) > kAspectRatioEpsilon)
      return 0;
   const UINT target_w = (std::min)(static_cast<UINT>(std::round(first.desc.Width * double(output_resolution.x) / render_resolution.x)), static_cast<UINT>(output_resolution.x));
   const UINT target_h = (std::min)(static_cast<UINT>(std::round(first.desc.Height * double(output_resolution.y) / render_resolution.y)), static_cast<UINT>(output_resolution.y));
   if (target_w == 0 || target_h == 0)
      return reject();
   const float2 target_resolution = {float(target_w), float(target_h)};
   uint32_t replacements = 0;
   for (UINT slot = 0; slot < target_count; ++slot)
   {
      auto& output = outputs[slot];
      if (!output.original_view)
         continue;
      UpscaledResource* entry = output.already_pooled ? &tracking.pool[output.pool_index] : GetLinkedUpscaled(tracking, output.source_key);
      if (entry && entry->texture && entry->rtv)
      {
         D3D11_TEXTURE2D_DESC desc;
         entry->texture->GetDesc(&desc);
         if (desc.Width != target_w || desc.Height != target_h || entry->view_format != output.view_format)
         {
            output.initial_contents = entry->texture;
            entry = nullptr;
         }
      }
      else
         entry = nullptr;
      if (!entry)
      {
         if (output.already_pooled)
            return reject();
         entry = AcquireUpscaledFromPool(device, context, tracking, output.desc, target_resolution, false, output.view_format);
         if (!entry)
            return reject();
         output.acquired = true;
      }
      output.pool_index = FindPoolIndexByPtr(tracking, entry);
      if (output.pool_index == kInvalidPoolIndex)
         return reject();
      bound[slot] = entry->rtv.get();
      ++replacements;
   }
   // Preserve a compatible DSV; resized post-process outputs cannot use a DSV
   // on the old grid. Color/CoC slots and null gaps are rebound together.
   if (dsv)
   {
      ComPtr<ID3D11Resource> resource;
      ComPtr<ID3D11Texture2D> texture;
      dsv->GetResource(resource.put());
      D3D11_TEXTURE2D_DESC desc{};
      if (SUCCEEDED(resource->QueryInterface(texture.put())))
         texture->GetDesc(&desc);
      if (desc.Width != target_w || desc.Height != target_h || desc.SampleDesc.Count != 1 || desc.ArraySize != 1)
      {
         // Passes masking with stencil only (no depth test) need that stencil on the new grid, otherwise the
         // test would silently turn off. Everything else keeps running without a DSV.
         ComPtr<ID3D11DepthStencilState> depth_stencil_state;
         context->OMGetDepthStencilState(depth_stencil_state.put(), nullptr);
         D3D11_DEPTH_STENCIL_DESC depth_stencil_desc{};
         if (depth_stencil_state)
            depth_stencil_state->GetDesc(&depth_stencil_desc);
         ID3D11DepthStencilView* upscaled_stencil = depth_stencil_state && depth_stencil_desc.StencilEnable && !depth_stencil_desc.DepthEnable
            ? AcquireUpscaledStencil(device, context, tracking, dsv.get(), target_w, target_h)
            : nullptr;
         dsv = upscaled_stencil;
      }
   }
   // Native-content fallback handles targets promoted later than their clear.
   // Active-chain full clears are linked and mirrored immediately, so no clear
   // value is retained across draws or native writes.
   for (UINT slot = 0; slot < target_count; ++slot)
   {
      auto& output = outputs[slot];
      if (!output.acquired)
         continue; // Existing linked targets retain the previous scaled draws.
      if (!initialize_from_native(output.initial_contents.get(), output.typed_format, bound[slot]))
         return reject();
   }
   for (UINT slot = 0; slot < target_count; ++slot)
   {
      auto& output = outputs[slot];
      if (!output.original_view)
         continue;
      if (!output.already_pooled)
         tracking.frame_links[output.source_key] = output.pool_index;
   }
   context->OMSetRenderTargets(target_count, bound, dsv.get());
   rtv_source_resolution = {float(first.desc.Width), float(first.desc.Height)};
   rtv_target_resolution = target_resolution;
   return replacements;
}

static void ReplaceUpscaledUAVOutput(
   ID3D11Device* device,
   ID3D11DeviceContext* context,
   UpscaleTrackingState& tracking,
   const float2& render_resolution,
   const float2& output_resolution,
   const std::function<bool(ID3D11Texture2D*, DXGI_FORMAT, ID3D11RenderTargetView*)>& initialize_from_native)
{
   if (render_resolution.x <= 0.f || render_resolution.y <= 0.f || output_resolution.x <= 0.f || output_resolution.y <= 0.f)
      return;
   ComPtr<ID3D11UnorderedAccessView> uav;
   context->CSGetUnorderedAccessViews(0, 1, uav.put());
   if (!uav)
      return;
   D3D11_UNORDERED_ACCESS_VIEW_DESC view_desc{};
   uav->GetDesc(&view_desc);
   ComPtr<ID3D11Resource> resource;
   ComPtr<ID3D11Texture2D> texture;
   uav->GetResource(resource.put());
   if (!resource || FAILED(resource->QueryInterface(texture.put())))
      return;
   D3D11_TEXTURE2D_DESC desc{};
   texture->GetDesc(&desc);
   if (view_desc.ViewDimension != D3D11_UAV_DIMENSION_TEXTURE2D || view_desc.Texture2D.MipSlice != 0 ||
       desc.ArraySize != 1 || desc.SampleDesc.Count != 1)
      return;
   if (desc.Width >= static_cast<UINT>(output_resolution.x) || desc.Height >= static_cast<UINT>(output_resolution.y))
      return;

   std::lock_guard<std::recursive_mutex> lock(tracking.mutex);
   const uintptr_t source_key = reinterpret_cast<uintptr_t>(resource.get());
   const DXGI_FORMAT view_format = view_desc.Format != DXGI_FORMAT_UNKNOWN && view_desc.Format != desc.Format ? view_desc.Format : DXGI_FORMAT_UNKNOWN;
   UpscaledResource* entry = GetLinkedUpscaled(tracking, source_key);
   if (!entry)
   {
      const double render_ar = double(render_resolution.x) / render_resolution.y;
      const double target_ar = double(desc.Width) / desc.Height;
      if (std::abs(target_ar / render_ar - 1.0) > kAspectRatioEpsilon)
         return;
   }
   const UINT target_w = (std::min)(static_cast<UINT>(std::round(desc.Width * double(output_resolution.x) / render_resolution.x)), static_cast<UINT>(output_resolution.x));
   const UINT target_h = (std::min)(static_cast<UINT>(std::round(desc.Height * double(output_resolution.y) / render_resolution.y)), static_cast<UINT>(output_resolution.y));
   if (target_w == 0 || target_h == 0)
      return;
   if (entry && entry->texture && entry->uav)
   {
      D3D11_TEXTURE2D_DESC entry_desc;
      entry->texture->GetDesc(&entry_desc);
      if (entry_desc.Width != target_w || entry_desc.Height != target_h || entry->view_format != view_format)
         entry = nullptr;
   }
   else
      entry = nullptr;
   if (!entry)
   {
      // Not written by an earlier draw of the chain (or its link doesn't fit): start from the native contents,
      // given a dispatch usually only writes part of its target
      entry = AcquireUpscaledFromPool(device, context, tracking, desc, float2{float(target_w), float(target_h)}, true, view_format);
      if (!entry)
         return;
      const uint32_t pool_index = FindPoolIndexByPtr(tracking, entry);
      if (pool_index == kInvalidPoolIndex || !entry->uav ||
          !initialize_from_native(texture.get(), view_desc.Format, entry->rtv.get()))
      {
         entry->in_use_this_frame = false;
         return;
      }
      tracking.frame_links[source_key] = pool_index;
   }
   ID3D11UnorderedAccessView* replacement = entry->uav.get();
   context->CSSetUnorderedAccessViews(0, 1, &replacement, nullptr);
}

// Scale viewport/scissor edges with the bound target's replacement dimensions.
static void ReplaceViewports(
   ID3D11DeviceContext* context,
   CommandListData& cmd_list_data,
   const float2& render_resolution,
   const float2& output_resolution)
{
   const double scale_x = double(output_resolution.x) / render_resolution.x;
   const double scale_y = double(output_resolution.y) / render_resolution.y;
   if (!cmd_list_data.viewports_scaled)
   {
      UINT count = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
      D3D11_VIEWPORT viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
      context->RSGetViewports(&count, viewports);
      for (UINT i = 0; i < count; ++i)
      {
         viewports[i].TopLeftX *= scale_x;
         viewports[i].TopLeftY *= scale_y;
         viewports[i].Width *= scale_x;
         viewports[i].Height *= scale_y;
      }
      if (count > 0)
         context->RSSetViewports(count, viewports);
      cmd_list_data.viewports_scaled = true;
   }
   if (!cmd_list_data.scissors_scaled)
   {
      ComPtr<ID3D11RasterizerState> rasterizer_state;
      context->RSGetState(rasterizer_state.put());
      D3D11_RASTERIZER_DESC rasterizer_desc{};
      if (rasterizer_state)
         rasterizer_state->GetDesc(&rasterizer_desc);
      if (rasterizer_desc.ScissorEnable)
      {
         UINT count = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
         D3D11_RECT rects[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
         context->RSGetScissorRects(&count, rects);
         for (UINT i = 0; i < count; ++i)
         {
            rects[i].left = static_cast<LONG>(std::floor(rects[i].left * scale_x));
            rects[i].top = static_cast<LONG>(std::floor(rects[i].top * scale_y));
            rects[i].right = static_cast<LONG>(std::ceil(rects[i].right * scale_x));
            rects[i].bottom = static_cast<LONG>(std::ceil(rects[i].bottom * scale_y));
         }
         if (count > 0)
            context->RSSetScissorRects(count, rects);
         cmd_list_data.scissors_scaled = true;
      }
   }
}
