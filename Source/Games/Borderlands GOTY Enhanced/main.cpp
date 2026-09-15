// Borderlands GOTY Enhanced — Luma HDR + SMAA mod (Unreal Engine 3.5, D3D11).
// - HDR: swapchain -> scRGB fp16; replaced UE3 final-color PS (0xB030BAA6 / 0xFE88487E) runs the game's own grade
//   as an extended HDR function, takes its hue from a soft ReinhardPiecewise reference through MacLeod-Boynton
//   emulation, then DICE-maps to the display. The video (0x0E97A4A0) and lens-flare (0x010371F2) passes are
//   replaced too. Core Display Composition does the paper-white scale + encode.
//   One HDR mod owns the swapchain -> any other HDR mod must be removed from the game folder.
// - AA: compute FXAA (3.11 work-queue) -> SMAA (ULTRA + color edge + depth predication) + optional RCAS. Edge
//   detection reads the scene as stored, in GAMMA space (POST_PROCESS_SPACE_TYPE 0, 1.0 = paper white); the blend
//   reads its linear decode. The result is CopyResource'd back into the swapchain.
// - AO: XeGTAO replaces the native HBAO+ (hash block below). Plus AF16x and a fix for the game's movie RAM leak.

#define GAME_BORDERLANDS_GOTY 1

// Don't pop the DEVELOPMENT auto-debugger MessageBox on DLL attach: under a borderless/fullscreen game it's
// invisible and blocks the loader (ReShade times out the addon load -> error 1114).
#define DISABLE_AUTO_DEBUGGER 1

#define GEOMETRY_SHADER_SUPPORT 0
#define ENABLE_SMAA 1

#include "..\..\Core\core.hpp"
#include <shellapi.h> // ShellExecuteA for About links (system() hangs the render thread in exclusive fullscreen)

// FXAA is a compute work-queue implementation (FXAA 3.11 CS):
//   0x81CDE53D = pass 1 edge-detect (builds WorkQueue into scratch buffers; leaves color untouched — left running).
//   0x08891303 = pass 2 resolve: WorkQueue + Luma + InColor(t2) -> Color(u0, swapchain in-place). Replaced with SMAA.
static constexpr uint32_t kFXAAResolveHash = 0x08891303; // FXAA resolve CS — replaced with SMAA
static constexpr uint32_t kCelShadingHash = 0x08DC66D1;  // cel-shading edge PS — binds scene depth at t0 (predication source)

// AO: XeGTAO replaces the game's native NVIDIA HBAO+ (GFSDK_SSAO). Full-res 4K chain:
// deinterleave 0xFFE232A6 -> normals 0xB2B47225 (left running) -> coarse horizon 0xF534EB09 -> bilateral
// blur 0x4E1BEE34 -> apply-multiply PS 0x44764BF6. We capture scene depth at the deinterleave and the
// packed view normals at the coarse pass (skipping both), then at the blur dispatch run the 4 XeGTAO passes
// into ITS u0 (the game's FINAL r16g16_float AO; apply reads .x) so the apply blit composites our AO
// unchanged. XeGTAO reads the game's own cb0 ($Globals: ProjInfo) + cb2 (MinZ_MaxZRatioCS), still bound at
// the injection point. No TAA -> NoiseIndex frozen 0, spatial denoise x2.
static constexpr uint32_t kAODeinterleaveHash = 0xFFE232A6; // scene depth -> quarter-res array — skipped (we build our own mip pyramid)
static constexpr uint32_t kAOCoarseHash = 0xF534EB09;       // HBAO+ horizon march (x2), binds view normals at t0 — skipped (capture normals)
static constexpr uint32_t kAOBlurHash = 0x4E1BEE34;         // bilateral blur -> FINAL r16g16_float u0 — replaced with XeGTAO

// User settings, persisted in the [Luma] config section (LoadConfigs) unless noted otherwise.
static bool g_smaa_enable = true;
static float g_rcas_sharpness = 0.f; // RCAS sharpen on SMAA output; default off — the ink outlines are already clean and sharpening haloes them.
static bool g_hide_ui = false;       // hide the game's HUD (skips swapchain-targeting UI draws) — for clean screenshots
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

// Ambient Occlusion: XeGTAO replaces the native HBAO+ (default ON = supersede it). Persisted as "XeGTAOEnable".
static bool g_gtao_enable = true;
// Runtime XeGTAO knobs (LumaGTAO cb b11); their sliders are DEVELOPMENT/TEST only. FinalValuePower = primary darkness dial
// (calibrate to the vanilla HBAO+ histogram — its PowExponent does not transfer numerically). DepthScale =
// viewZ divisor (UE3 units, near plane ~10 -> ~meters) so Intel's tuned radius/falloff apply; the dial
// against broad over-occlusion. RadiusOverride > 0 overrides EFFECT_RADIUS (in scaled units).
static float g_gtao_final_value_power = 1.0f;
static float g_gtao_depth_scale = 50.f;
static float g_gtao_radius_override = 0.f;
#if DEVELOPMENT
static int g_gtao_debug_view = 0; // 0=off 1=depth gradient 2=normals 3=AO x8 4=edges (drawn via the game's apply blit). DEV only: the shader's DebugViewRT blocks are #if DEVELOPMENT.
#endif

// Loading-movie memory-leak fix (toggle "Fix Movie Memory Leak" under Fixes).
// The game's Bink movies create D3D11 YUV decode buffers and never release them -> linear RAM
// growth -> OOM. The leak is the GAME, not Luma. We drop the game's leaked COM refs on OLD movie
// generations (orphaned: a movie's buffers are sampled only during its own playback). Tagged by
// creation call-stack RVAs in BorderlandsGOTY.exe (frozen remaster; non-matching build tags nothing
// = safe no-op). Movies keep playing. Diagnosis/validation: _tools/BL Leak Tracker.
static bool g_fix_movie_leak = true; // default ON; persisted as "FixMovieLeak"

namespace BLMovieLeakFix
{
   // Build-specific RVAs (frozen remaster). A non-matching build shifts these -> nothing tags ->
   // silent no-op; BUILD_CHECK_FRAME drives a one-shot telemetry warning for that case.
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
   // Coarse cross-thread gates (present thread writes g_frame; workers read). atomic = no data race;
   // real map sync is g_mtx.
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

   // Tag movie YUV decode buffers at creation (stack walk only for >= 2MB buffers).
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
      struct Plan
      {
         uintptr_t res;
         std::vector<uintptr_t> views;
         uint64_t bytes;
      };
      std::vector<Plan> plans;
      {
         const std::lock_guard<std::mutex> lk(g_mtx);
         for (const auto& kv : g_movie) // Phase A: pick victims (keep current + previous gen, must be idle)
         {
            const MovieTex& m = kv.second;
            if (m.gen > g_cur_gen - RELEASE_GEN_LAG)
               continue;
            if ((g_frame - m.created_frame) < RELEASE_IDLE_FRAMES)
               continue;
            plans.push_back({kv.first, {}, m.bytes});
         }
         if (plans.empty())
            return;
         std::unordered_map<uintptr_t, size_t> idx;
         for (size_t i = 0; i < plans.size(); ++i)
            idx[plans[i].res] = i;
         for (const auto& vk : g_view2res)
         {
            auto it = idx.find(vk.second);
            if (it != idx.end())
               plans[it->second].views.push_back(vk.first);
         }
         for (const auto& p : plans) // Phase B: UNLINK before any Release (re-entrant callbacks then find nothing)
         {
            for (uintptr_t v : p.views)
               g_view2res.erase(v);
            g_movie.erase(p.res);
         }
      }
      uint32_t ft = 0;
      uint64_t fb = 0; // Phase C: RELEASE with the lock released
      bool partial = false;
      for (const Plan& p : plans)
      {
         for (uintptr_t v : p.views)
            reinterpret_cast<IUnknown*>(v)->Release();
         // rc==0 => actually freed (count it); rc>0 => game holds extra refs, not reclaimed. (rc is a
         // by-value ULONG, never a deref of a freed object.)
         const ULONG rc = reinterpret_cast<IUnknown*>(p.res)->Release();
         if (rc == 0)
         {
            ft++;
            fb += p.bytes;
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
   ComPtr<ID3D11Buffer> cb_pred;
   float pred_tolerance = -1.f;
   ComPtr<ID3D11Texture2D> tex_pred;
   ComPtr<ID3D11UnorderedAccessView> uav_pred;
   ComPtr<ID3D11ShaderResourceView> srv_pred;
   uint32_t pred_w = 0, pred_h = 0;
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

   // SMAA metrics CB (b1) = (1/w, 1/h, w, h) + (predication scale,0,0,0), recreated on resolution or predication-state change.
   ComPtr<ID3D11Buffer> cb_smaa_metrics;
   uint32_t smaa_metrics_w = 0, smaa_metrics_h = 0;
   float smaa_metrics_pred_scale = -1.f; // 2.0 = valid depth (predication active), 1.0 = no/mismatched depth (plain ULTRA threshold)

   // Tracks the size DrawSMAA built its core-managed intermediates at (to rebuild on resolution change).
   uint32_t smaa_core_w = 0, smaa_core_h = 0;

   // SMAA inputs (fp16), recreated on resolution change: scene-color snapshot (CopyResource'd each frame, edge
   // detection) and its linear-light decode (neighborhood blend).
   ComPtr<ID3D11Texture2D> tex_input;
   ComPtr<ID3D11ShaderResourceView> srv_input;
   ComPtr<ID3D11Texture2D> tex_input_linear;
   ComPtr<ID3D11UnorderedAccessView> uav_input_linear;
   ComPtr<ID3D11ShaderResourceView> srv_input_linear;
   uint32_t smaa_temps_w = 0, smaa_temps_h = 0;

   // SMAA output temp (fp16, SRV+RTV). Copied into the swapchain target after SMAA (or fed to RCAS first).
   ComPtr<ID3D11Texture2D> tex_smaa_out;
   ComPtr<ID3D11RenderTargetView> tex_smaa_out_rtv;
   ComPtr<ID3D11ShaderResourceView> tex_smaa_out_srv;
   uint32_t smaa_out_w = 0, smaa_out_h = 0;

   // RCAS sharpen CB (b0) = (w, h, sharpness, 0), recreated on resolution/sharpness change.
   ComPtr<ID3D11Buffer> cb_sharpen;
   uint32_t sharpen_w = 0, sharpen_h = 0;
   float sharpen_amount = -1.f;
   // RCAS output temp (fp16, RTV). RCAS reads tex_smaa_out_srv -> writes here -> copied into the swapchain target.
   ComPtr<ID3D11Texture2D> tex_rcas_out;
   ComPtr<ID3D11RenderTargetView> tex_rcas_out_rtv;
   uint32_t rcas_out_w = 0, rcas_out_h = 0;

   // --- XeGTAO scratch (all at the game's AO full-res; cached, rebuilt on size change). ---
   // Game inputs captured per frame (reset in OnPresent): full-res r24 scene depth (deinterleave t0) and
   // the packed view normals (coarse-AO t0). A captured srv_gtao_depth arms the coarse/blur takeover.
   ComPtr<ID3D11ShaderResourceView> srv_gtao_depth;
   ComPtr<ID3D11ShaderResourceView> srv_gtao_normals;
   // Prefiltered viewspace-depth MIP pyramid (R32F, 5 mips) — 5 per-mip UAVs + one full SRV.
   ComPtr<ID3D11Texture2D> tex_gtao_depth_mips;
   ComPtr<ID3D11UnorderedAccessView> gtao_depth_mip_uavs[5];
   ComPtr<ID3D11ShaderResourceView> srv_gtao_depth_mips;
   // Two working AO+edges buffers (R8G8_UNORM) ping-ponged by main pass -> denoise 1.
   ComPtr<ID3D11Texture2D> tex_gtao_working[2];
   ComPtr<ID3D11UnorderedAccessView> uav_gtao_working[2];
   ComPtr<ID3D11ShaderResourceView> srv_gtao_working[2];
   uint32_t gtao_w = 0, gtao_h = 0;
   // LumaGTAO knob CB (b11) = (FinalValuePower, DepthScale, RadiusOverride, DebugView); recreated on change.
   ComPtr<ID3D11Buffer> cb_gtao;
   float gtao_cb_fvp = -1.f, gtao_cb_depth_scale = -1.f, gtao_cb_radius = -1.f, gtao_cb_debug = -1.f;

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
      }
      gtao_w = gtao_h = 0;
   }
};

class BorderlandsGoty final : public Game
{
   static BorderlandsGotyGameDeviceData& GetGameDeviceData(DeviceData& device_data)
   {
      return *static_cast<BorderlandsGotyGameDeviceData*>(device_data.game);
   }

   // Named injected shaders live in unordered_maps the render thread otherwise only reads: look them up with
   // "find" (operator[] would default-insert on a miss and mutate a map DrawSMAA reads concurrently).
   template <typename ShaderMap>
   static auto FindShader(const ShaderMap& shaders, uint32_t name_hash)
   {
      const auto it = shaders.find(name_hash);
      return it != shaders.end() ? it->second.get() : nullptr;
   }

   template <typename ShaderMap>
   static bool AllShadersReady(const ShaderMap& shaders, std::initializer_list<uint32_t> name_hashes)
   {
      for (uint32_t name_hash : name_hashes)
      {
         if (FindShader(shaders, name_hash) == nullptr)
            return false;
      }
      return true;
   }

   // Create an IMMUTABLE constant buffer holding `size` bytes from `data`. Resets `out`; returns true on success.
   static bool CreateImmutableCB(ID3D11Device* device, const void* data, UINT size, ComPtr<ID3D11Buffer>& out)
   {
      out.reset();
      D3D11_BUFFER_DESC bd = {};
      bd.ByteWidth = size;
      bd.Usage = D3D11_USAGE_IMMUTABLE;
      bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
      D3D11_SUBRESOURCE_DATA sd = {};
      sd.pSysMem = data;
      return SUCCEEDED(device->CreateBuffer(&bd, &sd, out.put()));
   }

   // Create a DEFAULT-usage 2D texture (1 mip, 1 sample) of w×h with the given bind flags, fp16 unless another
   // format is asked for. Resets `out`.
   static bool CreateDefaultTex(ID3D11Device* device, uint32_t w, uint32_t h, UINT bind_flags, ComPtr<ID3D11Texture2D>& out, DXGI_FORMAT format = DXGI_FORMAT_R16G16B16A16_FLOAT)
   {
      out.reset();
      D3D11_TEXTURE2D_DESC td = {};
      td.Width = w;
      td.Height = h;
      td.MipLevels = 1;
      td.ArraySize = 1;
      td.Format = format;
      td.SampleDesc.Count = 1;
      td.Usage = D3D11_USAGE_DEFAULT;
      td.BindFlags = bind_flags;
      return SUCCEEDED(device->CreateTexture2D(&td, nullptr, out.put()));
   }

#if DEVELOPMENT
   // One-shot readback of the predication mask, so g_smaa_pred_tolerance is calibrated from numbers rather than
   // from screenshots. A mean would be useless here: on a well-tuned frame the mask is 0 nearly everywhere and any
   // average drowns in that, so this reports COVERAGE at the level SMAA actually compares against (0.5) plus the
   // shape either side of it. Every texel is read, not a stride - a 1px silhouette is precisely the signal a
   // subsample would step over. Copies on the frame the button is pressed and maps on a later one (non-blocking),
   // so the press never stalls the render thread.
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

public:
   void OnInit(bool async) override
   {
      // Game-specific defines: TONEMAP_TYPE drives Luma_BL_Tonemap.hlsl, XE_GTAO_QUALITY drives Luma_BL_XeGTAO.hlsl.
      std::vector<ShaderDefineData> game_shader_defines_data = {
         {"TONEMAP_TYPE", '1', true, false, "0 - SDR: Vanilla clamped reference\n1 - HDR: Extended UE3 grade + DICE"},
         {"XE_GTAO_QUALITY", '3', true, false, "Ambient Occlusion (XeGTAO) quality (slice count)\n0 - Low\n1 - Medium\n2 - High\n3 - Very High\n4 - Ultra", 4},
      };
      shader_defines_data.append_range(game_shader_defines_data);

      // Post-process buffers in GAMMA space (UE3 engine, gamma-2.2 SDR): the gamma-SDR HUD blends on top in
      // gamma to look vanilla (linear space washes it out). Tonemap pre-scales by GamePaperWhite/UIPaperWhite
      // (UI_DRAW_TYPE 2); the core composition decodes gamma + applies paper white + scRGB encode.
      GetShaderDefineData(POST_PROCESS_SPACE_TYPE_HASH).SetDefaultValue('0');
      GetShaderDefineData(EARLY_DISPLAY_ENCODING_HASH).SetDefaultValue('0');
      GetShaderDefineData(VANILLA_ENCODING_TYPE_HASH).SetDefaultValue('1'); // game shipped gamma-2.2 SDR
      GetShaderDefineData(GAMMA_CORRECTION_TYPE_HASH).SetDefaultValue('1');
      GetShaderDefineData(GAMUT_MAPPING_TYPE_HASH).SetDefaultValue('1'); // gamut-map wild colors in composition
      GetShaderDefineData(UI_DRAW_TYPE_HASH).SetDefaultValue('2');       // HUD gets its own UIPaperWhite + gamma blend

      // Linear-light decode of the SMAA input for its neighborhood blend.
      native_shaders_definitions.emplace(CompileTimeStringHash("BL SMAA Linearize CS"),
         ShaderDefinition("Luma_BL_SMAALinearize", reshade::api::pipeline_subobject_type::compute_shader));
      // Depth-extract CS for SMAA predication: hardware d24 -> R16F plane-deviation edge-ness.
      native_shaders_definitions.emplace(CompileTimeStringHash("BL Depth Extract CS"),
         ShaderDefinition("Luma_BL_DepthExtract", reshade::api::pipeline_subobject_type::compute_shader));
      // RCAS sharpen PS (drawn via core "Copy VS" + DrawCustomPixelShader after SMAA).
      native_shaders_definitions.emplace(CompileTimeStringHash("BL Sharpen PS"),
         ShaderDefinition{"Luma_BL_Sharpen", reshade::api::pipeline_subobject_type::pixel_shader, nullptr, "sharpen_ps"});

      // XeGTAO (replaces the game's native HBAO+; see the AO hash block above). 4 compute passes out of one
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

      // Game uses CB slots b0-b3, so b11 (XeGTAO knobs) and b12/b13 are free for Luma.
      // luma_data is used by the Display Composition; luma_ui stays off (UI drawn by the game).
      luma_settings_cbuffer_index = 13;
      luma_data_cbuffer_index = 12;
      luma_ui_cbuffer_index = -1;

      // Manual Scene + UI Paper White sliders instead of the OS HDR reference level. Core gates the separate
      // "UI Paper White" slider on UI_DRAW_TYPE >= 1 && !use_os_reference_white_level. Default 203 nits (BT.2408).
      use_os_reference_white_level = false;

      // User grade controls (read in Luma_BL_Tonemap.hlsl via LumaSettings.GameSettings). All vanilla by default.
      default_luma_global_game_settings.Exposure = 1.f; // multiplier (1x)
      default_luma_global_game_settings.Saturation = 1.f;
      default_luma_global_game_settings.HighlightDechroma = 0.f; // off by default; only the mandatory DICE/gamut desaturation applies. Slider = optional taste.
      default_luma_global_game_settings.BloomIntensity = 1.f;
      default_luma_global_game_settings.Contrast = 1.f;
      default_luma_global_game_settings.Dithering = 1.f;          // subtle anti-banding on by default
      default_luma_global_game_settings.FlareOut = 1.f;           // additive lens-flare/glare scale (1 = vanilla)
      default_luma_global_game_settings.VideoAutoHDREnable = 1.f; // light AutoHDR on Bink movies, HDR only (on by default)
      default_luma_global_game_settings.VideoAutoHDRBoost = 0.5f; // highlight-expansion strength (peak ~165 nits at 0.5)
      cb_luma_global_settings.GameSettings = default_luma_global_game_settings;
   }

   void OnCreateDevice(ID3D11Device* native_device, DeviceData& device_data) override
   {
      device_data.game = new BorderlandsGotyGameDeviceData;
   }

   void OnDestroyDeviceData(DeviceData& device_data) override
   {
      // GameDeviceData lacks a virtual destructor; delete through the concrete type to release derived members.
      delete static_cast<BorderlandsGotyGameDeviceData*>(device_data.game);
      device_data.game = nullptr;
   }

   DrawOrDispatchOverrideType OnDrawOrDispatch(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, reshade::api::shader_stage stages, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, bool is_custom_pass, bool& updated_cbuffers, std::function<void()>* original_draw_dispatch_func) override
   {
      auto& gd = GetGameDeviceData(device_data);

      const bool is_immediate = native_device_context->GetType() == D3D11_DEVICE_CONTEXT_IMMEDIATE;

      // Hide HUD: cancel the game's UI draws (for clean screenshots). A UI draw = one targeting a swapchain back
      // buffer — the same detection the core UI handling uses (RTV resource in device_data.back_buffers; see
      // core.hpp). Compute/offscreen draws have no swapchain RTV so they're untouched.
      if (g_hide_ui && is_immediate && !is_custom_pass)
      {
         ComPtr<ID3D11RenderTargetView> rtv;
         native_device_context->OMGetRenderTargets(1, rtv.put(), nullptr);
         if (rtv)
         {
            ComPtr<ID3D11Resource> rtv_res;
            rtv->GetResource(rtv_res.put());
            if (rtv_res)
            {
               bool targeting_swapchain;
               {
                  const std::shared_lock lock(device_data.mutex);
                  targeting_swapchain = device_data.back_buffers.contains((uint64_t)rtv_res.get());
               }
               if (targeting_swapchain)
                  return DrawOrDispatchOverrideType::Replaced; // drop the UI draw
            }
         }
      }

      // SMAA predication inputs: the cel-shading edge pass binds the D24S8 depth at PS t0 and linearizes it with
      // PSOffsetConstants at PS b2.
      if (is_immediate && original_shader_hashes.Contains(kCelShadingHash, reshade::api::shader_stage::pixel))
      {
         gd.srv_depth.reset();
         gd.cb_game_offsets.reset();
         native_device_context->PSGetShaderResources(0, 1, gd.srv_depth.put());
         native_device_context->PSGetConstantBuffers(2, 1, gd.cb_game_offsets.put());
      }

      // XeGTAO replaces the game's native HBAO+ (chain order: deinterleave x2 -> normals 0xB2B47225 ->
      // coarse x2 -> blur -> apply blit). We take the chain over ONLY when everything is ready at the first
      // dispatch — a failure there leaves the whole native chain untouched (graceful fallback, like the SMAA
      // fp16 guard). The separate normals pass 0xB2B47225 is left running (not hooked) so its ViewNormalTex
      // output is valid for our main pass.
      if (g_gtao_enable && is_immediate)
      {
         // (a) Deinterleave: capture the full-res r24 scene depth (t0) + build/validate ALL scratch, then skip
         // the native dispatch. Both dispatches of the pair hit this branch (second is a cheap re-capture).
         if (original_shader_hashes.Contains(kAODeinterleaveHash, reshade::api::shader_stage::compute))
         {
            if (!AllShadersReady(device_data.native_compute_shaders, {CompileTimeStringHash("BL XeGTAO Prefilter Depths CS"), CompileTimeStringHash("BL XeGTAO Main Pass CS"), CompileTimeStringHash("BL XeGTAO Denoise Pass 1 CS"), CompileTimeStringHash("BL XeGTAO Denoise Pass 2 CS")}))
               return DrawOrDispatchOverrideType::None;

            ComPtr<ID3D11ShaderResourceView> srv_d;
            native_device_context->CSGetShaderResources(0, 1, srv_d.put());
            if (!srv_d)
               return DrawOrDispatchOverrideType::None;
            uint4 dinfo{};
            DXGI_FORMAT dfmt = DXGI_FORMAT_UNKNOWN;
            GetResourceInfo(srv_d.get(), dinfo, dfmt);
            if (dinfo.x == 0 || dinfo.y == 0)
               return DrawOrDispatchOverrideType::None;
            // Size the scratch (and dispatches) from the input depth desc (the game's HBAO+ full-res). The
            // game's own cb0 (InvFullResolution etc.) is content-dimensioned, so the shader's pixel<->UV math
            // is correct; the final pass writes the game's AO buffer at identical pixel coords.
            const uint32_t w = dinfo.x, h = dinfo.y;

            // (Re)create the scratch at the game's AO full-res (cached; NOT per-frame).
            if (gd.gtao_w != w || gd.gtao_h != h || !gd.tex_gtao_depth_mips || !gd.tex_gtao_working[1])
            {
               gd.ReleaseGTAOScratch();

               D3D11_TEXTURE2D_DESC td = {};
               td.Width = w;
               td.Height = h;
               td.MipLevels = 5; // XE_GTAO_DEPTH_MIP_LEVELS
               td.ArraySize = 1;
               td.Format = DXGI_FORMAT_R32_FLOAT;
               td.SampleDesc.Count = 1;
               td.Usage = D3D11_USAGE_DEFAULT;
               td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
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

               td.MipLevels = 1;
               td.Format = DXGI_FORMAT_R8G8_UNORM;
               for (int i = 0; ok && i < 2; i++)
               {
                  ok = ok && SUCCEEDED(native_device->CreateTexture2D(&td, nullptr, gd.tex_gtao_working[i].put()));
                  ok = ok && SUCCEEDED(native_device->CreateUnorderedAccessView(gd.tex_gtao_working[i].get(), nullptr, gd.uav_gtao_working[i].put()));
                  ok = ok && SUCCEEDED(native_device->CreateShaderResourceView(gd.tex_gtao_working[i].get(), nullptr, gd.srv_gtao_working[i].put()));
               }
               if (!ok)
               {
                  gd.ReleaseGTAOScratch();
                  return DrawOrDispatchOverrideType::None; // native chain runs whole
               }
               gd.gtao_w = w;
               gd.gtao_h = h;
            }

            // Knob CB (b11): immutable, recreated when a slider moves.
#if DEVELOPMENT
            const float dbg = (float)g_gtao_debug_view;
#else
            const float dbg = 0.f; // shader DebugViewRT is DEV-only; keep 0 elsewhere so the knob CB never churns
#endif
            if (!gd.cb_gtao || gd.gtao_cb_fvp != g_gtao_final_value_power || gd.gtao_cb_depth_scale != g_gtao_depth_scale ||
                gd.gtao_cb_radius != g_gtao_radius_override || gd.gtao_cb_debug != dbg)
            {
               const float knobs[4] = {g_gtao_final_value_power, g_gtao_depth_scale, g_gtao_radius_override, dbg};
               if (CreateImmutableCB(native_device, knobs, sizeof(knobs), gd.cb_gtao))
               {
                  gd.gtao_cb_fvp = g_gtao_final_value_power;
                  gd.gtao_cb_depth_scale = g_gtao_depth_scale;
                  gd.gtao_cb_radius = g_gtao_radius_override;
                  gd.gtao_cb_debug = dbg;
               }
            }
            if (!gd.cb_gtao)
               return DrawOrDispatchOverrideType::None;

            gd.srv_gtao_depth = srv_d;

            return DrawOrDispatchOverrideType::Replaced; // skip the native deinterleave
         }

         // (b) Coarse horizon march: capture the packed view normals (t0), skip the native dispatch. Only when
         // we own the chain this frame — otherwise the native pipeline is left fully intact.
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

         // (c) Bilateral blur: ITS u0 is the game's FINAL r16g16_float AO — run the 4 XeGTAO passes into it and
         // cancel the native dispatch. The game's apply blit (untouched) then multiplies it into the scene.
         if (original_shader_hashes.Contains(kAOBlurHash, reshade::api::shader_stage::compute))
         {
            if (!gd.srv_gtao_depth)
               return DrawOrDispatchOverrideType::None;

            ComPtr<ID3D11UnorderedAccessView> uav_final;
            native_device_context->CSGetUnorderedAccessViews(0, 1, uav_final.put());
            // No output bound: unreachable in practice (the bilateral blur always binds its u0). Without a target
            // there is nothing for either path to write, so leave the native blur running rather than cancel it.
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
            // cb0 ($Globals: ProjInfo etc.) and cb2 (MinZ_MaxZRatioCS) are read directly by our shaders at the
            // same b0/b2 slots, INHERITED from the game — bound by the deinterleave/coarse passes earlier in the
            // same contiguous HBAO+ chain, not rebound here by design (immediate context, fixed pass order, no
            // ClearState between them). If a future variant reorders the AO passes, capture+rebind explicitly.

            static constexpr std::array<ID3D11UnorderedAccessView*, 5> uav_nulls5 = {};
            static constexpr std::array<ID3D11ShaderResourceView*, 2> srv_nulls2 = {};

            // 1) Prefilter: game full-res depth -> our R32F mip pyramid (each thread does 2x2 -> 16x16 per group).
            {
               native_device_context->CSSetShaderResources(0, 2, srv_nulls2.data());
               ID3D11ShaderResourceView* srv = gd.srv_gtao_depth.get();
               ID3D11UnorderedAccessView* uavs[5] = {gd.gtao_depth_mip_uavs[0].get(), gd.gtao_depth_mip_uavs[1].get(),
                  gd.gtao_depth_mip_uavs[2].get(), gd.gtao_depth_mip_uavs[3].get(), gd.gtao_depth_mip_uavs[4].get()};
               native_device_context->CSSetUnorderedAccessViews(0, 5, uavs, nullptr);
               native_device_context->CSSetShaderResources(0, 1, &srv);
               native_device_context->CSSetShader(cs_prefilter, nullptr, 0);
               native_device_context->Dispatch((w + 15) / 16, (h + 15) / 16, 1);
               native_device_context->CSSetUnorderedAccessViews(0, 5, uav_nulls5.data(), nullptr);
            }
            // Binding ORDER matters: the UAV must be set BEFORE the SRVs in every pass. Binding an SRV whose
            // resource is still bound as a CS UAV (from the previous pass) makes the runtime silently NULL the
            // SRV (the existing UAV wins the hazard) — the whole chain then reads zeros while every write
            // "succeeds". Setting the new UAV first auto-unbinds the old one, so the SRV bind is clean.
            // 2) Main pass: pyramid + game view normals -> AO+edges (working0).
            {
               native_device_context->CSSetShaderResources(0, 2, srv_nulls2.data());
               ID3D11ShaderResourceView* srvs[2] = {gd.srv_gtao_depth_mips.get(), gd.srv_gtao_normals.get()};
               ID3D11UnorderedAccessView* uav = gd.uav_gtao_working[0].get();
               native_device_context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
               native_device_context->CSSetShaderResources(0, 2, srvs);
               native_device_context->CSSetShader(cs_main, nullptr, 0);
               native_device_context->Dispatch((w + 7) / 8, (h + 7) / 8, 1);
            }
            // 3) Denoise 1: working0 -> working1 (2 horizontal pixels per thread).
            {
               native_device_context->CSSetShaderResources(0, 2, srv_nulls2.data());
               ID3D11ShaderResourceView* srv = gd.srv_gtao_working[0].get();
               ID3D11UnorderedAccessView* uav = gd.uav_gtao_working[1].get();
               native_device_context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
               native_device_context->CSSetShaderResources(0, 1, &srv);
               native_device_context->CSSetShader(cs_denoise_1, nullptr, 0);
               native_device_context->Dispatch((w + 15) / 16, (h + 7) / 8, 1);
            }
            // 4) Denoise 2 (final): working1 -> the game's r16g16_float AO target.
            {
               native_device_context->CSSetShaderResources(0, 2, srv_nulls2.data());
               ID3D11ShaderResourceView* srv = gd.srv_gtao_working[1].get();
               ID3D11UnorderedAccessView* uav = uav_final.get();
               native_device_context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
               native_device_context->CSSetShaderResources(0, 1, &srv);
               native_device_context->CSSetShader(cs_denoise_2, nullptr, 0);
               native_device_context->Dispatch((w + 15) / 16, (h + 7) / 8, 1);
            }

            st.Restore(native_device_context);
            return DrawOrDispatchOverrideType::Replaced; // cancel the native blur
         }
      }

#if ENABLE_SMAA
      // Replace the compute FXAA resolve with SMAA. Replace EVERY occurrence in the frame (the game can run the
      // resolve more than once — e.g. menu/transition frames have two), each with its own InColor/Color target.
      if (g_smaa_enable && is_immediate && !is_custom_pass &&
          original_shader_hashes.Contains(kFXAAResolveHash, reshade::api::shader_stage::compute))
      {
         // FXAA resolve is IN-PLACE: InColor (t2) aliases Color (u0 = swapchain), so D3D auto-unbinds the SRV at
         // dispatch (t2 reads null). We therefore source the scene color from the UAV's resource (it holds the
         // tonemapped pre-FXAA color, since we're replacing FXAA) by copying it into an SRV-capable temp.
         ComPtr<ID3D11UnorderedAccessView> uav_color;
         native_device_context->CSGetUnorderedAccessViews(0, 1, uav_color.put());
         if (!uav_color)
            return DrawOrDispatchOverrideType::None;

         ComPtr<ID3D11Resource> color_res; // swapchain target (CopyResource source + destination)
         uav_color->GetResource(color_res.put());
         if (!color_res)
            return DrawOrDispatchOverrideType::None;

         uint4 cinfo{};
         DXGI_FORMAT cfmt = DXGI_FORMAT_UNKNOWN;
         GetResourceInfo(color_res.get(), cinfo, cfmt);
         const uint32_t w = cinfo.x, h = cinfo.y;
         if (w == 0 || h == 0)
            return DrawOrDispatchOverrideType::None;
         // fp16 guard: the SMAA path forces R16G16B16A16_FLOAT temps; CopyResource silently no-ops on a format
         // mismatch, so a non-fp16 swapchain (HDR upgrade absent) would feed SMAA uninitialized memory and copy
         // garbage back. Bail to the game's native FXAA instead of shipping a corrupt frame.
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
            return DrawOrDispatchOverrideType::None;
         }

         // Predication depth is valid only if captured this frame AND it matches the color dimensions (a resolution
         // change can leave a different-size depth). When invalid we pass a null predication texture and a scale of
         // 1.0 so SMAA uses the plain ULTRA threshold rather than the doubled predicated baseline.
         auto* pred_cs = FindShader(device_data.native_compute_shaders, CompileTimeStringHash("BL Depth Extract CS"));
         bool pred_ok = g_smaa_predication && gd.srv_depth && gd.cb_game_offsets && pred_cs != nullptr;
         if (pred_ok)
         {
            // The extract CS maps texels 1:1, so a depth buffer of a different size would read a sub-rect and
            // misalign the mask against the colour grid.
            uint4 dinfo{};
            DXGI_FORMAT dfmt = DXGI_FORMAT_UNKNOWN;
            GetResourceInfo(gd.srv_depth.get(), dinfo, dfmt);
            pred_ok = (dinfo.x == w && dinfo.y == h);
         }
         if (pred_ok)
         {
            if (!gd.cb_pred || gd.pred_tolerance != g_smaa_pred_tolerance)
            {
               const float pp[4] = {g_smaa_pred_tolerance, 0.f, 0.f, 0.f};
               if (CreateImmutableCB(native_device, pp, sizeof(pp), gd.cb_pred))
                  gd.pred_tolerance = g_smaa_pred_tolerance;
            }
            if (!gd.tex_pred || gd.pred_w != w || gd.pred_h != h)
            {
               gd.uav_pred.reset();
               gd.srv_pred.reset();
               gd.tex_pred.reset();
               if (CreateDefaultTex(native_device, w, h, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS, gd.tex_pred, DXGI_FORMAT_R16_FLOAT))
               {
                  native_device->CreateUnorderedAccessView(gd.tex_pred.get(), nullptr, gd.uav_pred.put());
                  native_device->CreateShaderResourceView(gd.tex_pred.get(), nullptr, gd.srv_pred.put());
                  gd.pred_w = w;
                  gd.pred_h = h;
               }
            }
            pred_ok = gd.cb_pred && gd.uav_pred && gd.srv_pred;
         }
         const float pred_scale = pred_ok ? 2.f : 1.f;

         // Shader-readiness gate (async loader / dev live-reload). If anything is missing, fall through to native FXAA.
         auto* linearize_cs = FindShader(device_data.native_compute_shaders, CompileTimeStringHash("BL SMAA Linearize CS"));
         if (linearize_cs == nullptr || !AllShadersReady(device_data.native_pixel_shaders, {CompileTimeStringHash("SMAA Edge Detection PS"), CompileTimeStringHash("SMAA Blending Weight Calculation PS"), CompileTimeStringHash("SMAA Neighborhood Blending PS")}) ||
             !AllShadersReady(device_data.native_vertex_shaders, {CompileTimeStringHash("SMAA Edge Detection VS"), CompileTimeStringHash("SMAA Blending Weight Calculation VS"), CompileTimeStringHash("SMAA Neighborhood Blending VS")}))
            return DrawOrDispatchOverrideType::None;

         // DrawSMAA sizes its edge/blend/DSV intermediates from the first RTV and rebuilds only on swapchain re-init.
         // On a resolution change (in-game Resolution Scale) drop the 3 core-managed views so they recreate at the new size.
         if (gd.smaa_core_w != w || gd.smaa_core_h != h)
         {
            auto& mr = device_data.managed_resources;
            mr.depth_stencil_views[CompileTimeStringHash("smaa_dsv")].reset();
            mr.render_target_views[CompileTimeStringHash("smaa_edge_detection")].reset();
            mr.render_target_views[CompileTimeStringHash("smaa_blending_weight_calculation")].reset();
            gd.smaa_core_w = w;
            gd.smaa_core_h = h;
         }

         // (Re)create the SMAA metrics CB on resolution change OR when the predication state flips (depth
         // present/absent). pred_scale only toggles 1.0<->2.0 on menu/transition boundaries, so recreation is rare.
         if (!gd.cb_smaa_metrics || gd.smaa_metrics_w != w || gd.smaa_metrics_h != h || gd.smaa_metrics_pred_scale != pred_scale)
         {
            const float metrics[8] = {1.f / (float)w, 1.f / (float)h, (float)w, (float)h, pred_scale, 0.f, 0.f, 0.f};
            if (CreateImmutableCB(native_device, metrics, sizeof(metrics), gd.cb_smaa_metrics))
            {
               gd.smaa_metrics_w = w;
               gd.smaa_metrics_h = h;
               gd.smaa_metrics_pred_scale = pred_scale;
            }
         }
         if (!gd.cb_smaa_metrics)
            return DrawOrDispatchOverrideType::None;

         // (Re)create the SMAA output temp (fp16, SRV+RTV).
         if (!gd.tex_smaa_out || gd.smaa_out_w != w || gd.smaa_out_h != h)
         {
            gd.tex_smaa_out_rtv.reset();
            gd.tex_smaa_out_srv.reset();
            gd.tex_smaa_out.reset();
            if (CreateDefaultTex(native_device, w, h, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET, gd.tex_smaa_out))
            {
               native_device->CreateRenderTargetView(gd.tex_smaa_out.get(), nullptr, gd.tex_smaa_out_rtv.put());
               native_device->CreateShaderResourceView(gd.tex_smaa_out.get(), nullptr, gd.tex_smaa_out_srv.put());
               gd.smaa_out_w = w;
               gd.smaa_out_h = h;
            }
         }
         if (!gd.tex_smaa_out_rtv || !gd.tex_smaa_out_srv)
            return DrawOrDispatchOverrideType::None;

         // (Re)create the SMAA inputs on resolution change (cached like tex_smaa_out, no full-res fp16 alloc/free
         // every replaced frame).
         if (!gd.tex_input || gd.smaa_temps_w != w || gd.smaa_temps_h != h)
         {
            gd.srv_input.reset();
            gd.uav_input_linear.reset();
            gd.srv_input_linear.reset();
            if (CreateDefaultTex(native_device, w, h, D3D11_BIND_SHADER_RESOURCE, gd.tex_input) &&
                CreateDefaultTex(native_device, w, h, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS, gd.tex_input_linear))
            {
               native_device->CreateShaderResourceView(gd.tex_input.get(), nullptr, gd.srv_input.put());
               native_device->CreateUnorderedAccessView(gd.tex_input_linear.get(), nullptr, gd.uav_input_linear.put());
               native_device->CreateShaderResourceView(gd.tex_input_linear.get(), nullptr, gd.srv_input_linear.put());
               gd.smaa_temps_w = w;
               gd.smaa_temps_h = h;
            }
         }
         if (!gd.srv_input || !gd.uav_input_linear || !gd.srv_input_linear)
            return DrawOrDispatchOverrideType::None;

         // Snapshot the (in-place) scene color out of the swapchain so SMAA can read it (no alloc — reuses tex_input).
         native_device_context->CopyResource(gd.tex_input.get(), color_res.get());

         // Linear-light decode of the snapshot for the neighborhood blend (Luma_BL_SMAALinearize.hlsl).
         {
            DrawStateStack<DrawStateStackType::Compute> linearize_state;
            linearize_state.Cache(native_device_context, device_data.uav_max_count);
            ID3D11ShaderResourceView* lin_srv = gd.srv_input.get();
            ID3D11UnorderedAccessView* lin_uav = gd.uav_input_linear.get();
            native_device_context->CSSetUnorderedAccessViews(0, 1, &lin_uav, nullptr);
            native_device_context->CSSetShaderResources(0, 1, &lin_srv);
            native_device_context->CSSetShader(linearize_cs, nullptr, 0);
            native_device_context->Dispatch((w + 7) / 8, (h + 7) / 8, 1);
            linearize_state.Restore(native_device_context);
         }

         // Predication extract: hardware d24 -> plane-deviation edge-ness in R16F. Core's Compute state stack restores
         // the game's CS state and unbinds our UAV before DrawSMAA reads it as an SRV (an SRV of a resource still bound
         // as a UAV reads as null).
         if (pred_ok)
         {
            DrawStateStack<DrawStateStackType::Compute> pred_state;
            pred_state.Cache(native_device_context, device_data.uav_max_count);
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
            pred_state.Restore(native_device_context);
         }

#if DEVELOPMENT
         // Both calibration aids read the mask the extract CS just wrote. The numeric one runs first so it still
         // reports while the debug view is on (that path returns early).
         if (pred_ok)
            LogPredicationStats(native_device, native_device_context, gd);

         if (pred_ok && g_smaa_pred_debug)
         {
            auto* debug_vs = FindShader(device_data.native_vertex_shaders, CompileTimeStringHash("Copy VS"));
            auto* debug_ps = FindShader(device_data.native_pixel_shaders, CompileTimeStringHash("Copy PS"));
            if (debug_vs != nullptr && debug_ps != nullptr)
            {
               // The mask is single-channel, so the core copy lands it in RED - unmistakably a debug view. It
               // REPLACES the antialiased frame, reusing the same temp-then-copy route the real path takes.
               DrawStateStack<DrawStateStackType::FullGraphics> debug_state;
               debug_state.Cache(native_device_context, device_data.uav_max_count);
               DrawCustomPixelShader(native_device_context, device_data.default_depth_stencil_state.get(), device_data.default_blend_state.get(), nullptr,
                  debug_vs, debug_ps, gd.srv_pred.get(), gd.tex_smaa_out_rtv.get(), w, h, false);
               debug_state.Restore(native_device_context);
               native_device_context->CopyResource(color_res.get(), gd.tex_smaa_out.get());
               return DrawOrDispatchOverrideType::Replaced;
            }
         }
#endif

         // --- SMAA (3 passes) into the temp RTV, then copy into the swapchain target. ---
         // Bind metrics at VS+PS b1 (DrawSMAA restores VS/PS/SRVs/RTs but NOT cbuffer slots).
         ComPtr<ID3D11Buffer> vs_cb1_orig, ps_cb1_orig;
         native_device_context->VSGetConstantBuffers(1, 1, vs_cb1_orig.put());
         native_device_context->PSGetConstantBuffers(1, 1, ps_cb1_orig.put());
         ID3D11Buffer* mcb = gd.cb_smaa_metrics.get();
         native_device_context->VSSetConstantBuffers(1, 1, &mcb);
         native_device_context->PSSetConstantBuffers(1, 1, &mcb);

         // Pass depth for predication only when valid (same-size, captured this frame); otherwise null + the
         // pred_scale=1.0 baked into the metrics CB make SMAA run as plain ULTRA instead of degraded predication.
         DrawSMAA(native_device, native_device_context, device_data,
            gd.tex_smaa_out_rtv.get(), gd.srv_input_linear.get(), gd.srv_input.get(),
            pred_ok ? gd.srv_pred.get() : nullptr /*predication (plane-deviation edge-ness)*/);

         // --- Optional RCAS sharpen on the SMAA output, then copy into the swapchain target. ---
         // SMAA output -> RCAS -> tex_rcas_out -> color_res. If sharpening is off or anything isn't ready, copy the
         // SMAA output straight through (never leave the swapchain unwritten on a Replaced dispatch).
         auto* sharpen_vs = FindShader(device_data.native_vertex_shaders, CompileTimeStringHash("Copy VS"));
         auto* sharpen_ps = FindShader(device_data.native_pixel_shaders, CompileTimeStringHash("BL Sharpen PS"));
         bool do_sharpen = g_rcas_sharpness > 0.f && sharpen_vs != nullptr && sharpen_ps != nullptr;
         if (do_sharpen)
         {
            // (Re)create the RCAS CB on resolution/sharpness change.
            if (!gd.cb_sharpen || gd.sharpen_w != w || gd.sharpen_h != h || gd.sharpen_amount != g_rcas_sharpness)
            {
               const float sp[4] = {(float)w, (float)h, g_rcas_sharpness, 0.f};
               if (CreateImmutableCB(native_device, sp, sizeof(sp), gd.cb_sharpen))
               {
                  gd.sharpen_w = w;
                  gd.sharpen_h = h;
                  gd.sharpen_amount = g_rcas_sharpness;
               }
            }
            // (Re)create the RCAS output temp (fp16, RTV+SRV-capable) on resolution change.
            if (!gd.tex_rcas_out || gd.rcas_out_w != w || gd.rcas_out_h != h)
            {
               gd.tex_rcas_out_rtv.reset();
               gd.tex_rcas_out.reset();
               if (CreateDefaultTex(native_device, w, h, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET, gd.tex_rcas_out))
               {
                  native_device->CreateRenderTargetView(gd.tex_rcas_out.get(), nullptr, gd.tex_rcas_out_rtv.put());
                  gd.rcas_out_w = w;
                  gd.rcas_out_h = h;
               }
            }
            if (!gd.cb_sharpen || !gd.tex_rcas_out_rtv)
               do_sharpen = false;
         }

         if (do_sharpen)
         {
            // DrawCustomPixelShader does NOT restore state -> wrap in core's DrawStateStack<FullGraphics>.
            DrawStateStack<DrawStateStackType::FullGraphics> sharpen_state;
            sharpen_state.Cache(native_device_context, device_data.uav_max_count);

            ID3D11Buffer* scb = gd.cb_sharpen.get();
            native_device_context->PSSetConstantBuffers(0, 1, &scb);
            DrawCustomPixelShader(native_device_context, device_data.default_depth_stencil_state.get(), device_data.default_blend_state.get(), nullptr,
               sharpen_vs, sharpen_ps, gd.tex_smaa_out_srv.get(), gd.tex_rcas_out_rtv.get(), w, h, false);

            sharpen_state.Restore(native_device_context);

            native_device_context->CopyResource(color_res.get(), gd.tex_rcas_out.get());
         }
         else
         {
            native_device_context->CopyResource(color_res.get(), gd.tex_smaa_out.get());
         }

         ID3D11Buffer* vcb = vs_cb1_orig.get();
         ID3D11Buffer* pcb = ps_cb1_orig.get();
         native_device_context->VSSetConstantBuffers(1, 1, &vcb);
         native_device_context->PSSetConstantBuffers(1, 1, &pcb);

         return DrawOrDispatchOverrideType::Replaced; // cancel the FXAA resolve dispatch
      }
#endif // ENABLE_SMAA

      return DrawOrDispatchOverrideType::None;
   }

   void OnPresent(ID3D11Device* native_device, DeviceData& device_data) override
   {
      auto& gd = GetGameDeviceData(device_data);

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

      // Predication inputs are captured per-frame at the cel pass; drop them every present so a frame without that
      // pass (menu/transition/reorder) uses NO predication, not last frame's (possibly wrong-size) depth.
      gd.srv_depth.reset();
      gd.cb_game_offsets.reset();

      // XeGTAO inputs are captured per-frame at the deinterleave/coarse passes; disarm the takeover + drop the
      // captured SRVs every present so a frame without the AO chain (AO off in-game / transition) can't apply a
      // stale AO buffer.
      gd.srv_gtao_depth.reset();
      gd.srv_gtao_normals.reset();

      device_data.has_drawn_main_post_processing = true;
   }

   void LoadConfigs() override
   {
      reshade::get_config_value(nullptr, NAME, "SMAAEnable", g_smaa_enable);
      reshade::get_config_value(nullptr, NAME, "RCASSharpness", g_rcas_sharpness);
      // Predication has no shipping UI (see DrawImGuiSettings), but stays overridable from the ini.
      reshade::get_config_value(nullptr, NAME, "SMAAPredication", g_smaa_predication);
      reshade::get_config_value(nullptr, NAME, "SMAAPredicationTolerance", g_smaa_pred_tolerance);
      reshade::get_config_value(nullptr, NAME, "XeGTAOEnable", g_gtao_enable);
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
      if (ImGui::Checkbox("SMAA Enable", &g_smaa_enable))
         reshade::set_config_value(nullptr, NAME, "SMAAEnable", g_smaa_enable);
      if (ImGui::IsItemHovered())
         ImGui::SetTooltip("Replaces the game's FXAA with SMAA (requires AA enabled in the game's video settings).");
      ImGui::BeginDisabled(!g_smaa_enable);
      ImGui::SliderFloat("RCAS Sharpness", &g_rcas_sharpness, 0.f, 1.f); // updates live; persist on release
      if (ImGui::IsItemDeactivatedAfterEdit())
         reshade::set_config_value(nullptr, NAME, "RCASSharpness", g_rcas_sharpness);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("Sharpening applied on top of SMAA (0 = off).");
#if DEVELOPMENT
      // Predication is not a preference: it only relaxes the edge threshold back to base ULTRA on geometry and
      // never below, so off is strictly worse. Kept as a bisect switch for devs, shipped on and out of sight.
      if (ImGui::Checkbox("SMAA Predication", &g_smaa_predication))
         reshade::set_config_value(nullptr, NAME, "SMAAPredication", g_smaa_predication);
      if (ImGui::IsItemHovered())
         ImGui::SetTooltip("Finds edges by geometry (scene depth) instead of by brightness alone.\nKeeps texture noise from being antialiased while still catching real silhouettes.");
      if (ImGui::SliderFloat("SMAA Predication Tolerance", &g_smaa_pred_tolerance, 0.002f, 0.2f, "%.3f", ImGuiSliderFlags_Logarithmic))
         reshade::set_config_value(nullptr, NAME, "SMAAPredicationTolerance", g_smaa_pred_tolerance);
      if (ImGui::IsItemHovered())
         ImGui::SetTooltip("How far a surface may deviate from its local plane before it counts as an edge,\nas a fraction of view depth. Lower = more edges. This is the calibration lever,\nnot the SMAA threshold. Logarithmic: the parameter is relative.");
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

      if (ImGui::SliderFloat("Exposure", &gs.Exposure, 0.f, 2.f))
      {
         reshade::set_config_value(nullptr, NAME, "Exposure", gs.Exposure);
         device_data.cb_luma_global_settings_dirty = true;
      }
      if (ImGui::IsItemHovered())
         ImGui::SetTooltip("Overall image brightness (1 = vanilla).");
      if (DrawResetButton(gs.Exposure, default_luma_global_game_settings.Exposure, "Exposure"))
         device_data.cb_luma_global_settings_dirty = true;

      if (ImGui::SliderFloat("Contrast", &gs.Contrast, 0.f, 2.f))
      {
         reshade::set_config_value(nullptr, NAME, "Contrast", gs.Contrast);
         device_data.cb_luma_global_settings_dirty = true;
      }
      if (ImGui::IsItemHovered())
         ImGui::SetTooltip("Overall image contrast, HDR only (1 = vanilla).");
      if (DrawResetButton(gs.Contrast, default_luma_global_game_settings.Contrast, "Contrast"))
         device_data.cb_luma_global_settings_dirty = true;

      if (ImGui::SliderFloat("Saturation", &gs.Saturation, 0.f, 2.f))
      {
         reshade::set_config_value(nullptr, NAME, "Saturation", gs.Saturation);
         device_data.cb_luma_global_settings_dirty = true;
      }
      if (ImGui::IsItemHovered())
         ImGui::SetTooltip("Color saturation, HDR only (1 = vanilla).");
      if (DrawResetButton(gs.Saturation, default_luma_global_game_settings.Saturation, "Saturation"))
         device_data.cb_luma_global_settings_dirty = true;

      if (ImGui::SliderFloat("Highlights Desaturation", &gs.HighlightDechroma, 0.f, 1.f))
      {
         reshade::set_config_value(nullptr, NAME, "HighlightDechroma", gs.HighlightDechroma);
         device_data.cb_luma_global_settings_dirty = true;
      }
      if (ImGui::IsItemHovered())
         ImGui::SetTooltip("How far the brightest sources fade to neutral white, HDR only (0 = keep color at any brightness).");
      if (DrawResetButton(gs.HighlightDechroma, default_luma_global_game_settings.HighlightDechroma, "HighlightDechroma"))
         device_data.cb_luma_global_settings_dirty = true;

      ImGui::SeparatorText("Bloom");
      if (ImGui::SliderFloat("Bloom Intensity", &gs.BloomIntensity, 0.f, 2.f))
      {
         reshade::set_config_value(nullptr, NAME, "BloomIntensity", gs.BloomIntensity);
         device_data.cb_luma_global_settings_dirty = true;
      }
      if (ImGui::IsItemHovered())
         ImGui::SetTooltip("Bloom strength (1 = vanilla, 0 = none).");
      if (DrawResetButton(gs.BloomIntensity, default_luma_global_game_settings.BloomIntensity, "BloomIntensity"))
         device_data.cb_luma_global_settings_dirty = true;

      // --- Ambient Occlusion: XeGTAO replaces the game's native HBAO+ ---
      ImGui::SeparatorText("Ambient Occlusion");
      if (ImGui::Checkbox("XeGTAO Enable", &g_gtao_enable))
         reshade::set_config_value(nullptr, NAME, "XeGTAOEnable", g_gtao_enable);
      if (ImGui::IsItemHovered())
         ImGui::SetTooltip("Replaces the game's HBAO+ with XeGTAO (cleaner, more accurate ambient occlusion).");
#if DEVELOPMENT || TEST
      // DEVELOPMENT/TEST calibration only (drive cb_gtao, recreated on change at the deinterleave hook). Not persisted.
      ImGui::BeginDisabled(!g_gtao_enable);
      ImGui::SliderFloat("GTAO Final Value Power", &g_gtao_final_value_power, 0.3f, 4.5f);                 // midtone-shadow contrast dial
      ImGui::SliderFloat("GTAO Depth Scale", &g_gtao_depth_scale, 1.f, 200.f);                             // UE3 units -> ~meters; the anti-over-occlusion dial
      ImGui::SliderFloat("GTAO Radius Override", &g_gtao_radius_override, 0.f, 5.f);                       // 0 = use EFFECT_RADIUS
#if DEVELOPMENT                                                                                            // shader DebugViewRT blocks are #if DEVELOPMENT — don't draw a dead combo in TEST
      ImGui::Combo("GTAO Debug View", &g_gtao_debug_view, "Off\0Depth gradient\0Normals\0AO x8\0Edges\0"); // diagnostics through the AO apply
#endif
      ImGui::EndDisabled();
#endif

      // --- Post-effect scales + output toggle ---
      ImGui::SeparatorText("Effects");

      if (ImGui::SliderFloat("Lens Flare Intensity", &gs.FlareOut, 0.f, 1.f))
      {
         reshade::set_config_value(nullptr, NAME, "FlareOut", gs.FlareOut);
         device_data.cb_luma_global_settings_dirty = true;
      }
      if (ImGui::IsItemHovered())
         ImGui::SetTooltip("Lens-flare / glare strength (1 = vanilla, 0 = off).");
      if (DrawResetButton(gs.FlareOut, default_luma_global_game_settings.FlareOut, "FlareOut"))
         device_data.cb_luma_global_settings_dirty = true;

      bool video_auto_hdr = gs.VideoAutoHDREnable > 0.5f;
      if (ImGui::Checkbox("Video AutoHDR", &video_auto_hdr))
      {
         gs.VideoAutoHDREnable = video_auto_hdr ? 1.f : 0.f;
         reshade::set_config_value(nullptr, NAME, "VideoAutoHDREnable", gs.VideoAutoHDREnable);
         device_data.cb_luma_global_settings_dirty = true;
      }
      if (ImGui::IsItemHovered())
         ImGui::SetTooltip("Adds HDR highlights to pre-rendered videos (HDR only).");

      ImGui::BeginDisabled(!video_auto_hdr);
      if (ImGui::SliderFloat("Video HDR Boost", &gs.VideoAutoHDRBoost, 0.f, 1.f))
         device_data.cb_luma_global_settings_dirty = true;
      if (ImGui::IsItemDeactivatedAfterEdit())
         reshade::set_config_value(nullptr, NAME, "VideoAutoHDRBoost", gs.VideoAutoHDRBoost);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("Video highlight strength (0 = off).");
      if (DrawResetButton(gs.VideoAutoHDRBoost, default_luma_global_game_settings.VideoAutoHDRBoost, "VideoAutoHDRBoost"))
         device_data.cb_luma_global_settings_dirty = true;
      ImGui::EndDisabled();

      bool dithering = gs.Dithering > 0.5f;
      if (ImGui::Checkbox("Dithering", &dithering))
      {
         gs.Dithering = dithering ? 1.f : 0.f;
         reshade::set_config_value(nullptr, NAME, "Dithering", gs.Dithering);
         device_data.cb_luma_global_settings_dirty = true;
      }
      if (ImGui::IsItemHovered())
         ImGui::SetTooltip("Reduces gradient banding.");

      ImGui::SeparatorText("UI");
      if (ImGui::Checkbox("Hide Gameplay UI", &g_hide_ui))
         reshade::set_config_value(nullptr, NAME, "HideUI", g_hide_ui);
      if (ImGui::IsItemHovered())
         ImGui::SetTooltip("Disables the in-game UI.");

      ImGui::SeparatorText("Fixes");
      if (ImGui::Checkbox("Fix Movie Memory Leak", &g_fix_movie_leak))
         reshade::set_config_value(nullptr, NAME, "FixMovieLeak", g_fix_movie_leak);
      if (ImGui::IsItemHovered())
         ImGui::SetTooltip("Frees the loading/cutscene movie memory the game leaks on each transition (fixes the RAM-growth crash). Movies still play.");
      if (g_fix_movie_leak && !BLMovieLeakFix::g_have_movie.load() && BLMovieLeakFix::g_frame >= BLMovieLeakFix::BUILD_CHECK_FRAME)
         ImGui::TextColored(ImVec4(1.f, 0.6f, 0.f, 1.f), "Inactive: no movie buffers detected (game build may differ).");
#if DEVELOPMENT || TEST
      ImGui::Text("freed: %u textures, %.1f MB", BLMovieLeakFix::g_freed_tex, BLMovieLeakFix::g_freed_bytes / 1048576.0);
#endif
   }

   void PrintImGuiAbout() override
   {
      ImGui::PushTextWrapPos(0.f);
      ImGui::Text(
         "Luma for \"Borderlands GOTY Enhanced\" is developed by DristoforColumb and is open source and free.\n"
         "It adds HDR and replaces the game's FXAA with SMAA and its HBAO+ with XeGTAO, plus 16x anisotropic filtering and a fix for the game's movie memory leak.\n"
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
                  "\nDICE (HDR tonemapper)"
                  "\nMacLeod-Boynton hue emulation (RenoDX)"
                  "\nSMAA (Iryoku)"
                  "\nXeGTAO (Intel)"
                  "\nAMD FidelityFX (RCAS)");
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

      // Native HDR: swapchain -> scRGB fp16; core Display Composition does the paper-white scale + scRGB encode +
      // gamut map at present. Replaced tonemap PS writes gamma-encoded HDR (1.0 = paper white) into the (now fp16) post chain.
      swapchain_format_upgrade_type = TextureFormatUpgradesType::AllowedEnabled;
      swapchain_upgrade_type = SwapchainUpgradeType::scRGB; // r8g8b8a8_unorm backbuffer -> r16g16b16a16_float
      texture_format_upgrades_type = TextureFormatUpgradesType::AllowedEnabled;
      // Safety minimum: the remaster already renders its post chain in fp16. r10g10b10a2 covers any 10-bit target at
      // swapchain size (the backbuffer itself is r8g8b8a8); r11g11b10_float includes the HBAO+ view normals XeGTAO
      // reads. r8/b8 formats are left alone deliberately - nothing downstream needs them, and _srgb -> fp16 risks a
      // sampling shift.
      texture_upgrade_formats = {
         reshade::api::format::r10g10b10a2_unorm,
         reshade::api::format::r10g10b10a2_typeless,
         reshade::api::format::r11g11b10_float,
      };
      texture_format_upgrades_2d_size_filters = 0 | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainResolution | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainAspectRatio;
      force_disable_display_composition = false; // core composition does the scRGB encode + paper white

      // AF16x: mode 4 upgrades the game's AF samplers to MaxAnisotropy=16 (clarity on oblique surfaces).
      // LOD bias offset stays 0 (no TAA in this game; a negative bias would shimmer).
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
