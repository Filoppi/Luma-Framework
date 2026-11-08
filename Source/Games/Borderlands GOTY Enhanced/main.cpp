// Borderlands GOTY Enhanced: Luma HDR + SMAA mod (Unreal Engine 3.5, D3D11).
// - HDR: swapchain -> scRGB fp16; replaced UE3 final-color PS (0xB030BAA6 / 0xFE88487E) runs the game's own grade
//   as an extended HDR function, takes its hue from a soft ReinhardPiecewise reference through MacLeod-Boynton
//   emulation, then a max-channel C-infinity rolloff maps it to the display. The video (0x0E97A4A0) and lens-flare
//   (0x010371F2) passes are replaced too. Core Display Composition does the paper-white scale + encode.
//   Only one HDR mod can own the swapchain: any other must be removed from the game folder.
// - AA: compute FXAA (3.11 work-queue) -> SMAA (ULTRA + color edge + depth predication) + optional RCAS. Edge
//   detection reads the scene as stored, in GAMMA space (POST_PROCESS_SPACE_TYPE 0, 1.0 = paper white); the blend
//   filters it in linear light. The last pass writes the swapchain. Optional SMAA T2x on the patched shaders' jitter and motion vectors.
// - DLSS / FSR / XeSS (DLAA): the game renders no motion vectors and no jitter; both come from patched shaders
//   (MotionVectorPatches.h). The upscaler runs before the DOF/Bloom gather, SMAA then steps aside (RCAS stays). FSR 4 and XeSS
//   run through the DX11->DX12 interop (UseLumaFSR4, UseLumaXeSS). FSR also takes masks from the alpha blended draws.
// - AO: GT-VBAO, or XeGTAO with it off, replaces the native HBAO+ (hash block below). Plus AF16x and a fix for the game's movie RAM
//   leak.

#define GAME_BORDERLANDS_GOTY 1

// Don't pop the DEVELOPMENT auto-debugger MessageBox on DLL attach: under a borderless/fullscreen game it's
// invisible and blocks the loader (ReShade times out the addon load -> error 1114).
#define DISABLE_AUTO_DEBUGGER 1

// FSR 4 and XeSS create a D3D12 device through the DX11->DX12 interop, which Core has to ignore
#define CHECK_GRAPHICS_API_COMPATIBILITY 1

#define GEOMETRY_SHADER_SUPPORT 0
#define ENABLE_SMAA 1
#define ENABLE_RCAS 1
// The motion vector and jitter draws wrap the game's own draws, so they need "original_draw_dispatch_func"
#define ENABLE_POST_DRAW_DISPATCH_CALLBACK 1
// The motion vector draw key reads the draw's arguments ("last_draw_dispatch_data")
#define ENABLE_DRAW_DISPATCH_DATA_CACHE 1

#include "..\..\Core\core.hpp"
// SMAA's area texture without the U-shape smoothing (see "Luma_SMAA_impl.hlsl")
#include "SMAA_AreaTex_SharpU.h"
#include "MotionVectorPatches.h"
#include "patched_draws.h"
#include <shellapi.h> // ShellExecuteA for About links (system() hangs the render thread in exclusive fullscreen)

// A Luma native shader ("native_*_shaders") by name, null until compiled. Looked up with "find": "operator[]" would insert on a miss,
// mutating a map Core's draw helpers read concurrently.
template <typename ShaderMap>
static auto FindShader(const ShaderMap& shaders, uint32_t name)
{
   const auto it = shaders.find(name);
   return (it != shaders.end() ? it->second.get() : nullptr);
}

// Whether all the named Luma native shaders are compiled
template <typename ShaderMap, typename... Names>
static bool HasShaders(const ShaderMap& shaders, Names... names)
{
   return ((FindShader(shaders, names) != nullptr) && ...);
}

// Whether Core's SMAA passes ("DrawSMAA()") are compiled
static bool HasSMAAShaders(const DeviceData& device_data)
{
   return HasShaders(device_data.native_pixel_shaders, "SMAA Edge Detection PS"_h, "SMAA Blending Weight Calculation PS"_h, "SMAA Neighborhood Blending PS"_h) &&
          HasShaders(device_data.native_vertex_shaders, "SMAA Edge Detection VS"_h, "SMAA Blending Weight Calculation VS"_h, "SMAA Neighborhood Blending VS"_h);
}

// The size of the texture behind a view (0x0 without one)
static uint2 GetViewTextureSize(ID3D11View* view)
{
   uint4 size;
   DXGI_FORMAT format;
   GetResourceInfo(view, size, format);
   return uint2{size.x, size.y};
}

// This frame's Halton (2, 3) jitter in pixels, over the upscaler's phase count (the default without an upscaler instance)
static std::array<float, 2> GetSRJitter(const DeviceData& device_data, const SR::InstanceData* sr_instance_data)
{
   const int phases = (sr_instance_data ? (std::max)(sr_implementations[device_data.sr_type]->GetJitterPhases(sr_instance_data), 1) : SR::GetDefaultJitterPhases());
   const unsigned int phase = cb_luma_global_settings.FrameIndex % unsigned(phases);
   return {SR::HaltonSequence(phase, 2), SR::HaltonSequence(phase, 3)};
}

// The camera FSR needs, from a row vector view projection with an affine view (clip = p * M, element (row, column) at m[column * 4 + row]).
// Clip z = A * clip w + B, so device depth = A + B / view depth (A = 1 for an infinite far plane, then B = -near). Each value is 0 when
// the projection lacks it.
struct ViewProjectionCamera
{
   double vert_fov = 0.0; // Radians: the up axis has length 1 / tan(fov / 2)
   double near_plane = 0.0;
   double far_plane = 0.0; // Finite: near x 1e6 for an infinite projection
};
static ViewProjectionCamera GetViewProjectionCamera(const float* m)
{
   const auto at = [&](int row, int column)
   { return double(m[column * 4 + row]); };
   double dot_23 = 0.0, dot_33 = 0.0;
   for (int column = 0; column < 3; column++)
   {
      dot_23 += at(2, column) * at(3, column);
      dot_33 += at(3, column) * at(3, column);
   }
   const double depth_a = (dot_33 > 0.0 ? dot_23 / dot_33 : 1.0);
   const double depth_b = at(2, 3) - depth_a * at(3, 3);
   const double up_length = std::sqrt(at(1, 0) * at(1, 0) + at(1, 1) * at(1, 1) + at(1, 2) * at(1, 2));
   ViewProjectionCamera camera;
   camera.vert_fov = (up_length > 0.0 ? 2.0 * std::atan(1.0 / up_length) : 0.0);
   camera.near_plane = (depth_a != 0.0 ? -depth_b / depth_a : 0.0);
   if (camera.near_plane > 0.0)
   {
      camera.far_plane = (depth_a > 1.0 + 1e-6 ? depth_b / (1.0 - depth_a) : camera.near_plane * 1e6);
   }
   return camera;
}

// Persisted setting widgets: the control, its ini save, a tooltip (also shown while disabled) and Core's reset button
static void DrawSettingSlider(const char* label, const char* key, float* value, float default_value, float min_value, float max_value, const char* tooltip, const char* format = "%.3f", ImGuiSliderFlags flags = 0)
{
   ImGui::SliderFloat(label, value, min_value, max_value, format, flags);
   // Saved once the edit ends, not on every drag step
   if (ImGui::IsItemDeactivatedAfterEdit())
   {
      reshade::set_config_value(nullptr, NAME, key, *value);
   }
   if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
   {
      ImGui::SetTooltip("%s", tooltip);
   }
   DrawResetButton(*value, default_value, key);
}

// A bool, or a 0/1 float (a cbuffer field), shown as a checkbox. "forced": shown as "forced_value" and disabled while another feature
// replaces it, keeping the saved choice.
template <typename T>
static void DrawSettingCheckbox(const char* label, const char* key, T* value, std::type_identity_t<T> default_value, const char* tooltip, bool forced = false, bool forced_value = false)
{
   bool shown = (forced ? forced_value : (float(*value) > 0.5f));
   ImGui::BeginDisabled(forced);
   if (ImGui::Checkbox(label, &shown))
   {
      *value = T(shown);
      reshade::set_config_value(nullptr, NAME, key, *value);
   }
   ImGui::EndDisabled();
   if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
   {
      ImGui::SetTooltip("%s", tooltip);
   }
   if (!forced)
   {
      DrawResetButton(*value, default_value, key);
   }
}

// FXAA is a compute work-queue implementation (FXAA 3.11 CS), four dispatches in this order:
//   0x81CDE53D = edge detection: a work queue per edge orientation, each pixel with its two colors packed as float16 (t0 = Color).
//   0x43A10668 = the indirect dispatch arguments from the queue counts.
//   0x78019A89 = resolve of one queue: blends each queued pixel's colors and writes Color (u0, the swapchain) in place.
//   0x08891303 = resolve of the other queue, the same way (InColor t2 aliases Color u0, the swapchain).
// All four are skipped when SMAA or DLSS / FSR own the resolve (see "fxaa_replaced"): the last one is where SMAA / RCAS run. Left
// running, the first resolve's half of FXAA would reach SMAA's input and the upscaled image.
static constexpr uint32_t kFXAAEdgeHash = 0x81CDE53D;
static constexpr uint32_t kFXAAArgumentsHash = 0x43A10668;
static constexpr uint32_t kFXAAFirstResolveHash = 0x78019A89;
static constexpr uint32_t kFXAAResolveHash = 0x08891303; // Replaced with SMAA (RCAS only after DLSS / FSR)
static constexpr uint32_t kCelShadingHash = 0x08DC66D1;  // Cel-shading edge PS: binds the scene depth at t0 (the predication source)

// AO: XeGTAO replaces the game's native NVIDIA HBAO+ (GFSDK_SSAO). Full-res chain: deinterleave 0xFFE232A6 -> normals
// 0xB2B47225 (left running) -> coarse horizon 0xF534EB09 -> bilateral blur 0x4E1BEE34 -> apply-multiply PS 0x44764BF6. The scene
// depth is captured at the deinterleave and the packed view normals at the coarse pass (both skipped); at the blur dispatch the
// XeGTAO passes write its u0 (the game's final r16g16_float AO, the apply reads .x), so the apply blit composites it unchanged.
// XeGTAO reads the game's own cb0 ($Globals: ProjInfo) and cb2 (MinZ_MaxZRatioCS), still bound there. Noise and denoise count: see
// "IsGTAOTemporal".
static constexpr uint32_t kAODeinterleaveHash = 0xFFE232A6; // Scene depth -> quarter-res array: skipped (XeGTAO builds its own mip pyramid)
static constexpr uint32_t kAOCoarseHash = 0xF534EB09;       // HBAO+ horizon march (x2), binds the view normals at t0: skipped (normals captured)
static constexpr uint32_t kAOBlurHash = 0x4E1BEE34;         // Bilateral blur -> the final r16g16_float AO at u0: replaced with XeGTAO

// DLSS / FSR: the scene's first post passes, which end a motion vector frame (the upscaler runs right before): the DOF/Bloom
// gather CS (reads the scene copy and the depth copy), else the uber post (DOF and Bloom off in the game's settings).
static constexpr uint32_t kDOFBloomGatherHash = 0xF0D8D818;
static constexpr uint32_t kUberPostHashes[] = {0xB030BAA6, 0xFE88487E};

// Motion vectors for DLSS / FSR (the game renders none, see MotionVectorPatches.h): the opaque draws into the fp16 scene draw with
// patched shaders that also write an extra target, the vertex shader's second run reading the draw's previous frame b0 / b1 / b3.
#if DEVELOPMENT
static bool g_mv_enable = false;
static bool g_mv_debug_view = false;
// FSR's reactive and transparency & composition masks from the scene's alpha blended draws (see
// "MotionVectorPatch::PatchPixelShaderReactive")
static bool g_sr_reactive_enable = true;
static float g_sr_reactive_scale = 1.f;      // The alpha blended draws' reactivity, scaled (AMD's default 1)
static float g_sr_reactive_threshold = 0.5f; // Under it 0, over it 0.9 (AMD's 0.2, but under 0.5 static glows shake); 0: the scaled reactivity
static bool g_sr_reactive_debug_view = false;
static bool g_mv_force_jitter = false;   // The projection jitter without an upscaler
static bool g_mv_disable_jitter = false; // No projection jitter under the upscaler (A/B of jitter-dependent artifacts)
#else
static constexpr bool g_sr_reactive_enable = true;
static constexpr float g_sr_reactive_scale = 1.f;
static constexpr float g_sr_reactive_threshold = 0.5f;
static constexpr bool g_mv_enable = false;
static constexpr bool g_mv_force_jitter = false;
static constexpr bool g_mv_disable_jitter = false;
#endif

#if DEVELOPMENT
// "DrawWithMotionVectors" refusals, by "MV_REJECT" reason
static constexpr const char* mv_reject_names[] = {"extra_target", "no_scene", "other_depth_color", "format", "size", "create", "blend", "shaders"};
#endif

// SMAA's resources go after this many presents without it (the upscaler antialiasing, or the game's AA off): ~10 s, so menus and
// loading screens between upscaled frames, which run SMAA, don't recreate them each time
static constexpr uint32_t idle_release_frames = 600;

// User settings, persisted in the "NAME" config section (see "LoadConfigs") unless noted otherwise.
static bool g_smaa_enable = true;
static bool g_smaa_t2x = false;      // SMAA T2x, on the motion vector draws' jitter and motion vectors. Off without SMAA, or with an upscaler
static float g_rcas_sharpness = 0.f; // RCAS on the SMAA or DLSS / FSR output; off by default: the ink outlines are clean and sharpening haloes them
static bool g_hide_ui = false;       // Hides the game's HUD (skips its draws into the swapchain), for clean screenshots
// SMAA predication on geometry. The signal is plane-deviation edge-ness built from the scene depth, not the depth
// itself (see Luma_BL_DepthExtract.hlsl); the tolerance is the only free parameter and is a fraction of view
// depth, so MoH Airborne's calibrated 0.02 carries over unchanged.
static bool g_smaa_predication = true;
static float g_smaa_pred_tolerance = 0.02f;
#if DEVELOPMENT
// Calibration aids for the tolerance: what predication does is an ABSENCE of smearing, which the eye reads badly
// and worse in motion, so judge the mask itself rather than the frame.
static bool g_smaa_pred_debug = false;   // show the predication mask instead of the antialiased frame
static bool g_smaa_pred_measure = false; // one-shot: read the mask back and log its distribution (UI button)
#endif

// Ambient Occlusion: XeGTAO replaces the native HBAO+, on by default. Persisted as "XeGTAOEnable".
static bool g_gtao_enable = true;
// Runtime XeGTAO knobs (LumaGTAO cb b11); their sliders are DEVELOPMENT/TEST only. FinalValuePower: the primary darkness dial,
// calibrated to the vanilla HBAO+ histogram (its PowExponent does not transfer numerically). DepthScale: the view z divisor (UE3
// units, near plane ~10, to ~meters) so Intel's tuned radius and falloff apply; the dial against broad over-occlusion.
// RadiusOverride > 0 overrides EFFECT_RADIUS (in scaled units).
static float g_gtao_final_value_power = 1.0f;
static float g_gtao_depth_scale = 50.f;
static float g_gtao_radius_override = 0.f;
// GT-VBAO's visibility bitmask main pass instead of XeGTAO's horizons; prefilter, denoise and the b11 knobs stay XeGTAO's. Persisted as
// "GTVBAOEnable". Its power is calibrated on two lit scenes: the mean darkening of XeGTAO, softer than HBAO+ where that's darkest.
static bool g_gtvbao_enable = true;
static float g_gtvbao_final_value_power = 0.9f;
static float g_gtao_history_weight = 0.9f; // The AO history's weight under DLSS/FSR, 0 turns it off
#if DEVELOPMENT
static int g_gtao_temporal = 0;   // 0 = with DLSS/FSR (see "IsGTAOTemporal"), 1 = off, 2 = on
static int g_gtao_debug_view = 0; // 0 off, 1 depth gradient, 2 normals, 3 AO x8, 4 edges, through the game's apply blit (DebugViewRT is DEV only)
#else
static constexpr int g_gtao_temporal = 0;
static constexpr int g_gtao_debug_view = 0; // The shader's DebugViewRT is DEV only
#endif

// "Fix Movie Memory Leak": the game's Bink movies create D3D11 Y'CbCr decode buffers and never release them, so RAM grows linearly
// until OOM (the leak is the game's, not Luma's). The game's leaked COM references are dropped on old movie generations (orphaned:
// a movie's buffers are sampled only during its own playback), tagged by creation call stack RVAs in BorderlandsGOTY.exe (a frozen
// remaster; a non-matching build tags nothing, a safe no-op). Movies keep playing.
static bool g_fix_movie_leak = true; // Persisted as "FixMovieLeak"

namespace BLMovieLeakFix
{
   // Build-specific RVAs (see above); BUILD_CHECK_FRAME drives a one-shot warning when a non-matching build tags nothing.
   constexpr uintptr_t RVA_CREATE_WRAPPER = 0xBFF27; // ret addr in the RHI resource-create wrapper; measured movie-path-only (never hit without the span below)
   constexpr uintptr_t RVA_STREAM_LO = 0x58A000;     // streaming/movie fn span (create call sites)
   constexpr uintptr_t RVA_STREAM_HI = 0x58C000;
   constexpr uint32_t BUILD_CHECK_FRAME = 18000; // ~5 min; movies tag well before this if build matches
   constexpr uint32_t NEW_GEN_GAP_FRAMES = 90;   // frame gap that separates two movies into "generations"
   constexpr int MAX_FRAMES = 32, SKIP_FRAMES = 1;
   constexpr uint64_t STACKWALK_MIN_BYTES = 2ull * 1024 * 1024; // only walk the stack for big resources (cheap)
   constexpr int RELEASE_GEN_LAG = 2;                           // release only gen <= cur_gen-2 (keep current + previous)
   constexpr uint32_t RELEASE_IDLE_FRAMES = 600;                // and only after this many frames since creation (~10s)
   constexpr uint32_t RELEASE_FLUSH_PERIOD = 120;               // flush at most every N present frames

   struct MovieTex
   {
      uint64_t bytes;
      int gen;
      uint32_t created_frame;
   };

   static std::mutex g_mtx;
   static std::unordered_map<uint64_t, MovieTex> g_movie;    // resource handle -> info
   static std::unordered_map<uint64_t, uint64_t> g_view2res; // view handle -> resource handle (tagged buffers only)
   static uintptr_t g_exe_base = 0;
   static int g_cur_gen = 0;
   static uint32_t g_last_movie_frame = 0;
   // Coarse cross-thread gates (the present thread writes g_frame, workers read it); the maps are synchronized by g_mtx.
   static std::atomic<bool> g_have_movie{false};
   static std::atomic<uint32_t> g_frame{0};
   static uint64_t g_freed_bytes = 0;
   static uint32_t g_freed_tex = 0;
   static bool g_build_checked = false; // one-shot build-mismatch telemetry guard (present thread only)

   inline bool StackIsMovie(void* const* frames, int n)
   {
      if (!g_exe_base)
         return false;
      bool has_create = false, has_stream = false;
      for (int i = 0; i < n; ++i)
      {
         const uintptr_t a = reinterpret_cast<uintptr_t>(frames[i]);
         if (a < g_exe_base)
            continue;
         const uintptr_t rva = a - g_exe_base;
         if (rva == RVA_CREATE_WRAPPER)
            has_create = true;
         else if (rva >= RVA_STREAM_LO && rva < RVA_STREAM_HI)
            has_stream = true;
      }
      return has_create && has_stream;
   }

   // Tags the movies' Y'CbCr decode buffers at creation (the stack is walked only from "STACKWALK_MIN_BYTES")
   void OnInitResource(reshade::api::device*, const reshade::api::resource_desc& desc, const reshade::api::subresource_data*, reshade::api::resource_usage, reshade::api::resource handle)
   {
      // Decode targets are BUFFERS (measured); gating on type excludes the textures that share the
      // streaming-fn span -> no mistag/UAF, and skips the stack walk for every texture.
      if (desc.type != reshade::api::resource_type::buffer)
         return;
      const uint64_t bytes = desc.buffer.size;
      if (bytes < STACKWALK_MIN_BYTES)
         return;
      void* frames[MAX_FRAMES];
      const int n = RtlCaptureStackBackTrace(SKIP_FRAMES, MAX_FRAMES, frames, nullptr);
      if (n <= 0 || !StackIsMovie(frames, n))
         return;
      const std::lock_guard<std::mutex> lk(g_mtx);
      const uint32_t f = g_frame;
      if (g_cur_gen == 0 || (f - g_last_movie_frame) > NEW_GEN_GAP_FRAMES)
         g_cur_gen += 1;
      g_last_movie_frame = f;
      g_movie[handle.handle] = MovieTex{bytes, g_cur_gen, f};
      g_have_movie = true;
   }

   void OnDestroyResource(reshade::api::device*, reshade::api::resource handle)
   {
      if (!g_have_movie)
         return;
      const std::lock_guard<std::mutex> lk(g_mtx);
      g_movie.erase(handle.handle);
   }

   void OnInitResourceView(reshade::api::device*, reshade::api::resource res, reshade::api::resource_usage, const reshade::api::resource_view_desc&, reshade::api::resource_view view)
   {
      if (!g_have_movie || !view.handle)
         return;
      const std::lock_guard<std::mutex> lk(g_mtx);
      if (g_movie.find(res.handle) != g_movie.end())
         g_view2res[view.handle] = res.handle;
   }

   void OnDestroyResourceView(reshade::api::device*, reshade::api::resource_view view)
   {
      if (!g_have_movie)
         return;
      const std::lock_guard<std::mutex> lk(g_mtx);
      g_view2res.erase(view.handle);
   }

   // Release the game's leaked COM refs on old orphaned generations. Re-entrancy-safe: our Release()
   // re-enters OnDestroyResource[View] on this thread -> COLLECT+UNLINK under lock, then RELEASE unlocked.
   void Flush()
   {
      std::unordered_map<uintptr_t, uint64_t> victims; // Resource -> its bytes
      std::vector<uintptr_t> views;
      {
         const std::lock_guard<std::mutex> lk(g_mtx);
         // Phase A: pick victims (keep current + previous gen, must be idle) and UNLINK them before any Release (re-entrant callbacks
         // then find nothing)
         std::erase_if(g_movie, [&](const auto& movie)
            {
               const auto& [resource, m] = movie;
               if (m.gen > g_cur_gen - RELEASE_GEN_LAG || (g_frame - m.created_frame) < RELEASE_IDLE_FRAMES)
                  return false;
               victims.emplace(resource, m.bytes);
               return true; });
         if (victims.empty())
            return;
         std::erase_if(g_view2res, [&](const auto& entry)
            {
               const auto& [view, resource] = entry;
               if (!victims.contains(resource))
                  return false;
               views.push_back(view);
               return true; });
      }
      // Phase B: RELEASE with the lock released, the views before their resources
      for (const uintptr_t view : views)
         reinterpret_cast<IUnknown*>(view)->Release();
      uint32_t ft = 0;
      uint64_t fb = 0;
      bool partial = false;
      for (const auto& [resource, bytes] : victims)
      {
         // rc==0 => actually freed (count it); rc>0 => game holds extra refs, not reclaimed. (rc is a
         // by-value ULONG, never a deref of a freed object.)
         const ULONG rc = reinterpret_cast<IUnknown*>(resource)->Release();
         if (rc == 0)
         {
            ft++;
            fb += bytes;
         }
         else
            partial = true;
      }
      g_freed_tex += ft;
      g_freed_bytes += fb;
#if DEVELOPMENT || TEST
      if (ft || partial)
      {
         char b[224];
         snprintf(b, sizeof(b), "[BL-Leak] reclaimed %u movie textures, ~%.1f MB (cum %.1f MB)%s",
            ft, fb / 1048576.0, g_freed_bytes / 1048576.0,
            partial ? " [WARN: some buffers held extra game refs, not reclaimed]" : "");
         reshade::log::message(reshade::log::level::info, b);
      }
#else
      (void)partial;
#endif
   }
} // namespace BLMovieLeakFix

struct BorderlandsGotyGameDeviceData final : public GameDeviceData
{
#if DEVELOPMENT || TEST
   bool logged_no_fp16 = false; // warned once: swapchain not fp16 (HDR upgrade absent) -> SMAA skipped
#endif

   // Predication inputs, captured per frame from the cel-shading pass: scene depth (D24S8, viewed
   // r24_unorm_x8_uint) and the PSOffsetConstants (MinZ_MaxZRatio) that pass linearizes it with.
   ComPtr<ID3D11ShaderResourceView> srv_depth;
   ComPtr<ID3D11Buffer> cb_game_offsets;

   // SMAA predication: plane-deviation edge-ness (R16F) built from srv_depth by the BL Depth Extract CS.
   com_ptr<ID3D11Buffer> cb_pred;
   ComPtr<ID3D11Texture2D> tex_pred;
   ComPtr<ID3D11UnorderedAccessView> uav_pred;
   ComPtr<ID3D11ShaderResourceView> srv_pred;
#if DEVELOPMENT
   // Staging copy for the one-shot mask readback (see LogPredicationStats), allocated on first use.
   struct TextureCapture
   {
      ComPtr<ID3D11Texture2D> staging;
      UINT width = 0;
      UINT height = 0;
      DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
      bool copy_pending = false;
   };
   TextureCapture pred_measure;
#endif

   // SMAA metrics CB (b1) = (1/w, 1/h, w, h) + (predication scale,0,0,0): 2.0 = valid depth (predication active), 1.0 = no/mismatched
   // depth (plain ULTRA threshold)
   com_ptr<ID3D11Buffer> cb_smaa_metrics;

   // The color size the SMAA, predication and RCAS resources were built at (see the FXAA resolve replacement)
   uint32_t smaa_w = 0, smaa_h = 0;

   // Scene-color snapshot (fp16, CopyResource'd from the swapchain each frame, the resolve being in place): SMAA's input (edge
   // detection, and the neighborhood blend in linear light), or RCAS's after DLSS / FSR
   ComPtr<ID3D11Texture2D> tex_input;
   ComPtr<ID3D11ShaderResourceView> srv_input;

   // SMAA output temp (fp16, SRV+RTV): RCAS's input when it follows SMAA, or copied into the swapchain target if that has no RTV
   ComPtr<ID3D11Texture2D> tex_smaa_out;
   ComPtr<ID3D11RenderTargetView> tex_smaa_out_rtv;
   ComPtr<ID3D11ShaderResourceView> tex_smaa_out_srv;

   // RCAS output temp (fp16, RTV), copied into the swapchain target, only if that has no RTV
   ComPtr<ID3D11Texture2D> tex_rcas_out;
   ComPtr<ID3D11RenderTargetView> tex_rcas_out_rtv;
   // This FXAA chain is ours (SMAA or DLSS / FSR own its resolve): its passes are skipped, decided at its edge detection
   bool fxaa_replaced = false;
   // The frames SMAA, the snapshot's users (SMAA or RCAS), and SMAA's output temp (RCAS after SMAA, or no RTV on the target) were last
   // used, for "idle_release_frames"
   uint32_t smaa_frame = 0;
   uint32_t snapshot_frame = 0;
   uint32_t smaa_out_frame = 0;
   // SMAA T2x: fixed at present for the next frame like "sr_active". The jitter phase of this frame (-1 without), the antialiased linear
   // frames of the two phases, and the frame the last one was written
   bool t2x_active = false;
   int t2x_phase = -1;
   ComPtr<ID3D11Texture2D> t2x_frames[2];
   ComPtr<ID3D11RenderTargetView> t2x_frame_rtvs[2];
   ComPtr<ID3D11ShaderResourceView> t2x_frame_srvs[2];
   uint32_t t2x_last_frame = 0;

   // --- XeGTAO scratch (all at the game's AO full-res; cached, rebuilt on size change). ---
   // Game inputs captured per frame (reset in OnPresent): full-res r24 scene depth (deinterleave t0) and
   // the packed view normals (coarse-AO t0). A captured srv_gtao_depth arms the coarse/blur takeover.
   ComPtr<ID3D11ShaderResourceView> srv_gtao_depth;
   ComPtr<ID3D11ShaderResourceView> srv_gtao_normals;
   // Prefiltered viewspace-depth MIP pyramid (R32F, 5 mips): 5 per-mip UAVs + one full SRV.
   ComPtr<ID3D11Texture2D> tex_gtao_depth_mips;
   ComPtr<ID3D11UnorderedAccessView> gtao_depth_mip_uavs[5];
   ComPtr<ID3D11ShaderResourceView> srv_gtao_depth_mips;
   // Two working AO+edges buffers (R8G8_UNORM) ping-ponged by main pass -> denoise 1.
   ComPtr<ID3D11Texture2D> tex_gtao_working[2];
   ComPtr<ID3D11UnorderedAccessView> uav_gtao_working[2];
   ComPtr<ID3D11ShaderResourceView> srv_gtao_working[2];
   uint32_t gtao_w = 0, gtao_h = 0;
   // The AO history, R16G16_FLOAT (AO, view Z), ping-ponged: written by "temporal_pass_cs"
   ComPtr<ID3D11Texture2D> tex_gtao_history[2];
   ComPtr<ID3D11UnorderedAccessView> uav_gtao_history[2];
   ComPtr<ID3D11ShaderResourceView> srv_gtao_history[2];
   uint32_t gtao_history_index = 0; // The one written last
   uint32_t gtao_history_frame = 0; // Luma frame index of that write: the history restarts unless it was the previous frame
   com_ptr<ID3D11Buffer> cb_gtao_temporal;
   // LumaGTAO knob CB (b11) = (FinalValuePower, DepthScale, RadiusOverride, DebugView, NoiseIndex), written every frame
   com_ptr<ID3D11Buffer> cb_gtao;

   // "IsSRActive", taken at present: Core's "Super Resolution" selection changes after it, mid frame for the draws
   bool sr_active = false;
   // No upscaler picked (see "CleanExtraSRResources"), or SMAA T2x turned off: the motion vector resources go at the next present
   std::atomic<bool> release_sr_resources = false;
   // "DrawUpscaler" made a new upscaler output texture (see there)
   bool sr_output_recreated = false;
   // The upscaler's output, read in place of its input (the scene's last copy, else the scene) by the uber and the DOF/Bloom gather
   com_ptr<ID3D11ShaderResourceView> sr_output_srv;
   ID3D11Resource* sr_input = nullptr;

   // DLSS / FSR: the per pixel DOF blur amount accumulated over frames (Luma_BL_DOFGather.hlsl), ping-pong, and its constants (b5)
   com_ptr<ID3D11Texture2D> dof_history[2];
   com_ptr<ID3D11ShaderResourceView> dof_history_srvs[2];
   com_ptr<ID3D11UnorderedAccessView> dof_history_uavs[2];
   uint32_t dof_history_index = 0;
   bool dof_history_valid = false;
   com_ptr<ID3D11Buffer> dof_history_cb;

   // Motion vectors: shaders patched on first use, by original hash (null on failure), and the target (sized like the scene; every
   // blend state writes it unblended, see "OnCreateBlendState")
   std::atomic<bool> mv_active = false; // Motion vectors and jitter this frame: an upscaler is active, or the DEV toggle (set at present)
   std::shared_mutex mv_mutex;
   // A patched shader (null if refused) with, for a vertex shader, whether it reads b3 (skinned), the byte offset of its LocalToWorld
   // translation row in b0 (none for world space geometry, matched by draw key alone), and the bytes of b0 / b1 / b3 it reads
   // ("DXBC::ConstantBufferBytes": the previous frame's copies upload only those)
   template <typename T>
   struct PatchedShader
   {
      com_ptr<T> shader;
      bool skinned = false;
      uint32_t translation_offset = UINT_MAX;
      std::array<UINT, std::size(MotionVectorPatches::previous_slots)> read_sizes = {};
   };
   std::unordered_map<uint32_t, PatchedShader<ID3D11VertexShader>> mv_vertex_shaders;
   std::unordered_map<uint32_t, PatchedShader<ID3D11PixelShader>> mv_pixel_shaders;
   // The alpha blended draws' mask versions, by "mv_reactive_blend" - 1
   std::unordered_map<uint32_t, PatchedShader<ID3D11PixelShader>> mv_reactive_pixel_shaders[2];
   com_ptr<ID3D11Texture2D> mv_texture;
   com_ptr<ID3D11RenderTargetView> mv_rtv;
   com_ptr<ID3D11ShaderResourceView> mv_srv;  // Read by the DOF gather (its history), GT-VBAO's AO history and SMAA T2x
   com_ptr<ID3D11UnorderedAccessView> mv_uav; // Null without typed UAV loads of its format (then no fill)
   // FSR's reactive and transparency & composition masks, written by the fill from "mv_reactive_target"
   com_ptr<ID3D11Texture2D> mv_reactive;
   com_ptr<ID3D11UnorderedAccessView> mv_reactive_uav;
   com_ptr<ID3D11Texture2D> mv_transparency;
   com_ptr<ID3D11UnorderedAccessView> mv_transparency_uav;
   // What the alpha blended draws wrote (x reactive, y transparency & composition; max blended, see "OnCreateBlendState")
   com_ptr<ID3D11Texture2D> mv_reactive_target;
   com_ptr<ID3D11RenderTargetView> mv_reactive_target_rtv;
   com_ptr<ID3D11ShaderResourceView> mv_reactive_target_srv;
   uint32_t sr_reactive_frame = 0;
   // A frame opens at its first mesh draw into output sized depth (the depth prepass: the jitter is chosen there), starts at its
   // first motion vector draw (the target is cleared) and ends at the first post pass ("kDOFBloomGatherHash", the uber post), once
   // per present.
   bool mv_scene_open = false;
   bool mv_scene_done = false;
   bool mv_frame_ended = true;
   // The frame's scene depth (the depth view's resource), its last copy (the post passes and the outlines read that copy), and a
   // view of the one the camera fill and the upscaler read (the depth itself if it can be read, else the copy)
   com_ptr<ID3D11Resource> mv_depth;
   uint64_t mv_depth_copy = 0;
   com_ptr<ID3D11ShaderResourceView> mv_depth_srv;
   bool mv_fill_pending = false;
   bool mv_filled = false; // This frame's fill ran: SMAA T2x's motion vectors are whole
   com_ptr<ID3D11Buffer> mv_fill_buffer;
   // The fp16 scene the motion vector draws write, and its last copy (the DOF/Bloom gather and the uber post read the copy)
   com_ptr<ID3D11Resource> mv_scene_color;
   uint64_t mv_scene_color_copy = 0;
   // The projection jitter (pixels, +y down), chosen when the scene opens; its NDC offset is at VS "MotionVectorPatches::jitter_slot"
   // of every mesh draw depth tested against the scene
   std::array<float, 2> mv_jitter = {};
   std::array<float, 2> mv_jitter_ndc = {}; // The same offset in NDC (y up), as the jitter buffer holds it
   com_ptr<ID3D11Buffer> mv_jitter_buffer;
   // Per-draw lookups kept for the next draw (reset when the scene opens, views and states can be recreated between scenes): the
   // jitter path's last depth view and whether it's the scene depth (its depth test: "bound_states.DepthTested"), the motion vector
   // path's last accepted targets and the last blend state's opacity (null = the default state, opaque), the last vertex and pixel
   // shader's patched versions (owned by "mv_vertex_shaders" / "mv_pixel_shaders", never erased).
   ID3D11DepthStencilView* jitter_dsv = nullptr;
   bool jitter_dsv_scene = false;
   ID3D11RenderTargetView* mv_accepted_rtv = nullptr;
   ID3D11DepthStencilView* mv_accepted_dsv = nullptr;
   ID3D11BlendState* mv_blend_state = nullptr;
   bool mv_blend_opaque = true;
   uint8_t mv_reactive_blend = 0;
   // The immediate context's bindings as the game last bound them. The constant buffers aren't tracked: the motion vector draws
   // query b0 / b1 / b3 together.
   PatchedDraws::BoundStates bound_states;
   uint32_t mv_last_vertex_shader_hash = 0;
   const PatchedShader<ID3D11VertexShader>* mv_last_vertex_shader = nullptr;
   uint32_t mv_last_pixel_shader_hash = 0;
   ID3D11PixelShader* mv_last_pixel_shader = nullptr;
   PatchedDraws::BoundShader<ID3D11VertexShader> mv_bound_vertex_shader;
   PatchedDraws::BoundShader<ID3D11PixelShader> mv_bound_pixel_shader;

   // CPU copies of the b0 / b1 / b3 buffers the motion vector draws bind (see "PatchedDraws::ObjectConstants"): the engine uploads
   // every one with UpdateSubresource (whole buffer, only when a constant changed). Every call but "MayBeRegistered" under
   // "mv_constants_mutex": a destroyed buffer's release can come from another thread.
   using ConstantsCopy = PatchedDraws::ObjectConstants::Copy;
   std::shared_mutex mv_constants_mutex;
   PatchedDraws::ObjectConstants mv_constants;
   // Previous frame constants of the motion vector draws (see "PatchedDraws::PreviousConstants")
   PatchedDraws::PreviousConstants mv_previous_constants;
   // Motion vector draws by draw key (shaders, buffers, arguments), with LocalToWorld and b0 / b1 / b3. A draw takes
   // the previous frame's constants of its key's nearest draw (same object, a frame earlier), its camera included: the first person
   // weapon draws with a camera of its own.
   struct MotionVectorObject
   {
      PatchedDraws::ObjectTransform transform;
      ConstantsCopy object; // b0
      ConstantsCopy camera; // b1
      ConstantsCopy bones;  // b3, skinned draws only
   };
   PatchedDraws::FrameObjects<MotionVectorObject, PatchedDraws::KeyHash> mv_objects;
   // The frame's world camera (b1 of its first motion vector draw) and the previous frame's
   ConstantsCopy mv_camera;
   ConstantsCopy mv_previous_camera;
   // b1's ViewProjectionMatrix is translated (the camera at the origin, CameraPosition 0): the camera's translation comes with each
   // object's LocalToWorld (b0, whose previous copy carries the previous one). Geometry without LocalToWorld (instanced foliage, VS
   // 0x9B0EA126 / 0xB4A05DE8: instance matrices in vertex inputs, rebuilt every frame) gets it from "CameraMove()" instead, through its
   // previous camera. Per frame: the matched world camera objects' previous minus current translation, and their cached median.
   std::vector<std::array<float, 3>> mv_camera_moves;
   size_t mv_camera_move_samples = 0;
   std::array<float, 3> mv_camera_move = {};
   std::vector<uint8_t> mv_moved_camera; // A previous camera moved by it, uploaded by one draw
   uint32_t mv_frame_index = 0;          // The Luma frame index of the last motion vector frame

#if DEVELOPMENT
   // Per frame counts for the DEV panel (the last complete frame's shown)
   struct MotionVectorStats
   {
      uint32_t motion_vector_draws = 0, jitter_draws = 0, matched = 0, camera_only = 0, other_camera = 0, uncopied = 0, updates = 0, sr_draws = 0;
      uint32_t registered_buffers = 0;    // At present: the b0 / b1 / b3 buffers with CPU copies (see "mv_constants")
      uint32_t constants_pool = 0;        // At present: the pooled copies (see "PatchedDraws::ObjectConstants::NewCopy")
      uint32_t refused_region_copies = 0; // Region copies of the open scene or its depth that aren't whole (see "OverrideCopyTextureRegion")
      uint32_t fxaa_skipped = 0;          // FXAA dispatches skipped before the replaced resolve (see "fxaa_replaced")
      uint32_t reactive_draws = 0;        // Alpha blended draws that wrote the masks
      uint32_t ended_by = 0;
      uint32_t rejected[std::size(mv_reject_names)] = {};                                            // "DrawWithMotionVectors" refusals by reason ("MV_REJECT")
      uint32_t rejected_format = 0, rejected_dimension = 0, rejected_width = 0, rejected_height = 0; // The last target refused by format or size
   };
   MotionVectorStats mv_stats, mv_last_stats;
   uint32_t mv_destroyed_buffers = 0; // Registered buffers the game destroyed, in the session (see "OnDestroyResource")
   int mv_draw_reject = -1;           // The current draw's "MV_REJECT" reason (-1 for none), for the MCP trace note
#endif

   void ReleaseGTAOScratch()
   {
      tex_gtao_depth_mips.reset();
      for (auto& uav : gtao_depth_mip_uavs)
         uav.reset();
      srv_gtao_depth_mips.reset();
      for (int i = 0; i < 2; i++)
      {
         tex_gtao_working[i].reset();
         uav_gtao_working[i].reset();
         srv_gtao_working[i].reset();
         tex_gtao_history[i].reset();
         uav_gtao_history[i].reset();
         srv_gtao_history[i].reset();
      }
      gtao_history_frame = 0;
      gtao_w = gtao_h = 0;
   }

   void ReleaseReactiveMasks()
   {
      mv_reactive.reset();
      mv_reactive_uav.reset();
      mv_transparency.reset();
      mv_transparency_uav.reset();
      mv_reactive_target.reset();
      mv_reactive_target_rtv.reset();
      mv_reactive_target_srv.reset();
   }

   void ReleaseMotionVectorTarget()
   {
      mv_texture.reset();
      mv_rtv.reset();
      mv_srv.reset();
      mv_uav.reset();
      ReleaseReactiveMasks(); // Sized like it
   }

   void ReleaseT2xFrames()
   {
      for (int i = 0; i < 2; i++)
      {
         t2x_frames[i].reset();
         t2x_frame_rtvs[i].reset();
         t2x_frame_srvs[i].reset();
      }
   }

   void ReleaseSMAAOutput()
   {
      tex_smaa_out.reset();
      tex_smaa_out_rtv.reset();
      tex_smaa_out_srv.reset();
   }

   // SMAA's own resources (predication, output temp), apart from Core's ("ReleaseSMAAIntermediates")
   void ReleaseSMAAScratch()
   {
      tex_pred.reset();
      uav_pred.reset();
      srv_pred.reset();
      ReleaseSMAAOutput();
   }

   // What SMAA and RCAS share: the snapshot, and RCAS's output temp
   void ReleaseSnapshotScratch()
   {
      tex_input.reset();
      srv_input.reset();
      tex_rcas_out.reset();
      tex_rcas_out_rtv.reset();
   }
};

class BorderlandsGoty final : public Game
{
   static BorderlandsGotyGameDeviceData& GetGameDeviceData(DeviceData& device_data)
   {
      return *static_cast<BorderlandsGotyGameDeviceData*>(device_data.game);
   }

   // A DEFAULT usage 2D texture (1 mip, 1 sample), fp16 unless "format" says otherwise. Resets "out".
   static bool CreateDefaultTex(ID3D11Device* device, uint32_t w, uint32_t h, UINT bind_flags, ComPtr<ID3D11Texture2D>* out, DXGI_FORMAT format = DXGI_FORMAT_R16G16B16A16_FLOAT)
   {
      out->reset();
      const CD3D11_TEXTURE2D_DESC td(format, w, h, 1, 1, bind_flags);
      return SUCCEEDED(device->CreateTexture2D(&td, nullptr, out->put()));
   }

#if DEVELOPMENT
   // One-shot readback of the predication mask, to calibrate g_smaa_pred_tolerance from numbers rather than screenshots. It reports
   // COVERAGE at the level SMAA compares against (0.5) and the shape either side, not a mean: on a well-tuned frame the mask is 0
   // nearly everywhere. Every texel is read, as a 1px silhouette is exactly what a stride would step over. Copies on the press frame
   // and maps on a later one without waiting, so the press never stalls the render thread.
   static void LogPredicationStats(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, BorderlandsGotyGameDeviceData& gd)
   {
      auto& capture = gd.pred_measure;
      if (!gd.tex_pred || (!g_smaa_pred_measure && !capture.copy_pending))
         return;

      D3D11_TEXTURE2D_DESC td = {};
      gd.tex_pred->GetDesc(&td);
      if (capture.width != td.Width || capture.height != td.Height || capture.format != td.Format)
      {
         D3D11_TEXTURE2D_DESC sd = td;
         sd.Usage = D3D11_USAGE_STAGING;
         sd.BindFlags = 0;
         sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
         sd.MiscFlags = 0;
         capture.staging.reset();
         capture.copy_pending = false;
         if (FAILED(native_device->CreateTexture2D(&sd, nullptr, capture.staging.put())) || !capture.staging)
         {
            capture.width = 0;
            return;
         }
         capture.width = td.Width;
         capture.height = td.Height;
         capture.format = td.Format;
      }

      if (!capture.copy_pending)
      {
         g_smaa_pred_measure = false;
         native_device_context->CopyResource(capture.staging.get(), gd.tex_pred.get());
         capture.copy_pending = true;
         return;
      }

      D3D11_MAPPED_SUBRESOURCE mapped = {};
      if (FAILED(native_device_context->Map(capture.staging.get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped)) || mapped.pData == nullptr)
         return; // still in flight, retry next frame

      constexpr uint32_t kBins = 256;
      uint64_t histogram[kBins] = {};
      uint64_t total = 0;
      uint64_t non_finite = 0;
      for (UINT y = 0; y < capture.height; y++)
      {
         const uint16_t* row = (const uint16_t*)((const uint8_t*)mapped.pData + (size_t)y * mapped.RowPitch);
         for (UINT x = 0; x < capture.width; x++)
         {
            const float v = DirectX::PackedVector::XMConvertHalfToFloat(row[x]);
            // The range test, not a clamp: a NaN fails BOTH ordered comparisons, so the clamped form would fall
            // through to (uint32_t)NaN and index far outside this stack array. The extract CS saturates, so a NaN
            // here means it met one upstream; count them rather than bin them, a silent 0 would read as flat.
            if (!(v >= 0.f && v <= 1.f))
            {
               non_finite++;
               total++;
               continue;
            }
            histogram[(uint32_t)(v * (float)(kBins - 1))]++;
            total++;
         }
      }
      native_device_context->Unmap(capture.staging.get(), 0);
      capture.copy_pending = false;

      auto fraction_above = [&](float level)
      {
         uint64_t hits = 0;
         for (uint32_t b = (uint32_t)(level * (float)(kBins - 1)) + 1u; b < kBins; b++)
            hits += histogram[b];
         return 100.0 * (double)hits / (double)total;
      };
      auto percentile = [&](double p)
      {
         const uint64_t target = (uint64_t)(p * (double)total);
         uint64_t running = 0;
         for (uint32_t b = 0; b < kBins; b++)
         {
            running += histogram[b];
            if (running >= target)
               return (float)b / (float)(kBins - 1);
         }
         return 1.f;
      };

      char line[512];
      snprintf(line, sizeof(line),
         "[BL-Pred] tol=%.4f | FIRES(>0.5)=%.3f%% | >0.1=%.3f%% >0.25=%.3f%% >0.75=%.3f%% >0.9=%.3f%% | flat(bin0)=%.2f%% | p50=%.3f p90=%.3f p99=%.3f p999=%.3f | nonfinite=%llu | %ux%u",
         g_smaa_pred_tolerance, fraction_above(0.5f), fraction_above(0.1f), fraction_above(0.25f), fraction_above(0.75f), fraction_above(0.9f),
         100.0 * (double)histogram[0] / (double)total, percentile(0.5), percentile(0.9), percentile(0.99), percentile(0.999),
         (unsigned long long)non_finite, capture.width, capture.height);
      reshade::log::message(reshade::log::level::info, line);
   }
#endif

   // XeGTAO noise per frame and one denoise pass (Intel's XeGTAO.h with TAA; GT-VBAO keeps both) while DLSS / FSR accumulate the lit
   // scene the AO multiplies into; without them a moving pattern would boil, so it stays frozen and denoises twice
   static bool IsGTAOTemporal(DeviceData& device_data)
   {
      return g_gtao_temporal ? g_gtao_temporal == 2 : IsSRActive(device_data);
   }

   // An upscaler is picked and hasn't failed (it then gives way to SMAA until picked again). Fixed for the whole frame (see
   // "OnPresent"): a selection made after the motion vector state was set would otherwise run the upscaler on mixed state.
   static bool IsSRActive(DeviceData& device_data)
   {
      return GetGameDeviceData(device_data).sr_active;
   }

   // The alpha blended draws write the reactive masks for FSR (DLSS's current presets ignore them)
   static bool IsReactiveMaskUsed(DeviceData& device_data)
   {
      return g_sr_reactive_enable && IsSRActive(device_data) && (device_data.sr_type == SR::Type::FSR || device_data.sr_type == SR::Type::FSR_4);
   }

   // A b0 / b1 / b3 buffer's CPU copy (null until its first update); registers it for a copy at every update. Under
   // "mv_constants_mutex".
   static BorderlandsGotyGameDeviceData::ConstantsCopy GetConstantsCopy(BorderlandsGotyGameDeviceData* game_device_data, ID3D11Buffer* buffer)
   {
      if (!buffer)
         return nullptr;
      return game_device_data->mv_constants.Snapshot(game_device_data->mv_constants.Register(buffer));
   }

   // Motion vectors: the CPU copy of a registered b0 / b1 / b3 buffer, from the game's UpdateSubresource (before it runs). The engine
   // uploads whole buffers (no box: "size" is UINT64_MAX).
   static bool OnUpdateBufferRegion(reshade::api::device* device, const void* data, reshade::api::resource resource, uint64_t offset, uint64_t size)
   {
      DeviceData* const device_data = device->get_private_data<DeviceData>();
      if (!device_data || !device_data->game || !data)
         return false;
      auto& game_device_data = GetGameDeviceData(*device_data);
      if (!game_device_data.mv_active)
         return false;
      // Lock free: the hook sees every UpdateSubresource of the game (a few thousand a frame, most into other buffers)
      if (!game_device_data.mv_constants.MayBeRegistered(resource.handle))
         return false;
      const std::lock_guard lock(game_device_data.mv_constants_mutex);
      if (game_device_data.mv_constants.OnUpdate(resource.handle, data, offset, size))
      {
#if DEVELOPMENT
         game_device_data.mv_stats.updates++;
#endif
      }
      return false;
   }

   // Motion vectors: a destroyed b0 / b1 / b3 buffer leaves the registry and the filter (its address can come back as another buffer,
   // of another size)
   static void OnDestroyResource(reshade::api::device* device, reshade::api::resource resource)
   {
      DeviceData* const device_data = device->get_private_data<DeviceData>();
      if (!device_data || !device_data->game)
         return;
      auto& game_device_data = GetGameDeviceData(*device_data);
      if (!game_device_data.mv_constants.MayBeRegistered(resource.handle))
         return;
      const std::lock_guard lock(game_device_data.mv_constants_mutex);
      if (!game_device_data.mv_constants.Unregister(resource.handle))
         return;
#if DEVELOPMENT
      game_device_data.mv_destroyed_buffers++;
#endif
   }

   // Motion vectors: the copies of the scene and of its depth (the last ones before the first post pass are what post reads)
   bool OverrideCopyResource(ID3D11Device* native_device, DeviceData& device_data, uint64_t& dst_resource, uint64_t& src_resource) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);
      if (game_device_data.mv_scene_open)
      {
         if (src_resource == uint64_t(game_device_data.mv_scene_color.get()))
         {
            game_device_data.mv_scene_color_copy = dst_resource;
         }
         else if (src_resource == uint64_t(game_device_data.mv_depth.get()))
         {
            game_device_data.mv_depth_copy = dst_resource;
         }
      }
      return false;
   }
   bool OverrideCopyTextureRegion(ID3D11Device* native_device, DeviceData& device_data, uint64_t& dst_resource, uint32_t dst_subresource, const D3D11_BOX* dst_box, uint64_t& src_resource, uint32_t src_subresource, const D3D11_BOX* src_box) override
   {
      // UE3 copies the scene and its depth with CopySubresourceRegion too (10 a gameplay frame, see "refused_region_copies"). Only a
      // whole texture copy is one of them (the upscaler reads it, and the post passes read it in full).
      auto& game_device_data = GetGameDeviceData(device_data);
      const bool scene_or_depth = src_resource == uint64_t(game_device_data.mv_scene_color.get()) || src_resource == uint64_t(game_device_data.mv_depth.get());
      bool whole = dst_subresource == 0 && src_subresource == 0 && (!dst_box || (dst_box->left == 0 && dst_box->top == 0 && dst_box->front == 0));
      if (whole && src_box && scene_or_depth)
      {
         uint4 size;
         DXGI_FORMAT unused_format;
         GetResourceInfo(reinterpret_cast<ID3D11Resource*>(src_resource), size, unused_format);
         whole = src_box->left == 0 && src_box->top == 0 && src_box->right >= size.x && src_box->bottom >= size.y;
      }
      if (!whole)
      {
#if DEVELOPMENT
         if (game_device_data.mv_scene_open && scene_or_depth)
         {
            game_device_data.mv_stats.refused_region_copies++;
         }
#endif
         return false;
      }
      return OverrideCopyResource(native_device, device_data, dst_resource, src_resource);
   }

   // The bound shader's motion vector version, patched from Core's bytecode copy on first use (a null shader if it can't be); the entry
   // is never erased. Vertex shaders that don't place vertices with b1's ViewProjectionMatrix (height fog quads bind it for the camera
   // position only) are refused: jittered, they would shift against their own UVs.
   template <typename T>
   static const BorderlandsGotyGameDeviceData::PatchedShader<T>& GetMotionVectorShader(ID3D11Device* native_device, DeviceData& device_data, std::unordered_map<uint32_t, BorderlandsGotyGameDeviceData::PatchedShader<T>>* shaders, uint32_t hash, reshade::api::pipeline pipeline, uint8_t reactive = 0)
   {
      constexpr bool vertex = std::is_same_v<T, ID3D11VertexShader>;
      auto& game_device_data = GetGameDeviceData(device_data);
      {
         const std::shared_lock lock(game_device_data.mv_mutex);
         if (const auto it = shaders->find(hash); it != shaders->end())
            return it->second;
      }
      std::vector<uint8_t> patched;
      std::string error = "no bytecode";
      bool screen_space = false;
      BorderlandsGotyGameDeviceData::PatchedShader<T> entry;
      {
         const std::shared_lock lock(s_mutex_generic);
         if (const auto it = device_data.pipeline_cache_by_pipeline_handle.find(pipeline.handle); it != device_data.pipeline_cache_by_pipeline_handle.end() && it->second->subobjects_cache)
         {
            const auto* desc = static_cast<const reshade::api::shader_desc*>(it->second->subobjects_cache[0].data);
            const auto* code = static_cast<const uint8_t*>(desc->code);
            if constexpr (vertex)
            {
               patched = MotionVectorPatch::PatchVertexShader(code, desc->code_size, MotionVectorPatches::layout, &error);
            }
            else if (reactive != 0)
            {
               patched = MotionVectorPatch::PatchPixelShaderReactive(code, desc->code_size, MotionVectorPatches::layout, MotionVectorPatches::reactive_slot, (reactive == 2 ? MotionVectorPatch::ReactiveMode::ADDITIVE : MotionVectorPatch::ReactiveMode::ALPHA), &error);
            }
            else
            {
               patched = MotionVectorPatch::PatchPixelShader(code, desc->code_size, MotionVectorPatches::layout, &error);
            }
            com_ptr<ID3D11ShaderReflection> reflection;
            if (vertex && !patched.empty() && Shader::d3d_reflect && SUCCEEDED(Shader::d3d_reflect(code, desc->code_size, IID_PPV_ARGS(&reflection))))
            {
               D3D11_SHADER_VARIABLE_DESC variable_desc;
               if (FAILED(reflection->GetConstantBufferByName("VSOffsetConstants")->GetVariableByName("ViewProjectionMatrix")->GetDesc(&variable_desc)) || (variable_desc.uFlags & D3D_SVF_USED) == 0)
               {
                  patched.clear();
                  error = "screen space (ViewProjectionMatrix unused)";
                  screen_space = true;
               }
               else
               {
                  D3D11_SHADER_INPUT_BIND_DESC bind_desc;
                  entry.skinned = SUCCEEDED(reflection->GetResourceBindingDescByName("VSBoneConstants", &bind_desc)) && bind_desc.BindPoint == MotionVectorPatches::previous_slots[2].first;
                  // The object's translation, LocalToWorld's 4th row (row vectors)
                  if (SUCCEEDED(reflection->GetConstantBufferByName("$Globals")->GetVariableByName("LocalToWorld")->GetDesc(&variable_desc)) && (variable_desc.uFlags & D3D_SVF_USED) != 0)
                  {
                     entry.translation_offset = variable_desc.StartOffset + 3 * 16;
                  }
                  for (size_t i = 0; i < entry.read_sizes.size(); i++)
                     entry.read_sizes[i] = DXBC::ConstantBufferBytes(code, desc->code_size, MotionVectorPatches::previous_slots[i].first);
               }
            }
         }
      }
      if (!patched.empty())
      {
         HRESULT hr;
         if constexpr (vertex)
         {
            hr = native_device->CreateVertexShader(patched.data(), patched.size(), nullptr, &entry.shader);
         }
         else
         {
            hr = native_device->CreatePixelShader(patched.data(), patched.size(), nullptr, &entry.shader);
         }
         if (FAILED(hr))
         {
            error = std::format("create 0x{:08X}", uint32_t(hr));
         }
      }
      // Failures in every build (bug reports), every patched shader only in development
      if (DEVELOPMENT || !entry.shader)
      {
         reshade::log::message((entry.shader || screen_space) ? reshade::log::level::info : reshade::log::level::warning, std::format("[BL MV] {} 0x{:08X} {}", vertex ? "VS" : (reactive != 0 ? "PS (mask)" : "PS"), hash, entry.shader ? "patched" : error).c_str());
      }
      const std::unique_lock lock(game_device_data.mv_mutex);
      return shaders->try_emplace(hash, std::move(entry)).first->second;
   }

   // The bound vertex shader's patched version (null if refused), looked up again only when the game's changes
   static ID3D11VertexShader* GetPatchedVertexShader(ID3D11Device* native_device, const CommandListData& cmd_list_data, DeviceData& device_data, uint32_t hash)
   {
      auto& game_device_data = GetGameDeviceData(device_data);
      if (hash != game_device_data.mv_last_vertex_shader_hash || !game_device_data.mv_last_vertex_shader)
      {
         game_device_data.mv_last_vertex_shader = &GetMotionVectorShader(native_device, device_data, &game_device_data.mv_vertex_shaders, hash, cmd_list_data.pipeline_state_original_vertex_shader);
         game_device_data.mv_last_vertex_shader_hash = hash;
      }
      return game_device_data.mv_last_vertex_shader->shader.get();
   }

   // Opens the scene at the frame's first mesh draw into output sized depth (the world depth prepass): takes the scene depth and
   // picks the jitter the whole scene draws with. Once per present (the HUD and later passes never reopen it).
   static void OpenScene(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, const CommandListData& cmd_list_data, DeviceData& device_data, uint32_t vertex_shader_hash, ID3D11DepthStencilView* dsv)
   {
      auto& game_device_data = GetGameDeviceData(device_data);
      const uint2 depth_size = GetViewTextureSize(dsv);
      if (depth_size.x != device_data.output_resolution.x || depth_size.y != device_data.output_resolution.y || !GetPatchedVertexShader(native_device, cmd_list_data, device_data, vertex_shader_hash))
         return;
      game_device_data.mv_scene_open = true;
      game_device_data.mv_depth.reset();
      dsv->GetResource(&game_device_data.mv_depth);
      game_device_data.mv_depth_copy = 0;
      game_device_data.mv_scene_color.reset();
      game_device_data.mv_scene_color_copy = 0;
      game_device_data.jitter_dsv = nullptr;
      game_device_data.bound_states.ResetDepthTest();
      game_device_data.mv_accepted_rtv = nullptr;
      game_device_data.mv_accepted_dsv = nullptr;
      game_device_data.mv_blend_state = nullptr;
      game_device_data.mv_blend_opaque = true;
      game_device_data.mv_reactive_blend = 0;
      // Halton (2, 3) over the upscaler's phase count; pixels to NDC (y up)
      const SR::InstanceData* const sr_instance_data = (IsSRActive(device_data) ? device_data.GetSRInstanceData() : nullptr);
      game_device_data.mv_jitter = (((sr_instance_data || g_mv_force_jitter) && !g_mv_disable_jitter) ? GetSRJitter(device_data, sr_instance_data) : std::array<float, 2>{});
      // SMAA T2x: the sample offsets (0.25, 0.25) then (-0.25, -0.25) in y down pixels, for "SMAA.hlsl"'s T2x subsample indices 1 and
      // 2. That is its table read with y up, which measured better. The projection moves the other way.
      game_device_data.t2x_phase = ((game_device_data.t2x_active && !g_mv_disable_jitter && game_device_data.t2x_frames[0] && game_device_data.t2x_last_frame + 1 == cb_luma_global_settings.FrameIndex) ? int(cb_luma_global_settings.FrameIndex & 1) : -1);
      if (game_device_data.t2x_phase >= 0)
      {
         const float shift = (game_device_data.t2x_phase == 0 ? -0.25f : 0.25f);
         game_device_data.mv_jitter = {shift, shift};
      }
      game_device_data.mv_jitter_ndc = {game_device_data.mv_jitter[0] * 2.f / device_data.output_resolution.x, game_device_data.mv_jitter[1] * -2.f / device_data.output_resolution.y};
      const float ndc_jitter[4] = {game_device_data.mv_jitter_ndc[0], game_device_data.mv_jitter_ndc[1], 0.f, 0.f};
      if (!PatchedDraws::WriteDynamicConstants(native_device, native_device_context, std::addressof(game_device_data.mv_jitter_buffer), ndc_jitter, sizeof(ndc_jitter)))
      {
         // No stale jitter on the scene draws either: no motion vectors this frame
         game_device_data.mv_jitter = {};
         game_device_data.mv_jitter_ndc = {};
         game_device_data.mv_jitter_buffer.reset();
         game_device_data.t2x_phase = -1;
      }
   }

#if DEVELOPMENT
#define MV_REJECT(reason) ([&](auto& gd) { gd.mv_stats.rejected[reason]++; gd.mv_draw_reject = int(reason); return false; }(GetGameDeviceData(device_data)))
#else
#define MV_REJECT(reason) false
#endif

   // The camera's translation since the previous frame (see "mv_camera_moves"): the median of this frame's matched world camera objects
   // so far, as moving objects are few. Zero before the first one.
   static const std::array<float, 3>& CameraMove(BorderlandsGotyGameDeviceData* game_device_data)
   {
      auto& moves = game_device_data->mv_camera_moves;
      if (game_device_data->mv_camera_move_samples != moves.size())
      {
         game_device_data->mv_camera_move_samples = moves.size();
         for (size_t axis = 0; axis < 3; axis++)
         {
            const auto middle = moves.begin() + moves.size() / 2;
            std::nth_element(moves.begin(), middle, moves.end(), [axis](const auto& a, const auto& b)
               { return a[axis] < b[axis]; });
            game_device_data->mv_camera_move[axis] = (*middle)[axis];
         }
      }
      return game_device_data->mv_camera_move;
   }

   // A row vector view projection (b1's first 4 rows) for points translated by "move": row 3 += move * rows 0-2
   template <typename T>
   static void MoveViewProjection(T* view_projection, const std::array<float, 3>& move)
   {
      for (size_t column = 0; column < 4; column++)
         view_projection[12 + column] += move[0] * view_projection[column] + move[1] * view_projection[4 + column] + move[2] * view_projection[8 + column];
   }

   // The fill's and the AO history's constants, a multiple of 16 bytes: [0-15] current clip space to the previous frame's, for
   // column vectors (b1 holds the row vector matrix, transposed here), in double, identity without both cameras; [16-17] this frame's
   // NDC jitter; [18-23] the caller's
   static std::array<float, 24> GetCameraReprojectionConstants(BorderlandsGotyGameDeviceData* game_device_data)
   {
      Math::Matrix44D current, previous;
      current.SetIdentity();
      previous.SetIdentity();
      if (game_device_data->mv_camera && game_device_data->mv_previous_camera && game_device_data->mv_camera->size() >= 16 * sizeof(float) && game_device_data->mv_previous_camera->size() >= 16 * sizeof(float))
      {
         std::copy_n(reinterpret_cast<const float*>(game_device_data->mv_camera->data()), 16, current.GetData());
         std::copy_n(reinterpret_cast<const float*>(game_device_data->mv_previous_camera->data()), 16, previous.GetData());
         // Depth unprojects to this frame's translated world (see "mv_camera_moves")
         MoveViewProjection(previous.GetData(), CameraMove(game_device_data));
         current.Transpose();
         previous.Transpose();
         current.Invert();
      }
      const Math::Matrix44D reprojection = previous * current;
      std::array<float, 24> constants = {};
      for (int i = 0; i < 16; i++)
      {
         constants[i] = float(reprojection.GetData()[i]);
      }
      constants[16] = game_device_data->mv_jitter_ndc[0];
      constants[17] = game_device_data->mv_jitter_ndc[1];
      return constants;
   }

   // The tracked bindings the "PatchedDraws::BoundStates" events update: the immediate context's
   static auto* GetBoundStates(reshade::api::command_list* cmd_list)
   {
      DeviceData* const device_data = cmd_list->get_device()->get_private_data<DeviceData>();
      const CommandListData* const cmd_list_data = cmd_list->get_private_data<CommandListData>();
      return ((device_data && device_data->game && cmd_list_data && cmd_list_data->is_primary) ? &GetGameDeviceData(*device_data).bound_states : nullptr);
   }

   // Classifies the bound blend state (cached in "mv_blend_state"): "mv_blend_opaque" for the motion vectors (additive lights, decals
   // and translucents keep the motion vectors of what's behind them, and so do draws that write no color: occlusion query bounding
   // boxes, every blend state writes the motion vector target, see "OnCreateBlendState"), and "mv_reactive_blend" for the masks: 0 not
   // alpha blended (opaque, additive ONE/ONE lights, modulated shadows), 1 alpha blended (SRC_ALPHA / INV_SRC_ALPHA: smoke, glass,
   // water; reactive and transparency & composition), 2 additive (SRC_ALPHA / ONE: sparks, glows; reactive)
   static void ClassifyBoundBlend(ID3D11DeviceContext* native_device_context, BorderlandsGotyGameDeviceData* game_device_data)
   {
      ID3D11BlendState* const blend_state = game_device_data->bound_states.BlendState(native_device_context);
      if (blend_state == game_device_data->mv_blend_state)
      {
         return;
      }
      D3D11_BLEND_DESC blend_desc = CD3D11_BLEND_DESC(D3D11_DEFAULT);
      if (blend_state)
      {
         blend_state->GetDesc(&blend_desc);
      }
      const D3D11_RENDER_TARGET_BLEND_DESC& rt0 = blend_desc.RenderTarget[0];
      game_device_data->mv_blend_opaque = rt0.RenderTargetWriteMask != 0 && (!rt0.BlendEnable || (rt0.SrcBlend == D3D11_BLEND_ONE && rt0.DestBlend == D3D11_BLEND_ZERO && rt0.BlendOp == D3D11_BLEND_OP_ADD));
      game_device_data->mv_reactive_blend = ((!rt0.BlendEnable || rt0.SrcBlend != D3D11_BLEND_SRC_ALPHA) ? 0 : (rt0.DestBlend == D3D11_BLEND_ONE ? 2 : 1));
      game_device_data->mv_blend_state = blend_state;
   }

   // Draws an opaque draw into the fp16 scene (the world and weapon base passes: the scene target alone, output sized, with the scene
   // depth) with the patched shaders, adding the motion vector target ("target_slot", past the game's) and the previous frame's
   // b0 / b1 / b3 ("previous_slots"). False if it can't (the draw then goes to "DrawWithJitter").
   static bool DrawWithMotionVectors(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, const CommandListData& cmd_list_data, DeviceData& device_data, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, const std::function<void()>& draw, const com_ptr<ID3D11RenderTargetView> (&rtvs)[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT], ID3D11DepthStencilView* dsv)
   {
      auto& game_device_data = GetGameDeviceData(device_data);
      // The scene target alone, plus the motion vector and mask targets the last draws left bound
      for (UINT slot = 1; slot < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; slot++)
      {
         if (rtvs[slot] && (slot != MotionVectorPatches::target_slot || rtvs[slot] != game_device_data.mv_rtv) && (slot != MotionVectorPatches::reactive_slot || rtvs[slot] != game_device_data.mv_reactive_target_rtv))
            return MV_REJECT(0);
      }
      if (!rtvs[0] || !dsv || !game_device_data.mv_scene_open)
         return MV_REJECT(1);
      // Before a target is accepted: a colorless draw (UE3's occlusion query boxes) into another target would claim it as the scene
      ClassifyBoundBlend(native_device_context, &game_device_data);
      if (!game_device_data.mv_blend_opaque)
         return MV_REJECT(6);
      // Known targets: checked, and the motion vector target built for them
      if (rtvs[0].get() != game_device_data.mv_accepted_rtv || dsv != game_device_data.mv_accepted_dsv)
      {
         com_ptr<ID3D11Resource> depth;
         dsv->GetResource(&depth);
         com_ptr<ID3D11Resource> color;
         rtvs[0]->GetResource(&color);
         if (depth != game_device_data.mv_depth || !color || (game_device_data.mv_scene_color && color != game_device_data.mv_scene_color))
            return MV_REJECT(2);
         // UE3 views its single sample targets as multisampled (TEXTURE2DMS)
         D3D11_RENDER_TARGET_VIEW_DESC rtv_desc;
         rtvs[0]->GetDesc(&rtv_desc);
         com_ptr<ID3D11Texture2D> color_texture;
         D3D11_TEXTURE2D_DESC color_desc = {};
         if (SUCCEEDED(color->QueryInterface(&color_texture)))
         {
            color_texture->GetDesc(&color_desc);
         }
#if DEVELOPMENT
         game_device_data.mv_stats.rejected_format = rtv_desc.Format;
         game_device_data.mv_stats.rejected_dimension = rtv_desc.ViewDimension;
         game_device_data.mv_stats.rejected_width = color_desc.Width;
         game_device_data.mv_stats.rejected_height = color_desc.Height;
#endif
         if (rtv_desc.Format != DXGI_FORMAT_R16G16B16A16_FLOAT || (rtv_desc.ViewDimension != D3D11_RTV_DIMENSION_TEXTURE2D && rtv_desc.ViewDimension != D3D11_RTV_DIMENSION_TEXTURE2DMS) || color_desc.SampleDesc.Count != 1)
            return MV_REJECT(3);
         const uint2 size = {color_desc.Width, color_desc.Height};
         if (size.x != device_data.output_resolution.x || size.y != device_data.output_resolution.y)
            return MV_REJECT(4);
         game_device_data.mv_scene_color = color;
         const std::unique_lock lock(game_device_data.mv_mutex);
         D3D11_TEXTURE2D_DESC desc = {};
         if (game_device_data.mv_texture)
         {
            game_device_data.mv_texture->GetDesc(&desc);
         }
         // R16G16_FLOAT: DLSS takes it (and RG32), FSR keeps 16 bits internally. The error is under 0.1% of the motion, and it is
         // cheaper than R32G32 for the upscaler and the DOF gather that read it.
         constexpr DXGI_FORMAT format = DXGI_FORMAT_R16G16_FLOAT;
         if (desc.Width != size.x || desc.Height != size.y)
         {
            game_device_data.ReleaseMotionVectorTarget();
            // The fill reads the target back through its UAV
            D3D11_FEATURE_DATA_FORMAT_SUPPORT2 support = {format};
            const bool typed_uav_load = SUCCEEDED(native_device->CheckFeatureSupport(D3D11_FEATURE_FORMAT_SUPPORT2, &support, sizeof(support))) && (support.OutFormatSupport2 & D3D11_FORMAT_SUPPORT2_UAV_TYPED_LOAD) != 0;
            desc = {.Width = size.x, .Height = size.y, .MipLevels = 1, .ArraySize = 1, .Format = format, .SampleDesc = {.Count = 1}, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE | (typed_uav_load ? D3D11_BIND_UNORDERED_ACCESS : 0u)};
            if (FAILED(native_device->CreateTexture2D(&desc, nullptr, &game_device_data.mv_texture)) || FAILED(native_device->CreateRenderTargetView(game_device_data.mv_texture.get(), nullptr, &game_device_data.mv_rtv)))
            {
               game_device_data.mv_texture.reset();
               game_device_data.mv_rtv.reset();
               return MV_REJECT(5);
            }
            native_device->CreateShaderResourceView(game_device_data.mv_texture.get(), nullptr, &game_device_data.mv_srv);
            if (typed_uav_load)
            {
               native_device->CreateUnorderedAccessView(game_device_data.mv_texture.get(), nullptr, &game_device_data.mv_uav);
            }
            game_device_data.mv_frame_ended = true;
         }
         game_device_data.mv_accepted_rtv = rtvs[0].get();
         game_device_data.mv_accepted_dsv = dsv;
      }

      ID3D11VertexShader* const vertex_shader = GetPatchedVertexShader(native_device, cmd_list_data, device_data, original_shader_hashes.vertex_shaders[0]);
      if (const uint32_t pixel_shader_hash = original_shader_hashes.pixel_shaders[0]; pixel_shader_hash != game_device_data.mv_last_pixel_shader_hash)
      {
         game_device_data.mv_last_pixel_shader = GetMotionVectorShader(native_device, device_data, &game_device_data.mv_pixel_shaders, pixel_shader_hash, cmd_list_data.pipeline_state_original_pixel_shader).shader.get();
         game_device_data.mv_last_pixel_shader_hash = pixel_shader_hash;
      }
      ID3D11PixelShader* const pixel_shader = game_device_data.mv_last_pixel_shader;
      if (!vertex_shader || !pixel_shader || !game_device_data.mv_jitter_buffer)
         return MV_REJECT(7);
      if (std::exchange(game_device_data.mv_frame_ended, false))
      {
         // The camera fill's and the upscaler's depth: the scene depth if it can be read, else its copy (taken after the depth prepass)
         game_device_data.mv_depth_srv.reset();
         com_ptr<ID3D11Texture2D> depth_texture;
         D3D11_TEXTURE2D_DESC depth_desc = {};
         if (game_device_data.mv_depth && SUCCEEDED(game_device_data.mv_depth->QueryInterface(&depth_texture)))
         {
            depth_texture->GetDesc(&depth_desc);
         }
         if ((depth_desc.BindFlags & D3D11_BIND_SHADER_RESOURCE) == 0 && game_device_data.mv_depth_copy)
         {
            depth_texture.reset();
            depth_desc = {};
            if (SUCCEEDED(reinterpret_cast<ID3D11Resource*>(game_device_data.mv_depth_copy)->QueryInterface(&depth_texture)))
            {
               depth_texture->GetDesc(&depth_desc);
            }
         }
         if ((depth_desc.BindFlags & D3D11_BIND_SHADER_RESOURCE) != 0 && (depth_desc.Format == DXGI_FORMAT_R24G8_TYPELESS || depth_desc.Format == DXGI_FORMAT_R32G8X24_TYPELESS || depth_desc.Format == DXGI_FORMAT_R32_TYPELESS) && depth_desc.SampleDesc.Count == 1)
         {
            const DXGI_FORMAT srv_format = (depth_desc.Format == DXGI_FORMAT_R24G8_TYPELESS ? DXGI_FORMAT_R24_UNORM_X8_TYPELESS : (depth_desc.Format == DXGI_FORMAT_R32G8X24_TYPELESS ? DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS : DXGI_FORMAT_R32_FLOAT));
            const CD3D11_SHADER_RESOURCE_VIEW_DESC srv_desc(D3D11_SRV_DIMENSION_TEXTURE2D, srv_format, 0, 1);
            native_device->CreateShaderResourceView(depth_texture.get(), &srv_desc, &game_device_data.mv_depth_srv);
         }
         game_device_data.mv_fill_pending = game_device_data.mv_depth_srv && game_device_data.mv_uav && FindShader(device_data.native_compute_shaders, CompileTimeStringHash("BL Motion Vector Fill CS")) != nullptr;
         // The fill's marker: the largest float16 (a larger clear value is stored as it in R16G16_FLOAT)
         const FLOAT clear_value = (game_device_data.mv_fill_pending ? 65504.f : 0.f);
         const FLOAT clear[4] = {clear_value, clear_value, 0.f, 0.f};
         native_device_context->ClearRenderTargetView(game_device_data.mv_rtv.get(), clear);
         // The reactive masks: what the alpha blended draws write, from zero
         if (IsReactiveMaskUsed(device_data) && (!game_device_data.mv_reactive_target || !game_device_data.mv_reactive_uav || !game_device_data.mv_transparency_uav))
         {
            D3D11_TEXTURE2D_DESC mv_desc = {};
            game_device_data.mv_texture->GetDesc(&mv_desc);
            const CD3D11_TEXTURE2D_DESC reactive_desc(DXGI_FORMAT_R8G8_UNORM, mv_desc.Width, mv_desc.Height, 1, 1, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
            const CD3D11_TEXTURE2D_DESC mask_desc(DXGI_FORMAT_R8_UNORM, mv_desc.Width, mv_desc.Height, 1, 1, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS);
            const std::unique_lock lock(game_device_data.mv_mutex);
            game_device_data.ReleaseReactiveMasks();
            if (SUCCEEDED(native_device->CreateTexture2D(&reactive_desc, nullptr, &game_device_data.mv_reactive_target)) && SUCCEEDED(native_device->CreateRenderTargetView(game_device_data.mv_reactive_target.get(), nullptr, &game_device_data.mv_reactive_target_rtv)))
            {
               native_device->CreateShaderResourceView(game_device_data.mv_reactive_target.get(), nullptr, &game_device_data.mv_reactive_target_srv);
            }
            if (SUCCEEDED(native_device->CreateTexture2D(&mask_desc, nullptr, &game_device_data.mv_reactive)))
            {
               native_device->CreateUnorderedAccessView(game_device_data.mv_reactive.get(), nullptr, &game_device_data.mv_reactive_uav);
            }
            if (SUCCEEDED(native_device->CreateTexture2D(&mask_desc, nullptr, &game_device_data.mv_transparency)))
            {
               native_device->CreateUnorderedAccessView(game_device_data.mv_transparency.get(), nullptr, &game_device_data.mv_transparency_uav);
            }
            game_device_data.sr_reactive_frame = cb_luma_global_settings.FrameIndex;
         }
         if (game_device_data.mv_reactive_target_rtv)
         {
            const FLOAT zero[4] = {};
            native_device_context->ClearRenderTargetView(game_device_data.mv_reactive_target_rtv.get(), zero);
         }
         // Last frame's camera and objects are the previous ones, unless frames without a scene (menus, videos) came between
         const bool previous_valid = game_device_data.mv_camera && cb_luma_global_settings.FrameIndex - game_device_data.mv_frame_index <= 1;
         game_device_data.mv_previous_camera = (previous_valid ? game_device_data.mv_camera : nullptr);
         game_device_data.mv_camera = nullptr;
         game_device_data.mv_frame_index = cb_luma_global_settings.FrameIndex;
         game_device_data.mv_camera_moves.clear();
         game_device_data.mv_camera_move_samples = 0;
         game_device_data.mv_camera_move = {};
         game_device_data.mv_objects.EndFrame(previous_valid);
      }

      // The game's b0 / b1 / b3. The slots added past them stay bound after the draw: no game vertex shader reads a constant buffer
      // at slot 4 or above.
      com_ptr<ID3D11Buffer> game_cbs[4];
      native_device_context->VSGetConstantBuffers(0, UINT(std::size(game_cbs)), &game_cbs[0]);
      ID3D11Buffer* const current[std::size(MotionVectorPatches::previous_slots)] = {game_cbs[MotionVectorPatches::previous_slots[0].first].get(), game_cbs[MotionVectorPatches::previous_slots[1].first].get(), game_cbs[MotionVectorPatches::previous_slots[2].first].get()};
      const bool skinned = game_device_data.mv_last_vertex_shader->skinned;
      BorderlandsGotyGameDeviceData::ConstantsCopy object, camera, bones;
      {
         const std::lock_guard lock(game_device_data.mv_constants_mutex);
         object = GetConstantsCopy(&game_device_data, current[0]);
         camera = GetConstantsCopy(&game_device_data, current[1]);
         if (skinned)
         {
            bones = GetConstantsCopy(&game_device_data, current[2]);
         }
      }
      const auto copy_size = [](const BorderlandsGotyGameDeviceData::ConstantsCopy& copy)
      { return copy ? copy->size() : size_t(0); };
      // The previous frame's b0 / b1 / b3: the same object's from last frame, else this draw's with last frame's world camera (no object
      // motion). None (no CPU copy yet, or an unmatched weapon draw): the current ones (zero motion).
      const std::vector<uint8_t>* uploads[std::size(MotionVectorPatches::previous_slots)] = {};
      if (object && camera && (!skinned || bones))
      {
         // The world camera: the frame's first motion vector draw's (the world base pass comes before the weapon's)
         if (!game_device_data.mv_camera)
         {
            game_device_data.mv_camera = camera;
         }

         // Draw key: same mesh, same shaders. Objects sharing it (props) are told apart by translation. No instance count.
         UINT vertex_offset = 0;
         ID3D11Buffer* const vertex_buffer = game_device_data.bound_states.VertexBuffer(native_device_context, &vertex_offset);
         UINT index_offset = 0;
         ID3D11Buffer* const index_buffer = game_device_data.bound_states.IndexBuffer(native_device_context, &index_offset);
         const DrawDispatchData& draw_data = last_draw_dispatch_data;
         uint64_t key = 0;
         for (const uint64_t value : {uint64_t(original_shader_hashes.vertex_shaders[0]), uint64_t(original_shader_hashes.pixel_shaders[0]), reinterpret_cast<uint64_t>(vertex_buffer), uint64_t(vertex_offset), reinterpret_cast<uint64_t>(index_buffer), uint64_t(index_offset), uint64_t(draw_data.index_count), uint64_t(draw_data.first_index), uint64_t(uint32_t(draw_data.vertex_offset)), uint64_t(draw_data.vertex_count), uint64_t(draw_data.first_vertex)})
            HashCombine(key, value);
         PatchedDraws::ObjectTransform transform = {};
         const bool translated = game_device_data.mv_last_vertex_shader->translation_offset != UINT_MAX;
         if (const uint32_t offset = game_device_data.mv_last_vertex_shader->translation_offset; translated && offset + 3 * sizeof(float) <= object->size())
         {
            transform = PatchedDraws::ReadRowVectorTransform(object->data() + offset - 3 * 16);
         }

         // The same object at the same transform by its draw order or the exact index, else a linear search among the key's candidates
         const BorderlandsGotyGameDeviceData::MotionVectorObject* const match = game_device_data.mv_objects.FindNearest(key, transform, [&](const auto& candidate)
            { return copy_size(candidate.object) == object->size() && copy_size(candidate.camera) == camera->size() && copy_size(candidate.bones) == copy_size(bones); });
         const bool world_camera = PatchedDraws::SameBytes(camera, game_device_data.mv_camera);
         if (match)
         {
            // Last frame's list outlives the draw (the previous objects only change at the next frame start)
            uploads[0] = match->object.get();
            uploads[1] = match->camera.get();
            if (skinned)
            {
               uploads[2] = match->bones.get();
            }
            if (translated && world_camera)
            {
               game_device_data.mv_camera_moves.push_back({match->transform[9] - transform[9], match->transform[10] - transform[10], match->transform[11] - transform[11]});
            }
#if DEVELOPMENT
            game_device_data.mv_stats.matched++;
#endif
         }
         else if (game_device_data.mv_previous_camera && copy_size(game_device_data.mv_previous_camera) == camera->size() && world_camera)
         {
            // Not found, drawn with the world camera: its own constants with last frame's world camera (camera motion only)
            uploads[1] = game_device_data.mv_previous_camera.get();
#if DEVELOPMENT
            game_device_data.mv_stats.camera_only++;
#endif
         }
#if DEVELOPMENT
         else
         {
            game_device_data.mv_stats.other_camera++;
         }
#endif
         // Without LocalToWorld the vertices are already translated by this frame's camera: the previous camera moves back by its translation
         if (!translated && world_camera && uploads[1] && uploads[1]->size() >= 16 * sizeof(float))
         {
            game_device_data.mv_moved_camera.assign(uploads[1]->begin(), uploads[1]->end());
            MoveViewProjection(reinterpret_cast<float*>(game_device_data.mv_moved_camera.data()), CameraMove(&game_device_data));
            uploads[1] = &game_device_data.mv_moved_camera;
         }
         // Kept as drawn for the next frame
         game_device_data.mv_objects.Push(key, {transform, std::move(object), std::move(camera), std::move(bones)}, translated);
      }
#if DEVELOPMENT
      else
      {
         game_device_data.mv_stats.uncopied++;
      }
#endif
      // Uploads only the bytes the vertex shader reads (the engine's buffers are pooled by size, larger than most shaders' constants)
      game_device_data.mv_previous_constants.Bind(native_device, native_device_context, MotionVectorPatches::previous_slots, uploads, current, "BL", std::span<const UINT>(game_device_data.mv_last_vertex_shader->read_sizes));
      ID3D11Buffer* const jitter = game_device_data.mv_jitter_buffer.get();
      native_device_context->VSSetConstantBuffers(MotionVectorPatches::jitter_slot, 1, &jitter);
      // Left bound after the draw (set directly, bypassing Core's state tracking): the game's next draws either bind their own
      // targets and shaders, or are motion vector draws too. The draws in between write no "o4" (no game pixel shader in the scene
      // declares a fifth target), so the motion vector target keeps its contents.
      if (rtvs[MotionVectorPatches::target_slot] != game_device_data.mv_rtv)
      {
         ID3D11RenderTargetView* targets[MotionVectorPatches::target_slot + 1] = {rtvs[0].get()};
         targets[MotionVectorPatches::target_slot] = game_device_data.mv_rtv.get();
         native_device_context->OMSetRenderTargets(MotionVectorPatches::target_slot + 1, targets, dsv);
      }
      PatchedDraws::BindPatchedShader(native_device_context, vertex_shader, &game_device_data.mv_bound_vertex_shader);
      PatchedDraws::BindPatchedShader(native_device_context, pixel_shader, &game_device_data.mv_bound_pixel_shader);

      draw();
#if DEVELOPMENT
      game_device_data.mv_stats.motion_vector_draws++;
#endif
      return true;
   }

   // Jitter for the scene's mesh draws without motion vectors (patched vertex shader, game pixel shader): the depth prepasses,
   // additive lights, decals, fog volumes, distortion and translucents. Every draw depth tested against the scene takes the same
   // jitter, or jittered and unjittered depths of the same surface fail each other's test. False if it can't (the draw runs untouched).
   static bool DrawWithJitter(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, const CommandListData& cmd_list_data, DeviceData& device_data, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, const std::function<void()>& draw, const com_ptr<ID3D11RenderTargetView> (&rtvs)[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT], ID3D11DepthStencilView* dsv)
   {
      auto& game_device_data = GetGameDeviceData(device_data);
      if (!game_device_data.mv_scene_open || game_device_data.mv_jitter == std::array<float, 2>{} || !game_device_data.mv_jitter_buffer)
         return false;
      // Meshes only (full screen passes have no vertex buffer or no depth test), into the scene depth (not shadows)
      if (!dsv)
         return false;
      if (dsv != game_device_data.jitter_dsv)
      {
         com_ptr<ID3D11Resource> depth;
         dsv->GetResource(&depth);
         game_device_data.jitter_dsv = dsv;
         game_device_data.jitter_dsv_scene = depth == game_device_data.mv_depth;
      }
      if (!game_device_data.jitter_dsv_scene || !game_device_data.bound_states.DepthTested(native_device_context))
         return false;
      if (!game_device_data.bound_states.VertexBuffer(native_device_context))
         return false;
      ID3D11VertexShader* const vertex_shader = GetPatchedVertexShader(native_device, cmd_list_data, device_data, original_shader_hashes.vertex_shaders[0]);
      if (!vertex_shader)
         return false;

      // An alpha blended draw into the scene writes its mask, reactive or transparency & composition (its pixel shader patched, the mask
      // target added past the motion vector one)
      ID3D11PixelShader* reactive_shader = nullptr;
      if (IsReactiveMaskUsed(device_data) && game_device_data.mv_reactive_target_rtv && rtvs[0] && rtvs[0].get() == game_device_data.mv_accepted_rtv)
      {
         ClassifyBoundBlend(native_device_context, &game_device_data);
         if (const uint8_t blend = game_device_data.mv_reactive_blend; blend != 0)
         {
            reactive_shader = GetMotionVectorShader(native_device, device_data, &game_device_data.mv_reactive_pixel_shaders[blend - 1], original_shader_hashes.pixel_shaders[0], cmd_list_data.pipeline_state_original_pixel_shader, blend).shader.get();
         }
      }

      // The patched vertex shader and the jitter stay bound after the draw (see "DrawWithMotionVectors"), with the game's pixel shader
      // (a motion vector draw's is put back) or its mask version, and the mask target
      PatchedDraws::BindPatchedShader(native_device_context, vertex_shader, &game_device_data.mv_bound_vertex_shader);
      ID3D11Buffer* const jitter = game_device_data.mv_jitter_buffer.get();
      native_device_context->VSSetConstantBuffers(MotionVectorPatches::jitter_slot, 1, &jitter);
      if (reactive_shader)
      {
         if (rtvs[MotionVectorPatches::reactive_slot] != game_device_data.mv_reactive_target_rtv)
         {
            ID3D11RenderTargetView* targets[MotionVectorPatches::reactive_slot + 1] = {};
            for (uint32_t slot = 0; slot < MotionVectorPatches::reactive_slot; slot++)
            {
               targets[slot] = rtvs[slot].get();
            }
            targets[MotionVectorPatches::reactive_slot] = game_device_data.mv_reactive_target_rtv.get();
            native_device_context->OMSetRenderTargets(MotionVectorPatches::reactive_slot + 1, targets, dsv);
         }
         PatchedDraws::BindPatchedShader(native_device_context, reactive_shader, &game_device_data.mv_bound_pixel_shader);
#if DEVELOPMENT
         game_device_data.mv_stats.reactive_draws++;
#endif
      }
      else
      {
         PatchedDraws::RestoreGameShader(native_device_context, &game_device_data.mv_bound_pixel_shader);
      }
      draw();
#if DEVELOPMENT
      game_device_data.mv_stats.jitter_draws++;
#endif
      return true;
   }

   // DLSS / FSR on the jittered scene (its last copy, which the DOF/Bloom gather and the uber post read, else the scene itself), the
   // scene depth and the motion vectors, at native resolution; the uber and the gather read its output in place of its input (see
   // "OnDrawOrDispatch"). False if it didn't draw (missing input, or the upscaler failed).
   static bool DrawUpscaler(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, DeviceData& device_data, bool reactive_masks)
   {
      auto& game_device_data = GetGameDeviceData(device_data);
      if (!game_device_data.mv_texture || !game_device_data.mv_camera || game_device_data.mv_camera->size() < 16 * sizeof(float) || !game_device_data.mv_depth_srv || !game_device_data.mv_scene_color)
         return false;
      ID3D11Resource* const scene_resource = (game_device_data.mv_scene_color_copy ? reinterpret_cast<ID3D11Resource*>(game_device_data.mv_scene_color_copy) : game_device_data.mv_scene_color.get());
      com_ptr<ID3D11Texture2D> scene;
      if (FAILED(scene_resource->QueryInterface(&scene)))
         return false;
      D3D11_TEXTURE2D_DESC scene_desc = {}, mv_desc = {};
      scene->GetDesc(&scene_desc);
      game_device_data.mv_texture->GetDesc(&mv_desc);
      if ((scene_desc.Format != DXGI_FORMAT_R16G16B16A16_FLOAT && scene_desc.Format != DXGI_FORMAT_R16G16B16A16_TYPELESS) || scene_desc.SampleDesc.Count != 1 || (scene_desc.BindFlags & D3D11_BIND_SHADER_RESOURCE) == 0 || scene_desc.Width != mv_desc.Width || scene_desc.Height != mv_desc.Height)
         return false;
      // None when "Super Resolution" changed after present (see "IsSRActive"): no output texture is made for it
      SR::InstanceData* const sr_instance_data = device_data.GetSRInstanceData();
      if (!sr_instance_data)
         return false;
      com_ptr<ID3D11Resource> depth;
      game_device_data.mv_depth_srv->GetResource(&depth);

      D3D11_TEXTURE2D_DESC output_desc = {};
      if (device_data.sr_output_color)
      {
         device_data.sr_output_color->GetDesc(&output_desc);
      }
      if (output_desc.Width != scene_desc.Width || output_desc.Height != scene_desc.Height)
      {
         device_data.sr_output_color.reset();
         output_desc = {.Width = scene_desc.Width, .Height = scene_desc.Height, .MipLevels = 1, .ArraySize = 1, .Format = DXGI_FORMAT_R16G16B16A16_FLOAT, .SampleDesc = {.Count = 1}, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS};
         game_device_data.sr_output_srv.reset();
         if (SUCCEEDED(native_device->CreateTexture2D(&output_desc, nullptr, &device_data.sr_output_color)) && FAILED(native_device->CreateShaderResourceView(device_data.sr_output_color.get(), nullptr, &game_device_data.sr_output_srv)))
         {
            device_data.sr_output_color.reset();
         }
         game_device_data.sr_output_recreated = true;
      }
      if (!device_data.sr_output_color)
      {
         // Back to SMAA until the upscaler is picked again
         device_data.sr_suppressed = true;
         return false;
      }

      // FSR needs the camera (DLSS ignores it): b1's ViewProjectionMatrix multiplies row vectors, with an infinite far plane
      const ViewProjectionCamera camera = GetViewProjectionCamera(reinterpret_cast<const float*>(game_device_data.mv_camera->data()));

      SR::SettingsData settings_data;
      settings_data.output_width = scene_desc.Width;
      settings_data.output_height = scene_desc.Height;
      settings_data.render_width = scene_desc.Width;
      settings_data.render_height = scene_desc.Height;
      settings_data.dynamic_resolution = false;
      settings_data.hdr = true;
      settings_data.inverted_depth = false;
      settings_data.mvs_jittered = false;
      // The motion vectors are UV deltas, previous minus current
      settings_data.mvs_x_scale = float(scene_desc.Width);
      settings_data.mvs_y_scale = float(scene_desc.Height);
      settings_data.auto_exposure = device_data.sr_type != SR::Type::FSR && device_data.sr_type != SR::Type::FSR_4; // FSR's auto exposure clips highlights
      settings_data.render_preset = dlss_render_preset;
      sr_implementations[device_data.sr_type]->UpdateSettings(sr_instance_data, native_device_context, settings_data);

      SR::SuperResolutionImpl::DrawData draw_data;
      draw_data.source_color = scene.get();
      draw_data.output_color = device_data.sr_output_color.get();
      draw_data.motion_vectors = game_device_data.mv_texture.get();
      draw_data.depth_buffer = depth.get();
      // FSR's masks, when this scene's fill wrote them
      draw_data.bias_mask = (reactive_masks ? game_device_data.mv_reactive.get() : nullptr);
      draw_data.transparency_alpha = (reactive_masks ? game_device_data.mv_transparency.get() : nullptr);
      // Without auto exposure, FSR 4 refuses a dispatch that has no exposure texture (FFX_API_RETURN_ERROR_PARAMETER)
      if (device_data.sr_type == SR::Type::FSR_4)
      {
         if (!device_data.sr_exposure)
         {
            const D3D11_TEXTURE2D_DESC exposure_desc = {
               .Width = 1,
               .Height = 1,
               .MipLevels = 1,
               .ArraySize = 1,
               .Format = DXGI_FORMAT_R32_FLOAT,
               .SampleDesc = {.Count = 1},
               .Usage = D3D11_USAGE_IMMUTABLE,
               .BindFlags = D3D11_BIND_SHADER_RESOURCE,
            };
            const D3D11_SUBRESOURCE_DATA exposure_data = {.pSysMem = &device_data.sr_exposure_texture_value, .SysMemPitch = sizeof(float)};
            native_device->CreateTexture2D(&exposure_desc, &exposure_data, &device_data.sr_exposure);
         }
         draw_data.exposure = device_data.sr_exposure.get();
      }
      draw_data.render_width = scene_desc.Width;
      draw_data.render_height = scene_desc.Height;
      // As applied (pixels, +y down)
      draw_data.jitter_x = game_device_data.mv_jitter[0];
      draw_data.jitter_y = game_device_data.mv_jitter[1];
      draw_data.reset = device_data.force_reset_sr;
      if (camera.vert_fov > 0.0)
      {
         draw_data.vert_fov = float(camera.vert_fov);
      }
      if (camera.near_plane > 0.0)
      {
         draw_data.near_plane = float(camera.near_plane);
         draw_data.far_plane = float(camera.far_plane);
      }
      if (!sr_implementations[device_data.sr_type]->Draw(sr_instance_data, native_device_context, draw_data))
      {
         // Back to SMAA until the upscaler is picked again
         device_data.sr_suppressed = true;
         return false;
      }
      // The post passes after it read the output in place of the input (see "OnDrawOrDispatch"); nothing reads the scene or its copy after
      // the gather (probed in gameplay, the main menu, the inventory, the sniper scope and Fight For Your Life)
      game_device_data.sr_input = scene_resource;
      // DLSS draws nothing into a new output texture (the session's first, or one made after "None", which Core frees): the frame shows
      // the texture's stale memory until its feature is created again after a draw. Settings changed once here force that at the next
      // frame's "UpdateSettings".
      if (std::exchange(game_device_data.sr_output_recreated, false) && device_data.sr_type == SR::Type::DLSS)
      {
         SR::SettingsData throwaway_settings_data = settings_data;
         throwaway_settings_data.mvs_jittered = !throwaway_settings_data.mvs_jittered;
         sr_implementations[device_data.sr_type]->UpdateSettings(sr_instance_data, native_device_context, throwaway_settings_data);
      }
      device_data.has_drawn_sr = true;
      return true;
   }

   // Ends the scene at its first post pass (immediate context): the camera motion fill for the pixels no motion vector draw wrote, then
   // DLSS / FSR ("DrawUpscaler"), both before that pass reads the scene
   static void EndScene(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, DeviceData& device_data)
   {
      auto& game_device_data = GetGameDeviceData(device_data);
      game_device_data.mv_scene_open = false;
      game_device_data.mv_scene_done = true;
      DrawStateStack<DrawStateStackType::FullGraphics> graphics_state;
      DrawStateStack<DrawStateStackType::Compute> compute_state;
      graphics_state.Cache(native_device_context, device_data.uav_max_count);
      compute_state.Cache(native_device_context, device_data.uav_max_count);
      auto* const fill_shader = FindShader(device_data.native_compute_shaders, CompileTimeStringHash("BL Motion Vector Fill CS"));
      // The reactive and transparency & composition masks, written by the fill from what the alpha blended draws wrote
      const bool write_reactive = IsReactiveMaskUsed(device_data) && game_device_data.mv_reactive_target_srv && game_device_data.mv_reactive_uav && game_device_data.mv_transparency_uav;
      bool reactive_written = false;
      if (std::exchange(game_device_data.mv_fill_pending, false) && fill_shader)
      {
         std::array<float, 24> constants = GetCameraReprojectionConstants(&game_device_data);
         constants[18] = g_sr_reactive_scale;
         constants[19] = g_sr_reactive_threshold;
         constants[20] = (write_reactive ? 1.f : 0.f);
         if (PatchedDraws::WriteDynamicConstants(native_device, native_device_context, std::addressof(game_device_data.mv_fill_buffer), constants.data(), sizeof(constants)))
         {
            // The depth may be bound as the depth target, and the motion vectors and masks as render targets
            native_device_context->OMSetRenderTargets(0, nullptr, nullptr);
            ID3D11Buffer* const buffer = game_device_data.mv_fill_buffer.get();
            ID3D11ShaderResourceView* const srvs[2] = {game_device_data.mv_depth_srv.get(), write_reactive ? game_device_data.mv_reactive_target_srv.get() : nullptr};
            ID3D11UnorderedAccessView* const uavs[3] = {game_device_data.mv_uav.get(), write_reactive ? game_device_data.mv_reactive_uav.get() : nullptr, write_reactive ? game_device_data.mv_transparency_uav.get() : nullptr};
            native_device_context->CSSetConstantBuffers(0, 1, &buffer);
            native_device_context->CSSetShaderResources(0, UINT(std::size(srvs)), srvs);
            native_device_context->CSSetUnorderedAccessViews(0, UINT(std::size(uavs)), uavs, nullptr);
            native_device_context->CSSetShader(fill_shader, nullptr, 0);
            native_device_context->Dispatch((uint32_t(device_data.output_resolution.x) + 7) / 8, (uint32_t(device_data.output_resolution.y) + 7) / 8, 1);
            ID3D11UnorderedAccessView* const null_uavs[std::size(uavs)] = {};
            ID3D11ShaderResourceView* const null_srvs[std::size(srvs)] = {};
            native_device_context->CSSetUnorderedAccessViews(0, UINT(std::size(null_uavs)), null_uavs, nullptr);
            native_device_context->CSSetShaderResources(0, UINT(std::size(null_srvs)), null_srvs);
            game_device_data.mv_filled = true;
            if (write_reactive)
            {
               reactive_written = true;
               game_device_data.sr_reactive_frame = cb_luma_global_settings.FrameIndex;
            }
         }
      }
      if (IsSRActive(device_data))
      {
         [[maybe_unused]] const bool drawn = DrawUpscaler(native_device, native_device_context, device_data, reactive_written);
#if DEVELOPMENT
         game_device_data.mv_stats.sr_draws += drawn;
#endif
      }
      compute_state.Restore(native_device_context);
      graphics_state.Restore(native_device_context);
   }

   // Core swaps the game's samplers for their biased variants only as they're bound, so after a change of
   // "texture_mip_lod_bias_offset" a sampler that stays bound keeps the previous bias until the game rebinds it. Swaps every stage's
   // bound samplers for the current bias's variants. Expects "s_mutex_samplers" locked exclusively.
   static void RebindUpgradedSamplers(ID3D11DeviceContext* native_device_context, DeviceData* device_data)
   {
      using GetSamplers = void (STDMETHODCALLTYPE ID3D11DeviceContext::*)(UINT, UINT, ID3D11SamplerState**);
      using SetSamplers = void (STDMETHODCALLTYPE ID3D11DeviceContext::*)(UINT, UINT, ID3D11SamplerState* const*);
      constexpr std::pair<GetSamplers, SetSamplers> stages[] = {
         {&ID3D11DeviceContext::VSGetSamplers, &ID3D11DeviceContext::VSSetSamplers},
         {&ID3D11DeviceContext::HSGetSamplers, &ID3D11DeviceContext::HSSetSamplers},
         {&ID3D11DeviceContext::DSGetSamplers, &ID3D11DeviceContext::DSSetSamplers},
         {&ID3D11DeviceContext::GSGetSamplers, &ID3D11DeviceContext::GSSetSamplers},
         {&ID3D11DeviceContext::PSGetSamplers, &ID3D11DeviceContext::PSSetSamplers},
         {&ID3D11DeviceContext::CSGetSamplers, &ID3D11DeviceContext::CSSetSamplers},
      };
      auto& custom_samplers = device_data->custom_sampler_by_original_sampler;
      const float bias = device_data->texture_mip_lod_bias_offset;
      for (const auto& [get_samplers, set_samplers] : stages)
      {
         ID3D11SamplerState* samplers[D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT] = {};
         (native_device_context->*get_samplers)(0, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT, samplers);
         bool swapped = false;
         for (ID3D11SamplerState*& sampler : samplers)
         {
            if (!sampler)
               continue;
            // The game's sampler: the bound one, or the one whose variant is bound
            auto original = custom_samplers.find(reinterpret_cast<uint64_t>(sampler));
            if (original == custom_samplers.end())
            {
               original = std::find_if(custom_samplers.begin(), custom_samplers.end(), [sampler](const auto& entry)
                  { return std::any_of(entry.second.begin(), entry.second.end(), [sampler](const auto& variant)
                       { return variant.second.get() == sampler; }); });
               if (original == custom_samplers.end())
                  continue;
            }
            auto& [original_handle, variants] = *original;
            auto variant = variants.find(bias);
            if (variant == variants.end())
            {
               D3D11_SAMPLER_DESC desc;
               reinterpret_cast<ID3D11SamplerState*>(original_handle)->GetDesc(&desc);
               variant = variants.emplace(bias, CreateCustomSampler(*device_data, device_data->native_device, desc)).first;
            }
            // No variant: a sampler Core doesn't upgrade, the game's own
            ID3D11SamplerState* const replacement = (variant->second ? variant->second.get() : reinterpret_cast<ID3D11SamplerState*>(original_handle));
            if (replacement != sampler)
            {
               sampler->Release();
               sampler = replacement;
               sampler->AddRef();
               swapped = true;
            }
         }
         if (swapped)
         {
            (native_device_context->*set_samplers)(0, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT, samplers);
         }
         for (ID3D11SamplerState* sampler : samplers)
         {
            if (sampler)
            {
               sampler->Release();
            }
         }
      }
   }

public:
   void OnInit(bool async) override
   {
#if DEVELOPMENT
      // For the MCP "luma_dev_values" tool
      Mcp::RegisterToggles({{"mv_enable", &g_mv_enable}, {"mv_debug_view", &g_mv_debug_view}, {"mv_force_jitter", &g_mv_force_jitter}, { "mv_disable_jitter",
                               &g_mv_disable_jitter }});
      Mcp::RegisterToggles({{"smaa_enable", &g_smaa_enable}, {"smaa_t2x", &g_smaa_t2x}, {"smaa_predication", &g_smaa_predication}, {"smaa_pred_debug", &g_smaa_pred_debug}, {"smaa_pred_measure", &g_smaa_pred_measure},
         {"hide_ui", &g_hide_ui}, {"gtao_enable", &g_gtao_enable}, { "fix_movie_leak",
            &g_fix_movie_leak }});
      Mcp::RegisterToggles({{"gtvbao_enable", &g_gtvbao_enable}, {"sr_reactive_enable", &g_sr_reactive_enable}, { "sr_reactive_debug_view",
                               &g_sr_reactive_debug_view }});
      Mcp::RegisterValues({{"gtvbao_final_value_power", &g_gtvbao_final_value_power, 0.3f, 4.5f}, {"gtao_history_weight", &g_gtao_history_weight, 0.f, 0.98f}, {"sr_reactive_scale", &g_sr_reactive_scale, 0.f, 4.f}, {"sr_reactive_threshold", &g_sr_reactive_threshold, 0.f, 1.f}});
      Mcp::RegisterValues({{"rcas_sharpness", &g_rcas_sharpness, 0.f, 1.f}, {"smaa_pred_tolerance", &g_smaa_pred_tolerance, 0.002f, 0.2f}, {"gtao_final_value_power", &g_gtao_final_value_power, 0.3f, 4.5f},
         {"gtao_depth_scale", &g_gtao_depth_scale, 1.f, 200.f}, {"gtao_radius_override", &g_gtao_radius_override, 0.f, 5.f}});
      Mcp::RegisterInts({{"gtao_debug_view", &g_gtao_debug_view, 0, 4}, {"gtao_temporal", &g_gtao_temporal, 0, 2}});
      Mcp::RegisterTextures({MCP_GAME_TEXTURE("smaa.input", tex_input),
         MCP_GAME_TEXTURE("smaa.pred_mask", tex_pred),
         MCP_GAME_TEXTURE("smaa.output", tex_smaa_out),
         MCP_GAME_TEXTURE("rcas.output", tex_rcas_out),
         MCP_GAME_TEXTURE("gtao.depth_mips", tex_gtao_depth_mips),
         MCP_GAME_TEXTURE("mv.velocity", mv_texture)});
#endif
      // Game-specific defines: TONEMAP_TYPE drives Luma_BL_Tonemap.hlsl, XE_GTAO_QUALITY drives Luma_BL_XeGTAO.hlsl.
      std::vector<ShaderDefineData> game_shader_defines_data = {
         {"TONEMAP_TYPE", '1', true, false, "0 - SDR: Vanilla clamped reference\n1 - HDR: Extended UE3 grade + C-infinity rolloff", 1},
         {"XE_GTAO_QUALITY", '3', true, false, "Ambient Occlusion (XeGTAO) quality (slice count)\n0 - Low\n1 - Medium\n2 - High\n3 - Very High\n4 - Ultra", 4},
      };
      shader_defines_data.append_range(game_shader_defines_data);

      // Post-process buffers stay gamma-encoded, as the game's gamma 2.2 SDR: the gamma SDR HUD then blends as in vanilla (a linear
      // buffer washes it out). The tonemap pre-scales by GamePaperWhite / UIPaperWhite (UI_DRAW_TYPE 2); the composition decodes,
      // applies paper white and encodes scRGB.
      GetShaderDefineData(POST_PROCESS_SPACE_TYPE_HASH).SetDefaultValue('0');
      GetShaderDefineData(EARLY_DISPLAY_ENCODING_HASH).SetDefaultValue('0');
      GetShaderDefineData(VANILLA_ENCODING_TYPE_HASH).SetDefaultValue('1'); // game shipped gamma-2.2 SDR
      GetShaderDefineData(GAMMA_CORRECTION_TYPE_HASH).SetDefaultValue('1');
      GetShaderDefineData(GAMUT_MAPPING_TYPE_HASH).SetDefaultValue('1'); // gamut-map wild colors in composition
      GetShaderDefineData(UI_DRAW_TYPE_HASH).SetDefaultValue('2');       // HUD gets its own UIPaperWhite + gamma blend

      // Depth-extract CS for SMAA predication: hardware d24 -> R16F plane-deviation edge-ness.
      native_shaders_definitions.emplace(CompileTimeStringHash("BL Depth Extract CS"),
         ShaderDefinition("Luma_BL_DepthExtract", reshade::api::pipeline_subobject_type::compute_shader));

      // XeGTAO (replaces the game's native HBAO+; see the AO hash block above). Compute passes out of one
      // file; the two denoise variants differ only by XE_GTAO_FINAL_APPLY (the final one writes the game's
      // r16g16_float AO target).
      native_shaders_definitions.emplace(CompileTimeStringHash("BL XeGTAO Prefilter Depths CS"),
         ShaderDefinition{"Luma_BL_XeGTAO", reshade::api::pipeline_subobject_type::compute_shader, nullptr, "prefilter_depths16x16_cs"});
      native_shaders_definitions.emplace(CompileTimeStringHash("BL XeGTAO Main Pass CS"),
         ShaderDefinition{"Luma_BL_XeGTAO", reshade::api::pipeline_subobject_type::compute_shader, nullptr, "main_pass_cs"});
      native_shaders_definitions.emplace(CompileTimeStringHash("BL XeGTAO Denoise Pass 1 CS"),
         ShaderDefinition{"Luma_BL_XeGTAO", reshade::api::pipeline_subobject_type::compute_shader, nullptr, "denoise_pass_cs", {{"XE_GTAO_FINAL_APPLY", "0"}}});
      native_shaders_definitions.emplace(CompileTimeStringHash("BL XeGTAO Denoise Pass 2 CS"),
         ShaderDefinition{"Luma_BL_XeGTAO", reshade::api::pipeline_subobject_type::compute_shader, nullptr, "denoise_pass_cs", {{"XE_GTAO_FINAL_APPLY", "1"}}});
      // GT-VBAO: its main pass, its slice count and noise under the AO history, and the history
      native_shaders_definitions.emplace(CompileTimeStringHash("BL GT-VBAO Main Pass CS"),
         ShaderDefinition{"Luma_BL_XeGTAO", reshade::api::pipeline_subobject_type::compute_shader, nullptr, "gtvbao_main_pass_cs"});
      native_shaders_definitions.emplace(CompileTimeStringHash("BL GT-VBAO Temporal Main Pass CS"),
         ShaderDefinition{"Luma_BL_XeGTAO", reshade::api::pipeline_subobject_type::compute_shader, nullptr, "gtvbao_main_pass_cs", {{"GTVBAO_SLICE_COUNT", "1.0"}, {"GTVBAO_FAST_NOISE", "1"}}});
      native_shaders_definitions.emplace(CompileTimeStringHash("BL XeGTAO Temporal CS"),
         ShaderDefinition{"Luma_BL_XeGTAO", reshade::api::pipeline_subobject_type::compute_shader, nullptr, "temporal_pass_cs"});

      // DLSS / FSR: camera motion for the motion vector pixels no patched draw wrote (sky, unpatched draws), the DOF/Bloom gather with the
      // DOF history, the CPU copies of the per-draw constants, and the motion vector target written by every blend state
      native_shaders_definitions.emplace(CompileTimeStringHash("BL Motion Vector Fill CS"),
         ShaderDefinition("Luma_BL_MotionVectorFill", reshade::api::pipeline_subobject_type::compute_shader));
      native_shaders_definitions.emplace(CompileTimeStringHash("BL SMAA T2x Blending Weight Calculation PS"),
         ShaderDefinition{"Luma_SMAA_impl", reshade::api::pipeline_subobject_type::pixel_shader, nullptr, "smaa_blending_weight_calculation_ps", {{"SMAA_T2X", "1"}}});
      native_shaders_definitions.emplace(CompileTimeStringHash("BL SMAA T2x Neighborhood Blending PS"),
         ShaderDefinition{"Luma_SMAA_impl", reshade::api::pipeline_subobject_type::pixel_shader, nullptr, "smaa_neighborhood_blending_ps", {{"SMAA_T2X", "1"}}});
      native_shaders_definitions.emplace(CompileTimeStringHash("BL SMAA T2x Resolve PS"),
         ShaderDefinition{"Luma_SMAA_impl", reshade::api::pipeline_subobject_type::pixel_shader, nullptr, "smaa_resolve_ps", {{"SMAA_T2X", "1"}}});
      native_shaders_definitions.emplace(CompileTimeStringHash("BL DOF Gather CS"),
         ShaderDefinition{"Luma_BL_DOFGather", reshade::api::pipeline_subobject_type::compute_shader, nullptr, "gather_cs"});
      reshade::register_event<reshade::addon_event::update_buffer_region>(OnUpdateBufferRegion);
      reshade::register_event<reshade::addon_event::destroy_resource>(OnDestroyResource);
      reshade::register_event<reshade::addon_event::create_pipeline>(PatchedDraws::OnCreateBlendState<MotionVectorPatches::target_slot, MotionVectorPatches::reactive_slot>);
      reshade::register_event<reshade::addon_event::bind_pipeline>(PatchedDraws::OnBindPipeline<GetBoundStates>);
      reshade::register_event<reshade::addon_event::bind_vertex_buffers>(PatchedDraws::OnBindVertexBuffers<GetBoundStates>);
      reshade::register_event<reshade::addon_event::bind_index_buffer>(PatchedDraws::OnBindIndexBuffer<GetBoundStates>);

      // The game uses constant buffer slots b0-b3; Luma takes b12/b13 here, and b5 (DOF gather, CS), b8-b11 (motion vector patches,
      // VS), b10 (AO history, CS) and b11 (XeGTAO knobs, CS) elsewhere.
      // luma_data is used by the Display Composition; luma_ui stays off (UI drawn by the game).
      luma_settings_cbuffer_index = 13;
      luma_data_cbuffer_index = 12;
      luma_ui_cbuffer_index = -1;

      // Scene and UI Paper White sliders by default, not the OS reference white level: Core shows the separate "UI Paper White" only
      // with UI_DRAW_TYPE >= 1 and !use_os_reference_white_level. Default 203 nits (BT.2408).
      use_os_reference_white_level = false;

      // User controls (LumaSettings.GameSettings, see GameCBuffers.hlsl); the grade is vanilla by default.
      default_luma_global_game_settings.Exposure = 1.f; // multiplier (1x)
      default_luma_global_game_settings.Saturation = 1.f;
      default_luma_global_game_settings.HighlightDechroma = 0.f; // Off: the hue reference turns highlights without whitening them
      default_luma_global_game_settings.BloomIntensity = 1.f;
      default_luma_global_game_settings.Contrast = 1.f;
      default_luma_global_game_settings.Dithering = 1.f;          // subtle anti-banding on by default
      default_luma_global_game_settings.FlareOut = 1.f;           // additive lens-flare/glare scale (1 = vanilla)
      default_luma_global_game_settings.VideoAutoHDREnable = 1.f; // light AutoHDR on Bink movies, HDR only
      default_luma_global_game_settings.VideoAutoHDRBoost = 0.5f; // highlight-expansion strength (peak ~165 nits at 0.5)
      cb_luma_global_settings.GameSettings = default_luma_global_game_settings;
   }

   void OnCreateDevice(ID3D11Device* native_device, DeviceData& device_data) override
   {
      device_data.game = new BorderlandsGotyGameDeviceData;
#if DEVELOPMENT
      // For the MCP "luma_dev_values" tool: the last complete frame's counts
      const auto& stats = GetGameDeviceData(device_data).mv_last_stats;
      Mcp::RegisterCounters({{"mv.draws", &stats.motion_vector_draws}, {"mv.jitter_draws", &stats.jitter_draws}, {"mv.matched", &stats.matched}, {"mv.camera_only", &stats.camera_only},
                               {"mv.other_camera", &stats.other_camera}, {"mv.uncopied", &stats.uncopied}, {"mv.updates", &stats.updates}, {"mv.sr_draws", &stats.sr_draws}, {"mv.ended_by_hash", &stats.ended_by},
                               {"mv.registered_buffers", &stats.registered_buffers}, {"mv.constants_pool", &stats.constants_pool},
                               {"mv.destroyed_buffers", &GetGameDeviceData(device_data).mv_destroyed_buffers}, {"mv.refused_region_copies", &stats.refused_region_copies},
                               { "fxaa.skipped",
                                  &stats.fxaa_skipped }},
         &device_data);
      for (size_t i = 0; i < std::size(mv_reject_names); i++)
         Mcp::RegisterCounter(std::string("mv.rejected.") + mv_reject_names[i], &stats.rejected[i], &device_data);
#endif
   }

   void OnDestroyDeviceData(DeviceData& device_data) override
   {
#if DEVELOPMENT
      Mcp::Unregister(&device_data);
#endif
      Game::OnDestroyDeviceData(device_data);
   }

   DrawOrDispatchOverrideType OnDrawOrDispatch(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, reshade::api::shader_stage stages, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, bool is_custom_pass, bool& updated_cbuffers, std::function<void()>* original_draw_dispatch_func) override
   {
      auto& gd = GetGameDeviceData(device_data);

      // Every pass handled here draws on the immediate context
      if (native_device_context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE)
         return DrawOrDispatchOverrideType::None;
      const bool uber = original_shader_hashes.Contains(kUberPostHashes[0], reshade::api::shader_stage::pixel) || original_shader_hashes.Contains(kUberPostHashes[1], reshade::api::shader_stage::pixel);
      const bool gather = original_shader_hashes.Contains(kDOFBloomGatherHash, reshade::api::shader_stage::compute);

      // DLSS / FSR: the scene's draws with motion vectors or jitter (see "DrawWithMotionVectors"), and the upscaler at its first post pass
      if (gd.mv_active)
      {
         if (gather || uber)
         {
            if (gd.mv_scene_open)
            {
#if DEVELOPMENT
               gd.mv_stats.ended_by = (gather ? kDOFBloomGatherHash : uint32_t(original_shader_hashes.pixel_shaders[0]));
#endif
               EndScene(native_device, native_device_context, device_data);
            }
         }
         else if (!gd.mv_scene_done && !is_custom_pass && original_draw_dispatch_func && *original_draw_dispatch_func && (stages & reshade::api::shader_stage::vertex) == reshade::api::shader_stage::vertex)
         {
            com_ptr<ID3D11RenderTargetView> rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
            com_ptr<ID3D11DepthStencilView> dsv;
            native_device_context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, &rtvs[0], &dsv);
            if (!gd.mv_scene_open && dsv)
            {
               OpenScene(native_device, native_device_context, cmd_list_data, device_data, original_shader_hashes.vertex_shaders[0], dsv.get());
            }
            const bool motion_vectors = DrawWithMotionVectors(native_device, native_device_context, cmd_list_data, device_data, original_shader_hashes, *original_draw_dispatch_func, rtvs, dsv.get());
            const bool jitter = !motion_vectors && DrawWithJitter(native_device, native_device_context, cmd_list_data, device_data, original_shader_hashes, *original_draw_dispatch_func, rtvs, dsv.get());
#if DEVELOPMENT
            Trace::Annotate(cmd_list_data, motion_vectors ? "mv" : (jitter ? "jitter" : "unpatched"));
            if (const int reject = std::exchange(gd.mv_draw_reject, -1); reject >= 0)
            {
               Trace::Annotate(cmd_list_data, "mv_reject", reject);
            }
#endif
            if (motion_vectors || jitter)
               return DrawOrDispatchOverrideType::Replaced;
         }
      }
      // DLSS / FSR: the uber and the DOF/Bloom gather (the game's, or ours below) read the upscaled scene where they bind its input
      if ((uber || gather) && device_data.has_drawn_sr && gd.sr_input)
      {
         com_ptr<ID3D11ShaderResourceView> bound;
         if (uber)
         {
            native_device_context->PSGetShaderResources(0, 1, &bound);
         }
         else
         {
            native_device_context->CSGetShaderResources(0, 1, &bound);
         }
         com_ptr<ID3D11Resource> resource;
         if (bound)
         {
            bound->GetResource(&resource);
         }
         if (resource.get() == gd.sr_input)
         {
            ID3D11ShaderResourceView* const upscaled = gd.sr_output_srv.get();
            if (uber)
            {
               native_device_context->PSSetShaderResources(0, 1, &upscaled);
            }
            else
            {
               native_device_context->CSSetShaderResources(0, 1, &upscaled);
            }
         }
      }

      // Every draw without a patched shader: the game's own, if the last patched draw's are still bound
      PatchedDraws::RestoreGameShader(native_device_context, &gd.mv_bound_vertex_shader);
      PatchedDraws::RestoreGameShader(native_device_context, &gd.mv_bound_pixel_shader);

      // DLSS / FSR: the DOF/Bloom gather takes its DOF amount from a per pixel history, updated in the same pass (the jittered depth
      // flipped whole blocks, see Luma_BL_DOFGather.hlsl)
      if (gather && device_data.has_drawn_sr && original_draw_dispatch_func && *original_draw_dispatch_func)
      {
         auto* const gather_shader = FindShader(device_data.native_compute_shaders, CompileTimeStringHash("BL DOF Gather CS"));
         const uint2 size = {uint32_t(device_data.output_resolution.x), uint32_t(device_data.output_resolution.y)};
         // The history, at the scene's size
         D3D11_TEXTURE2D_DESC history_desc = {};
         if (gd.dof_history[0])
         {
            gd.dof_history[0]->GetDesc(&history_desc);
         }
         if (history_desc.Width != size.x || history_desc.Height != size.y)
         {
            gd.dof_history_valid = false;
            history_desc = {.Width = size.x, .Height = size.y, .MipLevels = 1, .ArraySize = 1, .Format = DXGI_FORMAT_R16_FLOAT, .SampleDesc = {.Count = 1}, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS};
            for (size_t i = 0; i < std::size(gd.dof_history); i++)
            {
               gd.dof_history[i].reset();
               gd.dof_history_srvs[i].reset();
               gd.dof_history_uavs[i].reset();
               if (SUCCEEDED(native_device->CreateTexture2D(&history_desc, nullptr, &gd.dof_history[i])))
               {
                  native_device->CreateShaderResourceView(gd.dof_history[i].get(), nullptr, &gd.dof_history_srvs[i]);
                  native_device->CreateUnorderedAccessView(gd.dof_history[i].get(), nullptr, &gd.dof_history_uavs[i]);
               }
            }
         }
         const uint32_t previous = gd.dof_history_index, next = gd.dof_history_index ^ 1;
         // Weight of this frame: 0.1 (about ten frames), 1 when the history restarts with the upscaler's
         const float constants[4] = {(gd.dof_history_valid && !device_data.force_reset_sr) ? 0.1f : 1.f};
         // The upscaled scene (bound above) and the game's depth (the last copy, before the first person weapon)
         com_ptr<ID3D11ShaderResourceView> game_srvs[2];
         native_device_context->CSGetShaderResources(0, UINT(std::size(game_srvs)), &game_srvs[0]);
         if (gather_shader && gd.dof_history_srvs[previous] && gd.dof_history_uavs[next] && gd.mv_srv && game_srvs[1] && PatchedDraws::WriteDynamicConstants(native_device, native_device_context, std::addressof(gd.dof_history_cb), constants, sizeof(constants)))
         {
            com_ptr<ID3D11UnorderedAccessView> target_uav;
            native_device_context->CSGetUnorderedAccessViews(0, 1, &target_uav);
            DrawStateStack<DrawStateStackType::Compute> compute_state;
            DrawStateStack<DrawStateStackType::SimpleGraphics> graphics_state;
            compute_state.Cache(native_device_context, device_data.uav_max_count);
            graphics_state.Cache(native_device_context, device_data.uav_max_count);
            // The motion vector target may still be bound for output (see "DrawWithMotionVectors"): its SRV would be null
            native_device_context->OMSetRenderTargets(0, nullptr, nullptr);

            // The scene, the game's depth, the motion vectors and last frame's history in; the gather's target and the next history out
            // (outputs bound first: D3D11 nulls an SRV of a resource still bound for output)
            ID3D11Buffer* const cb = gd.dof_history_cb.get();
            ID3D11SamplerState* const linear_sampler = device_data.sampler_state_linear.get();
            ID3D11UnorderedAccessView* const uavs[2] = {target_uav.get(), gd.dof_history_uavs[next].get()};
            ID3D11ShaderResourceView* const srvs[4] = {game_srvs[0].get(), game_srvs[1].get(), gd.mv_srv.get(), gd.dof_history_srvs[previous].get()};
            native_device_context->CSSetConstantBuffers(5, 1, &cb);
            native_device_context->CSSetSamplers(0, 1, &linear_sampler);
            native_device_context->CSSetUnorderedAccessViews(0, UINT(std::size(uavs)), uavs, nullptr);
            native_device_context->CSSetShaderResources(0, UINT(std::size(srvs)), srvs);
            native_device_context->CSSetShader(gather_shader, nullptr, 0);
            // One thread per 2x2 scene texels, as the game's
            native_device_context->Dispatch((size.x + 15) / 16, (size.y + 15) / 16, 1);

            compute_state.Restore(native_device_context);
            graphics_state.Restore(native_device_context);
            gd.dof_history_index = next;
            gd.dof_history_valid = true;
            return DrawOrDispatchOverrideType::Replaced;
         }
      }

      // Hide HUD: cancel the game's UI draws, those into a swapchain back buffer ("device_data.back_buffers", as Core's UI detection).
      // Compute and offscreen draws have no swapchain RTV, so they're untouched.
      if (g_hide_ui && !is_custom_pass)
      {
         ComPtr<ID3D11RenderTargetView> rtv;
         native_device_context->OMGetRenderTargets(1, rtv.put(), nullptr);
         ComPtr<ID3D11Resource> rtv_res;
         if (rtv)
            rtv->GetResource(rtv_res.put());
         const std::shared_lock lock(device_data.mutex);
         if (rtv_res && device_data.back_buffers.contains(reinterpret_cast<uint64_t>(rtv_res.get())))
            return DrawOrDispatchOverrideType::Replaced; // drop the UI draw
      }

      // SMAA predication inputs: the cel-shading edge pass binds the D24S8 depth at PS t0 and linearizes it with
      // PSOffsetConstants at PS b2.
      if (original_shader_hashes.Contains(kCelShadingHash, reshade::api::shader_stage::pixel))
      {
         gd.srv_depth.reset();
         gd.cb_game_offsets.reset();
         native_device_context->PSGetShaderResources(0, 1, gd.srv_depth.put());
         native_device_context->PSGetConstantBuffers(2, 1, gd.cb_game_offsets.put());
      }

      // XeGTAO over the native HBAO+ (see the AO hash block above; deinterleave x2 -> normals -> coarse x2 -> blur -> apply blit).
      // The chain is taken over only when everything is ready at the first dispatch; a failure there leaves the whole native chain
      // untouched, like the SMAA fp16 guard. The normals pass 0xB2B47225 isn't hooked: the XeGTAO main pass reads its ViewNormalTex.
      if (g_gtao_enable)
      {
         // (a) Deinterleave: captures the full-res r24 scene depth (t0) and builds every scratch resource, then skips the native
         // dispatch. Both dispatches of the pair come here (the second is a cheap re-capture).
         if (original_shader_hashes.Contains(kAODeinterleaveHash, reshade::api::shader_stage::compute))
         {
            if (!HasShaders(device_data.native_compute_shaders, CompileTimeStringHash("BL XeGTAO Prefilter Depths CS"), CompileTimeStringHash("BL XeGTAO Main Pass CS"), CompileTimeStringHash("BL XeGTAO Denoise Pass 1 CS"), CompileTimeStringHash("BL XeGTAO Denoise Pass 2 CS")))
               return DrawOrDispatchOverrideType::None;

            ComPtr<ID3D11ShaderResourceView> srv_d;
            native_device_context->CSGetShaderResources(0, 1, srv_d.put());
            if (!srv_d)
               return DrawOrDispatchOverrideType::None;
            // Size the scratch (and dispatches) from the input depth desc (the game's HBAO+ full-res). The
            // game's own cb0 (InvFullResolution etc.) is content-dimensioned, so the shader's pixel<->UV math
            // is correct; the final pass writes the game's AO buffer at identical pixel coords.
            const uint2 depth_size = GetViewTextureSize(srv_d.get());
            if (depth_size.x == 0 || depth_size.y == 0)
               return DrawOrDispatchOverrideType::None;
            const uint32_t w = depth_size.x, h = depth_size.y;

            // The AO history only with the per frame noise (see "IsGTAOTemporal")
            const bool temporal = IsGTAOTemporal(device_data);
            if (gd.gtao_w != w || gd.gtao_h != h || temporal != bool(gd.tex_gtao_history[1]))
            {
               gd.ReleaseGTAOScratch();

               // 5 mips: XE_GTAO_DEPTH_MIP_LEVELS
               const D3D11_TEXTURE2D_DESC td = {.Width = w, .Height = h, .MipLevels = 5, .ArraySize = 1, .Format = DXGI_FORMAT_R32_FLOAT, .SampleDesc = {.Count = 1}, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS};
               bool ok = SUCCEEDED(native_device->CreateTexture2D(&td, nullptr, gd.tex_gtao_depth_mips.put()));
               D3D11_UNORDERED_ACCESS_VIEW_DESC ud = {};
               ud.Format = td.Format;
               ud.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
               for (int i = 0; ok && i < 5; i++)
               {
                  ud.Texture2D.MipSlice = i;
                  ok = SUCCEEDED(native_device->CreateUnorderedAccessView(gd.tex_gtao_depth_mips.get(), &ud, gd.gtao_depth_mip_uavs[i].put()));
               }
               ok = ok && SUCCEEDED(native_device->CreateShaderResourceView(gd.tex_gtao_depth_mips.get(), nullptr, gd.srv_gtao_depth_mips.put()));

               const auto create_scratch = [&](auto* texture, auto* uav, auto* srv, DXGI_FORMAT format)
               {
                  return CreateDefaultTex(native_device, w, h, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS, texture, format) &&
                         SUCCEEDED(native_device->CreateUnorderedAccessView(texture->get(), nullptr, uav->put())) &&
                         SUCCEEDED(native_device->CreateShaderResourceView(texture->get(), nullptr, srv->put()));
               };
               for (int i = 0; ok && i < 2; i++)
               {
                  ok = create_scratch(std::addressof(gd.tex_gtao_working[i]), std::addressof(gd.uav_gtao_working[i]), std::addressof(gd.srv_gtao_working[i]), DXGI_FORMAT_R8G8_UNORM);
                  ok = ok && (!temporal || create_scratch(std::addressof(gd.tex_gtao_history[i]), std::addressof(gd.uav_gtao_history[i]), std::addressof(gd.srv_gtao_history[i]), DXGI_FORMAT_R16G16_FLOAT));
               }
               if (!ok)
               {
                  gd.ReleaseGTAOScratch();
                  return DrawOrDispatchOverrideType::None; // native chain runs whole
               }
               gd.gtao_w = w;
               gd.gtao_h = h;
            }

            // Knob CB (b11); the last is the noise index
            const float knobs[8] = {(g_gtvbao_enable ? g_gtvbao_final_value_power : g_gtao_final_value_power), g_gtao_depth_scale, g_gtao_radius_override, float(g_gtao_debug_view), temporal ? float(cb_luma_global_settings.FrameIndex % 64) : 0.f};
            if (!PatchedDraws::WriteDynamicConstants(native_device, native_device_context, std::addressof(gd.cb_gtao), knobs, sizeof(knobs)))
               return DrawOrDispatchOverrideType::None;

            gd.srv_gtao_depth = srv_d;

            return DrawOrDispatchOverrideType::Replaced; // skip the native deinterleave
         }

         // (b) Coarse horizon march: captures the packed view normals (t0) and skips the native dispatch, only when the chain was taken
         // over this frame (else the native chain runs whole).
         if (original_shader_hashes.Contains(kAOCoarseHash, reshade::api::shader_stage::compute))
         {
            if (!gd.srv_gtao_depth)
               return DrawOrDispatchOverrideType::None;
            ComPtr<ID3D11ShaderResourceView> srv_n;
            native_device_context->CSGetShaderResources(0, 1, srv_n.put());
            if (srv_n)
               gd.srv_gtao_normals = srv_n;
            return DrawOrDispatchOverrideType::Replaced; // skip the native coarse march
         }

         // (c) Bilateral blur: its u0 is the game's final r16g16_float AO. The XeGTAO passes write it in place of the native dispatch;
         // the game's apply blit (untouched) then multiplies it into the scene.
         if (original_shader_hashes.Contains(kAOBlurHash, reshade::api::shader_stage::compute))
         {
            if (!gd.srv_gtao_depth)
               return DrawOrDispatchOverrideType::None;

            ComPtr<ID3D11UnorderedAccessView> uav_final;
            native_device_context->CSGetUnorderedAccessViews(0, 1, uav_final.put());
            // No output bound: unreachable in practice (the blur always binds its u0); with nothing for either path to write, the
            // native blur runs.
            if (!uav_final)
               return DrawOrDispatchOverrideType::None;
            if (!gd.srv_gtao_normals)
            {
               // Shouldn't happen (chain order is fixed); write "no AO" so a stale buffer can't apply.
               const FLOAT ones[4] = {1.f, 1.f, 1.f, 1.f};
               native_device_context->ClearUnorderedAccessViewFloat(uav_final.get(), ones);
               return DrawOrDispatchOverrideType::Replaced;
            }

            const uint32_t w = gd.gtao_w, h = gd.gtao_h;
            auto* cs_prefilter = FindShader(device_data.native_compute_shaders, CompileTimeStringHash("BL XeGTAO Prefilter Depths CS"));
            auto* cs_main = FindShader(device_data.native_compute_shaders, CompileTimeStringHash("BL XeGTAO Main Pass CS"));
            auto* cs_denoise_1 = FindShader(device_data.native_compute_shaders, CompileTimeStringHash("BL XeGTAO Denoise Pass 1 CS"));
            auto* cs_denoise_2 = FindShader(device_data.native_compute_shaders, CompileTimeStringHash("BL XeGTAO Denoise Pass 2 CS"));
            DrawStateStack<DrawStateStackType::Compute> st;
            st.Cache(native_device_context, device_data.uav_max_count);

            ID3D11Buffer* kcb = gd.cb_gtao.get();
            native_device_context->CSSetConstantBuffers(11, 1, &kcb);
            ID3D11SamplerState* smp = device_data.sampler_state_point.get();
            native_device_context->CSSetSamplers(0, 1, &smp);
            // cb0 ($Globals: ProjInfo etc.) and cb2 (MinZ_MaxZRatioCS) are read at the same b0 / b2 slots, inherited from the
            // deinterleave / coarse passes earlier in the same contiguous HBAO+ chain (immediate context, fixed pass order, no
            // ClearState between them). If a variant reorders the AO passes, capture and rebind them.

            // Every pass sets its UAVs before its SRVs and unbinds its UAVs after its dispatch: an SRV of a resource still bound as a
            // CS UAV is silently bound as null (the UAV wins the hazard), and the chain then reads zeros while every write "succeeds".
            const auto dispatch = [&](ID3D11ComputeShader* shader, std::initializer_list<ID3D11ShaderResourceView*> srvs, std::initializer_list<ID3D11UnorderedAccessView*> uavs, UINT groups_x, UINT groups_y)
            {
               static constexpr std::array<ID3D11ShaderResourceView*, 5> srv_nulls = {};
               static constexpr std::array<ID3D11UnorderedAccessView*, 5> uav_nulls = {};
               native_device_context->CSSetShaderResources(0, UINT(srv_nulls.size()), srv_nulls.data());
               native_device_context->CSSetUnorderedAccessViews(0, UINT(uavs.size()), uavs.begin(), nullptr);
               native_device_context->CSSetShaderResources(0, UINT(srvs.size()), srvs.begin());
               native_device_context->CSSetShader(shader, nullptr, 0);
               native_device_context->Dispatch(groups_x, groups_y, 1);
               native_device_context->CSSetUnorderedAccessViews(0, UINT(uavs.size()), uav_nulls.data(), nullptr);
            };
            // 1) Prefilter: the game's full-res depth -> the R32F mip pyramid (2x2 per thread, 16x16 per group).
            dispatch(cs_prefilter, {gd.srv_gtao_depth.get()}, {gd.gtao_depth_mip_uavs[0].get(), gd.gtao_depth_mip_uavs[1].get(), gd.gtao_depth_mip_uavs[2].get(), gd.gtao_depth_mip_uavs[3].get(), gd.gtao_depth_mip_uavs[4].get()}, (w + 15) / 16, (h + 15) / 16);
            // The history turns working0 into working1, only with the per frame noise and this frame's motion vectors (their patched
            // draws ran in the base pass; the camera fill hasn't yet): a frame whose motion vector target wasn't cleared and drawn would
            // read the last one's
            auto* cs_gtvbao_main = FindShader(device_data.native_compute_shaders, CompileTimeStringHash("BL GT-VBAO Main Pass CS"));
            auto* cs_gtvbao_temporal_main = FindShader(device_data.native_compute_shaders, CompileTimeStringHash("BL GT-VBAO Temporal Main Pass CS"));
            auto* cs_temporal = FindShader(device_data.native_compute_shaders, CompileTimeStringHash("BL XeGTAO Temporal CS"));
            const bool gtvbao = g_gtvbao_enable && cs_gtvbao_main;
            const bool temporal = IsGTAOTemporal(device_data);
            const bool history_active = temporal && g_gtao_history_weight > 0.f && cs_temporal && gd.tex_gtao_history[1] && gd.mv_srv && gd.mv_camera && gd.mv_fill_pending;
            // 2) Main pass: pyramid + game view normals -> AO+edges (working0). GT-VBAO takes 1 slice under the history, with the FAST noise
            // at t4 (Core's "FAST Noise").
            ID3D11ComputeShader* main_pass = (gtvbao ? cs_gtvbao_main : cs_main);
            ID3D11ShaderResourceView* fast_noise = nullptr;
            if (const auto it = device_data.managed_resources.shader_resource_views.find("FAST Noise"_h); gtvbao && history_active && cs_gtvbao_temporal_main && it != device_data.managed_resources.shader_resource_views.end())
            {
               fast_noise = it->second.get();
               main_pass = cs_gtvbao_temporal_main;
            }
            dispatch(main_pass, {gd.srv_gtao_depth_mips.get(), gd.srv_gtao_normals.get(), nullptr, nullptr, fast_noise}, {gd.uav_gtao_working[0].get()}, (w + 7) / 8, (h + 7) / 8);
            int denoise_source = 0;
            if (history_active)
            {
               const bool history_valid = gd.gtao_history_frame + 1 == cb_luma_global_settings.FrameIndex && gd.mv_previous_camera && !device_data.force_reset_sr;
               std::array<float, 24> temporal_constants = GetCameraReprojectionConstants(&gd);
               temporal_constants[18] = (history_valid ? g_gtao_history_weight : 0.f);
               if (PatchedDraws::WriteDynamicConstants(native_device, native_device_context, std::addressof(gd.cb_gtao_temporal), temporal_constants.data(), sizeof(temporal_constants)))
               {
                  // A motion vector target still bound for output (only ever at its slot) would null its view as an input
                  com_ptr<ID3D11RenderTargetView> rtvs[MotionVectorPatches::target_slot + 1];
                  native_device_context->OMGetRenderTargets(UINT(std::size(rtvs)), &rtvs[0], nullptr);
                  const bool mv_bound = rtvs[MotionVectorPatches::target_slot].get() == gd.mv_rtv.get();
                  DrawStateStack<DrawStateStackType::SimpleGraphics> graphics_state;
                  if (mv_bound)
                  {
                     graphics_state.Cache(native_device_context, device_data.uav_max_count);
                     native_device_context->OMSetRenderTargets(0, nullptr, nullptr);
                  }
                  ID3D11Buffer* const temporal_cb = gd.cb_gtao_temporal.get();
                  native_device_context->CSSetConstantBuffers(10, 1, &temporal_cb);
                  const uint32_t previous = gd.gtao_history_index, next = previous ^ 1u;
                  dispatch(cs_temporal, {gd.srv_gtao_working[0].get(), gd.srv_gtao_history[previous].get(), gd.mv_srv.get(), gd.srv_gtao_depth.get()}, {gd.uav_gtao_working[1].get(), gd.uav_gtao_history[next].get()}, (w + 7) / 8, (h + 7) / 8);
                  if (mv_bound)
                  {
                     graphics_state.Restore(native_device_context);
                  }
                  gd.gtao_history_index = next;
                  gd.gtao_history_frame = cb_luma_global_settings.FrameIndex;
                  denoise_source = 1;
               }
            }
            // 3) Denoise 1, into the other working target (2 horizontal pixels per thread). Skipped when temporal: the final pass reads its
            // source. GT-VBAO keeps both passes even then: its bitmask noise is coarser.
            const bool single_denoise = temporal && !gtvbao;
            if (!single_denoise)
            {
               dispatch(cs_denoise_1, {gd.srv_gtao_working[denoise_source].get()}, {gd.uav_gtao_working[denoise_source ^ 1].get()}, (w + 15) / 16, (h + 7) / 8);
               denoise_source ^= 1;
            }
            // 4) Denoise 2 (final) -> the game's r16g16_float AO target.
            dispatch(cs_denoise_2, {gd.srv_gtao_working[denoise_source].get()}, {uav_final.get()}, (w + 15) / 16, (h + 7) / 8);

            st.Restore(native_device_context);
            return DrawOrDispatchOverrideType::Replaced; // cancel the native blur
         }
      }

#if ENABLE_SMAA
      // FXAA's passes before the resolve SMAA / RCAS replace are skipped too (the first resolve writes the swapchain): decided at the edge
      // detection, whose t0 is the swapchain target. The replaced resolve then never falls back to the game's, whose queue would be stale.
      if (original_shader_hashes.Contains(kFXAAEdgeHash, reshade::api::shader_stage::compute))
      {
         gd.fxaa_replaced = false;
         if ((g_smaa_enable || device_data.has_drawn_sr) && !is_custom_pass)
         {
            // fp16 only (as the resolve's guard below): without the HDR swapchain upgrade the game's FXAA runs whole
            com_ptr<ID3D11ShaderResourceView> color;
            native_device_context->CSGetShaderResources(0, 1, &color);
            uint4 unused_size;
            DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
            if (color)
            {
               GetResourceInfo(color.get(), unused_size, format);
            }
            gd.fxaa_replaced = format == DXGI_FORMAT_R16G16B16A16_FLOAT;
         }
      }
      if (gd.fxaa_replaced && (original_shader_hashes.Contains(kFXAAEdgeHash, reshade::api::shader_stage::compute) || original_shader_hashes.Contains(kFXAAArgumentsHash, reshade::api::shader_stage::compute) || original_shader_hashes.Contains(kFXAAFirstResolveHash, reshade::api::shader_stage::compute)))
      {
#if DEVELOPMENT
         gd.mv_stats.fxaa_skipped++;
#endif
         return DrawOrDispatchOverrideType::Replaced;
      }

      // The compute FXAA resolve, replaced with SMAA at every occurrence in the frame (menu and transition frames run two), each with
      // its own InColor / Color target. After DLSS / FSR (which already antialiased the scene, FXAA would blur it) only RCAS runs, or
      // nothing.
      if ((g_smaa_enable || device_data.has_drawn_sr) && !is_custom_pass &&
          original_shader_hashes.Contains(kFXAAResolveHash, reshade::api::shader_stage::compute))
      {
         // Without this chain's earlier passes the game's resolve would read a stale queue: anything that can't run here skips it
         const DrawOrDispatchOverrideType fallback = (gd.fxaa_replaced ? DrawOrDispatchOverrideType::Replaced : DrawOrDispatchOverrideType::None);
         // The resolve is in place: InColor (t2) aliases Color (u0, the swapchain), so D3D11 unbinds the SRV at dispatch (t2 reads
         // null). The scene color (tonemapped, pre-FXAA, the earlier FXAA passes being skipped) is taken from the UAV's resource
         // instead, copied into an SRV-capable snapshot.
         ComPtr<ID3D11UnorderedAccessView> uav_color;
         native_device_context->CSGetUnorderedAccessViews(0, 1, uav_color.put());
         if (!uav_color)
            return fallback;

         ComPtr<ID3D11Resource> color_res; // swapchain target (CopyResource source + destination)
         uav_color->GetResource(color_res.put());
         if (!color_res)
            return fallback;

         uint4 cinfo{};
         DXGI_FORMAT cfmt = DXGI_FORMAT_UNKNOWN;
         GetResourceInfo(color_res.get(), cinfo, cfmt);
         const uint32_t w = cinfo.x, h = cinfo.y;
         if (w == 0 || h == 0)
            return fallback;
         // fp16 guard: the SMAA path uses R16G16B16A16_FLOAT temps and CopyResource silently no-ops on a format mismatch, so a
         // non-fp16 swapchain (HDR upgrade absent) would feed SMAA uninitialized memory and copy garbage back. The game's FXAA runs
         // instead.
         if (cfmt != DXGI_FORMAT_R16G16B16A16_FLOAT)
         {
#if DEVELOPMENT || TEST
            if (!gd.logged_no_fp16)
            {
               gd.logged_no_fp16 = true;
               reshade::log::message(reshade::log::level::warning,
                  "[BL-SMAA] swapchain not fp16 (HDR upgrade absent) -> SMAA skipped, native FXAA kept.");
            }
#endif
            return fallback;
         }
         const bool smaa = !device_data.has_drawn_sr;
         if (!smaa && g_rcas_sharpness <= 0.f)
            return DrawOrDispatchOverrideType::Replaced;

         // A new color size (a resolution change) drops every resource sized like it; each is rebuilt below when missing (DrawSMAA
         // rebuilds its own intermediates)
         if (gd.smaa_w != w || gd.smaa_h != h)
         {
            gd.ReleaseSMAAScratch();
            gd.ReleaseSnapshotScratch();
            gd.smaa_w = w;
            gd.smaa_h = h;
         }

         // Held through SMAA so a shader reload cannot release them mid-use; "DrawSMAA" looks its shaders up with "at"
         const std::shared_lock lock_shader_objects(s_mutex_shader_objects);

         // Predication depth is valid only if captured this frame and sized like the color (a resolution change can leave a
         // different-size depth). When invalid, a null predication texture and a scale of 1.0 give SMAA the plain ULTRA threshold
         // rather than the doubled predicated baseline.
         auto* pred_cs = FindShader(device_data.native_compute_shaders, CompileTimeStringHash("BL Depth Extract CS"));
         // The extract CS maps texels 1:1, so a depth buffer of a different size would read a sub-rect and misalign the mask against the
         // color grid.
         bool pred_ok = smaa && g_smaa_predication && gd.srv_depth && gd.cb_game_offsets && pred_cs != nullptr && GetViewTextureSize(gd.srv_depth.get()) == uint2{w, h};
         if (pred_ok)
         {
            if (!gd.tex_pred && CreateDefaultTex(native_device, w, h, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS, std::addressof(gd.tex_pred), DXGI_FORMAT_R16_FLOAT))
            {
               native_device->CreateUnorderedAccessView(gd.tex_pred.get(), nullptr, gd.uav_pred.put());
               native_device->CreateShaderResourceView(gd.tex_pred.get(), nullptr, gd.srv_pred.put());
            }
            const float pp[4] = {g_smaa_pred_tolerance, 0.f, 0.f, 0.f};
            pred_ok = gd.uav_pred && gd.srv_pred && PatchedDraws::WriteDynamicConstants(native_device, native_device_context, std::addressof(gd.cb_pred), pp, sizeof(pp));
         }

         // Shader-readiness gate (async loader / dev live-reload): anything missing takes "fallback".
         if (smaa && (!HasSMAAShaders(device_data)))
            return fallback;

         // SMAA T2x: SMAA into this frame's linear target with the velocity, then the resolve with the previous frame. It needs this
         // frame's motion vectors from the fill, at the target's size, once per frame (a transition's second resolve stays 1x). Without
         // them this frame is 1x and the next one doesn't jitter.
         auto* const copy_vs = FindShader(device_data.native_vertex_shaders, CompileTimeStringHash("Copy VS"));
         auto* const t2x_weight_ps = FindShader(device_data.native_pixel_shaders, CompileTimeStringHash("BL SMAA T2x Blending Weight Calculation PS"));
         auto* const t2x_blend_ps = FindShader(device_data.native_pixel_shaders, CompileTimeStringHash("BL SMAA T2x Neighborhood Blending PS"));
         auto* const t2x_resolve_ps = FindShader(device_data.native_pixel_shaders, CompileTimeStringHash("BL SMAA T2x Resolve PS"));
         const uint32_t frame_index = cb_luma_global_settings.FrameIndex;
         bool t2x = smaa && gd.t2x_active && gd.mv_filled && gd.t2x_last_frame != frame_index && gd.mv_srv && t2x_weight_ps && t2x_blend_ps && t2x_resolve_ps && copy_vs && GetViewTextureSize(gd.mv_srv.get()) == uint2{w, h};
         const int t2x_current = int(frame_index & 1);
         bool t2x_history = gd.t2x_frames[0] && gd.t2x_last_frame + 1 == frame_index;
         if (t2x && GetViewTextureSize(gd.t2x_frame_srvs[0].get()) != uint2{w, h})
         {
            t2x_history = false;
            gd.ReleaseT2xFrames();
            for (int i = 0; i < 2; i++)
            {
               if (!CreateDefaultTex(native_device, w, h, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET, std::addressof(gd.t2x_frames[i])) ||
                   FAILED(native_device->CreateRenderTargetView(gd.t2x_frames[i].get(), nullptr, gd.t2x_frame_rtvs[i].put())) ||
                   FAILED(native_device->CreateShaderResourceView(gd.t2x_frames[i].get(), nullptr, gd.t2x_frame_srvs[i].put())))
               {
                  gd.ReleaseT2xFrames();
                  break;
               }
            }
            t2x = gd.t2x_frames[0].get() != nullptr;
         }

         // b1: the metrics, the predication threshold scale and T2x's subsample indices for the jitter phase (see "OpenScene"), 0 for 1x
         const float subsample_indices = ((t2x && gd.t2x_phase >= 0) ? float(gd.t2x_phase + 1) : 0.f);
         const float metrics[12] = {1.f / (float)w, 1.f / (float)h, (float)w, (float)h, (pred_ok ? 2.f : 1.f), 0.f, 0.f, 0.f, subsample_indices, subsample_indices, subsample_indices, 0.f};
         if (smaa && !PatchedDraws::WriteDynamicConstants(native_device, native_device_context, std::addressof(gd.cb_smaa_metrics), metrics, sizeof(metrics)))
            return fallback;

         const bool do_sharpen = g_rcas_sharpness > 0.f && PrepareRCAS(native_device, device_data);
         // After DLSS / FSR with RCAS not ready: the upscaled image stays as it is
         if (!smaa && !do_sharpen)
            return DrawOrDispatchOverrideType::Replaced;

         // Snapshot the in-place scene color out of the swapchain so SMAA / RCAS can read it
         if (!gd.srv_input && CreateDefaultTex(native_device, w, h, D3D11_BIND_SHADER_RESOURCE, std::addressof(gd.tex_input)))
         {
            native_device->CreateShaderResourceView(gd.tex_input.get(), nullptr, gd.srv_input.put());
         }
         if (!gd.srv_input)
            return fallback;
         native_device_context->CopyResource(gd.tex_input.get(), color_res.get());
         gd.snapshot_frame = cb_luma_global_settings.FrameIndex;

         if (smaa)
         {
            gd.smaa_frame = cb_luma_global_settings.FrameIndex;
            // The predication extract ("BL Depth Extract CS"). Core's Compute state stack restores the game's CS state and unbinds our
            // UAV before DrawSMAA reads it as an SRV (an SRV of a resource still bound as a UAV reads as null).
            if (pred_ok)
            {
               DrawStateStack<DrawStateStackType::Compute> compute_state;
               compute_state.Cache(native_device_context, device_data.uav_max_count);
               ID3D11ShaderResourceView* pred_srv = gd.srv_depth.get();
               ID3D11UnorderedAccessView* pred_uav = gd.uav_pred.get();
               ID3D11Buffer* pred_cbs[1] = {gd.cb_pred.get()};
               ID3D11Buffer* game_cb[1] = {gd.cb_game_offsets.get()};
               native_device_context->CSSetUnorderedAccessViews(0, 1, &pred_uav, nullptr);
               native_device_context->CSSetShaderResources(0, 1, &pred_srv);
               native_device_context->CSSetConstantBuffers(0, 1, pred_cbs);
               native_device_context->CSSetConstantBuffers(2, 1, game_cb); // PSOffsetConstants: MinZ_MaxZRatio
               native_device_context->CSSetShader(pred_cs, nullptr, 0);
               native_device_context->Dispatch((w + 7) / 8, (h + 7) / 8, 1);
               compute_state.Restore(native_device_context);
            }
         }

         // SMAA's output temp, when RCAS follows it or the swapchain target has no RTV
         const auto create_smaa_out = [&]()
         {
            gd.smaa_out_frame = cb_luma_global_settings.FrameIndex;
            if (!gd.tex_smaa_out && CreateDefaultTex(native_device, w, h, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET, std::addressof(gd.tex_smaa_out)))
            {
               native_device->CreateRenderTargetView(gd.tex_smaa_out.get(), nullptr, gd.tex_smaa_out_rtv.put());
               native_device->CreateShaderResourceView(gd.tex_smaa_out.get(), nullptr, gd.tex_smaa_out_srv.put());
            }
            return gd.tex_smaa_out_rtv && gd.tex_smaa_out_srv;
         };

#if DEVELOPMENT
         // Both calibration aids read the mask the extract CS just wrote. The numeric one runs first so it still
         // reports while the debug view is on (that path returns early).
         if (pred_ok)
            LogPredicationStats(native_device, native_device_context, gd);

         if (pred_ok && g_smaa_pred_debug)
         {
            auto* debug_ps = FindShader(device_data.native_pixel_shaders, CompileTimeStringHash("Copy PS"));
            if (copy_vs != nullptr && debug_ps != nullptr && create_smaa_out())
            {
               // The single-channel mask lands in red through Core's copy, in place of the antialiased frame, through SMAA's output
               // temp copied into the target.
               DrawStateStack<DrawStateStackType::FullGraphics> debug_state;
               debug_state.Cache(native_device_context, device_data.uav_max_count);
               DrawCustomPixelShader(native_device_context, device_data.default_depth_stencil_state.get(), device_data.default_blend_state.get(), nullptr,
                  copy_vs, debug_ps, gd.srv_pred.get(), gd.tex_smaa_out_rtv.get(), w, h, false);
               debug_state.Restore(native_device_context);
               native_device_context->CopyResource(color_res.get(), gd.tex_smaa_out.get());
               return DrawOrDispatchOverrideType::Replaced;
            }
         }
#endif

         // The last pass writes the swapchain target itself (no temp copied back), through a view made for this dispatch: a kept one would
         // hold the back buffer through a swapchain resize. Without one (a target without RTV binding), a temp copied into it.
         ComPtr<ID3D11RenderTargetView> color_rtv;
         native_device->CreateRenderTargetView(color_res.get(), nullptr, color_rtv.put());
         // The target is the resolve's CS UAV: D3D11 unbinds it there once it's bound as a render target, put back after
         DrawStateStack<DrawStateStackType::Compute> resolve_compute_state;
         resolve_compute_state.Cache(native_device_context, device_data.uav_max_count);

         // --- SMAA (3 passes), into the swapchain target, or into the temp RCAS reads (or that's copied into it) ---
         const bool smaa_into_temp = smaa && (do_sharpen || !color_rtv);
         ID3D11ShaderResourceView* sharpen_source = gd.srv_input.get();
         if (smaa)
         {
            if (smaa_into_temp && !create_smaa_out())
               return fallback;

            // The snapshot for both the edge detection and the blend (filtered in linear light, Luma_SMAA_impl.hlsl). Null predication
            // when invalid ("pred_ok"): with a predication scale of 1 in the metrics, plain ULTRA.
            ID3D11RenderTargetView* const smaa_output_rtv = (smaa_into_temp ? gd.tex_smaa_out_rtv.get() : color_rtv.get());
            // Core creates its area texture at the first draw; ours goes in first, without the U-shape smoothing (see "SMAA_AreaTex_SharpU.h")
            if (auto& area_tex_srv = device_data.managed_resources.shader_resource_views["smaa_area_tex"_h]; !area_tex_srv)
            {
               std::vector<unsigned char> area_tex(std::begin(areaTexBytes), std::end(areaTexBytes));
               for (const auto& [offset, value] : areaTexSharpUPatch)
               {
                  area_tex[offset] = value;
               }
               const D3D11_TEXTURE2D_DESC area_tex_desc = {
                  .Width = AREATEX_WIDTH,
                  .Height = AREATEX_HEIGHT,
                  .MipLevels = 1,
                  .ArraySize = 1,
                  .Format = DXGI_FORMAT_R8G8_UNORM,
                  .SampleDesc = {.Count = 1},
                  .Usage = D3D11_USAGE_IMMUTABLE,
                  .BindFlags = D3D11_BIND_SHADER_RESOURCE,
               };
               const D3D11_SUBRESOURCE_DATA area_tex_data = {.pSysMem = area_tex.data(), .SysMemPitch = AREATEX_PITCH};
               if (com_ptr<ID3D11Texture2D> texture; SUCCEEDED(native_device->CreateTexture2D(&area_tex_desc, &area_tex_data, &texture)))
               {
                  native_device->CreateShaderResourceView(texture.get(), nullptr, area_tex_srv.put());
               }
            }
            // SMAA T2x: Core's blending weight and neighborhood blending pixel shaders are swapped with ours for the draw ("SMAA_T2X" in
            // "Luma_SMAA_impl.hlsl"), whose neighborhood blend reads the velocity at t3, a slot "DrawSMAA()" leaves alone. Under
            // "lock_shader_objects", which a shader reload waits for.
            auto& pixel_shaders = device_data.native_pixel_shaders;
            const auto swap_t2x_shaders = [&]()
            {
               std::swap(pixel_shaders.at("SMAA Blending Weight Calculation PS"_h), pixel_shaders.at(CompileTimeStringHash("BL SMAA T2x Blending Weight Calculation PS")));
               std::swap(pixel_shaders.at("SMAA Neighborhood Blending PS"_h), pixel_shaders.at(CompileTimeStringHash("BL SMAA T2x Neighborhood Blending PS")));
            };
            com_ptr<ID3D11ShaderResourceView> t3_original;
            if (t2x)
            {
               native_device_context->PSGetShaderResources(3, 1, &t3_original);
               ID3D11ShaderResourceView* const velocity_srv = gd.mv_srv.get();
               native_device_context->PSSetShaderResources(3, 1, &velocity_srv);
               swap_t2x_shaders();
            }
            DrawSMAA(native_device, native_device_context, device_data,
               t2x ? gd.t2x_frame_rtvs[t2x_current].get() : smaa_output_rtv, gd.srv_input.get(), gd.srv_input.get(),
               pred_ok ? gd.srv_pred.get() : nullptr /*predication (plane-deviation edge-ness)*/, gd.cb_smaa_metrics.get());
            if (t2x)
            {
               swap_t2x_shaders();
               ID3D11ShaderResourceView* const t3_original_srv = t3_original.get();
               native_device_context->PSSetShaderResources(3, 1, &t3_original_srv);

               // The resolve: t0 this frame, t1 the previous one or this one again without a history, t2 the motion vectors, s0 linear and
               // s1 point, b1 the metrics (DrawSMAA restored the game's)
               DrawStateStack<DrawStateStackType::FullGraphics> resolve_state;
               resolve_state.Cache(native_device_context, device_data.uav_max_count);
               // The motion vector target may still be bound for output: its SRV would be null
               native_device_context->OMSetRenderTargets(0, nullptr, nullptr);
               ID3D11ShaderResourceView* const resolve_srvs[2] = {gd.t2x_frame_srvs[t2x_history ? (t2x_current ^ 1) : t2x_current].get(), gd.mv_srv.get()};
               native_device_context->PSSetShaderResources(1, 2, resolve_srvs);
               ID3D11SamplerState* const resolve_samplers[2] = {device_data.sampler_state_linear.get(), device_data.sampler_state_point.get()};
               native_device_context->PSSetSamplers(0, 2, resolve_samplers);
               ID3D11Buffer* const metrics_cb = gd.cb_smaa_metrics.get();
               native_device_context->PSSetConstantBuffers(1, 1, &metrics_cb);
               DrawCustomPixelShader(native_device_context, device_data.default_depth_stencil_state.get(), device_data.default_blend_state.get(), nullptr,
                  copy_vs, t2x_resolve_ps, gd.t2x_frame_srvs[t2x_current].get(), smaa_output_rtv, w, h, false);
               resolve_state.Restore(native_device_context);
               gd.t2x_last_frame = frame_index;
            }
            sharpen_source = gd.tex_smaa_out_srv.get();
         }

         // --- Optional RCAS on the SMAA (or upscaled) output, into the swapchain target (or a temp copied into it) ---
         ID3D11RenderTargetView* sharpen_target = color_rtv.get();
         if (do_sharpen && !sharpen_target)
         {
            if (!gd.tex_rcas_out && CreateDefaultTex(native_device, w, h, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET, std::addressof(gd.tex_rcas_out)))
            {
               native_device->CreateRenderTargetView(gd.tex_rcas_out.get(), nullptr, gd.tex_rcas_out_rtv.put());
            }
            sharpen_target = gd.tex_rcas_out_rtv.get();
         }
         if (do_sharpen && sharpen_target)
         {
            DrawRCAS(native_device_context, device_data, sharpen_source, sharpen_target, g_rcas_sharpness);
            if (!color_rtv)
            {
               native_device_context->CopyResource(color_res.get(), gd.tex_rcas_out.get());
            }
         }
         else if (smaa_into_temp)
         {
            // SMAA went into its temp and no RCAS followed
            native_device_context->CopyResource(color_res.get(), gd.tex_smaa_out.get());
         }

         resolve_compute_state.Restore(native_device_context);
         return DrawOrDispatchOverrideType::Replaced; // cancel the FXAA resolve dispatch
      }
#endif // ENABLE_SMAA

      return DrawOrDispatchOverrideType::None;
   }

   void OnPresent(ID3D11Device* native_device, DeviceData& device_data) override
   {
      auto& gd = GetGameDeviceData(device_data);

      // DLSS / FSR: the upscaler's history restarts after any frame it didn't draw (menus, loading, just picked); the selection and the
      // motion vector state are fixed here for the next frame (see "IsSRActive")
      device_data.force_reset_sr = !device_data.has_drawn_sr;
      device_data.has_drawn_sr = false;
      gd.sr_active = device_data.sr_type != SR::Type::None && !device_data.sr_suppressed;
      device_data.taa_detected = gd.sr_active; // The overlay's SR tick
      gd.t2x_phase = -1;
      gd.mv_filled = false;
      const bool t2x_was_active = gd.t2x_active;
      gd.t2x_active = g_smaa_enable && g_smaa_t2x && !gd.sr_active;
      if (t2x_was_active && !gd.t2x_active)
      {
         gd.ReleaseT2xFrames();
         gd.release_sr_resources = true; // The motion vector resources too, below
      }
      const bool mv_was_active = gd.mv_active.exchange(IsSRActive(device_data) || g_mv_enable || gd.t2x_active);
      if (mv_was_active && !gd.mv_active)
      {
         // The copies stop following the uploads: a draw after the upscaler comes back must find none ("uncopied", zero motion) until its
         // buffer is uploaded again, not a stale one
         {
            const std::lock_guard lock(gd.mv_constants_mutex);
            gd.mv_constants.ReleaseUploads();
            gd.mv_constants.Restart();
         }
         gd.mv_objects.Reset();
         gd.mv_camera.reset();
         gd.mv_previous_camera.reset();
      }
      // None picked: Core freed its upscaler resources and output, ours go too (recreated when an upscaler is picked again)
      if (device_data.sr_type == SR::Type::None && !g_mv_enable && !gd.t2x_active && gd.release_sr_resources.exchange(false))
      {
         gd.sr_output_srv.reset();
         gd.sr_input = nullptr;
         {
            const std::unique_lock lock(gd.mv_mutex);
            gd.ReleaseMotionVectorTarget();
         }
         gd.mv_depth_srv.reset();
         gd.mv_depth.reset();
         gd.mv_scene_color.reset();
         for (size_t i = 0; i < std::size(gd.dof_history); i++)
         {
            gd.dof_history[i].reset();
            gd.dof_history_srvs[i].reset();
            gd.dof_history_uavs[i].reset();
         }
         gd.dof_history_valid = false;
         gd.dof_history_cb.reset();
         gd.mv_fill_buffer.reset();
         gd.mv_jitter_buffer.reset();
         gd.mv_previous_constants = {};
      }
      // A scene no post pass ended ends here: its jitter must not reach the next frame's draws before the prepass
      gd.mv_scene_open = false;
      gd.mv_scene_done = false;
      gd.mv_frame_ended = true;
      gd.mv_fill_pending = false;
      gd.fxaa_replaced = false;
      // -1 at native resolution, 0 with XeSS (Core biases the anisotropic samplers, all of the game's with the AF16x upgrade)
      if (!custom_texture_mip_lod_bias_offset)
      {
         const float bias = (IsSRActive(device_data) ? SR::GetMipLODBias(device_data.sr_type, device_data.output_resolution.y, device_data.output_resolution.y) : 0.f);
         const std::unique_lock lock_samplers(s_mutex_samplers);
         if (device_data.texture_mip_lod_bias_offset != bias)
         {
            device_data.texture_mip_lod_bias_offset = bias;
            com_ptr<ID3D11DeviceContext> native_device_context;
            native_device->GetImmediateContext(&native_device_context);
            RebindUpgradedSamplers(native_device_context.get(), &device_data);
         }
      }
      // The masks go once the fill stopped writing them (DLSS, the masks off, SMAA), like SMAA's resources below
      if (gd.mv_reactive_target && cb_luma_global_settings.FrameIndex - gd.sr_reactive_frame > idle_release_frames)
      {
         const std::unique_lock lock(gd.mv_mutex);
         gd.ReleaseReactiveMasks();
      }
      // SMAA's resources go once it stopped running (the upscaler antialiases, or the game's AA is off), the snapshot too once RCAS
      // stopped as well; on the render thread, between frames
      if (cb_luma_global_settings.FrameIndex - gd.smaa_frame > idle_release_frames)
      {
         gd.ReleaseSMAAScratch();
         ReleaseSMAAIntermediates(device_data);
      }
      if (cb_luma_global_settings.FrameIndex - gd.snapshot_frame > idle_release_frames)
      {
         gd.ReleaseSnapshotScratch();
      }
      // No SMAA T2x frames for a while (the game's AA off): its frames go too
      if (gd.t2x_frames[0] && cb_luma_global_settings.FrameIndex - gd.t2x_last_frame > idle_release_frames)
      {
         gd.ReleaseT2xFrames();
      }
      // SMAA writes the swapchain itself once RCAS is off again: its temp goes too
      if (gd.tex_smaa_out && cb_luma_global_settings.FrameIndex - gd.smaa_out_frame > idle_release_frames)
      {
         gd.ReleaseSMAAOutput();
      }
      // The pooled constant copies only the pool holds (superseded, no object keeps them) are free for the next ones, as many as the
      // last frame asked for: a burst between two presents (loading screens) would keep its peak otherwise (74k copies, ~126 MB
      // measured). Without motion vectors, none.
      {
         const std::lock_guard lock(gd.mv_constants_mutex);
         gd.mv_constants.TrimPool(gd.mv_active);
#if DEVELOPMENT
         gd.mv_stats.registered_buffers = uint32_t(gd.mv_constants.entries.size());
         gd.mv_stats.constants_pool = uint32_t(gd.mv_constants.pool.size());
#endif
      }
#if DEVELOPMENT
      gd.mv_last_stats = std::exchange(gd.mv_stats, {});
      // "MV Debug View": Core's debug draw of the target, absolute values in pixels
      {
         const std::shared_lock lock(gd.mv_mutex);
         if (g_mv_debug_view && gd.mv_texture)
         {
            D3D11_TEXTURE2D_DESC desc = {};
            gd.mv_texture->GetDesc(&desc);
            Trace::debug_draw_auto_clear_texture = false;
            Trace::debug_draw_options |= (uint32_t)Trace::DebugDrawTextureOptionsMask::Abs | (uint32_t)Trace::DebugDrawTextureOptionsMask::UVToPixelSpace;
            device_data.debug_draw_texture = gd.mv_texture.get();
            device_data.debug_draw_texture_format = desc.Format;
            device_data.debug_draw_texture_size = {desc.Width, desc.Height, 1, 1};
         }
         else if (g_sr_reactive_debug_view && gd.mv_reactive)
         {
            Trace::debug_draw_auto_clear_texture = false;
            Trace::debug_draw_options |= (uint32_t)Trace::DebugDrawTextureOptionsMask::RedOnly;
            Trace::debug_draw_options &= ~((uint32_t)Trace::DebugDrawTextureOptionsMask::Abs | (uint32_t)Trace::DebugDrawTextureOptionsMask::UVToPixelSpace);
            device_data.debug_draw_texture = gd.mv_reactive.get();
            device_data.debug_draw_texture_format = DXGI_FORMAT_R8_UNORM;
            device_data.debug_draw_texture_size = {uint32_t(device_data.output_resolution.x), uint32_t(device_data.output_resolution.y), 1, 1};
         }
         else if (device_data.debug_draw_texture && (device_data.debug_draw_texture.get() == gd.mv_texture.get() || device_data.debug_draw_texture.get() == gd.mv_reactive.get()))
         {
            device_data.debug_draw_texture = nullptr;
         }
      }
#endif

      // Leak fix: detect in OnInitResource (any thread), release here (present thread, no create in flight).
      BLMovieLeakFix::g_frame++;
      if (g_fix_movie_leak && (BLMovieLeakFix::g_frame % BLMovieLeakFix::RELEASE_FLUSH_PERIOD) == 0)
         BLMovieLeakFix::Flush();

      // One-shot telemetry: warn if a non-matching build tagged nothing (otherwise a silent no-op).
      if (g_fix_movie_leak && !BLMovieLeakFix::g_build_checked && BLMovieLeakFix::g_frame >= BLMovieLeakFix::BUILD_CHECK_FRAME)
      {
         BLMovieLeakFix::g_build_checked = true;
         if (!BLMovieLeakFix::g_have_movie.load())
            reshade::log::message(reshade::log::level::warning,
               "[BL-Leak] no movie buffers detected after warmup -- game build may differ from the one this leak fix targets; the fix is inactive (RAM-growth crash not mitigated).");
      }

      // Predication inputs are captured per frame at the cel pass and dropped every present, so a frame without that pass (menus,
      // transitions) uses no predication rather than last frame's (possibly wrong-size) depth.
      gd.srv_depth.reset();
      gd.cb_game_offsets.reset();

      // XeGTAO inputs are captured per frame at the deinterleave / coarse passes; dropping them every present disarms the takeover,
      // so a frame without the AO chain (AO off in game, transitions) can't apply a stale AO buffer.
      gd.srv_gtao_depth.reset();
      gd.srv_gtao_normals.reset();

      device_data.has_drawn_main_post_processing = true;
   }

   void CleanExtraSRResources(DeviceData& device_data) override
   {
      GetGameDeviceData(device_data).release_sr_resources = true;
   }

   void LoadConfigs() override
   {
      reshade::get_config_value(nullptr, NAME, "SMAAEnable", g_smaa_enable);
      reshade::get_config_value(nullptr, NAME, "SMAAT2x", g_smaa_t2x);
      reshade::get_config_value(nullptr, NAME, "RCASSharpness", g_rcas_sharpness);
      // Predication has no shipping UI (see DrawImGuiSettings), but stays overridable from the ini.
      reshade::get_config_value(nullptr, NAME, "SMAAPredication", g_smaa_predication);
      reshade::get_config_value(nullptr, NAME, "SMAAPredicationTolerance", g_smaa_pred_tolerance);
      reshade::get_config_value(nullptr, NAME, "XeGTAOEnable", g_gtao_enable);
      reshade::get_config_value(nullptr, NAME, "GTVBAOEnable", g_gtvbao_enable);
      // Grade sliders (cb_luma_global_settings_dirty is already true at init -> uploaded on first frame).
      reshade::get_config_value(nullptr, NAME, "Exposure", cb_luma_global_settings.GameSettings.Exposure);
      reshade::get_config_value(nullptr, NAME, "Saturation", cb_luma_global_settings.GameSettings.Saturation);
      reshade::get_config_value(nullptr, NAME, "HighlightDechroma", cb_luma_global_settings.GameSettings.HighlightDechroma);
      reshade::get_config_value(nullptr, NAME, "BloomIntensity", cb_luma_global_settings.GameSettings.BloomIntensity);
      reshade::get_config_value(nullptr, NAME, "Contrast", cb_luma_global_settings.GameSettings.Contrast);
      reshade::get_config_value(nullptr, NAME, "Dithering", cb_luma_global_settings.GameSettings.Dithering);
      reshade::get_config_value(nullptr, NAME, "FlareOut", cb_luma_global_settings.GameSettings.FlareOut);
      reshade::get_config_value(nullptr, NAME, "VideoAutoHDREnable", cb_luma_global_settings.GameSettings.VideoAutoHDREnable);
      reshade::get_config_value(nullptr, NAME, "VideoAutoHDRBoost", cb_luma_global_settings.GameSettings.VideoAutoHDRBoost);
      reshade::get_config_value(nullptr, NAME, "HideUI", g_hide_ui);
      reshade::get_config_value(nullptr, NAME, "FixMovieLeak", g_fix_movie_leak);
   }

   void DrawImGuiSettings(DeviceData& device_data) override
   {
      ImGui::SeparatorText("Anti-Aliasing");
      const bool sr_active = IsSRActive(device_data);
      // The upscaler (Super Resolution, in the Settings tab) replaces SMAA: shown off, the saved choice is kept
      ImGui::BeginDisabled(sr_active);
      DrawSettingCheckbox("SMAA Enable", "SMAAEnable", &g_smaa_enable, true, "Replaces the game's FXAA with SMAA (requires AA enabled in the game's video settings; not used with DLSS/FSR).", sr_active);
      ImGui::EndDisabled();
      ImGui::BeginDisabled(!g_smaa_enable || sr_active);
      DrawSettingCheckbox("SMAA T2x", "SMAAT2x", &g_smaa_t2x, false, "Smoother edges and less shimmering, by blending each frame with the previous one.\nCan look slightly softer in motion. Not used with DLSS/FSR.", sr_active);
      ImGui::EndDisabled();
      ImGui::BeginDisabled(!g_smaa_enable && !sr_active);
      DrawSettingSlider("RCAS Sharpness", "RCASSharpness", &g_rcas_sharpness, 0.f, 0.f, 1.f, "Sharpening applied on top of SMAA or DLSS/FSR (0 = off; requires AA enabled in the game's video settings).");
#if DEVELOPMENT
      // Predication is not a preference: it only relaxes the edge threshold back to base ULTRA on geometry and
      // never below, so off is strictly worse. Kept as a bisect switch for devs, shipped on and out of sight.
      DrawSettingCheckbox("SMAA Predication", "SMAAPredication", &g_smaa_predication, true, "Finds edges by geometry (scene depth) instead of by brightness alone.\nKeeps texture noise from being antialiased while still catching real silhouettes.");
      DrawSettingSlider("SMAA Predication Tolerance", "SMAAPredicationTolerance", &g_smaa_pred_tolerance, 0.02f, 0.002f, 0.2f, "How far a surface may deviate from its local plane before it counts as an edge,\nas a fraction of view depth. Lower = more edges. This is the calibration lever,\nnot the SMAA threshold. Logarithmic: the parameter is relative.", "%.3f", ImGuiSliderFlags_Logarithmic);
      ImGui::Checkbox("SMAA Predication Debug View", &g_smaa_pred_debug);
      if (ImGui::IsItemHovered())
         ImGui::SetTooltip("Show the predication mask (red) instead of the frame.\nWant: black on flat surfaces, red across silhouettes.\nAll red = tolerance too low (predication is doing nothing).\nAll black = too high (silhouettes never regain sensitivity).");
      if (ImGui::Button("Measure Predication"))
         g_smaa_pred_measure = true;
      if (ImGui::IsItemHovered())
         ImGui::SetTooltip("Read the mask back and log its distribution to ReShade.log.\nStand still, set a tolerance, press; repeat per value and compare the lines.\nFIRES(>0.5) is the share of the frame that regains base sensitivity.");
#endif
      ImGui::EndDisabled();

      // --- Grade (read in Luma_BL_Tonemap.hlsl via LumaSettings.GameSettings) ---
      auto& gs = cb_luma_global_settings.GameSettings;
      ImGui::SeparatorText("Grade");
      DrawSettingSlider("Exposure", "Exposure", &gs.Exposure, default_luma_global_game_settings.Exposure, 0.f, 2.f, "Overall image brightness (1 = vanilla).");
      DrawSettingSlider("Contrast", "Contrast", &gs.Contrast, default_luma_global_game_settings.Contrast, 0.f, 2.f, "Overall image contrast, HDR only (1 = vanilla).");
      DrawSettingSlider("Saturation", "Saturation", &gs.Saturation, default_luma_global_game_settings.Saturation, 0.f, 2.f, "Color saturation, HDR only (1 = vanilla).");
      DrawSettingSlider("Highlights Desaturation", "HighlightDechroma", &gs.HighlightDechroma, default_luma_global_game_settings.HighlightDechroma, 0.f, 1.f, "How far the brightest sources fade to neutral white, HDR only (0 = keep color at any brightness).");

      ImGui::SeparatorText("Bloom");
      DrawSettingSlider("Bloom Intensity", "BloomIntensity", &gs.BloomIntensity, default_luma_global_game_settings.BloomIntensity, 0.f, 2.f, "Bloom strength (1 = vanilla, 0 = none).");

      ImGui::SeparatorText("Ambient Occlusion");
      DrawSettingCheckbox("XeGTAO Enable", "XeGTAOEnable", &g_gtao_enable, true, "Replaces the game's HBAO+ with XeGTAO (cleaner, more accurate ambient occlusion).");
      DrawSettingCheckbox("GT-VBAO Enable", "GTVBAOEnable", &g_gtvbao_enable, true, "Replaces XeGTAO's occlusion with GT-VBAO (less dark halo around thin objects). Needs XeGTAO Enable.", !g_gtao_enable, false);
#if DEVELOPMENT || TEST
      // DEVELOPMENT/TEST calibration only (drive cb_gtao, written at the deinterleave hook). Not persisted.
      ImGui::BeginDisabled(!g_gtao_enable);
      ImGui::SliderFloat("GTAO Final Value Power", &g_gtao_final_value_power, 0.3f, 4.5f); // midtone-shadow contrast dial
      ImGui::SliderFloat("GT-VBAO Final Value Power", &g_gtvbao_final_value_power, 0.3f, 4.5f);
      ImGui::SliderFloat("GTAO History Weight", &g_gtao_history_weight, 0.f, 0.98f);                       // Under DLSS/FSR, 0 = off
      ImGui::SliderFloat("GTAO Depth Scale", &g_gtao_depth_scale, 1.f, 200.f);                             // UE3 units -> ~meters; the anti-over-occlusion dial
      ImGui::SliderFloat("GTAO Radius Override", &g_gtao_radius_override, 0.f, 5.f);                       // 0 = use EFFECT_RADIUS
#if DEVELOPMENT                                                                                            // Both knobs are constexpr outside it (the shader's DebugViewRT is DEVELOPMENT only)
      ImGui::Combo("GTAO Debug View", &g_gtao_debug_view, "Off\0Depth gradient\0Normals\0AO x8\0Edges\0"); // diagnostics through the AO apply
      ImGui::Combo("GTAO Temporal Noise", &g_gtao_temporal, "Auto (DLSS/FSR)\0Off (frozen)\0On (frame % 64)\0");
#endif
      ImGui::EndDisabled();
#endif

      ImGui::SeparatorText("Effects");

      DrawSettingSlider("Lens Flare Intensity", "FlareOut", &gs.FlareOut, default_luma_global_game_settings.FlareOut, 0.f, 1.f, "Lens-flare / glare strength (1 = vanilla, 0 = off).");

      DrawSettingCheckbox("Video AutoHDR", "VideoAutoHDREnable", &gs.VideoAutoHDREnable, default_luma_global_game_settings.VideoAutoHDREnable, "Adds HDR highlights to pre-rendered videos (HDR only).");

      ImGui::BeginDisabled(gs.VideoAutoHDREnable <= 0.5f);
      DrawSettingSlider("Video HDR Boost", "VideoAutoHDRBoost", &gs.VideoAutoHDRBoost, default_luma_global_game_settings.VideoAutoHDRBoost, 0.f, 1.f, "Video highlight strength (0 = off).");
      ImGui::EndDisabled();

      DrawSettingCheckbox("Dithering", "Dithering", &gs.Dithering, default_luma_global_game_settings.Dithering, "Reduces gradient banding.");

      ImGui::SeparatorText("UI");
      DrawSettingCheckbox("Hide Gameplay UI", "HideUI", &g_hide_ui, false, "Disables the in-game UI.");

      ImGui::SeparatorText("Fixes");
      DrawSettingCheckbox("Fix Movie Memory Leak", "FixMovieLeak", &g_fix_movie_leak, true, "Frees the loading/cutscene movie memory the game leaks on each transition (fixes the RAM-growth crash). Movies still play.");
      if (g_fix_movie_leak && !BLMovieLeakFix::g_have_movie.load() && BLMovieLeakFix::g_frame >= BLMovieLeakFix::BUILD_CHECK_FRAME)
         ImGui::TextColored(ImVec4(1.f, 0.6f, 0.f, 1.f), "Inactive: no movie buffers detected (game build may differ).");
#if DEVELOPMENT || TEST
      ImGui::Text("freed: %u textures, %.1f MB", BLMovieLeakFix::g_freed_tex, BLMovieLeakFix::g_freed_bytes / 1048576.0);
#endif
   }

#if DEVELOPMENT
   void DrawImGuiDevSettings(DeviceData& device_data) override
   {
      const auto& stats = GetGameDeviceData(device_data).mv_last_stats;
      ImGui::SeparatorText("Motion vector research");
      ImGui::Checkbox("MV Enable", &g_mv_enable);
      if (ImGui::IsItemHovered())
         ImGui::SetTooltip("Draws the scene with the motion vector shaders without DLSS/FSR: camera and object motion (each draw finds its own\nprevious frame b0/b1/b3). The image must not change; the debug view is black with a static camera and lights up only\nwhat moves. ReShade.log: patched/refused shaders. Not saved.");
      ImGui::Checkbox("MV Force Jitter", &g_mv_force_jitter);
      if (ImGui::IsItemHovered())
         ImGui::SetTooltip("Jitters the scene (Halton 2/3) without an upscaler, with MV Enable. The image shakes by a subpixel; nothing\nmay flicker or lose pixels, and the debug view stays black with a static camera. Not saved.");
      ImGui::Checkbox("MV Disable Jitter", &g_mv_disable_jitter);
      if (ImGui::IsItemHovered())
         ImGui::SetTooltip("No projection jitter under DLSS/FSR (the upscaler gets zero jitter): isolates artifacts that come from the jitter. Not saved.");
      ImGui::Checkbox("MV Debug View", &g_mv_debug_view);
      if (ImGui::IsItemHovered())
         ImGui::SetTooltip("Shows the motion vector target (absolute, in pixels) through Core's debug draw.");
      ImGui::Checkbox("Reactive Mask", &g_sr_reactive_enable);
      if (ImGui::IsItemHovered())
      {
         ImGui::SetTooltip("Marks the pixels alpha blended draws drew, so FSR 3 and 4 trust their history less: all of them as reactive (over\nthe threshold), the non-additive ones (smoke, glass, water) also as transparency & composition. Not saved.");
      }
      ImGui::SliderFloat("Reactive Scale", &g_sr_reactive_scale, 0.f, 4.f);
      ImGui::SliderFloat("Reactive Threshold", &g_sr_reactive_threshold, 0.f, 1.f);
      if (ImGui::IsItemHovered())
      {
         ImGui::SetTooltip("Scaled reactivity under it is 0, over it 0.9 (AMD's binary mask; AMD 0.2, default 0.5: lower makes static glows shake). 0: the scaled reactivity itself.");
      }
      ImGui::Checkbox("Reactive Debug View", &g_sr_reactive_debug_view);
      ImGui::Text("Alpha blended draws with a mask: %u", stats.reactive_draws);
      ImGui::Text("Last frame: %u motion vector draws (%u matched, %u camera only, %u other camera, %u uncopied), %u jitter draws", stats.motion_vector_draws, stats.matched, stats.camera_only, stats.other_camera, stats.uncopied, stats.jitter_draws);
      ImGui::Text("Constant updates copied: %u, upscaler draws: %u, scene ended by 0x%08X", stats.updates, stats.sr_draws, stats.ended_by);
      std::string refused = "Refused:";
      for (size_t i = 0; i < std::size(mv_reject_names); i++)
         refused += std::format(" {} {}", stats.rejected[i], mv_reject_names[i]);
      ImGui::TextUnformatted(refused.c_str());
      ImGui::Text("Last checked target: format %u, dimension %u, %ux%u (output %.0fx%.0f)", stats.rejected_format, stats.rejected_dimension, stats.rejected_width, stats.rejected_height, double(device_data.output_resolution.x), double(device_data.output_resolution.y));
   }
#endif

   void PrintImGuiAbout() override
   {
      ImGui::PushTextWrapPos(0.f);
      ImGui::Text(
         "Luma for \"Borderlands GOTY Enhanced\" is developed by DristoforColumb and is open source and free.\n"
         "It adds HDR and DLAA or FSR 3 native anti-aliasing, and replaces the game's FXAA with SMAA and its HBAO+ with XeGTAO, plus 16x anisotropic filtering and a fix for the game's movie memory leak.\n"
         "Enable Anti-Aliasing and Ambient Occlusion in the game's video settings for SMAA and XeGTAO to apply.\n"
         "Do NOT run another HDR mod (e.g. RenoDX) alongside it.\n"
         "Thanks to the Luma team and contributors.\n"
         "If you enjoy it, consider donating.");
      ImGui::PopTextWrapPos();

      ImGui::NewLine();
      ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(70, 134, 0, 255));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(70 + 9, 134 + 9, 0, 255));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(70 + 18, 134 + 18, 0, 255));
      static const std::string donation_link = std::string("Buy DristoforColumb a Coffee on ko-fi ") + std::string(ICON_FK_OK);
      if (ImGui::Button(donation_link.c_str()))
         ShellExecuteA(nullptr, "open", "https://ko-fi.com/dristoforcolumb", nullptr, nullptr, SW_SHOWNORMAL);
      ImGui::PopStyleColor(3);

      ImGui::NewLine();
      static const std::string social_link = std::string("Join our \"HDR Den\" Discord ") + std::string(ICON_FK_SEARCH);
      if (ImGui::Button(social_link.c_str()))
      {
         // Unique link for Luma's HDR Den (tracks the origin of people joining); do not share for other purposes.
         static const std::string discord_link = std::string("https://discord.gg/J9fM") + std::string("3EVuEZ");
         ShellExecuteA(nullptr, "open", discord_link.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
      }
      static const std::string contributing_link = std::string("Contribute on Github ") + std::string(ICON_FK_FILE_CODE);
      if (ImGui::Button(contributing_link.c_str()))
         ShellExecuteA(nullptr, "open", "https://github.com/Filoppi/Luma-Framework", nullptr, nullptr, SW_SHOWNORMAL);

      ImGui::NewLine();
      ImGui::Text("Build Date: %s %s", __DATE__, __TIME__);

      ImGui::NewLine();
      ImGui::Text("Credits:"
                  "\n\nMain:"
                  "\nDristoforColumb"
                  "\n\nThird Party:"
                  "\nReShade"
                  "\nImGui"
                  "\nRenoDX (HDR tonemap method)"
                  "\nMacLeod-Boynton hue emulation (RenoDX)"
                  "\nSMAA (Iryoku)"
                  "\nXeGTAO (Intel)"
                  "\nGT-VBAO (Mirko Salm)"
                  "\nAMD FidelityFX (RCAS + FSR)"
                  "\nNVIDIA NGX (DLSS)");
   }
};

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
   if (ul_reason_for_call == DLL_PROCESS_ATTACH)
   {
      const char* project_name = PROJECT_NAME;
      const char* cleared_project_name = (project_name[0] == '_') ? (project_name + 1) : project_name;

      uint32_t mod_version = 3; // a bump resets stale shader-define slots and cached settings/shaders
      Globals::SetGlobals(cleared_project_name, "Borderlands GOTY Enhanced Luma HDR + SMAA mod", "", mod_version);

      // scRGB fp16 swapchain; the replaced tonemap writes gamma-encoded HDR (1.0 = paper white) into the fp16 post chain, and the
      // Display Composition scales paper white, encodes and gamut maps at present.
      swapchain_format_upgrade_type = TextureFormatUpgradesType::AllowedEnabled;
      swapchain_upgrade_type = SwapchainUpgradeType::scRGB; // r8g8b8a8_unorm backbuffer -> r16g16b16a16_float
      texture_format_upgrades_type = TextureFormatUpgradesType::AllowedEnabled;
      // Safety minimum: the remaster already renders its post chain in fp16. r10g10b10a2 covers any 10-bit target at
      // swapchain size (the backbuffer itself is r8g8b8a8); r11g11b10_float includes the HBAO+ view normals XeGTAO
      // reads. r8g8b8a8 / b8g8r8a8 are left alone deliberately: nothing downstream needs them, and _srgb -> fp16 risks a
      // sampling shift.
      texture_upgrade_formats = {
         reshade::api::format::r10g10b10a2_unorm,
         reshade::api::format::r10g10b10a2_typeless,
         reshade::api::format::r11g11b10_float,
      };
      texture_format_upgrades_2d_size_filters = 0 | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainResolution | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainAspectRatio;
      force_disable_display_composition = false;

      // AF16x: mode 4 upgrades the game's AF samplers to MaxAnisotropy=16 (clarity on oblique surfaces).
      // LOD bias offset 0 without DLSS/FSR (no TAA: a negative bias would shimmer), set per frame with them (see OnPresent).
      enable_samplers_upgrade = true; // boot-time only
      samplers_upgrade_mode = 4;

      game = new BorderlandsGoty();
   }

   CoreMain(hModule, ul_reason_for_call, lpReserved);

   // Leak-fix resource hooks: register AFTER CoreMain's reshade::register_addon. Multiple callbacks per
   // event are allowed (coexist with core); CoreMain's DLL_PROCESS_DETACH unregister_addon removes them.
   if (ul_reason_for_call == DLL_PROCESS_ATTACH)
   {
      BLMovieLeakFix::g_exe_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
      reshade::register_event<reshade::addon_event::init_resource>(BLMovieLeakFix::OnInitResource);
      reshade::register_event<reshade::addon_event::destroy_resource>(BLMovieLeakFix::OnDestroyResource);
      reshade::register_event<reshade::addon_event::init_resource_view>(BLMovieLeakFix::OnInitResourceView);
      reshade::register_event<reshade::addon_event::destroy_resource_view>(BLMovieLeakFix::OnDestroyResourceView);
   }

   return TRUE;
}
