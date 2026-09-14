#define GAME_LA_NOIRE 1

#include "..\..\Core\core.hpp"

struct GameDeviceDataTemplate final : public GameDeviceData
{
};

class GameShadowOfMordor final : public Game
{
   static GameDeviceDataTemplate& GetGameDeviceData(DeviceData& device_data)
   {
      return *static_cast<GameDeviceDataTemplate*>(device_data.game);
   }

public:
   void OnInit(bool async) override
   {
      std::vector<ShaderDefineData> game_shader_defines_data = {
         {"TONEMAP_TYPE", '1', true, false, "0 - Vanilla SDR\n1 - Luma HDR (Vanilla+)"},
      };
      shader_defines_data.append_range(game_shader_defines_data);
      assert(shader_defines_data.size() < MAX_SHADER_DEFINES);

      GetShaderDefineData(POST_PROCESS_SPACE_TYPE_HASH).SetDefaultValue('0');
      GetShaderDefineData(EARLY_DISPLAY_ENCODING_HASH).SetDefaultValue('0');
      GetShaderDefineData(VANILLA_ENCODING_TYPE_HASH).SetDefaultValue('1');
      GetShaderDefineData(GAMMA_CORRECTION_TYPE_HASH).SetDefaultValue('1');
      GetShaderDefineData(UI_DRAW_TYPE_HASH).SetDefaultValue('0');

      GetShaderDefineData(TEST_SDR_HDR_SPLIT_VIEW_MODE_NATIVE_IMPL_HASH).SetDefaultValue('1');

      luma_settings_cbuffer_index = 10;
      luma_data_cbuffer_index = 9;
   }

   void OnCreateDevice(ID3D11Device* native_device, DeviceData& device_data) override
   {
      device_data.game = new GameDeviceDataTemplate;
   }

   DrawOrDispatchOverrideType OnDrawOrDispatch(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, reshade::api::shader_stage stages, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, bool is_custom_pass, bool& updated_cbuffers, std::function<void()>* original_draw_dispatch_func) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);

      return DrawOrDispatchOverrideType::None;
   }
   void OnPresent(ID3D11Device* native_device, DeviceData& device_data) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);
   }

   void PrintImGuiAbout() override
   {
      ImGui::Text("Template Luma mod - about and credits section", "");
   }
};

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
   if (ul_reason_for_call == DLL_PROCESS_ATTACH)
   {
      Globals::SetGlobals(PROJECT_NAME, "L.A. Noire Luma mod");
      Globals::DEVELOPMENT_STATE = Globals::ModDevelopmentState::NonFunctional;
      Globals::VERSION = 1;

      swapchain_format_upgrade_type = TextureFormatUpgradesType::AllowedEnabled;
      swapchain_upgrade_type = SwapchainUpgradeType::scRGB;
      texture_format_upgrades_type = TextureFormatUpgradesType::AllowedEnabled;
      texture_upgrade_formats = {
            reshade::api::format::r8g8b8a8_unorm,
            //reshade::api::format::r8g8b8a8_unorm_srgb,
            //reshade::api::format::r8g8b8a8_typeless,
            //reshade::api::format::r8g8b8x8_unorm,
            //reshade::api::format::r8g8b8x8_unorm_srgb,
            //reshade::api::format::b8g8r8a8_unorm, // Lego
            //reshade::api::format::b8g8r8a8_unorm_srgb,
            //reshade::api::format::b8g8r8a8_typeless,
            //reshade::api::format::b8g8r8x8_unorm,
            //reshade::api::format::b8g8r8x8_unorm_srgb,
            //reshade::api::format::b8g8r8x8_typeless,
            //reshade::api::format::r11g11b10_float,
      };

      enable_samplers_upgrade = true;

      game = new GameShadowOfMordor();
   }

   CoreMain(hModule, ul_reason_for_call, lpReserved);

   return TRUE;
}