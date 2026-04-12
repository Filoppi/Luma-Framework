#define GAME_HEAVY_RAIN 1

// See "strip_original_shaders_debug_data"
#define ENABLE_ORIGINAL_SHADERS_MEMORY_EDITS 1

#include "..\..\Core\core.hpp"

#include "..\..\Core\includes\shader_patching.h"

#include "patches/Patches.h"

namespace
{
   ShaderHashesList pixel_shader_hashes_BloomAndLensFlare;
   ShaderHashesList pixel_shader_hashes_Tonemap;
   ShaderHashesList pixel_shader_hashes_AA;

   bool pending_swapchain_resize = false;
}

struct GameDeviceDataHeavyRain final : public GameDeviceData
{
   com_ptr<ID3D11Resource> scene_color_resource;
   com_ptr<ID3D11RenderTargetView> scene_color_rtv;
   SanitizeNaNsData sanitize_nans_data;

#if DEVELOPMENT || TEST
   bool drew_scene = false;
#endif
};

class HeavyRain final : public Game
{
public:
   static GameDeviceDataHeavyRain& GetGameDeviceData(DeviceData& device_data)
   {
      return *static_cast<GameDeviceDataHeavyRain*>(device_data.game);
   }

   void OnLoad(std::filesystem::path& file_path, bool failed) override
   {
      if (!failed)
      {
         if (GetModuleHandle(TEXT("eossdk-win64-shipping.dll")) != NULL) {
            if (MessageBoxA(NULL, "This mod only works on the Steam and GOG versions of the game.\nUltrawide fixes will also work on the Epic Store version, but custom shaders might not all load, as that's an older version of the game.", "Incompatible Game Version", MB_OK | MB_SETFOREGROUND) == IDOK) {
               // Just continue for now
            }
         }

         Patches::Init(NAME, Globals::VERSION); // This might already try to patch the executable but likely not as it's still got the default aspect ratio (we don't know the window AR yet)
      }
   }

   void OnInit(bool async) override
   {
      std::vector<ShaderDefineData> game_shader_defines_data = {
         {"ENABLE_LUMA", '1', true, false, "Allows disabling the mod's improvements to the game's look", 1},
         {"ENABLE_FILM_GRAIN", '1', true, false, "Allows disabling the game's Film Grain effect, which Luma already improves by default", 1},
         {"ENABLE_FAKE_HDR", '1', true, false, "Enable a \"Fake\" HDR boosting effect, as the game's dynamic range was fairly limited to begin with", 1},
         {"ENABLE_COLOR_GRADING", '1', true, false, "Allows disabling the color grading LUT (some other color filters might still get applied)", 1},
         {"ENABLE_POST_PROCESS_EFFECTS", '1', true, false, "Allows disabling all post process effects light Bloom, Lens Flare etc", 1},
      };
      shader_defines_data.append_range(game_shader_defines_data);

      luma_settings_cbuffer_index = 13;
      luma_data_cbuffer_index = 12;

      GetShaderDefineData(POST_PROCESS_SPACE_TYPE_HASH).SetDefaultValue('0');
      GetShaderDefineData(VANILLA_ENCODING_TYPE_HASH).SetDefaultValue('1'); // Gamma 2.2 in and out
      GetShaderDefineData(GAMMA_CORRECTION_TYPE_HASH).SetDefaultValue('1');
      GetShaderDefineData(UI_DRAW_TYPE_HASH).SetDefaultValue('2');

      GetShaderDefineData(TEST_SDR_HDR_SPLIT_VIEW_MODE_NATIVE_IMPL_HASH).SetDefaultValue('1'); // The game just clipped, so HDR is an extension of SDR (except for some shaders that we adjust)

#if 1 // Default to the display aspect ratio, so we apply the patch as early as possible, reducing the possible "damage" it does (it still doesn't prevent users from force toggling fullscreen and borderless once for the changes to take effect). This seems to occasionally be able to boot the game in UW without resizing the window, so chances are the code is multithreaded
      int screen_width = GetSystemMetrics(SM_CXSCREEN);
      int screen_height = GetSystemMetrics(SM_CYSCREEN);
      bool patched = Patches::SetOutputResolution(screen_width, screen_height);
      pending_swapchain_resize |= patched;
      if (!patched)
      {
         reshade::log::message(reshade::log::level::warning, "Heavy Rain Luma failed to patch for Ultrawide compatibility.\nIf you have already patched the executable, restore the original.\nSteam and GOG versions of the game should work.");
      }
#endif
   }

   void OnCreateDevice(ID3D11Device* native_device, DeviceData& device_data) override
   {
      device_data.game = new GameDeviceDataHeavyRain;
   }

   void OnInitSwapchain(reshade::api::swapchain* swapchain) override
   {
      auto& device_data = *swapchain->get_device()->get_private_data<DeviceData>();

      bool patched = Patches::SetOutputResolution(device_data.output_resolution.x + 0.5f, device_data.output_resolution.y + 0.5f);
      if (!patched)
      {
         reshade::log::message(reshade::log::level::warning, "Heavy Rain Luma failed to patch for Ultrawide compatibility.\nIf you have already patched the executable, restore the original.\nSteam and GOG versions of the game should work.");
      }
      pending_swapchain_resize |= patched;

      // Assume it's the output resolution until proven otherwise
      cb_luma_global_settings.GameSettings.InvRenderRes.x = 1.f / device_data.output_resolution.x;
      cb_luma_global_settings.GameSettings.InvRenderRes.y = 1.f / device_data.output_resolution.y;
      device_data.cb_luma_global_settings_dirty = true;

      auto& game_device_data = GetGameDeviceData(device_data);
      game_device_data.scene_color_rtv.reset();
      game_device_data.scene_color_resource.reset();
      game_device_data.sanitize_nans_data = {};
   }

   // Add a saturate on opaque/transparent materials
   // Without this, some materials output alphas beyond 0-1, messing up the results if the render target is FLOAT (as opposed to the original UNORM)
   std::unique_ptr<std::byte[]> ModifyShaderByteCode(const std::byte* code, size_t& size, reshade::api::pipeline_subobject_type type, uint64_t shader_hash, const std::byte* shader_object, size_t shader_object_size) override
   {
      if (type != reshade::api::pipeline_subobject_type::pixel_shader) return nullptr;

      std::unique_ptr<std::byte[]> new_code = nullptr;

#if 1
      // All shaders that write on the scene color buffer have these ops at the beginning
      // dcl_output o0.xyzw
      // dcl_output o1.xyzw
      // dcl_output o2.x
      // Note that the last one is float4 in the input signature but only x is used in the shader code
      uint8_t found_dcl_outputs = 0;

      size_t size_u32 = size / sizeof(uint32_t);
      const uint32_t* code_u32 = reinterpret_cast<const uint32_t*>(code);
      size_t i = 0;
      bool found_first_dcl = false;
      while (i < size_u32)
      {
         uint32_t opcode_token = code_u32[i];
         D3D10_SB_OPCODE_TYPE opcode_type = DECODE_D3D10_SB_OPCODE_TYPE(opcode_token);

         // Stop scanning the DCL opcodes, they are all declared at the beginning
         if (ShaderPatching::opcodes_dcl.contains(opcode_type))
            found_first_dcl = true;
         else if (found_first_dcl) // Extra safety in case there were instructions before DCL ones
            break;
         
         size_t instruction_size = opcode_type == D3D10_SB_OPCODE_CUSTOMDATA ? code_u32[i + 1] : DECODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(opcode_token); // Includes itself

         if (opcode_type == D3D10_SB_OPCODE_DCL_OUTPUT)
         {
            assert(instruction_size == 2);
            uint32_t operand_token = code_u32[i + 1];
            bool four_channels = true;
            if (found_dcl_outputs == 2)
            {
               four_channels = false;
            }
            uint32_t test_operand_token =
                ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_4_COMPONENT) |
                ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(D3D10_SB_OPERAND_4_COMPONENT_MASK_MODE) |
                ENCODE_D3D10_SB_OPERAND_4_COMPONENT_MASK(D3D10_SB_OPERAND_4_COMPONENT_MASK_X) |
                ENCODE_D3D10_SB_OPERAND_4_COMPONENT_MASK(four_channels ? D3D10_SB_OPERAND_4_COMPONENT_MASK_Y : D3D10_SB_OPERAND_4_COMPONENT_MASK_X) |
                ENCODE_D3D10_SB_OPERAND_4_COMPONENT_MASK(four_channels ? D3D10_SB_OPERAND_4_COMPONENT_MASK_Z : D3D10_SB_OPERAND_4_COMPONENT_MASK_X) |
                ENCODE_D3D10_SB_OPERAND_4_COMPONENT_MASK(four_channels ? D3D10_SB_OPERAND_4_COMPONENT_MASK_W : D3D10_SB_OPERAND_4_COMPONENT_MASK_X) |
                ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_OUTPUT) |
                ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_1D) |
                ENCODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(found_dcl_outputs, D3D10_SB_OPERAND_INDEX_IMMEDIATE32);
            if (operand_token == test_operand_token)
            {
               found_dcl_outputs++;
            }
         }

         i += instruction_size;
         if (instruction_size == 0)
            break;
      }

      bool pattern_found = found_dcl_outputs == 3;
#else // This one misses some shaders, seemengly a few ones in the transparency/additive phase
      // All opaque and transparent materials have a CBuffer variable by this name
      const char str_to_find[] = "ALPHA_TEST_PARAM";
      const std::vector<std::byte> pattern_safety_check(reinterpret_cast<const std::byte*>(str_to_find), reinterpret_cast<const std::byte*>(str_to_find) + strlen(str_to_find));
      bool pattern_found = !System::ScanMemoryForPattern(reinterpret_cast<const std::byte*>(shader_object), shader_object_size, pattern_safety_check).empty();
#endif

      if (pattern_found)
      {
         std::vector<uint8_t> appended_patch;

         constexpr bool enable_unorm_emulation = true;
         if (enable_unorm_emulation)
         {
            std::vector<uint32_t> mov_sat_o0w_o0w = ShaderPatching::GetMovInstruction(D3D10_SB_OPERAND_TYPE_OUTPUT, 0, D3D10_SB_OPERAND_TYPE_OUTPUT, 0, true);
            appended_patch.insert(appended_patch.end(), reinterpret_cast<uint8_t*>(mov_sat_o0w_o0w.data()), reinterpret_cast<uint8_t*>(mov_sat_o0w_o0w.data()) + mov_sat_o0w_o0w.size() * sizeof(uint32_t));
            std::vector<uint32_t> max_o0xyz_o0xyz_0 = ShaderPatching::GetMaxInstruction(D3D10_SB_OPERAND_TYPE_OUTPUT, 0, D3D10_SB_OPERAND_TYPE_OUTPUT, 0);
            appended_patch.insert(appended_patch.end(), reinterpret_cast<uint8_t*>(max_o0xyz_o0xyz_0.data()), reinterpret_cast<uint8_t*>(max_o0xyz_o0xyz_0.data()) + max_o0xyz_o0xyz_0.size() * sizeof(uint32_t));
         }

         // Allocate new buffer and copy original shader code, then append the new code to fix UNORM to FLOAT texture upgrades
         // This emulates UNORM render target behaviour on FLOAT render targets (from texture upgrades), without limiting the rgb color range.
         // o0.rgb = max(o0.rgb, 0); // max is 0x34
         // o0.w = saturate(o0.w); // mov is 0x36
         new_code = std::make_unique<std::byte[]>(size + appended_patch.size());

         // Pattern to search for: 3E 00 00 01 (the last byte is the size, and the minimum is 1 (the unit is 4 bytes), given it also counts for the opcode and it's own size byte
         const std::vector<std::byte> return_pattern = { std::byte{0x3E}, std::byte{0x00}, std::byte{0x00}, std::byte{0x01} };

         // Append before the ret instruction if there's one at the end (there might not be?)
         // Our patch shouldn't pre-include a ret value (though it'd probably work anyway)!
         // Note that we could also just remove the return instruction and the shader would compile fine anyway? Unless the shader had any branches (if we added one, we should force add return!)
         if (!appended_patch.empty() && code[size - return_pattern.size()] == return_pattern[0] && code[size - return_pattern.size() + 1] == return_pattern[1] && code[size - return_pattern.size() + 2] == return_pattern[2] && code[size - return_pattern.size() + 3] == return_pattern[3])
         {
            size_t insert_pos = size - return_pattern.size();
            // Copy everything before pattern
            std::memcpy(new_code.get(), code, insert_pos);
            // Insert the patch
            std::memcpy(new_code.get() + insert_pos, appended_patch.data(), appended_patch.size());
            // Copy the rest (including the return instruction)
            std::memcpy(new_code.get() + insert_pos + appended_patch.size(), code + insert_pos, size - insert_pos);
         }
         // Append patch at the end
         else
         {
            std::memcpy(new_code.get(), code, size);
            std::memcpy(new_code.get() + size, appended_patch.data(), appended_patch.size());
         }

         size += appended_patch.size();
      }

      return new_code;
   }

   DrawOrDispatchOverrideType OnDrawOrDispatch(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, reshade::api::shader_stage stages, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, bool is_custom_pass, bool& updated_cbuffers, std::function<void()>* original_draw_dispatch_func) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);

#if DEVELOPMENT || TEST
      com_ptr<ID3D11DeviceContext> immediate_context;
      native_device->GetImmediateContext(&immediate_context);
      if (immediate_context.get() != native_device_context)
      {
         game_device_data.drew_scene = true;
      }
#endif

      // Clear up and smooth over NaNs, given that the game has a habit of outputting a few of them in rendering.
      // This requires specific AA methods enabled, but everybody should have them.
      if (!cb_luma_global_settings.GameSettings.DrewTonemap && original_shader_hashes.Contains(pixel_shader_hashes_AA))
      {
         DrawStateStack<DrawStateStackType::FullGraphics> draw_state_stack; // Use full mode because setting the RTV here might unbind the same resource being bound as SRV
         DrawStateStack<DrawStateStackType::Compute> compute_state_stack;
         draw_state_stack.Cache(native_device_context, device_data.uav_max_count);
         compute_state_stack.Cache(native_device_context, device_data.uav_max_count);

         // Manually create the RTV for the resource
         com_ptr<ID3D11Resource> resource;
         com_ptr<ID3D11ShaderResourceView> srv;
         native_device_context->PSGetShaderResources(0, 1, &srv);
         if (srv.get())
         {
            srv->GetResource(&resource);
         }
         if (resource.get() != game_device_data.scene_color_resource.get())
         {
            game_device_data.scene_color_resource = resource;
            game_device_data.scene_color_rtv.reset();
            if (game_device_data.scene_color_resource)
            {
               native_device->CreateRenderTargetView(game_device_data.scene_color_resource.get(), nullptr, &game_device_data.scene_color_rtv);
            }
         }

         // Avoids nans spreading over the transparency phase and TAA etc (character shaders occasionally spit out some)
         // Note: this is likely not needed anymore given we patch (almost) all shaders that draw on the scene buffer to avoid nans.
         if (test_index != 18)
            SanitizeNaNs(native_device, native_device_context, game_device_data.scene_color_rtv.get(), device_data, game_device_data.sanitize_nans_data, true);

#if DEVELOPMENT
         const std::shared_lock lock_trace(s_mutex_trace);
         if (trace_running)
         {
            const std::unique_lock lock_trace_2(cmd_list_data.mutex_trace);
            TraceDrawCallData trace_draw_call_data;
            trace_draw_call_data.type = TraceDrawCallData::TraceDrawCallType::Custom;
            trace_draw_call_data.command_list = native_device_context;

            trace_draw_call_data.custom_name = "Sanitize Scene Color NaNs";
            // Re-use the RTV data for simplicity
            GetResourceInfo(game_device_data.scene_color_rtv.get(), trace_draw_call_data.rt_size[0], trace_draw_call_data.rt_format[0], &trace_draw_call_data.rt_type_name[0], &trace_draw_call_data.rt_hash[0]);
            cmd_list_data.trace_draw_calls_data.insert(cmd_list_data.trace_draw_calls_data.end() - 1, trace_draw_call_data); // Add this one before any other
         }
#endif

         // Restore the compute state first, given it might be considered as output and take over SR bindings of the same resource
         compute_state_stack.Restore(native_device_context);
         draw_state_stack.Restore(native_device_context);
      }

      // Tonemapper. We need to know whether it has already drawn to balance the brightness of some follow up screen space post process effects
      if (!cb_luma_global_settings.GameSettings.DrewTonemap && original_shader_hashes.Contains(pixel_shader_hashes_Tonemap))
      {
         // Update the render resolution as it doesn't match the swapchain resolution in UW unless we have an UW fix.
         // The final tonemapper is always drawn on the main command list, and the menu uses this shader so it's initialized from the beginning.
         if (cmd_list_data.is_primary)
         {
            com_ptr<ID3D11RenderTargetView> rtv;
            native_device_context->OMGetRenderTargets(1, &rtv, nullptr);
            if (rtv)
            {
               com_ptr<ID3D11Resource> rt_resource;
               rtv->GetResource(&rt_resource);
               if (rt_resource)
               {
                  com_ptr<ID3D11Texture2D> rt;
                  HRESULT hr = rt_resource->QueryInterface(&rt);
                  if (SUCCEEDED(hr) && rt)
                  {
                     D3D11_TEXTURE2D_DESC rt_desc;
                     rt->GetDesc(&rt_desc);

                     device_data.render_resolution.x = rt_desc.Width;
                     device_data.render_resolution.y = rt_desc.Height;

                     // Wait until we have confirmed the tonemapping texture is fullscreen (no black bars) to stop the warning
                     // The game has a bug where it might allocate textures 1 pixel smaller than the output, so use a wider tolernace
                     if (pending_swapchain_resize && Math::AlmostEqual(device_data.render_resolution.x, device_data.output_resolution.x, 1.5f) && Math::AlmostEqual(device_data.render_resolution.y, device_data.output_resolution.y, 1.5f))
                     {
                        pending_swapchain_resize = false;
                     }
                  }
               }
            }
         }

         // Note: the game occasionally composes multiple viewports with multiple tonemapper passes (in different threads, likely),
         // in that case it uses partial viewports, so we can skip that case and simply pay the consequences of never setting the flag to true.
         // The solution would be to to store the flag by command list, but it really doesn't matter.
         D3D11_VIEWPORT viewport;
         uint32_t num_viewports = 1;
         native_device_context->RSGetViewports(&num_viewports, &viewport);
         if (Math::AlmostEqual(float(viewport.Width), device_data.render_resolution.x, 0.5f) && Math::AlmostEqual(float(viewport.Height), device_data.render_resolution.y, 0.5f))
         {
            cb_luma_global_settings.GameSettings.DrewTonemap = true;
            device_data.cb_luma_global_settings_dirty = true;
         }
      }

      return DrawOrDispatchOverrideType::None;
   }

   void OnPresent(ID3D11Device* native_device, DeviceData& device_data) override
   {
#if DEVELOPMENT || TEST
      auto& game_device_data = GetGameDeviceData(device_data);
      ASSERT_ONCE(!game_device_data.drew_scene || cb_luma_global_settings.GameSettings.DrewTonemap); // TODO: make sure we caught all tonemapper permutations, check the dumps for patterns otherwise
#endif
      cb_luma_global_settings.GameSettings.DrewTonemap = false;
      cb_luma_global_settings.GameSettings.InvRenderRes.x = 1.f / device_data.render_resolution.x;
      cb_luma_global_settings.GameSettings.InvRenderRes.y = 1.f / device_data.render_resolution.y;
      device_data.cb_luma_global_settings_dirty = true;
   }

   void LoadConfigs() override
   {
      reshade::api::effect_runtime* runtime = nullptr;
      reshade::get_config_value(runtime, NAME, "BloomAndLensFlareIntensity", cb_luma_global_settings.GameSettings.BloomAndLensFlareIntensity);
      reshade::get_config_value(runtime, NAME, "ColorGradingIntensity", cb_luma_global_settings.GameSettings.ColorGradingIntensity);
      reshade::get_config_value(runtime, NAME, "HDRBoostAmount", cb_luma_global_settings.GameSettings.HDRBoostAmount);
      // "device_data.cb_luma_global_settings_dirty" should already be true at this point
   }

   void DrawImGuiSettings(DeviceData& device_data) override
   {
      reshade::api::effect_runtime* runtime = nullptr;

      ImGui::NewLine();

      if (pending_swapchain_resize)
      {
         ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 200, 0, 255)); // yellow/orange
         ImGui::TextUnformatted("Warning: Your resolution isn't 16:9, for Luma to correctly patch in support for your aspect ratio,\nplease go to the game graphics settings and swap between fullscreen, borderless or windowed (any will do).\nThis message might also appear after you change resolution again, please ignore it if everything is fine.");
         ImGui::PopStyleColor();
      }

      if (ImGui::SliderFloat("Bloom and Lens Flare Intensity", &cb_luma_global_settings.GameSettings.BloomAndLensFlareIntensity, 0.f, 1.f))
      {
         reshade::set_config_value(runtime, NAME, "BloomAndLensFlareIntensity", cb_luma_global_settings.GameSettings.BloomAndLensFlareIntensity);
      }
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      {
         ImGui::SetTooltip("Note that this might not work on every scene");
      }
      DrawResetButton(cb_luma_global_settings.GameSettings.BloomAndLensFlareIntensity, 1.f, "BloomAndLensFlareIntensity", runtime);

      if (ImGui::SliderFloat("Color Grading Intensity", &cb_luma_global_settings.GameSettings.ColorGradingIntensity, 0.f, 1.f))
      {
         reshade::set_config_value(runtime, NAME, "ColorGradingIntensity", cb_luma_global_settings.GameSettings.ColorGradingIntensity);
      }
      DrawResetButton(cb_luma_global_settings.GameSettings.ColorGradingIntensity, 0.8f, "ColorGradingIntensity", runtime);

      if (cb_luma_global_settings.DisplayMode == DisplayModeType::HDR && GetShaderDefineCompiledNumericalValue(char_ptr_crc32("ENABLE_FAKE_HDR")) > 0)
      {
         if (ImGui::SliderFloat("HDR Boost", &cb_luma_global_settings.GameSettings.HDRBoostAmount, 0.f, 1.f))
         {
            reshade::set_config_value(runtime, NAME, "HDRBoostAmount", cb_luma_global_settings.GameSettings.HDRBoostAmount);
         }
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         {
            ImGui::SetTooltip("Vanilla look at 0");
         }
         DrawResetButton(cb_luma_global_settings.GameSettings.HDRBoostAmount, 0.5f, "HDRBoostAmount", runtime);
      }

#if DEVELOPMENT
      // This happens doing presentation so it should be safe as the render thread is waiting.
      // It requires a windowed/fullscreen toggle to fully apply.
      static float aspect_ratio = 1920.f;
      if (ImGui::SliderFloat("Aspect Ratio", &aspect_ratio, 960.f, 5760.f, "%.0f"))
      {
         Patches::SetOutputResolution(aspect_ratio + 0.5f, 1080.f + 0.5);
      }
#endif
   }

   void PrintImGuiAbout() override
   {
      ImGui::Text("Luma for \"Heavy Rain\" is developed by Pumbo and is open source and free.\n"
         "It adds HDR rendering and output, improved SDR output, improved tonemapping,\n"
         "and additionally it packages ultrawide fixes from Rose.\n"
         "If you enjoy it, consider donating to any of the contributors.", "");

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
      // Make the Rose button ~purple for consistency with their style
      ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(128, 0, 128, 255)); // purple
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(192, 0, 192, 255)); // lighter purple / pinkish
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(255, 128, 255, 255)); // bright pink for active
#if 1 // Second link includes the first already
      static const std::string donation_link_rose = std::string("Subscribe to Rose on Patreon ") + std::string(ICON_FK_OK);
      if (ImGui::Button(donation_link_rose.c_str()))
      {
         system("start https://www.patreon.com/rozzi");
      }
#endif
      static const std::string donation_link_rose_2 = std::string("Donate to Rose ") + std::string(ICON_FK_OK);
      if (ImGui::Button(donation_link_rose_2.c_str()))
      {
         system("start https://linktr.ee/rozziroxx");
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

         "\n\nAcknowledgments:"
         "\nRose"

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

// TODO: fix crash on swapchain present, and the very rare black screen after loading into a chapter.
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
   if (ul_reason_for_call == DLL_PROCESS_ATTACH)
   {
      Globals::SetGlobals(PROJECT_NAME, "Heavy Rain Luma mod");
      Globals::VERSION = 2;

      swapchain_format_upgrade_type = TextureFormatUpgradesType::AllowedEnabled;
      swapchain_upgrade_type = SwapchainUpgradeType::scRGB;
      texture_format_upgrades_type = TextureFormatUpgradesType::AllowedEnabled;
      // Most of these are probably not needed, the game always uses B8G8R8A8_UNORM, with no SRGB views etc
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

            reshade::api::format::r11g11b10_float,
      };
      // Without aspect ratio patches, the game will be fixed at 16:9
      texture_format_upgrades_2d_size_filters = 0 | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainResolution | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainAspectRatio | (uint32_t)TextureFormatUpgrades2DSizeFilters::CustomAspectRatio;
      prevent_fullscreen_state = true;

      pixel_shader_hashes_Tonemap.pixel_shaders = { Shader::Hash_StrToNum("B8164665"), Shader::Hash_StrToNum("8D4D1C88"), Shader::Hash_StrToNum("80408373"), Shader::Hash_StrToNum("7DF19F48"), Shader::Hash_StrToNum("146F3D1E"), Shader::Hash_StrToNum("D7C7F000"), Shader::Hash_StrToNum("E6FC219C"), Shader::Hash_StrToNum("A4E4EBA1"), Shader::Hash_StrToNum("4F74080B"), Shader::Hash_StrToNum("E7CF3D21") };
      pixel_shader_hashes_AA.pixel_shaders = { Shader::Hash_StrToNum("2AFCA697"), Shader::Hash_StrToNum("378E53A8") };

      redirected_shader_hashes["ColorGradingAndFilmGrain"] = { "B8164665", "8D4D1C88", "80408373", "7DF19F48", "146F3D1E", "D7C7F000", "E6FC219C", "A4E4EBA1", "4F74080B", "E7CF3D21" };

#if DEVELOPMENT
      forced_shader_names.emplace(std::stoul("E891B1C7", nullptr, 16), "Generate Menu Water Ripples");
      forced_shader_names.emplace(std::stoul("B8164665", nullptr, 16), "UI 3D Sprite"); // It's also the tonemapper (or well, part of it)
      forced_shader_names.emplace(std::stoul("FCCA9228", nullptr, 16), "UI Rectangle");
      forced_shader_names.emplace(std::stoul("51EC238A", nullptr, 16), "Depth of Field Composition");
      forced_shader_names.emplace(std::stoul("DA234666", nullptr, 16), "Depth of Field Composition 2 (?)");
      forced_shader_names.emplace(std::stoul("D8CA0E64", nullptr, 16), "Clear");
      forced_shader_names.emplace(std::stoul("842BBE45", nullptr, 16), "Custom Copy");
      forced_shader_names.emplace(std::stoul("EB0884FA", nullptr, 16), "Custom Copy");
      forced_shader_names.emplace(std::stoul("73ED3069", nullptr, 16), "3D UI");
      forced_shader_names.emplace(std::stoul("87DD1D36", nullptr, 16), "Noise");
      forced_shader_names.emplace(std::stoul("5CA62BE2", nullptr, 16), "Some Post Process");
#endif

      // Defaults are hardcoded in ImGUI too
      cb_luma_global_settings.GameSettings.BloomAndLensFlareIntensity = 1.f;
      cb_luma_global_settings.GameSettings.ColorGradingIntensity = 0.8f; // Don't default to 1 (vanilla) because it's too desaturated and distorted for HDR. 0.5 to 0.667 would be even nicer but would shift too much from the og look.
      cb_luma_global_settings.GameSettings.HDRBoostAmount = 0.5f;
      // TODO: use "default_luma_global_game_settings"

#if ENABLE_ORIGINAL_SHADERS_MEMORY_EDITS && 0 // We found another way
      strip_original_shaders_debug_data = true;
      sub_game_shaders_appendix = "Stripped"; // Just to not pollute the original shaders dump collection
#endif

      game = new HeavyRain();
   }
   else if (ul_reason_for_call == DLL_PROCESS_DETACH)
   {
      Patches::Uninit();
   }

   CoreMain(hModule, ul_reason_for_call, lpReserved);

   return TRUE;
}