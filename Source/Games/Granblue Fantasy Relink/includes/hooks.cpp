#include "..\..\Core\core.hpp"
#include "cbuffers.h"
#include "hooks.hpp"
#include "common.hpp"

namespace
{
   constexpr size_t kTransScanWindow = 0x500;

   struct Signature
   {
      const char* name;
      std::span<const System::BytePattern> pattern;
      int disp_pos;
      int insn_len;
      int adjust;
   };

   constexpr std::array<System::BytePattern, 93> kPipelinePattern = {{
      0x55, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x56,
      0x57, 0x53, 0x48, 0x81, 0xEC, 0x18, 0x01, 0x00, 0x00, 0x48,
      0x8D, 0xAC, 0x24, 0x80, 0x00, 0x00, 0x00, 0xC5, 0x78, 0x29,
      0x95, 0x80, 0x00, 0x00, 0x00, 0xC5, 0x78, 0x29, 0x4D, 0x70,
      0xC5, 0x78, 0x29, 0x45, 0x60, 0xC5, 0xF8, 0x29, 0x7D, 0x50,
      0xC5, 0xF8, 0x29, 0x75, 0x40, 0x48, 0xC7, 0x45, 0x38, 0xFE,
      0xFF, 0xFF, 0xFF, 0x48, 0x89, 0xD6, 0x48, 0x89, 0xC8, 0x48,
      0x89, 0x4D, 0x20, 0x48, 0x39, 0x0D, System::ANY, System::ANY, System::ANY, System::ANY,
      0x75, 0x3B, 0x48, 0x39, 0x35, System::ANY, System::ANY, System::ANY, System::ANY, 0x75,
      0x32, 0xB0, 0x01
   }};

   constexpr std::array<System::BytePattern, 48> kTransPattern = {{
      0x55, 0x41, 0x57, 0x41, 0x56, 0x56, 0x57, 0x53, 0x48, 0x81,
      0xEC, 0xB8, 0x00, 0x00, 0x00, 0x48, 0x8D, 0xAC, 0x24, 0x80,
      0x00, 0x00, 0x00, 0x48, 0xC7, 0x45, 0x30, 0xFE, 0xFF, 0xFF,
      0xFF, 0x48, 0x89, 0x55, 0xE0, 0x48, 0x89, 0xCE, 0x8B, 0x05,
      System::ANY, System::ANY, System::ANY, System::ANY, 0x48, 0x83, 0xF8, 0x0B
   }};

   constexpr std::array<System::BytePattern, 27> kInitPattern = {{
      0x56, 0x48, 0x83, 0xEC, 0x40, 0xC5, 0xF8, 0x29, 0x7C, 0x24,
      0x30, 0xC5, 0xF8, 0x29, 0x74, 0x24, 0x20, 0x48, 0x89, 0xCE,
      0x48, 0x8D, 0x0D, System::ANY, System::ANY, System::ANY, System::ANY
   }};

   constexpr std::array<System::BytePattern, 9> kRenderWidthPattern = {{
      0x8B, 0x05, System::ANY, System::ANY, System::ANY, System::ANY, 0x48, 0x8B, 0x0D
   }};

   constexpr std::array<System::BytePattern, 9> kRenderHeightPattern = {{
      0x8B, 0x05, System::ANY, System::ANY, System::ANY, System::ANY, 0x48, 0x8B, 0x15
   }};

   constexpr std::array<System::BytePattern, 10> kCameraIndexPattern = {{
      0x8B, 0x05, System::ANY, System::ANY, System::ANY, System::ANY, 0x48, 0x83, 0xF8, 0x0B
   }};

   constexpr std::array<System::BytePattern, 10> kCameraTablePattern = {{
      0x48, 0x8D, 0x0D, System::ANY, System::ANY, System::ANY, System::ANY, 0x48, 0x8B, 0x1C
   }};

   constexpr std::array<System::BytePattern, 10> kTAASettingsPattern = {{
      0x48, 0x8B, 0x3D, System::ANY, System::ANY, System::ANY, System::ANY, 0x48, 0x8D, 0x05
   }};

   constexpr std::array<System::BytePattern, 10> kTAARunningPattern = {{
      0x48, 0x8B, 0x05, System::ANY, System::ANY, System::ANY, System::ANY, 0x80, 0x38, 0x00
   }};

   constexpr std::array<System::BytePattern, 10> kTAAScalePattern = {{
      0x48, 0x8B, 0x0D, System::ANY, System::ANY, System::ANY, System::ANY, 0xF6, 0x41, 0x65
   }};

   constexpr std::array<System::BytePattern, 12> kJitterPhasePattern = {{
      0x48, 0x8B, 0x05, System::ANY, System::ANY, System::ANY, System::ANY, 0x89, 0xC1, 0x80,
      0xE1, 0x3F
   }};

   constexpr std::array<System::BytePattern, 9> kTAAResetPattern = {{
      0x80, 0x3D, System::ANY, System::ANY, System::ANY, System::ANY, 0x01, 0x75, 0x07
   }};

   constexpr std::array<Signature, 3> kCodeSignatures = {{
      {"InitializeDX11RenderingPipeline", kPipelinePattern, 0, 0, 0},
      {"Jitter Write Site", kTransPattern, 0, 0, 0},
      {"TemporalAAComponentInit", kInitPattern, 0, 0, 0},
   }};

   constexpr std::array<Signature, 9> kDataSignatures = {{
      {"g_renderWidth", kRenderWidthPattern, 2, 6, 0},
      {"g_renderHeight", kRenderHeightPattern, 2, 6, 0},
      {"g_camera_index", kCameraIndexPattern, 2, 6, 0},
      {"g_camera_table", kCameraTablePattern, 3, 7, 0},
      {"g_taa_settings_obj", kTAASettingsPattern, 3, 7, -8},
      {"g_taa_running_flag", kTAARunningPattern, 3, 7, 0},
      {"g_taa_render_scale_flag_ptr", kTAAScalePattern, 3, 7, 0},
      {"g_jitter_phase_counter", kJitterPhasePattern, 3, 7, 0},
      {"TAA Reset Flag", kTAAResetPattern, 2, 7, 0},
   }};

   // "reshade::log::message" doesn't take varargs, so format first
   template <typename... Args>
   void gbfr_log(reshade::log::level level, const char* fmt, const Args&... args)
   {
      char buffer[512];
      std::snprintf(buffer, sizeof(buffer), fmt, args...);
      reshade::log::message(level, buffer);
   }

   // Phase 1: scan a globally-unique code signature; the match address IS the function entry
   void resolve_code_address(size_t index, const char* name, void** target,
                            const std::vector<std::byte*>& matches, uintptr_t base, uintptr_t fallback_rva)
   {
      auto& r = g_gbfr_scan_results[index];
      r.name = name;
      r.match_count = static_cast<unsigned>(matches.size());

      const uintptr_t scan_addr = (matches.size() == 1)
                                       ? reinterpret_cast<uintptr_t>(matches[0])
                                       : 0;
      const bool from_scan = (scan_addr != 0);

      if (from_scan)
         *target = reinterpret_cast<void*>(scan_addr);
      else if (g_gbfr_fallback_enabled)
         *target = reinterpret_cast<void*>(base + fallback_rva);
      else
         *target = nullptr;

      r.address = reinterpret_cast<uintptr_t>(*target);
      r.from_scan = from_scan;
      r.resolved = (*target != nullptr);

      if (from_scan)
         gbfr_log(reshade::log::level::info,
            "GBFR: %s @ 0x%llX (scan, %u match(es))", name, r.address, r.match_count);
      else
         gbfr_log(
            g_gbfr_fallback_enabled ? reshade::log::level::warning : reshade::log::level::error,
            "GBFR: %s @ 0x%llX (%s, %u match(es))", name, r.address,
            g_gbfr_fallback_enabled ? "fallback" : "scan failed, fallback disabled", r.match_count);
   }

   // Phase 2: scan a data-reference signature inside the trans window; the constant
   // address is derived from each match's own disp32 (convergence required)
   void resolve_data_address(size_t index, const char* name, uintptr_t* target,
                            const Signature& sig, uintptr_t window_addr,
                            uintptr_t base, uintptr_t fallback_rva)
   {
      auto& r = g_gbfr_scan_results[index];
      r.name = name;

      std::vector<std::byte*> matches;
      uintptr_t derived = 0;
      bool converged = false;
      if (window_addr != 0)
      {
         matches = System::ScanMemoryForPattern(
            reinterpret_cast<const std::byte*>(window_addr), kTransScanWindow, sig.pattern, false);

         // Convergence: all matches must resolve to the same rip-relative address
         if (!matches.empty())
         {
            converged = true;
            for (const std::byte* match : matches)
            {
               int32_t disp = 0;
               std::memcpy(&disp, match + sig.disp_pos, sizeof(disp));
               const uintptr_t ref =
                  reinterpret_cast<uintptr_t>(match + sig.insn_len) +
                  static_cast<uintptr_t>(static_cast<int64_t>(disp));
               const uintptr_t ref_adjusted =
                  ref + static_cast<uintptr_t>(static_cast<int64_t>(sig.adjust));
               if (derived == 0)
                  derived = ref_adjusted;
               else if (ref_adjusted != derived)
                  converged = false;
            }
         }
      }
      r.match_count = static_cast<unsigned>(matches.size());

      const bool from_scan = (converged && derived != 0);
      if (from_scan)
         *target = derived;
      else if (g_gbfr_fallback_enabled)
         *target = base + fallback_rva;
      else
         *target = 0;

      r.address = *target;
      r.from_scan = from_scan;
      r.resolved = (*target != 0);

      if (from_scan)
         gbfr_log(reshade::log::level::info,
            "GBFR: %s @ 0x%llX (scan, %u match(es))", name, r.address, r.match_count);
      else
         gbfr_log(
            g_gbfr_fallback_enabled ? reshade::log::level::warning : reshade::log::level::error,
            "GBFR: %s @ 0x%llX (%s, %u match(es))", name, r.address,
            g_gbfr_fallback_enabled ? "fallback" : "scan failed, fallback disabled", r.match_count);
   }
}

bool ResolveGBFRAddresses()
{
   const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
   if (base == 0)
      return false;

   if (g_gbfr_addresses_resolved.load(std::memory_order_acquire))
   {
      for (const auto& r : g_gbfr_scan_results)
         if (r.name != nullptr && !r.resolved)
            return false;
      return true;
   }

#if defined(V2_0_4) || defined(V2_0_5) || defined(V2_0_6)
   // Runtime scan covers v2.0.4+ (function layout identical across 2.0.4/2.0.5/2.0.6)
   constexpr bool scan_enabled = true;
#else
   // Older versions keep compiled constants (layouts differ, patterns not validated)
   constexpr bool scan_enabled = false;
#endif

#ifndef GBFR_DISABLE_ADDRESS_FALLBACK
   g_gbfr_fallback_enabled = true;
#else
   // Testing mode: scan failures must not silently fall back to compiled constants
   g_gbfr_fallback_enabled = false;
#endif

   auto& addr = g_resolved_addresses;

   if (!scan_enabled)
   {
      reshade::log::message(reshade::log::level::info,
         "GBFR: signature scan not applicable for this version, using compiled constants");
      // Compiled constants (no scan)
      g_gbfr_scan_results[0] = {"InitializeDX11RenderingPipeline", base + kInitializeDX11RenderingPipeline_RVA, true, false, 0};
      addr.initialize_dx11_rendering_pipeline = reinterpret_cast<void*>(base + kInitializeDX11RenderingPipeline_RVA);
      g_gbfr_scan_results[1] = {"Jitter Write Site", base + kJitterWrite_RVA, true, false, 0};
      addr.jitter_write_site = reinterpret_cast<void*>(base + kJitterWrite_RVA);
#ifdef PATCH_JITTER_TABLE_INIT
      g_gbfr_scan_results[2] = {"TemporalAAComponentInit", base + kTemporalAntiAliasingComponent_Init_RVA, true, false, 0};
      addr.temporal_aa_component_init = reinterpret_cast<void*>(base + kTemporalAntiAliasingComponent_Init_RVA);
#endif
      g_gbfr_scan_results[3] = {"g_renderWidth", base + kRenderWidth_RVA, true, false, 0};
      addr.render_width = base + kRenderWidth_RVA;
      g_gbfr_scan_results[4] = {"g_renderHeight", base + kRenderHeight_RVA, true, false, 0};
      addr.render_height = base + kRenderHeight_RVA;
      g_gbfr_scan_results[5] = {"g_camera_index", base + kCameraIndex_RVA, true, false, 0};
      addr.camera_index = base + kCameraIndex_RVA;
      g_gbfr_scan_results[6] = {"g_camera_table", base + kCameraTable_RVA, true, false, 0};
      addr.camera_table = base + kCameraTable_RVA;
      g_gbfr_scan_results[7] = {"g_taa_settings_obj", base + kTAASettingsGlobal_RVA, true, false, 0};
      addr.taa_settings_global = base + kTAASettingsGlobal_RVA;
#if defined(V2_0_3) || defined(V2_0_4) || defined(V2_0_5) || defined(V2_0_6)
      g_gbfr_scan_results[8] = {"g_taa_running_flag", base + kTAARunningFlag_RVA, true, false, 0};
      addr.taa_running_flag = base + kTAARunningFlag_RVA;
      g_gbfr_scan_results[9] = {"g_taa_render_scale_flag_ptr", base + kTAARenderScaleFlagPointer_RVA, true, false, 0};
      addr.taa_render_scale_flag_ptr = base + kTAARenderScaleFlagPointer_RVA;
#endif
      g_gbfr_scan_results[10] = {"g_jitter_phase_counter", base + kJitterPhaseCounter_RVA, true, false, 0};
      addr.jitter_phase_counter = base + kJitterPhaseCounter_RVA;
      g_gbfr_scan_results[11] = {"TAA Reset Flag", base + kTAAResetFlag_RVA, true, false, 0};
      addr.taa_reset_flag = base + kTAAResetFlag_RVA;
#ifdef V1_3_2
      addr.camera_global = base + kCameraGlobal_RVA;
#endif
      g_gbfr_addresses_resolved.store(true, std::memory_order_release);
      return true;
   }

   // Phase 1: code signatures (globally unique; the match address is the function entry)
   const std::vector<std::byte*> pipeline_matches = System::ScanModuleForPattern(
      kCodeSignatures[0].pattern, false);
   resolve_code_address(0, kCodeSignatures[0].name, &addr.initialize_dx11_rendering_pipeline,
                        pipeline_matches, base, kInitializeDX11RenderingPipeline_RVA);

   const std::vector<std::byte*> trans_matches = System::ScanModuleForPattern(
      kCodeSignatures[1].pattern, false);
   resolve_code_address(1, kCodeSignatures[1].name, &addr.jitter_write_site,
                        trans_matches, base, kJitterWrite_RVA);

#ifdef PATCH_JITTER_TABLE_INIT
   const std::vector<std::byte*> init_matches = System::ScanModuleForPattern(
      kCodeSignatures[2].pattern, false);
   resolve_code_address(2, kCodeSignatures[2].name, &addr.temporal_aa_component_init,
                        init_matches, base, kTemporalAntiAliasingComponent_Init_RVA);
#endif

   // Phase 2: data signatures, scoped to the trans function window
   const uintptr_t trans_window_addr = g_gbfr_scan_results[1].resolved ? g_gbfr_scan_results[1].address : 0;
   resolve_data_address(3, kDataSignatures[0].name, &addr.render_width,
                        kDataSignatures[0], trans_window_addr, base, kRenderWidth_RVA);
   resolve_data_address(4, kDataSignatures[1].name, &addr.render_height,
                        kDataSignatures[1], trans_window_addr, base, kRenderHeight_RVA);
   resolve_data_address(5, kDataSignatures[2].name, &addr.camera_index,
                        kDataSignatures[2], trans_window_addr, base, kCameraIndex_RVA);
   resolve_data_address(6, kDataSignatures[3].name, &addr.camera_table,
                        kDataSignatures[3], trans_window_addr, base, kCameraTable_RVA);
   resolve_data_address(7, kDataSignatures[4].name, &addr.taa_settings_global,
                        kDataSignatures[4], trans_window_addr, base, kTAASettingsGlobal_RVA);
#if defined(V2_0_3) || defined(V2_0_4) || defined(V2_0_5) || defined(V2_0_6)
   resolve_data_address(8, kDataSignatures[5].name, &addr.taa_running_flag,
                        kDataSignatures[5], trans_window_addr, base, kTAARunningFlag_RVA);
   resolve_data_address(9, kDataSignatures[6].name, &addr.taa_render_scale_flag_ptr,
                        kDataSignatures[6], trans_window_addr, base, kTAARenderScaleFlagPointer_RVA);
#endif
   resolve_data_address(10, kDataSignatures[7].name, &addr.jitter_phase_counter,
                        kDataSignatures[7], trans_window_addr, base, kJitterPhaseCounter_RVA);
   resolve_data_address(11, kDataSignatures[8].name, &addr.taa_reset_flag,
                        kDataSignatures[8], trans_window_addr, base, kTAAResetFlag_RVA);

   const bool all_resolved = [&]()
   {
      for (const auto& r : g_gbfr_scan_results)
         if (r.name != nullptr && !r.resolved)
            return false;
      return true;
   }();

   gbfr_log(all_resolved ? reshade::log::level::info : reshade::log::level::warning,
      "GBFR: address resolution %s (%s)", all_resolved ? "complete" : "INCOMPLETE",
      g_gbfr_fallback_enabled ? "fallback available" : "fallback disabled (GBFR_DISABLE_ADDRESS_FALLBACK)");

   g_gbfr_addresses_resolved.store(true, std::memory_order_release);
   return all_resolved;
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
#if defined(V2_0_3) || defined(V2_0_4) || defined(V2_0_5) || defined(V2_0_6)
   // v2.0.3+: Jitter stored in TAA component table at [rcx + 8*(phase&0x3F) + 0x28]
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
#if defined(V2_0_3) || defined(V2_0_4) || defined(V2_0_5) || defined(V2_0_6)
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

#if defined(V2_0_3) || defined(V2_0_4) || defined(V2_0_5) || defined(V2_0_6)
   // v2.0.3+: qword_147371338 is a POINTER to the TAA running flag byte.
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
#if defined(V2_0_3) || defined(V2_0_4) || defined(V2_0_5) || defined(V2_0_6)
   // v2.0.3+: kTAASettingsGlobal_RVA is a 16-byte xmmword buffer, not a pointer.
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

      // Keep g_renderWidth/g_renderHeight in sync with the args we pass to the trampoline.
      // TAA component reads these at +0x6B81058/+0x6B8105C to decide whether to run
      // the temporal upscale path. Without this write, render == output and TUPDrawPass skips.
      if (g_resolved_addresses.render_width != 0 && g_resolved_addresses.render_height != 0)
      {
         *reinterpret_cast<int*>(g_resolved_addresses.render_width) = render_w;
         *reinterpret_cast<int*>(g_resolved_addresses.render_height) = render_h;
      }
   }

   // Pass render dims to the game — g_outputWidth/g_outputHeight are not touched.
   return g_rt_creation_hook.unsafe_call<char>(render_w, render_h);
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


