// The Witcher 2: Assassins of Kings Enhanced Edition — Luma HDR mod (REDengine, 32-bit, DX9 -> D3D11 via dgVoodoo2).
//
// Hashes are the dgVoodoo-TRANSLATED ones and change with every wrapper build: 2.87.3 and 2.81.3 are keyed
// (2.81.3 emits ps_4_0 for the same passes), any other build needs a re-dump.
// The post chain is fp16 throughout and the "tonemap" is an adaptive exposure multiply, no curve or clamp.
// The UI blends src-alpha onto that fp16 scene in gamma space; the main menu draws no tonemap at all.
// Only ONE Luma .addon, and no other swapchain-hooking ReShade addon: they crash through dgVoodoo.

// The MessageBox is invisible under a borderless/fullscreen game and blocks the loader -> ReShade error 1114.
#define DISABLE_AUTO_DEBUGGER 1

#define GAME_THE_WITCHER_2 1

#define ENABLE_NGX 0 // NGX is x64-only and the game is 32-bit (and there are no motion vectors anyway)
#define ENABLE_FIDELITY_SK 0
#define GEOMETRY_SHADER_SUPPORT 0

#define ENABLE_SMAA 1 // replaces the final grade's built-in FXAA with SMAA ULTRA (+RCAS); core registers the 6 "SMAA ..." passes
// SMAA runs POST-final-grade via the post-draw callback, so it needs original_draw_dispatch_func non-null.
#define ENABLE_POST_DRAW_DISPATCH_CALLBACK 1

// No Luma bloom: the engine already draws a thresholdless glow around every light, so a pyramid on top
// re-blooms what the canvas contains. Both that and replacing the glow were built and rejected.

#include "..\..\Core\core.hpp"

// Tonemap ("exposure") permutations, dgVoodoo-translated ps_5_0 hashes.
static constexpr uint32_t kTonemapAdaptiveNoTint = 0x91348C0F; // DX9 0xC5ADBC35: exposure+scale, alpha passthrough
static constexpr uint32_t kTonemapAdaptiveTint = 0x00E31BF9;   // DX9 0xF01A691E: + fade/saturation/tint
// Two static permutations (exposure from PSC_LumRanges) have never been captured; their signature is 1 texture,
// dp4 cb4[58], min cap cb4[59].x. Not declared as 0: an absent pipeline hash reads as 0 and would match.
// Final grade (FXAA + gamma + tints + vignette), last pass before UI; hosts the HDR block and the SMAA hook.
static constexpr uint32_t kFinalGrade = 0xDE5CF9CD;
static constexpr uint32_t kFinalGradeNoAA = 0xCF3B72A9;       // game AA off: no FXAA block, scene alpha passed through
static constexpr uint32_t kFinalGradeNoVignette = 0xBABBFFAD; // no FXAA and no vignette; dump-verified as the last perm
// Native SSAO generator (HBAO variant, VS 0x5D9D0449): half-res r32_float LINEAR view depth at t0 -> half-res
// r8g8b8a8 (.x = AO, .y = viewZ). Only this draw is replaced; the vanilla chain downstream reads just .x:
// pack 0x953119B5 -> ping-pong 0xC131C40D x2 -> blur 0xD01CBD13 x2 -> apply 0x5C63E1C2.
// Needs SSAO on in the game's video settings.
static constexpr uint32_t kAOGen = 0x3FEEC0F7;
// AO pack (t0 = full-res r32_float LINEAR depth, t1 = the AO target): depth-capture fallback for SMAA
// predication, since it runs every frame while the tonemap capture only fires on the TINT perm.
static constexpr uint32_t kAOPack = 0x953119B5;

// The same passes as translated by dgVoodoo 2.81.3 (the build that runs under Proton), which emits ps_4_0 and
// therefore different hashes. Dump-verified as signature-identical to their counterparts above: same
// interpolators, t/s registers and cb slots, so the replacements and the slot-based captures are shared.
static constexpr uint32_t kTonemapAdaptiveNoTint_v281 = 0x6CF3E8B7;
static constexpr uint32_t kTonemapAdaptiveTint_v281 = 0xB293C5B1;
static constexpr uint32_t kFinalGrade_v281 = 0x517DC6D5;
static constexpr uint32_t kFinalGradeNoAA_v281 = 0xBBFEC706;
static constexpr uint32_t kFinalGradeNoVignette_v281 = 0x2CA0631E;
static constexpr uint32_t kAOGen_v281 = 0x6EC596CA;
static constexpr uint32_t kAOPack_v281 = 0x495E9133;

// The engine's glow chain (halo around candles and torches, distinct from the god rays) is left vanilla:
// copy 0x5A8E5532 -> 12-tap blur 0x88C500CF x2 -> screen blend 0x12931281.

// User settings, persisted in the [Luma] config section (LoadConfigs) unless noted otherwise.
static bool g_smaa_enable = true;
static float g_rcas_sharpness = 0.f;   // RCAS sharpen on SMAA output (0 = off)
static bool g_smaa_predication = true; // SMAA depth predication (r32f depth captured at the tint tonemap or the AO pack pass)
// Plane deviation counted as a full edge, as a fraction of view depth, so it is resolution independent.
// 0.02 = ~14 cm at the measured 7 m median depth; XeGTAO uses 0.011 and ASSAO 0.040 for the same test.
static float g_smaa_pred_tolerance = 0.02f;
static bool g_gtao_enable = true; // XeGTAO replaces the native SSAO generator (kAOGen)
static bool g_hide_ui = false;    // hide the game's HUD (for clean screenshots); session-only, never persisted

// XeGTAO knobs CB slot, must match "register(b9)" in Luma_TW2_XeGTAO.hlsl. Not b11: core's DrawBloom owns
// that slot for its own constants.
static constexpr UINT kGTAOKnobsCBSlot = 9;
// XeGTAO calibration knobs (DEV sliders, shipped at calibrated values).
static float g_gtao_final_value_power = 1.f; // primary darkness dial (user-calibrated: matches the vanilla AO histogram, mean 0.90 vs native 0.89)
static float g_gtao_depth_scale = 1.f;       // viewZ divisor (game units -> ~meters); dial against broad over-occlusion
static float g_gtao_radius_override = 0.f;   // > 0 overrides the shader's EFFECT_RADIUS (view units after DepthScale)
#if DEVELOPMENT
static int g_gtao_debug_view = 0; // 0=off 1=depth gradient 2=normals 3=AO x8 4=edges (shader honors it under DEVELOPMENT too)
#endif

struct TheWitcher2GameDeviceData final : public GameDeviceData
{
   // Set when the final grade runs, cleared every Present: scopes the Hide UI skip to this frame's
   // post-grade span.
   bool final_grade_fired_this_frame = false;

   // Repaired blend states, keyed by the ORIGINAL desc: a pointer key would go stale when a state is
   // released and its address reused.
   struct BlendDescCompare
   {
      bool operator()(const D3D11_BLEND_DESC& a, const D3D11_BLEND_DESC& b) const
      {
         return memcmp(&a, &b, sizeof(D3D11_BLEND_DESC)) < 0;
      }
   };
   std::map<D3D11_BLEND_DESC, ComPtr<ID3D11BlendState>, BlendDescCompare> fixed_blend_states;

   // ---- SMAA (see RunPostFinalGradeSMAA) ----
   // SMAA metrics CB (b1) = (1/w,1/h,w,h) + (predication scale,0,0,0); scale 2.0 when predication on, else 1.0.
   ComPtr<ID3D11Buffer> cb_smaa_metrics;
   uint32_t smaa_metrics_w = 0, smaa_metrics_h = 0;
   uint32_t smaa_core_w = 0, smaa_core_h = 0;
   // SMAA scratch. tex_input = SRV snapshot of the canvas (already gamma, fed to both DrawSMAA color args).
   ComPtr<ID3D11Texture2D> tex_input;
   ComPtr<ID3D11ShaderResourceView> srv_input;
   uint32_t smaa_temps_w = 0, smaa_temps_h = 0;
   // RCAS input temp (SRV+RTV), allocated ONLY while sharpening is on: with RCAS off, SMAA's last pass writes
   // the canvas directly and this stays null.
   ComPtr<ID3D11Texture2D> tex_smaa_out;
   ComPtr<ID3D11RenderTargetView> tex_smaa_out_rtv;
   ComPtr<ID3D11ShaderResourceView> tex_smaa_out_srv;
   uint32_t smaa_out_w = 0, smaa_out_h = 0;
   // RCAS sharpen CB (b0) = (w,h,sharpness,0) + output temp (canvas format, RTV).
   ComPtr<ID3D11Buffer> cb_sharpen;
   uint32_t sharpen_w = 0, sharpen_h = 0;
   float sharpen_amount = -1.f;
   // Full-res r32_float depth, captured at whichever comes first: the TINT tonemap draw (t1) or the AO pack
   // pass (t0). tex_pred is the R16F edge-ness from the Depth Extract CS, not a depth.
   ComPtr<ID3D11ShaderResourceView> srv_scene_depth;
   ComPtr<ID3D11Texture2D> tex_pred;
   ComPtr<ID3D11UnorderedAccessView> uav_pred;
   ComPtr<ID3D11ShaderResourceView> srv_pred;
   uint32_t pred_w = 0, pred_h = 0;
   ComPtr<ID3D11Buffer> cb_pred;
   float pred_tolerance = -1.f;
   float smaa_metrics_pred_scale = -1.f; // recreate the metrics CB when predication turns on/off

   // ---- XeGTAO (see RunXeGTAO) ----
   // Sized from the depth SRV captured at the hooked draw, so no per-present reset is needed. tex_gtao_final
   // is our copy source: the game's AO RT is created without D3D11_BIND_UNORDERED_ACCESS.
   ComPtr<ID3D11Texture2D> tex_gtao_depth_mips; // R32F, 5 mips (prefiltered view-space depth pyramid)
   ComPtr<ID3D11UnorderedAccessView> gtao_depth_mip_uavs[5];
   ComPtr<ID3D11ShaderResourceView> srv_gtao_depth_mips;
   ComPtr<ID3D11Texture2D> tex_gtao_working[2]; // R8G8_UNORM AO+edges ping-pong
   ComPtr<ID3D11UnorderedAccessView> uav_gtao_working[2];
   ComPtr<ID3D11ShaderResourceView> srv_gtao_working[2];
   ComPtr<ID3D11Texture2D> tex_gtao_final; // gtao_final_fmt, CopyResource'd into the game's AO RT
   ComPtr<ID3D11UnorderedAccessView> uav_gtao_final;
   uint32_t gtao_w = 0, gtao_h = 0;
   DXGI_FORMAT gtao_final_fmt = DXGI_FORMAT_UNKNOWN; // actual (possibly Luma-upgraded) AO RT format
   // The triple above is committed even on failure, so this latch tells "allocated" from "already failed" and
   // stops a per-frame retry that fragments a 32-bit address space. ReleaseGTAOScratch clears it.
   bool gtao_alloc_failed = false;
   ComPtr<ID3D11Buffer> cb_gtao; // knobs + viewport (kGTAOKnobsCBSlot), immutable, recreated on change
   float gtao_cb_fvp = -1.f, gtao_cb_depth_scale = -1.f, gtao_cb_radius = -1.f, gtao_cb_debug = -1.f;
   uint32_t gtao_cb_w = 0, gtao_cb_h = 0;

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
      tex_gtao_final.reset();
      uav_gtao_final.reset();
      gtao_w = 0;
      gtao_h = 0;
      gtao_final_fmt = DXGI_FORMAT_UNKNOWN;
      gtao_alloc_failed = false;
   }

   // Turning a feature off gives the address space back: at 4K these hold ~130 MB (SMAA) and ~30 MB (GTAO)
   // in a 32-bit process, and everything is recreated on demand.
   void ReleaseSMAAScratch()
   {
      srv_input.reset();
      tex_input.reset();
      smaa_temps_w = smaa_temps_h = 0;
      ReleaseSharpenScratch();
   }

   // The RCAS intermediate exists only while sharpening is on: with RCAS at 0 the SMAA chain writes the canvas
   // directly, so this is ~66 MB (4K rgba16f) of pure waste until the next resolution change.
   void ReleaseSharpenScratch()
   {
      tex_smaa_out_rtv.reset();
      tex_smaa_out_srv.reset();
      tex_smaa_out.reset();
      smaa_out_w = smaa_out_h = 0;
      cb_sharpen.reset();
      sharpen_w = sharpen_h = 0;
      sharpen_amount = -1.f;
   }

   void ReleasePredicationScratch()
   {
      uav_pred.reset();
      srv_pred.reset();
      tex_pred.reset();
      pred_w = pred_h = 0;
      cb_pred.reset();
      pred_tolerance = -1.f;
   }
};

class TheWitcher2Game final : public Game
{
   static TheWitcher2GameDeviceData& GetGameDeviceData(DeviceData& device_data)
   {
      return *static_cast<TheWitcher2GameDeviceData*>(device_data.game);
   }

   // Matches a pass by both of its keyed dgVoodoo hashes.
   static bool ContainsPixelShader(const ShaderHashesList<OneShaderPerPipeline>& shader_hashes, uint32_t hash, uint32_t hash_v281)
   {
      return shader_hashes.Contains(hash, reshade::api::shader_stage::pixel) || shader_hashes.Contains(hash_v281, reshade::api::shader_stage::pixel);
   }

   static bool IsTonemap(const ShaderHashesList<OneShaderPerPipeline>& shader_hashes)
   {
      return ContainsPixelShader(shader_hashes, kTonemapAdaptiveNoTint, kTonemapAdaptiveNoTint_v281) || ContainsPixelShader(shader_hashes, kTonemapAdaptiveTint, kTonemapAdaptiveTint_v281);
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

   // Any of the three final-grade permutations (FXAA and vignette are compiled in or out independently); all
   // three host the Luma HDR block through the same shader file.
   static bool IsFinalGrade(const ShaderHashesList<OneShaderPerPipeline>& shader_hashes)
   {
      return ContainsPixelShader(shader_hashes, kFinalGrade, kFinalGrade_v281) || ContainsPixelShader(shader_hashes, kFinalGradeNoAA, kFinalGradeNoAA_v281) || ContainsPixelShader(shader_hashes, kFinalGradeNoVignette, kFinalGradeNoVignette_v281);
   }

   // dgVoodoo sometimes leaves blending ENABLED on a secondary render target while RT0 has it off. D3D9 has one
   // global blend state and only per-RT write masks (D3DRS_COLORWRITEENABLE1/2/3), so the game never asked for
   // it and that target is corrupted. Flotsam water (PS 0xDA16C815): RT1 is the r32_float LINEAR DEPTH fog
   // reads, and the shader ends "mov o1.xyzw, v7.xxxx", so src_alpha IS the depth.
   // Repair = copy RT0's blend fields onto the offending targets; write masks stay, legal per-RT in D3D9.
   // The inverse shape is only reported: enabling blending where the wrapper left it off could only add damage.
   static DrawOrDispatchOverrideType FixImpossiblePerRTBlend(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, TheWitcher2GameDeviceData& game_device_data, reshade::api::shader_stage stages, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, bool is_custom_pass, std::function<void()>* original_draw_dispatch_func)
   {
      // Our own injected passes set their blend state deliberately. Re-issuing the draw is the only way to
      // apply a different state, so without that callback there is nothing to do.
      if (is_custom_pass || (stages & reshade::api::shader_stage::pixel) == 0 || original_draw_dispatch_func == nullptr)
         return DrawOrDispatchOverrideType::None;

      ComPtr<ID3D11BlendState> blend_state;
      FLOAT blend_factor[4];
      UINT sample_mask = 0;
      native_device_context->OMGetBlendState(blend_state.put(), blend_factor, &sample_mask);
      if (!blend_state)
         return DrawOrDispatchOverrideType::None; // no state object = default (blending off everywhere)

      D3D11_BLEND_DESC bd;
      blend_state->GetDesc(&bd);
      if (!bd.IndependentBlendEnable)
         return DrawOrDispatchOverrideType::None; // one state for all targets: already D3D9-shaped

      // Only BOUND targets count: the wrapper leaves stale BlendEnable in unused descriptor slots, which alone
      // matches nearly every draw and would take over passes other hooks own. Free descriptor scan first.
      bool disagreement = false;
      for (UINT i = 1; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT && !disagreement; i++)
         disagreement = bd.RenderTarget[i].BlendEnable != bd.RenderTarget[0].BlendEnable;
      if (!disagreement)
         return DrawOrDispatchOverrideType::None;

      ID3D11RenderTargetView* rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
      native_device_context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtvs, nullptr);
      bool bound[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
      for (UINT i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; i++)
      {
         bound[i] = rtvs[i] != nullptr;
         if (rtvs[i])
            rtvs[i]->Release(); // OMGetRenderTargets hands back references; only the bound/not-bound answer is kept
      }

      bool needs_fix = false;
      [[maybe_unused]] bool inverse_shape = false;
      for (UINT i = 1; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; i++)
      {
         if (!bound[i] || bd.RenderTarget[i].BlendEnable == bd.RenderTarget[0].BlendEnable)
            continue;
         if (bd.RenderTarget[0].BlendEnable == FALSE)
            needs_fix = true;
         else
            inverse_shape = true;
      }

#if DEVELOPMENT
      // One line per distinct shader: a session across locations and weather then names every pass carrying
      // this, which is how a water permutation outside the captured frames would surface.
      if (needs_fix || inverse_shape)
      {
         static std::unordered_set<uint64_t> logged_shaders;
         const uint64_t pixel_shader_hash = original_shader_hashes.pixel_shaders[0];
         if (logged_shaders.emplace(pixel_shader_hash).second)
         {
            reshade::log::message(reshade::log::level::warning,
               std::format("[TW2-BlendFix] impossible per-RT blend state (dgVoodoo artefact) on pixel shader 0x{:X} - {}", pixel_shader_hash, needs_fix ? "repaired" : "inverse shape, left alone").c_str());
         }
      }
#endif

      if (!needs_fix)
         return DrawOrDispatchOverrideType::None;

      ComPtr<ID3D11BlendState> fixed_state;
      if (const auto it = game_device_data.fixed_blend_states.find(bd); it != game_device_data.fixed_blend_states.end())
      {
         fixed_state = it->second;
      }
      else
      {
         D3D11_BLEND_DESC fixed_desc = bd;
         for (UINT i = 1; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; i++)
         {
            if (fixed_desc.RenderTarget[i].BlendEnable == bd.RenderTarget[0].BlendEnable)
               continue;
            const UINT8 write_mask = fixed_desc.RenderTarget[i].RenderTargetWriteMask; // legal per-RT in D3D9, keep it
            fixed_desc.RenderTarget[i] = bd.RenderTarget[0];
            fixed_desc.RenderTarget[i].RenderTargetWriteMask = write_mask;
         }
         if (FAILED(native_device->CreateBlendState(&fixed_desc, fixed_state.put())) || !fixed_state)
            return DrawOrDispatchOverrideType::None; // leave the draw untouched rather than run it half-applied
         game_device_data.fixed_blend_states[bd] = fixed_state;
      }

      native_device_context->OMSetBlendState(fixed_state.get(), blend_factor, sample_mask);
      (*original_draw_dispatch_func)();
      native_device_context->OMSetBlendState(blend_state.get(), blend_factor, sample_mask); // hand the game back its own state
      return DrawOrDispatchOverrideType::Replaced;
   }

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

   static bool CreateDefaultTex(ID3D11Device* device, uint32_t w, uint32_t h, UINT bind_flags, ComPtr<ID3D11Texture2D>& out, DXGI_FORMAT format)
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

#if ENABLE_SMAA
   // SMAA on the graded gamma canvas, after the original final-grade draw and before the UI draws on it; the
   // replaced grade skipped its own FXAA via LumaData.CustomData2.
   void RunPostFinalGradeSMAA(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, DeviceData& device_data, TheWitcher2GameDeviceData& gd, ID3D11Resource* canvas_res, ID3D11RenderTargetView* canvas_rtv)
   {
      uint4 cinfo{};
      DXGI_FORMAT cfmt = DXGI_FORMAT_UNKNOWN;
      GetResourceInfo(canvas_res, cinfo, cfmt);
      uint32_t w = cinfo.x, h = cinfo.y;
      if (w == 0 || h == 0 || (uint32_t)cfmt == (uint32_t)DXGI_FORMAT_UNKNOWN)
      {
         return;
      }

      // Skip SMAA this frame if a pass is still missing (async loader / live reload).
      const bool smaa_ready =
         AllShadersReady(device_data.native_pixel_shaders, {CompileTimeStringHash("SMAA Edge Detection PS"), CompileTimeStringHash("SMAA Blending Weight Calculation PS"), CompileTimeStringHash("SMAA Neighborhood Blending PS")}) &&
         AllShadersReady(device_data.native_vertex_shaders, {CompileTimeStringHash("SMAA Edge Detection VS"), CompileTimeStringHash("SMAA Blending Weight Calculation VS"), CompileTimeStringHash("SMAA Neighborhood Blending VS")});
      if (!smaa_ready)
      {
         return;
      }

      // Drop DrawSMAA's core-managed intermediates on resolution change so they recreate at the new size.
      if (gd.smaa_core_w != w || gd.smaa_core_h != h)
      {
         auto& mr = device_data.managed_resources;
         mr.depth_stencil_views[CompileTimeStringHash("smaa_dsv")].reset();
         mr.render_target_views[CompileTimeStringHash("smaa_edge_detection")].reset();
         mr.render_target_views[CompileTimeStringHash("smaa_blending_weight_calculation")].reset();
         gd.smaa_core_w = w;
         gd.smaa_core_h = h;
      }

      // Fall back to plain ULTRA when an input is missing (never scale 2.0 with a null texture) or the depth
      // size differs: the extract CS maps texels 1:1, so a mismatch reads a sub-rect and misaligns the mask.
      auto* pred_cs = FindShader(device_data.native_compute_shaders, CompileTimeStringHash("TW2 Depth Extract CS"));
      bool pred_ok = g_smaa_predication && gd.srv_scene_depth.get() != nullptr && pred_cs != nullptr;
      if (pred_ok)
      {
         uint4 dinfo{};
         DXGI_FORMAT dfmt = DXGI_FORMAT_UNKNOWN;
         GetResourceInfo(gd.srv_scene_depth.get(), dinfo, dfmt);
         pred_ok = dinfo.x == w && dinfo.y == h;
      }
      if (pred_ok)
      {
         if (!gd.cb_pred || gd.pred_tolerance != g_smaa_pred_tolerance)
         {
            const float p[4] = {g_smaa_pred_tolerance, 0.f, 0.f, 0.f};
            if (CreateImmutableCB(native_device, p, sizeof(p), gd.cb_pred))
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

      // Metrics CB: predication scale 2.0 when active, else 1.0. Recreate on resolution or predication flip.
      const float pred_scale = pred_ok ? 2.0f : 1.0f;
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
         return;

      // Resolve RCAS before allocating: it decides whether the last pass writes the canvas RTV directly,
      // which removes both the copy back and the intermediate.
      auto* sharpen_vs = FindShader(device_data.native_vertex_shaders, CompileTimeStringHash("Copy VS"));
      auto* sharpen_ps = FindShader(device_data.native_pixel_shaders, CompileTimeStringHash("TW2 Sharpen PS"));
      bool do_sharpen = g_rcas_sharpness > 0.f && sharpen_vs != nullptr && sharpen_ps != nullptr;
      if (do_sharpen)
      {
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
         if (!gd.tex_smaa_out || gd.smaa_out_w != w || gd.smaa_out_h != h)
         {
            gd.tex_smaa_out_rtv.reset();
            gd.tex_smaa_out_srv.reset();
            gd.tex_smaa_out.reset();
            if (CreateDefaultTex(native_device, w, h, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET, gd.tex_smaa_out, cfmt))
            {
               native_device->CreateRenderTargetView(gd.tex_smaa_out.get(), nullptr, gd.tex_smaa_out_rtv.put());
               native_device->CreateShaderResourceView(gd.tex_smaa_out.get(), nullptr, gd.tex_smaa_out_srv.put());
               gd.smaa_out_w = w;
               gd.smaa_out_h = h;
            }
         }
         if (!gd.cb_sharpen || !gd.tex_smaa_out_rtv || !gd.tex_smaa_out_srv)
            do_sharpen = false;
      }

      if (!gd.tex_input || gd.smaa_temps_w != w || gd.smaa_temps_h != h)
      {
         gd.srv_input.reset();
         gd.tex_input.reset();
         if (CreateDefaultTex(native_device, w, h, D3D11_BIND_SHADER_RESOURCE, gd.tex_input, cfmt))
         {
            native_device->CreateShaderResourceView(gd.tex_input.get(), nullptr, gd.srv_input.put());
            gd.smaa_temps_w = w;
            gd.smaa_temps_h = h;
         }
      }
      if (!gd.srv_input)
         return;

      // Snapshot the canvas color: the chain writes the canvas, so it must sample this copy, not the canvas.
      native_device_context->CopyResource(gd.tex_input.get(), canvas_res);

      // Predication signal: the game's linear r32f depth -> plane-deviation edge-ness in R16F (gd.tex_pred);
      // see Luma_TW2_DepthExtract.hlsl for why this is an edge test rather than a depth rescale.
      if (pred_ok)
      {
         DrawStateStack<DrawStateStackType::Compute> pred_cs_state;
         pred_cs_state.Cache(native_device_context, device_data.uav_max_count);

         ID3D11ShaderResourceView* ps_srv = gd.srv_scene_depth.get();
         ID3D11UnorderedAccessView* ps_uav = gd.uav_pred.get();
         ID3D11Buffer* ps_cb = gd.cb_pred.get();
         native_device_context->CSSetShaderResources(0, 1, &ps_srv);
         native_device_context->CSSetUnorderedAccessViews(0, 1, &ps_uav, nullptr);
         native_device_context->CSSetConstantBuffers(0, 1, &ps_cb);
         native_device_context->CSSetShader(pred_cs, nullptr, 0);
         native_device_context->Dispatch((w + 7) / 8, (h + 7) / 8, 1);

         pred_cs_state.Restore(native_device_context);
      }

      // SMAA (3 passes). Metrics CB at VS+PS b1 (DrawSMAA restores VS/PS/SRVs/RTs, not cbuffers).
      ComPtr<ID3D11Buffer> vs_cb1_orig, ps_cb1_orig;
      native_device_context->VSGetConstantBuffers(1, 1, vs_cb1_orig.put());
      native_device_context->PSGetConstantBuffers(1, 1, ps_cb1_orig.put());
      ID3D11Buffer* mcb = gd.cb_smaa_metrics.get();
      native_device_context->VSSetConstantBuffers(1, 1, &mcb);
      native_device_context->PSSetConstantBuffers(1, 1, &mcb);

      // The last pass of the chain renders straight into the canvas RTV — no write-back copy. Reading the
      // canvas is safe because SMAA/RCAS sample the snapshot (tex_input), never the canvas itself.
      DrawSMAA(native_device, native_device_context, device_data,
         do_sharpen ? gd.tex_smaa_out_rtv.get() : canvas_rtv, gd.srv_input.get(), gd.srv_input.get(),
         pred_ok ? gd.srv_pred.get() : nullptr /*predication signal*/);

      if (do_sharpen)
      {
         DrawStateStack<DrawStateStackType::FullGraphics> sharpen_state;
         sharpen_state.Cache(native_device_context, device_data.uav_max_count);

         ID3D11Buffer* scb = gd.cb_sharpen.get();
         native_device_context->PSSetConstantBuffers(0, 1, &scb);
         DrawCustomPixelShader(native_device_context, device_data.default_depth_stencil_state.get(), device_data.default_blend_state.get(), nullptr,
            sharpen_vs, sharpen_ps, gd.tex_smaa_out_srv.get(), canvas_rtv, w, h, false);

         sharpen_state.Restore(native_device_context);
      }

      ID3D11Buffer* vcb = vs_cb1_orig.get();
      ID3D11Buffer* pcb = ps_cb1_orig.get();
      native_device_context->VSSetConstantBuffers(1, 1, &vcb);
      native_device_context->PSSetConstantBuffers(1, 1, &pcb);
   }
#endif // ENABLE_SMAA

   // Take over the kAOGen draw: 4 XeGTAO compute passes into our scratch, then CopyResource into the game's AO
   // RT so the vanilla chain keeps working. Inputs come from the hooked draw (depth SRV t0, RTV, cb4 for the
   // NDC->view ray scale); a missing one returns None so the native HBAO draw runs.
   DrawOrDispatchOverrideType RunXeGTAO(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, DeviceData& device_data, TheWitcher2GameDeviceData& gd)
   {
      // Resolve the four passes once (async loader / live reload) and reuse the pointers at dispatch.
      auto* cs_prefilter = FindShader(device_data.native_compute_shaders, CompileTimeStringHash("TW2 XeGTAO Prefilter Depths CS"));
      auto* cs_main = FindShader(device_data.native_compute_shaders, CompileTimeStringHash("TW2 XeGTAO Main Pass CS"));
      auto* cs_denoise_1 = FindShader(device_data.native_compute_shaders, CompileTimeStringHash("TW2 XeGTAO Denoise Pass 1 CS"));
      auto* cs_denoise_2 = FindShader(device_data.native_compute_shaders, CompileTimeStringHash("TW2 XeGTAO Denoise Pass 2 CS"));
      if (cs_prefilter == nullptr || cs_main == nullptr || cs_denoise_1 == nullptr || cs_denoise_2 == nullptr)
         return DrawOrDispatchOverrideType::None;

      // Game depth (the hooked draw's t0): half-res r32_float linear view-space depth.
      ComPtr<ID3D11ShaderResourceView> srv_depth;
      native_device_context->PSGetShaderResources(0, 1, srv_depth.put());
      if (!srv_depth)
         return DrawOrDispatchOverrideType::None;
      uint4 dinfo{};
      DXGI_FORMAT dfmt = DXGI_FORMAT_UNKNOWN;
      GetResourceInfo(srv_depth.get(), dinfo, dfmt);
      const uint32_t w = dinfo.x, h = dinfo.y;
      if (w == 0 || h == 0)
         return DrawOrDispatchOverrideType::None;

      // The game's AO render target; must match the depth size (both half render res).
      ComPtr<ID3D11RenderTargetView> rtv;
      native_device_context->OMGetRenderTargets(1, rtv.put(), nullptr);
      if (!rtv)
         return DrawOrDispatchOverrideType::None;
      ComPtr<ID3D11Resource> rt_res;
      rtv->GetResource(rt_res.put());
      ComPtr<ID3D11Texture2D> rt_tex;
      if (!rt_res || FAILED(rt_res->QueryInterface(rt_tex.put())))
         return DrawOrDispatchOverrideType::None;
      // Read the ACTUAL descriptor: our own indirect upgrade can make this RT rgba16_float while ReShade
      // metadata still reports the original, and CopyResource silently no-ops on a family mismatch (frozen AO).
      D3D11_TEXTURE2D_DESC rt_desc;
      rt_tex->GetDesc(&rt_desc);
      if (rt_desc.Width != w || rt_desc.Height != h)
         return DrawOrDispatchOverrideType::None;
      // Typeless family -> typed equivalent, since our UAV-written copy source needs a typed format.
      DXGI_FORMAT final_fmt = rt_desc.Format;
      if (final_fmt == DXGI_FORMAT_R8G8B8A8_TYPELESS)
         final_fmt = DXGI_FORMAT_R8G8B8A8_UNORM;
      else if (final_fmt == DXGI_FORMAT_R16G16B16A16_TYPELESS)
         final_fmt = DXGI_FORMAT_R16G16B16A16_FLOAT;
      // Typed UAV store is only guaranteed for these two, and they are the only formats the upgrade list can
      // produce. Anything else would pass the copy check below and then fail at CreateTexture2D.
      if (final_fmt != DXGI_FORMAT_R8G8B8A8_UNORM && final_fmt != DXGI_FORMAT_R16G16B16A16_FLOAT)
         return DrawOrDispatchOverrideType::None;
      if (!AreFormatsCopyCompatible(rt_desc.Format, final_fmt))
         return DrawOrDispatchOverrideType::None;

      // The XeGTAO shader derives its NDC->view ray scale from the game's cb4 (bound to the PS stage at the
      // hooked draw); it is copied to the CS stage below (in-place takeover, same frame state).
      ComPtr<ID3D11Buffer> game_cb4;
      native_device_context->PSGetConstantBuffers(4, 1, game_cb4.put());
      if (!game_cb4)
         return DrawOrDispatchOverrideType::None;

      // (Re)create the scratch set on first use, resolution change, or RT format change (all-or-nothing).
      if (gd.gtao_w != w || gd.gtao_h != h || gd.gtao_final_fmt != final_fmt || !gd.tex_gtao_depth_mips || !gd.tex_gtao_working[1] || !gd.tex_gtao_final)
      {
         if (gd.gtao_alloc_failed && gd.gtao_w == w && gd.gtao_h == h && gd.gtao_final_fmt == final_fmt)
            return DrawOrDispatchOverrideType::None;

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

         // Final AO in the game's channel layout (.x = AO), in the RT's ACTUAL format so CopyResource is legal.
         // The shader writes a plain float4 UAV, valid against both unorm8 and fp16.
         td.Format = final_fmt;
         ok = ok && SUCCEEDED(native_device->CreateTexture2D(&td, nullptr, gd.tex_gtao_final.put()));
         ok = ok && SUCCEEDED(native_device->CreateUnorderedAccessView(gd.tex_gtao_final.get(), nullptr, gd.uav_gtao_final.put()));

         if (!ok)
            gd.ReleaseGTAOScratch(); // drop the partials (this also resets the triple below and the latch)
         // Commit the target triple either way: on failure it is what the latch is keyed to.
         gd.gtao_w = w;
         gd.gtao_h = h;
         gd.gtao_final_fmt = final_fmt;
         gd.gtao_alloc_failed = !ok;
         if (!ok)
            return DrawOrDispatchOverrideType::None; // leaves the native draw active
      }

#if DEVELOPMENT
      const float dbg = (float)g_gtao_debug_view;
#else
      const float dbg = 0.f;
#endif
      // Knobs + viewport CB. The viewport rides along so the shader never trusts game constants for it.
      if (!gd.cb_gtao || gd.gtao_cb_fvp != g_gtao_final_value_power || gd.gtao_cb_depth_scale != g_gtao_depth_scale ||
          gd.gtao_cb_radius != g_gtao_radius_override || gd.gtao_cb_debug != dbg || gd.gtao_cb_w != w || gd.gtao_cb_h != h)
      {
         const float knobs[8] = {g_gtao_final_value_power, g_gtao_depth_scale, g_gtao_radius_override, dbg, 1.f / (float)w, 1.f / (float)h, 0.f, 0.f};
         if (CreateImmutableCB(native_device, knobs, sizeof(knobs), gd.cb_gtao))
         {
            gd.gtao_cb_fvp = g_gtao_final_value_power;
            gd.gtao_cb_depth_scale = g_gtao_depth_scale;
            gd.gtao_cb_radius = g_gtao_radius_override;
            gd.gtao_cb_debug = dbg;
            gd.gtao_cb_w = w;
            gd.gtao_cb_h = h;
         }
      }
      if (!gd.cb_gtao)
         return DrawOrDispatchOverrideType::None;

      DrawStateStack<DrawStateStackType::Compute> st;
      st.Cache(native_device_context, device_data.uav_max_count);

      ID3D11Buffer* gcb = game_cb4.get();
      native_device_context->CSSetConstantBuffers(4, 1, &gcb);
      ID3D11Buffer* kcb = gd.cb_gtao.get();
      native_device_context->CSSetConstantBuffers(kGTAOKnobsCBSlot, 1, &kcb);
      ID3D11SamplerState* smp = device_data.sampler_state_point.get();
      native_device_context->CSSetSamplers(0, 1, &smp);

      static constexpr std::array<ID3D11UnorderedAccessView*, 5> uav_nulls5 = {};
      static constexpr std::array<ID3D11ShaderResourceView*, 2> srv_nulls2 = {};

      // Prefilter game depth into the R32F mip pyramid; each thread covers 2x2 pixels.
      {
         native_device_context->CSSetShaderResources(0, 2, srv_nulls2.data());
         ID3D11ShaderResourceView* srv = srv_depth.get();
         ID3D11UnorderedAccessView* uavs[5] = {gd.gtao_depth_mip_uavs[0].get(), gd.gtao_depth_mip_uavs[1].get(),
            gd.gtao_depth_mip_uavs[2].get(), gd.gtao_depth_mip_uavs[3].get(), gd.gtao_depth_mip_uavs[4].get()};
         native_device_context->CSSetUnorderedAccessViews(0, 5, uavs, nullptr);
         native_device_context->CSSetShaderResources(0, 1, &srv);
         native_device_context->CSSetShader(cs_prefilter, nullptr, 0);
         native_device_context->Dispatch((w + 15) / 16, (h + 15) / 16, 1);
         native_device_context->CSSetUnorderedAccessViews(0, 5, uav_nulls5.data(), nullptr);
      }
      // Bind each destination UAV before its source SRVs: D3D11 otherwise keeps the previous UAV hazard and
      // silently nulls the conflicting SRV. Normals are generated from depth inside the shader.
      {
         native_device_context->CSSetShaderResources(0, 2, srv_nulls2.data());
         ID3D11ShaderResourceView* srv = gd.srv_gtao_depth_mips.get();
         ID3D11UnorderedAccessView* uav = gd.uav_gtao_working[0].get();
         native_device_context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
         native_device_context->CSSetShaderResources(0, 1, &srv);
         native_device_context->CSSetShader(cs_main, nullptr, 0);
         native_device_context->Dispatch((w + 7) / 8, (h + 7) / 8, 1);
      }
      // First denoiser writes working1, two horizontal pixels per thread.
      {
         native_device_context->CSSetShaderResources(0, 2, srv_nulls2.data());
         ID3D11ShaderResourceView* srv = gd.srv_gtao_working[0].get();
         ID3D11UnorderedAccessView* uav = gd.uav_gtao_working[1].get();
         native_device_context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
         native_device_context->CSSetShaderResources(0, 1, &srv);
         native_device_context->CSSetShader(cs_denoise_1, nullptr, 0);
         native_device_context->Dispatch((w + 15) / 16, (h + 7) / 8, 1);
      }
      {
         native_device_context->CSSetShaderResources(0, 2, srv_nulls2.data());
         ID3D11ShaderResourceView* srv = gd.srv_gtao_working[1].get();
         ID3D11UnorderedAccessView* uav = gd.uav_gtao_final.get();
         native_device_context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
         native_device_context->CSSetShaderResources(0, 1, &srv);
         native_device_context->CSSetShader(cs_denoise_2, nullptr, 0);
         native_device_context->Dispatch((w + 15) / 16, (h + 7) / 8, 1);
         native_device_context->CSSetUnorderedAccessViews(0, 1, uav_nulls5.data(), nullptr);
      }

      st.Restore(native_device_context);

      // The destination is still bound as the draw's render target, and copying into an OM-bound resource is a
      // D3D11 hazard the runtime does not resolve: unbind around the copy, then restore the game's binding.
      {
         ComPtr<ID3D11DepthStencilView> dsv_orig;
         native_device_context->OMGetRenderTargets(0, nullptr, dsv_orig.put());
         native_device_context->OMSetRenderTargets(0, nullptr, nullptr);
         native_device_context->CopyResource(rt_res.get(), gd.tex_gtao_final.get());
         ID3D11RenderTargetView* rtv_raw = rtv.get();
         native_device_context->OMSetRenderTargets(1, &rtv_raw, dsv_orig.get());
      }

      return DrawOrDispatchOverrideType::Replaced;
   }

public:
   void OnInit(bool async) override
   {
      // Game-specific HDR toggle consumed by the replaced tonemap shaders (Luma_TW2_Tonemap.hlsl).
      std::vector<ShaderDefineData> game_shader_defines_data = {
         {"TONEMAP_TYPE", '1', true, false, "0 - SDR: Vanilla (bit-exact reference)\n1 - HDR: highlight expansion + DICE display map", 1},
         {"XE_GTAO_QUALITY", '3', true, false, "XeGTAO quality (slice count)\n0 - Low\n1 - Medium\n2 - High\n3 - Very High\n4 - Ultra", 4},
      };
      shader_defines_data.append_range(game_shader_defines_data);
      assert(shader_defines_data.size() < MAX_SHADER_DEFINES);

      // DX9-era gamma-2.2 SDR: post buffers stay GAMMA so the gamma-SDR HUD blends like vanilla. The final
      // grade pre-scales by GamePaperWhite/UIPaperWhite (UI_DRAW_TYPE 2) so the HUD lands at its own level.
      GetShaderDefineData(POST_PROCESS_SPACE_TYPE_HASH).SetDefaultValue('0');
      GetShaderDefineData(EARLY_DISPLAY_ENCODING_HASH).SetDefaultValue('0');
      GetShaderDefineData(VANILLA_ENCODING_TYPE_HASH).SetDefaultValue('1'); // Gamma 2.2 in and out
      GetShaderDefineData(GAMMA_CORRECTION_TYPE_HASH).SetDefaultValue('1');
      GetShaderDefineData(GAMUT_MAPPING_TYPE_HASH).SetDefaultValue('1'); // gamut-map wild colors in composition
      GetShaderDefineData(UI_DRAW_TYPE_HASH).SetDefaultValue('2');       // HUD gets its own UIPaperWhite + gamma blend

      // Manual Scene + UI Paper White sliders instead of the OS HDR reference level (UI default 203 nits, BT.2408).
      use_os_reference_white_level = false;

      // The game (via dgVoodoo) binds b0-b5 only (runtime-verified); b12/b13 are free for Luma.
      luma_settings_cbuffer_index = 13;
      luma_data_cbuffer_index = 12;
      luma_ui_cbuffer_index = -1;

      // Grade controls: Exposure in Luma_TW2_Tonemap.hlsl, the rest in the final grade, all vanilla no-ops by
      // default. No highlight-expansion knob: the fp16 overshoot already reaches ~2-6x.
      default_luma_global_game_settings.Exposure = 1.f;               // scene multiplier (1x)
      default_luma_global_game_settings.Saturation = 1.f;             // Oklab saturation
      default_luma_global_game_settings.HighlightDechroma = 0.f;      // off; only mandatory DICE/gamut desat applies
      default_luma_global_game_settings.Dithering = 1.f;              // animated triangular dither at output (HDR and SDR), anti-banding on
      default_luma_global_game_settings.VideoAutoHDREnable = 1.f;     // light AutoHDR on pre-rendered videos (HDR only)
      default_luma_global_game_settings.VideoAutoHDRBoost = 0.5f;     // highlight-expansion strength (peak ~165 nits at 0.5)
      default_luma_global_game_settings.VignetteIntensity = 1.f;      // vanilla vignette darkening (0 = none)
      default_luma_global_game_settings.HighlightsHueChroma = 0.4f;   // vanilla clip whitening: reproduces 40% of the clip's measured chroma loss (see the shader)
      default_luma_global_game_settings.HighlightsHueStrength = 0.8f; // hue adoption; never 1.0 — a fully clipped reference is achromatic (see the shader)
      default_luma_global_game_settings.Contrast = 1.f;               // slope contrast around 18% mid-gray (HDR path)
      default_luma_global_game_settings.BloomIntensity = 1.f;         // engine glow scale (0 = no halo around lights)
      default_luma_global_game_settings.ColorGradingIntensity = 1.f;  // vanilla highlight/shadow tint strength (0 = untinted grade)
      cb_luma_global_settings.GameSettings = default_luma_global_game_settings;

#if ENABLE_SMAA
      // Core auto-registers the 6 SMAA passes. The canvas is GAMMA space and feeds both DrawSMAA color args
      // directly, with no color-prep CS.
      // RCAS sharpen PS (drawn via core "Copy VS" + DrawCustomPixelShader after SMAA).
      native_shaders_definitions.emplace(CompileTimeStringHash("TW2 Sharpen PS"),
         ShaderDefinition{"Luma_TW2_Sharpen", reshade::api::pipeline_subobject_type::pixel_shader, nullptr, "sharpen_ps"});
      // Depth-extract CS for SMAA predication: game r32f LINEAR view-space depth -> R16F edge-ness in [0,1].
      native_shaders_definitions.emplace(CompileTimeStringHash("TW2 Depth Extract CS"),
         ShaderDefinition("Luma_TW2_DepthExtract", reshade::api::pipeline_subobject_type::compute_shader));
#endif

      // XeGTAO compute passes (Luma_TW2_XeGTAO.hlsl); the two denoisers differ only by XE_GTAO_FINAL_APPLY.
      native_shaders_definitions.emplace(CompileTimeStringHash("TW2 XeGTAO Prefilter Depths CS"),
         ShaderDefinition{"Luma_TW2_XeGTAO", reshade::api::pipeline_subobject_type::compute_shader, nullptr, "prefilter_depths16x16_cs"});
      native_shaders_definitions.emplace(CompileTimeStringHash("TW2 XeGTAO Main Pass CS"),
         ShaderDefinition{"Luma_TW2_XeGTAO", reshade::api::pipeline_subobject_type::compute_shader, nullptr, "main_pass_cs"});
      native_shaders_definitions.emplace(CompileTimeStringHash("TW2 XeGTAO Denoise Pass 1 CS"),
         ShaderDefinition{"Luma_TW2_XeGTAO", reshade::api::pipeline_subobject_type::compute_shader, nullptr, "denoise_pass_cs", {{"XE_GTAO_FINAL_APPLY", "0"}}});
      native_shaders_definitions.emplace(CompileTimeStringHash("TW2 XeGTAO Denoise Pass 2 CS"),
         ShaderDefinition{"Luma_TW2_XeGTAO", reshade::api::pipeline_subobject_type::compute_shader, nullptr, "denoise_pass_cs", {{"XE_GTAO_FINAL_APPLY", "1"}}});
   }

   void OnCreateDevice(ID3D11Device* native_device, DeviceData& device_data) override
   {
      device_data.game = new TheWitcher2GameDeviceData;
   }

   void OnDestroyDeviceData(DeviceData& device_data) override
   {
      // GameDeviceData lacks a virtual destructor; delete through the concrete type to release derived members.
      delete static_cast<TheWitcher2GameDeviceData*>(device_data.game);
      device_data.game = nullptr;
   }

   DrawOrDispatchOverrideType OnDrawOrDispatch(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, reshade::api::shader_stage stages, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, bool is_custom_pass, bool& updated_cbuffers, std::function<void()>* original_draw_dispatch_func) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);

      // After the final grade the HUD is the only alpha-blended geometry while the post chain is opaque, so
      // keying on blend state within this frame's post-grade span hides it without touching the scene.
      if (g_hide_ui && game_device_data.final_grade_fired_this_frame && !is_custom_pass && !IsFinalGrade(original_shader_hashes))
      {
         ComPtr<ID3D11BlendState> blend_state;
         FLOAT blend_factor[4];
         UINT sample_mask = 0;
         native_device_context->OMGetBlendState(blend_state.put(), blend_factor, &sample_mask);
         if (blend_state)
         {
            D3D11_BLEND_DESC bd;
            blend_state->GetDesc(&bd);
            if (bd.RenderTarget[0].BlendEnable != FALSE)
               return DrawOrDispatchOverrideType::Skip;
         }
      }

      if (IsTonemap(original_shader_hashes))
      {
         // One permutation, two roles: the MAIN grade draws at swapchain resolution, aux draws feed DoF/flare
         // smaller. Both stay bit-exact vanilla; the role only picks which draw marks main post processing.
         bool is_main = false;
         ComPtr<ID3D11RenderTargetView> rtv;
         native_device_context->OMGetRenderTargets(1, rtv.put(), nullptr);
         if (rtv)
         {
            ComPtr<ID3D11Resource> rt_resource;
            rtv->GetResource(rt_resource.put());
            ComPtr<ID3D11Texture2D> rt_texture;
            if (rt_resource && SUCCEEDED(rt_resource->QueryInterface(rt_texture.put())))
            {
               D3D11_TEXTURE2D_DESC rt_desc;
               rt_texture->GetDesc(&rt_desc);
               // Not an equality test: with UberSampling the scene renders LARGER than the swapchain. Matching
               // aspect plus at-least-swapchain size still excludes the smaller aux targets.
               const UINT out_w = (UINT)device_data.output_resolution.x;
               const UINT out_h = (UINT)device_data.output_resolution.y;
               const bool at_least_full_res = rt_desc.Width >= out_w && rt_desc.Height >= out_h;
               const bool aspect_matches = out_h != 0 && rt_desc.Height != 0 && fabsf(((float)rt_desc.Width / (float)rt_desc.Height) - ((float)out_w / (float)out_h)) < 0.05f;
               is_main = at_least_full_res && aspect_matches;
            }
         }

         if (is_main)
         {
            device_data.has_drawn_main_post_processing = true;
         }

#if ENABLE_SMAA
         // The tint perm binds the full-res r32_float depth at t1 (declared-but-unused there); capture it for
         // SMAA predication. Bindings are read regardless of the pass being hash-replaced.
         if (ContainsPixelShader(original_shader_hashes, kTonemapAdaptiveTint, kTonemapAdaptiveTint_v281))
         {
            game_device_data.srv_scene_depth = nullptr;
            native_device_context->PSGetShaderResources(1, 1, game_device_data.srv_scene_depth.put());
         }
#endif

         // Upload BOTH: once updated_cbuffers is set core skips its own upload, and sending only LumaData
         // would leave LumaSettings (b13) zeroed -> GamePaperWhiteNits 0 -> UI prescale blacks the screen.
         SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, stages, LumaConstantBufferType::LumaSettings);
         SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, stages, LumaConstantBufferType::LumaData, is_main ? 1u : 0u);
         updated_cbuffers = true;
      }
      else if (IsFinalGrade(original_shader_hashes))
      {
         // CustomData2 = SMAA active, which makes the grade skip its built-in FXAA.
         game_device_data.final_grade_fired_this_frame = true; // opens the Hide UI window for the rest of the frame

         const uint32_t smaa_active = g_smaa_enable ? 1u : 0u;

         SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, stages, LumaConstantBufferType::LumaSettings);
         SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, stages, LumaConstantBufferType::LumaData, 0u, smaa_active);
         updated_cbuffers = true;

#if ENABLE_SMAA
         // Run the original grade draw, then SMAA on its output. Without the callback this falls back to a
         // normal draw, and the grade has already skipped FXAA for this frame: one frame of no AA.
         if (g_smaa_enable && original_draw_dispatch_func != nullptr)
         {
            (*original_draw_dispatch_func)();
            ComPtr<ID3D11RenderTargetView> grade_rtv;
            native_device_context->OMGetRenderTargets(1, grade_rtv.put(), nullptr);
            if (grade_rtv)
            {
               ComPtr<ID3D11Resource> canvas_res;
               grade_rtv->GetResource(canvas_res.put());
               if (canvas_res)
                  RunPostFinalGradeSMAA(native_device, native_device_context, device_data, game_device_data, canvas_res.get(), grade_rtv.get());
            }
            return DrawOrDispatchOverrideType::Replaced; // we ran the original draw ourselves
         }
#endif
      }
      // Native SSAO generator -> XeGTAO takeover; independent of the tonemap/grade chain above.
      if (g_gtao_enable && ContainsPixelShader(original_shader_hashes, kAOGen, kAOGen_v281))
      {
         return RunXeGTAO(native_device, native_device_context, device_data, game_device_data);
      }
#if ENABLE_SMAA
      if (ContainsPixelShader(original_shader_hashes, kAOPack, kAOPack_v281))
      {
         // Fallback depth capture: the AO pack pass binds the same r32_float depth at t0 every frame, while
         // the tonemap capture only fires on the TINT perm. First capture of the frame wins, same buffer.
         if (g_smaa_predication && !game_device_data.srv_scene_depth)
         {
            native_device_context->PSGetShaderResources(0, 1, game_device_data.srv_scene_depth.put());
         }
      }
#endif

      // LAST on purpose: this one re-issues the draw itself, so it must yield to every hook above, or it runs
      // vanilla a pass another hook meant to take over (the native SSAO draw XeGTAO replaces).
      return FixImpossiblePerRTBlend(native_device, native_device_context, game_device_data, stages, original_shader_hashes, is_custom_pass, original_draw_dispatch_func);
   }

   void OnPresent(ID3D11Device* native_device, DeviceData& device_data) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);

      // Menu/loading frames run no tonemap: "has_drawn_main_post_processing" stays false there so the core
      // display composition treats the frame as plain SDR UI at UIPaperWhite (do NOT force it here).
      game_device_data.final_grade_fired_this_frame = false; // re-arm the Hide UI window for the next frame

      // Give the scratch back when a feature is switched off; it is all lazily recreated. Predication and the
      // RCAS intermediate are separate because either can be off while SMAA runs.
      if (!g_gtao_enable && game_device_data.gtao_w != 0)
         game_device_data.ReleaseGTAOScratch();
#if ENABLE_SMAA
      if (!g_smaa_enable && game_device_data.tex_input)
         game_device_data.ReleaseSMAAScratch();
      if ((!g_smaa_enable || !g_smaa_predication) && game_device_data.tex_pred)
         game_device_data.ReleasePredicationScratch();
      if (g_rcas_sharpness <= 0.f && game_device_data.tex_smaa_out)
         game_device_data.ReleaseSharpenScratch();
      // Per-frame capture: never let a stale depth SRV from a previous scene leak into a frame whose
      // tonemap didn't re-capture it (menus; the SMAA pass then falls back to null predication).
      game_device_data.srv_scene_depth = nullptr;
#endif
   }

   void LoadConfigs() override
   {
#if ENABLE_SMAA
      reshade::get_config_value(nullptr, NAME, "SMAAEnable", g_smaa_enable);
      reshade::get_config_value(nullptr, NAME, "RCASSharpness", g_rcas_sharpness);
      reshade::get_config_value(nullptr, NAME, "SMAAPredication", g_smaa_predication);
#endif
      reshade::get_config_value(nullptr, NAME, "GTAOEnable", g_gtao_enable);

      // HDR grade sliders (cb_luma_global_settings_dirty is already true at init -> uploaded on first frame).
      auto& gs = cb_luma_global_settings.GameSettings;
      reshade::get_config_value(nullptr, NAME, "Exposure", gs.Exposure);
      reshade::get_config_value(nullptr, NAME, "Saturation", gs.Saturation);
      reshade::get_config_value(nullptr, NAME, "HighlightDechroma", gs.HighlightDechroma);
      reshade::get_config_value(nullptr, NAME, "Dithering", gs.Dithering);
      reshade::get_config_value(nullptr, NAME, "VignetteIntensity", gs.VignetteIntensity);
      reshade::get_config_value(nullptr, NAME, "Contrast", gs.Contrast);
      reshade::get_config_value(nullptr, NAME, "BloomIntensity", gs.BloomIntensity);
      reshade::get_config_value(nullptr, NAME, "ColorGradingIntensity", gs.ColorGradingIntensity);
      // "Hide Gameplay UI" is deliberately NOT persisted: a saved HUD-less state makes the
      // next launch look broken.
      {
         bool video_auto_hdr = gs.VideoAutoHDREnable > 0.5f;
         reshade::get_config_value(nullptr, NAME, "VideoAutoHDREnable", video_auto_hdr);
         gs.VideoAutoHDREnable = video_auto_hdr ? 1.f : 0.f;
      }
      reshade::get_config_value(nullptr, NAME, "VideoAutoHDRBoost", gs.VideoAutoHDRBoost);
   }

   void DrawImGuiSettings(DeviceData& device_data) override
   {
#if ENABLE_SMAA
      ImGui::SeparatorText("Anti-Aliasing");
      if (ImGui::Checkbox("SMAA Enable", &g_smaa_enable))
         reshade::set_config_value(nullptr, NAME, "SMAAEnable", g_smaa_enable);
      if (ImGui::IsItemHovered())
         ImGui::SetTooltip("Replaces the game's FXAA with SMAA (works with the game's Anti-aliasing setting on or off).");
      ImGui::BeginDisabled(!g_smaa_enable);
      ImGui::SliderFloat("RCAS Sharpness", &g_rcas_sharpness, 0.f, 1.f);
      if (ImGui::IsItemDeactivatedAfterEdit())
         reshade::set_config_value(nullptr, NAME, "RCASSharpness", g_rcas_sharpness);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("Sharpening applied on top of SMAA (0 = off).");
      if (DrawResetButton<float, false>(g_rcas_sharpness, 0.f, "RCASSharpness"))
         reshade::set_config_value(nullptr, NAME, "RCASSharpness", g_rcas_sharpness);
#if DEVELOPMENT || TEST
      if (ImGui::Checkbox("SMAA Predication", &g_smaa_predication))
         reshade::set_config_value(nullptr, NAME, "SMAAPredication", g_smaa_predication);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("Relaxes the edge threshold back to base ULTRA on geometric silhouettes only. Off = plain ULTRA everywhere (threshold scale 1.0).");
      ImGui::SliderFloat("SMAA Predication Tolerance", &g_smaa_pred_tolerance, 0.002f, 0.1f, "%.3f", ImGuiSliderFlags_Logarithmic);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("Plane deviation counted as a full edge, as a fraction of view depth. The ONLY predication dial — the shader threshold stays 0.5 by design (see Luma_TW2_DepthExtract.hlsl). AO precedents: 0.011 (XeGTAO) .. 0.040 (ASSAO).");
#endif
      ImGui::EndDisabled();
#endif

      // Exposure and Color Grading Intensity act on SDR and HDR alike; the gated block below is HDR-only.
      ImGui::SeparatorText("Grade");
      auto& gs = cb_luma_global_settings.GameSettings;
      auto& gs_def = default_luma_global_game_settings;

      if (ImGui::SliderFloat("Exposure", &gs.Exposure, 0.f, 2.f))
         device_data.cb_luma_global_settings_dirty = true;
      if (ImGui::IsItemDeactivatedAfterEdit())
         reshade::set_config_value(nullptr, NAME, "Exposure", gs.Exposure);
      if (ImGui::IsItemHovered())
         ImGui::SetTooltip("Overall image brightness (1 = vanilla).");
      if (DrawResetButton<float, false>(gs.Exposure, gs_def.Exposure, "Exposure"))
      {
         device_data.cb_luma_global_settings_dirty = true;
         reshade::set_config_value(nullptr, NAME, "Exposure", gs.Exposure);
      }

      // Not HDR-gated: the tint lerps this fades live in the vanilla grade tail, so it applies in SDR as well.
      if (ImGui::SliderFloat("Color Grading Intensity", &gs.ColorGradingIntensity, 0.f, 1.f))
         device_data.cb_luma_global_settings_dirty = true;
      if (ImGui::IsItemDeactivatedAfterEdit())
         reshade::set_config_value(nullptr, NAME, "ColorGradingIntensity", gs.ColorGradingIntensity);
      if (ImGui::IsItemHovered())
         ImGui::SetTooltip("Strength of the game's own color grading (1 = vanilla, 0 = neutral).");
      if (DrawResetButton<float, false>(gs.ColorGradingIntensity, gs_def.ColorGradingIntensity, "ColorGradingIntensity"))
      {
         device_data.cb_luma_global_settings_dirty = true;
         reshade::set_config_value(nullptr, NAME, "ColorGradingIntensity", gs.ColorGradingIntensity);
      }

      // Hidden outside HDR: the final grade's SDR branch skips the whole HDR block, so these would be dead
      // controls. Matches the shader's own "DisplayMode == 1" test.
      if (cb_luma_global_settings.DisplayMode == DisplayModeType::HDR)
      {
         if (ImGui::SliderFloat("Contrast", &gs.Contrast, 0.f, 2.f))
            device_data.cb_luma_global_settings_dirty = true;
         if (ImGui::IsItemDeactivatedAfterEdit())
            reshade::set_config_value(nullptr, NAME, "Contrast", gs.Contrast);
         if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Overall image contrast, HDR only (1 = vanilla).");
         if (DrawResetButton<float, false>(gs.Contrast, gs_def.Contrast, "Contrast"))
         {
            device_data.cb_luma_global_settings_dirty = true;
            reshade::set_config_value(nullptr, NAME, "Contrast", gs.Contrast);
         }

         if (ImGui::SliderFloat("Saturation", &gs.Saturation, 0.f, 2.f))
            device_data.cb_luma_global_settings_dirty = true;
         if (ImGui::IsItemDeactivatedAfterEdit())
            reshade::set_config_value(nullptr, NAME, "Saturation", gs.Saturation);
         if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Color saturation, HDR only (1 = vanilla).");
         if (DrawResetButton<float, false>(gs.Saturation, gs_def.Saturation, "Saturation"))
         {
            device_data.cb_luma_global_settings_dirty = true;
            reshade::set_config_value(nullptr, NAME, "Saturation", gs.Saturation);
         }

         if (ImGui::SliderFloat("Highlights Desaturation", &gs.HighlightDechroma, 0.f, 1.f))
            device_data.cb_luma_global_settings_dirty = true;
         if (ImGui::IsItemDeactivatedAfterEdit())
            reshade::set_config_value(nullptr, NAME, "HighlightDechroma", gs.HighlightDechroma);
         if (ImGui::IsItemHovered())
            ImGui::SetTooltip("How soon bright sources fade to neutral white, HDR only (0 = keep color at any brightness).");
         if (DrawResetButton<float, false>(gs.HighlightDechroma, gs_def.HighlightDechroma, "HighlightDechroma"))
         {
            device_data.cb_luma_global_settings_dirty = true;
            reshade::set_config_value(nullptr, NAME, "HighlightDechroma", gs.HighlightDechroma);
         }

#if DEVELOPMENT || TEST
         // Calibration knobs for the vanilla highlight emulation (not persisted, so the defaults above are
         // what ship — see the RestoreHueAndChrominance call in FinalGrade_0xDE5CF9CD.ps_5_0.hlsl).
         if (ImGui::SliderFloat("Vanilla Clip Whitening", &gs.HighlightsHueChroma, 0.f, 1.f, "%.2f"))
            device_data.cb_luma_global_settings_dirty = true;
         if (ImGui::IsItemHovered())
            ImGui::SetTooltip("chrominanceStrength of the vanilla highlight emulation: the fraction of the vanilla clip's own chroma loss that gets reproduced (0.40 = 40% of it, 1 = the vanilla amount exactly, 0 = vanilla hue but our saturation). The only one of the two knobs that can whiten; calibration in FinalGrade_0xDE5CF9CD.ps_5_0.hlsl.");
         if (ImGui::SliderFloat("Vanilla Clip Hue", &gs.HighlightsHueStrength, 0.f, 1.f, "%.2f"))
            device_data.cb_luma_global_settings_dirty = true;
         if (ImGui::IsItemHovered())
            ImGui::SetTooltip("hueStrength of the same call: how much of the vanilla clip's hue angle is adopted (0.8 default). Never changes saturation by itself. Keep it below 1.0 — the hue runs away as the helper's chrominance ratio goes singular; measured margins in FinalGrade_0xDE5CF9CD.ps_5_0.hlsl.");
#endif
      }

      // No "Luma Bloom Enable": the engine's glow is kept and only scaled. Applies in SDR too.
      ImGui::SeparatorText("Bloom");
      if (ImGui::SliderFloat("Bloom Intensity", &gs.BloomIntensity, 0.f, 2.f))
         device_data.cb_luma_global_settings_dirty = true;
      if (ImGui::IsItemDeactivatedAfterEdit())
         reshade::set_config_value(nullptr, NAME, "BloomIntensity", gs.BloomIntensity);
      if (ImGui::IsItemHovered())
         ImGui::SetTooltip("Bloom strength (1 = vanilla, 0 = none).");
      if (DrawResetButton<float, false>(gs.BloomIntensity, gs_def.BloomIntensity, "BloomIntensity"))
      {
         device_data.cb_luma_global_settings_dirty = true;
         reshade::set_config_value(nullptr, NAME, "BloomIntensity", gs.BloomIntensity);
      }

      ImGui::SeparatorText("Ambient Occlusion");
      if (ImGui::Checkbox("XeGTAO Enable", &g_gtao_enable))
         reshade::set_config_value(nullptr, NAME, "GTAOEnable", g_gtao_enable);
      if (ImGui::IsItemHovered())
         ImGui::SetTooltip("Replaces the game's SSAO with XeGTAO (cleaner, more accurate ambient occlusion; requires SSAO enabled in the game's video settings).");
#if DEVELOPMENT || TEST
      ImGui::BeginDisabled(!g_gtao_enable);
      ImGui::SliderFloat("GTAO Final Value Power", &g_gtao_final_value_power, 0.3f, 4.5f, "%.2f");
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("Primary darkness dial (higher = darker AO). Calibrated to 1.0 here: the native HBAO histogram mean is 0.89 against XeGTAO's 0.90.");
      ImGui::SliderFloat("GTAO Depth Scale", &g_gtao_depth_scale, 0.01f, 200.f, "%.2f", ImGuiSliderFlags_Logarithmic);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("viewZ divisor (game depth units -> meters). Stays 1.0 in this game: its depth buffer is already LINEAR view-space metres (measured p50 7.3, max 686).");
      ImGui::SliderFloat("GTAO Radius Override", &g_gtao_radius_override, 0.f, 5.f, "%.3f");
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("0 = shader EFFECT_RADIUS define (0.81, anchored to the native 1.18 m radius from cb4[11]); > 0 overrides it, in metres.");
      ImGui::Combo("GTAO Debug View", &g_gtao_debug_view, "Off\0Depth gradient\0Normals\0AO x8\0Edges\0");
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("Draws diagnostics through the game's AO apply (multiplied into the scene; DEVELOPMENT shader only). Depth gradient dead/flat or normals blocky = wrong input; AO x8 = spot broad over-occlusion.");
      ImGui::EndDisabled();
#endif

      ImGui::SeparatorText("Effects");

      // Vignette lives in the vanilla grade tail, so this applies in SDR as well as HDR.
      if (ImGui::SliderFloat("Vignette Intensity", &gs.VignetteIntensity, 0.f, 1.f))
         device_data.cb_luma_global_settings_dirty = true;
      if (ImGui::IsItemDeactivatedAfterEdit())
         reshade::set_config_value(nullptr, NAME, "VignetteIntensity", gs.VignetteIntensity);
      if (ImGui::IsItemHovered())
         ImGui::SetTooltip("Scales the game's vignette darkening (1 = vanilla, 0 = none).");
      if (DrawResetButton<float, false>(gs.VignetteIntensity, gs_def.VignetteIntensity, "VignetteIntensity"))
      {
         device_data.cb_luma_global_settings_dirty = true;
         reshade::set_config_value(nullptr, NAME, "VignetteIntensity", gs.VignetteIntensity);
      }

      // HDR-path only: PumboAutoHDR self-noops when peak == paper white, which both SDR modes force.
      if (cb_luma_global_settings.DisplayMode == DisplayModeType::HDR)
      {
         bool video_auto_hdr = gs.VideoAutoHDREnable > 0.5f;
         if (ImGui::Checkbox("Video AutoHDR", &video_auto_hdr))
         {
            gs.VideoAutoHDREnable = video_auto_hdr ? 1.f : 0.f;
            device_data.cb_luma_global_settings_dirty = true;
            reshade::set_config_value(nullptr, NAME, "VideoAutoHDREnable", video_auto_hdr);
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
         if (DrawResetButton<float, false>(gs.VideoAutoHDRBoost, gs_def.VideoAutoHDRBoost, "VideoAutoHDRBoost"))
         {
            device_data.cb_luma_global_settings_dirty = true;
            reshade::set_config_value(nullptr, NAME, "VideoAutoHDRBoost", gs.VideoAutoHDRBoost);
         }
         ImGui::EndDisabled();
      }

      // The final grade dithers in gamma either way, so this stays outside the HDR gate above.
      bool dithering = gs.Dithering > 0.5f;
      if (ImGui::Checkbox("Dithering", &dithering))
      {
         gs.Dithering = dithering ? 1.f : 0.f;
         device_data.cb_luma_global_settings_dirty = true;
         reshade::set_config_value(nullptr, NAME, "Dithering", gs.Dithering);
      }
      if (ImGui::IsItemHovered())
         ImGui::SetTooltip("Reduces gradient banding.");

      ImGui::SeparatorText("UI");
      ImGui::Checkbox("Hide Gameplay UI", &g_hide_ui); // Session-only to avoid a confusing HUD-less restart.
      if (ImGui::IsItemHovered())
         ImGui::SetTooltip("Disables the in-game UI.");
   }

   void PrintImGuiAbout() override
   {
      ImGui::PushTextWrapPos(0.f);
      ImGui::Text(
         "Luma for \"The Witcher 2: Assassins of Kings Enhanced Edition\" is developed by DristoforColumb and is open source and free.\n"
         "It adds HDR and replaces the game's FXAA with SMAA and its SSAO with XeGTAO, plus 16x anisotropic filtering.\n"
         "It runs through dgVoodoo2 (DirectX 9 -> 11).\n"
         "Enable SSAO in the game's video settings for XeGTAO to apply; SMAA works either way.\n"
         "Keep the in-game Brightness and Gamma sliders at their defaults.\n"
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
                  "\nDICE (HDR tonemapper)"
                  "\nOklab (hue/chroma restoration)"
                  "\nSMAA (Iryoku)"
                  "\nXeGTAO (Intel)"
                  "\nAMD FidelityFX (RCAS)"
                  "\ndgVoodoo2 (DirectX 9 -> 11 wrapper, required)");
   }
};

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
   if (ul_reason_for_call == DLL_PROCESS_ATTACH)
   {
      Globals::SetGlobals(PROJECT_NAME, "The Witcher 2 Luma mod");
      Globals::DEVELOPMENT_STATE = Globals::ModDevelopmentState::Finished;

      // scRGB fp16 swapchain (the game's backbuffer is b8g8r8x8/b8g8r8a8 8-bit).
      swapchain_format_upgrade_type = TextureFormatUpgradesType::AllowedEnabled;
      swapchain_upgrade_type = SwapchainUpgradeType::scRGB;

      // Exclusive fullscreen -> borderless. force_borderless extends that to LEAVING fullscreen, so the title
      // bar cannot come back after a mode switch or an alt-tab.
      prevent_fullscreen_state = true;
      force_borderless = true;

      // The only resource still clipping the HDR signal is dgVoodoo's swapchain-resolution present-blit
      // intermediate. Upgrade it INDIRECTLY: it is wrapper-internal, and changing its creation format breaks
      // the translator's bookkeeping, giving a black screen even in menus.
      // One format plus the size filters keeps this to a single extra fp16 mirror at 4K; a broad list hit
      // bad_alloc in this 32-bit process. Fullscreen only: that path's intermediate is r8g8b8a8_typeless.
      texture_format_upgrades_type = TextureFormatUpgradesType::AllowedEnabled;
      enable_indirect_texture_format_upgrades = true; // creation-time mirrors, substituted at bind
      enable_chain_indirect_texture_format_upgrades = ChainTextureFormatUpgradesType::DirectDependencies;
      texture_upgrade_formats = {
         reshade::api::format::r8g8b8a8_typeless, // dgVoodoo's present-blit intermediates (narrow list: 32-bit VA budget)
      };
      texture_format_upgrades_2d_size_filters = (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainResolution | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainAspectRatio;

      // AF16x. A sampler census found only MIN_MAG_MIP_LINEAR (0x15, world), MIN_MAG_LINEAR_MIP_POINT (0x14,
      // post/UI) and MIN_MAG_MIP_POINT, not one anisotropic sampler: the game has no AF option. Mode 4 alone is
      // therefore a no-op, and force_upgrade_linear_samplers is what buys AF by promoting the trilinear class.
      // The census also reports MipLODBias 0.000 everywhere, so there is no negative bias to clamp.
      enable_samplers_upgrade = true; // boot-time only (cannot change after device creation)
      samplers_upgrade_mode = 4;
      force_upgrade_linear_samplers = true;

      game = new TheWitcher2Game();
   }

   CoreMain(hModule, ul_reason_for_call, lpReserved);

   return TRUE;
}
