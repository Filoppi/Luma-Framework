#include "..\..\Core\core.hpp"
#include "cbuffers.h"
#include "hooks.hpp"
#include "common.hpp"

bool ResolveGBFRAddresses()
{
   const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
   if (base == 0)
      return false;

   // Hook/function targets (code addresses)
   g_resolved_addresses.initialize_dx11_rendering_pipeline = reinterpret_cast<void*>(base + kInitializeDX11RenderingPipeline_RVA);
   g_resolved_addresses.jitter_write_site = reinterpret_cast<void*>(base + kJitterWrite_RVA);
#ifdef PATCH_JITTER_TABLE_INIT
   g_resolved_addresses.temporal_aa_component_init = reinterpret_cast<void*>(base + kTemporalAntiAliasingComponent_Init_RVA);
#endif

   // Data addresses
   g_resolved_addresses.render_width = base + kRenderWidth_RVA;
   g_resolved_addresses.render_height = base + kRenderHeight_RVA;
   g_resolved_addresses.camera_index = base + kCameraIndex_RVA;
   g_resolved_addresses.camera_table = base + kCameraTable_RVA;
   g_resolved_addresses.taa_settings_global = base + kTAASettingsGlobal_RVA;
#ifdef V2_0_3
   g_resolved_addresses.taa_running_flag = base + kTAARunningFlag_RVA;
   g_resolved_addresses.taa_render_scale_flag_ptr = base + kTAARenderScaleFlagPointer_RVA;
#endif
   g_resolved_addresses.jitter_phase_counter = base + kJitterPhaseCounter_RVA;
#ifdef V1_3_2
   g_resolved_addresses.camera_global = base + kCameraGlobal_RVA;
#endif
   g_resolved_addresses.taa_reset_flag = base + kTAAResetFlag_RVA;

   return true;
}

bool TryReadCameraJitter(float2& out_jitter)
{
#ifdef V1_3_2
   // OLD binary: CameraGlobal mechanism
   if (g_resolved_addresses.camera_global != 0)
   {
      const uintptr_t camera = g_resolved_addresses.camera_global;
      const uintptr_t projection_ptr = *reinterpret_cast<const uintptr_t*>(camera + kCameraProjectionDataOffset);
      if (projection_ptr == 0)
         return false;

      out_jitter.x = *reinterpret_cast<const float*>(projection_ptr + kProjectionJitterXOffset);
      out_jitter.y = *reinterpret_cast<const float*>(projection_ptr + kProjectionJitterYOffset);
      return true;
   }
#else
   // NEW binary: CameraIndex + CameraTable mechanism
   if (g_resolved_addresses.camera_index != 0 && g_resolved_addresses.camera_table != 0)
   {
      const int camera_idx = *reinterpret_cast<const int*>(g_resolved_addresses.camera_index);
      if (camera_idx < 0 || camera_idx > 0xb)
         return false;

      const uintptr_t camera = *reinterpret_cast<const uintptr_t*>(
         g_resolved_addresses.camera_table + static_cast<size_t>(camera_idx) * 8);
      if (camera == 0)
         return false;

      const uintptr_t projection_ptr = *reinterpret_cast<const uintptr_t*>(camera + kCameraProjectionDataOffset);
      if (projection_ptr == 0)
         return false;

      out_jitter.x = *reinterpret_cast<const float*>(projection_ptr + kProjectionJitterXOffset);
      out_jitter.y = *reinterpret_cast<const float*>(projection_ptr + kProjectionJitterYOffset);
      return true;
   }
#endif
   return false;
}

void OnJitterWrite(safetyhook::Context& ctx)
{
#ifdef V2_0_3
   // v2.0.3: Jitter stored in TAA component table at [rcx + 8*(phase&0x3F) + 0x28]
   // ctx.rcx = TemporalAntiAliasingComponent*, phase counter is global
   const uint8_t phase = *reinterpret_cast<const uint8_t*>(g_resolved_addresses.jitter_phase_counter);
   const uintptr_t jit_addr = ctx.rcx + 8 * (phase & 0x3F) + 0x28;
   g_hook_globals.table_jitter_x_bits.store(
      *reinterpret_cast<const uint32_t*>(jit_addr), std::memory_order_release);
   g_hook_globals.table_jitter_y_bits.store(
      *reinterpret_cast<const uint32_t*>(jit_addr + 4), std::memory_order_release);
#else
   // v2.0.2/v1.3.2: Jitter written to camera projection, captured from registers
   g_hook_globals.table_jitter_x_bits.store(static_cast<uint32_t>(ctx.rcx), std::memory_order_release);
   g_hook_globals.table_jitter_y_bits.store(static_cast<uint32_t>(ctx.rax), std::memory_order_release);
#endif
   g_hook_globals.table_jitter_valid.store(true, std::memory_order_release);
#ifdef PATCH_JITTER_TABLE_INIT
   // Capture phase index for the init hook — source differs per version.
#ifdef V2_0_3
   const uint8_t phase_idx = *reinterpret_cast<const uint8_t*>(g_resolved_addresses.jitter_phase_counter);
#else
   const uint8_t phase_idx = *reinterpret_cast<const uint8_t*>(ctx.rsi + kTAAJitterPhaseIndexOffset);
#endif
   g_hook_globals.cached_jitter_phase_idx.store(phase_idx, std::memory_order_release);
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
   // Use phase index cached by OnJitterWrite to avoid off-by-one from g_frame_counter
   // (incremented on game-logic thread one frame ahead of render thread).
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
   // No-op when PATCH_JITTER_TABLE_INIT is defined — the init hook handles phase control.
   // When disabled, patches phase mask bytes in the game's jitter write function.
   static_assert((JITTER_PHASES & (JITTER_PHASES - 1)) == 0, "JITTER_PHASES must be a power of 2");
   static_assert(JITTER_PHASES >= 1 && JITTER_PHASES <= 64, "JITTER_PHASES must be between 1 and 64");

#ifndef PATCH_JITTER_TABLE_INIT
   constexpr uint8_t mask = static_cast<uint8_t>(JITTER_PHASES - 1);
   const uintptr_t patch_addrs[2] = {
      g_resolved_addresses.jitter_phase_mask_cl_imm,
      g_resolved_addresses.jitter_phase_mask_eax_imm,
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

#ifdef V2_0_3
   // v2.0.3: qword_147371338 (RVA 0x7371338) is a POINTER to the TAA running flag byte.
   // Verified in TemporalAntiAliasingComponent::trans (RVA 0x215F9C0):
   //   mov rax, cs:qword_147371338  (RVA 0x7371338) — load pointer
   //   cmp byte ptr [rax], 0        — read byte at target address
   //   cmp byte ptr [rax], 1        — if not 1, skip render scale adjustment
   // Requires double-dereference: load pointer, then read byte.
   const uintptr_t pointer_addr = g_resolved_addresses.taa_running_flag;
   if (pointer_addr == 0)
      return last_known;

   __try
   {
      const uintptr_t target_addr = *reinterpret_cast<const uintptr_t*>(pointer_addr);
      const bool taa_running = (*reinterpret_cast<const uint8_t*>(target_addr) & 1) != 0;
      s_last_taa_running.store(taa_running, std::memory_order_release);
      return taa_running;
   }
   __except (EXCEPTION_EXECUTE_HANDLER)
   {
      return last_known;
   }
#else
   // v2.0.2/v1.3.2: TAA running flag at offset 0x65 from settings object pointer.
   // Verified in 1.3.2 TemporalAntiAliasingComponent::trans (RVA 0x1A9E9D7):
   //   mov rax, cs:g_taa_settings_obj
   //   test byte ptr [rax+65h], 1
   const uintptr_t settings_ptr_addr = g_resolved_addresses.taa_settings_global;
   if (settings_ptr_addr == 0)
      return last_known;

   __try
   {
      const uintptr_t settings_obj = *reinterpret_cast<const uintptr_t*>(settings_ptr_addr);
      if (settings_obj == 0)
         return last_known;

      const bool taa_running = (*reinterpret_cast<const uint8_t*>(settings_obj + 0x65) & 1) != 0;
      s_last_taa_running.store(taa_running, std::memory_order_release);
      return taa_running;
   }
   __except (EXCEPTION_EXECUTE_HANDLER)
   {
      return last_known;
   }
#endif
}

bool TryGetSettingsObject(uintptr_t& out_settings_obj)
{
#ifdef V2_0_3
   // v2.0.3: kTAASettingsGlobal_RVA is a 16-byte xmmword buffer, not a pointer.
   // No settings object to dereference.
   out_settings_obj = 0;
   return false;
#else
   // v2.0.2/v1.3.2: kTAASettingsGlobal_RVA is a pointer-to-struct.
   const uintptr_t settings_ptr_addr = g_resolved_addresses.taa_settings_global;
   if (settings_ptr_addr == 0)
   {
      out_settings_obj = 0;
      return false;
   }

   __try
   {
      out_settings_obj = *reinterpret_cast<const uintptr_t*>(settings_ptr_addr);
      return out_settings_obj != 0;
   }
   __except (EXCEPTION_EXECUTE_HANDLER)
   {
      out_settings_obj = 0;
      return false;
   }
#endif
}

void* GetVTableFunction(void* obj, size_t index)
{
   void** vtable = *reinterpret_cast<void***>(obj);
   return vtable[index];
}

bool TryReadTAAResetFlag()
{
   const uintptr_t addr = g_resolved_addresses.taa_reset_flag;
   if (addr == 0)
      return false;
   __try
   {
      return (*reinterpret_cast<const uint8_t*>(addr) & 1) != 0;
   }
   __except (EXCEPTION_EXECUTE_HANDLER)
   {
      return false;
   }
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

      // Resolve these now, but write them after the trampoline below. Pre-writing the
      // globals can hide a hot render-scale change from the game's size cache.
      render_w_addr = g_resolved_addresses.render_width;
      render_h_addr = g_resolved_addresses.render_height;
   }

   // Pass render dims to the game — g_outputWidth/g_outputHeight are not touched.
   const char result = g_rt_creation_hook.unsafe_call<char>(render_w, render_h);

   // Keep the frame graph globals in sync after the game has observed the new size.
   if (render_w_addr != 0 && render_h_addr != 0)
   {
      *reinterpret_cast<int*>(render_w_addr) = render_w;
      *reinterpret_cast<int*>(render_h_addr) = render_h;
   }

   return result;
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

