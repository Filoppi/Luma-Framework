// Mass Effect Legendary Edition trilogy Luma mod (Unreal Engine 3, native DX11, x64). One addon, three games:
// stage-1 hashes and bloom slots are per game, the FXAA/HBAO+/DoF/bloom/present chains are shared.
//
// Stage 1 replaces the SDR-clamping uber-post tonemap with an HDR reconstruction and DICE rolloff; stage 2
// (0x0765601C) decodes the gamma intermediate and applies Game Paper White. SMAA replaces compute FXAA, fp16
// pyramidal bloom replaces the UNORM bloom, XeGTAO writes the native AO target; native fp16 DoF is unchanged.
//
// Sub-native borderless is best-effort: the game allocates desktop-sized targets but renders a top-left
// sub-rectangle through cb2 DynamicScale, so injected in-place passes process the full allocation.

#define DISABLE_AUTO_DEBUGGER 1 // The DEVELOPMENT attach prompt is hidden by fullscreen and blocks the loader.

#define ENABLE_NGX 0 // UE3 LE exposes only an 8-bit SoftEdge mask, not usable motion vectors for DLSS/DLAA.
#define ENABLE_FIDELITY_SK 0
#define GEOMETRY_SHADER_SUPPORT 0
#define ENABLE_SMAA 1  // replaces the game's compute FXAA
#define ENABLE_BLOOM 1 // fp16 pyramidal bloom replaces the game's clamped bloom

#include "..\..\Core\core.hpp"
#include <shellapi.h> // ShellExecuteA for About links (system() hangs the render thread in exclusive fullscreen)
#include <vector>
#include <unordered_set>
#include <cmath>
#include <array>
#include <memory>
#include <optional>
#include <span>

// Selects the per-game tonemap table used by injected resource-slot handling; replacement stays CSO-hash keyed.
enum class MEGame
{
   Unknown = 0,
   ME1,
   ME2,
   ME3,
};
static MEGame g_me_game = MEGame::ME1;

static MEGame DetectMEGame()
{
   char path[MAX_PATH] = {};
   GetModuleFileNameA(nullptr, path, MAX_PATH);
   std::string exe(path);
   for (auto& c : exe)
      c = (char)tolower((unsigned char)c);
   if (exe.find("masseffect3") != std::string::npos)
      return MEGame::ME3;
   if (exe.find("masseffect2") != std::string::npos)
      return MEGame::ME2;
   if (exe.find("masseffect1") != std::string::npos)
      return MEGame::ME1;
   return MEGame::ME1; // Treat an unknown executable as ME1.
}

// SMAA replaces the shared MiniEngine FXAA resolve on the fp16 gamma post buffer. The prepass and indirect-
// argument dispatches touch only work queues and remain native.
static constexpr uint32_t kFXAAResolveHHash = 0xB53BB634; // Horizontal resolve: replaced with SMAA.
static constexpr uint32_t kFXAAResolveVHash = 0xF43DBFFD; // Vertical in-place refine: skipped after SMAA.

// Stage-1 tonemap permutations (MB = motion blur, FG = film grain). Slots are stored per permutation because
// MB binds depth at t0 and pushes everything up one, and ME3 additionally binds velocity at t2. They mirror
// R_SCENE and R_BLOOM in the matching HLSL body with no compile-time cross-check, so a re-captured permutation
// must move both sides. Unlisted permutations stay vanilla; this table drives only bloom and SMAA depth capture.
struct TonemapPermDesc
{
   uint32_t hash;
   uint8_t scene_slot; // 1 on motion-blur permutations, where t0 is depth instead of scene color.
   uint8_t bloom_slot;
};
static constexpr TonemapPermDesc kTonemapPermsME1[] = {
   {0x151FE4CA, 1, 5}, // MB+FG, LUT grade
   {0x69F03340, 1, 5}, // MB, LUT grade
   {0x109F3B6E, 0, 4}, // FG, LUT grade
   {0x8C8E8CA2, 0, 4}, // LUT grade
   {0xAAE8755A, 0, 4}, // analytic grade (no LUT)
};
static constexpr TonemapPermDesc kTonemapPermsME2[] = {
   {0x2754F750, 1, 5}, // MB, LUT grade
   {0x1536C5B5, 1, 5}, // MB+FG, LUT grade
   {0x940979D8, 1, 5}, // MB, Filmic+LUT grade
   {0x75BFAFBC, 1, 5}, // MB+FG, Filmic+LUT grade
   {0xCC76075F, 0, 4}, // analytic grade (no LUT)
   {0xD077D06B, 0, 4}, // LUT grade
   {0x8E0C0DBB, 0, 4}, // FG, LUT grade
   {0x222186F8, 0, 4}, // Filmic+LUT grade
   {0xEC890842, 0, 4}, // FG, Filmic+LUT grade
};
static constexpr TonemapPermDesc kTonemapPermsME3[] = {
   {0x36B90B12, 1, 6}, // MB(depth)+Filmic+LUT grade
   {0x49BD5A95, 1, 6}, // MB(depth)+FG, Filmic+LUT grade
   {0x00944C2E, 0, 4}, // Filmic+LUT grade
   {0x5AA0BD09, 0, 4}, // FG, Filmic+LUT grade
   {0x225A8330, 0, 4}, // analytic grade (no LUT)
};
// Selected once in DllMain.
static std::span<const TonemapPermDesc> g_tonemap_perms = kTonemapPermsME1;

// Everything that differs between the three games. ME2/ME3 share the native HBAO+ radius of 48 uu against ME1's
// 30 uu; GTAO visibility power is 1 everywhere, so it stays at its global default instead of living here.
struct MEGameProfile
{
   std::span<const TonemapPermDesc> tonemap_perms;
   float gtao_radius_override; // Native radius (uu) / DepthScale; 0 keeps the shader's own radius.
   float bloom_scale_ref;      // Divisor on the native bright-pass scale; 1 reproduces it.
};
static constexpr MEGameProfile ProfileFor(MEGame game)
{
   switch (game)
   {
   case MEGame::ME2:
      return {kTonemapPermsME2, 0.96f, 1.0f};
   case MEGame::ME3:
      return {kTonemapPermsME3, 0.96f, 1.0f};
   default:
      return {kTonemapPermsME1, 0.f, 1.0f};
   }
}
// Quarter-resolution bloom bright-pass; cb0.xy = (BloomScale, Threshold).
static constexpr uint32_t kBloomBrightPassHash = 0xF8942FF1;

// User-facing AA settings persisted through ReShade.
static bool g_smaa_enable = true;
static float g_rcas_sharpness = 0.f; // RCAS sharpen on the SMAA output (0 = off, opt-in)

// SMAA predication, uploaded as SmaaPredication.xyz. Not exposed: tuned to this engine's non-reverse hyperbolic
// R24 depth, whose small far-silhouette deltas need 0.001 rather than SMAA's generic 0.01.
static constexpr float kPredScale = 2.0f;       // [1,5] off-edge threshold multiplier.
static constexpr float kPredThreshold = 0.001f; // Depth delta that identifies an edge.
static constexpr float kPredStrength = 0.4f;    // [0,1] edge influence on the color threshold.

// fp16 pyramidal bloom built from the stage-1 linear scene and rebound at the native bloom slot, keeping the
// game's tint and screen blend. Mip count is fixed: DrawBloom resizes its static mip cache only in DEVELOPMENT.
static bool g_bloom_enable = true;
// One sigma per mip, so the two counts cannot drift apart.
static constexpr std::array<float, 6> kBloomSigmas = {1.5f, 2.f, 2.f, 2.f, 2.f, 1.f};
static float g_bloom_intensity = 1.0f;
// Divisor on the artist-authored per-scene cb0.x, read two frames late through a no-stall ring. 1 reproduces the
// native bright pass.
static float g_bloom_scale_ref = 1.0f;

// Bink targets the intermediate gamma buffer or the swapchain directly; GameSettings.VideoOnSwapchain reports
// which, so the replacement applies the UI/Game scale exactly once.
static constexpr uint32_t kVideoBinkHash = 0x7B5C59DF;
// Shared stage 2 decodes the gamma scene/HUD composite into Game-relative linear scRGB. Native SDR omits it.
static constexpr uint32_t kOutputStage2Hash = 0x0765601C;

// XeGTAO replaces the half-resolution GFSDK HBAO+ chain, writing the game's R8_UNORM AO target at the blur
// dispatch; the native apply blit still composites. No TAA, so noise is frozen and two spatial denoisers run.
static constexpr uint32_t kAODeinterleaveHash = 0x497830D8; // Depth deinterleave: skipped after capture.
static constexpr uint32_t kAOHorizonHash = 0x80212FD6;      // Horizon march: skipped after normal capture.
static constexpr uint32_t kAOBlurHash = 0x06D92B08;         // Blur: replaced with XeGTAO.

static bool g_gtao_enable = true;
static bool g_video_auto_hdr_enable = true; // Expand Bink highlights in HDR; off preserves vanilla SDR video.
static bool g_hide_ui = false;              // Session-only HUD suppression for clean captures.
// Runtime XeGTAO parameters in b11. HBAO+ PowExponent does not transfer numerically (different visibility
// curve), so FinalValuePower is calibrated against the native AO histogram.
static float g_gtao_final_value_power = 1.0f;
// Converts UE3 view-Z units (approximately 2 cm per uu) to the scale expected by XeGTAO.
static float g_gtao_depth_scale = 50.f;
// A positive value overrides EFFECT_RADIUS; native radius is sqrt(-1 / NegInvR2) / DepthScale.
static float g_gtao_radius_override = 0.f;
#if DEVELOPMENT || TEST
static int g_gtao_debug_view = 0; // 0=off, 1=depth, 2=normals, 3=AO x8, 4=edges.
#endif

// Native DoF is retained: its fp16 near/far chain has no SDR clamp, and stage 1 composites those buffers.

// Per-device SMAA resources. The gamma snapshot feeds both DrawSMAA color inputs; no linear copy is needed.
struct MassEffectGameDeviceData final : public GameDeviceData
{
   // Handles already processed by SMAA this frame; later in-place FXAA resolves must be skipped.
   std::unordered_set<uint64_t> smaa_applied_handles;

   // R24 scene depth captured from motion-blur tonemap permutations for SMAA predication.
   ComPtr<ID3D11ShaderResourceView> srv_depth;

   // SMAA b1: target metrics followed by predication scale, threshold, and strength.
   ComPtr<ID3D11Buffer> cb_smaa_metrics;
   uint32_t smaa_metrics_w = 0, smaa_metrics_h = 0;
   bool smaa_metrics_predicated = false; // Whether the cached metrics carry the depth-edge term or plain ULTRA.

   // Size DrawSMAA built its core-managed intermediates at (rebuild on resolution change).
   uint32_t smaa_core_w = 0, smaa_core_h = 0;

   // SRV-readable snapshot of the in-place gamma post buffer.
   ComPtr<ID3D11Texture2D> tex_input;
   ComPtr<ID3D11ShaderResourceView> srv_input;
   uint32_t smaa_temps_w = 0, smaa_temps_h = 0;

   // fp16 SMAA output, copied back directly or through RCAS.
   ComPtr<ID3D11Texture2D> tex_smaa_out;
   ComPtr<ID3D11RenderTargetView> tex_smaa_out_rtv;
   ComPtr<ID3D11ShaderResourceView> tex_smaa_out_srv;
   uint32_t smaa_out_w = 0, smaa_out_h = 0;

   // RCAS b0 = (width, height, sharpness, 0).
   ComPtr<ID3D11Buffer> cb_sharpen;
   uint32_t sharpen_w = 0, sharpen_h = 0;
   float sharpen_amount = -1.f;
   ComPtr<ID3D11Texture2D> tex_rcas_out;
   ComPtr<ID3D11RenderTargetView> tex_rcas_out_rtv;
   uint32_t rcas_out_w = 0, rcas_out_h = 0;

   // XeGTAO scratch at half-res AO size: R24 depth from deinterleave t0, packed R8G8 view normals from horizon t0.
   ComPtr<ID3D11ShaderResourceView> srv_gtao_depth;
   ComPtr<ID3D11ShaderResourceView> srv_gtao_normals;
   // Set only after a complete takeover at deinterleave; otherwise the native chain remains intact.
   bool gtao_active_this_frame = false;

   // Five-level R32F view-space-depth pyramid.
   ComPtr<ID3D11Texture2D> tex_gtao_depth_mips;
   ComPtr<ID3D11UnorderedAccessView> gtao_depth_mip_uavs[5];
   ComPtr<ID3D11ShaderResourceView> srv_gtao_depth_mips;
   // R8G8_UNORM AO/edge ping-pong; the second denoiser writes the game's u0.
   ComPtr<ID3D11Texture2D> tex_gtao_working[2];
   ComPtr<ID3D11UnorderedAccessView> uav_gtao_working[2];
   ComPtr<ID3D11ShaderResourceView> srv_gtao_working[2];
   uint32_t gtao_w = 0, gtao_h = 0;

   // Immutable b11 = (FinalValuePower, DepthScale, RadiusOverride, DebugView).
   ComPtr<ID3D11Buffer> cb_gtao;
   float gtao_cb_fvp = -1.f, gtao_cb_depth_scale = -1.f, gtao_cb_radius = -1.f, gtao_cb_debug = -1.f;

   // Bright-pass cb0 staging ring for no-stall per-scene BloomScale and threshold capture.
   ComPtr<ID3D11Buffer> bloom_scale_ring[3];
   int bloom_scale_ring_wr = 0;
   int bloom_scale_ring_filled = 0;              // Do not map until every slot contains real data.
   bool bloom_scale_captured_this_frame = false; // Once-per-frame capture gate.
   bool scene_post_done_this_frame = false;      // Arms HUD suppression after the FXAA resolve.
   bool stage2_seen_this_frame = false;          // False means final composition must decode the gamma swapchain.
   float bloom_scale_live = -1.f;                // Negative until the first successful readback.
   float bloom_threshold_live = -1.f;            // Negative selects the 1.2 fallback.
#if DEVELOPMENT
   int bloom_bright_pass_hits = 0; // Bright-pass captures this frame.
#endif
};

class MassEffectLE final : public Game
{
   static MassEffectGameDeviceData& GetGameDeviceData(DeviceData& device_data)
   {
      return *static_cast<MassEffectGameDeviceData*>(device_data.game);
   }

   // Registered shader names, hashed once so registration, readiness probes and dispatch cannot drift apart.
   static constexpr uint32_t kNameBloomVS = CompileTimeStringHash("Bloom VS");
   static constexpr uint32_t kNameBloomPrefilterPS = CompileTimeStringHash("Bloom Prefilter PS");
   static constexpr uint32_t kNameBloomDownsamplePS = CompileTimeStringHash("Bloom Downsample PS");
   static constexpr uint32_t kNameBloomUpsamplePS = CompileTimeStringHash("Bloom Upsample PS");
   static constexpr uint32_t kNameGTAOPrefilterCS = CompileTimeStringHash("MELE XeGTAO Prefilter Depths CS");
   static constexpr uint32_t kNameGTAOMainPassCS = CompileTimeStringHash("MELE XeGTAO Main Pass CS");
   static constexpr uint32_t kNameGTAODenoise1CS = CompileTimeStringHash("MELE XeGTAO Denoise Pass 1 CS");
   static constexpr uint32_t kNameGTAODenoise2CS = CompileTimeStringHash("MELE XeGTAO Denoise Pass 2 CS");
   static constexpr uint32_t kNameSMAAEdgeVS = CompileTimeStringHash("SMAA Edge Detection VS");
   static constexpr uint32_t kNameSMAAEdgePS = CompileTimeStringHash("SMAA Edge Detection PS");
   static constexpr uint32_t kNameSMAAWeightVS = CompileTimeStringHash("SMAA Blending Weight Calculation VS");
   static constexpr uint32_t kNameSMAAWeightPS = CompileTimeStringHash("SMAA Blending Weight Calculation PS");
   static constexpr uint32_t kNameSMAABlendVS = CompileTimeStringHash("SMAA Neighborhood Blending VS");
   static constexpr uint32_t kNameSMAABlendPS = CompileTimeStringHash("SMAA Neighborhood Blending PS");
   static constexpr uint32_t kNameCopyVS = CompileTimeStringHash("Copy VS");
   static constexpr uint32_t kNameSharpenPS = CompileTimeStringHash("MELE Sharpen PS");

   // operator[] default-inserts on a miss, which would mutate a map the render thread otherwise only reads.
   template <typename ShaderMap>
   static auto FindShader(const ShaderMap& shaders, uint32_t name)
   {
      const auto it = shaders.find(name);
      return it != shaders.end() ? it->second.get() : nullptr;
   }
   template <typename ShaderMap>
   static bool AllShadersReady(const ShaderMap& shaders, std::initializer_list<uint32_t> names)
   {
      for (const uint32_t name : names)
      {
         if (FindShader(shaders, name) == nullptr)
            return false;
      }
      return true;
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

   // (Re)create an fp16 scratch target and its views on resolution change. Pass nullptr for a view the target does
   // not use: bind flags follow the requested views, so an unused one cannot leave a stale flag behind.
   static bool EnsureRGBA16FTarget(ID3D11Device* device, uint32_t w, uint32_t h, ComPtr<ID3D11Texture2D>& tex,
      ComPtr<ID3D11RenderTargetView>* rtv, ComPtr<ID3D11ShaderResourceView>* srv, uint32_t& cached_w, uint32_t& cached_h)
   {
      if (!tex || cached_w != w || cached_h != h)
      {
         if (rtv != nullptr)
            rtv->reset();
         if (srv != nullptr)
            srv->reset();
         tex.reset();
         const UINT bind_flags = (srv != nullptr ? D3D11_BIND_SHADER_RESOURCE : 0u) | (rtv != nullptr ? D3D11_BIND_RENDER_TARGET : 0u);
         if (CreateDefaultRGBA16FTex(device, w, h, bind_flags, tex))
         {
            if (rtv != nullptr)
               device->CreateRenderTargetView(tex.get(), nullptr, rtv->put());
            if (srv != nullptr)
               device->CreateShaderResourceView(tex.get(), nullptr, srv->put());
            cached_w = w;
            cached_h = h;
         }
      }
      return (rtv == nullptr || *rtv) && (srv == nullptr || *srv);
   }

   static void ReleaseGTAOScratch(MassEffectGameDeviceData& gd)
   {
      gd.tex_gtao_depth_mips.reset();
      for (auto& uav : gd.gtao_depth_mip_uavs)
         uav.reset();
      gd.srv_gtao_depth_mips.reset();
      for (int i = 0; i < 2; i++)
      {
         gd.tex_gtao_working[i].reset();
         gd.uav_gtao_working[i].reset();
         gd.srv_gtao_working[i].reset();
      }
      gd.gtao_w = gd.gtao_h = 0;
   }

public:
   void OnInit(bool async) override
   {
      // The game follows Windows HDR. Native HDR runs stage 2, so the frame reaches the swapchain linear and this
      // game owns Game Paper White; native SDR has no stage 2, so it stays gamma and Core's composition owns the
      // decode and the scale. Decided here because the shader compiler starts right after OnInit, with a null
      // window because no swapchain exists yet (Display falls back to the primary monitor).
      bool hdr_supported = false, hdr_enabled = false;
      Display::IsHDRSupportedAndEnabled(0, hdr_supported, hdr_enabled);
      const char native_hdr = hdr_enabled ? '1' : '0';

      auto& post_process_space = GetShaderDefineData(POST_PROCESS_SPACE_TYPE_HASH);
      post_process_space.SetDefaultValue(native_hdr);
      post_process_space.SetValueFixed(true);
      auto& early_display_encoding = GetShaderDefineData(EARLY_DISPLAY_ENCODING_HASH); // Inert unless the above is 1.
      early_display_encoding.SetDefaultValue(native_hdr);
      early_display_encoding.SetValueFixed(true);
      GetShaderDefineData(VANILLA_ENCODING_TYPE_HASH).SetDefaultValue('0');
      // The stage-1/stage-2 chain already carries gamma 2.2, so 1 would apply the sRGB mismatch a second time and
      // crush shadows. On the native-SDR topology Core still corrects sRGB against 2.2 on its own, through the
      // display-mode term of its composition pass.
      GetShaderDefineData(GAMMA_CORRECTION_TYPE_HASH).SetDefaultValue('0');
      // Exposes UI Paper White without renormalizing the already combined scene/HUD buffer; type 2 would
      // double-apply the transport ratio.
      GetShaderDefineData(UI_DRAW_TYPE_HASH).SetDefaultValue('1');

      use_os_reference_white_level = false; // Explicit Scene and UI Paper White controls.

      // Native post passes use b0-b3; inject Luma cbuffers at the high slots.
      luma_settings_cbuffer_index = 13;
      luma_data_cbuffer_index = 12;

      // Core registers SMAA through ENABLE_SMAA; no input linearization is needed because the post buffer stays
      // gamma encoded. RCAS runs afterwards through Copy VS and DrawCustomPixelShader.
      native_shaders_definitions.emplace(kNameSharpenPS,
         ShaderDefinition{"Luma_MELE_Sharpen", reshade::api::pipeline_subobject_type::pixel_shader, nullptr, "sharpen_ps"});

      // Four XeGTAO compute entries share one source; XE_GTAO_FINAL_APPLY selects the game's R8_UNORM target.
      native_shaders_definitions.emplace(kNameGTAOPrefilterCS,
         ShaderDefinition{"Luma_MELE_XeGTAO", reshade::api::pipeline_subobject_type::compute_shader, nullptr, "prefilter_depths16x16_cs"});
      native_shaders_definitions.emplace(kNameGTAOMainPassCS,
         ShaderDefinition{"Luma_MELE_XeGTAO", reshade::api::pipeline_subobject_type::compute_shader, nullptr, "main_pass_cs"});
      native_shaders_definitions.emplace(kNameGTAODenoise1CS,
         ShaderDefinition{"Luma_MELE_XeGTAO", reshade::api::pipeline_subobject_type::compute_shader, nullptr, "denoise_pass_cs", {{"XE_GTAO_FINAL_APPLY", "0"}}});
      native_shaders_definitions.emplace(kNameGTAODenoise2CS,
         ShaderDefinition{"Luma_MELE_XeGTAO", reshade::api::pipeline_subobject_type::compute_shader, nullptr, "denoise_pass_cs", {{"XE_GTAO_FINAL_APPLY", "1"}}});

      // With no TAA, Very High slice count and two denoise passes provide spatial stability.
      std::vector<ShaderDefineData> game_shader_defines_data = {
         {"XE_GTAO_QUALITY", '3', true, false, "XeGTAO quality (slice count)\n0 - Low\n1 - Medium\n2 - High\n3 - Very High\n4 - Ultra", 4},
      };
      shader_defines_data.append_range(game_shader_defines_data);

      // Grade controls default to a vanilla no-op. Exposure affects SDR and HDR, the rest are HDR only.
      default_luma_global_game_settings.Exposure = 1.f;
      default_luma_global_game_settings.Saturation = 1.f;
      default_luma_global_game_settings.HighlightDechroma = 0.f;
      default_luma_global_game_settings.Contrast = 1.f;
      default_luma_global_game_settings.VignetteIntensity = 1.f;
      default_luma_global_game_settings.FilmGrainIntensity = 1.f;
      default_luma_global_game_settings.BloomIntensity = g_bloom_intensity; // Per-game gain lives in g_bloom_scale_ref.
      default_luma_global_game_settings.BloomThreshold = 1.2f;              // ME1 fallback until live capture succeeds.
      default_luma_global_game_settings.Dithering = 1.f;                    // Output anti-banding.
      default_luma_global_game_settings.VideoAutoHDREnable = 1.f;           // Off preserves vanilla SDR video.
      default_luma_global_game_settings.VideoAutoHDRBoost = 0.5f;           // 0=1x, 0.5=2.0625x, 1=3.125x UI white.
      default_luma_global_game_settings.VideoOnSwapchain = 0.f;             // Set per Bink draw.
      cb_luma_global_settings.GameSettings = default_luma_global_game_settings;
   }

   void OnCreateDevice(ID3D11Device* native_device, DeviceData& device_data) override
   {
      device_data.game = new MassEffectGameDeviceData;
   }

   void OnDestroyDeviceData(DeviceData& device_data) override
   {
      // GameDeviceData lacks a virtual destructor; delete through the concrete type to release derived members.
      delete static_cast<MassEffectGameDeviceData*>(device_data.game);
      device_data.game = nullptr;
   }

   // Capture bright-pass cb0 into a staging ring and map the oldest entry with DO_NOT_WAIT, so the readback
   // never stalls. OnPresent turns the live artist-authored BloomScale into the effective intensity.
   void CaptureBloomScale(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, MassEffectGameDeviceData& gd, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes)
   {
      if (g_bloom_enable && original_shader_hashes.Contains(kBloomBrightPassHash, reshade::api::shader_stage::pixel) && !gd.bloom_scale_captured_this_frame)
      {
         gd.bloom_scale_captured_this_frame = true; // Do not advance the ring twice in one frame.
#if DEVELOPMENT
         gd.bloom_bright_pass_hits++;
#endif
         ComPtr<ID3D11Buffer> cb0;
         native_device_context->PSGetConstantBuffers(0, 1, cb0.put());
         if (cb0)
         {
            if (!gd.bloom_scale_ring[2]) // Gate on the last slot so partial allocation retries next frame.
            {
               D3D11_BUFFER_DESC bd{};
               cb0->GetDesc(&bd);
               bd.Usage = D3D11_USAGE_STAGING;
               bd.BindFlags = 0;
               bd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
               bd.MiscFlags = 0;
               for (auto& b : gd.bloom_scale_ring)
                  b = nullptr; // com_ptr::put() requires null and this discards prior partial allocation.
               for (auto& b : gd.bloom_scale_ring)
               {
                  if (FAILED(native_device->CreateBuffer(&bd, nullptr, b.put())))
                  {
                     for (auto& r : gd.bloom_scale_ring)
                        r = nullptr;
                     break;
                  }
               }
            }
            if (gd.bloom_scale_ring[2])
            {
               native_device_context->CopyResource(gd.bloom_scale_ring[gd.bloom_scale_ring_wr].get(), cb0.get());
               if (gd.bloom_scale_ring_filled < 3)
                  gd.bloom_scale_ring_filled++;
               if (gd.bloom_scale_ring_filled >= 3)
               {
                  const int oldest = (gd.bloom_scale_ring_wr + 1) % 3; // Written two frames ago and expected idle.
                  D3D11_MAPPED_SUBRESOURCE ms{};
                  if (SUCCEEDED(native_device_context->Map(gd.bloom_scale_ring[oldest].get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &ms)))
                  {
                     const float* f = reinterpret_cast<const float*>(ms.pData);
                     const float bscale = f[0];     // BloomScaleAndThreshold.x.
                     const float bthreshold = f[1]; // BloomScaleAndThreshold.y, per-scene artist dial.
                     native_device_context->Unmap(gd.bloom_scale_ring[oldest].get(), 0);
                     if (bscale >= 0.f && bscale < 100.f) // Reject implausible readback data.
                     {
                        gd.bloom_scale_live = bscale;
                     }
                     if (bthreshold > 0.f && bthreshold < 100.f)
                     {
                        gd.bloom_threshold_live = bthreshold;
                     }
                  }
               }
               gd.bloom_scale_ring_wr = (gd.bloom_scale_ring_wr + 1) % 3; // Map failure skips only this update.
            }
         }
      }
   }

   // Core uploads a dirty settings cbuffer before the replaced pass runs, so the flag applies to the same draw.
   void TagVideoTarget(ID3D11DeviceContext* native_device_context, DeviceData& device_data, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes)
   {
      if (original_shader_hashes.Contains(kVideoBinkHash, reshade::api::shader_stage::pixel))
      {
         ComPtr<ID3D11RenderTargetView> rtv;
         native_device_context->OMGetRenderTargets(1, rtv.put(), nullptr);
         ComPtr<ID3D11Resource> rt_res;
         if (rtv)
            rtv->GetResource(rt_res.put());
         if (rt_res)
         {
            const uint64_t rt_handle = reinterpret_cast<uint64_t>(rt_res.get());
            bool on_swapchain;
            {
               // Swapchain recreation mutates back_buffers under unique_lock; draw hooks must take a shared lock.
               std::shared_lock lock(device_data.mutex);
               on_swapchain = device_data.back_buffers.contains(rt_handle);
            }
            auto& gs = cb_luma_global_settings.GameSettings;
            const float flag = on_swapchain ? 1.f : 0.f;
            if (gs.VideoOnSwapchain != flag)
            {
               gs.VideoOnSwapchain = flag;
               device_data.cb_luma_global_settings_dirty = true;
            }
         }
      }
   }

   // Stage 1 supplies motion-blur depth for SMAA and the scene/bloom slots for fp16 bloom injection.
   void InjectBloomAndDepth(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, DeviceData& device_data, MassEffectGameDeviceData& gd, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes)
   {
      const TonemapPermDesc* perm = nullptr;
      for (const TonemapPermDesc& candidate : g_tonemap_perms)
      {
         if (original_shader_hashes.Contains(candidate.hash, reshade::api::shader_stage::pixel))
         {
            perm = &candidate;
            break;
         }
      }
      if (perm == nullptr)
         return;

      if (g_smaa_enable)
      {
         if (perm->scene_slot != 0)
         {
            // Only motion-blur permutations bind depth at t0; non-MB t0 is scene color.
            ComPtr<ID3D11ShaderResourceView> srv_d;
            native_device_context->PSGetShaderResources(0, 1, srv_d.put());
            if (srv_d)
               gd.srv_depth = srv_d;
         }
         else
         {
            gd.srv_depth.reset(); // Disable predication instead of reusing stale depth.
         }
      }

      // Build fp16 bloom from the tonemap's linear scene and rebind its native bloom slot.
      const bool bloom_ready =
         AllShadersReady(device_data.native_vertex_shaders, {kNameBloomVS}) &&
         AllShadersReady(device_data.native_pixel_shaders, {kNameBloomPrefilterPS, kNameBloomDownsamplePS, kNameBloomUpsamplePS});
      if (g_bloom_enable && bloom_ready)
      {
         ComPtr<ID3D11ShaderResourceView> srv_scene;
         native_device_context->PSGetShaderResources(perm->scene_slot, 1, srv_scene.put());
         if (srv_scene)
         {
            // Bloom prefilter reads GameSettings.BloomThreshold from b13; DrawBloom binds only b11.
            if (luma_settings_cbuffer_index < D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT)
            {
               ID3D11Buffer* settings_cb = device_data.luma_global_settings.get();
               native_device_context->PSSetConstantBuffers(luma_settings_cbuffer_index, 1, &settings_cb);
            }
            ComPtr<ID3D11ShaderResourceView> srv_bloom;
            DrawBloom(native_device, native_device_context, device_data, srv_scene.get(), (int)kBloomSigmas.size(), kBloomSigmas.data(), srv_bloom.put());
            if (srv_bloom)
            {
               ID3D11ShaderResourceView* p = srv_bloom.get();
               native_device_context->PSSetShaderResources(perm->bloom_slot, 1, &p);
            }
         }
      }
   }

   // Take over HBAO+ only when every XeGTAO shader and resource is ready at the first dispatch, otherwise the
   // whole native deinterleave -> horizon -> blur -> apply chain stays active. A returned value is terminal for
   // the callback; nullopt means no AO hash matched and the caller continues.
   std::optional<DrawOrDispatchOverrideType> RunXeGTAO(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, DeviceData& device_data, MassEffectGameDeviceData& gd, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes)
   {
      if (g_gtao_enable)
      {
         // Deinterleave: capture half-resolution R24 depth, prepare all scratch resources, then skip native work.
         if (original_shader_hashes.Contains(kAODeinterleaveHash, reshade::api::shader_stage::compute))
         {
            const bool gtao_shaders_ready =
               AllShadersReady(device_data.native_compute_shaders, {kNameGTAOPrefilterCS, kNameGTAOMainPassCS, kNameGTAODenoise1CS, kNameGTAODenoise2CS});
            if (!gtao_shaders_ready)
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
            // Native cb0 stays content-dimensioned, so pixel/UV mapping holds even when the allocation is larger.
            uint32_t w = dinfo.x, h = dinfo.y;

            if (gd.gtao_w != w || gd.gtao_h != h || !gd.tex_gtao_depth_mips || !gd.tex_gtao_working[1])
            {
               ReleaseGTAOScratch(gd);

               D3D11_TEXTURE2D_DESC td = {};
               td.Width = w;
               td.Height = h;
               td.MipLevels = 5; // XE_GTAO_DEPTH_MIP_LEVELS.
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
                  ReleaseGTAOScratch(gd);                  // All or nothing; retry clean next frame.
                  return DrawOrDispatchOverrideType::None; // Leaves the native chain active.
               }
               gd.gtao_w = w;
               gd.gtao_h = h;
            }

#if DEVELOPMENT || TEST
            const float dbg = (float)g_gtao_debug_view;
#else
            const float dbg = 0.f;
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
            gd.gtao_active_this_frame = true;
            return DrawOrDispatchOverrideType::Replaced;
         }

         // Horizon march: capture packed view normals and skip native work only after a successful takeover.
         if (original_shader_hashes.Contains(kAOHorizonHash, reshade::api::shader_stage::compute))
         {
            if (!gd.gtao_active_this_frame)
               return DrawOrDispatchOverrideType::None;
            ComPtr<ID3D11ShaderResourceView> srv_n;
            native_device_context->CSGetShaderResources(0, 1, srv_n.put());
            if (srv_n)
               gd.srv_gtao_normals = srv_n;

            return DrawOrDispatchOverrideType::Replaced;
         }

         // Blur: run all XeGTAO passes and write the game's final R8_UNORM u0; native apply performs composition.
         if (original_shader_hashes.Contains(kAOBlurHash, reshade::api::shader_stage::compute))
         {
            if (!gd.gtao_active_this_frame)
               return DrawOrDispatchOverrideType::None;

            ComPtr<ID3D11UnorderedAccessView> uav_final;
            native_device_context->CSGetUnorderedAccessViews(0, 1, uav_final.put());
            if (!uav_final)
               return DrawOrDispatchOverrideType::Replaced; // Earlier native stages were already skipped.
            if (!gd.srv_gtao_depth || !gd.srv_gtao_normals)
            {
               // Missing fixed-order inputs: write neutral AO instead of applying stale data.
               const FLOAT ones[4] = {1.f, 1.f, 1.f, 1.f};
               native_device_context->ClearUnorderedAccessViewFloat(uav_final.get(), ones);
               return DrawOrDispatchOverrideType::Replaced;
            }

            // Dispatch dimensions follow the captured content size; native apply ignores any larger stale region.
            const uint32_t w = gd.gtao_w, h = gd.gtao_h;
            DrawStateStack<DrawStateStackType::Compute> st;
            st.Cache(native_device_context, device_data.uav_max_count);

            ID3D11Buffer* kcb = gd.cb_gtao.get();
            native_device_context->CSSetConstantBuffers(11, 1, &kcb);
            ID3D11SamplerState* smp = device_data.sampler_state_point.get();
            native_device_context->CSSetSamplers(0, 1, &smp);
            // XeGTAO deliberately inherits native b0 ($Globals/ProjInfo) and b2 (MinZ_MaxZRatioCS): the immediate
            // context runs a fixed contiguous AO chain with no ClearState, so the horizon-pass bindings persist.

            static constexpr std::array<ID3D11UnorderedAccessView*, 5> uav_nulls5 = {};
            static constexpr std::array<ID3D11ShaderResourceView*, 2> srv_nulls2 = {};

            // Prefilter game depth into the R32F mip pyramid; each thread covers 2x2 pixels.
            {
               native_device_context->CSSetShaderResources(0, 2, srv_nulls2.data());
               ID3D11ShaderResourceView* srv = gd.srv_gtao_depth.get();
               ID3D11UnorderedAccessView* uavs[5] = {gd.gtao_depth_mip_uavs[0].get(), gd.gtao_depth_mip_uavs[1].get(),
                  gd.gtao_depth_mip_uavs[2].get(), gd.gtao_depth_mip_uavs[3].get(), gd.gtao_depth_mip_uavs[4].get()};
               native_device_context->CSSetUnorderedAccessViews(0, 5, uavs, nullptr);
               native_device_context->CSSetShaderResources(0, 1, &srv);
               native_device_context->CSSetShader(FindShader(device_data.native_compute_shaders, kNameGTAOPrefilterCS), nullptr, 0);
               native_device_context->Dispatch((w + 15) / 16, (h + 15) / 16, 1);
               native_device_context->CSSetUnorderedAccessViews(0, 5, uav_nulls5.data(), nullptr);
            }
            // Bind each destination UAV before its source SRVs: D3D11 otherwise keeps the previous UAV hazard and
            // silently nulls the conflicting SRV. Main pass writes AO and edges to working0.
            {
               native_device_context->CSSetShaderResources(0, 2, srv_nulls2.data());
               ID3D11ShaderResourceView* srvs[2] = {gd.srv_gtao_depth_mips.get(), gd.srv_gtao_normals.get()};
               ID3D11UnorderedAccessView* uav = gd.uav_gtao_working[0].get();
               native_device_context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
               native_device_context->CSSetShaderResources(0, 2, srvs);
               native_device_context->CSSetShader(FindShader(device_data.native_compute_shaders, kNameGTAOMainPassCS), nullptr, 0);
               native_device_context->Dispatch((w + 7) / 8, (h + 7) / 8, 1);
            }
            // First denoiser writes working1, two horizontal pixels per thread.
            {
               native_device_context->CSSetShaderResources(0, 2, srv_nulls2.data());
               ID3D11ShaderResourceView* srv = gd.srv_gtao_working[0].get();
               ID3D11UnorderedAccessView* uav = gd.uav_gtao_working[1].get();
               native_device_context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
               native_device_context->CSSetShaderResources(0, 1, &srv);
               native_device_context->CSSetShader(FindShader(device_data.native_compute_shaders, kNameGTAODenoise1CS), nullptr, 0);
               native_device_context->Dispatch((w + 15) / 16, (h + 7) / 8, 1);
            }
            // Final denoiser writes the game's R8_UNORM AO target.
            {
               native_device_context->CSSetShaderResources(0, 2, srv_nulls2.data());
               ID3D11ShaderResourceView* srv = gd.srv_gtao_working[1].get();
               ID3D11UnorderedAccessView* uav = uav_final.get();
               native_device_context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
               native_device_context->CSSetShaderResources(0, 1, &srv);
               native_device_context->CSSetShader(FindShader(device_data.native_compute_shaders, kNameGTAODenoise2CS), nullptr, 0);
               native_device_context->Dispatch((w + 15) / 16, (h + 7) / 8, 1);
            }

            st.Restore(native_device_context);
            return DrawOrDispatchOverrideType::Replaced;
         }
      }

      return {};
   }

   // Replace the in-place FXAA resolve with SMAA by taking color from u0. Disabled SMAA leaves FXAA native.
   DrawOrDispatchOverrideType RunSMAAResolve(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, DeviceData& device_data, MassEffectGameDeviceData& gd, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, bool is_custom_pass)
   {
      const bool is_resolve_h = original_shader_hashes.Contains(kFXAAResolveHHash, reshade::api::shader_stage::compute);
      const bool is_resolve_v = original_shader_hashes.Contains(kFXAAResolveVHash, reshade::api::shader_stage::compute);
      // The resolve is the final scene-post step and arms HUD suppression for subsequent draws regardless of AA.
      if (is_resolve_h || is_resolve_v)
         gd.scene_post_done_this_frame = true;
      if (!g_smaa_enable)
         return DrawOrDispatchOverrideType::None;
      if (!is_custom_pass && (is_resolve_h || is_resolve_v))
      {
         ComPtr<ID3D11UnorderedAccessView> uav_color;
         native_device_context->CSGetUnorderedAccessViews(0, 1, uav_color.put());
         if (!uav_color)
            return DrawOrDispatchOverrideType::None;
         ComPtr<ID3D11Resource> color_res;
         uav_color->GetResource(color_res.put());
         if (!color_res)
            return DrawOrDispatchOverrideType::None;
         const uint64_t color_handle = reinterpret_cast<uint64_t>(color_res.get());

         // Skip later resolves for an already processed resource; rerunning in-place FXAA would corrupt SMAA.
         if (gd.smaa_applied_handles.count(color_handle) != 0)
            return DrawOrDispatchOverrideType::Replaced;
         // Only horizontal resolve triggers SMAA; an unexpected vertical-only resolve remains native.
         if (!is_resolve_h)
            return DrawOrDispatchOverrideType::None;

         uint4 cinfo{};
         DXGI_FORMAT cfmt = DXGI_FORMAT_UNKNOWN;
         GetResourceInfo(color_res.get(), cinfo, cfmt);
         const uint32_t w = cinfo.x, h = cinfo.y;
         if (w == 0 || h == 0)
            return DrawOrDispatchOverrideType::None;
         // SMAA temporaries are RGBA16F; CopyResource silently fails across formats, so fall back to FXAA on mismatch.
         if (cfmt != DXGI_FORMAT_R16G16B16A16_FLOAT)
            return DrawOrDispatchOverrideType::None;

         // Predication requires current, same-sized depth; otherwise a null texture and scale 1 select plain ULTRA.
         bool depth_ok = false;
         if (gd.srv_depth)
         {
            uint4 dinfo{};
            DXGI_FORMAT dfmt = DXGI_FORMAT_UNKNOWN;
            GetResourceInfo(gd.srv_depth.get(), dinfo, dfmt);
            depth_ok = (dinfo.x == w && dinfo.y == h);
         }
         // Threshold and strength are inert without the depth-edge term.
         const float pred_scale = depth_ok ? kPredScale : 1.f;

         // Async loading and live reload may temporarily require native FXAA fallback.
         const bool smaa_ready =
            AllShadersReady(device_data.native_pixel_shaders, {kNameSMAAEdgePS, kNameSMAAWeightPS, kNameSMAABlendPS}) &&
            AllShadersReady(device_data.native_vertex_shaders, {kNameSMAAEdgeVS, kNameSMAAWeightVS, kNameSMAABlendVS});
         if (!smaa_ready)
            return DrawOrDispatchOverrideType::None;

         // DrawSMAA rebuilds managed views only on swapchain init; drop them explicitly on resolution changes.
         if (gd.smaa_core_w != w || gd.smaa_core_h != h)
         {
            auto& mr = device_data.managed_resources;
            mr.depth_stencil_views[CompileTimeStringHash("smaa_dsv")].reset();
            mr.render_target_views[CompileTimeStringHash("smaa_edge_detection")].reset();
            mr.render_target_views[CompileTimeStringHash("smaa_blending_weight_calculation")].reset();
            gd.smaa_core_w = w;
            gd.smaa_core_h = h;
         }

         if (!gd.cb_smaa_metrics || gd.smaa_metrics_w != w || gd.smaa_metrics_h != h || gd.smaa_metrics_predicated != depth_ok)
         {
            const float metrics[8] = {1.f / (float)w, 1.f / (float)h, (float)w, (float)h, pred_scale, kPredThreshold, kPredStrength, 0.f};
            if (CreateImmutableCB(native_device, metrics, sizeof(metrics), gd.cb_smaa_metrics))
            {
               gd.smaa_metrics_w = w;
               gd.smaa_metrics_h = h;
               gd.smaa_metrics_predicated = depth_ok;
            }
         }
         if (!gd.cb_smaa_metrics)
            return DrawOrDispatchOverrideType::None;

         // The fp16 SMAA output is both a render target and an SRV for the optional sharpen pass.
         if (!EnsureRGBA16FTarget(native_device, w, h, gd.tex_smaa_out, std::addressof(gd.tex_smaa_out_rtv), std::addressof(gd.tex_smaa_out_srv), gd.smaa_out_w, gd.smaa_out_h))
            return DrawOrDispatchOverrideType::None;

         // The gamma snapshot is only ever read.
         if (!EnsureRGBA16FTarget(native_device, w, h, gd.tex_input, nullptr, std::addressof(gd.srv_input), gd.smaa_temps_w, gd.smaa_temps_h))
            return DrawOrDispatchOverrideType::None;

         native_device_context->CopyResource(gd.tex_input.get(), color_res.get());

         // DrawSMAA restores shaders, resources, and targets, but not cbuffer slots; save VS/PS b1 explicitly.
         ComPtr<ID3D11Buffer> vs_cb1_orig, ps_cb1_orig;
         native_device_context->VSGetConstantBuffers(1, 1, vs_cb1_orig.put());
         native_device_context->PSGetConstantBuffers(1, 1, ps_cb1_orig.put());
         ID3D11Buffer* mcb = gd.cb_smaa_metrics.get();
         native_device_context->VSSetConstantBuffers(1, 1, &mcb);
         native_device_context->PSSetConstantBuffers(1, 1, &mcb);

         DrawSMAA(native_device, native_device_context, device_data,
            gd.tex_smaa_out_rtv.get(), gd.srv_input.get() /*edge color (gamma)*/, gd.srv_input.get() /*blend color (gamma)*/,
            depth_ok ? gd.srv_depth.get() : nullptr /*predication*/);

         // Apply optional RCAS, otherwise copy SMAA directly so the cancelled resolve always produces output.
         const bool sharpen_shaders_ready =
            AllShadersReady(device_data.native_vertex_shaders, {kNameCopyVS}) &&
            AllShadersReady(device_data.native_pixel_shaders, {kNameSharpenPS});
         bool do_sharpen = g_rcas_sharpness > 0.f && sharpen_shaders_ready;
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
            // Written by the sharpen pass and then copied out, so it needs no SRV.
            const bool rcas_target_ready =
               EnsureRGBA16FTarget(native_device, w, h, gd.tex_rcas_out, std::addressof(gd.tex_rcas_out_rtv), nullptr, gd.rcas_out_w, gd.rcas_out_h);
            if (!gd.cb_sharpen || !rcas_target_ready)
               do_sharpen = false;
         }

         if (do_sharpen)
         {
            auto* sharpen_vs = FindShader(device_data.native_vertex_shaders, kNameCopyVS);
            auto* sharpen_ps = FindShader(device_data.native_pixel_shaders, kNameSharpenPS);
            // DrawCustomPixelShader does not restore state; FullGraphics also prevents RCAS b0 leaking into HUD draws.
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

         // Restore native VS/PS b1; no compute state was changed.
         ID3D11Buffer* vcb = vs_cb1_orig.get();
         ID3D11Buffer* pcb = ps_cb1_orig.get();
         native_device_context->VSSetConstantBuffers(1, 1, &vcb);
         native_device_context->PSSetConstantBuffers(1, 1, &pcb);

         gd.smaa_applied_handles.insert(color_handle);
         device_data.has_drawn_main_post_processing = true;
         return DrawOrDispatchOverrideType::Replaced;
      }

      return DrawOrDispatchOverrideType::None;
   }

   // Captures inputs for SMAA/bloom/XeGTAO/video and replaces their native dispatches; stage-1 HDR replacement
   // stays hash-driven by Core.
   DrawOrDispatchOverrideType OnDrawOrDispatch(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, reshade::api::shader_stage stages, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, bool is_custom_pass, bool& updated_cbuffers, std::function<void()>* original_draw_dispatch_func) override
   {
      auto& gd = GetGameDeviceData(device_data);

      const bool is_immediate = cmd_list_data.is_primary; // Cached by Core; avoids a per-draw virtual query.
      // Do not reject custom passes globally: the hash-replaced stage-1 tonemap still supplies SMAA depth and the
      // bloom slot. Individual native-only branches apply !is_custom_pass where required.
      if (!is_immediate)
         return DrawOrDispatchOverrideType::None;

      // Core uploads LumaData after this callback for replaced shaders, so stage 2 and any later direct Bink draw
      // receive the updated encoding state in the same frame.
      if (original_shader_hashes.Contains(kOutputStage2Hash, reshade::api::shader_stage::pixel))
         gd.stage2_seen_this_frame = true;

      // HUD draws occur after the FXAA resolve. Suppress only plain game draws in that window so stage 1, stage 2,
      // movies without a scene resolve, and pre-scene menus remain intact.
      if (g_hide_ui && !is_custom_pass && gd.scene_post_done_this_frame)
         return DrawOrDispatchOverrideType::Replaced;

      CaptureBloomScale(native_device, native_device_context, gd, original_shader_hashes);

      TagVideoTarget(native_device_context, device_data, original_shader_hashes);

      InjectBloomAndDepth(native_device, native_device_context, device_data, gd, original_shader_hashes);

      if (const auto gtao_result = RunXeGTAO(native_device, native_device_context, device_data, gd, original_shader_hashes))
         return *gtao_result;

      return RunSMAAResolve(native_device, native_device_context, device_data, gd, original_shader_hashes, is_custom_pass);
   }

   void UpdateLumaInstanceDataCB(CB::LumaInstanceDataPadded& data, CommandListData&, DeviceData& device_data) override
   {
      // Read by the Bink replacement to decide whether a direct swapchain draw may emit linear scRGB.
      const auto& gd = GetGameDeviceData(device_data);
      data.GameData.SwapchainGammaEncoded = gd.stage2_seen_this_frame ? 0.f : 1.f;
   }

   void OnPresent(ID3D11Device* native_device, DeviceData& device_data) override
   {
      auto& gd = GetGameDeviceData(device_data);
      gd.stage2_seen_this_frame = false;
      // Clear per-frame SMAA state so menus and transitions cannot reuse stale predication depth.
      gd.smaa_applied_handles.clear();
      gd.srv_depth.reset();
      // Drop XeGTAO inputs and ownership so failed or absent AO chains cannot reuse stale resources.
      gd.srv_gtao_depth.reset();
      gd.srv_gtao_normals.reset();
      gd.gtao_active_this_frame = false;
      gd.bloom_scale_captured_this_frame = false; // Re-arm BloomScale capture.
      gd.scene_post_done_this_frame = false;      // Re-armed by the FXAA resolve.
#if DEVELOPMENT
      gd.bloom_bright_pass_hits = 0;
#endif

      // This is the sole writer of effective BloomIntensity. UI code edits only the raw slider and enable state,
      // preventing raw/derived values from oscillating while dragging. Native bloom keeps multiplier 1.
      {
         auto& gs = cb_luma_global_settings.GameSettings;
         float eff = 1.f;
         if (g_bloom_enable)
         {
            float scale = 1.f;
            if (gd.bloom_scale_live >= 0.f && g_bloom_scale_ref > 1e-4f)
            {
               scale = gd.bloom_scale_live / g_bloom_scale_ref;
               scale = scale < 0.f ? 0.f : (scale > 4.f ? 4.f : scale); // Avoid Windows min/max macros.
            }
            eff = g_bloom_intensity * scale;
         }
         if (fabsf(gs.BloomIntensity - eff) > 1e-4f)
         {
            gs.BloomIntensity = eff;
            device_data.cb_luma_global_settings_dirty = true;
         }

         // Follow native per-scene cb0.y; use ME1's 1.2 default until the first readback.
         const float thr = gd.bloom_threshold_live >= 0.f ? gd.bloom_threshold_live : 1.2f;
         if (fabsf(gs.BloomThreshold - thr) > 1e-4f)
         {
            gs.BloomThreshold = thr;
            device_data.cb_luma_global_settings_dirty = true;
         }
      }
   }

   void LoadConfigs() override
   {
      reshade::get_config_value(nullptr, PROJECT_NAME, "SMAAEnable", g_smaa_enable);
      reshade::get_config_value(nullptr, PROJECT_NAME, "RCASSharpness", g_rcas_sharpness);
      auto& gs = cb_luma_global_settings.GameSettings;
      reshade::get_config_value(nullptr, PROJECT_NAME, "Exposure", gs.Exposure);
      reshade::get_config_value(nullptr, PROJECT_NAME, "Saturation", gs.Saturation);
      reshade::get_config_value(nullptr, PROJECT_NAME, "HighlightDechroma", gs.HighlightDechroma);
      reshade::get_config_value(nullptr, PROJECT_NAME, "Contrast", gs.Contrast);
      reshade::get_config_value(nullptr, PROJECT_NAME, "VignetteIntensity", gs.VignetteIntensity);
      reshade::get_config_value(nullptr, PROJECT_NAME, "FilmGrainIntensity", gs.FilmGrainIntensity);
      reshade::get_config_value(nullptr, PROJECT_NAME, "BloomEnable", g_bloom_enable);
      reshade::get_config_value(nullptr, PROJECT_NAME, "BloomIntensity", g_bloom_intensity);
      reshade::get_config_value(nullptr, PROJECT_NAME, "Dithering", gs.Dithering);
      reshade::get_config_value(nullptr, PROJECT_NAME, "VideoAutoHDREnable", g_video_auto_hdr_enable);
      gs.VideoAutoHDREnable = g_video_auto_hdr_enable ? 1.f : 0.f;
      reshade::get_config_value(nullptr, PROJECT_NAME, "VideoAutoHDRBoost", gs.VideoAutoHDRBoost);
      reshade::get_config_value(nullptr, PROJECT_NAME, "GTAOEnable", g_gtao_enable);
      reshade::get_config_value(nullptr, PROJECT_NAME, "GTAOFinalValuePower", g_gtao_final_value_power);
      reshade::get_config_value(nullptr, PROJECT_NAME, "GTAODepthScale", g_gtao_depth_scale);
      reshade::get_config_value(nullptr, PROJECT_NAME, "GTAORadiusOverride", g_gtao_radius_override);
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
         ImGui::SetTooltip("Sharpening applied on top of SMAA (0 = off).");
      ImGui::EndDisabled();

      ImGui::SeparatorText("Grade");
      auto& gs = cb_luma_global_settings.GameSettings;
      auto& gd_def = default_luma_global_game_settings;

      if (ImGui::SliderFloat("Exposure", &gs.Exposure, 0.f, 2.f))
         device_data.cb_luma_global_settings_dirty = true;
      if (ImGui::IsItemDeactivatedAfterEdit())
         reshade::set_config_value(nullptr, PROJECT_NAME, "Exposure", gs.Exposure);
      if (ImGui::IsItemHovered())
         ImGui::SetTooltip("Overall image brightness (1 = vanilla).");
      if (DrawResetButton<float, false>(gs.Exposure, gd_def.Exposure, "Exposure"))
      {
         device_data.cb_luma_global_settings_dirty = true;
         reshade::set_config_value(nullptr, PROJECT_NAME, "Exposure", gs.Exposure); // LoadConfigs reads PROJECT_NAME, not [Luma].
      }

      if (cb_luma_global_settings.DisplayMode == DisplayModeType::HDR)
      {
         if (ImGui::SliderFloat("Contrast", &gs.Contrast, 0.f, 2.f))
            device_data.cb_luma_global_settings_dirty = true;
         if (ImGui::IsItemDeactivatedAfterEdit())
            reshade::set_config_value(nullptr, PROJECT_NAME, "Contrast", gs.Contrast);
         if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Overall image contrast, HDR only (1 = vanilla).");
         if (DrawResetButton<float, false>(gs.Contrast, gd_def.Contrast, "Contrast"))
         {
            device_data.cb_luma_global_settings_dirty = true;
            reshade::set_config_value(nullptr, PROJECT_NAME, "Contrast", gs.Contrast);
         }

         if (ImGui::SliderFloat("Saturation", &gs.Saturation, 0.f, 2.f))
            device_data.cb_luma_global_settings_dirty = true;
         if (ImGui::IsItemDeactivatedAfterEdit())
            reshade::set_config_value(nullptr, PROJECT_NAME, "Saturation", gs.Saturation);
         if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Color saturation, HDR only (1 = vanilla).");
         if (DrawResetButton<float, false>(gs.Saturation, gd_def.Saturation, "Saturation"))
         {
            device_data.cb_luma_global_settings_dirty = true;
            reshade::set_config_value(nullptr, PROJECT_NAME, "Saturation", gs.Saturation);
         }

         if (ImGui::SliderFloat("Highlights Desaturation", &gs.HighlightDechroma, 0.f, 1.f))
            device_data.cb_luma_global_settings_dirty = true;
         if (ImGui::IsItemDeactivatedAfterEdit())
            reshade::set_config_value(nullptr, PROJECT_NAME, "HighlightDechroma", gs.HighlightDechroma);
         if (ImGui::IsItemHovered())
            ImGui::SetTooltip("How soon bright sources fade to neutral white, HDR only (0 = keep color at any brightness).");
         if (DrawResetButton<float, false>(gs.HighlightDechroma, gd_def.HighlightDechroma, "HighlightDechroma"))
         {
            device_data.cb_luma_global_settings_dirty = true;
            reshade::set_config_value(nullptr, PROJECT_NAME, "HighlightDechroma", gs.HighlightDechroma);
         }
      }

      ImGui::SeparatorText("Bloom");
      if (ImGui::Checkbox("Luma Bloom Enable", &g_bloom_enable))
      {
         reshade::set_config_value(nullptr, PROJECT_NAME, "BloomEnable", g_bloom_enable);
         device_data.cb_luma_global_settings_dirty = true;
      }
      if (ImGui::IsItemHovered())
         ImGui::SetTooltip("Replaces the game's bloom with a wider, softer HDR bloom.");
      ImGui::BeginDisabled(!g_bloom_enable);
      if (ImGui::SliderFloat("Bloom Intensity", &g_bloom_intensity, 0.f, 2.f))
         device_data.cb_luma_global_settings_dirty = true;
      if (ImGui::IsItemDeactivatedAfterEdit())
         reshade::set_config_value(nullptr, PROJECT_NAME, "BloomIntensity", g_bloom_intensity);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("Bloom strength (1 = vanilla, 0 = none).");
      if (DrawResetButton<float, false>(g_bloom_intensity, gd_def.BloomIntensity, "BloomIntensity"))
      {
         device_data.cb_luma_global_settings_dirty = true;
         reshade::set_config_value(nullptr, PROJECT_NAME, "BloomIntensity", g_bloom_intensity);
      }
      ImGui::EndDisabled();

      ImGui::SeparatorText("Ambient Occlusion");
      if (ImGui::Checkbox("XeGTAO Enable", &g_gtao_enable))
         reshade::set_config_value(nullptr, PROJECT_NAME, "GTAOEnable", g_gtao_enable);
      if (ImGui::IsItemHovered())
         ImGui::SetTooltip("Replaces the game's HBAO+ with XeGTAO (cleaner, more accurate ambient occlusion).");
#if DEVELOPMENT || TEST
      ImGui::BeginDisabled(!g_gtao_enable);
      ImGui::SliderFloat("GTAO Final Value Power", &g_gtao_final_value_power, 0.3f, 4.5f, "%.2f");
      if (ImGui::IsItemDeactivatedAfterEdit())
         reshade::set_config_value(nullptr, PROJECT_NAME, "GTAOFinalValuePower", g_gtao_final_value_power);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("Primary darkness dial (higher = darker AO). Calibrate to match vanilla's overall darkening.");
      ImGui::SliderFloat("GTAO Depth Scale", &g_gtao_depth_scale, 10.f, 200.f, "%.0f", ImGuiSliderFlags_Logarithmic);
      if (ImGui::IsItemDeactivatedAfterEdit())
         reshade::set_config_value(nullptr, PROJECT_NAME, "GTAODepthScale", g_gtao_depth_scale);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("viewZ divisor (UE3 units -> ~meters). FIRST dial if AO shows broad depth-correlated over-occlusion.");
      ImGui::SliderFloat("GTAO Radius Override", &g_gtao_radius_override, 0.f, 3.f, "%.3f");
      if (ImGui::IsItemDeactivatedAfterEdit())
         reshade::set_config_value(nullptr, PROJECT_NAME, "GTAORadiusOverride", g_gtao_radius_override);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("0 = shader EFFECT_RADIUS define; > 0 overrides it to match the vanilla HBAO+ radius (uu / DepthScale).");
      ImGui::Combo("GTAO Debug View", &g_gtao_debug_view, "Off\0Depth gradient\0Normals\0AO x8\0Edges\0");
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("Draws diagnostics through the game's AO apply (multiplied into the scene). Depth gradient dead/flat or normals blocky = wrong input; AO x8 = spot broad over-occlusion.");
      ImGui::EndDisabled();
#endif

      ImGui::SeparatorText("Effects");
      if (ImGui::SliderFloat("Vignette Intensity", &gs.VignetteIntensity, 0.f, 1.f))
         device_data.cb_luma_global_settings_dirty = true;
      if (ImGui::IsItemDeactivatedAfterEdit())
         reshade::set_config_value(nullptr, PROJECT_NAME, "VignetteIntensity", gs.VignetteIntensity);
      if (ImGui::IsItemHovered())
         ImGui::SetTooltip("Scales the game's vignette darkening (1 = vanilla, 0 = none).");
      if (DrawResetButton<float, false>(gs.VignetteIntensity, gd_def.VignetteIntensity, "VignetteIntensity"))
      {
         device_data.cb_luma_global_settings_dirty = true;
         reshade::set_config_value(nullptr, PROJECT_NAME, "VignetteIntensity", gs.VignetteIntensity);
      }

      if (ImGui::SliderFloat("Film Grain Intensity", &gs.FilmGrainIntensity, 0.f, 1.f))
         device_data.cb_luma_global_settings_dirty = true;
      if (ImGui::IsItemDeactivatedAfterEdit())
         reshade::set_config_value(nullptr, PROJECT_NAME, "FilmGrainIntensity", gs.FilmGrainIntensity);
      if (ImGui::IsItemHovered())
         ImGui::SetTooltip("Scales the game's film grain (1 = vanilla, 0 = off).");
      if (DrawResetButton<float, false>(gs.FilmGrainIntensity, gd_def.FilmGrainIntensity, "FilmGrainIntensity"))
      {
         device_data.cb_luma_global_settings_dirty = true;
         reshade::set_config_value(nullptr, PROJECT_NAME, "FilmGrainIntensity", gs.FilmGrainIntensity);
      }

      if (cb_luma_global_settings.DisplayMode == DisplayModeType::HDR)
      {
         if (ImGui::Checkbox("Video AutoHDR", &g_video_auto_hdr_enable))
         {
            reshade::set_config_value(nullptr, PROJECT_NAME, "VideoAutoHDREnable", g_video_auto_hdr_enable);
            gs.VideoAutoHDREnable = g_video_auto_hdr_enable ? 1.f : 0.f;
            device_data.cb_luma_global_settings_dirty = true;
         }
         if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Adds HDR highlights to pre-rendered videos (HDR only).");
         ImGui::BeginDisabled(!g_video_auto_hdr_enable);
         if (ImGui::SliderFloat("Video HDR Boost", &gs.VideoAutoHDRBoost, 0.f, 1.f))
            device_data.cb_luma_global_settings_dirty = true;
         if (ImGui::IsItemDeactivatedAfterEdit())
            reshade::set_config_value(nullptr, PROJECT_NAME, "VideoAutoHDRBoost", gs.VideoAutoHDRBoost);
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Video highlight strength (0 = off).");
         if (DrawResetButton<float, false>(gs.VideoAutoHDRBoost, gd_def.VideoAutoHDRBoost, "VideoAutoHDRBoost"))
         {
            device_data.cb_luma_global_settings_dirty = true;
            reshade::set_config_value(nullptr, PROJECT_NAME, "VideoAutoHDRBoost", gs.VideoAutoHDRBoost);
         }
         ImGui::EndDisabled();
      }

      bool dithering = gs.Dithering > 0.5f;
      if (ImGui::Checkbox("Dithering", &dithering))
      {
         gs.Dithering = dithering ? 1.f : 0.f;
         device_data.cb_luma_global_settings_dirty = true;
         reshade::set_config_value(nullptr, PROJECT_NAME, "Dithering", gs.Dithering);
      }
      if (ImGui::IsItemHovered())
         ImGui::SetTooltip("Reduces gradient banding (HDR output).");

      ImGui::SeparatorText("UI");
      ImGui::Checkbox("Hide Gameplay UI", &g_hide_ui); // Session-only to avoid a confusing HUD-less restart.
      if (ImGui::IsItemHovered())
         ImGui::SetTooltip("Disables the in-game UI.");

#if DEVELOPMENT
      {
         auto& gd = GetGameDeviceData(device_data);
         const char* gname = g_me_game == MEGame::ME1 ? "ME1" : (g_me_game == MEGame::ME2 ? "ME2" : "ME3");
         const float eff_thr = gd.bloom_threshold_live >= 0.f ? gd.bloom_threshold_live : 1.2f;
         ImGui::SeparatorText("Bloom DEV readout");
         ImGui::Text("game=%s  bright-pass hits/frame=%d  (0 = capture hook never fired)", gname, gd.bloom_bright_pass_hits);
         ImGui::Text("threshold_live=%.4f  scale_live=%.4f  (-1 = not captured yet)", gd.bloom_threshold_live, gd.bloom_scale_live);
         ImGui::Text("scale_ref=%.4f  eff threshold=%.4f  eff intensity=%.4f", g_bloom_scale_ref, eff_thr, cb_luma_global_settings.GameSettings.BloomIntensity);
      }
#endif
   }

   void PrintImGuiAbout() override
   {
      ImGui::PushTextWrapPos(0.f);
      ImGui::Text(
         "Luma for \"Mass Effect Legendary Edition\" is developed by DristoforColumb and is open source and free.\n"
         "It replaces the game's FXAA with SMAA, its bloom with a wider HDR bloom, and its HBAO+ with XeGTAO, plus 16x anisotropic filtering.\n"
         "With the game's HDR enabled it also replaces the native HDR tonemap with a higher quality one.\n"
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
                  "\nSMAA (Iryoku)"
                  "\nXeGTAO (Intel)"
                  "\nAMD FidelityFX (RCAS)"
                  "\nDICE (HDR tonemapper)"
                  "\n3Dmigoto"
                  "\nRenoDX (HDR tonemap method)");
   }
};

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
   if (ul_reason_for_call == DLL_PROCESS_ATTACH)
   {
      g_me_game = DetectMEGame();
      // Per-game defaults; LoadConfigs may override the persisted ones afterwards.
      const MEGameProfile profile = ProfileFor(g_me_game);
      g_tonemap_perms = profile.tonemap_perms;
      g_gtao_radius_override = profile.gtao_radius_override;
      g_bloom_scale_ref = profile.bloom_scale_ref;

      Globals::SetGlobals(PROJECT_NAME, "Mass Effect Legendary Edition Luma mod", "", 3);

      // Native in-game HDR is required; it already supplies the fp16 scRGB swapchain and RGBA16F stage-1/2
      // transport. A general texture upgrade would also catch R8G8B8A8 velocity and UI, breaking their contracts.
      swapchain_format_upgrade_type = TextureFormatUpgradesType::AllowedEnabled; // Enables scRGB and linear composition.
      swapchain_upgrade_type = SwapchainUpgradeType::scRGB;
      texture_format_upgrades_type = TextureFormatUpgradesType::None; // HDR buffers are already fp16.

      // Mode 4 sets MaxAnisotropy=16. Keep zero LOD bias because there is no TAA to suppress shimmer.
      enable_samplers_upgrade = true; // Boot-time only.
      samplers_upgrade_mode = 4;

      game = new MassEffectLE();
   }

   CoreMain(hModule, ul_reason_for_call, lpReserved);

   return TRUE;
}
