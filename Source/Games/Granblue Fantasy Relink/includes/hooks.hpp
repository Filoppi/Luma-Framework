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
   uintptr_t camera_global = 0;
   uintptr_t camera_table = 0;
   uintptr_t camera_index = 0;
   uintptr_t taa_settings_global = 0;
   uintptr_t jitter_phase_counter = 0;
   uintptr_t jitter_phase_mask_cl_imm = 0;
   uintptr_t jitter_phase_mask_eax_imm = 0;

   bool resolve_attempted = false;
   bool ready = false;
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

constexpr size_t kVSSetConstantBuffers1_VTableIndex = 119;
constexpr uintptr_t kInitializeDX11RenderingPipeline_RVA = 0x007F9E10;
constexpr uintptr_t kUpdateScreenResolution_RVA = 0x005F7960;
constexpr uintptr_t kDispatchRenderPassViewport_RVA = 0x022D0500;
constexpr uintptr_t kUIRenderOrchestrator_RVA = 0x03222A10;
constexpr uintptr_t kOutputWidth_RVA = 0x06B84090;
constexpr uintptr_t kOutputHeight_RVA = 0x06B84094;
constexpr uintptr_t kRenderWidth_RVA = 0x06B84088;
constexpr uintptr_t kRenderHeight_RVA = 0x06B8408C;
constexpr uintptr_t kCameraGlobal_RVA = 0x00000000;
constexpr uintptr_t kCameraTable_RVA = 0x054BF400;
constexpr uintptr_t kCameraIndex_RVA = 0x07021320;
constexpr uintptr_t kCameraProjectionDataOffset = 0x60;
constexpr uintptr_t kProjectionJitterXOffset = 0x940;
constexpr uintptr_t kProjectionJitterYOffset = 0x944;
constexpr uintptr_t kTAASettingsGlobal_RVA = 0x07032DE0;
constexpr uintptr_t kJitterPhaseCounter_RVA = 0x0703F470;
constexpr uintptr_t kJitterPhaseMask_CL_RVA = 0x02165808;
constexpr uintptr_t kJitterPhaseMask_EAX_RVA = 0x0216580E;
constexpr uintptr_t kJitterWrite_RVA = 0x0216582D;
constexpr uintptr_t kTemporalAntiAliasingComponent_Init_RVA = 0x02165260;
constexpr uintptr_t kTAAJitterTableOffset = 0x28;
constexpr uintptr_t kTAAJitterPhaseIndexOffset = 0x24; // this->jitter_phase_index; written by Trans at same time as camera write (mov [rsi+24h], cl @ 0x141A9EB77)
constexpr size_t kTAAJitterTableCount = 64;

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
uintptr_t ResolveGBFRDataOrFallback(uintptr_t resolved_absolute, uintptr_t fallback_rva);
void* ResolveGBFRCodeOrFallback(void* resolved_absolute, uintptr_t fallback_rva);

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
