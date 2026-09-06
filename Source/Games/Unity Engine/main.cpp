#define GAME_UNITY_ENGINE 1

#include "..\..\Core\core.hpp"

namespace
{
   // Note: these are serialized in settings so avoid reordering unless necessary.
   // These names will be set in shaders as define, so you can branch on them if really necessary.
   constexpr uint32_t GAME_UNITY_ENGINE_GENERIC = 0;
   constexpr uint32_t GAME_WHITE_KNUCKLE = 1;
   constexpr uint32_t GAME_SHADOWS_OF_DOUBT = 2;
   constexpr uint32_t GAME_VERTIGO = 3;
   constexpr uint32_t GAME_POPTLC = 4;
   constexpr uint32_t GAME_COCOON = 5;
   constexpr uint32_t GAME_SKATE_STORY = 6;
   constexpr uint32_t GAME_FAR_LONE_SAILS = 7;
   constexpr uint32_t GAME_FAR_CHANGING_TIDES = 8;
   constexpr uint32_t GAME_HOLLOW_KNIGHT_SILKSONG = 9;
   //TODOFT: add a way to add shader hashes name definitions hardcoded in code for debugging
   // TODO: PoP has a resource written by the CPU at the beginning

   // List of all the games this generic engine mod supports.
   // Other games might be supported too if they use the same shaders.
   // These might be x32 or x64 or both, the mod will only load if the architecture matches anyway.
   const std::map<std::set<std::string>, GameInfo> games_database = {
       { { "White Knuckle.exe" }, MAKE_GAME_INFO("White Knuckle", "WK", GAME_WHITE_KNUCKLE, { "Pumbo" }) },
       { { "Shadows of Doubt.exe" }, MAKE_GAME_INFO("Shadows of Doubt", "SoD", GAME_SHADOWS_OF_DOUBT, { "Pumbo" }) },
       { { "Vertigo.exe" }, MAKE_GAME_INFO("Vertigo", "VRTG", GAME_VERTIGO, { "Pumbo" }) },
       { { "TheLostCrown.exe", "TheLostCrown_plus.exe" }, MAKE_GAME_INFO("Prince of Persia: The Lost Crown", "PoPTLC", GAME_POPTLC, std::vector<std::string>({ "Ersh", "Pumbo" })), },
       { { "universe.exe" }, MAKE_GAME_INFO("COCOON", "COCN", GAME_COCOON, { "Pumbo" }) },
       { { "SkateStory.exe" }, MAKE_GAME_INFO("Skate Story", "SK8S", GAME_SKATE_STORY, { "Pumbo" }) },
       { { "FarLoneSails.exe" }, MAKE_GAME_INFO("FAR: Lone Sails", "FLS", GAME_FAR_LONE_SAILS, { "Pumbo" }) },
       { { "FarChangingTides.exe" }, MAKE_GAME_INFO("FAR: Changing Tides", "FCT", GAME_FAR_CHANGING_TIDES, { "Pumbo" }) },
       { { "Hollow Knight Silksong.exe" }, MAKE_GAME_INFO("Hollow Knight: Silksong", "HKS", GAME_HOLLOW_KNIGHT_SILKSONG, { "Pumbo" }) },
   };

   const GameInfo& GetGameInfoFromID(uint32_t id)
   {
      for (const auto& [key, value] : games_database)
      {
         if (value.id == id)
         {
            return value;
         }
      }
      static const GameInfo default_game_info = MAKE_GAME_INFO("Generic Unity Game", "", GAME_UNITY_ENGINE_GENERIC, { "" });
      return default_game_info;
   }

   // If not found, treat everything as generic (assuming default engine behaviours)
   const GameInfo* game_info = nullptr;
   uint32_t game_id = GAME_UNITY_ENGINE_GENERIC;

   // HKS
   namespace
   {
      ShaderHashesList shader_hashes_CharacterLight;
      ShaderHashesList shader_hashes_UI_VideoDecode;
      ShaderHashesList shader_hashes_UI_Sprite;
      ShaderHashesList shader_hashes_Tonemap;

      float fake_hdr_effect = 0.667f;
      float expand_hdr_gamut = 0.1f;
      float character_light_intensity = 1.f;
      float character_light_radius = 1.f;
      float character_light_smoothness = 1.f;
   }
}

struct GameDeviceDataHollowKnightSilksong final : public GameDeviceData
{
   bool video_playing = false;
   com_ptr<ID3D11Resource> video_texture;
};

class UnityEngine final : public Game
{
public:
   void OnInit(bool async) override
   {
      reshade::api::effect_runtime* runtime = nullptr;
      reshade::get_config_value(runtime, NAME, "GameID", game_id);

      // If the user didn't force the mod to behave like a specific game,
      // try to identify what game this is based on the executable name, given we have a list.
      if (game_id == 0)
      {
         const std::string executable_name = System::GetProcessExecutableName();
         for (const auto& [key, value] : games_database)
         {
            bool done = false;
            for (const auto& sub_key : key)
            {
               if (sub_key == executable_name)
               {
                  game_info = &value;
                  game_id = game_info->id;
                  done = true;
                  break;
               }
            }
            if (done)
               break;
         }
      }
      else
      {
         for (const auto& [key, value] : games_database)
         {
            if (value.id == game_id)
            {
               game_info = &value;
               break;
            }
         }
         // Fall back to generic if the game we specified didn't exist
         if (!game_info)
         {
            game_id = GAME_UNITY_ENGINE_GENERIC;
         }
      }

      if (game_info)
      {
         sub_game_shader_define = game_info->shader_define.c_str(); // This data is persistent
         sub_game_shaders_appendix = game_info->internal_name; // Make sure we dump in a sub folder, to keep them separate
      }
      // Allow to branch on behaviour for a generic mod too in shaders
      else
      {
         static_assert(GAME_UNITY_ENGINE_GENERIC == 0); // Rename the string literal here too if you rename the variable
         sub_game_shader_define = "GAME_UNITY_ENGINE_GENERIC";
      }

      char ui_type = '2';

      // Needed by "SoD" given it used sRGB views (it should work on other Unity games too)
      // Most recent Unity games do the whole post processing, UI and swapchain presentation in linear (sRGB textures), even if the swapchain isn't sRGB.
      force_vanilla_swapchain_linear = true;

      // Games like SoD have problems if upgrading random resources, they get stuck during loading screens (we could try some more advanced upgrade rules, but it's not particularly necessary).
      // Unity does bloom in HDR so upgrading mips isn't really necessary, and often it's seemingly done at full resolution anyway.
      if (game_id != GAME_SHADOWS_OF_DOUBT)
      {
         texture_format_upgrades_2d_size_filters |= (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainAspectRatio;
         // Without further mods, these games (occasionally or always) internally render at 16:9 (including the UI), with a final copy to the swapchain adding black blacks
         if (game_id == GAME_COCOON // Main menu is 16:9
            || game_id == GAME_POPTLC // Whole game is 16:9
            || game_id == GAME_FAR_CHANGING_TIDES // Whole game is 16:9
            || game_id == GAME_SKATE_STORY // Game is Vert- in Ultrawide (at least in the demo), so add proper 16:9 support to the mod
            )
         {
            texture_format_upgrades_2d_size_filters |= (uint32_t)TextureFormatUpgrades2DSizeFilters::CustomAspectRatio;
            texture_format_upgrades_2d_custom_aspect_ratios = { 16.f / 9.f };
         }
      }

      if (game_id == GAME_POPTLC)
      {
         // Needed by bloom and FXAA (at least the typeless one)
         texture_upgrade_formats.emplace(reshade::api::format::r10g10b10a2_typeless);
         texture_upgrade_formats.emplace(reshade::api::format::r10g10b10a2_unorm);

         redirected_shader_hashes["PoPTLC_Tonemap"] =
            {
               "FA1EB89D",
               "A8D65F39",
               "5A0FD042",
               "DB8E089A",
               "331779B3",
               "37BB5F3B",
               "3A60763D",
               "3B79940A",
               "486FAF9A",
               "681DD226",
               "9B9CCB1B",
               "E17B54F4",
               "EAD71346",
            };
      }

      if (game_id == GAME_HOLLOW_KNIGHT_SILKSONG)
      {
         texture_format_upgrades_2d_size_filters |= (uint32_t)TextureFormatUpgrades2DSizeFilters::CustomAspectRatio;
         prevent_fullscreen_state = false; // TODO: FSE crashes whether this is true or not. Not to be used.
         // TODO: this game resizes render targets before the swapchain, so if we upgrade by swapchain aspect ratio, sometimes it fails
         force_vanilla_swapchain_linear = false; // Game was all gamma space

         shader_hashes_CharacterLight.pixel_shaders.emplace(0xC80BBEC9);
         shader_hashes_CharacterLight.pixel_shaders.emplace(0x112C8692); // TODO: new shader that adds dithering... maybe we should warn users to disable dithering in the game settings if this is detected as Luma's dithering is better

         shader_hashes_UI_VideoDecode.pixel_shaders.emplace(std::stoul("8674BE1F", nullptr, 16));
         shader_hashes_UI_Sprite.pixel_shaders.emplace(std::stoul("2FDE313D", nullptr, 16));

         shader_hashes_Tonemap.pixel_shaders.emplace(std::stoul("12E5FE2B", nullptr, 16));
         shader_hashes_Tonemap.pixel_shaders.emplace(std::stoul("871453FD", nullptr, 16));
         shader_hashes_Tonemap.pixel_shaders.emplace(std::stoul("DD377C05", nullptr, 16));

         texture_format_upgrades_2d_aspect_ratio_pixel_threshold = 4; // Needed for videos... somehow they have border scaling

         std::vector<ShaderDefineData> game_shader_defines_data = {
            {"ENABLE_LUMA", '1', true, false, "Enables all Luma's post processing modifications, to improve the image and output HDR.", 1},
            {"ENABLE_CHARACTER_LIGHT", '1', true, false, "Allows disabling the character/hero/player light that the game uses to give visibility around the character.", 1},
            {"ENABLE_VIGNETTE", '1', true, false, "Allows disabling the vignette effect. Luma already fixes it for ultrawide given it was too strong out of the box.", 1},
            {"ENABLE_DARKNESS_EFFECT", '1', true, false, "Allows disabling the darkness effect. The game draws a veil of darkness at the edges of the screen, especially on top.", 1},
            {"ENABLE_DITHERING", '1', true, false, "Adds a pass of dithering on the HDR output, to fight off banding due to the game excessive usage of low quality textures.\nLuma already fixes most of the banding to begin with, so this is optional.\nDisabling dithering in the game's settings is suggested as Luma's doesn't need it.", 1},
            {"ENABLE_COLOR_GRADING", '1', true, false, "Disables the game's color grading LUT. The game won't look as intended without it, so just use this if you are curious.", 1},
            {"FIX_BLUR_OFFSET", '1', true, false, "The game's background blur was accidentally offsetting with each blur interation, this fixes it, making sure it looks correct at any quality setting.", 1},
         };
         shader_defines_data.append_range(game_shader_defines_data);

         GetShaderDefineData(TEST_SDR_HDR_SPLIT_VIEW_MODE_NATIVE_IMPL_HASH).SetDefaultValue('1');
         GetShaderDefineData(GAMUT_MAPPING_TYPE_HASH).SetDefaultValue('1'); // Needed in HDR
      }

      if (game_id == GAME_FAR_LONE_SAILS)
      {
         // This game was HDR out of the box (simply clipped), and thus requires no custom shaders.
         // It's not clear why the tonemapping + color grading LUT maps to an output greater than 1, but it appears like the game was just clipped.
         // TODO: for now this lacks tonemapping but it barely matters as the game isn't that bright.
         ui_type = '0';
      }

      // Games that use the ACES tonemapping LUT should go here
      if (game_id == GAME_UNITY_ENGINE_GENERIC || game_id == GAME_SHADOWS_OF_DOUBT || game_id == GAME_VERTIGO)
      {
         texture_format_upgrades_lut_size = 32;
         texture_format_upgrades_lut_dimensions = LUTDimensions::_3D;

         std::vector<ShaderDefineData> game_shader_defines_data = {
            {"TONEMAP_TYPE", '1', true, false, "0 - SDR: Vanilla (ACES)\n1 - HDR: HDR ACES (recommended)\n2 - HDR: Vanilla+ (DICE+Oklab) (SDR hue conserving)\n3 - HDR: Vanilla+ (DICE) (vibrant)\n4 - HDR: Vanilla+ (DICE+desaturation)\n5 - HDR: Untonemapped (test only)", 5},
         };
         shader_defines_data.append_range(game_shader_defines_data);
      }
      if (game_id == GAME_COCOON)
      {
         std::vector<ShaderDefineData> game_shader_defines_data = {
            {"ENABLE_FILM_GRAIN", '1', true, false, "Allows disabling the game's faint film grain effect", 1},
         };

         shader_defines_data.append_range(game_shader_defines_data);

         GetShaderDefineData(TEST_SDR_HDR_SPLIT_VIEW_MODE_NATIVE_IMPL_HASH).SetDefaultValue('1');
      }
      if (game_id == GAME_HOLLOW_KNIGHT_SILKSONG)
      {
         GetShaderDefineData(POST_PROCESS_SPACE_TYPE_HASH).SetDefaultValue('0');
         // No gamma mismatch baked in the textures as the game never applied gamma, it was gamma from the beginning (likely as an extreme optimization)!
         GetShaderDefineData(GAMMA_CORRECTION_TYPE_HASH).SetDefaultValue('1');
         GetShaderDefineData(VANILLA_ENCODING_TYPE_HASH).SetDefaultValue('1');
      }
      else
      {
         // All recent Unity games do all post processing in linear space, until the swapchain (usually included too)
         GetShaderDefineData(POST_PROCESS_SPACE_TYPE_HASH).SetDefaultValue('1');
         // All recent Unity games used sRGB textures, hence implicitly applied sRGB gamma without ever using the formula in shaders,
         // but as usual, they were likely developed and made for gamma 2.2 displays.
         GetShaderDefineData(GAMMA_CORRECTION_TYPE_HASH).SetDefaultValue('1');
      }
      // Unity games almost always have a clear last shader, so we can pre-scale by the inverse of the UI brightness, so the UI can draw at a custom brightness.
      // The UI usually draws in linear space too, though that's an engine setting.
      GetShaderDefineData(UI_DRAW_TYPE_HASH).SetDefaultValue(ui_type);
   }

   void OnCreateDevice(ID3D11Device* native_device, DeviceData& device_data) override
   {
      if (game_id == GAME_HOLLOW_KNIGHT_SILKSONG)
      {
         device_data.game = new GameDeviceDataHollowKnightSilksong;
      }
   }

   void OnInitSwapchain(reshade::api::swapchain* swapchain) override
   {
      // The game is locked to a maximum aspect ratio of 2.3916666666666666666666666666667, at least without mods
      // The video files, at least some (e.g. the opening video), are in 1916x1080 and downscale through bloom
      if (game_id == GAME_HOLLOW_KNIGHT_SILKSONG)
      {
         auto& device_data = *swapchain->get_device()->get_private_data<DeviceData>();

         float output_aspect_ratio = device_data.output_resolution.x / device_data.output_resolution.y;

         {
            const std::unique_lock lock(device_data.resource_upgrades.mutex);
            device_data.resource_upgrades.texture_format_upgrades_2d_custom_aspect_ratios = { 16.f / 9.f, 1916.f / 1080.f, min(output_aspect_ratio, 2.3916666666666666666666666666667f) };
         }
      }
   }

   DrawOrDispatchOverrideType OnDrawOrDispatch(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, reshade::api::shader_stage stages, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, bool is_custom_pass, bool& updated_cbuffers, std::function<void()>* original_draw_dispatch_func) override
   {
      // TODO: make a subclass for these games?
      if (game_id == GAME_HOLLOW_KNIGHT_SILKSONG)
      {
         auto& game_device_data = *static_cast<GameDeviceDataHollowKnightSilksong*>(device_data.game);

         if (is_custom_pass && original_shader_hashes.Contains(shader_hashes_CharacterLight))
         {
            uint custom_data_1 = 1; // Flag to tell that we are customizing the data
            uint custom_data_2 = Math::AsUInt(character_light_smoothness);
            float custom_data_3 = character_light_intensity;
            float custom_data_4 = character_light_radius;
            SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, stages, LumaConstantBufferType::LumaSettings);
            SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, stages, LumaConstantBufferType::LumaData, custom_data_1, custom_data_2, custom_data_3, custom_data_4);
            updated_cbuffers = true;
            return DrawOrDispatchOverrideType::None;
         }

         if (original_shader_hashes.Contains(shader_hashes_UI_VideoDecode))
         {
            com_ptr<ID3D11RenderTargetView> rtv;
            native_device_context->OMGetRenderTargets(1, &rtv, nullptr);
            if (rtv.get())
            {
               game_device_data.video_texture = nullptr;
               rtv->GetResource(&game_device_data.video_texture); // Note: we need to keep this in memory as the decoding shader only runs every x frames, hopefully it's not re-used for other purposes later but I doubt, it's of a specific size
            }
            return DrawOrDispatchOverrideType::None;
         }

         if (is_custom_pass && original_shader_hashes.Contains(shader_hashes_UI_Sprite))
         {
            com_ptr<ID3D11ShaderResourceView> srv;
            native_device_context->PSGetShaderResources(0, 1, &srv);
            if (srv.get())
            {
               com_ptr<ID3D11Resource> resource;
               srv->GetResource(&resource);

               if (resource == game_device_data.video_texture)
               {
                  game_device_data.video_playing = true;
               }
            }
            return DrawOrDispatchOverrideType::None;
         }

         if (is_custom_pass && original_shader_hashes.Contains(shader_hashes_Tonemap))
         {
            device_data.has_drawn_main_post_processing = true;

            uint custom_data_1 = game_device_data.video_playing; // AutoHDR on videos in the tonemap pass
            float custom_data_3 = fake_hdr_effect; // Note that this will apply over some black screens with UI too, and Menus, because they pass through tonemapping (and it's cool that they do!)
            float custom_data_4 = expand_hdr_gamut / 4.0; // From 0-1 to 0-0.25, which in shader becomes from 1-1.25
            SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, stages, LumaConstantBufferType::LumaSettings);
            SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, stages, LumaConstantBufferType::LumaData, custom_data_1, 0, custom_data_3, custom_data_4);
            updated_cbuffers = true;
            return DrawOrDispatchOverrideType::None;
         }
      }

      return DrawOrDispatchOverrideType::None;
   }

   void OnPresent(ID3D11Device* native_device, DeviceData& device_data) override
   {
      if (game_id == GAME_HOLLOW_KNIGHT_SILKSONG)
      {
         auto& game_device_data = *static_cast<GameDeviceDataHollowKnightSilksong*>(device_data.game);
         game_device_data.video_playing = false;
         //game_device_data.video_texture = nullptr;
         
         device_data.has_drawn_main_post_processing = true;
      }
   }


   void LoadConfigs() override
   {
      reshade::api::effect_runtime* runtime = nullptr;
      reshade::get_config_value(runtime, NAME, "FakeHDREffect", fake_hdr_effect);
      reshade::get_config_value(runtime, NAME, "ExpandHDRGamut", expand_hdr_gamut);
      reshade::get_config_value(runtime, NAME, "CharacterLightIntensity", character_light_intensity);
      reshade::get_config_value(runtime, NAME, "CharacterLightRadius", character_light_radius);
      reshade::get_config_value(runtime, NAME, "CharacterLightSmoothness", character_light_smoothness);
   }

   void DrawImGuiSettings(DeviceData& device_data) override
   {
      reshade::api::effect_runtime* runtime = nullptr;

      ImGui::NewLine();

      if (game_id == GAME_HOLLOW_KNIGHT_SILKSONG)
      {
         if (cb_luma_global_settings.DisplayMode == DisplayModeType::HDR)
         {
            if (ImGui::SliderFloat("HDR Boost", &fake_hdr_effect, 0.f, 1.f)) // Call it "HDR Boost" instead of "Fake HDR" to make it more appealing (it's cool, it's just a highlights curve)
            {
               reshade::set_config_value(runtime, NAME, "FakeHDREffect", fake_hdr_effect);
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            {
               ImGui::SetTooltip("\"Artificially\" increases the amount of highlights in the game, given that the game's lighting was created for SDR and is fairly flat.\nHigher values are better to be reserved for lower \"Scene Paper White\" values.");
            }
            DrawResetButton(fake_hdr_effect, 0.667f, "FakeHDREffect", runtime);

            if (ImGui::SliderFloat("Expand Color Gamut", &expand_hdr_gamut, 0.f, 1.f)) // Call it "HDR Boost" instead of "Fake HDR" to make it more appealing (it's cool, it's just a highlights curve)
            {
               reshade::set_config_value(runtime, NAME, "ExpandHDRGamut", expand_hdr_gamut);
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            {
               ImGui::SetTooltip("Increases the saturation of colors, expanding into HDR color gamuts.\nThe game is meant to look desaturated so don't overdo it.\n0 is neutral/vanilla.");
            }
            DrawResetButton(expand_hdr_gamut, 0.1f, "ExpandHDRGamut", runtime);
         }

         ImGui::SetNextItemOpen(true, ImGuiCond_Once);
         if (ImGui::TreeNode("Advanced Settings"))
         {
            if (ImGui::SliderFloat("Character Light Intensity", &character_light_intensity, 0.f, 2.f))
            {
               reshade::set_config_value(runtime, NAME, "CharacterLightIntensity", character_light_intensity);
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            {
               ImGui::SetTooltip("Allows you to change the intensity (brightness) of the light around the player character that the game uses to boost visibility for gameplay purposes.\nIt can be a bit jarring in HDR (or SDR too, depending on your taste).");
            }
            DrawResetButton(character_light_intensity, 1.f, "CharacterLightIntensity", runtime);

            if (ImGui::SliderFloat("Character Light Radius", &character_light_radius, 0.f, 2.f))
            {
               reshade::set_config_value(runtime, NAME, "CharacterLightRadius", character_light_radius);
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            {
               ImGui::SetTooltip("Allows you to change the radius (range) of the light around the player character that the game uses to boost visibility for gameplay purposes.");
            }
            DrawResetButton(character_light_radius, 1.f, "CharacterLightRadius", runtime);

            if (ImGui::SliderFloat("Character Light Smoothness", &character_light_smoothness, 0.f, 2.f))
            {
               reshade::set_config_value(runtime, NAME, "CharacterLightSmoothness", character_light_smoothness);
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            {
               ImGui::SetTooltip("Allows you to change the smoothness (falloff) of the light around the player character that the game uses to boost visibility for gameplay purposes.");
            }
            DrawResetButton(character_light_smoothness, 1.f, "CharacterLightSmoothness", runtime);

            ImGui::TreePop();
         }
      }
   }

#if DEVELOPMENT
   void DrawImGuiDevSettings(DeviceData& device_data) override
   {
      reshade::api::effect_runtime* runtime = nullptr;

      // This can only be changed in a development enviroment, given it's not very necessary if not for debugging,
      // and we'd need to add re-initialization code (and also expose the names ImGUI).
      // Don't change it if you want to keep the automatic detection on boot.
      std::string game_name = GetGameInfoFromID(game_id).title;
      if (game_id <= 0) // Automatic mode (no forced game)
      {
         game_name = "Auto";
      }
      // TODO: turn into a drop down list (we'd need to store an extra setting)
      if (ImGui::SliderInt("Game ID", &(int&)game_id, 0, games_database.size(), game_name.c_str()))
      {
         reshade::set_config_value(runtime, NAME, "GameID", game_id);
      }
#if 0
      int current_game_id = game_id;

      // Build an array of names or get from your database
      std::vector<const char*> game_names;
      game_names.push_back("Auto");
      game_names.reserve(games_database.size());
      for (auto& game : games_database) {
         game_names.push_back(game.second.title.c_str());
      }

      if (ImGui::Combo("Game ID", &current_game_id, game_names.data(), (int)game_names.size()))
      {
         game_id = current_game_id;
         reshade::set_config_value(runtime, NAME, "GameID", game_id);
      }
#endif
   }
#endif

   void PrintImGuiAbout() override
   {
      auto FormatAuthors = [](const std::vector<std::string>& authors) -> std::string
         {
            if (authors.empty()) return "Unknown";
            if (authors.size() == 1) return authors[0];
            if (authors.size() == 2) return authors[0] + " and " + authors[1];

            std::string result;
            for (size_t i = 0; i < authors.size() - 1; ++i)
            {
               result += authors[i] + ", ";
            }
            result += "and " + authors.back();
            return result;
         };

      const std::string game_title = game_info ? game_info->title : "Unity Engine";
      const std::string mod_authors = game_info ? FormatAuthors(game_info->mod_authors) : "Pumbo";
      ImGui::Text(("Luma for " + game_title + " is developed by " + mod_authors + ". It is open source and free.\nIf you enjoy it, consider donating.").c_str(), "");

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
      if (ImGui::Button(donation_link_pumbo_2.c_str())) //TODOFT5: add to all of my mods!
      {
         system("start https://ko-fi.com/realpumbo");
      }
      ImGui::PopStyleColor(3);

      ImGui::NewLine();
      // Restore the previous color, otherwise the state we set would persist even if we popped it
      ImGui::PushStyleColor(ImGuiCol_Button, button_color);
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, button_hovered_color);
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, button_active_color);
#if 1 //TODOFT: add nexus link here and below and in all other mods
      if (game_id == GAME_HOLLOW_KNIGHT_SILKSONG)
      {
         static const std::string mod_link = std::string("Nexus Mods Page ") + std::string(ICON_FK_SEARCH);
         if (ImGui::Button(mod_link.c_str()))
         {
            system("start https://www.nexusmods.com/hollowknightsilksong/mods/23");
         }
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
      ImGui::Text(("Credits:"
         "\n\nMod:"
         "\n" + mod_authors +
         "\nLuma Framework:"
         "\nPumbo"

         "\n\nThird Party:"
         "\nReShade"
         "\nImGui"
         "\nRenoDX"
         "\n3Dmigoto"
         "\nOklab"
         "\nACES"
         "\nDICE (HDR tonemapper)").c_str()
         , "");
   }
};

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
   if (ul_reason_for_call == DLL_PROCESS_ATTACH)
   {
      Globals::SetGlobals(PROJECT_NAME, "Unity Engine Luma mod");
      Globals::VERSION = 2;

      // Unity apparently never uses these
      luma_settings_cbuffer_index = 13;
      luma_data_cbuffer_index = 12;

      swapchain_format_upgrade_type = TextureFormatUpgradesType::AllowedEnabled;
      swapchain_upgrade_type = SwapchainUpgradeType::scRGB;
      texture_format_upgrades_type = TextureFormatUpgradesType::AllowedEnabled;
      texture_format_upgrades_2d_size_filters = 0 | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainResolution;
      // PoPTLC only requires r8g8b8a8_typeless (and "r10g10b10a2_typeless" with FXAA) but will work with others regardless
      texture_upgrade_formats = {
            reshade::api::format::r8g8b8a8_unorm,
            reshade::api::format::r8g8b8a8_unorm_srgb,
            reshade::api::format::r8g8b8a8_typeless,
            reshade::api::format::r11g11b10_float,
      };

#if !DEVELOPMENT
      // Delete inside files even if the mod moved because they were overly long!
      old_shader_file_names.emplace("INSD_Tonemap_0x2FE2C060_0xBEC46939_0x90337E76_0x8DEE69CB_0xBA96FA20_0x2D6B78F6_0xA5777313_0x0AE21975_0x519DF6E7_0xC5DABDD4_0x9D414A70_0xBFAB5215_0xC4065BE1 _0xF0503978.ps_4_0.hlsl");
      old_shader_file_names.emplace("INSD_Tonemap_0x2FE2C060_0xBEC46939_0x90337E76_0x8DEE69CB_0xBA96FA20_0x2D6B78F6_0xA5777313_0x0AE21975_0x519DF6E7_0xC5DABDD4_0x9D414A70_0xBFAB5215_0xC4065BE1_0xF0503978.ps_4_0.hlsl");
#endif

#if DEVELOPMENT // Unity flips Y coordinates on all textures until the final swapchain draws
      debug_draw_options |= (uint32_t)DebugDrawTextureOptionsMask::FlipY;
#endif

      game = new UnityEngine();
   }

   CoreMain(hModule, ul_reason_for_call, lpReserved);

   return TRUE;
}