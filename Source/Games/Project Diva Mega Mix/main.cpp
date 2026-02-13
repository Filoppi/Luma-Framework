#define GAME_PROJECTDIVAMEGAMIX 1

#define GEOMETRY_SHADER_SUPPORT 0
#define ALLOW_SHADERS_DUMPING 0
// #define ENABLE_POST_DRAW_CALLBACK 0

#include "..\..\Core\core.hpp"

namespace TonemapInfo
{
   static int FlagDrawnTonemap =     0x40000000; //1<<30
   // static int FlagSprites =          0x20000000; //1<<29
   // static int FlagComplex =          0x10000000; //1<<28
   static int FlagDrawnFinal =       0x08000000; //1<<27
   static int FlagIsFMV =            0x04000000; //1<<26
   static int FlagDrawnHPBarDelta =  0x02000000; //1<<25
   static int IndexBitMask =         0x0000000F;
         
   int GetDefaultReset() { return 0; }
         
   int SetDrawnTonemapTrue(int v) { return v | FlagDrawnTonemap; }
   bool GetDrawnTonemap(int v) { return (v & FlagDrawnTonemap) > 0; }
         
   // int SetSpritesTrue(int v) { return v | FlagSprites; }
   // bool GetSprites(int v) { return (v & FlagSprites) > 0; }
   //       
   // int SetComplexTrue(int v) { return v | FlagComplex; }
   // bool GetComplex(int v) { return (v & FlagComplex) > 0; }

   int SetDrawnFinalTrue(int v) { return v | FlagDrawnFinal; }
   bool GetDrawnFinal(int v) { return (v & FlagDrawnFinal) > 0; }

   int SetIsFMVTrue(int v) { return v | FlagIsFMV; }
   bool GetIsFMV(int v) { return (v & FlagIsFMV) > 0; }

   int SetDrawnHPBarDeltaTrue(int v) { return v | FlagDrawnHPBarDelta; }
   bool GetDrawnHPBarDelta(int v) { return (v & FlagDrawnHPBarDelta) > 0; }
         
   int SetIndexAndDrawnTonemapTrue(int v, int i) { return v | FlagDrawnTonemap | (IndexBitMask & i); }
   int GetIndex(int v) { return v & IndexBitMask; }
   int GetIndexOnlyIfDrawn(int v) { return GetDrawnTonemap(v) ? v & IndexBitMask : -1; }

   const char* const TonemapDebugInfo[] = {
      "FutureTone", //0
      "FutureTone, BGSprites", //1
      "FutureTone", //2
      "FutureTone, BGSprites", //3
      "FutureTone, BGSprites", //4
      "MegaMix", //5
      "MegaMix", //6
      "MegaMix, BGSprites", //7
      "MegaMix, BGSprites", //8
      "MegaMix", //9
      "MegaMix, BGSprites (Customization)", //10
   };
}

namespace ShaderHashesLists
{
   ShaderHashesList Tonemap0;
   ShaderHashesList Tonemap1;
   ShaderHashesList Tonemap2;
   ShaderHashesList Tonemap3;
   ShaderHashesList Tonemap4;
   ShaderHashesList Tonemap5;
   ShaderHashesList Tonemap6;
   ShaderHashesList Tonemap7;
   ShaderHashesList Tonemap8;
   ShaderHashesList Tonemap9;
   ShaderHashesList Tonemap10;
   ShaderHashesList MLAA;
   ShaderHashesList Final;
   ShaderHashesList Mov;
   ShaderHashesList UISpritesHPBarDelta;
   ShaderHashesList UISpritesText;
   ShaderHashesList ToSwapchain;
   
}
void ShaderHashesLists_Setup()
{
   ShaderHashesLists::Tonemap0.pixel_shaders.emplace(std::stoul("0x7CFCDF1A", nullptr, 16)); //complex
   ShaderHashesLists::Tonemap1.pixel_shaders.emplace(std::stoul("0x6047C5DE", nullptr, 16)); //complex sprite
   ShaderHashesLists::Tonemap2.pixel_shaders.emplace(std::stoul("0x8CAB805E", nullptr, 16)); //complex (un-witnessed)
   ShaderHashesLists::Tonemap3.pixel_shaders.emplace(std::stoul("0x87371E76", nullptr, 16)); //complex sprites (un-witnessed)
   ShaderHashesLists::Tonemap4.pixel_shaders.emplace(std::stoul("0xB3273DF8", nullptr, 16)); //complex sprites (un-witnessed)
   ShaderHashesLists::Tonemap5.pixel_shaders.emplace(std::stoul("0x55660220", nullptr, 16)); //fast
   ShaderHashesLists::Tonemap6.pixel_shaders.emplace(std::stoul("0x29307B56", nullptr, 16)); //fast (un-witnessed)
   ShaderHashesLists::Tonemap7.pixel_shaders.emplace(std::stoul("0x5A8C281C", nullptr, 16)); //fast sprites (un-witnessed)
   ShaderHashesLists::Tonemap8.pixel_shaders.emplace(std::stoul("0xCBB08175", nullptr, 16)); //fast sprites 
   ShaderHashesLists::Tonemap9.pixel_shaders.emplace(std::stoul("0xD4CB36EE", nullptr, 16)); //fast (un-witnessed)
   ShaderHashesLists::Tonemap10.pixel_shaders.emplace(std::stoul("0xF6BEC634", nullptr, 16)); //fast sprites (in customization)

   
   
   ShaderHashesLists::MLAA.pixel_shaders.emplace(std::stoul("0x3ACC6F7A", nullptr, 16)); //edge0
   ShaderHashesLists::MLAA.pixel_shaders.emplace(std::stoul("0x5DA2FE05", nullptr, 16)); //edge1
   ShaderHashesLists::MLAA.pixel_shaders.emplace(std::stoul("0x5C5FD160", nullptr, 16)); //resolve
   
   ShaderHashesLists::Final.pixel_shaders.emplace(std::stoul("0x56443BE9", nullptr, 16));
   
   ShaderHashesLists::Mov.pixel_shaders.emplace(std::stoul("0x62D69253", nullptr, 16));
   
   ShaderHashesLists::UISpritesHPBarDelta.pixel_shaders.emplace(std::stoul("0xD0162389", nullptr, 16));
   ShaderHashesLists::UISpritesText.pixel_shaders.emplace(std::stoul("0x7F6C8EC7", nullptr, 16));
   
   ShaderHashesLists::ToSwapchain.pixel_shaders.emplace(std::stoul("0xA200B172", nullptr, 16));
}

namespace Globals
{
   static bool IsUI = true;
   static bool IsFullscreenOverlayFx = true;
   static int TonemapInfoBackup = 0;
   static int SwapchainChangeCount = 0;
   static bool IsSkipUntilUI = false;
   static bool IsSkipTextAfterFinal = false;
}

struct ProjectDivaMegaMixGameDeviceData final : public GameDeviceData
{
   bool DrawnToSwapchain = false;
   
   // com_ptr<ID3D11Texture2D> UIOutputTexOrig = nullptr;
   //
   // com_ptr<ID3D11Texture2D> UIOutputTex = nullptr;
   // D3D11_TEXTURE2D_DESC UIOutputTexDesc;
   //
   // com_ptr<ID3D11RenderTargetView> UIOutputRtv = nullptr;
   // D3D11_RENDER_TARGET_VIEW_DESC UIOutputRtvDesc;
   //
   // com_ptr<ID3D11ShaderResourceView> UIOutputSrv = nullptr;
   // D3D11_RENDER_TARGET_VIEW_DESC UIOutputSrvDesc;
   //
   // bool IsFinalCopyToken = false;
   
   void ResetOnSwapchain()
   {
      // //invalidate
      // UIOutputTex = nullptr;
      // UIOutputRtv = nullptr;
      // UIOutputSrv = nullptr;
   }
   
   void ResetOnPresent()
   {
      DrawnToSwapchain = false;
      // IsFinalCopyToken = false;
   }
};

namespace ShaderDefineInfo
{
   constexpr uint32_t CUSTOM_TONEMAP                   = char_ptr_crc32("CUSTOM_TONEMAP");
   constexpr uint32_t CUSTOM_HDTVREC709                = char_ptr_crc32("CUSTOM_HDTVREC709");
   constexpr uint32_t CUSTOM_PCBLOWOUT = char_ptr_crc32("CUSTOM_PCBLOWOUT");
   constexpr uint32_t CUSTOM_FAKEBT2020 = char_ptr_crc32("CUSTOM_FAKEBT2020");
   constexpr uint32_t CUSTOM_LUT_BLOWOUT_REDUCTION = char_ptr_crc32("CUSTOM_LUT_BLOWOUT_REDUCTION");
   constexpr uint32_t CUSTOM_COLORGRADE = char_ptr_crc32("CUSTOM_COLORGRADE");
   constexpr uint32_t CUSTOM_UPSCALE_MOV = char_ptr_crc32("CUSTOM_UPSCALE_MOV");
   constexpr uint32_t CUSTOM_UPSCALE_BGSPRITES = char_ptr_crc32("CUSTOM_UPSCALE_BGSPRITES");
   constexpr uint32_t CUSTOM_UPSCALE_TOON = char_ptr_crc32("CUSTOM_UPSCALE_TOON");
   constexpr uint32_t CUSTOM_HUDBRIGHTNESS = char_ptr_crc32("CUSTOM_HUDBRIGHTNESS");

   static char InvertCharBool(char b)
   {
      return b == '0' ? '1' : '0'; 
   }
   
   //This feels dumb O(n) everytime, but it is the most consistent.
   static int Get(uint32_t p)
   {
      auto* d = &GetShaderDefineData(p);
      return d->editable_data.value[0] - '0';
   }

   static void Set(uint32_t p, char c)
   {
      auto* d = &GetShaderDefineData(p);
      d->SetValue(c);
      defines_need_recompilation = true;
   }

   static void ToggleBool(uint32_t p)
   {
      auto* d = &GetShaderDefineData(p);
      d->SetValue(InvertCharBool(d->editable_data.value[0]));
      defines_need_recompilation = true;
   }
}

class ProjectDivaMegaMix final : public Game
{
   // Helper to hide ugly casts
   static ProjectDivaMegaMixGameDeviceData& GetGameDeviceData(DeviceData& device_data)
   {
      return *static_cast<ProjectDivaMegaMixGameDeviceData*>(device_data.game);
   }
   
public:
   void OnInit(bool async) override
   {
      //Def
      std::vector<ShaderDefineData> game_shader_defines_data = {
         {"GAMMA_CORRECTION_RANGE_TYPE", '0', true, !DEVELOPMENT, "0 - Full range.\n1 - 0-1 only.", 1},
         {"SWAPCHAIN_SKIPALL", '0', true, false, "Skip majority of the swapchain proxy shader (DisplayComposite.hlsl).\nWill not decode gamma if shaders are disabled/unloaded.", 1},
         {"SWAPCHAIN_CLAMP_PEAK", '0', true, false, "Clamp the absolute final color.\n0 - Unclamped (up to display).\n1 - Per channel clamp (blows out).\n2 - Scale down by max channel (sat preserving).", 2},
         {"SWAPCHAIN_CLAMP_COLORSPACE", '0', true, !DEVELOPMENT, "Clamp colorspace against invalid colors.\n(Really only for OCD, as it should only be inconsequential black.)\n0 - Unclamped.\n1 - BT2020.", 1},
         {"_____CUSTOM_____", '0', true, false, "Just a divider.", 1},
         {"CUSTOM_TONEMAP", '2', true, false, "HDR tonemapper, primarily the shoulder.\n0 - Off (Unclamped).\n1 - Reinhard Piecewise (Gradual)\n2 - Frostbite Exponential Rolloff (Aggressive)", 2},
         {"CUSTOM_TONEMAP_CLAMP", '2', true, false, "Clamp overshoot from luma scaled HDR tonemap.\n0 - Unclamped (up to display).\n1 - Per channel clamp (blows out).\n2 - Scale down by max channel (sat preserving).", 2},
         {"CUSTOM_TONEMAP_PERCHANNEL", '0', true, false, "HDR tonemap scaling.\n(Only for the curious because as it's destructive, stacking on top of SDR Influence.)\n0 - Luminance\n1 - Per-Channel", 1},
         {"CUSTOM_TONEMAP_TRYIGNOREUI", '0', true, false, "If only UI is rendering, deactivates HDR tonemapper.", 1},
         {"CUSTOM_PCBLOWOUT", '1', true, false, "Choose how the per-channel blowout is done.\n0 - No Tonemap, Clamp/Saturate (Original)\n1 - Reinhard Piecewise (Gradual)\n2 - Frostbite Exponential Rolloff (Aggressive)", 2},
         {"CUSTOM_FAKEBT2020", '1', true, false, "Encode BT2020 before gamma decode to push colors out to wcg.", 1},
         {"CUSTOM_LUT_BLOWOUT_REDUCTION", '1', true, false, "Enable YUV LUT blowout reduction.", 1},
         {"CUSTOM_COLORGRADE", '0', true, false, "Enable HDR Color Grading.\n0 - Off\n1 - During final (working in only BT709, but won't affect UI).\n2 - During swapchain (working in BT2020).", 2},
         {"CUSTOM_UPSCALE_MOV", '2', true, false, "PumboAutoHDR for FMV.\n0 - Off\n1 - On\n2 - On and disable HDR tonemap when active.", 2},
         {"CUSTOM_UPSCALE_BGSPRITES", '1', true, false, "Auto HDR (Inverse Tonemap) for background 2D sprites in complex \"Future Tone\" scenes (e.g. Torinoko City).", 1},
         {"CUSTOM_UPSCALE_TOON", '2', true, false, "Auto HDR for flat toon \"Switch\" scenes (e.g. Catch the Wave, Deep Sea City Underground, etc.).\n0 - Off\n1 - On\n2 - Ignore Customization Menu", 2},
         {"CUSTOM_HUDBRIGHTNESS", '1', true, false, "Sample shader texture resources to detect specific UI to change their brightness.\nElse, they are too bright.", 1},
         {"CUSTOM_TONEMAP_IDENTIFY", '0', true, !DEVELOPMENT, "Draw binary representation of tonemap uber variant number.", 1},
         {"CUSTOM_HDTVREC709", '0', true, false, "Decode color and swapchain to HDTV rec.709, like PS4's display output.", 1},
      };
      shader_defines_data.append_range(game_shader_defines_data);
      assert(shader_defines_data.size() < MAX_SHADER_DEFINES);
      
      //Default built-in
      GetShaderDefineData(POST_PROCESS_SPACE_TYPE_HASH).SetDefaultValue('0');
      GetShaderDefineData(EARLY_DISPLAY_ENCODING_HASH).SetDefaultValue('0');
      GetShaderDefineData(VANILLA_ENCODING_TYPE_HASH).SetDefaultValue('1');
      GetShaderDefineData(GAMMA_CORRECTION_TYPE_HASH).SetDefaultValue('1');
      GetShaderDefineData(UI_DRAW_TYPE_HASH).SetDefaultValue('2');
      if (!DEVELOPMENT)
      {
         GetShaderDefineData(TEST_SDR_HDR_SPLIT_VIEW_MODE_NATIVE_IMPL_HASH).SetValueFixed(true);
         GetShaderDefineData(TEST_SDR_HDR_SPLIT_VIEW_MODE_NATIVE_IMPL_HASH).editable = false;
         GetShaderDefineData(char_ptr_crc32("TEST_SDR_HDR_SPLIT_VIEW_MODE")).SetValueFixed(true);
         GetShaderDefineData(char_ptr_crc32("TEST_SDR_HDR_SPLIT_VIEW_MODE")).editable = false;
      }
      
      //cb
      luma_settings_cbuffer_index = 13;
      luma_data_cbuffer_index = 12;

      //GameSettings default
      default_luma_global_game_settings.TonemapperRolloffStart = cb_luma_global_settings.GameSettings.TonemapperRolloffStart = 36.f;
      default_luma_global_game_settings.Exposure = cb_luma_global_settings.GameSettings.Exposure = 1.0f;
      default_luma_global_game_settings.BloomStrength = cb_luma_global_settings.GameSettings.BloomStrength = 1.f;
      
      default_luma_global_game_settings.LUT = cb_luma_global_settings.GameSettings.LUT = 1.0f;
      default_luma_global_game_settings.LUTBlowoutReduction = cb_luma_global_settings.GameSettings.LUTBlowoutReduction = 0.225f;
      default_luma_global_game_settings.LUTBlowoutReductionLookBack = cb_luma_global_settings.GameSettings.LUTBlowoutReductionLookBack = 0.61f;
      
      default_luma_global_game_settings.PCBlowoutStrength = cb_luma_global_settings.GameSettings.PCBlowoutStrength = 1.f;
      default_luma_global_game_settings.PCBlowoutStart = cb_luma_global_settings.GameSettings.PCBlowoutStart = 0.95f;
      default_luma_global_game_settings.PCBlowoutEnd = cb_luma_global_settings.GameSettings.PCBlowoutEnd = 1.15f;
      
      default_luma_global_game_settings.FakeBT2020ChromaCorrect = cb_luma_global_settings.GameSettings.FakeBT2020ChromaCorrect = 0.55f;
      default_luma_global_game_settings.FakeBT2020LumaCorrect = cb_luma_global_settings.GameSettings.FakeBT2020LumaCorrect = 0.35f;
      
      default_luma_global_game_settings.UpscaleMovPumboPow = cb_luma_global_settings.GameSettings.UpscaleMovPumboPow = 3.6f;
      default_luma_global_game_settings.UpscaleBGSpritesMax = cb_luma_global_settings.GameSettings.UpscaleBGSpritesMax = 4.4f;
      default_luma_global_game_settings.UpscaleBGSpritesExp = cb_luma_global_settings.GameSettings.UpscaleBGSpritesExp = 0.18f;
      default_luma_global_game_settings.UpscaleToonMax = cb_luma_global_settings.GameSettings.UpscaleToonMax = 1.25f;
      default_luma_global_game_settings.UpscaleToonExp = cb_luma_global_settings.GameSettings.UpscaleToonExp = 0.18f;
      
      default_luma_global_game_settings.HUDBrightnessHealthBar = cb_luma_global_settings.GameSettings.HUDBrightnessHealthBar = 1.0f;
      default_luma_global_game_settings.HUDBrightnessHealthBarDelta = cb_luma_global_settings.GameSettings.HUDBrightnessHealthBarDelta = 0.5f;
      default_luma_global_game_settings.HUDBrightnessProgressBar = cb_luma_global_settings.GameSettings.HUDBrightnessProgressBar = 0.8f;
      default_luma_global_game_settings.HUDBrightnessNoteResponse = cb_luma_global_settings.GameSettings.HUDBrightnessNoteResponse = 0.75f;
      default_luma_global_game_settings.HUDBrightnessHoldComboBg = cb_luma_global_settings.GameSettings.HUDBrightnessHoldComboBg = 0.5f;
      default_luma_global_game_settings.HUDBrightnessPJDLogo = cb_luma_global_settings.GameSettings.HUDBrightnessPJDLogo = 1.0f;
      
      default_luma_global_game_settings.CGContrast = cb_luma_global_settings.GameSettings.CGContrast = 1.f;
      default_luma_global_game_settings.CGContrastMidGray = cb_luma_global_settings.GameSettings.CGContrastMidGray = 36.f;
      default_luma_global_game_settings.CGSaturation = cb_luma_global_settings.GameSettings.CGSaturation = 1.f;
      default_luma_global_game_settings.CGHighlightsStrength = cb_luma_global_settings.GameSettings.CGHighlightsStrength = 1.f;
      default_luma_global_game_settings.CGHighlightsMidGray = cb_luma_global_settings.GameSettings.CGHighlightsMidGray = 36.f;
      default_luma_global_game_settings.CGShadowsStrength = cb_luma_global_settings.GameSettings.CGShadowsStrength = 1.f;
      default_luma_global_game_settings.CGShadowsMidGray = cb_luma_global_settings.GameSettings.CGShadowsMidGray = 36.f;
   }

   // This needs to be overridden with your own "GameDeviceData" sub-class (destruction is automatically handled)
   void OnCreateDevice(ID3D11Device* native_device, DeviceData& device_data) override
   {
      //device_data.game
      device_data.game = new ProjectDivaMegaMixGameDeviceData;
   }

   void OnInitSwapchain(reshade::api::swapchain* swapchain)
   {
      auto& device_data = *swapchain->get_device()->get_private_data<DeviceData>();
      auto& game_device_data = GetGameDeviceData(device_data);

      //game_device_data
      game_device_data.ResetOnSwapchain();
      
      //SwapchainChangeCount
      Globals::SwapchainChangeCount++;
   }

   DrawOrDispatchOverrideType OnDrawOrDispatch(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, reshade::api::shader_stage stages, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, bool is_custom_pass, bool& updated_cbuffers, std::function<void()>* original_draw_dispatch_func) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);

      //Skip: no pixel shader
      size_t ps_size = original_shader_hashes.pixel_shaders.size();
      if (ps_size == 0) return DrawOrDispatchOverrideType::None; 
      
      // TONEMAP UBER ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
      
      if (!TonemapInfo::GetDrawnFinal(cb_luma_global_settings.GameSettings.TonemapInfo) && //if final went, no tonemap possible
         !TonemapInfo::GetDrawnTonemap(cb_luma_global_settings.GameSettings.TonemapInfo))
      {
         //get
         int ti = cb_luma_global_settings.GameSettings.TonemapInfo;
         
         //complex (future tone)
         if (original_shader_hashes.Contains(ShaderHashesLists::Tonemap0))
         {
            ti = TonemapInfo::SetIndexAndDrawnTonemapTrue(ti, 0);
            // ti = TonemapInfo::SetComplexTrue(ti);
         }
         else if (original_shader_hashes.Contains(ShaderHashesLists::Tonemap1))
         {
            ti = TonemapInfo::SetIndexAndDrawnTonemapTrue(ti, 1);
            // ti = TonemapInfo::SetComplexTrue(ti);
            // ti = TonemapInfo::SetSpritesTrue(ti);
         }
         else if (original_shader_hashes.Contains(ShaderHashesLists::Tonemap2))
         {
            ti = TonemapInfo::SetIndexAndDrawnTonemapTrue(ti, 2);
            // ti = TonemapInfo::SetComplexTrue(ti);
         }
         else if (original_shader_hashes.Contains(ShaderHashesLists::Tonemap3))
         {
            ti = TonemapInfo::SetIndexAndDrawnTonemapTrue(ti, 3);
            // ti = TonemapInfo::SetComplexTrue(ti);
            // ti = TonemapInfo::SetSpritesTrue(ti);
         }
         else if (original_shader_hashes.Contains(ShaderHashesLists::Tonemap4))
         {
            ti = TonemapInfo::SetIndexAndDrawnTonemapTrue(ti, 4);
            // ti = TonemapInfo::SetComplexTrue(ti);
            // ti = TonemapInfo::SetSpritesTrue(ti);
         }
         //fast (cartoon)
         else if (original_shader_hashes.Contains(ShaderHashesLists::Tonemap5))
         {
            ti = TonemapInfo::SetIndexAndDrawnTonemapTrue(ti, 5);
         }
         else if (original_shader_hashes.Contains(ShaderHashesLists::Tonemap6))
         {
            ti = TonemapInfo::SetIndexAndDrawnTonemapTrue(ti, 6);
         }
         else if (original_shader_hashes.Contains(ShaderHashesLists::Tonemap7))
         {
            ti = TonemapInfo::SetIndexAndDrawnTonemapTrue(ti, 7);
            // ti = TonemapInfo::SetSpritesTrue(ti);
         }
         else if (original_shader_hashes.Contains(ShaderHashesLists::Tonemap8))
         {
            ti = TonemapInfo::SetIndexAndDrawnTonemapTrue(ti, 8);
            // ti = TonemapInfo::SetSpritesTrue(ti);
         }
         else if (original_shader_hashes.Contains(ShaderHashesLists::Tonemap9))
         {
            ti = TonemapInfo::SetIndexAndDrawnTonemapTrue(ti, 9);
         }
         else if (original_shader_hashes.Contains(ShaderHashesLists::Tonemap10))
         {
            ti = TonemapInfo::SetIndexAndDrawnTonemapTrue(ti, 10);
            // ti = TonemapInfo::SetSpritesTrue(ti);
         }

         //set
         if (TonemapInfo::GetDrawnTonemap(ti))
         {
            cb_luma_global_settings.GameSettings.TonemapInfo = ti;
            device_data.cb_luma_global_settings_dirty = true;
            
            return DrawOrDispatchOverrideType::None;
         }

         //continue...
      }
      
      // FULLSCREEN OVERLAY FX ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

      //See EXTRA

      // AA ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
      
      // FINAL /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
      
      if (!TonemapInfo::GetDrawnFinal(cb_luma_global_settings.GameSettings.TonemapInfo) &&
         original_shader_hashes.Contains(ShaderHashesLists::Final))
      {
         //drawn
         cb_luma_global_settings.GameSettings.TonemapInfo = TonemapInfo::SetDrawnFinalTrue(cb_luma_global_settings.GameSettings.TonemapInfo);
         device_data.has_drawn_main_post_processing = true;
         device_data.cb_luma_global_settings_dirty = true;

         // //has UIOutputRtv setup from prev frame?
         // if (game_device_data.UIOutputRtv.get() != nullptr)
         // {
         //    //give token
         //    game_device_data.IsFinalCopyToken = true;
         // }
         
         return DrawOrDispatchOverrideType::None;
      }

      // UI /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

      //See EXTRA
      // Includes PV's FXs but also HUD.

      //Mov
      if (TonemapInfo::GetDrawnFinal(cb_luma_global_settings.GameSettings.TonemapInfo) &&
         original_shader_hashes.Contains(ShaderHashesLists::Mov))
      {
         //flag
         cb_luma_global_settings.GameSettings.TonemapInfo = TonemapInfo::SetIsFMVTrue(cb_luma_global_settings.GameSettings.TonemapInfo);
         device_data.cb_luma_global_settings_dirty = true;

         return DrawOrDispatchOverrideType::None;
      }

      //HPBarDelta
      if (TonemapInfo::GetDrawnFinal(cb_luma_global_settings.GameSettings.TonemapInfo) &&
         !TonemapInfo::GetDrawnHPBarDelta(cb_luma_global_settings.GameSettings.TonemapInfo) &&
         original_shader_hashes.Contains(ShaderHashesLists::UISpritesHPBarDelta))
      {
         //flag
         cb_luma_global_settings.GameSettings.TonemapInfo = TonemapInfo::SetDrawnHPBarDeltaTrue(cb_luma_global_settings.GameSettings.TonemapInfo);
         device_data.cb_luma_global_settings_dirty = true;
      }

      // //IsFinalCopyToken
      // if (game_device_data.IsFinalCopyToken)
      // {
      //    //use token
      //    game_device_data.IsFinalCopyToken = false;
      //
      //    //error: not exist
      //    ASSERT(game_device_data.UIOutputTexOrig.get() != nullptr);
      //
      //    //copy
      //    native_device_context->CopyResource(game_device_data.UIOutputTex.get(), game_device_data.UIOutputTexOrig.get());
      // }

      // TO SWAPCHAIN /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
      
      if (TonemapInfo::GetDrawnFinal(cb_luma_global_settings.GameSettings.TonemapInfo) &&
         !game_device_data.DrawnToSwapchain &&
         original_shader_hashes.Contains(ShaderHashesLists::ToSwapchain))
      {
         game_device_data.DrawnToSwapchain = true;

         // if (game_device_data.UIOutputTex.get() == nullptr)
         // {
         //    //shader res 0
         //    com_ptr<ID3D11ShaderResourceView> srv;
         //    native_device_context->PSGetShaderResources(0, 1, &srv);
         //    ASSERT(srv.get() != nullptr);
         //
         //    //get resource
         //    com_ptr<ID3D11Resource> srv_res;
         //    srv->GetResource(&srv_res);
         //    ASSERT(SUCCEEDED(srv_res != nullptr));
         //    
         //    //get tex
         //    com_ptr<ID3D11Texture2D> srv_tex;
         //    auto hr0 = srv_res->QueryInterface(&srv_tex);
         //    ASSERT(SUCCEEDED(hr0));
         //    game_device_data.UIOutputTexOrig = srv_tex; //save for later
         //
         //    //get desc
         //    D3D11_TEXTURE2D_DESC stv_tex_desc;
         //    srv_tex->GetDesc(&stv_tex_desc);
         //    
         //    //create desc unorm
         //    game_device_data.UIOutputTexDesc.Width          = stv_tex_desc.Width;
         //    game_device_data.UIOutputTexDesc.Height         = stv_tex_desc.Height;
         //    game_device_data.UIOutputTexDesc.MipLevels      = stv_tex_desc.MipLevels;
         //    game_device_data.UIOutputTexDesc.ArraySize      = stv_tex_desc.ArraySize;
         //    game_device_data.UIOutputTexDesc.Format         = stv_tex_desc.Format /*DXGI_FORMAT_R16G16B16A16_UNORM*/;
         //    game_device_data.UIOutputTexDesc.SampleDesc     = stv_tex_desc.SampleDesc;
         //    game_device_data.UIOutputTexDesc.Usage          = stv_tex_desc.Usage;
         //    game_device_data.UIOutputTexDesc.BindFlags      = stv_tex_desc.BindFlags;
         //    game_device_data.UIOutputTexDesc.CPUAccessFlags = stv_tex_desc.CPUAccessFlags;
         //    game_device_data.UIOutputTexDesc.MiscFlags      = stv_tex_desc.MiscFlags;
         //    
         //    //create tex
         //    auto hr1 = native_device->CreateTexture2D(&game_device_data.UIOutputTexDesc, nullptr, &game_device_data.UIOutputTex);
         //    ASSERT(SUCCEEDED(hr1));
         //    
         //    //create rtv for later
         //    auto hr2 = native_device->CreateRenderTargetView(game_device_data.UIOutputTex.get(), nullptr, &game_device_data.UIOutputRtv);
         //    ASSERT(SUCCEEDED(hr2));
         //    
         //    //create shader res for later
         //    auto hr3 = native_device->CreateShaderResourceView(game_device_data.UIOutputTex.get(), nullptr, &game_device_data.UIOutputSrv);
         //    ASSERT(SUCCEEDED(hr3));
         //
         //    //skip so shader dont explode (just 1 frame)
         //    return DrawOrDispatchOverrideType::Skip;
         // }
         //
         // //add ui tex as shader res
         // native_device_context->PSSetShaderResources(1, 1, &game_device_data.UIOutputSrv);
         
         return DrawOrDispatchOverrideType::None;
      }

      // EXTRA /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

      //FULLSCREEN OVERLAY FX
      if (!Globals::IsFullscreenOverlayFx &&
         TonemapInfo::GetDrawnTonemap(cb_luma_global_settings.GameSettings.TonemapInfo) &&
         !TonemapInfo::GetDrawnFinal(cb_luma_global_settings.GameSettings.TonemapInfo) &&
         !original_shader_hashes.Contains(ShaderHashesLists::MLAA))
      {
         return DrawOrDispatchOverrideType::Skip;
      }
      
      //UI
      if (TonemapInfo::GetDrawnFinal(cb_luma_global_settings.GameSettings.TonemapInfo) &&
         !game_device_data.DrawnToSwapchain &&
         !original_shader_hashes.Contains(ShaderHashesLists::Mov))
      {
         //skip IsUI
         if (!Globals::IsUI) return DrawOrDispatchOverrideType::Skip;

         //skip SpritesText
         if (Globals::IsSkipTextAfterFinal && original_shader_hashes.Contains(ShaderHashesLists::UISpritesText)) return DrawOrDispatchOverrideType::Skip; 
         
         // //replace rtv
         // native_device_context->OMSetRenderTargets(1, &game_device_data.UIOutputRtv, nullptr);
      }

      //IsSkipUntilUI
      if (Globals::IsSkipUntilUI &&
         !TonemapInfo::GetDrawnTonemap(cb_luma_global_settings.GameSettings.TonemapInfo) &&
         !TonemapInfo::GetDrawnFinal(cb_luma_global_settings.GameSettings.TonemapInfo))
      {
         return DrawOrDispatchOverrideType::Skip;
      }
      
      return DrawOrDispatchOverrideType::None;
   }

   void OnPresent(ID3D11Device* native_device, DeviceData& device_data)
   {
      auto& game_device_data = GetGameDeviceData(device_data);

      //reset TonemapInfo
      Globals::TonemapInfoBackup = cb_luma_global_settings.GameSettings.TonemapInfo;
      cb_luma_global_settings.GameSettings.TonemapInfo = TonemapInfo::GetDefaultReset();

      //reset game/device_data
      game_device_data.ResetOnPresent();
      device_data.has_drawn_main_post_processing = false;;
   }

   void LoadConfigs() override
   {
      reshade::api::effect_runtime* runtime = nullptr;

      //Load ReShade settings
      reshade::get_config_value(runtime, NAME, "TonemapperRolloffStart", cb_luma_global_settings.GameSettings.TonemapperRolloffStart);
      reshade::get_config_value(runtime, NAME, "Exposure", cb_luma_global_settings.GameSettings.Exposure);
      reshade::get_config_value(runtime, NAME, "BloomStrength", cb_luma_global_settings.GameSettings.BloomStrength);

      reshade::get_config_value(runtime, NAME, "LUT", cb_luma_global_settings.GameSettings.LUT);
      reshade::get_config_value(runtime, NAME, "LUTBlowoutReduction", cb_luma_global_settings.GameSettings.LUTBlowoutReduction);
      reshade::get_config_value(runtime, NAME, "LUTBlowoutReductionLookBack", cb_luma_global_settings.GameSettings.LUTBlowoutReductionLookBack);
      
      reshade::get_config_value(runtime, NAME, "PCBlowoutStrength", cb_luma_global_settings.GameSettings.PCBlowoutStrength);
      reshade::get_config_value(runtime, NAME, "PCBlowoutStart", cb_luma_global_settings.GameSettings.PCBlowoutStart);
      reshade::get_config_value(runtime, NAME, "PCBlowoutEnd", cb_luma_global_settings.GameSettings.PCBlowoutEnd);
      
      reshade::get_config_value(runtime, NAME, "FakeBT2020ChromaCorrect", cb_luma_global_settings.GameSettings.FakeBT2020ChromaCorrect);
      reshade::get_config_value(runtime, NAME, "FakeBT2020LumaCorrect", cb_luma_global_settings.GameSettings.FakeBT2020LumaCorrect);
      
      reshade::get_config_value(runtime, NAME, "UpscaleMovPumboPow", cb_luma_global_settings.GameSettings.UpscaleMovPumboPow);
      reshade::get_config_value(runtime, NAME, "UpscaleBGSpritesMax", cb_luma_global_settings.GameSettings.UpscaleBGSpritesMax);
      reshade::get_config_value(runtime, NAME, "UpscaleBGSpritesExp", cb_luma_global_settings.GameSettings.UpscaleBGSpritesExp);
      reshade::get_config_value(runtime, NAME, "UpscaleToonMax", cb_luma_global_settings.GameSettings.UpscaleToonMax);
      reshade::get_config_value(runtime, NAME, "UpscaleToonExp", cb_luma_global_settings.GameSettings.UpscaleToonExp);

      reshade::get_config_value(runtime, NAME, "HUDBrightnessHealthBar", cb_luma_global_settings.GameSettings.HUDBrightnessHealthBar);
      reshade::get_config_value(runtime, NAME, "HUDBrightnessHealthBarDelta", cb_luma_global_settings.GameSettings.HUDBrightnessHealthBarDelta);
      reshade::get_config_value(runtime, NAME, "HUDBrightnessProgressBar", cb_luma_global_settings.GameSettings.HUDBrightnessProgressBar);
      reshade::get_config_value(runtime, NAME, "HUDBrightnessNoteResponse", cb_luma_global_settings.GameSettings.HUDBrightnessNoteResponse);
      reshade::get_config_value(runtime, NAME, "HUDBrightnessHoldComboBg", cb_luma_global_settings.GameSettings.HUDBrightnessHoldComboBg);
      reshade::get_config_value(runtime, NAME, "HUDBrightnessPJDLogo", cb_luma_global_settings.GameSettings.HUDBrightnessPJDLogo);

      reshade::get_config_value(runtime, NAME, "CGContrast", cb_luma_global_settings.GameSettings.CGContrast);
      reshade::get_config_value(runtime, NAME, "CGContrastMidGray", cb_luma_global_settings.GameSettings.CGContrastMidGray);
      reshade::get_config_value(runtime, NAME, "CGSaturation", cb_luma_global_settings.GameSettings.CGSaturation);
      reshade::get_config_value(runtime, NAME, "CGHighlightsStrength", cb_luma_global_settings.GameSettings.CGHighlightsStrength);
      reshade::get_config_value(runtime, NAME, "CGHighlightsMidGray", cb_luma_global_settings.GameSettings.CGHighlightsMidGray);
      reshade::get_config_value(runtime, NAME, "CGShadowsStrength", cb_luma_global_settings.GameSettings.CGShadowsStrength);
      reshade::get_config_value(runtime, NAME, "CGShadowsMidGray", cb_luma_global_settings.GameSettings.CGShadowsMidGray);
      
      reshade::get_config_value(runtime, NAME, "IsFullscreenOverlayFx", Globals::IsFullscreenOverlayFx);
      reshade::get_config_value(runtime, NAME, "IsUI", Globals::IsUI);
      reshade::get_config_value(runtime, NAME, "IsSkipUntilUI", Globals::IsSkipUntilUI);
      reshade::get_config_value(runtime, NAME, "IsSkipTextAfterFinal", Globals::IsSkipTextAfterFinal);
      
      if (custom_sdr_gamma == 0) custom_sdr_gamma = 2.2f;
      reshade::get_config_value(runtime, NAME, "EOTFGammaCorrection", custom_sdr_gamma);
      // defines_need_recompilation = true;
      
      // GetGameDeviceData(device_data).cb_luma_global_settings_dirty = true;
   }

   void DrawImGuiSettings(DeviceData& device_data) override
   {
      reshade::api::effect_runtime* runtime = nullptr;

      auto& game_device_data = GetGameDeviceData(device_data);
      bool is_disabled; //for Begin/EndDisabled();

      // ImGui::NewLine();
      is_disabled = ShaderDefineInfo::Get(ShaderDefineInfo::CUSTOM_TONEMAP) == 0;
      if (is_disabled) ImGui::BeginDisabled();
      if (ImGui::SliderFloat("HDR Tonemapper Rolloff Start", &cb_luma_global_settings.GameSettings.TonemapperRolloffStart, 20.f, 500.f, "%.0f"))
         reshade::set_config_value(runtime, NAME, "TonemapperRolloffStart", cb_luma_global_settings.GameSettings.TonemapperRolloffStart);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("HDR tonemapper's rolloff/shoulder start in nits.\nObviously, don't set exceeding peak.");
      if (cb_luma_global_settings.GameSettings.TonemapperRolloffStart > cb_luma_global_settings.ScenePeakWhite)
      {
         ImGui::SameLine();
         ImGui::SmallButton(ICON_FK_WARNING);
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("This is higher than peak!!!");
      }
      DrawResetButton(cb_luma_global_settings.GameSettings.TonemapperRolloffStart, default_luma_global_game_settings.TonemapperRolloffStart, "TonemapperRolloffStart", runtime);
      if (is_disabled) ImGui::EndDisabled();
      
      ImGui::NewLine(); ////////////////////////////////////////////////////////////////////////////////////

      //Gamma Correction
      if (ImGui::Button("Toggle Gamma Correction")) {ShaderDefineInfo::ToggleBool(GAMMA_CORRECTION_TYPE_HASH);}
      
      bool is_disabled_sdr = cb_luma_global_settings.DisplayMode == DisplayModeType::SDR; //completely disable for SDR
      if (is_disabled_sdr && ShaderDefineInfo::Get(GAMMA_CORRECTION_TYPE_HASH) == 1) ShaderDefineInfo::ToggleBool(GAMMA_CORRECTION_TYPE_HASH); //force sRGB 
      if (is_disabled_sdr) ImGui::BeginDisabled();
      
      is_disabled = ShaderDefineInfo::Get(GAMMA_CORRECTION_TYPE_HASH) == 0;
      if (is_disabled) ImGui::BeginDisabled();
      {
         float gamma_prev = custom_sdr_gamma;
         bool changed_gamma = ImGui::SliderFloat("EOTF / Gamma Correction", &custom_sdr_gamma, 1.0f, 3.0f, "%.1f");
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("(GAMMA_CORRECTION_TYPE) EOTF / Gamma Correction.\nUse if display or OS doesn't do so already, where 2.2 will match SDR.");
         changed_gamma |= DrawResetButton<float, false>(custom_sdr_gamma, 2.2f, nullptr, runtime);
         changed_gamma |= gamma_prev != custom_sdr_gamma; //bruh case
         if (changed_gamma)
         {
            defines_need_recompilation = true;
            reshade::set_config_value(runtime, NAME, "EOTFGammaCorrection", custom_sdr_gamma);
         }
      }
      if (is_disabled) ImGui::EndDisabled();
      
      if (is_disabled_sdr) ImGui::EndDisabled();
      
      //CUSTOM_HDTVREC709
      {
         bool b = ShaderDefineInfo::Get(ShaderDefineInfo::CUSTOM_HDTVREC709) == 1;
         if (ImGui::Checkbox("PS4", &b)) ShaderDefineInfo::ToggleBool(ShaderDefineInfo::CUSTOM_HDTVREC709);
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Do aggressive HDTV rec.709 gamma seen on PS4.");
      }

      ImGui::NewLine(); ////////////////////////////////////////////////////////////////////////////////////
      
      ImGui::Text("README:");
      ImGui::BulletText("See Shader Defines to recompile custom shaders with alternative features,\nor disable for performance and preference.");
      ImGui::BulletText("BEWARE: This game is wildly inconsistent with luminance.\n\"Whatever looks good in SDR\" type beat!");
      ImGui::BulletText("HDR tonemap is done on swapchain level,\nto encompass all of a PV's elements,\nincluding UI sprites like lens flare that would clip.");
      ImGui::BulletText("CUSTOM_UPSCALE_BGSPRITES has like 1 or 2 false positives (e.g. ending of Amatsu Kitsune).");
      ImGui::BulletText("SDR Display Mode above is not supported.");

      ImGui::NewLine();
      ImGui::Text("Options:");

      if (ImGui::SliderFloat("Exposure", &cb_luma_global_settings.GameSettings.Exposure, 0.f, 2.f))
         reshade::set_config_value(runtime, NAME, "Exposure", cb_luma_global_settings.GameSettings.Exposure);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Exposure multiplier before HDR tonemap to output.\nAlt to Game Brightness without shifting EOTF / Gamma Correction influence range.");
      DrawResetButton(cb_luma_global_settings.GameSettings.Exposure, default_luma_global_game_settings.Exposure, "Exposure", runtime);

      if (ImGui::SliderFloat("Bloom", &cb_luma_global_settings.GameSettings.BloomStrength, 0.f, 2.f))
         reshade::set_config_value(runtime, NAME, "BloomStrength", cb_luma_global_settings.GameSettings.BloomStrength);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Bloom strength.");
      DrawResetButton(cb_luma_global_settings.GameSettings.BloomStrength, default_luma_global_game_settings.BloomStrength, "BloomStrength", runtime);

      ImGui::NewLine(); ////////////////////////////////////////////////////////////////////////////////////

      if (ImGui::SliderFloat("SDR Influence", &cb_luma_global_settings.GameSettings.LUT, 0.f, 1.f))
         reshade::set_config_value(runtime, NAME, "LUT", cb_luma_global_settings.GameSettings.LUT);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("LUT, blowout, and color grading influence from original.");
      DrawResetButton(cb_luma_global_settings.GameSettings.LUT, default_luma_global_game_settings.LUT, "LUT", runtime);
      
      if (ShaderDefineInfo::Get(ShaderDefineInfo::CUSTOM_LUT_BLOWOUT_REDUCTION) == 0) ImGui::BeginDisabled(); 
         
      if (ImGui::SliderFloat("LUT Blowout Reduction", &cb_luma_global_settings.GameSettings.LUTBlowoutReduction, 0.f, 1.0f))
         reshade::set_config_value(runtime, NAME, "LUTBlowoutReduction", cb_luma_global_settings.GameSettings.LUTBlowoutReduction);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("(CUSTOM_LUT_BLOWOUT_REDUCTION) YUV LUT blowout reduction.\nToo strong will lead to coloring things that should be blown out white.");
      DrawResetButton(cb_luma_global_settings.GameSettings.LUTBlowoutReduction, default_luma_global_game_settings.LUTBlowoutReduction, "LUTBlowoutReduction", runtime);

      if (ImGui::SliderFloat("LUT Blowout Reduction: Look Back", &cb_luma_global_settings.GameSettings.LUTBlowoutReductionLookBack, 0.f, 1.0f))
         reshade::set_config_value(runtime, NAME, "LUTBlowoutReductionLookBack", cb_luma_global_settings.GameSettings.LUTBlowoutReductionLookBack);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("(CUSTOM_LUT_BLOWOUT_REDUCTION) Multiplier on luminance to sample LUT at a less blown out spot.\nToo strong will lead to coloring things that should be blown out white.");
      DrawResetButton(cb_luma_global_settings.GameSettings.LUTBlowoutReductionLookBack, default_luma_global_game_settings.LUTBlowoutReductionLookBack, "LUTBlowoutReductionLookBack", runtime);

      if (ShaderDefineInfo::Get(ShaderDefineInfo::CUSTOM_LUT_BLOWOUT_REDUCTION) == 0) ImGui::EndDisabled(); 

      if (ImGui::SliderFloat("Per-Channel Blowout", &cb_luma_global_settings.GameSettings.PCBlowoutStrength, 0.f, 1.f))
         reshade::set_config_value(runtime, NAME, "PCBlowoutStrength", cb_luma_global_settings.GameSettings.PCBlowoutStrength);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Originally after tonemapping, values are clipped 0-1 SDR.\nThis the strength of its influence.\nSee also CUSTOM_PCBLOWOUT for options.");
      DrawResetButton(cb_luma_global_settings.GameSettings.PCBlowoutStrength, default_luma_global_game_settings.PCBlowoutStrength, "PCBlowoutStrength", runtime);

      if (ShaderDefineInfo::Get(ShaderDefineInfo::CUSTOM_PCBLOWOUT) == 0) ImGui::BeginDisabled(); 

      if (ImGui::SliderFloat("Per-Channel Blowout: Start", &cb_luma_global_settings.GameSettings.PCBlowoutStart, 0.f, 1.25f))
         reshade::set_config_value(runtime, NAME, "PCBlowoutStart", cb_luma_global_settings.GameSettings.PCBlowoutStart);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("(CUSTOM_PCBLOWOUT) If a tonemapper selected, controls the shoulder's start of the per-channel tonemapper.");
      DrawResetButton(cb_luma_global_settings.GameSettings.PCBlowoutStart, default_luma_global_game_settings.PCBlowoutStart, "PCBlowoutStart", runtime);

      if (ImGui::SliderFloat("Per-Channel Blowout: End", &cb_luma_global_settings.GameSettings.PCBlowoutEnd, 0.75f, 1.75f))
         reshade::set_config_value(runtime, NAME, "PCBlowoutEnd", cb_luma_global_settings.GameSettings.PCBlowoutEnd);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("(CUSTOM_PCBLOWOUT) If a tonemapper selected, controls the peak of the per-channel tonemapper.");
      DrawResetButton(cb_luma_global_settings.GameSettings.PCBlowoutEnd, default_luma_global_game_settings.PCBlowoutEnd, "PCBlowoutEnd", runtime);
      
      if (ShaderDefineInfo::Get(ShaderDefineInfo::CUSTOM_PCBLOWOUT) == 0) ImGui::EndDisabled(); 

      ImGui::NewLine(); ////////////////////////////////////////////////////////////////////////////////////

      is_disabled = ShaderDefineInfo::Get(ShaderDefineInfo::CUSTOM_FAKEBT2020) == 0;
      if (is_disabled) ImGui::BeginDisabled(); 

      if (ImGui::SliderFloat("Fake BT2020: Chrominance Correct", &cb_luma_global_settings.GameSettings.FakeBT2020ChromaCorrect, 0.f, 1.f, "%.4f"))
         reshade::set_config_value(runtime, NAME, "FakeBT2020ChromaCorrect", cb_luma_global_settings.GameSettings.FakeBT2020ChromaCorrect);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("(CUSTOM_FAKEBT2020) Chroma correction to reduce the strength of chrominance boost from fake BT2020 decode.");
      DrawResetButton(cb_luma_global_settings.GameSettings.FakeBT2020ChromaCorrect, default_luma_global_game_settings.FakeBT2020ChromaCorrect, "FakeBT2020ChromaCorrect", runtime);

      if (ImGui::SliderFloat("Fake BT2020: Luminance Correct", &cb_luma_global_settings.GameSettings.FakeBT2020LumaCorrect, 0.f, 1.f, "%.4f"))
         reshade::set_config_value(runtime, NAME, "FakeBT2020LumaCorrect", cb_luma_global_settings.GameSettings.FakeBT2020LumaCorrect);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("(CUSTOM_FAKEBT2020) Luma correction to reduce the luma decrease side effect (metallic feeling) from fake BT2020 decode.");
      DrawResetButton(cb_luma_global_settings.GameSettings.FakeBT2020LumaCorrect, default_luma_global_game_settings.FakeBT2020LumaCorrect, "FakeBT2020LumaCorrect", runtime);
      
      if (is_disabled) ImGui::EndDisabled(); 

      ImGui::NewLine(); ////////////////////////////////////////////////////////////////////////////////////
      
      is_disabled = ShaderDefineInfo::Get(ShaderDefineInfo::CUSTOM_COLORGRADE) == 0;
      if (is_disabled) ImGui::BeginDisabled(); 

      if (ImGui::SliderFloat("Color Grading: Saturation", &cb_luma_global_settings.GameSettings.CGSaturation, 0.f, 2.f, "%.4f"))
         reshade::set_config_value(runtime, NAME, "CGSaturation", cb_luma_global_settings.GameSettings.CGSaturation);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("(CUSTOM_COLORGRADE) Final saturation before HDR tonemap.");
      DrawResetButton(cb_luma_global_settings.GameSettings.CGSaturation, default_luma_global_game_settings.CGSaturation, "CGSaturation", runtime);

      if (ImGui::SliderFloat("Color Grading: Contrast", &cb_luma_global_settings.GameSettings.CGContrast, 0.f, 2.f, "%.4f"))
         reshade::set_config_value(runtime, NAME, "CGContrast", cb_luma_global_settings.GameSettings.CGContrast);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("(CUSTOM_COLORGRADE) RenoDX power based contrast before HDR tonemap.");
      DrawResetButton(cb_luma_global_settings.GameSettings.CGContrast, default_luma_global_game_settings.CGContrast, "CGContrast", runtime);
      
      if (ImGui::SliderFloat("Color Grading: Contrast Mid Gray", &cb_luma_global_settings.GameSettings.CGContrastMidGray, 0.f, 500.f))
         reshade::set_config_value(runtime, NAME, "CGContrastMidGray", cb_luma_global_settings.GameSettings.CGContrastMidGray);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("(CUSTOM_COLORGRADE) Contrast's mid gray value to stretch in/out luminance.");
      DrawResetButton(cb_luma_global_settings.GameSettings.CGContrastMidGray, default_luma_global_game_settings.CGContrastMidGray, "CGContrastMidGray", runtime);
      
      if (ImGui::SliderFloat("Color Grading: Highlights", &cb_luma_global_settings.GameSettings.CGHighlightsStrength, 0.f, 2.f))
         reshade::set_config_value(runtime, NAME, "CGHighlightsStrength", cb_luma_global_settings.GameSettings.CGHighlightsStrength);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("(CUSTOM_COLORGRADE) RenoDX highlights boost/compress.");
      DrawResetButton(cb_luma_global_settings.GameSettings.CGHighlightsStrength, default_luma_global_game_settings.CGHighlightsStrength, "CGHighlightsStrength", runtime);
      
      if (ImGui::SliderFloat("Color Grading: Highlights Mid Gray", &cb_luma_global_settings.GameSettings.CGHighlightsMidGray, 0.f, 500.f))
         reshade::set_config_value(runtime, NAME, "CGHighlightsMidGray", cb_luma_global_settings.GameSettings.CGHighlightsMidGray);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("(CUSTOM_COLORGRADE) Highlights mid gray / threshold value to manipulate luminance around.");
      DrawResetButton(cb_luma_global_settings.GameSettings.CGHighlightsMidGray, default_luma_global_game_settings.CGHighlightsMidGray, "CGHighlightsMidGray", runtime);

      if (ImGui::SliderFloat("Color Grading: Shadows", &cb_luma_global_settings.GameSettings.CGShadowsStrength, 0.f, 2.f))
         reshade::set_config_value(runtime, NAME, "CGShadowsStrength", cb_luma_global_settings.GameSettings.CGShadowsStrength);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("(CUSTOM_COLORGRADE) RenoDX shadows boost/compress.");
      DrawResetButton(cb_luma_global_settings.GameSettings.CGShadowsStrength, default_luma_global_game_settings.CGShadowsStrength, "CGShadowsStrength", runtime);
      
      if (ImGui::SliderFloat("Color Grading: Shadows Mid Gray", &cb_luma_global_settings.GameSettings.CGShadowsMidGray, 0.f, 500.f))
         reshade::set_config_value(runtime, NAME, "CGShadowsMidGray", cb_luma_global_settings.GameSettings.CGShadowsMidGray);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("(CUSTOM_COLORGRADE) Shadows mid gray / threshold value to manipulate luminance around.");
      DrawResetButton(cb_luma_global_settings.GameSettings.CGShadowsMidGray, default_luma_global_game_settings.CGShadowsMidGray, "CGShadowsMidGray", runtime);
      
      if (is_disabled) ImGui::EndDisabled(); 

      ImGui::NewLine(); ////////////////////////////////////////////////////////////////////////////////////

      is_disabled = ShaderDefineInfo::Get(ShaderDefineInfo::CUSTOM_UPSCALE_MOV) == 0;
      if (is_disabled) ImGui::BeginDisabled(); 
      if (ImGui::SliderFloat("Upscale FMV: AutoHDR Power", &cb_luma_global_settings.GameSettings.UpscaleMovPumboPow, 0.f, 5.f))
         reshade::set_config_value(runtime, NAME, "UpscaleMovPumboPow", cb_luma_global_settings.GameSettings.UpscaleMovPumboPow);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("(CUSTOM_UPSCALE_MOV) FMV PumboAutoHDR shoulder power.");
      DrawResetButton(cb_luma_global_settings.GameSettings.UpscaleMovPumboPow, default_luma_global_game_settings.UpscaleMovPumboPow, "UpscaleMovPumboPow", runtime);
      if (is_disabled) ImGui::EndDisabled();

      ////////

      is_disabled = ShaderDefineInfo::Get(ShaderDefineInfo::CUSTOM_UPSCALE_BGSPRITES) == 0;
      if (is_disabled) ImGui::BeginDisabled(); 

      if (ImGui::SliderFloat("Upscale BG Sprites: Max Input", &cb_luma_global_settings.GameSettings.UpscaleBGSpritesMax, 1.f, 6.f))
         reshade::set_config_value(runtime, NAME, "UpscaleBGSpritesMax", cb_luma_global_settings.GameSettings.UpscaleBGSpritesMax);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("(CUSTOM_UPSCALE_BGSPRITES) Max value expected by inverse tonemap for SDR background sprites in complex \"Future Tone\" scenes.");
      DrawResetButton(cb_luma_global_settings.GameSettings.UpscaleBGSpritesMax, default_luma_global_game_settings.UpscaleBGSpritesMax, "UpscaleBGSpritesMax", runtime);

      if (ImGui::SliderFloat("Upscale BG Sprites: Exposure", &cb_luma_global_settings.GameSettings.UpscaleBGSpritesExp, 0.f, 1.f))
         reshade::set_config_value(runtime, NAME, "UpscaleBGSpritesExp", cb_luma_global_settings.GameSettings.UpscaleBGSpritesExp);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("(CUSTOM_UPSCALE_BGSPRITES) Max value expected by inverse tonemap for SDR background sprites in complex \"Future Tone\" scenes.");
      DrawResetButton(cb_luma_global_settings.GameSettings.UpscaleBGSpritesExp, default_luma_global_game_settings.UpscaleBGSpritesExp, "UpscaleBGSpritesExp", runtime);

      if (is_disabled) ImGui::EndDisabled(); 

      ////////

      is_disabled = ShaderDefineInfo::Get(ShaderDefineInfo::CUSTOM_UPSCALE_TOON) == 0;
      if (is_disabled) ImGui::BeginDisabled(); 

      if (ImGui::SliderFloat("Upscale Toon: Max Input", &cb_luma_global_settings.GameSettings.UpscaleToonMax, 1.f, 2.f))
         reshade::set_config_value(runtime, NAME, "UpscaleToonMax", cb_luma_global_settings.GameSettings.UpscaleToonMax);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("(CUSTOM_UPSCALE_TOON) Max value expected by inverse tonemap for toon \"Switch\" shading scenes.");
      DrawResetButton(cb_luma_global_settings.GameSettings.UpscaleToonMax, default_luma_global_game_settings.UpscaleToonMax, "UpscaleToonMax", runtime);

      if (ImGui::SliderFloat("Upscale Toon: Exposure", &cb_luma_global_settings.GameSettings.UpscaleToonExp, 0.f, 1.f))
         reshade::set_config_value(runtime, NAME, "UpscaleToonExp", cb_luma_global_settings.GameSettings.UpscaleToonExp);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("(CUSTOM_UPSCALE_TOON) Max value expected by inverse tonemap for toon \"Switch\" shading scenes.");
      DrawResetButton(cb_luma_global_settings.GameSettings.UpscaleToonExp, default_luma_global_game_settings.UpscaleToonExp, "UpscaleToonExp", runtime);
      
      if (is_disabled) ImGui::EndDisabled(); 

      ImGui::NewLine(); ////////////////////////////////////////////////////////////////////////////////////

      is_disabled = ShaderDefineInfo::Get(ShaderDefineInfo::CUSTOM_HUDBRIGHTNESS) == 0;
      if (is_disabled) ImGui::BeginDisabled(); 

      if (ImGui::SliderFloat("HUD Brightness: Health Bar", &cb_luma_global_settings.GameSettings.HUDBrightnessHealthBar, 0.f, 1.f))
         reshade::set_config_value(runtime, NAME, "HUDBrightnessHealthBar", cb_luma_global_settings.GameSettings.HUDBrightnessHealthBar);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("(CUSTOM_HUDBRIGHTNESS) Brightness multiplier for Health Bar.");
      DrawResetButton(cb_luma_global_settings.GameSettings.HUDBrightnessHealthBar, default_luma_global_game_settings.HUDBrightnessHealthBar, "HUDBrightnessHealthBar", runtime);

      if (ImGui::SliderFloat("HUD Brightness: Health Bar Delta", &cb_luma_global_settings.GameSettings.HUDBrightnessHealthBarDelta, 0.f, 1.f))
         reshade::set_config_value(runtime, NAME, "HUDBrightnessHealthBarDelta", cb_luma_global_settings.GameSettings.HUDBrightnessHealthBarDelta);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("(CUSTOM_HUDBRIGHTNESS) Brightness multiplier for Health Bar Delta (piece that lingers on change).");
      DrawResetButton(cb_luma_global_settings.GameSettings.HUDBrightnessHealthBarDelta, default_luma_global_game_settings.HUDBrightnessHealthBarDelta, "HUDBrightnessHealthBarDelta", runtime);

      if (ImGui::SliderFloat("HUD Brightness: Progress Bar", &cb_luma_global_settings.GameSettings.HUDBrightnessProgressBar, 0.f, 1.f))
         reshade::set_config_value(runtime, NAME, "HUDBrightnessProgressBar", cb_luma_global_settings.GameSettings.HUDBrightnessProgressBar);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("(CUSTOM_HUDBRIGHTNESS) Brightness multiplier for bottom Progress Bar.");
      DrawResetButton(cb_luma_global_settings.GameSettings.HUDBrightnessProgressBar, default_luma_global_game_settings.HUDBrightnessProgressBar, "HUDBrightnessProgressBar", runtime);

      if (ImGui::SliderFloat("HUD Brightness: Note Response", &cb_luma_global_settings.GameSettings.HUDBrightnessNoteResponse, 0.f, 1.f))
         reshade::set_config_value(runtime, NAME, "HUDBrightnessNoteResponse", cb_luma_global_settings.GameSettings.HUDBrightnessNoteResponse);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("(CUSTOM_HUDBRIGHTNESS) Brightness multiplier for the \"boom\" fx when hitting a note.");
      DrawResetButton(cb_luma_global_settings.GameSettings.HUDBrightnessNoteResponse, default_luma_global_game_settings.HUDBrightnessNoteResponse, "HUDBrightnessNoteResponse", runtime);

      if (ImGui::SliderFloat("HUD Brightness: Hold Combo BG", &cb_luma_global_settings.GameSettings.HUDBrightnessHoldComboBg, 0.f, 1.f))
         reshade::set_config_value(runtime, NAME, "HUDBrightnessHoldComboBg", cb_luma_global_settings.GameSettings.HUDBrightnessHoldComboBg);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("(CUSTOM_HUDBRIGHTNESS) Brightness multiplier for the background of the Hold Combo popup.");
      DrawResetButton(cb_luma_global_settings.GameSettings.HUDBrightnessHoldComboBg, default_luma_global_game_settings.HUDBrightnessHoldComboBg, "HUDBrightnessHoldComboBg", runtime);

      if (ImGui::SliderFloat("HUD Brightness: PJD Logo", &cb_luma_global_settings.GameSettings.HUDBrightnessPJDLogo, 0.f, 1.f))
         reshade::set_config_value(runtime, NAME, "HUDBrightnessPJDLogo", cb_luma_global_settings.GameSettings.HUDBrightnessPJDLogo);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("(CUSTOM_HUDBRIGHTNESS) Brightness multiplier for goofy Music Video logo top right.");
      DrawResetButton(cb_luma_global_settings.GameSettings.HUDBrightnessPJDLogo, default_luma_global_game_settings.HUDBrightnessPJDLogo, "HUDBrightnessPJDLogo", runtime);
      
      if (is_disabled) ImGui::EndDisabled(); 

      ImGui::NewLine(); ////////////////////////////////////////////////////////////////////////////////////

      if (ImGui::Checkbox("Fullscreen Overlay FX", &Globals::IsFullscreenOverlayFx))
         reshade::set_config_value(runtime, NAME, "IsFullscreenOverlayFx", Globals::IsFullscreenOverlayFx);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("Toggle IsFullscreenOverlayFx.\nWill discard all shaders after tonemap shader and before final.");
      DrawResetButton(Globals::IsFullscreenOverlayFx, true, "IsFullscreenOverlayFx", runtime);
      
      if (ImGui::Checkbox("UI", &Globals::IsUI))
         reshade::set_config_value(runtime, NAME, "IsUI", Globals::IsUI);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("Toggle UI.\nWill discard all shaders after final shader.");
      DrawResetButton(Globals::IsUI, true, "IsUI", runtime);

      if (ImGui::Checkbox("Skip Until UI", &Globals::IsSkipUntilUI))
         reshade::set_config_value(runtime, NAME, "IsSkipUntilUI", Globals::IsSkipUntilUI);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("Skip as much draw calls as possible until UI.");
      DrawResetButton(Globals::IsSkipUntilUI, false, "IsSkipUntilUI", runtime);

      if (ImGui::Checkbox("Skip UI Text", &Globals::IsSkipTextAfterFinal))
         reshade::set_config_value(runtime, NAME, "IsSkipTextAfterFinal", Globals::IsSkipUntilUI);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("For turning off lyrics, skips all text after final shader has drawn.");
      DrawResetButton(Globals::IsSkipTextAfterFinal, false, "IsSkipTextAfterFinal", runtime);
      
      ImGui::NewLine(); ////////////////////////////////////////////////////////////////////////////////////

      ImGui::Text("Debug Info:");
      {
         const int ti = TonemapInfo::GetIndexOnlyIfDrawn(Globals::TonemapInfoBackup);
         
         std::string s = "Tonemap Uber Variant: " + std::to_string(ti);
         ImGui::BulletText(s.c_str());
         
         std::string s99 = "Tonemap Debug Info: " + (TonemapInfo::GetIndexOnlyIfDrawn(Globals::TonemapInfoBackup) >= 0 ? static_cast<std::string>(TonemapInfo::TonemapDebugInfo[ti]) : "N/A");
         ImGui::BulletText(s99.c_str());
         
         // std::string s2 = "Tonemap Complex (Future Tone): " + std::to_string(TonemapInfo::GetComplex(Globals::TonemapInfoBackup));
         // ImGui::BulletText(s2.c_str());
         //
         // std::string s3 = "Tonemap BG Sprites: " + std::to_string(TonemapInfo::GetSprites(Globals::TonemapInfoBackup));
         // ImGui::BulletText(s3.c_str());
         
         std::string s1 = "Drawn Final: " + std::to_string(TonemapInfo::GetDrawnFinal(Globals::TonemapInfoBackup));
         ImGui::BulletText(s1.c_str());
         
         std::string s6 = "Drawn Sprites HPBarDelta: " + std::to_string(TonemapInfo::GetDrawnHPBarDelta(Globals::TonemapInfoBackup));
         ImGui::BulletText(s6.c_str());
         
         std::string s5 = "FMV Mode Detected: " + std::to_string(TonemapInfo::GetIsFMV(Globals::TonemapInfoBackup));
         ImGui::BulletText(s5.c_str());
         
         std::string s4 = "Swapchain Change Count: " + std::to_string(Globals::SwapchainChangeCount);
         ImGui::BulletText(s4.c_str());
      }
      
#if DEVELOPMENT
      ImGui::NewLine();
#endif
   }

   void PrintImGuiAbout() override
   {
      ImGui::Text("Build Date:");
      ImGui::Text(__DATE__);
      ImGui::Text(__TIME__);
      ImGui::NewLine();
      
      ImGui::Text("Credits:");
      ImGui::Bullet(); ImGui::Text("Luma: Pumbo (Filoppi)");
      ImGui::Bullet(); ImGui::Text("RenoDX: clshortfuse");
      ImGui::Bullet(); ImGui::Text("Mod: XgarhontX");
      ImGui::Bullet(); ImGui::Text("Development Help & Bug Hunter: MLGSmallSmoke35");
      ImGui::Bullet(); ImGui::Text("Bug Hunter & Benchmarker: Pikota");
      ImGui::Bullet(); ImGui::Text("Bug Hunter: Pino");

      ImGui::NewLine();
      ImGui::Text("Third Party:");
      ImGui::Bullet(); ImGui::Text("ReShade");
      ImGui::Bullet(); ImGui::Text("ImGui");
      ImGui::Bullet(); ImGui::Text("RenoDX");
      ImGui::Bullet(); ImGui::Text("3Dmigoto");
      ImGui::Bullet(); ImGui::Text("Oklab");
      ImGui::Bullet(); ImGui::Text("JzAzBz");
      // ImGui::Bullet(); ImGui::Text("NVIDIA");
      // ImGui::Bullet(); ImGui::Text("AMD");
      ImGui::Bullet(); ImGui::Text("DICE");
   }
};

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
   if (ul_reason_for_call == DLL_PROCESS_ATTACH)
   {
      //name
      Globals::SetGlobals(PROJECT_NAME, "Hatsune Miku: Project DIVA Mega Mix+ - Luma Mod");
      Globals::VERSION = 1;

      //SetupShaderHashesLists
      ShaderHashesLists_Setup();

      // //enable_ui_separation
      // enable_ui_separation = true;
      
      //swapchain upgrade
      swapchain_format_upgrade_type  = TextureFormatUpgradesType::AllowedEnabled;
      swapchain_upgrade_type         = SwapchainUpgradeType::scRGB;

      //texture upgrade
      texture_format_upgrades_type   = TextureFormatUpgradesType::AllowedEnabled;
      //enable_indirect_texture_format_upgrades = true;
      //enable_automatic_indirect_texture_format_upgrades = true;
      texture_upgrade_formats = {
         reshade::api::format::r8g8b8a8_unorm
      };

      texture_format_upgrades_2d_size_filters = 0 | (uint32_t)TextureFormatUpgrades2DSizeFilters::CustomAspectRatio | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainAspectRatio;
      texture_format_upgrades_2d_custom_aspect_ratios = { 16.f / 9.f }; 
      texture_format_upgrades_2d_aspect_ratio_pixel_threshold = 32; //leeway TODO: too loose?

      //Lut
      // r16g16f already.

      game = new ProjectDivaMegaMix();
   }

   CoreMain(hModule, ul_reason_for_call, lpReserved);

   return TRUE;
}