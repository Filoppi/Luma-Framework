// Borderlands 2 + The Pre-Sequel — Luma HDR + SMAA + DLSS/FSR/XeSS mod (Unreal Engine 3, native DX9 -> D3D11 via dgVoodoo2).
//
// dgVoodoo2 translates SM3.0 to ps_5_0 (ps_4_0 under 2.81.3), so CSO hashes differ from the native DX9 ones. Launch
// the game exe DIRECTLY: XNA Launcher.exe also loads d3d9 and would capture ReShade instead of the game.
// One shared addon serves both games, discriminated by the tonemap hash:
// - TONEMAP PS 0xD00AA2A7 (BL2) / 0xFCFE623E (TPS): scene fp16 + bloom + vignette + LUT + DOF -> 8-bit LDR.
//   Replaced to recover HDR; the HUD draws after it (onto the LDR on BL2, onto a post-FXAA buffer on TPS).
// - FXAA PS 0x0D3001F6 (only with in-game AA on) -> cancelled while SMAA is on (see the FXAA override): dropped on
//   BL2, where it runs pre-tonemap into a buffer nothing samples, and reduced to a plain copy on TPS, where it runs
//   post-tonemap into the buffer the HUD then draws onto. SMAA itself is injected post-tonemap, so it neither
//   depends on the game's AA setting nor perturbs the DoF.
// Post buffers stay gamma-space SDR. Only ONE Luma .addon per game folder, and no other HDR mod (e.g. RenoDX)
// alongside it.
// The game has no usable motion vectors or jitter, so patched shaders build them (see "MotionVectorPatches.h"). The upscalers run in
// the SR bridge's x64 helper, as NGX is x64-only and the game is 32-bit.

// Don't pop the DEVELOPMENT auto-debugger MessageBox on DLL attach: under a borderless/fullscreen game it's
// invisible and blocks the loader (ReShade times out the addon load -> error 1114).
#define DISABLE_AUTO_DEBUGGER 1

#define GEOMETRY_SHADER_SUPPORT 0
#define ENABLE_SMAA 1  // SMAA ULTRA (+RCAS) injected post-tonemap; core auto-registers the 6 "SMAA ..." passes from Luma_SMAA_impl
#define ENABLE_RCAS 1  // core registers the "RCAS PS" pass, drawn by DrawRCAS after SMAA
#define ENABLE_BLOOM 1 // core auto-registers the Bloom VS/Prefilter/Downsample/Upsample passes -> Luma_Bloom_impl
// SMAA runs POST-tonemap through the post-draw callback (see RunPostTonemapSMAA); needs original_draw_dispatch_func.
#define ENABLE_POST_DRAW_DISPATCH_CALLBACK 1
// The motion vector draw key reads the draw's arguments ("last_draw_dispatch_data")
#define ENABLE_DRAW_DISPATCH_DATA_CACHE 1

#include "..\..\Core\core.hpp"
#include "MotionVectorPatches.h"
#include "patched_draws.h"
#include <shellapi.h> // ShellExecuteA for About links (system("start ...") hangs the render thread in exclusive fullscreen)

// The camera an upscaler needs (FSR), from a row vector view projection (clip = p * M) with an affine view, element (row, column) at
// m[column * 4 + row]. Clip z = A * clip w + B, so device depth = A + B / view depth (A = 1 for an infinite far plane, then
// B = -near). Each value is 0 when the projection lacks it.
struct ViewProjectionCamera
{
   double depth_a = 0.0;
   double depth_b = 0.0;
   double vert_fov = 0.0; // Radians: the up axis has length 1 / tan(fov / 2)
   double near_plane = 0.0;
   double far_plane = 0.0; // Finite: near * 1e6 for an infinite projection
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
   ViewProjectionCamera camera;
   camera.depth_a = (dot_33 > 0.0 ? dot_23 / dot_33 : 1.0);
   camera.depth_b = at(2, 3) - camera.depth_a * at(3, 3);
   const double up_length = std::sqrt(at(1, 0) * at(1, 0) + at(1, 1) * at(1, 1) + at(1, 2) * at(1, 2));
   camera.vert_fov = (up_length > 0.0 ? 2.0 * std::atan(1.0 / up_length) : 0.0);
   camera.near_plane = (camera.depth_a != 0.0 ? -camera.depth_b / camera.depth_a : 0.0);
   if (camera.near_plane > 0.0)
   {
      camera.far_plane = (camera.depth_a > 1.0 + 1e-6 ? camera.depth_b / (1.0 - camera.depth_a) : camera.near_plane * 1e6);
   }
   return camera;
}

// FXAA resolve PS (only present when AA is enabled in the game's video settings). Cancelled while SMAA is on (see
// the draw override); also used by Hide UI to keep this opaque pass out of the HUD filter.
static constexpr uint32_t kFXAAResolveHash = 0x0D3001F6;
static constexpr uint32_t kTonemapHash = 0xD00AA2A7;    // BL2: writes the LDR buffer the HUD then draws onto
static constexpr uint32_t kTonemapHashTPS = 0xFCFE623E; // The Pre-Sequel: same engine, different tonemap CSO

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
// Luma_BL2TPS_Tonemap.hlsl (BL2 default) and Tonemap_0xFCFE623E.ps_5_0.hlsl (TPS), so keep them in sync:
//   bloom -> TM_T_LUMABLOOM : BL2 t5 / TPS t8 (TPS t5 is the native DOF)
static constexpr uint32_t kLumaBloomSlotBL2 = 5;
static constexpr uint32_t kLumaBloomSlotTPS = 8;

// Scaleform item-card price shaders: mask-fill + digit-glyph PS hashes (a pair per dgVoodoo build).
static constexpr uint32_t kScaleformMaskFillHash2813 = 0x9F8EA541;
static constexpr uint32_t kScaleformDigitGlyphHash2813 = 0x63898919;
static constexpr uint32_t kScaleformMaskFillHash2873 = 0x616BEBBD;
static constexpr uint32_t kScaleformDigitGlyphHash2873 = 0x79CDF7BA;

// The motion vector fill's name in Core's native shader tables (see "Luma_BL2TPS_MotionVectorFill.hlsl")
static constexpr uint32_t kMotionVectorFillName = CompileTimeStringHash("BL2TPS Motion Vector Fill CS");

// The scene's first post passes after its translucency, which end a motion vector frame (the upscaler runs right before the first one;
// measured at BL2 4K: light shafts, FXAA, the depth of field's low resolution copy, bloom, tonemap).
// Not the depth of field's CoC (0x003AD65E): it runs before the translucent draws, which then draw into the scene again.
// The 2.81.3 twins are hashed offline from GlobalShaderCache-PC-D3D-SM3.bin (TDownsampleLightShaftsPixelShader<LS_Point> and its
// directional/spot light types, TFilterPixelShader, TDOFGatherPixelShader0; the bright pass's known twin reproduced as the control).
// TPS's cache shares these byte for byte except its tonemap and light shaft downsamples, and only its directional downsample still
// hashes apart after dgVoodoo.
static const ShaderHashesList kScenePostHashes = {.pixel_shaders = {
                                                     0xEB2678CD, // light shafts downsample
                                                     0x13098E07, // light shafts downsample, dgVoodoo 2.81.3
                                                     0xFE261570, // light shafts downsample, directional light
                                                     0xBFD04077, // light shafts downsample, directional light, dgVoodoo 2.81.3
                                                     0xA33D1DF9, // light shafts downsample, directional light, TPS
                                                     0x944209BA, // light shafts downsample, directional light, TPS, dgVoodoo 2.81.3
                                                     0xC3B44D30, // light shafts downsample, spot light (BL2 and TPS)
                                                     0x93DD55D8, // light shafts downsample, spot light (BL2 and TPS), dgVoodoo 2.81.3
                                                     0x070EAE70, // depth of field low resolution scene
                                                     0x6DF81571, // depth of field low resolution scene, dgVoodoo 2.81.3
                                                     0xC710CF7C, // bloom downsample
                                                     0x77A6E651, // bloom downsample, dgVoodoo 2.81.3
                                                     kBloomBrightPassHash,
                                                     kBloomBrightPassHash_v281,
                                                     kFXAAResolveHash,
                                                     kFXAAResolveHash_v281,
                                                     kTonemapHash,
                                                     kTonemapHash_v281,
                                                     kTonemapHashTPS,
                                                     kTonemapHashTPS_v281,
                                                  }};

// FSR's reactive mask is the alpha blended draws' reactivity, scaled, then 0 under the threshold and 0.9 over it. AMD's threshold is
// 0.2, but 0.5 doesn't shake static glows. The masks are freed once the fill hasn't written them for this many frames.
// Tunable live (MCP) in development builds
static PUBLISHING_CONSTEXPR float kReactiveScale = 1.f;
static PUBLISHING_CONSTEXPR float kReactiveThreshold = 0.5f;
static constexpr uint32_t kReactiveMasksIdleFrames = 300;

// User settings (persisted via ReShade config under the shared NAME section; loaded in LoadConfigs).
static bool g_smaa_enable = true;
static float g_rcas_sharpness = 0.f;        // RCAS sharpen on SMAA output (0 = off)
static bool g_hide_ui = false;              // hide the game's HUD (for clean screenshots)
static bool g_smaa_predication = true;      // SMAA predication on geometry (depth from scene-color .a)
static float g_smaa_pred_tolerance = 0.02f; // plane deviation counted as a full edge, as a fraction of view depth (MoH Airborne's calibrated value)
#if DEVELOPMENT
// Calibration aid for g_smaa_pred_tolerance, the only free parameter in the predication path: what predication
// does is an ABSENCE of smearing, which the eye reads badly and worse in motion, so judge the mask, not the frame.
static bool g_smaa_pred_debug = false;   // show the predication mask instead of the antialiased frame
static bool g_smaa_pred_measure = false; // one-shot: read the mask back and log its distribution (UI button)
#endif
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

#if DEVELOPMENT
// Why "DrawWithMotionVectors" refused a draw: the DEV counters, and the names they are registered under
enum MotionVectorReject : int
{
   REJECT_EXTRA_TARGET,
   REJECT_NO_SCENE,
   REJECT_OTHER_DEPTH_COLOR,
   REJECT_FORMAT,
   REJECT_SIZE,
   REJECT_CREATE,
   REJECT_BLEND,
   REJECT_SHADERS,
   REJECT_DEPTH_TEST,
   REJECT_COUNT
};
static constexpr const char* kMotionVectorRejectNames[REJECT_COUNT] = {"extra_target", "no_scene", "other_depth_color", "format", "size", "create", "blend", "shaders", "depth_test"};
#endif

struct Borderlands2GameDeviceData final : public GameDeviceData
{
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
   bool grade_k_warned = false; // one-shot: a non-zero ImageAdjustments K, which the HDR continuation does not model

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
   TextureCapture pred_measure;                   // one-shot readback of the predication mask, driven by the UI button
   float bloom_tint_live[3] = {-1.f, -1.f, -1.f}; // tonemap cb4[16].rgb, the per-area authored composite tint
   std::unordered_set<uint64_t> grade_cb_logged;  // quantized constant sets already reported
   std::unordered_set<uint64_t> bloom_cb_logged;
#endif

   // SMAA metrics CB (b1) = (1/w,1/h,w,h) + (predication scale,0,0,0); scale 2.0 when predication on, else 1.0.
   ComPtr<ID3D11Buffer> cb_smaa_metrics;
   uint32_t smaa_metrics_w = 0, smaa_metrics_h = 0;

   // SMAA scratch, one texture per colour domain DrawSMAA takes. tex_input_encoded = snapshot of the LDR as the
   // tonemap wrote it (gamma 2.2), for edge detection; tex_input_linear = its decode (fp16), for the neighborhood
   // blend, written by the linearize CS.
   ComPtr<ID3D11Texture2D> tex_input_encoded;
   ComPtr<ID3D11ShaderResourceView> srv_input_encoded;
   ComPtr<ID3D11Texture2D> tex_input_linear;
   ComPtr<ID3D11UnorderedAccessView> uav_input_linear;
   ComPtr<ID3D11ShaderResourceView> srv_input_linear;
   uint32_t smaa_temps_w = 0, smaa_temps_h = 0;

   // SMAA output temp (SRV+RTV), allocated only when RCAS is on: it is the RCAS input. Without RCAS the SMAA
   // chain renders into the LDR RTV directly.
   ComPtr<ID3D11Texture2D> tex_smaa_out;
   ComPtr<ID3D11RenderTargetView> tex_smaa_out_rtv;
   ComPtr<ID3D11ShaderResourceView> tex_smaa_out_srv;
   uint32_t smaa_out_w = 0, smaa_out_h = 0;

   // Resource the tonemap renders to; on BL2 the HUD draws onto it afterwards. Used by Hide UI and the FXAA override.
   uint64_t ldr_buffer_handle = 0;
   // Set when the tonemap runs, cleared every Present: scopes Hide UI's alpha-blend skip to the post-tonemap
   // span of THIS frame (so next frame's pre-tonemap transparents aren't dropped). See the Hide UI block in OnDrawOrDispatch.
   bool tonemap_fired_this_frame = false;

   // SMAA depth predication. Scene-color SRV (depth packed in .a) captured at the tonemap, which the FXAA override
   // also keys on, + the single-channel (R16F) plane-deviation edge-ness the BL2TPS Depth Extract CS builds from it.
   ComPtr<ID3D11ShaderResourceView> srv_scene_depth;
   ComPtr<ID3D11Texture2D> tex_pred;
   ComPtr<ID3D11UnorderedAccessView> uav_pred;
   ComPtr<ID3D11ShaderResourceView> srv_pred;
   uint32_t pred_w = 0, pred_h = 0;
   ComPtr<ID3D11Buffer> cb_pred;
   float pred_tolerance = -1.f;
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

   // ---- DLSS / FSR / XeSS through the SR bridge (the game is 32-bit): motion vectors from patched shaders, per-draw constants from dgVoodoo's vc4 ----
   // Latched at present (see "IsSRActive"), as the "Super Resolution" selection can change mid frame. It also gates this frame's motion
   // vectors and jitter.
   std::atomic<bool> sr_active = false;
   // The upscaler's output goes back into the scene (and its copy, see "mv_scene_copy") without its alpha (the encoded view depth the
   // post passes read), drawn from this view of it
   com_ptr<ID3D11BlendState> sr_rgb_blend_state;
   com_ptr<ID3D11ShaderResourceView> sr_output_srv;
   // Set when None is picked (in the overlay, after present latched "sr_active"); the upscaler's resources go at the next present
   std::atomic<bool> release_sr_resources = false;

   std::shared_mutex mv_mutex;
   // A game shader's patched version by its hash (null if refused), made on first use. A vertex shader's also has the bytes of vc4 it
   // reads ("DXBC::ConstantBufferBytes", the only ones uploaded as the previous frame's copy), its LocalToWorld translation row, and
   // whether its vertices come already translated by this frame's PreViewTranslation (see "DrawWithMotionVectors")
   template <typename T>
   struct PatchedShader
   {
      com_ptr<T> shader;
      UINT read_size = 0;
      UINT translation_offset = 0;
      bool camera_relative_vertices = false;
   };
   std::unordered_map<uint32_t, PatchedShader<ID3D11VertexShader>> mv_vertex_shaders;
   std::unordered_map<uint32_t, PatchedShader<ID3D11PixelShader>> mv_pixel_shaders;
   // The alpha blended draws' pixel shaders with the mask target, by blend (see "ClassifyBoundBlend", index - 1)
   std::unordered_map<uint32_t, PatchedShader<ID3D11PixelShader>> mv_reactive_pixel_shaders[2];
   // The motion vector target (sized like the scene; every blend state writes it unblended, see "OnCreateBlendState")
   com_ptr<ID3D11Texture2D> mv_texture;
   com_ptr<ID3D11RenderTargetView> mv_rtv;
   com_ptr<ID3D11UnorderedAccessView> mv_uav; // Null without typed UAV loads of its format (then no upscaler)
   com_ptr<ID3D11ShaderResourceView> mv_srv;
   // The upscaler's depth, built from the scene's alpha by the fill (the game's depth has no shader resource view)
   com_ptr<ID3D11Texture2D> mv_device_depth;
   com_ptr<ID3D11UnorderedAccessView> mv_device_depth_uav;
   // FSR's reactive and transparency & composition masks (written by the fill from "mv_reactive_target")
   com_ptr<ID3D11Texture2D> mv_reactive;
   com_ptr<ID3D11UnorderedAccessView> mv_reactive_uav;
   com_ptr<ID3D11Texture2D> mv_transparency;
   com_ptr<ID3D11UnorderedAccessView> mv_transparency_uav;
   // What the alpha blended draws wrote (x reactive, y transparency & composition; max blended, see "OnCreateBlendState"), read by the
   // fill. Created when FSR first runs with the masks, gone "kReactiveMasksIdleFrames" after the fill last wrote them (address space).
   com_ptr<ID3D11Texture2D> mv_reactive_target;
   com_ptr<ID3D11RenderTargetView> mv_reactive_target_rtv;
   com_ptr<ID3D11ShaderResourceView> mv_reactive_target_srv;
   uint32_t sr_reactive_frame = 0;
   // A frame opens at its first mesh draw into output sized depth (the jitter is chosen there), starts at its first motion vector
   // draw (the target is cleared) and ends at the first post pass ("kScenePostHashes"), once per present.
   bool mv_scene_open = false;
   bool mv_scene_done = false;
   bool mv_frame_ended = true;
   bool mv_fill_pending = false;
   // The upscaler drew the last frame: only then the scene jitters, as skipped frames would reach the screen jittered. Those are scenes
   // no listed pass ends and the loading screen's. Measured: its spinning weapon draws with jitter, but without a motion vector draw
   // the fill and the upscaler don't run.
   bool mv_jitter_allowed = false;
   float sr_vert_fov = 1.0471976f; // FSR's vertical FOV (radians): the last camera's, 60 degrees until one is seen
   // The upscalers' near and far (game units): the last camera's with a usable projection, Core's defaults until one is seen
   float sr_near_plane = SR::SuperResolutionImpl::DrawData{}.near_plane;
   float sr_far_plane = SR::SuperResolutionImpl::DrawData{}.far_plane;
   com_ptr<ID3D11Resource> mv_depth; // The scene depth (the depth view's resource)
   // The fp16 scene the motion vector draws write, and the game's view of it
   com_ptr<ID3D11Resource> mv_scene_color;
   com_ptr<ID3D11RenderTargetView> mv_scene_rtv;
   com_ptr<ID3D11ShaderResourceView> mv_scene_srv; // The fill's view of it, kept while the scene is the same resource
   // The copy of the scene the first post pass reads (UE3 resolves the scene surface into a texture), null if it reads none. The
   // upscaler's output goes into both: post passes draw onto the scene and resolve it again (light shafts, measured).
   com_ptr<ID3D11Resource> mv_scene_copy;
   com_ptr<ID3D11Buffer> mv_fill_buffer;
   // The projection jitter (pixels, +y down), chosen when the scene opens; its NDC offset is at VS "MotionVectorPatches::JITTER_SLOT"
   // of every mesh draw depth tested against the scene
   std::array<float, 2> mv_jitter = {};
   std::array<float, 2> mv_jitter_ndc = {}; // The same offset in NDC (y up), as the jitter buffer holds it
   com_ptr<ID3D11Buffer> mv_jitter_buffer;
   // Per-draw lookups kept for the next draw and reset when the scene opens: the jitter path's last depth view and whether it's the scene
   // depth, the motion vector path's last accepted targets, the last blend state's opacity, and the last vertex and pixel shaders'
   // patched versions (owned by the maps above, never erased)
   ID3D11DepthStencilView* jitter_dsv = nullptr;
   bool jitter_dsv_scene = false;
   ID3D11DepthStencilView* mv_accepted_dsv = nullptr;
   ID3D11BlendState* mv_blend_state = nullptr; // See "ClassifyBoundBlend"
   bool mv_blend_opaque = true;
   // The immediate context's bindings as the game last bound them (see "PatchedDraws::BoundStates"), vc4 at
   // "MotionVectorPatches::OBJECT_SLOT"
   PatchedDraws::BoundStates<MotionVectorPatches::OBJECT_SLOT> bound_states;
   uint8_t mv_reactive_blend = 0;
   uint32_t mv_last_vertex_shader_hash = 0;
   PatchedShader<ID3D11VertexShader> mv_last_vertex_shader;
   uint32_t mv_last_pixel_shader_hash = 0;
   ID3D11PixelShader* mv_last_pixel_shader = nullptr;
   PatchedDraws::BoundShader<ID3D11VertexShader> mv_bound_vertex_shader;
   PatchedDraws::BoundShader<ID3D11PixelShader> mv_bound_pixel_shader;

   // CPU copies of the vc4 buffers the motion vector draws bind (see "PatchedDraws::ObjectConstants"), from a Map(WRITE_DISCARD) at
   // its Unmap or an UpdateSubresource. Every call but "MayBeRegistered" and "Restart" is under "mv_constants_mutex", as the buffer
   // hooks can run on several threads.
   using ConstantsCopy = PatchedDraws::ObjectConstants::Copy;
   std::shared_mutex mv_constants_mutex;
   PatchedDraws::ObjectConstants mv_constants;
   // Previous frame constants of the motion vector draws (see "PatchedDraws::PreviousConstants")
   PatchedDraws::PreviousConstants mv_previous_constants;
   // Motion vector draws by draw key (shaders, buffers, arguments), with a world LocalToWorld and vc4. A draw takes the previous
   // frame's vc4 of its key's nearest draw (same object, a frame earlier), its camera included.
   struct MotionVectorObject
   {
      PatchedDraws::ObjectTransform transform; // LocalToWorld, its translation in world space
      ConstantsCopy constants;
   };
   PatchedDraws::FrameObjects<MotionVectorObject, PatchedDraws::KeyHash> mv_objects;
   // The frame's camera (vc4 of its first motion vector draw) and the previous frame's
   ConstantsCopy mv_camera;
   ConstantsCopy mv_previous_camera;
   uint32_t mv_frame_index = 0;              // The Luma frame index of the last motion vector frame
   std::vector<uint8_t> mv_camera_only_copy; // An unmatched draw's constants with last frame's camera (reused)

#if DEVELOPMENT
   // Per frame counts for the MCP (the last complete frame's registered). The buffer hooks' ones are written under "mv_constants_mutex".
   struct MotionVectorStats
   {
      uint32_t motion_vector_draws = 0, jitter_draws = 0, matched = 0, camera_only = 0, other_camera = 0, uncopied = 0, maps = 0, updates = 0, sr_draws = 0;
      uint32_t reactive_draws = 0;          // Alpha blended draws that wrote FSR's masks
      uint32_t ended_by = 0;                // The ending pass's PS hash
      uint32_t rejected[REJECT_COUNT] = {}; // "DrawWithMotionVectors" refusals by reason ("MotionVectorReject")
   };
   MotionVectorStats mv_stats, mv_last_stats;
#endif

   // The motion vector target and the device depth the fill writes, recreated at their next use
   void ReleaseMotionVectorTargets()
   {
      mv_texture.reset();
      mv_rtv.reset();
      mv_uav.reset();
      mv_srv.reset();
      mv_device_depth.reset();
      mv_device_depth_uav.reset();
      ReleaseReactiveMasks();
   }

   // FSR's masks and what the draws wrote for them, recreated at their next use
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

   // Named injected shaders live in unordered_maps the render thread otherwise only reads: look them up with
   // "find" (operator[] would default-insert on a miss, mutating a map core's draw helpers read concurrently).
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

   // Default-pool 2D texture. Format defaults to fp16 (the SMAA linear copy); pass the live LDR format for LDR-shaped
   // temps, since CopyResource requires identical formats.
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
   // Post-tonemap SMAA on the gamma LDR: AFTER the tonemap, so it cannot perturb the DoF composited inside it.
   // Snapshot LDR -> linearize (+ predication extract) CS -> DrawSMAA -> optional RCAS; the last pass writes the LDR RTV.
   // Without "smaa" (an upscaler antialiased the frame) only RCAS runs: snapshot -> RCAS -> LDR.
   void RunPostTonemapSMAA(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, DeviceData& device_data, Borderlands2GameDeviceData& gd, ID3D11RenderTargetView* ldr_rtv, bool smaa)
   {
      ComPtr<ID3D11Resource> ldr_res;
      ldr_rtv->GetResource(ldr_res.put());
      uint4 cinfo{};
      DXGI_FORMAT cfmt = DXGI_FORMAT_UNKNOWN;
      GetResourceInfo(ldr_res.get(), cinfo, cfmt);
      uint32_t w = cinfo.x, h = cinfo.y;
      if (w == 0 || h == 0 || (uint32_t)cfmt == (uint32_t)DXGI_FORMAT_UNKNOWN)
      {
         return;
      }

      if (!smaa)
      {
         if (!PrepareRCAS(native_device, device_data))
            return;
         uint4 snapshot_size{};
         DXGI_FORMAT snapshot_format = DXGI_FORMAT_UNKNOWN;
         if (gd.tex_input_encoded)
         {
            GetResourceInfo(gd.tex_input_encoded.get(), snapshot_size, snapshot_format);
         }
         if (!gd.srv_input_encoded || snapshot_size.x != w || snapshot_size.y != h || snapshot_format != cfmt)
         {
            gd.srv_input_encoded.reset();
            gd.tex_input_encoded.reset();
            gd.smaa_temps_w = 0; // The SMAA chain's temps are made again with it
            if (!CreateDefaultTex(native_device, w, h, D3D11_BIND_SHADER_RESOURCE, gd.tex_input_encoded, cfmt) || FAILED(native_device->CreateShaderResourceView(gd.tex_input_encoded.get(), nullptr, gd.srv_input_encoded.put())))
               return;
         }
         native_device_context->CopyResource(gd.tex_input_encoded.get(), ldr_res.get());
         DrawRCAS(native_device_context, device_data, gd.srv_input_encoded.get(), ldr_rtv, g_rcas_sharpness);
         return;
      }

      // Shader-readiness gate (async loader / dev live-reload): skip SMAA this frame if anything is missing. The
      // decode feeds the neighborhood pass; without it that pass would sample an empty texture, so the chain is
      // all-or-nothing rather than partially configured.
      auto* linearize_cs = FindShader(device_data.native_compute_shaders, CompileTimeStringHash("BL2TPS SMAA Linearize CS"));
      const bool smaa_ready = linearize_cs != nullptr &&
                              AllShadersReady(device_data.native_pixel_shaders, {CompileTimeStringHash("SMAA Edge Detection PS"), CompileTimeStringHash("SMAA Blending Weight Calculation PS"), CompileTimeStringHash("SMAA Neighborhood Blending PS")}) &&
                              AllShadersReady(device_data.native_vertex_shaders, {CompileTimeStringHash("SMAA Edge Detection VS"), CompileTimeStringHash("SMAA Blending Weight Calculation VS"), CompileTimeStringHash("SMAA Neighborhood Blending VS")});
      if (!smaa_ready)
      {
         return;
      }

      // SMAA depth predication: plane-deviation edge-ness from the captured scene-color SRV (.a). Plain ULTRA fallback when
      // any input is missing (never scale 2.0 with a null texture).
      auto* pred_cs = FindShader(device_data.native_compute_shaders, CompileTimeStringHash("BL2TPS Depth Extract CS"));
      bool pred_ok = g_smaa_predication && gd.srv_scene_depth && pred_cs != nullptr;
      if (pred_ok)
      {
         // The extract CS maps texels 1:1, so a scene buffer of a different size would read a sub-rect and
         // misalign the predication signal against the LDR grid. Fall back to plain ULTRA instead.
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

      // Resolve RCAS before allocating: it decides whether SMAA renders into the LDR RTV directly or into the
      // intermediate RCAS reads.
      bool do_sharpen = g_rcas_sharpness > 0.f && PrepareRCAS(native_device, device_data);
      if (do_sharpen)
      {
         // SMAA output temp (LDR format, SRV+RTV): only needed as the RCAS input.
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
         {
            do_sharpen = false;
         }
      }

      // The two colour inputs: the encoded snapshot (LDR format, so CopyResource matches) and its linear decode.
      // The decode is fp16 on purpose - quantizing linear light back into the LDR's 8-bit-derived range before the
      // blend would put the encode's rounding inside SMAA's arithmetic.
      if (!gd.tex_input_encoded || gd.smaa_temps_w != w || gd.smaa_temps_h != h)
      {
         gd.srv_input_encoded.reset();
         gd.tex_input_encoded.reset();
         gd.srv_input_linear.reset();
         gd.uav_input_linear.reset();
         gd.tex_input_linear.reset();
         if (CreateDefaultTex(native_device, w, h, D3D11_BIND_SHADER_RESOURCE, gd.tex_input_encoded, cfmt) && CreateDefaultTex(native_device, w, h, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS, gd.tex_input_linear))
         {
            native_device->CreateShaderResourceView(gd.tex_input_encoded.get(), nullptr, gd.srv_input_encoded.put());
            native_device->CreateUnorderedAccessView(gd.tex_input_linear.get(), nullptr, gd.uav_input_linear.put());
            native_device->CreateShaderResourceView(gd.tex_input_linear.get(), nullptr, gd.srv_input_linear.put());
            gd.smaa_temps_w = w;
            gd.smaa_temps_h = h;
         }
      }
      if (!gd.srv_input_encoded || !gd.uav_input_linear || !gd.srv_input_linear)
         return;

      // Snapshot the LDR color into an SRV-readable temp (LDR is both the SMAA input and its output target).
      native_device_context->CopyResource(gd.tex_input_encoded.get(), ldr_res.get());

      // Both compute prepasses, under one state stack. Restoring it before DrawSMAA is what makes the results
      // readable: an SRV of a resource still bound as a UAV reads as null, so the linear copy has to leave u0
      // before the neighborhood pass samples it.
      {
         DrawStateStack<DrawStateStackType::Compute> cs_state;
         cs_state.Cache(native_device_context, device_data.uav_max_count);

         // Decode the snapshot into linear light for the neighborhood blend. Edge detection keeps the encoded one.
         ID3D11ShaderResourceView* lin_srv = gd.srv_input_encoded.get();
         ID3D11UnorderedAccessView* lin_uav = gd.uav_input_linear.get();
         native_device_context->CSSetUnorderedAccessViews(0, 1, &lin_uav, nullptr);
         native_device_context->CSSetShaderResources(0, 1, &lin_srv);
         native_device_context->CSSetShader(linearize_cs, nullptr, 0);
         native_device_context->Dispatch((w + 7) / 8, (h + 7) / 8, 1);

         // Predication extract: scene-color .a -> plane-deviation edge-ness in R16F (gd.tex_pred). Independent of
         // the snapshot; see Luma_BL2TPS_DepthExtract.hlsl for why this is an edge test rather than a depth rescale.
         if (pred_ok)
         {
            ID3D11ShaderResourceView* ps_srv = gd.srv_scene_depth.get();
            ID3D11UnorderedAccessView* ps_uav = gd.uav_pred.get();
            ID3D11Buffer* ps_cb = gd.cb_pred.get();
            native_device_context->CSSetUnorderedAccessViews(0, 1, &ps_uav, nullptr);
            native_device_context->CSSetShaderResources(0, 1, &ps_srv);
            native_device_context->CSSetConstantBuffers(0, 1, &ps_cb);
            native_device_context->CSSetShader(pred_cs, nullptr, 0);
            native_device_context->Dispatch((w + 7) / 8, (h + 7) / 8, 1);
         }

         cs_state.Restore(native_device_context);
      }

#if DEVELOPMENT
      // Both calibration aids read the mask the extract CS just wrote. The numeric one runs first so it still
      // reports while the debug view is on (that path returns early).
      if (pred_ok)
         LogPredicationStats(native_device, native_device_context, gd);

      if (pred_ok && g_smaa_pred_debug)
      {
         auto* copy_vs = FindShader(device_data.native_vertex_shaders, CompileTimeStringHash("Copy VS"));
         auto* copy_ps = FindShader(device_data.native_pixel_shaders, CompileTimeStringHash("Copy PS"));
         if (copy_vs != nullptr && copy_ps != nullptr)
         {
            // The mask is single-channel, so the core copy lands it in RED - unmistakably a debug view. It
            // REPLACES the antialiased frame rather than blending over it, hence the early return (which also
            // skips the metrics CB swap below, so there is nothing left to restore).
            DrawStateStack<DrawStateStackType::FullGraphics> debug_state;
            debug_state.Cache(native_device_context, device_data.uav_max_count);
            DrawCustomPixelShader(native_device_context, device_data.default_depth_stencil_state.get(), device_data.default_blend_state.get(), nullptr,
               copy_vs, copy_ps, gd.srv_pred.get(), ldr_rtv, w, h, false);
            debug_state.Restore(native_device_context);
            return;
         }
      }
#endif

      // The last pass of the chain renders straight into the LDR RTV, which is safe because SMAA and RCAS sample
      // the snapshot copies, never the LDR itself.
      DrawSMAA(native_device, native_device_context, device_data,
         do_sharpen ? gd.tex_smaa_out_rtv.get() : ldr_rtv,
         gd.srv_input_linear.get() /*neighborhood blend (linear light)*/,
         gd.srv_input_encoded.get() /*edge detection (gamma 2.2)*/,
         pred_ok ? gd.srv_pred.get() : nullptr /*predication signal*/, gd.cb_smaa_metrics.get());

      if (do_sharpen)
      {
         DrawRCAS(native_device_context, device_data, gd.tex_smaa_out_srv.get(), ldr_rtv, g_rcas_sharpness);
      }
   }
#endif // ENABLE_SMAA

   // ---- DLSS / FSR / XeSS through the SR bridge, with motion vectors and jitter from patched shaders (vc4 layout in "MotionVectorPatches") ----
   // c0-c3, row vectors, translated by PreViewTranslation (UE3 draws in world space minus the camera position)
   static constexpr size_t kViewProjectionOffset = MotionVectorPatches::VIEW_PROJECTION_ROW * 16;
   // ... c4 the camera position and c5 PreViewTranslation: the whole camera
   static constexpr size_t kCameraSize = 6 * 16;
   static constexpr size_t kPreViewTranslationOffset = (MotionVectorPatches::OBJECT_ROW_OFFSET + 5) * 16;
   // c9: LocalToWorld's translation row, which includes PreViewTranslation
   static constexpr size_t kTranslationOffset = (MotionVectorPatches::OBJECT_ROW_OFFSET + 9) * 16;
   // Skinned vertex shaders (bones from c6) hold LocalToWorld at c231-c234
   static constexpr size_t kSkinnedTranslationOffset = (MotionVectorPatches::OBJECT_ROW_OFFSET + 234) * 16;
   // A camera ("mv_camera") comes from constants holding the translation row, so it always holds the whole camera
   static_assert(kTranslationOffset + 16 >= kViewProjectionOffset + kCameraSize);

   // An upscaler is picked and hasn't failed (it then gives way to SMAA until picked again). Fixed for the whole frame (see
   // "OnPresent"): a selection made after the motion vector state was set would otherwise run the upscaler on mixed state.
   static bool IsSRActive(DeviceData& device_data)
   {
      return GetGameDeviceData(device_data).sr_active;
   }

   // The upscalers that read the reactive mask the alpha blended draws write: FSR. DLSS's current presets ignore it.
   static bool IsReactiveMaskUsed(DeviceData& device_data)
   {
      return IsSRActive(device_data) && (device_data.sr_type == SR::Type::FSR || device_data.sr_type == SR::Type::FSR_4);
   }

   static std::array<float, 3> GetPreViewTranslation(const std::vector<uint8_t>& constants)
   {
      std::array<float, 3> translation;
      std::memcpy(translation.data(), constants.data() + kPreViewTranslationOffset, sizeof(translation));
      return translation;
   }

   // Last frame's view projection (row vectors) for this frame's PreViewTranslation: previous clip = (world + previous PVT) * previous
   // VP = (translated + previous PVT - PVT) * previous VP, so the translation row gains the delta times the first three rows
   static std::array<double, 16> GetPreviousViewProjection(const std::vector<uint8_t>& previous_camera, const std::vector<uint8_t>& camera)
   {
      const float* const previous = reinterpret_cast<const float*>(previous_camera.data() + kViewProjectionOffset);
      const std::array<float, 3> previous_translation = GetPreViewTranslation(previous_camera), translation = GetPreViewTranslation(camera);
      std::array<double, 16> view_projection;
      std::copy_n(previous, 16, view_projection.data());
      for (int row = 0; row < 3; row++)
      {
         const double delta = double(previous_translation[row]) - translation[row];
         for (int column = 0; column < 4; column++)
         {
            view_projection[12 + column] += delta * previous[row * 4 + column];
         }
      }
      return view_projection;
   }

   // Motion vectors: a registered vc4 buffer mapped for a whole rewrite, remembered until its Unmap. The hooks can run on several
   // threads, hence the lock (see "mv_constants_mutex").
   static void OnMapBufferRegion(reshade::api::device* device, reshade::api::resource resource, uint64_t offset, uint64_t size, reshade::api::map_access access, void** data)
   {
      DeviceData* const device_data = device->get_private_data<DeviceData>();
      if (!device_data || !device_data->game || !GetGameDeviceData(*device_data).sr_active || !data || !*data)
         return;
      auto& gd = GetGameDeviceData(*device_data);
      // Lock free: the hooks see every Map and Unmap of the game
      if (!gd.mv_constants.MayBeRegistered(resource.handle))
         return;
      const std::lock_guard lock(gd.mv_constants_mutex);
      gd.mv_constants.OnMap(resource.handle, offset, access, data);
   }

   // Motion vectors: a redirected Map of a vc4 buffer writes its data to the buffer, before its Unmap. Not gated on "sr_active": the
   // present that turns it off can fall between the two.
   static void OnUnmapBufferRegion(reshade::api::device* device, reshade::api::resource resource)
   {
      DeviceData* const device_data = device->get_private_data<DeviceData>();
      if (!device_data || !device_data->game)
         return;
      auto& gd = GetGameDeviceData(*device_data);
      if (!gd.mv_constants.MayBeRegistered(resource.handle))
         return;
      const std::lock_guard lock(gd.mv_constants_mutex);
      gd.mv_constants.OnUnmap(resource.handle);
#if DEVELOPMENT
      gd.mv_stats.maps++; // Unmaps of buffers that may be registered (OnUnmap doesn't say whether it copied)
#endif
   }

   // Motion vectors: the CPU copy of a vc4 buffer from an UpdateSubresource (before it runs). Needs the unsigned "full add-on support"
   // ReShade (the signed build doesn't raise the event).
   static bool OnUpdateBufferRegion(reshade::api::device* device, const void* data, reshade::api::resource resource, uint64_t offset, uint64_t size)
   {
      DeviceData* const device_data = device->get_private_data<DeviceData>();
      if (!device_data || !device_data->game || !data || !GetGameDeviceData(*device_data).sr_active)
         return false;
      auto& gd = GetGameDeviceData(*device_data);
      if (!gd.mv_constants.MayBeRegistered(resource.handle))
         return false;
      const std::lock_guard lock(gd.mv_constants_mutex);
      gd.mv_constants.OnUpdate(resource.handle, data, offset, size);
#if DEVELOPMENT
      gd.mv_stats.updates++; // Updates of buffers that may be registered
#endif
      return false;
   }

   // The bound shader's motion vector version (or, "reactive" > 0, a pixel shader's mask one, see "ClassifyBoundBlend"), patched from
   // Core's bytecode copy on first use (null if it can't be, e.g. a vertex shader that doesn't place vertices with the view projection)
   template <typename T>
   static Borderlands2GameDeviceData::PatchedShader<T> GetMotionVectorShader(ID3D11Device* native_device, DeviceData& device_data,
      std::unordered_map<uint32_t, Borderlands2GameDeviceData::PatchedShader<T>>* shaders, uint32_t hash, reshade::api::pipeline pipeline, uint8_t reactive = 0)
   {
      constexpr bool vertex = std::is_same_v<T, ID3D11VertexShader>;
      auto& gd = GetGameDeviceData(device_data);
      {
         const std::shared_lock lock(gd.mv_mutex);
         if (const auto it = shaders->find(hash); it != shaders->end())
            return it->second;
      }
      std::vector<uint8_t> patched;
      std::string error = "no bytecode";
      UINT read_size = 0;
      UINT translation_offset = kTranslationOffset;
      bool camera_relative_vertices = false;
      {
         const std::shared_lock lock(s_mutex_generic);
         if (const auto it = device_data.pipeline_cache_by_pipeline_handle.find(pipeline.handle); it != device_data.pipeline_cache_by_pipeline_handle.end() && it->second->subobjects_cache)
         {
            const auto* desc = static_cast<const reshade::api::shader_desc*>(it->second->subobjects_cache[0].data);
            const auto* code = static_cast<const uint8_t*>(desc->code);
            if constexpr (vertex)
            {
               patched = MotionVectorPatch::PatchVertexShader(code, desc->code_size, MotionVectorPatches::LAYOUT, &error);
               read_size = DXBC::ConstantBufferBytes(code, desc->code_size, MotionVectorPatches::OBJECT_SLOT);
               if (DXBC::ReadsConstantRow(code, desc->code_size, MotionVectorPatches::OBJECT_SLOT, kSkinnedTranslationOffset / 16))
               {
                  translation_offset = kSkinnedTranslationOffset;
               }
               // Camera relative: the position depends on the view projection of vc4 (c0-c3) alone, with no translation from it
               // (PreViewTranslation c5, LocalToWorld c6-c9 or skinned c231-c234, terrain's c10-c13, a sprite's center...). The rows read
               // don't tell: instanced foliage reads c6 as its light map scale and bias (its instance matrices are camera relative), while
               // other instanced meshes add c6 to their instance translation. This matches 13 of the 874 dumped vertex shaders: instanced foliage (0x35035418,
               // 0x36AAA831, 0x4562B3A2, 0x55238014, 0x69DB8300, 0x726C5819, 0xAF966A18, 0xBADE03D9, 0xCBBB7761, 0xD59C4FB4, 0xFD587A0F)
               // and position only ones (0x483A369B, 0x5FC5315D).
               camera_relative_vertices = DXBC::PositionDependsOnlyOnConstantRows(code, desc->code_size, MotionVectorPatches::OBJECT_SLOT, kViewProjectionOffset / 16, kViewProjectionOffset / 16 + 3);
            }
            else if (reactive != 0)
            {
               patched = MotionVectorPatch::PatchPixelShaderReactive(code, desc->code_size, MotionVectorPatches::LAYOUT, MotionVectorPatches::REACTIVE_SLOT, reactive == 2 ? MotionVectorPatch::ReactiveMode::ADDITIVE : MotionVectorPatch::ReactiveMode::ALPHA, &error);
            }
            else
            {
               patched = MotionVectorPatch::PatchPixelShader(code, desc->code_size, MotionVectorPatches::LAYOUT, &error);
            }
         }
      }
      com_ptr<T> shader;
      if (!patched.empty())
      {
         HRESULT hr;
         if constexpr (vertex)
         {
            hr = native_device->CreateVertexShader(patched.data(), patched.size(), nullptr, &shader);
         }
         else
         {
            hr = native_device->CreatePixelShader(patched.data(), patched.size(), nullptr, &shader);
         }
         if (FAILED(hr))
         {
            error = std::format("create 0x{:08X}", uint32_t(hr));
         }
      }
      // Failures are logged in every build (bug reports) except the expected screen space refusals, every patched shader only in development
      const bool screen_space = error.starts_with("screen space");
      if (DEVELOPMENT || (!shader && !screen_space))
      {
         reshade::log::message((shader || screen_space) ? reshade::log::level::info : reshade::log::level::warning,
            std::format("[BL2 MV] {} 0x{:08X} {}", vertex ? "VS" : (reactive != 0 ? "PS (mask)" : "PS"), hash, shader ? "patched" : error).c_str());
      }
      const std::unique_lock lock(gd.mv_mutex);
      return shaders->try_emplace(hash, Borderlands2GameDeviceData::PatchedShader<T>{.shader = shader, .read_size = read_size, .translation_offset = translation_offset, .camera_relative_vertices = camera_relative_vertices}).first->second;
   }

   // The bound vertex shader's patched version (null shader if refused), looked up again only when the game's shader changes
   static const Borderlands2GameDeviceData::PatchedShader<ID3D11VertexShader>& GetPatchedVertexShader(ID3D11Device* native_device, CommandListData& cmd_list_data, DeviceData& device_data, uint32_t hash)
   {
      auto& gd = GetGameDeviceData(device_data);
      if (hash != gd.mv_last_vertex_shader_hash)
      {
         gd.mv_last_vertex_shader = GetMotionVectorShader(native_device, device_data, &gd.mv_vertex_shaders, hash, cmd_list_data.pipeline_state_original_vertex_shader);
         gd.mv_last_vertex_shader_hash = hash;
      }
      return gd.mv_last_vertex_shader;
   }

   // Opens the scene at the frame's first mesh draw into output sized depth: takes the scene depth and picks the jitter the whole
   // scene draws with. Once per present (the HUD and later passes never reopen it).
   static void OpenScene(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, uint32_t vertex_shader_hash, ID3D11DepthStencilView* dsv)
   {
      auto& gd = GetGameDeviceData(device_data);
      uint4 depth_size;
      DXGI_FORMAT unused_format;
      GetResourceInfo(dsv, depth_size, unused_format);
      if (depth_size.x != device_data.output_resolution.x || depth_size.y != device_data.output_resolution.y || !GetPatchedVertexShader(native_device, cmd_list_data, device_data, vertex_shader_hash).shader)
         return;
      gd.mv_scene_open = true;
      gd.mv_depth.reset();
      dsv->GetResource(&gd.mv_depth);
      gd.mv_scene_color.reset();
      gd.mv_scene_rtv.reset();
      gd.jitter_dsv = nullptr;
      gd.bound_states.ResetDepthTest();
      gd.mv_accepted_dsv = nullptr;
      gd.mv_blend_state = nullptr;
      gd.mv_blend_opaque = true;
      gd.mv_reactive_blend = 0;
      // Halton (2, 3) over the upscaler's phase count, in pixels then NDC (y up). None until the upscaler is ready, so the scene shows as
      // it is, antialiased with SMAA, while the bridge's helper starts. The type is read once, as in "DrawUpscaler".
      const SR::Type sr_type = device_data.sr_type;
      const SR::InstanceData* sr_instance_data = ((IsSRActive(device_data) && sr_type != SR::Type::None) ? device_data.GetSRInstanceData() : nullptr);
      if (sr_instance_data && !sr_implementations[sr_type]->IsReady(sr_instance_data))
      {
         sr_instance_data = nullptr;
      }
      gd.mv_jitter = {};
      if (sr_instance_data && gd.mv_jitter_allowed)
      {
         const unsigned int phase = cb_luma_global_settings.FrameIndex % unsigned((std::max)(sr_implementations[sr_type]->GetJitterPhases(sr_instance_data), 1));
         gd.mv_jitter = {SR::HaltonSequence(phase, 2), SR::HaltonSequence(phase, 3)};
      }
      gd.mv_jitter_ndc = {gd.mv_jitter[0] * 2.f / device_data.output_resolution.x, gd.mv_jitter[1] * -2.f / device_data.output_resolution.y};
      const float ndc_jitter[4] = {gd.mv_jitter_ndc[0], gd.mv_jitter_ndc[1], 0.f, 0.f};
      if (!PatchedDraws::WriteDynamicConstants(native_device, native_device_context, std::addressof(gd.mv_jitter_buffer), ndc_jitter, sizeof(ndc_jitter)))
      {
         // No stale jitter on the scene draws either: no motion vectors this frame
         gd.mv_jitter = {};
         gd.mv_jitter_ndc = {};
         gd.mv_jitter_buffer.reset();
      }
   }

   // The tracked bindings the "PatchedDraws::BoundStates" events update: the immediate context's
   static auto* GetBoundStates(reshade::api::command_list* cmd_list)
   {
      DeviceData* const device_data = cmd_list->get_device()->get_private_data<DeviceData>();
      const CommandListData* const cmd_list_data = cmd_list->get_private_data<CommandListData>();
      return ((device_data && device_data->game && cmd_list_data && cmd_list_data->is_primary) ? &GetGameDeviceData(*device_data).bound_states : nullptr);
   }

   // Classifies the bound blend state (cached in "mv_blend_state"): "mv_blend_opaque" for the motion vectors (additive lights, decals
   // and translucents keep the motion vectors of what's behind them, and so do colorless draws: the occlusion query bounding boxes),
   // and "mv_reactive_blend" for FSR's masks: 0 not alpha blended (opaque, additive ONE/ONE lights, modulated shadows), 1 alpha
   // blended (SRC_ALPHA / INV_SRC_ALPHA: smoke, glass, water; reactive and transparency & composition), 2 additive (SRC_ALPHA / ONE:
   // sparks, glows; reactive)
   static void ClassifyBoundBlend(ID3D11DeviceContext* native_device_context, Borderlands2GameDeviceData* gd)
   {
      ID3D11BlendState* const blend_state = gd->bound_states.BlendState(native_device_context);
      if (blend_state == gd->mv_blend_state)
      {
         return;
      }
      D3D11_BLEND_DESC blend_desc = CD3D11_BLEND_DESC(D3D11_DEFAULT);
      if (blend_state)
      {
         blend_state->GetDesc(&blend_desc);
      }
      const D3D11_RENDER_TARGET_BLEND_DESC& rt0 = blend_desc.RenderTarget[0];
      gd->mv_blend_opaque = rt0.RenderTargetWriteMask != 0 && (!rt0.BlendEnable || (rt0.SrcBlend == D3D11_BLEND_ONE && rt0.DestBlend == D3D11_BLEND_ZERO && rt0.BlendOp == D3D11_BLEND_OP_ADD));
      gd->mv_reactive_blend = ((!rt0.BlendEnable || rt0.SrcBlend != D3D11_BLEND_SRC_ALPHA) ? 0 : (rt0.DestBlend == D3D11_BLEND_ONE ? 2 : 1));
      gd->mv_blend_state = blend_state;
   }

   // A refusal of "DrawWithMotionVectors" (returns false), counted by reason in development builds; "gd" is its device data
#if DEVELOPMENT
#define MV_REJECT(reason) (gd.mv_stats.rejected[reason]++, false)
#else
#define MV_REJECT(reason) false
#endif

   // Draws an opaque draw into the fp16 scene (the scene target alone, output sized, with the scene depth) with the patched shaders,
   // adding the motion vector target ("TARGET_SLOT", past the game's) and the previous frame's vc4 ("PREVIOUS_SLOTS"). False if it
   // can't (the draw then goes to "DrawWithJitter").
   static bool DrawWithMotionVectors(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data,
      const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, const std::function<void()>& draw, const com_ptr<ID3D11RenderTargetView> (&rtvs)[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT], ID3D11DepthStencilView* dsv)
   {
      auto& gd = GetGameDeviceData(device_data);
      // The scene target alone, plus the motion vector and mask targets the last patched draws left bound
      for (UINT slot = 1; slot < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; slot++)
      {
         if (rtvs[slot] && (slot != MotionVectorPatches::TARGET_SLOT || rtvs[slot] != gd.mv_rtv) && (slot != MotionVectorPatches::REACTIVE_SLOT || rtvs[slot] != gd.mv_reactive_target_rtv))
            return MV_REJECT(REJECT_EXTRA_TARGET);
      }
      if (!rtvs[0] || !dsv || !gd.mv_scene_open)
         return MV_REJECT(REJECT_NO_SCENE);
      // Depth tested meshes only: the loading screen's stencil masked composites (PS 0xD81C32BD, 0xC121788F) draw into another fp16
      // target before its weapon and would take the scene's place
      if (!gd.bound_states.DepthTested(native_device_context))
         return MV_REJECT(REJECT_DEPTH_TEST);
      ClassifyBoundBlend(native_device_context, &gd);
      if (!gd.mv_blend_opaque)
         return MV_REJECT(REJECT_BLEND);
      // Known targets: checked, and the motion vector target built for them
      if (rtvs[0] != gd.mv_scene_rtv || dsv != gd.mv_accepted_dsv)
      {
         com_ptr<ID3D11Resource> depth;
         dsv->GetResource(&depth);
         com_ptr<ID3D11Resource> color;
         rtvs[0]->GetResource(&color);
         if (depth != gd.mv_depth || !color || (gd.mv_scene_color && color != gd.mv_scene_color))
            return MV_REJECT(REJECT_OTHER_DEPTH_COLOR);
         D3D11_RENDER_TARGET_VIEW_DESC rtv_desc;
         rtvs[0]->GetDesc(&rtv_desc);
         com_ptr<ID3D11Texture2D> color_texture;
         D3D11_TEXTURE2D_DESC color_desc = {};
         if (SUCCEEDED(color->QueryInterface(&color_texture)))
         {
            color_texture->GetDesc(&color_desc);
         }
         // The fill reads the scene: it needs a shader resource view. Filtered by the resource, not the view (dgVoodoo binds single-slice
         // array views).
         if (rtv_desc.Format != DXGI_FORMAT_R16G16B16A16_FLOAT || color_desc.ArraySize != 1 || color_desc.SampleDesc.Count != 1 || (color_desc.BindFlags & D3D11_BIND_SHADER_RESOURCE) == 0)
            return MV_REJECT(REJECT_FORMAT);
         const uint2 size = {color_desc.Width, color_desc.Height};
         if (size.x != device_data.output_resolution.x || size.y != device_data.output_resolution.y)
            return MV_REJECT(REJECT_SIZE);
         const std::unique_lock lock(gd.mv_mutex);
         D3D11_TEXTURE2D_DESC desc = {};
         if (gd.mv_texture)
         {
            gd.mv_texture->GetDesc(&desc);
         }
         // R16G16_FLOAT: FSR keeps 16 bits internally; the error is under 0.1% of the motion (BL GOTY)
         constexpr DXGI_FORMAT format = DXGI_FORMAT_R16G16_FLOAT;
         if (desc.Width != size.x || desc.Height != size.y)
         {
            gd.ReleaseMotionVectorTargets();
            // The fill reads the target back through its UAV
            D3D11_FEATURE_DATA_FORMAT_SUPPORT2 support = {format};
            const bool typed_uav_load = SUCCEEDED(native_device->CheckFeatureSupport(D3D11_FEATURE_FORMAT_SUPPORT2, &support, sizeof(support))) && (support.OutFormatSupport2 & D3D11_FORMAT_SUPPORT2_UAV_TYPED_LOAD) != 0;
            desc = CD3D11_TEXTURE2D_DESC(format, size.x, size.y, 1, 1, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE | (typed_uav_load ? D3D11_BIND_UNORDERED_ACCESS : 0u));
            if (FAILED(SRBridge::CreateSharableTexture(native_device, desc, &gd.mv_texture)) || FAILED(native_device->CreateRenderTargetView(gd.mv_texture.get(), nullptr, &gd.mv_rtv)))
            {
               gd.mv_texture.reset();
               gd.mv_rtv.reset();
               return MV_REJECT(REJECT_CREATE);
            }
            if (typed_uav_load)
            {
               native_device->CreateUnorderedAccessView(gd.mv_texture.get(), nullptr, &gd.mv_uav);
            }
            const CD3D11_TEXTURE2D_DESC depth_desc(DXGI_FORMAT_R32_FLOAT, size.x, size.y, 1, 1, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS);
            if (SUCCEEDED(SRBridge::CreateSharableTexture(native_device, depth_desc, &gd.mv_device_depth)))
            {
               native_device->CreateUnorderedAccessView(gd.mv_device_depth.get(), nullptr, &gd.mv_device_depth_uav);
            }
            gd.mv_frame_ended = true;
         }
         gd.mv_scene_color = color;
         gd.mv_scene_rtv = rtvs[0];
         gd.mv_accepted_dsv = dsv;
      }

      ID3D11VertexShader* const vertex_shader = GetPatchedVertexShader(native_device, cmd_list_data, device_data, original_shader_hashes.vertex_shaders[0]).shader.get();
      if (const uint32_t pixel_shader_hash = original_shader_hashes.pixel_shaders[0]; pixel_shader_hash != gd.mv_last_pixel_shader_hash)
      {
         gd.mv_last_pixel_shader = GetMotionVectorShader(native_device, device_data, &gd.mv_pixel_shaders, pixel_shader_hash, cmd_list_data.pipeline_state_original_pixel_shader).shader.get();
         gd.mv_last_pixel_shader_hash = pixel_shader_hash;
      }
      ID3D11PixelShader* const pixel_shader = gd.mv_last_pixel_shader;
      if (!vertex_shader || !pixel_shader || !gd.mv_jitter_buffer)
         return MV_REJECT(REJECT_SHADERS);
      if (std::exchange(gd.mv_frame_ended, false))
      {
         gd.mv_fill_pending = gd.mv_uav && gd.mv_device_depth_uav && FindShader(device_data.native_compute_shaders, kMotionVectorFillName) != nullptr;
         // The fill's marker: the largest float16 (a larger clear value is stored as it in R16G16_FLOAT)
         const FLOAT clear_value = (gd.mv_fill_pending ? 65504.f : 0.f);
         const FLOAT clear[4] = {clear_value, clear_value, 0.f, 0.f};
         native_device_context->ClearRenderTargetView(gd.mv_rtv.get(), clear);
         // The reactive masks: what the alpha blended draws write, from zero
         if (IsReactiveMaskUsed(device_data) && !gd.mv_reactive_target)
         {
            D3D11_TEXTURE2D_DESC mv_desc;
            gd.mv_texture->GetDesc(&mv_desc);
            const CD3D11_TEXTURE2D_DESC reactive_desc(DXGI_FORMAT_R8G8_UNORM, mv_desc.Width, mv_desc.Height, 1, 1, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
            const std::unique_lock lock(gd.mv_mutex);
            if (SUCCEEDED(native_device->CreateTexture2D(&reactive_desc, nullptr, &gd.mv_reactive_target)) && SUCCEEDED(native_device->CreateRenderTargetView(gd.mv_reactive_target.get(), nullptr, &gd.mv_reactive_target_rtv)))
            {
               native_device->CreateShaderResourceView(gd.mv_reactive_target.get(), nullptr, &gd.mv_reactive_target_srv);
            }
            gd.sr_reactive_frame = cb_luma_global_settings.FrameIndex;
         }
         if (gd.mv_reactive_target_rtv)
         {
            const FLOAT zero[4] = {};
            native_device_context->ClearRenderTargetView(gd.mv_reactive_target_rtv.get(), zero);
         }
         // Last frame's camera and objects are the previous ones, unless frames without a scene (menus, videos) came between
         const bool previous_valid = gd.mv_camera && cb_luma_global_settings.FrameIndex - gd.mv_frame_index <= 1;
         gd.mv_previous_camera = (previous_valid ? gd.mv_camera : nullptr);
         gd.mv_camera = nullptr;
         gd.mv_frame_index = cb_luma_global_settings.FrameIndex;
         gd.mv_objects.EndFrame(previous_valid);
      }

      // The game's vc4 (object, camera and bones in one). The slots added past it stay bound after the draw: no translated shader reads
      // a constant buffer past b4.
      ID3D11Buffer* const current = gd.bound_states.ObjectBuffer(native_device_context);
      // The buffer's CPU copy (null until its first upload); the lookup registers it for a copy at every upload
      const UINT read_size = gd.mv_last_vertex_shader.read_size;
      Borderlands2GameDeviceData::ConstantsCopy constants;
      if (current)
      {
         const std::lock_guard lock(gd.mv_constants_mutex);
         constants = gd.mv_constants.Snapshot(&gd.mv_constants.Register(current));
      }
      // The previous frame's vc4: the same object's from last frame, else this draw's with last frame's camera (no object motion).
      // None (no CPU copy yet, another camera): the current one (zero motion).
      const std::vector<uint8_t>* upload = nullptr;
      if (constants && constants->size() >= kTranslationOffset + 16)
      {
         // The frame's camera: its first motion vector draw's
         if (!gd.mv_camera)
         {
            gd.mv_camera = constants;
         }
         const bool frame_camera = std::memcmp(constants->data() + kViewProjectionOffset, gd.mv_camera->data() + kViewProjectionOffset, kCameraSize) == 0;

         // Draw key: same mesh and shaders, no instance count, with the buffer offsets
         UINT vertex_offset, index_offset;
         ID3D11Buffer* const vertex_buffer = gd.bound_states.VertexBuffer(native_device_context, &vertex_offset);
         ID3D11Buffer* const index_buffer = gd.bound_states.IndexBuffer(native_device_context, &index_offset);
         const DrawDispatchData& draw_data = last_draw_dispatch_data;
         uint64_t key = 0;
         // On x86 the pointers zero extend like ReShade's handles
         for (const uint64_t value : {uint64_t(original_shader_hashes.vertex_shaders[0]), uint64_t(original_shader_hashes.pixel_shaders[0]), uint64_t(reinterpret_cast<uintptr_t>(vertex_buffer)), uint64_t(vertex_offset),
                 uint64_t(reinterpret_cast<uintptr_t>(index_buffer)), uint64_t(index_offset), uint64_t(draw_data.index_count), uint64_t(draw_data.first_index), uint64_t(uint32_t(draw_data.vertex_offset)),
                 uint64_t(draw_data.vertex_count), uint64_t(draw_data.first_vertex)})
         {
            HashCombine(key, value);
         }
         // LocalToWorld (its translation in world space: it includes the draw's PreViewTranslation) separates objects that share a key
         // (props), as a tie-break only. The axes too: modular pieces share a pivot and differ only by rotation (BL2's arches and
         // discs, measured). Skinned meshes' c9 is a bone row, the same for copies of a model (ME1's holstered weapons).
         const size_t translation_offset = (constants->size() >= gd.mv_last_vertex_shader.translation_offset + 16 ? gd.mv_last_vertex_shader.translation_offset : kTranslationOffset);
         PatchedDraws::ObjectTransform transform = PatchedDraws::ReadRowVectorTransform(constants->data() + translation_offset - 3 * 16);
         const std::array<float, 3> view_translation = GetPreViewTranslation(*constants);
         for (int i = 0; i < 3; i++)
         {
            transform[9 + i] -= view_translation[i];
         }

         // The same object at the same transform by its draw order or the exact index, else a linear search among the key's candidates
         const Borderlands2GameDeviceData::MotionVectorObject* match = gd.mv_objects.FindNearest(key, transform, [&](const auto& candidate)
            { return candidate.constants->size() == constants->size(); });
         // "source" (only what the shader reads, at least the camera) with the view projection of "previous_camera" for this frame's
         // PreViewTranslation
         const auto upload_moved_camera = [&](const std::vector<uint8_t>& source, const std::vector<uint8_t>& previous_camera)
         {
            const size_t copy_size = (read_size != 0 ? std::clamp<size_t>(read_size, kViewProjectionOffset + kCameraSize, source.size()) : source.size());
            gd.mv_camera_only_copy.assign(source.begin(), source.begin() + copy_size);
            const std::array<double, 16> previous_view_projection = GetPreviousViewProjection(previous_camera, *constants);
            float* const view_projection = reinterpret_cast<float*>(gd.mv_camera_only_copy.data() + kViewProjectionOffset);
            for (int i = 0; i < 16; i++)
            {
               view_projection[i] = float(previous_view_projection[i]);
            }
            upload = &gd.mv_camera_only_copy;
         };
         if (match)
         {
            // Camera relative vertices are already translated by this frame's PreViewTranslation: last frame's constants, with its
            // view projection moved to it.
            if (gd.mv_last_vertex_shader.camera_relative_vertices)
            {
               upload_moved_camera(*match->constants, *match->constants);
            }
            else
            {
               upload = &*match->constants;
            }
#if DEVELOPMENT
            gd.mv_stats.matched++;
#endif
         }
         else if (frame_camera && gd.mv_previous_camera)
         {
            // Not found, drawn with the frame's camera: its own constants with last frame's view projection (camera motion only)
            upload_moved_camera(*constants, *gd.mv_previous_camera);
#if DEVELOPMENT
            gd.mv_stats.camera_only++;
#endif
         }
#if DEVELOPMENT
         else
         {
            gd.mv_stats.other_camera++;
         }
#endif
         gd.mv_objects.Push(key, {.transform = transform, .constants = std::move(constants)});
      }
#if DEVELOPMENT
      else
      {
         gd.mv_stats.uncopied++;
      }
#endif
      ID3D11Buffer* const previous_current[] = {current};
      gd.mv_previous_constants.Bind(native_device, native_device_context, MotionVectorPatches::PREVIOUS_SLOTS, {&upload, 1}, previous_current, "BL2", {&read_size, 1});
      ID3D11Buffer* const jitter = gd.mv_jitter_buffer.get();
      native_device_context->VSSetConstantBuffers(MotionVectorPatches::JITTER_SLOT, 1, &jitter);
      // Left bound after the draw: the game's next draws either bind their own targets and shaders, or are motion vector draws too.
      // The game's pixel shaders write o0 only, so the target keeps its contents.
      if (rtvs[MotionVectorPatches::TARGET_SLOT] != gd.mv_rtv)
      {
         ID3D11RenderTargetView* targets[MotionVectorPatches::TARGET_SLOT + 1] = {rtvs[0].get()};
         targets[MotionVectorPatches::TARGET_SLOT] = gd.mv_rtv.get();
         native_device_context->OMSetRenderTargets(MotionVectorPatches::TARGET_SLOT + 1, targets, dsv);
      }
      PatchedDraws::BindPatchedShader(native_device_context, vertex_shader, &gd.mv_bound_vertex_shader);
      PatchedDraws::BindPatchedShader(native_device_context, pixel_shader, &gd.mv_bound_pixel_shader);

      draw();
#if DEVELOPMENT
      gd.mv_stats.motion_vector_draws++;
#endif
      return true;
   }

   // Jitter for the scene's mesh draws without motion vectors (patched vertex shader, game pixel shader): depth passes, lights, decals,
   // translucents. Every draw depth tested against the scene takes the same jitter, or jittered and unjittered depths of the same
   // surface fail each other's test. False if it can't (the draw runs untouched).
   static bool DrawWithJitter(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data,
      const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, const std::function<void()>& draw, const com_ptr<ID3D11RenderTargetView> (&rtvs)[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT], ID3D11DepthStencilView* dsv)
   {
      auto& gd = GetGameDeviceData(device_data);
      if (!gd.mv_scene_open || gd.mv_jitter == std::array<float, 2>{} || !gd.mv_jitter_buffer)
         return false;
      // Meshes only (full screen passes have no vertex buffer or no depth test), into the scene depth (not shadows)
      if (!dsv)
         return false;
      if (dsv != gd.jitter_dsv)
      {
         com_ptr<ID3D11Resource> depth;
         dsv->GetResource(&depth);
         gd.jitter_dsv = dsv;
         gd.jitter_dsv_scene = depth == gd.mv_depth;
      }
      if (!gd.jitter_dsv_scene || !gd.bound_states.DepthTested(native_device_context))
         return false;
      const ID3D11Buffer* const vertex_buffer = gd.bound_states.VertexBuffer(native_device_context);
      if (!vertex_buffer)
         return false;
      ID3D11VertexShader* const vertex_shader = GetPatchedVertexShader(native_device, cmd_list_data, device_data, original_shader_hashes.vertex_shaders[0]).shader.get();
      if (!vertex_shader)
         return false;

      // An alpha blended draw into the scene writes its mask, reactive or transparency & composition (its pixel shader patched, the mask
      // target added past the motion vector one)
      ID3D11PixelShader* reactive_shader = nullptr;
      if (IsReactiveMaskUsed(device_data) && gd.mv_reactive_target_rtv && rtvs[0] && rtvs[0] == gd.mv_scene_rtv)
      {
         ClassifyBoundBlend(native_device_context, &gd);
         if (const uint8_t blend = gd.mv_reactive_blend; blend != 0)
         {
            reactive_shader = GetMotionVectorShader(native_device, device_data, &gd.mv_reactive_pixel_shaders[blend - 1], original_shader_hashes.pixel_shaders[0], cmd_list_data.pipeline_state_original_pixel_shader, blend).shader.get();
         }
      }

      // The patched vertex shader and the jitter stay bound after the draw (see "DrawWithMotionVectors"), with the game's pixel shader
      // (a motion vector draw's is put back) or its mask version, and the mask target
      PatchedDraws::BindPatchedShader(native_device_context, vertex_shader, &gd.mv_bound_vertex_shader);
      ID3D11Buffer* const jitter = gd.mv_jitter_buffer.get();
      native_device_context->VSSetConstantBuffers(MotionVectorPatches::JITTER_SLOT, 1, &jitter);
      if (reactive_shader)
      {
         if (rtvs[MotionVectorPatches::REACTIVE_SLOT] != gd.mv_reactive_target_rtv)
         {
            ID3D11RenderTargetView* targets[MotionVectorPatches::REACTIVE_SLOT + 1] = {};
            for (uint32_t slot = 0; slot < MotionVectorPatches::REACTIVE_SLOT; slot++)
            {
               targets[slot] = rtvs[slot].get();
            }
            targets[MotionVectorPatches::REACTIVE_SLOT] = gd.mv_reactive_target_rtv.get();
            native_device_context->OMSetRenderTargets(MotionVectorPatches::REACTIVE_SLOT + 1, targets, dsv);
         }
         PatchedDraws::BindPatchedShader(native_device_context, reactive_shader, &gd.mv_bound_pixel_shader);
#if DEVELOPMENT
         gd.mv_stats.reactive_draws++;
#endif
      }
      else
      {
         PatchedDraws::RestoreGameShader(native_device_context, &gd.mv_bound_pixel_shader);
      }
      draw();
#if DEVELOPMENT
      gd.mv_stats.jitter_draws++;
#endif
      return true;
   }

   // The upscaler on the jittered scene, its depth (from its alpha, see the fill) and the motion vectors; the result goes back
   // into the scene's color (and its copy, see "mv_scene_copy"), its alpha (the post passes' encoded depth) kept. False if it didn't draw
   // (missing input, or the upscaler failed).
   static bool DrawUpscaler(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, DeviceData& device_data, bool reactive_mask)
   {
      auto& gd = GetGameDeviceData(device_data);
      auto* const copy_vs = FindShader(device_data.native_vertex_shaders, CompileTimeStringHash("Copy VS"));
      auto* const copy_ps = FindShader(device_data.native_pixel_shaders, CompileTimeStringHash("Copy PS"));
      if (!gd.mv_texture || !gd.mv_camera || !gd.mv_device_depth || !gd.mv_scene_color || !gd.mv_scene_rtv || !copy_vs || !copy_ps)
         return false;
      com_ptr<ID3D11Texture2D> scene;
      if (FAILED(gd.mv_scene_color->QueryInterface(&scene)))
         return false;
      D3D11_TEXTURE2D_DESC scene_desc;
      scene->GetDesc(&scene_desc);
      // The overlay can switch the type to None any time: read it once, and never index the implementations with None
      const SR::Type sr_type = device_data.sr_type;
      SR::InstanceData* const sr_instance_data = (sr_type != SR::Type::None ? device_data.GetSRInstanceData() : nullptr);
      if (!sr_instance_data)
         return false;
      SR::SuperResolutionImpl* const sr_implementation = sr_implementations[sr_type].get();

      D3D11_TEXTURE2D_DESC output_desc = {};
      if (device_data.sr_output_color)
      {
         device_data.sr_output_color->GetDesc(&output_desc);
      }
      if (output_desc.Width != scene_desc.Width || output_desc.Height != scene_desc.Height)
      {
         device_data.sr_output_color.reset();
         gd.sr_output_srv.reset();
         output_desc = CD3D11_TEXTURE2D_DESC(DXGI_FORMAT_R16G16B16A16_FLOAT, scene_desc.Width, scene_desc.Height, 1, 1, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS);
         if (SUCCEEDED(SRBridge::CreateSharableTexture(native_device, output_desc, &device_data.sr_output_color)))
         {
            native_device->CreateShaderResourceView(device_data.sr_output_color.get(), nullptr, &gd.sr_output_srv);
         }
      }
      if (!device_data.sr_output_color || !gd.sr_output_srv)
      {
         // Back to SMAA until the upscaler is picked again
         device_data.sr_suppressed = true;
         return false;
      }
      if (!gd.sr_rgb_blend_state)
      {
         D3D11_BLEND_DESC blend_desc = CD3D11_BLEND_DESC(D3D11_DEFAULT);
         blend_desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN | D3D11_COLOR_WRITE_ENABLE_BLUE;
         native_device->CreateBlendState(&blend_desc, &gd.sr_rgb_blend_state);
         if (!gd.sr_rgb_blend_state)
            return false;
      }

      // FSR 4 fails to dispatch without an exposure texture (Core clears it when None is picked). It's a constant 1, as the tonemap
      // applies the scene's exposure after the upscaler, and 2D as DLSS fails with a 1D one.
      if (sr_type == SR::Type::FSR_4 && !device_data.sr_exposure)
      {
         const CD3D11_TEXTURE2D_DESC exposure_desc(DXGI_FORMAT_R32_FLOAT, 1, 1, 1, 1, D3D11_BIND_SHADER_RESOURCE, D3D11_USAGE_IMMUTABLE);
         const D3D11_SUBRESOURCE_DATA exposure_data = {.pSysMem = &device_data.sr_exposure_texture_value, .SysMemPitch = sizeof(float)};
         native_device->CreateTexture2D(&exposure_desc, &exposure_data, &device_data.sr_exposure);
      }

      // FSR needs the camera (vc4's view projection multiplies row vectors)
      const ViewProjectionCamera camera = GetViewProjectionCamera(reinterpret_cast<const float*>(gd.mv_camera->data() + kViewProjectionOffset));

      const bool is_fsr = (sr_type == SR::Type::FSR || sr_type == SR::Type::FSR_4);
      const SR::SettingsData settings_data = {
         .output_width = scene_desc.Width,
         .output_height = scene_desc.Height,
         .render_width = scene_desc.Width,
         .render_height = scene_desc.Height,
         .hdr = true,
         // The motion vectors are UV deltas, previous minus current
         .mvs_x_scale = float(scene_desc.Width),
         .mvs_y_scale = float(scene_desc.Height),
         // The scene is not exposed yet: the tonemap multiplies it by its exposure (ImageAdjustments2.w, 1.0 to 1.8) after the
         // upscaler. DLSS's auto exposure covers that, but FSR's clips highlights, so it runs at exposure 1.
         .auto_exposure = !is_fsr,
         .render_preset = dlss_render_preset,
      };
      sr_implementation->UpdateSettings(sr_instance_data, native_device_context, settings_data);

      // FSR requires a FOV (it errors on 0), and both take near and far: a camera without an up axis or a usable projection keeps the
      // last ones
      if (camera.vert_fov > 0.0)
      {
         gd.sr_vert_fov = float(camera.vert_fov);
      }
      // A finite far (depth 1) even when the projection has none (FSR's context is FFX_FSR3_ENABLE_DEPTH_INFINITE only with inverted
      // depth)
      if (camera.near_plane > 0.0)
      {
         gd.sr_near_plane = float(camera.near_plane);
         gd.sr_far_plane = float(camera.far_plane);
      }
      const SR::SuperResolutionImpl::DrawData draw_data = {
         .reset = device_data.force_reset_sr,
         .output_color = device_data.sr_output_color.get(),
         .source_color = scene.get(),
         .motion_vectors = gd.mv_texture.get(),
         .depth_buffer = gd.mv_device_depth.get(),
         // The constant 1 exposure texture FSR 4 needs
         .exposure = (sr_type == SR::Type::FSR_4 ? device_data.sr_exposure.get() : nullptr),
         // The reactive masks (FSR; DLSS ignores them)
         .bias_mask = (reactive_mask ? gd.mv_reactive.get() : nullptr),
         .transparency_alpha = (reactive_mask ? gd.mv_transparency.get() : nullptr),
         // As applied (pixels, +y down)
         .jitter_x = gd.mv_jitter[0],
         .jitter_y = gd.mv_jitter[1],
         .vert_fov = gd.sr_vert_fov,
         .near_plane = gd.sr_near_plane,
         .far_plane = gd.sr_far_plane,
      };
      if (!sr_implementation->Draw(sr_instance_data, native_device_context, draw_data))
      {
         // Back to SMAA until the upscaler is picked again
         device_data.sr_suppressed = true;
         return false;
      }
      // The RGB write mask keeps the scene's encoded depth in the alpha
      DrawCustomPixelShader(native_device_context, device_data.default_depth_stencil_state.get(), gd.sr_rgb_blend_state.get(), nullptr, copy_vs, copy_ps,
         gd.sr_output_srv.get(), gd.mv_scene_rtv.get(), scene_desc.Width, scene_desc.Height, true);
      // The copy is the game's resolve of the same scene, which the post passes read: whole, alpha included, as the scene now is
      if (gd.mv_scene_copy)
      {
         native_device_context->CopyResource(gd.mv_scene_copy.get(), scene.get());
      }
      // Not while the bridge's helper starts (the color copied as it is): SMAA stays on and the next frame resets
      device_data.has_drawn_sr = sr_implementation->IsReady(sr_instance_data);
      return true;
   }

   // Ends the scene at its first post pass: the depth and camera motion fill (see "Luma_BL2TPS_MotionVectorFill.hlsl"), then the
   // upscaler, both before that pass reads the scene
   static void EndScene(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, DeviceData& device_data)
   {
      auto& gd = GetGameDeviceData(device_data);
      gd.mv_scene_open = false;
      gd.mv_scene_done = true;
      DrawStateStack<DrawStateStackType::FullGraphics> graphics_state;
      DrawStateStack<DrawStateStackType::Compute> compute_state;
      graphics_state.Cache(native_device_context, device_data.uav_max_count);
      compute_state.Cache(native_device_context, device_data.uav_max_count);
      auto* const fill_shader = FindShader(device_data.native_compute_shaders, kMotionVectorFillName);
      if (gd.mv_scene_srv)
      {
         com_ptr<ID3D11Resource> srv_resource;
         gd.mv_scene_srv->GetResource(&srv_resource);
         if (srv_resource != gd.mv_scene_color)
         {
            gd.mv_scene_srv.reset();
         }
      }
      // The reactive and transparency & composition masks, written by the fill from what the alpha blended draws wrote
      const bool reactive = IsReactiveMaskUsed(device_data) && gd.mv_reactive_target_srv;
      if (reactive)
      {
         D3D11_TEXTURE2D_DESC desc = {};
         if (gd.mv_reactive)
         {
            gd.mv_reactive->GetDesc(&desc);
         }
         if (desc.Width != uint32_t(device_data.output_resolution.x) || desc.Height != uint32_t(device_data.output_resolution.y))
         {
            const std::unique_lock lock(gd.mv_mutex);
            gd.mv_reactive.reset();
            gd.mv_reactive_uav.reset();
            gd.mv_transparency.reset();
            gd.mv_transparency_uav.reset();
            desc = CD3D11_TEXTURE2D_DESC(DXGI_FORMAT_R8_UNORM, uint32_t(device_data.output_resolution.x), uint32_t(device_data.output_resolution.y), 1, 1, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS);
            if (SUCCEEDED(SRBridge::CreateSharableTexture(native_device, desc, &gd.mv_reactive)))
            {
               native_device->CreateUnorderedAccessView(gd.mv_reactive.get(), nullptr, &gd.mv_reactive_uav);
            }
            if (SUCCEEDED(SRBridge::CreateSharableTexture(native_device, desc, &gd.mv_transparency)))
            {
               native_device->CreateUnorderedAccessView(gd.mv_transparency.get(), nullptr, &gd.mv_transparency_uav);
            }
         }
      }
      const bool write_reactive = reactive && gd.mv_reactive_uav && gd.mv_transparency_uav;
      bool mv_filled = false; // The fill wrote this frame's motion vectors
      if (std::exchange(gd.mv_fill_pending, false) && fill_shader && gd.mv_camera && gd.mv_scene_color &&
          (gd.mv_scene_srv || SUCCEEDED(native_device->CreateShaderResourceView(gd.mv_scene_color.get(), nullptr, &gd.mv_scene_srv))))
      {
         // Current clip space to the previous frame's: previous * inverse(current) for column vectors (vc4 holds the row vector matrix,
         // transposed here), the previous one moved to this frame's PreViewTranslation, in double
         const float* const view_projection = reinterpret_cast<const float*>(gd.mv_camera->data() + kViewProjectionOffset);
         Math::Matrix44D current, previous;
         current.SetIdentity();
         previous.SetIdentity();
         if (gd.mv_previous_camera)
         {
            std::copy_n(view_projection, 16, current.GetData());
            const std::array<double, 16> previous_view_projection = GetPreviousViewProjection(*gd.mv_previous_camera, *gd.mv_camera);
            std::copy_n(previous_view_projection.data(), 16, previous.GetData());
            current.Transpose();
            previous.Transpose();
            current.Invert();
         }
         const Math::Matrix44D reprojection = previous * current;
         const ViewProjectionCamera camera = GetViewProjectionCamera(view_projection);
         float constants[24] = {}; // A multiple of 16 bytes
         for (int i = 0; i < 16; i++)
         {
            constants[i] = float(reprojection.GetData()[i]);
         }
         constants[16] = gd.mv_jitter_ndc[0];
         constants[17] = gd.mv_jitter_ndc[1];
         constants[18] = float(camera.depth_a);
         constants[19] = float(camera.depth_b);
         constants[20] = kReactiveScale;
         constants[21] = kReactiveThreshold;
         constants[22] = (write_reactive ? 1.f : 0.f);
         if (PatchedDraws::WriteDynamicConstants(native_device, native_device_context, std::addressof(gd.mv_fill_buffer), constants, sizeof(constants)))
         {
            // The scene and the motion vectors may be bound as render targets
            native_device_context->OMSetRenderTargets(0, nullptr, nullptr);
            ID3D11Buffer* const buffer = gd.mv_fill_buffer.get();
            ID3D11ShaderResourceView* const srvs[2] = {gd.mv_scene_srv.get(), (write_reactive ? gd.mv_reactive_target_srv.get() : nullptr)};
            ID3D11UnorderedAccessView* const uavs[4] = {gd.mv_uav.get(), gd.mv_device_depth_uav.get(), (write_reactive ? gd.mv_reactive_uav.get() : nullptr), (write_reactive ? gd.mv_transparency_uav.get() : nullptr)};
            native_device_context->CSSetConstantBuffers(0, 1, &buffer);
            native_device_context->CSSetShaderResources(0, UINT(std::size(srvs)), srvs);
            native_device_context->CSSetUnorderedAccessViews(0, UINT(std::size(uavs)), uavs, nullptr);
            native_device_context->CSSetShader(fill_shader, nullptr, 0);
            native_device_context->Dispatch((uint32_t(device_data.output_resolution.x) + 7) / 8, (uint32_t(device_data.output_resolution.y) + 7) / 8, 1);
            ID3D11UnorderedAccessView* const null_uavs[std::size(uavs)] = {};
            ID3D11ShaderResourceView* const null_srvs[std::size(srvs)] = {};
            native_device_context->CSSetUnorderedAccessViews(0, UINT(std::size(null_uavs)), null_uavs, nullptr);
            native_device_context->CSSetShaderResources(0, UINT(std::size(null_srvs)), null_srvs);
            mv_filled = true;
            if (write_reactive)
            {
               gd.sr_reactive_frame = cb_luma_global_settings.FrameIndex;
            }
         }
      }
      // The upscaler's depth comes from the fill
      if (IsSRActive(device_data) && mv_filled)
      {
         [[maybe_unused]] const bool drawn = DrawUpscaler(native_device, native_device_context, device_data, write_reactive);
#if DEVELOPMENT
         gd.mv_stats.sr_draws += drawn;
#endif
      }
      compute_state.Restore(native_device_context);
      graphics_state.Restore(native_device_context);
   }

public:
   static void UnregisterEvents()
   {
      reshade::unregister_event<reshade::addon_event::map_buffer_region>(OnMapBufferRegion);
      reshade::unregister_event<reshade::addon_event::unmap_buffer_region>(OnUnmapBufferRegion);
      reshade::unregister_event<reshade::addon_event::update_buffer_region>(OnUpdateBufferRegion);
      reshade::unregister_event<reshade::addon_event::create_pipeline>(PatchedDraws::OnCreateBlendState<MotionVectorPatches::TARGET_SLOT, MotionVectorPatches::REACTIVE_SLOT>);
      reshade::unregister_event<reshade::addon_event::bind_pipeline>(PatchedDraws::OnBindPipeline<GetBoundStates>);
      reshade::unregister_event<reshade::addon_event::push_descriptors>(PatchedDraws::OnPushConstantBuffers<GetBoundStates>);
      reshade::unregister_event<reshade::addon_event::bind_vertex_buffers>(PatchedDraws::OnBindVertexBuffers<GetBoundStates>);
      reshade::unregister_event<reshade::addon_event::bind_index_buffer>(PatchedDraws::OnBindIndexBuffer<GetBoundStates>);
   }

   void OnInit(bool async) override
   {
      // ReShade can init the addon more than once per load of this dll, and nothing here may run twice: "register_event" doesn't dedupe (a
      // second "OnMapBufferRegion()" would take its own redirected pointer for the buffer's, which never gets its data) and the game's
      // shader defines would be added again
      static bool initialized = false;
      if (std::exchange(initialized, true))
         return;

#if DEVELOPMENT
      // For the MCP "luma_dev_values" tool
      Mcp::RegisterToggles({{"smaa_enable", &g_smaa_enable}, {"smaa_predication", &g_smaa_predication}, {"smaa_pred_debug", &g_smaa_pred_debug}, {"smaa_pred_measure", &g_smaa_pred_measure}, { "hide_ui",
                               &g_hide_ui }});
      Mcp::RegisterMirroredToggle("luma_bloom_enable", &g_luma_bloom_enable, &cb_luma_global_settings.GameSettings.LumaBloomEnable);
      Mcp::RegisterMirroredToggle("video_auto_hdr_enable", &g_video_auto_hdr_enable, &cb_luma_global_settings.GameSettings.VideoAutoHDREnable);
      Mcp::RegisterValues({{"rcas_sharpness", &g_rcas_sharpness, 0.f, 1.f}, {"smaa_pred_tolerance", &g_smaa_pred_tolerance, 0.002f, 0.2f}, {"bloom_intensity", &g_bloom_intensity, 0.f, 2.f},
         {"sr_reactive_scale", &kReactiveScale, 0.f, 4.f}, {"sr_reactive_threshold", &kReactiveThreshold, 0.f, 1.f}});
      Mcp::RegisterInts({{ "bloom_nmips",
         &g_bloom_nmips,
         1,
         int(std::size(g_bloom_sigmas)) }});
      Mcp::RegisterTextures({MCP_GAME_TEXTURE("mv.velocity", mv_texture), MCP_GAME_TEXTURE("mv.depth", mv_device_depth), MCP_GAME_TEXTURE("sr.reactive", mv_reactive),
         MCP_GAME_TEXTURE("sr.transparency", mv_transparency), MCP_GAME_TEXTURE("sr.draws_mask", mv_reactive_target), MCP_GAME_TEXTURE("mv.scene", mv_scene_color),
         MCP_GAME_TEXTURE("mv.scene_copy", mv_scene_copy), MCP_GAME_TEXTURE("smaa.input", tex_input_encoded), MCP_GAME_TEXTURE("smaa.input_linear", tex_input_linear),
         MCP_GAME_TEXTURE("smaa.pred_mask", tex_pred), MCP_GAME_TEXTURE("smaa.output", tex_smaa_out)});
#endif
      // Game-specific toggle consumed by the tonemap replacement (Luma_BL2TPS_Tonemap.hlsl).
      std::vector<ShaderDefineData> game_shader_defines_data = {
         {"TONEMAP_TYPE", '1', true, false, "0 - SDR: Vanilla grade (reference)\n1 - HDR: native grade + reconstructed luminance + C-infinity rolloff", 1},
      };
      shader_defines_data.append_range(game_shader_defines_data);
      assert(shader_defines_data.size() < MAX_SHADER_DEFINES);

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

      // Core auto-registers the 6 SMAA passes and the RCAS sharpen PS; these two are this game's own.
      // Depth-extract CS for SMAA predication: scene-color .a (linear view Z) -> R16F plane-deviation edge-ness.
      native_shaders_definitions.emplace(CompileTimeStringHash("BL2TPS Depth Extract CS"),
         ShaderDefinition("Luma_BL2TPS_DepthExtract", reshade::api::pipeline_subobject_type::compute_shader));
      // Linearize CS for SMAA's neighborhood blend: gamma 2.2 LDR snapshot -> linear-light fp16 copy.
      native_shaders_definitions.emplace(CompileTimeStringHash("BL2TPS SMAA Linearize CS"),
         ShaderDefinition("Luma_BL2TPS_SMAALinearize", reshade::api::pipeline_subobject_type::compute_shader));

      // DLSS/FSR/XeSS: their depth and the camera motion from the scene's alpha, the CPU copies of vc4 (dgVoodoo maps it or updates it),
      // and the motion vector target written by every blend state. Registered once: the buffer hooks are cheap while no upscaler runs
      // (the Map and UpdateSubresource ones return on "sr_active"), and the game's calls can come from any thread.
      sr_game_tooltip = "DLSS, FSR and XeSS run in Luma-Upscaler.exe, which has to be next to the game's exe.\n";
      native_shaders_definitions.emplace(kMotionVectorFillName,
         ShaderDefinition("Luma_BL2TPS_MotionVectorFill", reshade::api::pipeline_subobject_type::compute_shader));
      reshade::register_event<reshade::addon_event::map_buffer_region>(OnMapBufferRegion);
      reshade::register_event<reshade::addon_event::unmap_buffer_region>(OnUnmapBufferRegion);
      reshade::register_event<reshade::addon_event::update_buffer_region>(OnUpdateBufferRegion);
      reshade::register_event<reshade::addon_event::create_pipeline>(PatchedDraws::OnCreateBlendState<MotionVectorPatches::TARGET_SLOT, MotionVectorPatches::REACTIVE_SLOT>);
      reshade::register_event<reshade::addon_event::bind_pipeline>(PatchedDraws::OnBindPipeline<GetBoundStates>);
      reshade::register_event<reshade::addon_event::push_descriptors>(PatchedDraws::OnPushConstantBuffers<GetBoundStates>);
      reshade::register_event<reshade::addon_event::bind_vertex_buffers>(PatchedDraws::OnBindVertexBuffers<GetBoundStates>);
      reshade::register_event<reshade::addon_event::bind_index_buffer>(PatchedDraws::OnBindIndexBuffer<GetBoundStates>);

      // The game's post passes use cb0..cb5 and no translated shader reads one past b4, so b9/b10 (the motion vector draws, see
      // "MotionVectorPatches"), b11 (core DrawBloom's own constants) and b12/b13 are free for Luma.
      luma_settings_cbuffer_index = 13;
      luma_data_cbuffer_index = 12;

      // User settings mirrored into LumaSettings.GameSettings. The grade sliders default to a vanilla no-op;
      // Exposure/Bloom/Vignette act on both SDR+HDR, Saturation/Dechroma/Contrast HDR-only; those three and Dithering
      // need TONEMAP_TYPE 1.
      default_luma_global_game_settings.Exposure = 1.f;           // scene multiplier (1x)
      default_luma_global_game_settings.Saturation = 1.f;         // BT.709-luminance lerp (Color.hlsl Saturation)
      default_luma_global_game_settings.HighlightDechroma = 0.f;  // off; the native grade keeps its own highlight whitening
      default_luma_global_game_settings.BloomIntensity = 1.f;     // seed only; OnDrawOrDispatch owns the effective value
      default_luma_global_game_settings.Contrast = 1.f;           // a power of relative luminance around mid gray, before the display map
      default_luma_global_game_settings.VignetteIntensity = 1.f;  // game vignette darkening scale
      default_luma_global_game_settings.LumaBloomEnable = 1.f;    // 1 = Luma HDR pyramidal bloom, 0 = vanilla game bloom
      default_luma_global_game_settings.Dithering = 1.f;          // animated triangular dither at output (HDR and SDR), anti-banding on
      default_luma_global_game_settings.VideoAutoHDREnable = 1.f; // light AutoHDR on Bink videos (HDR only)
      default_luma_global_game_settings.VideoAutoHDRBoost = 0.5f; // highlight-expansion strength (peak ~165 nits at 0.5)
      default_luma_global_game_settings.BloomThreshold = 1.f;     // replaced once the native bright pass is read back
      cb_luma_global_settings.GameSettings = default_luma_global_game_settings;
   }

   void OnCreateDevice(ID3D11Device* native_device, DeviceData& device_data) override
   {
      device_data.game = new Borderlands2GameDeviceData;
#if DEVELOPMENT
      // For the MCP "luma_dev_values" tool: the last complete frame's counts. Plain reads of values the render thread (and the buffer
      // hooks under their lock) write: a torn value is acceptable for a diagnostic.
      const auto& stats = GetGameDeviceData(device_data).mv_last_stats;
      Mcp::RegisterCounters({{"mv.draws", &stats.motion_vector_draws}, {"mv.jitter_draws", &stats.jitter_draws}, {"mv.matched", &stats.matched}, {"mv.camera_only", &stats.camera_only},
                               {"mv.other_camera", &stats.other_camera}, {"mv.uncopied", &stats.uncopied}, {"mv.maps", &stats.maps}, {"mv.updates", &stats.updates},
                               {"mv.sr_draws", &stats.sr_draws}, {"mv.reactive_draws", &stats.reactive_draws}, { "mv.ended_by_hash",
                                  &stats.ended_by }},
         &device_data);
      for (size_t i = 0; i < std::size(kMotionVectorRejectNames); i++)
      {
         Mcp::RegisterCounter(std::string("mv.rejected.") + kMotionVectorRejectNames[i], &stats.rejected[i], &device_data);
      }
#endif
   }

   // None picked, from the overlay (after present latched "sr_active"): released at the next present, see "OnPresent"
   void CleanExtraSRResources(DeviceData& device_data) override
   {
      GetGameDeviceData(device_data).release_sr_resources = true;
   }

   void OnDestroyDeviceData(DeviceData& device_data) override
   {
#if DEVELOPMENT
      Mcp::Unregister(&device_data);
#endif
      // GameDeviceData lacks a virtual destructor; delete through the concrete type to release derived members.
      delete static_cast<Borderlands2GameDeviceData*>(device_data.game);
      device_data.game = nullptr;
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

   // One-shot readback of the predication mask, so g_smaa_pred_tolerance is calibrated from numbers rather than
   // from screenshots. A mean would be useless here: on a well-tuned frame the mask is 0 nearly everywhere and any
   // average drowns in that, so this reports COVERAGE at the level SMAA actually compares against (0.5) plus the
   // shape either side of it. Every texel is read, not a stride - a 1px silhouette is precisely the signal a
   // subsample would step over. Copies on the frame the button is pressed and maps on a later one (non-blocking),
   // like the bloom A/B below, so the press never stalls the render thread.
   static void LogPredicationStats(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, Borderlands2GameDeviceData& gd)
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
      const uint64_t total = (uint64_t)capture.width * capture.height;
      uint64_t non_finite = 0;
      for (UINT y = 0; y < capture.height; y++)
      {
         const uint16_t* row = (const uint16_t*)((const uint8_t*)mapped.pData + (size_t)y * mapped.RowPitch);
         for (UINT x = 0; x < capture.width; x++)
         {
            const float v = DirectX::PackedVector::XMConvertHalfToFloat(row[x]);
            // The range test, not a clamp: a NaN fails BOTH ordered comparisons, so the clamped form would fall
            // through to (uint32_t)NaN - 0x80000000 on x86 - and index far outside this stack array. The extract
            // CS saturates, so a NaN here means it met one upstream (ME2 measured inf - inf in the scene alpha on
            // the same shader family); count them rather than bin them, a silent 0 would read as a flat surface.
            if (v >= 0.f && v <= 1.f)
               histogram[(uint32_t)(v * (float)(kBins - 1))]++;
            else
               non_finite++;
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
         "[BL2-Pred] tol=%.4f | FIRES(>0.5)=%.3f%% | >0.1=%.3f%% >0.25=%.3f%% >0.75=%.3f%% >0.9=%.3f%% | flat(bin0)=%.2f%% | p50=%.3f p90=%.3f p99=%.3f p999=%.3f | nonfinite=%llu | %ux%u",
         g_smaa_pred_tolerance, fraction_above(0.5f), fraction_above(0.1f), fraction_above(0.25f), fraction_above(0.75f), fraction_above(0.9f),
         100.0 * (double)histogram[0] / (double)total, percentile(0.5), percentile(0.9), percentile(0.99), percentile(0.999),
         (unsigned long long)non_finite, capture.width, capture.height);
      LogGradeLine(line);
   }

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
   // DOFKernelSize, vignette). Raw values only; the curve itself lives in the shader, which continues it for HDR
   // (BL2TPS_TryBuildWorkingLuminance). cb4[16].rgb is the per-area bloom tint the bloom A/B mirrors.
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

      // The HDR working continuation models the K = 0 curve only (BL2TPS_TryBuildWorkingLuminance in
      // Luma_BL2TPS_Tonemap.hlsl); on anything else it declines and the native graded colour is presented as-is.
      // K has never been observed non-zero, so say so loudly, once, if it ever is. Ahead of the de-dup below on
      // purpose: A and Z follow W, so exposure drift alone keeps adding sets until its 64-set cap returns early.
      if (ia3[0] != 0.f && !gd.grade_k_warned)
      {
         gd.grade_k_warned = true;
         LogGradeLine(std::format("[BL-HDR] WARNING: non-zero ImageAdjustments K={:.5f}; the HDR working continuation does not "
                                  "model this curve, so HDR falls back to the native graded range for it.",
            ia3[0]));
      }

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

      // DLSS/FSR/XeSS: the scene's draws with motion vectors or jitter (see "DrawWithMotionVectors"), and the upscaler at its first post pass
      if (gd.sr_active && is_immediate)
      {
         const uint32_t pixel_shader_hash = uint32_t(original_shader_hashes.pixel_shaders[0]);
         if (!gd.mv_scene_done && kScenePostHashes.Contains(pixel_shader_hash, reshade::api::shader_stage::pixel))
         {
            if (gd.mv_scene_open)
            {
#if DEVELOPMENT
               gd.mv_stats.ended_by = pixel_shader_hash;
#endif
               // A copy of the scene (same description, CopyResource compatible) among the pass's textures
               gd.mv_scene_copy.reset();
               if (gd.mv_scene_color)
               {
                  com_ptr<ID3D11ShaderResourceView> srvs[8];
                  native_device_context->PSGetShaderResources(0, 8, &srvs[0]);
                  for (int slot = 0; slot < 8 && !gd.mv_scene_copy; slot++)
                  {
                     com_ptr<ID3D11Resource> resource;
                     if (srvs[slot])
                     {
                        srvs[slot]->GetResource(&resource);
                     }
                     if (!resource || resource == gd.mv_scene_color || !AreResourcesEqual(resource.get(), gd.mv_scene_color.get()))
                        continue;
                     gd.mv_scene_copy = resource;
                  }
               }
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
            if (DrawWithMotionVectors(native_device, native_device_context, cmd_list_data, device_data, original_shader_hashes, *original_draw_dispatch_func, rtvs, dsv.get()) ||
                DrawWithJitter(native_device, native_device_context, cmd_list_data, device_data, original_shader_hashes, *original_draw_dispatch_func, rtvs, dsv.get()))
               return DrawOrDispatchOverrideType::Replaced;
         }
      }
      // Every draw without a patched shader: the game's own, if the last patched draw's are still bound
      if (is_immediate)
      {
         PatchedDraws::RestoreGameShader(native_device_context, &gd.mv_bound_vertex_shader);
         PatchedDraws::RestoreGameShader(native_device_context, &gd.mv_bound_pixel_shader);
      }

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

      // The tonemap draw: track its LDR target (Hide UI, FXAA override), capture the scene SRV, build the Luma bloom, then SMAA.
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
         // Capture the scene-color SRV (PS t0) for SMAA predication (its .a holds linear view-space depth) and for the
         // FXAA override, which recognizes BL2's resolve by it. The tonemap reads (doesn't overwrite) this buffer, so .a
         // is still valid when SMAA runs later this frame.
         ComPtr<ID3D11ShaderResourceView> scene_srv;
         native_device_context->PSGetShaderResources(0, 1, scene_srv.put());
         if (scene_srv)
            gd.srv_scene_depth = scene_srv;

#if ENABLE_BLOOM
         // Pyramidal bloom from the fp16 scene (tonemap t0), bound at PS t5 (BL2) / t8 (TPS); it ignores native bloom
         // t1, so no doubling. The graphics state stack restores the tonemap's RT/PS/SRVs afterwards.
         if (g_luma_bloom_enable && scene_srv)
         {
            auto& gs = cb_luma_global_settings.GameSettings;

            // THE SOLE WRITER of the effective BloomIntensity - the UI only touches the raw slider. Only the Luma
            // composite reads it (the game's own bloom is never scaled), so it is written only where the pyramid runs.
            const float effective_intensity = g_bloom_intensity * kBloomPyramidToNativeEnergy;
            if (fabsf(gs.BloomIntensity - effective_intensity) > 1e-6f)
            {
               gs.BloomIntensity = effective_intensity;
               device_data.cb_luma_global_settings_dirty = true;
            }

            // Follow the area's authored knee: the prefilter reads GameSettings.BloomThreshold, kept live off the
            // native bright pass. 1.0 until the first readback, mid-range for this game (measured 0.50 to 1.32).
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
            LogBloomEnergy(native_device, native_device_context, gd, is_tps, gs.BloomThreshold);
#endif
         }
#endif

#if ENABLE_SMAA
         // Run the original tonemap draw ourselves, then SMAA on its LDR output; a normal draw without the callback. On a frame the
         // upscaler already antialiased only RCAS runs.
         const bool antialias = (device_data.has_drawn_sr ? g_rcas_sharpness > 0.f : g_smaa_enable);
         if (antialias && original_draw_dispatch_func != nullptr)
         {
            // Returning Replaced short-circuits core's per-pass SetLumaConstantBuffers, so upload the grade CB
            // here or the draw samples last-present's values (one frame of lag on the sliders).
            SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, reshade::api::shader_stage::pixel, LumaConstantBufferType::LumaSettings);
            updated_cbuffers = true;
            (*original_draw_dispatch_func)();
            if (rtv)
            {
               RunPostTonemapSMAA(native_device, native_device_context, device_data, gd, rtv.get(), !device_data.has_drawn_sr);
            }
            return DrawOrDispatchOverrideType::Replaced; // we ran the original draw ourselves
         }
#endif
      }

#if ENABLE_SMAA
      // Cancel the native FXAA resolve while SMAA or the upscaler is on, keyed on the resolve's own SOURCE, so nothing here has to know
      // which game it is in. Reading the scene the tonemap samples at t0 is BL2's shape (devkit at 4K: it draws before
      // the tonemap, 623 vs 642, and nothing samples its output), so the draw is dropped. Reading the tonemap's output
      // is TPS's: it routes the antialiased frame to the buffer the HUD draws onto, so it is copied. Anything else (no
      // tonemap captured yet, an unhooked uber permutation, a recreated target, a size or format mismatch) keeps the
      // game's own draw: a second AA pass is harmless, a buffer left unwritten under the HUD is not.
      if ((g_smaa_enable || IsSRActive(device_data)) && is_immediate && !is_custom_pass && IsFXAA(original_shader_hashes))
      {
         ComPtr<ID3D11ShaderResourceView> fxaa_srv;
         native_device_context->PSGetShaderResources(0, 1, fxaa_srv.put());
         ComPtr<ID3D11Resource> src_res;
         if (fxaa_srv)
            fxaa_srv->GetResource(src_res.put());
         if (src_res)
         {
            ComPtr<ID3D11Resource> scene_res;
            if (gd.srv_scene_depth)
               gd.srv_scene_depth->GetResource(scene_res.put());
            if (src_res.get() == scene_res.get())
               return DrawOrDispatchOverrideType::Replaced; // BL2: upstream of the tonemap, its output feeds nothing

            if ((uint64_t)src_res.get() == gd.ldr_buffer_handle)
            {
               ComPtr<ID3D11RenderTargetView> fxaa_rtv;
               native_device_context->OMGetRenderTargets(1, fxaa_rtv.put(), nullptr);
               ComPtr<ID3D11Resource> dst_res;
               if (fxaa_rtv)
                  fxaa_rtv->GetResource(dst_res.put());
               // CopyResource silently no-ops on a mismatch, which would leave a stale buffer under the HUD.
               if (dst_res && dst_res.get() != src_res.get() && AreResourcesEqual(dst_res.get(), src_res.get()))
               {
                  native_device_context->CopyResource(dst_res.get(), src_res.get());
                  return DrawOrDispatchOverrideType::Replaced;
               }
            }
         }
      }
#endif

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

      // SR: the history restarts after any frame the upscaler didn't draw (menus, loading, just picked); the selection and the motion
      // vector state are fixed here for the next frame (see "IsSRActive"). Core clears neither of these flags.
      gd.mv_jitter_allowed = device_data.has_drawn_sr;
      device_data.force_reset_sr = !device_data.has_drawn_sr;
      device_data.has_drawn_sr = false;
      const bool sr_active = device_data.sr_type != SR::Type::None && !device_data.sr_suppressed;
      if (sr_active && !gd.sr_active)
      {
         // The hooks missed the uploads while idle, so every earlier copy is stale: draws have none (zero motion) until their buffer's next upload
         gd.mv_constants.Restart();
      }
      gd.sr_active = sr_active;
      // None was picked (Core already cleared the output): the upscaler's inputs and output go too, as the SR bridge's helper stopped.
      // Recreated when an upscaler is picked again (the helper takes seconds to start). Picked again before this present: nothing to release.
      if (gd.release_sr_resources.exchange(false) && !sr_active)
      {
         gd.sr_output_srv.reset();
         {
            const std::unique_lock lock(gd.mv_mutex);
            gd.ReleaseMotionVectorTargets();
            // The game's scene, depth and scene copy (taken again at the next scene), so a resize after None doesn't keep the old ones
            // alive; and the vc4 copies the object tables and the cameras hold
            gd.mv_depth.reset();
            gd.mv_scene_color.reset();
            gd.mv_scene_rtv.reset();
            gd.mv_scene_srv.reset();
            gd.mv_scene_copy.reset();
            gd.mv_objects.Reset();
            gd.mv_camera.reset();
            gd.mv_previous_camera.reset();
            gd.mv_fill_buffer.reset();
            gd.mv_jitter_buffer.reset();
            gd.mv_previous_constants = {}; // Its 4 MiB dynamic ring is address space too
         }
         const std::lock_guard lock(gd.mv_constants_mutex);
         gd.mv_constants.ReleasePool();
      }
      {
         const std::lock_guard lock(gd.mv_constants_mutex);
         gd.mv_constants.TrimPool(sr_active);
#if DEVELOPMENT
         // Under the hooks' lock, as they count into it
         gd.mv_last_stats = gd.mv_stats;
         gd.mv_stats = {};
#endif
      }
      // FSR's masks go once the fill stopped writing them (DLSS, XeSS, SMAA)
      if (gd.mv_reactive_target && cb_luma_global_settings.FrameIndex - gd.sr_reactive_frame > kReactiveMasksIdleFrames)
      {
         const std::unique_lock lock(gd.mv_mutex);
         gd.ReleaseReactiveMasks();
      }
      // A scene no post pass ended ends here: its jitter must not reach the next frame's draws before its first mesh
      gd.mv_scene_open = false;
      gd.mv_scene_done = false;
      gd.mv_frame_ended = true;
      gd.mv_fill_pending = false;
      // -1 at native resolution, 0 with XeSS (the game's anisotropic samplers are all upgraded to AF16x)
      if (!custom_texture_mip_lod_bias_offset)
      {
         std::shared_lock shared_lock_samplers(s_mutex_samplers);
         device_data.texture_mip_lod_bias_offset = (gd.sr_active ? SR::GetMipLODBias(device_data.sr_type, device_data.output_resolution.y, device_data.output_resolution.y) : 0.f);
      }
   }

   void LoadConfigs() override
   {
      reshade::get_config_value(nullptr, NAME, "SMAAEnable", g_smaa_enable);
      reshade::get_config_value(nullptr, NAME, "RCASSharpness", g_rcas_sharpness);
      // Predication has no shipping UI (see DrawImGuiSettings), but stays overridable from the ini.
      reshade::get_config_value(nullptr, NAME, "SMAAPredication", g_smaa_predication);
      reshade::get_config_value(nullptr, NAME, "SMAAPredicationTolerance", g_smaa_pred_tolerance);
      reshade::get_config_value(nullptr, NAME, "HideUI", g_hide_ui);

      // Grade sliders (cb_luma_global_settings_dirty is already true at init -> uploaded on first frame).
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
         ImGui::SetTooltip("Replaces the game's FXAA with SMAA (works with the game's Anti-aliasing setting on or off).");
      ImGui::BeginDisabled(!g_smaa_enable);
      ImGui::SliderFloat("RCAS Sharpness", &g_rcas_sharpness, 0.f, 1.f);
      if (ImGui::IsItemDeactivatedAfterEdit())
         reshade::set_config_value(nullptr, NAME, "RCASSharpness", g_rcas_sharpness);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("Sharpening applied on top of SMAA (0 = off).");
      if (DrawResetButton<float, false>(g_rcas_sharpness, 0.f, "RCASSharpness"))
         reshade::set_config_value(nullptr, NAME, "RCASSharpness", g_rcas_sharpness);
#if DEVELOPMENT
      // Predication is not a preference: it only relaxes the edge threshold back to base ULTRA on geometry and
      // never below, so off is strictly worse. Kept as a bisect switch for devs, shipped on and out of sight.
      if (ImGui::Checkbox("SMAA Predication", &g_smaa_predication))
         reshade::set_config_value(nullptr, NAME, "SMAAPredication", g_smaa_predication);
      if (ImGui::IsItemHovered())
         ImGui::SetTooltip("Finds edges by geometry (scene depth) instead of by brightness alone.\nKeeps cel-shade texture noise from being antialiased while still catching real silhouettes.");
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
         ImGui::SetTooltip("Read the mask back and log its distribution to Luma-BL2.log (next to the exe).\nStand still, set a tolerance, press; repeat per value and compare the lines.\nFIRES(>0.5) is the share of the frame that regains base sensitivity.");
#endif
      ImGui::EndDisabled();

      // Grade sliders, read in Luma_BL2TPS_Tonemap.hlsl; every default is a vanilla no-op, and OnInit says which
      // of them act in SDR as well.
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
         ImGui::SetTooltip("How far the brightest sources fade to neutral white, HDR only (0 = keep color at any brightness).");
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

      // Scales the injected Luma bloom only. With the pyramid off the game's own bloom runs at the strength its
      // artists authored, so there is nothing here to turn - hence disabled rather than silently inert.
      ImGui::BeginDisabled(!g_luma_bloom_enable);
      if (ImGui::SliderFloat("Bloom Intensity", &g_bloom_intensity, 0.f, 2.f))
         device_data.cb_luma_global_settings_dirty = true;
      if (ImGui::IsItemDeactivatedAfterEdit())
         reshade::set_config_value(nullptr, NAME, "BloomIntensity", g_bloom_intensity);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("Bloom strength (1 = vanilla, 0 = none).");
      if (DrawResetButton<float, false>(g_bloom_intensity, 1.f, "BloomIntensity"))
      {
         device_data.cb_luma_global_settings_dirty = true;
         reshade::set_config_value(nullptr, NAME, "BloomIntensity", g_bloom_intensity);
      }
      ImGui::EndDisabled();

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

      // Luma_BL2TPS_Tonemap.hlsl dithers in HDR and SDR alike, so this checkbox has no display-mode gate.
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
         "It adds HDR, HDR bloom, SMAA anti-aliasing, DLSS/FSR/XeSS and 16x anisotropic filtering.\n"
         "It runs through dgVoodoo2 (DirectX 9 -> 11): put the x86 D3D9.dll of dgVoodoo 2.87.x next to the game's exe,\n"
         "set OutputAPI = d3d11_fl11_0 and the watermark off in dgVoodoo.conf, and start the exe directly.\n"
         "The Ultra HD texture pack can run into the 32-bit memory limit.\n"
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
                  "\nSMAA (Iryoku)"
                  "\nAMD FidelityFX (RCAS)"
                  "\ndgVoodoo2 by Dege (DirectX 9 -> 11 wrapper, required)");
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
      // risk). The LOD bias offset follows the upscaler (see "OnPresent"); 0 otherwise, a negative bias would shimmer.
      enable_samplers_upgrade = true; // boot-time only (can't change after device creation)
      samplers_upgrade_mode = 4;

      game = new Borderlands2();
   }

   else if (ul_reason_for_call == DLL_PROCESS_DETACH)
   {
      Borderlands2::UnregisterEvents();
   }

   CoreMain(hModule, ul_reason_for_call, lpReserved);

   return TRUE;
}
