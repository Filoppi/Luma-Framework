#pragma once

// Core's current header defines translation-unit-local state. This is its SR-facing interface;
// Upscaling.cpp is included once by main.cpp, not compiled as a separate Core consumer.
#include "shared.h"
#include "NativeInputs.hpp"

namespace QuantumBreakUpscaling
{
   struct ShaderPass
   {
      uint32_t hash;
      reshade::api::shader_stage stage;
   };

   inline constexpr ShaderPass depth_linearization_pass{0xA43343D6u, reshade::api::shader_stage::pixel};
   inline constexpr ShaderPass history_reprojection_pass{0xE8337D48u, reshade::api::shader_stage::compute};
   inline constexpr ShaderPass temporal_resolve_pass{0x99274617u, reshade::api::shader_stage::pixel};

#if ENABLE_SR && (DEVELOPMENT || TEST)
   struct NativeInputUsage
   {
      std::array<std::atomic<uint64_t>, static_cast<size_t>(NativeInputFallback::Count)> selections = {};
      std::atomic<uint64_t> native_draws = 0u;
      std::atomic<uint64_t> native_successes = 0u;
      std::atomic<uint64_t> taa_resolves = 0u;
      std::atomic<uint64_t> last_report_at = 0u;
      std::atomic<uint64_t> last_total = 0u;
      // Metadata-only transition coverage, not another input capture path.
      std::array<std::atomic<uint64_t>, 4> binding_states = {};
      std::atomic<uint64_t> native_missing_bindings = 0u;
      std::atomic<uint64_t> transition_skips = 0u;
      std::array<std::atomic<uint8_t>, static_cast<size_t>(NativeInputFallback::Count)> reported_gates = {};
   };

   // Display only: these stored values are never read by native eligibility or the SR backend.
   struct NativeInputSnapshot
   {
      NativeSceneCamera camera;
      float jitter_x = 0.f;
      float jitter_y = 0.f;
      float pixel_jitter_x = 0.f;
      float pixel_jitter_y = 0.f;
      NativeInputFallback reason = NativeInputFallback::NoSceneScope;
   };
#endif

   struct Data
   {
#if ENABLE_SR
      // Per-frame clip/device depth from the pre-linearization input, and geometry velocity.
      com_ptr<ID3D11Resource> sr_clip_depth;
      com_ptr<ID3D11Resource> sr_motion_vectors;
#if DEVELOPMENT || TEST
      NativeInputUsage native_input_usage;
      NativeInputSnapshot debug_inputs;
      SRRenderSize debug_render_size;
      SRRenderSize debug_output_size;
      bool debug_used_cached_clip_depth = false;
      bool debug_prev_had_motion_vectors = false;
      bool debug_prev_had_clip_depth = false;
      bool debug_prev_used_cached_clip_depth = false;
#endif
      // Gamma-to-linear conversion scratch and SR output view.
      com_ptr<ID3D11Texture2D> sr_linear_input_color;
      com_ptr<ID3D11ShaderResourceView> sr_linear_input_color_srv;
      com_ptr<ID3D11RenderTargetView> sr_linear_input_color_rtv;
      com_ptr<ID3D11ShaderResourceView> sr_output_color_srv;

      // Texture shape history controls SR resets; width/height changes are included.
      bool has_previous_source_desc = false;
      bool has_previous_depth_desc = false;
      bool has_previous_motion_vectors_desc = false;
      D3D11_TEXTURE2D_DESC sr_clip_depth_desc = {};
      D3D11_TEXTURE2D_DESC previous_source_desc = {};
      D3D11_TEXTURE2D_DESC previous_depth_desc = {};
      D3D11_TEXTURE2D_DESC previous_motion_vectors_desc = {};
      uint32_t dlss_scene_warmup_remaining = 0u;
      SR::Type previous_sr_type = SR::Type::None;
#endif
   };

   struct TemporalResolveResult
   {
      bool requested = false;
      bool succeeded = false;
      bool stop_processing = false;
   };

   void RegisterShaderHashes();
   bool MatchesPass(const ShaderHashesList<OneShaderPerPipeline>& hashes, ShaderPass pass);
   void OnInit();
   bool IsRequested(const DeviceData& device_data);
   // Device init runs after Core has selected the supported SR backend, outside addon DllMain.
   void OnInitDevice(DeviceData& device_data);
   void PrepareNativeInputs(ID3D11DeviceContext* context, const DeviceData& device_data);
   void CaptureClipDepthFromLinearizationPass(ID3D11DeviceContext* context, Data& data);
   void CaptureMotionVectors(ID3D11DeviceContext* context, Data& data);
   bool IsTemporalReplacementActive(ID3D11DeviceContext* context, const CommandListData& cmd_list_data, const DeviceData& device_data);
   // Executes SR before the original temporal resolve; callers honor stop_processing.
   TemporalResolveResult RunTemporalResolve(ID3D11Device* device, ID3D11DeviceContext* context, DeviceData& device_data,
      Data& data, bool temporal_replacement_active);
   void SetSRTypeForTemporalResolve(DeviceData& device_data, bool succeeded);
   void ForceResetIfRequestedAndFailed(DeviceData& device_data, const TemporalResolveResult& result);
   void BindOutputToTemporalResolve(ID3D11DeviceContext* context, Data& data, bool succeeded);
   void CleanResources(DeviceData& device_data, Data& data);
   void OnDestroyDevice(Data* data);
   void OnPresent(DeviceData& device_data, Data& data);
   void DrawSettings(DeviceData& device_data);
   // Development/Test status only; no GPU reference capture is performed here.
   void DrawDebug(const Data& data, bool saw_history_reprojection_pass, bool saw_temporal_resolve_pass,
      bool had_scene_temporal_resolve_last_frame, uint32_t ui_only_frame_hold_counter);
} // namespace QuantumBreakUpscaling