#pragma once

#include <atomic>
#include <memory>
#include <numbers>
#include "log.hpp"
#include "native_cbuffers.hpp"
#include "upscale_tracking.hpp"

struct GameDeviceDataFFXV final : public GameDeviceData
{
#if ENABLE_SR
   // SR - Resources extracted from TAA pass (may be reused by game)
   ComPtr<ID3D11Resource> sr_motion_vectors;
   ComPtr<ID3D11Resource> sr_source_color;
   ComPtr<ID3D11Resource> depth_buffer;
   ComPtr<ID3D11UnorderedAccessView> sr_motion_vectors_uav;
   ComPtr<ID3D11Texture2D> exposure_texture;
   ComPtr<ID3D11Texture2D> sr_depth_backup;

#endif // ENABLE_SR
   std::atomic<bool> has_drawn_upscaling = false;
   std::atomic<bool> has_drawn_autoexposure = false;
   std::atomic<bool> found_taa_cb = false;
   std::atomic<bool> found_per_view_globals = false;
   std::atomic<bool> use_exposure_texture = false;
   std::unique_ptr<cbTemporalAA> taa_cb_data;

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
   float2 projection_jitters = {0, 0};

   // Post-TAA upscale tracking
   UpscaleTrackingState upscale_tracking;
#if DEVELOPMENT || TEST
   uint32_t dbg_replaced_srvs = 0;
   uint32_t dbg_replaced_rtvs = 0;
   uint32_t dbg_replaced_viewports = 0;
   uint32_t dbg_replaced_scissors = 0;
   // When true, log unmodified viewport/scissor state for intermediate passes between TAA and Upscale
   bool dbg_log_baseline_state = false;
#endif
};
