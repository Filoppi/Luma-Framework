// Borderlands 2 + The Pre-Sequel — Luma HDR + SMAA mod (Unreal Engine 3, native DX9 -> D3D11 via dgVoodoo2).
//
// DX9 game, DX11-only Luma -> dgVoodoo2 translates SM3.0 to ps_5_0, so CSO hashes differ from the native DX9 ones
// (re-dumped via devkit). Launch the game exe DIRECTLY: the XNA Launcher.exe also loads d3d9 and would capture
// ReShade instead of the game. One shared addon serves both games (tonemap hash = discriminator).
//
// Pipeline (devkit-verified, see NOTES.md):
// - TONEMAP PS 0xD00AA2A7 (BL2) / 0xFCFE623E (TPS): scene fp16 + bloom + vignette + LUT + DOF -> 8-bit LDR.
//   Replaced to recover HDR (DICE + paper-white); UI composites AFTER, on the LDR.
// - FXAA PS 0x0D3001F6 (only when in-game AA on) -> replaced with SMAA ULTRA + optional RCAS.
//
// All SDR/gamma space. One HDR mod owns the swapchain -> only ONE Luma .addon, dgVoodoo & ReShade both 32-bit
// (other ReShade addons that also hook the swapchain crash via dgVoodoo -> keep disabled in normal use).

// Don't pop the DEVELOPMENT auto-debugger MessageBox on DLL attach: under a borderless/fullscreen game it's
// invisible and blocks the loader (ReShade times out the addon load -> error 1114).
#define DISABLE_AUTO_DEBUGGER 1

#define ENABLE_NGX 0 // no DLSS/DLAA: UE3 has no usable motion vectors, and NGX is x64-only (game is 32-bit)
#define ENABLE_FIDELITY_SK 0
#define GEOMETRY_SHADER_SUPPORT 0
#define ENABLE_SMAA 1  // replaces the FXAA resolve PS with SMAA ULTRA (+RCAS); core auto-registers the 6 "SMAA ..." passes from Luma_SMAA_impl
#define ENABLE_BLOOM 1 // core auto-registers the Bloom VS/Prefilter/Downsample/Upsample passes -> Luma_Bloom_impl
// SMAA runs POST-tonemap (on the LDR output) via the post-draw callback, so it can't perturb the game's DoF
// (composited INSIDE the tonemap). Needs original_draw_dispatch_func non-null.
#define ENABLE_POST_DRAW_DISPATCH_CALLBACK 1

#include "..\..\Core\core.hpp"
#include <shellapi.h> // ShellExecuteA for About links (system("start ...") hangs the render thread in exclusive fullscreen)

// FXAA resolve PS (only present when AA is enabled in the game's video settings) — replaced with SMAA. RE in NOTES.
static constexpr uint32_t kFXAAResolveHash = 0x0D3001F6;
static constexpr uint32_t kTonemapHash = 0xD00AA2A7;    // BL2: writes the LDR buffer the HUD then draws onto
static constexpr uint32_t kTonemapHashTPS = 0xFCFE623E; // The Pre-Sequel: same engine, different tonemap CSO (one addon serves both)

// Luma-injected SRV slots on the tonemap. These MUST match the shader register macros in
// Tonemap_0xD00AA2A7.ps_5_0.hlsl (BL2) + Tonemap_0xFCFE623E.ps_5_0.hlsl (TPS) — there is no compile-time link,
// so keep them in sync if a slot is ever re-RE'd:
//   bloom -> TM_T_LUMABLOOM : BL2 t5 / TPS t8 (TPS t5 is the native DOF)
//   DoF   -> register(t7)   : t7 on both (free on BL2 & TPS)
static constexpr uint32_t kLumaBloomSlotBL2 = 5;
static constexpr uint32_t kLumaBloomSlotTPS = 8;
static constexpr uint32_t kLumaDoFSlot = 7;

// User settings (persisted via ReShade config under the project name; loaded in LoadConfigs).
static bool g_smaa_enable = true;
static float g_rcas_sharpness = 0.f;                                   // RCAS sharpen on SMAA output (0 = off)
static bool g_hide_ui = false;                                         // hide the game's HUD (for clean screenshots)
static bool g_smaa_predication = true;                                 // SMAA depth predication (depth from scene-color .a)
static float g_smaa_pred_k = 1000.f;                                   // predication depth compress (world units): D=z/(z+k); k=1000 measured optimum (~100% far-silhouette recall, ~0% flat false-fire @4K; plateau past 1000)
static bool g_luma_bloom_enable = true;                                // replace the game's clamped bloom with Luma HDR pyramidal bloom (live toggle)
static int g_bloom_nmips = 6;                                          // bloom pyramid mip count
static float g_bloom_sigmas[6] = {1.5f, 2.0f, 2.0f, 2.0f, 1.0f, 1.0f}; // per-mip Gaussian sigma (tapered, wider middle for a soft natural halo)
static int g_dof_type = 2;                                             // DoF path (live): 0 = vanilla game DoF, 2 = Luma separable Gaussian (default, UI checkbox)
static float g_dof_radius = 9.f;                                       // DoF strength (full-res px @ 4K Gaussian blur extent). Default, users tune

struct Borderlands2GameDeviceData final : public GameDeviceData
{

   // SMAA metrics CB (b1) = (1/w,1/h,w,h) + (predication scale,0,0,0); scale 2.0 when predication on, else 1.0.
   ComPtr<ID3D11Buffer> cb_smaa_metrics;
   uint32_t smaa_metrics_w = 0, smaa_metrics_h = 0;

   uint32_t smaa_core_w = 0, smaa_core_h = 0;

   // SMAA scratch. tex_input = SRV snapshot of the LDR (it's already gamma, fed to both DrawSMAA color args directly).
   ComPtr<ID3D11Texture2D> tex_input;
   ComPtr<ID3D11ShaderResourceView> srv_input;
   uint32_t smaa_temps_w = 0, smaa_temps_h = 0;

   // SMAA output temp (SRV+RTV). Copied back into the LDR (or via RCAS first).
   ComPtr<ID3D11Texture2D> tex_smaa_out;
   ComPtr<ID3D11RenderTargetView> tex_smaa_out_rtv;
   ComPtr<ID3D11ShaderResourceView> tex_smaa_out_srv;
   uint32_t smaa_out_w = 0, smaa_out_h = 0;

   // RCAS sharpen CB (b0) = (w,h,sharpness,0) + output temp (fp16, RTV).
   ComPtr<ID3D11Buffer> cb_sharpen;
   uint32_t sharpen_w = 0, sharpen_h = 0;
   float sharpen_amount = -1.f;
   ComPtr<ID3D11Texture2D> tex_rcas_out;
   ComPtr<ID3D11RenderTargetView> tex_rcas_out_rtv;
   uint32_t rcas_out_w = 0, rcas_out_h = 0;

   // Resource the tonemap (0xD00AA2A7) renders to; on BL2 the HUD draws onto it afterwards. Used by Hide UI.
   uint64_t ldr_buffer_handle = 0;
   // Set when the tonemap runs, cleared every Present: scopes Hide UI's alpha-blend skip to the post-tonemap
   // span of THIS frame (so next frame's pre-tonemap transparents aren't dropped). See the Hide HUD block.
   bool tonemap_fired_this_frame = false;

   // SMAA depth predication. Scene-color SRV (depth packed in .a) captured at the tonemap, + a normalized
   // single-channel (R16F) predication depth produced by the BL2TPS Depth Extract CS.
   ComPtr<ID3D11ShaderResourceView> srv_scene_depth;
   ComPtr<ID3D11Texture2D> tex_pred;
   ComPtr<ID3D11UnorderedAccessView> uav_pred;
   ComPtr<ID3D11ShaderResourceView> srv_pred;
   uint32_t pred_w = 0, pred_h = 0;
   ComPtr<ID3D11Buffer> cb_pred;
   float pred_k = -1.f;
   float smaa_metrics_pred_scale = -1.f; // recreate the metrics CB when predication turns on/off

   // Luma HDR pyramidal bloom output (linear fp16), generated at the tonemap from the scene SRV, bound to PS t5 (BL2) / t8 (TPS).
   ComPtr<ID3D11ShaderResourceView> srv_luma_bloom;

   // Half-res Karis-prefiltered scene for the Gaussian DoF blur (area-averaged source -> no under-sampling
   // grain on sharp/bright detail). Cached per render resolution, bound at tonemap PS t7.
   ComPtr<ID3D11Texture2D> tex_dof_prefilter; // RGBA16F
   ComPtr<ID3D11ShaderResourceView> srv_dof_prefilter;
   ComPtr<ID3D11UnorderedAccessView> uav_dof_prefilter;
   uint32_t dof_pf_w = 0, dof_pf_h = 0;

   // "Luma Gaussian" DoF: separable blur of the half-res prefilter (H -> blur_h, V -> blur_out). blur_out is
   // bound at tonemap PS t7 in Gaussian mode. Cached per render resolution; CBs recreated when the radius changes.
   ComPtr<ID3D11Texture2D> tex_dof_blur_h, tex_dof_blur_out; // RGBA16F, half render resolution
   ComPtr<ID3D11ShaderResourceView> srv_dof_blur_h, srv_dof_blur_out;
   ComPtr<ID3D11UnorderedAccessView> uav_dof_blur_h, uav_dof_blur_out;
   ComPtr<ID3D11Buffer> cb_dof_blur_h, cb_dof_blur_v; // cb_dof_blur (b0): axis-step + sigma, per pass
   float dof_blur_radius = -1.f;                      // recreate the CBs when the DoF Radius slider changes
};

class Borderlands2 final : public Game
{
   static Borderlands2GameDeviceData& GetGameDeviceData(DeviceData& device_data)
   {
      return *static_cast<Borderlands2GameDeviceData*>(device_data.game);
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

   // Default-pool 2D texture. Format defaults to fp16 (bloom/scene callers); pass the live LDR 8-bit format for
   // SMAA color temps, since CopyResource requires identical formats.
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

#if ENABLE_SMAA
   // Post-tonemap SMAA on the LDR (gamma space). Runs AFTER the tonemap so it can't perturb the DoF (composited
   // inside the tonemap; vanilla == SMAA-off, devkit-verified). Snapshot LDR -> SRV (already gamma; fed to both
   // DrawSMAA color args, no color-prep) -> DrawSMAA -> optional RCAS -> copy back into the LDR. No-op if not ready.
   void RunPostTonemapSMAA(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, DeviceData& device_data, Borderlands2GameDeviceData& gd, ID3D11Resource* ldr_res)
   {
      uint4 cinfo{};
      DXGI_FORMAT cfmt = DXGI_FORMAT_UNKNOWN;
      GetResourceInfo(ldr_res, cinfo, cfmt);
      uint32_t w = cinfo.x, h = cinfo.y;
      if (w == 0 || h == 0 || (uint32_t)cfmt == (uint32_t)DXGI_FORMAT_UNKNOWN)
      {
         return;
      }

      // Shader-readiness gate (async loader / dev live-reload): skip SMAA this frame if anything is missing.
      const bool smaa_ready =
         device_data.native_pixel_shaders[CompileTimeStringHash("SMAA Edge Detection PS")].get() != nullptr &&
         device_data.native_pixel_shaders[CompileTimeStringHash("SMAA Blending Weight Calculation PS")].get() != nullptr &&
         device_data.native_pixel_shaders[CompileTimeStringHash("SMAA Neighborhood Blending PS")].get() != nullptr &&
         device_data.native_vertex_shaders[CompileTimeStringHash("SMAA Edge Detection VS")].get() != nullptr &&
         device_data.native_vertex_shaders[CompileTimeStringHash("SMAA Blending Weight Calculation VS")].get() != nullptr &&
         device_data.native_vertex_shaders[CompileTimeStringHash("SMAA Neighborhood Blending VS")].get() != nullptr;
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

      // SMAA depth predication: normalized-depth from the captured scene-color SRV (.a). Plain ULTRA fallback when
      // any input is missing (never scale 2.0 with a null texture).
      const bool pred_cs_ready = device_data.native_compute_shaders[CompileTimeStringHash("BL2TPS Depth Extract CS")].get() != nullptr;
      bool pred_ok = g_smaa_predication && gd.srv_scene_depth && pred_cs_ready;
      if (pred_ok)
      {
         if (!gd.cb_pred || gd.pred_k != g_smaa_pred_k)
         {
            const float p[4] = {g_smaa_pred_k, 0.f, 0.f, 0.f};
            if (CreateImmutableCB(native_device, p, sizeof(p), gd.cb_pred))
               gd.pred_k = g_smaa_pred_k;
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

      // SMAA output temp (LDR format, SRV+RTV).
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
      if (!gd.tex_smaa_out_rtv || !gd.tex_smaa_out_srv)
         return;

      // tex_input = SRV-readable temp for the LDR (already gamma -> feeds both DrawSMAA color args, no color-prep).
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

      // Snapshot the LDR color into an SRV-readable temp (LDR is both the SMAA input and the write-back target).
      native_device_context->CopyResource(gd.tex_input.get(), ldr_res);

      // Predication depth extract: scene-color .a -> normalized R16F (gd.tex_pred). Independent of the LDR snapshot.
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
         native_device_context->CSSetShader(device_data.native_compute_shaders[CompileTimeStringHash("BL2TPS Depth Extract CS")].get(), nullptr, 0);
         native_device_context->Dispatch((w + 7) / 8, (h + 7) / 8, 1);

         pred_cs_state.Restore(native_device_context);
      }

      // SMAA (3 passes) -> tex_smaa_out. Metrics CB at VS+PS b1 (DrawSMAA restores VS/PS/SRVs/RTs, not cbuffers).
      ComPtr<ID3D11Buffer> vs_cb1_orig, ps_cb1_orig;
      native_device_context->VSGetConstantBuffers(1, 1, vs_cb1_orig.put());
      native_device_context->PSGetConstantBuffers(1, 1, ps_cb1_orig.put());
      ID3D11Buffer* mcb = gd.cb_smaa_metrics.get();
      native_device_context->VSSetConstantBuffers(1, 1, &mcb);
      native_device_context->PSSetConstantBuffers(1, 1, &mcb);

      DrawSMAA(native_device, native_device_context, device_data,
         gd.tex_smaa_out_rtv.get(), gd.srv_input.get(), gd.srv_input.get(),
         pred_ok ? gd.srv_pred.get() : nullptr /*predication depth (scene .a)*/);

      // Optional RCAS sharpen on the SMAA output, then copy into the LDR target.
      const bool sharpen_ready =
         device_data.native_vertex_shaders[CompileTimeStringHash("Copy VS")].get() != nullptr &&
         device_data.native_pixel_shaders[CompileTimeStringHash("BL2TPS Sharpen PS")].get() != nullptr;
      bool do_sharpen = g_rcas_sharpness > 0.f && sharpen_ready;
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
         if (!gd.tex_rcas_out || gd.rcas_out_w != w || gd.rcas_out_h != h)
         {
            gd.tex_rcas_out_rtv.reset();
            gd.tex_rcas_out.reset();
            if (CreateDefaultTex(native_device, w, h, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET, gd.tex_rcas_out, cfmt))
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
         auto* sharpen_ps = device_data.native_pixel_shaders[CompileTimeStringHash("BL2TPS Sharpen PS")].get();
         DrawStateStack<DrawStateStackType::FullGraphics> sharpen_state;
         sharpen_state.Cache(native_device_context, device_data.uav_max_count);

         ID3D11Buffer* scb = gd.cb_sharpen.get();
         native_device_context->PSSetConstantBuffers(0, 1, &scb);
         DrawCustomPixelShader(native_device_context, device_data.default_depth_stencil_state.get(), device_data.default_blend_state.get(), nullptr,
            sharpen_vs, sharpen_ps, gd.tex_smaa_out_srv.get(), gd.tex_rcas_out_rtv.get(), w, h, false);

         sharpen_state.Restore(native_device_context);
         native_device_context->CopyResource(ldr_res, gd.tex_rcas_out.get());
      }
      else
      {
         native_device_context->CopyResource(ldr_res, gd.tex_smaa_out.get());
      }

      ID3D11Buffer* vcb = vs_cb1_orig.get();
      ID3D11Buffer* pcb = ps_cb1_orig.get();
      native_device_context->VSSetConstantBuffers(1, 1, &vcb);
      native_device_context->PSSetConstantBuffers(1, 1, &pcb);
   }
#endif // ENABLE_SMAA

public:
   void OnInit(bool async) override
   {
      // UE3 is all SDR (UNORM) gamma space: post buffers stay GAMMA so the
      // gamma-SDR HUD blends on top like vanilla. The tonemap pre-scales the scene by GamePaperWhite/UIPaperWhite
      // (UI_DRAW_TYPE 2) so the HUD lands at its own UIPaperWhite; the composition decodes gamma + paper white + scRGB.
      GetShaderDefineData(POST_PROCESS_SPACE_TYPE_HASH).SetDefaultValue('0');
      GetShaderDefineData(EARLY_DISPLAY_ENCODING_HASH).SetDefaultValue('0');
      GetShaderDefineData(VANILLA_ENCODING_TYPE_HASH).SetDefaultValue('1'); // Gamma 2.2 in and out
      GetShaderDefineData(GAMMA_CORRECTION_TYPE_HASH).SetDefaultValue('1');
      GetShaderDefineData(GAMUT_MAPPING_TYPE_HASH).SetDefaultValue('1'); // gamut-map wild colors in composition
      GetShaderDefineData(UI_DRAW_TYPE_HASH).SetDefaultValue('2');       // HUD gets its own UIPaperWhite + gamma blend

      // Manual Scene + UI Paper White sliders instead of the OS HDR reference level. Core gates the separate
      // "UI Paper White" slider on UI_DRAW_TYPE >= 1 && !use_os_reference_white_level. UI default 203 nits (BT.2408).
      use_os_reference_white_level = false;

      // The 6 SMAA passes are auto-registered by core when ENABLE_SMAA. Our tonemap outputs GAMMA space and the LDR
      // snapshot is fed directly to both DrawSMAA color args (no color-prep CS): edge-detect on the gamma copy +
      // neighborhood-blend on the color copy, both the same gamma LDR here (keeps thin features, matches FXAA).
      // RCAS sharpen PS (drawn via core "Copy VS" + DrawCustomPixelShader after SMAA).
      native_shaders_definitions.emplace(CompileTimeStringHash("BL2TPS Sharpen PS"),
         ShaderDefinition{"Luma_BL2TPS_Sharpen", reshade::api::pipeline_subobject_type::pixel_shader, nullptr, "sharpen_ps"});
      // Depth-extract CS for SMAA predication: scene-color .a (linear view Z) -> normalized R16F predication depth.
      native_shaders_definitions.emplace(CompileTimeStringHash("BL2TPS Depth Extract CS"),
         ShaderDefinition("Luma_BL2TPS_DepthExtract", reshade::api::pipeline_subobject_type::compute_shader));

      // Gaussian DoF prefilter: half-res Karis-tamed scene downsample that the separable blur runs on (an
      // averaged-area source, not full-res point samples -> removes under-sampling grain/sparkle).
      native_shaders_definitions.emplace(CompileTimeStringHash("BL2TPS DoF Prefilter CS"),
         ShaderDefinition{"Luma_BL2TPS_DoFPrefilter", reshade::api::pipeline_subobject_type::compute_shader, nullptr, "dof_prefilter_cs"});
      // "Luma Gaussian" DoF: separable blur of the prefiltered half-res scene (dispatched H then V).
      native_shaders_definitions.emplace(CompileTimeStringHash("BL2TPS DoF Gaussian Blur CS"),
         ShaderDefinition{"Luma_BL2TPS_DoFGaussian", reshade::api::pipeline_subobject_type::compute_shader, nullptr, "dof_blur_cs"});

      // The game's post passes use cb0..cb5; b12/b13 are free for Luma (measured via devkit).
      luma_settings_cbuffer_index = 13;
      luma_data_cbuffer_index = 12;

      // User HDR grade controls (read in Tonemap_0xD00AA2A7.ps_5_0.hlsl via LumaSettings.GameSettings). All
      // default to a vanilla no-op. Exposure/Bloom/Vignette act on both SDR+HDR; Saturation/Dechroma/Contrast HDR-only.
      default_luma_global_game_settings.Exposure = 1.f;          // scene multiplier (1x)
      default_luma_global_game_settings.Saturation = 1.f;        // Oklab saturation
      default_luma_global_game_settings.HighlightDechroma = 0.f; // off; only mandatory DICE/gamut desat applies
      default_luma_global_game_settings.BloomIntensity = 1.f;    // Luma HDR bloom strength (additive)
      default_luma_global_game_settings.Contrast = 1.f;          // slope around 18% mid-gray
      default_luma_global_game_settings.VignetteIntensity = 1.f; // game vignette darkening scale
      default_luma_global_game_settings.LumaBloomEnable = 1.f;   // 1 = Luma HDR pyramidal bloom, 0 = vanilla game bloom
      default_luma_global_game_settings.DOFRadius = 9.f;         // DoF strength (px @ 4K); = vanilla DoF peak (sigma ~1.98 half-res px)
      default_luma_global_game_settings.DOFType = 2.f;           // 0 = vanilla game DoF, 2 = Luma separable Gaussian (default)
      default_luma_global_game_settings.Dithering = 1.f;         // animated triangular dither at output (HDR), anti-banding on
      cb_luma_global_settings.GameSettings = default_luma_global_game_settings;
   }

   void OnCreateDevice(ID3D11Device* native_device, DeviceData& device_data) override
   {
      device_data.game = new Borderlands2GameDeviceData;
   }

   void OnDestroyDeviceData(DeviceData& device_data) override
   {
      if (device_data.game)
      {
         auto& gd = GetGameDeviceData(device_data);
         gd.cb_smaa_metrics.reset();
         gd.srv_input.reset();
         gd.tex_input.reset();
         gd.tex_smaa_out.reset();
         gd.tex_smaa_out_rtv.reset();
         gd.tex_smaa_out_srv.reset();
         gd.cb_sharpen.reset();
         gd.tex_rcas_out.reset();
         gd.tex_rcas_out_rtv.reset();
         gd.srv_luma_bloom.reset();
      }
      delete device_data.game;
      device_data.game = nullptr;
   }

   // DoF prefilter: downsample the scene to a half-res, Karis-firefly-tamed buffer (gd.srv_dof_prefilter)
   // that the separable Gaussian blur runs on instead of full-res t0 -> each tap is an averaged area, killing the
   // under-sampling grain on sharp/bright detail. Resources cached per render resolution.
   void RunDoFPrefilter(ID3D11Device* device, ID3D11DeviceContext* ctx, DeviceData& device_data, Borderlands2GameDeviceData& gd, ID3D11ShaderResourceView* scene_srv)
   {
      if (!scene_srv)
         return;

      // Size from the bound scene resource (NOT output_resolution) so the half-res stays aligned to the t7 sample if
      // the scene ever differs from the swapchain (UE3 ScreenPercentage < 100). Mirrors bloom/SMAA self-sizing.
      ComPtr<ID3D11Resource> scene_res;
      scene_srv->GetResource(scene_res.put());
      ComPtr<ID3D11Texture2D> scene_tex;
      if (!scene_res || FAILED(scene_res->QueryInterface(IID_PPV_ARGS(scene_tex.put()))))
         return;
      D3D11_TEXTURE2D_DESC sd = {};
      scene_tex->GetDesc(&sd);
      const uint32_t w = sd.Width, h = sd.Height;
      if (w == 0 || h == 0)
         return;

      const uint32_t hw = (w + 1) / 2, hh = (h + 1) / 2; // half render resolution
      if (gd.dof_pf_w != hw || gd.dof_pf_h != hh)
      {
         gd.tex_dof_prefilter.reset();
         gd.srv_dof_prefilter.reset();
         gd.uav_dof_prefilter.reset();
         gd.dof_pf_w = 0;
         gd.dof_pf_h = 0;

         D3D11_TEXTURE2D_DESC d = {};
         d.Width = hw;
         d.Height = hh;
         d.MipLevels = 1;
         d.ArraySize = 1;
         d.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
         d.SampleDesc.Count = 1;
         d.Usage = D3D11_USAGE_DEFAULT;
         d.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
         if (FAILED(device->CreateTexture2D(&d, nullptr, gd.tex_dof_prefilter.put())))
            return;
         device->CreateShaderResourceView(gd.tex_dof_prefilter.get(), nullptr, gd.srv_dof_prefilter.put());
         device->CreateUnorderedAccessView(gd.tex_dof_prefilter.get(), nullptr, gd.uav_dof_prefilter.put());
         gd.dof_pf_w = hw;
         gd.dof_pf_h = hh;
      }
      if (!gd.srv_dof_prefilter || !gd.uav_dof_prefilter)
         return;

      ID3D11ComputeShader* cs_pf = device_data.native_compute_shaders[CompileTimeStringHash("BL2TPS DoF Prefilter CS")].get();
      if (!cs_pf)
         return;

      DrawStateStack<DrawStateStackType::Compute> pf_state;
      pf_state.Cache(ctx, device_data.uav_max_count);

      ID3D11ShaderResourceView* srv_null[1] = {};
      ID3D11UnorderedAccessView* uav_null[1] = {};
      ID3D11ShaderResourceView* src = scene_srv;
      ID3D11UnorderedAccessView* uav = gd.uav_dof_prefilter.get();
      ctx->CSSetShader(cs_pf, nullptr, 0);
      ctx->CSSetShaderResources(0, 1, &src);
      ctx->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
      ctx->Dispatch((hw + 7) / 8, (hh + 7) / 8, 1);
      ctx->CSSetShaderResources(0, 1, srv_null);
      ctx->CSSetUnorderedAccessViews(0, 1, uav_null, nullptr);

      pf_state.Restore(ctx);
   }

   // "Luma Gaussian" DoF: downsample (RunDoFPrefilter) then a separable Gaussian blur (H then V) of the half-res
   // scene, leaving the blurred half-res in gd.srv_dof_blur_out for the tonemap to sample (one tap) and blend by
   // the game's per-pixel focus weight. Dense blur -> grain-free.
   void RunDoFGaussian(ID3D11Device* device, ID3D11DeviceContext* ctx, DeviceData& device_data, Borderlands2GameDeviceData& gd, ID3D11ShaderResourceView* scene_srv)
   {
      if (!scene_srv)
         return;

      // 1. Half-res Karis-tamed source (sizes itself from scene_srv; also sets gd.dof_pf_w/h + gd.srv_dof_prefilter).
      RunDoFPrefilter(device, ctx, device_data, gd, scene_srv);
      if (!gd.srv_dof_prefilter)
         return;
      const uint32_t hw = gd.dof_pf_w, hh = gd.dof_pf_h;
      if (hw == 0 || hh == 0)
         return;

      // 2. (Re)create the half-res ping-pong blur targets on size change.
      D3D11_TEXTURE2D_DESC cur = {};
      if (gd.tex_dof_blur_out)
         gd.tex_dof_blur_out->GetDesc(&cur);
      const bool size_changed = (!gd.tex_dof_blur_out || cur.Width != hw || cur.Height != hh);
      if (size_changed)
      {
         gd.tex_dof_blur_h.reset();
         gd.srv_dof_blur_h.reset();
         gd.uav_dof_blur_h.reset();
         gd.tex_dof_blur_out.reset();
         gd.srv_dof_blur_out.reset();
         gd.uav_dof_blur_out.reset();
         auto make = [&](ComPtr<ID3D11Texture2D>& t, ComPtr<ID3D11ShaderResourceView>& s, ComPtr<ID3D11UnorderedAccessView>& u) -> bool
         {
            D3D11_TEXTURE2D_DESC d = {};
            d.Width = hw;
            d.Height = hh;
            d.MipLevels = 1;
            d.ArraySize = 1;
            d.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
            d.SampleDesc.Count = 1;
            d.Usage = D3D11_USAGE_DEFAULT;
            d.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
            if (FAILED(device->CreateTexture2D(&d, nullptr, t.put())))
               return false;
            device->CreateShaderResourceView(t.get(), nullptr, s.put());
            device->CreateUnorderedAccessView(t.get(), nullptr, u.put());
            return true;
         };
         if (!make(gd.tex_dof_blur_h, gd.srv_dof_blur_h, gd.uav_dof_blur_h))
            return;
         if (!make(gd.tex_dof_blur_out, gd.srv_dof_blur_out, gd.uav_dof_blur_out))
            return;
      }

      // 3. (Re)create the per-axis CBs (axis-step + sigma) on size or radius change.
      if (size_changed || !gd.cb_dof_blur_h || !gd.cb_dof_blur_v || gd.dof_blur_radius != g_dof_radius)
      {
         const float sigma = g_dof_radius * 0.22f; // half-res sigma; radius 9 = vanilla DoF peak (sigma ~1.98 half-res px @ 4K, R2 0.98)
         const float ph[4] = {1.0f / (float)hw, 0.f, sigma, 0.f};
         const float pv[4] = {0.f, 1.0f / (float)hh, sigma, 0.f};
         gd.cb_dof_blur_h.reset();
         gd.cb_dof_blur_v.reset();
         if (!CreateImmutableCB(device, ph, sizeof(ph), gd.cb_dof_blur_h))
            return;
         if (!CreateImmutableCB(device, pv, sizeof(pv), gd.cb_dof_blur_v))
            return;
         gd.dof_blur_radius = g_dof_radius;
      }

      ID3D11ComputeShader* cs_blur = device_data.native_compute_shaders[CompileTimeStringHash("BL2TPS DoF Gaussian Blur CS")].get();
      if (!cs_blur)
         return;

      DrawStateStack<DrawStateStackType::Compute> blur_state;
      blur_state.Cache(ctx, device_data.uav_max_count);

      ID3D11SamplerState* lin = device_data.sampler_state_linear.get();
      ctx->CSSetSamplers(0, 1, &lin);
      ctx->CSSetShader(cs_blur, nullptr, 0);
      ID3D11ShaderResourceView* srv_null[1] = {};
      ID3D11UnorderedAccessView* uav_null[1] = {};
      const UINT gx = (hw + 7) / 8, gy = (hh + 7) / 8;

      // H pass: prefilter -> blur_h
      ID3D11Buffer* cbh = gd.cb_dof_blur_h.get();
      ctx->CSSetConstantBuffers(0, 1, &cbh);
      ID3D11ShaderResourceView* s_pf = gd.srv_dof_prefilter.get();
      ID3D11UnorderedAccessView* u_h = gd.uav_dof_blur_h.get();
      ctx->CSSetShaderResources(0, 1, &s_pf);
      ctx->CSSetUnorderedAccessViews(0, 1, &u_h, nullptr);
      ctx->Dispatch(gx, gy, 1);
      ctx->CSSetShaderResources(0, 1, srv_null);
      ctx->CSSetUnorderedAccessViews(0, 1, uav_null, nullptr);

      // V pass: blur_h -> blur_out (bound at tonemap t7 in Gaussian mode)
      ID3D11Buffer* cbv = gd.cb_dof_blur_v.get();
      ctx->CSSetConstantBuffers(0, 1, &cbv);
      ID3D11ShaderResourceView* s_h = gd.srv_dof_blur_h.get();
      ID3D11UnorderedAccessView* u_o = gd.uav_dof_blur_out.get();
      ctx->CSSetShaderResources(0, 1, &s_h);
      ctx->CSSetUnorderedAccessViews(0, 1, &u_o, nullptr);
      ctx->Dispatch(gx, gy, 1);
      ctx->CSSetShaderResources(0, 1, srv_null);
      ctx->CSSetUnorderedAccessViews(0, 1, uav_null, nullptr);

      blur_state.Restore(ctx);
   }

   DrawOrDispatchOverrideType OnDrawOrDispatch(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, reshade::api::shader_stage stages, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, bool is_custom_pass, bool& updated_cbuffers, std::function<void()>* original_draw_dispatch_func) override
   {
      auto& gd = GetGameDeviceData(device_data);
      const bool is_immediate = native_device_context->GetType() == D3D11_DEVICE_CONTEXT_IMMEDIATE;

      // Track the LDR buffer (the tonemap's render target). The HUD draws onto it afterwards; the Hide UI
      // toggle uses this to recognize (and drop) those HUD draws.
      if (is_immediate && (original_shader_hashes.Contains(kTonemapHash, reshade::api::shader_stage::pixel) || original_shader_hashes.Contains(kTonemapHashTPS, reshade::api::shader_stage::pixel)))
      {
         // The Pre-Sequel's tonemap inserts a LightShaftTexture at slot 1, shifting its native textures down one
         // (LUT@t4, DOF@t5) vs BL2. The replaced tonemap shader (Tonemap_0xFCFE623E) compensates in its register
         // map; the only C++ consequence is the injected Luma bloom must move off t5 (TPS's native DOF) to t8.
         const bool is_tps = original_shader_hashes.Contains(kTonemapHashTPS, reshade::api::shader_stage::pixel);
         gd.tonemap_fired_this_frame = true; // Hide UI scopes its post-tonemap alpha-blend skip to this span
         ComPtr<ID3D11RenderTargetView> rtv;
         native_device_context->OMGetRenderTargets(1, rtv.put(), nullptr);
         if (rtv)
         {
            ComPtr<ID3D11Resource> res;
            rtv->GetResource(res.put());
            if (res)
               gd.ldr_buffer_handle = (uint64_t)res.get();
         }
         // Capture the scene-color SRV (PS t0) for SMAA predication: its .a holds linear view-space depth. The
         // tonemap reads (doesn't overwrite) this buffer, so .a is still valid when SMAA runs later this frame.
         ComPtr<ID3D11ShaderResourceView> scene_srv;
         native_device_context->PSGetShaderResources(0, 1, scene_srv.put());
         if (scene_srv)
            gd.srv_scene_depth = scene_srv;

#if ENABLE_BLOOM
         // Luma HDR pyramidal bloom from the fp16 scene (tonemap t0), bound at PS t5 (BL2) / t8 (TPS) for the
         // replaced tonemap to composite. Karis (firefly cut) then multi-mip pyramid, in a full-graphics state
         // stack so DrawBloom restores the tonemap's RT/PS/SRVs. Tonemap ignores native bloom t1 here -> no double.
         if (g_luma_bloom_enable && scene_srv)
         {
            DrawStateStack<DrawStateStackType::FullGraphics> bloom_state;
            bloom_state.Cache(native_device_context, device_data.uav_max_count);

            ComPtr<ID3D11ShaderResourceView> srv_karis;
            DrawKarisAverage(native_device, native_device_context, device_data, scene_srv.get(), srv_karis.put());
            if (srv_karis)
               DrawBloom(native_device, native_device_context, device_data, srv_karis.get(), g_bloom_nmips, g_bloom_sigmas, gd.srv_luma_bloom.put());

            bloom_state.Restore(native_device_context);

            if (gd.srv_luma_bloom)
            {
               ID3D11ShaderResourceView* b = gd.srv_luma_bloom.get();
               native_device_context->PSSetShaderResources(is_tps ? kLumaBloomSlotTPS : kLumaBloomSlotBL2, 1, &b);
            }
         }
#endif

         // DoF source for the tonemap, by type (DOFType selects the tonemap branch): Gaussian builds the half-res
         // Karis prefilter, separable-blurs it, and binds the blurred half-res at PS t7 (single tap); Vanilla binds
         // nothing (the tonemap reads the game's t4).
         {
            const float dof_type_f = (float)g_dof_type;
            if (cb_luma_global_settings.GameSettings.DOFType != dof_type_f)
            {
               cb_luma_global_settings.GameSettings.DOFType = dof_type_f;
               device_data.cb_luma_global_settings_dirty = true;
            }
            if (scene_srv && g_dof_type == 2) // Luma separable Gaussian
            {
               RunDoFGaussian(native_device, native_device_context, device_data, gd, scene_srv.get());
               if (gd.srv_dof_blur_out)
               {
                  ID3D11ShaderResourceView* bl = gd.srv_dof_blur_out.get();
                  native_device_context->PSSetShaderResources(kLumaDoFSlot, 1, &bl);
               }
            }
         }

#if ENABLE_SMAA
         // SMAA runs POST-tonemap (can't perturb the DoF): manually run the original tonemap draw, then SMAA on its
         // LDR output. Native FXAA left untouched -> DoF vanilla-identical. Falls back to a normal draw if no callback.
         if (g_smaa_enable && original_draw_dispatch_func != nullptr)
         {
            // Returning Replaced short-circuits core's per-pass SetLumaConstantBuffers (core.hpp), so upload the
            // (possibly dirty) grade/DOFType CB ourselves before running the tonemap draw - else it samples
            // last-present's values (1-frame lag on grade sliders / DoF toggle). The standard fleet custom-pass idiom.
            SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, reshade::api::shader_stage::pixel, LumaConstantBufferType::LumaSettings);
            updated_cbuffers = true;
            (*original_draw_dispatch_func)();
            if (rtv)
            {
               ComPtr<ID3D11Resource> ldr_res;
               rtv->GetResource(ldr_res.put());
               if (ldr_res)
                  RunPostTonemapSMAA(native_device, native_device_context, device_data, gd, ldr_res.get());
            }
            return DrawOrDispatchOverrideType::Replaced; // we ran the original draw ourselves
         }
#endif
      }

      // Hide HUD: drop post-tonemap alpha-blended draws. Tonemap/FXAA/composite are all OPAQUE; the HUD (Scaleform)
      // is the only alpha-blended geometry after the tonemap. Keying on blend state (not the RT handle) is
      // buffer-independent: BL2 draws the HUD on the tonemap's LDR, but TPS routes it to a separate post-FXAA buffer.
      // `tonemap_fired_this_frame` scopes this to the current frame's post-tonemap span (so next frame's pre-tonemap
      // transparents survive). RT==LDR kept as a BL2 superset; the mod menu draws post-composition, stays visible.
      if (g_hide_ui && is_immediate && !is_custom_pass && gd.tonemap_fired_this_frame && !original_shader_hashes.Contains(kTonemapHash, reshade::api::shader_stage::pixel) && !original_shader_hashes.Contains(kTonemapHashTPS, reshade::api::shader_stage::pixel) && !original_shader_hashes.Contains(kFXAAResolveHash, reshade::api::shader_stage::pixel))
      {
         ComPtr<ID3D11BlendState> blend_state;
         FLOAT blend_factor[4];
         UINT sample_mask = 0;
         native_device_context->OMGetBlendState(blend_state.put(), blend_factor, &sample_mask);
         bool alpha_blended = false;
         if (blend_state)
         {
            D3D11_BLEND_DESC bd;
            blend_state->GetDesc(&bd);
            alpha_blended = bd.RenderTarget[0].BlendEnable != FALSE;
         }

         bool on_ldr_buffer = false;
         if (gd.ldr_buffer_handle)
         {
            ComPtr<ID3D11RenderTargetView> rtv;
            native_device_context->OMGetRenderTargets(1, rtv.put(), nullptr);
            if (rtv)
            {
               ComPtr<ID3D11Resource> res;
               rtv->GetResource(res.put());
               on_ldr_buffer = (res && (uint64_t)res.get() == gd.ldr_buffer_handle);
            }
         }

         if (alpha_blended || on_ldr_buffer)
            return DrawOrDispatchOverrideType::Skip;
      }

      return DrawOrDispatchOverrideType::None;
   }

   void OnPresent(ID3D11Device* native_device, DeviceData& device_data) override
   {
      auto& gd = GetGameDeviceData(device_data);
      gd.tonemap_fired_this_frame = false; // new frame: re-arm Hide UI's post-tonemap alpha-blend scope
   }

   void LoadConfigs() override
   {
      reshade::get_config_value(nullptr, PROJECT_NAME, "SMAAEnable", g_smaa_enable);
      reshade::get_config_value(nullptr, PROJECT_NAME, "RCASSharpness", g_rcas_sharpness);
      reshade::get_config_value(nullptr, PROJECT_NAME, "HideUI", g_hide_ui);

      // HDR grade sliders (cb_luma_global_settings_dirty is already true at init -> uploaded on first frame).
      auto& gs = cb_luma_global_settings.GameSettings;
      reshade::get_config_value(nullptr, PROJECT_NAME, "Exposure", gs.Exposure);
      reshade::get_config_value(nullptr, PROJECT_NAME, "Saturation", gs.Saturation);
      reshade::get_config_value(nullptr, PROJECT_NAME, "HighlightDechroma", gs.HighlightDechroma);
      reshade::get_config_value(nullptr, PROJECT_NAME, "BloomIntensity", gs.BloomIntensity);
      reshade::get_config_value(nullptr, PROJECT_NAME, "Contrast", gs.Contrast);
      reshade::get_config_value(nullptr, PROJECT_NAME, "VignetteIntensity", gs.VignetteIntensity);

      g_dof_type = 2; // default = Luma Gaussian
      reshade::get_config_value(nullptr, PROJECT_NAME, "DOFType", g_dof_type);
      reshade::get_config_value(nullptr, PROJECT_NAME, "DOFRadius", g_dof_radius);
      gs.DOFType = (float)g_dof_type;
      gs.DOFRadius = g_dof_radius;

      reshade::get_config_value(nullptr, PROJECT_NAME, "LumaBloomEnable", g_luma_bloom_enable);
      gs.LumaBloomEnable = g_luma_bloom_enable ? 1.f : 0.f; // mirror to the shader composite switch

      reshade::get_config_value(nullptr, PROJECT_NAME, "Dithering", gs.Dithering);
   }

   void DrawImGuiSettings(DeviceData& device_data) override
   {
      ImGui::SeparatorText("Anti-Aliasing");
      if (ImGui::Checkbox("SMAA Enable", &g_smaa_enable))
         reshade::set_config_value(nullptr, PROJECT_NAME, "SMAAEnable", g_smaa_enable);
      if (ImGui::IsItemHovered())
         ImGui::SetTooltip("Replaces the game's FXAA with SMAA (requires AA enabled in the game's video settings).");
      ImGui::BeginDisabled(!g_smaa_enable);
      ImGui::SliderFloat("RCAS Sharpness", &g_rcas_sharpness, 0.f, 1.f);
      if (ImGui::IsItemDeactivatedAfterEdit())
         reshade::set_config_value(nullptr, PROJECT_NAME, "RCASSharpness", g_rcas_sharpness);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("RCAS sharpening applied to the SMAA output (0 = off).");
      DrawResetButton(g_rcas_sharpness, 0.f, "RCASSharpness");
      ImGui::EndDisabled();

      // --- HDR grade (read in Tonemap_0xD00AA2A7.ps_5_0.hlsl via LumaSettings.GameSettings). All vanilla by default.
      // Exposure/Bloom/Vignette act on SDR+HDR; Saturation/Highlights/Contrast on the HDR display path only. ---
      ImGui::SeparatorText("Grade");
      auto& gs = cb_luma_global_settings.GameSettings;
      auto& gd_def = default_luma_global_game_settings;

      if (ImGui::SliderFloat("Exposure", &gs.Exposure, 0.f, 2.f))
         device_data.cb_luma_global_settings_dirty = true;
      if (ImGui::IsItemDeactivatedAfterEdit())
         reshade::set_config_value(nullptr, PROJECT_NAME, "Exposure", gs.Exposure);
      if (ImGui::IsItemHovered())
         ImGui::SetTooltip("Scene-referred exposure multiplier (1 = vanilla).");
      if (DrawResetButton(gs.Exposure, gd_def.Exposure, "Exposure"))
         device_data.cb_luma_global_settings_dirty = true;

      if (ImGui::SliderFloat("Contrast", &gs.Contrast, 0.f, 2.f))
         device_data.cb_luma_global_settings_dirty = true;
      if (ImGui::IsItemDeactivatedAfterEdit())
         reshade::set_config_value(nullptr, PROJECT_NAME, "Contrast", gs.Contrast);
      if (ImGui::IsItemHovered())
         ImGui::SetTooltip("Slope contrast around 18% mid-gray, HDR only (1 = vanilla).");
      if (DrawResetButton(gs.Contrast, gd_def.Contrast, "Contrast"))
         device_data.cb_luma_global_settings_dirty = true;

      if (ImGui::SliderFloat("Saturation", &gs.Saturation, 0.f, 2.f))
         device_data.cb_luma_global_settings_dirty = true;
      if (ImGui::IsItemDeactivatedAfterEdit())
         reshade::set_config_value(nullptr, PROJECT_NAME, "Saturation", gs.Saturation);
      if (ImGui::IsItemHovered())
         ImGui::SetTooltip("Color saturation (Oklab), HDR only (1 = vanilla).");
      if (DrawResetButton(gs.Saturation, gd_def.Saturation, "Saturation"))
         device_data.cb_luma_global_settings_dirty = true;

      if (ImGui::SliderFloat("Highlights Desaturation", &gs.HighlightDechroma, 0.f, 1.f))
         device_data.cb_luma_global_settings_dirty = true;
      if (ImGui::IsItemDeactivatedAfterEdit())
         reshade::set_config_value(nullptr, PROJECT_NAME, "HighlightDechroma", gs.HighlightDechroma);
      if (ImGui::IsItemHovered())
         ImGui::SetTooltip("How soon bright sources fade to neutral white, HDR only (0 = keep color at any brightness).");
      if (DrawResetButton(gs.HighlightDechroma, gd_def.HighlightDechroma, "HighlightDechroma"))
         device_data.cb_luma_global_settings_dirty = true;

      if (ImGui::Checkbox("Luma HDR Bloom", &g_luma_bloom_enable))
      {
         gs.LumaBloomEnable = g_luma_bloom_enable ? 1.f : 0.f;
         device_data.cb_luma_global_settings_dirty = true;
         reshade::set_config_value(nullptr, PROJECT_NAME, "LumaBloomEnable", g_luma_bloom_enable);
      }
      if (ImGui::IsItemHovered())
         ImGui::SetTooltip("Replaces the game's bloom with a wider, softer HDR bloom. Off = vanilla game bloom.");

      if (ImGui::SliderFloat("Bloom Intensity", &gs.BloomIntensity, 0.f, 2.f))
         device_data.cb_luma_global_settings_dirty = true;
      if (ImGui::IsItemDeactivatedAfterEdit())
         reshade::set_config_value(nullptr, PROJECT_NAME, "BloomIntensity", gs.BloomIntensity);
      if (ImGui::IsItemHovered())
         ImGui::SetTooltip("Bloom strength (1 = vanilla). Scales the Luma HDR bloom when enabled, else the vanilla game bloom.");
      if (DrawResetButton(gs.BloomIntensity, gd_def.BloomIntensity, "BloomIntensity"))
         device_data.cb_luma_global_settings_dirty = true;

      if (ImGui::SliderFloat("Vignette Intensity", &gs.VignetteIntensity, 0.f, 1.f))
         device_data.cb_luma_global_settings_dirty = true;
      if (ImGui::IsItemDeactivatedAfterEdit())
         reshade::set_config_value(nullptr, PROJECT_NAME, "VignetteIntensity", gs.VignetteIntensity);
      if (ImGui::IsItemHovered())
         ImGui::SetTooltip("Scales the game's vignette darkening (1 = vanilla, 0 = none).");
      if (DrawResetButton(gs.VignetteIntensity, gd_def.VignetteIntensity, "VignetteIntensity"))
         device_data.cb_luma_global_settings_dirty = true;

      // DoF: one checkbox toggles the Luma separable HDR Gaussian (g_dof_type 2) vs the vanilla game DoF (0).
      bool dof_on = (g_dof_type != 0);
      if (ImGui::Checkbox("Luma Gaussian DoF", &dof_on))
      {
         g_dof_type = dof_on ? 2 : 0;
         gs.DOFType = (float)g_dof_type;
         device_data.cb_luma_global_settings_dirty = true;
         reshade::set_config_value(nullptr, PROJECT_NAME, "DOFType", g_dof_type);
      }
      if (ImGui::IsItemHovered())
         ImGui::SetTooltip("Replaces the game's depth-of-field with a smoother HDR version. Off = vanilla game DoF.");

      ImGui::BeginDisabled(g_dof_type == 0);
      if (ImGui::SliderFloat("DoF Strength", &g_dof_radius, 1.f, 64.f))
      {
         gs.DOFRadius = g_dof_radius;
         device_data.cb_luma_global_settings_dirty = true;
      }
      if (ImGui::IsItemDeactivatedAfterEdit())
         reshade::set_config_value(nullptr, PROJECT_NAME, "DOFRadius", g_dof_radius);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("Depth of Field blur strength.");
      if (DrawResetButton(g_dof_radius, 9.f, "DOFRadius"))
      {
         gs.DOFRadius = g_dof_radius;
         device_data.cb_luma_global_settings_dirty = true;
      }
      ImGui::EndDisabled();

      bool dithering = gs.Dithering > 0.5f;
      if (ImGui::Checkbox("Dithering", &dithering))
      {
         gs.Dithering = dithering ? 1.f : 0.f;
         device_data.cb_luma_global_settings_dirty = true;
         reshade::set_config_value(nullptr, PROJECT_NAME, "Dithering", gs.Dithering);
      }
      if (ImGui::IsItemHovered())
         ImGui::SetTooltip("Reduces gradient banding.");

      ImGui::SeparatorText("UI");
      if (ImGui::Checkbox("Hide Gameplay UI", &g_hide_ui))
         reshade::set_config_value(nullptr, PROJECT_NAME, "HideUI", g_hide_ui);
      if (ImGui::IsItemHovered())
         ImGui::SetTooltip("Disables the in-game UI.");
   }

   void PrintImGuiAbout() override
   {
      ImGui::PushTextWrapPos(0.f);
      ImGui::Text(
         "Luma for \"Borderlands 2 & The Pre-Sequel\" is developed by DristoforColumb and is open source and free.\n"
         "It adds native HDR, replaces the game's FXAA with SMAA, and adds HDR bloom, depth-of-field, and 16x anisotropic filtering.\n"
         "It runs through dgVoodoo2 (DirectX 9 -> 11).\n"
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
                  "\ndgVoodoo2 (DirectX 9 -> 11 wrapper, required)"
                  "");
   }
};

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
   if (ul_reason_for_call == DLL_PROCESS_ATTACH)
   {
      Globals::SetGlobals(PROJECT_NAME, "Borderlands 2 & The Pre-Sequel Luma mod");
      Globals::VERSION = 1;

      swapchain_format_upgrade_type = TextureFormatUpgradesType::AllowedEnabled;
      swapchain_upgrade_type = SwapchainUpgradeType::scRGB;
      texture_format_upgrades_type = TextureFormatUpgradesType::AllowedEnabled;
      enable_indirect_texture_format_upgrades = true;
      enable_chain_indirect_texture_format_upgrades = ChainTextureFormatUpgradesType::DirectDependencies;
      texture_upgrade_formats = {
         reshade::api::format::r8g8b8a8_unorm,
         reshade::api::format::r8g8b8a8_unorm_srgb,
         reshade::api::format::r8g8b8a8_typeless,
         reshade::api::format::r8g8b8x8_unorm,
         reshade::api::format::r8g8b8x8_unorm_srgb,
         reshade::api::format::b8g8r8a8_unorm,
         reshade::api::format::b8g8r8a8_unorm_srgb,
         reshade::api::format::b8g8r8a8_typeless,
         reshade::api::format::b8g8r8x8_unorm,
         reshade::api::format::b8g8r8x8_unorm_srgb,
         reshade::api::format::b8g8r8x8_typeless,

         reshade::api::format::r16g16b16a16_unorm,

         reshade::api::format::r10g10b10a2_unorm,
         reshade::api::format::r10g10b10a2_typeless,

         reshade::api::format::r11g11b10_float,
      };
      // The LDR backbuffer the tonemap writes (8-bit) + the bloom buffers are swapchain-res/aspect.
      texture_format_upgrades_2d_size_filters = 0 | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainResolution | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainAspectRatio | (uint32_t)TextureFormatUpgrades2DSizeFilters::No1Px;
      // The game runs within 16:9 unless aspect ratio is unlocked; force-upgrade that aspect too.
      int screen_width = GetSystemMetrics(SM_CXSCREEN);
      int screen_height = GetSystemMetrics(SM_CYSCREEN);
      texture_format_upgrades_2d_size_filters |= (uint32_t)TextureFormatUpgrades2DSizeFilters::CustomAspectRatio;
      texture_format_upgrades_2d_custom_aspect_ratios = {float(screen_width) / float(screen_height), 16.f / 9.f};

      // AF16x: mode 4 upgrades the game's AF samplers to MaxAnisotropy=16 (clarity on oblique surfaces, zero
      // risk). LOD bias offset stays 0 (no TAA here; a negative bias would shimmer).
      enable_samplers_upgrade = true; // boot-time only (can't change after device creation)
      samplers_upgrade_mode = 4;

      game = new Borderlands2();
   }

   CoreMain(hModule, ul_reason_for_call, lpReserved);

   return TRUE;
}
