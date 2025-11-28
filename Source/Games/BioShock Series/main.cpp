#define GAME_BIOSHOCK_SERIES 1

#include "..\..\Core\core.hpp"

#include "BS2_CrashFix\BS2_CrashFix.h"

#include <MinHook.h>

namespace
{
   ShaderHashesList pixel_shader_hashes_Bloom;
   ShaderHashesList pixel_shader_hashes_Tonemap;
   ShaderHashesList pixel_shader_hashes_AA;
   ShaderHashesList pixel_shader_hashes_depth_copy;
   ShaderHashesList shader_hashes_Fog;
   ShaderHashesList compute_shader_hashes_AO_main_pass;
   ShaderHashesList compute_shader_hashes_AO_denoise_pass1;
   ShaderHashesList compute_shader_hashes_AO_denoise_pass2;

   enum class BioShockGame
   {
      BioShock_Remastered,
      BioShock_2_Remastered,
      BioShock_Infinite,
   };

   BioShockGame bioshock_game = BioShockGame::BioShock_Remastered;

   // XeGTAO
   constexpr size_t XE_GTAO_DEPTH_MIP_LEVELS = 5;
   constexpr UINT XE_GTAO_NUMTHREADS_X = 8;
   constexpr UINT XE_GTAO_NUMTHREADS_Y = 8;
   bool g_xegtao_enable = true;

#if DEVELOPMENT
   bool g_show_raw_ao = false;
#endif

   bool g_smaa_enable = true;

   // User settings:
   bool enable_luts_normalization = true; // TODO: try it (in BS2 luts are written on the CPU, they might be raised?)

#if DEVELOPMENT
   bool fix_bloom_samplers = true;
#else
   constexpr bool fix_bloom_samplers = true;
#endif

   bool sent_aa_assert = false;

   bool crash_fix_applied = false;

   // Original from pcgw. Unknown author.
   // New version from Ersh.
   const std::vector<uint8_t> bs2_weapon_fov_pattern = { 0xD9, 0x80, 0x88, 0x04, 0x00, 0x00, 0xD9, 0x5C, 0x24, 0x10 };
   std::vector<std::byte*> bs2_weapon_fov_matches;
   constexpr float bs2_default_fov = 75.f; // Common/default value...
   float bs2_last_written_fov = bs2_default_fov;

   // TODO: do this with a trampoline that reads the original value and converts it on the fly in code,
   // instead of reading the original value every frame and constantly changing the code to scale properly.
   // Remove minhook if this isn't done.
   void PatchBS2Ultrawide(float target_aspect_ratio)
   {
      // The game's weapon (first person model) scaled Vert- instead of Hor+.
      // The code was reading a game's global CVAR for the weapon FoV that was
      // often changed by gameplay (e.g. when aiming down sight).
      // Early mods replaced the default FoV of 75 with a fixed matching scaled FoV for your aspect ratio, however that's not ideal,
      // given that the scaling wouldn't match anymore when aiming down sight and the base value changes.
      // Note that this might be one frame late, however only the scaling distance from the current FoV to 75 is one frame late, and won't be a problem.
      // We only patch beyond 16:9 as below it should already look correct.
      if (bs2_weapon_fov_matches.size() == 1)
      {
         // Read the value
         float current_fov = bs2_default_fov;

#if 0 // TODO: finish this... the FoV float cvar offset is relative to the current EAX, which we have no access to from here (it'd depend on the thread etc apparently), this might not be a problem with a trampoline? For now fall back to a fixed patching method (we could actually do this in the swapchain resize function...)
         // Offset is little-endian dword at address + 2
         uint32_t offset;
         std::memcpy(&offset, bs2_weapon_fov_matches[0] + 2, sizeof(offset));

         // Compute address of float
         HMODULE module_handle = GetModuleHandle(nullptr); // Handle to the current executable
         std::byte* base = reinterpret_cast<std::byte*>(module_handle);
         float* fov_ptr = reinterpret_cast<float*>(base + offset);

         // Optionally check memory protection if you want to be safe
         MEMORY_BASIC_INFORMATION mbi;
         if (VirtualQuery(fov_ptr, &mbi, sizeof(mbi)) != sizeof(mbi) || !(mbi.Protect & (PAGE_READWRITE | PAGE_READONLY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE))) return;

         current_fov = *fov_ptr;

         current_fov = std::clamp(current_fov, 0.01f, 179.99f); // Extra safety against garbage values or in case the game had not set the cvar
#endif

         // Don't patch below 16:9
         float adjusted_fov = ScaleHorizontalFOV(current_fov, false, 16.f / 9.f, max(target_aspect_ratio, 16.f / 9.f));

#if DEVELOPMENT // Just for test, no need to expose this to users unless they massively increased the scene fov
         static float fov_scale = 1.f;
#else
         constexpr float fov_scale = 1.f;
#endif
         if (fov_scale != 1.f)
            adjusted_fov = ScaleHorizontalFOV(adjusted_fov, false, 1.f, fov_scale);

         // Nothing to do, avoid re-writing the memory when not needed
         if (adjusted_fov == bs2_last_written_fov) return;

         bs2_last_written_fov = adjusted_fov; // Do so even if we failed to write below, given there wouldn't be much point in trying again

         std::vector<uint8_t> bs2_weapon_fov_patch = { 0xC7, 0x44, 0x24, 0x10, 0x00, 0x00, 0x96, 0x42, 0x90, 0x90 }; // Defaults to 75

         // Write the new fov in the float part
         std::memcpy(&bs2_weapon_fov_patch[4], &adjusted_fov, sizeof(adjusted_fov));

         // Restore the original Vert- code below 16:9 given that's how it should be
         if (target_aspect_ratio <= (16.f / 9.f))
         {
            bs2_weapon_fov_patch = bs2_weapon_fov_pattern;
         }

         DWORD old_protect;
         BOOL success = VirtualProtect(bs2_weapon_fov_matches[0], bs2_weapon_fov_patch.size(), PAGE_EXECUTE_READWRITE, &old_protect);
         if (success)
         {
            std::memcpy(bs2_weapon_fov_matches[0], bs2_weapon_fov_patch.data(), bs2_weapon_fov_patch.size());

            DWORD temp_protect;
            VirtualProtect(bs2_weapon_fov_matches[0], bs2_weapon_fov_patch.size(), old_protect, &temp_protect);
         }
         ASSERT_ONCE(success);
      }
   }
}

struct GameDeviceDataBioshockSeries final : public GameDeviceData
{
   bool drew_tonemap = false;
   bool drew_aa = false;

   // BS/BS2/Infinite fog
   com_ptr<ID3D11Texture2D> scene_texture;
   com_ptr<ID3D11ShaderResourceView> scene_texture_srv;

#if DEVELOPMENT
   com_ptr<ID3D11ShaderResourceView> srv_raw_ao;
#endif

    com_ptr<ID3D11ShaderResourceView> srv_depth;
};

class BioshockSeries final : public Game
{
public:
   static GameDeviceDataBioshockSeries& GetGameDeviceData(DeviceData& device_data)
   {
      return *static_cast<GameDeviceDataBioshockSeries*>(device_data.game);
   }

   void OnInit(bool async) override
   {
      std::vector<ShaderDefineData> game_shader_defines_data = {
         {"TONEMAP_TYPE", '1', true, false, "0 - SDR: Vanilla\n2 - SDR/HDR: Vanilla+\n3 - HDR: Untonemapped"},
         {"ALLOW_AA", '1', true, false, "Allows disabling the game's FXAA implementation", 1},
         {"ENABLE_LUMA", '1', true, false, "Allow disabling the mod's improvements to the game's look", 1},
         {"ENABLE_IMPROVED_BLOOM", '1', true, false, "Reduces the excessive bloom's pixelation due to usage of nearest neighbor texture sampling in the original shaders", 1},
         {"ENABLE_LUT_EXTRAPOLATION", '1', true, false, "Use Luma's signature technique for expanding Color Grading LUTs from SDR to HDR", 1},
         {"ENABLE_COLOR_GRADING", '1', true, false, "Allows disabling the color grading LUT (some other color filters might still get applied)", 1},
         {"DISABLE_BLACK_BARS", '0', true, false,
         "Disable broken black bars in UltraWide, given that they only cover an horizontal part of the screen, leaving the vertical view visible.\nThis will remove them from 16:9 too", 1}, // TODO: WIP
         {"LUT_SAMPLING_ERROR_EMULATION_MODE", '1', true, false,
         "BioShock 2 Remastered had a bug in the color grading shader that accidentally boosted contrast and clipped both shadow and highlight."
         "\nLuma fixes that, however without the bug shadows are fairly raised, so this attempts to emulate the error without clipping detail."
         "\nMode 2 and 3 are alternative looks, use them if you prefer.", 3},
         {"DEFAULT_GAMMA_RAMP_EMULATION_MODE", '0', true, false,
         "BioShock 2 Remastered used a deprecated Windows XP API to change the display brightness, Luma disables that as it doesn't work in HDR,"
         "\nhowever the default brightness value was not neutral but was a gamma power of 1.2, and thus made everything brighter."
         "\nThe calibration menu looked calibrated with gamma set to ~1, and the mod does that by default,"
         "\nhowever it's arguable whether the default value or the calibrated value is the most accurate one, so pick what you want."
         "\nWe are only applying this correction to the game scene and not the UI, as it doesn't seem necessary there."
         "\nMode 1 and 2 are different ways or applying the gamma correction, so pick what you prefer.", 2},
         {"XE_GTAO_QUALITY", '2', true, false, "0 - Low\n1 - Medium\n2 - High\n3 - Very High\n4 - Ultra", 4},
      };
      shader_defines_data.append_range(game_shader_defines_data);

      // SMAA
      native_shaders_definitions.emplace(CompileTimeStringHash("SMAA Linear To sRGB CS"), ShaderDefinition("Luma_SMAA_LinearTosRGB_CS", reshade::api::pipeline_subobject_type::compute_shader));

      // Other games don't need this. BS2 uses "SetDeviceGammaRamp" and defaulted to 1.2. BS1 uses "IDXGIOutput::SetGammaControl" but defaults to a neutral value. BSI doesn't seem to use either.
      if (bioshock_game != BioShockGame::BioShock_2_Remastered)
      {
         GetShaderDefineData(char_ptr_crc32("LUT_SAMPLING_ERROR_EMULATION_MODE")).SetDefaultValue('0');
         GetShaderDefineData(char_ptr_crc32("LUT_SAMPLING_ERROR_EMULATION_MODE")).SetValueFixed(true);
      }
      GetShaderDefineData(POST_PROCESS_SPACE_TYPE_HASH).SetDefaultValue('0');
      GetShaderDefineData(VANILLA_ENCODING_TYPE_HASH).SetDefaultValue('1');
      GetShaderDefineData(GAMMA_CORRECTION_TYPE_HASH).SetDefaultValue('1');
      GetShaderDefineData(UI_DRAW_TYPE_HASH).SetDefaultValue('2');
      GetShaderDefineData(GAMUT_MAPPING_TYPE_HASH).SetDefaultValue('1'); // Enable it, especially given the fog correction generating wild colors

      GetShaderDefineData(TEST_SDR_HDR_SPLIT_VIEW_MODE_NATIVE_IMPL_HASH).SetDefaultValue('1'); // The game just clipped, so HDR is an extension of SDR (except for some shaders that we adjust)

      if (bioshock_game == BioShockGame::BioShock_Infinite)
      {
         // XeGTAO
         native_shaders_definitions.emplace(CompileTimeStringHash("BSI XeGTAO Prefilter Depths"), ShaderDefinition("Luma_BSI_XeGTAO_impl", reshade::api::pipeline_subobject_type::compute_shader, nullptr, "prefilter_depths16x16_cs"));
         native_shaders_definitions.emplace(CompileTimeStringHash("BSI XeGTAO Main Pass"), ShaderDefinition("Luma_BSI_XeGTAO_impl", reshade::api::pipeline_subobject_type::compute_shader, nullptr, "main_pass_cs"));
         native_shaders_definitions.emplace(CompileTimeStringHash("BSI XeGTAO Denoise Pass"), ShaderDefinition("Luma_BSI_XeGTAO_impl", reshade::api::pipeline_subobject_type::compute_shader, nullptr, "denoise_pass_cs"));
         
#if DEVELOPMENT
         native_shaders_definitions.emplace(CompileTimeStringHash("Show AO"), ShaderDefinition("Luma_ShowAO", reshade::api::pipeline_subobject_type::pixel_shader));
#endif

      }
   }

   void OnLoad(std::filesystem::path& file_path, bool failed) override
   {
      if (!failed)
      {
         if (bioshock_game == BioShockGame::BioShock_2_Remastered)
         {
            HMODULE module_handle = GetModuleHandle(nullptr); // Handle to the current executable
            auto dos_header = reinterpret_cast<PIMAGE_DOS_HEADER>(module_handle);
            auto nt_headers = reinterpret_cast<PIMAGE_NT_HEADERS>(reinterpret_cast<std::byte*>(module_handle) + dos_header->e_lfanew);

            std::byte* base = reinterpret_cast<std::byte*>(module_handle);
            std::size_t section_size = nt_headers->OptionalHeader.SizeOfImage;

            bs2_weapon_fov_matches = System::ScanMemoryForPattern(base, section_size, bs2_weapon_fov_pattern);

            if (bs2_weapon_fov_matches.size() != 1)
            {
               reshade::log::message(reshade::log::level::warning, "BioShock 2 Remastered Luma failed to patch for Ultrawide compatibility. If you have already patched the executable, restore the original for proper dynamic Hor+ FoV on Weapons");
            }
         }

         if (bioshock_game == BioShockGame::BioShock_Remastered || bioshock_game == BioShockGame::BioShock_2_Remastered)
         {
            reshade::register_event<reshade::addon_event::init_resource>(BioshockSeries::OnInitResource);
         }
      }
   }

   void LoadConfigs() override
   {
      reshade::api::effect_runtime* runtime = nullptr;
      reshade::get_config_value(runtime, NAME, "FogCorrectionIntensity", cb_luma_global_settings.GameSettings.FogCorrectionIntensity);
      reshade::get_config_value(runtime, NAME, "FogIntensity", cb_luma_global_settings.GameSettings.FogIntensity);
      reshade::get_config_value(runtime, NAME, "BloomIntensity", cb_luma_global_settings.GameSettings.BloomIntensity);
      reshade::get_config_value(runtime, NAME, "BloomRadius", cb_luma_global_settings.GameSettings.BloomRadius);
      reshade::get_config_value(runtime, NAME, "XeGTAOEnable", g_xegtao_enable);
      reshade::get_config_value(runtime, NAME, "SMAAEnable", g_smaa_enable);
      // "device_data.cb_luma_global_settings_dirty" should already be true at this point
   }

   void DrawImGuiSettings(DeviceData& device_data) override
   {
      reshade::api::effect_runtime* runtime = nullptr;

      ImGui::NewLine();

      if (bioshock_game == BioShockGame::BioShock_2_Remastered)
      {
         if (allow_disabling_gamma_ramp)
         {
            HDC hDC = GetDC(game_window); // Pass NULL to get the DC for the entire screen (NULL = desktop, primary display, the gamma ramp only ever applies to that apparently)
            WORD gamma_ramp[3][256];
            bool neutral_gamma_ramp = true;
            if (GetDeviceGammaRamp(hDC, gamma_ramp) == TRUE)
            {
               for (int i = 1; i < 255; i++)
                  neutral_gamma_ramp &= (gamma_ramp[0][i] == i * 257) && (gamma_ramp[1][i] == i * 257) && (gamma_ramp[2][i] == i * 257);
            }
            ReleaseDC(game_window, hDC);

            if (!neutral_gamma_ramp)
            {
               ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 200, 0, 255)); // yellow/orange
               ImGui::TextUnformatted("Warning: The game uses an old Windows XP library to change the image gamma. This doesn't work well in HDR and only causes issues.\n"
                  "Either press the \"Reset Gamma Ramp\" button here every time the game window goes in focus, or set your GPU driver settings to force reference mode (if available by your GPU vendor).\n"
                  "The best alternative is to set \"Gamma\" to \"1\" in the \"Bioshock2SP.ini\" config file, under the \"ShockGame.ShockUserSettings\" section.\n"
                  "The mod is intended to be played with a neutral gamma as it's designed around that assumption.");
               ImGui::PopStyleColor();
               ImGui::NewLine();
            }
         }

         // TODO: re-implement Fog fixes in BS1 after fixing the fog shader errors
         if (ImGui::SliderFloat("Fog Correction Intensity", &cb_luma_global_settings.GameSettings.FogCorrectionIntensity, 0.f, 1.f))
            reshade::set_config_value(runtime, NAME, "FogCorrectionIntensity", cb_luma_global_settings.GameSettings.FogCorrectionIntensity);
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Fog was \"additive\" in this game, which caused severely raised blacks, this preserves the feel of the fog without destroying contrast, making the game more atmospheric.\nNote that this also applies to underwater sections.\nSet it to 0 for the a Vanilla experience, however it will not look good on OLED.");
         DrawResetButton(cb_luma_global_settings.GameSettings.FogCorrectionIntensity, default_luma_global_game_settings.FogCorrectionIntensity, "FogCorrectionIntensity", runtime);

         if (ImGui::SliderFloat("Fog Intensity", &cb_luma_global_settings.GameSettings.FogIntensity, 0.f, 2.f))
            reshade::set_config_value(runtime, NAME, "FogIntensity", cb_luma_global_settings.GameSettings.FogIntensity);
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("You can decrease or increase fog to your liking.");
         DrawResetButton(cb_luma_global_settings.GameSettings.FogIntensity, default_luma_global_game_settings.FogIntensity, "FogIntensity", runtime);

         if (ImGui::SliderFloat("Bloom Radius", &cb_luma_global_settings.GameSettings.BloomRadius, 0.f, 1.f))
            reshade::set_config_value(runtime, NAME, "BloomRadius", cb_luma_global_settings.GameSettings.BloomRadius);
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("The bloom radius is arguably too wide for modern resolutions, customize it to your liking. 1 is the Vanilla value.");
         DrawResetButton(cb_luma_global_settings.GameSettings.BloomRadius, default_luma_global_game_settings.BloomRadius, "BloomRadius", runtime);
      }

#if DEVELOPMENT
      ImGui::Checkbox("Show Raw AO", &g_show_raw_ao);
#endif

      if (ImGui::Checkbox("XeGTAO enable", &g_xegtao_enable))
        reshade::set_config_value(runtime, NAME, "XeGTAOEnable", g_xegtao_enable);

      if (ImGui::Checkbox("SMAA enable", &g_smaa_enable))
         reshade::set_config_value(runtime, NAME, "SMAAEnable", g_smaa_enable);

      if (ImGui::SliderFloat("Bloom Intensity", &cb_luma_global_settings.GameSettings.BloomIntensity, 0.f, 2.f))
         reshade::set_config_value(runtime, NAME, "BloomIntensity", cb_luma_global_settings.GameSettings.BloomIntensity);
      DrawResetButton(cb_luma_global_settings.GameSettings.BloomIntensity, default_luma_global_game_settings.BloomIntensity, "BloomIntensity", runtime);

#if 0 // TODO
      ImGui::NewLine();

      // This isn't serialized because it could cause issues/confusion if it's enabled on boot
      ImGui::Checkbox("Hide Gameplay UI", &hide_gameplay_ui);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      {
         ImGui::SetTooltip("Hides the whole UI outside of the main menu\nWARNING: this can cause confusion and isn't perfect (everything is hidden, even some non gameplay UI and Menus)");
      }
      DrawResetButton<decltype(hide_gameplay_ui), false>(hide_gameplay_ui, false, "Hide Gameplay UI", runtime);

      ImGui::NewLine();

      if (ImGui::TreeNode("Camera Mode"))
      {
         ImGui::Checkbox("Enable", &enable_camera_mode);
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         {
            ImGui::SetTooltip("Basic Camera Mode. Pause the game and enable it to be able to move the camera around and take screenshots. Works during in engine cutscenes too.\nWARNING: if you rotate the camera backwards, some geometry might not render");
         }
         DrawResetButton<decltype(enable_camera_mode), false>(enable_camera_mode, false, "Enable Camera Mode", runtime);

         ImGui::BeginDisabled(!enable_camera_mode);

         ImGui::SliderFloat3("Translation", &camera_mode_translation.x, -1.f, 1.f);
         DrawResetButton<decltype(camera_mode_translation), false>(camera_mode_translation, {}, "Translation", runtime);
         ImGui::SliderFloat3("Rotation", &camera_mode_rotation.x, -M_PI, M_PI);
         DrawResetButton<decltype(camera_mode_rotation), false>(camera_mode_rotation, {}, "Rotation", runtime);
         ImGui::SliderFloat("FoV Scale", &camera_mode_fov_scale, 0.1f, 10.f);
         DrawResetButton<decltype(camera_mode_fov_scale), false>(camera_mode_fov_scale, 1.f, "FoV Scale", runtime);

         ImGui::EndDisabled();

         ImGui::TreePop();
      }
#endif
   }

#if DEVELOPMENT
   void DrawImGuiDevSettings(DeviceData& device_data) override
   {
      if (ImGui::Checkbox("Fix Bloom Samplers", &fix_bloom_samplers));
   }
#endif

   void PrintImGuiAbout() override
   {
      ImGui::Text("Luma for \"BioShock Remastered\", \"BioShock 2 Remastered\" and \"BioShock Infinite\" is developed by Pumbo and is open source and free.\nIf you enjoy it, consider donating.\n"
         "The \"BioShock 2 Remastered\" mod comes bundled with the \"Crash Fix\" mod by \"gir489\", which fixes multiple crashes with the game, and with an Ultrawide FoV fix for the first person models.", "");

      const auto button_color = ImGui::GetStyleColorVec4(ImGuiCol_Button);
      const auto button_hovered_color = ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered);
      const auto button_active_color = ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive);
      ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(70, 134, 0, 255));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(70 + 9, 134 + 9, 0, 255));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(70 + 18, 134 + 18, 0, 255));
      static const std::string donation_link_pumbo = std::string("Buy Pumbo a Coffee on buymeacoffee ") + std::string(ICON_FK_OK);
      if (ImGui::Button(donation_link_pumbo.c_str()))
      {
         system("start https://buymeacoffee.com/realfiloppi");
      }
      static const std::string donation_link_pumbo_2 = std::string("Buy Pumbo a Coffee on ko-fi ") + std::string(ICON_FK_OK);
      if (ImGui::Button(donation_link_pumbo_2.c_str()))
      {
         system("start https://ko-fi.com/realpumbo");
      }
      ImGui::PopStyleColor(3);

      ImGui::NewLine();
      // Restore the previous color, otherwise the state we set would persist even if we popped it
      ImGui::PushStyleColor(ImGuiCol_Button, button_color);
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, button_hovered_color);
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, button_active_color);
#if 0
      static const std::string mod_link = std::string("Nexus Mods Page ") + std::string(ICON_FK_SEARCH);
      if (ImGui::Button(mod_link.c_str()))
      {
         system("start https://www.nexusmods.com/prey2017/mods/149");
      }
#endif
      static const std::string social_link = std::string("Join our \"HDR Den\" Discord ") + std::string(ICON_FK_SEARCH);
      if (ImGui::Button(social_link.c_str()))
      {
         // Unique link for Luma by Pumbo (to track the origin of people joining), do not share for other purposes
         static const std::string obfuscated_link = std::string("start https://discord.gg/J9fM") + std::string("3EVuEZ");
         system(obfuscated_link.c_str());
      }
      static const std::string contributing_link = std::string("Contribute on Github ") + std::string(ICON_FK_FILE_CODE);
      if (ImGui::Button(contributing_link.c_str()))
      {
         system("start https://github.com/Filoppi/Luma-Framework");
      }
      ImGui::PopStyleColor(3);

      ImGui::NewLine();
      ImGui::Text("Credits:"
         "\n\nMain:"
         "\nPumbo"

         "\n\nContributors:"
         "\nErsh"
         "\nGaramond"

         "\n\nThird Party:"
         "\nReShade"
         "\nImGui"
         "\nRenoDX"
         "\n3Dmigoto"
         "\nDXVK"
         "\ngir489 (BioShock 2 Remastered Crash Fix)"
         "\nOklab"
         "\nDICE (HDR tonemapper)"
         "\nGTAO (Intel)"
         "\nSMAA"
         , "");
   }

   void OnCreateDevice(ID3D11Device* native_device, DeviceData& device_data) override
   {
      device_data.game = new GameDeviceDataBioshockSeries;
   }

   void OnInitSwapchain(reshade::api::swapchain* swapchain) override
   {
      auto& device_data = *swapchain->get_device()->get_private_data<DeviceData>();

      cb_luma_global_settings.GameSettings.OutputRes.x = device_data.output_resolution.x;
      cb_luma_global_settings.GameSettings.OutputRes.y = device_data.output_resolution.y;
      cb_luma_global_settings.GameSettings.InvOutputRes.x = 1.f / device_data.output_resolution.x;
      cb_luma_global_settings.GameSettings.InvOutputRes.y = 1.f / device_data.output_resolution.y;
      device_data.cb_luma_global_settings_dirty = true;
   }

   DrawOrDispatchOverrideType OnDrawOrDispatch(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, reshade::api::shader_stage stages, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, bool is_custom_pass, bool& updated_cbuffers, std::function<void()>* original_draw_dispatch_func) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);

      if (original_shader_hashes.Contains(pixel_shader_hashes_depth_copy))
      {
         // RT should be r32f.
         com_ptr<ID3D11RenderTargetView> rtv;
         native_device_context->OMGetRenderTargets(1, &rtv, nullptr);
         com_ptr<ID3D11Resource> resource;
         rtv->GetResource(&resource);
         game_device_data.srv_depth.reset();
         native_device->CreateShaderResourceView(resource.get(), nullptr, &game_device_data.srv_depth);
      }

      if (original_shader_hashes.Contains(compute_shader_hashes_AO_main_pass))
      {
         if (g_xegtao_enable)
         {
            // Original binds:
            // t0 - depth - r32f
            // t1 - normal - rgba8unorm
            // u0 - out - rgba8unorm
            // s0 - point wrap

            // Backup the Out UAV and the Normal SRV since we will overide them,
            // before we will need them. 
            com_ptr<ID3D11UnorderedAccessView> uav_original;
            native_device_context->CSGetUnorderedAccessViews(0, 1, &uav_original);
            com_ptr<ID3D11ShaderResourceView> srv_normal;
            native_device_context->CSGetShaderResources(1, 1, &srv_normal);

            // XeGTAOPrefilterDepths16x16 pass
            //

            D3D11_TEXTURE2D_DESC tex_desc = {};
            tex_desc.Width = device_data.output_resolution.x;
            tex_desc.Height = device_data.output_resolution.y;
            tex_desc.MipLevels = XE_GTAO_DEPTH_MIP_LEVELS;
            tex_desc.ArraySize = 1;
            tex_desc.Format = DXGI_FORMAT_R32_FLOAT;
            tex_desc.SampleDesc.Count = 1;
            tex_desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;

            // Create prefilter depths views.
            com_ptr<ID3D11Texture2D> tex;
            native_device->CreateTexture2D(&tex_desc, nullptr, &tex);
            std::array<ID3D11UnorderedAccessView*, XE_GTAO_DEPTH_MIP_LEVELS> uav_prefilter_depths;
            D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
            uav_desc.Format = tex_desc.Format;
            uav_desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
            for (int i = 0; i < uav_prefilter_depths.size(); ++i)
            {
               uav_desc.Texture2D.MipSlice = i;
               native_device->CreateUnorderedAccessView(tex.get(), &uav_desc, &uav_prefilter_depths[i]);
            }
            com_ptr<ID3D11ShaderResourceView> srv_prefilter_depths;
            native_device->CreateShaderResourceView(tex.get(), nullptr, &srv_prefilter_depths);

            // Bindings.
            native_device_context->CSSetShader(device_data.native_compute_shaders[CompileTimeStringHash("BSI XeGTAO Prefilter Depths")].get(), nullptr, 0);
            native_device_context->CSSetUnorderedAccessViews(0, uav_prefilter_depths.size(), uav_prefilter_depths.data(), nullptr);

            native_device_context->Dispatch((tex_desc.Width + 16 - 1) / 16, (tex_desc.Height + 16 - 1) / 16, 1);

            // Unbind UAVs and release uav_prefilter_depths.
            static constexpr std::array<ID3D11UnorderedAccessView*, uav_prefilter_depths.size()> uav_nulls_prefilter_depths_pass = {};
            native_device_context->CSSetUnorderedAccessViews(0, uav_nulls_prefilter_depths_pass.size(), uav_nulls_prefilter_depths_pass.data(), nullptr);
            for (int i = 0; i < uav_prefilter_depths.size(); ++i)
            {
               uav_prefilter_depths[i]->Release();
            }

            //

            // XeGTAOMainPass pass
            //

            // Create AO term and Edges views.
            tex_desc.Format = DXGI_FORMAT_R8G8_UNORM;
            tex_desc.MipLevels = 1;
            tex.reset();
            native_device->CreateTexture2D(&tex_desc, nullptr, &tex);
            com_ptr<ID3D11UnorderedAccessView> uav_main_pass;
            native_device->CreateUnorderedAccessView(tex.get(), nullptr, &uav_main_pass);
            com_ptr<ID3D11ShaderResourceView> srv_main_pass;
            native_device->CreateShaderResourceView(tex.get(), nullptr, &srv_main_pass);

            // Bindings.
            native_device_context->CSSetShader(device_data.native_compute_shaders[CompileTimeStringHash("BSI XeGTAO Main Pass")].get(), nullptr, 0);
            const std::array srvs_main_pass = { srv_prefilter_depths.get(), srv_normal.get() };
            native_device_context->CSSetShaderResources(0, srvs_main_pass.size(), srvs_main_pass.data());
            const std::array uavs_main_pass = { uav_main_pass.get() };
            native_device_context->CSSetUnorderedAccessViews(0, uavs_main_pass.size(), uavs_main_pass.data(), nullptr);

            native_device_context->Dispatch((tex_desc.Width + XE_GTAO_NUMTHREADS_X - 1) / XE_GTAO_NUMTHREADS_X, (tex_desc.Height + XE_GTAO_NUMTHREADS_Y - 1) / XE_GTAO_NUMTHREADS_Y, 1);

            //

            // XeGTAODenoisePass pass
            //
            // Doing only one XeGTAODenoisePass pass (as last/final pass) correspond to "Denoising level: Sharp" from the XeGTAO demo.
            //

            // Bindings.
            const std::array uavs_denoise_pass = { uav_original.get() };
            native_device_context->CSSetUnorderedAccessViews(0, uavs_denoise_pass.size(), uavs_denoise_pass.data(), nullptr);
            native_device_context->CSSetShader(device_data.native_compute_shaders[CompileTimeStringHash("BSI XeGTAO Denoise Pass")].get(), nullptr, 0);
            const std::array srvs_denoise_pass = { srv_main_pass.get() };
            native_device_context->CSSetShaderResources(0, srvs_denoise_pass.size(), srvs_denoise_pass.data());

            native_device_context->Dispatch((tex_desc.Width + (XE_GTAO_NUMTHREADS_X * 2) - 1) / (XE_GTAO_NUMTHREADS_X * 2), (tex_desc.Height + XE_GTAO_NUMTHREADS_Y - 1) / XE_GTAO_NUMTHREADS_Y, 1);

            //

            return DrawOrDispatchOverrideType::Replaced;
         }

         return DrawOrDispatchOverrideType::None;
      }

      if (g_xegtao_enable && original_shader_hashes.Contains(compute_shader_hashes_AO_denoise_pass1))
      {
         return DrawOrDispatchOverrideType::Skip;
      }

      if (original_shader_hashes.Contains(compute_shader_hashes_AO_denoise_pass2))
      {
#if DEVELOPMENT
         // This is for both XeGTAO and the original AO.
         if (g_show_raw_ao)
         {
            com_ptr<ID3D11UnorderedAccessView> uav;
            native_device_context->CSGetUnorderedAccessViews(0, 1, &uav);
            com_ptr<ID3D11Resource> resource;
            uav->GetResource(&resource);
            game_device_data.srv_raw_ao.reset();
            native_device->CreateShaderResourceView(resource.get(), nullptr, &game_device_data.srv_raw_ao);
         }
#endif

         if (g_xegtao_enable)
         {
            return DrawOrDispatchOverrideType::Skip;
         }
         return DrawOrDispatchOverrideType::None;
      }

      // Copy the scene and feed it to the additive fog shader, so we can pre-blend with the background in the customized shader, without raising blacks etc
      // TODO: skip this if fog correction is at 0%, and branch in the shader to not read the background.
      if (is_custom_pass && !game_device_data.drew_tonemap && original_shader_hashes.Contains(shader_hashes_Fog))
      {
         com_ptr<ID3D11RenderTargetView> rtv;
         native_device_context->OMGetRenderTargets(1, &rtv, nullptr);

         if (rtv)
         {
            com_ptr<ID3D11Resource> rtr;
            rtv->GetResource(&rtr);

            uint4 size_a, size_b;
            DXGI_FORMAT format_a, format_b;
            GetResourceInfo(game_device_data.scene_texture.get(), size_a, format_a);
            GetResourceInfo(rtr.get(), size_b, format_b);
            if (size_a != size_b || format_a != format_b)
            {
               game_device_data.scene_texture = CloneTexture<ID3D11Texture2D>(native_device, rtr.get(), DXGI_FORMAT_UNKNOWN, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET, D3D11_BIND_UNORDERED_ACCESS, false, false);
               game_device_data.scene_texture_srv = nullptr;
               if (game_device_data.scene_texture)
               {
                  HRESULT hr = native_device->CreateShaderResourceView(game_device_data.scene_texture.get(), nullptr, &game_device_data.scene_texture_srv);
                  ASSERT_ONCE(SUCCEEDED(hr));
               }
            }

            native_device_context->CopyResource(game_device_data.scene_texture.get(), rtr.get());

#if DEVELOPMENT
            const std::shared_lock lock_trace(s_mutex_trace);
            if (trace_running)
            {
               const std::unique_lock lock_trace_2(cmd_list_data.mutex_trace);
               TraceDrawCallData trace_draw_call_data;
               trace_draw_call_data.type = TraceDrawCallData::TraceDrawCallType::Custom;
               trace_draw_call_data.command_list = native_device_context;
               trace_draw_call_data.custom_name = "Copy Fog Background";
               cmd_list_data.trace_draw_calls_data.insert(cmd_list_data.trace_draw_calls_data.end() - 1, trace_draw_call_data);
            }
#endif

            // Note: we could try to just set the original RTV as SRV as well, given that the same pixel is first being read and then written, there should be no conflicts and it should avoid copies, however that's illegal in DX
            ID3D11ShaderResourceView* const scene_texture_srv_const = game_device_data.scene_texture_srv.get();
            native_device_context->PSSetShaderResources(1, 1, &scene_texture_srv_const);
         }

         return DrawOrDispatchOverrideType::None;
      }

      // Bloom
      if (is_custom_pass && !game_device_data.drew_tonemap && original_shader_hashes.Contains(pixel_shader_hashes_Bloom) && fix_bloom_samplers)
      {
         // Bloom used a nearest neighbor sampler, which made no sense and made it pixelated
         ID3D11SamplerState* const sampler_state_linear = device_data.sampler_state_linear.get();
         native_device_context->PSSetSamplers(0, 1, &sampler_state_linear);

         return DrawOrDispatchOverrideType::None;
      }

      // Tonemapper
      if (!game_device_data.drew_tonemap && original_shader_hashes.Contains(pixel_shader_hashes_Tonemap))
      {
         if (g_smaa_enable)
         {
            // SRV0 should be the scene in linear color space.
            com_ptr<ID3D11ShaderResourceView> srv;
            native_device_context->PSGetShaderResources(0, 1, &srv);

            // Get the scene resource and texture description from SRV.
            com_ptr<ID3D11Resource> resource;
            srv->GetResource(&resource);
            com_ptr<ID3D11Texture2D> tex;
            resource->QueryInterface(&tex);
            D3D11_TEXTURE2D_DESC tex_desc;
            tex->GetDesc(&tex_desc);

            // LinearTosRGB pass
            //
            // In addition we will copy SRV.
            //

            // Create UAs and views.
            tex_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
            tex.reset();
            native_device->CreateTexture2D(&tex_desc, nullptr, &tex);
            com_ptr<ID3D11UnorderedAccessView> uav_linear_to_gamma;
            native_device->CreateUnorderedAccessView(tex.get(), nullptr, &uav_linear_to_gamma);
            com_ptr<ID3D11ShaderResourceView> srv_linear_to_gamma;
            native_device->CreateShaderResourceView(tex.get(), nullptr, &srv_linear_to_gamma);
            tex.reset();
            native_device->CreateTexture2D(&tex_desc, nullptr, &tex);
            com_ptr<ID3D11UnorderedAccessView> uav_copy;
            native_device->CreateUnorderedAccessView(tex.get(), nullptr, &uav_copy);
            com_ptr<ID3D11ShaderResourceView> srv_copy;
            native_device->CreateShaderResourceView(tex.get(), nullptr, &srv_copy);

            // Bindings.
            static constexpr ID3D11ShaderResourceView* srv_null = nullptr;
            native_device_context->PSSetShaderResources(0, 1, &srv_null);
            const std::array srvs = { srv.get() };
            native_device_context->CSSetShaderResources(0, srvs.size(), srvs.data());
            const std::array cs_uavs_linear_to_gamma = { uav_copy.get(), uav_linear_to_gamma.get() };
            native_device_context->CSSetUnorderedAccessViews(0, cs_uavs_linear_to_gamma.size(), cs_uavs_linear_to_gamma.data(), nullptr);
            native_device_context->CSSetShader(device_data.native_compute_shaders[CompileTimeStringHash("SMAA Linear To sRGB CS")].get(), nullptr, 0);

            native_device_context->Dispatch((tex_desc.Width + 8 - 1) / 8, (tex_desc.Height + 8 - 1) / 8, 1);

            // Unbind UAVs and SRVs.
            static constexpr std::array<ID3D11UnorderedAccessView*, 2> uav_nulls = {};
            native_device_context->CSSetUnorderedAccessViews(0, uav_nulls.size(), uav_nulls.data(), nullptr);
            native_device_context->CSSetShaderResources(0, 1, &srv_null);

            //

            native_device_context->PSSetShaderResources(0, srvs.size(), srvs.data());

            // Create RTV to the scenes's resource.
            com_ptr<ID3D11RenderTargetView> rtv;
            native_device->CreateRenderTargetView(resource.get(), nullptr, &rtv);

            DrawSMAA(native_device, native_device_context, device_data, rtv.get(), srv_copy.get(), srv_linear_to_gamma.get(), game_device_data.srv_depth.get());
         }

         game_device_data.drew_tonemap = true;
      }

      // FXAA (run at the end after DoF too, which is questionable, but whatever)
      if (!game_device_data.drew_aa && original_shader_hashes.Contains(pixel_shader_hashes_AA))
      {
         game_device_data.drew_aa = true;
      }
      // TODO: make sure all shaders that run after FXAA are UI? Cuz tonemapping is there

      return DrawOrDispatchOverrideType::None;
   }

   void OnPresent(ID3D11Device* native_device, DeviceData& device_data) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);

#if DEVELOPMENT
      if (g_show_raw_ao)
      {
         com_ptr<ID3D11DeviceContext> ctx;
         native_device->GetImmediateContext(&ctx);

         // Create backbuffer RTV.
         auto backbuffer = (ID3D11Resource*)*device_data.back_buffers.begin();
         com_ptr<ID3D11RenderTargetView> rtv_backbuffer;
         native_device->CreateRenderTargetView(backbuffer, nullptr, &rtv_backbuffer);

         // Back up the original and set the new primitive topology. 
         D3D11_PRIMITIVE_TOPOLOGY primitive_topology_original;
         ctx->IAGetPrimitiveTopology(&primitive_topology_original);
         ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

         // Bindings.
         const std::array rtvs = { rtv_backbuffer.get() };
         ctx->OMSetRenderTargets(rtvs.size(), rtvs.data(), nullptr);
         ctx->VSSetShader(device_data.native_vertex_shaders[CompileTimeStringHash("Copy VS")].get(), nullptr, 0);
         ctx->PSSetShader(device_data.native_pixel_shaders[CompileTimeStringHash("Show AO")].get(), nullptr, 0);
         const std::array srvs = { game_device_data.srv_raw_ao.get() };
         ctx->PSSetShaderResources(0, srvs.size(), srvs.data());

         ctx->Draw(4, 0);

         // Restore the orginal primitive topology.
         ctx->IASetPrimitiveTopology(primitive_topology_original);
      }
#endif

      if (game_device_data.drew_tonemap && !game_device_data.drew_aa && !sent_aa_assert)
      {
         sent_aa_assert = true;
         // NOTE: this crashes in FSE if we pass in the game window handle
         MessageBoxA(NULL, "Luma detected that Anti-Aliasing (FXAA) is disabled in the game's settings, please re-enable it for tonemapping and UI brightness scaling to work properly.\nUse \"ALLOW_AA\" in Advanced Settings to force AA off.", NAME, MB_SETFOREGROUND);
      }

      game_device_data.drew_tonemap = false;
      game_device_data.drew_aa = false;

      if (bioshock_game == BioShockGame::BioShock_2_Remastered)
      {
         // Do it on present as this is almost guaranteed to be a place where the render thread has stopped and isn't reading that code,
         // which presumably was only related to projection matrices so read while building the command lists.
         // ReShade addons are loaded and unloaded multiple times on boot by the game, but present is only called after the final load.
         float target_aspect_ratio = device_data.output_resolution.x / device_data.output_resolution.y;
         PatchBS2Ultrawide(target_aspect_ratio);
      }

      if (!crash_fix_applied)
      {
         crash_fix_applied = true; // Apply, or try to apply, once

         // This only works on Steam (unless proven otherwise, the exe might be identical in the Epic store version)
         if (bioshock_game == BioShockGame::BioShock_2_Remastered && GetModuleHandle(TEXT("steam_api.dll")) != NULL)
         {
            bool skip_crash_fix = false;
            reshade::get_config_value(nullptr, NAME, "SkipCrashFix", skip_crash_fix);
            // In case it ever caused issues, or the game was updated, allow skipping it
            if (!skip_crash_fix)
            {
               // We load this a bit late, in the first present, as that's guaranteed to happen after the DXGI dll was loaded and unloaded multiple times (or anyway, ReShade reloaded our Addon multiple times),
               // the main reason is that the patches can't be uninstalled and keep game functions pointing a memory that would then have been unloaded, so it'd crash if the game called any between an addon unload and load.
               ApplyCrashFix();
            }
         }
      }
   }

   static void OnInitResource(reshade::api::device* device, const reshade::api::resource_desc& desc, const reshade::api::subresource_data* initial_data, reshade::api::resource_usage initial_state, reshade::api::resource resource)
   {
      if (desc.texture.format == reshade::api::format::r24_unorm_x8_uint)
      {
         auto& device_data = *device->get_private_data<DeviceData>();
         auto& game_device_data = GetGameDeviceData(device_data);
         auto native_device = (ID3D11Device*)device->get_native();
         game_device_data.srv_depth.reset();
         native_device->CreateShaderResourceView((ID3D11Resource*)resource.handle, nullptr, &game_device_data.srv_depth);
      }
   }
};

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
   if (ul_reason_for_call == DLL_PROCESS_ATTACH)
   {
      Globals::SetGlobals(PROJECT_NAME, "BioShock Remastered + BioShock 2 Remastered + BioShock Infinite Luma mod");
      Globals::VERSION = 1;

      const std::string executable_name = System::GetProcessExecutableName();
      if (executable_name == "BioshockHD.exe")
      {
         bioshock_game = BioShockGame::BioShock_Remastered;
         sub_game_shaders_appendix = "BS";
      }
      else if (executable_name == "Bioshock2HD.exe")
      {
         bioshock_game = BioShockGame::BioShock_2_Remastered;
         sub_game_shaders_appendix = "BS2";
      }
      // Steam and Epic Store versions respectively
      else if (executable_name == "BioShockInfinite.exe" || executable_name == "ShippingPC-XGame.exe")
      {
         bioshock_game = BioShockGame::BioShock_Infinite;
         sub_game_shaders_appendix = "BSI";
         Globals::DEVELOPMENT_STATE = Globals::ModDevelopmentState::WorkInProgress;
      }
      else
      {
         // TODO: handle case and serialzie the answer next to the exe name to avoid re-showing it?
         MessageBoxA(NULL, "Unknown BioShock game", NAME, MB_SETFOREGROUND);
#if 0 // Won't work with either dynamic or static loading
         //#pragma comment(lib, "Comctl32.lib") // Makes ReShade fail to load the addon
         TASKDIALOG_BUTTON buttons[] =
         {
             { 1, L"BioShock Remastered" },
             { 2, L"BioShock 2 Remastered" },
             { 3, L"BioShock Infinite" }
         };

         TASKDIALOGCONFIG config = { 0 };
         config.cbSize = sizeof(config);
         config.hwndParent = nullptr;
         config.dwFlags = TDF_USE_COMMAND_LINKS;
         config.dwCommonButtons = 0;
         config.pszWindowTitle = L"Select BioShock Game";
         config.pszMainInstruction = L"Luma could not identify the BioShock game, please pick the your BioShock game:";
         config.pButtons = buttons;
         config.cButtons = ARRAYSIZE(buttons);

         int pressed_button = 0;
         if (SUCCEEDED(TaskDialogIndirect(&config, &pressed_button, nullptr, nullptr)))
         {
            switch (pressed_button)
            {
            case 1: bioshock_game = BioShockGame::BioShock_Remastered; break;
            case 2: bioshock_game = BioShockGame::BioShock_2_Remastered; break;
            case 3: bioshock_game = BioShockGame::BioShock_Infinite; break;
            }
         }
#endif
      }

      luma_settings_cbuffer_index = 13;
      luma_data_cbuffer_index = 12;

      swapchain_format_upgrade_type = TextureFormatUpgradesType::AllowedEnabled;
      swapchain_upgrade_type = SwapchainUpgradeType::scRGB;
      texture_format_upgrades_type = TextureFormatUpgradesType::AllowedEnabled;
      // TODO: check if BSI need R11G11B10F to R16G16B16A16F upgrades. BS(1) needs 8bit etc?. Mix the mods in BFI? UseLowPrecisionColorBuffer=False FloatingPointRenderTargets=True
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

            reshade::api::format::r11g11b10_float, // Used by Bloom and maybe lens flare stuff

            reshade::api::format::r32g32b32a32_float, // BSI uses 32bpc render targets for the scene and it's insane! So we restore them to 16bpc // TODO: verify it's not due to config edits
      };
      if (bioshock_game == BioShockGame::BioShock_Infinite)
      {
         enable_indirect_texture_format_upgrades = true; // TODO: try
         enable_automatic_indirect_texture_format_upgrades = true;
      }
      texture_format_upgrades_2d_size_filters = 0 | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainResolution | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainAspectRatio;
      enable_upgraded_texture_resource_copy_redirection = true; // TODO: investigate more if it happens (thus disabled in dev mode)

      // The game does very funky stuff when trying to go FSE, even if we block it (it re-creates a lot of shaders and textures)
      force_borderless = true;

      allow_disabling_gamma_ramp = true; // For Bioshock 2 // TODO: call this every frame or anyway when alt tabbing in, as the game constantly resets it. Or when we get window focus, or go FSE. Or just patch the function out... Or ask users to force reference mode.

      // Default values
      default_luma_global_game_settings.FogCorrectionIntensity = 1.f; // 0 is vanilla. Values between 0.75 and 1 as great defaults.
      default_luma_global_game_settings.FogIntensity = 1.f;
      default_luma_global_game_settings.BloomIntensity = bioshock_game == BioShockGame::BioShock_Infinite ? 1.f : 0.8f; // TODO: test in BSI
      default_luma_global_game_settings.BloomRadius = 0.75f; // 1 is vanilla, however it was too big and looks too blurry
      cb_luma_global_settings.GameSettings = default_luma_global_game_settings;

#if DEVELOPMENT
      forced_shader_names.emplace(std::stoul("0A8EC2D3", nullptr, 16), "Clear");
      forced_shader_names.emplace(std::stoul("77BD2E9F", nullptr, 16), "BS2_Bloom"); // Defined here as we don't override the PS
      // TODO: BS2 First person mask shaders: 0xB06C4F2A 0xA62CF959
#endif

      if (bioshock_game == BioShockGame::BioShock_Remastered)
      {
         pixel_shader_hashes_Tonemap.pixel_shaders = { Shader::Hash_StrToNum("6457104F") };
      }
      else if (bioshock_game == BioShockGame::BioShock_2_Remastered)
      {
         pixel_shader_hashes_Bloom.pixel_shaders = { Shader::Hash_StrToNum("37852042"), Shader::Hash_StrToNum("8F640A18"), Shader::Hash_StrToNum("2D9142B2"), Shader::Hash_StrToNum("77BD2E9F") };
         pixel_shader_hashes_Tonemap.pixel_shaders = { Shader::Hash_StrToNum("6F92E3E3") };
         shader_hashes_Fog.pixel_shaders.emplace(std::stoul("FC0B307B", nullptr, 16));
      }
      else if (bioshock_game == BioShockGame::BioShock_Infinite)
      {
         pixel_shader_hashes_Tonemap.pixel_shaders = { Shader::Hash_StrToNum("29D570D8") };
         pixel_shader_hashes_AA.pixel_shaders = { Shader::Hash_StrToNum("27BD2A2E"), Shader::Hash_StrToNum("5CDD5AB1") }; // Different qualities
         pixel_shader_hashes_depth_copy.pixel_shaders = { Shader::Hash_StrToNum("496E549B") };
         compute_shader_hashes_AO_main_pass.compute_shaders = { Shader::Hash_StrToNum("1E7B9941"), Shader::Hash_StrToNum("348372D0") }; // High and Ultra quality
         compute_shader_hashes_AO_denoise_pass1.compute_shaders = { Shader::Hash_StrToNum("F6ED18D8") };
         compute_shader_hashes_AO_denoise_pass2.compute_shaders = { Shader::Hash_StrToNum("BA9A4DB1") };
      }
      // Shared between games
      if (bioshock_game == BioShockGame::BioShock_Remastered || bioshock_game == BioShockGame::BioShock_2_Remastered)
      {
         pixel_shader_hashes_AA.pixel_shaders = { Shader::Hash_StrToNum("EC834D82") };
      }

      game = new BioshockSeries();
   }
   else if (ul_reason_for_call == DLL_PROCESS_DETACH)
   {
      if (crash_fix_applied)
      {
         CloseCrashFix();
      }
   }

   CoreMain(hModule, ul_reason_for_call, lpReserved);

   return TRUE;
}
