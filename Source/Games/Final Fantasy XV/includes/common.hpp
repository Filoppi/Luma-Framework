#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <numbers>
#include <unordered_map>
#include "native_cbuffers.hpp"
#include "upscale_tracking.hpp"
#include "rain.hpp"

struct GameDeviceDataFFXV final : public GameDeviceData
{
#if ENABLE_SR
   // SR - Resources extracted from TAA pass (may be reused by game)
   ComPtr<ID3D11Resource> sr_motion_vectors;
   ComPtr<ID3D11Resource> sr_source_color;
   ComPtr<ID3D11Resource> depth_buffer;
   ComPtr<ID3D11UnorderedAccessView> sr_motion_vectors_uav;
   ComPtr<ID3D11Texture2D> sr_depth_backup;
   ComPtr<ID3D11ShaderResourceView> sr_output_srv;
#endif // ENABLE_SR
   std::atomic<bool> has_drawn_upscaling = false;
   std::atomic<bool> has_drawn_tonemap = false;
   std::atomic<bool> found_taa_cb = false;
   std::atomic<bool> found_per_view_globals = false;
   std::unique_ptr<cbTemporalAA> taa_cb_data;
   float dlss_pre_exposure_raw = 1.f;
   float dlss_pre_exposure_smoothed = 1.f;
   float dlss_pre_exposure_smoothing = 0.15f;
   bool has_dlss_pre_exposure_history = false;
   ComPtr<ID3D11Buffer> cb_taa_buffer;
   void* cb_taa_buffer_map_data = nullptr;

   // Cached view buffer (once found)
   ID3D11Buffer* cached_view_buffer = nullptr;

   // Extracted camera data
   float camera_fov = 60.0f * std::numbers::pi_v<float> / 180.0f;
   float camera_near = 0.1f;
   float camera_far = 1000.0f;

   bool has_processed_view_buffer = false;

   float2 taa_jitters = {0, 0};
   float2 taa_jitter_uv = {};
   float2 projection_jitters = {0, 0};

   // Post-TAA upscale tracking
   UpscaleTrackingState upscale_tracking;

   DrawStateStack<DrawStateStackType::FullGraphics> taa_state;

   FFXVRainLayer rain;

   // VBAO foliage mask: the G-buffer depth stencil (found at the linear depth pass, valid for this frame), its stencil view (cached while the resource stays the same),
   // and the mask texture at the AO resolution (R8_UINT, 1 = foliage)
   ComPtr<ID3D11Resource> gbuffer_depth_stencil;
   ComPtr<ID3D11ShaderResourceView> stencil_srv;
   const ID3D11Resource* stencil_srv_source = nullptr; // The stencil view keeps its resource alive, so the address can't be reused while it's cached
   ComPtr<ID3D11Texture2D> foliage_mask;
   ComPtr<ID3D11ShaderResourceView> foliage_mask_srv;
   ComPtr<ID3D11UnorderedAccessView> foliage_mask_uav;
   bool foliage_mask_failed_logged = false;

   // Foliage mip bias: per depth stencil state (immutable, kept alive so its address can't be reused) whether it's a G-buffer stencil write,
   // and per sampler Luma bound (anisotropic, with the SR mip bias) its copy with the foliage bias correction.
   std::mutex foliage_mip_bias_mutex;
   std::unordered_map<const ID3D11DepthStencilState*, std::pair<ComPtr<ID3D11DepthStencilState>, bool>> gbuffer_stencil_write_states;
   std::unordered_map<const ID3D11SamplerState*, std::pair<ComPtr<ID3D11SamplerState>, ComPtr<ID3D11SamplerState>>> foliage_samplers; // Source (kept alive), biased copy
   float foliage_samplers_mip_lod_bias_offset = 0.f; // The SR mip bias the sampler copies were built with

   void ResetPerFrameData()
   {
      gbuffer_depth_stencil = nullptr;

      // Clear per-frame source links while keeping persistent pooled allocations.
      upscale_tracking.ResetFrame();
      rain.ResetFrame();

      has_drawn_upscaling = false;
      has_drawn_tonemap = false;
      taa_state.state.reset();
      found_taa_cb = false;
      has_processed_view_buffer = false;
      found_per_view_globals = false;

   }
};
