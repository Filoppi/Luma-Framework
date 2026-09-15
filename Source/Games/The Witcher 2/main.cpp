// The Witcher 2: Assassins of Kings Enhanced Edition — Luma HDR mod (REDengine, 32-bit, DX9 -> D3D11 via dgVoodoo2).
//
// Hashes are the dgVoodoo-TRANSLATED ones and change with every wrapper build: 2.87.3 and 2.81.3 are keyed
// (2.81.3 emits ps_4_0 for the same passes), any other build needs a re-dump.
// The post chain is fp16 throughout and linear until the final grade's vMidtone power encodes it; the "tonemap" is
// an adaptive exposure multiply, no curve or clamp. The UI blends src-alpha onto the graded gamma canvas; the main
// menu draws no tonemap at all.
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

// Every pass is keyed under both dgVoodoo builds: 2.87.3, and 2.81.3 (the build that runs under Proton), which emits
// ps_4_0 and therefore different hashes. Dump-verified as signature-identical: same interpolators, t/s registers and cb
// slots, so the replacements and the slot-based captures are shared.
struct DgVoodooHashes
{
   uint32_t v2873;
   uint32_t v2813;
};

// Tonemap ("exposure") permutations.
static constexpr DgVoodooHashes kTonemapExposure = {0x91348C0F, 0x6CF3E8B7};   // DX9 0xC5ADBC35: exposure+scale, alpha passthrough
static constexpr DgVoodooHashes kTonemapBrightPass = {0x00E31BF9, 0xB293C5B1}; // DX9 0xF01A691E: bloom bright-pass (threshold ramp, saturation, colour)
// Two static permutations (exposure from PSC_LumRanges) have never been captured; their signature is 1 texture,
// dp4 cb4[58], min cap cb4[59].x. Not declared as 0: an absent pipeline hash reads as 0 and would match.
// Final grade (FXAA + colour balance + split toning + vignette), last pass before UI; hosts the HDR block and the SMAA hook.
static constexpr DgVoodooHashes kFinalGrade = {0xDE5CF9CD, 0x517DC6D5};
static constexpr DgVoodooHashes kFinalGradeNoAA = {0xCF3B72A9, 0xBBFEC706};       // game AA off: no FXAA block, scene alpha passed through
static constexpr DgVoodooHashes kFinalGradeNoVignette = {0xBABBFFAD, 0x2CA0631E}; // no FXAA and no vignette
// FXAA on, vignette off: the fourth corner of the 2x2 permutation matrix.
static constexpr DgVoodooHashes kFinalGradeAANoVignette = {0x058E2498, 0xA966D512};
// Native SSAO generator (HBAO variant, VS 0x5D9D0449): half-res r32_float LINEAR view depth at t0 -> half-res
// r8g8b8a8 (.x = AO, .y = viewZ). Only this draw is replaced; the vanilla chain downstream reads just .x:
// pack 0x953119B5 -> ping-pong 0xC131C40D x2 -> blur 0xD01CBD13 x2 -> apply 0x5C63E1C2.
// Needs SSAO on in the game's video settings.
static constexpr DgVoodooHashes kAOGen = {0x3FEEC0F7, 0x6EC596CA};
// AO pack (t0 = full-res r32_float LINEAR depth, t1 = the AO target): depth-capture fallback for SMAA
// predication, since it runs every frame while the tonemap capture only fires on the BRIGHT-PASS perm.
static constexpr DgVoodooHashes kAOPack = {0x953119B5, 0x495E9133};

// The engine's glow chain (halo around candles and torches, distinct from the god rays): copy 0x5A8E5532 and the
// 12-tap blur 0x88C500CF x2 stay vanilla; the screen blend 0x12931281 is replaced (LightShaftBlend_0x12931281).

// User settings, persisted in the [Luma] config section (LoadConfigs) unless noted otherwise.
static bool g_smaa_enable = true;
static float g_rcas_sharpness = 0.f;   // RCAS sharpen on SMAA output (0 = off)
static bool g_smaa_predication = true; // SMAA depth predication (r32f depth captured at the bright-pass tonemap or the AO pack pass)
// Plane deviation counted as a full edge, as a fraction of view depth, so it is resolution independent.
// 0.02 = ~14 cm at the measured 7 m median depth; XeGTAO uses 0.011 and ASSAO 0.040 for the same test.
static float g_smaa_pred_tolerance = 0.02f;
static bool g_gtao_enable = true; // XeGTAO replaces the native SSAO generator (kAOGen)
static bool g_hide_ui = false;    // hide the game's HUD (for clean screenshots); session-only, never persisted

// XeGTAO knobs CB slot, must match "register(b9)" in Luma_TW2_XeGTAO.hlsl. Not b11: core's DrawBloom owns
// that slot for its own constants.
static constexpr UINT kGTAOKnobsCBSlot = 9;
// XeGTAO calibration knobs (DEV sliders). DepthScale and RadiusOverride ship at their calibrated values;
// FinalValuePower does NOT - 2.2 is a preference, while 1.0 is the value whose AO histogram matches the
// game's own HBAO (mean 0.90 against native 0.89).
static float g_gtao_final_value_power = 2.2f; // primary darkness dial (higher = darker)
static float g_gtao_depth_scale = 1.f;        // viewZ divisor (game units -> ~meters); dial against broad over-occlusion
static float g_gtao_radius_override = 0.f;    // > 0 overrides the shader's EFFECT_RADIUS (view units after DepthScale)
#if DEVELOPMENT
static int g_gtao_debug_view = 0; // 0=off 1=depth gradient 2=normals 3=AO x8 4=edges (the shader's debug blocks are DEVELOPMENT-only too)
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
   // The one resolution every surface below is sized to, core's own DrawSMAA intermediates included: they all come
   // from the same canvas, so a change drops the lot and the per-pointer checks rebuild it.
   uint32_t scratch_w = 0, scratch_h = 0;
   // SMAA metrics CB (b1) = (1/w,1/h,w,h) + (predication scale,0,0,0); scale 2.0 when predication on, else 1.0.
   ComPtr<ID3D11Buffer> cb_smaa_metrics;
   // SMAA scratch. tex_input = SRV snapshot of the canvas (gamma, for edge detection); tex_input_linear = its
   // linear-light decode, for the neighborhood blend.
   ComPtr<ID3D11Texture2D> tex_input;
   ComPtr<ID3D11ShaderResourceView> srv_input;
   ComPtr<ID3D11Texture2D> tex_input_linear;
   ComPtr<ID3D11UnorderedAccessView> uav_input_linear;
   ComPtr<ID3D11ShaderResourceView> srv_input_linear;
   // RCAS input temp (SRV+RTV), allocated ONLY while sharpening is on: with RCAS off, SMAA's last pass writes
   // the canvas directly and this stays null.
   ComPtr<ID3D11Texture2D> tex_smaa_out;
   ComPtr<ID3D11RenderTargetView> tex_smaa_out_rtv;
   ComPtr<ID3D11ShaderResourceView> tex_smaa_out_srv;
   // RCAS sharpen CB (b0) = (w,h,sharpness,0) + output temp (canvas format, RTV).
   ComPtr<ID3D11Buffer> cb_sharpen;
   float sharpen_amount = 0.f; // the value cb_sharpen was built with; meaningful only while cb_sharpen exists
   // Full-res r32_float depth, captured at whichever comes first: the BRIGHT-PASS tonemap draw (t1) or the AO
   // pack pass (t0). tex_pred is the R16F edge-ness from the Depth Extract CS, not a depth.
   ComPtr<ID3D11ShaderResourceView> srv_scene_depth;
   ComPtr<ID3D11Texture2D> tex_pred;
   ComPtr<ID3D11UnorderedAccessView> uav_pred;
   ComPtr<ID3D11ShaderResourceView> srv_pred;
   ComPtr<ID3D11Buffer> cb_pred;
   // The values cb_pred and cb_smaa_metrics were built with; meaningful only while that CB exists.
   float pred_tolerance = 0.f;
   float smaa_metrics_pred_scale = 0.f; // recreate the metrics CB when predication turns on/off

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
   // The size and format the set was built for. Committed even when the allocation fails, so a null set under a
   // matching triple means "failed" and no per-frame retry fragments the 32-bit address space. ReleaseGTAOScratch
   // clears it.
   uint32_t gtao_w = 0, gtao_h = 0;
   DXGI_FORMAT gtao_final_fmt = DXGI_FORMAT_UNKNOWN; // actual (possibly Luma-upgraded) AO RT format
   ComPtr<ID3D11Buffer> cb_gtao;                     // knobs + viewport (kGTAOKnobsCBSlot), immutable, recreated on change and with the set
   float gtao_cb_data[8] = {};                       // what cb_gtao was built with; meaningful only while cb_gtao exists

#if DEVELOPMENT
   // ---- Vanilla constant logger (see LogVanillaGrade / LogVanillaTonemap) ----
   // Staging copies are mapped a frame later without waiting, so the render thread never stalls.
   struct ConstantCapture
   {
      ComPtr<ID3D11Buffer> staging;
      UINT bytes = 0;
      bool copy_pending = false;
   };
   ConstantCapture grade_cb;
   ConstantCapture tonemap_cb;
   ConstantCapture aogen_cb;
   ComPtr<ID3D11Texture2D> adaptation_staging; // 1x1 copy of the exposure pass's sLumFinal
   bool adaptation_copy_pending = false;
   std::string last_grade_line;
   std::string last_tonemap_line;
   std::string last_aogen_line;
   uint32_t frame_counter = 0;
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
      }
      tex_gtao_final.reset();
      uav_gtao_final.reset();
      gtao_w = 0;
      gtao_h = 0;
      gtao_final_fmt = DXGI_FORMAT_UNKNOWN;
      cb_gtao.reset(); // it holds the viewport size
   }

   // Turning a feature off gives the address space back: at 4K these hold ~200 MB (SMAA) and ~30 MB (GTAO)
   // in a 32-bit process, and everything is recreated on demand.
   void ReleaseSMAAScratch()
   {
      srv_input.reset();
      tex_input.reset();
      uav_input_linear.reset();
      srv_input_linear.reset();
      tex_input_linear.reset();
      ReleaseSharpenScratch();
   }

   // The RCAS intermediate exists only while sharpening is on: with RCAS at 0 the SMAA chain writes the canvas
   // directly, so this is ~66 MB (4K rgba16f) of pure waste until the next resolution change.
   void ReleaseSharpenScratch()
   {
      tex_smaa_out_rtv.reset();
      tex_smaa_out_srv.reset();
      tex_smaa_out.reset();
      cb_sharpen.reset();
   }

   void ReleasePredicationScratch()
   {
      uav_pred.reset();
      srv_pred.reset();
      tex_pred.reset();
      cb_pred.reset();
   }
};

class TheWitcher2Game final : public Game
{
   static TheWitcher2GameDeviceData& GetGameDeviceData(DeviceData& device_data)
   {
      return *static_cast<TheWitcher2GameDeviceData*>(device_data.game);
   }

   // Matches a pass by both of its keyed dgVoodoo hashes.
   static bool ContainsPixelShader(const ShaderHashesList<OneShaderPerPipeline>& shader_hashes, const DgVoodooHashes& pass)
   {
      return shader_hashes.Contains(pass.v2873, reshade::api::shader_stage::pixel) || shader_hashes.Contains(pass.v2813, reshade::api::shader_stage::pixel);
   }

   static bool IsTonemap(const ShaderHashesList<OneShaderPerPipeline>& shader_hashes)
   {
      return ContainsPixelShader(shader_hashes, kTonemapExposure) || ContainsPixelShader(shader_hashes, kTonemapBrightPass);
   }

#if DEVELOPMENT
   // Another process can own ReShade.log (the BL2 launcher precedent), so every line also goes to our own file
   // beside the exe.
   static void LogVanillaLine(const std::string& line)
   {
      reshade::log::message(reshade::log::level::info, line.c_str());
      std::ofstream file("Luma-TW2.log", std::ios::app);
      if (file)
         file << line << std::endl;
   }

   // Copy the pass's PS cb4 and hand back `row_count` rows of it, one frame late (the BL2 CaptureConstantRows path).
   // False = nothing to read yet, normal on the first calls.
   static bool CaptureConstantRows(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, TheWitcher2GameDeviceData::ConstantCapture& capture, uint32_t first_row, uint32_t row_count, float* out)
   {
      ComPtr<ID3D11Buffer> cb;
      native_device_context->PSGetConstantBuffers(4, 1, cb.put());
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

   // CEnvFinalColorBalanceParameters at the final grade (cb4[60..71] = DX9 c52..c63), logged whenever a value changes.
   // vMidtone should sit near 1/2.2: it is the only power stage between the linear lighting and the gamma-space UI.
   static void LogVanillaGrade(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, TheWitcher2GameDeviceData& gd)
   {
      if ((gd.frame_counter & 15u) != 0u)
         return;
      float rows[12 * 4] = {};
      if (!CaptureConstantRows(native_device, native_device_context, gd.grade_cb, 60, 12, rows))
         return;
      const auto row = [&rows](uint32_t cb4_index)
      {
         const float* v = &rows[(cb4_index - 60) * 4];
         return std::format("({:.4f}, {:.4f}, {:.4f}, {:.4f})", v[0], v[1], v[2], v[3]);
      };
      std::string line = std::format("[TW2-Grade] vHighlight={} vMidtone={} vShadow={} vVignetteWeights={} vVignetteColor={} vSplitToneShadows={} vSplitToneHighlights={} vSplitToneBalance={} vSplitToneRange={}",
         row(60), row(61), row(62), row(66), row(67), row(68), row(69), row(70), row(71));
      if (line == gd.last_grade_line)
         return;
      LogVanillaLine(std::format("{} frame={}", line, gd.frame_counter));
      gd.last_grade_line = std::move(line);
   }

   // The exposure pass at its main draw: PSC_LumWeights and PSC_LumRanges2 (cb4[58..59]) whenever they change, and the
   // 1x1 adaptation texel at t1 every 120 frames, which holds (black, white, gain = 1 / max(white - black, 0.01)).
   static void LogVanillaTonemap(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, TheWitcher2GameDeviceData& gd)
   {
      if ((gd.frame_counter % 120u) != 0u)
         return;
      float rows[2 * 4] = {};
      if (CaptureConstantRows(native_device, native_device_context, gd.tonemap_cb, 58, 2, rows))
      {
         std::string line = std::format("[TW2-Tonemap] PSC_LumWeights=({:.4f}, {:.4f}, {:.4f}, {:.4f}) PSC_LumRanges2=({:.4f}, {:.4f}, {:.4f}, {:.4f})", rows[0], rows[1], rows[2], rows[3], rows[4], rows[5], rows[6], rows[7]);
         if (line != gd.last_tonemap_line)
         {
            LogVanillaLine(std::format("{} frame={}", line, gd.frame_counter));
            gd.last_tonemap_line = std::move(line);
         }
      }

      // Validated on every call, not once: anything but a 1x1 fp16 texel would decode as garbage.
      ComPtr<ID3D11ShaderResourceView> srv;
      native_device_context->PSGetShaderResources(1, 1, srv.put());
      if (!srv)
         return;
      ComPtr<ID3D11Resource> res;
      srv->GetResource(res.put());
      ComPtr<ID3D11Texture2D> tex;
      if (!res || FAILED(res->QueryInterface(tex.put())))
         return;
      D3D11_TEXTURE2D_DESC td = {};
      tex->GetDesc(&td);
      if (td.Format != DXGI_FORMAT_R16G16B16A16_FLOAT || td.Width != 1 || td.Height != 1)
         return;

      if (gd.adaptation_copy_pending)
      {
         D3D11_MAPPED_SUBRESOURCE mapped = {};
         if (SUCCEEDED(native_device_context->Map(gd.adaptation_staging.get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped)) && mapped.pData != nullptr)
         {
            const uint16_t* texel = static_cast<const uint16_t*>(mapped.pData);
            LogVanillaLine(std::format("[TW2-Adaptation] black={:.5f} white={:.5f} gain={:.5f} frame={}", DirectX::PackedVector::XMConvertHalfToFloat(texel[0]),
               DirectX::PackedVector::XMConvertHalfToFloat(texel[1]), DirectX::PackedVector::XMConvertHalfToFloat(texel[2]), gd.frame_counter));
            native_device_context->Unmap(gd.adaptation_staging.get(), 0);
            gd.adaptation_copy_pending = false;
         }
      }
      if (!gd.adaptation_copy_pending)
      {
         if (!gd.adaptation_staging)
         {
            D3D11_TEXTURE2D_DESC sd = td;
            sd.MipLevels = 1;
            sd.ArraySize = 1;
            sd.Usage = D3D11_USAGE_STAGING;
            sd.BindFlags = 0;
            sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            sd.MiscFlags = 0;
            if (FAILED(native_device->CreateTexture2D(&sd, nullptr, gd.adaptation_staging.put())))
               return;
         }
         native_device_context->CopySubresourceRegion(gd.adaptation_staging.get(), 0, 0, 0, 0, tex.get(), 0, nullptr);
         gd.adaptation_copy_pending = true;
      }
   }

   // The native SSAO generator's layout and constants, logged on change. Meaning read from its disassembly (NVIDIA HBAO
   // shape): cb4[8] = AO target (W, H, 1/W, 1/H), cb4[9] = (1/tanX, 1/tanY, tanX, tanY), cb4[10] = (directions, steps,
   // tan angle bias, angle bias), cb4[11] = (R, R^2, 1/R, H/W), cb4[14] = (noise tile scale, distance fade rate, fade
   // amount, attenuation), cb4[15].y = contrast, cb4[16].y = max kernel radius in AO target pixels. XeGTAO reuses only
   // cb4[9].zw; the rest are candidates for following the game per environment.
   static void LogAOGenLayout(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, const DeviceData& device_data, TheWitcher2GameDeviceData& gd)
   {
      if ((gd.frame_counter & 15u) != 0u)
         return;
      float rows[9 * 4] = {};
      if (!CaptureConstantRows(native_device, native_device_context, gd.aogen_cb, 8, 9, rows))
         return;
      D3D11_VIEWPORT viewport = {};
      UINT viewport_count = 1;
      native_device_context->RSGetViewports(&viewport_count, &viewport);
      ComPtr<ID3D11RenderTargetView> rtv;
      native_device_context->OMGetRenderTargets(1, rtv.put(), nullptr);
      ComPtr<ID3D11ShaderResourceView> srv_depth;
      native_device_context->PSGetShaderResources(0, 1, srv_depth.put());
      uint4 rt_info{}, depth_info{};
      DXGI_FORMAT rt_fmt = DXGI_FORMAT_UNKNOWN, depth_fmt = DXGI_FORMAT_UNKNOWN;
      GetResourceInfo(rtv.get(), rt_info, rt_fmt);
      GetResourceInfo(srv_depth.get(), depth_info, depth_fmt);
      const auto row = [&rows](uint32_t cb4_index)
      {
         const float* v = &rows[(cb4_index - 8) * 4];
         return std::format("({:.6f}, {:.6f}, {:.6f}, {:.6f})", v[0], v[1], v[2], v[3]);
      };
      std::string line = std::format("[TW2-AOGen] output={}x{} viewport=({:.1f}, {:.1f}) {:.1f}x{:.1f} rt={}x{} depth={}x{} cb4[8]={} cb4[9]={} cb4[10]={} cb4[11]={} cb4[14]={} cb4[15]={} cb4[16]={}",
         (uint32_t)device_data.output_resolution.x, (uint32_t)device_data.output_resolution.y, viewport.TopLeftX, viewport.TopLeftY, viewport.Width, viewport.Height,
         rt_info.x, rt_info.y, depth_info.x, depth_info.y, row(8), row(9), row(10), row(11), row(14), row(15), row(16));
      if (line == gd.last_aogen_line)
         return;
      LogVanillaLine(std::format("{} frame={}", line, gd.frame_counter));
      gd.last_aogen_line = std::move(line);
   }
#endif

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

   // Any of the four final-grade permutations (FXAA and vignette are compiled in or out independently); all
   // four host the Luma HDR block through the same shader file.
   static bool IsFinalGrade(const ShaderHashesList<OneShaderPerPipeline>& shader_hashes)
   {
      return ContainsPixelShader(shader_hashes, kFinalGrade) || ContainsPixelShader(shader_hashes, kFinalGradeNoAA) || ContainsPixelShader(shader_hashes, kFinalGradeNoVignette) || ContainsPixelShader(shader_hashes, kFinalGradeAANoVignette);
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

      const bool rt0_blending = bd.RenderTarget[0].BlendEnable != FALSE;
#if !DEVELOPMENT
      // Only the "RT0 off, RTn on" shape is repaired, so outside DEVELOPMENT a blending RT0 skips the scan and the RT
      // query (the inverse shape is only ever logged).
      if (rt0_blending)
         return DrawOrDispatchOverrideType::None;
#endif

      // Only BOUND targets count: the wrapper leaves stale BlendEnable in unused descriptor slots, which alone
      // matches nearly every draw and would take over passes other hooks own. Free descriptor scan first.
      bool disagreement = false;
      for (UINT i = 1; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT && !disagreement; i++)
         disagreement = bd.RenderTarget[i].BlendEnable != bd.RenderTarget[0].BlendEnable;
      if (!disagreement)
         return DrawOrDispatchOverrideType::None;

      ID3D11RenderTargetView* rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
      native_device_context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtvs, nullptr);
      // RT0's blend bit is loop-invariant, so the two shapes are mutually exclusive: one flag out of the loop.
      bool bound_disagreement = false;
      for (UINT i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; i++)
      {
         if (rtvs[i] == nullptr)
            continue;
         bound_disagreement |= i > 0 && bd.RenderTarget[i].BlendEnable != bd.RenderTarget[0].BlendEnable;
         rtvs[i]->Release(); // OMGetRenderTargets hands back references; only the bound/not-bound answer is kept
      }
      const bool needs_fix = bound_disagreement && !rt0_blending;
      [[maybe_unused]] const bool inverse_shape = bound_disagreement && rt0_blending;

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
   // Core's DrawSMAA intermediates, ~83 MB at 4K, sized from the RTV handed to them and dropped only on swapchain
   // init, not on this canvas' resize. The SRVs hold their own reference, so release both.
   static void ReleaseCoreSMAAIntermediates(DeviceData& device_data)
   {
      auto& mr = device_data.managed_resources;
      mr.depth_stencil_views[CompileTimeStringHash("smaa_dsv")].reset();
      mr.render_target_views[CompileTimeStringHash("smaa_edge_detection")].reset();
      mr.render_target_views[CompileTimeStringHash("smaa_blending_weight_calculation")].reset();
      mr.shader_resource_views[CompileTimeStringHash("smaa_edge_detection")].reset();
      mr.shader_resource_views[CompileTimeStringHash("smaa_blending_weight_calculation")].reset();
   }

   // SMAA on the graded gamma canvas, after the original final-grade draw and before the UI draws on it; the
   // replaced grade skipped its own FXAA via LumaData.CustomData2.
   void RunPostFinalGradeSMAA(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, DeviceData& device_data, TheWitcher2GameDeviceData& gd, ID3D11Resource* canvas_res, ID3D11RenderTargetView* canvas_rtv)
   {
      uint4 cinfo{};
      DXGI_FORMAT cfmt = DXGI_FORMAT_UNKNOWN;
      GetResourceInfo(canvas_res, cinfo, cfmt);
      uint32_t w = cinfo.x, h = cinfo.y;
      if (w == 0 || h == 0 || cfmt == DXGI_FORMAT_UNKNOWN)
      {
         return;
      }

      // Skip SMAA this frame if a pass is still missing (async loader / live reload).
      auto* linearize_cs = FindShader(device_data.native_compute_shaders, CompileTimeStringHash("TW2 SMAA Linearize CS"));
      const bool smaa_ready = linearize_cs != nullptr &&
                              AllShadersReady(device_data.native_pixel_shaders, {CompileTimeStringHash("SMAA Edge Detection PS"), CompileTimeStringHash("SMAA Blending Weight Calculation PS"), CompileTimeStringHash("SMAA Neighborhood Blending PS")}) &&
                              AllShadersReady(device_data.native_vertex_shaders, {CompileTimeStringHash("SMAA Edge Detection VS"), CompileTimeStringHash("SMAA Blending Weight Calculation VS"), CompileTimeStringHash("SMAA Neighborhood Blending VS")});
      if (!smaa_ready)
      {
         return;
      }

      // One resolution latch: every surface here is canvas-sized, so a change drops the lot, core's intermediates
      // included, and the per-pointer checks below rebuild it.
      if (gd.scratch_w != w || gd.scratch_h != h)
      {
         gd.ReleaseSMAAScratch();
         gd.ReleasePredicationScratch();
         ReleaseCoreSMAAIntermediates(device_data);
         gd.cb_smaa_metrics.reset(); // it holds the resolution itself
         gd.scratch_w = w;
         gd.scratch_h = h;
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
            const float pred_params[4] = {g_smaa_pred_tolerance, 0.f, 0.f, 0.f};
            if (CreateImmutableCB(native_device, pred_params, sizeof(pred_params), gd.cb_pred))
               gd.pred_tolerance = g_smaa_pred_tolerance;
         }
         if (!gd.tex_pred && CreateDefaultTex(native_device, w, h, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS, gd.tex_pred, DXGI_FORMAT_R16_FLOAT))
         {
            native_device->CreateUnorderedAccessView(gd.tex_pred.get(), nullptr, gd.uav_pred.put());
            native_device->CreateShaderResourceView(gd.tex_pred.get(), nullptr, gd.srv_pred.put());
         }
         pred_ok = gd.cb_pred && gd.uav_pred && gd.srv_pred;
      }

      // Metrics CB: predication scale 2.0 when active, else 1.0. Recreate on resolution or predication flip.
      const float pred_scale = pred_ok ? 2.0f : 1.0f;
      if (!gd.cb_smaa_metrics || gd.smaa_metrics_pred_scale != pred_scale)
      {
         const float metrics[8] = {1.f / (float)w, 1.f / (float)h, (float)w, (float)h, pred_scale, 0.f, 0.f, 0.f};
         if (CreateImmutableCB(native_device, metrics, sizeof(metrics), gd.cb_smaa_metrics))
            gd.smaa_metrics_pred_scale = pred_scale;
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
         if (!gd.cb_sharpen || gd.sharpen_amount != g_rcas_sharpness)
         {
            const float sharpen_params[4] = {(float)w, (float)h, g_rcas_sharpness, 0.f};
            if (CreateImmutableCB(native_device, sharpen_params, sizeof(sharpen_params), gd.cb_sharpen))
               gd.sharpen_amount = g_rcas_sharpness;
         }
         if (!gd.tex_smaa_out && CreateDefaultTex(native_device, w, h, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET, gd.tex_smaa_out, cfmt))
         {
            native_device->CreateRenderTargetView(gd.tex_smaa_out.get(), nullptr, gd.tex_smaa_out_rtv.put());
            native_device->CreateShaderResourceView(gd.tex_smaa_out.get(), nullptr, gd.tex_smaa_out_srv.put());
         }
         if (!gd.cb_sharpen || !gd.tex_smaa_out_rtv || !gd.tex_smaa_out_srv)
            do_sharpen = false;
      }

      if (!gd.tex_input && CreateDefaultTex(native_device, w, h, D3D11_BIND_SHADER_RESOURCE, gd.tex_input, cfmt))
         native_device->CreateShaderResourceView(gd.tex_input.get(), nullptr, gd.srv_input.put());
      if (!gd.tex_input_linear && CreateDefaultTex(native_device, w, h, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS, gd.tex_input_linear, DXGI_FORMAT_R16G16B16A16_FLOAT))
      {
         native_device->CreateUnorderedAccessView(gd.tex_input_linear.get(), nullptr, gd.uav_input_linear.put());
         native_device->CreateShaderResourceView(gd.tex_input_linear.get(), nullptr, gd.srv_input_linear.put());
      }
      if (!gd.srv_input || !gd.uav_input_linear || !gd.srv_input_linear)
         return;

      // Snapshot the canvas color: the chain writes the canvas, so it must sample this copy, not the canvas.
      native_device_context->CopyResource(gd.tex_input.get(), canvas_res);

      // Linear-light decode of the snapshot for the neighborhood blend (Luma_TW2_SMAALinearize.hlsl).
      {
         DrawStateStack<DrawStateStackType::Compute> linearize_state;
         linearize_state.Cache(native_device_context, device_data.uav_max_count);
         native_device_context->CSSetUnorderedAccessViews(0, 1, gd.uav_input_linear.get_addressof(), nullptr);
         native_device_context->CSSetShaderResources(0, 1, gd.srv_input.get_addressof());
         native_device_context->CSSetShader(linearize_cs, nullptr, 0);
         native_device_context->Dispatch((w + 7) / 8, (h + 7) / 8, 1);
         linearize_state.Restore(native_device_context);
      }

      // Predication signal: the game's linear r32f depth -> plane-deviation edge-ness in R16F (gd.tex_pred);
      // see Luma_TW2_DepthExtract.hlsl for why this is an edge test rather than a depth rescale.
      if (pred_ok)
      {
         DrawStateStack<DrawStateStackType::Compute> pred_cs_state;
         pred_cs_state.Cache(native_device_context, device_data.uav_max_count);

         native_device_context->CSSetShaderResources(0, 1, gd.srv_scene_depth.get_addressof());
         native_device_context->CSSetUnorderedAccessViews(0, 1, gd.uav_pred.get_addressof(), nullptr);
         native_device_context->CSSetConstantBuffers(0, 1, gd.cb_pred.get_addressof());
         native_device_context->CSSetShader(pred_cs, nullptr, 0);
         native_device_context->Dispatch((w + 7) / 8, (h + 7) / 8, 1);

         pred_cs_state.Restore(native_device_context);
      }

      // SMAA (3 passes). Metrics CB at VS+PS b1 (DrawSMAA restores VS/PS/SRVs/RTs, not cbuffers).
      ComPtr<ID3D11Buffer> vs_cb1_orig, ps_cb1_orig;
      native_device_context->VSGetConstantBuffers(1, 1, vs_cb1_orig.put());
      native_device_context->PSGetConstantBuffers(1, 1, ps_cb1_orig.put());
      native_device_context->VSSetConstantBuffers(1, 1, gd.cb_smaa_metrics.get_addressof());
      native_device_context->PSSetConstantBuffers(1, 1, gd.cb_smaa_metrics.get_addressof());

      // The last pass of the chain renders straight into the canvas RTV — no write-back copy. Reading the
      // canvas is safe because SMAA/RCAS sample the snapshot (tex_input), never the canvas itself.
      DrawSMAA(native_device, native_device_context, device_data,
         do_sharpen ? gd.tex_smaa_out_rtv.get() : canvas_rtv, gd.srv_input_linear.get(), gd.srv_input.get(),
         pred_ok ? gd.srv_pred.get() : nullptr /*predication signal*/);

      if (do_sharpen)
      {
         DrawStateStack<DrawStateStackType::FullGraphics> sharpen_state;
         sharpen_state.Cache(native_device_context, device_data.uav_max_count);

         native_device_context->PSSetConstantBuffers(0, 1, gd.cb_sharpen.get_addressof());
         DrawCustomPixelShader(native_device_context, device_data.default_depth_stencil_state.get(), device_data.default_blend_state.get(), nullptr,
            sharpen_vs, sharpen_ps, gd.tex_smaa_out_srv.get(), canvas_rtv, w, h, false);

         sharpen_state.Restore(native_device_context);
      }

      native_device_context->VSSetConstantBuffers(1, 1, vs_cb1_orig.get_addressof());
      native_device_context->PSSetConstantBuffers(1, 1, ps_cb1_orig.get_addressof());
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
      // Read the ACTUAL descriptor: our own indirect upgrade can make this RT rgba16_float while ReShade
      // metadata still reports the original, and CopyResource silently no-ops on a family mismatch (frozen AO).
      uint4 rt_info{};
      DXGI_FORMAT rt_fmt = DXGI_FORMAT_UNKNOWN;
      GetResourceInfo(rt_res.get(), rt_info, rt_fmt); // no resource reads as 0x0, which fails the size match
      if (rt_info.x != w || rt_info.y != h)
         return DrawOrDispatchOverrideType::None;
      // Typeless -> typed within the same family (so CopyResource stays legal), since our UAV-written copy source needs
      // a typed format. Typed UAV store is only guaranteed for these two, and they are the only formats the upgrade
      // list can produce; anything else would fail at CreateTexture2D.
      const DXGI_FORMAT final_fmt = (DXGI_FORMAT)reshade::api::format_to_default_typed((reshade::api::format)rt_fmt);
      if (final_fmt != DXGI_FORMAT_R8G8B8A8_UNORM && final_fmt != DXGI_FORMAT_R16G16B16A16_FLOAT)
         return DrawOrDispatchOverrideType::None;

      // The XeGTAO shader derives its NDC->view ray scale from the game's cb4 (bound to the PS stage at the
      // hooked draw); it is copied to the CS stage below (in-place takeover, same frame state).
      ComPtr<ID3D11Buffer> game_cb4;
      native_device_context->PSGetConstantBuffers(4, 1, game_cb4.put());
      if (!game_cb4)
         return DrawOrDispatchOverrideType::None;

      // (Re)create the scratch set on first use, resolution change, or RT format change (all-or-nothing).
      if (gd.gtao_w != w || gd.gtao_h != h || gd.gtao_final_fmt != final_fmt)
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

         for (int i = 0; ok && i < 2; i++)
         {
            ok = ok && CreateDefaultTex(native_device, w, h, td.BindFlags, gd.tex_gtao_working[i], DXGI_FORMAT_R8G8_UNORM);
            ok = ok && SUCCEEDED(native_device->CreateUnorderedAccessView(gd.tex_gtao_working[i].get(), nullptr, gd.uav_gtao_working[i].put()));
            ok = ok && SUCCEEDED(native_device->CreateShaderResourceView(gd.tex_gtao_working[i].get(), nullptr, gd.srv_gtao_working[i].put()));
         }

         // Final AO in the game's channel layout (.x = AO), in the RT's ACTUAL format so CopyResource is legal.
         // The shader writes a plain float4 UAV, valid against both unorm8 and fp16.
         ok = ok && CreateDefaultTex(native_device, w, h, td.BindFlags, gd.tex_gtao_final, final_fmt);
         ok = ok && SUCCEEDED(native_device->CreateUnorderedAccessView(gd.tex_gtao_final.get(), nullptr, gd.uav_gtao_final.put()));

         if (!ok)
            gd.ReleaseGTAOScratch(); // drop the partials
         // Commit the target triple either way: a null set under it then reads as "failed", with no retry.
         gd.gtao_w = w;
         gd.gtao_h = h;
         gd.gtao_final_fmt = final_fmt;
      }
      if (!gd.tex_gtao_final)
         return DrawOrDispatchOverrideType::None; // the allocation failed for this size and format: the native draw runs

#if DEVELOPMENT
      const float debug_view = (float)g_gtao_debug_view;
#else
      const float debug_view = 0.f;
#endif
      // Knobs + viewport CB. The viewport rides along so the shader never trusts game constants for it.
      const float knobs[8] = {g_gtao_final_value_power, g_gtao_depth_scale, g_gtao_radius_override, debug_view, 1.f / (float)w, 1.f / (float)h, 0.f, 0.f};
      if (!gd.cb_gtao || memcmp(gd.gtao_cb_data, knobs, sizeof(knobs)) != 0)
      {
         if (CreateImmutableCB(native_device, knobs, sizeof(knobs), gd.cb_gtao))
            memcpy(gd.gtao_cb_data, knobs, sizeof(knobs));
      }
      if (!gd.cb_gtao)
         return DrawOrDispatchOverrideType::None;

      DrawStateStack<DrawStateStackType::Compute> compute_state;
      compute_state.Cache(native_device_context, device_data.uav_max_count);

      native_device_context->CSSetConstantBuffers(4, 1, game_cb4.get_addressof());
      native_device_context->CSSetConstantBuffers(kGTAOKnobsCBSlot, 1, gd.cb_gtao.get_addressof());
      ID3D11SamplerState* point_sampler = device_data.sampler_state_point.get();
      native_device_context->CSSetSamplers(0, 1, &point_sampler);

      static constexpr std::array<ID3D11UnorderedAccessView*, 5> uav_nulls5 = {};
      static constexpr std::array<ID3D11ShaderResourceView*, 2> srv_nulls2 = {};

      // Prefilter game depth into the R32F mip pyramid; each thread covers 2x2 pixels.
      {
         native_device_context->CSSetShaderResources(0, 2, srv_nulls2.data());
         ID3D11UnorderedAccessView* uavs[5] = {gd.gtao_depth_mip_uavs[0].get(), gd.gtao_depth_mip_uavs[1].get(),
            gd.gtao_depth_mip_uavs[2].get(), gd.gtao_depth_mip_uavs[3].get(), gd.gtao_depth_mip_uavs[4].get()};
         native_device_context->CSSetUnorderedAccessViews(0, 5, uavs, nullptr);
         native_device_context->CSSetShaderResources(0, 1, srv_depth.get_addressof());
         native_device_context->CSSetShader(cs_prefilter, nullptr, 0);
         native_device_context->Dispatch((w + 15) / 16, (h + 15) / 16, 1);
         native_device_context->CSSetUnorderedAccessViews(0, 5, uav_nulls5.data(), nullptr);
      }
      // Bind each destination UAV before its source SRVs: D3D11 otherwise keeps the previous UAV hazard and
      // silently nulls the conflicting SRV. Normals are generated from depth inside the shader.
      {
         native_device_context->CSSetShaderResources(0, 2, srv_nulls2.data());
         native_device_context->CSSetUnorderedAccessViews(0, 1, gd.uav_gtao_working[0].get_addressof(), nullptr);
         native_device_context->CSSetShaderResources(0, 1, gd.srv_gtao_depth_mips.get_addressof());
         native_device_context->CSSetShader(cs_main, nullptr, 0);
         native_device_context->Dispatch((w + 7) / 8, (h + 7) / 8, 1);
      }
      // First denoiser writes working1, two horizontal pixels per thread.
      {
         native_device_context->CSSetShaderResources(0, 2, srv_nulls2.data());
         native_device_context->CSSetUnorderedAccessViews(0, 1, gd.uav_gtao_working[1].get_addressof(), nullptr);
         native_device_context->CSSetShaderResources(0, 1, gd.srv_gtao_working[0].get_addressof());
         native_device_context->CSSetShader(cs_denoise_1, nullptr, 0);
         native_device_context->Dispatch((w + 15) / 16, (h + 7) / 8, 1);
      }
      {
         native_device_context->CSSetShaderResources(0, 2, srv_nulls2.data());
         native_device_context->CSSetUnorderedAccessViews(0, 1, gd.uav_gtao_final.get_addressof(), nullptr);
         native_device_context->CSSetShaderResources(0, 1, gd.srv_gtao_working[1].get_addressof());
         native_device_context->CSSetShader(cs_denoise_2, nullptr, 0);
         native_device_context->Dispatch((w + 15) / 16, (h + 7) / 8, 1);
         native_device_context->CSSetUnorderedAccessViews(0, 1, uav_nulls5.data(), nullptr);
      }

      compute_state.Restore(native_device_context);

      // The destination is still bound as the draw's render target, and copying into an OM-bound resource is a
      // D3D11 hazard the runtime does not resolve: unbind around the copy, then restore the game's binding.
      {
         ComPtr<ID3D11DepthStencilView> dsv_orig;
         native_device_context->OMGetRenderTargets(0, nullptr, dsv_orig.put());
         native_device_context->OMSetRenderTargets(0, nullptr, nullptr);
         native_device_context->CopyResource(rt_res.get(), gd.tex_gtao_final.get());
         native_device_context->OMSetRenderTargets(1, rtv.get_addressof(), dsv_orig.get());
      }

      return DrawOrDispatchOverrideType::Replaced;
   }

public:
   void OnInit(bool async) override
   {
      // Game-specific HDR toggle, read by the tonemap, glow/shaft blend and final grade replacements.
      std::vector<ShaderDefineData> game_shader_defines_data = {
         {"TONEMAP_TYPE", '1', true, false, "0 - SDR: Vanilla (bit-exact reference)\n1 - HDR: extended native grade + MacLeod-Boynton hue + DICE display map", 1},
         {"XE_GTAO_QUALITY", '3', true, false, "XeGTAO quality (slice count)\n0 - Low\n1 - Medium\n2 - High\n3 - Very High\n4 - Ultra", 4},
      };
      shader_defines_data.append_range(game_shader_defines_data);
      assert(shader_defines_data.size() < MAX_SHADER_DEFINES);

      // DX9-era gamma-2.2 SDR: the scene is linear until the final grade's vMidtone power encodes it (1/2.2 in the
      // neutral environment), so the canvas the HUD blends onto is GAMMA, like vanilla. The final grade pre-scales by
      // GamePaperWhite/UIPaperWhite (UI_DRAW_TYPE 2) so the HUD lands at its own level.
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

      // Grade controls: Exposure in Luma_TW2_Tonemap.hlsl, the video knobs in Video_0x30BE6D87, the rest in the final
      // grade. Vanilla no-ops by default apart from Dithering and Video AutoHDR. No highlight-expansion knob: the fp16
      // overshoot already reaches ~2-6x.
      default_luma_global_game_settings.Exposure = 1.f;              // scene multiplier (1x)
      default_luma_global_game_settings.Saturation = 1.f;            // saturation around luminance
      default_luma_global_game_settings.HighlightDechroma = 0.f;     // off; only mandatory DICE/gamut desat applies
      default_luma_global_game_settings.Dithering = 1.f;             // animated triangular dither at output (HDR and SDR), anti-banding on
      default_luma_global_game_settings.VideoAutoHDREnable = 1.f;    // light AutoHDR on pre-rendered videos (HDR only)
      default_luma_global_game_settings.VideoAutoHDRBoost = 0.5f;    // highlight-expansion strength (peak ~165 nits at 0.5)
      default_luma_global_game_settings.VignetteIntensity = 1.f;     // vanilla vignette darkening (0 = none)
      default_luma_global_game_settings.Contrast = 1.f;              // slope contrast around 18% mid-gray (HDR path)
      default_luma_global_game_settings.BloomIntensity = 1.f;        // engine glow scale (0 = no halo around lights)
      default_luma_global_game_settings.ColorGradingIntensity = 1.f; // vanilla shadow/highlight split toning strength (0 = no split toning)
      cb_luma_global_settings.GameSettings = default_luma_global_game_settings;

#if ENABLE_SMAA
      // Core auto-registers the 6 SMAA passes. Edge detection reads the GAMMA canvas; the blend reads its linear
      // decode.
      native_shaders_definitions.emplace(CompileTimeStringHash("TW2 SMAA Linearize CS"),
         ShaderDefinition("Luma_TW2_SMAALinearize", reshade::api::pipeline_subobject_type::compute_shader));
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
      const bool is_final_grade = IsFinalGrade(original_shader_hashes);

      // After the final grade the HUD is the only alpha-blended geometry while the post chain is opaque, so
      // keying on blend state within this frame's post-grade span hides it without touching the scene.
      if (g_hide_ui && game_device_data.final_grade_fired_this_frame && !is_custom_pass && !is_final_grade)
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
         // One permutation, two roles: the MAIN grade draws at swapchain size or larger, aux draws feed DoF/flare
         // smaller. Both stay bit-exact vanilla; the role only picks which draw marks main post processing.
         ComPtr<ID3D11RenderTargetView> rtv;
         native_device_context->OMGetRenderTargets(1, rtv.put(), nullptr);
         uint4 rt_info{};
         DXGI_FORMAT rt_fmt = DXGI_FORMAT_UNKNOWN;
         GetResourceInfo(rtv.get(), rt_info, rt_fmt); // no render target reads as 0x0: not main
         // Not an equality test: with UberSampling the scene renders LARGER than the swapchain. Matching
         // aspect plus at-least-swapchain size still excludes the smaller aux targets.
         const UINT out_w = (UINT)device_data.output_resolution.x;
         const UINT out_h = (UINT)device_data.output_resolution.y;
         const bool is_main = rt_info.x >= out_w && rt_info.y >= out_h && out_h != 0 && rt_info.y != 0 && fabsf(((float)rt_info.x / (float)rt_info.y) - ((float)out_w / (float)out_h)) < 0.05f;

         if (is_main)
         {
            device_data.has_drawn_main_post_processing = true;
         }

#if DEVELOPMENT
         if (is_main && ContainsPixelShader(original_shader_hashes, kTonemapExposure))
            LogVanillaTonemap(native_device, native_device_context, game_device_data);
#endif

#if ENABLE_SMAA
         // The bright-pass perm binds the full-res r32_float depth at t1 (declared-but-unused there); capture it for
         // SMAA predication. Bindings are read regardless of the pass being hash-replaced.
         if (g_smaa_predication && ContainsPixelShader(original_shader_hashes, kTonemapBrightPass))
         {
            native_device_context->PSGetShaderResources(1, 1, game_device_data.srv_scene_depth.put());
         }
#endif
      }
      else if (is_final_grade)
      {
         // CustomData2 = SMAA active, which makes the grade skip its built-in FXAA.
         game_device_data.final_grade_fired_this_frame = true; // opens the Hide UI window for the rest of the frame
#if DEVELOPMENT
         LogVanillaGrade(native_device, native_device_context, game_device_data);
#endif

         SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, stages, LumaConstantBufferType::LumaSettings);
         SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, stages, LumaConstantBufferType::LumaData, 0u, g_smaa_enable ? 1u : 0u);
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
      if (ContainsPixelShader(original_shader_hashes, kAOGen))
      {
#if DEVELOPMENT
         LogAOGenLayout(native_device, native_device_context, device_data, game_device_data); // with XeGTAO off too
#endif
         if (g_gtao_enable)
            return RunXeGTAO(native_device, native_device_context, device_data, game_device_data);
      }
#if ENABLE_SMAA
      if (ContainsPixelShader(original_shader_hashes, kAOPack))
      {
         // Fallback depth capture: the AO pack pass binds the same r32_float depth at t0 every frame, while
         // the tonemap capture only fires on the BRIGHT-PASS perm. First capture of the frame wins, same buffer.
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
#if DEVELOPMENT
      game_device_data.frame_counter++;
#endif

      // Give the scratch back when a feature is switched off; it is all lazily recreated. Predication and the
      // RCAS intermediate are separate because either can be off while SMAA runs.
      if (!g_gtao_enable && game_device_data.gtao_w != 0)
         game_device_data.ReleaseGTAOScratch();
#if ENABLE_SMAA
      if (!g_smaa_enable && game_device_data.tex_input)
      {
         game_device_data.ReleaseSMAAScratch();
         ReleaseCoreSMAAIntermediates(device_data);
      }
      if ((!g_smaa_enable || !g_smaa_predication) && game_device_data.tex_pred)
         game_device_data.ReleasePredicationScratch();
      if (g_rcas_sharpness <= 0.f && game_device_data.tex_smaa_out)
         game_device_data.ReleaseSharpenScratch();
      // Per-frame capture: never let a stale depth SRV from a previous scene leak into a frame whose
      // tonemap didn't re-capture it (menus; the SMAA pass then falls back to null predication).
      game_device_data.srv_scene_depth.reset();
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
      DrawResetButton(g_rcas_sharpness, 0.f, "RCASSharpness");
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

      // One grade slider: draw, mark the cbuffer dirty, persist when the edit ends, tooltip, reset (DrawResetButton
      // writes the config itself). AllowWhenDisabled so the tooltip still shows inside a BeginDisabled block.
      const auto slider = [&](const char* label, float* value, float default_value, const char* key, float max_value, const char* tooltip)
      {
         if (ImGui::SliderFloat(label, value, 0.f, max_value))
            device_data.cb_luma_global_settings_dirty = true;
         if (ImGui::IsItemDeactivatedAfterEdit())
            reshade::set_config_value(nullptr, NAME, key, *value);
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", tooltip);
         if (DrawResetButton(*value, default_value, key))
            device_data.cb_luma_global_settings_dirty = true;
      };

      slider("Exposure", &gs.Exposure, gs_def.Exposure, "Exposure", 2.f, "Overall image brightness (1 = vanilla).");
      // Not HDR-gated: the split toning this fades lives in the vanilla grade tail, so it applies in SDR as well.
      slider("Color Grading Intensity", &gs.ColorGradingIntensity, gs_def.ColorGradingIntensity, "ColorGradingIntensity", 1.f, "Strength of the game's own color grading (1 = vanilla, 0 = neutral).");

      // Hidden outside HDR: the final grade's SDR branch skips the whole HDR block, so these would be dead
      // controls. Matches the shader's own "DisplayMode == 1" test.
      if (cb_luma_global_settings.DisplayMode == DisplayModeType::HDR)
      {
         slider("Contrast", &gs.Contrast, gs_def.Contrast, "Contrast", 2.f, "Overall image contrast, HDR only (1 = vanilla).");
         slider("Saturation", &gs.Saturation, gs_def.Saturation, "Saturation", 2.f, "Color saturation, HDR only (1 = vanilla).");
         slider("Highlights Desaturation", &gs.HighlightDechroma, gs_def.HighlightDechroma, "HighlightDechroma", 1.f,
            "How far the brightest sources fade to neutral white, HDR only (0 = keep color at any brightness).");
      }

      // No "Luma Bloom Enable": the engine's glow is kept and only scaled. Applies in SDR too.
      ImGui::SeparatorText("Bloom");
      slider("Bloom Intensity", &gs.BloomIntensity, gs_def.BloomIntensity, "BloomIntensity", 2.f, "Bloom strength (1 = vanilla, 0 = none).");

      ImGui::SeparatorText("Ambient Occlusion");
      if (ImGui::Checkbox("XeGTAO Enable", &g_gtao_enable))
         reshade::set_config_value(nullptr, NAME, "GTAOEnable", g_gtao_enable);
      if (ImGui::IsItemHovered())
         ImGui::SetTooltip("Replaces the game's SSAO with XeGTAO (cleaner, more accurate ambient occlusion; requires SSAO enabled in the game's video settings).");
#if DEVELOPMENT || TEST
      ImGui::BeginDisabled(!g_gtao_enable);
      ImGui::SliderFloat("GTAO Final Value Power", &g_gtao_final_value_power, 0.3f, 4.5f, "%.2f");
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("Primary darkness dial (higher = darker AO). Shipped at 2.2 by preference; 1.0 is the value that matches the native HBAO histogram, mean 0.89 against XeGTAO's 0.90.");
      ImGui::SliderFloat("GTAO Depth Scale", &g_gtao_depth_scale, 0.01f, 200.f, "%.2f", ImGuiSliderFlags_Logarithmic);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("viewZ divisor (game depth units -> meters). Stays 1.0 in this game: its depth buffer is already LINEAR view-space metres (measured p50 7.3, max 686).");
      ImGui::SliderFloat("GTAO Radius Override", &g_gtao_radius_override, 0.f, 5.f, "%.3f");
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("0 = shader EFFECT_RADIUS define (0.81, anchored to the native 1.18 m radius from cb4[11]); > 0 overrides it, in metres.");
#if DEVELOPMENT // the shader's debug blocks exist in DEVELOPMENT only
      ImGui::Combo("GTAO Debug View", &g_gtao_debug_view, "Off\0Depth gradient\0Normals\0AO x8\0Edges\0");
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("Draws diagnostics through the game's AO apply (multiplied into the scene; DEVELOPMENT shader only). Depth gradient dead/flat or normals blocky = wrong input; AO x8 = spot broad over-occlusion.");
#endif
      ImGui::EndDisabled();
#endif

      ImGui::SeparatorText("Effects");

      // Vignette lives in the vanilla grade tail, so this applies in SDR as well as HDR.
      slider("Vignette Intensity", &gs.VignetteIntensity, gs_def.VignetteIntensity, "VignetteIntensity", 1.f, "Scales the game's vignette darkening (1 = vanilla, 0 = none).");

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
         slider("Video HDR Boost", &gs.VideoAutoHDRBoost, gs_def.VideoAutoHDRBoost, "VideoAutoHDRBoost", 1.f, "Video highlight strength (0 = off).");
         ImGui::EndDisabled();
      }

      // The final grade dithers in SDR and HDR alike, so this stays outside the HDR gate above.
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
                  "\nAMD FidelityFX (RCAS)"
                  "\ndgVoodoo2 by Dege (DirectX 9 -> 11 wrapper, required)");
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
      // "No1Px" as in BL2, ME1 and MoHA: dgVoodoo fills every unused SRV slot with 1x1 placeholders (devkit: a 1x1
      // r8g8b8a8 texture and a 1x1 cube on every post draw), and a 1x1 trivially passes the aspect filter, so core would
      // mirror a typeless one too (and assert in DEVELOPMENT).
      texture_format_upgrades_2d_size_filters = (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainResolution | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainAspectRatio | (uint32_t)TextureFormatUpgrades2DSizeFilters::No1Px;

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
