#include "../../Core/core.hpp"
#include "cbuffers.h"
#include "hooks.hpp"
#include "common.hpp"

namespace
{
   uintptr_t ResolveCurrentCamera()
   {
      if (g_resolved_addresses.camera_global != 0)
         return g_resolved_addresses.camera_global;

      const uintptr_t camera_table = g_resolved_addresses.camera_table;
      const uintptr_t camera_index = g_resolved_addresses.camera_index;
      if (camera_table == 0 || camera_index == 0)
         return 0;

      __try
      {
         const uint32_t index = *reinterpret_cast<const uint32_t*>(camera_index);
         if (index > 0x0B)
            return 0;

         return *reinterpret_cast<const uintptr_t*>(camera_table + index * sizeof(uintptr_t));
      }
      __except (EXCEPTION_EXECUTE_HANDLER)
      {
         return 0;
      }
   }
} // namespace

bool TryReadCameraJitter(float2& out_jitter)
{
   const uintptr_t camera = ResolveCurrentCamera();
   if (camera == 0)
      return false;

   __try
   {
      const uintptr_t projection_ptr = *reinterpret_cast<const uintptr_t*>(camera + kCameraProjectionDataOffset);
      if (projection_ptr == 0)
         return false;

      out_jitter.x = *reinterpret_cast<const float*>(projection_ptr + kProjectionJitterXOffset);
      out_jitter.y = *reinterpret_cast<const float*>(projection_ptr + kProjectionJitterYOffset);
      return true;
   }
   __except (EXCEPTION_EXECUTE_HANDLER)
   {
      return false;
   }
}

void OnJitterWrite(safetyhook::Context& ctx)
{
   g_hook_globals.table_jitter_x_bits.store(static_cast<uint32_t>(ctx.rcx), std::memory_order_release);
   g_hook_globals.table_jitter_y_bits.store(static_cast<uint32_t>(ctx.rax), std::memory_order_release);
   g_hook_globals.table_jitter_valid.store(true, std::memory_order_release);
#ifdef PATCH_JITTER_TABLE_INIT
   // ctx.rsi = TemporalAntiAliasingComponent* (this); jitter_phase_index at +0x24 is written by
   // 'mov [rsi+24h], cl' at 0x141A9EB77, four instructions before this hook fires at kJitterWrite_RVA.
   // This is definitively the index used to look up the table entry that was just written to the camera.
   const auto phase = *reinterpret_cast<const uint8_t*>(ctx.rsi + kTAAJitterPhaseIndexOffset);
   g_hook_globals.cached_jitter_phase_idx.store(phase, std::memory_order_release);
#endif
}

bool TryReadTableJitter(float2& out_jitter)
{
   if (!g_hook_globals.table_jitter_valid.load(std::memory_order_acquire))
      return false;
   const uint32_t x_bits = g_hook_globals.table_jitter_x_bits.load(std::memory_order_relaxed);
   const uint32_t y_bits = g_hook_globals.table_jitter_y_bits.load(std::memory_order_relaxed);
   std::memcpy(&out_jitter.x, &x_bits, sizeof(float));
   std::memcpy(&out_jitter.y, &y_bits, sizeof(float));
   return true;
}

#ifdef PATCH_JITTER_TABLE_INIT
constexpr std::array<float2, JITTER_PHASES> precomputed_jitters = []()
{
   std::array<float2, JITTER_PHASES> entries{};
   for (unsigned int i = 0; i < entries.size(); i++)
      entries[i] = float2{SR::HaltonSequence(i, 2), SR::HaltonSequence(i, 3)};
   return entries;
}();

bool TryReadTableJitterFromCounter(float2& out_jitter)
{
   // Use the phase index cached by OnJitterWrite rather than g_frame_counter.
   // g_frame_counter is incremented by GBFR_CameraProjectionData_BulkUpdate_Caller on the
   // game-logic thread (lock inc @ 0x14019F6AA) one frame ahead of the render thread, causing
   // an off-by-one. cached_jitter_phase_idx is captured from ctx.rsi+0x24 at the exact
   // moment the camera write fires — always in sync regardless of TAA component lifecycle.
   if (!g_hook_globals.table_jitter_valid.load(std::memory_order_acquire))
      return false;
   const uint8_t phase_idx = g_hook_globals.cached_jitter_phase_idx.load(std::memory_order_acquire);
   out_jitter = precomputed_jitters[phase_idx % JITTER_PHASES];
   return true;
}

static void __fastcall Hooked_TemporalAntiAliasingComponentInit(void* self)
{
   g_taa_init_hook.call<void>(self);
   auto* table = reinterpret_cast<float2*>(reinterpret_cast<uint8_t*>(self) + kTAAJitterTableOffset);
   for (size_t i = 0; i < kTAAJitterTableCount; i++)
      table[i] = precomputed_jitters[i % JITTER_PHASES];
}
#endif

void PatchJitterPhases()
{
   static_assert((JITTER_PHASES & (JITTER_PHASES - 1)) == 0, "JITTER_PHASES must be a power of 2");
   static_assert(JITTER_PHASES >= 1 && JITTER_PHASES <= 64, "JITTER_PHASES must be between 1 and 64");

#ifndef PATCH_JITTER_TABLE_INIT
   constexpr uint8_t mask = static_cast<uint8_t>(JITTER_PHASES - 1);
   const uintptr_t patch_addrs[2] = {
      ResolveGBFRDataOrFallback(g_resolved_addresses.jitter_phase_mask_cl_imm, kJitterPhaseMask_CL_RVA),
      ResolveGBFRDataOrFallback(g_resolved_addresses.jitter_phase_mask_eax_imm, kJitterPhaseMask_EAX_RVA),
   };
   for (uintptr_t addr : patch_addrs)
   {
      if (addr == 0)
         continue;
      auto* byte_ptr = reinterpret_cast<uint8_t*>(addr);
      DWORD old_protect;
      VirtualProtect(byte_ptr, 1, PAGE_EXECUTE_READWRITE, &old_protect);
      *byte_ptr = mask;
      VirtualProtect(byte_ptr, 1, old_protect, &old_protect);
   }
#endif
}

bool IsTAARunningThisFrame()
{
   // During pause/unpause transitions the settings object can be rebuilt temporarily.
   // Keep and return the last known-good value when reads are transiently unavailable.
   static std::atomic<bool> s_last_taa_running{false};

   const bool last_known = s_last_taa_running.load(std::memory_order_acquire);
   const uintptr_t settings_ptr_addr = ResolveGBFRDataOrFallback(
      g_resolved_addresses.taa_settings_global,
      kTAASettingsGlobal_RVA);
   if (settings_ptr_addr == 0)
      return last_known;

   __try
   {
      const uintptr_t settings_obj = *reinterpret_cast<const uintptr_t*>(settings_ptr_addr);
      if (settings_obj == 0)
         return last_known;

      const bool taa_running = (*reinterpret_cast<const uint8_t*>(settings_obj + 22) & 1) != 0;
      s_last_taa_running.store(taa_running, std::memory_order_release);
      return taa_running;
   }
   __except (EXCEPTION_EXECUTE_HANDLER)
   {
      return last_known;
   }
}

void* GetVTableFunction(void* obj, size_t index)
{
   void** vtable = *reinterpret_cast<void***>(obj);
   return vtable[index];
}

// GBFR_InitializeDX11RenderingPipeline is called every frame from a single caller.
// It has a dimension cache (g_rtDimensionCache) that gates RT recreation — cache is
// written with the incoming args before RT creation, so substituting args here means
// the render dims are cached automatically and RT recreation only fires on dim change.
//
// screen_width/screen_height are read by the caller from g_outputWidth/g_outputHeight,
// so they always carry the current output resolution. We substitute render-scaled dims
// into the trampoline call and write them to g_renderWidth/g_renderHeight — the globals
// the frame graph reads to decide whether the temporal upscale path runs.
// g_outputWidth/g_outputHeight are never modified.
static char __fastcall Hooked_InitializeDX11RenderingPipeline(int screen_width, int screen_height)
{
   int render_w = screen_width;
   int render_h = screen_height;
   uintptr_t render_w_addr = 0;
   uintptr_t render_h_addr = 0;

   DeviceData* device_data = g_device_data_ptr.load(std::memory_order_acquire);
   if (device_data && screen_width > 0 && screen_height > 0)
   {
      // screen_width/height ARE the output dims — keep output_resolution current every frame.
      device_data->output_resolution.x = static_cast<float>(screen_width);
      device_data->output_resolution.y = static_cast<float>(screen_height);

      const float scale = render_scale;
      const double aspect_ratio = static_cast<double>(screen_width) / screen_height;
      auto render_dims = Math::FindClosestIntegerResolutionForAspectRatio(
         screen_width * static_cast<double>(scale),
         screen_height * static_cast<double>(scale),
         aspect_ratio);
      device_data->render_resolution.x = static_cast<float>(render_dims[0]);
      device_data->render_resolution.y = static_cast<float>(render_dims[1]);

      render_w = static_cast<int>((std::max)(1u, render_dims[0]));
      render_h = static_cast<int>((std::max)(1u, render_dims[1]));

      // Resolve these now, but write them after the trampoline below.
      render_w_addr = ResolveGBFRDataOrFallback(
         g_resolved_addresses.render_width,
         kRenderWidth_RVA);
      render_h_addr = ResolveGBFRDataOrFallback(
         g_resolved_addresses.render_height,
         kRenderHeight_RVA);
   }

   // Pass render dims to the game — g_outputWidth/g_outputHeight are not touched.
   const char result = g_rt_creation_hook.unsafe_call<char>(render_w, render_h);

   // Sync after the game's RT recreation path sees the new dimensions. Pre-writing these globals
   // can hide hot render scale changes from the game's size cache, making them restart-only.
   if (render_w_addr != 0 && render_h_addr != 0)
   {
      *reinterpret_cast<int*>(render_w_addr) = render_w;
      *reinterpret_cast<int*>(render_h_addr) = render_h;
   }

   return result;
}

// Not hooked. Hooked_InitializeDX11RenderingPipeline runs every frame and receives
// screen_width/screen_height directly from g_outputWidth/g_outputHeight, so it always
// has the current output dims without needing to intercept the resolution-change path.
__int64 __fastcall Hooked_UpdateScreenResolution(__int64 a1)
{
   return g_update_screen_resolution_hook.unsafe_call<__int64>(a1);
}

// Called every frame by the UI render orchestrator (sub_143222A10).
// Records a1 (the UI state object pointer) so Hooked_DispatchRenderPassViewport can
// identify which GBFR_DispatchRenderPassViewport calls originate from the UI pipeline.
void OnUIRenderOrchestratorEntry(safetyhook::Context& ctx)
{
   g_hook_globals.ui_render_ctx.store(ctx.rcx, std::memory_order_relaxed);
}

// GBFR_DispatchRenderPassViewport is the single chokepoint for all RSSetViewports calls.
// When DLSS render-scale is active, all render targets are sized at render dims (e.g.
// 2880x1620 at 75%), including UI composition targets that should be at full output dims
// (3840x2160) so the UI fills the screen.
//
// Detection: the UI render orchestrator (sub_143222A10) always passes the same state object
// pointer as rcx through to GBFR_DispatchRenderPassViewport. We track that pointer via
// OnUIRenderOrchestratorEntry and override passDesc width/height to output dims when:
//   - rcx == the recorded UI state object (this call originates from the UI pipeline), AND
//   - the current dims match render dims (the RT was created at render scale, not output).
//
// Blur/convolution intermediates at other dimensions (e.g. 1920x1080) pass through unchanged.
__int64 __fastcall Hooked_DispatchRenderPassViewport(__int64 render_ctx, __int64 pass_desc_ptr)
{
   const uintptr_t ui_ctx = g_hook_globals.ui_render_ctx.load(std::memory_order_relaxed);
   if (ui_ctx != 0 && render_ctx == static_cast<__int64>(ui_ctx))
   {
      DeviceData* device_data = g_device_data_ptr.load(std::memory_order_acquire);
      if (device_data)
      {
         int* dims = reinterpret_cast<int*>(pass_desc_ptr + 0x50);
         const int render_w = static_cast<int>(device_data->render_resolution.x);
         const int render_h = static_cast<int>(device_data->render_resolution.y);
         if (dims[0] == render_w && dims[1] == render_h)
         {
            dims[0] = static_cast<int>(device_data->output_resolution.x);
            dims[1] = static_cast<int>(device_data->output_resolution.y);
         }
      }
   }
   return g_dispatch_viewport_hook.unsafe_call<__int64>(render_ctx, pass_desc_ptr);
}

void PatchSceneBufferInHook(
   ID3D11DeviceContext1* pContext,
   ID3D11Buffer* pBuffer,
   UINT firstConstant,
   UINT numConstants)
{
   DeviceData* device_data = g_device_data_ptr.load(std::memory_order_acquire);
   ID3D11Device* native_device = g_native_device_ptr.load(std::memory_order_acquire);
   if (!device_data || !native_device)
   {
      ASSERT_ONCE_MSG(false, "PatchSceneBufferInHook: device_data or native_device null");
      return;
   }

   auto& game_device_data = *static_cast<GameDeviceDataGBFR*>(device_data->game);

   constexpr UINT scene_buffer_size = sizeof(cbSceneBuffer);
   const UINT scene_buffer_constants = scene_buffer_size / 16;
   if (numConstants < scene_buffer_constants)
   {
      ASSERT_ONCE_MSG(false, "PatchSceneBufferInHook: numConstants too small");
      return;
   }

   auto it = device_data->native_compute_shaders.find(CompileTimeStringHash("GBFR Patch SceneBuffer"));
   if (it == device_data->native_compute_shaders.end() || !it->second)
   {
      ASSERT_ONCE_MSG(false, "PatchSceneBufferInHook: compute shader not found");
      return;
   }

   if (!game_device_data.scratch_scene_buffer || !game_device_data.scratch_scene_buffer_uav)
   {
      ASSERT_ONCE_MSG(false, "PatchSceneBufferInHook: scratch buffer or UAV missing");
      return;
   }

   DrawStateStack<DrawStateStackType::Compute> compute_state_stack;
   compute_state_stack.Cache(pContext, device_data->uav_max_count);

   if (device_data->luma_instance_data)
   {
      ID3D11Buffer* luma_cbs[] = {device_data->luma_instance_data.get()};
      pContext->CSSetConstantBuffers(8, 1, luma_cbs);
   }

   {
      ID3D11Buffer* cbs[] = {pBuffer};
      UINT firsts[] = {firstConstant};
      UINT counts[] = {numConstants};
      pContext->CSSetConstantBuffers1(0, 1, cbs, firsts, counts);
   }

   ID3D11UnorderedAccessView* uavs[] = {game_device_data.scratch_scene_buffer_uav.get()};
   pContext->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);

   pContext->CSSetShader(it->second.get(), nullptr, 0);
   pContext->Dispatch(1, 1, 1);

   ID3D11UnorderedAccessView* null_uavs[] = {nullptr};
   pContext->CSSetUnorderedAccessViews(0, 1, null_uavs, nullptr);

   D3D11_BOX src_box = {};
   src_box.left = 0;
   src_box.right = scene_buffer_size;
   src_box.top = 0;
   src_box.bottom = 1;
   src_box.front = 0;
   src_box.back = 1;
   pContext->CopySubresourceRegion(
      pBuffer,
      0,
      firstConstant * 16,
      0,
      0,
      game_device_data.scratch_scene_buffer.get(),
      0,
      &src_box);

   compute_state_stack.Restore(pContext);
}

void STDMETHODCALLTYPE Hooked_VSSetConstantBuffers1_Immediate(
   ID3D11DeviceContext1* pContext,
   UINT StartSlot,
   UINT NumBuffers,
   ID3D11Buffer* const* ppConstantBuffers,
   const UINT* pFirstConstant,
   const UINT* pNumConstants)
{
   g_VSSetConstantBuffers1_hook_immediate.unsafe_call<void>(
      pContext, StartSlot, NumBuffers, ppConstantBuffers, pFirstConstant, pNumConstants);
}

void STDMETHODCALLTYPE Hooked_VSSetConstantBuffers1_Deferred(
   ID3D11DeviceContext1* pContext,
   UINT StartSlot,
   UINT NumBuffers,
   ID3D11Buffer* const* ppConstantBuffers,
   const UINT* pFirstConstant,
   const UINT* pNumConstants)
{
   g_VSSetConstantBuffers1_hook_deferred.unsafe_call<void>(
      pContext, StartSlot, NumBuffers, ppConstantBuffers, pFirstConstant, pNumConstants);
}
