#define GAME_SUNSET_OVERDRIVE 1

#define GEOMETRY_SHADER_SUPPORT 0
#define DISABLE_AUTO_DEBUGGER 1
// The velocity passes (with the motion vector target added), the bloom's split bright passes (on the pre-SR scene) and
// "PS_OcclCoverDropouts" (the reactive mask's copy point) draw through "original_draw_dispatch_func"
#define ENABLE_POST_DRAW_DISPATCH_CALLBACK 1
// Core's SMAA in place of the game's SMAA blend, 1x and T2x (see "DrawSMAAInPlaceOfBlend")
#define ENABLE_SMAA 1
#define ENABLE_RCAS 1

#include "..\..\Core\core.hpp"
// Compiled into this project (see the .vcxproj), for "InstallEngineHooks"
#include "..\..\External\reshade\deps\minhook\include\MinHook.h"
// SMAA's area texture without the U-shape smoothing (see "Luma_SMAA_impl.hlsl" and "DrawSMAAInPlaceOfBlend")
#include "SMAA_AreaTex_SharpU.h"

// Sunset Overdrive (PC, 2018) is an x64 D3D11 game on Insomniac's engine. Its engine shaders are embedded in "Sunset.exe" along with
// their name tables, and the post passes below were confirmed in game.
// The 16 "PS_ToneMap*" permutations (bloom, 3D LUT, film grain and vignette, each on or off) apply the exposure, a per-channel 1D tone
// curve, the bloom, the sRGB encoding and the LUT (see "Includes/ToneMap.hlsl").
const ShaderHashesList shader_hashes_ToneMap = {.pixel_shaders = {
                                                   0x92D51420,
                                                   0x7880CCF1,
                                                   0x86EE37B6,
                                                   0xC291251A,
                                                   0xE4EAD75E,
                                                   0x6619803F,
                                                   0x2A6A872A,
                                                   0xEE7FB6CC,
                                                   0x58E6385C,
                                                   0x633A5894,
                                                   0xD6B7231C,
                                                   0xE3E7DD84,
                                                   0x980ACE18,
                                                   0x2F20FD47,
                                                   0x902D30F9,
                                                   0xB2BBC089,
                                                }};
// "PS_CopyBuffer" is a generic copy. One of its uses draws the post-AA scene onto the swapchain, scaled to the window, before the UI.
constexpr uint32_t shader_hash_CopyBuffer = 0xF7085ACD;
// The only two velocity writers, replaced to also write the motion vectors for SR and SMAA T2x (see their shaders). The object pass
// only runs on frames with motion blur on or with a jitter stamp in view+0x120C (see "JitterDetour").
constexpr uint32_t shader_hash_CameraVelocity = 0x524BC7C4;
constexpr uint32_t shader_hash_ObjectVelocity = 0x928DFB65;
// SR runs on the scene (R11G11B10F, t5) at the post chain's entry, once every scene draw is in. That's the motion blur's color
// downsample with motion blur on, else the exposure downsample "PS_Tex". That one is a generic copy that also runs mid-forward, so we
// recognize it by its target, the t5 of the exposure CS "CS_AverageLumAccum" on the previous frame. The tonemap (scene at t6) is the
// last resort.
constexpr uint32_t shader_hash_MotionBlurDownsampleColor = 0x665173C0;
constexpr uint32_t shader_hash_Tex = 0x6CDD786E;
// "PS_OcclCoverDropouts" draws into the scene once a frame, after the fog and before the transparents. It's the reactive mask's copy
// point because it also runs with the game's Bloom off, which drops the mid-forward "PS_Tex" copy.
constexpr uint32_t shader_hash_OcclCoverDropouts = 0x7B7BCCF2;
constexpr uint32_t shader_hash_AverageLumAccum = 0x0DEE3EB1;
// The bright passes that split the bloom into an opaque part, min(t5, t6), and an alpha part, t5 - min(t5, t6). t5 is the scene's
// exposure downsample and t6 the mid-forward copy (jittered, before SR).
constexpr uint32_t shader_hash_BrightPassOpaque = 0x8EACA7B7;
constexpr uint32_t shader_hash_BrightPassAlpha = 0x6EB0D51C;
constexpr uint32_t shader_hash_CopyVS = CompileTimeStringHash("Copy VS");
constexpr uint32_t shader_hash_ReactiveMask = CompileTimeStringHash("SO Reactive Mask CS");
constexpr uint32_t shader_hash_SRExposure = CompileTimeStringHash("SO SR Exposure CS");
// The Bink video shaders, replaced for AutoHDR (see "Includes/Video.hlsl"). Full-screen movies draw into the swapchain, other uses
// into UI textures.
const ShaderHashesList shader_hashes_Video = {.pixel_shaders = {0x80649033, 0x0FA626B3, 0x11DDCB3A}};
// The game's SMAA runs on the tonemap's output: an edges CS, a weights CS dispatched indirectly over the edge pixels, then the blend
// PS. With SMAA 1x the blend reads a copy of the tonemap's output
// at t5 and writes the tonemap's target. With T2x it reads the tonemap's output at t5 and writes a temp.
constexpr uint32_t shader_hash_SmaaFindEdges = 0x8948AF03;
constexpr uint32_t shader_hash_SmaaComputeWeights = 0x0CC29EC5;
constexpr uint32_t shader_hash_SmaaApplyBlend = 0xD8DB5270;
constexpr uint32_t shader_hash_SmaaPredication = CompileTimeStringHash("SO SMAA Predication CS");
// The game's T2x temporal resolve into the tonemap's target. Without a history, "PS_Tex" copies the blend's temp there instead.
constexpr uint32_t shader_hash_SmaaResolveReproj = 0xFF745C34;
constexpr uint32_t shader_hash_SmaaT2xWeights = CompileTimeStringHash("SO SMAA T2x Blending Weight Calculation PS");
constexpr uint32_t shader_hash_SmaaT2xBlend = CompileTimeStringHash("SO SMAA T2x Neighborhood Blending PS");
constexpr uint32_t shader_hash_SmaaT2xResolve = CompileTimeStringHash("SO SMAA T2x Resolve PS");
// SMAA's scratch textures (Core's and the predication's) are released after this many presents without SMAA (~5 s at 120 fps), such
// as with "SMAA Enable" off, under SR or in menus
constexpr uint32_t SMAA_IDLE_RELEASE_FRAMES = 600;

static bool g_hide_ui = false; // Session only, so a restart always has a HUD
static bool g_fix_corrupted_save_warning = true;
static bool g_smaa_enable = true;
static bool g_smaa_t2x = false; // Our SMAA T2x (when "SMAA Enable" is on), whatever the game's option
static float g_rcas_sharpness = 0.f;
// The reactive mask for FSR (see "Luma_SO_ReactiveMask.hlsl")
constexpr float SR_REACTIVE_SCALE = 1.f;
constexpr float SR_REACTIVE_THRESHOLD = 0.2f;

namespace
{
   // The "Sunset.exe" code and data that SR and our SMAA use, as RVAs of the Steam build (TimeDateStamp 0x5C096926). The code is
   // encrypted on disk and unpacked at startup, so we check it against the live bytes.
   struct GameAddresses
   {
      uint8_t* jitter = nullptr;                      // void(View*, float* jx, float* jy): the camera view's projection jitter, NDC
      uint8_t* frame_params = nullptr;                // void(packet*): render command 49, copies the frame packet for the render thread
      const int32_t* game_frame = nullptr;            // The frame counter the jitter stamps into view+0x120C
      const int32_t* render_frame = nullptr;          // The render thread's copy of it, for the frame it draws
      int32_t* render_aa_option = nullptr;            // The render thread's copy of the "Anti-Aliasing" option (from the frame packet), which the SMAA and motion blur passes read
      const int32_t* debug_render_mode = nullptr;     // The game's jitter needs it under 2
      const uint8_t* skip_smaa = nullptr;             // A settings flag that skips SMAA and the game's jitter
      const uint8_t* shared_render_buffers = nullptr; // The "RenderBuffers" of the game's camera views (view+0x1160)
      uint8_t* save_entry_is_file = nullptr;          // bool(entry*): the save scans' "the found file isn't a directory" (see "SaveEntryIsFileDetour")
   };
   // The "Anti-Aliasing" option's values (game global 0x42AED00)
   constexpr int32_t AA_OPTION_OFF = 0;
   constexpr int32_t AA_OPTION_SMAA = 1;
   constexpr int32_t AA_OPTION_SMAA_T2X = 2;

   GameAddresses game_addresses; // Set right before the hooks are installed (the detours read it)

   // The jitter the hook produced for a game frame, with the camera values FSR needs. The render thread looks up the frame it draws.
   struct JitterRecord
   {
      int32_t frame = -1;
      float x = 0.f; // Pixels, +x right
      float y = 0.f; // Pixels, +y down
      float near_plane = 0.f;
      float far_plane = 0.f;
      float tan_half_fov_y = 0.f;
   };
   // The game thread can run a frame or two ahead of the render thread
   struct JitterHistory
   {
      std::shared_mutex mutex;
      std::array<JitterRecord, 4> records;
   } jitter_history;

   // Set at present. "VANILLA_JITTER" keeps the game's own jitter (with "SMAA Enable" off). "NO_JITTER" gives no jitter and no stamp,
   // for our SMAA 1x: the game's own jitter would stamp the frame, which turns its T2x resolve on whatever the option.
   // "STAMP_ONLY_JITTER" stamps without an offset (SR before its first drawn frame) and "SMAA_T2X_JITTER" gives SMAA T2x's two phases.
   // A positive value is the Halton phase count.
   constexpr int VANILLA_JITTER = -1;
   constexpr int NO_JITTER = -3;
   constexpr int STAMP_ONLY_JITTER = 0;
   constexpr int SMAA_T2X_JITTER = -2;
   std::atomic<int> g_jitter_phases = VANILLA_JITTER;
   // The "Anti-Aliasing" option the game's SMAA (and the motion blur's T2x mask) see in place of the game's: off under SR, SMAA or T2x
   // with our SMAA, and the game's own ("RENDER_AA_GAME") otherwise. We write it into the render thread's copy after each frame packet
   // copy, so the game's option, its menu and the screens that force SMAA keep their own value.
   constexpr int32_t RENDER_AA_GAME = -1;
   std::atomic<int32_t> g_render_aa_option = RENDER_AA_GAME;

   using JitterFunc = void (*)(uint8_t* view, float* jx, float* jy);
   JitterFunc jitter_original = nullptr;
   using FrameParamsFunc = void (*)(int64_t packet);
   FrameParamsFunc frame_params_original = nullptr;
   using SaveEntryFunc = bool (*)(const uint8_t* entry);
   SaveEntryFunc save_entry_is_file_original = nullptr;

   // The save scans (0x1880F70 and 0x18815A3) load every file of the save folder as a slot, but the two settings files. Steam Cloud's
   // "steam_autocloud.vdf" then becomes a slot that fails to load, the "corrupted save" warning at every start. Only the ".save" files the
   // game writes count as slots here.
   bool SaveEntryIsFileDetour(const uint8_t* entry)
   {
      if (!save_entry_is_file_original(entry))
         return false;
      if (!g_fix_corrupted_save_warning)
         return true;
      const std::wstring_view name = reinterpret_cast<const wchar_t*>(entry + 0x34); // WIN32_FIND_DATAW::cFileName
      constexpr std::wstring_view save_extension = L".save";
      return name.size() >= save_extension.size() && _wcsicmp(name.data() + name.size() - save_extension.size(), save_extension.data()) == 0;
   }

   void FrameParamsDetour(int64_t packet)
   {
      frame_params_original(packet);
      if (const int32_t aa_option = g_render_aa_option.load(std::memory_order_relaxed); aa_option != RENDER_AA_GAME)
      {
         *game_addresses.render_aa_option = aa_option;
      }
   }

   // With SR or our SMAA T2x, the main scene view gets our jitter and the frame stamp in view+0x120C, which the object velocity pass needs
   // with motion blur off. Only game camera views call it (not shadow, reflection or UI views).
   void JitterDetour(uint8_t* view, float* jx, float* jy)
   {
      const int phases = g_jitter_phases.load(std::memory_order_relaxed);
      // Our SMAA T2x keeps the game's jitter conditions (debug render mode, skip flag), but not its fade out in motion (view+0x1210)
      if (phases == VANILLA_JITTER || (phases == SMAA_T2X_JITTER && (*game_addresses.debug_render_mode >= 2 || *game_addresses.skip_smaa != 0)))
      {
         jitter_original(view, jx, jy);
         return;
      }
      // Only the main scene view gets our jitter: the one with the shared render buffers and a perspective projection (flag bit 1 =
      // orthographic). The others get none, since the game's own would follow the game's option, which Luma never writes.
      const bool is_main_view = *reinterpret_cast<const uint8_t* const*>(view + 0x1160) == game_addresses.shared_render_buffers;
      const bool is_orthographic = (*reinterpret_cast<const uint32_t*>(view + 0x364) & 2) != 0;
      if (phases == NO_JITTER || !is_main_view || is_orthographic)
      {
         *jx = 0.f;
         *jy = 0.f;
         return;
      }
      const int32_t frame = *game_addresses.game_frame;
      *reinterpret_cast<int32_t*>(view + 0x120C) = frame;
      // SMAA T2x moves the projection by (-0.25, -0.25) then (0.25, 0.25) pixels (y down), for "SMAA.hlsl"'s subsample indices 1 and 2.
      // That is its table read with y up, which measured better.
      std::array<float, 2> offset = {};
      if (phases > 0)
      {
         const unsigned int phase = unsigned(frame) % unsigned(phases);
         offset = {SR::HaltonSequence(phase, 2), SR::HaltonSequence(phase, 3)};
      }
      else if (phases == SMAA_T2X_JITTER)
      {
         const float smaa_t2x_offset = ((frame & 1) == 0 ? -0.25f : 0.25f);
         offset = {smaa_t2x_offset, smaa_t2x_offset};
      }
      const JitterRecord record = {
         .frame = frame,
         .x = offset[0],
         .y = offset[1],
         // The camera block still holds the previous frame's projection here: close enough for FSR's near, far and FOV
         .near_plane = *reinterpret_cast<const float*>(view + 0x330),
         .far_plane = *reinterpret_cast<const float*>(view + 0x334),
         .tan_half_fov_y = *reinterpret_cast<const float*>(view + 0x35C),
      };
      // Over the view rect (the render size), NDC y up
      *jx = 2.f * record.x / *reinterpret_cast<const float*>(view + 0x388);
      *jy = -2.f * record.y / *reinterpret_cast<const float*>(view + 0x38C);
      const std::unique_lock lock(jitter_history.mutex);
      jitter_history.records[unsigned(frame) % jitter_history.records.size()] = record;
   }

   // The jitter the hook produced for the frame the render thread draws, if it ran for it
   std::optional<JitterRecord> FindRenderFrameJitter()
   {
      const int32_t render_frame = *game_addresses.render_frame;
      const std::shared_lock lock(jitter_history.mutex);
      const JitterRecord& record = jitter_history.records[unsigned(render_frame) % jitter_history.records.size()];
      if (record.frame != render_frame)
         return std::nullopt;
      return record;
   }
} // namespace

// The size of the texture behind a view (0x0 without one)
static uint2 GetViewTextureSize(ID3D11View* view)
{
   uint4 size;
   DXGI_FORMAT format;
   GetResourceInfo(view, size, format);
   return uint2{size.x, size.y};
}

// A Luma native shader ("native_*_shaders") by name, null until compiled. Looked up with "find": "operator[]" would insert on a miss,
// mutating a map Core's draw helpers read. Callers hold "s_mutex_shader_objects", as a reload can race the lookup.
template <typename ShaderMap>
static auto FindShader(const ShaderMap& shaders, uint32_t name)
{
   const auto it = shaders.find(name);
   return (it != shaders.end() ? it->second.get() : nullptr);
}

// Whether Core's SMAA passes ("DrawSMAA()") are compiled
static bool HasSMAAShaders(const DeviceData& device_data)
{
   const auto& ps = device_data.native_pixel_shaders;
   const auto& vs = device_data.native_vertex_shaders;
   return FindShader(ps, "SMAA Edge Detection PS"_h) && FindShader(ps, "SMAA Blending Weight Calculation PS"_h) && FindShader(ps, "SMAA Neighborhood Blending PS"_h) &&
          FindShader(vs, "SMAA Edge Detection VS"_h) && FindShader(vs, "SMAA Blending Weight Calculation VS"_h) && FindShader(vs, "SMAA Neighborhood Blending VS"_h);
}

// Persisted setting widgets: the control, its ini save, a tooltip (also shown while disabled) and Core's reset button
static void DrawSettingSlider(const char* label, const char* key, float* value, float default_value, float min_value, float max_value, const char* tooltip)
{
   ImGui::SliderFloat(label, value, min_value, max_value, "%.3f");
   // Saved once the edit ends, not on every drag step
   if (ImGui::IsItemDeactivatedAfterEdit())
   {
      reshade::set_config_value(nullptr, NAME, key, *value);
   }
   if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
   {
      ImGui::SetTooltip("%s", tooltip);
   }
   DrawResetButton(*value, default_value, key);
}

// A bool, or a 0/1 float (a cbuffer field), shown as a checkbox. "forced": shown off and disabled while another feature replaces it,
// keeping the saved choice.
template <typename T>
static void DrawSettingCheckbox(const char* label, const char* key, T* value, std::type_identity_t<T> default_value, const char* tooltip, bool forced = false)
{
   bool shown = !forced && float(*value) > 0.5f;
   ImGui::BeginDisabled(forced);
   if (ImGui::Checkbox(label, &shown))
   {
      *value = T(shown);
      reshade::set_config_value(nullptr, NAME, key, *value);
   }
   ImGui::EndDisabled();
   if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
   {
      ImGui::SetTooltip("%s", tooltip);
   }
   if (!forced)
   {
      DrawResetButton(*value, default_value, key);
   }
}

// A texture with a view per bind flag
struct TextureViews
{
   com_ptr<ID3D11Texture2D> texture;
   com_ptr<ID3D11RenderTargetView> rtv;
   com_ptr<ID3D11ShaderResourceView> srv;
   com_ptr<ID3D11UnorderedAccessView> uav;
};

// Keeps "views" if they already have the size, format and bind flags, else recreates them, all or nothing (cleared on failure)
static bool EnsureTextureViews(ID3D11Device* native_device, uint2 size, DXGI_FORMAT format, UINT bind_flags, TextureViews* views)
{
   if (views->texture)
   {
      D3D11_TEXTURE2D_DESC existing_desc;
      views->texture->GetDesc(&existing_desc);
      if (existing_desc.Width == size.x && existing_desc.Height == size.y && existing_desc.Format == format && existing_desc.BindFlags == bind_flags)
         return true;
   }
   *views = {};
   const D3D11_TEXTURE2D_DESC desc = {
      .Width = size.x,
      .Height = size.y,
      .MipLevels = 1,
      .ArraySize = 1,
      .Format = format,
      .SampleDesc = {.Count = 1},
      .Usage = D3D11_USAGE_DEFAULT,
      .BindFlags = bind_flags,
   };
   TextureViews created;
   if (FAILED(native_device->CreateTexture2D(&desc, nullptr, &created.texture)) ||
       ((bind_flags & D3D11_BIND_RENDER_TARGET) != 0 && FAILED(native_device->CreateRenderTargetView(created.texture.get(), nullptr, &created.rtv))) ||
       ((bind_flags & D3D11_BIND_SHADER_RESOURCE) != 0 && FAILED(native_device->CreateShaderResourceView(created.texture.get(), nullptr, &created.srv))) ||
       ((bind_flags & D3D11_BIND_UNORDERED_ACCESS) != 0 && FAILED(native_device->CreateUnorderedAccessView(created.texture.get(), nullptr, &created.uav))))
      return false;
   *views = created;
   return true;
}

struct SunsetOverdriveGameDeviceData final : public GameDeviceData
{
   bool sr_active = false; // Latched at present for the next frame
   bool sr_tried = false;  // SR's "Draw" ran this frame (once)
   // R16G16_FLOAT motion vectors at the velocity target's size, drawn by the velocity passes
   TextureViews motion_vectors;
   bool motion_vectors_drawn = false;           // This frame
   com_ptr<ID3D11ShaderResourceView> depth_srv; // This frame's D32S8 scene depth (t0 of the camera velocity pass)
   com_ptr<ID3D11Resource> exposure_downsample; // t5 of "CS_AverageLumAccum" when it last ran
   // The scene before SR at the exposure downsample's size, drawn as the game's "PS_Tex" (bilinear), for the bloom's opaque/alpha split
   TextureViews bloom_split_scene;
   bool bloom_split_scene_drawn = false; // This frame
   JitterRecord camera;                  // The last one found for a drawn frame (FSR fails without a FOV)
   // The reactive mask (FSR): the scene before the transparent phase, copied this frame, and the mask at its size
   TextureViews reactive_opaque_scene;
   bool reactive_opaque_scene_copied = false;
   TextureViews reactive_mask;
   com_ptr<ID3D11ShaderResourceView> exposure_buffer_srv; // The tonemap's g_AdaptedLumBuffer (t7, [1] = exposure), the last time it ran
   TextureViews sr_exposure;                              // FSR's 1x1 exposure (see "Luma_SO_SRExposure.hlsl")

   // RCAS sharpens the tonemap's output into this, which the copy onto the swapchain then reads
   TextureViews rcas_output;

   // SMAA's predication edge-ness at the scene's size
   TextureViews smaa_predication;
   uint32_t smaa_last_frame = 0;      // FrameIndex of SMAA's last run, for "SMAA_IDLE_RELEASE_FRAMES"
   bool smaa_compute_skipped = false; // The game's SMAA edges and weights were skipped this frame

   // Whether our SMAA T2x is on, latched at present. "t2x_frames" holds SMAA's output of this frame and of the previous one,
   // alternating, as gamma 2.2 decoded RGB with the velocity length in alpha. The previous one is the history only if "t2x_frame", the
   // last frame the resolve ran, was the frame before.
   bool t2x_active = false;
   std::array<TextureViews, 2> t2x_frames;
   uint32_t t2x_frame = UINT32_MAX;
};

class SunsetOverdrive final : public Game
{
public:
   void OnInit(bool async) override
   {
      GetShaderDefineData(POST_PROCESS_SPACE_TYPE_HASH).SetDefaultValue('0');
      GetShaderDefineData(EARLY_DISPLAY_ENCODING_HASH).SetDefaultValue('0');
      GetShaderDefineData(VANILLA_ENCODING_TYPE_HASH).SetDefaultValue('0');
      GetShaderDefineData(GAMMA_CORRECTION_TYPE_HASH).SetDefaultValue('1');
      // The scene copy and videos onto the swapchain pre-scale by GamePaperWhite/UIPaperWhite, so the Scaleform UI drawn raw on top
      // gets its own paper white (Includes/Common.hlsl)
      GetShaderDefineData(UI_DRAW_TYPE_HASH).SetDefaultValue('2');
      // 0 stays selectable as the vanilla reference every HDR change gets compared against (Includes/ToneMap.hlsl)
      std::vector<ShaderDefineData> game_shader_defines_data = {
         {"TONEMAP_TYPE", '1', true, false, "0 - Vanilla SDR\n1 - Luma HDR", 1},
      };
      shader_defines_data.append_range(game_shader_defines_data);
      assert(shader_defines_data.size() < MAX_SHADER_DEFINES);

      // The embedded engine shaders bind b0-b3 (material shaders are unverified)
      luma_settings_cbuffer_index = 13;
      luma_data_cbuffer_index = 12;
      luma_ui_cbuffer_index = -1;

      // Manual paper white sliders, not the OS reference level. Core shows the UI one on UI_DRAW_TYPE >= 1.
      use_os_reference_white_level = false;

      default_luma_global_game_settings = {
         .Dithering = 1.f,
         .ColorGradingIntensity = 1.f,
         .BloomIntensity = 1.f,
         .FilmGrainIntensity = 1.f,
         .Exposure = 1.f,
         .Contrast = 1.f,
         .Saturation = 1.f,
         .VideoAutoHDREnable = 1.f,
         .VideoAutoHDRBoost = 0.5f,
      };
      cb_luma_global_settings.GameSettings = default_luma_global_game_settings;

      native_shaders_definitions.emplace(shader_hash_SmaaPredication, ShaderDefinition{"Luma_SO_SMAAPredication", reshade::api::pipeline_subobject_type::compute_shader});
      // SMAA T2x's own passes ("SMAA_T2X" in "Luma_SMAA_impl.hlsl"). The edge detection and the vertex shaders are 1x's.
      native_shaders_definitions.emplace(shader_hash_SmaaT2xWeights, ShaderDefinition{"Luma_SMAA_impl", reshade::api::pipeline_subobject_type::pixel_shader, nullptr, "smaa_blending_weight_calculation_ps", {{"SMAA_T2X", "1"}}});
      native_shaders_definitions.emplace(shader_hash_SmaaT2xBlend, ShaderDefinition{"Luma_SMAA_impl", reshade::api::pipeline_subobject_type::pixel_shader, nullptr, "smaa_neighborhood_blending_ps", {{"SMAA_T2X", "1"}}});
      native_shaders_definitions.emplace(shader_hash_SmaaT2xResolve, ShaderDefinition{"Luma_SMAA_impl", reshade::api::pipeline_subobject_type::pixel_shader, nullptr, "smaa_resolve_ps", {{"SMAA_T2X", "1"}}});
      native_shaders_definitions.emplace(shader_hash_ReactiveMask, ShaderDefinition{"Luma_SO_ReactiveMask", reshade::api::pipeline_subobject_type::compute_shader});
      native_shaders_definitions.emplace(shader_hash_SRExposure, ShaderDefinition{"Luma_SO_SRExposure", reshade::api::pipeline_subobject_type::compute_shader});
   }

   static SunsetOverdriveGameDeviceData& GetGameDeviceData(DeviceData& device_data)
   {
      return *static_cast<SunsetOverdriveGameDeviceData*>(device_data.game);
   }

   void OnCreateDevice(ID3D11Device* native_device, DeviceData& device_data) override
   {
      device_data.game = new SunsetOverdriveGameDeviceData;
   }

   // Draws a velocity pass with the motion vectors added as the shaders' extra output (RT2 of the camera pass, RT1 of the object
   // pass), then restores the game's targets
   static DrawOrDispatchOverrideType DrawVelocityWithMotionVectors(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, SunsetOverdriveGameDeviceData* game_device_data, std::function<void()>* original_draw_dispatch_func, bool camera_pass)
   {
      const UINT slot = (camera_pass ? 2 : 1);
      com_ptr<ID3D11RenderTargetView> rtvs[2];
      com_ptr<ID3D11DepthStencilView> dsv;
      native_device_context->OMGetRenderTargets(slot, &rtvs[0], &dsv);
      ID3D11RenderTargetView* const velocity_rtv = rtvs[slot - 1].get();
      if (!velocity_rtv || !original_draw_dispatch_func || !*original_draw_dispatch_func)
         return DrawOrDispatchOverrideType::None;
      // The camera pass runs first and starts the frame's motion vectors at the velocity target's size
      if (camera_pass)
      {
         const uint2 size = GetViewTextureSize(velocity_rtv);
         if (!EnsureTextureViews(native_device, size, DXGI_FORMAT_R16G16_FLOAT, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET, &game_device_data->motion_vectors))
            return DrawOrDispatchOverrideType::None;
         // Stencil rejected pixels (depth-stencil state 1 is unverified) stay still
         constexpr float zero[4] = {};
         native_device_context->ClearRenderTargetView(game_device_data->motion_vectors.rtv.get(), zero);
         game_device_data->motion_vectors_drawn = true;
      }
      else if (!game_device_data->motion_vectors_drawn)
      {
         return DrawOrDispatchOverrideType::None;
      }
      ID3D11RenderTargetView* draw_rtvs[] = {rtvs[0].get(), rtvs[1].get(), nullptr};
      draw_rtvs[slot] = game_device_data->motion_vectors.rtv.get();
      native_device_context->OMSetRenderTargets(slot + 1, draw_rtvs, dsv.get());
      (*original_draw_dispatch_func)();
      native_device_context->OMSetRenderTargets(slot, draw_rtvs, dsv.get());
      return DrawOrDispatchOverrideType::Replaced;
   }

   // FSR reads the reactive mask (DLSS's presets ignore it)
   static bool UsesReactiveMask(const DeviceData& device_data)
   {
      return device_data.sr_type == SR::Type::FSR;
   }

   // Dispatches "cs" with "uav" at u0 and "srvs" from t0, between a cache and a restore of the compute state ("bind_constants", if
   // any, runs after the cache so the restore undoes it). The UAV is bound first: nothing else may alias it.
   static void DispatchCompute(ID3D11DeviceContext* native_device_context, const DeviceData& device_data, ID3D11ComputeShader* cs, ID3D11UnorderedAccessView* uav, std::span<ID3D11ShaderResourceView* const> srvs, uint2 groups, const std::function<void()>& bind_constants = {})
   {
      DrawStateStack<DrawStateStackType::Compute> compute_state;
      compute_state.Cache(native_device_context, device_data.uav_max_count);
      if (bind_constants)
      {
         bind_constants();
      }
      native_device_context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
      native_device_context->CSSetShaderResources(0, UINT(srvs.size()), srvs.data());
      native_device_context->CSSetShader(cs, nullptr, 0);
      native_device_context->Dispatch(groups.x, groups.y, 1);
      compute_state.Restore(native_device_context);
   }

   // The reactive mask from the scene's change since the copy before the transparent phase (see "Luma_SO_ReactiveMask.hlsl")
   static bool DrawReactiveMask(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData* cmd_list_data, DeviceData* device_data, SunsetOverdriveGameDeviceData* game_device_data, ID3D11ShaderResourceView* scene_srv, uint2 size)
   {
      if (GetViewTextureSize(game_device_data->reactive_opaque_scene.srv.get()) != size)
         return false;
      if (!EnsureTextureViews(native_device, size, DXGI_FORMAT_R8_UNORM, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS, &game_device_data->reactive_mask))
         return false;
      const std::shared_lock lock_shader_objects(s_mutex_shader_objects);
      auto* const reactive_mask_cs = FindShader(device_data->native_compute_shaders, shader_hash_ReactiveMask);
      if (!reactive_mask_cs)
         return false;
      ID3D11ShaderResourceView* const srvs[3] = {game_device_data->reactive_opaque_scene.srv.get(), scene_srv, game_device_data->exposure_buffer_srv.get()};
      DispatchCompute(native_device_context, *device_data, reactive_mask_cs, game_device_data->reactive_mask.uav.get(), srvs, uint2{(size.x + 7) / 8, (size.y + 7) / 8}, [&]()
         { SetLumaConstantBuffers(native_device_context, *cmd_list_data, *device_data, reshade::api::shader_stage::compute, LumaConstantBufferType::LumaData, 0, 0, SR_REACTIVE_SCALE, SR_REACTIVE_THRESHOLD); });
      return true;
   }

   // The reactive mask's "before": the scene after the fog and the occlusion dropouts, before the transparents and VFX (see
   // "shader_hash_OcclCoverDropouts")
   static void CopyReactiveOpaqueScene(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, ID3D11Resource* scene, SunsetOverdriveGameDeviceData* game_device_data)
   {
      com_ptr<ID3D11Texture2D> scene_texture;
      if (!game_device_data->motion_vectors_drawn || !scene || FAILED(scene->QueryInterface(&scene_texture)))
         return;
      D3D11_TEXTURE2D_DESC scene_desc;
      scene_texture->GetDesc(&scene_desc);
      const uint2 size = {scene_desc.Width, scene_desc.Height};
      if (scene_desc.Format != DXGI_FORMAT_R11G11B10_FLOAT || size != GetViewTextureSize(game_device_data->motion_vectors.rtv.get()))
         return;
      if (!EnsureTextureViews(native_device, size, DXGI_FORMAT_R11G11B10_FLOAT, D3D11_BIND_SHADER_RESOURCE, &game_device_data->reactive_opaque_scene))
         return;
      native_device_context->CopyResource(game_device_data->reactive_opaque_scene.texture.get(), scene);
      game_device_data->reactive_opaque_scene_copied = true;
   }

   // DLSS or FSR 3 on the scene before the post chain reads it, written back in place. "split_bloom" means the bloom's bright
   // passes are still to come, which isn't the case at the tonemap.
   static void DrawSR(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData* cmd_list_data, DeviceData* device_data, SunsetOverdriveGameDeviceData* game_device_data, ID3D11ShaderResourceView* scene_srv, bool split_bloom)
   {
      if (!scene_srv || !game_device_data->motion_vectors_drawn || !game_device_data->depth_srv)
         return;
      com_ptr<ID3D11Resource> depth;
      game_device_data->depth_srv->GetResource(&depth);
      com_ptr<ID3D11Resource> scene_resource;
      scene_srv->GetResource(&scene_resource);
      com_ptr<ID3D11Texture2D> scene;
      if (FAILED(scene_resource->QueryInterface(&scene)))
         return;
      D3D11_TEXTURE2D_DESC scene_desc;
      scene->GetDesc(&scene_desc);
      // Read once, so the whole draw uses one SR. Upstream Core has no lock against the overlay switching it meanwhile (the other games
      // don't take one either); a draw lock in Core would close that window.
      const SR::Type sr_type = device_data->sr_type;
      SR::InstanceData* const sr_instance_data = device_data->GetSRInstanceData();
      const uint2 scene_size = {scene_desc.Width, scene_desc.Height};
      const bool scene_matches_motion_vectors = scene_desc.Format == DXGI_FORMAT_R11G11B10_FLOAT && scene_size == GetViewTextureSize(game_device_data->motion_vectors.rtv.get());
      if (!sr_instance_data || !scene_matches_motion_vectors || (std::min)(scene_size.x, scene_size.y) < sr_instance_data->min_resolution)
         return;

      // The scene's format, so the output is copied back as is
      bool output_recreated = false;
      D3D11_TEXTURE2D_DESC output_desc = {};
      if (device_data->sr_output_color)
      {
         device_data->sr_output_color->GetDesc(&output_desc);
      }
      if (output_desc.Width != scene_desc.Width || output_desc.Height != scene_desc.Height)
      {
         device_data->sr_output_color = nullptr;
         output_desc = {.Width = scene_desc.Width, .Height = scene_desc.Height, .MipLevels = 1, .ArraySize = 1, .Format = scene_desc.Format, .SampleDesc = {.Count = 1}, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS};
         if (FAILED(native_device->CreateTexture2D(&output_desc, nullptr, &device_data->sr_output_color)))
         {
            device_data->sr_suppressed = true; // Back to the anti-aliasing without SR until SR is picked again
            return;
         }
         output_recreated = true;
      }

      const SR::SettingsData settings_data = {
         .output_width = scene_desc.Width,
         .output_height = scene_desc.Height,
         .render_width = scene_desc.Width,
         .render_height = scene_desc.Height,
         .hdr = true, // Linear, scene referred, not pre-exposed
         .inverted_depth = true,
         // Render pixels, previous minus current, jitter removed (see the velocity shaders)
         .mvs_jittered = false,
         // DLSS's own. FSR's clips highlights, so it gets the tonemap's exposure below.
         .auto_exposure = sr_type != SR::Type::FSR,
         // "Default" is K for DLAA (L and M crush hues)
         .render_preset = (dlss_render_preset != 0 ? dlss_render_preset : 11u),
      };
      auto& sr_implementation = *sr_implementations[sr_type];
      sr_implementation.UpdateSettings(sr_instance_data, native_device_context, settings_data);

      // The jitter of the frame the render thread draws; none (still a valid input) if the hook didn't run for it
      const std::optional<JitterRecord> render_frame_jitter = FindRenderFrameJitter();
      const JitterRecord jitter = render_frame_jitter.value_or(JitterRecord{});
      if (render_frame_jitter)
      {
         game_device_data->camera = *render_frame_jitter;
      }
      const JitterRecord& camera = game_device_data->camera;
      SR::SuperResolutionImpl::DrawData draw_data = {
         .reset = device_data->force_reset_sr || output_recreated,
         .output_color = device_data->sr_output_color.get(),
         .source_color = scene.get(),
         .motion_vectors = game_device_data->motion_vectors.texture.get(),
         .depth_buffer = depth.get(),
         .render_width = scene_desc.Width,
         .render_height = scene_desc.Height,
         // As applied to the projection (pixels, +y down)
         .jitter_x = jitter.x,
         .jitter_y = jitter.y,
         .vert_fov = (camera.tan_half_fov_y > 0.f ? 2.f * std::atan(camera.tan_half_fov_y) : (60.f * float(M_PI) / 180.f)), // FSR fails without it
         .frame_index = cb_luma_global_settings.FrameIndex,
      };
      if (camera.near_plane > 0.f && camera.far_plane > camera.near_plane)
      {
         draw_data.near_plane = camera.near_plane;
         draw_data.far_plane = camera.far_plane;
      }
      if (game_device_data->reactive_opaque_scene_copied && UsesReactiveMask(*device_data) && DrawReactiveMask(native_device, native_device_context, cmd_list_data, device_data, game_device_data, scene_srv, scene_size))
      {
         draw_data.bias_mask = game_device_data->reactive_mask.texture.get();
      }
      // FSR's exposure is the tonemap's own multiplier (see "Luma_SO_SRExposure.hlsl")
      const bool fsr_exposure = sr_type == SR::Type::FSR && game_device_data->exposure_buffer_srv;
      if (fsr_exposure && EnsureTextureViews(native_device, uint2{1, 1}, DXGI_FORMAT_R32_FLOAT, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS, &game_device_data->sr_exposure))
      {
         const std::shared_lock lock_shader_objects(s_mutex_shader_objects);
         if (auto* const sr_exposure_cs = FindShader(device_data->native_compute_shaders, shader_hash_SRExposure))
         {
            ID3D11ShaderResourceView* const srv = game_device_data->exposure_buffer_srv.get();
            DispatchCompute(native_device_context, *device_data, sr_exposure_cs, game_device_data->sr_exposure.uav.get(), {&srv, 1}, uint2{1, 1}, [&]()
               { SetLumaConstantBuffers(native_device_context, *cmd_list_data, *device_data, reshade::api::shader_stage::compute, LumaConstantBufferType::LumaSettings); });
            draw_data.exposure = game_device_data->sr_exposure.texture.get();
         }
      }

      // NGX and FFX don't restore the state they touch
      DrawStateStack<DrawStateStackType::FullGraphics> graphics_state;
      DrawStateStack<DrawStateStackType::Compute> compute_state;
      graphics_state.Cache(native_device_context, device_data->uav_max_count);
      compute_state.Cache(native_device_context, device_data->uav_max_count);
      game_device_data->sr_tried = true;
      const bool drawn = sr_implementation.Draw(sr_instance_data, native_device_context, draw_data);
      // The bloom's opaque/alpha split compares the scene with the jittered mid-forward copy. Fed SR's output, the anti-aliased edges
      // would leak into the alpha bloom and shimmer with the jitter, so those passes read the scene before SR, as in vanilla.
      if (com_ptr<ID3D11Texture2D> exposure_downsample; drawn && split_bloom && game_device_data->exposure_downsample && SUCCEEDED(game_device_data->exposure_downsample->QueryInterface(&exposure_downsample)))
      {
         D3D11_TEXTURE2D_DESC exposure_downsample_desc;
         exposure_downsample->GetDesc(&exposure_downsample_desc);
         const uint2 size = {exposure_downsample_desc.Width, exposure_downsample_desc.Height};
         const std::shared_lock lock_shader_objects(s_mutex_shader_objects);
         auto* const scale_vs = FindShader(device_data->native_vertex_shaders, CompileTimeStringHash("Scale VS"));
         auto* const scale_ps = FindShader(device_data->native_pixel_shaders, CompileTimeStringHash("Scale PS"));
         if (scale_vs && scale_ps && EnsureTextureViews(native_device, size, DXGI_FORMAT_R11G11B10_FLOAT, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET, &game_device_data->bloom_split_scene))
         {
            native_device_context->OMSetRenderTargetsAndUnorderedAccessViews(0, nullptr, nullptr, 0, 0, nullptr, nullptr);
            DrawCustomPixelShader(native_device_context, device_data->default_depth_stencil_state.get(), device_data->default_blend_state.get(), device_data->sampler_state_linear.get(), scale_vs, scale_ps, scene_srv, game_device_data->bloom_split_scene.rtv.get(), size.x, size.y);
            game_device_data->bloom_split_scene_drawn = true;
         }
      }
      compute_state.Restore(native_device_context);
      graphics_state.Restore(native_device_context);
      if (!drawn)
      {
         device_data->sr_suppressed = true;
         return;
      }
      native_device_context->CopyResource(scene.get(), device_data->sr_output_color.get());
      device_data->has_drawn_sr = true;
   }

   // Sharpens the tonemap's output (t6 of the copy onto the swapchain) with RCAS into a texture, which the copy then reads instead
   static void DrawRCASOnSwapchainCopy(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, DeviceData* device_data, SunsetOverdriveGameDeviceData* game_device_data)
   {
      com_ptr<ID3D11ShaderResourceView> source_srv;
      native_device_context->PSGetShaderResources(6, 1, &source_srv);
      if (!source_srv)
         return;
      D3D11_SHADER_RESOURCE_VIEW_DESC source_srv_desc;
      source_srv->GetDesc(&source_srv_desc);
      const uint2 size = GetViewTextureSize(source_srv.get());
      if (source_srv_desc.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D || size.x == 0 || size.y == 0)
         return;

      // "DrawRCAS()" looks its shaders up with ".at()": a shader reload must not release them meanwhile
      const std::shared_lock lock_shader_objects(s_mutex_shader_objects);
      if (!PrepareRCAS(native_device, *device_data))
         return;
      // The source's view format, so the copy reads the same encoding
      if (!EnsureTextureViews(native_device, size, source_srv_desc.Format, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET, &game_device_data->rcas_output))
         return;
      DrawRCAS(native_device_context, *device_data, source_srv.get(), game_device_data->rcas_output.rtv.get(), g_rcas_sharpness);
      ID3D11ShaderResourceView* const rcas_srv = game_device_data->rcas_output.srv.get();
      native_device_context->PSSetShaderResources(6, 1, &rcas_srv);
   }

   // Core's SMAA in place of the game's blend draw: it reads the blend's color input (t5) and writes the blend's target. Returns false
   // when an input or a shader is missing.
   static bool DrawSMAAInPlaceOfBlend(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData* cmd_list_data, DeviceData* device_data, SunsetOverdriveGameDeviceData* game_device_data)
   {
      com_ptr<ID3D11RenderTargetView> rtv;
      native_device_context->OMGetRenderTargets(1, &rtv, nullptr);
      com_ptr<ID3D11ShaderResourceView> color_srv;
      native_device_context->PSGetShaderResources(5, 1, &color_srv);
      if (!rtv || !color_srv)
         return false;
      const uint2 size = GetViewTextureSize(color_srv.get());
      if (size != GetViewTextureSize(rtv.get()) || size.x == 0 || size.y == 0)
         return false;

      // Held through SMAA so a shader reload cannot release them mid-use; "DrawSMAA" looks its shaders up with "at"
      const std::shared_lock lock_shader_objects(s_mutex_shader_objects);
      if (!HasSMAAShaders(*device_data))
         return false;
      game_device_data->smaa_last_frame = cb_luma_global_settings.FrameIndex;

      // Predication from this frame's scene depth at the same size; without it plain ULTRA
      auto* const predication_cs = FindShader(device_data->native_compute_shaders, shader_hash_SmaaPredication);
      const bool predication = game_device_data->depth_srv && GetViewTextureSize(game_device_data->depth_srv.get()) == size && predication_cs &&
                               EnsureTextureViews(native_device, size, DXGI_FORMAT_R16_FLOAT, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS,
                                  &game_device_data->smaa_predication);

      if (predication)
      {
         // Unbind the render targets: were the depth still bound as the DSV, D3D11 would silently null its SRV in the predication pass
         com_ptr<ID3D11RenderTargetView> rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
         com_ptr<ID3D11DepthStencilView> dsv;
         native_device_context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, &rtvs[0], &dsv);
         native_device_context->OMSetRenderTargets(0, nullptr, nullptr);
         ID3D11ShaderResourceView* const raw_depth_srv = game_device_data->depth_srv.get();
         DispatchCompute(native_device_context, *device_data, predication_cs, game_device_data->smaa_predication.uav.get(), {&raw_depth_srv, 1}, uint2{(size.x + 7) / 8, (size.y + 7) / 8});
         native_device_context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, reinterpret_cast<ID3D11RenderTargetView* const*>(&rtvs[0]), dsv.get());
      }

      // SMAA T2x draws SMAA into this frame's linear target with the velocity, then the resolve with the previous frame into the blend's
      // target. It needs this frame's motion vectors at the same size. Without them this frame is SMAA 1x, which the game's own resolve
      // or copy then carries on.
      auto* const copy_vs = FindShader(device_data->native_vertex_shaders, shader_hash_CopyVS);
      auto* const t2x_weights_ps = FindShader(device_data->native_pixel_shaders, shader_hash_SmaaT2xWeights);
      auto* const t2x_blend_ps = FindShader(device_data->native_pixel_shaders, shader_hash_SmaaT2xBlend);
      auto* const t2x_resolve_ps = FindShader(device_data->native_pixel_shaders, shader_hash_SmaaT2xResolve);
      const bool t2x_shaders = copy_vs && t2x_weights_ps && t2x_blend_ps && t2x_resolve_ps;
      bool t2x = game_device_data->t2x_active && game_device_data->motion_vectors_drawn && GetViewTextureSize(game_device_data->motion_vectors.srv.get()) == size && t2x_shaders;
      const uint32_t frame_index = cb_luma_global_settings.FrameIndex;
      const int t2x_current = int(frame_index & 1);
      bool t2x_history = game_device_data->t2x_frame + 1 == frame_index;
      auto& t2x_frames = game_device_data->t2x_frames;
      if (t2x && (GetViewTextureSize(t2x_frames[0].srv.get()) != size || GetViewTextureSize(t2x_frames[1].srv.get()) != size))
      {
         t2x_history = false;
         constexpr UINT bind_flags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
         t2x = EnsureTextureViews(native_device, size, DXGI_FORMAT_R16G16B16A16_FLOAT, bind_flags, &t2x_frames[0]) &&
               EnsureTextureViews(native_device, size, DXGI_FORMAT_R16G16B16A16_FLOAT, bind_flags, &t2x_frames[1]);
      }
      // The area texture subsamples of the jitter phase the frame was drawn with (see "JitterDetour"), 0 if it didn't jitter
      float subsample_indices = 0.f;
      if (t2x)
      {
         if (const std::optional<JitterRecord> jitter = FindRenderFrameJitter(); jitter && jitter->x != 0.f)
         {
            subsample_indices = (jitter->x < 0.f ? 1.f : 2.f);
         }
      }

      // The SMAA shaders read the target size, the predication scale and T2x's subsample indices from the Luma data (the render
      // resolution isn't the swapchain's)
      SetLumaConstantBuffers(native_device_context, *cmd_list_data, *device_data, reshade::api::shader_stage::vertex | reshade::api::shader_stage::pixel, LumaConstantBufferType::LumaSettings);
      SetLumaConstantBuffers(native_device_context, *cmd_list_data, *device_data, reshade::api::shader_stage::vertex | reshade::api::shader_stage::pixel, LumaConstantBufferType::LumaData, size.x, size.y, (predication ? 2.f : 1.f), subsample_indices);
      // Core creates its area texture at the first draw; ours goes in first, without the U-shape smoothing (see "SMAA_AreaTex_SharpU.h")
      if (auto& area_tex_srv = device_data->managed_resources.shader_resource_views["smaa_area_tex"_h]; !area_tex_srv)
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
      // "Luma_SMAA_impl.hlsl"), whose neighborhood blend reads the velocity at t3, a slot "DrawSMAA()" leaves alone. The swap is within
      // the shared lock, which a shader reload waits for, and only our SMAA reads these shaders. Upstream "DrawSMAA()" takes no pixel
      // shader overrides; with such an argument in Core the swap would go.
      auto& pixel_shaders = device_data->native_pixel_shaders;
      const auto swap_t2x_shaders = [&]()
      {
         std::swap(pixel_shaders.at("SMAA Blending Weight Calculation PS"_h), pixel_shaders.at(shader_hash_SmaaT2xWeights));
         std::swap(pixel_shaders.at("SMAA Neighborhood Blending PS"_h), pixel_shaders.at(shader_hash_SmaaT2xBlend));
      };
      com_ptr<ID3D11ShaderResourceView> t3_original;
      if (t2x)
      {
         native_device_context->PSGetShaderResources(3, 1, &t3_original);
         ID3D11ShaderResourceView* const velocity_srv = game_device_data->motion_vectors.srv.get();
         native_device_context->PSSetShaderResources(3, 1, &velocity_srv);
         swap_t2x_shaders();
      }
      // The neighborhood blend reads the gamma input too and filters it in linear light itself (see "SMAA_NEIGHBORHOOD_GAMMA_IN_LINEAR")
      DrawSMAA(native_device, native_device_context, *device_data, (t2x ? t2x_frames[t2x_current].rtv.get() : rtv.get()), color_srv.get(), color_srv.get(), (predication ? game_device_data->smaa_predication.srv.get() : nullptr));
      if (t2x)
      {
         swap_t2x_shaders();
         ID3D11ShaderResourceView* const t3_original_raw = t3_original.get();
         native_device_context->PSSetShaderResources(3, 1, &t3_original_raw);

         // The resolve: t0 this frame, t1 the previous one or this one again without a history, t2 the motion vectors, s0 linear and
         // s1 point
         DrawStateStack<DrawStateStackType::FullGraphics> resolve_state;
         resolve_state.Cache(native_device_context, device_data->uav_max_count);
         ID3D11ShaderResourceView* const resolve_srvs[2] = {t2x_frames[t2x_history ? (t2x_current ^ 1) : t2x_current].srv.get(), game_device_data->motion_vectors.srv.get()};
         native_device_context->PSSetShaderResources(1, 2, resolve_srvs);
         ID3D11SamplerState* const resolve_samplers[2] = {device_data->sampler_state_linear.get(), device_data->sampler_state_point.get()};
         native_device_context->PSSetSamplers(0, 2, resolve_samplers);
         DrawCustomPixelShader(native_device_context, device_data->default_depth_stencil_state.get(), device_data->default_blend_state.get(), nullptr, copy_vs, t2x_resolve_ps, t2x_frames[t2x_current].srv.get(), rtv.get(), size.x, size.y, false);
         resolve_state.Restore(native_device_context);
         game_device_data->t2x_frame = frame_index;
      }
      return true;
   }

   // Draws a plain copy of the bound t5 into the bound target, in place of the draw. We use it for the game's T2x resolve after ours,
   // where t5 is the blend's temp that ours wrote, and for the game's blend when its edges and weights were skipped but ours couldn't
   // draw. There t5 is a copy of the target with SMAA 1x, or with T2x the tonemap's output, which the game's resolve or copy carries on.
   static bool DrawColorInputCopy(ID3D11DeviceContext* native_device_context, const DeviceData& device_data)
   {
      com_ptr<ID3D11RenderTargetView> rtv;
      native_device_context->OMGetRenderTargets(1, &rtv, nullptr);
      com_ptr<ID3D11ShaderResourceView> srv;
      native_device_context->PSGetShaderResources(5, 1, &srv);
      const std::shared_lock lock_shader_objects(s_mutex_shader_objects);
      auto* const copy_vs = FindShader(device_data.native_vertex_shaders, shader_hash_CopyVS);
      auto* const copy_ps = FindShader(device_data.native_pixel_shaders, CompileTimeStringHash("Copy PS"));
      const uint2 size = GetViewTextureSize(rtv.get());
      if (!rtv || !srv || !copy_vs || !copy_ps || size != GetViewTextureSize(srv.get()))
         return false;
      DrawStateStack<DrawStateStackType::FullGraphics> state;
      state.Cache(native_device_context, device_data.uav_max_count);
      DrawCustomPixelShader(native_device_context, device_data.default_depth_stencil_state.get(), device_data.default_blend_state.get(), nullptr, copy_vs, copy_ps, srv.get(), rtv.get(), size.x, size.y);
      state.Restore(native_device_context);
      return true;
   }

   DrawOrDispatchOverrideType OnDrawOrDispatch(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, reshade::api::shader_stage stages, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, bool is_custom_pass, bool& updated_cbuffers, std::function<void()>* original_draw_dispatch_func) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);
      if (original_shader_hashes.Contains(shader_hash_AverageLumAccum, reshade::api::shader_stage::compute))
      {
         game_device_data.exposure_downsample = nullptr;
         com_ptr<ID3D11ShaderResourceView> srv;
         native_device_context->CSGetShaderResources(5, 1, &srv);
         if (srv)
         {
            srv->GetResource(&game_device_data.exposure_downsample);
         }
         return DrawOrDispatchOverrideType::None;
      }
      // The game's SMAA edges and weights aren't needed while Core's SMAA replaces its blend (which turns into a copy should ours fail)
      if (original_shader_hashes.Contains(shader_hash_SmaaFindEdges, reshade::api::shader_stage::compute) || original_shader_hashes.Contains(shader_hash_SmaaComputeWeights, reshade::api::shader_stage::compute))
      {
         const std::shared_lock lock_shader_objects(s_mutex_shader_objects);
         if (!g_smaa_enable || !HasSMAAShaders(device_data))
            return DrawOrDispatchOverrideType::None;
         game_device_data.smaa_compute_skipped = true;
         return DrawOrDispatchOverrideType::Skip;
      }
      if ((stages & reshade::api::shader_stage::pixel) != reshade::api::shader_stage::pixel)
         return DrawOrDispatchOverrideType::None;
      const auto is_pixel_shader = [&](uint32_t hash)
      { return original_shader_hashes.Contains(hash, reshade::api::shader_stage::pixel); };
      const auto render_target_resource = [&]()
      {
         com_ptr<ID3D11RenderTargetView> rtv;
         native_device_context->OMGetRenderTargets(1, &rtv, nullptr);
         com_ptr<ID3D11Resource> resource;
         if (rtv)
         {
            rtv->GetResource(&resource);
         }
         return resource;
      };
      const auto targets_back_buffer = [&]()
      {
         const com_ptr<ID3D11Resource> resource = render_target_resource();
         const std::shared_lock lock(device_data.mutex);
         return resource && device_data.back_buffers.contains(reinterpret_cast<uint64_t>(resource.get()));
      };

      if (is_pixel_shader(shader_hash_CameraVelocity))
      {
         game_device_data.depth_srv = nullptr;
         native_device_context->PSGetShaderResources(0, 1, &game_device_data.depth_srv);
      }
      if (g_smaa_enable && is_pixel_shader(shader_hash_SmaaApplyBlend))
      {
         if (DrawSMAAInPlaceOfBlend(native_device, native_device_context, &cmd_list_data, &device_data, &game_device_data))
         {
            updated_cbuffers = true;
            return DrawOrDispatchOverrideType::Replaced;
         }
         if (game_device_data.smaa_compute_skipped && DrawColorInputCopy(native_device_context, device_data))
            return DrawOrDispatchOverrideType::Replaced;
      }
      if (game_device_data.t2x_frame == cb_luma_global_settings.FrameIndex && is_pixel_shader(shader_hash_SmaaResolveReproj) && DrawColorInputCopy(native_device_context, device_data))
         return DrawOrDispatchOverrideType::Replaced;
      // The velocity passes add the motion vectors for SR and SMAA T2x
      const bool motion_vectors = (game_device_data.sr_active || game_device_data.t2x_active) && native_device_context->GetType() == D3D11_DEVICE_CONTEXT_IMMEDIATE;
      if (motion_vectors && is_pixel_shader(shader_hash_CameraVelocity))
         return DrawVelocityWithMotionVectors(native_device, native_device_context, &game_device_data, original_draw_dispatch_func, true);
      if (motion_vectors && is_pixel_shader(shader_hash_ObjectVelocity))
         return DrawVelocityWithMotionVectors(native_device, native_device_context, &game_device_data, original_draw_dispatch_func, false);
      const bool is_tonemap = shader_hashes_ToneMap.Contains(original_shader_hashes.pixel_shaders[0], reshade::api::shader_stage::pixel);
      if (is_tonemap)
      {
         game_device_data.exposure_buffer_srv = nullptr;
         native_device_context->PSGetShaderResources(7, 1, &game_device_data.exposure_buffer_srv);
      }
      if (game_device_data.sr_active && motion_vectors && !game_device_data.sr_tried && !device_data.sr_suppressed)
      {
         UINT scene_slot = 0;
         const bool needs_reactive_copy = !game_device_data.reactive_opaque_scene_copied && UsesReactiveMask(device_data);
         if (is_pixel_shader(shader_hash_MotionBlurDownsampleColor))
         {
            scene_slot = 5;
         }
         else if (is_pixel_shader(shader_hash_OcclCoverDropouts))
         {
            // The copy comes after its draw, from its target (the scene)
            if (needs_reactive_copy && original_draw_dispatch_func && *original_draw_dispatch_func)
            {
               (*original_draw_dispatch_func)();
               CopyReactiveOpaqueScene(native_device, native_device_context, render_target_resource().get(), &game_device_data);
               return DrawOrDispatchOverrideType::Replaced;
            }
         }
         else if (is_pixel_shader(shader_hash_Tex))
         {
            // A generic copy: the exposure downsample by its target, else the mid-forward copy (the reactive mask's fallback "before")
            if (game_device_data.exposure_downsample && render_target_resource() == game_device_data.exposure_downsample)
            {
               scene_slot = 5;
            }
            else if (needs_reactive_copy)
            {
               com_ptr<ID3D11ShaderResourceView> scene_srv;
               native_device_context->PSGetShaderResources(5, 1, &scene_srv);
               com_ptr<ID3D11Resource> scene;
               if (scene_srv)
               {
                  scene_srv->GetResource(&scene);
               }
               CopyReactiveOpaqueScene(native_device, native_device_context, scene.get(), &game_device_data);
            }
         }
         else if (is_tonemap)
         {
            scene_slot = 6;
         }
         if (scene_slot != 0)
         {
            com_ptr<ID3D11ShaderResourceView> scene_srv;
            native_device_context->PSGetShaderResources(scene_slot, 1, &scene_srv);
            DrawSR(native_device, native_device_context, &cmd_list_data, &device_data, &game_device_data, scene_srv.get(), !is_tonemap);
         }
      }
      const bool is_bloom_split_pass = game_device_data.bloom_split_scene_drawn && (is_pixel_shader(shader_hash_BrightPassOpaque) || is_pixel_shader(shader_hash_BrightPassAlpha));
      if (is_bloom_split_pass && original_draw_dispatch_func && *original_draw_dispatch_func)
      {
         com_ptr<ID3D11ShaderResourceView> scene_srv;
         native_device_context->PSGetShaderResources(5, 1, &scene_srv);
         if (GetViewTextureSize(scene_srv.get()) == GetViewTextureSize(game_device_data.bloom_split_scene.srv.get()))
         {
            ID3D11ShaderResourceView* const split_scene_srv = game_device_data.bloom_split_scene.srv.get();
            native_device_context->PSSetShaderResources(5, 1, &split_scene_srv);
            (*original_draw_dispatch_func)();
            ID3D11ShaderResourceView* const original_scene_srv = scene_srv.get();
            native_device_context->PSSetShaderResources(5, 1, &original_scene_srv);
            return DrawOrDispatchOverrideType::Replaced;
         }
      }
      if (is_tonemap)
      {
         device_data.has_drawn_main_post_processing = true;
         return DrawOrDispatchOverrideType::None;
      }
      // Videos: AutoHDR only onto the swapchain (LumaData.CustomData1), never skipped by Hide Gameplay UI
      if (shader_hashes_Video.Contains(original_shader_hashes.pixel_shaders[0], reshade::api::shader_stage::pixel))
      {
         SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, stages, LumaConstantBufferType::LumaSettings);
         SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, stages, LumaConstantBufferType::LumaData, (targets_back_buffer() ? 1 : 0));
         updated_cbuffers = true;
         return DrawOrDispatchOverrideType::None;
      }
      if (!device_data.has_drawn_main_post_processing)
         return DrawOrDispatchOverrideType::None;
      const bool is_copy = is_pixel_shader(shader_hash_CopyBuffer);
      if ((!is_copy && !g_hide_ui) || !targets_back_buffer())
         return DrawOrDispatchOverrideType::None;

      // The scene lands on the swapchain: sharpened after SR or our SMAA, and flagged so the copy dithers it once (LumaData.CustomData1)
      if (is_copy)
      {
         if ((device_data.has_drawn_sr || game_device_data.smaa_last_frame == cb_luma_global_settings.FrameIndex) && g_rcas_sharpness > 0.f)
         {
            DrawRCASOnSwapchainCopy(native_device, native_device_context, &device_data, &game_device_data);
         }
         SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, stages, LumaConstantBufferType::LumaSettings);
         SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, stages, LumaConstantBufferType::LumaData, 1);
         updated_cbuffers = true;
         return DrawOrDispatchOverrideType::None;
      }

      // Everything drawn onto the swapchain after the scene copy is Scaleform UI (HUD, menus, prompts)
      return DrawOrDispatchOverrideType::Skip;
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

   // The engine hooks and code patches SR and our SMAA need, checked against this build's live code (the exe is unpacked at startup)
   static void InstallEngineHooks()
   {
      auto* const base = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
      // All null for another build, or before the code is unpacked
      const auto find_game_addresses = [base]() -> GameAddresses
      {
         const auto* const nt_headers = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + reinterpret_cast<const IMAGE_DOS_HEADER*>(base)->e_lfanew);
         using System::ANY;
         // cmp [rip+?], 2; mov rax, [rcx+0x1160]
         constexpr std::array<System::BytePattern, 14> jitter_prologue = {{0x83, 0x3D, ANY, ANY, ANY, ANY, 0x02,
            0x48, 0x8B, 0x81, 0x60, 0x11, 0x00, 0x00}};
         uint8_t* const jitter = base + 0x1886EC0;
         // sub rsp, 0x48; movaps xmm0, [rip+?]; mov edx, 5; mov rax, [rcx+8]
         constexpr std::array<System::BytePattern, 20> frame_params_prologue = {{0x48, 0x83, 0xEC, 0x48, 0x0F, 0x28, 0x05, ANY, ANY, ANY,
            ANY, 0xBA, 0x05, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x41, 0x08}};
         uint8_t* const frame_params = base + 0x1895020;
         // mov eax, [rcx+8]; shr eax, 4; not eax; and eax, 1; ret (FILE_ATTRIBUTE_DIRECTORY clear)
         constexpr std::array<System::BytePattern, 12> save_entry_is_file_code = {{0x8B, 0x41, 0x08, 0xC1, 0xE8, 0x04,
            0xF7, 0xD0, 0x83, 0xE0, 0x01, 0xC3}};
         uint8_t* const save_entry_is_file = base + 0x1E68240;
         const auto matches = [](const uint8_t* code, std::span<const System::BytePattern> prologue)
         { return !System::ScanMemoryForPattern(reinterpret_cast<const std::byte*>(code), prologue.size(), prologue, true).empty(); };
         const bool is_steam_build = nt_headers->FileHeader.TimeDateStamp == 0x5C096926;
         if (!is_steam_build || !matches(jitter, jitter_prologue) || !matches(frame_params, frame_params_prologue) ||
             !matches(save_entry_is_file, save_entry_is_file_code))
            return {};
         return {
            .jitter = jitter,
            .frame_params = frame_params,
            .game_frame = reinterpret_cast<const int32_t*>(base + 0x427CEB0),
            .render_frame = reinterpret_cast<const int32_t*>(base + 0x42AF3A4),
            .render_aa_option = reinterpret_cast<int32_t*>(base + 0x42AF340),
            .debug_render_mode = reinterpret_cast<const int32_t*>(base + 0x42AEB64),
            .skip_smaa = base + 0x42AEC60,
            .shared_render_buffers = base + 0x42AFFC0,
            .save_entry_is_file = save_entry_is_file,
         };
      };
      if (const GameAddresses addresses = find_game_addresses(); addresses.jitter && MH_Initialize() == MH_OK)
      {
         game_addresses = addresses;
         const auto install = [](uint8_t* target, void* detour, void** original)
         {
            if (MH_CreateHook(target, detour, original) != MH_OK || MH_EnableHook(target) != MH_OK)
            {
               *original = nullptr;
            }
         };
         install(addresses.jitter, reinterpret_cast<void*>(&JitterDetour), reinterpret_cast<void**>(&jitter_original));
         install(addresses.frame_params, reinterpret_cast<void*>(&FrameParamsDetour), reinterpret_cast<void**>(&frame_params_original));
         install(addresses.save_entry_is_file, reinterpret_cast<void*>(&SaveEntryIsFileDetour), reinterpret_cast<void**>(&save_entry_is_file_original));

         // The object velocity pass skips far, small or slow objects (its per object score, 0x190AD20, fades them out for the motion
         // blur), so they have no motion vectors: zero the two distance terms of the score (mulss xmm1, 0.004 and mulss xmm0, 0.1
         // -> xorps, nop), which keeps every moving object at full weight. Only over the exact vanilla bytes (rip-relative operands
         // included), so another build or an addon reload (already patched) leaves the code alone.
         const auto patch = [](uint8_t* code, std::span<const uint8_t, 8> vanilla, std::span<const uint8_t, 8> patched)
         {
            if (std::memcmp(code, vanilla.data(), vanilla.size()) == 0)
            {
               System::PatchMemory(code, patched.data(), patched.size());
            }
         };
         constexpr uint8_t mulss_xmm1_distance[] = {0xF3, 0x0F, 0x59, 0x0D, 0x74, 0x80, 0xFE, 0x00}; // mulss xmm1, [0.004]
         constexpr uint8_t mulss_xmm0_size[] = {0xF3, 0x0F, 0x59, 0x05, 0xE5, 0x98, 0xB3, 0x00};     // mulss xmm0, [0.1]
         constexpr uint8_t zero_xmm1[] = {0x0F, 0x57, 0xC9, 0x0F, 0x1F, 0x44, 0x00, 0x00};
         constexpr uint8_t zero_xmm0[] = {0x0F, 0x57, 0xC0, 0x0F, 0x1F, 0x44, 0x00, 0x00};
         patch(base + 0x190BA30, mulss_xmm1_distance, zero_xmm1);
         patch(base + 0x190BA63, mulss_xmm0_size, zero_xmm0);
      }
      if (!jitter_original || !frame_params_original)
      {
         sr_game_tooltip = "Unsupported game executable version: Super Resolution can't engage.\n";
      }
   }

   void OnPresent(ID3D11Device* native_device, DeviceData& device_data) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);
      // Once, at the first present: by then the game code is unpacked
      static bool hooks_attempted = false;
      if (!std::exchange(hooks_attempted, true))
      {
         InstallEngineHooks();
      }

      device_data.has_drawn_main_post_processing = false;
      // Any frame SR didn't draw (off, failed, a menu without a scene) restarts its history at the next one
      const bool sr_drew_last_frame = device_data.has_drawn_sr;
      device_data.force_reset_sr = !sr_drew_last_frame;
      device_data.has_drawn_sr = false;
      const bool hooks_installed = jitter_original && frame_params_original;
      game_device_data.sr_active = device_data.sr_type != SR::Type::None && !device_data.sr_suppressed && hooks_installed;
      // The overlay's SR tick (with "has_drawn_sr"): our own TAA input path, the motion vectors and the jitter hook, is in
      device_data.taa_detected = game_device_data.sr_active;
      game_device_data.sr_tried = false;
      game_device_data.motion_vectors_drawn = false;
      game_device_data.depth_srv = nullptr;
      game_device_data.reactive_opaque_scene_copied = false;
      game_device_data.bloom_split_scene_drawn = false;
      game_device_data.smaa_compute_skipped = false;
      // SMAA's scratch textures are released once idle, Core's intermediates included
      if (game_device_data.smaa_last_frame != 0 && cb_luma_global_settings.FrameIndex - game_device_data.smaa_last_frame > SMAA_IDLE_RELEASE_FRAMES)
      {
         game_device_data.smaa_last_frame = 0;
         game_device_data.smaa_predication = {};
         ReleaseSMAAIntermediates(device_data);
      }
      // The jitter starts after a drawn frame (none while SR can't find its inputs)
      const SR::InstanceData* const sr_instance_data = (game_device_data.sr_active && sr_drew_last_frame ? device_data.GetSRInstanceData() : nullptr);
      // Our SMAA T2x in place of the game's: its own jitter (the game's fades out in motion and is half the reference offset)
      game_device_data.t2x_active = g_smaa_enable && g_smaa_t2x && !game_device_data.sr_active && hooks_installed;
      if (!game_device_data.t2x_active)
      {
         game_device_data.t2x_frames = {};
      }
      int32_t render_aa_option = RENDER_AA_GAME;
      int jitter_phases = VANILLA_JITTER;
      if (game_device_data.sr_active)
      {
         render_aa_option = AA_OPTION_OFF;
         jitter_phases = (sr_instance_data ? (std::max)(sr_implementations[device_data.sr_type]->GetJitterPhases(sr_instance_data), 1) : STAMP_ONLY_JITTER);
      }
      else if (game_device_data.t2x_active)
      {
         render_aa_option = AA_OPTION_SMAA_T2X;
         jitter_phases = SMAA_T2X_JITTER;
      }
      else if (g_smaa_enable)
      {
         render_aa_option = AA_OPTION_SMAA;
         jitter_phases = NO_JITTER;
      }
      g_render_aa_option = render_aa_option;
      g_jitter_phases = jitter_phases;
      // SR resolves more texture detail: -1 mip bias at native resolution
      if (const float bias = (sr_instance_data ? SR::SuperResolutionImpl::GetMipLODBias(sr_instance_data) : 0.f); !custom_texture_mip_lod_bias_offset)
      {
         const std::unique_lock lock_samplers(s_mutex_samplers);
         if (device_data.texture_mip_lod_bias_offset != bias)
         {
            device_data.texture_mip_lod_bias_offset = bias;
            com_ptr<ID3D11DeviceContext> native_device_context;
            native_device->GetImmediateContext(&native_device_context);
            RebindUpgradedSamplers(native_device_context.get(), &device_data);
         }
      }
   }

   void CleanExtraSRResources(DeviceData& device_data) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);
      game_device_data.motion_vectors = {};
      game_device_data.motion_vectors_drawn = false;
      game_device_data.exposure_downsample = nullptr;
      game_device_data.reactive_opaque_scene = {};
      game_device_data.reactive_mask = {};
      game_device_data.rcas_output = {};
      game_device_data.bloom_split_scene = {};
      game_device_data.sr_exposure = {};
   }

   void LoadConfigs() override
   {
      auto& settings = cb_luma_global_settings.GameSettings;
      reshade::get_config_value(nullptr, NAME, "Dithering", settings.Dithering);
      reshade::get_config_value(nullptr, NAME, "ColorGradingIntensity", settings.ColorGradingIntensity);
      reshade::get_config_value(nullptr, NAME, "BloomIntensity", settings.BloomIntensity);
      reshade::get_config_value(nullptr, NAME, "FilmGrainIntensity", settings.FilmGrainIntensity);
      reshade::get_config_value(nullptr, NAME, "Exposure", settings.Exposure);
      reshade::get_config_value(nullptr, NAME, "Contrast", settings.Contrast);
      reshade::get_config_value(nullptr, NAME, "Saturation", settings.Saturation);
      reshade::get_config_value(nullptr, NAME, "VideoAutoHDREnable", settings.VideoAutoHDREnable);
      reshade::get_config_value(nullptr, NAME, "VideoAutoHDRBoost", settings.VideoAutoHDRBoost);
      reshade::get_config_value(nullptr, NAME, "RCASSharpness", g_rcas_sharpness);
      reshade::get_config_value(nullptr, NAME, "SMAAEnable", g_smaa_enable);
      reshade::get_config_value(nullptr, NAME, "SMAAT2x", g_smaa_t2x);
      reshade::get_config_value(nullptr, NAME, "FixCorruptedSaveWarning", g_fix_corrupted_save_warning);
   }

   void DrawImGuiSettings(DeviceData& device_data) override
   {
      auto& settings = cb_luma_global_settings.GameSettings;

      // Super Resolution itself is Core's combo, above
      ImGui::SeparatorText("Anti-Aliasing");
      // Super Resolution replaces SMAA only where it can engage (the game hooks are in), as latched at the last present
      const bool sr_engaged = GetGameDeviceData(device_data).sr_active;
      DrawSettingCheckbox("SMAA Enable", "SMAAEnable", &g_smaa_enable, true, "Replaces the game's SMAA with Luma's (sharper, more complete edges; works with the game's Anti-Aliasing setting on or off; not used with DLSS/FSR).", sr_engaged);
      ImGui::BeginDisabled(!g_smaa_enable);
      DrawSettingCheckbox("SMAA T2x", "SMAAT2x", &g_smaa_t2x, false, "Smoother edges and less shimmering, by blending each frame with the previous one.\nCan look slightly softer in motion. Not used with DLSS/FSR.", sr_engaged);
      ImGui::EndDisabled();
      // RCAS runs after our SMAA or Super Resolution only
      ImGui::BeginDisabled(!g_smaa_enable && !sr_engaged);
      DrawSettingSlider("RCAS Sharpness", "RCASSharpness", &g_rcas_sharpness, 0.f, 0.f, 1.f, "Sharpening applied on top of anti-aliasing (0 = off).");
      ImGui::EndDisabled();

      // The 3D LUT, the bloom, its lens flare, the film grain and the vignette all live in the tonemap (only the permutations that have them)
      ImGui::SeparatorText("Grade");
      DrawSettingSlider("Exposure", "Exposure", &settings.Exposure, default_luma_global_game_settings.Exposure, 0.f, 2.f, "Overall image brightness (1 = vanilla).");
      DrawSettingSlider("Contrast", "Contrast", &settings.Contrast, default_luma_global_game_settings.Contrast, 0.f, 2.f, "Overall image contrast, HDR only (1 = vanilla).");
      DrawSettingSlider("Saturation", "Saturation", &settings.Saturation, default_luma_global_game_settings.Saturation, 0.f, 2.f, "Color saturation, HDR only (1 = vanilla).");
      DrawSettingSlider("Color Grading Intensity", "ColorGradingIntensity", &settings.ColorGradingIntensity, default_luma_global_game_settings.ColorGradingIntensity, 0.f, 1.f, "Strength of the game's own color grading (1 = vanilla, 0 = neutral).");

      ImGui::SeparatorText("Bloom");
      DrawSettingSlider("Bloom Intensity", "BloomIntensity", &settings.BloomIntensity, default_luma_global_game_settings.BloomIntensity, 0.f, 2.f, "Bloom strength (1 = vanilla, 0 = none).");

      ImGui::SeparatorText("Effects");
      DrawSettingSlider("Film Grain Intensity", "FilmGrainIntensity", &settings.FilmGrainIntensity, default_luma_global_game_settings.FilmGrainIntensity, 0.f, 2.f, "Scales the game's film grain (1 = vanilla, 0 = off).");
      DrawSettingCheckbox("Video AutoHDR", "VideoAutoHDREnable", &settings.VideoAutoHDREnable, default_luma_global_game_settings.VideoAutoHDREnable, "Adds HDR highlights to pre-rendered videos (HDR only).");
      ImGui::BeginDisabled(settings.VideoAutoHDREnable <= 0.5f);
      DrawSettingSlider("Video HDR Boost", "VideoAutoHDRBoost", &settings.VideoAutoHDRBoost, default_luma_global_game_settings.VideoAutoHDRBoost, 0.f, 1.f, "Video highlight strength (0 = off).");
      ImGui::EndDisabled();
      DrawSettingCheckbox("Dithering", "Dithering", &settings.Dithering, default_luma_global_game_settings.Dithering, "Reduces gradient banding.");

      ImGui::SeparatorText("UI");
      ImGui::Checkbox("Hide Gameplay UI", &g_hide_ui);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      {
         ImGui::SetTooltip("Disables the in-game UI.");
      }

      ImGui::SeparatorText("Fixes");
      DrawSettingCheckbox("Fix Corrupted Save Warning", "FixCorruptedSaveWarning", &g_fix_corrupted_save_warning, true, "Stops the \"corrupted save\" warning at every start (caused by Steam Cloud's sync file in the save folder).");
   }

   void PrintImGuiAbout() override
   {
      ImGui::PushTextWrapPos(0.f);
      ImGui::Text(
         "Luma for \"Sunset Overdrive\" is developed by DristoforColumb and is open source and free.\n"
         "It adds HDR, DLAA or FSR 3 native anti-aliasing and improves SMAA anti-aliasing, plus 16x anisotropic filtering.\n"
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
      {
         ShellExecuteA(nullptr, "open", "https://ko-fi.com/dristoforcolumb", nullptr, nullptr, SW_SHOWNORMAL);
      }
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
      {
         ShellExecuteA(nullptr, "open", "https://github.com/Filoppi/Luma-Framework", nullptr, nullptr, SW_SHOWNORMAL);
      }

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
                  "\nAMD FidelityFX (RCAS + FSR 3)"
                  "\nNVIDIA NGX (DLSS)");
   }
};

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
   if (ul_reason_for_call == DLL_PROCESS_ATTACH)
   {
      Globals::SetGlobals(PROJECT_NAME, "Sunset Overdrive Luma mod", "", 1);

      // The scene and bloom are already float (R11G11B10). The tonemap writes R10G10B10A2 at the game's render resolution,
      // which SMAA, the T2x history and PS_CopyBuffer carry to the RGBA8 swapchain (the window size, pillarboxed to 16:9).
      swapchain_format_upgrade_type = TextureFormatUpgradesType::AllowedEnabled;
      swapchain_upgrade_type = SwapchainUpgradeType::scRGB;
      texture_format_upgrades_type = TextureFormatUpgradesType::AllowedEnabled;
      texture_upgrade_formats = {
         reshade::api::format::r10g10b10a2_unorm,
         reshade::api::format::r10g10b10a2_typeless,
      };
      // The render resolution is an option independent of the window, so we also match its 16:9 aspect ratio, not only the swapchain size
      texture_format_upgrades_2d_size_filters = (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainResolution | (uint32_t)TextureFormatUpgrades2DSizeFilters::CustomAspectRatio;

      // Exclusive fullscreen is blocked (Core default); turn the game's fullscreen requests into a borderless window instead
      force_borderless = true;

      // The SR mip bias (the engine's samplers have none and no option for it)
      enable_samplers_upgrade = true;

#if DEVELOPMENT
      // Offline map of the exe-embedded engine shaders, for traces and the DevKit
      for (const auto& [hash, name] : std::initializer_list<std::pair<uint32_t, const char*>>{
              {0x92D51420, "ToneMap"},
              {0x7880CCF1, "ToneMap (Bloom)"},
              {0x86EE37B6, "ToneMap (LUT)"},
              {0xC291251A, "ToneMap (Bloom LUT)"},
              {0xE4EAD75E, "ToneMap (Grain)"},
              {0x6619803F, "ToneMap (Bloom Grain)"},
              {0x2A6A872A, "ToneMap (LUT Grain)"},
              {0xEE7FB6CC, "ToneMap (Bloom LUT Grain)"},
              {0x58E6385C, "ToneMap (Vignette)"},
              {0x633A5894, "ToneMap (Bloom Vignette)"},
              {0xD6B7231C, "ToneMap (LUT Vignette)"},
              {0xE3E7DD84, "ToneMap (Bloom LUT Vignette)"},
              {0x980ACE18, "ToneMap (Grain Vignette)"},
              {0x2F20FD47, "ToneMap (Bloom Grain Vignette)"},
              {0x902D30F9, "ToneMap (LUT Grain Vignette)"},
              {0xB2BBC089, "ToneMap (Bloom LUT Grain Vignette)"},
              {shader_hash_CopyBuffer, "Copy Buffer (post tonemap)"},
              {0x2EB348CF, "Bright Pass"},
              {shader_hash_BrightPassOpaque, "Bright Pass Opaque"},
              {shader_hash_BrightPassAlpha, "Bright Pass Alpha"},
              {0xE2230CBF, "Bloom Blur Down"},
              {0x52300544, "Bloom Blur Up"},
              {shader_hash_AverageLumAccum, "Average Lum Accumulate"},
              {0x33A2F37D, "Average Lum Finalize"},
              {shader_hash_SmaaFindEdges, "SMAA Find Edges"},
              {shader_hash_SmaaComputeWeights, "SMAA Compute Weights"},
              {shader_hash_SmaaApplyBlend, "SMAA Neighborhood Blend"},
              {shader_hash_SmaaResolveReproj, "SMAA T2x Resolve Reproject"},
              {shader_hash_CameraVelocity, "Linear Depth + Camera Velocity"},
              {shader_hash_ObjectVelocity, "Object Velocity"},
              {0xD665B34D, "Motion Blur Pack Velocity"},
              {0xFCE207A5, "Motion Blur Apply"},
              {shader_hash_MotionBlurDownsampleColor, "Motion Blur Downsample Color"},
              {shader_hash_Tex, "Tex (copy)"},
              {0xA5ADD0FE, "Apply Light Linked List (deferred lighting)"},
              {0x172FF9A3, "SSAO Depth Downsample"},
              {0x7770B550, "SSAO Generate"},
              {0xA1CCFEC8, "SSAO Generate Interleaved"},
              {0xF70ADA39, "SSAO Blur Combine"},
              {0x4750F817, "SSAO Upsample"},
              {0x6854DB47, "SSAO Upsample Combine"},
              {0xFEB8BC17, "DoF Downsample Far"},
              {0x575FE7DF, "DoF Downsample Near"},
              {0x1C679396, "DoF Downsample Neighborhood Near"},
              {0x2D574464, "DoF Gather Neighborhood Near"},
              {0xBA698E9E, "DoF Generate Far"},
              {0x81F5F129, "DoF Generate Near"},
              {0x80649033, "Video YCbCr"},
              {0x0FA626B3, "Video NV12"},
              {0x11DDCB3A, "Video YCbCrA"},
              {0x26F1A4C4, "Quantize Color Buffer"},
           })
      {
         forced_shader_names.emplace(hash, name);
      }
#endif

      game = new SunsetOverdrive();
   }
   else if (ul_reason_for_call == DLL_PROCESS_DETACH)
   {
      // The engine hooks would jump into the unloaded addon
      MH_Uninitialize();
   }

   CoreMain(hModule, ul_reason_for_call, lpReserved);

   return TRUE;
}
