#define GAME_NEEDFORSPEEDRIVALS 1

#define DISABLE_AUTO_DEBUGGER 1
// #define ENABLE_SMAA 1

#include "..\..\Core\core.hpp"


namespace UserSettings
{
   template<typename T>
   struct Val
   {
      T curr;
      T def;
      void Init(const T& v) { curr = def = v; }
      explicit operator T&() { return curr; }
      explicit operator const T&() const { return curr; }
      Val& operator=(const T& v) { curr = v; return *this; }
   };
   static Val<bool> ui;
   static Val<float> miplodbias;
   static Val<float> bloom;
   
   void OnInit()
   {
      ui.Init(true);
      miplodbias.Init(-0.25f);
      bloom.Init(1.f);
      
      default_luma_global_game_settings.Bloom = cb_luma_global_settings.GameSettings.Bloom = 1.f;
      default_luma_global_game_settings.WhiteClip = cb_luma_global_settings.GameSettings.WhiteClip = 1.f;
      default_luma_global_game_settings.HighlightSat = cb_luma_global_settings.GameSettings.HighlightSat = 1.f;
   }

   void LoadConfigs(reshade::api::effect_runtime* runtime)
   {
      reshade::get_config_value(runtime, NAME, "UI", ui.curr);
      reshade::get_config_value(runtime, NAME, "MipLodBias", miplodbias.curr);
      
      reshade::get_config_value(runtime, NAME, "Bloom", bloom.curr);
      reshade::get_config_value(runtime, NAME, "WhiteClip", cb_luma_global_settings.GameSettings.WhiteClip);
      reshade::get_config_value(runtime, NAME, "HighlightSat", cb_luma_global_settings.GameSettings.HighlightSat);
   }
}

namespace ShaderDefines
{
   constexpr uint32_t TEST_USER_PEAK_FXAA = char_ptr_crc32("TEST_USER_PEAK_FXAA");
   constexpr uint32_t TONEMAP_DOF = char_ptr_crc32("TONEMAP_DOF");
   constexpr uint32_t TONEMAP_UVDISTORT = char_ptr_crc32("TONEMAP_UVDISTORT");
   constexpr uint32_t TONEMAP_VIGNETTE = char_ptr_crc32("TONEMAP_VIGNETTE");
   constexpr uint32_t TONEMAP_FILMGRAIN = char_ptr_crc32("TONEMAP_FILMGRAIN");
   constexpr uint32_t TONEMAP_LUT = char_ptr_crc32("TONEMAP_LUT");
   constexpr uint32_t TONEMAP_FXAA = char_ptr_crc32("TONEMAP_FXAA");
   
   void OnInit()
   {
      static const std::vector<ShaderDefineData> game_shader_defines_data = {
         {"GAMMA_CORRECTION_RANGE_TYPE", '0', true, true, "0 - Full range.\n1 - 0-1 only.", 1},
         {"TEST_USER_PEAK_FXAA", '0', true, false, "Show white rectangles for peak test.", 1},
         {"TONEMAP_DOF", '1', true, false, "Enable Depth of Field in tonemapping.", 1},
         {"TONEMAP_UVDISTORT", '1', true, false, "Enable UV Distortion in tonemapping.", 1},
         {"TONEMAP_VIGNETTE", '1', true, false, "Enable Vignette in tonemapping.", 1},
         {"TONEMAP_FILMGRAIN", '1', true, false, "Enable Film Grain in tonemapping.", 1},
         {"TONEMAP_LUT", '1', true, false, "Enable LUT in tonemapping.", 1},
         {"TONEMAP_FXAA", '1', true, false, "Enable FXAA.", 2},
      };
      shader_defines_data.append_range(game_shader_defines_data);

      GetShaderDefineData(POST_PROCESS_SPACE_TYPE_HASH).SetDefaultValue('0');
      GetShaderDefineData(GAMMA_CORRECTION_TYPE_HASH).SetDefaultValue('0');
      GetShaderDefineData(UI_DRAW_TYPE_HASH).SetDefaultValue('2');

      auto_recompile_defines = true;
   }
   
   static char InvertCharBool(char b)
   {
      return b == '0' ? '1' : '0'; 
   }
   
   static int Get(uint32_t p)
   {
      auto* d = &GetShaderDefineData(p);
      return d->editable_data.value[0] - '0';
   }

   static bool GetBool(uint32_t p)
   {
      return Get(p) > 0;
   }

   static void Set(uint32_t p, char c)
   {
      auto* d = &GetShaderDefineData(p);
      if (d->editable_data.value[0] == c) return;
      d->SetValue(c);
      defines_need_recompilation = true;
   }
   
   static void Set(uint32_t p, int i)
   {
      auto* d = &GetShaderDefineData(p);
      char c = static_cast<char>(i + '0');
      Set(p, c);
   }

   static void Set(uint32_t p, bool b)
   {
      int i = b ? 1 : 0;
      Set(p, i);
   }

   static void ToggleBool(uint32_t p)
   {
      auto* d = &GetShaderDefineData(p);
      d->SetValue(InvertCharBool(d->editable_data.value[0]));
      defines_need_recompilation = true;
   }

   static void UIResetButton(uint32_t p)
   {
      auto* d = &GetShaderDefineData(p);
      if (d->editable_data.value[0] != d->default_data.value[0]) {
         int id = static_cast<int>(reinterpret_cast<uintptr_t>(d));
         ImGui::PushID(id);
         ImGui::SameLine();
         if (ImGui::SmallButton(ICON_FK_UNDO))
         {
            d->Reset();
            defines_need_recompilation = true;
         }
         ImGui::PopID();
      }
   }

   static bool UIToggleCheckmark(uint32_t d, const char* label, const char* tooltip)
   {
      bool def = GetBool(d);
      
      ImGui::PushID(std::string(label).append("_").append(std::to_string(d)).c_str());
      bool c = ImGui::Checkbox(label, &def);
      ImGui::PopID();

      if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip(tooltip);
      
      if (c) ToggleBool(d);
      
      UIResetButton(d);
      return def;
   }
      
   static int UIDropDown(uint32_t d, const char* label, const char* const items[], const char* tooltip)
   {
      int def = Get(d);
      bool c = ImGui::Combo(label, &def, items, IM_ARRAYSIZE(items));
      if (c) Set(d, def);
      if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip(tooltip);
      UIResetButton(d);
      return def;
   }

   static int UIDropDown(uint32_t d, const char* label, std::initializer_list<const char*> items_list, const char* tooltip)
   {
      std::vector<const char*> items(items_list);
      int def = Get(d);
      ImGui::PushID(std::string(label).append("_").append(std::to_string(d)).c_str());
      bool c = ImGui::Combo(label, &def, items.data(), static_cast<int>(items.size()));
      ImGui::PopID();
      if (c) Set(d, def);
      if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip(tooltip);
      UIResetButton(d);
      return def;
   }
}

struct GameDeviceDataNeedForSpeedRivals final : public GameDeviceData
{
   struct DrawnState
   {
      // bool depth = false;
      bool fxaa = false;
      bool ui = false;
   } drawn;
   void ResetDrawnState() { drawn = DrawnState{}; }
};

namespace Website
{
   void OpenWebsite(const char* url) {
#if defined(_WIN32) || defined(_WIN64)
      std::string command = "start " + std::string(url);
      std::system(command.c_str());
#elif defined(__linux__)
      std::string command = "xdg-open " + std::string(url);
      std::system(command.c_str());
#elif defined(__APPLE__)
      std::string command = "open " + std::string(url);
      std::system(command.c_str());
#endif
   }
}

class GameNeedForSpeedRivals final : public Game
{
public:
   void OnInit(bool async) override
   {
      // log
      message(reshade::log::level::info, "OnInit()");

      // UserSettings
      UserSettings::OnInit();
      
      // Shader Defines
      ShaderDefines::OnInit();

      // Indirect Upgrade by Shader Hash: Tonemap output, FXAA input
      auto_texture_format_upgrade_shader_hashes[0xB06EC0CD] = std::pair{ std::vector<uint8_t>{ 0 }, std::vector<uint8_t>() }; //t000
      auto_texture_format_upgrade_shader_hashes[0xDF795FB5] = std::pair{ std::vector<uint8_t>{ 0 }, std::vector<uint8_t>() }; //t001
      auto_texture_format_upgrade_shader_hashes[0x80B1ADCC] = std::pair{ std::vector<uint8_t>{ 0 }, std::vector<uint8_t>() }; //t002
      auto_texture_format_upgrade_shader_hashes[0xD2D57404] = std::pair{ std::vector<uint8_t>{ 0 }, std::vector<uint8_t>() }; //t003
      
      // CB Indices
      luma_settings_cbuffer_index = 13;
      luma_data_cbuffer_index = 12;
   }

   DrawOrDispatchOverrideType OnDrawOrDispatch(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, reshade::api::shader_stage stages, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, bool is_custom_pass, bool& updated_cbuffers, std::function<void()>* original_draw_dispatch_func) override
   {
      auto* game_device_data = static_cast<GameDeviceDataNeedForSpeedRivals*>(device_data.game);

      static uint64_t ps = 0;
      static uint64_t ps_prev = 0;
      ps_prev = ps;
      ps = original_shader_hashes.pixel_shaders[0];

      // // case: linearize depth
      // if (!game_device_data->drawn.depth && ps == 0x7A55670A)
      // {
      //    game_device_data->drawn.depth = true;
      //
      //    // get rtv 0 depth
      //    native_device_context->OMGetRenderTargets(1, device_data.managed_resources.render_target_views["depth"_h].put(), nullptr);
      //    device_data.managed_resources.render_target_views["depth"_h].get()->GetResource(device_data.managed_resources.resources["depth"_h].put());
      // }

      // case: FXAA
      if (!game_device_data->drawn.fxaa && ps == 0xDACCCB84)
      {
         game_device_data->drawn.fxaa = true;
         game_device_data->drawn.ui = true;

#if DEVELOPMENT
         // create flag file for prev_ps
         static std::unordered_set<uint64_t> seen_ps;
         if (!seen_ps.contains(ps_prev))
         {
            seen_ps.insert(ps_prev);
            reshade::log::message(reshade::log::level::info, std::format("New tonemap shader hash 0x{:016X}", ps_prev).c_str());
         }
#endif
         
         return DrawOrDispatchOverrideType::None;
      }
      
      // case: UI
      if (!UserSettings::ui && game_device_data->drawn.ui)
      {
         return DrawOrDispatchOverrideType::Skip;
      }

      return DrawOrDispatchOverrideType::None;
   }

   void OnPresent(ID3D11Device* native_device, DeviceData& device_data) override
   {
      auto* game_device_data = static_cast<GameDeviceDataNeedForSpeedRivals*>(device_data.game);
      
      // Reset drawn state
      game_device_data->ResetDrawnState();
      
      // u_miplodbias
      ignore_upgraded_samplers = UserSettings::miplodbias.curr >= 0.f;
      device_data.texture_mip_lod_bias_offset = UserSettings::miplodbias.curr;
   }

   void LoadConfigs() override
   {
      reshade::api::effect_runtime* runtime = nullptr;

      // UserSettings
      UserSettings::LoadConfigs(runtime);
      
      // custom_sdr_gamma
      reshade::get_config_value(runtime, NAME, "custom_sdr_gamma", custom_sdr_gamma);
      ShaderDefines::Set(GAMMA_CORRECTION_TYPE_HASH, custom_sdr_gamma > 0);
      defines_need_recompilation = true;
   }

   void DrawImGuiSettings(DeviceData& device_data) override
   {
      reshade::api::effect_runtime* runtime = nullptr;
      
      // Gamma Correction
      if (ImGui::CollapsingHeader("Gamma Correction"))
      {
         // Info
         ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 1.f, 0.75f, 1.f));
         ImGui::TextWrapped("[SDR/HDR Gamma Mismatch]");
         ImGui::PopStyleColor();
         ImGui::Bullet(); ImGui::SameLine(); ImGui::TextWrapped("Reintroduce gamma mismatch to lower shadows, perhaps intended by original grade.");

         if (ImGui::Button("Further Explanation & Test (Google Slides)"))
            Website::OpenWebsite("https://docs.google.com/presentation/d/e/2PACX-1vSXeLHlbm6repcS7fels1-SXYGRmzziRrnuJ8nDO8J5rsWV3dT1-nVyCKp0Tj_stwx-9qlCI-N6rYIT/pub?start=false&loop=false&slide=id.g3e007eafba8_0_0");

         ImGui::Separator();
         
         // Dropdown for custom_sdr_gamma
         const char* const items[] = { "Off (sRGB)", "Match SDR (2.2)", "Stronger (2.4)" };
         int custom_sdr_gamma_index = 0;
         if (custom_sdr_gamma > 0) { //detect
            if (abs(custom_sdr_gamma - 2.2f) < 0.001f) custom_sdr_gamma_index = 1;
            else if (custom_sdr_gamma == 2.4f) custom_sdr_gamma_index = 2;
         }
         ImGui::PushID("GammaCorrection custom_sdr_gamma");
         if (ImGui::Combo("Correction", &custom_sdr_gamma_index, items, IM_ARRAYSIZE(items))) //user set & save
         {
            switch (custom_sdr_gamma_index)
            {
            default: custom_sdr_gamma = 0.f; break;
            case 1: custom_sdr_gamma = 2.2f; break;
            case 2: custom_sdr_gamma = 2.4f; break;
            }
            ShaderDefines::Set(GAMMA_CORRECTION_TYPE_HASH, custom_sdr_gamma > 0);
            defines_need_recompilation = true;
            reshade::set_config_value(runtime, NAME, "custom_sdr_gamma", custom_sdr_gamma);
         }
         ImGui::PopID();
      }

      // Brightness
      if (ImGui::CollapsingHeader("HDR Brightness"))
      {
         ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 1.f, 0.75f, 1.f));
         ImGui::TextWrapped("[Don't Exceed Display Maximum]");
         ImGui::PopStyleColor();
         
         ImGui::Bullet(); ImGui::SameLine(); ImGui::TextWrapped("Use default Luma sliders above!");

         auto prev = ShaderDefines::Get(ShaderDefines::TEST_USER_PEAK_FXAA);
         ShaderDefines::UIToggleCheckmark(ShaderDefines::TEST_USER_PEAK_FXAA, "Test Pattern (Read Tooltip)", "3 rectangles inside a big one.\n- Left should disappear. (2x Peak)\n- Middle should be barely visible. (Peak)\n- Right should be easy to see. (0.5x Peak)");
         if (prev != ShaderDefines::Get(ShaderDefines::TEST_USER_PEAK_FXAA)) UserSettings::ui = ShaderDefines::Get(ShaderDefines::TEST_USER_PEAK_FXAA) == 0;

         ImGui::Separator();

         if (ImGui::SliderFloat("White Clip", &cb_luma_global_settings.GameSettings.WhiteClip, 0.f, 2.f, "%.3f"))
            reshade::set_config_value(runtime, NAME, "WhiteClip", cb_luma_global_settings.GameSettings.WhiteClip);
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Increase to straighten the rolloff, causing more clipping.");
         DrawResetButton(cb_luma_global_settings.GameSettings.WhiteClip, default_luma_global_game_settings.WhiteClip, "WhiteClip", runtime);

         if (ImGui::SliderFloat("Highlights Saturation", &cb_luma_global_settings.GameSettings.HighlightSat, 0.f, 2.f, "%.3f"))
            reshade::set_config_value(runtime, NAME, "HighlightSat", cb_luma_global_settings.GameSettings.HighlightSat);
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("How much of raw punch through the blown out color grade?\n0 is most like intended SDR, but makes HDR highlights too white.");
         DrawResetButton(cb_luma_global_settings.GameSettings.HighlightSat, default_luma_global_game_settings.HighlightSat, "HighlightSat", runtime);
      }

      // Misc.
      if (ImGui::CollapsingHeader("Miscellaneous"))
      {
         if (ImGui::SliderFloat("Bloom", &cb_luma_global_settings.GameSettings.Bloom, 0.f, 2.f, "%.3f"))
            reshade::set_config_value(runtime, NAME, "Bloom", cb_luma_global_settings.GameSettings.Bloom);
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Bloom multiplier.");
         DrawResetButton(cb_luma_global_settings.GameSettings.Bloom, default_luma_global_game_settings.Bloom, "Bloom", runtime);
         
         ShaderDefines::UIToggleCheckmark(ShaderDefines::TONEMAP_DOF, "Depth of Field", "Allows Depth of Field pass to apply.");
         ShaderDefines::UIToggleCheckmark(ShaderDefines::TONEMAP_UVDISTORT, "Lens Distortion", "Texture coords distortion when sampling color.");
         ShaderDefines::UIToggleCheckmark(ShaderDefines::TONEMAP_VIGNETTE, "Vignette", "Enables Vignette.");
         ShaderDefines::UIToggleCheckmark(ShaderDefines::TONEMAP_LUT, "Color Grading (Debug)", "Enables Color Grading via Look Up Texture.\n(Disabling this shows the naked input color.)");
         ShaderDefines::UIToggleCheckmark(ShaderDefines::TONEMAP_FILMGRAIN, "Film Grain", "Enables Film Grain.");
         ShaderDefines::UIDropDown(ShaderDefines::TONEMAP_FXAA, "FXAA", {"Off", "On", "Extreme"}, "Enables FXAA pass.");

         if (ImGui::SliderFloat("Mip LOD Bias", &UserSettings::miplodbias.curr, 0.f, -1.f, "%.3f"))
            reshade::set_config_value(runtime, NAME, "u_miplodbias", UserSettings::miplodbias.curr);
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Negative bias boosts texture sharpness at a distance,\nthough too much and this game becomes coarse.");
         DrawResetButton(UserSettings::miplodbias.curr, UserSettings::miplodbias.def, "u_miplodbias", runtime);

         ImGui::Checkbox("Draw UI", &UserSettings::ui.curr);
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Enable/Disable the UI rendering.");
      }

      if (DEVELOPMENT) ImGui::Separator();
   }

   void PrintImGuiAbout() override
   {
      ImGui::Text("Build Date:");
      ImGui::Text(__DATE__);
      ImGui::Text(__TIME__);
      ImGui::NewLine();
   }
};

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
   if (ul_reason_for_call == DLL_PROCESS_ATTACH)
   {
      Globals::SetGlobals(PROJECT_NAME, "Need For Speed Rivals Luma mod");
      Globals::VERSION = 1;

      swapchain_format_upgrade_type  = TextureFormatUpgradesType::AllowedEnabled;
      swapchain_upgrade_type         = SwapchainUpgradeType::scRGB;
      
      texture_format_upgrades_type   = TextureFormatUpgradesType::AllowedEnabled;
      texture_upgrade_formats = {};
      texture_format_upgrades_2d_size_filters = 0;
      enable_chain_indirect_texture_format_upgrades = ChainTextureFormatUpgradesType::DirectDependencies;

      enable_samplers_upgrade = true;

      game = new GameNeedForSpeedRivals();
   }

   CoreMain(hModule, ul_reason_for_call, lpReserved);

   return TRUE;
}