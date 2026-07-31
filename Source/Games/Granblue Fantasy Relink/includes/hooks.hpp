#pragma once

#include "safetyhook.hpp"

struct GBFRResolvedAddresses
{
   void* initialize_dx11_rendering_pipeline = nullptr;
   void* dispatch_render_pass_viewport = nullptr;
   void* ui_render_orchestrator = nullptr;
   void* jitter_write_site = nullptr;
#ifdef PATCH_JITTER_TABLE_INIT
   void* temporal_aa_component_init = nullptr;
#endif

   uintptr_t output_width = 0;
   uintptr_t output_height = 0;
   uintptr_t render_width = 0;
   uintptr_t render_height = 0;

   // NEW binary: CameraIndex + CameraTable mechanism
   uintptr_t camera_index = 0;
   uintptr_t camera_table = 0;
   // OLD binary (V1_3_2): CameraGlobal pointer
#ifdef V1_3_2
   uintptr_t camera_global = 0;
#endif
   uintptr_t taa_settings_global = 0;
   uintptr_t jitter_phase_counter = 0;
   uintptr_t jitter_phase_mask_cl_imm = 0;
   uintptr_t jitter_phase_mask_eax_imm = 0;
};

struct GBFRHookGlobals
{
   SafetyHookInline rt_creation_hook;
   SafetyHookInline update_screen_resolution_hook;
   SafetyHookInline vs_set_constant_buffers1_hook_immediate;
   SafetyHookInline vs_set_constant_buffers1_hook_deferred;
   SafetyHookInline dispatch_viewport_hook;
   safetyhook::MidHook jitter_write_hook;
   safetyhook::MidHook ui_orchestrator_hook;
#ifdef PATCH_JITTER_TABLE_INIT
   SafetyHookInline taa_init_hook;
#endif

   std::atomic<DeviceData*> device_data_ptr = nullptr;
   std::atomic<ID3D11Device*> native_device_ptr = nullptr;
   std::atomic<uint32_t> table_jitter_x_bits{0};
   std::atomic<uint32_t> table_jitter_y_bits{0};
   std::atomic<bool> table_jitter_valid{false};
#ifdef PATCH_JITTER_TABLE_INIT
   // Cached by OnJitterWrite from ctx.rsi+kTAAJitterPhaseIndexOffset (TemporalAntiAliasingComponent::jitter_phase_index).
   // Written atomically at the same moment the camera receives its jitter — always in sync, no global pointer dependency.
   std::atomic<uint8_t> cached_jitter_phase_idx{0};
#endif
   // UI render orchestrator (sub_143222A10) first arg — the UI state object pointer.
   // Updated every frame via mid-hook; used by Hooked_DispatchRenderPassViewport to
   // identify which GBFR_DispatchRenderPassViewport calls originate from the UI pipeline.
   std::atomic<uintptr_t> ui_render_ctx{0};
};

// Current binary (v2.0.3) RVAs — default build
constexpr size_t kVSSetConstantBuffers1_VTableIndex = 119;
constexpr uintptr_t kInitializeDX11RenderingPipeline_RVA = 0x007F3480;
constexpr uintptr_t kUpdateScreenResolution_RVA = 0x005F7960;
constexpr uintptr_t kDispatchRenderPassViewport_RVA = 0x022CAB70; // GBFR_DispatchRenderPassViewport
constexpr uintptr_t kUIRenderOrchestrator_RVA = 0x03222A10;       // sub_143222A10 — UI pipeline entry
constexpr uintptr_t kOutputWidth_RVA = 0x06B81060;                // g_outputWidth  — 1920 (0x0780)
constexpr uintptr_t kOutputHeight_RVA = 0x06B81064;               // g_outputHeight — 1080 (0x0438)
constexpr uintptr_t kRenderWidth_RVA = 0x06B81058;                // g_renderWidth  — 1920
constexpr uintptr_t kRenderHeight_RVA = 0x06B8105C;               // g_renderHeight — 1080
constexpr uintptr_t kCameraIndex_RVA = 0x0701E2E0;                // CameraIndex (new table-based mechanism)
constexpr uintptr_t kCameraTable_RVA = 0x054BB3A0;                // CameraTable (array of camera pointers)
constexpr uintptr_t kCameraProjectionDataOffset = 0x60;
constexpr uintptr_t kProjectionJitterXOffset = 0x940;
constexpr uintptr_t kProjectionJitterYOffset = 0x944;
constexpr uintptr_t kTAASettingsGlobal_RVA = 0x0702FDA0;
constexpr uintptr_t kJitterPhaseCounter_RVA = 0x0703C430;
constexpr uintptr_t kJitterPhaseMask_CL_RVA = 0x0215FDB8;   // JitterWrite - 0x25
constexpr uintptr_t kJitterPhaseMask_EAX_RVA = 0x0215FDBE;   // JitterWrite - 0x1F
constexpr uintptr_t kJitterWrite_RVA = 0x0215FDDD;
constexpr uintptr_t kTemporalAntiAliasingComponent_Init_RVA = 0x0215F810;
constexpr uintptr_t kTAAJitterTableOffset = 0x28;
constexpr uintptr_t kTAAJitterPhaseIndexOffset = 0x24; // this->jitter_phase_index; written by Trans at same time as camera write (mov [rsi+24h], cl @ 0x14215FDB9)
constexpr size_t kTAAJitterTableCount = 64;

// Old binary (v1.3.2) RVAs — used when V1_3_2 is defined
#ifdef V1_3_2
constexpr uintptr_t kInitializeDX11RenderingPipeline_RVA = 0x007455C2;
constexpr uintptr_t kUpdateScreenResolution_RVA = 0x00593960;
constexpr uintptr_t kDispatchRenderPassViewport_RVA = 0x01BFF340;
constexpr uintptr_t kUIRenderOrchestrator_RVA = 0x03222A10;
constexpr uintptr_t kOutputWidth_RVA = 0x068B4090;
constexpr uintptr_t kOutputHeight_RVA = 0x068B4094;
constexpr uintptr_t kRenderWidth_RVA = 0x068B4088;
constexpr uintptr_t kRenderHeight_RVA = 0x068B408C;
constexpr uintptr_t kCameraGlobal_RVA = 0x068B4F90;               // CameraGlobal (old mechanism)
constexpr uintptr_t kTAASettingsGlobal_RVA = 0x06D32DE0;
constexpr uintptr_t kJitterPhaseCounter_RVA = 0x06D3F470;
constexpr uintptr_t kJitterPhaseMask_CL_RVA = 0x01A9EB76;
constexpr uintptr_t kJitterPhaseMask_EAX_RVA = 0x01A9EB7C;
constexpr uintptr_t kJitterWrite_RVA = 0x01A9EB6B;
constexpr uintptr_t kTemporalAntiAliasingComponent_Init_RVA = 0x01A9E5D0;
#endif

inline GBFRHookGlobals g_hook_globals;
inline auto& g_rt_creation_hook = g_hook_globals.rt_creation_hook;
inline auto& g_update_screen_resolution_hook = g_hook_globals.update_screen_resolution_hook;
inline auto& g_VSSetConstantBuffers1_hook_immediate = g_hook_globals.vs_set_constant_buffers1_hook_immediate;
inline auto& g_VSSetConstantBuffers1_hook_deferred = g_hook_globals.vs_set_constant_buffers1_hook_deferred;
inline auto& g_dispatch_viewport_hook = g_hook_globals.dispatch_viewport_hook;
inline auto& g_jitter_write_hook = g_hook_globals.jitter_write_hook;
inline auto& g_ui_orchestrator_hook = g_hook_globals.ui_orchestrator_hook;
#ifdef PATCH_JITTER_TABLE_INIT
inline auto& g_taa_init_hook = g_hook_globals.taa_init_hook;
#endif
inline auto& g_device_data_ptr = g_hook_globals.device_data_ptr;
inline auto& g_native_device_ptr = g_hook_globals.native_device_ptr;
inline GBFRResolvedAddresses g_resolved_addresses;

bool ResolveGBFRAddresses();

bool TryReadCameraJitter(float2& out_jitter);
void OnJitterWrite(safetyhook::Context& ctx);
void OnUIRenderOrchestratorEntry(safetyhook::Context& ctx);
bool TryReadTableJitter(float2& out_jitter);
void PatchJitterPhases();
#ifdef PATCH_JITTER_TABLE_INIT
bool TryReadTableJitterFromCounter(float2& out_jitter);
void __fastcall Hooked_TemporalAntiAliasingComponentInit(void* self);
#endif
bool IsTAARunningThisFrame();
void* GetVTableFunction(void* obj, size_t index);

char __fastcall Hooked_InitializeDX11RenderingPipeline(int screen_width, int screen_height);
__int64 __fastcall Hooked_DispatchRenderPassViewport(__int64 render_ctx, __int64 pass_desc_ptr);
__int64 __fastcall Hooked_UpdateScreenResolution(__int64 a1);
void STDMETHODCALLTYPE Hooked_VSSetConstantBuffers1_Immediate(
   ID3D11DeviceContext1* pContext,
   UINT StartSlot,
   UINT NumBuffers,
   ID3D11Buffer* const* ppConstantBuffers,
   const UINT* pFirstConstant,
   const UINT* pNumConstants);
void STDMETHODCALLTYPE Hooked_VSSetConstantBuffers1_Deferred(
   ID3D11DeviceContext1* pContext,
   UINT StartSlot,
   UINT NumBuffers,
   ID3D11Buffer* const* ppConstantBuffers,
   const UINT* pFirstConstant,
   const UINT* pNumConstants);
void PatchSceneBufferInHook(
   ID3D11DeviceContext1* pContext,
   ID3D11Buffer* pBuffer,
   UINT firstConstant,
   UINT numConstants);
