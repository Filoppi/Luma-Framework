#define MIDDLE_EARTH_SHADOW_OF_MORDOR 1

#include "..\..\Core\core.hpp"

struct GameDeviceDataShadowOfMordor : GameDeviceData //TODO: just use global namespace
{
   enum DrawnState
   {
      unknown,
      rolloff,
      // particles_and_stuff
      // motion_blur_per_obj_vectors, // idk (unsafe for 16f)
      bloom,
      // sun,
      // motion_blur,
      // depth_of_field,
      lut,
      blit
   };
};

class GameShadowOfMordor final : public Game 
{
public:
   void OnInit(bool async) override
   {
      message(reshade::log::level::info, "OnInit()");

      // cb
      luma_settings_cbuffer_index = 9;
      luma_data_cbuffer_index = 10;

      // shader def
      static const std::vector<ShaderDefineData> game_shader_defines_data = {
         {"GAMMA_CORRECTION_RANGE_TYPE", '0', true, true, "0 - Full range.\n1 - 0-1 only.", 1},
         {"SWAPCHAIN_CLAMP_PEAK", DEVELOPMENT ? '0' : '1', true, false, "Final color clamp before present.\n0 - Unclamped (up to display).\n1 - Per channel clamp (blows out).", 1},
         {"SWAPCHAIN_TEST_USER_PEAK", '0', true, false, "Show a simple white rectangle peak test.", 1},
         {"SWAPCHAIN_TEST_IS_BLACK", '0', true, false, "If not 0, force to brighter", 1},
      };
      shader_defines_data.append_range(game_shader_defines_data);
      auto_recompile_defines = true;

      GetShaderDefineData(POST_PROCESS_SPACE_TYPE_HASH).SetDefaultValue('0');
      GetShaderDefineData(EARLY_DISPLAY_ENCODING_HASH).SetDefaultValue('0');
      GetShaderDefineData(VANILLA_ENCODING_TYPE_HASH).SetDefaultValue('0');
      GetShaderDefineData(GAMMA_CORRECTION_TYPE_HASH).SetDefaultValue('1');
      GetShaderDefineData(UI_DRAW_TYPE_HASH).SetDefaultValue('2');
      if (!DEVELOPMENT)
      {
         GetShaderDefineData(TEST_SDR_HDR_SPLIT_VIEW_MODE_NATIVE_IMPL_HASH).SetValueFixed(true);
         GetShaderDefineData(TEST_SDR_HDR_SPLIT_VIEW_MODE_NATIVE_IMPL_HASH).editable = false;
         GetShaderDefineData(char_ptr_crc32("TEST_SDR_HDR_SPLIT_VIEW_MODE")).SetValueFixed(true);
         GetShaderDefineData(char_ptr_crc32("TEST_SDR_HDR_SPLIT_VIEW_MODE")).editable = false;
      }

      // indirect upgrades
      auto_texture_format_upgrade_shader_hashes[0xEFA73C0C] = std::pair{std::vector<uint8_t>{0}, std::vector<uint8_t>()};
      auto_texture_format_upgrade_shader_hashes[0x00A4FD79] = std::pair{std::vector<uint8_t>{0}, std::vector<uint8_t>()};
      auto_texture_format_upgrade_shader_hashes[0x40CAFDE9] = std::pair{std::vector<uint8_t>{0}, std::vector<uint8_t>()};
      auto_texture_format_upgrade_shader_hashes[0x3201362F] = std::pair{std::vector<uint8_t>{0}, std::vector<uint8_t>()};
      auto_texture_format_upgrade_shader_hashes[0x103C8F9D] = std::pair{std::vector<uint8_t>{0}, std::vector<uint8_t>()};
      auto_texture_format_upgrade_shader_hashes[0x5358AA7E] = std::pair{std::vector<uint8_t>{0}, std::vector<uint8_t>()};
      auto_texture_format_upgrade_shader_hashes[0x0BE76B98] = std::pair{std::vector<uint8_t>{0}, std::vector<uint8_t>()};
      auto_texture_format_upgrade_shader_hashes[0xECB20855] = std::pair{std::vector<uint8_t>{0}, std::vector<uint8_t>()};
      auto_texture_format_upgrade_shader_hashes[0x4466E09A] = std::pair{std::vector<uint8_t>{0}, std::vector<uint8_t>()};
      auto_texture_format_upgrade_shader_hashes[0xCE6D49B0] = std::pair{std::vector<uint8_t>{0}, std::vector<uint8_t>()};
      auto_texture_format_upgrade_shader_hashes[0xDC064703] = std::pair{std::vector<uint8_t>{0}, std::vector<uint8_t>()};
      auto_texture_format_upgrade_shader_hashes[0x49F5CA76] = std::pair{std::vector<uint8_t>{0}, std::vector<uint8_t>()};
      auto_texture_format_upgrade_shader_hashes[0xAA4C2778] = std::pair{std::vector<uint8_t>{0}, std::vector<uint8_t>()};
      auto_texture_format_upgrade_shader_hashes[0x543655A4] = std::pair{std::vector<uint8_t>{0}, std::vector<uint8_t>()};
      auto_texture_format_upgrade_shader_hashes[0x29838F5A] = std::pair{std::vector<uint8_t>{0}, std::vector<uint8_t>()};
      auto_texture_format_upgrade_shader_hashes[0xF5B965D2] = std::pair{std::vector<uint8_t>{0}, std::vector<uint8_t>()};
   }

   void OnCreateDevice(ID3D11Device* native_device, DeviceData& device_data) override
   {
      message(reshade::log::level::info, "OnCreateDevice()");

      // create data
      device_data.game = new GameDeviceDataShadowOfMordor;
   }

   void PrintImGuiAbout() override
   {
      ImGui::Text("Template Luma mod - about and credits section", "");
   }

   DrawOrDispatchOverrideType OnDrawOrDispatch(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, reshade::api::shader_stage stages, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, bool is_custom_pass, bool& updated_cbuffers, std::function<void()>* original_draw_dispatch_func) override
   {
      GameDeviceDataShadowOfMordor* game_device_data = static_cast<GameDeviceDataShadowOfMordor*>(device_data.game);
      DrawOrDispatchOverrideType result = DrawOrDispatchOverrideType::None;
      // uint64_t ps = original_shader_hashes.pixel_shaders[0];
      // uint64_t cs = original_shader_hashes.compute_shaders[0];
      //
      // // default case
      // if (ps == 0) return DrawOrDispatchOverrideType::None;
      //
      // // Tonemap Rolloff
      // constexpr uint64_t hash_rolloff = 0x29838F5A;
      // if (!game_device_data->drawn.rolloff && ps == hash_rolloff)
      // {
      //    game_device_data->drawn.rolloff = true;
      // }
      //
      // // Gamma Space 3D rendering
      //
      // // Bloom
      // constexpr uint64_t hash_bloomdown0 = 0xAA4C2778;
      // if (!game_device_data->drawn.bloom && ps == hash_bloomdown0)
      // {
      //    game_device_data->drawn.bloom = true;
      // }
      //
      // if (game_device_data->drawn.rolloff && !game_device_data->drawn.bloom)
      // {
      //    ID3D11RenderTargetView* rtv = nullptr;
      //    native_device_context->OMGetRenderTargets(1, &rtv, nullptr); //get
      //
      //    DrawStateStack<DrawStateStackType::SimpleGraphics> cache;
      //    cache.Cache(native_device_context, 0);
      //
      //    static SanitizeNaNsData data;
      //    SanitizeNaNs(native_device, native_device_context, rtv, device_data, data, false);
      //
      //    cache.Restore(native_device_context, false, true);
      // }
      //
      // // Sun
      //
      // // Motion Blur
      //
      // // Depth of Field
      //
      // // LUT
      // constexpr uint64_t hash_lut = 0xC4F01A54;
      // if (game_device_data->drawn.rolloff && !game_device_data->drawn.lut && ps == hash_lut)
      // {
      //    game_device_data->drawn.lut = true;
      // }
      //
      // if (game_device_data->drawn.rolloff && !game_device_data->drawn.lut)
      // {
      //    if (!auto_texture_format_upgrade_shader_hashes.contains(ps))
      //    {
      //       auto_texture_format_upgrade_shader_hashes[ps] = std::pair{ std::vector<uint8_t>{ 0 }, std::vector<uint8_t>() }; // DX11 logic
      //       message(reshade::log::level::info, std::format("Auto-upgrade texture formats for shader hash 0x{:016X}", ps).c_str());
      //    }
      // }
      //
      // // Gamma Blit
      // constexpr uint64_t hash_blit = 0x7D397EA5;
      // if (!game_device_data->drawn.blit && ps == hash_blit)
      // {
      //    game_device_data->drawn.blit = true;
      // }

      return result;
   }

   void OnPresent(ID3D11Device* native_device, DeviceData& device_data) override
   {
      GameDeviceDataShadowOfMordor* game_device_data = static_cast<GameDeviceDataShadowOfMordor*>(device_data.game);
   }
};

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
   if (ul_reason_for_call == DLL_PROCESS_ATTACH)
   {
      Globals::SetGlobals(PROJECT_NAME, "Shadow of Mordor - Luma");
      Globals::VERSION = 1;

      swapchain_format_upgrade_type = TextureFormatUpgradesType::AllowedEnabled;
      swapchain_upgrade_type = SwapchainUpgradeType::scRGB;

      texture_format_upgrades_type = TextureFormatUpgradesType::AllowedEnabled;
      enable_chain_indirect_texture_format_upgrades = ChainTextureFormatUpgradesType::DirectAndIndirectDependencies;
      texture_upgrade_formats = {};
      texture_format_upgrades_2d_size_filters = 0;

      game = new GameShadowOfMordor();
   }

   CoreMain(hModule, ul_reason_for_call, lpReserved);

   return TRUE;
}