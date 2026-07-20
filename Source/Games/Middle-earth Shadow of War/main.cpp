#define MIDDLE_EARTH_SHADOW_OF_WAR 1

#define ENABLE_NGX 1

#include "..\..\Core\core.hpp"

// TODO: Fix this globaly? Define NOMINMAX before including windows.h.
#undef min
#undef max

namespace
{
   // Shader Hashes
   const ShaderHashesList shader_hashes_linearize_depth_and_generate_mvs = {.compute_shaders = {0x0E095BB1}};
   const ShaderHashesList shader_hashes_TAA = {.pixel_shaders = {0x8D06D556}};
   const ShaderHashesList shader_hashes_tonemap = {.pixel_shaders = {0xA4592384, 0xFD32C367}};
   const ShaderHashesList shader_hashes_photomode_exposure = {.pixel_shaders = {0xF4316866}};
   const ShaderHashesList shader_hashes_swapchain = {.pixel_shaders = {0x68EABB8D, 0x09C22C0E}};

   // Hashes for new Shader Defines
   constexpr uint32_t GAMMA_CORRECT_CUSTOM = char_ptr_crc32("GAMMA_CORRECT_CUSTOM");
   constexpr uint32_t TEST_USER_PEAK = char_ptr_crc32("TEST_USER_PEAK");
   constexpr uint32_t FIRE_RETUNED = char_ptr_crc32("FIRE_RETUNED");
   constexpr uint32_t RCAS_ENABLED = char_ptr_crc32("RCAS_ENABLED");

   // Jitter from TAA cb
   float g_jitter_x;
   float g_jitter_y;

   // A copy of device_data.sr_type
   SR::Type sr_type_copy = SR::Type::None;
   
   bool is_ui = true; // User toggle for UI skip draw
   bool sr_copy_resource = false; // User toggle to exec CopyResource() to fix missing bloom
   bool sr_user_allow_upgraded_samplers = false; // Let user control ignore_upgraded_samplers
}

struct GameDeviceDataMiddleEarthShadowOfWar final : GameDeviceData
{
   bool drawn_tonemap = false;
   bool drawn_swapchain = false;
   
   void ResetDrawnState()
   {
      drawn_tonemap = false;
      drawn_swapchain = false;
   }
};

namespace DisplayMode
{
   static bool IsHDR() { return cb_luma_global_settings.DisplayMode == DisplayModeType::HDR; }
   
   static bool is_first = true;
   constexpr auto flag_file = "Luma_AlwaysHDRLaunch";
   
   // Change to HDR and set brightness settings //TODO: Make ChangeDisplayMode() publicly available from core.hpp
   void ChangeDisplayModeHDR(reshade::api::effect_runtime* runtime, bool enable_hdr_on_display = true, IDXGISwapChain3* swapchain = nullptr)
   {
      DisplayModeType display_mode = DisplayModeType::HDR;
      int display_mode_i = int(DisplayModeType::HDR);
      reshade::set_config_value(runtime, NAME, "DisplayMode", display_mode_i);
      cb_luma_global_settings.DisplayMode = display_mode;
      OnDisplayModeChanged();
      if (display_mode >= DisplayModeType::HDR)
      {
         if (enable_hdr_on_display)
         {
            Display::SetHDREnabled(game_window);
            bool dummy_bool;
            Display::IsHDRSupportedAndEnabled(game_window, dummy_bool, hdr_enabled_display, swapchain);
         }
         if (!reshade::get_config_value(runtime, NAME, "ScenePeakWhite", cb_luma_global_settings.ScenePeakWhite) || cb_luma_global_settings.ScenePeakWhite <= 0.f)
         {
            cb_luma_global_settings.ScenePeakWhite = default_paper_white;
         }
         
         if (use_os_reference_white_level)
         {
            float hdr_paper_white = 80.f;
            if (Display::GetSDRWhiteLevel(0, hdr_paper_white))
            {
               cb_luma_global_settings.ScenePaperWhite = hdr_paper_white;
               cb_luma_global_settings.UIPaperWhite = hdr_paper_white;
            }
            else
            {
               use_os_reference_white_level = false;
            }
         }
         else
         {
            if (!reshade::get_config_value(runtime, NAME, "ScenePaperWhite", cb_luma_global_settings.ScenePaperWhite))
            {
               cb_luma_global_settings.ScenePaperWhite = default_paper_white;
            }
            if (!reshade::get_config_value(runtime, NAME, "UIPaperWhite", cb_luma_global_settings.UIPaperWhite))
            {
               cb_luma_global_settings.UIPaperWhite = default_paper_white;
            }
         }
      }
   };

   // Forces user to launch in SDR mode for proper HDR upgrades.
   static void OnInitSwapchain(reshade::api::swapchain* swapchain)
   {
      // gatekeep: is_first
      if (!is_first) return;
      is_first = false;
      
      // If is HDR, tell user, force, and exit.
      bool has_file = std::filesystem::exists(flag_file);
      bool enabled;
      bool supported;
      Display::IsHDRSupportedAndEnabled(game_window, supported, enabled);
      if (enabled)
      {
         // inform user
         if (!has_file) MessageBoxA(game_window, "For proper HDR upgrades, we need the game to launch in SDR mode.\n\nLuma will try to disable Windows HDR and relaunch.\nThis will become automated if successful.", "Relaunch Required", MB_OK | MB_ICONINFORMATION);
         
         // disable HDR
         if (Display::SetHDREnabled(game_window, false))
         {
            MessageBoxA(game_window, "Failed to disable Windows HDR.\n\nPlease disable it manually and relaunch the game.", "HDR Disable Failed", MB_OK | MB_ICONERROR);
            std::exit(1);
         }
         
         // start new process
         {
            // ModuleFileName
            char exe_path[MAX_PATH];
            GetModuleFileNameA(NULL, exe_path, MAX_PATH);
            
            // start
            STARTUPINFOA si;
            PROCESS_INFORMATION pi;
            ZeroMemory(&si, sizeof(si));
            si.cb = sizeof(si);
            ZeroMemory(&pi, sizeof(pi));
            if (!CreateProcessA(exe_path, NULL, NULL, NULL, FALSE, CREATE_NEW_CONSOLE, NULL, NULL, &si, &pi))
            {
               MessageBoxA(game_window, "Failed to relaunch the game in SDR mode.\n\nPlease relaunch manually.", "Relaunch Failed", MB_OK | MB_ICONERROR);
               std::exit(1);
            }
         }

         // create flag file
         if (!has_file)
         {
            std::ofstream stream(flag_file);
            stream.close();
         }
         
         std::exit(0); //exit
      }
      else
      {
         ChangeDisplayModeHDR(nullptr);
      }
      

      is_first = false;
   }
}

// Shader Defines helpers to draw ImGui
namespace ShaderDefines
{
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

class MiddleEarthShadowOfWar final : public Game
{
public:
   static GameDeviceDataMiddleEarthShadowOfWar& GetGameDeviceData(DeviceData& device_data)
   {      
      return *(GameDeviceDataMiddleEarthShadowOfWar*)device_data.game;
   }

   void OnLoad(std::filesystem::path& file_path, bool failed = false) override
   {
      // log
      message(reshade::log::level::info, "OnLoad()");
      
      reshade::register_event<reshade::addon_event::update_buffer_region>(OnUpdateBufferRegion);
   }

   void OnInit(bool async) override
   {
      // log
      message(reshade::log::level::info, "OnInit()");

      // Shader Defines: append new
      static const std::vector<ShaderDefineData> game_shader_defines_data = {
         {"GAMMA_CORRECTION_RANGE_TYPE", '0', true, !DEVELOPMENT, "0 - Full range.\n1 - 0-1 only.", 1},
         {"TEST_USER_PEAK", '0', true, false, "Show white rectangles for peak test.", 1},
         {"GAMMA_CORRECT_CUSTOM", '1', true, false, "Correct gamma decode, lowering shadows to match SDR.", 1},
         {"FIRE_RETUNED", '1', true, false, "Retuned fire shader to reduce clipping.", 1},
         {"RCAS_ENABLED", '0', true, false, "Robust Contrast Adaptive Sharpening.", 1},
      };
      shader_defines_data.append_range(game_shader_defines_data);

      // Shader Defines: change defaults
      GetShaderDefineData(POST_PROCESS_SPACE_TYPE_HASH).SetDefaultValue('1'); // linear
      GetShaderDefineData(GAMMA_CORRECTION_TYPE_HASH).SetDefaultValue('0'); // don't use built in gamma correction
      GetShaderDefineData(UI_DRAW_TYPE_HASH).SetDefaultValue('2'); // Direct (inverse scene brightness draws)

      // Force
      auto_recompile_defines = true;

      // CB init default values
      default_luma_global_game_settings.WhiteClip = cb_luma_global_settings.GameSettings.WhiteClip = 1.;
      default_luma_global_game_settings.BlowoutCorrection = cb_luma_global_settings.GameSettings.BlowoutCorrection = 0.3;
      default_luma_global_game_settings.GodRays = cb_luma_global_settings.GameSettings.GodRays = 1.;
      default_luma_global_game_settings.Bloom = cb_luma_global_settings.GameSettings.Bloom = 1.;
      default_luma_global_game_settings.RCAS = cb_luma_global_settings.GameSettings.RCAS = 0.;

      // UI Buffer Indirect Upgrades
      {
         // Add many, since Alt-Tabbing (swapchain recreates) will clear upgrades.
         auto_texture_format_upgrade_shader_hashes[0x3D829665] = std::pair{std::vector<uint8_t>{0}, std::vector<uint8_t>()}; // ui base
         auto_texture_format_upgrade_shader_hashes[0x61BC2E86] = std::pair{std::vector<uint8_t>{0}, std::vector<uint8_t>()}; // ui glow
         auto_texture_format_upgrade_shader_hashes[0xE6453EB0] = std::pair{std::vector<uint8_t>{0}, std::vector<uint8_t>()}; // ui idk...
         auto_texture_format_upgrade_shader_hashes[0x34A2050F] = std::pair{std::vector<uint8_t>{0}, std::vector<uint8_t>()}; // ui trans
         auto_texture_format_upgrade_shader_hashes[0x9BA33763] = std::pair{std::vector<uint8_t>{0}, std::vector<uint8_t>()}; // ui text
         auto_texture_format_upgrade_shader_hashes[0xB2EAAA62] = std::pair{std::vector<uint8_t>{0}, std::vector<uint8_t>()}; // ui mov

         auto_texture_format_upgrade_shader_hashes[0xFD32C367] = std::pair{std::vector<uint8_t>{0}, std::vector<uint8_t>()}; // tonemap SDR
      }

      // cb inject indices
      luma_settings_cbuffer_index = 13;
      luma_data_cbuffer_index = 12;
   }

   void OnInitSwapchain(reshade::api::swapchain* swapchain)
   {
      // log
      message(reshade::log::level::info, "OnInitSwapchain()");
      
      // DisplayMode force restart into SDR mode
      DisplayMode::OnInitSwapchain(swapchain);
   }

   static bool OnUpdateBufferRegion(reshade::api::device* device, const void* data, reshade::api::resource resource, uint64_t offset, uint64_t size)
   {
      // RETURN: no SR
      if (sr_type_copy == SR::Type::None) return false;
      
      // Find TAA jitter
      auto native_resource = (ID3D11Resource*)resource.handle;
      ComPtr<ID3D11Buffer> buffer;
      auto hr = native_resource->QueryInterface(buffer.put());
      if (SUCCEEDED(hr))
      {
         D3D11_BUFFER_DESC desc;
         buffer->GetDesc(&desc);

         // This alone should be reliable? Needs testing!
         if (desc.BindFlags == D3D11_BIND_CONSTANT_BUFFER && desc.ByteWidth == 544)
         {
            // cb0[31].xy in PS TAA 0x8D06D556.
            g_jitter_x = ((float4*)data)[31].x;
            g_jitter_y = ((float4*)data)[31].y;
         }
      }
      return false;
   }

   DrawOrDispatchOverrideType OnDrawOrDispatch(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, reshade::api::shader_stage stages, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, bool is_custom_pass, bool& updated_cbuffers, std::function<void()>* original_draw_dispatch_func) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);
      auto& managed_resources = game_device_data.managed_resources;

      // Depth & Motion Vectors (SR)
      if (device_data.sr_type != SR::Type::None && original_shader_hashes.Contains(shader_hashes_linearize_depth_and_generate_mvs))
      {
         // Take depth
         ComPtr<ID3D11ShaderResourceView> srv;
         native_device_context->CSGetShaderResources(1, 1, srv.put());
         srv->GetResource(managed_resources.resources["depth"_h].put());
         return DrawOrDispatchOverrideType::None;
      }

      // TODO AO

      // TAA (SR)
      if (device_data.sr_type != SR::Type::None && original_shader_hashes.Contains(shader_hashes_TAA))
      {
         // DLSS requires an immediate context for execution!
         ASSERT_ONCE(native_device_context->GetType() == D3D11_DEVICE_CONTEXT_IMMEDIATE);

         // Settings
         auto* sr_instance_data = device_data.GetSRInstanceData();
         ASSERT_ONCE(sr_instance_data);

         SR::SettingsData settings_data;
         settings_data.output_width = device_data.output_resolution.x;
         settings_data.output_height = device_data.output_resolution.y;
         settings_data.render_width = device_data.render_resolution.x;
         settings_data.render_height = device_data.render_resolution.y;
         settings_data.dynamic_resolution = false;
         settings_data.hdr = true;
         settings_data.inverted_depth = true;
         settings_data.mvs_jittered = false;

         // MVs are in UV space so we need to scale them to screen space for DLSS.
         settings_data.mvs_x_scale = -device_data.render_resolution.x;
         settings_data.mvs_y_scale = -device_data.render_resolution.y;

         settings_data.render_preset = dlss_render_preset;
         settings_data.auto_exposure = true;

         sr_implementations[device_data.sr_type]->UpdateSettings(sr_instance_data, native_device_context, settings_data);

         // Get resources
         std::array<ID3D11ShaderResourceView*, 2> srvs;
         native_device_context->PSGetShaderResources(0, srvs.size(), srvs.data());

         ComPtr<ID3D11Resource> resource_mvs;
         srvs[0]->GetResource(resource_mvs.put());
         ComPtr<ID3D11Resource> resource_scene;
         srvs[1]->GetResource(resource_scene.put());

         ComPtr<ID3D11RenderTargetView> rtv;
         native_device_context->OMGetRenderTargets(1, rtv.put(), nullptr);
         ComPtr<ID3D11Resource> resource_rt;
         rtv->GetResource(resource_rt.put());

         SR::SuperResolutionImpl::DrawData draw_data;
         draw_data.source_color = resource_scene.get();
         draw_data.output_color = resource_rt.get();
         draw_data.motion_vectors = resource_mvs.get();
         draw_data.depth_buffer = managed_resources.resources["depth"_h].get();

         // Jitters are in range [-1, 1].
         draw_data.jitter_x = g_jitter_x * -0.5f;
         draw_data.jitter_y = g_jitter_y * 0.5f;

         draw_data.render_width = device_data.render_resolution.x;
         draw_data.render_height = device_data.render_resolution.y;

         // DRAW
         device_data.has_drawn_sr = sr_implementations[device_data.sr_type]->Draw(sr_instance_data, native_device_context, draw_data);

         // Copy back to input, fixing missing transparency FXs.
         if (sr_copy_resource) native_device_context->CopyResource(resource_scene.get(), resource_rt.get());

         // Reset pointers
         ResetCOMArray(srvs);

         return DrawOrDispatchOverrideType::Replaced;
      }

      // Tonemap
      if (original_shader_hashes.Contains(shader_hashes_tonemap))
      {
         game_device_data.drawn_tonemap = true;
         return DrawOrDispatchOverrideType::None;
      }

      // Swapchain / UI Combine
      if (original_shader_hashes.Contains(shader_hashes_swapchain))
      {
         game_device_data.drawn_swapchain = true;
         return DrawOrDispatchOverrideType::None;
      }

      // User UI toggle
      if (!is_ui && game_device_data.drawn_tonemap && !game_device_data.drawn_swapchain)
      {
         return DrawOrDispatchOverrideType::Skip;
      }
      
      return DrawOrDispatchOverrideType::None;
   }

   void OnPresent(ID3D11Device* native_device, DeviceData& device_data) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);

      // game_device_data reset pipeline state tracking
      game_device_data.ResetDrawnState();

      // force_reset_sr
      if (!device_data.has_drawn_sr) device_data.force_reset_sr = true;
      sr_type_copy = device_data.sr_type;

      // texture_mip_lod_bias_offset
      if (!custom_texture_mip_lod_bias_offset)
      {
         std::shared_lock shared_lock_samplers(s_mutex_samplers);
         if (device_data.sr_type != SR::Type::None && !device_data.sr_suppressed && !sr_user_allow_upgraded_samplers)
         {
            device_data.texture_mip_lod_bias_offset = SR::GetMipLODBias(device_data.render_resolution.y, device_data.output_resolution.y); // This results in -1 at output res
         }
         else
         {
            device_data.texture_mip_lod_bias_offset = 0.0f;
         }
      }
   }

   void LoadConfigs() override
   {
      message(reshade::log::level::info, "LoadConfigs()");
      reshade::api::effect_runtime* runtime = nullptr;
      
      // CB load user
      reshade::get_config_value(runtime, NAME, "WhiteClip", cb_luma_global_settings.GameSettings.WhiteClip);
      reshade::get_config_value(runtime, NAME, "BlowoutCorrection", cb_luma_global_settings.GameSettings.BlowoutCorrection);
      reshade::get_config_value(runtime, NAME, "GodRays", cb_luma_global_settings.GameSettings.GodRays);
      reshade::get_config_value(runtime, NAME, "Bloom", cb_luma_global_settings.GameSettings.Bloom);
      reshade::get_config_value(runtime, NAME, "RCAS", cb_luma_global_settings.GameSettings.RCAS);

      // SRUser
      reshade::get_config_value(runtime, NAME, "sr_copy_resource", sr_copy_resource);
      reshade::get_config_value(runtime, NAME, "sr_user_allow_upgraded_samplers", sr_user_allow_upgraded_samplers);
      ignore_upgraded_samplers = !sr_user_allow_upgraded_samplers;
   }

   void DrawImGuiSettings(DeviceData& device_data) override
   {
      reshade::api::effect_runtime* runtime = nullptr;
      
      ImGui::Separator(); ////////////////////////////////////////////////////////////////////////////

      // SECTION: Gamma Correction
      if (DisplayMode::IsHDR() && ImGui::CollapsingHeader("Gamma Correction"))
      {
         ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.75f, 1.f, 1.f, 1.f));
         ImGui::TextWrapped("[Windows' gamma decode in HDR is weaker than in SDR, causing washed out shadows.]");
         ImGui::PopStyleColor();
         
         ShaderDefines::UIToggleCheckmark(GAMMA_CORRECT_CUSTOM, "Enable Correction", "sRGB -> 2.2");
         
         if (ImGui::Button("Further Explanation & Test (Google Slides)"))
            Website::OpenWebsite("https://docs.google.com/presentation/d/e/2PACX-1vSXeLHlbm6repcS7fels1-SXYGRmzziRrnuJ8nDO8J5rsWV3dT1-nVyCKp0Tj_stwx-9qlCI-N6rYIT/pub?start=false&loop=false&slide=id.g3e007eafba8_0_0");
      }

      // SECTION: Peak Brightness
      if (DisplayMode::IsHDR() && ImGui::CollapsingHeader("Peak Brightness"))
      {
         ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.75f, 1.f, 1.f, 1.f));
         ImGui::TextWrapped("[Don't Exceed Display Maximum!]");
         ImGui::PopStyleColor();
         
         ImGui::Bullet(); ImGui::SameLine(); ImGui::TextWrapped("(Use default Luma sliders above.)");
         ShaderDefines::UIToggleCheckmark(TEST_USER_PEAK, "Test Pattern (Read Tooltip)", "3 rectangles inside a big one.\n- Left should disappear.\n- Middle should be barely visible.\n- Right should be easy to see.");

         ImGui::NewLine();

         ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.75f, 1.f, 1.f, 1.f));
         ImGui::TextWrapped("[Extended Highlights Customization]");
         ImGui::PopStyleColor();
         
         ImGui::PushID("Peak Brightness: WhiteClip");
         if (ImGui::SliderFloat("Clip", &cb_luma_global_settings.GameSettings.WhiteClip, 0.f, 4.f, "%.3f"))
            reshade::set_config_value(runtime, NAME, "WhiteClip", cb_luma_global_settings.GameSettings.WhiteClip);
         ImGui::PopID();
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Increase to make rolloff compression straighter, where the highlights becomes more clipped.");
         DrawResetButton(cb_luma_global_settings.GameSettings.WhiteClip, default_luma_global_game_settings.WhiteClip, "WhiteClip", runtime);

         ImGui::PushID("Peak Brightness: BlowoutCorrection");
         if (ImGui::SliderFloat("Blowout Correction", &cb_luma_global_settings.GameSettings.BlowoutCorrection, 0.f, 0.8f, "%.3f"))
            reshade::set_config_value(runtime, NAME, "BlowoutCorrection", cb_luma_global_settings.GameSettings.BlowoutCorrection);
         ImGui::PopID();
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Correct the hues of new highlights using the results from SDR.\nToo high will look unnatural.");
         DrawResetButton(cb_luma_global_settings.GameSettings.BlowoutCorrection, default_luma_global_game_settings.BlowoutCorrection, "BlowoutCorrection", runtime);

      }

      // SECTION: SR
      if (ImGui::CollapsingHeader("Temporal Anti-Aliasing"))
      {
         if (device_data.sr_type == SR::Type::None)
         {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.75f * 0.5, 1.f * 0.5, 1.f * 0.5, 1.f));
            ImGui::TextWrapped("[Super Resolution: Disabled]");
            ImGui::PopStyleColor();

            ImGui::Bullet(); ImGui::SameLine(); ImGui::TextWrapped("Vanilla TAA is pending a fix.\nIt'll artefact to black for extremely bright highlights.");

         }
         else
         {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.75f, 1.f, 1.f, 1.f));
            ImGui::TextWrapped("[Super Resolution: Enabled]");
            ImGui::PopStyleColor();
         
            ImGui::Bullet(); ImGui::SameLine(); ImGui::TextWrapped("Requires TAA!");
            ImGui::Bullet(); ImGui::SameLine(); ImGui::TextWrapped("Unfortunately, only 100%% internal/render resolution is supported.");

            ImGui::NewLine();

            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.75f, 1.f, 1.f, 1.f));
            ImGui::TextWrapped("[Super Resolution: Bloom & Transparency Missing Fix]");
            ImGui::PopStyleColor();

            ImGui::PushID("SR: copy_resource"); 
            if (ImGui::Checkbox("Copy Resource Fix", &sr_copy_resource))
               reshade::set_config_value(runtime, NAME, "sr_copy_resource", sr_copy_resource);
            ImGui::PopID();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
               ImGui::SetTooltip("If Bloom and other transparency are missing, enable this.");
            DrawResetButton(sr_copy_resource, false, "sr_copy_resource", runtime);
         }

         ImGui::NewLine();

         ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.75f, 1.f, 1.f, 1.f));
         ImGui::TextWrapped("[Higher Quality Textures Sampling]");
         ImGui::PopStyleColor();

         ImGui::PushID("SR: sr_user_allow_upgraded_samplers"); 
         if (ImGui::Checkbox("Allow Sampler Upgrades (Read Tooltip)", &sr_user_allow_upgraded_samplers))
         {
            reshade::set_config_value(runtime, NAME, "sr_user_allow_upgraded_samplers", sr_user_allow_upgraded_samplers);
            ignore_upgraded_samplers = !sr_user_allow_upgraded_samplers;
         }
         ImGui::PopID();
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Upgrade texture samplers to retain detail at a distance, though unnoticeable?\nMay change more than just texture detail.\nWill costs some performance.");
         DrawResetButton(sr_user_allow_upgraded_samplers, false, "sr_user_allow_upgraded_samplers", runtime);
      }

      // SECTION: Miscellaneous
      if (ImGui::CollapsingHeader("Miscellaneous"))
      {
         ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.75f, 1.f, 1.f, 1.f));
         ImGui::TextWrapped("[Various FX sliders & toggles.]");
         ImGui::PopStyleColor();

         if (ImGui::SliderFloat("GodRays", &cb_luma_global_settings.GameSettings.GodRays, 0.f, 2.f, "%.3f"))
            reshade::set_config_value(runtime, NAME, "GodRays", cb_luma_global_settings.GameSettings.GodRays);
         ImGui::PopID();
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Final god rays multiplier.");
         DrawResetButton(cb_luma_global_settings.GameSettings.GodRays, default_luma_global_game_settings.GodRays, "GodRays", runtime);
         
         ImGui::PushID("Miscellaneous: Bloom");
         if (ImGui::SliderFloat("Bloom", &cb_luma_global_settings.GameSettings.Bloom, 0.f, 2.f, "%.3f"))
            reshade::set_config_value(runtime, NAME, "Bloom", cb_luma_global_settings.GameSettings.Bloom);
         ImGui::PopID();
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Final bloom multiplier.");
         DrawResetButton(cb_luma_global_settings.GameSettings.Bloom, default_luma_global_game_settings.Bloom, "Bloom", runtime);

         ImGui::PushID("Miscellaneous: RCAS");
         if (ImGui::SliderFloat("RCAS", &cb_luma_global_settings.GameSettings.RCAS, 0.f, 1.f, "%.3f"))
            reshade::set_config_value(runtime, NAME, "RCAS", cb_luma_global_settings.GameSettings.RCAS);
         ImGui::PopID();
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Robust Contrast Adaptive Sharpening on scene color.");
         DrawResetButton(cb_luma_global_settings.GameSettings.RCAS, default_luma_global_game_settings.RCAS, "RCAS", runtime);
         ShaderDefines::Set(RCAS_ENABLED, cb_luma_global_settings.GameSettings.RCAS > 0.f); //bruh

         // TODO: AO
         
         ImGui::PushID("Miscellaneous: UIToggle is_allowed");
         ImGui::Checkbox("UI", &is_ui);
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Can skip drawing the UI (everything after tonemap shader).");
         ImGui::PopID();
      }

      // SECTION: Retuned Fire 
      if (ImGui::CollapsingHeader("Retuned Fire"))
      {
         ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.75f, 1.f, 1.f, 1.f));
         ImGui::TextWrapped("[Alleviates clipping for fire texture sprites.]");
         ImGui::PopStyleColor();

         ImGui::Bullet(); ImGui::SameLine(); ImGui::TextWrapped("This changes its style, becoming more lava-ish orange.");
         ImGui::Bullet(); ImGui::SameLine(); ImGui::TextWrapped("If off, shaders will intentionally break, so just ignore the warning. Thanks!");

         ShaderDefines::UIToggleCheckmark(FIRE_RETUNED, "Enable Retuning", "Reduces fire texture clipping.");
      }
      
      if (DEVELOPMENT) ImGui::Separator(); ////////////////////////////////////////////////////////////////////////////
   }

   void PrintImGuiAbout() override
   {
      ImGui::Text("Build Date:");
      ImGui::Text(__DATE__);
      ImGui::Text(__TIME__);
      ImGui::NewLine();
      
      ImGui::Text("Middle-earth: Shadow of War Luma mod - about and credits section", "");
   }

   void OnDisplayModeChanged()
   {
      // FORCED: Gamma Correct
      ShaderDefines::Set(GAMMA_CORRECT_CUSTOM, DisplayMode::IsHDR());
   }
};

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
   if (ul_reason_for_call == DLL_PROCESS_ATTACH)
   {
      // Globals
      Globals::SetGlobals(PROJECT_NAME, "Middle-earth: Shadow of War Luma mod");
      Globals::VERSION = 1;
      
      // Swapchain upgrades
      swapchain_format_upgrade_type = TextureFormatUpgradesType::AllowedEnabled;
      swapchain_upgrade_type = SwapchainUpgradeType::scRGB;
      
      // Resource upgrades
      texture_format_upgrades_type = TextureFormatUpgradesType::AllowedEnabled;
      enable_chain_indirect_texture_format_upgrades = ChainTextureFormatUpgradesType::DirectDependencies;

      // SR
      enable_samplers_upgrade = true;

      game = new MiddleEarthShadowOfWar();
   }

   CoreMain(hModule, ul_reason_for_call, lpReserved);

   return TRUE;
}