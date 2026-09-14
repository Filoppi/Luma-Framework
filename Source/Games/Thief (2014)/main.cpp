#define GAME_THIEF 1

#define LUMA_PATCH_BYTECODE_SYNC 1

#include "..\..\Core\core.hpp"

namespace
{
   ShaderHashesList shader_hashes_FinalPostProcess;
   ShaderHashesList shader_hashes_BlackBars;

	bool remove_black_bars = false;
}

class Thief final : public Game
{
public:
   void OnInit(bool async) override
   {
      std::vector<ShaderDefineData> game_shader_defines_data = {
         {"ENABLE_FAKE_HDR", '1', true, false, "Enable a \"Fake\" HDR boosting effect, as the game's dynamic range was very limited to begin with", 1},
      };
      shader_defines_data.append_range(game_shader_defines_data);
      // The game was SDR all along, but it was all linear space (sRGB textures), it never directly applied gamma, it relied on sRGB and not views for conversions (UI is in gamma space)
      // For now, until we have custom UI blending, we force it to output in gamma space.
      GetShaderDefineData(POST_PROCESS_SPACE_TYPE_HASH).SetDefaultValue('1');
      GetShaderDefineData(VANILLA_ENCODING_TYPE_HASH).SetDefaultValue('0');
      GetShaderDefineData(GAMMA_CORRECTION_TYPE_HASH).SetDefaultValue('1');
      GetShaderDefineData(UI_DRAW_TYPE_HASH).SetDefaultValue('3');

      GetShaderDefineData(TEST_SDR_HDR_SPLIT_VIEW_MODE_NATIVE_IMPL_HASH).SetDefaultValue('1'); // The game just hard clipped to 1, it was all UNORM, it had no HDR rendering
   }

   // Fix all luminance calculations in the game
   std::unique_ptr<std::byte[]> PatchShaderBytecodeSync(const std::byte* code, size_t& size, reshade::api::pipeline_subobject_type type, uint64_t shader_hash, const std::byte* shader_object, size_t shader_object_size) override
   {
      if (type != reshade::api::pipeline_subobject_type::pixel_shader) return nullptr;

      // Used by original game code (UE3)
      // float3(0.299, 0.587, 0.114)
      const std::vector<std::byte> pattern_bt_601_luminance_a = {
       std::byte{0x87}, std::byte{0x16}, std::byte{0x99}, std::byte{0x3E},
       std::byte{0xA2}, std::byte{0x45}, std::byte{0x16}, std::byte{0x3F},
       std::byte{0xD5}, std::byte{0x78}, std::byte{0xE9}, std::byte{0x3D}
      };
      // Used by FXAA and a couple other shaders
      // float3(0.3, 0.59, 0.11)
      const std::vector<std::byte> pattern_bt_601_luminance_b = {
       std::byte{0x9A}, std::byte{0x99}, std::byte{0x99}, std::byte{0x3E},
       std::byte{0x3D}, std::byte{0x0A}, std::byte{0x17}, std::byte{0x3F},
       std::byte{0xAE}, std::byte{0x47}, std::byte{0xE1}, std::byte{0x3D}
      };

      const std::vector<std::byte> pattern_bt_709_luminance = {
       std::byte{0xD0}, std::byte{0xB3}, std::byte{0x59}, std::byte{0x3E},
       std::byte{0x59}, std::byte{0x17}, std::byte{0x37}, std::byte{0x3F},
       std::byte{0x98}, std::byte{0xDD}, std::byte{0x93}, std::byte{0x3D}
      };

      // Remove the saturate flag from the last to mul_sat in the shader after the first instance of (on any registers) appeared:
      // r5.x = saturate(dot(r6.yz, float2(0.816496611,0.577350259)));
      // r5.y = saturate(dot(r6.xyz, float3(-0.707106769,-0.408248305,0.577350259)));
      // r5.z = saturate(dot(r6.yzx, float3(-0.408248305,0.577350259,0.707106769)));
      // If there's only one mul_sat, it's ok. Both are on two different registers (not a squaring a single variable),
      // and the second one would be next to a cb0[25].xyz sum. Both the mul_sat are done on xyz.
      const std::vector<std::byte> pattern_normals_decode = { std::byte{0xEC}, std::byte{0x05}, std::byte{0x51}, std::byte{0x3F}, std::byte{0x3A}, std::byte{0xCD}, std::byte{0x13}, std::byte{0x3F} };
      const std::vector<std::byte> pattern_mul_sat = { std::byte{0x38}, std::byte{0x20}, std::byte{0x00}, std::byte{0x07}, std::byte{0x72}, std::byte{0x00}, std::byte{0x10}, std::byte{0x00} }; // mul_sat

      std::unique_ptr<std::byte[]> new_code = nullptr;

      std::vector<std::byte*> matches_normals_decode = System::ScanMemoryForPattern(reinterpret_cast<const std::byte*>(code), size, pattern_normals_decode);
      if (!matches_normals_decode.empty())
      {
         // The remaining patterns are after the last mad_sat/mul_sat (there could be quite a few mad_sat/mul_sat around, the one we care for is seemengly always the last one)
         const size_t size_offset = matches_normals_decode[0] - code;
         std::vector<std::byte*> matches_mul_sat = System::ScanMemoryForPattern(matches_normals_decode[0], size - size_offset, pattern_mul_sat);
         uint8_t matches_count = 0;
         // Only do the last two matches
         for (int64_t i = int64_t(matches_mul_sat.size()) - 1; i >= 0; i--)
         {
            // Allocate new buffer and copy original shader code
            if (!new_code)
            {
               new_code = std::make_unique<std::byte[]>(size);
               std::memcpy(new_code.get(), code, size);
            }

            // Remove the 0x20 saturate flag in the second byte
            size_t offset = matches_mul_sat[i] - code;
            new_code[offset + 1] = std::byte{ 0x00 };

            matches_count++;
            if (matches_count == 2)
            {
               break;
            }
         }
      }

      std::vector<std::byte*> matches_bt_601_luminance = System::ScanMemoryForPattern(reinterpret_cast<const std::byte*>(code), size, pattern_bt_601_luminance_a);
      matches_bt_601_luminance.append_range(System::ScanMemoryForPattern(reinterpret_cast<const std::byte*>(code), size, pattern_bt_601_luminance_b));
      if (!matches_bt_601_luminance.empty())
      {
         if (!new_code)
         {
            new_code = std::make_unique<std::byte[]>(size);
            std::memcpy(new_code.get(), code, size);
         }

         // Always correct the wrong luminance calculations
         for (std::byte* match : matches_bt_601_luminance)
         {
            // Calculate offset of each match relative to original code
            size_t offset = match - code;
            std::memcpy(new_code.get() + offset, pattern_bt_709_luminance.data(), pattern_bt_709_luminance.size());
         }
      }

      return new_code;
   }

   DrawOrDispatchOverrideType OnDrawOrDispatch(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, reshade::api::shader_stage stages, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, bool is_custom_pass, bool& updated_cbuffers, std::function<void()>* original_draw_dispatch_func) override
   {
      if (!device_data.has_drawn_main_post_processing && original_shader_hashes.Contains(shader_hashes_FinalPostProcess))
      {
         device_data.has_drawn_main_post_processing = true;

         // UI doesn't write to the swapchain in this game
         if (enable_ui_separation)
         {
            device_data.ui_initial_original_rtv = nullptr;
            native_device_context->OMGetRenderTargets(1, &device_data.ui_initial_original_rtv, nullptr);
         }
      }
      else if (device_data.has_drawn_main_post_processing)
      {
         // Refresh the final render target in case it was swapped by one of the later passes
         if (enable_ui_separation && original_shader_hashes.Contains(shader_hashes_UI_excluded))
         {
            device_data.ui_initial_original_rtv = nullptr;
            native_device_context->OMGetRenderTargets(1, &device_data.ui_initial_original_rtv, nullptr);
         }
         if (remove_black_bars && original_shader_hashes.Contains(shader_hashes_BlackBars))
         {
            return DrawOrDispatchOverrideType::Skip;
         }
      }
      return DrawOrDispatchOverrideType::None;
   }

   void OnPresent(ID3D11Device* native_device, DeviceData& device_data) override
   {
      device_data.has_drawn_main_post_processing = false;
   }

   void LoadConfigs() override
   {
      reshade::api::effect_runtime* runtime = nullptr;
      reshade::get_config_value(runtime, NAME, "RemoveBlackBars", remove_black_bars);
   }

   void DrawImGuiSettings(DeviceData& device_data) override
   {
      reshade::api::effect_runtime* runtime = nullptr;

      if (ImGui::Checkbox("Remove Black Bars", &remove_black_bars))
      {
         reshade::set_config_value(runtime, NAME, "RemoveBlackBars", remove_black_bars);
      }
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      {
         ImGui::SetTooltip("At non 16:9 resolutions, the game might display black bars in some scenes, this will remove them.");
      }
   }

   void PrintImGuiAbout() override
   {
      ImGui::Text("Luma for \"Thief\" is developed by Pumbo and is open source and free.\nIf you enjoy it, consider donating.\nMake sure FXAA is enabled in the game settings for the mod to work properly.", "");

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

         "\n\nThird Party:"
         "\nReShade"
         "\nImGui"
         "\nRenoDX"
         "\n3Dmigoto"
         "\nOklab"
         "\nDICE (HDR tonemapper)"
         , "");
   }
};

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
   if (ul_reason_for_call == DLL_PROCESS_ATTACH)
   {
      Globals::SetGlobals(PROJECT_NAME, "Thief (2014) Luma mod");
      Globals::VERSION = 1;
      Globals::DEVELOPMENT_STATE = Globals::ModDevelopmentState::Playable;

      luma_settings_cbuffer_index = 13;
      luma_data_cbuffer_index = 12;

      // Needed as the UI can both generate NaNs (I think) and also do subtractive blends that result in colors with invalid (or overly low) luminances (that can be fixed by clamping all UI shaders alpha to 0-1) (update: it probably didn't do any subtractive blends),
      // thus drawing it separately and composing it on top, is better.
      // The game also casts a TYPELESS texture as UNORM, while it was previously cast as UNORM_SRGB (linear) (float textures can't preserve this behaviour)
      enable_ui_separation = true;

      swapchain_format_upgrade_type = TextureFormatUpgradesType::AllowedEnabled;
      swapchain_upgrade_type = SwapchainUpgradeType::scRGB;
      texture_format_upgrades_type = TextureFormatUpgradesType::AllowedEnabled;
      texture_upgrade_formats = {
            reshade::api::format::r8g8b8a8_unorm,
            reshade::api::format::r8g8b8a8_unorm_srgb,
            reshade::api::format::r8g8b8a8_typeless,

            // For some reason thief used these as high quality SDR buffers (it seems like a mistake, but possibly the intention was to have higher quality in post processing)
            reshade::api::format::r16g16b16a16_unorm,

            reshade::api::format::r11g11b10_float,
      };
      texture_format_upgrades_2d_size_filters = 0 | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainResolution | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainAspectRatio | (uint32_t)TextureFormatUpgrades2DSizeFilters::No1Px;
#if DEVELOPMENT // Seemingly not needed in this game but makes development easier. Or maybe not? It hangs the game on boot sometimes? Nah it hangs anyway
      enable_indirect_texture_format_upgrades = true;
#endif
      enable_chain_indirect_texture_format_upgrades = ChainTextureFormatUpgradesType::DirectDependencies;

      // The final swapchain copy is through an sRGB view, whether the swapchain is sRGB or not (note that sRGB swapchains don't support flip models).
      force_vanilla_swapchain_linear = true;

      // Final order is Tonemap->FXAA(optional)->PP(optional)
      shader_hashes_FinalPostProcess.pixel_shaders = {
         // Tonemappers
         std::stoul("DDDC6D72", nullptr, 16),
         std::stoul("C9DB6671", nullptr, 16),
         std::stoul("BBFCB3DB", nullptr, 16),
         std::stoul("B8BC8EE2", nullptr, 16),
         std::stoul("B3C85DAA", nullptr, 16),
         std::stoul("A394022E", nullptr, 16),
         std::stoul("A350CA27", nullptr, 16),
         std::stoul("8787CA4A", nullptr, 16),
         std::stoul("91387CB8", nullptr, 16),
         std::stoul("47FB9170", nullptr, 16),
         std::stoul("7FCEB0F9", nullptr, 16),
         std::stoul("7EAAD3CF", nullptr, 16),
         std::stoul("7D63D6F8", nullptr, 16),
         std::stoul("7D02C225", nullptr, 16),
         std::stoul("3C5929C5", nullptr, 16),
         std::stoul("1F8A7C3B", nullptr, 16),

         // FXAA (add this optionally, just in the edge case where tonemap didn't run and FXAA was enabled (it's expected to be))
         std::stoul("1EAE8451", nullptr, 16),
      };
      shader_hashes_UI_excluded.pixel_shaders = {
         // In order of execution:

         // FXAA
         std::stoul("1EAE8451", nullptr, 16),

         // Misc post process
         // TODO: review whether 6537153A and 4824964A belong in there, they probably do (either way it shouldn't hurt if they are run before the tonemappers).
         std::stoul("CDC104C3", nullptr, 16),
         std::stoul("6537153A", nullptr, 16),
         std::stoul("4824964A", nullptr, 16),
         std::stoul("4606F1C6", nullptr, 16),

         // UI black bars (they draw with sRGB views instead of non sRGB views (gamma space) like the rest of the UI, and anyway they are not part of the UI)
         std::stoul("E9255521", nullptr, 16),

         // Swapchain copy (after UI) (not needed anyway as the render target would have changed)
         std::stoul("7FF6EC9E", nullptr, 16),
      };
      shader_hashes_BlackBars.pixel_shaders.emplace(std::stoul("E9255521", nullptr, 16));

      game = new Thief();
   }

   CoreMain(hModule, ul_reason_for_call, lpReserved);

   return TRUE;
}