// Borderlands 2 + The Pre-Sequel — Luma HDR + SMAA mod (Unreal Engine 3, native DX9 -> D3D11 via dgVoodoo2).
//
// dgVoodoo2 translates SM3.0 to ps_5_0, so CSO hashes differ from the native DX9 ones. Launch the game exe
// DIRECTLY: XNA Launcher.exe also loads d3d9 and would capture ReShade instead of the game.
// One shared addon serves both games, discriminated by the tonemap hash:
// - TONEMAP PS 0xD00AA2A7 (BL2) / 0xFCFE623E (TPS): scene fp16 + bloom + vignette + LUT + DOF -> 8-bit LDR.
//   Replaced to recover HDR; the UI composites AFTER, on the LDR.
// - FXAA PS 0x0D3001F6 (only with in-game AA on) -> replaced with SMAA ULTRA + optional RCAS.
// All SDR/gamma space. Only ONE Luma .addon, and no other swapchain-hooking ReShade addon: they crash through
// dgVoodoo.

// Don't pop the DEVELOPMENT auto-debugger MessageBox on DLL attach: under a borderless/fullscreen game it's
// invisible and blocks the loader (ReShade times out the addon load -> error 1114).
#define DISABLE_AUTO_DEBUGGER 1

// No DLSS/DLAA: UE3 has no usable motion vectors, and NGX is x64-only (game is 32-bit)
#define GEOMETRY_SHADER_SUPPORT 0
#define ENABLE_SMAA 1  // replaces the FXAA resolve PS with SMAA ULTRA (+RCAS); core auto-registers the 6 "SMAA ..." passes from Luma_SMAA_impl
#define ENABLE_BLOOM 1 // core auto-registers the Bloom VS/Prefilter/Downsample/Upsample passes -> Luma_Bloom_impl
// SMAA runs POST-tonemap through the post-draw callback (see RunPostTonemapSMAA); needs original_draw_dispatch_func.
#define ENABLE_POST_DRAW_DISPATCH_CALLBACK 1

#include "..\..\Core\core.hpp"
#include <shellapi.h> // ShellExecuteA for About links (system("start ...") hangs the render thread in exclusive fullscreen)

// FXAA resolve PS (only present when AA is enabled in the game's video settings) — replaced with SMAA.
static constexpr uint32_t kFXAAResolveHash = 0x0D3001F6;
static constexpr uint32_t kTonemapHash = 0xD00AA2A7;    // BL2: writes the LDR buffer the HUD then draws onto
static constexpr uint32_t kTonemapHashTPS = 0xFCFE623E; // The Pre-Sequel: same engine, different tonemap CSO (one addon serves both)

// dgVoodoo 2.81.3 emits ps_4_0 where 2.87.3 emits ps_5_0, so the SAME shaders hash differently; the Is* helpers
// below match both. FXAA/video/icon are byte-shared between BL2 and TPS, so one 2.81.3 hash each covers both.
static constexpr uint32_t kFXAAResolveHash_v281 = 0xDF7DB98D; // BL2/TPS FXAA under dgVoodoo 2.81.3
static constexpr uint32_t kTonemapHash_v281 = 0xF14F8664;     // BL2 tonemap under dgVoodoo 2.81.3
static constexpr uint32_t kTonemapHashTPS_v281 = 0x2079F1E8;  // The Pre-Sequel tonemap under dgVoodoo 2.81.3

// Native bloom bright pass: 4 taps of the full-res scene into the half-res bloom buffer, weighting each by
// saturate((max3(colour * BloomScale) - BloomThreshold) * 0.5). It carries the per-area authored pair the Luma
// bloom needs - BloomScale in cb4[16].x, BloomThreshold in cb4[17].y. Byte-shared between BL2 and TPS.
static constexpr uint32_t kBloomBrightPassHash = 0x997ACB8E;      // dgVoodoo 2.87.3 (ps_5_0)
static constexpr uint32_t kBloomBrightPassHash_v281 = 0x5605F6C2; // dgVoodoo 2.81.3 (ps_4_0)

// Luma-injected SRV slots on the tonemap. No compile-time link to the shader register macros in
// Tonemap_0xD00AA2A7.ps_5_0.hlsl (BL2) and Tonemap_0xFCFE623E.ps_5_0.hlsl (TPS), so keep them in sync:
//   bloom -> TM_T_LUMABLOOM : BL2 t5 / TPS t8 (TPS t5 is the native DOF)
static constexpr uint32_t kLumaBloomSlotBL2 = 5;
static constexpr uint32_t kLumaBloomSlotTPS = 8;

// Scaleform item-card price shaders: mask-fill + digit-glyph PS hashes (a pair per dgVoodoo build).
static constexpr uint32_t kScaleformMaskFillHash2813 = 0x9F8EA541;
static constexpr uint32_t kScaleformDigitGlyphHash2813 = 0x63898919;
static constexpr uint32_t kScaleformMaskFillHash2873 = 0x616BEBBD;
static constexpr uint32_t kScaleformDigitGlyphHash2873 = 0x79CDF7BA;

// User settings (persisted via ReShade config under the shared NAME section; loaded in LoadConfigs).
static bool g_smaa_enable = true;
static float g_rcas_sharpness = 0.f;                                   // RCAS sharpen on SMAA output (0 = off)
static bool g_hide_ui = false;                                         // hide the game's HUD (for clean screenshots)
static bool g_smaa_predication = true;                                 // SMAA depth predication (depth from scene-color .a)
static float g_smaa_pred_k = 1000.f;                                   // predication depth compress (world units): D=z/(z+k); k=1000 (far-silhouette recall plateaus past this)
static bool g_luma_bloom_enable = true;                                // replace the game's clamped bloom with Luma HDR pyramidal bloom (live toggle)
static float g_bloom_intensity = 1.f;                                  // user-facing bloom strength (1 = vanilla); the uploaded value is derived from it
static bool g_video_auto_hdr_enable = true;                            // light AutoHDR on Bink videos, HDR only (live toggle)
static int g_bloom_nmips = 6;                                          // bloom pyramid mip count
static float g_bloom_sigmas[6] = {1.5f, 2.0f, 2.0f, 2.0f, 1.0f, 1.0f}; // per-mip Gaussian sigma (tapered, wider middle for a soft natural halo)
// Mean-luminance ratio between the game's own bloom buffer and this pyramid, read back live on the same frames:
// native sits at 0.158 of ours. Folded into the effective BloomIntensity so 1 means vanilla strength. A property of
// THIS pyramid (octaves, Karis prefilter, threshold placement): retuning them invalidates it; the DEVELOPMENT A/B
// re-checks it every run.
static constexpr float kBloomPyramidToNativeEnergy = 0.158f;

struct Borderlands2GameDeviceData final : public GameDeviceData
{
   // Repaired blend states, keyed by the ORIGINAL desc. Keying by desc rather than by the
   // source state's pointer means a released state can't leave a stale key that a later allocation reuses.
   struct BlendDescCompare
   {
      bool operator()(const D3D11_BLEND_DESC& a, const D3D11_BLEND_DESC& b) const
      {
         return memcmp(&a, &b, sizeof(D3D11_BLEND_DESC)) < 0;
      }
   };
   std::map<D3D11_BLEND_DESC, ComPtr<ID3D11BlendState>, BlendDescCompare> fixed_blend_states;

   // One staging copy per captured pass, read a frame late (see CaptureConstantRows). One buffer each, not a
   // ring: the reader may skip a frame, so a DO_NOT_WAIT map of LAST frame's copy is enough and never blocks.
   struct ConstantCapture
   {
      ComPtr<ID3D11Buffer> staging;
      UINT bytes = 0;
      bool copy_pending = false;
   };

   // Live bright-pass readback: the artist-authored bloom threshold this area is using. Negative until the first
   // successful map, which the C++ fallback covers.
   ConstantCapture bloom_cb;
   float bloom_threshold_live = -1.f;
   bool bloom_scale_warned = false;     // one-shot: bright-pass BloomScale is not the 4 the knee assumes
   bool bloom_threshold_warned = false; // one-shot: the bright pass never reported, so the knee is a fallback

   // Paces the shipping bright-pass readback, and the DEVELOPMENT A/B below.
   uint32_t frame_counter = 0;

#if DEVELOPMENT
   ConstantCapture grade_cb;

   // A/B energy measurement between the native bloom buffer and the Luma pyramid (see LogBloomEnergy): one staging
   // texture each, read a frame late on a slow cadence.
   struct TextureCapture
   {
      ComPtr<ID3D11Texture2D> staging;
      UINT width = 0;
      UINT height = 0;
      DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
      bool copy_pending = false;
   };
   TextureCapture bloom_ab_native;
   TextureCapture bloom_ab_luma;
   float bloom_tint_live[3] = {-1.f, -1.f, -1.f}; // tonemap cb4[16].rgb, the per-area authored composite tint
   std::unordered_set<uint64_t> grade_cb_logged;  // quantized constant sets already reported
   std::unordered_set<uint64_t> bloom_cb_logged;
#endif

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

   // Resource the tonemap renders to; on BL2 the HUD draws onto it afterwards. Used by Hide UI.
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

   // Scaleform price-digit stencil repair: armed between a mask-submit and the glyph strips; the mask is
   // duplicated into a private scratch D24S8 (cached per RT size) that the strips then test EQUAL/ref=1 against.
   bool scaleform_mask_armed = false;
   ComPtr<ID3D11DepthStencilState> dss_scaleform_mask_write;
   ComPtr<ID3D11DepthStencilState> dss_scaleform_mask_test;
   ComPtr<ID3D11DepthStencilView> dsv_scaleform_mask_active;
   struct ScaleformMaskDS
   {
      uint32_t width = 0, height = 0;
      ComPtr<ID3D11Texture2D> tex;
      ComPtr<ID3D11DepthStencilView> dsv;
   };
   ScaleformMaskDS scaleform_mask_ds_cache[4];
   uint32_t scaleform_mask_ds_next = 0;
};

class Borderlands2 final : public Game
{
   static Borderlands2GameDeviceData& GetGameDeviceData(DeviceData& device_data)
   {
      return *static_cast<Borderlands2GameDeviceData*>(device_data.game);
   }

   // Pass identity by shader hash, folding every supported dgVoodoo version (2.87.3 ps_5_0 + 2.81.3 ps_4_0).
   static bool IsBL2Tonemap(const ShaderHashesList<OneShaderPerPipeline>& hashes)
   {
      return hashes.Contains(kTonemapHash, reshade::api::shader_stage::pixel) || hashes.Contains(kTonemapHash_v281, reshade::api::shader_stage::pixel);
   }
   static bool IsTPSTonemap(const ShaderHashesList<OneShaderPerPipeline>& hashes)
   {
      return hashes.Contains(kTonemapHashTPS, reshade::api::shader_stage::pixel) || hashes.Contains(kTonemapHashTPS_v281, reshade::api::shader_stage::pixel);
   }
   static bool IsAnyTonemap(const ShaderHashesList<OneShaderPerPipeline>& hashes)
   {
      return IsBL2Tonemap(hashes) || IsTPSTonemap(hashes);
   }
   static bool IsFXAA(const ShaderHashesList<OneShaderPerPipeline>& hashes)
   {
      return hashes.Contains(kFXAAResolveHash, reshade::api::shader_stage::pixel) || hashes.Contains(kFXAAResolveHash_v281, reshade::api::shader_stage::pixel);
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
   // Post-tonemap SMAA on the LDR (gamma space). It runs AFTER the tonemap so it cannot perturb the DoF that
   // is composited inside it. Snapshot LDR -> DrawSMAA -> optional RCAS -> copy back into the LDR.
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
      // UE3 is all SDR (UNORM) gamma space: post buffers stay GAMMA so the gamma-SDR HUD blends like vanilla.
      // The tonemap pre-scales by GamePaperWhite/UIPaperWhite (UI_DRAW_TYPE 2) so the HUD lands at its own level.
      GetShaderDefineData(POST_PROCESS_SPACE_TYPE_HASH).SetDefaultValue('0');
      GetShaderDefineData(EARLY_DISPLAY_ENCODING_HASH).SetDefaultValue('0');
      GetShaderDefineData(VANILLA_ENCODING_TYPE_HASH).SetDefaultValue('1'); // Gamma 2.2 in and out
      GetShaderDefineData(GAMMA_CORRECTION_TYPE_HASH).SetDefaultValue('1');
      GetShaderDefineData(GAMUT_MAPPING_TYPE_HASH).SetDefaultValue('1'); // gamut-map wild colors in composition
      GetShaderDefineData(UI_DRAW_TYPE_HASH).SetDefaultValue('2');       // HUD gets its own UIPaperWhite + gamma blend

      // Manual Scene + UI Paper White sliders instead of the OS HDR reference level. Core gates the separate
      // "UI Paper White" slider on UI_DRAW_TYPE >= 1 && !use_os_reference_white_level. UI default 203 nits (BT.2408).
      use_os_reference_white_level = false;

      // Core auto-registers the 6 SMAA passes. Our tonemap outputs GAMMA, and the LDR snapshot feeds both
      // DrawSMAA color args directly with no color-prep CS, which keeps thin features and matches FXAA.
      // RCAS sharpen PS (drawn via core "Copy VS" + DrawCustomPixelShader after SMAA).
      native_shaders_definitions.emplace(CompileTimeStringHash("BL2TPS Sharpen PS"),
         ShaderDefinition{"Luma_BL2TPS_Sharpen", reshade::api::pipeline_subobject_type::pixel_shader, nullptr, "sharpen_ps"});
      // Depth-extract CS for SMAA predication: scene-color .a (linear view Z) -> normalized R16F predication depth.
      native_shaders_definitions.emplace(CompileTimeStringHash("BL2TPS Depth Extract CS"),
         ShaderDefinition("Luma_BL2TPS_DepthExtract", reshade::api::pipeline_subobject_type::compute_shader));

      // The game's post passes use cb0..cb5; b12/b13 are free for Luma.
      luma_settings_cbuffer_index = 13;
      luma_data_cbuffer_index = 12;

      // User HDR grade controls (read in Luma_BL2TPS_Tonemap.hlsl via LumaSettings.GameSettings). All
      // default to a vanilla no-op. Exposure/Bloom/Vignette act on both SDR+HDR; Saturation/Dechroma/Contrast HDR-only.
      default_luma_global_game_settings.Exposure = 1.f;           // scene multiplier (1x)
      default_luma_global_game_settings.Saturation = 1.f;         // Oklab saturation
      default_luma_global_game_settings.HighlightDechroma = 0.f;  // off; only mandatory DICE/gamut desat applies
      default_luma_global_game_settings.BloomIntensity = 1.f;     // seed only; OnDrawOrDispatch owns the effective value
      default_luma_global_game_settings.Contrast = 1.f;           // slope around 18% mid-gray
      default_luma_global_game_settings.VignetteIntensity = 1.f;  // game vignette darkening scale
      default_luma_global_game_settings.LumaBloomEnable = 1.f;    // 1 = Luma HDR pyramidal bloom, 0 = vanilla game bloom
      default_luma_global_game_settings.Dithering = 1.f;          // animated triangular dither at output (HDR), anti-banding on
      default_luma_global_game_settings.VideoAutoHDREnable = 1.f; // light AutoHDR on Bink videos (HDR only)
      default_luma_global_game_settings.VideoAutoHDRBoost = 0.5f; // highlight-expansion strength (peak ~165 nits at 0.5)
      default_luma_global_game_settings.BloomThreshold = 1.f;     // replaced within a frame by the native bright pass
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
         gd.dss_scaleform_mask_write.reset();
         gd.dss_scaleform_mask_test.reset();
         gd.dsv_scaleform_mask_active.reset();
         for (auto& slot : gd.scaleform_mask_ds_cache)
         {
            slot.dsv.reset();
            slot.tex.reset();
         }
      }
      delete device_data.game;
      device_data.game = nullptr;
   }

   // dgVoodoo sometimes leaves blending ENABLED on a secondary RT while RT0 has it off; D3D9 has one global blend
   // state, so the game never asked for it and that target is corrupted (diagnosed in TW2, water PS 0xDA16C815). No
   // symptom known here: preventive, and the DEVELOPMENT log reports whether it occurs. Repair = copy RT0's blend
   // fields onto the offenders, write masks kept. Not gated on is_immediate: blend state records fine into a deferred
   // list. Returns true iff it ran the original draw itself.
   bool FixImpossiblePerRTBlend(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, Borderlands2GameDeviceData& gd, reshade::api::shader_stage stages, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, bool is_custom_pass, std::function<void()>* original_draw_dispatch_func)
   {
      // Our own injected passes set their blend state deliberately. Re-issuing the draw is the only way to
      // apply a different state, so without that callback there is nothing to do.
      if (is_custom_pass || (stages & reshade::api::shader_stage::pixel) == 0 || original_draw_dispatch_func == nullptr)
         return false;

      ComPtr<ID3D11BlendState> blend_state;
      FLOAT blend_factor[4];
      UINT sample_mask = 0;
      native_device_context->OMGetBlendState(blend_state.put(), blend_factor, &sample_mask);
      if (!blend_state)
         return false; // no state object = default (blending off everywhere)

      D3D11_BLEND_DESC bd;
      blend_state->GetDesc(&bd);
      if (!bd.IndependentBlendEnable)
         return false; // one state for all targets: already D3D9-shaped

      const bool rt0_blending = bd.RenderTarget[0].BlendEnable != FALSE;
#if !DEVELOPMENT
      // Only the "RT0 off, RTn on" shape is ever repaired, so outside DEVELOPMENT (where the inverse shape is
      // logged) a blending RT0 has nothing to do here: skip the descriptor scan and the render-target query both.
      if (rt0_blending)
         return false;
#endif

      // Only BOUND targets count: the wrapper leaves stale BlendEnable in unused descriptor slots, which alone
      // matches nearly every draw and would take over passes other hooks own. Free descriptor scan first.
      bool disagreement = false;
      for (UINT i = 1; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT && !disagreement; i++)
         disagreement = bd.RenderTarget[i].BlendEnable != bd.RenderTarget[0].BlendEnable;
      if (!disagreement)
         return false;

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
      // One line per distinct shader, so a play session reports every pass that carries this.
      if (needs_fix || inverse_shape)
      {
         // Locked: this function deliberately runs on every context (see its header), so two can reach the set.
         static std::mutex logged_shaders_mutex;
         static std::unordered_set<uint64_t> logged_shaders;
         const uint64_t pixel_shader_hash = original_shader_hashes.pixel_shaders[0];
         const std::scoped_lock logged_shaders_lock(logged_shaders_mutex);
         if (logged_shaders.emplace(pixel_shader_hash).second)
         {
            reshade::log::message(reshade::log::level::warning,
               std::format("[BL-BlendFix] impossible per-RT blend state (dgVoodoo artefact) on pixel shader 0x{:X} - {}", pixel_shader_hash, needs_fix ? "repaired" : "inverse shape, left alone").c_str());
         }
      }
#endif

      if (!needs_fix)
         return false;

      ComPtr<ID3D11BlendState> fixed_state;
      if (const auto it = gd.fixed_blend_states.find(bd); it != gd.fixed_blend_states.end())
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
            return false; // leave the draw untouched rather than run it half-applied
         gd.fixed_blend_states[bd] = fixed_state;
      }

      native_device_context->OMSetBlendState(fixed_state.get(), blend_factor, sample_mask);
      (*original_draw_dispatch_func)();
      native_device_context->OMSetBlendState(blend_state.get(), blend_factor, sample_mask); // hand the game back its own state
      return true;
   }

   // Re-applies the stencil test dgVoodoo drops on the item card's rolling price digits. Returns true iff it ran
   // the original draw itself (caller returns Replaced).
   bool RepairScaleformStencilMask(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, Borderlands2GameDeviceData& gd, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, bool is_custom_pass, bool is_immediate, std::function<void()>* original_draw_dispatch_func)
   {
      if (!is_immediate || is_custom_pass)
         return false;
      const bool is_mask_shader = original_shader_hashes.Contains(kScaleformMaskFillHash2813, reshade::api::shader_stage::pixel) || original_shader_hashes.Contains(kScaleformMaskFillHash2873, reshade::api::shader_stage::pixel);
      if (!gd.scaleform_mask_armed && !is_mask_shader)
         return false;

      // A mask submit is the mask PS drawn with color writes off + a stencil-writing state.
      bool mask_submit = false;
      if (is_mask_shader)
      {
         ComPtr<ID3D11BlendState> blend_state;
         FLOAT blend_factor[4];
         UINT sample_mask = 0;
         native_device_context->OMGetBlendState(blend_state.put(), blend_factor, &sample_mask);
         if (blend_state)
         {
            D3D11_BLEND_DESC bd;
            blend_state->GetDesc(&bd);
            if (bd.RenderTarget[0].RenderTargetWriteMask == 0)
            {
               ComPtr<ID3D11DepthStencilState> ds_state;
               UINT stencil_ref = 0;
               native_device_context->OMGetDepthStencilState(ds_state.put(), &stencil_ref);
               if (ds_state)
               {
                  D3D11_DEPTH_STENCIL_DESC dsd;
                  ds_state->GetDesc(&dsd);
                  mask_submit = dsd.StencilEnable != FALSE;
               }
            }
         }
      }

      if (mask_submit)
      {
         // Duplicate the mask into the scratch stencil (REPLACE/1); arm only when the duplicate actually lands.
         gd.scaleform_mask_armed = false;
         ComPtr<ID3D11RenderTargetView> rtv;
         ComPtr<ID3D11DepthStencilView> prev_dsv;
         native_device_context->OMGetRenderTargets(1, rtv.put(), prev_dsv.put());
         if (rtv && original_draw_dispatch_func != nullptr)
         {
            ComPtr<ID3D11Resource> rt_res;
            rtv->GetResource(rt_res.put());
            ComPtr<ID3D11Texture2D> rt_tex;
            if (rt_res && SUCCEEDED(rt_res->QueryInterface(IID_PPV_ARGS(rt_tex.put()))))
            {
               D3D11_TEXTURE2D_DESC rt_desc;
               rt_tex->GetDesc(&rt_desc);
               Borderlands2GameDeviceData::ScaleformMaskDS* scratch = nullptr;
               for (auto& slot : gd.scaleform_mask_ds_cache)
               {
                  if (slot.dsv && slot.width == rt_desc.Width && slot.height == rt_desc.Height)
                  {
                     scratch = &slot;
                     break;
                  }
               }
               if (!scratch)
               {
                  for (auto& slot : gd.scaleform_mask_ds_cache)
                  {
                     if (!slot.dsv)
                     {
                        scratch = &slot;
                        break;
                     }
                  }
                  if (!scratch)
                     scratch = &gd.scaleform_mask_ds_cache[gd.scaleform_mask_ds_next++ % std::size(gd.scaleform_mask_ds_cache)];
                  scratch->dsv.reset();
                  scratch->tex.reset();
                  D3D11_TEXTURE2D_DESC ds_desc = {};
                  ds_desc.Width = rt_desc.Width;
                  ds_desc.Height = rt_desc.Height;
                  ds_desc.MipLevels = 1;
                  ds_desc.ArraySize = 1;
                  ds_desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
                  ds_desc.SampleDesc = rt_desc.SampleDesc;
                  ds_desc.Usage = D3D11_USAGE_DEFAULT;
                  ds_desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
                  if (SUCCEEDED(native_device->CreateTexture2D(&ds_desc, nullptr, scratch->tex.put())))
                     native_device->CreateDepthStencilView(scratch->tex.get(), nullptr, scratch->dsv.put());
                  scratch->width = rt_desc.Width;
                  scratch->height = rt_desc.Height;
               }
               if (!gd.dss_scaleform_mask_write)
               {
                  D3D11_DEPTH_STENCIL_DESC write_desc = {};
                  write_desc.DepthEnable = FALSE;
                  write_desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
                  write_desc.DepthFunc = D3D11_COMPARISON_ALWAYS;
                  write_desc.StencilEnable = TRUE;
                  write_desc.StencilReadMask = D3D11_DEFAULT_STENCIL_READ_MASK;
                  write_desc.StencilWriteMask = D3D11_DEFAULT_STENCIL_WRITE_MASK;
                  write_desc.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
                  write_desc.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
                  write_desc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_REPLACE;
                  write_desc.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
                  write_desc.BackFace = write_desc.FrontFace;
                  native_device->CreateDepthStencilState(&write_desc, gd.dss_scaleform_mask_write.put());
               }
               if (scratch->dsv && gd.dss_scaleform_mask_write)
               {
                  ComPtr<ID3D11DepthStencilState> prev_ds_state;
                  UINT prev_stencil_ref = 0;
                  native_device_context->OMGetDepthStencilState(prev_ds_state.put(), &prev_stencil_ref);
                  native_device_context->ClearDepthStencilView(scratch->dsv.get(), D3D11_CLEAR_STENCIL, 1.f, 0);
                  ID3D11RenderTargetView* rtv_raw = rtv.get();
                  native_device_context->OMSetRenderTargets(1, &rtv_raw, scratch->dsv.get());
                  native_device_context->OMSetDepthStencilState(gd.dss_scaleform_mask_write.get(), 1u);
                  (*original_draw_dispatch_func)();
                  native_device_context->OMSetDepthStencilState(prev_ds_state.get(), prev_stencil_ref);
                  native_device_context->OMSetRenderTargets(1, &rtv_raw, prev_dsv.get());
                  gd.dsv_scaleform_mask_active = scratch->dsv;
                  gd.scaleform_mask_armed = true;
               }
            }
         }
         return false; // the real mask draw still proceeds through the normal path
      }

      if (gd.scaleform_mask_armed && (original_shader_hashes.Contains(kScaleformDigitGlyphHash2813, reshade::api::shader_stage::pixel) || original_shader_hashes.Contains(kScaleformDigitGlyphHash2873, reshade::api::shader_stage::pixel)))
      {
         // Only intervene on glyph draws with stencil ENABLED but the test neutered (ALWAYS func / zero read mask /
         // no stencil plane) = the broken masked content; StencilEnable FALSE = genuinely unmasked glyphs (leave),
         // an effective test = a correctly translated path (leave).
         ComPtr<ID3D11DepthStencilState> prev_ds_state;
         UINT prev_stencil_ref = 0;
         native_device_context->OMGetDepthStencilState(prev_ds_state.put(), &prev_stencil_ref);
         ComPtr<ID3D11RenderTargetView> rtv;
         ComPtr<ID3D11DepthStencilView> prev_dsv;
         native_device_context->OMGetRenderTargets(1, rtv.put(), prev_dsv.put());
         D3D11_DEPTH_STENCIL_DESC dsd = {};
         if (prev_ds_state)
            prev_ds_state->GetDesc(&dsd);
         const bool stencil_enabled = prev_ds_state && dsd.StencilEnable != FALSE;
         bool effective_stencil_test = false;
         if (stencil_enabled && prev_dsv)
         {
            D3D11_DEPTH_STENCIL_VIEW_DESC dsv_desc;
            prev_dsv->GetDesc(&dsv_desc);
            const bool has_stencil_plane = dsv_desc.Format == DXGI_FORMAT_D24_UNORM_S8_UINT || dsv_desc.Format == DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
            const bool any_func_tests = dsd.FrontFace.StencilFunc != D3D11_COMPARISON_ALWAYS || dsd.BackFace.StencilFunc != D3D11_COMPARISON_ALWAYS;
            effective_stencil_test = dsd.StencilReadMask != 0 && has_stencil_plane && any_func_tests;
         }
         if (stencil_enabled && !effective_stencil_test && original_draw_dispatch_func != nullptr && gd.dsv_scaleform_mask_active && rtv)
         {
            if (!gd.dss_scaleform_mask_test)
            {
               // Test-only: pass exactly where the duplicated mask wrote 1, never write, depth off.
               D3D11_DEPTH_STENCIL_DESC test_desc = {};
               test_desc.DepthEnable = FALSE;
               test_desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
               test_desc.DepthFunc = D3D11_COMPARISON_ALWAYS;
               test_desc.StencilEnable = TRUE;
               test_desc.StencilReadMask = D3D11_DEFAULT_STENCIL_READ_MASK;
               test_desc.StencilWriteMask = 0;
               test_desc.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
               test_desc.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
               test_desc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
               test_desc.FrontFace.StencilFunc = D3D11_COMPARISON_EQUAL;
               test_desc.BackFace = test_desc.FrontFace;
               native_device->CreateDepthStencilState(&test_desc, gd.dss_scaleform_mask_test.put());
            }
            if (gd.dss_scaleform_mask_test)
            {
               ID3D11RenderTargetView* rtv_raw = rtv.get();
               native_device_context->OMSetRenderTargets(1, &rtv_raw, gd.dsv_scaleform_mask_active.get());
               native_device_context->OMSetDepthStencilState(gd.dss_scaleform_mask_test.get(), 1u);
               (*original_draw_dispatch_func)();
               native_device_context->OMSetDepthStencilState(prev_ds_state.get(), prev_stencil_ref);
               native_device_context->OMSetRenderTargets(1, &rtv_raw, prev_dsv.get());
               return true;
            }
         }
         return false;
      }

      // Any other draw ends the mask span.
      gd.scaleform_mask_armed = false;
      gd.dsv_scaleform_mask_active.reset();
      return false;
   }

   // The Steam launcher's ReShade instance (it creates its own D3D11 device through dgVoodoo) owns ReShade.log for
   // the whole session and the game's lines are dropped, so mirror every line into our own file beside the exe.
   static void LogGradeLine(const std::string& line)
   {
      reshade::log::message(reshade::log::level::info, line.c_str());
      std::ofstream file("Luma-BL2.log", std::ios::app);
      if (file)
         file << line << std::endl;
   }

   // Copy the pass's PS constant buffer and hand back `row_count` rows of it, one frame late (the path proven on this
   // wrapper in MoH Airborne). False = nothing to read yet, normal on the first frames.
   static bool CaptureConstantRows(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context,
      Borderlands2GameDeviceData::ConstantCapture& capture, uint32_t slot, uint32_t first_row, uint32_t row_count, float* out)
   {
      ComPtr<ID3D11Buffer> cb;
      native_device_context->PSGetConstantBuffers(slot, 1, cb.put());
      if (!cb)
         return false;
      D3D11_BUFFER_DESC bd = {};
      cb->GetDesc(&bd);
      if (bd.ByteWidth < (first_row + row_count) * 16)
         return false;

      if (capture.bytes != bd.ByteWidth)
      {
         D3D11_BUFFER_DESC sd = {};
         sd.ByteWidth = bd.ByteWidth;
         sd.Usage = D3D11_USAGE_STAGING;
         sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
         capture.staging.reset();
         capture.copy_pending = false;
         if (FAILED(native_device->CreateBuffer(&sd, nullptr, capture.staging.put())) || !capture.staging)
         {
            capture.bytes = 0;
            return false;
         }
         capture.bytes = bd.ByteWidth;
      }

      bool have_rows = false;
      if (capture.copy_pending)
      {
         D3D11_MAPPED_SUBRESOURCE mapped = {};
         if (SUCCEEDED(native_device_context->Map(capture.staging.get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped)) && mapped.pData != nullptr)
         {
            std::memcpy(out, (const uint8_t*)mapped.pData + (size_t)first_row * 16, (size_t)row_count * 16);
            native_device_context->Unmap(capture.staging.get(), 0);
            capture.copy_pending = false;
            have_rows = true;
         }
      }
      if (!capture.copy_pending)
      {
         native_device_context->CopyResource(capture.staging.get(), cb.get());
         capture.copy_pending = true;
      }
      return have_rows;
   }

   // The artist-authored bloom pair off the game's bright pass (0x997ACB8E under dgVoodoo 2.87.3, 0x5605F6C2 under
   // 2.81.3), which per tap computes
   //     colour = tex.rgb * cb4[16].x;  w = saturate((max3(colour) - cb4[17].y) * 0.5);  out += colour * w
   // i.e. BloomScale in cb4[16].x, BloomThreshold in cb4[17].y. The scale measured a constant 4 everywhere, so only
   // the threshold is tracked; each distinct set is reported once.
   static void CaptureBloomConstants(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, Borderlands2GameDeviceData& gd)
   {
      // Paced: the value is authored per PostProcessVolume and moves on a human timescale; each capture copies cb4.
      if ((gd.frame_counter & 7u) != 0u)
         return;

      float rows[2 * 4] = {};
      if (!CaptureConstantRows(native_device, native_device_context, gd.bloom_cb, 4, 16, 2, rows))
         return;
      const float scale = rows[0];              // cb4[16].x
      const float threshold = rows[5];          // cb4[17].y
      if (threshold > 0.f && threshold < 100.f) // reject implausible readback rather than let it reach the frame
         gd.bloom_threshold_live = threshold;

      // The prefilter's knee drops the x4 because the bright pass reads the scene pre-divided by 4 and BloomScale
      // cancels it; an area authoring another scale would silently shift the knee by that factor, so say it once.
      if (!gd.bloom_scale_warned && fabsf(scale - 4.f) > 1e-3f)
      {
         gd.bloom_scale_warned = true;
         LogGradeLine(std::format("[BL-Bloom] WARNING: bright-pass BloomScale is {:.5f}, not the 4.0 the Luma prefilter's knee assumes - the bloom threshold is off by that factor", scale));
      }

#if DEVELOPMENT
      const float keyed[2] = {scale, threshold};
      const float quanta[2] = {1000.f, 1000.f};
      if (gd.bloom_cb_logged.size() < 64 && gd.bloom_cb_logged.emplace(QuantizedKey(keyed, quanta, 2)).second)
      {
         LogGradeLine(std::format("[BL-Bloom] bright pass: BloomScale={:.5f} BloomThreshold={:.5f}", scale, threshold));
      }
#endif
   }

#if DEVELOPMENT
   static uint64_t QuantizedKey(const float* values, const float* quanta, uint32_t count)
   {
      uint64_t key = 1469598103934665603ull;
      for (uint32_t i = 0; i < count; i++)
         key = (key ^ (uint64_t)(uint32_t)(int32_t)lroundf(values[i] * quanta[i])) * 1099511628211ull;
      return key;
   }

   // Mean and max luminance of a texture, read a frame late. Sparse (every 4th texel both ways) because this only
   // has to compare two averages, not reproduce them exactly.
   static bool SampleTextureStats(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context,
      ID3D11ShaderResourceView* srv, Borderlands2GameDeviceData::TextureCapture& capture, float& out_mean, float& out_max, DXGI_FORMAT& out_format)
   {
      if (srv == nullptr)
         return false;
      ComPtr<ID3D11Resource> res;
      srv->GetResource(res.put());
      ComPtr<ID3D11Texture2D> tex;
      if (!res || FAILED(res->QueryInterface(IID_PPV_ARGS(tex.put()))))
         return false;
      D3D11_TEXTURE2D_DESC td = {};
      tex->GetDesc(&td);
      D3D11_SHADER_RESOURCE_VIEW_DESC vd = {};
      srv->GetDesc(&vd);
      out_format = vd.Format; // the VIEW format is what the shader reads, and what decides UNORM vs FLOAT

      if (capture.width != td.Width || capture.height != td.Height || capture.format != td.Format)
      {
         D3D11_TEXTURE2D_DESC sd = td;
         sd.MipLevels = 1;
         sd.ArraySize = 1;
         sd.Usage = D3D11_USAGE_STAGING;
         sd.BindFlags = 0;
         sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
         sd.MiscFlags = 0;
         capture.staging.reset();
         capture.copy_pending = false;
         if (FAILED(native_device->CreateTexture2D(&sd, nullptr, capture.staging.put())) || !capture.staging)
         {
            capture.width = 0;
            return false;
         }
         capture.width = td.Width;
         capture.height = td.Height;
         capture.format = td.Format;
      }

      bool have_stats = false;
      if (capture.copy_pending)
      {
         D3D11_MAPPED_SUBRESOURCE mapped = {};
         if (SUCCEEDED(native_device_context->Map(capture.staging.get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped)) && mapped.pData != nullptr)
         {
            const bool is_unorm = vd.Format == DXGI_FORMAT_R16G16B16A16_UNORM;
            double sum = 0.0;
            double peak = 0.0;
            uint32_t count = 0;
            for (UINT y = 0; y < capture.height; y += 4)
            {
               const uint16_t* row = (const uint16_t*)((const uint8_t*)mapped.pData + (size_t)y * mapped.RowPitch);
               for (UINT x = 0; x < capture.width; x += 4)
               {
                  float rgb[3];
                  for (uint32_t c = 0; c < 3; c++)
                  {
                     const uint16_t raw = row[(size_t)x * 4 + c];
                     rgb[c] = is_unorm ? (raw / 65535.f) : DirectX::PackedVector::XMConvertHalfToFloat(raw);
                  }
                  const float luminance = 0.2126f * rgb[0] + 0.7152f * rgb[1] + 0.0722f * rgb[2];
                  sum += luminance;
                  peak = (luminance > peak) ? luminance : peak;
                  count++;
               }
            }
            native_device_context->Unmap(capture.staging.get(), 0);
            capture.copy_pending = false;
            if (count > 0)
            {
               out_mean = (float)(sum / count);
               out_max = (float)peak;
               have_stats = true;
            }
         }
      }
      if (!capture.copy_pending)
      {
         native_device_context->CopySubresourceRegion(capture.staging.get(), 0, 0, 0, 0, tex.get(), 0, nullptr);
         capture.copy_pending = true;
      }
      return have_stats;
   }

   // Vanilla adds native_buffer * BloomTint * 4 * gate (gate = 1 below scene luma 1.107); Luma adds pyramid *
   // BloomTint * 4 * BloomIntensity (pre-scaled by kBloomPyramidToNativeEnergy). The ratio of the two means is the
   // factor that constant is off by.
   static void LogBloomEnergy(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, Borderlands2GameDeviceData& gd, bool is_tps, float effective_threshold)
   {
      if ((gd.frame_counter % 120) != 0)
         return;
      ComPtr<ID3D11ShaderResourceView> native_srv;
      native_device_context->PSGetShaderResources(is_tps ? 2u : 1u, 1, native_srv.put());

      float native_mean = 0.f, native_max = 0.f, luma_mean = 0.f, luma_max = 0.f;
      DXGI_FORMAT native_fmt = DXGI_FORMAT_UNKNOWN, luma_fmt = DXGI_FORMAT_UNKNOWN; // luma_fmt: unused, SampleTextureStats reports it
      const bool have_native = SampleTextureStats(native_device, native_device_context, native_srv.get(), gd.bloom_ab_native, native_mean, native_max, native_fmt);
      const bool have_luma = SampleTextureStats(native_device, native_device_context, gd.srv_luma_bloom.get(), gd.bloom_ab_luma, luma_mean, luma_max, luma_fmt);
      if (!have_native || !have_luma || luma_mean <= 1e-9f)
         return;

      const float tint = (gd.bloom_tint_live[0] >= 0.f)
                            ? (gd.bloom_tint_live[0] + gd.bloom_tint_live[1] + gd.bloom_tint_live[2]) / 3.f
                            : 1.f;
      // Mirror of the two composites, so the ratio reads 1.0 when Bloom Intensity 1 really is vanilla strength.
      const float vanilla_add = native_mean * tint * 4.f;
      const float luma_add = luma_mean * tint * 4.f * cb_luma_global_settings.GameSettings.BloomIntensity;
      // Bloom Intensity at 0 makes the ratio meaningless, but the measured means still are: report those.
      const bool ratio_valid = luma_add > 1e-9f;
      const float ratio = ratio_valid ? (vanilla_add / luma_add) : 0.f;
      LogGradeLine(std::format("[BL-BloomAB] {} | thr_eff={:.4f} | native mean={:.6f} max={:.6f} fmt={} | luma mean={:.6f} max={:.6f} | tint={:.4f} | vanilla adds {:.6f}, we add {:.6f} -> x{:.4f}",
         is_tps ? "TPS" : "BL2", effective_threshold, native_mean, native_max, (uint32_t)native_fmt, luma_mean, luma_max, tint, vanilla_add, luma_add, ratio));

      // Fires on any scene, either game, where the shipped constant misses vanilla strength. Loose bound because the
      // ratio wanders with content (0.75-1.75 across BL2 areas, 0.59-1.27 across TPS), so only a real miss trips it.
      if (ratio_valid && native_mean > 1e-5f && (ratio < 0.5f || ratio > 2.0f))
      {
         LogGradeLine(std::format("[BL-BloomAB] WARNING: bloom energy off by x{:.2f} on {} - kBloomPyramidToNativeEnergy ({:.4f}) may need re-measuring for this game",
            ratio, is_tps ? "TPS" : "BL2", kBloomPyramidToNativeEnergy));
      }
   }

   // The tonemap's own constants off cb4: rows 15..22 are DX9 c7..c14 (ImageAdjustments1..3, HalfResMaskRect,
   // DOFKernelSize, vignette). Raw values only, the curve is solved in the shader (VanillaCurveLinear). cb4[16].rgb
   // is the per-area bloom tint the bloom A/B mirrors.
   static void CaptureGradeConstants(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, Borderlands2GameDeviceData& gd)
   {
      constexpr uint32_t kFirstRow = 15;
      constexpr uint32_t kRows = 8;

      float rows[kRows * 4] = {};
      if (!CaptureConstantRows(native_device, native_device_context, gd.grade_cb, 4, kFirstRow, kRows, rows))
         return;

      for (uint32_t i = 0; i < 3; i++)
         gd.bloom_tint_live[i] = rows[1 * 4 + i]; // cb4[16].rgb

      const float* ia2 = rows + 2 * 4; // cb4[17] = (A, Y, Z, W)
      const float* ia3 = rows + 3 * 4; // cb4[18].x = K
      // W is the live exposure gain and moves every frame while the eye adapts, so it is quantized coarsely and
      // the shape parameters keep 1e-3: one line per distinct curve, not per frame.
      const float keyed[5] = {ia2[0], ia2[1], ia2[2], ia2[3], ia3[0]};
      const float quanta[5] = {1000.f, 1000.f, 1000.f, 20.f, 1000.f};
      if (gd.grade_cb_logged.size() >= 64 || !gd.grade_cb_logged.emplace(QuantizedKey(keyed, quanta, 5)).second)
         return;

      LogGradeLine(std::format("[BL-Grade] A={:.5f} Y={:.5f} Z={:.5f} W={:.5f} K={:.5f} | vignette cb4[21]=({:.5f}, {:.5f}, {:.5f}, {:.5f}) cb4[22]=({:.5f}, {:.5f}, {:.5f}, {:.5f})",
         ia2[0], ia2[1], ia2[2], ia2[3], ia3[0],
         rows[6 * 4 + 0], rows[6 * 4 + 1], rows[6 * 4 + 2], rows[6 * 4 + 3],
         rows[7 * 4 + 0], rows[7 * 4 + 1], rows[7 * 4 + 2], rows[7 * 4 + 3]));
   }

#endif // DEVELOPMENT

   DrawOrDispatchOverrideType OnDrawOrDispatch(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, reshade::api::shader_stage stages, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, bool is_custom_pass, bool& updated_cbuffers, std::function<void()>* original_draw_dispatch_func) override
   {
      auto& gd = GetGameDeviceData(device_data);
      const bool is_immediate = native_device_context->GetType() == D3D11_DEVICE_CONTEXT_IMMEDIATE;

      if (is_immediate && !is_custom_pass && (stages & reshade::api::shader_stage::pixel) != 0)
      {
         // Track this area's authored bloom threshold off the native bright pass; the Luma prefilter needs it.
         if (original_shader_hashes.Contains(kBloomBrightPassHash, reshade::api::shader_stage::pixel) || original_shader_hashes.Contains(kBloomBrightPassHash_v281, reshade::api::shader_stage::pixel))
         {
            CaptureBloomConstants(native_device, native_device_context, gd);
         }
      }

      // Scaleform item-card price-digit stencil repair: returns Replaced when it re-runs the draw itself.
      if (RepairScaleformStencilMask(native_device, native_device_context, gd, original_shader_hashes, is_custom_pass, is_immediate, original_draw_dispatch_func))
         return DrawOrDispatchOverrideType::Replaced;

      // Track the LDR buffer (the tonemap's render target) for Hide UI.
      if (is_immediate && IsAnyTonemap(original_shader_hashes))
      {
         // TPS inserts a LightShaftTexture at slot 1, shifting its native textures down one (LUT@t4, DOF@t5); the
         // injected Luma bloom therefore moves off t5 to t8.
         const bool is_tps = IsTPSTonemap(original_shader_hashes);
#if DEVELOPMENT
         // Read the grade constants off this very draw: they are the input the vanilla curve runs on, so they
         // must be sampled here rather than at present, when another volume's values may already be bound.
         CaptureGradeConstants(native_device, native_device_context, gd);
#endif
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
         // Pyramidal bloom from the fp16 scene (tonemap t0), bound at PS t5 (BL2) / t8 (TPS); it ignores native bloom
         // t1, so no doubling. The graphics state stack restores the tonemap's RT/PS/SRVs afterwards.
         if (g_luma_bloom_enable && scene_srv)
         {
            // Follow the area's authored knee: the prefilter reads GameSettings.BloomThreshold, kept live off the
            // native bright pass. 1.0 until the first readback, mid-range for this game (measured 0.50 to 1.32).
            {
               auto& gs = cb_luma_global_settings.GameSettings;

               // THE SOLE WRITER of the effective BloomIntensity - the UI only touches the raw slider. The gain
               // applies only while the pyramid is the active bloom; the vanilla branch shares this field.
               const float effective_intensity = g_luma_bloom_enable ? (g_bloom_intensity * kBloomPyramidToNativeEnergy) : g_bloom_intensity;
               if (fabsf(gs.BloomIntensity - effective_intensity) > 1e-6f)
               {
                  gs.BloomIntensity = effective_intensity;
                  device_data.cb_luma_global_settings_dirty = true;
               }
               // Never-captured means the bright-pass hash did not match (new dgVoodoo build, or the pass is not
               // byte-shared after all). The fallback looks plausible, so say it once.
               if (gd.bloom_threshold_live < 0.f && !gd.bloom_threshold_warned && gd.frame_counter > 600)
               {
                  gd.bloom_threshold_warned = true;
                  LogGradeLine("[BL-Bloom] WARNING: the native bright pass never reported a threshold - the Luma bloom is running on the fallback, so its knee is not the game's");
               }
               const float thr = gd.bloom_threshold_live >= 0.f ? gd.bloom_threshold_live : default_luma_global_game_settings.BloomThreshold;
               if (fabsf(gs.BloomThreshold - thr) > 1e-4f)
               {
                  gs.BloomThreshold = thr;
                  device_data.cb_luma_global_settings_dirty = true;
               }
            }

            DrawStateStack<DrawStateStackType::FullGraphics> bloom_state;
            bloom_state.Cache(native_device_context, device_data.uav_max_count);

            // DrawBloom binds only b11, so the prefilter would not see LumaSettings without this (donor: MELE).
            if (luma_settings_cbuffer_index < D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT)
            {
               ID3D11Buffer* settings_cb = device_data.luma_global_settings.get();
               native_device_context->PSSetConstantBuffers(luma_settings_cbuffer_index, 1, &settings_cb);
            }

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
#if DEVELOPMENT
            LogBloomEnergy(native_device, native_device_context, gd, is_tps, cb_luma_global_settings.GameSettings.BloomThreshold);
#endif
         }
#endif

#if ENABLE_SMAA
         // Run the original tonemap draw ourselves, then SMAA on its LDR output; a normal draw without the callback.
         if (g_smaa_enable && original_draw_dispatch_func != nullptr)
         {
            // Returning Replaced short-circuits core's per-pass SetLumaConstantBuffers, so upload the grade CB
            // here or the draw samples last-present's values (one frame of lag on the sliders).
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

      // After the tonemap the Scaleform HUD is the only alpha-blended geometry. Keying on blend state rather than the
      // RT is buffer-independent: BL2 draws the HUD on the tonemap's LDR, TPS on a separate post-FXAA buffer.
      if (g_hide_ui && is_immediate && !is_custom_pass && gd.tonemap_fired_this_frame && !IsAnyTonemap(original_shader_hashes) && !IsFXAA(original_shader_hashes))
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

      // LAST on purpose: this one re-issues the draw itself, so it must yield to every hook above, or it runs
      // vanilla a pass another hook meant to take over.
      if (FixImpossiblePerRTBlend(native_device, native_device_context, gd, stages, original_shader_hashes, is_custom_pass, original_draw_dispatch_func))
         return DrawOrDispatchOverrideType::Replaced;

      return DrawOrDispatchOverrideType::None;
   }

   void OnPresent(ID3D11Device* native_device, DeviceData& device_data) override
   {
      auto& gd = GetGameDeviceData(device_data);
      gd.tonemap_fired_this_frame = false; // new frame: re-arm Hide UI's post-tonemap alpha-blend scope
      gd.frame_counter++;
      // Never carry a Scaleform mask span across frames.
      gd.scaleform_mask_armed = false;
      gd.dsv_scaleform_mask_active.reset();
   }

   void LoadConfigs() override
   {
      reshade::get_config_value(nullptr, NAME, "SMAAEnable", g_smaa_enable);
      reshade::get_config_value(nullptr, NAME, "RCASSharpness", g_rcas_sharpness);
      reshade::get_config_value(nullptr, NAME, "HideUI", g_hide_ui);

      // HDR grade sliders (cb_luma_global_settings_dirty is already true at init -> uploaded on first frame).
      auto& gs = cb_luma_global_settings.GameSettings;
      reshade::get_config_value(nullptr, NAME, "Exposure", gs.Exposure);
      reshade::get_config_value(nullptr, NAME, "Saturation", gs.Saturation);
      reshade::get_config_value(nullptr, NAME, "HighlightDechroma", gs.HighlightDechroma);
      reshade::get_config_value(nullptr, NAME, "BloomIntensity", g_bloom_intensity);
      reshade::get_config_value(nullptr, NAME, "Contrast", gs.Contrast);
      reshade::get_config_value(nullptr, NAME, "VignetteIntensity", gs.VignetteIntensity);

      reshade::get_config_value(nullptr, NAME, "LumaBloomEnable", g_luma_bloom_enable);
      gs.LumaBloomEnable = g_luma_bloom_enable ? 1.f : 0.f; // mirror to the shader composite switch

      reshade::get_config_value(nullptr, NAME, "Dithering", gs.Dithering);

      reshade::get_config_value(nullptr, NAME, "VideoAutoHDREnable", g_video_auto_hdr_enable);
      gs.VideoAutoHDREnable = g_video_auto_hdr_enable ? 1.f : 0.f; // mirror to the shader runtime gate
      reshade::get_config_value(nullptr, NAME, "VideoAutoHDRBoost", gs.VideoAutoHDRBoost);
   }

   void DrawImGuiSettings(DeviceData& device_data) override
   {
      ImGui::SeparatorText("Anti-Aliasing");
      if (ImGui::Checkbox("SMAA Enable", &g_smaa_enable))
         reshade::set_config_value(nullptr, NAME, "SMAAEnable", g_smaa_enable);
      if (ImGui::IsItemHovered())
         ImGui::SetTooltip("Replaces the game's FXAA with SMAA (requires AA enabled in the game's video settings).");
      ImGui::BeginDisabled(!g_smaa_enable);
      ImGui::SliderFloat("RCAS Sharpness", &g_rcas_sharpness, 0.f, 1.f);
      if (ImGui::IsItemDeactivatedAfterEdit())
         reshade::set_config_value(nullptr, NAME, "RCASSharpness", g_rcas_sharpness);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("Sharpening applied on top of SMAA (0 = off).");
      if (DrawResetButton<float, false>(g_rcas_sharpness, 0.f, "RCASSharpness"))
         reshade::set_config_value(nullptr, NAME, "RCASSharpness", g_rcas_sharpness);
      ImGui::EndDisabled();

      // HDR grade sliders, read in Luma_BL2TPS_Tonemap.hlsl; every default is a vanilla no-op (see OnInit).
      ImGui::SeparatorText("Grade");
      auto& gs = cb_luma_global_settings.GameSettings;
      auto& gd_def = default_luma_global_game_settings;

      if (ImGui::SliderFloat("Exposure", &gs.Exposure, 0.f, 2.f))
         device_data.cb_luma_global_settings_dirty = true;
      if (ImGui::IsItemDeactivatedAfterEdit())
         reshade::set_config_value(nullptr, NAME, "Exposure", gs.Exposure);
      if (ImGui::IsItemHovered())
         ImGui::SetTooltip("Overall image brightness (1 = vanilla).");
      if (DrawResetButton<float, false>(gs.Exposure, gd_def.Exposure, "Exposure"))
      {
         device_data.cb_luma_global_settings_dirty = true;
         reshade::set_config_value(nullptr, NAME, "Exposure", gs.Exposure);
      }

      if (ImGui::SliderFloat("Contrast", &gs.Contrast, 0.f, 2.f))
         device_data.cb_luma_global_settings_dirty = true;
      if (ImGui::IsItemDeactivatedAfterEdit())
         reshade::set_config_value(nullptr, NAME, "Contrast", gs.Contrast);
      if (ImGui::IsItemHovered())
         ImGui::SetTooltip("Overall image contrast, HDR only (1 = vanilla).");
      if (DrawResetButton<float, false>(gs.Contrast, gd_def.Contrast, "Contrast"))
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
      if (DrawResetButton<float, false>(gs.Saturation, gd_def.Saturation, "Saturation"))
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
      if (DrawResetButton<float, false>(gs.HighlightDechroma, gd_def.HighlightDechroma, "HighlightDechroma"))
      {
         device_data.cb_luma_global_settings_dirty = true;
         reshade::set_config_value(nullptr, NAME, "HighlightDechroma", gs.HighlightDechroma);
      }

      ImGui::SeparatorText("Bloom");
      if (ImGui::Checkbox("Luma Bloom Enable", &g_luma_bloom_enable))
      {
         gs.LumaBloomEnable = g_luma_bloom_enable ? 1.f : 0.f;
         device_data.cb_luma_global_settings_dirty = true;
         reshade::set_config_value(nullptr, NAME, "LumaBloomEnable", g_luma_bloom_enable);
      }
      if (ImGui::IsItemHovered())
         ImGui::SetTooltip("Replaces the game's bloom with a wider, softer HDR bloom.");

      // Scales whichever bloom is active: the injected Luma one when enabled, the game's own otherwise.
      if (ImGui::SliderFloat("Bloom Intensity", &g_bloom_intensity, 0.f, 2.f))
         device_data.cb_luma_global_settings_dirty = true;
      if (ImGui::IsItemDeactivatedAfterEdit())
         reshade::set_config_value(nullptr, NAME, "BloomIntensity", g_bloom_intensity);
      if (ImGui::IsItemHovered())
         ImGui::SetTooltip("Bloom strength (1 = vanilla, 0 = none).");
      if (DrawResetButton<float, false>(g_bloom_intensity, 1.f, "BloomIntensity"))
      {
         device_data.cb_luma_global_settings_dirty = true;
         reshade::set_config_value(nullptr, NAME, "BloomIntensity", g_bloom_intensity);
      }

      ImGui::SeparatorText("Effects");
      if (ImGui::SliderFloat("Vignette Intensity", &gs.VignetteIntensity, 0.f, 1.f))
         device_data.cb_luma_global_settings_dirty = true;
      if (ImGui::IsItemDeactivatedAfterEdit())
         reshade::set_config_value(nullptr, NAME, "VignetteIntensity", gs.VignetteIntensity);
      if (ImGui::IsItemHovered())
         ImGui::SetTooltip("Scales the game's vignette darkening (1 = vanilla, 0 = none).");
      if (DrawResetButton<float, false>(gs.VignetteIntensity, gd_def.VignetteIntensity, "VignetteIntensity"))
      {
         device_data.cb_luma_global_settings_dirty = true;
         reshade::set_config_value(nullptr, NAME, "VignetteIntensity", gs.VignetteIntensity);
      }

      if (ImGui::Checkbox("Video AutoHDR", &g_video_auto_hdr_enable))
      {
         gs.VideoAutoHDREnable = g_video_auto_hdr_enable ? 1.f : 0.f;
         device_data.cb_luma_global_settings_dirty = true;
         reshade::set_config_value(nullptr, NAME, "VideoAutoHDREnable", g_video_auto_hdr_enable);
      }
      if (ImGui::IsItemHovered())
         ImGui::SetTooltip("Adds HDR highlights to pre-rendered videos (HDR only).");

      ImGui::BeginDisabled(!g_video_auto_hdr_enable);
      if (ImGui::SliderFloat("Video HDR Boost", &gs.VideoAutoHDRBoost, 0.f, 1.f))
         device_data.cb_luma_global_settings_dirty = true;
      if (ImGui::IsItemDeactivatedAfterEdit())
         reshade::set_config_value(nullptr, NAME, "VideoAutoHDRBoost", gs.VideoAutoHDRBoost);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("Video highlight strength (0 = off).");
      if (DrawResetButton<float, false>(gs.VideoAutoHDRBoost, gd_def.VideoAutoHDRBoost, "VideoAutoHDRBoost"))
      {
         device_data.cb_luma_global_settings_dirty = true;
         reshade::set_config_value(nullptr, NAME, "VideoAutoHDRBoost", gs.VideoAutoHDRBoost);
      }
      ImGui::EndDisabled();

      // Gated on DisplayMode == 1 in Luma_BL2TPS_Tonemap.hlsl, so it only does anything on the HDR output.
      bool dithering = gs.Dithering > 0.5f;
      if (ImGui::Checkbox("Dithering", &dithering))
      {
         gs.Dithering = dithering ? 1.f : 0.f;
         device_data.cb_luma_global_settings_dirty = true;
         reshade::set_config_value(nullptr, NAME, "Dithering", gs.Dithering);
      }
      if (ImGui::IsItemHovered())
         ImGui::SetTooltip("Reduces gradient banding (HDR output).");

      ImGui::SeparatorText("UI");
      if (ImGui::Checkbox("Hide Gameplay UI", &g_hide_ui))
         reshade::set_config_value(nullptr, NAME, "HideUI", g_hide_ui);
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
