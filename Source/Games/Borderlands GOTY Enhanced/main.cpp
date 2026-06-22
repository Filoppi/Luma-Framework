// Borderlands GOTY Enhanced — Luma HDR + SMAA mod (Unreal Engine 3.5, D3D11).
// - HDR: swapchain -> scRGB fp16; replaced UE3 final-color PS (0xB030BAA6 / 0xFE88487E) recovers the clipped
//   highlights (UpgradeToneMap) + DICE display map. Core Display Composition does the paper-white scale + encode.
//   One HDR mod owns the swapchain -> RenoDX must be removed from the game folder.
// - AA: compute FXAA (3.11 work-queue) -> SMAA (ULTRA + color edge + depth predication) + optional RCAS. Runs on
//   the HDR-linear scene: sRGB-encoded copy for edge detect, linear copy for blend, CopyResource into swapchain.

#define GAME_BORDERLANDS_GOTY 1

// Don't pop the DEVELOPMENT auto-debugger MessageBox on DLL attach: under a borderless/fullscreen game it's
// invisible and blocks the loader (ReShade times out the addon load -> error 1114).
#define DISABLE_AUTO_DEBUGGER 1

#define ENABLE_NGX 0
#define ENABLE_FIDELITY_SK 0
#define GEOMETRY_SHADER_SUPPORT 0
#define ENABLE_SMAA 1 // auto-registers the 6 "SMAA ..." shaders from Luma_SMAA_impl (see core.hpp)

#include "..\..\Core\core.hpp"
#include <shellapi.h> // ShellExecuteA for About links (system() hangs the render thread in exclusive fullscreen)

// FXAA is a compute work-queue implementation (FXAA 3.11 CS), verified via devkit disassembly:
//   0x81CDE53D = pass 1 edge-detect (builds WorkQueue into scratch buffers; leaves color untouched — left running).
//   0x08891303 = pass 2 resolve: WorkQueue + Luma + InColor(t2) -> Color(u0, swapchain in-place). Replaced with SMAA.
static constexpr uint32_t kFXAAResolveHash = 0x08891303; // FXAA resolve CS — replaced with SMAA
static constexpr uint32_t kCelShadingHash = 0x08DC66D1;  // cel-shading edge PS — binds scene depth at t0 (predication source)

// User-facing settings (persisted via ReShade config; loaded in LoadConfigs, saved on UI change).
static bool g_smaa_enable = true;
static float g_rcas_sharpness = 0.f; // RCAS sharpen on SMAA output (0 = off). Conservative — ink outlines already AA'd; higher haloes.
static bool g_hide_ui = false;        // hide the game's HUD (skips swapchain-targeting UI draws) — for clean screenshots

// Loading-movie memory-leak fix (toggle "Fix Movie Memory Leak" under Fixes; default ON).
// The game's Bink movies create D3D11 YUV decode buffers and never release them -> linear RAM
// growth -> OOM. The leak is the GAME, not Luma. We drop the game's leaked COM refs on OLD movie
// generations (orphaned: a movie's buffers are sampled only during its own playback). Tagged by
// creation call-stack RVAs in BorderlandsGOTY.exe (frozen remaster; non-matching build tags nothing
// = safe no-op). Movies keep playing. Diagnosis/validation: Source/Tools/BL Leak Tracker.
static bool g_fix_movie_leak = true; // default ON; persisted as "FixMovieLeak"

namespace BLMovieLeakFix
{
   // Build-specific RVAs (frozen remaster). A non-matching build shifts these -> nothing tags ->
   // silent no-op; BUILD_CHECK_FRAME drives a one-shot telemetry warning for that case.
   constexpr uintptr_t RVA_CREATE_WRAPPER = 0xBFF27;       // ret addr after the RHI CreateTexture call
   constexpr uintptr_t RVA_STREAM_LO = 0x58A000;            // streaming/movie fn span (create call sites)
   constexpr uintptr_t RVA_STREAM_HI = 0x58C000;
   constexpr uint32_t  BUILD_CHECK_FRAME = 18000;          // ~5 min; movies tag well before this if build matches
   constexpr uint32_t  NEW_GEN_GAP_FRAMES   = 90;           // frame gap that separates two movies into "generations"
   constexpr int       MAX_FRAMES = 32, SKIP_FRAMES = 1;
   constexpr uint64_t  STACKWALK_MIN_BYTES  = 2ull * 1024 * 1024; // only walk the stack for big resources (cheap)
   constexpr int       RELEASE_GEN_LAG      = 2;            // release only gen <= cur_gen-2 (keep current + previous)
   constexpr uint32_t  RELEASE_IDLE_FRAMES  = 600;          // and only after this many frames since creation (~10s)
   constexpr uint32_t  RELEASE_FLUSH_PERIOD = 120;          // flush at most every N present frames

   struct MovieTex { uint64_t bytes; int gen; uint32_t created_frame; };

   static std::mutex g_mtx;
   static std::unordered_map<uint64_t, MovieTex> g_movie;    // resource handle -> info
   static std::unordered_map<uint64_t, uint64_t> g_view2res; // view handle -> resource handle (tagged textures only)
   static uintptr_t g_exe_base = 0;
   static int       g_cur_gen = 0;
   static uint32_t  g_last_movie_frame = 0;
   static bool      g_first_movie = true;
   // Coarse cross-thread gates (present thread writes g_frame; workers read). atomic = no data race;
   // real map sync is g_mtx.
   static std::atomic<bool>     g_have_movie{ false };
   static std::atomic<uint32_t> g_frame{ 0 };
   static uint64_t  g_freed_bytes = 0;
   static uint32_t  g_freed_tex = 0;
   static bool      g_build_checked = false; // one-shot build-mismatch telemetry guard (present thread only)

   inline uint64_t EstimateBytes(const reshade::api::resource_desc& d)
   {
      using namespace reshade::api;
      if (d.type == resource_type::buffer) return d.buffer.size;
      const uint32_t w = d.texture.width ? d.texture.width : 1;
      const uint32_t h = d.texture.height ? d.texture.height : 1;
      const uint32_t layers = d.texture.depth_or_layers ? d.texture.depth_or_layers : 1;
      const uint32_t levels = d.texture.levels ? d.texture.levels : 1;
      const uint32_t slice = format_slice_pitch(d.texture.format, format_row_pitch(d.texture.format, w), h);
      uint64_t s = (uint64_t)slice * layers, total = 0;
      for (uint32_t l = 0; l < levels; ++l) { total += s; s = s > 4 ? s / 4 : 1; }
      return total;
   }

   inline bool StackIsMovie(void* const* frames, int n)
   {
      if (!g_exe_base) return false;
      bool has_create = false, has_stream = false;
      for (int i = 0; i < n; ++i)
      {
         const uintptr_t a = reinterpret_cast<uintptr_t>(frames[i]);
         if (a < g_exe_base) continue;
         const uintptr_t rva = a - g_exe_base;
         if (rva == RVA_CREATE_WRAPPER) has_create = true;
         else if (rva >= RVA_STREAM_LO && rva < RVA_STREAM_HI) has_stream = true;
      }
      return has_create && has_stream;
   }

   // Tag movie YUV decode buffers at creation (stack walk only for >= 2MB buffers).
   void OnInitResource(reshade::api::device*, const reshade::api::resource_desc& desc, const reshade::api::subresource_data*, reshade::api::resource_usage, reshade::api::resource handle)
   {
      // Decode targets are BUFFERS (measured); gating on type excludes the textures that share the
      // streaming-fn span -> no mistag/UAF, and skips the stack walk for every texture.
      if (desc.type != reshade::api::resource_type::buffer) return;
      const uint64_t bytes = EstimateBytes(desc);
      if (bytes < STACKWALK_MIN_BYTES) return;
      void* frames[MAX_FRAMES];
      const int n = RtlCaptureStackBackTrace(SKIP_FRAMES, MAX_FRAMES, frames, nullptr);
      if (n <= 0 || !StackIsMovie(frames, n)) return;
      const std::lock_guard<std::mutex> lk(g_mtx);
      const uint32_t f = g_frame;
      if (g_first_movie || (f - g_last_movie_frame) > NEW_GEN_GAP_FRAMES) { g_cur_gen += 1; g_first_movie = false; }
      g_last_movie_frame = f;
      g_movie[handle.handle] = MovieTex{ bytes, g_cur_gen, f };
      g_have_movie = true;
   }

   void OnDestroyResource(reshade::api::device*, reshade::api::resource handle)
   {
      if (!g_have_movie) return;
      const std::lock_guard<std::mutex> lk(g_mtx);
      g_movie.erase(handle.handle);
   }

   void OnInitResourceView(reshade::api::device*, reshade::api::resource res, reshade::api::resource_usage, const reshade::api::resource_view_desc&, reshade::api::resource_view view)
   {
      if (!g_have_movie || !view.handle) return;
      const std::lock_guard<std::mutex> lk(g_mtx);
      if (g_movie.find(res.handle) != g_movie.end()) g_view2res[view.handle] = res.handle;
   }

   void OnDestroyResourceView(reshade::api::device*, reshade::api::resource_view view)
   {
      if (!g_have_movie) return;
      const std::lock_guard<std::mutex> lk(g_mtx);
      g_view2res.erase(view.handle);
   }

   // Release the game's leaked COM refs on old orphaned generations. Re-entrancy-safe: our Release()
   // re-enters OnDestroyResource[View] on this thread -> COLLECT+UNLINK under lock, then RELEASE unlocked.
   void Flush()
   {
      struct Plan { uintptr_t res; std::vector<uintptr_t> views; uint64_t bytes; };
      std::vector<Plan> plans;
      {
         const std::lock_guard<std::mutex> lk(g_mtx);
         for (const auto& kv : g_movie) // Phase A: pick victims (keep current + previous gen, must be idle)
         {
            const MovieTex& m = kv.second;
            if (m.gen > g_cur_gen - RELEASE_GEN_LAG) continue;
            if ((g_frame - m.created_frame) < RELEASE_IDLE_FRAMES) continue;
            plans.push_back({ kv.first, {}, m.bytes });
         }
         if (plans.empty()) return;
         std::unordered_map<uintptr_t, size_t> idx;
         for (size_t i = 0; i < plans.size(); ++i) idx[plans[i].res] = i;
         for (const auto& vk : g_view2res) { auto it = idx.find(vk.second); if (it != idx.end()) plans[it->second].views.push_back(vk.first); }
         for (const auto& p : plans) // Phase B: UNLINK before any Release (re-entrant callbacks then find nothing)
         {
            for (uintptr_t v : p.views) g_view2res.erase(v);
            g_movie.erase(p.res);
         }
      }
      uint32_t ft = 0; uint64_t fb = 0; // Phase C: RELEASE with the lock released
      uint32_t partial = 0;
      for (const Plan& p : plans)
      {
         for (uintptr_t v : p.views) reinterpret_cast<IUnknown*>(v)->Release();
         // rc==0 => actually freed (count it); rc>0 => game holds extra refs, not reclaimed. (rc is a
         // by-value ULONG, never a deref of a freed object.)
         const ULONG rc = reinterpret_cast<IUnknown*>(p.res)->Release();
         if (rc == 0) { ft++; fb += p.bytes; }
         else partial++;
      }
      g_freed_tex += ft; g_freed_bytes += fb;
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
}

struct BorderlandsGotyGameDeviceData final : public GameDeviceData
{
#if DEVELOPMENT || TEST
   bool logged_no_fp16 = false; // warned once: swapchain not fp16 (HDR upgrade absent) -> SMAA skipped
#endif

   // Scene depth (D24S8, viewed r24_unorm_x8_uint) captured from the cel-shading pass, fed to SMAA predication.
   ComPtr<ID3D11ShaderResourceView> srv_depth;

   // SMAA metrics CB (b1) = (1/w, 1/h, w, h) + (predication scale,0,0,0), recreated on resolution or predication-state change.
   ComPtr<ID3D11Buffer> cb_smaa_metrics;
   uint32_t smaa_metrics_w = 0, smaa_metrics_h = 0;
   float smaa_metrics_pred_scale = -1.f; // 2.0 = valid depth (predication active), 1.0 = no/mismatched depth (plain ULTRA threshold)

   // Tracks the size DrawSMAA built its core-managed intermediates at (to rebuild on resolution change).
   uint32_t smaa_core_w = 0, smaa_core_h = 0;

   // SMAA scratch (fp16), recreated on resolution change. tex_input = scene-color snapshot (CopyResource'd each
   // frame); tex_lin/tex_gam = linear + sRGB copies written by the linearize CS each frame.
   ComPtr<ID3D11Texture2D> tex_input, tex_lin, tex_gam;
   ComPtr<ID3D11ShaderResourceView> srv_input, srv_lin, srv_gam;
   ComPtr<ID3D11UnorderedAccessView> uav_lin, uav_gam;
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
};

class BorderlandsGoty final : public Game
{
   static BorderlandsGotyGameDeviceData& GetGameDeviceData(DeviceData& device_data)
   {
      return *static_cast<BorderlandsGotyGameDeviceData*>(device_data.game);
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

   // Create a DEFAULT-usage RGBA16F 2D texture (1 mip, 1 sample) of w×h with the given bind flags. Resets `out`.
   static bool CreateDefaultRGBA16FTex(ID3D11Device* device, uint32_t w, uint32_t h, UINT bind_flags, ComPtr<ID3D11Texture2D>& out)
   {
      out.reset();
      D3D11_TEXTURE2D_DESC td = {};
      td.Width = w;
      td.Height = h;
      td.MipLevels = 1;
      td.ArraySize = 1;
      td.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
      td.SampleDesc.Count = 1;
      td.Usage = D3D11_USAGE_DEFAULT;
      td.BindFlags = bind_flags;
      return SUCCEEDED(device->CreateTexture2D(&td, nullptr, out.put()));
   }

public:
   void OnInit(bool async) override
   {
      // Game-specific HDR toggles consumed by the replaced tonemap shaders (Luma_BL_Tonemap.hlsl).
      std::vector<ShaderDefineData> game_shader_defines_data = {
         {"TONEMAP_TYPE", '1', true, false, "0 - SDR: Vanilla (clamped reference)\n1 - HDR: recover highlights + DICE display map"},
         {"TONEMAP_IN_WIDER_GAMUT", '1', true, false, "Run the display map in a BT.2020 working space (gamut-correct saturated highlights). Not a display-gamut expansion.", 1},
         {"ENABLE_HUE_RESTORATION", '1', true, false, "Lock the HDR result's hue/chroma to the game's SDR grade (preserve artistic intent).", 1},
      };
      shader_defines_data.append_range(game_shader_defines_data);

      // Post-process buffers in GAMMA space (UE3 engine, gamma-2.2 SDR): the gamma-SDR HUD blends on top in
      // gamma to look vanilla (linear space washes it out). Tonemap pre-scales by GamePaperWhite/UIPaperWhite
      // (UI_DRAW_TYPE 2); the core composition decodes gamma + applies paper white + scRGB encode.
      GetShaderDefineData(POST_PROCESS_SPACE_TYPE_HASH).SetDefaultValue('0');
      GetShaderDefineData(EARLY_DISPLAY_ENCODING_HASH).SetDefaultValue('0');
      GetShaderDefineData(VANILLA_ENCODING_TYPE_HASH).SetDefaultValue('1');    // game shipped gamma-2.2 SDR
      GetShaderDefineData(GAMMA_CORRECTION_TYPE_HASH).SetDefaultValue('1');
      GetShaderDefineData(GAMUT_MAPPING_TYPE_HASH).SetDefaultValue('1');       // gamut-map wild colors in composition
      GetShaderDefineData(UI_DRAW_TYPE_HASH).SetDefaultValue('2');             // HUD gets its own UIPaperWhite + gamma blend

      // SMAA linearize helper (the 6 SMAA passes are auto-registered by core when ENABLE_SMAA).
      native_shaders_definitions.emplace(CompileTimeStringHash("SMAA Linear To sRGB CS"),
         ShaderDefinition("Luma_SMAA_LinearTosRGB_CS", reshade::api::pipeline_subobject_type::compute_shader));

      // RCAS sharpen PS (drawn via core "Copy VS" + DrawCustomPixelShader after SMAA).
      native_shaders_definitions.emplace(CompileTimeStringHash("BL Sharpen PS"),
         ShaderDefinition{"Luma_BL_Sharpen", reshade::api::pipeline_subobject_type::pixel_shader, nullptr, "sharpen_ps"});

      // Game uses CB slots b0-b3, so b12/b13 are free for Luma.
      // luma_data is used by the Display Composition; luma_ui stays off (UI drawn by the game).
      luma_settings_cbuffer_index = 13;
      luma_data_cbuffer_index = 12;
      luma_ui_cbuffer_index = -1;

      // Manual Scene + UI Paper White sliders instead of the OS HDR reference level. Core gates the separate
      // "UI Paper White" slider on UI_DRAW_TYPE >= 1 && !use_os_reference_white_level. Default 203 nits (BT.2408).
      use_os_reference_white_level = false;

      // User grade controls (read in Luma_BL_Tonemap.hlsl via LumaSettings.GameSettings). All vanilla by default.
      default_luma_global_game_settings.Exposure = 1.f;          // multiplier (1x)
      default_luma_global_game_settings.Saturation = 1.f;
      default_luma_global_game_settings.HighlightDechroma = 0.f; // off by default; only the mandatory DICE/gamut desaturation applies. Slider = optional taste.
      default_luma_global_game_settings.BloomIntensity = 1.f;
      default_luma_global_game_settings.Contrast = 1.f;
      default_luma_global_game_settings.Dithering = 1.f;         // subtle anti-banding on by default
      default_luma_global_game_settings.FlareOut = 1.f;          // additive lens-flare/glare scale (1 = vanilla)
      cb_luma_global_settings.GameSettings = default_luma_global_game_settings;
   }

   void OnCreateDevice(ID3D11Device* native_device, DeviceData& device_data) override
   {
      device_data.game = new BorderlandsGotyGameDeviceData;
   }

   void OnDestroyDeviceData(DeviceData& device_data) override
   {
      if (device_data.game)
      {
         auto& gd = GetGameDeviceData(device_data);
         gd.srv_depth.reset();
         gd.cb_smaa_metrics.reset();
         gd.srv_input.reset(); gd.tex_input.reset();
         gd.uav_lin.reset(); gd.srv_lin.reset(); gd.tex_lin.reset();
         gd.uav_gam.reset(); gd.srv_gam.reset(); gd.tex_gam.reset();
         gd.tex_smaa_out.reset();
         gd.tex_smaa_out_rtv.reset();
         gd.tex_smaa_out_srv.reset();
         gd.cb_sharpen.reset();
         gd.tex_rcas_out.reset();
         gd.tex_rcas_out_rtv.reset();
      }
      delete device_data.game;
      device_data.game = nullptr;
   }

   DrawOrDispatchOverrideType OnDrawOrDispatch(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, reshade::api::shader_stage stages, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, bool is_custom_pass, bool& updated_cbuffers, std::function<void()>* original_draw_dispatch_func) override
   {
      auto& gd = GetGameDeviceData(device_data);

      const bool is_immediate = native_device_context->GetType() == D3D11_DEVICE_CONTEXT_IMMEDIATE;

      // Hide HUD: cancel the game's UI draws (for clean screenshots). A UI draw = one targeting a swapchain back
      // buffer AFTER the scene post-processing ran — the same detection the core UI handling uses
      // (RTV resource in device_data.back_buffers; see core.hpp). Compute/offscreen draws have no swapchain RTV
      // so they're untouched, and pre-scene menu draws (post not yet done) still show.
      if (g_hide_ui && is_immediate && !is_custom_pass && device_data.has_drawn_main_post_processing)
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
               { const std::shared_lock lock(device_data.mutex); targeting_swapchain = device_data.back_buffers.contains((uint64_t)rtv_res.get()); }
               if (targeting_swapchain)
                  return DrawOrDispatchOverrideType::Replaced; // drop the UI draw
            }
         }
      }

      // Track the scene depth for SMAA predication: the cel-shading edge pass binds the D24S8 depth as PS t0.
      if (is_immediate && original_shader_hashes.Contains(kCelShadingHash, reshade::api::shader_stage::pixel))
      {
         ComPtr<ID3D11ShaderResourceView> srv_d;
         native_device_context->PSGetShaderResources(0, 1, srv_d.put());
         if (srv_d)
         {
            gd.srv_depth = srv_d;
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

         uint4 cinfo{}; DXGI_FORMAT cfmt = DXGI_FORMAT_UNKNOWN;
         GetResourceInfo(color_res.get(), cinfo, cfmt);
         uint32_t w = cinfo.x, h = cinfo.y, color_fmt = (uint32_t)cfmt;
         if (w == 0 || h == 0)
            return DrawOrDispatchOverrideType::None;
         // fp16 guard: the SMAA path forces R16G16B16A16_FLOAT temps; CopyResource silently no-ops on a format
         // mismatch, so a non-fp16 swapchain (HDR upgrade absent) would feed SMAA uninitialized memory and copy
         // garbage back. Bail to the game's native FXAA instead of shipping a corrupt frame.
         if (color_fmt != (uint32_t)DXGI_FORMAT_R16G16B16A16_FLOAT)
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
         bool depth_ok = false;
         if (gd.srv_depth)
         {
            uint4 dinfo{}; DXGI_FORMAT dfmt = DXGI_FORMAT_UNKNOWN;
            GetResourceInfo(gd.srv_depth.get(), dinfo, dfmt);
            depth_ok = (dinfo.x == w && dinfo.y == h);
         }
         const float pred_scale = depth_ok ? 2.f : 1.f;

         // Shader-readiness gate (async loader / dev live-reload). If anything is missing, fall through to native FXAA.
         const bool smaa_ready =
            device_data.native_pixel_shaders[CompileTimeStringHash("SMAA Edge Detection PS")].get() != nullptr &&
            device_data.native_pixel_shaders[CompileTimeStringHash("SMAA Blending Weight Calculation PS")].get() != nullptr &&
            device_data.native_pixel_shaders[CompileTimeStringHash("SMAA Neighborhood Blending PS")].get() != nullptr &&
            device_data.native_vertex_shaders[CompileTimeStringHash("SMAA Edge Detection VS")].get() != nullptr &&
            device_data.native_vertex_shaders[CompileTimeStringHash("SMAA Blending Weight Calculation VS")].get() != nullptr &&
            device_data.native_vertex_shaders[CompileTimeStringHash("SMAA Neighborhood Blending VS")].get() != nullptr &&
            device_data.native_compute_shaders[CompileTimeStringHash("SMAA Linear To sRGB CS")].get() != nullptr;
         if (!smaa_ready)
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
            if (CreateDefaultRGBA16FTex(native_device, w, h, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET, gd.tex_smaa_out))
            {
               native_device->CreateRenderTargetView(gd.tex_smaa_out.get(), nullptr, gd.tex_smaa_out_rtv.put());
               native_device->CreateShaderResourceView(gd.tex_smaa_out.get(), nullptr, gd.tex_smaa_out_srv.put());
               gd.smaa_out_w = w;
               gd.smaa_out_h = h;
            }
         }
         if (!gd.tex_smaa_out_rtv || !gd.tex_smaa_out_srv)
            return DrawOrDispatchOverrideType::None;

         // (Re)create the SMAA scratch textures + views on resolution change (cached like tex_smaa_out — avoids
         // ~3x full-res fp16 alloc/free every replaced frame). tex_input = SRV-readable snapshot of the in-place
         // scene color; tex_lin/tex_gam = linear + sRGB copies the linearize CS writes.
         if (!gd.tex_input || gd.smaa_temps_w != w || gd.smaa_temps_h != h)
         {
            gd.srv_input.reset(); gd.tex_input.reset();
            gd.uav_lin.reset(); gd.srv_lin.reset(); gd.tex_lin.reset();
            gd.uav_gam.reset(); gd.srv_gam.reset(); gd.tex_gam.reset();
            if (CreateDefaultRGBA16FTex(native_device, w, h, D3D11_BIND_SHADER_RESOURCE, gd.tex_input) &&
                CreateDefaultRGBA16FTex(native_device, w, h, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS, gd.tex_lin) &&
                CreateDefaultRGBA16FTex(native_device, w, h, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS, gd.tex_gam))
            {
               native_device->CreateShaderResourceView(gd.tex_input.get(), nullptr, gd.srv_input.put());
               native_device->CreateUnorderedAccessView(gd.tex_lin.get(), nullptr, gd.uav_lin.put());
               native_device->CreateUnorderedAccessView(gd.tex_gam.get(), nullptr, gd.uav_gam.put());
               native_device->CreateShaderResourceView(gd.tex_lin.get(), nullptr, gd.srv_lin.put());
               native_device->CreateShaderResourceView(gd.tex_gam.get(), nullptr, gd.srv_gam.put());
               gd.smaa_temps_w = w;
               gd.smaa_temps_h = h;
            }
         }
         if (!gd.srv_input || !gd.uav_lin || !gd.uav_gam || !gd.srv_lin || !gd.srv_gam)
            return DrawOrDispatchOverrideType::None;

         // Snapshot the (in-place) scene color out of the swapchain so SMAA can read it (no alloc — reuses tex_input).
         native_device_context->CopyResource(gd.tex_input.get(), color_res.get());

         // Wrap in core's Compute state stack: restores the game's prior CS shader/UAVs/SRV after our dispatch (core
         // only auto-restores compute state in DEVELOPMENT, so shipping needs this) and unbinds our u0/u1 UAVs before
         // DrawSMAA reads them as SRVs (avoids the output-vs-read hazard).
         {
            DrawStateStack<DrawStateStackType::Compute> linearize_cs_state;
            linearize_cs_state.Cache(native_device_context, device_data.uav_max_count);

            ID3D11ShaderResourceView* cs_srv = gd.srv_input.get();
            ID3D11UnorderedAccessView* cs_uavs[2] = {gd.uav_lin.get(), gd.uav_gam.get()}; // u0 = linear copy, u1 = sRGB
            native_device_context->CSSetShaderResources(0, 1, &cs_srv);
            native_device_context->CSSetUnorderedAccessViews(0, 2, cs_uavs, nullptr);
            native_device_context->CSSetShader(device_data.native_compute_shaders[CompileTimeStringHash("SMAA Linear To sRGB CS")].get(), nullptr, 0);
            native_device_context->Dispatch((w + 7) / 8, (h + 7) / 8, 1);

            linearize_cs_state.Restore(native_device_context);
         }

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
            gd.tex_smaa_out_rtv.get(), gd.srv_lin.get() /*color (linear)*/, gd.srv_gam.get() /*color gamma*/,
            depth_ok ? gd.srv_depth.get() : nullptr /*predication*/);

         // --- Optional RCAS sharpen on the (linear scRGB) SMAA output, then copy into the swapchain target. ---
         // SMAA output -> RCAS -> tex_rcas_out -> color_res. If sharpening is off or anything isn't ready, copy the
         // SMAA output straight through (never leave the swapchain unwritten on a Replaced dispatch).
         const bool sharpen_shaders_ready =
            device_data.native_vertex_shaders[CompileTimeStringHash("Copy VS")].get() != nullptr &&
            device_data.native_pixel_shaders[CompileTimeStringHash("BL Sharpen PS")].get() != nullptr;
         bool do_sharpen = g_rcas_sharpness > 0.f && sharpen_shaders_ready;
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
               if (CreateDefaultRGBA16FTex(native_device, w, h, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET, gd.tex_rcas_out))
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
            auto* sharpen_vs = device_data.native_vertex_shaders[CompileTimeStringHash("Copy VS")].get();
            auto* sharpen_ps = device_data.native_pixel_shaders[CompileTimeStringHash("BL Sharpen PS")].get();
            // DrawCustomPixelShader does NOT restore state → wrap in core's DrawStateStack<FullGraphics>.
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

         device_data.has_drawn_main_post_processing = true;
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

      // Predication depth is captured per-frame at the cel pass; drop it every present so a frame without that pass
      // (menu/transition/reorder) uses NO predication, not last frame's (possibly wrong-size) depth. Null handled at bind.
      gd.srv_depth.reset();

      device_data.has_drawn_main_post_processing = true;
   }

   void LoadConfigs() override
   {
      reshade::get_config_value(nullptr, NAME, "SMAAEnable", g_smaa_enable);
      reshade::get_config_value(nullptr, NAME, "RCASSharpness", g_rcas_sharpness);
      // Grade sliders (cb_luma_global_settings_dirty is already true at init -> uploaded on first frame).
      reshade::get_config_value(nullptr, NAME, "Exposure", cb_luma_global_settings.GameSettings.Exposure);
      reshade::get_config_value(nullptr, NAME, "Saturation", cb_luma_global_settings.GameSettings.Saturation);
      reshade::get_config_value(nullptr, NAME, "HighlightDechroma", cb_luma_global_settings.GameSettings.HighlightDechroma);
      reshade::get_config_value(nullptr, NAME, "BloomIntensity", cb_luma_global_settings.GameSettings.BloomIntensity);
      reshade::get_config_value(nullptr, NAME, "Contrast", cb_luma_global_settings.GameSettings.Contrast);
      reshade::get_config_value(nullptr, NAME, "Dithering", cb_luma_global_settings.GameSettings.Dithering);
      reshade::get_config_value(nullptr, NAME, "FlareOut", cb_luma_global_settings.GameSettings.FlareOut);
      reshade::get_config_value(nullptr, NAME, "HideUI", g_hide_ui);
      reshade::get_config_value(nullptr, NAME, "FixMovieLeak", g_fix_movie_leak);
   }

   void DrawImGuiSettings(DeviceData& device_data) override
   {
      ImGui::SeparatorText("Anti-Aliasing");
      if (ImGui::Checkbox("SMAA Enable", &g_smaa_enable))
         reshade::set_config_value(nullptr, NAME, "SMAAEnable", g_smaa_enable);
      ImGui::BeginDisabled(!g_smaa_enable);
      ImGui::SliderFloat("RCAS Sharpness", &g_rcas_sharpness, 0.f, 1.f); // updates live; persist on release
      if (ImGui::IsItemDeactivatedAfterEdit())
         reshade::set_config_value(nullptr, NAME, "RCASSharpness", g_rcas_sharpness);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("RCAS sharpening applied to the SMAA output (0 = off).");
      ImGui::EndDisabled();

      // --- HDR grade (read in Luma_BL_Tonemap.hlsl via LumaSettings.GameSettings; HDR tonemap path only) ---
      auto& gs = cb_luma_global_settings.GameSettings;
      ImGui::SeparatorText("Grade");

      if (ImGui::SliderFloat("Exposure", &gs.Exposure, 0.f, 2.f))
      {
         reshade::set_config_value(nullptr, NAME, "Exposure", gs.Exposure);
         device_data.cb_luma_global_settings_dirty = true;
      }
      if (ImGui::IsItemHovered())
         ImGui::SetTooltip("Scene-referred exposure multiplier (1 = vanilla).");
      if (DrawResetButton(gs.Exposure, default_luma_global_game_settings.Exposure, "Exposure"))
         device_data.cb_luma_global_settings_dirty = true;

      if (ImGui::SliderFloat("Contrast", &gs.Contrast, 0.f, 2.f))
      {
         reshade::set_config_value(nullptr, NAME, "Contrast", gs.Contrast);
         device_data.cb_luma_global_settings_dirty = true;
      }
      if (ImGui::IsItemHovered())
         ImGui::SetTooltip("Slope contrast around 18% mid-gray (1 = vanilla).");
      if (DrawResetButton(gs.Contrast, default_luma_global_game_settings.Contrast, "Contrast"))
         device_data.cb_luma_global_settings_dirty = true;

      if (ImGui::SliderFloat("Saturation", &gs.Saturation, 0.f, 2.f))
      {
         reshade::set_config_value(nullptr, NAME, "Saturation", gs.Saturation);
         device_data.cb_luma_global_settings_dirty = true;
      }
      if (DrawResetButton(gs.Saturation, default_luma_global_game_settings.Saturation, "Saturation"))
         device_data.cb_luma_global_settings_dirty = true;

      if (ImGui::SliderFloat("Highlights Desaturation", &gs.HighlightDechroma, 0.f, 1.f))
      {
         reshade::set_config_value(nullptr, NAME, "HighlightDechroma", gs.HighlightDechroma);
         device_data.cb_luma_global_settings_dirty = true;
      }
      if (ImGui::IsItemHovered())
         ImGui::SetTooltip("How soon bright sources fade to neutral white (0 = keep color at any brightness).");
      if (DrawResetButton(gs.HighlightDechroma, default_luma_global_game_settings.HighlightDechroma, "HighlightDechroma"))
         device_data.cb_luma_global_settings_dirty = true;

      // --- Post-effect scales + output toggle ---
      ImGui::SeparatorText("Effects");

      if (ImGui::SliderFloat("Bloom Intensity", &gs.BloomIntensity, 0.f, 2.f))
      {
         reshade::set_config_value(nullptr, NAME, "BloomIntensity", gs.BloomIntensity);
         device_data.cb_luma_global_settings_dirty = true;
      }
      if (DrawResetButton(gs.BloomIntensity, default_luma_global_game_settings.BloomIntensity, "BloomIntensity"))
         device_data.cb_luma_global_settings_dirty = true;

      if (ImGui::SliderFloat("Lens Flare Intensity", &gs.FlareOut, 0.f, 1.f))
      {
         reshade::set_config_value(nullptr, NAME, "FlareOut", gs.FlareOut);
         device_data.cb_luma_global_settings_dirty = true;
      }
      if (ImGui::IsItemHovered())
         ImGui::SetTooltip("Additive lens-flare / glare overlay strength (1 = vanilla, 0 = off).");
      if (DrawResetButton(gs.FlareOut, default_luma_global_game_settings.FlareOut, "FlareOut"))
         device_data.cb_luma_global_settings_dirty = true;

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
         ImGui::SetTooltip("Frees the loading/cutscene movie textures the game leaks on each transition (fixes the RAM-growth crash). Movies still play.");
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
         "It adds HDR and replaces the game's FXAA with SMAA (plus 16x anisotropic filtering).\n"
         "Thanks to the Luma team and contributors.\n"
         "Do NOT run another HDR mod (e.g. RenoDX) alongside it.");
      ImGui::PopTextWrapPos();

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
         "\nOklab (hue/chroma restoration)"
         "\nSMAA (Iryoku)"
         "\nAMD FidelityFX (RCAS)"
         , "");
   }
};

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
   if (ul_reason_for_call == DLL_PROCESS_ATTACH)
   {
      const char* project_name = PROJECT_NAME;
      const char* cleared_project_name = (project_name[0] == '_') ? (project_name + 1) : project_name;

      uint32_t mod_version = 2; // bump: AA-only -> native HDR (invalidates cached settings/shaders)
      Globals::SetGlobals(cleared_project_name, "Borderlands GOTY Enhanced Luma HDR + SMAA mod", "", mod_version);

      // Native HDR: swapchain -> scRGB fp16; core Display Composition does the paper-white scale + scRGB encode +
      // gamut map at present. Replaced tonemap PS writes scene-referred HDR-linear into the (now fp16) post chain.
      swapchain_format_upgrade_type = TextureFormatUpgradesType::AllowedEnabled;
      swapchain_upgrade_type = SwapchainUpgradeType::scRGB; // r10g10b10a2 backbuffer -> r16g16b16a16_float
      texture_format_upgrades_type = TextureFormatUpgradesType::AllowedEnabled;
      // Trimmed to a safety minimum (devkit-verified 2026-06-21): the remaster renders its whole post chain in
      // fp16 already, and the only low-precision target (r10g10b10a2 backbuffer) is handled by the upgrade above.
      // The broad r8/b8 set was dead weight (and _srgb->fp16 risks a sampling shift). Keep plausible fp16 intermediates.
      texture_upgrade_formats = {
         reshade::api::format::r10g10b10a2_unorm,
         reshade::api::format::r10g10b10a2_typeless,
         reshade::api::format::r11g11b10_float, // bloom / lens-flare-style intermediates
      };
      texture_format_upgrades_2d_size_filters = 0 | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainResolution | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainAspectRatio;
      force_disable_display_composition = false; // core composition now does the scRGB encode + paper white

      // AF16x: mode 4 upgrades the game's AF samplers to MaxAnisotropy=16 (clarity on oblique surfaces, zero risk).
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
