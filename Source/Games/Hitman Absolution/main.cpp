///////////////////////////////////////////////////////////////////////
// Of course, before any of this, create a new project for your game!
// - Copy "Templates\VisualStudio\Luma-Template.zip" into "%USERPROFILE%\Documents\Visual Studio [current version]\Templates\ProjectTemplates" (and restart Visual Studio).
// - Right click "Games" in the solution explorer, Add -> New Project... -> Template (Luma Template Project for new games)
// - Make sure the location is "Source\Games"!
///////////////////////////////////////////////////////////////////////

// Specify a define with the name of the game here (GAME_*).
// This is optional but can be used to hardcode custom (per game) behaviours in the core library,
// or other reasons. We can't automate it through the project define system (based on the project name) because
// we need these defines to be upper case and space free.
#define GAME_TEMPLATE 1
#define ENABLE_BLOOM 1 // core registers the Bloom VS/Prefilter/Downsample/Upsample passes used by DrawBloom (Luma Bloom setting)

//// Define all the global "core" defines before including its files: ////

// Enable support for Geometry Shaders (GS), rather rare.
#define GEOMETRY_SHADER_SUPPORT 0

// Enable to stop "You can now attach the debugger" popup.
#define DISABLE_AUTO_DEBUGGER 0

// DLSS/FSR are toggled via the "Luma Properties" page in the project properties (UseLumaNGX/UseLumaFSR).

// Instead of "manually" including the "core" library, we simply include its main code file (which is a header).
// The library in itself is standalone, as in, it compiles fine and could directly be used as a template addon etc if built as dll but,
// there's a major limitation in how libraries dependencies work by nature, and that is that you can only make
// one version of them for all other projects to use. For performance and tidiness reasons, we are interested in
// having global defines that can be turned on and off per game, as opposed to runtime (static) parameters.
// Hence why we specify the global defines before including the core Luma file (where near all of the generic Luma implementation is).
// If we wanted to use a library, we'd also need to add a core "main" definition in a cpp file, to link it properly.
// All externs that are currently defined in core would also need to be manually defined in each game's implementation (e.g. see "RESHADE_EXTERNS" above).
// The only disadvantage of not actually including the core as a library, is that we'll have to add the same include/library dependencies in our
// game project (e.g. add ReShade, DLSS, etc), and manually add all cpp files too.
//
// To compile in different modes (e.g. "DEVELOPMENT", "TEST" etc see "global_defines.h").
#include "..\..\Core\core.hpp"

// Forces the game window to a borderless popup covering the whole monitor.
static void ForceBorderlessFullscreen()
{
   HMONITOR h_monitor = MonitorFromWindow(game_window, MONITOR_DEFAULTTONEAREST);
   MONITORINFO mi = {};
   mi.cbSize = sizeof(mi);
   if (!GetMonitorInfo(h_monitor, &mi))
      return;
   const RECT& rc_monitor = mi.rcMonitor;

   LONG_PTR style = GetWindowLongPtr(game_window, GWL_STYLE);
   style &= ~(WS_OVERLAPPEDWINDOW | WS_BORDER | WS_THICKFRAME | WS_DLGFRAME);
   style |= WS_POPUP | WS_VISIBLE;
   SetWindowLongPtr(game_window, GWL_STYLE, style);

   LONG_PTR ex_style = GetWindowLongPtr(game_window, GWL_EXSTYLE);
   ex_style &= ~(WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE);
   SetWindowLongPtr(game_window, GWL_EXSTYLE, ex_style);

   SetWindowPos(game_window, nullptr,
      rc_monitor.left, rc_monitor.top,
      rc_monitor.right - rc_monitor.left, rc_monitor.bottom - rc_monitor.top,
      SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_SHOWWINDOW | SWP_ASYNCWINDOWPOS);
}
// Luma bloom: pyramid mip 0 is bound at kLumaBloomSlot for the tonemap draw. One sigma per mip, in mip texels (resolution-independent). Copied from Mass Effect.
static constexpr uint32_t kLumaBloomSlot = 4;
static constexpr uint32_t kTonemapHash = 0x46C50FC2;
static constexpr float g_bloom_sigmas[] = {1.5f, 2.f, 2.f, 2.f, 1.f, 0.5f};
constexpr uint32_t TONEMAP_TYPE_HASH = char_ptr_crc32("TONEMAP_TYPE");

struct TemplateGameDeviceData final : public GameDeviceData
{
   bool has_drawn_tonemap = false;
   // Non-owning view onto core's DrawBloom mip 0 (core-managed, released with the swapchain). Rebuilt every tonemap draw.
   ComPtr<ID3D11ShaderResourceView> srv_luma_bloom;
};

class TemplateGame final : public Game // Go view "Source/Core/includes/game.h" for all the overridable virtual functions
{
   // Optional helper to hide ugly casts
   static TemplateGameDeviceData& GetGameDeviceData(DeviceData& device_data)
   {
      return *static_cast<TemplateGameDeviceData*>(device_data.game);
   }

   // Karis average, then the bloom pyramid, off the HDR scene. The result is bound at kLumaBloomSlot for the draw.
   static void BindLumaBloom(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, DeviceData& device_data, ID3D11ShaderResourceView* srv_scene)
   {
      auto& gd = GetGameDeviceData(device_data);
      gd.srv_luma_bloom.reset(); // DrawBloom AddRef's its mip 0 into this
      if (srv_scene)
      {
         DrawStateStack<DrawStateStackType::FullGraphics> bloom_state;
         bloom_state.Cache(native_device_context, device_data.uav_max_count);

         ComPtr<ID3D11ShaderResourceView> srv_karis;
         DrawKarisAverage(native_device, native_device_context, device_data, srv_scene, srv_karis.put());
         if (srv_karis)
            DrawBloom(native_device, native_device_context, device_data, srv_karis.get(), (int)std::size(g_bloom_sigmas), g_bloom_sigmas, gd.srv_luma_bloom.put());

         bloom_state.Restore(native_device_context);
      }
      ID3D11ShaderResourceView* bloom_srv = gd.srv_luma_bloom.get();
      native_device_context->PSSetShaderResources(kLumaBloomSlot, 1, &bloom_srv);
   }

public:
   // You can define any data to initialize once here
   void OnInit(bool async) override
   {
      // You can add shader defines that will end up in the advanced settings for users to modify here.
      // These will be defined in the shaders, so they can be used to do static branches in them.
      // Ideally they should also be defined in the game's Settings.hlsl file.
      std::vector<ShaderDefineData> game_shader_defines_data = {
         {"TONEMAP_TYPE", /*default value*/ '1', true, false, /*tooltip*/ "0 - Vanilla SDR\n1 - Luma HDR (Vanilla+)", /*max value*/ 1},
      };
      shader_defines_data.append_range(game_shader_defines_data);
      assert(shader_defines_data.size() < MAX_SHADER_DEFINES); // Make sure there's room for at least one extra custom define to add for development (this isn't really relevant outside of development)

      // Define these according to the game's original technical details and the mod's implementation (see their declarations for more).
      GetShaderDefineData(POST_PROCESS_SPACE_TYPE_HASH).SetDefaultValue('0'); // What space are the colors in? Was the swapchain linear (sRGB texture format)? Did we change post processing to store in linear space?
      GetShaderDefineData(EARLY_DISPLAY_ENCODING_HASH).SetDefaultValue('0'); // Whether we do gamma correction and paper white scaling during post processing or we delay them until the final display composition pass
      GetShaderDefineData(VANILLA_ENCODING_TYPE_HASH).SetDefaultValue('1'); // Gamma 2.2: the HDR final pass encodes with pow(1/2.2) (DICE runs in that space), so decoding as sRGB overshot the peak by ~2%
      GetShaderDefineData(GAMMA_CORRECTION_TYPE_HASH).SetDefaultValue('1'); // What SDR transfer curve to we want to emulate? This is relevant even if we work in linear space, as there can be a gamma mismatch on it
      GetShaderDefineData(UI_DRAW_TYPE_HASH).SetDefaultValue('2'); // UI draws on the swapchain in gamma space after the final pass, which pre-divides the scene by UI paper white

      // Customize the cbuffers indexes here. They should be between 0 and 13 ("D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - 1"),
      // set them to invalid values (e.g. -1) to not set them (they won't be uploaded to the GPU, and thus not usable in shaders). All their values need to be different if they are valid.
      // These will be automatically sent to the GPU for every shader pass the mod overrides.
      luma_settings_cbuffer_index = 13;
      luma_data_cbuffer_index = 12; // Needed for debugging textures and having custom per pass data (including handling the final pass properly).
      luma_ui_cbuffer_index = -1; // Optional, see "UI_DRAW_TYPE" (this is for type 1)

      // Init the game settings here, you could also load them from ini in "LoadConfigs()".
      // These can be defined in a header shared between shaders and c++, and are sent to the GPU every time they change.
      //
      // It should be in "Shaders/Game/Includes/GameCBuffers.hlsl".
      //
      // To include it, the easiest way is adding as Forced Include within the Project Properties.
      // C/C++ -> All Options -> Forced Include File --> "../../../Shaders/$(ProjectName)/Includes/GameCBuffers.hlsl;" (w/o quotation marks)
      //
      // Otherwise, manually add it to the project files and include that at the top (before anything else, as the game cb settings struct needs to be defined),
      // or make a copy of it in c++ (making sure it's mirrored to hlsl), and define "LUMA_GAME_CB_STRUCTS" to avoid it being re-defined.
      //
      // Set "device_data.cb_luma_global_settings_dirty" to true if you changed values and need a re-upload to the GPU before OnPresent() (i.e. during OnDrawOrDispatch()).
      default_luma_global_game_settings.HighlightSaturation = cb_luma_global_settings.GameSettings.HighlightSaturation = 1.f;
      default_luma_global_game_settings.VanillaHueShift = cb_luma_global_settings.GameSettings.VanillaHueShift = 0.5f;
      default_luma_global_game_settings.BloomStrength = cb_luma_global_settings.GameSettings.BloomStrength = 0.75f;
      default_luma_global_game_settings.LumaBloom = cb_luma_global_settings.GameSettings.LumaBloom = 0u;
      default_luma_global_game_settings.LumaBloomStrength = cb_luma_global_settings.GameSettings.LumaBloomStrength = 1.f;
      default_luma_global_game_settings.VanillaGlareStrength = cb_luma_global_settings.GameSettings.VanillaGlareStrength = 0.25f;
      default_luma_global_game_settings.FilmGrainStrength = cb_luma_global_settings.GameSettings.FilmGrainStrength = 1.f;
      default_luma_global_game_settings.SkyHDRBoost = cb_luma_global_settings.GameSettings.SkyHDRBoost = 0.5f;
      default_luma_global_game_settings.SkyDebanding = cb_luma_global_settings.GameSettings.SkyDebanding = 1u;
      default_luma_global_game_settings.SunBrightness = cb_luma_global_settings.GameSettings.SunBrightness = 1.2f;
      default_luma_global_game_settings.Exposure = cb_luma_global_settings.GameSettings.Exposure = 1.f;
      default_luma_global_game_settings.Contrast = cb_luma_global_settings.GameSettings.Contrast = 1.f;
      default_luma_global_game_settings.Saturation = cb_luma_global_settings.GameSettings.Saturation = 1.f;
      default_luma_global_game_settings.Highlights = cb_luma_global_settings.GameSettings.Highlights = 1.f;
      default_luma_global_game_settings.Shadows = cb_luma_global_settings.GameSettings.Shadows = 1.f;
      default_luma_global_game_settings.BlackFloorRemoval = cb_luma_global_settings.GameSettings.BlackFloorRemoval = 1.f;
      default_luma_global_game_settings.HighlightRolloff = cb_luma_global_settings.GameSettings.HighlightRolloff = 0.f;
      default_luma_global_game_settings.DistanceHaze = cb_luma_global_settings.GameSettings.DistanceHaze = 1.f;
      default_luma_global_game_settings.HighlightHuePreservation = cb_luma_global_settings.GameSettings.HighlightHuePreservation = 0.5f;
      default_luma_global_game_settings.TopGradientStrength = cb_luma_global_settings.GameSettings.TopGradientStrength = 1.f;
      default_luma_global_game_settings.VignetteStrength = cb_luma_global_settings.GameSettings.VignetteStrength = 1.f;
      default_luma_global_game_settings.ShadowLuminanceGrading = cb_luma_global_settings.GameSettings.ShadowLuminanceGrading = 1.f;
      default_luma_global_game_settings.GammaCorrectionByLuminance = cb_luma_global_settings.GameSettings.GammaCorrectionByLuminance = 1.f;
      default_luma_global_game_settings.GamutExpansion = cb_luma_global_settings.GameSettings.GamutExpansion = 0.3f;
      default_luma_global_game_settings.DisplayMapping = cb_luma_global_settings.GameSettings.DisplayMapping = 1u;
      default_luma_global_game_settings.ShoulderStart = cb_luma_global_settings.GameSettings.ShoulderStart = 0.5f;
      default_luma_global_game_settings.HighlightPurity = cb_luma_global_settings.GameSettings.HighlightPurity = 1.f;
      default_luma_global_game_settings.EmissiveHDRBoost = cb_luma_global_settings.GameSettings.EmissiveHDRBoost = 0.5f;
      default_luma_global_game_settings.SunShaftHDRBoost = cb_luma_global_settings.GameSettings.SunShaftHDRBoost = 1.f;
   }

   // This needs to be overridden with your own "GameDeviceData" sub-class (destruction is automatically handled)
   void OnCreateDevice(ID3D11Device* native_device, DeviceData& device_data) override
   {
      device_data.game = new TemplateGameDeviceData;
   }

   DrawOrDispatchOverrideType OnDrawOrDispatch(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, reshade::api::shader_stage stages, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, bool is_custom_pass, bool& updated_cbuffers, std::function<void()>* original_draw_dispatch_func) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);

      // Main tonemap pass
      static uint32_t pixel_shader_hash_tonemap = Shader::Hash_StrToNum("46C50FC2");

      // Here you can track and customize shader passes by hash, you can do whatever you want in it
      if (!game_device_data.has_drawn_tonemap && original_shader_hashes.Contains(pixel_shader_hash_tonemap, reshade::api::shader_stage::pixel))
      {
         game_device_data.has_drawn_tonemap = true;

         device_data.has_drawn_main_post_processing = true;
      }

      // Luma bloom on the tonemap pass: the HDR scene at t1 feeds the pyramid, bound at t4 for this draw only when on.
      if (native_device_context->GetType() == D3D11_DEVICE_CONTEXT_IMMEDIATE && original_shader_hashes.Contains(kTonemapHash, reshade::api::shader_stage::pixel)
         && cb_luma_global_settings.GameSettings.LumaBloom != 0 && GetShaderDefineCompiledNumericalValue(TONEMAP_TYPE_HASH) >= 1)
      {
         ComPtr<ID3D11ShaderResourceView> srv_scene;
         native_device_context->PSGetShaderResources(1, 1, srv_scene.put());
         BindLumaBloom(native_device, native_device_context, device_data, srv_scene.get());
      }

      return DrawOrDispatchOverrideType::None; // Don't cancel the original draw call
   }
   void OnPresent(ID3D11Device* native_device, DeviceData& device_data) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);

      game_device_data.has_drawn_tonemap = false;
      device_data.has_drawn_main_post_processing = true;

      // LumaBloom off: give the address space back (core's DrawKarisAverage output, ~66 MB at 4K). Core only drops the UAV on swapchain init, so both views go here.
      if (cb_luma_global_settings.GameSettings.LumaBloom == 0u)
      {
         game_device_data.srv_luma_bloom.reset();
         auto& mr = device_data.managed_resources;
         mr.unordered_access_views[CompileTimeStringHash("luma_karis_average")].reset();
         mr.shader_resource_views[CompileTimeStringHash("luma_karis_average")].reset();
      }
      // Reapply borderless fullscreen if the game restored borders or the window does not cover the monitor.
      if (game_window && !IsIconic(game_window))
      {
         MONITORINFO mi = {};
         mi.cbSize = sizeof(mi);
         RECT rc_window = {};
         bool has_borders = (GetWindowLongPtr(game_window, GWL_STYLE) & (WS_CAPTION | WS_THICKFRAME | WS_BORDER)) != 0;
         bool rect_mismatch = !(GetMonitorInfo(MonitorFromWindow(game_window, MONITOR_DEFAULTTONEAREST), &mi) && GetWindowRect(game_window, &rc_window) && EqualRect(&rc_window, &mi.rcMonitor));
         if (has_borders || rect_mismatch)
            ForceBorderlessFullscreen();
      }
   }

   void LoadConfigs() override
   {
      reshade::api::effect_runtime* runtime = nullptr;

      // ReShade.ini is ready here, so load its values.
      reshade::get_config_value(runtime, NAME, "HighlightSaturation", cb_luma_global_settings.GameSettings.HighlightSaturation);
      reshade::get_config_value(runtime, NAME, "VanillaHueShift", cb_luma_global_settings.GameSettings.VanillaHueShift);
      reshade::get_config_value(runtime, NAME, "BloomStrength", cb_luma_global_settings.GameSettings.BloomStrength);
      reshade::get_config_value(runtime, NAME, "LumaBloom", cb_luma_global_settings.GameSettings.LumaBloom);
      reshade::get_config_value(runtime, NAME, "LumaBloomStrength", cb_luma_global_settings.GameSettings.LumaBloomStrength);
      reshade::get_config_value(runtime, NAME, "VanillaGlareStrength", cb_luma_global_settings.GameSettings.VanillaGlareStrength);
      reshade::get_config_value(runtime, NAME, "FilmGrainStrength", cb_luma_global_settings.GameSettings.FilmGrainStrength);
      reshade::get_config_value(runtime, NAME, "SkyHDRBoost", cb_luma_global_settings.GameSettings.SkyHDRBoost);
      reshade::get_config_value(runtime, NAME, "SkyDebanding", cb_luma_global_settings.GameSettings.SkyDebanding);
      reshade::get_config_value(runtime, NAME, "SunBrightness", cb_luma_global_settings.GameSettings.SunBrightness);
      reshade::get_config_value(runtime, NAME, "Exposure", cb_luma_global_settings.GameSettings.Exposure);
      reshade::get_config_value(runtime, NAME, "Contrast", cb_luma_global_settings.GameSettings.Contrast);
      reshade::get_config_value(runtime, NAME, "Saturation", cb_luma_global_settings.GameSettings.Saturation);
      reshade::get_config_value(runtime, NAME, "Highlights", cb_luma_global_settings.GameSettings.Highlights);
      reshade::get_config_value(runtime, NAME, "Shadows", cb_luma_global_settings.GameSettings.Shadows);
      reshade::get_config_value(runtime, NAME, "BlackFloorRemoval", cb_luma_global_settings.GameSettings.BlackFloorRemoval);
      reshade::get_config_value(runtime, NAME, "HighlightRolloff", cb_luma_global_settings.GameSettings.HighlightRolloff);
      reshade::get_config_value(runtime, NAME, "DistanceHaze", cb_luma_global_settings.GameSettings.DistanceHaze);
      reshade::get_config_value(runtime, NAME, "HighlightHuePreservation", cb_luma_global_settings.GameSettings.HighlightHuePreservation);
      reshade::get_config_value(runtime, NAME, "TopGradientStrength", cb_luma_global_settings.GameSettings.TopGradientStrength);
      reshade::get_config_value(runtime, NAME, "VignetteStrength", cb_luma_global_settings.GameSettings.VignetteStrength);
      reshade::get_config_value(runtime, NAME, "ShadowLuminanceGrading", cb_luma_global_settings.GameSettings.ShadowLuminanceGrading);
      reshade::get_config_value(runtime, NAME, "GammaCorrectionByLuminance", cb_luma_global_settings.GameSettings.GammaCorrectionByLuminance);
      reshade::get_config_value(runtime, NAME, "GamutExpansion", cb_luma_global_settings.GameSettings.GamutExpansion);
      reshade::get_config_value(runtime, NAME, "DisplayMapping", cb_luma_global_settings.GameSettings.DisplayMapping);
      reshade::get_config_value(runtime, NAME, "ShoulderStart", cb_luma_global_settings.GameSettings.ShoulderStart);
      reshade::get_config_value(runtime, NAME, "HighlightPurity", cb_luma_global_settings.GameSettings.HighlightPurity);
      reshade::get_config_value(runtime, NAME, "EmissiveHDRBoost", cb_luma_global_settings.GameSettings.EmissiveHDRBoost);
      reshade::get_config_value(runtime, NAME, "SunShaftHDRBoost", cb_luma_global_settings.GameSettings.SunShaftHDRBoost);
   }

   void DrawImGuiSettings(DeviceData& device_data) override
   {
      reshade::api::effect_runtime* runtime = nullptr;
      
      // Draw whatever ImGui to let users modify settings.
      if (ImGui::SliderFloat("Highlight Saturation", &cb_luma_global_settings.GameSettings.HighlightSaturation, 0.f, 2.f, "%.3f"))
         reshade::set_config_value(runtime, NAME, "HighlightSaturation", cb_luma_global_settings.GameSettings.HighlightSaturation);
      DrawResetButton(cb_luma_global_settings.GameSettings.HighlightSaturation, default_luma_global_game_settings.HighlightSaturation, "HighlightSaturation", runtime);

      if (ImGui::SliderFloat("Bloom Strength", &cb_luma_global_settings.GameSettings.BloomStrength, 0.f, 2.f, "%.3f"))
         reshade::set_config_value(runtime, NAME, "BloomStrength", cb_luma_global_settings.GameSettings.BloomStrength);
      DrawResetButton(cb_luma_global_settings.GameSettings.BloomStrength, default_luma_global_game_settings.BloomStrength, "BloomStrength", runtime);

      bool luma_bloom = cb_luma_global_settings.GameSettings.LumaBloom != 0;
      if (ImGui::Checkbox("Luma Bloom", &luma_bloom))
      {
         cb_luma_global_settings.GameSettings.LumaBloom = luma_bloom ? 1u : 0u;
         reshade::set_config_value(runtime, NAME, "LumaBloom", cb_luma_global_settings.GameSettings.LumaBloom);
      }
      DrawResetButton(cb_luma_global_settings.GameSettings.LumaBloom, default_luma_global_game_settings.LumaBloom, "LumaBloom", runtime);

      if (cb_luma_global_settings.GameSettings.LumaBloom != 0)
      {
         if (ImGui::SliderFloat("Luma Bloom Strength", &cb_luma_global_settings.GameSettings.LumaBloomStrength, 0.f, 4.f, "%.3f"))
            reshade::set_config_value(runtime, NAME, "LumaBloomStrength", cb_luma_global_settings.GameSettings.LumaBloomStrength);
         DrawResetButton(cb_luma_global_settings.GameSettings.LumaBloomStrength, default_luma_global_game_settings.LumaBloomStrength, "LumaBloomStrength", runtime);

         if (ImGui::SliderFloat("Game Glare/Flares with Luma Bloom", &cb_luma_global_settings.GameSettings.VanillaGlareStrength, 0.f, 1.f, "%.3f"))
            reshade::set_config_value(runtime, NAME, "VanillaGlareStrength", cb_luma_global_settings.GameSettings.VanillaGlareStrength);
         DrawResetButton(cb_luma_global_settings.GameSettings.VanillaGlareStrength, default_luma_global_game_settings.VanillaGlareStrength, "VanillaGlareStrength", runtime);
      }

      if (ImGui::SliderFloat("Film Grain Strength", &cb_luma_global_settings.GameSettings.FilmGrainStrength, 0.f, 2.f, "%.3f"))
         reshade::set_config_value(runtime, NAME, "FilmGrainStrength", cb_luma_global_settings.GameSettings.FilmGrainStrength);
      DrawResetButton(cb_luma_global_settings.GameSettings.FilmGrainStrength, default_luma_global_game_settings.FilmGrainStrength, "FilmGrainStrength", runtime);

      if (ImGui::SliderFloat("Sky HDR Boost", &cb_luma_global_settings.GameSettings.SkyHDRBoost, 0.f, 2.f, "%.3f"))
         reshade::set_config_value(runtime, NAME, "SkyHDRBoost", cb_luma_global_settings.GameSettings.SkyHDRBoost);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("Expands the brightest parts of the sky (near the sun) into HDR");
      DrawResetButton(cb_luma_global_settings.GameSettings.SkyHDRBoost, default_luma_global_game_settings.SkyHDRBoost, "SkyHDRBoost", runtime);

      bool sky_debanding = cb_luma_global_settings.GameSettings.SkyDebanding != 0;
      if (ImGui::Checkbox("Sky Debanding", &sky_debanding))
      {
         cb_luma_global_settings.GameSettings.SkyDebanding = sky_debanding ? 1u : 0u;
         reshade::set_config_value(runtime, NAME, "SkyDebanding", cb_luma_global_settings.GameSettings.SkyDebanding);
      }
      DrawResetButton(cb_luma_global_settings.GameSettings.SkyDebanding, default_luma_global_game_settings.SkyDebanding, "SkyDebanding", runtime);

      if (ImGui::SliderFloat("Sun Brightness", &cb_luma_global_settings.GameSettings.SunBrightness, 1.f, 10.f, "%.3f"))
         reshade::set_config_value(runtime, NAME, "SunBrightness", cb_luma_global_settings.GameSettings.SunBrightness);
      DrawResetButton(cb_luma_global_settings.GameSettings.SunBrightness, default_luma_global_game_settings.SunBrightness, "SunBrightness", runtime);

      if (ImGui::SliderFloat("Emissive HDR Boost", &cb_luma_global_settings.GameSettings.EmissiveHDRBoost, 0.f, 2.f, "%.3f"))
         reshade::set_config_value(runtime, NAME, "EmissiveHDRBoost", cb_luma_global_settings.GameSettings.EmissiveHDRBoost);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("Pushes the bright parts of lamps, screens and other self-lit surfaces into HDR. 0 = vanilla.");
      DrawResetButton(cb_luma_global_settings.GameSettings.EmissiveHDRBoost, default_luma_global_game_settings.EmissiveHDRBoost, "EmissiveHDRBoost", runtime);

      if (ImGui::SliderFloat("Sun Shaft HDR Boost", &cb_luma_global_settings.GameSettings.SunShaftHDRBoost, 0.f, 4.f, "%.3f"))
         reshade::set_config_value(runtime, NAME, "SunShaftHDRBoost", cb_luma_global_settings.GameSettings.SunShaftHDRBoost);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("Makes sun shafts and glare brighter near the sun, into HDR. 0 = vanilla.");
      DrawResetButton(cb_luma_global_settings.GameSettings.SunShaftHDRBoost, default_luma_global_game_settings.SunShaftHDRBoost, "SunShaftHDRBoost", runtime);

      if (ImGui::SliderFloat("Exposure", &cb_luma_global_settings.GameSettings.Exposure, 0.25f, 4.f, "%.3f"))
         reshade::set_config_value(runtime, NAME, "Exposure", cb_luma_global_settings.GameSettings.Exposure);
      DrawResetButton(cb_luma_global_settings.GameSettings.Exposure, default_luma_global_game_settings.Exposure, "Exposure", runtime);

      if (ImGui::SliderFloat("Contrast", &cb_luma_global_settings.GameSettings.Contrast, 0.5f, 2.f, "%.3f"))
         reshade::set_config_value(runtime, NAME, "Contrast", cb_luma_global_settings.GameSettings.Contrast);
      DrawResetButton(cb_luma_global_settings.GameSettings.Contrast, default_luma_global_game_settings.Contrast, "Contrast", runtime);

      if (ImGui::SliderFloat("Saturation", &cb_luma_global_settings.GameSettings.Saturation, 0.f, 2.f, "%.3f"))
         reshade::set_config_value(runtime, NAME, "Saturation", cb_luma_global_settings.GameSettings.Saturation);
      DrawResetButton(cb_luma_global_settings.GameSettings.Saturation, default_luma_global_game_settings.Saturation, "Saturation", runtime);

      if (ImGui::SliderFloat("Highlights", &cb_luma_global_settings.GameSettings.Highlights, 0.5f, 2.f, "%.3f"))
         reshade::set_config_value(runtime, NAME, "Highlights", cb_luma_global_settings.GameSettings.Highlights);
      DrawResetButton(cb_luma_global_settings.GameSettings.Highlights, default_luma_global_game_settings.Highlights, "Highlights", runtime);

      if (ImGui::SliderFloat("Shadows", &cb_luma_global_settings.GameSettings.Shadows, 0.5f, 2.f, "%.3f"))
         reshade::set_config_value(runtime, NAME, "Shadows", cb_luma_global_settings.GameSettings.Shadows);
      DrawResetButton(cb_luma_global_settings.GameSettings.Shadows, default_luma_global_game_settings.Shadows, "Shadows", runtime);

      if (ImGui::SliderFloat("Black Floor Removal", &cb_luma_global_settings.GameSettings.BlackFloorRemoval, 0.f, 1.f, "%.3f"))
         reshade::set_config_value(runtime, NAME, "BlackFloorRemoval", cb_luma_global_settings.GameSettings.BlackFloorRemoval);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("Removes the raised black level of the color grading, fading out toward mid gray (keeps shadow tint). 0 = vanilla.");
      DrawResetButton(cb_luma_global_settings.GameSettings.BlackFloorRemoval, default_luma_global_game_settings.BlackFloorRemoval, "BlackFloorRemoval", runtime);

      if (ImGui::SliderFloat("Highlight Rolloff", &cb_luma_global_settings.GameSettings.HighlightRolloff, 0.f, 1.f, "%.3f"))
         reshade::set_config_value(runtime, NAME, "HighlightRolloff", cb_luma_global_settings.GameSettings.HighlightRolloff);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("0 = grade continues into HDR highlights, 1 = vanilla SDR highlight rolloff");
      DrawResetButton(cb_luma_global_settings.GameSettings.HighlightRolloff, default_luma_global_game_settings.HighlightRolloff, "HighlightRolloff", runtime);

      if (ImGui::SliderFloat("Distance Haze", &cb_luma_global_settings.GameSettings.DistanceHaze, 0.f, 1.f, "%.3f"))
         reshade::set_config_value(runtime, NAME, "DistanceHaze", cb_luma_global_settings.GameSettings.DistanceHaze);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("Strength of the game's distance color grade (haze). 1 = vanilla");
      DrawResetButton(cb_luma_global_settings.GameSettings.DistanceHaze, default_luma_global_game_settings.DistanceHaze, "DistanceHaze", runtime);

      if (ImGui::SliderFloat("Highlight Hue Preservation", &cb_luma_global_settings.GameSettings.HighlightHuePreservation, 0.f, 1.f, "%.3f"))
         reshade::set_config_value(runtime, NAME, "HighlightHuePreservation", cb_luma_global_settings.GameSettings.HighlightHuePreservation);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("Keeps hues in bright highlights instead of per-channel shifts. 0 = vanilla");
      DrawResetButton(cb_luma_global_settings.GameSettings.HighlightHuePreservation, default_luma_global_game_settings.HighlightHuePreservation, "HighlightHuePreservation", runtime);

      if (ImGui::SliderFloat("Top Gradient", &cb_luma_global_settings.GameSettings.TopGradientStrength, 0.f, 1.f, "%.3f"))
         reshade::set_config_value(runtime, NAME, "TopGradientStrength", cb_luma_global_settings.GameSettings.TopGradientStrength);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("Strength of the warm color the game adds to the top of the screen (raises blacks there). 1 = vanilla");
      DrawResetButton(cb_luma_global_settings.GameSettings.TopGradientStrength, default_luma_global_game_settings.TopGradientStrength, "TopGradientStrength", runtime);

      if (ImGui::SliderFloat("Vignette", &cb_luma_global_settings.GameSettings.VignetteStrength, 0.f, 1.f, "%.3f"))
         reshade::set_config_value(runtime, NAME, "VignetteStrength", cb_luma_global_settings.GameSettings.VignetteStrength);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("Strength of the game's tinted vignette. 1 = vanilla");
      DrawResetButton(cb_luma_global_settings.GameSettings.VignetteStrength, default_luma_global_game_settings.VignetteStrength, "VignetteStrength", runtime);

      if (ImGui::SliderFloat("Hue Preserving Grading", &cb_luma_global_settings.GameSettings.ShadowLuminanceGrading, 0.f, 1.f, "%.3f"))
         reshade::set_config_value(runtime, NAME, "ShadowLuminanceGrading", cb_luma_global_settings.GameSettings.ShadowLuminanceGrading);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("Grades brightness by luminance and keeps hues (with the grade tint), taking saturation from the original per-channel grade (RenoDX HITMAN method). 0 = vanilla per-channel");
      DrawResetButton(cb_luma_global_settings.GameSettings.ShadowLuminanceGrading, default_luma_global_game_settings.ShadowLuminanceGrading, "ShadowLuminanceGrading", runtime);

      if (ImGui::SliderFloat("Hue Preserving Gamma", &cb_luma_global_settings.GameSettings.GammaCorrectionByLuminance, 0.f, 1.f, "%.3f"))
         reshade::set_config_value(runtime, NAME, "GammaCorrectionByLuminance", cb_luma_global_settings.GameSettings.GammaCorrectionByLuminance);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("Applies the gamma 2.0 to 2.2 correction by luminance instead of per channel (no saturation/hue shift). 0 = per channel");
      DrawResetButton(cb_luma_global_settings.GameSettings.GammaCorrectionByLuminance, default_luma_global_game_settings.GammaCorrectionByLuminance, "GammaCorrectionByLuminance", runtime);

      if (ImGui::SliderFloat("Gamut Expansion", &cb_luma_global_settings.GameSettings.GamutExpansion, 0.f, 1.f, "%.3f"))
         reshade::set_config_value(runtime, NAME, "GamutExpansion", cb_luma_global_settings.GameSettings.GamutExpansion);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("Expands already saturated colors beyond BT.709 into wide gamut, keeping hue and brightness. Neutrals and skin tones are protected. 0 = vanilla");
      DrawResetButton(cb_luma_global_settings.GameSettings.GamutExpansion, default_luma_global_game_settings.GamutExpansion, "GamutExpansion", runtime);

      int display_mapping = (int)cb_luma_global_settings.GameSettings.DisplayMapping;
      if (ImGui::Combo("Display Mapping", &display_mapping, "DICE (per channel)\0Reinhard Piecewise\0"))
      {
         cb_luma_global_settings.GameSettings.DisplayMapping = (uint32_t)display_mapping;
         reshade::set_config_value(runtime, NAME, "DisplayMapping", cb_luma_global_settings.GameSettings.DisplayMapping);
      }
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("Tone mapping of the display peak. DICE = per channel (vanilla-like). Reinhard Piecewise = by luminance with highlight desaturation control.");
      DrawResetButton(cb_luma_global_settings.GameSettings.DisplayMapping, default_luma_global_game_settings.DisplayMapping, "DisplayMapping", runtime);

      if (cb_luma_global_settings.GameSettings.DisplayMapping == 1u)
      {
         if (ImGui::SliderFloat("Shoulder Start", &cb_luma_global_settings.GameSettings.ShoulderStart, 0.1f, 0.9f, "%.3f"))
            reshade::set_config_value(runtime, NAME, "ShoulderStart", cb_luma_global_settings.GameSettings.ShoulderStart);
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Fraction of the display peak where highlight compression starts. Higher = more of the image untouched, sharper roll-off");
         DrawResetButton(cb_luma_global_settings.GameSettings.ShoulderStart, default_luma_global_game_settings.ShoulderStart, "ShoulderStart", runtime);

         if (ImGui::SliderFloat("Highlight Saturation (Display Mapping)", &cb_luma_global_settings.GameSettings.HighlightPurity, 0.f, 1.f, "%.3f"))
            reshade::set_config_value(runtime, NAME, "HighlightPurity", cb_luma_global_settings.GameSettings.HighlightPurity);
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Saturation of compressed highlights taken from a per-channel pass. 0 = pure luminance mapping (keeps saturation, may look neon), 1 = natural highlight desaturation");
         DrawResetButton(cb_luma_global_settings.GameSettings.HighlightPurity, default_luma_global_game_settings.HighlightPurity, "HighlightPurity", runtime);
      }
   }

   void PrintImGuiAbout() override
   {
      // Remember to credit Luma developers, the game mod creators, and all third party code that is used (plus, optionally, testers too)
      ImGui::Text("Hitman Absolution Luma mod - about and credits section", "");
   }
};

// This is where everything starts from, the very first call to the dll.
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
   if (ul_reason_for_call == DLL_PROCESS_ATTACH)
   {
      // Setup the globals (e.g. name etc). It's good to do this stuff before registering the ReShade addon, to make sure the names are up to date.
      const char* project_name = PROJECT_NAME;
      const char* cleared_project_name = (project_name[0] == '_') ? (project_name + 1) : project_name; // Remove the potential "_" at the beginning. This can include spaces!

      uint32_t mod_version = 1; // Increase this to reset the game settings and shader binaries after making large changes to your mod
      Globals::SetGlobals(cleared_project_name, "Hitman Absolution Luma mod", "" /*E.g. Nexus link*/, mod_version);

      // The following can be toggled in the dev settings (it generally only fully applies after changing the game's resolution).
      // Default these to "false" for the mod to not do "anything" that might cause issues with the game by default.
      swapchain_format_upgrade_type = TextureFormatUpgradesType::AllowedEnabled;
      swapchain_upgrade_type = SwapchainUpgradeType::scRGB;
      prevent_fullscreen_state = false; // HMA crashes (MSVCR100 0xc0000417) when FSE is blocked
      force_borderless = true;
      texture_format_upgrades_type = TextureFormatUpgradesType::AllowedEnabled;
      // Texture upgrades (8 bit unorm and 11 bit float etc to 16 bit float)
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

            reshade::api::format::r10g10b10a2_unorm,
            reshade::api::format::r10g10b10a2_typeless,

            //reshade::api::format::r16g16b16a16_unorm,

            reshade::api::format::r11g11b10_float,
      };
      // Set this to the size and type of the main color grading LUT you might want to upgrade (optional)
      texture_format_upgrades_lut_size = 32;
      texture_format_upgrades_lut_dimensions = LUTDimensions::_2D;
		// In case the textures failed to upgrade, tweak the filtering conditions to be more lenient (e.g. aspect ratio checks etc).
      texture_format_upgrades_2d_size_filters = 0 | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainResolution | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainAspectRatio;

      // Update samples to override the bias, based on the rendering resolution etc
      enable_samplers_upgrade = false;

#if DEVELOPMENT // If you want to track any shader names over time, you can hardcode them here by hash (they can be a useful reference in the pipeline)
      forced_shader_names.emplace(std::stoul("FD2925B4", nullptr, 16), "Tracked Shader Name");
#endif

#if !DEVELOPMENT // Put shaders that a previous version of the mod used but has ever since been deleted here, so that users updating the mod from an older version won't accidentally load them
      old_shader_file_names.emplace("Bloom_0xDC9373A8.ps_5_0.hlsl");
#endif

      // Create your game sub-class instance (it will be automatically destroyed on exit).
      // You do not need to do this if you have no custom data to store.
      game = new TemplateGame();
   }

   CoreMain(hModule, ul_reason_for_call, lpReserved);

   return TRUE;
}
