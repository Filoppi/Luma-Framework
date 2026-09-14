#define GAME_GENERIC 1

#define DISABLE_AUTO_DEBUGGER 1

#define DISABLE_FOCUS_LOSS_SUPPRESSION 1

#define CHECK_GRAPHICS_API_COMPATIBILITY 1

#define AVOID_INPUT_LOSS 1

#include "..\..\Core\core.hpp"

namespace
{
   std::set<reshade::api::format> toggleable_texture_upgrade_formats;
}

struct GenericGameDeviceData final : public GameDeviceData
{
};

// Generic mod that should work in many games out of the box, configurable at runtime.
// Texture upgrades and linearized scRGB HDR should work. An SDR to HDR upgrade can be done at the end.
class GenericGame final : public Game
{
   static GenericGameDeviceData& GetGameDeviceData(DeviceData& device_data)
   {
      return *static_cast<GenericGameDeviceData*>(device_data.game);
   }

public:
   void OnInit(bool async) override
   {
      luma_settings_cbuffer_index = 13;
      luma_data_cbuffer_index = 12;
   }

   void OnCreateDevice(ID3D11Device* native_device, DeviceData& device_data) override
   {
      device_data.game = new GenericGameDeviceData;
   }

   DrawOrDispatchOverrideType OnDrawOrDispatch(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, reshade::api::shader_stage stages, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, bool is_custom_pass, bool& updated_cbuffers, std::function<void()>* original_draw_dispatch_func) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);

      return DrawOrDispatchOverrideType::None; // Don't cancel the original draw call
   }
   void OnPresent(ID3D11Device* native_device, DeviceData& device_data) override
   {
   }

   void DrawImGuiSettings(DeviceData& device_data) override
   {
      reshade::api::effect_runtime* runtime = nullptr;

      ImGui::NewLine();
      // TODO: hide these in development given they are already shown in the dev settings?
      // Requires a change in resolution to (~fully) apply (no texture cloning yet)
      if (swapchain_format_upgrade_type > TextureFormatUpgradesType::None)
      {
         if (swapchain_format_upgrade_type == TextureFormatUpgradesType::AllowedEnabled ? ImGui::Button("Disable Swapchain Upgrade") : ImGui::Button("Enable Swapchain Upgrade"))
         {
            swapchain_format_upgrade_type = swapchain_format_upgrade_type == TextureFormatUpgradesType::AllowedEnabled ? TextureFormatUpgradesType::AllowedDisabled : TextureFormatUpgradesType::AllowedEnabled;
         }
      }
      if (texture_format_upgrades_type > TextureFormatUpgradesType::None)
      {
         if (texture_format_upgrades_type == TextureFormatUpgradesType::AllowedEnabled ? ImGui::Button("Disable Texture Format Upgrades") : ImGui::Button("Enable Texture Format Upgrades"))
         {
            texture_format_upgrades_type = texture_format_upgrades_type == TextureFormatUpgradesType::AllowedEnabled ? TextureFormatUpgradesType::AllowedDisabled : TextureFormatUpgradesType::AllowedEnabled;
         }

         // TODO: serialize all this stuff!
         ImGui::NewLine();
         ImGui::Text("Texture Format Upgrades:");
         for (auto toggleable_texture_upgrade_format : toggleable_texture_upgrade_formats)
         {
            // Dumb stream conversion
            std::ostringstream oss;
            oss << toggleable_texture_upgrade_format;
            std::string toggleable_texture_upgrade_format_name = oss.str();

            bool enabled = texture_upgrade_formats.contains(toggleable_texture_upgrade_format);

            int mode = enabled ? 1 : 0;
            const char* settings_name_strings[2] = { "Off", "On" };
            if (ImGui::SliderInt(toggleable_texture_upgrade_format_name.c_str(), &mode, 0, 1, settings_name_strings[mode], ImGuiSliderFlags_NoInput))
            {
               if (mode >= 1)
               {
                  texture_upgrade_formats.emplace(toggleable_texture_upgrade_format);
               }
               else
               {
                  texture_upgrade_formats.erase(toggleable_texture_upgrade_format);
               }
            }
         }
      }

      ImGui::NewLine();
      if (prevent_fullscreen_state ? ImGui::Button("Allow Fullscreen State") : ImGui::Button("Disallow Fullscreen State"))
      {
         prevent_fullscreen_state = !prevent_fullscreen_state;
      }
   }

   void PrintImGuiAbout() override
   {
      ImGui::Text("Generic Luma mod - Developed by Pumbo", "");
   }
};

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
   if (ul_reason_for_call == DLL_PROCESS_ATTACH)
   {
      std::filesystem::path file_path = System::GetModulePath(hModule);
      std::string file_name = file_path.stem().string();
      // Retrieve the dll name and use it as the addon name, so we can re-use this template mod for all games by simply renaming the executable (as long as the mod just involves replacing shaders).
      bool use_custom_game_name = false;
      if (file_name.starts_with(std::string(Globals::MOD_NAME) + "-"))
      {
         file_name.erase(0, std::string(Globals::MOD_NAME).length() + 1);
         if (file_name.starts_with('_'))
            file_name.erase(0, 1);
         if (file_name != "Generic" && file_name != "Generic Mod" && file_name != "GenericMod")
            use_custom_game_name = true;
      }

      const char* project_name = PROJECT_NAME;
      const char* cleared_project_name = (project_name[0] == '_') ? (project_name + 1) : project_name; // Remove the potential "_" at the beginning

      const char* game_name = use_custom_game_name ? file_name.c_str() : cleared_project_name; // Can include spaces!
      std::string mod_description = "Generic Luma mod";
      if (use_custom_game_name)
      {
         sub_game_shaders_appendix = file_name;
         mod_description += " for " + file_name;
      }
      else
      {
         sub_game_shaders_appendix = System::GetProcessExecutableName();
      }

      uint32_t mod_version = 1;
      Globals::SetGlobals(game_name, mod_description.c_str(), "https://github.com/Filoppi/Luma-Framework/", mod_version);

      swapchain_format_upgrade_type = TextureFormatUpgradesType::AllowedEnabled;
      swapchain_upgrade_type = SwapchainUpgradeType::scRGB;
      texture_format_upgrades_type = TextureFormatUpgradesType::AllowedEnabled;
      enable_indirect_texture_format_upgrades = true; // This is generally safer so enable it in the generic mod
      enable_chain_indirect_texture_format_upgrades = ChainTextureFormatUpgradesType::DirectDependencies; // Indirect dependencies are probably not needed as they'd already be upgraded too
      toggleable_texture_upgrade_formats = {
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

            reshade::api::format::r16g16b16a16_unorm,

            reshade::api::format::r11g11b10_float,
      };
      texture_upgrade_formats.insert(toggleable_texture_upgrade_formats.begin(), toggleable_texture_upgrade_formats.end());
      texture_upgrade_formats.erase(reshade::api::format::r16g16b16a16_unorm); // This might be more likely to cause damage than not generally, so remove by default
      texture_format_upgrades_2d_size_filters = 0 | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainResolution | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainAspectRatio | (uint32_t)TextureFormatUpgrades2DSizeFilters::No1Px;

      game = new GenericGame();
   }

   CoreMain(hModule, ul_reason_for_call, lpReserved);

   return TRUE;
}