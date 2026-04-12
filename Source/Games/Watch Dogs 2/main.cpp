#define GAME_WATCH_DOGS_2 1

// Hooking a debugger is forbidden
#define DISABLE_AUTO_DEBUGGER 1

#define ENABLE_ORIGINAL_SHADERS_MEMORY_EDITS 1

#include "..\..\Core\core.hpp"

#include "..\..\Core\includes\shader_patching.h"

namespace
{
   ShaderHashesList shader_hashes_ColorGradingLUT;
   ShaderHashesList shader_hashes_TemporalFiltering;
}

class WatchDogs2 final : public Game
{
public:
   void OnInit(bool async) override
   {
      std::vector<ShaderDefineData> game_shader_defines_data = {
         {"ENABLE_DITHER", '0', true, false, "Allows disabling the game's 8 bit dithering effect (luma disables it by default as it's all HDR)"},
      };
      shader_defines_data.append_range(game_shader_defines_data);
      GetShaderDefineData(POST_PROCESS_SPACE_TYPE_HASH).SetDefaultValue('1'); // Game was all linear, rendering is R16G16B16A16_FLOAT and post processing + UI is R8G8B8A8_UNORM_SRGB or B8G8R8A8_UNORM_SRGB.
      GetShaderDefineData(GAMMA_CORRECTION_TYPE_HASH).SetDefaultValue('0'); // Game seemengly looks better (less crush, less unnatural shadow) in sRGB than 2.2
      GetShaderDefineData(UI_DRAW_TYPE_HASH).SetDefaultValue('2');
   }

   std::unique_ptr<std::byte[]> ModifyShaderByteCode(const std::byte* code, size_t& size, reshade::api::pipeline_subobject_type type, uint64_t shader_hash, const std::byte* shader_object, size_t shader_object_size) override
   {
      if (type != reshade::api::pipeline_subobject_type::compute_shader)
         return nullptr;

      std::unique_ptr<std::byte[]> new_code = nullptr;

      // This compute shader was unsafe, it was reading and writing to the same coordinates of the same resources, from different threads at the same time, hence it needs some barriers to be added
      // Credits to Nukem, Blisto, doitsujin and pendingchaos for helping figure it out.
      if (shader_hash != 0x28BA3808)
      {
         return new_code;
      }

      std::vector<uint8_t> appended_patch;
      std::vector<const std::byte*> appended_patches_addresses;

      // Matches "AllMemoryBarrierWithGroupSync()" ("sync_uglobal_g_t" in asm)
      constexpr uint32_t flags =
         D3D11_SB_SYNC_THREADS_IN_GROUP |
         D3D11_SB_SYNC_THREAD_GROUP_SHARED_MEMORY |
         D3D11_SB_SYNC_UNORDERED_ACCESS_VIEW_MEMORY_GROUP |
         D3D11_SB_SYNC_UNORDERED_ACCESS_VIEW_MEMORY_GLOBAL;
      uint32_t opcode_token =
         ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_SYNC) |
         ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1) |
         ENCODE_D3D11_SB_SYNC_FLAGS(flags);
#if 1 // TODOFT: test... we got "sync_sat_uglobal_g_t" otherwise?
      // make 100% sure SAT is off (paranoia, but harmless)
      opcode_token &= ~D3D10_SB_INSTRUCTION_SATURATE_MASK;
#endif
      std::vector<uint32_t> opcode_token_patch = std::vector<uint32_t>{opcode_token};

      appended_patch.insert(appended_patch.end(), reinterpret_cast<uint8_t*>(opcode_token_patch.data()), reinterpret_cast<uint8_t*>(opcode_token_patch.data()) + opcode_token_patch.size() * sizeof(uint32_t));

      size_t size_u32 = size / sizeof(uint32_t);
      const uint32_t* code_u32 = reinterpret_cast<const uint32_t*>(code);
      size_t i = 0;
      while (i < size_u32)
      {
         uint32_t opcode_token = code_u32[i];
         D3D10_SB_OPCODE_TYPE opcode_type = DECODE_D3D10_SB_OPCODE_TYPE(opcode_token);
         size_t instruction_size = opcode_type == D3D10_SB_OPCODE_CUSTOMDATA ? code_u32[i + 1] : DECODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(opcode_token); // Includes itself

         if (opcode_type == D3D10_SB_OPCODE_IF)
         {
            // Add the patch before every single branch value.
            // Shift it by how much the data would have been shifted by prior patches we already added.
            size_t i_add = appended_patches_addresses.size() * appended_patch.size() / sizeof(uint32_t); // Patches should always be a multiple of DWORD
            appended_patches_addresses.emplace_back(reinterpret_cast<const std::byte*>(&code_u32[i + i_add]));
         }

         i += instruction_size;
         if (instruction_size == 0)
            break;
      }

      // Insert the patch for each address
      if (!appended_patches_addresses.empty())
      {
         new_code = std::make_unique<std::byte[]>(size + appended_patch.size() * appended_patches_addresses.size());

         std::memcpy(new_code.get(), code, size);

         size_t valid_size = size;

         std::unique_ptr<std::byte[]> scratch_buffer = std::make_unique<std::byte[]>(size + appended_patch.size() * appended_patches_addresses.size());

         for (const auto appended_patches_address : appended_patches_addresses)
         {
            size_t insert_pos = appended_patches_address - code; // These are already shifted to account for the previously inserted patches

            // Copy from the address we'll insert the patch at, until the end, into a temporary buffer
            std::memcpy(scratch_buffer.get(), new_code.get() + insert_pos, valid_size - insert_pos);
            // Insert the patch
            std::memcpy(new_code.get() + insert_pos, appended_patch.data(), appended_patch.size());
            // Fill back the previous data, shifted
            std::memcpy(new_code.get() + insert_pos + appended_patch.size(), scratch_buffer.get(), valid_size - insert_pos);

            valid_size += appended_patch.size();
         }

         size = valid_size;
      }

      return new_code;
   }

   DrawOrDispatchOverrideType OnDrawOrDispatch(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, reshade::api::shader_stage stages, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, bool is_custom_pass, bool& updated_cbuffers, std::function<void()>* original_draw_dispatch_func) override
   {
      // Make sure the swapchain copy shader always and only targets the swapchain RT, otherwise we'd need to branch in it!
      if (is_custom_pass && (stages & reshade::api::shader_stage::compute) != 0 && original_shader_hashes.Contains(shader_hashes_ColorGradingLUT))
      {
         // We need access to a linear sampler in the customized version of this CS, so add it (and make sure it's not overlapping with any other used slot, so we don't pollute the state)
         ID3D11SamplerState* const sampler_state_linear = device_data.sampler_state_linear.get();
         native_device_context->CSSetSamplers(D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT - 1, 1, &sampler_state_linear);

         return DrawOrDispatchOverrideType::None;
      }

      if ((stages & reshade::api::shader_stage::compute) != 0 && original_shader_hashes.Contains(shader_hashes_TemporalFiltering))
      {
         static bool has_sent_tf_warning = false;
         if (!has_sent_tf_warning && MessageBoxA(NULL, "Temporal Filtering is broken in Watch Dogs 2 depending on your GPU, Luma suggests against using it.", "Temporal Filtering detected", MB_OK | MB_SETFOREGROUND) == IDOK)
         {
            has_sent_tf_warning = true;
         }
      }

      return DrawOrDispatchOverrideType::None;
   }

   void PrintImGuiAbout() override
   {
      ImGui::Text("Luma for \"Watch Dogs 2\" is developed by Pumbo and is open source and free.\nIf you enjoy it, consider donating.", "");

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
         "\nDICE (HDR tonemapper)"
         , "");
   }
};

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
   if (ul_reason_for_call == DLL_PROCESS_ATTACH)
   {
      Globals::SetGlobals(PROJECT_NAME, "Watch Dogs 2 Luma mod");
      Globals::DEVELOPMENT_STATE = Globals::ModDevelopmentState::Playable;
      Globals::VERSION = 1;

      luma_settings_cbuffer_index = 12; // 13 is used
      luma_data_cbuffer_index = 11;

      swapchain_format_upgrade_type = TextureFormatUpgradesType::AllowedEnabled;
      swapchain_upgrade_type = SwapchainUpgradeType::scRGB;
      texture_format_upgrades_type = TextureFormatUpgradesType::AllowedEnabled;
      texture_upgrade_formats = {
#if 0 // TODO: needed?
            reshade::api::format::r8g8b8a8_unorm,
            reshade::api::format::r8g8b8a8_unorm_srgb,
            reshade::api::format::r8g8b8a8_typeless,
#endif
#if 0 // These are probably not needed (unused) but shouldn't hurt (actually they are!!!)
            reshade::api::format::r8g8b8x8_unorm,
            reshade::api::format::r8g8b8x8_unorm_srgb,
            reshade::api::format::b8g8r8a8_unorm,
            reshade::api::format::b8g8r8a8_unorm_srgb,
            reshade::api::format::b8g8r8a8_typeless,
            reshade::api::format::b8g8r8x8_unorm,
            reshade::api::format::b8g8r8x8_unorm_srgb,
            reshade::api::format::b8g8r8x8_typeless,
#else
            reshade::api::format::r8g8b8a8_typeless,
            reshade::api::format::b8g8r8a8_typeless,
#endif
#if 1
            reshade::api::format::r11g11b10_float,
#endif
      };
      //texture_format_upgrades_2d_size_filters = 0 | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainResolution | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainAspectRatio;
      texture_format_upgrades_2d_size_filters = (uint32_t)TextureFormatUpgrades2DSizeFilters::None;
      //enable_indirect_texture_format_upgrades = true; // Makes the game crash when we copy textures on the CPU in "OnMapTextureRegion" // TODOFT
      enable_chain_indirect_texture_format_upgrades = ChainTextureFormatUpgradesType::DirectDependencies;

#if 1
      // Upgrade post process textures only, the rest is not needed, it damages performance and potentially causes artifacts
      auto_texture_format_upgrade_shader_hashes = {
         // Tonemappers:
         {0x0A7D2AB7, {{0}, {}}},
         {0xAD6E5AAF, {{0}, {}}},
         {0x10FA30C8, {{0}, {}}},
         {0x2D2FB973, {{0}, {}}},
         {0x691AE5AF, {{0}, {}}},
         {0xEA7FA4E5, {{0}, {}}},
         {0x67A672D7, {{0}, {}}},
         {0xC8651827, {{0}, {}}},
         
         {0x35B62AAF, {{0}, {}}}, // Upscale (Temporal Filtering)
         {0x6C8FE673, {{0}, {}}}, // FXAA
         {0x5554278D, {{0}, {}}}, // SMAA
         {0x8ADB0AAD, {{0}, {}}}, // PostFX
         {0x65D9186F, {{0}, {}}}, // PostFX
         {0x5EA57AF3, {{0}, {}}}, // PostFX
         {0xF584A327, {{0}, {}}}, // PostFX
         {0x84DB2096, {{0}, {}}}, // Blur UI
      };
#endif
      texture_format_upgrades_lut_size = 32;
      texture_format_upgrades_lut_dimensions = LUTDimensions::_3D;

      shader_hashes_ColorGradingLUT.compute_shaders = {
         0xAC50585B,
         0xEED9A3FF,
         0x2B8472D5,
         0x8B8BEC2A,
         0x919F1537,
         0x56F305BB,
         0x2033D7C9,
         0x0A696247,
         0x28816DF0,
         0x7336D9BE,
         0x6F668AD1,
         0x60118D8B,
         0x8F54485D,
         0xB054D156,
         0x21774BE1,
         0x69D3F6E7,
      };
      shader_hashes_TemporalFiltering.compute_shaders = {
         0x45FD59AC,
         0x14AA8AC5,
      };

      redirected_shader_hashes["ColorGradingLUT"] =
         {
            "AC50585B",
            "EED9A3FF",
            "2B8472D5",
            "8B8BEC2A",
            "919F1537",
            "56F305BB",
            "2033D7C9",
            "0A696247",
            "28816DF0",
            "7336D9BE",
            "6F668AD1",
            "60118D8B",
            "8F54485D",
            "B054D156",
            "21774BE1",
            "69D3F6E7",
         };
      // TODO: edge cases are still missing
      redirected_shader_hashes["Tonemap"] =
         {
            "0A7D2AB7",
            "AD6E5AAF",
            "10FA30C8",
            "2D2FB973",
            "691AE5AF",
            "EA7FA4E5",
            "67A672D7",
            "C8651827",
         };

#if DEVELOPMENT
      forced_shader_names.emplace(Shader::Hash_StrToNum("74F79E89"), "Clean to Black");
      forced_shader_names.emplace(Shader::Hash_StrToNum("4B06125F"), "Clean to Black");
      forced_shader_names.emplace(Shader::Hash_StrToNum("765C1510"), "UPlay Overlay");
      forced_shader_names.emplace(Shader::Hash_StrToNum("C941F7C4"), "Copy Depth");
      forced_shader_names.emplace(Shader::Hash_StrToNum("E82D1C86"), "SMAA Edges Detection");
      forced_shader_names.emplace(Shader::Hash_StrToNum("B9DD88BE"), "SMAA Weights Detection");
      forced_shader_names.emplace(Shader::Hash_StrToNum("1445F2D0"), "SMAA Weights Detection + Temporal Reprojection");
      forced_shader_names.emplace(Shader::Hash_StrToNum("5554278D"), "SMAA");
#endif

      game = new WatchDogs2();
   }

   CoreMain(hModule, ul_reason_for_call, lpReserved);

   return TRUE;
}