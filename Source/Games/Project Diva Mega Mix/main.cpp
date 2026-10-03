#define GAME_PROJECT_DIVA_MEGA_MIX 1

#define ALLOW_SHADERS_DUMPING 0
#define DISABLE_AUTO_DEBUGGER 1
#define ENABLE_SMAA 1
// #define ENABLE_POST_DRAW_DISPATCH_CALLBACK 0
// #define DISABLE_SWAPCHAIN_FLIP_MODEL 1
#define MAX_SHADER_DEFINES_OVERRIDE 64
#include "..\..\Core\core.hpp"

namespace
{
   void DrawColoredSubHeader(const char* label, const ImVec4& color = ImColor(128, 255, 255, 255))
   {
      ImGui::PushStyleColor(ImGuiCol_Text, color);
      ImGui::Text("[%s]", label);
      ImGui::PopStyleColor();
   }

   bool DrawCollapsingHeaderEnabledColored(const char* label, bool enabled, bool is_default_open = false)
   {
      if (enabled) ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.4f, 0.4f, 0.8f, 1.f));
      bool is_open = ImGui::CollapsingHeader(label, is_default_open ? ImGuiTreeNodeFlags_DefaultOpen : ImGuiTreeNodeFlags_None);
      if (enabled) ImGui::PopStyleColor();
      return is_open;
   }
   
   float GetPulseMultiplier(float speed = 0.1f, float amount = 0.25f)
   {
      return (1.f-amount/2) + amount/2 * sinf(cb_luma_global_settings.FrameIndex * speed);
   }

   bool IsModEnabled() {return custom_shaders_enabled || !ignore_indirect_upgraded_textures || !ignore_upgraded_samplers;}

   float FPSToMilliseconds(float fps) { return fps > 0 ? 1000.f / fps : 0.f; }

   namespace TimerTimestamp
   {
      uint64_t ms_curr = 0;
      
      struct TimerTimestamp
      {
         uint64_t timestamp_start; // saved at create, time when timer was created
         uint64_t timestamp_finish; // calc at create, time when timer should finish
         bool is_completed = true;

         bool IsCompleted() const { return is_completed; }

         void Start(float ms)
         {
            uint64_t now = ms_curr;
            timestamp_start = now;
            timestamp_finish = now + ms;
            is_completed = false;
         }

         struct UpdateResults
         {
            [[maybe_unused]] bool is_completed_this_call;
            [[maybe_unused]] float ratio_until_finish;
         };
         UpdateResults Update() // Update loop
         {
            // already
            if (IsCompleted()) return {false, 1.f};

            // done this call
            uint64_t now = ms_curr;
            if (now >= timestamp_finish) 
            {
               is_completed = true;
               return {true, 1.f};
            }

            // in progress
            float ratio_unclamped = static_cast<float>(now - timestamp_start) / static_cast<float>(timestamp_finish - timestamp_start);
            return {false, std::clamp(ratio_unclamped, 0.f, 1.f)};
         }
      };

      TimerTimestamp Default() { return TimerTimestamp{0, 0, true}; }
      
      void OnPresent()
      {
         ms_curr = GetTickCount64();
      }
   }
   
#if DEVELOPMENT
   // returns if previewing
   bool PreviewShaderOutput(DeviceData& device_data, uint32_t hash)
   {
      // purge
      device_data.debug_draw_texture = nullptr;
      device_data.debug_draw_texture_format = DXGI_FORMAT_UNKNOWN;
      device_data.debug_draw_texture_size = {};

      // set
      if (debug_draw_shader_hash != hash)
      {
         debug_draw_shader_hash = hash;
         return true;
      }
      else
      {
         debug_draw_shader_hash = 0;
         return false;
      }
   }
#endif

namespace RenderTracker // partial
{
   // Handles //
   ComPtr<ID3D11Resource> res_main_color; // rgba16f that forward lighting draws to
   ComPtr<ID3D11Texture2D> res_main_color_tex;
   uint64_t res_main_color_handle = 0;
   uint2 res_main_color_size = {};

   ComPtr<ID3D11Resource> res_main_depth; // depth buffer that forward lighting draws to
   ComPtr<ID3D11Texture2D> res_main_depth_tex;
   uint64_t res_main_depth_handle = 0;
   uint2 res_main_depth_size = {}; // should be same as color

   void Reset_Handles()
   {
      res_main_color.reset(); res_main_color_tex.reset(); res_main_color_handle = 0;
      res_main_depth.reset(); res_main_depth_tex.reset(); res_main_depth_handle = 0;
      res_main_color_size = {0,0}; res_main_depth_size = {0,0};
   }
      
   // State (cleared on present) //
   uint32_t ps_prev;

   uint64_t last_cleared_rtv_handle = 0;

   uint8_t count_main_color_clears = 0; // count of ClearRenderTargetView on main color
   uint8_t count_main_color_clears_draws_after = 0; // (exec first-most) count of OnDrawOrDispatch after last ClearRenderTargetView on main color

   enum SSSState : uint8_t
   {
      SSSState_Unknown,
      SSSState_Setup,
      SSSState_Skipped,
      SSSState_Resolved,
   };
   SSSState sss_state = SSSState_Unknown;
   bool IsSSSResolvedOrSkipped() { return sss_state >= SSSState_Skipped; }
   bool IsSSSSetup() { return sss_state == SSSState_Setup; }
   bool IsSSSSkipped() { return sss_state == SSSState_Skipped; }
   bool IsSSSResolved() { return sss_state == SSSState_Resolved; }
   bool IsMainColorBeingOrHasBeenDrawnTo() { return IsSSSResolvedOrSkipped(); }
   
   bool is_drawn_to_swapchain = false; // when shader draws
   bool is_drawn_to_autoexposure0 = false; // when shader draws

   void Reset_State()
   {
      ps_prev = 0;
      count_main_color_clears = 0;
      count_main_color_clears_draws_after = 0;
      sss_state = SSSState_Unknown;
      is_drawn_to_swapchain = false;
      is_drawn_to_autoexposure0 = false;
   }
}

namespace GlobalsMegaMix
{
   bool is_draw_game_ui = true;
   bool is_draw_game_ui_text = true;
   
   int swapchain_change_count = 0;
   
   bool imgui_readme_done = false;
   bool imgui_show_advanced = false;
   
   bool is_gamma_correction_sync_paper_white = true;
}

namespace TonemapInfo
{
   int tonemapinfo_prev = 0;

   int FlagDrawnTonemap =     0x40000000; //1<<30
   // int FlagSprites =          0x20000000; //1<<29
   // int FlagComplex =          0x10000000; //1<<28
   int FlagDrawnFinal =       0x08000000; //1<<27
   // int FlagIsFMV =            0x04000000; //1<<26
   int FlagDrawnHPBarDelta =  0x02000000; //1<<25
   int IndexBitMask =         0x0000000F;
      
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

   // int SetIsFMVTrue(int v) { return v | FlagIsFMV; }
   // bool GetIsFMV(int v) { return (v & FlagIsFMV) > 0; }

   int SetDrawnHPBarDeltaTrue(int v) { return v | FlagDrawnHPBarDelta; }
   bool GetDrawnHPBarDelta(int v) { return (v & FlagDrawnHPBarDelta) > 0; }
      
   int SetIndexAndDrawnTonemapTrue(int v, int i) { return v | FlagDrawnTonemap | (IndexBitMask & i); }
   int GetIndex(int v) { return v & IndexBitMask; }
   int GetIndexOnlyIfDrawn(int v) { return GetDrawnTonemap(v) ? v & IndexBitMask : -1; }

   bool GetIsDrawnTonemapOrFinal(int v) { return v & (FlagDrawnTonemap | FlagDrawnFinal); }
   bool GetIsDrawnTonemapButNotFinal(int v) { return v & FlagDrawnTonemap && !(v & FlagDrawnFinal); }

   bool GetIsPBRFromIndex(int i) { return i <= 4; }
   
   const char* const TonemapDebugInfo[] = {
      "Complex", //0
      "Complex, BGSprites", //1
      "Complex", //2
      "Complex, BGSprites", //3
      "Complex, BGSprites", //4
      "Toon", //5
      "Toon", //6
      "Toon, BGSprites", //7
      "Toon, BGSprites", //8
      "Toon", //9
      "Toon, BGSprites (Customization)", //10
   };
}

namespace OutputHandling
{
   constexpr const char* Luma_MegaMix_ToSwapchain = "Luma_MegaMix_ToSwapchain"; // file name & shader variant

   bool IsSCRGB() { return swapchain_upgrade_type == SwapchainUpgradeType::scRGB; }
   bool IsHDR10() { return swapchain_upgrade_type == SwapchainUpgradeType::HDR10; }
   bool IsSDR8bit() { return swapchain_upgrade_type == SwapchainUpgradeType::None; }

   void OnDLL()
   {
      // scRGB rgba16f
      swapchain_upgrade_type         = SwapchainUpgradeType::scRGB;
      swapchain_format_upgrade_type  = TextureFormatUpgradesType::AllowedEnabled;

      // SDR rgba8
      if (std::filesystem::exists("Luma_Output8"))
      {
         swapchain_upgrade_type         = SwapchainUpgradeType::None;
         swapchain_format_upgrade_type  = TextureFormatUpgradesType::None;
      }
      // HDR10 rgb10a2
      else if (std::filesystem::exists("Luma_Output10") || !DEVELOPMENT)
      {
         swapchain_upgrade_type         = SwapchainUpgradeType::HDR10;
         swapchain_format_upgrade_type  = TextureFormatUpgradesType::AllowedEnabled;
      }
   }
   
   void OnInit()
   {
      // Luma_ToSwapchain
      auto def_val = "0";
      if (IsHDR10()) def_val = "1";
      else if (IsSDR8bit()) def_val = "2";
      native_shaders_definitions.emplace(CompileTimeStringHash(Luma_MegaMix_ToSwapchain), ShaderDefinition{ Luma_MegaMix_ToSwapchain, reshade::api::pipeline_subobject_type::pixel_shader,   nullptr, "main", {{ "CUSTOM_TOSWAPCHAIN", def_val}}});

      // Native Shaders: Display Composition replacement (will break DEVELOPMENT debug draw, but whatever)
      if (!IsSCRGB())
      {
         native_shaders_definitions.erase(CompileTimeStringHash("Display Composition"));
         native_shaders_definitions.emplace(CompileTimeStringHash("Display Composition"), ShaderDefinition{"Luma_MegaMix_DisplayComposition", reshade::api::pipeline_subobject_type::pixel_shader});
      }
   }

   DrawOrDispatchOverrideType OnDrawOrDispatchOverride(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, uint32_t ps)
   {
      // gatekeep: not ps
      if (ps != 0xA200B172) return DrawOrDispatchOverrideType::None;

      // gatekeep: RTV not backbuffer/swapchain
      if (!TonemapInfo::GetIsDrawnTonemapOrFinal(cb_luma_global_settings.GameSettings.TonemapInfo))
      {
         // get RTV RES Handle
         ComPtr<ID3D11RenderTargetView> rtv;
         native_device_context->OMGetRenderTargets(1, rtv.put(), nullptr);
         ComPtr<ID3D11Resource> res;
         rtv->GetResource(res.put());
         uint64_t handle = reinterpret_cast<uint64_t>(res.get());

         // set contains
         if (!device_data.back_buffers.contains(handle)) return DrawOrDispatchOverrideType::None;
      }
      // (otherwise, it's guaranteed after final)

      // set PS to Luma_Swapchain
      native_device_context->PSSetShader(device_data.native_pixel_shaders.at(CompileTimeStringHash(Luma_MegaMix_ToSwapchain)).get(), nullptr, 0);

      return DrawOrDispatchOverrideType::None;
   }
}

namespace ShaderHashesLists
{
   constexpr uint32_t DepthOfField0 = 0x83AE9A79; //uses depth to create mask
   constexpr uint32_t DepthOfField1 = 0x8814AF0D; //edits blur mask to expand edges?
   constexpr uint32_t DepthOfField2 = 0x043F4B65; //downsamples color using mask to determine blur (sample offset) amount + obvious mask for camera near/far blur
   constexpr uint32_t DepthOfField3 = 0x7D2DE42C; //more downsampling + blur using mask
   constexpr uint32_t DepthOfField4 = 0xD7064E88; //final combine back to native using all prior
   const std::unordered_set<uint32_t> DepthOfFieldShaders = { DepthOfField0, DepthOfField1, DepthOfField2, DepthOfField3, DepthOfField4 };
   constexpr uint32_t Downsample0 = 0x68722F15; //Generic downsample for bloom and autoexposure
   constexpr uint32_t Downsample1 = 0x7B4E4533; //downsample all the way down to near 1x1 for autoexposure
   constexpr uint32_t AutoExposure0 = 0xA58C1868; //auto exposure ring buffer write 
   constexpr uint32_t AutoExposure1 = 0xDF1AC023; //sample each ring buffer to compute avg (output is used by CPU to set cb value for tonemap v3.y)
   constexpr uint32_t SSSSetup0 = 0x93881580; // (aka sss_skina_ps)
   constexpr uint32_t SSSSetup1 = 0xCF8047E0; // (aka sss_skinas_ps)
   bool IsSSSSetupShader(uint32_t ps) { return ps == SSSSetup0 || ps == SSSSetup1; }
   constexpr uint32_t WaterWavesNormals0 = 0xE83D0A67;
   constexpr uint32_t GlassEyes = 0xDFD687E0; // (aka glass_eye_ps & glass_eyes_ps, 2 because paired VS can be diff)
   
   const std::unordered_map<uint32_t, uint8_t> Tonemaps = {
      { 0x7CFCDF1A, 0  }, //complex
      { 0x6047C5DE, 1  }, //complex sprite
      { 0x8CAB805E, 2  }, //complex (un-witnessed)
      { 0x87371E76, 3  }, //complex sprites (un-witnessed)
      { 0xB3273DF8, 4  }, //complex sprites (un-witnessed)
      { 0x55660220, 5  }, //fast
      { 0x29307B56, 6  }, //fast (un-witnessed)
      { 0x5A8C281C, 7  }, //fast sprites (un-witnessed)
      { 0xCBB08175, 8  }, //fast sprites 
      { 0xD4CB36EE, 9  }, //fast (un-witnessed)
      { 0xF6BEC634, 10 }, //fast sprites (in customization)
   };
   
   constexpr uint32_t Final = 0x56443BE9;
   constexpr uint32_t Mov = 0x62D69253;
   constexpr uint32_t UISpritesHPBarDelta = 0xD0162389;
   constexpr uint32_t UISpritesText = 0x7F6C8EC7;
   constexpr uint32_t ToSwapchain = 0xA200B172;
}

// namespace UISeparation //TODO: del, compeltely broken
// {
//    //UI transparency TODO: use ComPtr
//    com_ptr<ID3D11Texture2D> UIOutputTexOrig = nullptr; 
//    
//    com_ptr<ID3D11Texture2D> UIOutputTex = nullptr;
//    D3D11_TEXTURE2D_DESC UIOutputTexDesc;
//    
//    com_ptr<ID3D11RenderTargetView> UIOutputRtv = nullptr;
//    D3D11_RENDER_TARGET_VIEW_DESC UIOutputRtvDesc;
//    
//    com_ptr<ID3D11ShaderResourceView> UIOutputSrv = nullptr;
//    D3D11_RENDER_TARGET_VIEW_DESC UIOutputSrvDesc;
//    
//    bool IsFinalCopyToken = false;
//
//    void ResetOnSwapchain()
//    {
//       //invalidate
//       UIOutputTex = nullptr;
//       UIOutputRtv = nullptr;
//       UIOutputSrv = nullptr;
//    }
//
//    void ResetOnPresent()
//    {
//       IsFinalCopyToken = false;
//    }
// }

namespace ShaderDefineInfo
{
   constexpr uint32_t SWAPCHAIN_TEST_USER_PEAK          = char_ptr_crc32("SWAPCHAIN_TEST_USER_PEAK");
   constexpr uint32_t CUSTOM_TONEMAP_SCALING            = char_ptr_crc32("CUSTOM_TONEMAP_SCALING");
   constexpr uint32_t CUSTOM_TONEMAP_CLAMP              = char_ptr_crc32("CUSTOM_TONEMAP_CLAMP");
   constexpr uint32_t CUSTOM_CLAMP_PEAK                 = char_ptr_crc32("CUSTOM_CLAMP_PEAK");
   constexpr uint32_t CUSTOM_HDTVREC709_1               = char_ptr_crc32("CUSTOM_HDTVREC709_1");
   constexpr uint32_t CUSTOM_FAKEBT2020                 = char_ptr_crc32("CUSTOM_FAKEBT2020");
   constexpr uint32_t CUSTOM_LUT_BLOWOUT_GAUSSIAN       = char_ptr_crc32("CUSTOM_LUT_BLOWOUT_GAUSSIAN");
   constexpr uint32_t CUSTOM_LUT_BLOWOUT_GAUSSIAN_STOPS = char_ptr_crc32("CUSTOM_LUT_BLOWOUT_GAUSSIAN_STOPS");
   constexpr uint32_t CUSTOM_PCC_QUALITY                = char_ptr_crc32("CUSTOM_PCC_QUALITY");
   constexpr uint32_t CUSTOM_COLORGRADE                 = char_ptr_crc32("CUSTOM_COLORGRADE");
   constexpr uint32_t CUSTOM_COLORGRADE_SATORDER        = char_ptr_crc32("CUSTOM_COLORGRADE_SATORDER");
   constexpr uint32_t CUSTOM_UPSCALE_MOV                = char_ptr_crc32("CUSTOM_UPSCALE_MOV");
   constexpr uint32_t CUSTOM_UPSCALE_BGSPRITES          = char_ptr_crc32("CUSTOM_UPSCALE_BGSPRITES");
   constexpr uint32_t CUSTOM_UPSCALE_TOON               = char_ptr_crc32("CUSTOM_UPSCALE_TOON");
   constexpr uint32_t CUSTOM_HUDBRIGHTNESS              = char_ptr_crc32("CUSTOM_HUDBRIGHTNESS");
   constexpr uint32_t CUSTOM_GAMMA_CORRECTION_MODE      = char_ptr_crc32("CUSTOM_GAMMA_CORRECTION_MODE");
   constexpr uint32_t CUSTOM_GAMMACORRECT22             = char_ptr_crc32("CUSTOM_GAMMACORRECT22");
   // constexpr uint32_t CUSTOM_UITRANSPARENCY             = char_ptr_crc32("CUSTOM_UITRANSPARENCY");
   constexpr uint32_t CUSTOM_TESTBGSPRITES              = char_ptr_crc32("CUSTOM_TESTBGSPRITES");
   constexpr uint32_t CUSTOM_UPGRADE_DEBUG              = char_ptr_crc32("CUSTOM_UPGRADE_DEBUG");
   constexpr uint32_t CUSTOM_PROGRESSBAR                = char_ptr_crc32("CUSTOM_PROGRESSBAR");
   constexpr uint32_t CUSTOM_TONEMAP_IDENTIFY           = char_ptr_crc32("CUSTOM_TONEMAP_IDENTIFY");
   constexpr uint32_t CUSTOM_SDR_1                      = char_ptr_crc32("CUSTOM_SDR_1");
   constexpr uint32_t CUSTOM_PERCHANNELLUMAEMULATE      = char_ptr_crc32("CUSTOM_PERCHANNELLUMAEMULATE");
   constexpr uint32_t CUSTOM_BLOOM_THRESHOLD_1          = char_ptr_crc32("CUSTOM_BLOOM_THRESHOLD_1");
   constexpr uint32_t XEGTAO_SLICECOUNT                 = char_ptr_crc32("XEGTAO_SLICECOUNT");
   constexpr uint32_t XEGTAO_STEPSPERSLICE              = char_ptr_crc32("XEGTAO_STEPSPERSLICE");
   constexpr uint32_t XEGTAO_HALFRES                    = char_ptr_crc32("XEGTAO_HALFRES");
   constexpr uint32_t XEGTAO_NOISE                      = char_ptr_crc32("XEGTAO_NOISE");
   constexpr uint32_t XEGTAO_NORMALSMOOTH_QUALITY       = char_ptr_crc32("XEGTAO_NORMALSMOOTH_QUALITY");
   constexpr uint32_t XEGTAO_CHECKBOARD                 = char_ptr_crc32("XEGTAO_CHECKBOARD");
   constexpr uint32_t XEGTAO_UPSAMPLE                   = char_ptr_crc32("XEGTAO_UPSAMPLE");
   constexpr uint32_t XEGTAO_MANUALSIZE                 = char_ptr_crc32("XEGTAO_MANUALSIZE");
   constexpr uint32_t XEGTAO_THREADS_NORMALSGEN         = char_ptr_crc32("XEGTAO_THREADS_NORMALSGEN");
   constexpr uint32_t XEGTAO_THREADS_NORMALSSMOOTH      = char_ptr_crc32("XEGTAO_THREADS_NORMALSSMOOTH");
   constexpr uint32_t XEGTAO_THREADS_AO                 = char_ptr_crc32("XEGTAO_THREADS_AO");
   constexpr uint32_t XEGTAO_THREADS_DENOISE            = char_ptr_crc32("XEGTAO_THREADS_DENOISE");
   constexpr uint32_t CUSTOM_PS4BLUR_1                  = char_ptr_crc32("CUSTOM_PS4BLUR_1");
   constexpr uint32_t CUSTOM_HDRTONEMAPONSDR            = char_ptr_crc32("CUSTOM_HDRTONEMAPONSDR");
   constexpr uint32_t CUSTOM_SSAA_FILTER                = char_ptr_crc32("CUSTOM_SSAA_FILTER");
   constexpr uint32_t CUSTOM_ESM_DEPTH_NEW_SIZE         = char_ptr_crc32("CUSTOM_ESM_DEPTH_NEW_SIZE");
   constexpr uint32_t CUSTOM_ESM_DEPTH_FILTERREDUCE     = char_ptr_crc32("CUSTOM_ESM_DEPTH_FILTERREDUCE");
   constexpr uint32_t CUSTOM_ESM_DEPTH_SIGMAINCREASE    = char_ptr_crc32("CUSTOM_ESM_DEPTH_SIGMAINCREASE");
   constexpr uint32_t CUSTOM_ESM_COLOR_SIGMA            = char_ptr_crc32("CUSTOM_ESM_COLOR_SIGMA");
   constexpr uint32_t CUSTOM_DWR_SIGMA                  = char_ptr_crc32("CUSTOM_DWR_SIGMA");
   constexpr uint32_t CUSTOM_SSS_HQBLUR                 = char_ptr_crc32("CUSTOM_SSS_HQBLUR");
   constexpr uint32_t CUSTOM_SSS_EYEDODGE               = char_ptr_crc32("CUSTOM_SSS_EYEDODGE");

   void OnInit()
   {
      std::vector<ShaderDefineData> game_shader_defines_data = {
         {"GAMMA_CORRECTION_RANGE_TYPE", '0', true, false, "0 - Full range.\n1 - 0-1 only.", 1},
         {"SWAPCHAIN_CLAMP_COLORSPACE", '0', true, false, "Clamp colorspace against invalid colors.\n(Really only for OCD, as it should only be inconsequential black.)\n0 - Unclamped.\n1 - BT2020.", 1},
         {"SWAPCHAIN_TEST_USER_PEAK", '0', true, false, "Show a simple white rectangle peak test.", 1},
         {"CUSTOM_SDR_1", '0', true, false, "SDR path.", 1},
         {"CUSTOM_TONEMAP_SCALING", '0', true, false, "HDR tonemap scaling.\n0 - Luminance (natural)\n1 - Max-Channel (saturation preserve)", 1},
         {"CUSTOM_TONEMAP_CLAMP", '1', true, false, "(Only if CUSTOM_TONEMAP_SCALING is luminance scaled.)\nClamp overshoot from luma scaled HDR tonemap.\n0 - Unclamped (up to display).\n1 - Per channel clamp (blows out).\n2 - Scale down by max channel (sat preserving).", 2},
         {"CUSTOM_CLAMP_PEAK", '1', true, false, "Clamp the absolute final color.\n0 - Unclamped (up to display).\n1 - Per channel clamp (blows out).\n2 - Scale down by max channel (sat preserving).\n3 - Per channel rolloff slightly above peak (blows out).", 3},
         {"CUSTOM_GAMMA_CORRECTION_MODE", '0', true, false, "0 - Per-Channel.\n1 - Perceptual.", 1},
         {"CUSTOM_FAKEBT2020", '0', true, false, "Encode BT2020 before gamma decode to push colors out to wcg.", 1},
         {"CUSTOM_LUT_BLOWOUT_GAUSSIAN", '1', true, false, "Enable YCbCr LUT biased gaussian blur to stop steep chrominance drop offs in the curve.", 1},
         {"CUSTOM_LUT_BLOWOUT_GAUSSIAN_STOPS", '1', true, false, "Enable YCbCr LUT biased gaussian blur responds to HDR stops.", 1},
         {"CUSTOM_PCC_QUALITY", '0', true, false, "Quality of Per-CHannel Blowout blending.", 1},
         {"CUSTOM_COLORGRADE", '0', true, false, "Enable HDR luminance color grading.", 2},
         {"CUSTOM_COLORGRADE_SATORDER", '0', true, false, "Enable HDR global saturation slider.\n0 - Off\n1 - BT709 Before UI\n2 - BT2020 After UI", 2},
         {"CUSTOM_HUDBRIGHTNESS", '0', true, false, "Sample shader texture resources to detect specific UI to change their brightness.\nElse, they are too bright.", 2},
         {"CUSTOM_HDTVREC709_1", '0', true, false, "Decode color and swapchain to HDTV rec.709, like PS4's display output.", 1},
         {"CUSTOM_GAMMACORRECT22", '1', true, false, "Enable Gamma Correction 2.2 for OS and displays missing it.", 1},
         {"CUSTOM_PROGRESSBAR", '0', true, false, "Play head progress bar.", 2},
         {"CUSTOM_PS4BLUR_1", '0', true, false, "PS4 frame blur / ghosting.", 2},
         {"CUSTOM_BLOOM_THRESHOLD_1", '0', true, false, "Bloom threshold mode.", 4},
         {"CUSTOM_SSAA_FILTER", '0', true, false, "SSAA downsample filter", 2},
         {"CUSTOM_HDRTONEMAPONSDR", '0', true, false, "Use new HDR tonemapping in SDR path.", 1},
         {"CUSTOM_ESM_DEPTH_NEW_SIZE", '0', true, false, "Reduce shadow map gaussian blur steps.", 9},
         {"CUSTOM_ESM_DEPTH_FILTERREDUCE", '1', true, false, "Reduce shadow map gaussian blur steps.", 9},
         {"CUSTOM_ESM_DEPTH_SIGMAINCREASE", '0', true, false, "Reduce shadow map gaussian blur steps.", 9},
         {"CUSTOM_ESM_COLOR_SIGMA", '4', true, false, "Reduce shadow map gaussian blur steps.", 9},
         {"CUSTOM_DWR_SIGMA", '4', true, false, "Duplicate World Reflections sigma.", 9},
         {"CUSTOM_SSS_HQBLUR", '0', true, false, "Increase SSS blur taps.", 1},
         {"CUSTOM_SSS_EYEDODGE", '0', true, false, "Eyes won't use SSS resolve results.", 1},
         {"CUSTOM_PERCHANNELLUMAEMULATE", '1', true, false, "Emulate luminance loss from LDR per-channel tonemapping on single channel bright colors.", 1},
         {"XEGTAO_SLICECOUNT", '1', true, false, "XeGTAO samples.", 6},
         {"XEGTAO_STEPSPERSLICE", '0', true, false, "XeGTAO samples.", 2},
         {"XEGTAO_HALFRES", '1', true, false, "XeGTAO half resolution.", 1},
         {"XEGTAO_NOISE", '4', true, false, "XeGTAO moving noise.", 9},
         {"XEGTAO_NORMALSMOOTH_QUALITY", '1', true, false, "XeGTAO smooth normals quality.", 2},
         {"XEGTAO_MANUALSIZE", '0', true, false, "XeGTAO compute viewport size in shader.", 1},
         {"XEGTAO_CHECKBOARD", '0', true, false, "XeGTAO checkerboard rendering.", 2},
         {"XEGTAO_UPSAMPLE", '1', true, false, "XeGTAO Joint Bilateral Upsample.", 1},
         {"XEGTAO_THREADS_NORMALSGEN", '0', true, false, "XeGTAO compute shader thread groups.", 1},
         {"XEGTAO_THREADS_NORMALSSMOOTH", '0', true, false, "XeGTAO compute shader thread groups.", 1},
         {"XEGTAO_THREADS_AO", '1', true, false, "XeGTAO compute shader thread groups.", 1},
         {"XEGTAO_THREADS_DENOISE", '0', true, false, "XeGTAO compute shader thread groups.", 1},
         {"CUSTOM_TESTBGSPRITES", '0', true, false, "Test BG Sprites layering.", 2},
         {"CUSTOM_TONEMAP_IDENTIFY", '0', true, false, "Draw binary representation of tonemap uber variant number.", 1},
         {"CUSTOM_UPSCALE_MOV", '0', true, false, "PumboAutoHDR for FMV.\n0 - Off\n1 - On", 1},
         {"CUSTOM_UPSCALE_BGSPRITES", '0', true, false, "Auto HDR (Inverse Tonemap) for background 2D sprites in complex \"Future Tone\" scenes (e.g. Torinoko City).", 1},
         {"CUSTOM_UPSCALE_TOON", '0', true, false, "Auto HDR for flat toon scenes (e.g. Catch the Wave, Deep Sea City Underground, etc.).\n0 - Forced SDR\n1 - Treat as Complex\n2 - On\n3 - On (Ignore Customization Menu)", 3},
         {"CUSTOM_UPGRADE_DEBUG", '0', true, false, "Show inputs into UpgradeToneMap().", 5},
      };
      shader_defines_data.append_range(game_shader_defines_data);
      auto_recompile_defines = true; //force
      ASSERT_MSG(shader_defines_data.size() < MAX_SHADER_DEFINES, "Too many shader defines!");
      
      // Default built-in
      GetShaderDefineData(POST_PROCESS_SPACE_TYPE_HASH).SetDefaultValue('1'); GetShaderDefineData(POST_PROCESS_SPACE_TYPE_HASH).SetValue('1'); GetShaderDefineData(GAMMA_CORRECTION_TYPE_HASH).SetValueFixed(true);
      GetShaderDefineData(EARLY_DISPLAY_ENCODING_HASH).SetDefaultValue('0'); GetShaderDefineData(EARLY_DISPLAY_ENCODING_HASH).SetValue('0'); GetShaderDefineData(EARLY_DISPLAY_ENCODING_HASH).SetValueFixed(true);
      GetShaderDefineData(VANILLA_ENCODING_TYPE_HASH).SetDefaultValue('0'); GetShaderDefineData(VANILLA_ENCODING_TYPE_HASH).SetValue('0'); GetShaderDefineData(VANILLA_ENCODING_TYPE_HASH).SetValueFixed(true);
      GetShaderDefineData(GAMMA_CORRECTION_TYPE_HASH).SetDefaultValue('0'); GetShaderDefineData(GAMMA_CORRECTION_TYPE_HASH).SetValue('0'); GetShaderDefineData(GAMMA_CORRECTION_TYPE_HASH).SetValueFixed(true);
      GetShaderDefineData(UI_DRAW_TYPE_HASH).SetDefaultValue('2'); GetShaderDefineData(UI_DRAW_TYPE_HASH).SetValue('2'); GetShaderDefineData(UI_DRAW_TYPE_HASH).SetValueFixed(true);
   }

   void OnLoadConfigs()
   {
   }

   void OnShaderDefinesChanged()
   {
   }

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

   static bool GetB(uint32_t p)
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
      if (d->editable_data.value[0] == c) return;
      d->SetValue(c);
      defines_need_recompilation = true;
   }

   static void Set(uint32_t p, bool b)
   {
      Set(p, b ? '1' : '0');
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

   static bool UIToggleCheckmark(uint32_t d, const char* label, const char* tooltip, bool is_show_reset = true)
   {
      bool def = GetB(d);
      
      ImGui::PushID(std::string(label).append("_").append(std::to_string(d)).c_str());
      bool c = ImGui::Checkbox(label, &def);
      ImGui::PopID();

      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip(tooltip);
      
      if (c) ToggleBool(d);
      
      if (is_show_reset) UIResetButton(d);
      return def;
   }
      
   int UIDropDown(uint32_t d, const char* label, const char* const items[], const char* tooltip, bool is_show_reset = true)
   {
      int def = Get(d);
      bool c = ImGui::Combo(label, &def, items, IM_ARRAYSIZE(items));
      if (c) Set(d, def);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip(tooltip);
      if (is_show_reset) UIResetButton(d);
      return def;
   }

   // Overload: pass items inline as braced args, e.g. {"A", "B", "C"}
   int UIDropDown(uint32_t d, const char* label, std::initializer_list<const char*> items_list, const char* tooltip, bool is_show_reset = true)
   {
      std::vector<const char*> items(items_list);
      int def = Get(d);
      bool c = ImGui::Combo(label, &def, items.data(), static_cast<int>(items.size()));
      if (c) Set(d, def);
      if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip(tooltip);
      if (is_show_reset) UIResetButton(d);
      return def;
   }
}

namespace AutoExposureFix
{
   int fps_limit = 60;
   constexpr auto reshadesave = "AutoExposureFix";
   bool IsEnabled() { return fps_limit > 0; }

   int viewport_curr_index = 0;
   TimerTimestamp::TimerTimestamp timer = TimerTimestamp::Default();

   bool UpdateAndGetIsLimitAllowed()
   {
      // update loop
      timer.Update();

      // allow
      if (timer.IsCompleted())
      {
         timer.Start(FPSToMilliseconds(fps_limit));
         return true;
      }

      // disallow
      return false;
   }

   DrawOrDispatchOverrideType OnDrawOrDispatchOverride(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, uint32_t ps)
   {
      ASSERT_MSG(!(ps == ShaderHashesLists::AutoExposure0 && RenderTracker::is_drawn_to_autoexposure0), "AutoExposureFix: Already drawn!");
      
      // gatekeep
      if (!IsEnabled() || ps != ShaderHashesLists::AutoExposure0 || RenderTracker::is_drawn_to_autoexposure0 || TonemapInfo::GetIsDrawnTonemapOrFinal(cb_luma_global_settings.GameSettings.TonemapInfo))
         return DrawOrDispatchOverrideType::None;

      // progress
      RenderTracker::is_drawn_to_autoexposure0 = true;

      // detect if writing to all 32x1 (clears history) or just 1x1 (preserves history)
      D3D11_VIEWPORT vp{};
      UINT num_vp = 1;
      native_device_context->RSGetViewports(&num_vp, &vp);
      ASSERT_MSG(num_vp == 1, "AutoExposureFix: Unexpected number of viewports!");
      ASSERT_MSG(vp.Width == 1 || vp.Width == 32, "AutoExposureFix: Unexpected viewport width!");
      bool is_clear_mode = vp.Width > 1.f;

      // allow: clear mode
      if (is_clear_mode) return DrawOrDispatchOverrideType::None;
      
      // allow: redirect index to ours
      if (UpdateAndGetIsLimitAllowed())
      {
         vp.TopLeftX = static_cast<float>(viewport_curr_index);
         native_device_context->RSSetViewports(1, &vp);
         viewport_curr_index = (viewport_curr_index + 1) % 32;
         return DrawOrDispatchOverrideType::None;
      }

      // disallow: not clear or allowed
      return DrawOrDispatchOverrideType::Skip;
   }

   void OnUserSettingsChange()
   {
      timer = {};
   }

   void OnLoad(reshade::api::effect_runtime* runtime)
   {
      reshade::get_config_value(runtime, NAME, reshadesave, fps_limit);
   }
}

namespace CachedCB
{
   constexpr float white_clip_def = /*0.022f*//*0.1650*/0.1350;
   float white_clip = white_clip_def;

   float Encode_sRGB(float x)
   {
      return x <= 0.0031308f ? x * 12.92f : 1.055f * powf(x, 1.f / 2.4f) - 0.055f;
   }

   float Decode_sRGB(float x)
   {
      return x <= 0.04045f ? x / 12.92f : powf((x + 0.055f) / 1.055f, 2.4f);
   }

   float Encode_Rec709(float x)
   {
      return 0.0179999992f >= x ? 4.5f * x : 1.09899998f * powf(x, 0.449999988f) - 0.0989999995f;
   }

   float Decode_Rec709(float x)
   {
      return 0.0810000002f >= x ? 0.222222224f * x : powf(0.909918129f * (0.0989999995f + x), 2.22222233f);
   }

   float Rec709Correction(float x)
   {
      x = Encode_sRGB(x);
      x = Decode_Rec709(x);
      return x;
   }
   
   float CalcWhiteClip(float p, float pw, float wc)
   {
      float bruh1 = (p / 1000.f);
      float bruh = bruh1;
      bruh = powf(bruh, bruh1 < 1.f ? 4.4f : 3.6f); // fudge
      return (wc / pw) * 6000000.f * bruh; //kms, this is the biggest bandaid of all bandaids. gamma lighting ahh
   }

   float CalcPeak(float p, float pw, bool rec709)
   {
      p /= pw;
      if (rec709) p = Rec709Correction(p);
      return p;
   }

   float CalcIntScaling(float spw, float uipw, bool rec709)
   {
      float is = spw / uipw;
      if (rec709) is = Rec709Correction(is);
      return is;
   }

   void OnPresent() // Update loop
   {
      // changed?
      bool is_rec709 = ShaderDefineInfo::GetB(ShaderDefineInfo::CUSTOM_HDTVREC709_1);
      
      static float peak_prev = 0;
      static float paper_prev = 0;
      static float ui_paper_prev = 0;
      static float white_clip_prev = 0;
      static bool is_rec709_prev = false;

      [[unlikely]]
      if (cb_luma_global_settings.ScenePeakWhite != peak_prev ||
         cb_luma_global_settings.ScenePaperWhite != paper_prev ||
         cb_luma_global_settings.UIPaperWhite != ui_paper_prev ||
         white_clip != white_clip_prev ||
         is_rec709 != is_rec709_prev)
      {
         peak_prev = cb_luma_global_settings.ScenePeakWhite;
         paper_prev = cb_luma_global_settings.ScenePaperWhite;
         white_clip_prev = white_clip;
         ui_paper_prev = cb_luma_global_settings.UIPaperWhite;
         is_rec709_prev = is_rec709;

         // update
         cb_luma_global_settings.GameSettings.TonemapperPeakCached = CalcPeak(cb_luma_global_settings.ScenePeakWhite, cb_luma_global_settings.ScenePaperWhite, is_rec709);
         cb_luma_global_settings.GameSettings.TonemapperMaxExpectedCached = CalcWhiteClip(cb_luma_global_settings.ScenePeakWhite, cb_luma_global_settings.ScenePaperWhite, white_clip);
         cb_luma_global_settings.GameSettings.IntermediateScalingCached = CalcIntScaling(cb_luma_global_settings.ScenePaperWhite, cb_luma_global_settings.UIPaperWhite, is_rec709);

         // log
         // if (DEVELOPMENT) reshade::log::message(reshade::log::level::info, std::format("CachedCB: Peak: {}, Paper: {}, UI Paper: {}, WhiteClip: {}, Rec709: {}",
         //    cb_luma_global_settings.GameSettings.TonemapperPeakCached,
         //    cb_luma_global_settings.GameSettings.TonemapperMaxExpectedCached,
         //    cb_luma_global_settings.GameSettings.IntermediateScalingCached,
         //    white_clip,
         //    is_rec709).c_str());
      }
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

namespace MemoryHack
{
   uintptr_t base;
   uint32_t*  addr_puiGameLimit;
   uintptr_t* addr_menuFlagPtr;
   char*   addr_pvNameString;
   uint32_t*  addr_pvID;
   float_t*   addr_pvTimeSec;
   float_t*   addr_pvTimeTotalSec;

   void Init()
   {
      uintptr_t base = std::bit_cast<uintptr_t>(GetModuleHandleA("DivaMegaMix.exe"));
      ASSERT_MSG(base != 0, "FATAL: Failed to get base address of exe.");
      addr_puiGameLimit   = std::bit_cast<uint32_t*> (base + 0x14ABBB8);
      addr_menuFlagPtr    = std::bit_cast<uintptr_t*>(base + 0x11481E8); //to object
      addr_pvNameString   = std::bit_cast<char*>     (base + 0x12EF228); //failable
      addr_pvID           = std::bit_cast<uint32_t*> (base + 0x12B6350); //there are also like 5 other addresses
      addr_pvTimeSec      = std::bit_cast<float_t*>  (base + 0x12EF66C); //float
      addr_pvTimeTotalSec = std::bit_cast<float_t*>  (base + 0x12EF668); //float
   }

   //from obj @ ptr
   bool IsMenu() 
   {
      uintptr_t obj = *addr_menuFlagPtr;
      if (obj == 0) return false;
      return (*std::bit_cast<uint8_t*>(obj + 0x780) & 0x1) != 0;
   }
}

namespace IndividualPVTuning
{
   bool enabled = true;
   bool is_first_song = true; //token for first boot to first song played
   
   struct PVItem
   {
      int id = -1;
      bool is_clamp_1_stop = false;
      std::string reason;
   };

   //list of PVItems
   static std::vector<PVItem> pv_items = { //TODO: prob best if loaded as csv file
      {40, true, "(Yellow) Higher stops ruins luminance composition and burns your eyes."},
      {615, true, "(Melancholic) Higher stops ruins luminance composition and burns your eyes."}, 
      {3, true, "(That One Second in Slow Motion) Higher stops ruins sky, which is like in 90% of shots. Also lower for luminance consistency."}, 
      {814, true, "(Calc.) PV doesn't have good luminance for higher stops."}, 
      {807, true, "(Tale of the Deep-sea Lily) PV doesn't enough luminance for higher stops. So, rather force lower nits for faithful UI clipped hues."},
      {739, true, "(Decorator) Might as well be an FMV..."}, 
      {250, true, "(Nice To Meet You, Mr. Earthling) It's quite bright and reveals gamma lighting."}, 
      {261, true, "(Kimi no Taion) The few specular highlights are too jarring."},
      {261, true, "(Catch the Wave) With toon shading removed, background is about +1 stop, so limit to match with 3D elems."},
      {82, true, "(Two-Sided Lovers) With toon shading removed, background is about +1 stop, so limit to match with 3D elems."},
      {727, true, "(Love-Hate) With toon shading removed, background is about +1 stop, so limit to match with 3D elems."},
      {629, true, "(Negaposi＊Continues) With toon shading removed, background is about +1 stop, so limit to match with 3D elems."},
      {434, true, "(Oha-Yo-del!!) With toon shading removed, background is about +1 stop, so limit to match with 3D elems."},
      {243, true, "(Interviewer) With toon shading removed, background is about +1 stop, so limit to match with 3D elems."},
      {244, true, "(Snowman) With toon shading removed, background is about +1 stop, so limit to match with 3D elems."},
      {234, true, "(Deep Sea City Underground) With toon shading removed, background is about +1 stop, so limit to match with 3D elems."},
      // {251, true, "(PIANO*GIRL) With toon shading removed, background is about +1 stop, so limit to match with 3D elems."}, //has non toon sections worth allowing
      // {0, true, ""}, //
   };

   struct CurrentPV
   {
      uint id;
      PVItem* item;
   };
   CurrentPV current_pv;

   struct PrevSettings
   {
      float peak = -1;
   };
   PrevSettings prev_settings;
   
   void OnPresent()
   {      
      //is_disable_this_frame: special or gatekeep
      bool is_disable_this_frame = false;

      //if !enabled or SDR, return
      if (!enabled || cb_luma_global_settings.DisplayMode == DisplayModeType::SDR)
      {
         if (current_pv.item != nullptr) is_disable_this_frame = true;
         else return; //unnecessary to run the rest
      }

      //wait until first song played
      if (is_first_song && MemoryHack::addr_pvNameString[0] == 0) return;
      is_first_song = false;
      
      //dirty? (should be right as the game starts loading new PV)
      uint pv_id = is_disable_this_frame ? -1 : *MemoryHack::addr_pvID;
      bool is_dirty = current_pv.id != pv_id;

      //prev backup
      PVItem* prev_pv = current_pv.item;
      
      //current_pv
      [[unlikely]] if (is_dirty)
      {
         //id
         current_pv.id = pv_id;
         
         //item find
         current_pv.item = nullptr;
         for (auto& item : pv_items)
         {
            if (item.id == current_pv.id)
            {
               current_pv.item = &item;
               break;
            }
         }
      }
      
      //restore prev settings if changed
      [[unlikely]] if (is_dirty && prev_pv != nullptr)
      {
         //peak
         if (prev_pv->is_clamp_1_stop && roundf(cb_luma_global_settings.ScenePeakWhite) == roundf(cb_luma_global_settings.ScenePaperWhite * 2.f))
            cb_luma_global_settings.ScenePeakWhite = prev_settings.peak;
      }

      //save settings & apply new settings
      [[unlikely]] if (is_dirty && current_pv.item != nullptr)
      {
         //reset prev_settings
         prev_settings = PrevSettings();
         
         //peak
         if (current_pv.item->is_clamp_1_stop)
         {
            prev_settings.peak = cb_luma_global_settings.ScenePeakWhite;
            cb_luma_global_settings.ScenePeakWhite = cb_luma_global_settings.ScenePeakWhite = roundf(cb_luma_global_settings.ScenePaperWhite * 2.f); //+1 stop
            cb_luma_global_settings.ScenePeakWhite = min(prev_settings.peak, cb_luma_global_settings.ScenePeakWhite); //but don't exceed user
            reshade::set_config_value(nullptr, NAME, "ScenePeakWhite", prev_settings.peak); //just in case
         }

         //log
         std::string s;
         s = "IndividualPVTuning::OnPresent() Current PV: " + std::to_string(current_pv.id) + " " + (current_pv.item != nullptr ? "(tuning applied)" : "(no tuning)") + " " + (current_pv.item != nullptr ? current_pv.item->reason : "");
         message(reshade::log::level::info, s.c_str());
      }
   }

   void OnUI(reshade::api::effect_runtime* runtime)
   {
      DrawColoredSubHeader("For some PVs, forces +1 Stop to not ruin original composition.");
      
      if (ImGui::Checkbox("Opt Into PV Tuning", &enabled)) reshade::set_config_value(runtime, NAME, "IndividualPVTuningEnabled", enabled);
      ImGui::NewLine();

      //0 terminated string
      auto name_ptr = MemoryHack::addr_pvNameString;
      std::string name_str;
      for (int i = 0; i < 128; i++)
      {
         char c = name_ptr[i];
         if (c == 0) break;
         name_str += c;
      }

      DrawColoredSubHeader("Current");
      ImGui::Text("PV ID: %d", /*current_pv.id*/ *MemoryHack::addr_pvID);
      ImGui::Text("PV Name (maybe): %s", name_str.c_str());

      ImGui::NewLine();
      if (current_pv.item != nullptr) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.4f, 0.8f, 1.f));
      ImGui::Text("Applied Tweaks:");
      if (current_pv.item != nullptr) ImGui::PopStyleColor();
      if (current_pv.item != nullptr)
      {
         if (current_pv.item->is_clamp_1_stop) ImGui::BulletText("+1 stop Peak.");
      } else
      {
         ImGui::BulletText("None");
      }
      
      if (current_pv.item != nullptr)
      {
         ImGui::Text("Reasoning:");
         ImGui::Bullet(); ImGui::SameLine(); ImGui::TextWrapped("%s", current_pv.item->reason.c_str());
      }
   }

   void OnLoad(reshade::api::effect_runtime* runtime)
   {
      reshade::get_config_value(runtime, NAME, "IndividualPVTuningEnabled", enabled);
   }
}

namespace HighFPS
{
   //https://github.com/SpecialKO/SpecialK/blob/6fe51ee1eca4aee26a59e227ee5402ad3b55fcc0/src/plugins/unclassified.cpp#L1264

   bool enabled = false;
   int limit = 0;
   bool menu_clamp = false;

   bool IsReady()
   {
      return MemoryHack::addr_puiGameLimit != nullptr && MemoryHack::addr_menuFlagPtr != nullptr;
   }

   //must be per frame update/patch as the game forces and reset to 60
   void Patch()
   {
      if (!enabled || !IsReady()) return;
      uint32_t target = static_cast<uint32_t>(limit);
      if (menu_clamp && MemoryHack::IsMenu()) target = 60u;
      *MemoryHack::addr_puiGameLimit = target; //no need for VirtualProtect
   }

   void Unpatch()
   {
      if (!IsReady()) return;
      *MemoryHack::addr_puiGameLimit = 60u;
   }
}

namespace ProgressBar
{
   float progress_ratio = -1.f;
   float progress_ratio_prev = -1.f;

   int ColorGetPacked(float3 color_unorm)
   {
      uint8_t r = static_cast<uint8_t>(std::clamp(color_unorm.x * 255.f, 0.f, 255.f));
      uint8_t g = static_cast<uint8_t>(std::clamp(color_unorm.y * 255.f, 0.f, 255.f));
      uint8_t b = static_cast<uint8_t>(std::clamp(color_unorm.z * 255.f, 0.f, 255.f));
      uint8_t a = 255;
      return (a << 24) | (b << 16) | (g << 8) | r;
   }

   float3 ColorGetUnorm(int color_packed)
   {
      uint32_t c = static_cast<uint32_t>(color_packed);
      float r = static_cast<float>(c & 0xFF) / 255.f;
      float g = static_cast<float>((c >> 8) & 0xFF) / 255.f;
      float b = static_cast<float>((c >> 16) & 0xFF) / 255.f;
      return float3(r, g, b);
   }

   void OnUI(reshade::api::effect_runtime* runtime)
   {
      DrawColoredSubHeader("OSU looking ahh progress bar for PVs.");

      //cb
      bool enabled = ShaderDefineInfo::UIDropDown(ShaderDefineInfo::CUSTOM_PROGRESSBAR, "Enabled", {"Off", "Top", "Bottom"}, "Draw a simple progress bar for PVs.") > 0;
      if (!enabled) progress_ratio = -1.f;

      // float3 color picker
      float3 color_unorm = ColorGetUnorm(cb_luma_global_settings.GameSettings.ProgressBarColorPacked);
      if (ImGui::ColorEdit3("Color", &color_unorm.x))
      {
         cb_luma_global_settings.GameSettings.ProgressBarColorPacked = ColorGetPacked(color_unorm);
         reshade::set_config_value(runtime, NAME, "ProgressBarColorPacked", cb_luma_global_settings.GameSettings.ProgressBarColorPacked);
      }

      ImGui::NewLine();
      DrawColoredSubHeader("Progress");

      //ui progress bar
      float progress_ratio_ui = *MemoryHack::addr_pvTimeSec / *MemoryHack::addr_pvTimeTotalSec;
      ImGui::PushItemWidth(-FLT_MIN);
      ImGui::ProgressBar(progress_ratio_ui, ImVec2(-FLT_MIN, 0.0f));
      ImGui::PopItemWidth();

      //ui stats
      ImGui::Text("Time: %.2f / %.2f s", *MemoryHack::addr_pvTimeSec, *MemoryHack::addr_pvTimeTotalSec);
      ImGui::Text("Remaining: %.2f s", *MemoryHack::addr_pvTimeTotalSec - *MemoryHack::addr_pvTimeSec);
   }

   void OnPresent()
   {
      if (ShaderDefineInfo::Get(ShaderDefineInfo::CUSTOM_PROGRESSBAR) == 0) return;
      progress_ratio_prev = progress_ratio;
      progress_ratio = *MemoryHack::addr_pvTimeSec / *MemoryHack::addr_pvTimeTotalSec;
      cb_luma_global_settings.GameSettings.ProgressBarRatio = progress_ratio > progress_ratio_prev ? progress_ratio : -1;
   }

   void OnLoad(reshade::api::effect_runtime* runtime)
   {
      // ProgressBarRatio init
      cb_luma_global_settings.GameSettings.ProgressBarRatio = -1.f;

      // ProgressBarColorPacked init & load
      cb_luma_global_settings.GameSettings.ProgressBarColorPacked = 0xFFFFFFFF; // white
      reshade::get_config_value(runtime, NAME, "ProgressBarColorPacked", cb_luma_global_settings.GameSettings.ProgressBarColorPacked);
   }
}

namespace SeparateUIBrightness
{
   bool enabled = true;
   
   constexpr float brightness_menu_def = 203.f;
   constexpr float brightness_game_def = 300.f;
   constexpr float transition_ms_up_def = 500;
   constexpr float transition_ms_down_def = 100;
   float brightness_menu = brightness_menu_def;
   float brightness_game = brightness_game_def;
   float transition_ms_up = transition_ms_up_def;
   float transition_ms_down = transition_ms_down_def;

   constexpr auto reshadesave_enabled = "SeparateUIBrightnessEnabled";
   constexpr auto reshadesave_menu = "SeparateUIBrightnessMenu";
   constexpr auto reshadesave_game = "SeparateUIBrightnessGame";
   constexpr auto reshadesave_transition_ms_up = "SeparateUIBrightnessTransitionUp";
   constexpr auto reshadesave_transition_ms_down = "SeparateUIBrightnessTransitionDown";

   TimerTimestamp::TimerTimestamp transition_timer = TimerTimestamp::Default();

   float GetTargetBrightness(bool is_menu) { return is_menu ? brightness_menu : brightness_game; }
   float GetTargetBrightness() { return GetTargetBrightness(MemoryHack::IsMenu()); }

   void OnUIAlways(reshade::api::effect_runtime* runtime)
   {
      // detect change
      static bool use_os_reference_white_level_prev = false; // start false, since we don't need to do anything if so
      if (use_os_reference_white_level_prev != use_os_reference_white_level)
      {
         if (use_os_reference_white_level) enabled = false; // force off
         else reshade::get_config_value(runtime, NAME, reshadesave_enabled, enabled); // reapply user setting
      }
      use_os_reference_white_level_prev = use_os_reference_white_level;
   }

   void OnUI(reshade::api::effect_runtime* runtime)
   {
      if (use_os_reference_white_level)
      {
         ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.f, 0.f, 1.f));
         ImGui::Bullet(); ImGui::SameLine(); ImGui::TextWrapped("Please disable \"Link to OS Reference White Level\" first.");
         ImGui::PopStyleColor();
         return;
      }
      
      //enabled checkmark
      if (ImGui::Checkbox("Enabled", &enabled))
      {
         reshade::set_config_value(runtime, NAME, reshadesave_enabled, enabled);
      }
      
      bool is_disabled = !enabled;
      if (is_disabled) ImGui::BeginDisabled();
      {
         if (ImGui::SliderFloat("Menu Brightness", &brightness_menu, 1.f, 1000.f, "%.0f nits"))
            reshade::set_config_value(runtime, NAME, reshadesave_menu, brightness_menu);
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("UI paper white when browsing menus.");
         DrawResetButton(brightness_menu, brightness_menu_def, reshadesave_menu, runtime);

         if (ImGui::SliderFloat("Game Brightness", &brightness_game, 1.f, 1000.f, "%.0f nits"))
            reshade::set_config_value(runtime, NAME, reshadesave_game, brightness_game);
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("UI paper white when playing a PV / in gameplay.");
         DrawResetButton(brightness_game, brightness_game_def, reshadesave_game, runtime);
         
         float transition_up_s = transition_ms_up / 1000.f;
         if (ImGui::SliderFloat("Transition Time: Up", &transition_up_s, 0.f, 2.f, "%.2f s"))
         {
            transition_ms_up = transition_up_s * 1000.f;
            reshade::set_config_value(runtime, NAME, reshadesave_transition_ms_up, transition_ms_up);
         }
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Smooths out the transition when brightness increases.");
         DrawResetButton(transition_ms_up, transition_ms_up_def, reshadesave_transition_ms_up, runtime);

         float transition_down_s = transition_ms_down / 1000.f;
         if (ImGui::SliderFloat("Transition Time: Down", &transition_down_s, 0.f, 2.f, "%.2f s"))
         {
            transition_ms_down = transition_down_s * 1000.f;
            reshade::set_config_value(runtime, NAME, reshadesave_transition_ms_down, transition_ms_down);
         }
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Smooths out the transition when brightness decreases.");
         DrawResetButton(transition_ms_down, transition_ms_down_def, reshadesave_transition_ms_down, runtime);
      }
      if (is_disabled) ImGui::EndDisabled();
   }

   void OnPresent()
   {
      if (!enabled) return;

      // SDR
      if (cb_luma_global_settings.DisplayMode == DisplayModeType::SDR)
      {
         cb_luma_global_settings.UIPaperWhite = 80;
         return;
      }

      // menu change to start timer
      static bool is_menu_prev = MemoryHack::IsMenu();
      bool is_menu_curr = MemoryHack::IsMenu();
      static float brightness_on_start = 0;
      [[unlikely]] if (is_menu_prev != is_menu_curr)
      {
         is_menu_prev = is_menu_curr;
         brightness_on_start = cb_luma_global_settings.UIPaperWhite;
         
         float brightness_target = GetTargetBrightness(is_menu_curr);
         float transition_ms = brightness_target > brightness_on_start ? transition_ms_up : transition_ms_down;
         if (transition_ms <= 0) return;
         if (brightness_on_start == brightness_target) return;
         
         float full_diff = std::abs(brightness_game - brightness_menu);
         float curr_diff = std::abs(brightness_on_start - brightness_target);
         
         float diff_ratio = curr_diff / full_diff;
         float scaled_transition_ms = transition_ms * diff_ratio;
         
         transition_timer.Start(scaled_transition_ms);
      }

      // update
      if (transition_timer.IsCompleted())
      {
         cb_luma_global_settings.UIPaperWhite = GetTargetBrightness(is_menu_curr);
      }
      else [[unlikely]]
      {
         auto ur = transition_timer.Update();
         cb_luma_global_settings.UIPaperWhite = std::lerp(brightness_on_start, GetTargetBrightness(is_menu_curr), ur.ratio_until_finish);
      }
   }

   void OnLoad(reshade::api::effect_runtime* runtime)
   {
      reshade::get_config_value(runtime, NAME, reshadesave_enabled, enabled);
      if (use_os_reference_white_level) enabled = false; // conflicts if not.
      
      reshade::get_config_value(runtime, NAME, reshadesave_menu, brightness_menu);
      reshade::get_config_value(runtime, NAME, reshadesave_game, brightness_game);
      reshade::get_config_value(runtime, NAME, reshadesave_transition_ms_up, transition_ms_up);
      reshade::get_config_value(runtime, NAME, reshadesave_transition_ms_down, transition_ms_down);
   }
}

namespace SSAA // detects https://github.com/korenkonder/MMPlusMods/blob/master/src/SSAAEnable/dllmain.cpp
{
   bool enabled = false;
   
   constexpr const char* Luma_MegaMix_Final = "Luma_MegaMix_Final"; // file & shader name
   
   void OnInit()
   {
      native_shaders_definitions.emplace(CompileTimeStringHash(Luma_MegaMix_Final), ShaderDefinition{ Luma_MegaMix_Final, reshade::api::pipeline_subobject_type::pixel_shader, nullptr, "main", {{"CUSTOM_SSAA", "1"}} });
   }

   void OnRenderResolutionFound(uint2 render_resolution, DeviceData& device_data)
   {
      // check resolution TODO: instead of this, use memory hack?
      uint rtv_pix = render_resolution.x * render_resolution.y;
      uint native_pix = device_data.output_resolution.x * device_data.output_resolution.y;
      uint thres = native_pix * 1.1f;
      enabled = rtv_pix > thres;
      reshade::log::message(reshade::log::level::info, std::format("SSAA::OnRenderResolutionFound(): SSAA {} (Render: {}x{}, Output: {}x{})", enabled ? "enabled" : "disabled", render_resolution.x, render_resolution.y, device_data.output_resolution.x, device_data.output_resolution.y).c_str());
   }
   
   // normal Final isn't drawn when SSAA. we must find other ways to check
   bool IsFinalDrawing(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, uint32_t ps)
   {
      if (!enabled) return false;

      // must be after tonemap (else, it's redundant anyways)
      if (!TonemapInfo::GetDrawnTonemap(cb_luma_global_settings.GameSettings.TonemapInfo)) return false;

      // must be "sprite_simplest_0x26AF16B8"
      if (ps != 0x26AF16B8) return false;

      return true;
   }

   void OnDrawFinal(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data)
   {
      if (!enabled) return;

      // set PS to ours
      native_device_context->PSSetShader(device_data.native_pixel_shaders.at(CompileTimeStringHash(Luma_MegaMix_Final)).get(), nullptr, 0);

      // set linear sampler s1
      native_device_context->PSSetSamplers(1, 1, &device_data.sampler_state_linear);
   }

   void OnPresent()
   {

   }
}

namespace XeGTAO
{
   bool enabled = false;
   constexpr const char* reshadesave_enabled = "XeGTAOEnabled";

   bool disallow_for_npr = true;
   constexpr const char* reshadesave_disallow_for_npr = "XeGTAODisallowForNPR";

   PUBLISHING_CONSTEXPR bool is_fog_dodge = true; // use fog dodging variant // TODO: still false pos, not enough dodge (Doubleganger) or too much (Secret Garden)

   int denoise_count = 3; // denoise the AO result
   constexpr const char* reshadesave_denoise = "XeGTAODenoise";

   PUBLISHING_CONSTEXPR bool debug_late = false;
   PUBLISHING_CONSTEXPR bool debug_skipsmooth = false;

   enum DebugOut : uint8_t
   {
      None,
      AO,
      Normals,
      Depth,
   };
   DebugOut debug_out = None;

   enum State : uint8_t
   {
      Unknown, // on boot
      Ready,  // ready to draw
      Done, // drawn, wait for next frame
   };
   State state = Unknown;

   // key: relevant shader that XeGTAO must insert to.
   // value: index to find main color RES. -1 means RTV.
   std::unordered_map<uint32_t, int8_t> relevant_shaders_to_main_color_srv = {
      {0xF94D4A4A, -1}, // random write before transparency
      {0x043F4B65, 1}, // DoF uses scene color + prefiltered depth to blur
      {0x68722F15, 0}, // Downsample for bloom & autoexposure
   };

   constexpr size_t DEPTH_MIP_LEVELS = 5;
   
   constexpr const char* Luma_MegaMix_XeGTAO = "Luma_MegaMix_XeGTAO"; //file name
   constexpr const char* Luma_XeGTAO_Prefilter = "XeGTAO Prefilter Depths CS";
   constexpr const char* Luma_XeGTAO_NormalGenerate = "XeGTAO Normals Generate CS";
   constexpr const char* Luma_XeGTAO_NormalSmooth1 = "XeGTAO Normals Smooth 1 CS";
   constexpr const char* Luma_XeGTAO_NormalSmooth2 = "XeGTAO Normals Smooth 2 CS";
   constexpr const char* Luma_XeGTAO_MainPass = "XeGTAO Main Pass CS";
   constexpr const char* Luma_XeGTAO_MainPassFog = "XeGTAO Main Pass Fog CS";
   constexpr const char* Luma_XeGTAO_DenoisePass1 = "XeGTAO Denoise Pass 1 CS";
   constexpr const char* Luma_XeGTAO_DenoisePass2 = "XeGTAO Denoise Pass 2 CS";
   constexpr const char* Luma_XeGTAO_Apply = "XeGTAO Apply PS";
   constexpr const char* Luma_XeGTAO_ApplyDbgNormals = "XeGTAO Apply Debug Normals PS";
   constexpr const char* Luma_XeGTAO_ApplyDbgDepth = "XeGTAO Apply Debug Depth PS";
   constexpr const char* Luma_XeGTAO_ApplyDbgAO = "XeGTAO Apply Debug AO PS";

   void OnInit()
   {
      native_shaders_definitions.emplace(CompileTimeStringHash(Luma_XeGTAO_Prefilter),       ShaderDefinition{ Luma_MegaMix_XeGTAO, reshade::api::pipeline_subobject_type::compute_shader, nullptr, "prefilter_depths16x16_cs" });
      native_shaders_definitions.emplace(CompileTimeStringHash(Luma_XeGTAO_NormalGenerate),  ShaderDefinition{ Luma_MegaMix_XeGTAO, reshade::api::pipeline_subobject_type::compute_shader, nullptr, "normal_generate_cs" });
      native_shaders_definitions.emplace(CompileTimeStringHash(Luma_XeGTAO_NormalSmooth1),   ShaderDefinition{ Luma_MegaMix_XeGTAO, reshade::api::pipeline_subobject_type::compute_shader, nullptr, "normal_smooth_cs" });
      native_shaders_definitions.emplace(CompileTimeStringHash(Luma_XeGTAO_NormalSmooth2),   ShaderDefinition{ Luma_MegaMix_XeGTAO, reshade::api::pipeline_subobject_type::compute_shader, nullptr, "normal_smooth_cs", {{ "XE_GTAO_NORMALSMOOTH_2ND", "1" }} });
      native_shaders_definitions.emplace(CompileTimeStringHash(Luma_XeGTAO_MainPass),        ShaderDefinition{ Luma_MegaMix_XeGTAO, reshade::api::pipeline_subobject_type::compute_shader, nullptr, "main_pass_cs" });
      native_shaders_definitions.emplace(CompileTimeStringHash(Luma_XeGTAO_MainPassFog),     ShaderDefinition{ Luma_MegaMix_XeGTAO, reshade::api::pipeline_subobject_type::compute_shader, nullptr, "main_pass_cs" , { { "XEGTAO_FOG", "1" }} });
      native_shaders_definitions.emplace(CompileTimeStringHash(Luma_XeGTAO_DenoisePass1),    ShaderDefinition{ Luma_MegaMix_XeGTAO, reshade::api::pipeline_subobject_type::compute_shader, nullptr, "denoise_pass_cs" });
      native_shaders_definitions.emplace(CompileTimeStringHash(Luma_XeGTAO_DenoisePass2),    ShaderDefinition{ Luma_MegaMix_XeGTAO, reshade::api::pipeline_subobject_type::compute_shader, nullptr, "denoise_pass_cs", {{ "XE_GTAO_FINAL_APPLY", "1" }} });
      native_shaders_definitions.emplace(CompileTimeStringHash(Luma_XeGTAO_Apply),           ShaderDefinition{ Luma_MegaMix_XeGTAO, reshade::api::pipeline_subobject_type::pixel_shader,   nullptr, "apply_ps" });
      native_shaders_definitions.emplace(CompileTimeStringHash(Luma_XeGTAO_ApplyDbgNormals), ShaderDefinition{ Luma_MegaMix_XeGTAO, reshade::api::pipeline_subobject_type::pixel_shader,   nullptr, "apply_ps", {{ "XE_GTAO_DEBUG_NORMALS", "1" }}});
      native_shaders_definitions.emplace(CompileTimeStringHash(Luma_XeGTAO_ApplyDbgDepth),   ShaderDefinition{ Luma_MegaMix_XeGTAO, reshade::api::pipeline_subobject_type::pixel_shader,   nullptr, "apply_ps", {{ "XE_GTAO_DEBUG_DEPTH", "1" }}});
      native_shaders_definitions.emplace(CompileTimeStringHash(Luma_XeGTAO_ApplyDbgAO),      ShaderDefinition{ Luma_MegaMix_XeGTAO, reshade::api::pipeline_subobject_type::pixel_shader,   nullptr, "apply_ps", {{ "XE_GTAO_DEBUG_AO", "1" }}});
   }
   
   namespace CreatedResource
   {
      bool initialized = false;
      
      namespace PreFilteredDepth
      {
         D3D11_TEXTURE2D_DESC tex_desc;
         ComPtr<ID3D11Texture2D> tex = nullptr;

         std::array<ID3D11UnorderedAccessView*, DEPTH_MIP_LEVELS> uavs;
         
         ComPtr<ID3D11ShaderResourceView> srv = nullptr;
      }

      namespace Depth32
      {
         D3D11_TEXTURE2D_DESC tex_desc;
         ComPtr<ID3D11Texture2D> tex = nullptr;

         ComPtr<ID3D11UnorderedAccessView> uav = nullptr;
         
         ComPtr<ID3D11ShaderResourceView> srv = nullptr;
      }

      namespace Normals0
      {
         D3D11_TEXTURE2D_DESC tex_desc;
         ComPtr<ID3D11Texture2D> tex = nullptr;

         ComPtr<ID3D11UnorderedAccessView> uav = nullptr;
         
         ComPtr<ID3D11ShaderResourceView> srv = nullptr;
      }
      
      namespace Normals1
      {
         D3D11_TEXTURE2D_DESC tex_desc;
         ComPtr<ID3D11Texture2D> tex = nullptr;
      
         ComPtr<ID3D11UnorderedAccessView> uav = nullptr;
         
         ComPtr<ID3D11ShaderResourceView> srv = nullptr;
      }
      
      namespace Main0
      {
         D3D11_TEXTURE2D_DESC tex_desc;
         ComPtr<ID3D11Texture2D> tex = nullptr;

         ComPtr<ID3D11UnorderedAccessView> uav = nullptr;
         
         ComPtr<ID3D11ShaderResourceView> srv = nullptr;
      }
      
      namespace Main1
      {
         // D3D11_TEXTURE2D_DESC tex_desc; // same as Main0
         ComPtr<ID3D11Texture2D> tex = nullptr;

         ComPtr<ID3D11UnorderedAccessView> uav = nullptr;
         
         ComPtr<ID3D11ShaderResourceView> srv = nullptr;
      }
      
      namespace MainColorDuped
      {
         D3D11_TEXTURE2D_DESC tex_desc;
         ComPtr<ID3D11Texture2D> tex = nullptr;
         
         ComPtr<ID3D11ShaderResourceView> srv = nullptr;
      }
      
      void Create(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, uint2 size, bool is_half)
      {
         // gatekeep: created
         [[likely]]
         if (initialized) return;
         initialized = true;

         uint2 size_half = uint2{ size.x / 2, size.y / 2 };
         
         // PreFilteredDepth
         {
            // tex desc
            PreFilteredDepth::tex_desc = {};
            PreFilteredDepth::tex_desc.Width  = !is_half ? size.x : size_half.x;
            PreFilteredDepth::tex_desc.Height = !is_half ? size.y : size_half.y;
            PreFilteredDepth::tex_desc.MipLevels = DEPTH_MIP_LEVELS;
            PreFilteredDepth::tex_desc.ArraySize = 1;
            PreFilteredDepth::tex_desc.Format = /*DXGI_FORMAT_R32_FLOAT*/ DXGI_FORMAT_R16_FLOAT;
            PreFilteredDepth::tex_desc.SampleDesc.Count = 1;
            PreFilteredDepth::tex_desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;

            // tex
            auto hr0 = native_device->CreateTexture2D(&PreFilteredDepth::tex_desc, nullptr, PreFilteredDepth::tex.put());
            ASSERT_MSG(SUCCEEDED(hr0), "PreFilteredDepth hr0");

            // uavs
            D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc;
            uav_desc.Format = PreFilteredDepth::tex_desc.Format;
            uav_desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
            for (int i = 0; i < PreFilteredDepth::uavs.size(); ++i)
            {
               uav_desc.Texture2D.MipSlice = i;
               auto hr = native_device->CreateUnorderedAccessView(PreFilteredDepth::tex.get(), &uav_desc, &PreFilteredDepth::uavs[i]);
               ASSERT_MSG(SUCCEEDED(hr), "PreFilteredDepth loop hr");
            }
            
            // srv
            auto hr2 = native_device->CreateShaderResourceView(PreFilteredDepth::tex.get(), nullptr, PreFilteredDepth::srv.put());
            ASSERT_MSG(SUCCEEDED(hr2), "PreFilteredDepth hr2");
         }

         // Depth32
         {
            // tex desc
            Depth32::tex_desc = {};
            Depth32::tex_desc.Width  = !is_half ? size.x : size_half.x;
            Depth32::tex_desc.Height = !is_half ? size.y : size_half.y;
            Depth32::tex_desc.MipLevels = 1;
            Depth32::tex_desc.ArraySize = 1;
            Depth32::tex_desc.Format = DXGI_FORMAT_R32_FLOAT;
            Depth32::tex_desc.SampleDesc.Count = 1;
            Depth32::tex_desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;

            // tex
            auto hr0 = native_device->CreateTexture2D(&Depth32::tex_desc, nullptr, Depth32::tex.put());
            ASSERT_MSG(SUCCEEDED(hr0), "Depth32 hr0");

            // uav
            auto hr1 = native_device->CreateUnorderedAccessView(Depth32::tex.get(), nullptr, Depth32::uav.put());
            ASSERT_MSG(SUCCEEDED(hr1), "Depth32 hr1");

            // srv
            auto hr2 = native_device->CreateShaderResourceView(Depth32::tex.get(), nullptr, Depth32::srv.put());
            ASSERT_MSG(SUCCEEDED(hr2), "Depth32 hr2");
         }

         // Normals 0 & 1
         {
            // tex desc
            Normals0::tex_desc = {};
            Normals0::tex_desc.Width  = !is_half ? size.x : size_half.x;
            Normals0::tex_desc.Height = !is_half ? size.y : size_half.y;
            Normals0::tex_desc.MipLevels = 1;
            Normals0::tex_desc.ArraySize = 1;
            Normals0::tex_desc.Format = DXGI_FORMAT_R10G10B10A2_UNORM /*DXGI_FORMAT_R16G16B16A16_SNORM*/ /*DXGI_FORMAT_R11G11B10_FLOAT*/;
            Normals0::tex_desc.SampleDesc.Count = 1;
            Normals0::tex_desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;

            // tex
            auto hr0 = native_device->CreateTexture2D(&Normals0::tex_desc, nullptr, Normals0::tex.put());
            ASSERT_MSG(SUCCEEDED(hr0), "Normals hr0");
            auto hr0_1 = native_device->CreateTexture2D(&Normals0::tex_desc, nullptr, Normals1::tex.put());
            ASSERT_MSG(SUCCEEDED(hr0_1), "Normals hr0_1");

            // uav
            auto hr1 = native_device->CreateUnorderedAccessView(Normals0::tex.get(), nullptr, Normals0::uav.put());
            ASSERT_MSG(SUCCEEDED(hr1), "Normals hr1");
            auto hr1_1 = native_device->CreateUnorderedAccessView(Normals1::tex.get(), nullptr, Normals1::uav.put());
            ASSERT_MSG(SUCCEEDED(hr1_1), "Normals hr1_1");
            
            // srv
            auto hr2 = native_device->CreateShaderResourceView(Normals0::tex.get(), nullptr, Normals0::srv.put());
            ASSERT_MSG(SUCCEEDED(hr2), "Normals hr2");
            auto hr2_1 = native_device->CreateShaderResourceView(Normals1::tex.get(), nullptr, Normals1::srv.put());
            ASSERT_MSG(SUCCEEDED(hr2_1), "Normals hr2_1");
         }
         
         // Main 0 & 1
         {
            // tex desc
            Main0::tex_desc = {};
            Main0::tex_desc.Width  = !is_half ? size.x : size_half.x;
            Main0::tex_desc.Height = !is_half ? size.y : size_half.y;
            Main0::tex_desc.MipLevels = 1;
            Main0::tex_desc.ArraySize = 1;
            Main0::tex_desc.Format = DXGI_FORMAT_R8G8_UNORM;
            Main0::tex_desc.SampleDesc.Count = 1;
            Main0::tex_desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;

            // tex
            auto hr0_0 = native_device->CreateTexture2D(&Main0::tex_desc, nullptr, Main0::tex.put());
            ASSERT_MSG(SUCCEEDED(hr0_0), "Main0 hr0_0");
            auto hr0_1 = native_device->CreateTexture2D(&Main0::tex_desc, nullptr, Main1::tex.put());
            ASSERT_MSG(SUCCEEDED(hr0_1), "Main1 hr0_1");

            // uav
            auto hr1_0 = native_device->CreateUnorderedAccessView(Main0::tex.get(), nullptr, Main0::uav.put());
            ASSERT_MSG(SUCCEEDED(hr1_0), "Main0 hr1_0");
            auto hr1_1 = native_device->CreateUnorderedAccessView(Main1::tex.get(), nullptr, Main1::uav.put());
            ASSERT_MSG(SUCCEEDED(hr1_1), "Main1 hr1_1");

            // srv
            auto hr2_0 = native_device->CreateShaderResourceView(Main0::tex.get(), nullptr, Main0::srv.put());
            ASSERT_MSG(SUCCEEDED(hr2_0), "Main0 hr2_0");
            auto hr2_1 = native_device->CreateShaderResourceView(Main1::tex.get(), nullptr, Main1::srv.put());
            ASSERT_MSG(SUCCEEDED(hr2_1), "Main1 hr2_1");
         }

         // MainColorDuped
         {
            // tex desc
            MainColorDuped::tex_desc = {};
            MainColorDuped::tex_desc.Width  = size.x;
            MainColorDuped::tex_desc.Height = size.y;
            MainColorDuped::tex_desc.MipLevels = 1;
            MainColorDuped::tex_desc.ArraySize = 1;
            MainColorDuped::tex_desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
            MainColorDuped::tex_desc.SampleDesc.Count = 1;
            MainColorDuped::tex_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE /*| D3D11_BIND_RENDER_TARGET*/;

            // tex
            auto hr0 = native_device->CreateTexture2D(&MainColorDuped::tex_desc, nullptr, MainColorDuped::tex.put());
            ASSERT_MSG(SUCCEEDED(hr0), "MainColorDuped hr0");

            // srv
            auto hr1 = native_device->CreateShaderResourceView(MainColorDuped::tex.get(), nullptr, MainColorDuped::srv.put());
            ASSERT_MSG(SUCCEEDED(hr1), "MainColorDuped hr1");
            
            // // rtv
            // auto hr2 = native_device->CreateRenderTargetView(MainColorDuped::tex.get(), nullptr, MainColorDuped::rtv.put());
            // ASSERT_MSG(SUCCEEDED(hr2), "MainColorDuped hr2");
         }

         // log
         reshade::log::message(reshade::log::level::info, std::format("XeGTAO::Resource::Create() Created resources for size {}x{}", size.x, size.y).c_str());
      }

      void Reset()
      {
         initialized = false;
         
         PreFilteredDepth::tex.reset();
         for (auto& uav : PreFilteredDepth::uavs) uav = nullptr;
         PreFilteredDepth::srv.reset();

         Depth32::tex.reset();
         Depth32::uav.reset();
         Depth32::srv.reset();

         Normals0::tex.reset();
         Normals0::uav.reset();
         Normals0::srv.reset();
         
         Main0::tex.reset();
         Main0::uav.reset();
         Main0::srv.reset();
         Main1::tex.reset();
         Main1::uav.reset();
         Main1::srv.reset();

         MainColorDuped::tex.reset();
         MainColorDuped::srv.reset();
      }
   }

   namespace FoundResource
   {
      // if > 0, everything else should have been found and created.
      uint2 size = { 0, 0 };
      bool IsSizeValid() { return size.x > 0 && size.y > 0; }
      uint2 GetSizeHalf() { return { size.x / 2, size.y / 2 }; }
      
      namespace Depth
      {
         ComPtr<ID3D11Resource> res = nullptr;
         ComPtr<ID3D11ShaderResourceView> srv = nullptr; // created from RES
      }

      namespace Color
      {
         ComPtr<ID3D11Resource> res = nullptr;
         ComPtr<ID3D11RenderTargetView> rtv = nullptr; // created from RES, our own RTV
      }

      namespace SceneCB
      {
         ComPtr<ID3D11Buffer> cb = nullptr;
      }

      void Reset()
      {
         size = { 0, 0 };
         
         Depth::res.reset();
         Depth::srv.reset();
         
         Color::res.reset();
         Color::rtv.reset();

         SceneCB::cb.reset();
      }
   }

   namespace ThreadCount
   {
      struct ThreadCount
      {
         int thread_count = -1;
         UINT x = 0;
         UINT x_half = 0;
         UINT y = 0;
         UINT y_half = 0;

         void Update(uint32_t shader_def, bool is_half_size)
         {
            if (ShaderDefineInfo::Get(shader_def) != thread_count) //dirty?
            {
               uint2 size = is_half_size ? FoundResource::GetSizeHalf() : FoundResource::size;
               thread_count = !ShaderDefineInfo::Get(shader_def) ? 8 : 16;
               
               x = (size.x + thread_count - 1) / thread_count;
               x_half = (size.x + (thread_count * 2) - 1) / (thread_count * 2);
               
               y = (size.y + thread_count - 1) / thread_count;
               y_half = (size.y + (thread_count * 2) - 1) / (thread_count * 2);
            }
         }

         UINT GetXEffective(bool is_checkboard) const { return is_checkboard ? x_half : x; }
         UINT GetYEffective(bool is_checkboard) const { return is_checkboard ? y_half : y; }
      };

      ThreadCount normals_gen = {};
      ThreadCount normals_smooth = {};
      ThreadCount main_pass = {};
      ThreadCount denoise_pass = {};

      void Reset()
      {
         normals_gen = {};
         normals_smooth = {};
         main_pass = {};
         denoise_pass = {};
      }
   }

   void HardReset()
   {
      state = Unknown;
      FoundResource::Reset();
      CreatedResource::Reset();
      ThreadCount::Reset();
   }

   void ResetCreatedResource()
   {
      state = Unknown;
      CreatedResource::Reset();
      ThreadCount::Reset();
   }

   bool TrySetFromViews(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, uint32_t ps)
   {
      // update FoundResource?
      if (!FoundResource::IsSizeValid())
      {
         //////////////////////
         // Stage 1: Tonemap //
         //////////////////////
         // failed: still not found
         if (RenderTracker::res_main_color_handle == 0) return false;
         
         ///////////////////////////////////////
         // Stage 2: SSS for color, depth, cb //
         ///////////////////////////////////////
         // gatekeep: not relevant shader
         if (!ShaderHashesLists::IsSSSSetupShader(ps)) return false;
    
         // get DSV and RTV0 from original draw
         ComPtr<ID3D11DepthStencilView> dsv = nullptr;
         ComPtr<ID3D11RenderTargetView> rtv = nullptr;
         native_device_context->OMGetRenderTargets(1, rtv.put(), dsv.put());
         
         // get res from DSV & RTV
         ComPtr<ID3D11Resource> depth_res = nullptr;
         dsv->GetResource(depth_res.put());
         ComPtr<ID3D11Resource> color_res = nullptr;
         rtv->GetResource(color_res.put());

         // failed: color_res != FoundResource::correct_main_color_res_handle (i.e. X Song Pack HQ Mirrored World Reflections)
         if (reinterpret_cast<uint64_t>(color_res.get()) != RenderTracker::res_main_color_handle) return false;

         // Depth
         ASSERT_MSG(depth_res.get() == RenderTracker::res_main_depth.get(), "XeGTAO::TrySetFromViews() depth_res != ResourceTracker::res_main_depth"); // validate with RenderTracker
         FoundResource::Depth::res.attach(depth_res.detach());
         {
            // create our own SRV from RES
            D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
            srv_desc.Format = DXGI_FORMAT_R32_FLOAT; // view is D32_FLOAT, res is R32_TYPELESS
            srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            srv_desc.Texture2D.MipLevels = 1;
            
            auto hr0 = native_device->CreateShaderResourceView(FoundResource::Depth::res.get(), &srv_desc, FoundResource::Depth::srv.put());
            ASSERT_MSG(SUCCEEDED(hr0), "FoundResource Depth hr0");
         }

         // Color
         FoundResource::Color::res.attach(color_res.detach());
         {
            // create our own RTV from RES
            D3D11_RENDER_TARGET_VIEW_DESC rtv_desc = {};
            rtv_desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
            rtv_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
            auto hr0 = native_device->CreateRenderTargetView(FoundResource::Color::res.get(), &rtv_desc, FoundResource::Color::rtv.put());
            ASSERT_MSG(SUCCEEDED(hr0), "FoundResource Color hr0");

            // query for size (because game is 16:9 regardless of swapchain unless modded...)
            D3D11_TEXTURE2D_DESC tex_desc = {};
            ComPtr<ID3D11Texture2D> tex = nullptr;
            auto hr1 = FoundResource::Color::res->QueryInterface(IID_PPV_ARGS(tex.put()));
            ASSERT_MSG(SUCCEEDED(hr1), "FoundResource Color hr1");
            
            tex->GetDesc(&tex_desc);
            FoundResource::size = { tex_desc.Width, tex_desc.Height };
            ASSERT_MSG(FoundResource::IsSizeValid(), "FoundResource size invalid");
         }

         // Scene CB1
         auto previous_cb_handle = FoundResource::SceneCB::cb.get() ? reinterpret_cast<uint64_t>(FoundResource::SceneCB::cb.get()) : 0;
         native_device_context->PSGetConstantBuffers(1, 1, FoundResource::SceneCB::cb.put());
         if (DEVELOPMENT && previous_cb_handle != 0 && previous_cb_handle != reinterpret_cast<uint64_t>(FoundResource::SceneCB::cb.get()))
            ASSERT_MSG(FoundResource::SceneCB::cb.get() != nullptr, "FoundResource SceneCB changed to nullptr");

         // log
         reshade::log::message(reshade::log::level::info, std::format("XeGTAO::TrySetFromViews() FoundResource updated from shader {:08X} with size {}x{}", ps, FoundResource::size.x, FoundResource::size.y).c_str());
      }

      // Create XeGTAO resources
      bool is_size_valid = FoundResource::IsSizeValid();
      if (is_size_valid) CreatedResource::Create(native_device, native_device_context, cmd_list_data, device_data, FoundResource::size, ShaderDefineInfo::GetB(ShaderDefineInfo::XEGTAO_HALFRES));
      
      // success?
      return is_size_valid && CreatedResource::initialized;
   }

   bool TryDraw(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, uint32_t ps, int main_color_index)
   {
      // get bound main color RES from original draw
      ComPtr<ID3D11Resource> main_color_res = nullptr;
      uint64_t main_color_res_handle = 0;
      if (main_color_index >= 0)
      {
         // SRV
         ComPtr<ID3D11ShaderResourceView> main_color_srv = nullptr;
         native_device_context->PSGetShaderResources(main_color_index, 1, main_color_srv.put());
         ASSERT_MSG(main_color_srv != nullptr, "XeGTAO::TryDraw() main_color_srv is nullptr");
         main_color_srv->GetResource(main_color_res.put());
         main_color_res_handle = reinterpret_cast<uint64_t>(main_color_res.get());
      }
      else
      {
         // RTV 0
         ComPtr<ID3D11RenderTargetView> main_color_rtv = nullptr;
         native_device_context->OMGetRenderTargets(1, main_color_rtv.put(), nullptr);
         ASSERT_MSG(main_color_rtv != nullptr, "XeGTAO::TryDraw() main_color_rtv is nullptr");
         main_color_rtv->GetResource(main_color_res.put());
         main_color_res_handle = reinterpret_cast<uint64_t>(main_color_res.get());
      }

      // failed: bound main color RES != FoundResource::Color::res (i.e. X Song Pack HQ Mirrored World Reflections)
      if (main_color_res_handle != reinterpret_cast<uint64_t>(FoundResource::Color::res.get())) return false;
      
      // Thread counts setup
      int checkerboard_mode = ShaderDefineInfo::GetB(ShaderDefineInfo::XEGTAO_CHECKBOARD);
      UINT thread_x_effective;
      UINT thread_y_effective;

      int denoise_count_effective = denoise_count;
      constexpr std::array<int, 3> denoise_count_effective_table = { 0, 1, 3 };
      if (checkerboard_mode)
      {
         denoise_count = std::clamp(denoise_count, 0, 2);
         denoise_count_effective = denoise_count_effective_table[denoise_count];
      }

      // half res setup
      bool is_half_res = ShaderDefineInfo::GetB(ShaderDefineInfo::XEGTAO_HALFRES);
      // Back up draw 
      DrawStateStack<DrawStateStackType::SimpleGraphics> dss;
      dss.Cache(native_device_context, 0);
      
      // unbind OM RTV0 and DSV, avoid conflict
      if (main_color_index < 0)
      {
         constexpr ID3D11RenderTargetView* null_rtv = nullptr;
         constexpr ID3D11DepthStencilView* null_dsv = nullptr;
         native_device_context->OMSetRenderTargets(1, &null_rtv, null_dsv);
         constexpr ID3D11DepthStencilState* null_dss = nullptr;
         native_device_context->OMSetDepthStencilState(null_dss, 0);
      }

      // Bind samplers
      const std::array<ID3D11SamplerState*, 2> samplers = { device_data.sampler_state_point.get(), device_data.sampler_state_linear.get() };
      native_device_context->CSSetSamplers(0, samplers.size(), samplers.data());

      // CB bind
      native_device_context->CSSetConstantBuffers(1, 1, &FoundResource::SceneCB::cb);
      SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, reshade::api::shader_stage::compute, LumaConstantBufferType::LumaSettings);
      
      // PreFilterDepth bind and draw
      const std::array<ID3D11UnorderedAccessView*, DEPTH_MIP_LEVELS + 1> uavs_depth = {
         CreatedResource::Depth32::uav.get(),
         CreatedResource::PreFilteredDepth::uavs[0],
         CreatedResource::PreFilteredDepth::uavs[1],
         CreatedResource::PreFilteredDepth::uavs[2],
         CreatedResource::PreFilteredDepth::uavs[3],
         CreatedResource::PreFilteredDepth::uavs[4]
      };
      native_device_context->CSSetUnorderedAccessViews(0, uavs_depth.size(), uavs_depth.data(), nullptr); //out: prefiltered depth mips
      native_device_context->CSSetShader(device_data.native_compute_shaders.at(CompileTimeStringHash(Luma_XeGTAO_Prefilter)).get(), nullptr, 0);
      native_device_context->CSSetShaderResources(0, 1, &FoundResource::Depth::srv); //in: depth
      // native_device_context->Dispatch((FoundResource::size.x + 16 - 1) / 16, (FoundResource::size.y + 16 - 1) / 16, 1);
      native_device_context->Dispatch((CreatedResource::PreFilteredDepth::tex_desc.Width + 16 - 1) / 16, (CreatedResource::PreFilteredDepth::tex_desc.Height + 16 - 1) / 16, 1);
      
      // Unbind PreFilteredDepth UAVs
      constexpr std::array<ID3D11UnorderedAccessView*, DEPTH_MIP_LEVELS + 1> null_uavs_depth = { };
      native_device_context->CSSetUnorderedAccessViews(0, null_uavs_depth.size(), null_uavs_depth.data(), nullptr);
      
      // NormalGenerate bind and draw
      native_device_context->CSSetUnorderedAccessViews(0, 1, &CreatedResource::Normals0::uav, nullptr); //out: normals
      native_device_context->CSSetShader(device_data.native_compute_shaders.at(CompileTimeStringHash(Luma_XeGTAO_NormalGenerate)).get(), nullptr, 0);
      const std::array<ID3D11ShaderResourceView*, 2> srvs_normals_gen = { CreatedResource::Depth32::srv.get(), CreatedResource::PreFilteredDepth::srv.get() }; //in: depth, prefiltered depth
      native_device_context->CSSetShaderResources(0, srvs_normals_gen.size(), srvs_normals_gen.data()); //in: depth
      {
         ThreadCount::normals_gen.Update(ShaderDefineInfo::XEGTAO_THREADS_NORMALSGEN, is_half_res);
         thread_x_effective = ThreadCount::normals_gen.GetXEffective(checkerboard_mode >= 1);
         thread_y_effective = ThreadCount::normals_gen.GetYEffective(checkerboard_mode >= 2);
      }
      native_device_context->Dispatch(thread_x_effective, thread_y_effective, 1);
      ID3D11ShaderResourceView* normals_srv_effective = CreatedResource::Normals0::srv.get();
      
      if (!debug_skipsmooth)
      {
         // NormalsSmooth 1 bind and draw
         {
            ThreadCount::normals_smooth.Update(ShaderDefineInfo::XEGTAO_THREADS_NORMALSSMOOTH, is_half_res);
            thread_x_effective = ThreadCount::normals_smooth.GetXEffective(checkerboard_mode >= 1);
            thread_y_effective = ThreadCount::normals_smooth.GetYEffective(checkerboard_mode >= 2);
         }
         native_device_context->CSSetUnorderedAccessViews(0, 1, &CreatedResource::Normals1::uav, nullptr); //out: normals smoothed 1
         native_device_context->CSSetShader(device_data.native_compute_shaders.at(CompileTimeStringHash(Luma_XeGTAO_NormalSmooth1)).get(), nullptr, 0);
         const std::array<ID3D11ShaderResourceView*, 3> srvs_normals_smooth_1 = { CreatedResource::Depth32::srv.get(), CreatedResource::PreFilteredDepth::srv.get(), CreatedResource::Normals0::srv.get() }; //in: prefiltered depth, generated normals
         native_device_context->CSSetShaderResources(0, srvs_normals_smooth_1.size(), srvs_normals_smooth_1.data());
         native_device_context->Dispatch(thread_x_effective, thread_y_effective, 1);
         normals_srv_effective = CreatedResource::Normals1::srv.get();
         
         // NormalsSmooth 2 bind and draw
         if (ShaderDefineInfo::Get(ShaderDefineInfo::XEGTAO_NORMALSMOOTH_QUALITY) > 0)
         {
            native_device_context->CSSetUnorderedAccessViews(0, 1, &CreatedResource::Normals0::uav, nullptr); //out: normals smoothed 2 (will be used in main pass)
            native_device_context->CSSetShader(device_data.native_compute_shaders.at(CompileTimeStringHash(Luma_XeGTAO_NormalSmooth2)).get(), nullptr, 0);
            const std::array<ID3D11ShaderResourceView*, 3> srvs_normals_smooth_2 = { CreatedResource::Depth32::srv.get(), CreatedResource::PreFilteredDepth::srv.get(), CreatedResource::Normals1::srv.get() }; //in: prefiltered depth, normals smoothed 1
            native_device_context->CSSetShaderResources(0, srvs_normals_smooth_2.size(), srvs_normals_smooth_2.data());
            native_device_context->Dispatch(thread_x_effective, thread_y_effective, 1);
            normals_srv_effective = CreatedResource::Normals0::srv.get();
         }
      }

      // XeGTAO Main Pass bind and draw
      native_device_context->CSSetUnorderedAccessViews(0, 1, &CreatedResource::Main0::uav, nullptr); //out: AO term and edges
      native_device_context->CSSetShader(device_data.native_compute_shaders.at(!is_fog_dodge ? CompileTimeStringHash(Luma_XeGTAO_MainPass) : CompileTimeStringHash(Luma_XeGTAO_MainPassFog)).get(), nullptr, 0);
      const std::array<ID3D11ShaderResourceView*, 4> srvs_main_pass = { CreatedResource::Depth32::srv.get(), CreatedResource::PreFilteredDepth::srv.get(), FoundResource::Depth::srv.get(), normals_srv_effective }; //in: depth, generated normals
      native_device_context->CSSetShaderResources(0, srvs_main_pass.size(), srvs_main_pass.data());
      {
         ThreadCount::main_pass.Update(ShaderDefineInfo::XEGTAO_THREADS_AO, is_half_res);
         thread_x_effective = ThreadCount::main_pass.GetXEffective(checkerboard_mode >= 1);
         thread_y_effective = ThreadCount::main_pass.GetYEffective(checkerboard_mode >= 2);
      }
      native_device_context->Dispatch(thread_x_effective, thread_y_effective, 1);
      
      // Denoise bind and draw loop
      bool ao_flipflop = false;
      {
         ThreadCount::denoise_pass.Update(ShaderDefineInfo::XEGTAO_THREADS_DENOISE, is_half_res);
         thread_x_effective = ThreadCount::denoise_pass.x_half;
         thread_y_effective = ThreadCount::denoise_pass.y;
      }
      for (int i = 0; i < denoise_count_effective; i++)
      {
         // flipflop
         ID3D11ShaderResourceView*  ao_in  = !ao_flipflop ? CreatedResource::Main0::srv.get() : CreatedResource::Main1::srv.get();
         ID3D11UnorderedAccessView* ao_out = !ao_flipflop ? CreatedResource::Main1::uav.get() : CreatedResource::Main0::uav.get();
         ao_flipflop = !ao_flipflop;
         
         // final?
         auto cs = i < denoise_count_effective - 1 ? CompileTimeStringHash(Luma_XeGTAO_DenoisePass1) : CompileTimeStringHash(Luma_XeGTAO_DenoisePass2);

         // bind & draw
         native_device_context->CSSetUnorderedAccessViews(0, 1, &ao_out, nullptr); //out: denoised
         native_device_context->CSSetShader(device_data.native_compute_shaders.at(cs).get(), nullptr, 0);
         native_device_context->CSSetShaderResources(0, 1, &ao_in); //in: AO term and edges
         native_device_context->Dispatch(thread_x_effective, thread_y_effective,1); // half width, but cs does 2 pixels
      }
      ID3D11ShaderResourceView* ao_srv = !ao_flipflop ? CreatedResource::Main0::srv.get() : CreatedResource::Main1::srv.get();
      
      // Unbind CS
      constexpr std::array<ID3D11UnorderedAccessView*, 1> null_uavs = { };
      native_device_context->CSSetUnorderedAccessViews(0, null_uavs.size(), null_uavs.data(), nullptr);
      
      constexpr std::array<ID3D11ShaderResourceView*, 4> null_srvs = { };
      native_device_context->CSSetShaderResources(0, null_srvs.size(), null_srvs.data());
      
      constexpr ID3D11Buffer* null_1cb = nullptr;
      native_device_context->CSSetConstantBuffers(1, 1, &null_1cb);
      native_device_context->CSSetConstantBuffers(luma_data_cbuffer_index, 1, &null_1cb);
      
      constexpr ID3D11ComputeShader* null_cs = nullptr;
      native_device_context->CSSetShader(null_cs, nullptr, 0);
      
      constexpr std::array<ID3D11SamplerState*, 2> null_2samplers = { };
      native_device_context->CSSetSamplers(0, null_2samplers.size(), null_2samplers.data());
      
      // CopyResource() to MainColorDuped (has to be, since original main color is not UAV-able)
      native_device_context->CopyResource(CreatedResource::MainColorDuped::tex.get(), FoundResource::Color::res.get());
      
      // Apply XeGTAO to main color RTV0 bind and draw (will also be cleaned up by dss)
      {
         // debug views
         constexpr std::array<uint32_t, 4> ps_hashes = {
            CompileTimeStringHash(Luma_XeGTAO_Apply),
            CompileTimeStringHash(Luma_XeGTAO_ApplyDbgAO),
            CompileTimeStringHash(Luma_XeGTAO_ApplyDbgNormals),
            CompileTimeStringHash(Luma_XeGTAO_ApplyDbgDepth)
         };
         uint32_t ps_hash = ps_hashes[std::clamp(static_cast<uint32_t>(debug_out), 0u, static_cast<uint32_t>(ps_hashes.size() - 1))];
         const std::array<ID3D11ShaderResourceView*, 5> ps_srvs = { CreatedResource::MainColorDuped::srv.get(), ao_srv, FoundResource::Depth::srv.get(), CreatedResource::PreFilteredDepth::srv.get(), normals_srv_effective };

         // save index 3+
         ASSERT_ONCE_MSG(dss.srv_num == 3, "WTH! Is DrawStateStackType::SimpleGraphics srv_num != 3?!?!");
         ASSERT_ONCE_MSG(dss.samplers_num == 1, "WTH! Is DrawStateStackType::SimpleGraphics samplers_num != 1?!?!");
         std::array<ID3D11ShaderResourceView*, 2> ps_srvs_saved = { };
         native_device_context->PSGetShaderResources(3, ps_srvs_saved.size(), ps_srvs_saved.data()); // index 3 & 4
         
         const auto vs = device_data.native_vertex_shaders.find(Math::CompileTimeStringHash("Copy VS"));
         ASSERT_MSG(vs != device_data.native_vertex_shaders.end() && vs->second.get(), "XeGTAO TryDraw() failed to find Copy VS");
         ID3D11DepthStencilState* depth_stencil_state = nullptr;
         ID3D11BlendState* blend_state = nullptr;
         constexpr FLOAT blend_factor[4] = { 1.f, 1.f, 1.f, 0.f };
         native_device_context->OMSetBlendState(blend_state, blend_factor, 0xFFFFFFFF);
         native_device_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
         native_device_context->RSSetScissorRects(0, nullptr);
         D3D11_VIEWPORT viewport;
         viewport.TopLeftX = 0;
         viewport.TopLeftY = 0;
         viewport.Width = FoundResource::size.x;
         viewport.Height = FoundResource::size.y;
         viewport.MinDepth = 0;
         viewport.MaxDepth = 1;
         native_device_context->RSSetViewports(1, &viewport);
         native_device_context->PSSetShaderResources(0, ps_srvs.size(), ps_srvs.data());
         native_device_context->OMSetDepthStencilState(depth_stencil_state, 0);
         // native_device_context->PSSetSamplers(0, samplers.size(), samplers.data()); // DrawStateStackType::SimpleGraphics only saves 1, so just be safe and only use 1
            native_device_context->PSSetSamplers(0, 1, &device_data.sampler_state_point);
         native_device_context->OMSetRenderTargets(1, &FoundResource::Color::rtv, nullptr);
         native_device_context->VSSetShader(vs->second.get(), nullptr, 0);
         native_device_context->PSSetShader(device_data.native_pixel_shaders.at(ps_hash).get(), nullptr, 0);
         native_device_context->IASetInputLayout(nullptr);
         native_device_context->RSSetState(nullptr);
         native_device_context->PSSetConstantBuffers(1, 1, &FoundResource::SceneCB::cb);
         SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, reshade::api::shader_stage::pixel, LumaConstantBufferType::LumaSettings);
         native_device_context->Draw(4, 0);

         // restore index 3+ (rest is by dss.Restore())
         native_device_context->PSSetShaderResources(3, ps_srvs_saved.size(), ps_srvs_saved.data());
         for (auto& srv : ps_srvs_saved) if (srv) { srv->Release(); srv = nullptr; }
      }
      
      // restore draw state
      dss.Restore(native_device_context, true, true);

      return true;
   }

   DrawOrDispatchOverrideType OnDrawOrDispatchOverride(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, uint32_t ps)
   {
      // gatekeep: not enabled
      if (!enabled) return DrawOrDispatchOverrideType::None;
      if (disallow_for_npr && !TonemapInfo::GetIsPBRFromIndex(TonemapInfo::GetIndex(TonemapInfo::tonemapinfo_prev))) return DrawOrDispatchOverrideType::None;
      if (DEVELOPMENT && !IsModEnabled()) return DrawOrDispatchOverrideType::None;

      /*
       * Super irrelevant shaders that doesn't use depth, or uses some other depth (e.g. shadows)
       * Skin SSS shader precompute: RTV 0 main color, DSV depth
       * Skin SSS shader final resolve (0x54415551): no info
       * 2 Clear Resources for SSS (i think SSS)
       * Depth Write lighting resolve: RTV 0 main color, DSV depth write
       * 0xF94D4A4A: some blit before transparency
       * Depth Read lighting resolve: RTV 0 main color, DSV depth read
       * Depth of Field
       * Downsample for Bloom & Auto Exposure
       */
      switch (state)
      {
         [[unlikely]] case Unknown:
         {
            // try set/saving original resources
            if (TrySetFromViews(native_device, native_device_context, cmd_list_data, device_data, ps))
               state = Ready; //next state

            break; 
         }
         case Ready:
         {
            // failed: RenderTracker too early
            if (!RenderTracker::IsMainColorBeingOrHasBeenDrawnTo()) break;
            
            // failed: debug only allows dof or downsample shaders
            if (debug_late && ps != 0x043F4B65 && ps != 0x68722F15) break;
            
            // failed: no relevant_shaders_to_main_color_srv
            auto i = relevant_shaders_to_main_color_srv.find(ps);
            if (i == relevant_shaders_to_main_color_srv.end()) break;
            int main_color_index = i->second;
            
            // try drawing XeGTAO
            if (TryDraw(native_device, native_device_context, cmd_list_data, device_data, ps, main_color_index))
               state = Done;
            
            break;
         }
         case Done:
         default:
            break;
      }

      return DrawOrDispatchOverrideType::None;
   }

   void OnPresent()
   {
      // reset state (if not unknown)
      if (state != Unknown) state = Ready;
   }

   void OnLoad(reshade::api::effect_runtime* runtime)
   {
      reshade::get_config_value(runtime, NAME, reshadesave_enabled, enabled);
      reshade::get_config_value(runtime, NAME, reshadesave_disallow_for_npr, disallow_for_npr);
      reshade::get_config_value(runtime, NAME, reshadesave_denoise, denoise_count);
   }

   void OnInitSwapchain(bool is_resolution_changed)
   {
      if (is_resolution_changed) HardReset();
   }
}

namespace SSS
{
   constexpr const char* reshadesave_enabled = "SSSEnabled";
   bool enabled = false;

   enum State : uint8_t
   {
      Unknown, // start
      Setup,  // Setup skin surface buffer 0x93881580 drawn
      SubsequentSetups,
      PBR_Downsample,
      PBR_Widen,
      NPR_Contour,
      NPR_Downsample,
      NPR_SSSPrep,
      SSS,
      Done, // done for this frame
   };
   State state = Unknown; // denotes which shader has drawn prev

   uint32_t curr_pv_id = 0;

   constexpr const char* Luma_MegaMix_SSS = "Luma_MegaMix_SSS"; // file name
   constexpr const char* Luma_MegaMix_PreSSS = "Luma_MegaMix_PreSSS"; // shader name
   constexpr const char* Luma_MegaMix_SSS0 = "Luma_MegaMix_SSS0"; // shader name

   namespace Resources
   {
      struct Item
      {
         uint64_t original_res_handle; // original full res setup RES
         uint64_t replacement_res_handle = 0; // SRV16 to replace
         uint2 size = { 0, 0 };
         bool IsOriginalMainColor() const { return original_res_handle == RenderTracker::res_main_color_handle; }

         // orig (our own created to original)
         ComPtr<ID3D11ShaderResourceView> original_srv;
         ComPtr<ID3D11RenderTargetView> original_rtv;

         // new
         ComPtr<ID3D11ShaderResourceView> new_draw_srv;
         ComPtr<ID3D11RenderTargetView> new_draw_rtv;
         
         ComPtr<ID3D11ShaderResourceView> new_aux_srv;
         ComPtr<ID3D11RenderTargetView> new_aux_rtv;
         
         ComPtr<ID3D11ShaderResourceView> new_use_srv;
         ComPtr<ID3D11RenderTargetView> new_use_rtv;
         
         bool IsValid() const { return original_res_handle > 0; }
         void Reset()
         {
            size = { 0, 0 };
            original_res_handle = 0; replacement_res_handle = 0;
            original_srv.reset(); original_rtv.reset();
            new_draw_srv.reset(); new_draw_rtv.reset();
            new_aux_srv.reset(); new_aux_rtv.reset();
            new_use_srv.reset(); new_use_rtv.reset();
         }
      };

      std::array<Item, 2> items = { };
      int GetItemCount() { return std::count_if(items.begin(), items.end(), [](const Item& item) { return item.IsValid(); }); }

      Item* TryGetItemForOriginal(uint64_t original_res_handle)
      {
         for (Item& item : items)
         {
            if (item.original_res_handle == original_res_handle) return &item;
         }
         return nullptr;
      }

      Item* TryGetItemForReplace(uint64_t replacement_res_handle)
      {
         for (Item& item : items)
         {
            ASSERT_MSG(!item.IsValid() || item.replacement_res_handle > 0, "SSS::Resources::TryGetItemForReplace() IsValid() but replacement_res_handle == 0");
            if (item.IsValid() && item.replacement_res_handle == replacement_res_handle) return &item;
         }
         return nullptr;
      }

      Item* TryGetEmptyItem()
      {
         for (Item& item : items)
         {
            if (!item.IsValid()) return &item;
         }
         return nullptr;
      }

      void Reset(bool avoid_main_color = false)
      {
         for (Item& item : items)
         {
            if (avoid_main_color && item.IsOriginalMainColor()) continue;
            item.Reset();
         }
      }
   }

   // return only Skip or None (continues normal exec)
   DrawOrDispatchOverrideType OnDrawOrDispatchOverride(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, uint32_t ps)
   {
      /*
         PBR
         0x93881580 & 0xCF8047E0: geo setup
         0x26AF16B8: downsample
         0xF2254A92: edges wider
         0x54415551: sss0

         NPR
         0x93881580 & 0xCF8047E0: geo setup (with edge outline precompute)
         0x8E7027B5: npr contour1 resolve (slightly unrelated)
         0x26AF16B8: downsample
         0x086EEB5C: geo setup resolved to sss0 usable
         0x54415551: sss0
       */
      if (!enabled) return DrawOrDispatchOverrideType::None;
      if (DEVELOPMENT && !IsModEnabled()) return DrawOrDispatchOverrideType::None;

      // if SSS setup shader, force state (aka sss_skina_ps & sss_skinas_ps)
      if (ShaderHashesLists::IsSSSSetupShader(ps) && state != SubsequentSetups) state = Setup;

      static Resources::Item* current_item = nullptr; // acts like token
      static int replace_remaining = 0; // acts like token
      
      switch (state) // alert: denotes what has drawn prev
      {
         case Unknown: return DrawOrDispatchOverrideType::None;
         case Setup:
         {            
            // get RTV0
            ComPtr<ID3D11RenderTargetView> rtv0;
            ComPtr<ID3D11DepthStencilView> dsv0;
            native_device_context->OMGetRenderTargets(1, rtv0.put(), dsv0.put());
            ASSERT_MSG(rtv0 != nullptr, "SSS::OnDrawOrDispatchOverride() rtv0 is nullptr");
            ASSERT_MSG(dsv0 != nullptr, "SSS::OnDrawOrDispatchOverride() dsv0 is nullptr");
            
            // init item (only once per set)
            if (!current_item) 
            {
               // get RES from RTV0
               ComPtr<ID3D11Resource> res0;
               rtv0->GetResource(res0.put());
         
               // RES handle
               auto res_handle = reinterpret_cast<uint64_t>(res0.get());
         
               // get item
               current_item = Resources::TryGetItemForOriginal(res_handle);
         
               // create new
               [[unlikely]] if (!current_item)
               {
                  // query TEX
                  ComPtr<ID3D11Texture2D> tex0;
                  auto hr0a = res0->QueryInterface(tex0.put());
                  ASSERT_MSG(SUCCEEDED(hr0a), "SSS::OnDrawOrDispatchOverride() res0->QueryInterface(tex0) failed");
         
                  // get DESC for size
                  D3D11_TEXTURE2D_DESC tex_desc = {};
                  tex0->GetDesc(&tex_desc);
                  uint2 size = { tex_desc.Width, tex_desc.Height };
                  
                  // create item
                  current_item = Resources::TryGetEmptyItem();
                  ASSERT_MSG(current_item != nullptr, "SSS::Resources::CreateItem() no empty item slot");

                  // tex setup
                  current_item->size = size;
                  ComPtr<ID3D11Texture2D> replacement_tex;
                  tex_desc.Width = current_item->size.x;
                  tex_desc.Height = current_item->size.y;
                  tex_desc.ArraySize = 1;
                  tex_desc.SampleDesc.Count = 1;
                  tex_desc.SampleDesc.Quality = 0;
                  tex_desc.Usage = D3D11_USAGE_DEFAULT;
                  tex_desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
                  tex_desc.CPUAccessFlags = 0;

                  // new draw
                  tex_desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT; // SSS setup can tint, so not grayscale, requiring RGB (and A is strength)
                  tex_desc.MipLevels = 4; // if we dont, then we need a Blit shader to copy original draw RTV before GenerateMips()
                  tex_desc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
                  auto hr0aa = native_device->CreateTexture2D(&tex_desc, nullptr, replacement_tex.put());
                  ASSERT_MSG(SUCCEEDED(hr0aa), "SSS::Resources::Item::CreateReplacement() hr0aa");
                  auto hr0bb = native_device->CreateShaderResourceView(replacement_tex.get(), nullptr, current_item->new_draw_srv.put());
                  ASSERT_MSG(SUCCEEDED(hr0bb), "SSS::Resources::Item::CreateReplacement() hr0bb");
                  auto hr0cc = native_device->CreateRenderTargetView(replacement_tex.get(), nullptr, current_item->new_draw_rtv.put());
                  ASSERT_MSG(SUCCEEDED(hr0cc), "SSS::Resources::Item::CreateReplacement() hr0cc");

                  // new use
                  tex_desc.Format = DXGI_FORMAT_R11G11B10_FLOAT; // alpha is ignored by forward lighting shaders
                  tex_desc.MipLevels = 1;
                  tex_desc.MiscFlags = 0;
                  auto hr3 = native_device->CreateTexture2D(&tex_desc, nullptr, replacement_tex.put());
                  ASSERT_MSG(SUCCEEDED(hr3), "SSS::Resources::Item::CreateReplacement() hr3");
                  auto hr4 = native_device->CreateShaderResourceView(replacement_tex.get(), nullptr, current_item->new_use_srv.put());
                  ASSERT_MSG(SUCCEEDED(hr4), "SSS::Resources::Item::CreateReplacement() hr4");
                  auto hr5 = native_device->CreateRenderTargetView(replacement_tex.get(), nullptr, current_item->new_use_rtv.put());
                  ASSERT_MSG(SUCCEEDED(hr5), "SSS::Resources::Item::CreateReplacement() hr5");

                  // save original
                  current_item->original_res_handle = res_handle;
                  auto hr6 = native_device->CreateShaderResourceView(res0.get(), nullptr, current_item->original_srv.put());
                  ASSERT_MSG(SUCCEEDED(hr6), "SSS::Resources::Item::CreateReplacement() hr6");
                  auto hr7 = native_device->CreateRenderTargetView(res0.get(), nullptr, current_item->original_rtv.put());
                  ASSERT_MSG(SUCCEEDED(hr7), "SSS::Resources::Item::CreateReplacement() hr7");
               }
            }
            ASSERT_MSG(current_item, "SSS::OnDrawOrDispatchOverride() Setup current_item is nullptr right after create!?!?");
            ASSERT_ONCE_MSG(RenderTracker::count_main_color_clears_draws_after == 1 || !current_item->IsOriginalMainColor(), "SSS::OnDrawOrDispatchOverride() Setup() main_color_clears_draws_after != 1");

            // clear
            constexpr FLOAT clear_black[4] = { 0.f, 0.f, 0.f, 0.f };
            native_device_context->ClearRenderTargetView(current_item->new_draw_rtv.get(), clear_black);

            // set RTV as ours, for the rest of setup draws
            native_device_context->OMSetRenderTargets(1, &current_item->new_draw_rtv, dsv0.get());
            
            // next
            state = SubsequentSetups;
            return DrawOrDispatchOverrideType::None;
         }
         case SubsequentSetups:
         {
            ASSERT_MSG(current_item, "SSS::OnDrawOrDispatchOverride() Wait current_item is nullptr!");
            
            // verify that game didnt rebind RTV0
            if (DEVELOPMENT && ShaderHashesLists::IsSSSSetupShader(ps))
            {
               ComPtr<ID3D11RenderTargetView> rtv0;
               native_device_context->OMGetRenderTargets(1, rtv0.put(), nullptr);
               ASSERT_MSG(rtv0 != nullptr, "SSS::OnDrawOrDispatchOverride() Wait rtv0 is nullptr");
               ComPtr<ID3D11Resource> res0;
               rtv0->GetResource(res0.put());
               auto res_handle = reinterpret_cast<uint64_t>(res0.get());

               ComPtr<ID3D11Resource> res_new;
               current_item->new_draw_rtv->GetResource(res_new.put());
               auto res_new_handle = reinterpret_cast<uint64_t>(res_new.get());
               
               ASSERT_MSG(res_handle == res_new_handle, "SSS::OnDrawOrDispatchOverride() Wait rtv0 res_handle != new_draw_rtv res_handle");
            }
            
            // PBR_Downsample
            if (ps == 0x26AF16B8) // "sprite simple"
            {
               native_device_context->GenerateMips(current_item->new_draw_srv.get()); // generate mips as downsample
               
               state = PBR_Downsample; // next
               return DrawOrDispatchOverrideType::Skip; // skip downsample
            }

            // NPR_Contour (legit unused by vanilla game. only like 2 PVs from Megpoid has this.)
            if (ps == 0x8E7027B5) // "nprcontour1" 
            {
               // create new aux
               [[unlikely]] if (!current_item->new_aux_srv)
               {
                  D3D11_TEXTURE2D_DESC tex_desc = {};
                  ComPtr<ID3D11Texture2D> replacement_tex;
                  tex_desc.Width = current_item->size.x;
                  tex_desc.Height = current_item->size.y;
                  tex_desc.ArraySize = 1;
                  tex_desc.SampleDesc.Count = 1;
                  tex_desc.SampleDesc.Quality = 0;
                  tex_desc.Usage = D3D11_USAGE_DEFAULT;
                  tex_desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
                  tex_desc.CPUAccessFlags = 0;

                  tex_desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT; // SSS setup can tint (not grayscale) requiring RGB
                  tex_desc.MipLevels = 4;
                  tex_desc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
                  auto hr0 = native_device->CreateTexture2D(&tex_desc, nullptr, replacement_tex.put());
                  ASSERT_MSG(SUCCEEDED(hr0), "SSS::Resources::Item::CreateReplacement() hr0");
                  auto hr1 = native_device->CreateShaderResourceView(replacement_tex.get(), nullptr, current_item->new_aux_srv.put());
                  ASSERT_MSG(SUCCEEDED(hr1), "SSS::Resources::Item::CreateReplacement() hr1");
                  auto hr2 = native_device->CreateRenderTargetView(replacement_tex.get(), nullptr, current_item->new_aux_rtv.put());
                  ASSERT_MSG(SUCCEEDED(hr2), "SSS::Resources::Item::CreateReplacement() hr2");
               }

               // set SRV0 as new draw
               native_device_context->PSSetShaderResources(0, 1, &current_item->new_draw_srv);
               
               state = NPR_Contour; // next
               return DrawOrDispatchOverrideType::None; // allow this draw to continue
            }
            
            return DrawOrDispatchOverrideType::None;
         }
         case PBR_Downsample:
         {
            // PBR_Widen
            ASSERT_MSG(ps == 0xF2254A92, "SSS::OnDrawOrDispatchOverride() PBR_Downsample unexpected ps"); // this can false positive once when PV starts
            state = PBR_Widen; // next
            return DrawOrDispatchOverrideType::Skip; // skip, completely useless to expand SSS surfaces at native res
         }
         case NPR_Contour:
         {
            // NPR_Downsample
            ASSERT_MSG(ps == 0x26AF16B8, "SSS::OnDrawOrDispatchOverride() NPR_Edges unexpected ps");
            state = NPR_Downsample;
            return DrawOrDispatchOverrideType::Skip;
         }
         case NPR_Downsample:
         {
            // NPR_SSSPrep
            ASSERT_MSG(ps == 0x086EEB5C, "SSS::OnDrawOrDispatchOverride() NPR_Downsample unexpected ps");
            
            native_device_context->PSSetShader(device_data.native_pixel_shaders.at(CompileTimeStringHash(Luma_MegaMix_PreSSS)).get(), nullptr, 0);
            native_device_context->PSSetShaderResources(0, 1, &current_item->new_draw_srv); // SRV0 full res
            native_device_context->OMSetRenderTargets(1, &current_item->new_aux_rtv, nullptr); // RTV0 new res

            // viewport full
            D3D11_VIEWPORT viewport = {0, 0, static_cast<FLOAT>(current_item->size.x), static_cast<FLOAT>(current_item->size.y), 0.f, 1.f};
            native_device_context->RSSetViewports(1, &viewport);

            native_device_context->Draw(4, 0); // draw
            native_device_context->GenerateMips(current_item->new_aux_srv.get()); // generate mips as downsample
               
            state = NPR_SSSPrep;
            return DrawOrDispatchOverrideType::Replaced;
         }
         case NPR_SSSPrep: 
         case PBR_Widen:
         {
            // SSS resolve
            ASSERT_MSG(ps == 0x54415551, "SSS::OnDrawOrDispatchOverride() NPR_SSSPrep/PBR_Widen unexpected ps");
            
            ASSERT_MSG(current_item, "SSS::OnDrawOrDispatchOverride() PBR_Widen current_item is nullptr");
            bool is_pbr = state == PBR_Widen;

            // save RTV0 RES handle for replacement
            ComPtr<ID3D11RenderTargetView> rtv0 = nullptr;
            native_device_context->OMGetRenderTargets(1, rtv0.put(), nullptr);
            ASSERT_MSG(rtv0 != nullptr, "SSS::OnDrawOrDispatchOverride() PBR_Widen rtv0 is nullptr");
            
            ComPtr<ID3D11Resource> replacement_res = nullptr;
            rtv0->GetResource(replacement_res.put());
            current_item->replacement_res_handle = reinterpret_cast<uint64_t>(replacement_res.get()); 

            // set SRV0 & RTV0 as full res
            native_device_context->PSSetShaderResources(0, 1, is_pbr ? &current_item->new_draw_srv : &current_item->new_aux_srv); // NPR uses aux from pre-sss pass, PBR uses original
            native_device_context->OMSetRenderTargets(1, &current_item->new_use_rtv, nullptr);
            
            // replace SSS0 with our own (only if main color, aka no X Song Pack waste)
            if (current_item->IsOriginalMainColor()) native_device_context->PSSetShader(device_data.native_pixel_shaders.at(CompileTimeStringHash(Luma_MegaMix_SSS0)).get(), nullptr, 0);

            // viewport full
            D3D11_VIEWPORT viewport = {0, 0, static_cast<FLOAT>(current_item->size.x), static_cast<FLOAT>(current_item->size.y), 0.f, 1.f};
            native_device_context->RSSetViewports(1, &viewport);

            // next state
            current_item = nullptr;                        // consume
            replace_remaining = Resources::GetItemCount(); // rearm
            state = SSS;
            return DrawOrDispatchOverrideType::None;
         }
         case SSS:
         {
            // done?
            if (replace_remaining == 0 || TonemapInfo::GetIsDrawnTonemapOrFinal(cb_luma_global_settings.GameSettings.TonemapInfo))
            {
               state = Done;
               break;
            }

            // get SRV16
            ComPtr<ID3D11ShaderResourceView> srv16;
            native_device_context->PSGetShaderResources(16, 1, srv16.put());

            // skip: null SRV16
            if (!srv16) break;

            // get RES from SRV16
            ComPtr<ID3D11Resource> res16;
            srv16->GetResource(res16.put());

            // replace?
            Resources::Item* item = Resources::TryGetItemForReplace(reinterpret_cast<uint64_t>(res16.get()));
            if (item)
            {
               // set SRV16 as our new
               native_device_context->PSSetShaderResources(16, 1, &item->new_use_srv);

               // eye dodge
               if (/*cb_luma_global_settings.GameSettings.SSSEyeDodge > 0.f */ ShaderDefineInfo::GetB(ShaderDefineInfo::CUSTOM_SSS_EYEDODGE))
               {
                  // check SRV32 is null
                  if (DEVELOPMENT) // should be... only up to SRV20 is used
                  {
                     ComPtr<ID3D11ShaderResourceView> srv32;
                     native_device_context->PSGetShaderResources(32, 1, srv32.put());
                     ASSERT_MSG(!srv32, "SSS::OnDrawOrDispatchOverride() SSS SRV32 is not null.");
                  }
                  // set SRV32 as setup
                  native_device_context->PSSetShaderResources(32, 1, &item->new_draw_srv);
               }
               
               replace_remaining--; // use
            }

            // TODO: X Song Pack guarantees "swapchain final" shader to draw for HQ mirrored world reflections before switching sides
            
            return DrawOrDispatchOverrideType::None;
         }
         case Done:
         default:
            return DrawOrDispatchOverrideType::None;
      }
      
      return DrawOrDispatchOverrideType::None;
   }

   void OnInit()
   {
      native_shaders_definitions.emplace(CompileTimeStringHash(Luma_MegaMix_PreSSS), ShaderDefinition{ Luma_MegaMix_SSS, reshade::api::pipeline_subobject_type::pixel_shader, nullptr, "pre_sss", {}});
      native_shaders_definitions.emplace(CompileTimeStringHash(Luma_MegaMix_SSS0),   ShaderDefinition{ Luma_MegaMix_SSS, reshade::api::pipeline_subobject_type::pixel_shader, nullptr, "sss0", {}});
   }
   
   void OnLoad(reshade::api::effect_runtime* runtime)
   {
      reshade::get_config_value(runtime, NAME, reshadesave_enabled, enabled);
   }

   void HardReset()
   {
      Resources::Reset();
      state = Unknown;
   }

   void OnPresent()
   {
      if (!enabled) return;

      // reset state
      state = Unknown;
   }
}

namespace Bloom
{
   constexpr const char* reshadesave_enabled = "BloomEnabled";
   bool enabled = false;

   constexpr const char* reshadesave_sigma = "BloomSigma";
   constexpr float sigma_def = 0.10f;
   float sigma = sigma_def;

   constexpr const char* reshadesave_sigma_increase = "BloomSigmaIncrease";
   constexpr float sigma_increase_def = 0.36f;
   float sigma_increase = sigma_increase_def;

   constexpr const char* reshadesave_use_highest_mip = "BloomUseHighestMip";
   constexpr bool use_highest_mip_def = false;
   bool use_highest_mip = use_highest_mip_def;

   PUBLISHING_CONSTEXPR bool is_vanilla_bloom_blur_rtv_hq = true;

   namespace Shaders
   {
      // file name
      constexpr const char* Luma_MegaMix_Bloom = "Luma_MegaMix_Bloom"; 

      // shader names
      constexpr const char* Bloom_VS = "Bloom VS";
      constexpr const char* Bloom_DownsampleBeforePrefilter_PS = "Bloom Downsample Before Prefilter PS";
      constexpr const char* Bloom_Prefilter_PS = "Bloom Prefilter PS";
      constexpr const char* Bloom_Downsample_PS = "Bloom Downsample PS";
      constexpr const char* Bloom_Upsample_PS = "Bloom Upsample PS";
      constexpr const char* Bloom_Combine_PS = "Bloom Combine PS";
      
      constexpr const char* Bloom_Blur0_VS = "Bloom Blur0 VS";
      constexpr const char* Bloom_Blur0_PS = "Bloom Blur0 PS";

      void OnInit()
      {
         native_shaders_definitions.emplace(CompileTimeStringHash(Bloom_VS), ShaderDefinition{ Luma_MegaMix_Bloom, reshade::api::pipeline_subobject_type::vertex_shader, nullptr, "bloom_main_vs", {} });
         native_shaders_definitions.emplace(CompileTimeStringHash(Bloom_Prefilter_PS), ShaderDefinition{ Luma_MegaMix_Bloom, reshade::api::pipeline_subobject_type::pixel_shader, nullptr, "bloom_prefilter_ps", {} });
         native_shaders_definitions.emplace(CompileTimeStringHash(Bloom_Downsample_PS), ShaderDefinition{ Luma_MegaMix_Bloom, reshade::api::pipeline_subobject_type::pixel_shader, nullptr, "bloom_downsample_ps", {} });
         native_shaders_definitions.emplace(CompileTimeStringHash(Bloom_Upsample_PS), ShaderDefinition{ Luma_MegaMix_Bloom, reshade::api::pipeline_subobject_type::pixel_shader, nullptr, "bloom_upsample_ps", {} });
         native_shaders_definitions.emplace(CompileTimeStringHash(Bloom_DownsampleBeforePrefilter_PS), ShaderDefinition(Luma_MegaMix_Bloom, reshade::api::pipeline_subobject_type::pixel_shader, nullptr, "bloom_downsamplebeforeprefilter_ps"));
         native_shaders_definitions.emplace(CompileTimeStringHash(Bloom_Combine_PS), ShaderDefinition(Luma_MegaMix_Bloom, reshade::api::pipeline_subobject_type::pixel_shader, nullptr, "bloom_combine_ps"));

         native_shaders_definitions.emplace(CompileTimeStringHash(Bloom_Blur0_VS), ShaderDefinition(Luma_MegaMix_Bloom, reshade::api::pipeline_subobject_type::vertex_shader, nullptr, "bloom_blur0_vs"));
         native_shaders_definitions.emplace(CompileTimeStringHash(Bloom_Blur0_PS), ShaderDefinition(Luma_MegaMix_Bloom, reshade::api::pipeline_subobject_type::pixel_shader, nullptr, "bloom_blur0_ps"));
      }

      struct alignas(16) CBMegaMixBloom
      {
         float2 src_size;
         float2 inv_src_size;
         float2 axis;
         float sigma;
      };
      ComPtr<ID3D11Buffer> bloom_cb;

      ComPtr<ID3D11BlendState> bloom_blend_state;
   }
   
   enum State : uint8_t
   {
      Downsample0, // 0x68722F15 (higher res = more downsampling passes)
      Downsample1, // 0x41C419EE
      // Downsample2, // 0x68722F15
      // Downsample3, // 0x68722F15
      // Downsample4, // 0x68722F15
      // Downsample5, // 0x543E9A5B
      BloomDown0, // 0x7B4E4533
      BloomDown1, // 0x7B4E4533
      BloomDown2, // 0x7B4E4533
      BloomDown3, // 0x7B4E4533
      BloomDown4, // 0x7B4E4533
      BloomDown5, // 0x7B4E4533
      BloomBlur0, // 0x466D68A8
      BloomCombine, // 0xCD83E95E
      // AutoExposure0, // 0xA58C1868
      // AutoExposure1, // 0xDF1AC023
      Tonemap,
      Done,
   };
   State state = Downsample0; // denotes which shader is being drawn next.
   
   namespace Resources
   {
      int nmips = 0;
      
      ComPtr<ID3D11ShaderResourceView> orig_full_srv = nullptr;

      std::vector<ID3D11RenderTargetView*> rtv_mips_x(8); // width is one level higher than height
      std::vector<ID3D11ShaderResourceView*> srv_mips_x(8);
      
      std::vector<ID3D11RenderTargetView*> rtv_mips_y(8); // also used as bloom upsample outputs
      std::vector<ID3D11ShaderResourceView*> srv_mips_y(8);

      std::vector<ID3D11RenderTargetView*> rtv_mips_y1(8); // used to replace blurring
      std::vector<ID3D11ShaderResourceView*> srv_mips_y1(8);

      std::vector<D3D11_VIEWPORT> rtv_mips_y_viewports(8); // TODO: this needs copied to in-scope instance to successfully set 

      // bool IsValid() { return rtv_mips_x[0]; }

      void ResetArrays()
      {
         ResetCOMArray(rtv_mips_x);
         ResetCOMArray(srv_mips_x);
         ResetCOMArray(rtv_mips_y);
         ResetCOMArray(srv_mips_y);
         ResetCOMArray(rtv_mips_y1);
         ResetCOMArray(srv_mips_y1);
      }

      void HardReset()
      {
         ResetArrays();
         nmips = 0;
      }
   }

   // None allows continuing execution
   DrawOrDispatchOverrideType OnDrawOrDispatchOverride(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, uint32_t ps)
   {
      if (!enabled) return DrawOrDispatchOverrideType::None;
      if (DEVELOPMENT && !IsModEnabled()) return DrawOrDispatchOverrideType::None;
      
      switch (state)
      {
         case Downsample0:
         {
            // SRV0 is full res
            // RTV0 is 0.5x downsampled

            // wait until shader
            if (ps != 0x68722F15) break;
            
            // SRV0 orig_full_srv
            native_device_context->PSGetShaderResources(0, 1, Resources::orig_full_srv.put());
            
            state = Downsample1;
            break;
         }
         case Downsample1:
         {
            // SRV0 is downsampled
            // RTV0 is forced 255x144
            
            // wait until shader
            if (ps != 0x41C419EE) break;
            
            // Draw Bloom
            {
               // Backup TODO: del unneeded backups
               D3D11_PRIMITIVE_TOPOLOGY primitive_topology_original;
               native_device_context->IAGetPrimitiveTopology(&primitive_topology_original);

               ComPtr<ID3D11VertexShader> vs_original;
               native_device_context->VSGetShader(vs_original.put(), nullptr, nullptr);

               ComPtr<ID3D11PixelShader> ps_original;
               native_device_context->PSGetShader(ps_original.put(), nullptr, nullptr);

               ComPtr<ID3D11Buffer> cb_orginal;
               native_device_context->PSGetConstantBuffers(11, 1, cb_orginal.put());

               ComPtr<ID3D11SamplerState> ps_sampler_original;
               native_device_context->PSGetSamplers(0, 1, ps_sampler_original.put());

               ComPtr<ID3D11ShaderResourceView> ps_srv_original;
               native_device_context->PSGetShaderResources(0, 1, ps_srv_original.put());

               UINT num_viewports = 1;
               std::array<D3D11_VIEWPORT, 1> viewports_original;
               native_device_context->RSGetViewports(&num_viewports, viewports_original.data());

               ComPtr<ID3D11RasterizerState> rasterizer_original;
               native_device_context->RSGetState(rasterizer_original.put());

               ComPtr<ID3D11BlendState> blend_original;
               FLOAT blend_factor_original[4];
               UINT sample_mask_original;
               native_device_context->OMGetBlendState(blend_original.put(), blend_factor_original, &sample_mask_original);

               std::array<ID3D11RenderTargetView*, 1> rtvs_original = {};
               ComPtr<ID3D11DepthStencilView> dsv_original;
               native_device_context->OMGetRenderTargets(rtvs_original.size(), rtvs_original.data(), dsv_original.put());
               
               // Get the scene resource and texture description from the SRV.
               static UINT scene_width   = 0;
               static UINT scene_height  = 0;
               static UINT x_mip0_width  = 0;
               static UINT x_mip0_height = 0;
               static UINT y_mip0_width  = 0;
               static UINT y_mip0_height = 0;
                  
               [[unlikely]] if (Resources::nmips == 0)
               {
                  // Setup
                  D3D11_TEXTURE2D_DESC tex_desc;
                  ComPtr<ID3D11Texture2D> tex;
                  ComPtr<ID3D11Texture2D> tex1;
                  ComPtr<ID3D11Resource> resource;
                  
                  Resources::orig_full_srv->GetResource(resource.put());
                  auto hr = resource->QueryInterface(tex.put());
                  ASSERT_MSG(SUCCEEDED(hr), "Bloom resource->QueryInterface(tex) failed");
                  tex->GetDesc(&tex_desc);

                  // size calc (TODO: merge with below, but honestly whatever)
                  scene_width  = tex_desc.Width;
                  scene_height = tex_desc.Height;
               
                  x_mip0_width  = tex_desc.Width / 2;
                  x_mip0_height = tex_desc.Height;
               
                  y_mip0_width  = tex_desc.Width / 2;
                  y_mip0_height = tex_desc.Height / 2;
                  
                  // while loop to find when x <= 32 and resize nmips
                  Resources::nmips = 0;
                  {
                     uint w = y_mip0_width;
                     while (w > 33)
                     {
                        w /= 2;
                        Resources::nmips++;
                     }
                     Resources::nmips = std::clamp(Resources::nmips, 4, static_cast<int>(Resources::rtv_mips_x.size()));
                  }
                  Resources::ResetArrays();
                  
                  // Create Y MIPs and views.
                  tex_desc.Width = y_mip0_width;
                  tex_desc.Height = y_mip0_height;
                  tex_desc.MipLevels = Resources::nmips;
                  tex_desc.Format = /*DXGI_FORMAT_R16G16B16A16_FLOAT*/ DXGI_FORMAT_R11G11B10_FLOAT;
                  tex_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
                  auto hr0 = native_device->CreateTexture2D(&tex_desc, nullptr, tex.put());
                  ASSERT_MSG(SUCCEEDED(hr0), "Bloom hr0");
                  auto hr0a = native_device->CreateTexture2D(&tex_desc, nullptr, tex1.put());
                  ASSERT_MSG(SUCCEEDED(hr0a), "Bloom hr0a");
            
                  D3D11_RENDER_TARGET_VIEW_DESC rtv_desc = {};
                  rtv_desc.Format = tex_desc.Format;
                  rtv_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
            
                  D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
                  srv_desc.Format = tex_desc.Format;
                  srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                  srv_desc.Texture2D.MipLevels = 1;

                  for (int i = 0; i < Resources::nmips; ++i)
                  {
                     rtv_desc.Texture2D.MipSlice = i;
                     auto hr1 = native_device->CreateRenderTargetView(tex.get(), &rtv_desc, &Resources::rtv_mips_y[i]);
                     ASSERT_MSG(SUCCEEDED(hr1), "Bloom hr1");
                     auto hr1a = native_device->CreateRenderTargetView(tex1.get(), &rtv_desc, &Resources::rtv_mips_y1[i]);
                     ASSERT_MSG(SUCCEEDED(hr1a), "Bloom hr1a");
                     
                     srv_desc.Texture2D.MostDetailedMip = i;
                     auto hr2 = native_device->CreateShaderResourceView(tex.get(), &srv_desc, &Resources::srv_mips_y[i]);
                     ASSERT_MSG(SUCCEEDED(hr2), "Bloom hr2");
                     auto hr2a = native_device->CreateShaderResourceView(tex1.get(), &srv_desc, &Resources::srv_mips_y1[i]);
                     ASSERT_MSG(SUCCEEDED(hr2a), "Bloom hr2a");

                     Resources::rtv_mips_y_viewports[i].TopLeftX = 0.f;
                     Resources::rtv_mips_y_viewports[i].TopLeftY = 0.f;
                     Resources::rtv_mips_y_viewports[i].Width  = y_mip0_width  >> i;
                     Resources::rtv_mips_y_viewports[i].Height = y_mip0_height >> i;
                     Resources::rtv_mips_y_viewports[i].MinDepth = 0.f;
                     Resources::rtv_mips_y_viewports[i].MaxDepth = 1.f;
                  }
                  
                  // Create X MIP0 and views.
                  tex_desc.Width = x_mip0_width;
                  tex_desc.Height = x_mip0_height;
                  tex_desc.MipLevels = 1;
                  auto hr6 = native_device->CreateTexture2D(&tex_desc, nullptr, tex.put());
                  ASSERT_MSG(SUCCEEDED(hr6), "Bloom hr6");
                  auto hr7 = native_device->CreateRenderTargetView(tex.get(), nullptr, &Resources::rtv_mips_x[0]);
                  ASSERT_MSG(SUCCEEDED(hr7), "Bloom hr7");
                  auto hr8 = native_device->CreateShaderResourceView(tex.get(), nullptr, &Resources::srv_mips_x[0]);
                  ASSERT_MSG(SUCCEEDED(hr8), "Bloom hr8");
            
                  // Create rest of X MIPs and views.
                  for (UINT i = 1; i < Resources::nmips; ++i)
                  {
                     tex_desc.Width = max(1u, x_mip0_width >> i);
                     tex_desc.Height = max(1u, x_mip0_height >> i);
                     auto hr9 = native_device->CreateTexture2D(&tex_desc, nullptr, tex.put());
                     ASSERT_MSG(SUCCEEDED(hr9), "Bloom hr9");
                     auto hr10 = native_device->CreateRenderTargetView(tex.get(), nullptr, &Resources::rtv_mips_x[i]);
                     ASSERT_MSG(SUCCEEDED(hr10), "Bloom hr10");
                     auto hr11 = native_device->CreateShaderResourceView(tex.get(), nullptr, &Resources::srv_mips_x[i]);
                     ASSERT_MSG(SUCCEEDED(hr11), "Bloom hr11");
                  }

                  reshade::log::message(reshade::log::level::info, std::format("Bloom: created textures and views for {} mips", Resources::nmips).c_str());
               }

               //
               // Create bloom CB.
               //

               [[unlikely]] if (!Shaders::bloom_cb.get())
               {
                  D3D11_BUFFER_DESC buffer_desc = {};
                  buffer_desc.ByteWidth = sizeof(Shaders::CBMegaMixBloom);
                  buffer_desc.Usage = D3D11_USAGE_DYNAMIC;
                  buffer_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
                  buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
                  auto hr = native_device->CreateBuffer(&buffer_desc, nullptr, Shaders::bloom_cb.put());
                  ASSERT_MSG(SUCCEEDED(hr), "Bloom hr bloom_cb");
                  reshade::log::message(reshade::log::level::info, "Bloom: created constant buffer");
               }
            
               CBLumaBloomData cb_data;
            
               auto update_constant_buffer = [&]()
               {
                  D3D11_MAPPED_SUBRESOURCE mapped_subresource;
                  auto hr = native_device_context->Map(Shaders::bloom_cb.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_subresource);
                  ASSERT_MSG(SUCCEEDED(hr), "Bloom hr bloom_cb map");
                  std::memcpy(mapped_subresource.pData, &cb_data, sizeof(CBLumaBloomData));
                  native_device_context->Unmap(Shaders::bloom_cb.get(), 0);
               };
            
               //
               // Prefilter + downsample pass
               //
            
               // X
               D3D11_VIEWPORT viewport_x = {};
               viewport_x.Width = x_mip0_width;
               viewport_x.Height = x_mip0_height;
               
               cb_data.src_size = float2(scene_width, scene_height);
               cb_data.inv_src_size = float2(1.0f / cb_data.src_size.x, 1.0f / cb_data.src_size.y);
               cb_data.axis = float2(1.0f, 0.0f);
               cb_data.sigma = sigma;
               update_constant_buffer();
               
               native_device_context->OMSetRenderTargets(1, &Resources::rtv_mips_x[0], nullptr);
               native_device_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
               native_device_context->VSSetShader(device_data.native_vertex_shaders.at(CompileTimeStringHash(Shaders::Bloom_VS)).get(), nullptr, 0);
               native_device_context->PSSetShader(device_data.native_pixel_shaders.at(CompileTimeStringHash(Shaders::Bloom_DownsampleBeforePrefilter_PS)).get(), nullptr, 0);
               native_device_context->PSSetConstantBuffers(11, 1, &Shaders::bloom_cb);
               const std::array ps_samplers = { device_data.sampler_state_linear.get() };
               native_device_context->PSSetSamplers(0, ps_samplers.size(), ps_samplers.data());
               native_device_context->PSSetShaderResources(0, 1, &Resources::orig_full_srv);
               native_device_context->RSSetViewports(1, &viewport_x);
               native_device_context->RSSetState(nullptr);
            
               native_device_context->Draw(3, 0);

               // Y
               std::vector<D3D11_VIEWPORT> viewports_y(Resources::nmips);
               viewports_y[0].Width = y_mip0_width;
               viewports_y[0].Height = y_mip0_height;
            
               cb_data.src_size = float2(x_mip0_width, x_mip0_height);
               cb_data.inv_src_size = float2(1.0f / cb_data.src_size.x, 1.0f / cb_data.src_size.y);
               cb_data.axis = float2(0.0f, 1.0f);
               update_constant_buffer();
            
               native_device_context->OMSetRenderTargets(1, &Resources::rtv_mips_y[0], nullptr);
               native_device_context->PSSetShader(device_data.native_pixel_shaders.at(CompileTimeStringHash(Shaders::Bloom_Prefilter_PS)).get(), nullptr, 0);
               native_device_context->PSSetShaderResources(0, 1, &Resources::srv_mips_x[0]);
               native_device_context->RSSetViewports(1, &viewports_y[0]);
            
               native_device_context->Draw(3, 0);
            
               //
               // Downsample passes
               //
            
               // Render downsample passes.
               native_device_context->PSSetShader(device_data.native_pixel_shaders.at(CompileTimeStringHash(Shaders::Bloom_Downsample_PS)).get(), nullptr, 0);
               for (UINT i = 1; i < Resources::nmips; i++)
               {
                  // X
                  viewport_x.Width = max(1u, x_mip0_width >> i);
                  viewport_x.Height = max(1u, x_mip0_height >> i);
            
                  cb_data.src_size = float2(viewports_y[i - 1].Width, viewports_y[i - 1].Height);
                  cb_data.axis = float2(1.0f, 0.0f);
                  cb_data.inv_src_size = float2(1.0f / cb_data.src_size.x, 1.0f / cb_data.src_size.y);
                  cb_data.sigma += sigma_increase;
                  update_constant_buffer();
            
                  native_device_context->OMSetRenderTargets(1, &Resources::rtv_mips_x[i], nullptr);
                  native_device_context->PSSetShaderResources(0, 1, &Resources::srv_mips_y[i - 1]);
                  native_device_context->RSSetViewports(1, &viewport_x);
                  
                  native_device_context->Draw(3, 0);

                  // Y
                  viewports_y[i].Width = max(1u, y_mip0_width >> i);
                  viewports_y[i].Height = max(1u, y_mip0_height >> i);
            
                  cb_data.src_size = float2(viewport_x.Width, viewport_x.Height);
                  cb_data.axis = float2(0.0f, 1.0f);
                  cb_data.inv_src_size = float2(1.0f / cb_data.src_size.x, 1.0f / cb_data.src_size.y);
                  update_constant_buffer();
            
                  native_device_context->OMSetRenderTargets(1, &Resources::rtv_mips_y[i], nullptr);
                  native_device_context->PSSetShaderResources(0, 1, &Resources::srv_mips_x[i]);
                  native_device_context->RSSetViewports(1, &viewports_y[i]);
            
                  native_device_context->Draw(3, 0);
               }
            
               //
               // Upsample passes
               //

               // blend
               [[unlikely]] if (!Shaders::bloom_blend_state)
               {
                  CD3D11_BLEND_DESC blend_desc(D3D11_DEFAULT);
                  blend_desc.RenderTarget[0].BlendEnable = TRUE;
                  blend_desc.RenderTarget[0].SrcBlend = D3D11_BLEND_BLEND_FACTOR;
                  blend_desc.RenderTarget[0].DestBlend = D3D11_BLEND_BLEND_FACTOR;
                  ensure(native_device->CreateBlendState(&blend_desc, Shaders::bloom_blend_state.put()), >= 0);
               }

               // upsample 4 mips up
               uint8_t budget = 4;
               native_device_context->PSSetShader(device_data.native_pixel_shaders.at(CompileTimeStringHash(Shaders::Bloom_Upsample_PS)).get(), nullptr, 0);
               for (int i = Resources::nmips - 1; i > 0; i--)
               {
                  // do only necessary
                  if (budget == 0) break; //TODO: use i
                  budget--;
                  
                  // If both dst and src are D3D10_BLEND_BLEND_FACTOR
                  // factor of 0.5 will be energy preserving.
                  static constexpr FLOAT blend_factor[] = { 0.5f, 0.5f, 0.5f, 0.0f };
               
                  // Update CB.
                  cb_data.src_size = float2(viewports_y[i].Width, viewports_y[i].Height);
                  cb_data.inv_src_size = float2(1.0f / cb_data.src_size.x, 1.0f / cb_data.src_size.y);
                  update_constant_buffer();
               
                  native_device_context->OMSetRenderTargets(1, budget == 1 ? &Resources::rtv_mips_y1[i - 1] : &Resources::rtv_mips_y[i - 1], nullptr); // #WeirdSwap
                  native_device_context->PSSetShaderResources(0, 1, &Resources::srv_mips_y[i]);
                  native_device_context->RSSetViewports(1, &viewports_y[i - 1]);
                  native_device_context->OMSetBlendState(Shaders::bloom_blend_state.get(), blend_factor, UINT_MAX);
               
                  native_device_context->Draw(3, 0);
               }
               
               // // Return the final bloom.
               // *srv_bloom = Resources::srv_mips_y[0];
               // (*srv_bloom)->AddRef();
            
               // Restore.
               native_device_context->OMSetBlendState(blend_original.get(), blend_factor_original, sample_mask_original);
               native_device_context->OMSetRenderTargets(rtvs_original.size(), rtvs_original.data(), dsv_original.get());
               native_device_context->IASetPrimitiveTopology(primitive_topology_original);
               native_device_context->VSSetShader(vs_original.get(), nullptr, 0);
               native_device_context->PSSetShader(ps_original.get(), nullptr, 0);
               native_device_context->PSSetConstantBuffers(11, 1, &cb_orginal);
               native_device_context->PSSetSamplers(0, 1, &ps_sampler_original);
               native_device_context->PSSetShaderResources(0, 1, &ps_srv_original);
               native_device_context->RSSetViewports(viewports_original.size(), viewports_original.data());
               native_device_context->RSSetState(rasterizer_original.get());
            
               // Release com arrays.
               ResetCOMArray(rtvs_original);
            
               // auto reset_mips = [&]()
               // {
               //     ResetCOMArray(Resources::rtv_mips_x);
               //     ResetCOMArray(Resources::srv_mips_x);
               //     ResetCOMArray(Resources::rtv_mips_y);
               //     ResetCOMArray(Resources::srv_mips_y);
               // };
               //
               // LumaCallbacks::on_destroy_device.try_emplace("luma_bloom"_h, reset_mips);
               // LumaCallbacks::on_init_swapchain.try_emplace("luma_bloom"_h, reset_mips);
            }
            
            state = BloomDown0;
            break;
         }
         // (Auto Exposure downsample is independent from our new bloom)
         case BloomDown0: 
         {
            // wait until shader
            if (ps != 0x7B4E4533) break;

            // SRV0 set to 3rd last mip
            native_device_context->PSSetShaderResources(0, 1, &Resources::srv_mips_y[Resources::nmips - 3]);

            if (is_vanilla_bloom_blur_rtv_hq)
            {
               // RTV0 set to 3rd last mip
               native_device_context->OMSetRenderTargets(1, &Resources::rtv_mips_y1[Resources::nmips - 3], nullptr);
            
               // viewport
               D3D11_VIEWPORT viewport = Resources::rtv_mips_y_viewports[Resources::nmips - 3];
               native_device_context->RSSetViewports(1, &viewport);
            }

            state = BloomDown1;
            break;
         }
         case BloomDown1:
         {
            // wait until shader
            if (ps != 0x7B4E4533) break;

            // SRV0 set to 2nd last mip
            native_device_context->PSSetShaderResources(0, 1, &Resources::srv_mips_y[Resources::nmips - 2]);

            if (is_vanilla_bloom_blur_rtv_hq)
            {
               // RTV0 set to 2nd last mip
               native_device_context->OMSetRenderTargets(1, &Resources::rtv_mips_y1[Resources::nmips - 2], nullptr);
            
               // viewport
               D3D11_VIEWPORT viewport = Resources::rtv_mips_y_viewports[Resources::nmips - 2];
               native_device_context->RSSetViewports(1, &viewport);
            }

            state = BloomDown2;
            break;
         }
         case BloomDown2:
         {
            // wait until shader
            if (ps != 0x7B4E4533) break;
            
            // SRV0 set to last mip
            native_device_context->PSSetShaderResources(0, 1, &Resources::srv_mips_y[Resources::nmips - 1]);

            if (is_vanilla_bloom_blur_rtv_hq)
            {
               // RTV0 set to last mip
               native_device_context->OMSetRenderTargets(1, &Resources::rtv_mips_y1[Resources::nmips - 1], nullptr);
            
               // viewport
               D3D11_VIEWPORT viewport = Resources::rtv_mips_y_viewports[Resources::nmips - 1];
               native_device_context->RSSetViewports(1, &viewport);
            }
            
            state = BloomDown3;
            break;
         }
         case BloomDown3:
         {
            // wait until shader
            if (ps != 0x7B4E4533) break;

            if (is_vanilla_bloom_blur_rtv_hq)
            {
               // SRV0 set to 3rd last mip (flipped)
               native_device_context->PSSetShaderResources(0, 1, &Resources::srv_mips_y1[Resources::nmips - 3]);
            
               // RTV0 set to 3rd last mip (flipped)
               native_device_context->OMSetRenderTargets(1, &Resources::rtv_mips_y[Resources::nmips - 3], nullptr);
            
               // viewport
               D3D11_VIEWPORT viewport = Resources::rtv_mips_y_viewports[Resources::nmips - 3];
               native_device_context->RSSetViewports(1, &viewport);
            }
            
            state = BloomDown4;
            break;
         }
         case BloomDown4:
         {
            // wait until shader
            if (ps != 0x7B4E4533) break;

            if (is_vanilla_bloom_blur_rtv_hq)
            {
               // SRV0 set to 2nd last mip (flipped)
               native_device_context->PSSetShaderResources(0, 1, &Resources::srv_mips_y1[Resources::nmips - 2]);
            
               // RTV0 set to 2nd last mip (flipped)
               native_device_context->OMSetRenderTargets(1, &Resources::rtv_mips_y[Resources::nmips - 2], nullptr);
            
               // viewport
               D3D11_VIEWPORT viewport = Resources::rtv_mips_y_viewports[Resources::nmips - 2];
               native_device_context->RSSetViewports(1, &viewport);
            }

            state = BloomDown5;
            break;
         }
         case BloomDown5:
         {
            // wait until shader
            if (ps != 0x7B4E4533) break;

            if (is_vanilla_bloom_blur_rtv_hq)
            {
               // SRV0 set to last mip (flipped)
               native_device_context->PSSetShaderResources(0, 1, &Resources::srv_mips_y1[Resources::nmips - 1]);
            
               // RTV0 set to last mip (flipped)
               native_device_context->OMSetRenderTargets(1, &Resources::rtv_mips_y[Resources::nmips - 1], nullptr);
            
               // viewport
               D3D11_VIEWPORT viewport = Resources::rtv_mips_y_viewports[Resources::nmips - 1];
               native_device_context->RSSetViewports(1, &viewport);
            }

            state = BloomBlur0;
            break;
         }
         case BloomBlur0:
         {
            // wait until shader
            if (ps != 0x466D68A8) break;

            // SRV0 set to 4rd last mip
            native_device_context->PSSetShaderResources(0, 1, &Resources::srv_mips_y1[Resources::nmips - 4]); // #WeirdSwap

            if (is_vanilla_bloom_blur_rtv_hq)
            {
               // RTV0 set to 4rd last mip
               native_device_context->OMSetRenderTargets(1, &Resources::rtv_mips_y[Resources::nmips - 4], nullptr);
            
               // viewport
               D3D11_VIEWPORT viewport = Resources::rtv_mips_y_viewports[Resources::nmips - 4];
               native_device_context->RSSetViewports(1, &viewport);
            }
            
            // Set VS PS
            native_device_context->VSSetShader(device_data.native_vertex_shaders.at(CompileTimeStringHash(Shaders::Bloom_Blur0_VS)).get(), nullptr, 0);
            native_device_context->PSSetShader(device_data.native_pixel_shaders.at(CompileTimeStringHash(Shaders::Bloom_Blur0_PS)).get(), nullptr, 0);

            state = BloomCombine;
            break;
         }
         case BloomCombine:
         {
            // skip until shader
            if (ps != 0xCD83E95E) return DrawOrDispatchOverrideType::Skip;

            if (is_vanilla_bloom_blur_rtv_hq)
            {
               // SRV 0-3 are last 4 mip levels of bloom, descending order (0 is largest, 3 is smallest)
               const std::array<ID3D11ShaderResourceView*, 4> bloom_srvs = { Resources::srv_mips_y[Resources::nmips - 4], Resources::srv_mips_y[Resources::nmips - 3], Resources::srv_mips_y[Resources::nmips - 2], Resources::srv_mips_y[Resources::nmips - 1] };
               native_device_context->PSSetShaderResources(0, bloom_srvs.size(), bloom_srvs.data());
            }

            // set RTV0 as our buffer
            int mip = use_highest_mip ? 0 : 1;
            native_device_context->OMSetRenderTargets(1, &Resources::rtv_mips_y1[mip], nullptr);

            // set our combine shader
            native_device_context->PSSetShader(device_data.native_pixel_shaders.at(CompileTimeStringHash(Shaders::Bloom_Combine_PS)).get(), nullptr, 0);
            
            // viewport to size of mip0
            D3D11_VIEWPORT viewport = Resources::rtv_mips_y_viewports[mip];
            native_device_context->RSSetViewports(1, &viewport);
            
            state = Tonemap;
            break;
         }
         case Tonemap:
         case Done:
            break;
      }

      return DrawOrDispatchOverrideType::None;
   }

   void OnTonemapDraw(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data)
   {
      if (state != Tonemap) return;

      // set SRV1 as out new bloom output
      int mip = use_highest_mip ? 0 : 1;
      native_device_context->PSSetShaderResources(1, 1, &Resources::srv_mips_y1[mip]);
      
      state = Done;
   }

   void OnInit()
   {
      Shaders::OnInit();
   }

   void OnPresent()
   {
      state = Downsample0;
   }

   void HardReset()
   {
      Resources::HardReset();
   }

   void OnLoad(reshade::api::effect_runtime* runtime)
   {
      reshade::get_config_value(runtime, NAME, reshadesave_enabled, enabled);
      reshade::get_config_value(runtime, NAME, reshadesave_sigma, sigma);
      reshade::get_config_value(runtime, NAME, reshadesave_sigma_increase, sigma_increase);
      reshade::get_config_value(runtime, NAME, reshadesave_use_highest_mip, use_highest_mip);
   }
}

namespace SpotLightShadows
{
   bool enabled = false;
   constexpr const char* reshadesave_enabled = "SpotLightShadowsEnabled";
   
   enum State : uint8_t
   {
      DepthFinalize0, // 0xC1A00F28
      DepthFinalize1, // 0xC1A00F28
      Resolving, // 0x1CE07171 (writes DVS / opaque), 0x03C8D536 (reads DSV / transparent)
      ResolvingAtLeastOnce,
      TonemapUse, // Tonemap variants
      Done,
   };
   State state; // denotes next shader to be drawn

   namespace Resources
   {
      uint64_t depth_res_handle = 0;

      ComPtr<ID3D11ShaderResourceView> newcolor_srv = nullptr;
      ComPtr<ID3D11RenderTargetView> newcolor_rtv = nullptr;

      ComPtr<ID3D11DepthStencilView> newdsv = nullptr;

      void Reset()
      {
         depth_res_handle = 0;
         newcolor_srv.reset();
         newcolor_rtv.reset();
      }
   }

   void OnDrawOrDispatchOverride(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, uint32_t ps)
   {
      if (!enabled) return;
      if (DEVELOPMENT && !IsModEnabled()) return;
      
      [[unlikely]] if (!Resources::newcolor_srv && RenderTracker::res_main_color_size.x > 0)
      {
         // setup
         ComPtr<ID3D11Texture2D> new_texture;
         D3D11_TEXTURE2D_DESC tex_desc;
         tex_desc.Width  = RenderTracker::res_main_color_size.x;
         tex_desc.Height = RenderTracker::res_main_color_size.y;
         tex_desc.MipLevels = 1;
         tex_desc.ArraySize = 1;
         tex_desc.SampleDesc.Count = 1;
         tex_desc.SampleDesc.Quality = 0;
         tex_desc.Usage = D3D11_USAGE_DEFAULT;
         tex_desc.CPUAccessFlags = 0;
         tex_desc.MiscFlags = 0;

         // color
         tex_desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
         tex_desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
         auto hr2 = native_device->CreateTexture2D(&tex_desc, nullptr, new_texture.put());
         ASSERT_MSG(SUCCEEDED(hr2), "SpotLightShadows: OnTonemapDraw hr2");

         auto hr3 = native_device->CreateRenderTargetView(new_texture.get(), nullptr, Resources::newcolor_rtv.put());
         ASSERT_MSG(SUCCEEDED(hr3), "SpotLightShadows: OnTonemapDraw hr3");

         auto hr4 = native_device->CreateShaderResourceView(new_texture.get(), nullptr, Resources::newcolor_srv.put());
         ASSERT_MSG(SUCCEEDED(hr4), "SpotLightShadows: OnTonemapDraw hr4");

         // depth
         tex_desc.Format = DXGI_FORMAT_R32_TYPELESS;
         tex_desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
         auto hr5 = native_device->CreateTexture2D(&tex_desc, nullptr, new_texture.put());
         ASSERT_MSG(SUCCEEDED(hr5), "SpotLightShadows: OnTonemapDraw hr5");

         D3D11_DEPTH_STENCIL_VIEW_DESC dsv_desc = {};
         dsv_desc.Format = DXGI_FORMAT_D32_FLOAT;
         dsv_desc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
         auto hr6 = native_device->CreateDepthStencilView(new_texture.get(), &dsv_desc, Resources::newdsv.put());
         ASSERT_MSG(SUCCEEDED(hr6), "SpotLightShadows: OnTonemapDraw hr6");
         
         reshade::log::message(reshade::log::level::info, "SpotLightShadows: created new color buffer and views");
      }

      switch (state)
      {
         case DepthFinalize0:
         {
            // wait until shader
            if (ps != 0xC1A00F28) break;

            // next state
            state = DepthFinalize1;
            break;
         }
         case DepthFinalize1:
         {
            // wait until shader
            ASSERT_MSG(ps == 0xC1A00F28, "SpotLightShadows: DepthFinalize1 shader not detected next!");

            // RTV0 is precompute depth (will be bound SRV18 for Resolve)
            if (Resources::depth_res_handle == 0)
            {
               ComPtr<ID3D11RenderTargetView> rtv0;
               native_device_context->OMGetRenderTargets(1, rtv0.put(), nullptr);
               ASSERT_MSG(rtv0 != nullptr, "SpotLightShadows: RTV0 is null in DepthFinalize");
               
               ComPtr<ID3D11Resource> rtv0_resource;
               rtv0->GetResource(rtv0_resource.put());
               
               Resources::depth_res_handle = reinterpret_cast<uint64_t>(rtv0_resource.get());
            }

            // next state
            state = Resolving;
            break;
         }
         case Resolving:
         case ResolvingAtLeastOnce:
         {
            // wait until shader
            static std::unordered_set<uint32_t> resolving_shaders = { 0x1CE07171, 0x03C8D536 }; // TODO: use static array (0x1CE07171 writes DVS / is opaque & 0x03C8D536 reads DSV / is transparent)
            if (!resolving_shaders.contains(ps))
            {
               // last hurrah SVR10 == depth_res_handle
               uint64_t srv10_handle;
               {
                  ComPtr<ID3D11ShaderResourceView> srv10;
                  native_device_context->PSGetShaderResources(18, 1, srv10.put());
                  srv10_handle = !srv10 ? 0 : reinterpret_cast<uint64_t>(srv10.get());
               }
               
               if (srv10_handle != Resources::depth_res_handle)
               {
                  if (state == ResolvingAtLeastOnce) state = TonemapUse; // resolved at least once, so we can move on
                  break; // early "return"
               }

               resolving_shaders.insert(ps);
               ASSERT_MSG(false, "SpotLightShadows: New resolving shader!");
               reshade::log::message(reshade::log::level::warning, std::format("SpotLightShadows: New resolving shader! (ps=0x{:X})", ps).c_str());
            }

            // clear (DSV is required, while RTV fullscreen overwrites)
            if (state == Resolving) native_device_context->ClearDepthStencilView(Resources::newdsv.get(), D3D11_CLEAR_DEPTH, 0.0f, 0);

            // replace RTV0 & DSV
            native_device_context->OMSetRenderTargets(1, &Resources::newcolor_rtv, Resources::newdsv.get());

            // viewport full
            D3D11_VIEWPORT viewport;
            viewport.TopLeftX = 0;
            viewport.TopLeftY = 0;
            viewport.Width  = RenderTracker::res_main_color_size.x;
            viewport.Height = RenderTracker::res_main_color_size.y;
            viewport.MinDepth = 0;
            viewport.MaxDepth = 1;
            native_device_context->RSSetViewports(1, &viewport);

            // next state
            state = ResolvingAtLeastOnce;
            break;
         }
         case TonemapUse:
         case Done:
            break;
      }
   }

   void OnTonemapDraw(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data)
   {
      // replace SRV7
      if (state == TonemapUse) native_device_context->PSSetShaderResources(7, 1, &Resources::newcolor_srv);
   }

   void OnPresent()
   {
      state = DepthFinalize0;
   }

   void OnLoad(reshade::api::effect_runtime* runtime)
   {
      reshade::get_config_value(runtime, NAME, reshadesave_enabled, enabled);
   }

   void HardReset()
   {
      Resources::Reset();
      state = DepthFinalize0;
   }
}

namespace AntiAliasing
{
   enum Enabled : uint8_t
   {
      Vanilla,
      Disable,
      DLAA,
      SMAA,
   };
   Enabled enabled = Vanilla;
   constexpr const char* reshadesave_enabled = "AntiAliasingEnabled";

   enum State : uint8_t
   {
      MLAAEdges0, // 0x3ACC6F7A
      MLAAEdges1, // 0x5DA2FE05
      MLAAResolve, // 0x5C5FD160
      
      Done,
   };
   State state; // denotes next shader to be drawn

   constexpr const char* Luma_MegaMix_DLAA = "Luma_MegaMix_DLAA";
   constexpr const char* Luma_MegaMix_DLAA_VS = "Luma_MegaMix_DLAA_VS";
   constexpr const char* Luma_MegaMix_DLAA_PreFilter = "Luma_MegaMix_DLAA_PreFilter";
   constexpr const char* Luma_MegaMix_DLAA_Resolve = "Luma_MegaMix_DLAA_Resolve";

   namespace Resources
   {
      // auxiliary
      ComPtr<ID3D11ShaderResourceView> srv = nullptr;
      ComPtr<ID3D11RenderTargetView> rtv = nullptr;

      // 3D depth
      ComPtr<ID3D11ShaderResourceView> depth_srv = nullptr;

      void Reset()
      {
         srv.reset(); rtv.reset();
         depth_srv.reset();
      }
   }

   DrawOrDispatchOverrideType OnDrawOrDispatchOverride(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, uint32_t ps)
   {
      if (enabled == Vanilla) return DrawOrDispatchOverrideType::None;
      if (DEVELOPMENT && !IsModEnabled()) return DrawOrDispatchOverrideType::None;

      switch (state)
      {
         case MLAAEdges0:
         {
            if (ps == 0x3ACC6F7A)
            {
               state = MLAAEdges1;
               return DrawOrDispatchOverrideType::Skip;
            }
            break;
         }
         case MLAAEdges1:
         {
            if (ps == 0x5DA2FE05)
            {
               state = MLAAResolve;
               return DrawOrDispatchOverrideType::Skip;
            }
            break;
         }
         case MLAAResolve:
         {
            if (ps != 0x5C5FD160) return DrawOrDispatchOverrideType::None;
            state = Done;

            if (enabled == Disable) // Disable
            {
               // CopyResource SRV0 to RTV0
               ComPtr<ID3D11ShaderResourceView> srv0;
               native_device_context->PSGetShaderResources(0, 1, srv0.put());
               ComPtr<ID3D11Resource> srv0_resource;
               srv0->GetResource(srv0_resource.put());
            
               ComPtr<ID3D11RenderTargetView> rtv0;
               native_device_context->OMGetRenderTargets(1, rtv0.put(), nullptr);
               ComPtr<ID3D11Resource> rtv0_resource;
               rtv0->GetResource(rtv0_resource.put());
            
               native_device_context->CopyResource(rtv0_resource.get(), srv0_resource.get());
            }
            else if (enabled == DLAA) // DLAA
            {
               // get SRV0
               ComPtr<ID3D11ShaderResourceView> srv0;
               native_device_context->PSGetShaderResources(0, 1, srv0.put());
            
               // get RTV0
               ComPtr<ID3D11RenderTargetView> rtv0;
               native_device_context->OMGetRenderTargets(1, rtv0.put(), nullptr);
            
               // create Resources if not exist
               [[unlikely]] if (!Resources::srv)
               {
                  // query resolution
                  ComPtr<ID3D11Resource> rtv0_resource;
                  rtv0->GetResource(rtv0_resource.put());
            
                  ComPtr<ID3D11Texture2D> rtv0_texture;
                  auto hr = rtv0_resource->QueryInterface(rtv0_texture.put());
                  ASSERT_MSG(SUCCEEDED(hr), "AntiAliasing: hr");
            
                  D3D11_TEXTURE2D_DESC tex_desc;
                  rtv0_texture->GetDesc(&tex_desc);
                  ASSERT_MSG(tex_desc.Width == RenderTracker::res_main_color_size.x, "AntiAliasing: tex_desc.Width != RenderTracker::res_main_color_size.x");
                  ASSERT_MSG(tex_desc.Height == RenderTracker::res_main_color_size.y, "AntiAliasing: tex_desc.Height != RenderTracker::res_main_color_size.y");
                  
                  // create
                  D3D11_TEXTURE2D_DESC new_tex_desc;
                  new_tex_desc.Width = RenderTracker::res_main_color_size.x;
                  new_tex_desc.Height = RenderTracker::res_main_color_size.y;
                  new_tex_desc.MipLevels = 1;
                  new_tex_desc.ArraySize = 1;
                  new_tex_desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
                  new_tex_desc.SampleDesc.Count = 1;
                  new_tex_desc.SampleDesc.Quality = 0;
                  new_tex_desc.Usage = D3D11_USAGE_DEFAULT;
                  new_tex_desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
                  new_tex_desc.CPUAccessFlags = 0;
                  new_tex_desc.MiscFlags = 0;
               
                  ComPtr<ID3D11Texture2D> new_texture;
                  auto hr0 = native_device->CreateTexture2D(&new_tex_desc, nullptr, new_texture.put());
                  ASSERT_MSG(SUCCEEDED(hr0), "AntiAliasing: hr0");
            
                  auto hr1 = native_device->CreateShaderResourceView(new_texture.get(), nullptr, Resources::srv.put());
                  ASSERT_MSG(SUCCEEDED(hr1), "AntiAliasing: hr1");
            
                  auto hr2 = native_device->CreateRenderTargetView(new_texture.get(), nullptr, Resources::rtv.put());
                  ASSERT_MSG(SUCCEEDED(hr2), "AntiAliasing: hr2");
               
                  reshade::log::message(reshade::log::level::info, "AntiAliasing: created Resources");
               }
            
               // VS
               native_device_context->VSSetShader(device_data.native_vertex_shaders.at(CompileTimeStringHash(Luma_MegaMix_DLAA_VS)).get(), nullptr, 0);
            
               // prefilter
               native_device_context->OMSetRenderTargets(1, &Resources::rtv, nullptr);
               native_device_context->PSSetShader(device_data.native_pixel_shaders.at(CompileTimeStringHash(Luma_MegaMix_DLAA_PreFilter)).get(), nullptr, 0);
               native_device_context->Draw(4,0);
            
               // resolve
               native_device_context->OMSetRenderTargets(1, &rtv0, nullptr);
               native_device_context->PSSetShaderResources(0, 1, &Resources::srv);
               native_device_context->PSSetShader(device_data.native_pixel_shaders.at(CompileTimeStringHash(Luma_MegaMix_DLAA_Resolve)).get(), nullptr, 0);
               native_device_context->Draw(4,0);
            }
            else if (enabled == SMAA) // SMAA
            {
               // SRV0 & RTV0
               ComPtr<ID3D11ShaderResourceView> srv0;
               native_device_context->PSGetShaderResources(0, 1, srv0.put());
               
               ComPtr<ID3D11RenderTargetView> rtv0;
               native_device_context->OMGetRenderTargets(1, rtv0.put(), nullptr);

               // Depth SRV
               [[unlikely]] if (!Resources::depth_srv)
               {
                  D3D11_SHADER_RESOURCE_VIEW_DESC srv_depth_desc = {};
                  srv_depth_desc.Format = DXGI_FORMAT_R32_FLOAT;
                  srv_depth_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                  srv_depth_desc.Texture2D.MipLevels = 1;
                  srv_depth_desc.Texture2D.MostDetailedMip = 0;
                  auto hr = native_device->CreateShaderResourceView(RenderTracker::res_main_depth_tex.get(), &srv_depth_desc, Resources::depth_srv.put());
                  ASSERT_MSG(SUCCEEDED(hr), "AntiAliasing: hr");
               }

               // TODO: currently wasteful backing up draw state
               // TODO: currently wasteful querying tex for size to detect change
               // TODO: currently wasteful since no ready linear color tex
               DrawSMAA(native_device, native_device_context, device_data, rtv0.get(), srv0.get(), srv0.get(), Resources::depth_srv.get());
            }
            
            return DrawOrDispatchOverrideType::Replaced;
         }
         case Done:
         default:
            break;
      }
      
      return DrawOrDispatchOverrideType::None;
   }
   
   void OnInit()
   {
      native_shaders_definitions.emplace(CompileTimeStringHash(Luma_MegaMix_DLAA_VS), ShaderDefinition(Luma_MegaMix_DLAA, reshade::api::pipeline_subobject_type::vertex_shader, nullptr, "fullscreen_vs"));
      native_shaders_definitions.emplace(CompileTimeStringHash(Luma_MegaMix_DLAA_PreFilter), ShaderDefinition(Luma_MegaMix_DLAA, reshade::api::pipeline_subobject_type::pixel_shader, nullptr, "prefilter_ps"));
      native_shaders_definitions.emplace(CompileTimeStringHash(Luma_MegaMix_DLAA_Resolve), ShaderDefinition(Luma_MegaMix_DLAA, reshade::api::pipeline_subobject_type::pixel_shader, nullptr, "resolve_ps"));
   }
   
   void OnLoad(reshade::api::effect_runtime* runtime)
   {
      int enabled_int = enabled;
      if (reshade::get_config_value(runtime, NAME, reshadesave_enabled, enabled_int)) enabled = static_cast<Enabled>(enabled_int);
   }

   void OnPresent()
   {
      state = MLAAEdges0;
   }

   void HardReset()
   {
      Resources::Reset();
   }
}

namespace DepthOfField
{
   enum Enabled : uint8_t
   {
      Vanilla,
      Disable,
      // Enhanced, //TODO: implement
   };
   Enabled enabled = Vanilla;
   constexpr const char* reshadesave_enabled = "DepthOfFieldEnabled";

   enum State : uint8_t
   {
      TileCreate, // 0x83AE9A79
      TileExpand, // 0x8814AF0D
      PreSort, // 0x043F4B65
      Blur, // 0x7D2DE42C
      Resolve, // 0xD7064E88
      Done,
   };
   State state; // denotes next shader to be drawn

   DrawOrDispatchOverrideType OnDrawOrDispatchOverride(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, uint32_t ps)
   {
      if (enabled == Vanilla) return DrawOrDispatchOverrideType::None;
      if (DEVELOPMENT && !IsModEnabled()) return DrawOrDispatchOverrideType::None;

      switch (state)
      {
         case TileCreate:
         {
            if (ps == 0x83AE9A79)
            {
               state = TileExpand;
               return enabled == Disable ? DrawOrDispatchOverrideType::Skip : DrawOrDispatchOverrideType::None;
            }
            break;
         }
         case TileExpand:
         {
            if (ps == 0x8814AF0D)
            {
               state = PreSort;            
               return enabled == Disable ? DrawOrDispatchOverrideType::Skip : DrawOrDispatchOverrideType::None;
            }
            break;
         }
         case PreSort:
         {
            if (ps == 0x043F4B65)
            {
               state = Blur;
               return enabled == Disable ? DrawOrDispatchOverrideType::Skip : DrawOrDispatchOverrideType::None;
            }
            break;
         }
         case Blur:
         {
            if (ps == 0x7D2DE42C)
            {
               state = Resolve;
               return enabled == Disable ? DrawOrDispatchOverrideType::Skip : DrawOrDispatchOverrideType::None;
            }
            break;
         }
         case Resolve:
         {
            if (ps == 0xD7064E88)
            {
               state = TileCreate; // restart for 2nd pass if exists

               if (enabled == Disable)
               {
                  // CopyResource SRV3 to RTV0
                  ComPtr<ID3D11ShaderResourceView> srv3;
                  native_device_context->PSGetShaderResources(3, 1, srv3.put());
                  ComPtr<ID3D11Resource> srv3_resource;
                  srv3->GetResource(srv3_resource.put());

                  ComPtr<ID3D11RenderTargetView> rtv0;
                  native_device_context->OMGetRenderTargets(1, rtv0.put(), nullptr);
                  ComPtr<ID3D11Resource> rtv0_resource;
                  rtv0->GetResource(rtv0_resource.put());

                  native_device_context->CopyResource(rtv0_resource.get(), srv3_resource.get());

                  return DrawOrDispatchOverrideType::Replaced;
               }
            }
            break;
         }
         case Done:
         default:
            break;
      }

      return DrawOrDispatchOverrideType::None;
   }

   void OnTonemapAndFinalDraw()
   {
      state = Done; // reset for next frame
   }

   void OnLoad(reshade::api::effect_runtime* runtime)
   {
      int enabled_int = enabled;
      if (reshade::get_config_value(runtime, NAME, reshadesave_enabled, enabled_int)) enabled = static_cast<Enabled>(enabled_int);
   }

   void OnPresent()
   {
      state = TileCreate;
   }

   void HardReset()
   {
      
   }
}

namespace PS4Blur
{
   namespace Resources
   {
      ComPtr<ID3D11ShaderResourceView> srv0 = nullptr;
      ComPtr<ID3D11RenderTargetView> rtv0 = nullptr;

      ComPtr<ID3D11ShaderResourceView> srv1 = nullptr;
      ComPtr<ID3D11RenderTargetView> rtv1 = nullptr;

      uint2 size = { 0, 0 };

      bool flipflop = false; // false = srv0/rtv0, true = srv1/rtv1

      void Reset()
      {
         srv0.reset(); rtv0.reset();
         srv1.reset(); rtv1.reset();
         size = { 0, 0 };
         flipflop = false;
      }

      void Create(ID3D11Device* native_device, uint2 s)
      {
         size = s;

         D3D11_TEXTURE2D_DESC tex_desc;
         tex_desc.Width = size.x;
         tex_desc.Height = size.y;
         tex_desc.MipLevels = 1;
         tex_desc.ArraySize = 1;
         tex_desc.Format = DXGI_FORMAT_R11G11B10_FLOAT;
         tex_desc.SampleDesc.Count = 1;
         tex_desc.SampleDesc.Quality = 0;
         tex_desc.Usage = D3D11_USAGE_DEFAULT;
         tex_desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
         tex_desc.CPUAccessFlags = 0;
         tex_desc.MiscFlags = 0;

         ComPtr<ID3D11Texture2D> tex0;
         auto hr0 = native_device->CreateTexture2D(&tex_desc, nullptr, tex0.put());
         ASSERT_MSG(SUCCEEDED(hr0), "PS4Blur: Create tex0 hr0");
         auto hr1 = native_device->CreateShaderResourceView(tex0.get(), nullptr, srv0.put());
         ASSERT_MSG(SUCCEEDED(hr1), "PS4Blur: Create srv0 hr1");
         auto hr2 = native_device->CreateRenderTargetView(tex0.get(), nullptr, rtv0.put());
         ASSERT_MSG(SUCCEEDED(hr2), "PS4Blur: Create rtv0 hr2");

         auto hr3 = native_device->CreateTexture2D(&tex_desc, nullptr, tex0.put());
         ASSERT_MSG(SUCCEEDED(hr3), "PS4Blur: Create tex1 hr3");
         auto hr4 = native_device->CreateShaderResourceView(tex0.get(), nullptr, srv1.put());
         ASSERT_MSG(SUCCEEDED(hr4), "PS4Blur: Create srv1 hr4");
         auto hr5 = native_device->CreateRenderTargetView(tex0.get(), nullptr, rtv1.put());
         ASSERT_MSG(SUCCEEDED(hr5), "PS4Blur: Create rtv1 hr5");

         reshade::log::message(reshade::log::level::info, std::format("PS4Blur: Created Resources for size {}x{}", size.x, size.y).c_str());
      }
   }

   void OnDrawFinal(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data)
   {
      if (!ShaderDefineInfo::GetB(ShaderDefineInfo::CUSTOM_PS4BLUR_1)) return;
      if (DEVELOPMENT && !IsModEnabled()) return;

      // get RTV0 size (because SRV0 can be 2x SSAA!)
      ComPtr<ID3D11RenderTargetView> rtv0;
      native_device_context->OMGetRenderTargets(1, rtv0.put(), nullptr);
      ASSERT_MSG(rtv0 != nullptr, "PS4Blur: RTV0 is null in OnDrawFinal");

      [[unlikely]] if (Resources::size.x == 0)
      {
         ComPtr<ID3D11Resource> rtv0_resource;
         rtv0->GetResource(rtv0_resource.put());
         
         ComPtr<ID3D11Texture2D> rtv0_texture;
         auto hr = rtv0_resource->QueryInterface(rtv0_texture.put());
         ASSERT_MSG(SUCCEEDED(hr), "PS4Blur: OnDrawFinal hr");
         
         D3D11_TEXTURE2D_DESC tex_desc;
         rtv0_texture->GetDesc(&tex_desc);

         // create
         Resources::Create(native_device, { tex_desc.Width, tex_desc.Height });
      }

      // bind SRV1 to out new blur result
      native_device_context->PSSetShaderResources(1, 1, !Resources::flipflop ? &Resources::srv0 : &Resources::srv1);

      // set RTV to {orig, prev frame}
      const std::array<ID3D11RenderTargetView*, 2> rtv = { rtv0.get(), !Resources::flipflop ? Resources::rtv1.get() : Resources::rtv0.get() };
      native_device_context->OMSetRenderTargets(rtv.size(), rtv.data(), nullptr);

      // dual viewport
      D3D11_VIEWPORT viewports[2];
      viewports[0].TopLeftX = 0;
      viewports[0].TopLeftY = 0;
      viewports[0].Width = static_cast<float>(Resources::size.x);
      viewports[0].Height = static_cast<float>(Resources::size.y);
      viewports[0].MinDepth = 0;
      viewports[0].MaxDepth = 1;
      viewports[1].TopLeftX = 0;
      viewports[1].TopLeftY = 0;
      viewports[1].Width = static_cast<float>(Resources::size.x);
      viewports[1].Height = static_cast<float>(Resources::size.y);
      viewports[1].MinDepth = 0;
      viewports[1].MaxDepth = 1;
      native_device_context->RSSetViewports(2, viewports);

      // ++
      Resources::flipflop = !Resources::flipflop;
   }

   void HardReset()
   {
      Resources::Reset();
   }
}

namespace LUTBiasCached
{
   constexpr const char* Luma_MegaMix_LUTBiasCached = "Luma_MegaMix_LUTBiasCached";

   constexpr int LUT_CACHE_OUTPUT_SIZE = 2048; // remember to change in HLSL too!
   
   namespace Resources
   {
      ComPtr<ID3D11ShaderResourceView> srv = nullptr;
      ComPtr<ID3D11UnorderedAccessView> uav = nullptr;
   }

   void OnTonemapDraw(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data)
   {
      // gatekeep
      if (cb_luma_global_settings.DisplayMode != DisplayModeType::HDR && !ShaderDefineInfo::GetB(ShaderDefineInfo::CUSTOM_HDRTONEMAPONSDR)) return;
      if (!ShaderDefineInfo::GetB(ShaderDefineInfo::CUSTOM_LUT_BLOWOUT_GAUSSIAN)) return;
      if (DEVELOPMENT && !IsModEnabled()) return;

      // nulls
      constexpr ID3D11UnorderedAccessView* null_uav = nullptr;
      constexpr ID3D11ShaderResourceView* null_srv = nullptr;
      constexpr ID3D11ComputeShader* null_cs = nullptr;
      constexpr ID3D11SamplerState* null_sampler = nullptr;
      constexpr ID3D11Buffer* null_cb = nullptr;

      // create resources
      [[unlikely]] if (!Resources::srv.get())
      {
         D3D11_TEXTURE2D_DESC tex_desc;
         tex_desc.Width = LUT_CACHE_OUTPUT_SIZE;
         tex_desc.Height = 1;
         tex_desc.MipLevels = 1;
         tex_desc.ArraySize = 1;
         tex_desc.Format = DXGI_FORMAT_R16_FLOAT;
         tex_desc.SampleDesc.Count = 1;
         tex_desc.SampleDesc.Quality = 0;
         tex_desc.Usage = D3D11_USAGE_DEFAULT;
         tex_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
         tex_desc.CPUAccessFlags = 0;
         tex_desc.MiscFlags = 0;

         ComPtr<ID3D11Texture2D> tex;
         auto hr0 = native_device->CreateTexture2D(&tex_desc, nullptr, tex.put());
         ASSERT_MSG(SUCCEEDED(hr0), "LUTBiasCache: Create hr0");
         auto hr1 = native_device->CreateShaderResourceView(tex.get(), nullptr, Resources::srv.put());
         ASSERT_MSG(SUCCEEDED(hr1), "LUTBiasCache: Create hr1");
         auto hr2 = native_device->CreateUnorderedAccessView(tex.get(), nullptr, Resources::uav.put());
         ASSERT_MSG(SUCCEEDED(hr2), "LUTBiasCache: Create hr2");
      }

      // get & unbind PS SRV2
      ComPtr<ID3D11ShaderResourceView> srv2;
      native_device_context->PSGetShaderResources(2, 1, srv2.put());
      if (!srv2) return; // skip if null (so tonemap will also not use LUT)
      native_device_context->PSSetShaderResources(2, 1, &null_srv);
      
      // draw CS
      native_device_context->CSSetShaderResources(0, 1, &srv2);
      native_device_context->CSSetUnorderedAccessViews(0, 1, &Resources::uav, nullptr);
      native_device_context->CSSetSamplers(0, 1, &device_data.sampler_state_point);
      SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, reshade::api::shader_stage::compute, LumaConstantBufferType::LumaSettings);
      native_device_context->CSSetShader(device_data.native_compute_shaders.at(CompileTimeStringHash(Luma_MegaMix_LUTBiasCached)).get(), nullptr, 0);
      native_device_context->Dispatch((LUT_CACHE_OUTPUT_SIZE + 63) / 64, 1, 1);

      // clean CS
      native_device_context->CSSetShaderResources(0, 1, &null_srv);
      native_device_context->CSSetUnorderedAccessViews(0, 1, &null_uav, nullptr);
      native_device_context->CSSetShader(null_cs, nullptr, 0);
      native_device_context->CSSetSamplers(0, 1, &null_sampler);
      native_device_context->CSSetConstantBuffers(luma_data_cbuffer_index, 1, &null_cb);

      // rebind PS SRV6
      native_device_context->PSSetShaderResources(2, 1, &srv2);
      
      // bind PS SRV11 as LUTBiasCache
      native_device_context->PSSetShaderResources(11, 1, &Resources::srv);
   }

   void OnInit()
   {
      native_shaders_definitions.emplace(CompileTimeStringHash(Luma_MegaMix_LUTBiasCached), ShaderDefinition(Luma_MegaMix_LUTBiasCached, reshade::api::pipeline_subobject_type::compute_shader));
   }
}

namespace ShadowMap
{
   /*
      Main buffer: 2048x2048 RGBA8 & R32F
         Used by both character and world
         Downsamples to 512x512 RGBA8 & R32F

      Usage:
         - SRV6 World A (g_shadow_textures_0_) 512x512 RGBA8
            - Used by forward render lighting after SSS
         - SRV7 World B (g_shadow_textures_1_) 512x512 RGBA8
            - Used by forward render lighting after SSS
         - SRV19 Depth High (g_shadow_depth_textures_0_) 2048x2048 R32F
            - Used by SSS setup 0x93881580 SRV19
         - SRV20 Depth Low (g_shadow_depth_textures_1_) 512x512 R32F
            - Used by forward render lighting after SSS
   */
   
   uint GetEnabledDepthSize()
   {
      switch (ShaderDefineInfo::Get(ShaderDefineInfo::CUSTOM_ESM_DEPTH_NEW_SIZE))
      {
         case 1: return 1024; // index is also parsed in shader HLSL!
         case 2: return 2048;
         case 3: return 4096;
         case 4: return 8192;
         default: return 0;
      }
   }
   
   constexpr const char* reshadesave_enabled_color_mips = "ShadowMapEnabledColorMips";
   constexpr int enabled_color_mips_def = 2;
   int enabled_color_mips = enabled_color_mips_def;

   constexpr const char* reshadesave_color_mips_skip = "ShadowMapColorMipsSkip";
   constexpr int color_mips_skip_def = -1;
   int color_mips_skip = color_mips_skip_def;
   
   bool IsEnabled() {return ShaderDefineInfo::GetB(ShaderDefineInfo::CUSTOM_ESM_DEPTH_NEW_SIZE);}
   void DisableCompletely() {ShaderDefineInfo::Set(ShaderDefineInfo::CUSTOM_ESM_DEPTH_NEW_SIZE, 0);}

   // ps that draws to shadow map tex before expand/downsample/blur
   constexpr std::array<uint32_t, 2> silhouette_shaders = {
      0xF2368D1C, // has alpha 
      0x8BD626DD, // completely opaque
      // (there are other named variants, but the cso's are the exact same)
   };

   // shadow map & render progress state
   enum State : uint8_t
   {
      Silhouette,
      
      TexBDownsample0, // 0x58A712B5
      TexB4TapBlur0, // 0x4E2A88FE
      TexBCopy0, // 0x58A712B5
      
      DepthExpand0, // 0x54B1FF52 (this means silhouettes are done) (aka gaussian horizontal blur)
      DepthExpand1, // 0x54B1FF52 (aka gaussian vertical blur)
      DepthDownsample0, // 0x5F8E8C7E (aka minimize)
      DepthExpand2, // 0x723E27C4 (aka erosion)
      
      TexADownsample0, // 0x58A712B5
      Tex4TapBlur0, // 0x4E2A88FE
      TexACopy0, // 0x58A712B5
      
      SSSSetup, // 0x93881580,
      
      // then 2nd/duplicated world reflection rendering
      // then BG Sprites rendering (maybe before 2nd world reflection rendering?)

      MainRender,

      Done,
      Unused,
   };
   State state = Silhouette; // denotes next shader to be drawn
   bool state_texa_drawn = false; // denotes if TexA was drawn to this frame
   [[maybe_unused]] bool state_texa_drawn_imgui;
   bool state_texb_drawn = false; // denotes if multiple objects was drawn this frame (therefore TexB), skipping unnecessary stuff if not
   [[maybe_unused]] bool state_texb_drawn_imgui;
   uint8_t state_silhouette_draw_count = 0; // counts how many shadow cast shaders have been drawn this frame
   bool state_replacetoken_silhouette = true; // only needs RTV/DSV replacement once at start and another after TexB use
   bool state_replacetoken_sss = true; // only needs SRV replacement once for the whole pass
   bool state_noreset_is_pbr = true; // determined by 1st gaussian blur shader, if it uses normalize variant for NPR. (not reset by OnPresent())
   bool state_silhouette_clear_token_color = true; // token to clear if needed (OnClearRenderTargetViewMegaMix will also grant)
   bool state_silhouette_clear_token_depth = true; // token to clear if needed
   bool state_silhouette_clear_token_color_granted_this_frame = false; // granted by OnClearRenderTargetViewMegaMix
   uint64_t state_texdowna_rtv_handle = 0; // handle of TexA downsample that was used this frame. if !state_texa_used_this_frame, this is 0.


   void ResetState()
   {
      ASSERT_ONCE_MSG(state != MainRender, "ShadowMap: ResetState called while in MainRender state."); // this means we thought we need to replace more SRV usage, but it never happened!
      state = Silhouette;
      
      state_texa_drawn_imgui = state_texa_drawn;
      state_texb_drawn_imgui = state_texb_drawn;
      state_texa_drawn = false;
      state_texb_drawn = false;
      
      state_silhouette_draw_count = 0;
      
      state_replacetoken_silhouette = true;
      state_replacetoken_sss = true;
      
      state_silhouette_clear_token_color = true; // OnClearRenderTargetViewMegaMix will also grant
      state_silhouette_clear_token_depth = true;
      state_silhouette_clear_token_color_granted_this_frame = false;
   }

   namespace Shaders
   {
      constexpr const char* Luma_MegaMix_ESMDepthGaussian = "Luma_MegaMix_ESMDepthGaussian"; // file name
      constexpr const char* Luma_MegaMix_ESMDepthGaussian_Default = "Luma_MegaMix_ESMDepthGaussian_Default"; // shader name: gaussian as expected
      constexpr const char* Luma_MegaMix_ESMDepthGaussian_NPRNormalize = "Luma_MegaMix_ESMDepthGaussian_NPRNormalize"; // shader name: gaussian with normalization (x * 0.5 + 0.5) per sample for NPR

      constexpr const char* Luma_MegaMix_ESMColorGaussian = "Luma_MegaMix_ESMColorGaussian"; // file name
      constexpr const char* Luma_MegaMix_ESMColorGaussian_MainVS = "Luma_MegaMix_ESMColorGaussian_MainVS"; // shader name: fullscreen
      constexpr const char* Luma_MegaMix_ESMColorGaussian_DownPreFilter = "Luma_MegaMix_ESMColorGaussian_DownPreFilter"; // shader name: downsample gaussian, prefilter o0.xy = r0.xxxw
      constexpr const char* Luma_MegaMix_ESMColorGaussian_Down = "Luma_MegaMix_ESMColorGaussian_Down"; // shader name: downsample gaussian as expected 
      constexpr const char* Luma_MegaMix_ESMColorGaussian_Up = "Luma_MegaMix_ESMColorGaussian_Up"; // shader name: upsample
      constexpr const char* Luma_MegaMix_ESMColorGaussian_UpPostFilter = "Luma_MegaMix_ESMColorGaussian_UpPostFilter"; // shader name: upsample, postfilter o0.xyzw = r0.xxxy
      
      void OnInit()
      {
         native_shaders_definitions.emplace(CompileTimeStringHash(Luma_MegaMix_ESMDepthGaussian_Default), ShaderDefinition{ Luma_MegaMix_ESMDepthGaussian, reshade::api::pipeline_subobject_type::pixel_shader,   nullptr, "main", {{}}});
         native_shaders_definitions.emplace(CompileTimeStringHash(Luma_MegaMix_ESMDepthGaussian_NPRNormalize), ShaderDefinition{ Luma_MegaMix_ESMDepthGaussian, reshade::api::pipeline_subobject_type::pixel_shader,   nullptr, "main", {{"CUSTOM_ESM_DEPTH_NORMALIZE", "1"}}});

         native_shaders_definitions.emplace(CompileTimeStringHash(Luma_MegaMix_ESMColorGaussian_MainVS), ShaderDefinition{ Luma_MegaMix_ESMColorGaussian, reshade::api::pipeline_subobject_type::vertex_shader,   nullptr, "MainVS", {{}}});
         native_shaders_definitions.emplace(CompileTimeStringHash(Luma_MegaMix_ESMColorGaussian_DownPreFilter), ShaderDefinition{ Luma_MegaMix_ESMColorGaussian, reshade::api::pipeline_subobject_type::pixel_shader,   nullptr, "DownPreFilter", {{}}});
         native_shaders_definitions.emplace(CompileTimeStringHash(Luma_MegaMix_ESMColorGaussian_Down), ShaderDefinition{ Luma_MegaMix_ESMColorGaussian, reshade::api::pipeline_subobject_type::pixel_shader,   nullptr, "Down", {{}}});
         native_shaders_definitions.emplace(CompileTimeStringHash(Luma_MegaMix_ESMColorGaussian_Up), ShaderDefinition{ Luma_MegaMix_ESMColorGaussian, reshade::api::pipeline_subobject_type::pixel_shader,   nullptr, "Up", {{}}});
         native_shaders_definitions.emplace(CompileTimeStringHash(Luma_MegaMix_ESMColorGaussian_UpPostFilter), ShaderDefinition{ Luma_MegaMix_ESMColorGaussian, reshade::api::pipeline_subobject_type::pixel_shader,   nullptr, "UpPostFilter", {{}}});
      }
   }

   namespace Resources
   {
      // resource/handle search state
      enum State : uint8_t
      {
         FindDepthDrawHandle,
         FindDepthUseHandle,
         FindDepthDownHandle,
         FindColor,
         Done,
      };
      State state = FindDepthDrawHandle;
      bool IsOriginalSearchReady() { return state >= Done; }
      
      uint64_t orig_depthdraw_handle = 0; // 2048x2048 R32F
      uint64_t orig_depthuse_handle = 0; // 2048x2048 R32F, bound SRV19
      uint64_t orig_depthusedown_handle = 0; // 512x512 R32F, bound SRV20
      uint depth_size_orig = 0; // should not be 0 when State IsReady()

      uint64_t orig_tex_handle = 0; // 2048x2048 RGBA8
      // uint64_t orig_texdowna_handle = 0; // 512x512 RGBA8
      // uint64_t orig_texdownb_handle = 0; // 512x512 RGBA8
      uint tex_size_orig = 0; // should not be 0 when State IsReady()

      void Reset_Handles()
      {
         state = FindDepthDrawHandle;
         
         orig_depthdraw_handle = 0;
         orig_depthuse_handle = 0;
         orig_depthusedown_handle = 0;
         depth_size_orig = 0;
         
         orig_tex_handle = 0;
         // orig_texdowna_handle = 0;
         // orig_texdownb_handle = 0;
         tex_size_orig = 0;
      }

      uint depth_size_new = 0; // if 0, resources are not created
      bool IsDepthNewReady() { return depth_size_new > 0; }

      ComPtr<ID3D11ShaderResourceView> draw_depth_srv = nullptr; // r32f
      ComPtr<ID3D11DepthStencilView> draw_depth_dsv = nullptr;
      
      ComPtr<ID3D11ShaderResourceView> draw_depth_aux_srv = nullptr; // r32f
      ComPtr<ID3D11RenderTargetView> draw_depth_aux_rtv = nullptr;
      
      ComPtr<ID3D11ShaderResourceView> draw_depth_use_srv = nullptr; // r32f
      ComPtr<ID3D11RenderTargetView> draw_depth_use_rtv = nullptr;

      ComPtr<ID3D11ShaderResourceView> draw_depth_use_eroded_srv = nullptr; // r32f
      ComPtr<ID3D11RenderTargetView> draw_depth_use_eroded_rtv = nullptr;

      ComPtr<ID3D11ShaderResourceView> draw_color_srv = nullptr; // rgba8u
      ComPtr<ID3D11RenderTargetView> draw_color_rtv = nullptr;

      uint color_size_new = 0; // if 0, resources are not created. 1/2 of depth_size_new
      bool IsColorNewReady() { return color_size_new > 0; } // TODO: this is tied to depth, so remove
      int color_mips_new = 0;
      int color_mips_skip_new = 0;
      UINT x_mip0_width  = 0;
      UINT x_mip0_height = 0;
      UINT y_mip0_width  = 0;
      UINT y_mip0_height = 0;
      bool is_dirty_check_token = true; // grant by ImGui open, so we dont have to recheck all each

      std::vector<ID3D11ShaderResourceView*> color_aux_x_srvs(8); // height = width * 2
      std::vector<ID3D11RenderTargetView*>   color_aux_x_rtvs(8); 

      std::vector<ID3D11ShaderResourceView*> color_aux_y_srvs(8); // height = width
      std::vector<ID3D11RenderTargetView*>   color_aux_y_rtvs(8); 
      
      ComPtr<ID3D11ShaderResourceView> color_use_a_srv; // mip0 is use
      ComPtr<ID3D11RenderTargetView>   color_use_a_rtv;

      ComPtr<ID3D11ShaderResourceView> color_use_b_srv; // mip0 is use
      ComPtr<ID3D11RenderTargetView>   color_use_b_rtv;

      struct alignas(16) CBMegaMixColorAux
      {
         float2 src_size;
         float2 inv_src_size;
         float2 axis;
      };
      ComPtr<ID3D11Buffer> color_aux_cb;
      
      ComPtr<ID3D11BlendState> color_aux_blendstate;

      void Reset_Resources()
      {
         depth_size_new = 0;
         draw_depth_srv.reset(); draw_depth_dsv.reset();
         draw_color_srv.reset(); draw_color_rtv.reset();
         draw_depth_aux_srv.reset(); draw_depth_aux_rtv.reset();
         draw_depth_use_srv.reset(); draw_depth_use_rtv.reset();
         draw_depth_use_eroded_srv.reset(); draw_depth_use_eroded_rtv.reset();

         color_size_new = 0; 
         ResetCOMArray(color_aux_x_srvs); ResetCOMArray(color_aux_x_rtvs);
         ResetCOMArray(color_aux_y_srvs); ResetCOMArray(color_aux_y_rtvs);
         color_use_a_srv.reset(); color_use_a_rtv.reset();
         color_use_b_srv.reset(); color_use_b_rtv.reset();
      }
      
      void Reset()
      {
         is_dirty_check_token = true; // grant
         Reset_Resources();
         Reset_Handles();
      }

      void CreateIfDirty(ID3D11Device* native_device)
      {
         // token?
         [[likely]] if (!is_dirty_check_token) return;
         is_dirty_check_token = false; // consume

         // dirty?
         bool is_dirty = false;
         {
            // depth size
            static int prev_enabled_depth;
            int new_enabled_depth = ShaderDefineInfo::Get(ShaderDefineInfo::CUSTOM_ESM_DEPTH_NEW_SIZE);

            // color mips
            static int prev_enabled_color_mips;
            color_mips_new = enabled_color_mips;
            color_mips_new = max(color_mips_new, 1);
            color_mips_new = min(color_mips_new, color_aux_x_srvs.size());
            int new_enabled_tex_mips = color_mips_new;

            // color mips skip
            static int prev_color_mips_skip;
            color_mips_skip_new = color_mips_skip;
            if (color_mips_skip_new < 0) color_mips_skip_new = max(0, new_enabled_depth - 2); // treat as if always 2048x2048, so first mip0 is 1024x1024

            // dirty?
            is_dirty = !IsColorNewReady() || !IsDepthNewReady()
            || prev_enabled_depth != new_enabled_depth
            || prev_enabled_color_mips != new_enabled_tex_mips
            || prev_color_mips_skip != color_mips_skip;

            // save to prev
            prev_enabled_depth = new_enabled_depth;
            prev_enabled_color_mips = new_enabled_tex_mips;
            prev_color_mips_skip = color_mips_skip;
         }
         if (!is_dirty) return;
         
         // reset
         if (is_dirty) Reset_Resources();

         // set
         depth_size_new = GetEnabledDepthSize();
         color_size_new = depth_size_new / pow(2, 1 + color_mips_skip_new);

         ComPtr<ID3D11Texture2D> tex_ptr;

         // depth
         D3D11_TEXTURE2D_DESC depth_desc;
         depth_desc.Width = depth_size_new;
         depth_desc.Height = depth_size_new;
         depth_desc.MipLevels = 1;
         depth_desc.ArraySize = 1;
         depth_desc.Format = DXGI_FORMAT_R32_TYPELESS;
         depth_desc.SampleDesc.Count = 1;
         depth_desc.SampleDesc.Quality = 0;
         depth_desc.Usage = D3D11_USAGE_DEFAULT;
         depth_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_DEPTH_STENCIL;
         depth_desc.CPUAccessFlags = 0;
         depth_desc.MiscFlags = 0;
         
         ASSERT_MSG(depth_size_new > 0, "ShadowMap: TryCreate() depth_size_new is 0");
         {
            D3D11_DEPTH_STENCIL_VIEW_DESC depth_dsv_desc;
            depth_dsv_desc.Format = DXGI_FORMAT_D32_FLOAT;
            depth_dsv_desc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
            depth_dsv_desc.Flags = 0;
            depth_dsv_desc.Texture2D.MipSlice = 0;

            D3D11_SHADER_RESOURCE_VIEW_DESC depth_srv_desc;
            depth_srv_desc.Format = DXGI_FORMAT_R32_FLOAT;
            depth_srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            depth_srv_desc.Texture2D.MostDetailedMip = 0;
            depth_srv_desc.Texture2D.MipLevels = 1;

            D3D11_RENDER_TARGET_VIEW_DESC depth_rtv_desc;
            depth_rtv_desc.Format = DXGI_FORMAT_R32_FLOAT;
            depth_rtv_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
            depth_rtv_desc.Texture2D.MipSlice = 0;
            
            // depth draw
            auto hr0 = native_device->CreateTexture2D(&depth_desc, nullptr, tex_ptr.put());
            ASSERT_MSG(SUCCEEDED(hr0), "ShadowMap: Create() hr0");
            auto hr1 = native_device->CreateShaderResourceView(tex_ptr.get(), &depth_srv_desc, draw_depth_srv.put());
            ASSERT_MSG(SUCCEEDED(hr1), "ShadowMap: Create() hr1");
            auto hr2 = native_device->CreateDepthStencilView(tex_ptr.get(), &depth_dsv_desc, draw_depth_dsv.put());
            ASSERT_MSG(SUCCEEDED(hr2), "ShadowMap: Create() hr2");

            // depth aux & use
            depth_desc.Format = DXGI_FORMAT_R32_FLOAT;
            depth_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;

            auto hr3 = native_device->CreateTexture2D(&depth_desc, nullptr, tex_ptr.put());
            ASSERT_MSG(SUCCEEDED(hr3), "ShadowMap: Create() hr3");
            auto hr4 = native_device->CreateShaderResourceView(tex_ptr.get(), nullptr, draw_depth_aux_srv.put());
            ASSERT_MSG(SUCCEEDED(hr4), "ShadowMap: Create() hr4");
            auto hr5 = native_device->CreateRenderTargetView(tex_ptr.get(), nullptr, draw_depth_aux_rtv.put());
            ASSERT_MSG(SUCCEEDED(hr5), "ShadowMap: Create() hr5");

            auto hr6 = native_device->CreateTexture2D(&depth_desc, nullptr, tex_ptr.put());
            ASSERT_MSG(SUCCEEDED(hr6), "ShadowMap: Create() hr6");
            auto hr7 = native_device->CreateShaderResourceView(tex_ptr.get(), nullptr, draw_depth_use_srv.put());
            ASSERT_MSG(SUCCEEDED(hr7), "ShadowMap: Create() hr7");
            auto hr8 = native_device->CreateRenderTargetView(tex_ptr.get(), nullptr, draw_depth_use_rtv.put());
            ASSERT_MSG(SUCCEEDED(hr8), "ShadowMap: Create() hr8");

            // tex draw
            D3D11_TEXTURE2D_DESC tex_desc = depth_desc;
            tex_desc.Format = DXGI_FORMAT_R8G8_UNORM; // why does the game even use rgba for monochrome shadows?
            tex_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
            
            auto hr9 = native_device->CreateTexture2D(&tex_desc, nullptr, tex_ptr.put());
            ASSERT_MSG(SUCCEEDED(hr9), "ShadowMap: Create() hr9");
            auto hr10 = native_device->CreateShaderResourceView(tex_ptr.get(), nullptr, draw_color_srv.put());
            ASSERT_MSG(SUCCEEDED(hr10), "ShadowMap: Create() hr10");
            auto hr11 = native_device->CreateRenderTargetView(tex_ptr.get(), nullptr, draw_color_rtv.put());
            ASSERT_MSG(SUCCEEDED(hr11), "ShadowMap: Create() hr11");

            // log
            reshade::log::message(reshade::log::level::info, std::format("ShadowMap: Created Resources for depth {}x{}", depth_size_new, depth_size_new).c_str());
         }
         
         ASSERT_MSG(color_size_new > 0, "ShadowMap: TryCreate() color_size_new is 0");
         {
            // tex use a & b
            D3D11_TEXTURE2D_DESC tex_desc = depth_desc;
            tex_desc.Width = color_size_new;
            tex_desc.Height = color_size_new;
            tex_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
            
            tex_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; // TODO: why does the game even use rgba for monochrome shadows?
            tex_desc.MipLevels = 4;
            tex_desc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS; // farther away = higher mip

            auto hr15 = native_device->CreateTexture2D(&tex_desc, nullptr, tex_ptr.put());
            ASSERT_MSG(SUCCEEDED(hr15), "ShadowMap: Create() hr15");
            auto hr16 = native_device->CreateShaderResourceView(tex_ptr.get(), nullptr, color_use_a_srv.put());
            ASSERT_MSG(SUCCEEDED(hr16), "ShadowMap: Create() hr16");
            auto hr17 = native_device->CreateRenderTargetView(tex_ptr.get(), nullptr, color_use_a_rtv.put());
            ASSERT_MSG(SUCCEEDED(hr17), "ShadowMap: Create() hr17");

            auto hr18 = native_device->CreateTexture2D(&tex_desc, nullptr, tex_ptr.put());
            ASSERT_MSG(SUCCEEDED(hr18), "ShadowMap: Create() hr18");
            auto hr19 = native_device->CreateShaderResourceView(tex_ptr.get(), nullptr, color_use_b_srv.put());
            ASSERT_MSG(SUCCEEDED(hr19), "ShadowMap: Create() hr19");
            auto hr20 = native_device->CreateRenderTargetView(tex_ptr.get(), nullptr, color_use_b_rtv.put());
            ASSERT_MSG(SUCCEEDED(hr20), "ShadowMap: Create() hr20");

            // tex aux: setup // TODO: make helper to remove duplicate code in DuplicateWorldReflections
            tex_desc.MiscFlags = 0;
            tex_desc.Format = DXGI_FORMAT_R8G8_UNORM;
            tex_desc.MipLevels = color_mips_new;
            
            x_mip0_width  = color_size_new * 2; // size / 2 (x pass is first)
            x_mip0_height = color_size_new;     // size
            
            y_mip0_width  = color_size_new; // size / 2
            y_mip0_height = color_size_new; // size / 2

            // tex aux: y (mipmap chain)
            tex_desc.Width = y_mip0_width;
            tex_desc.Height = y_mip0_height;

            auto hr21 = native_device->CreateTexture2D(&tex_desc, nullptr, tex_ptr.put());
            ASSERT_MSG(SUCCEEDED(hr21), "ShadowMap: Create() hr21");

            D3D11_RENDER_TARGET_VIEW_DESC rtv_desc = {};
            rtv_desc.Format = tex_desc.Format;
            rtv_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
            
            D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
            srv_desc.Format = tex_desc.Format;
            srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            srv_desc.Texture2D.MipLevels = 1; // allow only 1 mip to get accessed

            for (int i = 0; i < color_mips_new; i++)
            {
               rtv_desc.Texture2D.MipSlice = i;
               auto hr22 = native_device->CreateRenderTargetView(tex_ptr.get(), &rtv_desc, &color_aux_y_rtvs[i]);
               ASSERT_MSG(SUCCEEDED(hr22), "ShadowMap: Create() hr22");
                     
               srv_desc.Texture2D.MostDetailedMip = i;
               auto hr23 = native_device->CreateShaderResourceView(tex_ptr.get(), &srv_desc, &color_aux_y_srvs[i]);
               ASSERT_MSG(SUCCEEDED(hr23), "ShadowMap: Create() hr23");
            }

            // tex aux: x (unlike y, needs individual tex)
            tex_desc.Width = x_mip0_width;
            tex_desc.Height = x_mip0_height;
            tex_desc.MipLevels = 1;
            
            auto hr24 = native_device->CreateTexture2D(&tex_desc, nullptr, tex_ptr.put());
            ASSERT_MSG(SUCCEEDED(hr24), "ShadowMap: Create() hr24");
            auto hr25 = native_device->CreateRenderTargetView(tex_ptr.get(), nullptr, &color_aux_x_rtvs[0]);
            ASSERT_MSG(SUCCEEDED(hr25), "ShadowMap: Create() hr25");
            auto hr26 = native_device->CreateShaderResourceView(tex_ptr.get(), nullptr, &color_aux_x_srvs[0]);
            ASSERT_MSG(SUCCEEDED(hr26), "ShadowMap: Create() hr26");

            for (UINT i = 1; i < color_mips_new; i++)
            {
               tex_desc.Width = max(1u, x_mip0_width >> i);
               tex_desc.Height = max(1u, x_mip0_height >> i);
               
               auto hr27 = native_device->CreateTexture2D(&tex_desc, nullptr, tex_ptr.put());
               ASSERT_MSG(SUCCEEDED(hr27), "ShadowMap: Create() hr27");
               auto hr28 = native_device->CreateRenderTargetView(tex_ptr.get(), nullptr, &color_aux_x_rtvs[i]);
               ASSERT_MSG(SUCCEEDED(hr28), "ShadowMap: Create() hr28");
               auto hr29 = native_device->CreateShaderResourceView(tex_ptr.get(), nullptr, &color_aux_x_srvs[i]);
               ASSERT_MSG(SUCCEEDED(hr29), "ShadowMap: Create() hr29");
            }

            // cb
            [[unlikely]] if (!color_aux_cb)
            {
               D3D11_BUFFER_DESC buffer_desc = {};
               buffer_desc.ByteWidth = sizeof(CBMegaMixColorAux);
               buffer_desc.Usage = D3D11_USAGE_DYNAMIC;
               buffer_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
               buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
               
               auto hr30 = native_device->CreateBuffer(&buffer_desc, nullptr, color_aux_cb.put());
               ASSERT_MSG(SUCCEEDED(hr30), "ShadowMap: Create() hr30");
            }
            

            // blend
            [[unlikely]] if (!color_aux_blendstate)
            {
               CD3D11_BLEND_DESC blend_desc(D3D11_DEFAULT);
               blend_desc.RenderTarget[0].BlendEnable = TRUE;
               blend_desc.RenderTarget[0].SrcBlend = D3D11_BLEND_BLEND_FACTOR;
               blend_desc.RenderTarget[0].DestBlend = D3D11_BLEND_BLEND_FACTOR;
               
               auto hr31 = native_device->CreateBlendState(&blend_desc, color_aux_blendstate.put());
               ASSERT_MSG(SUCCEEDED(hr31), "ShadowMap: Create() hr31");
            }
            
            // log
            reshade::log::message(reshade::log::level::info, std::format("ShadowMap: Created Resources for tex {}x{}", color_size_new, color_size_new).c_str());
         }
      }
   }

   void DrawWorldDownsamplePass(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, bool is_texb)
   {
      ASSERT_MSG(Resources::IsDepthNewReady(), "ShadowMap: DrawWorldDownsamplePass() Resources::IsDepthNewReady() false");
      ASSERT_MSG(Resources::IsColorNewReady(), "ShadowMap: DrawWorldDownsamplePass() Resources::IsColorNewReady() false");
      ASSERT_MSG(Resources::color_aux_cb, "ShadowMap: DrawWorldDownsamplePass() Resources::color_aux_cb null");

      // Backup TODO: del unneeded backups
      D3D11_PRIMITIVE_TOPOLOGY primitive_topology_original;
      native_device_context->IAGetPrimitiveTopology(&primitive_topology_original);
      
      // ComPtr<ID3D11VertexShader> vs_original;
      // native_device_context->VSGetShader(vs_original.put(), nullptr, nullptr);
      
      // ComPtr<ID3D11PixelShader> ps_original;
      // native_device_context->PSGetShader(ps_original.put(), nullptr, nullptr);
      
      // ComPtr<ID3D11Buffer> cb_orginal;
      // native_device_context->PSGetConstantBuffers(11, 1, cb_orginal.put());
      
      ComPtr<ID3D11SamplerState> ps_sampler_original;
      native_device_context->PSGetSamplers(0, 1, ps_sampler_original.put());
      
      // ComPtr<ID3D11ShaderResourceView> ps_srv_original;
      // native_device_context->PSGetShaderResources(0, 1, ps_srv_original.put());
      
      // UINT num_viewports = 1;
      // std::array<D3D11_VIEWPORT, 1> viewports_original;
      // native_device_context->RSGetViewports(&num_viewports, viewports_original.data());
      
      // ComPtr<ID3D11RasterizerState> rasterizer_original;
      // native_device_context->RSGetState(rasterizer_original.put());
      
      // ComPtr<ID3D11BlendState> blend_original;
      // FLOAT blend_factor_original[1];
      // UINT sample_mask_original;
      // native_device_context->OMGetBlendState(blend_original.put(), blend_factor_original, &sample_mask_original);
      
      // std::array<ID3D11RenderTargetView*, 1> rtvs_original = {};
      // ComPtr<ID3D11DepthStencilView> dsv_original;
      // native_device_context->OMGetRenderTargets(rtvs_original.size(), rtvs_original.data(), dsv_original.put());
      
      // cb
      Resources::CBMegaMixColorAux cb_data;
      auto update_constant_buffer = [&]()
      {
         D3D11_MAPPED_SUBRESOURCE mapped_subresource;
         auto hr = native_device_context->Map(Resources::color_aux_cb.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_subresource);
         ASSERT_MSG(SUCCEEDED(hr), "DrawWorldDownsamplePass: Map() failed");
         std::memcpy(mapped_subresource.pData, &cb_data, sizeof(Resources::CBMegaMixColorAux));
         native_device_context->Unmap(Resources::color_aux_cb.get(), 0);
      };

      // 0th Downsample: X w/ Prefilter
      D3D11_VIEWPORT viewport_x = {};
      viewport_x.Width = Resources::x_mip0_width;
      viewport_x.Height = Resources::x_mip0_height;

      cb_data.src_size = float2(Resources::color_size_new, Resources::color_size_new);
      cb_data.inv_src_size = float2(1.0f / cb_data.src_size.x, 1.0f / cb_data.src_size.y);
      cb_data.axis = float2(1.0f, 0.0f);
      // cb_data.sigma = sigma;
      update_constant_buffer();

      native_device_context->OMSetRenderTargets(1, &Resources::color_aux_x_rtvs[0], nullptr);
      native_device_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
      native_device_context->VSSetShader(device_data.native_vertex_shaders.at(CompileTimeStringHash(Shaders::Luma_MegaMix_ESMColorGaussian_MainVS)).get(), nullptr, 0); // same for the rest
      native_device_context->PSSetShader(device_data.native_pixel_shaders.at(CompileTimeStringHash(Shaders::Luma_MegaMix_ESMColorGaussian_DownPreFilter)).get(), nullptr, 0);
      native_device_context->PSSetConstantBuffers(11, 1, &Resources::color_aux_cb); // same for the rest
      const std::array ps_samplers = { device_data.sampler_state_linear.get() }; // same for the rest
      native_device_context->PSSetSamplers(0, ps_samplers.size(), ps_samplers.data());
      native_device_context->PSSetShaderResources(0, 1, &Resources::draw_color_srv);
      native_device_context->RSSetViewports(1, &viewport_x);
      native_device_context->RSSetState(nullptr);

      native_device_context->Draw(3, 0);

      // 0th Downsample: Y
      std::vector<D3D11_VIEWPORT> viewports_y(Resources::color_mips_new);
      viewports_y[0].Width = Resources::y_mip0_width;
      viewports_y[0].Height = Resources::y_mip0_height;
      
      cb_data.src_size = float2(Resources::x_mip0_width, Resources::x_mip0_height);
      cb_data.inv_src_size = float2(1.0f / cb_data.src_size.x, 1.0f / cb_data.src_size.y);
      cb_data.axis = float2(0.0f, 1.0f);
      update_constant_buffer();
      
      native_device_context->OMSetRenderTargets(1, &Resources::color_aux_y_rtvs[0], nullptr);
      native_device_context->PSSetShader(device_data.native_pixel_shaders.at(CompileTimeStringHash(Shaders::Luma_MegaMix_ESMColorGaussian_Down)).get(), nullptr, 0);
      native_device_context->PSSetShaderResources(0, 1, &Resources::color_aux_x_srvs[0]);
      native_device_context->RSSetViewports(1, &viewports_y[0]);
      
      native_device_context->Draw(3, 0);

      // Downsample
      for (UINT i = 1; i < Resources::color_mips_new; i++)
      {
         // X
         viewport_x.Width  = max(1u, Resources::x_mip0_width >> i);
         viewport_x.Height = max(1u, Resources::x_mip0_height >> i);
         
         cb_data.src_size = float2(viewports_y[i - 1].Width, viewports_y[i - 1].Height);
         cb_data.axis = float2(1.0f, 0.0f);
         cb_data.inv_src_size = float2(1.0f / cb_data.src_size.x, 1.0f / cb_data.src_size.y);
         // cb_data.sigma += color_sigma_increase;
         update_constant_buffer();
         
         native_device_context->OMSetRenderTargets(1, &Resources::color_aux_x_rtvs[i], nullptr);
         native_device_context->PSSetShaderResources(0, 1, &Resources::color_aux_y_srvs[i - 1]);
         native_device_context->RSSetViewports(1, &viewport_x);
          
         native_device_context->Draw(3, 0);

         // Y
         viewports_y[i].Width  = max(1u, Resources::y_mip0_width >> i);
         viewports_y[i].Height = max(1u, Resources::y_mip0_height >> i);
            
         cb_data.src_size = float2(viewport_x.Width, viewport_x.Height);
         cb_data.axis = float2(0.0f, 1.0f);
         cb_data.inv_src_size = float2(1.0f / cb_data.src_size.x, 1.0f / cb_data.src_size.y);
         update_constant_buffer();
         
         native_device_context->OMSetRenderTargets(1, &Resources::color_aux_y_rtvs[i], nullptr);
         native_device_context->PSSetShaderResources(0, 1, &Resources::color_aux_x_srvs[i]);
         native_device_context->RSSetViewports(1, &viewports_y[i]);
         
         native_device_context->Draw(3, 0);
      }

      // Upsample (climb up Y mips, until final where we postfilter into use a/b)
      native_device_context->PSSetShader(device_data.native_pixel_shaders.at(CompileTimeStringHash(Shaders::Luma_MegaMix_ESMColorGaussian_Up)).get(), nullptr, 0);
      constexpr FLOAT blend_factor_half[] = { 0.5f, 0.5f, 0.5f, 0.5f };
      constexpr FLOAT blend_factor_1[] = { 1.0f, 1.0f, 1.0f, 1.0f };
      for (int i = Resources::color_mips_new - 1; i > 0; i--)
      {
         bool is_final = i == 1;
      
         cb_data.src_size = float2(viewports_y[i].Width, viewports_y[i].Height);
         cb_data.inv_src_size = float2(1.0f / cb_data.src_size.x, 1.0f / cb_data.src_size.y);
         update_constant_buffer();
      
         if (is_final) native_device_context->PSSetShader(device_data.native_pixel_shaders.at(CompileTimeStringHash(Shaders::Luma_MegaMix_ESMColorGaussian_UpPostFilter)).get(), nullptr, 0); // final: postfilter
         native_device_context->OMSetRenderTargets(1, !is_final ? &Resources::color_aux_y_rtvs[i - 1] : (!is_texb ? &Resources::color_use_a_rtv : &Resources::color_use_b_rtv), nullptr);
         native_device_context->PSSetShaderResources(0, 1, &Resources::color_aux_y_srvs[i]);
         native_device_context->RSSetViewports(1, &viewports_y[i - 1]);
         native_device_context->OMSetBlendState(!is_final ? Resources::color_aux_blendstate.get() : nullptr, !is_final ? blend_factor_half : blend_factor_1, UINT_MAX); // final: full blend to stop prev frame bleed
      
         native_device_context->Draw(3, 0);
      }

      // Restore and Clean
      // native_device_context->OMSetBlendState(blend_original.get(), blend_factor_original, sample_mask_original);
      // native_device_context->OMSetRenderTargets(rtvs_original.size(), rtvs_original.data(), dsv_original.get());
      native_device_context->IASetPrimitiveTopology(primitive_topology_original);
      // native_device_context->VSSetShader(vs_original.get(), nullptr, 0);
      // native_device_context->PSSetShader(ps_original.get(), nullptr, 0);
      // native_device_context->PSSetConstantBuffers(11, 1, &cb_orginal);
      native_device_context->PSSetSamplers(0, 1, &ps_sampler_original);
      // native_device_context->PSSetShaderResources(0, 1, &ps_srv_original);
      // native_device_context->RSSetViewports(viewports_original.size(), viewports_original.data());
      // native_device_context->RSSetState(rasterizer_original.get());
      // ResetCOMArray(rtvs_original);
   }

   DrawOrDispatchOverrideType OnDrawOrDispatchOverride(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, uint32_t ps)
   {
      DrawOrDispatchOverrideType result = DrawOrDispatchOverrideType::None;
      if (!IsEnabled()) return result;
      if (DEVELOPMENT && !IsModEnabled()) return result;
      
      // force state
      if (state_silhouette_draw_count > 0)
      {
         // skip: DepthExpand0
         if (state <= DepthExpand0 && (ps == 0x54B1FF52 || ps == 0xCEDAC7C0))
            state = DepthExpand0;
         
         // skip: Tex B Downsample0
         else if (state == Silhouette && ps == 0x58A712B5)
            state = TexBDownsample0;

         // skip: Tex A not used
         else if (state == TexADownsample0 && ps != 0x58A712B5)
            state = SSSSetup;
         
         // backtrack: X Song Pack 100% res duplicate world reflections SSS again
         else if (state == MainRender && ShaderHashesLists::IsSSSSetupShader(ps)) 
         {
            ASSERT_MSG(!RenderTracker::IsMainColorBeingOrHasBeenDrawnTo(), "ShadowMap: OnDrawOrDispatchOverride() backtrack to MainRender, but RenderTracker::IsMainColorBeingOrHasBeenDrawnTo() true");
            state = SSSSetup;
            state_replacetoken_sss = true; // grant token again
         }
         
         // backtrack: X Song Pack 100% res duplicate world reflections usage again
         else if (state == Done && (ps == ShaderHashesLists::ToSwapchain || ps == 0x4E2A88FE) && !TonemapInfo::GetIsDrawnTonemapOrFinal(cb_luma_global_settings.GameSettings.TonemapInfo))
         {
            ASSERT_MSG(RenderTracker::IsMainColorBeingOrHasBeenDrawnTo(), "ShadowMap: OnDrawOrDispatchOverride() backtrack to MainRender, but RenderTracker::IsMainColorBeingOrHasBeenDrawnTo() false");
            state = MainRender;
         }
      }
      else
      {
         // skip: not used this frame
         if (state == Silhouette && RenderTracker::IsMainColorBeingOrHasBeenDrawnTo())
         {
            state = Unused;
         }
      }

      switch (state)
      {
         case Silhouette:
         {
            ASSERT_MSG(!RenderTracker::IsMainColorBeingOrHasBeenDrawnTo(), "ShadowMap: OnDrawOrDispatchOverride() Silhouette state, but RenderTracker::IsMainColorBeingOrHasBeenDrawnTo() true");

            // shadow caster ps?
            bool is_shadow_cast_shader = std::find(silhouette_shaders.begin(), silhouette_shaders.end(), ps) != silhouette_shaders.end();
            if (is_shadow_cast_shader) state_silhouette_draw_count++; // count++
            
            // ready
            if (Resources::IsOriginalSearchReady() && Resources::IsDepthNewReady() && is_shadow_cast_shader && state_replacetoken_silhouette)
            {
               // verify RTV0 & DSV are expected handles (this will correctly fail when PBR vs NPR switches. late 1 frame, but whatever since the game always uses transition effects when switching)
               ComPtr<ID3D11RenderTargetView> rtv0;
               ComPtr<ID3D11DepthStencilView> dsv;
               native_device_context->OMGetRenderTargets(1, rtv0.put(), dsv.put());
               ASSERT_MSG(rtv0 != nullptr, "ShadowMap: RTV0 null ShadowCast");
               ASSERT_MSG(dsv != nullptr, "ShadowMap: DSV null ShadowCast");
               
               ComPtr<ID3D11Resource> rtv0_res;
               ComPtr<ID3D11Resource> dsv_res;
               rtv0->GetResource(rtv0_res.put());
               dsv->GetResource(dsv_res.put());
               
               uint64_t rtv0_handle = reinterpret_cast<uint64_t>(rtv0_res.get());
               uint64_t dsv_handle = reinterpret_cast<uint64_t>(dsv_res.get());
               if (rtv0_handle != Resources::orig_tex_handle || dsv_handle != Resources::orig_depthdraw_handle)
               {
                  Resources::Reset_Handles();
                  break;
               }

               // clear
               if (state_silhouette_clear_token_color)
               {
                  state_silhouette_clear_token_color = false; // consume
                  constexpr float clear_color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
                  native_device_context->ClearRenderTargetView(Resources::draw_color_rtv.get(), clear_color);
               }
               if (state_silhouette_clear_token_depth)
               {
                  state_silhouette_clear_token_depth = false; // consume
                  native_device_context->ClearDepthStencilView(Resources::draw_depth_dsv.get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
               }
               
               // RTV0 & DSV as ours
               state_replacetoken_silhouette = false; // consume
               native_device_context->OMSetRenderTargets(1, &Resources::draw_color_rtv, Resources::draw_depth_dsv.get());

               // viewport to size
               float ratio = static_cast<float>(Resources::depth_size_new) / static_cast<float>(Resources::depth_size_orig);
               D3D11_VIEWPORT viewport;
               viewport.TopLeftX = ratio; // TODO: 1px, why?
               viewport.TopLeftY = ratio;
               float size = Resources::depth_size_new - (2 * ratio); // TODO: original is 2046x2046, 2px margin, why?
               viewport.Width = size;
               viewport.Height = size;
               viewport.MinDepth = 0;
               viewport.MaxDepth = 1;
               native_device_context->RSSetViewports(1, &viewport);
            }
            
            break;
         }
         // (multiple ShadowCast shader draws expected in between) ///////////////////////////
         case TexBDownsample0:
         {
            // gatekeep: ps
            if (ps != 0x58A712B5) break;
            ASSERT_MSG(!state_texb_drawn, "ShadowMap: TexBDownsample0 state_texb_used_this_frame true already! Is there a Tex C?");
            ASSERT_MSG(state_silhouette_draw_count > 0, "ShadowMap: TexBDownsample0 state_shadow_cast_count 0! There are no shadows to downsample!");

            // // not ready
            // [[unlikely]] if (Resources::state == Resources::FindTexBHandle)
            // {
            //    Resources::state = Resources::Done; // next
            //
            //    // get RTV0 handle
            //    ComPtr<ID3D11RenderTargetView> rtv0;
            //    native_device_context->OMGetRenderTargets(1, rtv0.put(), nullptr);
            //    ASSERT_MSG(rtv0 != nullptr, "ShadowMap: RTV0 null WorldBDownsample0 FindTexBHandle");
            //    ComPtr<ID3D11Resource> rtv0_resource;
            //    rtv0->GetResource(rtv0_resource.put());
            //    Resources::orig_texdownb_handle = reinterpret_cast<uint64_t>(rtv0_resource.get()); // save
            // }
            
            // ready
            if (Resources::IsOriginalSearchReady())
            {
               if (Resources::IsDepthNewReady())
               {
                  // replace
                  native_device_context->PSSetShaderResources(0, 1, &Resources::draw_color_srv); // SRV0: tex draw
               }

               if (Resources::IsColorNewReady())
               {
                  // replace with downsample gaussian pass
                  DrawWorldDownsamplePass(native_device, native_device_context, cmd_list_data, device_data, true);
                  result = DrawOrDispatchOverrideType::Replaced; // replaced
               }
            }

            // next
            state = TexB4TapBlur0;
            break;
         }
         case TexB4TapBlur0:
         {
            // gatekeep: ps
            if (ps != 0x4E2A88FE)
            {
               ASSERT_MSG(false, "ShadowMap: TexB4TapBlur0 ps not 0x4E2A88FE!?!?!");
               break;
            }

            // ready
            if (Resources::IsOriginalSearchReady() && Resources::IsColorNewReady())
            {
               result = DrawOrDispatchOverrideType::Replaced; // replaced prior
            }

            // next
            state = TexBCopy0;
            break;
         }
         case TexBCopy0:
         {
            // gatekeep: ps
            if (ps != 0x58A712B5)
            {
               ASSERT_MSG(false, "ShadowMap: TexBCopy0 ps not 0x58A712B5!?!?!");
               break;
            }

            // ready
            if (Resources::IsOriginalSearchReady() && Resources::IsColorNewReady())
            {
               result = DrawOrDispatchOverrideType::Replaced; // replaced prior
            }
            
            // state
            state_texb_drawn = true;

            // next (continues casting shadows)
            state = Silhouette;
            state_replacetoken_silhouette = true; // grant
            break;
         }
         // (multiple ShadowCast shader draws expected in between) ///////////////////////////
         case DepthExpand0: // ("main entry" is here, in a sense) (more accurately as gaussian horizontal blur)
         {
            // gatekeep: ps (PBR or NPR)
            ASSERT_MSG(ps == 0x54B1FF52 || ps == 0xCEDAC7C0, "ShadowMap: DepthExpand0 reached when ps not 0x54B1FF52 or 0xCEDAC7C0!");
            state_noreset_is_pbr = ps == 0x54B1FF52;

            // create resources
            Resources::CreateIfDirty(native_device);

            // not ready
            [[unlikely]] if (Resources::state == Resources::FindDepthDrawHandle) 
            {
               Resources::state = Resources::FindDepthUseHandle; // next

               // get SRV0 handle
               ComPtr<ID3D11ShaderResourceView> srv0;
               native_device_context->PSGetShaderResources(0, 1, srv0.put());
               ASSERT_MSG(srv0 != nullptr, "ShadowMap: SRV0 null DepthExpand0 FindDepthHandle");
               ComPtr<ID3D11Resource> res;
               srv0->GetResource(res.put());
               Resources::orig_depthdraw_handle = reinterpret_cast<uint64_t>(res.get()); // save

               // query SRV0 tex desc
               ComPtr<ID3D11Texture2D> tex;
               auto hr = res->QueryInterface(tex.put());
               ASSERT_MSG(SUCCEEDED(hr), "ShadowMap: QueryInterface srv0_texture DepthExpand0 FindDepthHandle");
               D3D11_TEXTURE2D_DESC tex_desc;
               tex->GetDesc(&tex_desc);
               Resources::depth_size_orig = tex_desc.Width; // save

               // confirm 2048x2048 or 1024x1024 R32F (99% confidence, but just be sure)
               [[unlikely]] if ((state_noreset_is_pbr && tex_desc.Width != 2048) || (!state_noreset_is_pbr && tex_desc.Width != 1024) || tex_desc.Height != tex_desc.Width || tex_desc.Format != DXGI_FORMAT_R32_TYPELESS)
               {
                  MessageBoxA(nullptr, "Unexpected behavior: Shadow Map is not 2048x2048 (PBR/FutureTone) or 1024x1024 (NPR/MegaMix)!\n\nAre you on highest graphics settings?\nIs there another mod that is already changing quality?\n\nNew Shadow Map features will now disable.", "Shadow Map", MB_OK | MB_ICONERROR);
                  Resources::Reset();
                  DisableCompletely();
                  break;
               }
            }

            // ready
            else if (Resources::IsOriginalSearchReady() && Resources::IsDepthNewReady())
            {
               // replace
               native_device_context->PSSetShaderResources(0, 1, &Resources::draw_depth_srv); // SRV0: depth draw
               native_device_context->OMSetRenderTargets(1, &Resources::draw_depth_aux_rtv, nullptr); // RTV0: depth aux
               native_device_context->PSSetShader(device_data.native_pixel_shaders.at(state_noreset_is_pbr ? CompileTimeStringHash(Shaders::Luma_MegaMix_ESMDepthGaussian_Default) : CompileTimeStringHash(Shaders::Luma_MegaMix_ESMDepthGaussian_NPRNormalize)).get(), nullptr, 0); // PS: higher quality depth gaussian blur

               // viewport to size
               D3D11_VIEWPORT viewport;
               viewport.TopLeftX = 0;
               viewport.TopLeftY = 0;
               viewport.Width  = Resources::depth_size_new;
               viewport.Height = Resources::depth_size_new;
               viewport.MinDepth = 0;
               viewport.MaxDepth = 1;
               native_device_context->RSSetViewports(1, &viewport);
            }

            // next
            state = DepthExpand1;
            break;
         }
         case DepthExpand1: // (more accurately as gaussian vertical blur)
         {
            // gatekeep: ps
            if (ps != 0x54B1FF52)
            {
               ASSERT_MSG(false, "ShadowMap: DepthExpand1 ps not 0x54B1FF52!?!?!");
               break;
            }

            // not ready
            [[unlikely]] if (Resources::state == Resources::FindDepthUseHandle)
            {
               Resources::state = Resources::FindDepthDownHandle; // next

               // get RTV0 handle
               ComPtr<ID3D11RenderTargetView> rtv0;
               native_device_context->OMGetRenderTargets(1, rtv0.put(), nullptr);
               ASSERT_MSG(rtv0 != nullptr, "ShadowMap: RTV0 null DepthExpand1 FindDepthUseHandle");
               ComPtr<ID3D11Resource> rtv0_res;
               rtv0->GetResource(rtv0_res.put());
               Resources::orig_depthuse_handle = reinterpret_cast<uint64_t>(rtv0_res.get());
            }

            // ready
            else if (Resources::IsOriginalSearchReady() && Resources::IsDepthNewReady())
            {
               // verify handle
               if (DEVELOPMENT)
               {
                  ComPtr<ID3D11RenderTargetView> rtv0;
                  native_device_context->OMGetRenderTargets(1, rtv0.put(), nullptr);
                  ASSERT_MSG(rtv0 != nullptr, "ShadowMap: RTV0 null DepthExpand1 IsReady");
                  
                  ComPtr<ID3D11Resource> rtv0_res;
                  rtv0->GetResource(rtv0_res.put());
                  
                  uint64_t rtv0_handle = reinterpret_cast<uint64_t>(rtv0_res.get());
                  ASSERT_MSG(rtv0_handle == Resources::orig_depthuse_handle, "ShadowMap: DepthExpand1 RTV0 handle does not match expected orig_depthuse_handle");
               }
               
               // replace
               native_device_context->PSSetShaderResources(0, 1, &Resources::draw_depth_aux_srv); // SRV0: depth aux
               native_device_context->OMSetRenderTargets(1, &Resources::draw_depth_use_rtv, nullptr); // RTV0: depth use (no more RTV use starting here! used as SRV19 later)
               native_device_context->PSSetShader(device_data.native_pixel_shaders.at(CompileTimeStringHash(Shaders::Luma_MegaMix_ESMDepthGaussian_Default)).get(), nullptr, 0); // PS: higher quality depth gaussian blur
            }

            // next
            state = DepthDownsample0;
            break;
         }
         case DepthDownsample0:
         {
            // gatekeep: ps
            if (ps != 0x5F8E8C7E)
            {
               ASSERT_MSG(false, "ShadowMap: DepthDownsample0 ps not 0x5F8E8C7E!?!?!");
               break;
            }

            // ready
            if (Resources::IsOriginalSearchReady() && Resources::IsDepthNewReady())
            {
               native_device_context->PSSetShaderResources(0, 1, &Resources::draw_depth_use_srv); // SRV0: depth use
            }

            // next
            state = DepthExpand2;
            break;
         }
         case DepthExpand2:
         {
            // gatekeep: ps
            if (ps != 0x723E27C4)
            {
               ASSERT_MSG(false, "ShadowMap: DepthExpand2 ps not 0x723E27C4!?!?!");
               break;
            }

            // not ready
            [[unlikely]] if (Resources::state == Resources::FindDepthDownHandle)
            {
               Resources::state = Resources::FindColor; // next

               // get RTV0 handle
               ComPtr<ID3D11RenderTargetView> rtv0;
               native_device_context->OMGetRenderTargets(1, rtv0.put(), nullptr);
               ASSERT_MSG(rtv0 != nullptr, "ShadowMap: RTV0 null DepthExpand2 FindDepthDownHandle");
               ComPtr<ID3D11Resource> rtv0_res;
               rtv0->GetResource(rtv0_res.put());
               Resources::orig_depthusedown_handle = reinterpret_cast<uint64_t>(rtv0_res.get());
            }

            // next
            state = TexADownsample0;
            break;
         }
         case TexADownsample0:
         {
            // gatekeep: ps
            if (ps != 0x58A712B5)
            {
               ASSERT_MSG(false, "ShadowMap: TexADownsample0 ps not 0x58A712B5!?!?!");
               break;
            }

            // not ready
            [[unlikely]] if (Resources::state == Resources::FindColor)
            {
               Resources::state = Resources::Done; // next

               // get RTV0 handle
               ComPtr<ID3D11RenderTargetView> rtv0;
               native_device_context->OMGetRenderTargets(1, rtv0.put(), nullptr);
               ASSERT_MSG(rtv0 != nullptr, "ShadowMap: RTV0 null WorldADownsample0 FindTexAHandle");
               ComPtr<ID3D11Resource> rtv0_resource;
               rtv0->GetResource(rtv0_resource.put());
               // Resources::orig_texdowna_handle = reinterpret_cast<uint64_t>(rtv0_resource.get()); // save

               // query RTV0 tex desc
               ComPtr<ID3D11Texture2D> tex;
               auto hr = rtv0_resource->QueryInterface(tex.put());
               ASSERT_MSG(SUCCEEDED(hr), "ShadowMap: QueryInterface rtv0_texture WorldADownsample0 FindTexAHandle");
               D3D11_TEXTURE2D_DESC tex_desc;
               tex->GetDesc(&tex_desc);
               Resources::tex_size_orig = tex_desc.Width; // save

               // confirm 512x512 RGBA8
               [[unlikely]] if (tex_desc.Width != 512 || tex_desc.Width != tex_desc.Height || tex_desc.Format != DXGI_FORMAT_R8G8B8A8_UNORM)
               {
                  MessageBoxA(nullptr, "Unexpected behavior: Shadow Map downsampled is not 512x512!\n\nAre you on highest graphics settings?\nIs there another mod that is already changing quality?\n\nNew Shadow Map features will now disable.", "Shadow Map", MB_OK | MB_ICONERROR);
                  Resources::Reset();
                  DisableCompletely();
                  break;
               }

               // get SRV0 handle
               ComPtr<ID3D11ShaderResourceView> srv0;
               native_device_context->PSGetShaderResources(0, 1, srv0.put());
               ASSERT_MSG(srv0 != nullptr, "ShadowMap: SRV0 null WorldADownsample0 FindTexAHandle");
               ComPtr<ID3D11Resource> srv0_resource;
               srv0->GetResource(srv0_resource.put());
               Resources::orig_tex_handle = reinterpret_cast<uint64_t>(srv0_resource.get());
            }

            // ready
            else if (Resources::IsOriginalSearchReady())
            {
               if (Resources::IsDepthNewReady())
               {
                  // replace
                  native_device_context->PSSetShaderResources(0, 1, &Resources::draw_color_srv); // SRV0: tex draw
               }

               if (Resources::IsColorNewReady())
               {
                  // The game can "random = funny" use either Tex A or B for downsample.
                  // We are consistent with Tex A & B, so we must detect original to flip accordingly.
                  if (state_texb_drawn)
                  {
                     // RTV0 handle
                     ComPtr<ID3D11RenderTargetView> rtv0;
                     native_device_context->OMGetRenderTargets(1, rtv0.put(), nullptr);
                     ASSERT_MSG(rtv0 != nullptr, "ShadowMap: RTV0 null WorldADownsample0 IsReady");
                     ComPtr<ID3D11Resource> rtv0_res;
                     rtv0->GetResource(rtv0_res.put());
                     uint64_t rtv0_handle = reinterpret_cast<uint64_t>(rtv0_res.get());

                     state_texdowna_rtv_handle = rtv0_handle;
                  }
                  
                  // replace with downsample gaussian pass
                  DrawWorldDownsamplePass(native_device, native_device_context, cmd_list_data, device_data, false);
                  result = DrawOrDispatchOverrideType::Replaced; // replaced

                  // bind aux for debug view (effective in capture next draw call)
                  if (DEVELOPMENT)
                  {
                     const std::array<ID3D11ShaderResourceView*, 9> srvs = {
                        Resources::color_aux_y_srvs[0], // has all mips
                        Resources::color_aux_x_srvs[0], Resources::color_aux_x_srvs[1], Resources::color_aux_x_srvs[2], Resources::color_aux_x_srvs[3], Resources::color_aux_x_srvs[4], Resources::color_aux_x_srvs[5], Resources::color_aux_x_srvs[6], Resources::color_aux_x_srvs[7]
                     };
                     native_device_context->PSSetShaderResources(64, srvs.size(), srvs.data());
                  }
               }
            }

            // next
            state = Tex4TapBlur0;
            break;
         }
         case Tex4TapBlur0:
         {
            // gatekeep: ps
            if (ps != 0x4E2A88FE)
            {
               ASSERT_ONCE_MSG(false, "ShadowMap: Tex4TapBlur0 ps not 0x4E2A88FE!?!?!");
               if (DEVELOPMENT) reshade::log::message(reshade::log::level::warning, std::format("ShadowMap: Tex4TapBlur0 ps not 0x4E2A88FE!?!?!. ps={:#010X}", ps).c_str());
               break;
            }

            // ready
            if (Resources::IsOriginalSearchReady() && Resources::IsColorNewReady())
            {
               result = DrawOrDispatchOverrideType::Replaced; // replaced prior
            }

            // next
            state = TexACopy0;
            break;
         }
         case TexACopy0:
         {
            // gatekeep: ps
            if (ps != 0x58A712B5)
            {
               ASSERT_MSG(false, "ShadowMap: TexACopy0 ps not 0x58A712B5!?!?!");
               break;
            }

            // ready
            if (Resources::IsOriginalSearchReady() && Resources::IsColorNewReady())
            {
               // generate mips
               native_device_context->GenerateMips(Resources::color_use_a_srv.get());
               if (state_texb_drawn) native_device_context->GenerateMips(Resources::color_use_b_srv.get());
               
               result = DrawOrDispatchOverrideType::Replaced; // replaced prior
            }

            // state use
            state_texa_drawn = true;

            // next
            state = SSSSetup;
            break;
         }
         case SSSSetup: 
         {
            // skip: to MainRender
            if (ps == 0x54415551 || !state_noreset_is_pbr) // SSS resolve shader drawn, or NPR no SSS
            {
               state = MainRender;
               break;
            }
            
            // gatekeep: ps
            if (!ShaderHashesLists::IsSSSSetupShader(ps)) break;

            // ready
            if (Resources::IsOriginalSearchReady() && Resources::IsDepthNewReady() && state_replacetoken_sss)
            {
               // verify SRV19-20 are expected handles (SRV6-7 are bound here too)
               if (DEVELOPMENT) // 100% confidence
               {
                  ComPtr<ID3D11ShaderResourceView> srv19;
                  native_device_context->PSGetShaderResources(19, 1, srv19.put());
                  ComPtr<ID3D11Resource> srv19_res;
                  srv19->GetResource(srv19_res.put());
                  uint64_t srv19_handle = reinterpret_cast<uint64_t>(srv19_res.get());
                  if (srv19_handle != Resources::orig_depthuse_handle)
                  {
                     ASSERT_ONCE_MSG(srv19_handle == Resources::orig_depthuse_handle, "ShadowMap: SSSSetup SRV19 handle does not match expected orig_depthuse_handle");
                     break;
                  }
               }

               // replace SRV 19
               state_replacetoken_sss = false; // consume
               native_device_context->PSSetShaderResources(19, 1, &Resources::draw_depth_use_srv);
            }
            
            break;
         }
         case MainRender:
         {
            // replace
            if (Resources::IsOriginalSearchReady())
            {
               // gatekeep: not used // TODO: instead of searching, register ReShade event for resource binding 
               ComPtr<ID3D11ShaderResourceView> srv19;
               native_device_context->PSGetShaderResources(19, 1, srv19.put());
               if (!srv19) break; // failed: null
               
               ComPtr<ID3D11Resource> srv19_res;
               srv19->GetResource(srv19_res.put());
               uint64_t srv19_handle = reinterpret_cast<uint64_t>(srv19_res.get());
               if (srv19_handle != Resources::orig_depthuse_handle) break; // failed: not expected handle
               
               if (Resources::IsColorNewReady())
               {
                  if (state_texa_drawn && !state_texb_drawn)
                  {
                     native_device_context->PSSetShaderResources(6, 1, &Resources::color_use_a_srv); // set A like normal
                  }
                  else if (state_texa_drawn && state_texb_drawn)
                  {
                     // SRV6 handle
                     ComPtr<ID3D11ShaderResourceView> srv6;
                     native_device_context->PSGetShaderResources(6, 1, srv6.put());
                     ComPtr<ID3D11Resource> srv6_res;
                     srv6->GetResource(srv6_res.put());
                     uint64_t srv6_handle = reinterpret_cast<uint64_t>(srv6_res.get());
                     
                     bool is_flipped = srv6_handle != state_texdowna_rtv_handle;
                     const std::array<ID3D11ShaderResourceView*, 2> srvs = {
                        !is_flipped ? Resources::color_use_a_srv.get() : Resources::color_use_b_srv.get(),
                        !is_flipped ? Resources::color_use_b_srv.get() : Resources::color_use_a_srv.get()
                     };
                     native_device_context->PSSetShaderResources(6, srvs.size(), srvs.data());
                  }
                  else if (!state_texa_drawn && state_texb_drawn)
                  {
                     ASSERT_MSG(false, "ShadowMap: MainRender impossible TexB use but not TexA.");
                  }
               }
               if (Resources::IsDepthNewReady())
               {
                  // replace SRV 19
                  native_device_context->PSSetShaderResources(19, 1, &Resources::draw_depth_use_srv);
               }
               
               // next
               state = Done;
            }
            
            break;
         }
         case Done:
         case Unused:
         default: break;
      }

      return result;
   }

   bool OnClearRenderTargetViewMegaMix(uint32_t rtv_handle)
   {
      bool is_prevent = false; // result

      // gatekeeps
      if (!IsEnabled()) return is_prevent;
      if (DEVELOPMENT && !IsModEnabled()) return is_prevent;

      // relevant?
      bool is_cleared_original_color = rtv_handle == Resources::orig_tex_handle;
      
      // grant color clear token (depth is not cleared)
      if (is_cleared_original_color && state_silhouette_draw_count > 0)
      {
         ASSERT_ONCE_MSG(!state_silhouette_clear_token_color_granted_this_frame, "ShadowMap: OnClearRenderTargetViewMegaMix already grant clear."); // max of 1 extra clear
         state_silhouette_clear_token_color = true; // grant
         state_silhouette_clear_token_color_granted_this_frame = true; // state
         state_replacetoken_silhouette = true; // grant token again (especially if TexB is never used, skipping its grant)
         is_prevent = true; // dont need to waste
      }

      return is_prevent;
   }

   void OnPresent()
   {
      if (!IsEnabled()) return;
      ResetState();
   }

   void HardReset()
   {
      Resources::Reset();
   }
   
   void OnInit()
   {
      Shaders::OnInit();
   }

   void OnLoad(reshade::api::effect_runtime* runtime)
   {
      reshade::get_config_value(runtime, NAME, reshadesave_enabled_color_mips, enabled_color_mips);
      reshade::get_config_value(runtime, NAME, reshadesave_color_mips_skip, color_mips_skip);
   }
}

namespace DuplicateWorldReflections
{
   const std::unordered_set<uint32_t> draw_shaders = {
      0x208EEA46,
      0x8C19842C,
      0x74805C84,
      // 0x4579859D, // default skin shader (PBR)
      // 0xA263A14B, // default skin shader (no SSS) (NPR)
   };

   // 512x256 rgba16f (but alpha is a waste!)
   // (doesnt have 0x58A712B5 before!)
   // 0x4E2A88FE: 4 taps
   // 0x58A712B5: copy with multiplier
   // (used as SRV15)

   constexpr const char* reshadesave_enabled = "DuplicateWorldReflectionsEnabled";
   bool enabled = false;

   constexpr const char* reshadesave_blur_mips = "DuplicateWorldReflectionsBlurMips";
   constexpr int blur_mips_def = 2;
   int blur_mips = blur_mips_def;

   constexpr const char* reshadesave_desired_resolution_x = "DuplicateWorldReflectionsDesiredResolutionX";
   constexpr const char* reshadesave_desired_resolution_y = "DuplicateWorldReflectionsDesiredResolutionY";
   constexpr uint2 desired_resolution_def = {512, 256};
   uint2 desired_resolution = desired_resolution_def;

   constexpr const char* reshadesave_override_full_resolution = "DuplicateWorldReflectionsOverrideFullResolution";
   constexpr bool override_full_resolution_def = true;
   bool override_full_resolution = override_full_resolution_def; // X Song Pack 100% res duplicate world reflections
   
   enum State : uint8_t
   {
      Wait, // waits for SSS if PBR
      Draw, // replace draw_shaders with our resource and viewport
      AfterDraw, 
      Blur, // 4 tap blur, bypassed for our own downsample, accounting g_color_scale & g_color_offset
      Copy, // copies back to original resource, bypassed for our own upsample, accounting for g_color_scale
      Use, // scan for SRV15 usage to replace with our resource
      Done,
   };
   State state = Wait; // denotes what comes next
   bool state_dof = false; // if true, dof is started and needs to be skipped
   void ResetState()
   {
      ASSERT_MSG(state != Use || *MemoryHack::addr_pvTimeSec <= 1, "DuplicateWorldReflections: ResetState() called while in Use state!"); // meaning: setup without any usage???
      //TODO: if above keeps happening (it doesnt), then invalidate handles, especially if unverified
      state = Wait;
      state_dof = false;
   }

   namespace Shaders
   {
      constexpr const char* Luma_MegaMix_DuplicateWorldReflections = "Luma_MegaMix_DuplicateWorldReflections"; // file
      constexpr const char* Luma_MegaMix_DuplicateWorldReflections_MainVS = "Luma_MegaMix_DuplicateWorldReflections_MainVS"; // shader
      constexpr const char* Luma_MegaMix_DuplicateWorldReflections_Down = "Luma_MegaMix_DuplicateWorldReflections_Down"; // shader
      constexpr const char* Luma_MegaMix_DuplicateWorldReflections_DownFilter = "Luma_MegaMix_DuplicateWorldReflections_DownFilter"; // shader (Blur's g_color_scale & g_color_offset)
      constexpr const char* Luma_MegaMix_DuplicateWorldReflections_UpFilter = "Luma_MegaMix_DuplicateWorldReflections_UpFilter"; // shader (Copy's g_color_scale)
      constexpr const char* Luma_MegaMix_DuplicateWorldReflections_Up = "Luma_MegaMix_DuplicateWorldReflections_Up"; // shader
      
      void OnInit()
      {
         native_shaders_definitions.emplace(CompileTimeStringHash(Luma_MegaMix_DuplicateWorldReflections_MainVS), ShaderDefinition{ Luma_MegaMix_DuplicateWorldReflections, reshade::api::pipeline_subobject_type::vertex_shader, nullptr, "MainVS", {{}}});
         native_shaders_definitions.emplace(CompileTimeStringHash(Luma_MegaMix_DuplicateWorldReflections_Down), ShaderDefinition{ Luma_MegaMix_DuplicateWorldReflections, reshade::api::pipeline_subobject_type::pixel_shader, nullptr, "Down", {{}}});
         native_shaders_definitions.emplace(CompileTimeStringHash(Luma_MegaMix_DuplicateWorldReflections_DownFilter), ShaderDefinition{ Luma_MegaMix_DuplicateWorldReflections, reshade::api::pipeline_subobject_type::pixel_shader,nullptr, "DownFilter", {{}}});
         native_shaders_definitions.emplace(CompileTimeStringHash(Luma_MegaMix_DuplicateWorldReflections_UpFilter), ShaderDefinition{ Luma_MegaMix_DuplicateWorldReflections, reshade::api::pipeline_subobject_type::pixel_shader, nullptr, "UpFilter", {{}}});
         native_shaders_definitions.emplace(CompileTimeStringHash(Luma_MegaMix_DuplicateWorldReflections_Up), ShaderDefinition{ Luma_MegaMix_DuplicateWorldReflections, reshade::api::pipeline_subobject_type::pixel_shader, nullptr, "Up", {{}}});
      }
   }

   namespace Resources
   {
      bool IsSizeOriginalDownsample(uint2 resolution) { return resolution.x == 512 && resolution.y == 256; }
      bool IsSizeFullResolution(uint2 resolution) { return resolution.x == RenderTracker::res_main_color_size.x && resolution.y == RenderTracker::res_main_color_size.y; }
      bool IsSizeEitherValid(uint2 resolution) { return IsSizeOriginalDownsample(resolution) || IsSizeFullResolution(resolution); }
      
      struct HandleItem
      {
         bool is_verified = false;
         uint64_t handle = 0;
         bool is_full_resolution = false;
         
         bool IsValid() { return handle != 0; }
         
         void Reset()
         {
            is_verified = false;
            handle = 0;
            is_full_resolution = false;
         }
      };

      std::array<HandleItem, 2> handles = {};
      HandleItem* handles_current = nullptr;

      HandleItem Handles_Get(bool i) {return handles[i];}

      HandleItem* Handles_Find(uint64_t handle)
      {
         for (auto& item : handles) if (item.handle == handle) return &item;
         return nullptr;
      }

      HandleItem* Handles_CreateCandidate(uint64_t handle, uint2 size, ID3D11Texture2D* color_tex, ID3D11Device* native_device)
      {
         HandleItem* result = nullptr;
         for (auto& item : handles)
            if (!item.is_verified)
               result = &item;

         [[unlikely]] if (!result)
         {
            ASSERT_MSG(false, "DuplicateWorldReflections: Handles_Create() failed to find a free slot!");
            for (HandleItem& item : handles) item.Reset();
            result = &handles[0];
         }
         
         result->is_verified = false;
         result->handle = handle;
         result->is_full_resolution = IsSizeFullResolution(size);
         
         return result;
      }
      
      void Reset_Handles()
      {
         for (HandleItem& item : handles) item.Reset();
         Resources::handles_current = nullptr;
      }

      uint2 effective_resolution = {0,0};
      bool IsReadyResource() { return effective_resolution.x > 0; }
      uint effective_mips = 0;
      uint2 x_mip0 = {0,0};
      uint2 y_mip0 = {0,0};
      bool is_dirty_token = true;

      ComPtr<ID3D11DepthStencilView> dsv;

      ComPtr<ID3D11ShaderResourceView> srv;
      ComPtr<ID3D11RenderTargetView>   rtv;
      
      std::vector<ID3D11ShaderResourceView*> srvs_x(8);
      std::vector<ID3D11RenderTargetView*>   rtvs_x(8);
      
      std::vector<ID3D11ShaderResourceView*> srvs_y(8);
      std::vector<ID3D11RenderTargetView*>   rtvs_y(8);

      std::array<uint2, 8> viewport_y_sizes = {};

      struct alignas(16) CBMegaMixDuplicateWorldReflections
      {
         float2 src_size;
         float2 inv_src_size;
         float2 axis;
      };
      ComPtr<ID3D11Buffer> cb;
      CBMegaMixDuplicateWorldReflections cb_data = {};
      void CBUpdate(ID3D11DeviceContext* native_device_context)
      {
         D3D11_MAPPED_SUBRESOURCE mapped_subresource;
         auto hr = native_device_context->Map(cb.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_subresource);
         ASSERT_MSG(SUCCEEDED(hr), "CBUpdate: Map() failed");
         std::memcpy(mapped_subresource.pData, &cb_data, sizeof(CBMegaMixDuplicateWorldReflections));
         native_device_context->Unmap(cb.get(), 0);
      }
      
      ComPtr<ID3D11BlendState> blendstate;

      void Reset_Resources()
      {
         effective_resolution = {0,0};
         dsv = nullptr;
         ResetCOMArray(srvs_x); ResetCOMArray(rtvs_x);
         ResetCOMArray(srvs_y); ResetCOMArray(rtvs_y);
      }

      void Reset()
      {
         is_dirty_token = true; // grant
         Reset_Handles();
         Reset_Resources();
      }

      void CreateOrRecreateIfDirty(ID3D11Device* native_device, HandleItem* item)
      {
         ASSERT_MSG(item, "DuplicateWorldReflections: CreateIfDirty() called with null item!");
         
         // gatekeep: token
         [[likely]] if (!is_dirty_token) return;
         is_dirty_token = false;
         
         static uint2 effective_resolution_prev = {0,0};
         uint2 effective_resolution_new = desired_resolution; // resolution
         effective_resolution_new.x = max(16u, effective_resolution_new.x);
         effective_resolution_new.y = max(16u, effective_resolution_new.y);

         static uint8_t effective_mips_prev = 0; // mips count
         uint8_t effective_mips_new = blur_mips;
         effective_mips_new = min(effective_mips_new, srvs_x.size());
         effective_mips_new = max(1, effective_mips_new);

         // dirty?
         bool is_dirty = !IsReadyResource()
         || effective_resolution_new != effective_resolution_prev
         || effective_mips_new != effective_mips_prev;

         // save to prev
         effective_resolution_prev = effective_resolution_new;
         effective_mips_prev = effective_mips_new;

         // gatekeep: dirty
         if (!is_dirty) return;

         // clear
         Reset_Resources();
         ASSERT_MSG(!IsReadyResource(), "DuplicateWorldReflections: ready after reset!?");
         effective_resolution = effective_resolution_new;
         effective_mips = effective_mips_new;

         // create setup
         ComPtr<ID3D11Texture2D> tex;
         
         D3D11_TEXTURE2D_DESC tex_desc = {};
         tex_desc.Width = effective_resolution.x;
         tex_desc.Height = effective_resolution.y;
         tex_desc.ArraySize = 1;
         tex_desc.SampleDesc.Count = 1;
         tex_desc.Usage = D3D11_USAGE_DEFAULT;
         tex_desc.MiscFlags = 0;

         // depth
         tex_desc.Format = DXGI_FORMAT_R32_TYPELESS;
         tex_desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
         tex_desc.MipLevels = 1;

         auto hr0 = native_device->CreateTexture2D(&tex_desc, nullptr, tex.put());
         ASSERT_MSG(SUCCEEDED(hr0), "DuplicateWorldReflections: Create hr0");

         D3D11_DEPTH_STENCIL_VIEW_DESC dsv_desc = {};
         dsv_desc.Format = DXGI_FORMAT_D32_FLOAT;
         dsv_desc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
         dsv_desc.Flags = 0;
         dsv_desc.Texture2D.MipSlice = 0;
         auto hr1 = native_device->CreateDepthStencilView(tex.get(), &dsv_desc, dsv.put());
         ASSERT_MSG(SUCCEEDED(hr1), "DuplicateWorldReflections: Create hr1");

         // color  
         tex_desc.Format = DXGI_FORMAT_R11G11B10_FLOAT;
         tex_desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
         tex_desc.MipLevels = 1;

         auto hr2 = native_device->CreateTexture2D(&tex_desc, nullptr, tex.put());
         ASSERT_MSG(SUCCEEDED(hr2), "DuplicateWorldReflections: Create hr2");
         auto hr3 = native_device->CreateShaderResourceView(tex.get(), nullptr, srv.put());
         ASSERT_MSG(SUCCEEDED(hr3), "DuplicateWorldReflections: Create hr3");
         auto hr4 = native_device->CreateRenderTargetView(tex.get(), nullptr, rtv.put());
         ASSERT_MSG(SUCCEEDED(hr4), "DuplicateWorldReflections: Create hr4");

         // color mips setup
         tex_desc.Format = DXGI_FORMAT_R11G11B10_FLOAT;
         tex_desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
         tex_desc.MipLevels = effective_mips;

         x_mip0.x = effective_resolution.x / 2; // size / 2 (x pass is first)
         x_mip0.y = effective_resolution.y;     // size 
         
         y_mip0.x = effective_resolution.x / 2; // size / 2
         y_mip0.y = effective_resolution.y / 2; // size / 2

         // color y (mipmap chain)
         tex_desc.Width = y_mip0.x;
         tex_desc.Height = y_mip0.y;

         auto hr5 = native_device->CreateTexture2D(&tex_desc, nullptr, tex.put());
         ASSERT_MSG(SUCCEEDED(hr5), "DuplicateWorldReflections: Create hr5");

         D3D11_RENDER_TARGET_VIEW_DESC rtv_desc = {};
         rtv_desc.Format = tex_desc.Format;
         rtv_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
         
         D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
         srv_desc.Format = tex_desc.Format;
         srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
         srv_desc.Texture2D.MipLevels = 1; // allow only 1 mip to get accessed 

         for (int i = 0; i < effective_mips; i++)
         {
            rtv_desc.Texture2D.MipSlice = i;
            auto hr6 = native_device->CreateRenderTargetView(tex.get(), &rtv_desc, &rtvs_y[i]);
            ASSERT_MSG(SUCCEEDED(hr6), "DuplicateWorldReflections: Create hr6");
                     
            srv_desc.Texture2D.MostDetailedMip = i;
            auto hr7 = native_device->CreateShaderResourceView(tex.get(), &srv_desc, &srvs_y[i]);
            ASSERT_MSG(SUCCEEDED(hr7), "DuplicateWorldReflections: Create hr7");
         }

         // color x (needs individual tex)
         tex_desc.Width = x_mip0.x;
         tex_desc.Height = x_mip0.y;
         tex_desc.MipLevels = 1;

         auto hr8 = native_device->CreateTexture2D(&tex_desc, nullptr, tex.put());
         ASSERT_MSG(SUCCEEDED(hr8), "DuplicateWorldReflections: Create hr8");
         auto hr9 = native_device->CreateShaderResourceView(tex.get(), nullptr, &srvs_x[0]);
         ASSERT_MSG(SUCCEEDED(hr9), "DuplicateWorldReflections: Create hr9");
         auto hr10 = native_device->CreateRenderTargetView(tex.get(), nullptr, &rtvs_x[0]);
         ASSERT_MSG(SUCCEEDED(hr10), "DuplicateWorldReflections: Create hr10");

         for (UINT i = 1; i < effective_mips; i++)
         {
            tex_desc.Width  = max(1u, x_mip0.x >> i);
            tex_desc.Height = max(1u, x_mip0.y >> i);

            auto hr11 = native_device->CreateTexture2D(&tex_desc, nullptr, tex.put());
            ASSERT_MSG(SUCCEEDED(hr11), "DuplicateWorldReflections: Create hr11");
            auto hr12 = native_device->CreateRenderTargetView(tex.get(), nullptr, &rtvs_x[i]);
            ASSERT_MSG(SUCCEEDED(hr12), "DuplicateWorldReflections: Create hr12");
            auto hr13 = native_device->CreateShaderResourceView(tex.get(), nullptr, &srvs_x[i]);
            ASSERT_MSG(SUCCEEDED(hr13), "DuplicateWorldReflections: Create hr13");
         }

         // cb
         [[unlikely]] if (!cb)
         {
            D3D11_BUFFER_DESC buffer_desc = {};
            buffer_desc.ByteWidth = sizeof(CBMegaMixDuplicateWorldReflections);
            buffer_desc.Usage = D3D11_USAGE_DYNAMIC;
            buffer_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
               
            auto hr14 = native_device->CreateBuffer(&buffer_desc, nullptr, cb.put());
            ASSERT_MSG(SUCCEEDED(hr14), "DuplicateWorldReflections: Create hr14");
         }

         // blend
         [[unlikely]] if (!blendstate)
         {
            CD3D11_BLEND_DESC blend_desc(D3D11_DEFAULT);
            blend_desc.RenderTarget[0].BlendEnable = TRUE;
            blend_desc.RenderTarget[0].SrcBlend = D3D11_BLEND_BLEND_FACTOR;
            blend_desc.RenderTarget[0].DestBlend = D3D11_BLEND_BLEND_FACTOR;
               
            auto hr15 = native_device->CreateBlendState(&blend_desc, blendstate.put());
            ASSERT_MSG(SUCCEEDED(hr15), "DuplicateWorldReflections: Create hr15");
         }

         ASSERT_MSG(IsReadyResource(), "DuplicateWorldReflections: not ready after create!?");
         reshade::log::message(reshade::log::level::info, std::format("DuplicateWorldReflections: Created resources {}x{}, {} mips", effective_resolution.x, effective_resolution.y, effective_mips).c_str());
      }

      void DrawDownsample(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, DeviceData& device_data, HandleItem* handle_item)
      {
         // 0th Downsample: X
         D3D11_VIEWPORT viewport_x = {};
         viewport_x.Width = x_mip0.x;
         viewport_x.Height = x_mip0.y;

         cb_data.src_size = float2(effective_resolution.x, effective_resolution.y);
         cb_data.inv_src_size = float2(1.0f / cb_data.src_size.x, 1.0f / cb_data.src_size.y);
         cb_data.axis = float2(1.0f, 0.0f);
         // cb_data.sigma = sigma;
         CBUpdate(native_device_context);

         native_device_context->OMSetRenderTargets(1, &Resources::rtvs_x[0], nullptr);
         native_device_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
         native_device_context->VSSetShader(device_data.native_vertex_shaders.at(CompileTimeStringHash(Shaders::Luma_MegaMix_DuplicateWorldReflections_MainVS)).get(), nullptr, 0); // same for the rest
         native_device_context->PSSetShader(device_data.native_pixel_shaders.at(CompileTimeStringHash(Shaders::Luma_MegaMix_DuplicateWorldReflections_Down)).get(), nullptr, 0);
         native_device_context->PSSetConstantBuffers(11, 1, &cb);
         const std::array ps_samplers = { device_data.sampler_state_linear.get() };
         native_device_context->PSSetSamplers(0, ps_samplers.size(), ps_samplers.data());
         native_device_context->PSSetShaderResources(0, 1, &srv);
         native_device_context->RSSetViewports(1, &viewport_x);
         native_device_context->RSSetState(nullptr);

         native_device_context->Draw(3, 0);
         
         // 0th Downsample: Y
         D3D11_VIEWPORT viewport_y = {};
         viewport_y.Width  = y_mip0.x;
         viewport_y.Height = y_mip0.y;
         viewport_y_sizes[0] = y_mip0; // save for upsample later
      
         cb_data.src_size = float2(viewport_y.Width, viewport_y.Height);
         cb_data.inv_src_size = float2(1.0f / cb_data.src_size.x, 1.0f / cb_data.src_size.y);
         cb_data.axis = float2(0.0f, 1.0f);
         CBUpdate(native_device_context);
      
         native_device_context->OMSetRenderTargets(1, &rtvs_y[0], nullptr);
         native_device_context->PSSetShaderResources(0, 1, &srvs_x[0]);
         native_device_context->RSSetViewports(1, &viewport_y);
      
         native_device_context->Draw(3, 0);

         // Downsample
         for (uint i = 1; i < effective_mips; i++)
         {
            bool is_last = i + 1 >= effective_mips;
            
            // X
            viewport_x.Width  = max(1u, x_mip0.x >> i);
            viewport_x.Height = max(1u, x_mip0.y >> i);
         
            cb_data.src_size = float2(viewport_y_sizes[i - 1].x, viewport_y_sizes[i - 1].y);
            cb_data.axis = float2(1.0f, 0.0f);
            cb_data.inv_src_size = float2(1.0f / cb_data.src_size.x, 1.0f / cb_data.src_size.y);
            // cb_data.sigma += color_sigma_increase;
            CBUpdate(native_device_context);
         
            native_device_context->OMSetRenderTargets(1, &rtvs_x[i], nullptr);
            native_device_context->PSSetShaderResources(0, 1, &srvs_y[i - 1]);
            native_device_context->RSSetViewports(1, &viewport_x);
          
            native_device_context->Draw(3, 0);

            // Y
            viewport_y_sizes[i].x = max(1u, y_mip0.x >> i);
            viewport_y_sizes[i].y = max(1u, y_mip0.y >> i);
            viewport_y.Width  = viewport_y_sizes[i].x;
            viewport_y.Height = viewport_y_sizes[i].y;

            cb_data.src_size = float2(viewport_x.Width, viewport_x.Height);
            cb_data.axis = float2(0.0f, 1.0f);
            cb_data.inv_src_size = float2(1.0f / cb_data.src_size.x, 1.0f / cb_data.src_size.y);
            CBUpdate(native_device_context);

            if (is_last) native_device_context->PSSetShader(device_data.native_pixel_shaders.at(CompileTimeStringHash(Shaders::Luma_MegaMix_DuplicateWorldReflections_DownFilter)).get(), nullptr, 0); // filter
            native_device_context->OMSetRenderTargets(1, &rtvs_y[i], nullptr);
            native_device_context->PSSetShaderResources(0, 1, &srvs_x[i]);
            native_device_context->RSSetViewports(1, &viewport_y);

            native_device_context->Draw(3, 0);
         }
      }

      void DrawUpsample(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, DeviceData& device_data)
      {
         native_device_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
         native_device_context->VSSetShader(device_data.native_vertex_shaders.at(CompileTimeStringHash(Shaders::Luma_MegaMix_DuplicateWorldReflections_MainVS)).get(), nullptr, 0); // same for the rest
         native_device_context->PSSetShader(device_data.native_pixel_shaders.at(CompileTimeStringHash(Shaders::Luma_MegaMix_DuplicateWorldReflections_UpFilter)).get(), nullptr, 0);
         // native_device_context->PSSetConstantBuffers(11, 1, &cb);
         // const std::array ps_samplers = { device_data.sampler_state_linear.get() };
         // native_device_context->PSSetSamplers(0, ps_samplers.size(), ps_samplers.data());

         D3D11_VIEWPORT viewport_y = {};
         constexpr FLOAT blend_factor_half[] = { 0.5f, 0.5f, 0.5f, 0.5f };
         for (int i = effective_mips - 1; i > 0; i--)
         {
            bool is_2nd_draw = (effective_mips - 1 - 1) == i;
            
            viewport_y.Width  = viewport_y_sizes[i - 1].x;
            viewport_y.Height = viewport_y_sizes[i - 1].y;
      
            cb_data.src_size = float2(viewport_y_sizes[i].x, viewport_y_sizes[i].y);
            cb_data.inv_src_size = float2(1.0f / cb_data.src_size.x, 1.0f / cb_data.src_size.y);
            CBUpdate(native_device_context);
      
            if (is_2nd_draw) native_device_context->PSSetShader(device_data.native_pixel_shaders.at(CompileTimeStringHash(Shaders::Luma_MegaMix_DuplicateWorldReflections_Up)).get(), nullptr, 0); // filter only once
            native_device_context->OMSetRenderTargets(1, &rtvs_y[i - 1], nullptr);
            native_device_context->PSSetShaderResources(0, 1, &srvs_y[i]);
            native_device_context->RSSetViewports(1, &viewport_y);
            native_device_context->OMSetBlendState(blendstate.get(), blend_factor_half, UINT_MAX);
      
            native_device_context->Draw(3, 0);
         }

         // restore (mainly for rare WaterWavesNormals0)
         constexpr FLOAT blend_factor_1[] = { 1.0f, 1.0f, 1.0f, 1.0f };
         native_device_context->OMSetBlendState(nullptr, blend_factor_1, UINT_MAX); // most likely not needed, but just in case
         native_device_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP); // revert
      }
   }
   
   DrawOrDispatchOverrideType OnDrawOrDispatchOverride(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, uint32_t ps)
   {
      DrawOrDispatchOverrideType result = DrawOrDispatchOverrideType::None;

      // gatekeeps:
      if (!enabled) return result;
      if (DEVELOPMENT && !IsModEnabled()) return result;
      if (!RenderTracker::IsMainColorBeingOrHasBeenDrawnTo()) return result; // reflections are always drawn after SSS

      // force state: stop waiting
      if (state == Wait && RenderTracker::IsMainColorBeingOrHasBeenDrawnTo()) state = Draw;
      // force state: relevant (Copy wasn't prev) 4Tap Blur draw
      if (ps == 0x4E2A88FE && RenderTracker::ps_prev != 0x58A712B5) state = Blur;
      
      switch (state) // denotes next expected
      {
         case Wait:
            break;
         case Draw:
         {
            // gatekeeps:
            if (!Resources::handles_current || !Resources::handles_current->is_verified) break; // no / unverified item
            if (!Resources::IsReadyResource()) break; // new resources not created

            // tokens
            bool is_replace_token = false;
            
            // is_replace_token: via draw shader check (only for 512x256 as it uses low quality variants)
            if (/*!is_replace_token &&*/ !Resources::handles_current->is_full_resolution)
               is_replace_token = draw_shaders.contains(ps); // grant?

            // is_replace_token: via RTV handle check (full resolution required)
            if (!is_replace_token)
            {
               ComPtr<ID3D11RenderTargetView> rtv;
               native_device_context->OMGetRenderTargets(1, rtv.put(), nullptr);
               if (!rtv) goto AfterRTVHandleCheck; // failed: null

               ComPtr<ID3D11Resource> res;
               rtv->GetResource(res.put());
               uint64_t handle = reinterpret_cast<uint64_t>(res.get());
               
               is_replace_token = Resources::handles_current->handle == handle; // grant?
            }
            AfterRTVHandleCheck:

            // replace?
            if (is_replace_token)
            {
               // clear
               constexpr float clear_color[4] = {0,0,0,0}; // always
               native_device_context->ClearRenderTargetView(Resources::rtv.get(), clear_color);
               native_device_context->ClearDepthStencilView(Resources::dsv.get(), D3D11_CLEAR_DEPTH, 0.0f, 0);
               
               // replace RTV & DSV
               native_device_context->OMSetRenderTargets(1, &Resources::rtv, Resources::dsv.get());

               // verify viewport size
               if (DEVELOPMENT)
               {
                  D3D11_VIEWPORT viewport;
                  UINT num_viewports = 1;
                  native_device_context->RSGetViewports(&num_viewports, &viewport);
                  uint2 expected_res = Resources::handles_current->is_full_resolution ? RenderTracker::res_main_color_size : uint2{512, 256};
                  ASSERT_MSG(uint2(viewport.Width, viewport.Height) == expected_res, "DuplicateWorldReflections: Viewport size not expected!");
               }
               
               // replace viewport
               ASSERT_MSG(Resources::effective_resolution.x > 0 && Resources::effective_resolution.y > 0, "DuplicateWorldReflections: effective_resolution not valid");
               D3D11_VIEWPORT viewport;
               viewport.TopLeftX = 0;
               viewport.TopLeftY = 0;
               viewport.Width  = Resources::effective_resolution.x;
               viewport.Height = Resources::effective_resolution.y;
               viewport.MinDepth = 0;
               viewport.MaxDepth = 1;
               native_device_context->RSSetViewports(1, &viewport);

               // next
               state = AfterDraw;
            }
            
            break;
         }
         case AfterDraw:
         {
            // full resolution: skip wasteful DoF
            if (Resources::handles_current->is_full_resolution)
            {
               if (!state_dof && ShaderHashesLists::DepthOfFieldShaders.contains(ps)) state_dof = true; // DoF started
               if (state_dof) result = DrawOrDispatchOverrideType::Skip; // skip DoF
            }
            break;
         }
         case Blur:
         {
            // SRV0
            ComPtr<ID3D11ShaderResourceView> srv0;
            native_device_context->PSGetShaderResources(0, 1, srv0.put());
            ASSERT_MSG(srv0, "DuplicateWorldReflections: srv0 null Blur");

            ComPtr<ID3D11Resource> res;
            srv0->GetResource(res.put());
            uint64_t handle = reinterpret_cast<uint64_t>(res.get());

            // dirty handle?
            [[unlikely]] if (Resources::handles_current && Resources::handles_current->handle != handle)
               Resources::handles_current = Resources::Handles_Find(handle);

            // create candidate?
            [[unlikely]] if (!Resources::handles_current)
            {
               // TEX
               ComPtr<ID3D11Texture2D> tex;
               auto hr = res->QueryInterface(tex.put());
               ASSERT_MSG(SUCCEEDED(hr), "DuplicateWorldReflections: QueryInterface rtv_texture Blur");
               D3D11_TEXTURE2D_DESC tex_desc;
               tex->GetDesc(&tex_desc);

               // failed: bad size
               [[unlikely]] if (!Resources::IsSizeEitherValid(uint2(tex_desc.Width, tex_desc.Height)))
               {
                  ASSERT_MSG(false, "DuplicateWorldReflections: RTV size not valid Blur");
                  Resources::handles_current = nullptr; // reset
                  break;
               }

               // create
               Resources::handles_current = Resources::Handles_CreateCandidate(handle, {tex_desc.Width, tex_desc.Height}, tex.get(), native_device);
               reshade::log::message(reshade::log::level::info, std::format("DuplicateWorldReflections: Creating new candidate! 0x{:016X} {}x{}", handle, tex_desc.Width, tex_desc.Height).c_str());
            }
            ASSERT_MSG(Resources::handles_current, "DuplicateWorldReflections: Blur, current_item null");
            ASSERT_MSG(Resources::handles_current->IsValid(), "DuplicateWorldReflections: Blur, current_item not valid");
            
            // try create resources
            Resources::CreateOrRecreateIfDirty(native_device, Resources::handles_current);

            // ready
            if (Resources::handles_current->is_verified && Resources::IsReadyResource())
            {
               Resources::DrawDownsample(native_device, native_device_context, device_data, Resources::handles_current);
               result = DrawOrDispatchOverrideType::Skip;
            }

            // next
            state = Copy;
            break;
         }
         case Copy:
         {
            ASSERT_MSG(ps == 0x58A712B5, "DuplicateWorldReflections: Unexpected, Copy shader 0x58A712B5 not next!"); // this is always next
            ASSERT_MSG(Resources::handles_current->IsValid(), "DuplicateWorldReflections: Copy, current_item not valid");

            // upsample
            if (Resources::handles_current->is_verified && Resources::IsReadyResource())
            {
               Resources::DrawUpsample(native_device, native_device_context, device_data);
               result = DrawOrDispatchOverrideType::Skip;
            }

            state = Use;
            break;
         }
         case Use:
         {
            ASSERT_MSG(Resources::handles_current->IsValid(), "DuplicateWorldReflections: Use, current_item not valid");

            // force fail: full resolution but not allowed/overriding
            if (Resources::handles_current->is_full_resolution && !override_full_resolution)
            {
               Resources::handles_current->is_verified = false; // always invalidate
               state = Done; // next
               break;
            }
            
            // scan SRV15 usage to replace with our resource
            ComPtr<ID3D11ShaderResourceView> srv15;
            native_device_context->PSGetShaderResources(15, 1, srv15.put());
            if (!srv15) break; // failed: null
            
            ComPtr<ID3D11Resource> srv15_res;
            srv15->GetResource(srv15_res.put());
            
            uint64_t srv15_handle = reinterpret_cast<uint64_t>(srv15_res.get());

            // verify if not yet
            [[unlikely]] if (!Resources::handles_current->is_verified)
            {
               if (Resources::handles_current->handle == srv15_handle)
               {
                  Resources::handles_current->is_verified = true;
                  reshade::log::message(reshade::log::level::info, std::format("DuplicateWorldReflections: Verified! Is Full Resolution? {}", Resources::handles_current->is_full_resolution).c_str());
               }
            }
            // use
            else [[likely]]
            {
               native_device_context->PSSetShaderResources(15, 1, &Resources::srvs_y[0]);
               state = Done;
               
               // bind SRVs for debug
               if (DEVELOPMENT)
               {
                  const std::array<ID3D11ShaderResourceView*, 9> srvs_debug = {
                     Resources::srvs_y[0], // mipchain
                     Resources::srvs_x[0], Resources::srvs_x[1], Resources::srvs_x[2], Resources::srvs_x[3], Resources::srvs_x[4], Resources::srvs_x[5], Resources::srvs_x[6], Resources::srvs_x[7]
                  };
                  native_device_context->PSSetShaderResources(94, srvs_debug.size(), srvs_debug.data());
               }
            }
         }
         case Done:
            break;
      }

      return result;
   }

   void OnInit()
   {
      Shaders::OnInit();
   }

   void HardReset()
   {
      Resources::Reset();
   }
   
   void OnLoad(reshade::api::effect_runtime* runtime)
   {
      reshade::get_config_value(runtime, NAME, reshadesave_enabled, enabled);
      reshade::get_config_value(runtime, NAME, reshadesave_blur_mips, blur_mips);
      reshade::get_config_value(runtime, NAME, reshadesave_desired_resolution_x, desired_resolution.x);
      reshade::get_config_value(runtime, NAME, reshadesave_desired_resolution_y, desired_resolution.y);
      reshade::get_config_value(runtime, NAME, reshadesave_override_full_resolution, override_full_resolution);
   }

   void OnPresent()
   {
      if (!enabled) return;
      ResetState();
   }
}

#if DEVELOPMENT
namespace LUTBuilderScan
{
   std::unordered_set<uint64_t> scanned_lut_res;
   uint64_t prev_used_res = 0;

   void OnDrawOrDispatchOverride(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData&device_data, uint32_t ps)
   {
      // LUT isn't built by GPU (TEX DESC doesn't allow RTV)
   }

   void OnTonemapDraw(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data)
   {
      // get SRV2 (LUT)
      ComPtr<ID3D11ShaderResourceView> srv;
      native_device_context->PSGetShaderResources(2, 1, srv.put());

      // skip if null
      if (!srv) return;

      // RES
      ComPtr<ID3D11Resource> res;
      srv->GetResource(res.put());
      prev_used_res = reinterpret_cast<uint64_t>(res.get());

      // skip if old
      if (scanned_lut_res.contains(reinterpret_cast<uint64_t>(res.get()))) return;

      // insert
      scanned_lut_res.insert(reinterpret_cast<uint64_t>(res.get()));
      reshade::log::message(reshade::log::level::info, std::format("LUTBuilderScan: Found LUTBuilder LUT at SRV2, res={}.", reinterpret_cast<uint64_t>(res.get())).c_str());
      ASSERT_ONCE_MSG(scanned_lut_res.size() <= 2, "LUTBuilderScan: Found 3 LUTBuilder LUTs, check log!"); // only expect 2
   }

   void OnInitSwapchain()
   {
      scanned_lut_res.clear();
      reshade::log::message(reshade::log::level::info, "LUTBuilderScan: Cleared resource_hashes on swapchain init.");
   }
}
#endif

namespace RenderTracker // partial
{
   void OnPushDescriptors(reshade::api::command_list* cmd_list, reshade::api::shader_stage stages, reshade::api::pipeline_layout layout, uint32_t layout_param, const reshade::api::descriptor_table_update& update)
   {
      SKIP_UNSUPPORTED_DEVICE_API(cmd_list->get_device()->get_api());

      // TODO: ShadowMap override usage bindings
      // TODO: SSS override usage bindings
   }

   bool OnClearRenderTargetViewMegaMix(reshade::api::command_list* cmd_list, reshade::api::resource_view rtv, const float color[4], uint32_t rect_count, const reshade::api::rect* rects)
   {
      SKIP_UNSUPPORTED_DEVICE_API(cmd_list->get_device()->get_api());

      last_cleared_rtv_handle = cmd_list->get_device()->get_resource_from_view(rtv).handle;
      ID3D11Resource* res = reinterpret_cast<ID3D11Resource*>(last_cleared_rtv_handle);

      bool is_prevent = false;
      // (Note: SSS setup RTV0 == main forward lit RTV0 == tonemap SRV0, cleared at SSS start)
      // (Note: MLAA never clears)
      // (Note: Final never clears)
      // (Note: back buffer clears right before usage)
      
      if (last_cleared_rtv_handle == res_main_color_handle)
      {
         count_main_color_clears++;
         count_main_color_clears_draws_after = 0;
      }

      // event
      is_prevent |= ShadowMap::OnClearRenderTargetViewMegaMix(last_cleared_rtv_handle);

      return is_prevent;
   }

   DrawOrDispatchOverrideType OnDrawOrDispatchOverride(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, uint32_t ps)
   {
      DrawOrDispatchOverrideType result = DrawOrDispatchOverrideType::None;

      // ps_prev
      static uint32_t ps_prev_aux = 0; // since this is called at beginning of OnDrawOrDispatchOverride(), we need 2 variables to correctly store prev
      ps_prev = ps_prev_aux;
      ps_prev_aux = ps;
      
      // main_color_clears_draws_after ++
      if (count_main_color_clears > 0) count_main_color_clears_draws_after++;

      // sss_state
      switch (sss_state)
      {
         case SSSState_Unknown:
         {
            // depending on immediate subsequent ps of first main_color clear, we can determine if SSS is setup or skipped
            static const std::unordered_set<uint32_t> sss_draw_shaders = { ShaderHashesLists::SSSSetup0, ShaderHashesLists::SSSSetup1, 0x26AF16B8, 0x8E7027B5 }; // setups, sprite simple, nprcontour1
            // ASSERT_MSG(ps != 0x54415551, "RenderTracker: Unexpected, SSS resolve 0x54415551 called before setup!"); // TODO: X Song Pack path will fail this
            if (count_main_color_clears == 1) sss_state = sss_draw_shaders.contains(ps) ? SSSState_Setup : SSSState_Skipped;
            break;
         }
         case SSSState_Setup:
         {
            if (ps == 0x54415551) sss_state = SSSState_Resolved;
            break;
         }
         case SSSState_Skipped:
         case SSSState_Resolved:
            break;
      }

      // find depth
      [[unlikely]] if (!res_main_depth)
      {
         // NPR route via Contour0 setup/resolve
         if (ps_prev == 0xD3B7A039 && ps == 0x86E4A609) // prev is setup, current is resolve
         {
            // SRV10 is depth
            ComPtr<ID3D11ShaderResourceView> srv10;
            native_device_context->PSGetShaderResources(10, 1, srv10.put());
            ASSERT_MSG(srv10 != nullptr, "RenderTracker: SRV10 null Contour");
            srv10->GetResource(res_main_depth.put());
            res_main_depth_handle = reinterpret_cast<uint64_t>(res_main_depth.get());
         }
         // PBR route via SSS Setup
         else if (sss_state == SSSState_Setup && (ps == ShaderHashesLists::SSSSetup0 || ps == ShaderHashesLists::SSSSetup1))
         {
            // DSV is depth
            ComPtr<ID3D11DepthStencilView> dsv;
            native_device_context->OMGetRenderTargets(0, nullptr, dsv.put());
            ASSERT_MSG(dsv != nullptr, "RenderTracker: DSV null SSS");
            dsv->GetResource(res_main_depth.put());
            res_main_depth_handle = reinterpret_cast<uint64_t>(res_main_depth.get());
         }

         // size
         if (res_main_depth)
         {
            // TEX
            auto hr = res_main_depth->QueryInterface(res_main_depth_tex.put());
            D3D11_TEXTURE2D_DESC tex_desc;
            res_main_depth_tex->GetDesc(&tex_desc);
            res_main_depth_size = {tex_desc.Width, tex_desc.Height};

            // compare with color
            if (res_main_color_size.x > 0 && res_main_color_size.y > 0
               && (res_main_color_size.x != tex_desc.Width || res_main_color_size.y != tex_desc.Height))
            {
               MessageBoxA(nullptr, "A fatal error occurred when detecting Render Resolution.\nPlease report!", "Error", MB_ICONERROR);
               Reset_Handles();
            }
         }
      }

      return result;
   }
   
   void OnDLLMainAfterCore(DWORD ul_reason_for_call)
   {
      if (ul_reason_for_call == DLL_PROCESS_ATTACH)
      {
         reshade::register_event<reshade::addon_event::clear_render_target_view>(OnClearRenderTargetViewMegaMix);
      }
      else if (ul_reason_for_call == DLL_PROCESS_DETACH)
      {
         reshade::register_event<reshade::addon_event::clear_render_target_view>(OnClearRenderTargetViewMegaMix);
      }
   }

   void OnInitSwapchain() // TODO: if user is insane enough to change resolution mid-game inside a PV where DoF is active, gg...
   {
      // reset
      Reset_Handles();
      Reset_State();
   }

   void OnTonemapDraw(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data)
   {
      [[unlikely]] if (!res_main_color) // TODO: if user is insane enough to change resolution mid-game inside a PV where DoF is active, gg...
      {
         // SRV0 for RES
         ComPtr<ID3D11ShaderResourceView> srv;
         native_device_context->PSGetShaderResources(0, 1, srv.put());
         ASSERT_MSG(srv != nullptr, "DuplicateWorldReflections: SRV0 null TonemapDraw");
         srv->GetResource(res_main_color.put());
         res_main_color_handle = reinterpret_cast<uint64_t>(res_main_color.get());

         // TEX for size
         auto hr = res_main_color->QueryInterface(res_main_color_tex.put());
         D3D11_TEXTURE2D_DESC tex_desc;
         res_main_color_tex->GetDesc(&tex_desc);
         res_main_color_size = {tex_desc.Width, tex_desc.Height};

         // compare with depth
         if (res_main_depth_size.x > 0 && res_main_depth_size.y > 0
            && (res_main_depth_size.x != tex_desc.Width || res_main_depth_size .y != tex_desc.Height))
         {
            MessageBoxA(nullptr, "A fatal error occurred when detecting Render Resolution.\nPlease report!", "Error", MB_ICONERROR);
            Reset_Handles();
         }

         // event
         if (res_main_color_size.x > 0 && res_main_color_size.y > 0) SSAA::OnRenderResolutionFound(res_main_color_size, device_data);
      }
   }

   void OnPresent()
   {
      // reset
      // ASSERT_MSG(!(IsSSSResolved() && count_main_color_clears == 1), "DuplicateWorldReflections OnPresent: SSS for main color resolved, but nothing used it."); // TODO: del, safely triggers when loading
      Reset_State();
   }
}
} // unnamed namespace

class ProjectDivaMegaMix final : public Game
{
public:
   void OnInit(bool async) override
   {
      // log
      message(reshade::log::level::info, "OnInit()");

      // force same version before load to not reset settings
      reshade::set_config_value(nullptr, NAME, "Version", Globals::VERSION); // TODO: WHY IS THIS TROLLING?!?!?

      // OutputHandling
      OutputHandling::OnInit();

      // SSAA
      SSAA::OnInit();
      
      // ShaderDefines
      ShaderDefineInfo::OnInit();
      if (!DEVELOPMENT) ShaderDefineInfo::Set(DEVELOPMENT_HASH, false);

      // cb
      luma_settings_cbuffer_index = 13;
      luma_data_cbuffer_index = 12;

      // event
      XeGTAO::OnInit();
      SSS::OnInit();
      Bloom::OnInit();
      AntiAliasing::OnInit();
      LUTBiasCached::OnInit();
      ShadowMap::OnInit();
      DuplicateWorldReflections::OnInit();
      
      // GameSettings default
      // default_luma_global_game_settings.TonemapperRolloffStart = cb_luma_global_settings.GameSettings.TonemapperRolloffStart = 36.f;
      default_luma_global_game_settings.BloomStrength = cb_luma_global_settings.GameSettings.BloomStrength = 1.f;
      default_luma_global_game_settings.BloomStrengths = cb_luma_global_settings.GameSettings.BloomStrengths = float4(1.f, 1.f, 1.f, 1.f);
      default_luma_global_game_settings.PerChannelLuminanceReductionEmulateStrength = cb_luma_global_settings.GameSettings.PerChannelLuminanceReductionEmulateStrength = 0.25f;
      
      default_luma_global_game_settings.GammaCorrection22PaperWhite = cb_luma_global_settings.GameSettings.GammaCorrection22PaperWhite = 203.f;
      default_luma_global_game_settings.GammaPerceptualChrominanceCorrect = cb_luma_global_settings.GameSettings.GammaPerceptualChrominanceCorrect = 0.25f;
      
      // default_luma_global_game_settings.UITransparency = cb_luma_global_settings.GameSettings.UITransparency = 1.f;
      
      default_luma_global_game_settings.LUTGaussianBlurStep = cb_luma_global_settings.GameSettings.LUTGaussianBlurStep = 40.f;
      default_luma_global_game_settings.LUTGaussianBlurBias = cb_luma_global_settings.GameSettings.LUTGaussianBlurBias = 3.1f;
      
      // default_luma_global_game_settings.PCBlowoutLumaEnd = cb_luma_global_settings.GameSettings.PCBlowoutLumaEnd = 2.016f;
      // default_luma_global_game_settings.PCBlowoutPerChannelEnd = cb_luma_global_settings.GameSettings.PCBlowoutPerChannelEnd = 2.64f;
      // default_luma_global_game_settings.PCBlowoutPerChannelClip = cb_luma_global_settings.GameSettings.PCBlowoutPerChannelClip = 3.918f;
      // default_luma_global_game_settings.PCBlowoutPerChannel2ndStartRatio = cb_luma_global_settings.GameSettings.PCBlowoutPerChannel2ndStartRatio = 0.93f;
      // default_luma_global_game_settings.PCBlowoutPerChannel2ndEnd = cb_luma_global_settings.GameSettings.PCBlowoutPerChannel2ndEnd = 2.517f;
      
      default_luma_global_game_settings.FakeBT2020Chroma = cb_luma_global_settings.GameSettings.FakeBT2020Chroma = 0.125f;
      default_luma_global_game_settings.FakeBT2020Luma = cb_luma_global_settings.GameSettings.FakeBT2020Luma = 0.125f;
      
      default_luma_global_game_settings.UpscaleMovPumboPow = cb_luma_global_settings.GameSettings.UpscaleMovPumboPow = 3.6f;
      default_luma_global_game_settings.UpscaleBGSpritesMax = cb_luma_global_settings.GameSettings.UpscaleBGSpritesMax = 4.4f;
      default_luma_global_game_settings.UpscaleBGSpritesExp = cb_luma_global_settings.GameSettings.UpscaleBGSpritesExp = 0.30f;
      default_luma_global_game_settings.UpscaleToonMax = cb_luma_global_settings.GameSettings.UpscaleToonMax = 1.400f;
      default_luma_global_game_settings.UpscaleToonExp = cb_luma_global_settings.GameSettings.UpscaleToonExp = 0.18f;
      
      default_luma_global_game_settings.HUDBrightnessHealthBar = cb_luma_global_settings.GameSettings.HUDBrightnessHealthBar = 0.65f;
      default_luma_global_game_settings.HUDBrightnessHealthBarDelta = cb_luma_global_settings.GameSettings.HUDBrightnessHealthBarDelta = 0.5f;
      default_luma_global_game_settings.HUDBrightnessProgressBar = cb_luma_global_settings.GameSettings.HUDBrightnessProgressBar = 0.8f;
      default_luma_global_game_settings.HUDBrightnessCommonIcons = cb_luma_global_settings.GameSettings.HUDBrightnessCommonIcons = 0.5f;
      default_luma_global_game_settings.HUDBrightnessNoteResponse = cb_luma_global_settings.GameSettings.HUDBrightnessNoteResponse = 0.75f;
      default_luma_global_game_settings.HUDBrightnessHoldComboBg = cb_luma_global_settings.GameSettings.HUDBrightnessHoldComboBg = 0.5f;
      default_luma_global_game_settings.HUDBrightnessPJDLogo = cb_luma_global_settings.GameSettings.HUDBrightnessPJDLogo = 1.0f;
      
      default_luma_global_game_settings.CGContrast = cb_luma_global_settings.GameSettings.CGContrast = 1.f;
      default_luma_global_game_settings.CGContrastMidGray = cb_luma_global_settings.GameSettings.CGContrastMidGray = 36.f;
      default_luma_global_game_settings.CGSaturation = cb_luma_global_settings.GameSettings.CGSaturation = 1.0275f;
      default_luma_global_game_settings.CGHighlightsStrength = cb_luma_global_settings.GameSettings.CGHighlightsStrength = 1.f;
      default_luma_global_game_settings.CGHighlightsMidGray = cb_luma_global_settings.GameSettings.CGHighlightsMidGray = 36.f;
      default_luma_global_game_settings.CGShadowsStrength = cb_luma_global_settings.GameSettings.CGShadowsStrength = 1.f;
      default_luma_global_game_settings.CGShadowsMidGray = cb_luma_global_settings.GameSettings.CGShadowsMidGray = 36.f;
      
      default_luma_global_game_settings.XeGTAOFinalPower = cb_luma_global_settings.GameSettings.XeGTAOFinalPower = 1.f;
      
      default_luma_global_game_settings.SSSRadius = cb_luma_global_settings.GameSettings.SSSRadius = 1.f;
      default_luma_global_game_settings.SSSEyeDodge = cb_luma_global_settings.GameSettings.SSSEyeDodge = 0.f;

      default_luma_global_game_settings.FrameBlendRatio = cb_luma_global_settings.GameSettings.FrameBlendRatio = 0.33f;

      default_luma_global_game_settings.NPRContourStrength = cb_luma_global_settings.GameSettings.NPRContourStrength = 1.0f;
      default_luma_global_game_settings.NPRContourRadius = cb_luma_global_settings.GameSettings.NPRContourRadius = 1.0f;

#if DEVELOPMENT
      // debug_draw_options edit
      debug_draw_options = debug_draw_options & ~(uint)DebugDrawTextureOptionsMask::Tonemap;
#endif

#if DAV_CORE == 1 // always show Brightness sliders
      // Display Composition is only useful for Frame Capture
      force_disable_display_composition = !DEVELOPMENT || !OutputHandling::IsSCRGB();
#endif
   }
   
   void OnCreateDevice(ID3D11Device* native_device, DeviceData& device_data) override
   {
      // log
      message(reshade::log::level::info, "OnCreateDevice()");
      
      // HighFPS
      MemoryHack::Init();
   }

   void OnInitSwapchain_ResetEvent(bool is_resolution_changed)
   {
      // event
      RenderTracker::OnInitSwapchain();
      XeGTAO::OnInitSwapchain(is_resolution_changed);
      SSS::HardReset();
      Bloom::HardReset();
      SpotLightShadows::HardReset();
      AntiAliasing::HardReset();
      DepthOfField::HardReset();
      PS4Blur::HardReset();
#if DEVELOPMENT
      LUTBuilderScan::OnInitSwapchain();
#endif
      // UISeparation::ResetOnSwapchain();
      ShadowMap::HardReset();
      DuplicateWorldReflections::HardReset();
   }

   void OnInitSwapchain(reshade::api::swapchain* swapchain) override
   {
      // log
      message(reshade::log::level::info, "OnInitSwapchain()");
      
      auto& device_data = *swapchain->get_device()->get_private_data<DeviceData>();

      // resolution changed?
      static uint2 last_size = {};
      uint2 size = uint2(device_data.output_resolution.x, device_data.output_resolution.y);
      bool is_resolution_changed = size != last_size;

      // log
      if (is_resolution_changed) reshade::log::message(reshade::log::level::info, std::format("OnInitSwapchain(): resolution changed to {}x{}", size.x, size.y).c_str());

      // event
      OnInitSwapchain_ResetEvent(is_resolution_changed);

      // ++
      GlobalsMegaMix::swapchain_change_count++;
   }

   DrawOrDispatchOverrideType OnDrawOrDispatch(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, reshade::api::shader_stage stages, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, bool is_custom_pass, bool& updated_cbuffers, std::function<void()>* original_draw_dispatch_func) override
   {      
      auto ps = original_shader_hashes.pixel_shaders[0];
      // auto cs = original_shader_hashes.compute_shaders[0];

      // // skip not ps
      // [[unlikely]]
      // if (ps == 0) return DrawOrDispatchOverrideType::None;

      // RenderTracker
      RenderTracker::OnDrawOrDispatchOverride(native_device, native_device_context, cmd_list_data, device_data, ps);

      // LUTBuilderScan
#if DEVELOPMENT
      LUTBuilderScan::OnDrawOrDispatchOverride(native_device, native_device_context, cmd_list_data, device_data, ps);
#endif

      // Swapchain ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

      OutputHandling::OnDrawOrDispatchOverride(native_device, native_device_context, cmd_list_data, device_data, ps);

      // SpotLightShadows ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

      SpotLightShadows::OnDrawOrDispatchOverride(native_device, native_device_context, cmd_list_data, device_data, ps);

      // XeGTAO ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

      XeGTAO::OnDrawOrDispatchOverride(native_device, native_device_context, cmd_list_data, device_data, ps);

      // ShadowMap ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
      {
         auto r = ShadowMap::OnDrawOrDispatchOverride(native_device, native_device_context, cmd_list_data, device_data, ps);
         if (r != DrawOrDispatchOverrideType::None) return r;
      }
      // SSS ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
      {
         auto r = SSS::OnDrawOrDispatchOverride(native_device, native_device_context, cmd_list_data, device_data, ps);
         if (r != DrawOrDispatchOverrideType::None) return r;
      }
      // DuplicateWorldReflections ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
      {
         auto r = DuplicateWorldReflections::OnDrawOrDispatchOverride(native_device, native_device_context, cmd_list_data, device_data, ps);
         if (r != DrawOrDispatchOverrideType::None) return r;
      }
      // Bloom ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
      {
         auto r = Bloom::OnDrawOrDispatchOverride(native_device, native_device_context, cmd_list_data, device_data, ps);
         if (r != DrawOrDispatchOverrideType::None) return r;
      }
      // AutoExposureFix ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
      {
         auto r = AutoExposureFix::OnDrawOrDispatchOverride(native_device, native_device_context, cmd_list_data, device_data, ps);
         if (r != DrawOrDispatchOverrideType::None) return r;
      }
      // Depth of Field /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
      {
         auto r = DepthOfField::OnDrawOrDispatchOverride(native_device, native_device_context, cmd_list_data, device_data, ps);
         if (r != DrawOrDispatchOverrideType::None) return r;
      }
      // TONEMAP UBER //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
      if (!TonemapInfo::GetDrawnFinal(cb_luma_global_settings.GameSettings.TonemapInfo) && //if final drawn, no tonemap possible
         !TonemapInfo::GetDrawnTonemap(cb_luma_global_settings.GameSettings.TonemapInfo))
      {
         // get
         int ti = cb_luma_global_settings.GameSettings.TonemapInfo;

         // set?
         auto it = ShaderHashesLists::Tonemaps.find(ps);
         if (it != ShaderHashesLists::Tonemaps.end())
         {
            ti = TonemapInfo::SetIndexAndDrawnTonemapTrue(ti, it->second);
            
            cb_luma_global_settings.GameSettings.TonemapInfo = ti;
            device_data.cb_luma_global_settings_dirty = true; //reupload for later shaders

            // event
            RenderTracker::OnTonemapDraw(native_device, native_device_context, cmd_list_data, device_data);
            Bloom::OnTonemapDraw(native_device, native_device_context, cmd_list_data, device_data);
            SpotLightShadows::OnTonemapDraw(native_device, native_device_context, cmd_list_data, device_data);
            DepthOfField::OnTonemapAndFinalDraw();
#if DEVELOPMENT
            LUTBuilderScan::OnTonemapDraw(native_device, native_device_context, cmd_list_data, device_data);
#endif
            LUTBiasCached::OnTonemapDraw(native_device, native_device_context, cmd_list_data, device_data);
            
            return DrawOrDispatchOverrideType::None;
         }
      }

      // AA ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
      {
         auto r = AntiAliasing::OnDrawOrDispatchOverride(native_device, native_device_context, cmd_list_data, device_data, ps);
         if (r != DrawOrDispatchOverrideType::None) return r;
      }
      // FINAL /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
      
      if (!TonemapInfo::GetDrawnFinal(cb_luma_global_settings.GameSettings.TonemapInfo) &&
         (ps == ShaderHashesLists::Final || SSAA::IsFinalDrawing(native_device, native_device_context, cmd_list_data, device_data, ps)))
      {
         //drawn
         cb_luma_global_settings.GameSettings.TonemapInfo = TonemapInfo::SetDrawnFinalTrue(cb_luma_global_settings.GameSettings.TonemapInfo);
         device_data.has_drawn_main_post_processing = true;
         device_data.cb_luma_global_settings_dirty = true;

         // //UI Transparency: IsFinalCopyToken
         // if (cb_luma_global_settings.GameSettings.UITransparency < 1.f && UISeparation::UIOutputRtv.get() != nullptr)
         // {
         //    //give token
         //    UISeparation::IsFinalCopyToken = true;
         // }

         // event
         SSAA::OnDrawFinal(native_device, native_device_context, cmd_list_data, device_data); // can replace PS
         PS4Blur::OnDrawFinal(native_device, native_device_context, cmd_list_data, device_data); // can replace SRV, RTV, Viewport
         DepthOfField::OnTonemapAndFinalDraw();

         return DrawOrDispatchOverrideType::None;
      }

      // UI /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

      //See EXTRA
      // Includes PV's FXs but also HUD.

      // //Mov
      // if (TonemapInfo::GetDrawnFinal(cb_luma_global_settings.GameSettings.TonemapInfo) &&
      //    ps == ShaderHashesLists::Mov/*original_shader_hashes.Contains(ShaderHashesLists::Mov)*/)
      // {
      //    //flag
      //    cb_luma_global_settings.GameSettings.TonemapInfo = TonemapInfo::SetIsFMVTrue(cb_luma_global_settings.GameSettings.TonemapInfo);
      //    device_data.cb_luma_global_settings_dirty = true;
      //
      //    return DrawOrDispatchOverrideType::None;
      // }

      //HPBarDelta
      if (TonemapInfo::GetDrawnFinal(cb_luma_global_settings.GameSettings.TonemapInfo) &&
         !TonemapInfo::GetDrawnHPBarDelta(cb_luma_global_settings.GameSettings.TonemapInfo) &&
         ps == ShaderHashesLists::UISpritesHPBarDelta)
      {
         //flag
         cb_luma_global_settings.GameSettings.TonemapInfo = TonemapInfo::SetDrawnHPBarDeltaTrue(cb_luma_global_settings.GameSettings.TonemapInfo);
         device_data.cb_luma_global_settings_dirty = true;
      }

      // //UI Transparency: IsFinalCopyToken
      // if (cb_luma_global_settings.GameSettings.UITransparency < 1.f && UISeparation::IsFinalCopyToken)
      // {
      //    //use token
      //    UISeparation::IsFinalCopyToken = false;
      //
      //    //error: not exist
      //    ASSERT(UISeparation::UIOutputTexOrig.get() != nullptr);
      //
      //    //copy
      //    native_device_context->CopyResource(UISeparation::UIOutputTex.get(), UISeparation::UIOutputTexOrig.get());
      // }

      // TO SWAPCHAIN /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
      
      if (TonemapInfo::GetDrawnFinal(cb_luma_global_settings.GameSettings.TonemapInfo) &&
         !RenderTracker::is_drawn_to_swapchain &&
         ps == ShaderHashesLists::ToSwapchain)
      {
         RenderTracker::is_drawn_to_swapchain = true;

         // //UI Transparency
         // if (cb_luma_global_settings.GameSettings.UITransparency < 1.f && UISeparation::UIOutputTex.get() == nullptr)
         // {
         //    //shader res 0
         //    com_ptr<ID3D11ShaderResourceView> srv;
         //    native_device_context->PSGetShaderResources(0, 1, &srv);
         //    ASSERT(srv.get() != nullptr);
         //
         //    //get resource
         //    com_ptr<ID3D11Resource> srv_res;
         //    srv->GetResource(&srv_res);
         //    ASSERT(srv_res.get() != nullptr);
         //    
         //    //get tex
         //    com_ptr<ID3D11Texture2D> srv_tex;
         //    auto hr0 = srv_res->QueryInterface(&srv_tex);
         //    ASSERT(SUCCEEDED(hr0));
         //    UISeparation::UIOutputTexOrig = srv_tex; //save for later
         //
         //    //get desc
         //    D3D11_TEXTURE2D_DESC stv_tex_desc;
         //    srv_tex->GetDesc(&stv_tex_desc);
         //    
         //    //create desc unorm
         //    UISeparation::UIOutputTexDesc.Width          = stv_tex_desc.Width;
         //    UISeparation::UIOutputTexDesc.Height         = stv_tex_desc.Height;
         //    UISeparation::UIOutputTexDesc.MipLevels      = stv_tex_desc.MipLevels;
         //    UISeparation::UIOutputTexDesc.ArraySize      = stv_tex_desc.ArraySize;
         //    UISeparation::UIOutputTexDesc.Format         = stv_tex_desc.Format /*DXGI_FORMAT_R16G16B16A16_UNORM*/;
         //    UISeparation::UIOutputTexDesc.SampleDesc     = stv_tex_desc.SampleDesc;
         //    UISeparation::UIOutputTexDesc.Usage          = stv_tex_desc.Usage;
         //    UISeparation::UIOutputTexDesc.BindFlags      = stv_tex_desc.BindFlags;
         //    UISeparation::UIOutputTexDesc.CPUAccessFlags = stv_tex_desc.CPUAccessFlags;
         //    UISeparation::UIOutputTexDesc.MiscFlags      = stv_tex_desc.MiscFlags;
         //    
         //    //create tex
         //    auto hr1 = native_device->CreateTexture2D(&UISeparation::UIOutputTexDesc, nullptr, &UISeparation::UIOutputTex);
         //    ASSERT(SUCCEEDED(hr1));
         //    
         //    //create rtv for later
         //    auto hr2 = native_device->CreateRenderTargetView(UISeparation::UIOutputTex.get(), nullptr, &UISeparation::UIOutputRtv);
         //    ASSERT(SUCCEEDED(hr2));
         //    
         //    //create shader res for later
         //    auto hr3 = native_device->CreateShaderResourceView(UISeparation::UIOutputTex.get(), nullptr, &UISeparation::UIOutputSrv);
         //    ASSERT(SUCCEEDED(hr3));
         //
         //    //skip so shader dont explode (just 1 frame)
         //    return DrawOrDispatchOverrideType::Skip;
         // }
         //
         // //add ui tex as shader res
         // if (cb_luma_global_settings.GameSettings.UITransparency < 1.f)
         //    native_device_context->PSSetShaderResources(1, 1, &UISeparation::UIOutputSrv);
         
         return DrawOrDispatchOverrideType::None;
      }

      // EXTRA /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

      // //FULLSCREEN OVERLAY FX
      // if (!Globals::IsFullscreenOverlayFx &&
      //    TonemapInfo::GetDrawnTonemap(cb_luma_global_settings.GameSettings.TonemapInfo) &&
      //    !TonemapInfo::GetDrawnFinal(cb_luma_global_settings.GameSettings.TonemapInfo))
      // {
      //    //case: FXAA
      //    if (!ResourceTracker::IsDrawnMLAAPrev) return DrawOrDispatchOverrideType::Skip;
      //
      //    //case: MLAA
      //    if (ResourceTracker::IsDrawnMLAA) return DrawOrDispatchOverrideType::Skip;
      //
      //    //case: wait until MLAA
      //    return DrawOrDispatchOverrideType::None;
      // }
      
      //UI
      if (TonemapInfo::GetDrawnFinal(cb_luma_global_settings.GameSettings.TonemapInfo) &&
         !RenderTracker::is_drawn_to_swapchain &&
         ps != ShaderHashesLists::Mov)
      {
         //skip IsUI
         if (!GlobalsMegaMix::is_draw_game_ui) return DrawOrDispatchOverrideType::Skip;

         //skip SpritesText
         if (!GlobalsMegaMix::is_draw_game_ui_text
            && ps == ShaderHashesLists::UISpritesText)
            return DrawOrDispatchOverrideType::Skip; 
         
         // //UI Transparency: Replace RTV
         // if (cb_luma_global_settings.GameSettings.UITransparency < 1.f)
         //    native_device_context->OMSetRenderTargets(1, &UISeparation::UIOutputRtv, nullptr);
      }
      
      return DrawOrDispatchOverrideType::None;
   }

   void OnPresent(ID3D11Device* native_device, DeviceData& device_data)
   {
      // event
      RenderTracker::OnPresent();
      CachedCB::OnPresent(); 
      IndividualPVTuning::OnPresent();
      HighFPS::Patch();
      ProgressBar::OnPresent();
      SeparateUIBrightness::OnPresent();
      SSAA::OnPresent();
      XeGTAO::OnPresent();
      SSS::OnPresent();
      Bloom::OnPresent();
      SpotLightShadows::OnPresent();
      AntiAliasing::OnPresent();
      DepthOfField::OnPresent();
      ShadowMap::OnPresent();
      DuplicateWorldReflections::OnPresent();
      
      // reset TonemapInfo
      TonemapInfo::tonemapinfo_prev = cb_luma_global_settings.GameSettings.TonemapInfo;
      cb_luma_global_settings.GameSettings.TonemapInfo = TonemapInfo::GetDefaultReset();
      device_data.has_drawn_main_post_processing = false;

      // TimerTimestamp
      TimerTimestamp::OnPresent();
   }

   void LoadConfigs() override
   {
      //log
      message(reshade::log::level::info, "LoadConfigs()");
      
      reshade::api::effect_runtime* runtime = nullptr;

      ShaderDefineInfo::OnLoadConfigs();

      //try force 400 nits
      if (!reshade::get_config_value(runtime, NAME, "ScenePeakWhite", cb_luma_global_settings.ScenePeakWhite)) cb_luma_global_settings.ScenePeakWhite = 400.f;

      //Load custom settings
      reshade::get_config_value(runtime, NAME, "TonemapperMaxExpected", CachedCB::white_clip /*cb_luma_global_settings.GameSettings.TonemapperMaxExpected*/);
      reshade::get_config_value(runtime, NAME, "BloomStrength", cb_luma_global_settings.GameSettings.BloomStrength);
      reshade::get_config_value(runtime, NAME, "BloomStrengthsX", cb_luma_global_settings.GameSettings.BloomStrengths.x);
      reshade::get_config_value(runtime, NAME, "BloomStrengthsY", cb_luma_global_settings.GameSettings.BloomStrengths.y);
      reshade::get_config_value(runtime, NAME, "BloomStrengthsZ", cb_luma_global_settings.GameSettings.BloomStrengths.z);
      reshade::get_config_value(runtime, NAME, "BloomStrengthsW", cb_luma_global_settings.GameSettings.BloomStrengths.w);
      reshade::get_config_value(runtime, NAME, "PerChannelLuminanceReductionEmulateStrength", cb_luma_global_settings.GameSettings.PerChannelLuminanceReductionEmulateStrength);
      
      reshade::get_config_value(runtime, NAME, "GammaCorrection22PaperWhite", cb_luma_global_settings.GameSettings.GammaCorrection22PaperWhite);
      reshade::get_config_value(runtime, NAME, "GammaPerceptualChrominanceCorrect", cb_luma_global_settings.GameSettings.GammaPerceptualChrominanceCorrect);

      // reshade::get_config_value(runtime, NAME, "UITransparency", cb_luma_global_settings.GameSettings.UITransparency);
      
      reshade::get_config_value(runtime, NAME, "LUTGaussianBlurStep", cb_luma_global_settings.GameSettings.LUTGaussianBlurStep);
      reshade::get_config_value(runtime, NAME, "LUTGaussianBlurBias", cb_luma_global_settings.GameSettings.LUTGaussianBlurBias);
      
      // reshade::get_config_value(runtime, NAME, "PCBlowoutLumaEnd", cb_luma_global_settings.GameSettings.PCBlowoutLumaEnd);
      // reshade::get_config_value(runtime, NAME, "PCBlowoutPerChannelClip", cb_luma_global_settings.GameSettings.PCBlowoutPerChannelClip);
      // reshade::get_config_value(runtime, NAME, "PCBlowoutPerChannelEnd", cb_luma_global_settings.GameSettings.PCBlowoutPerChannelEnd);
      // reshade::get_config_value(runtime, NAME, "PCBlowoutPerChannel2ndStartRatio", cb_luma_global_settings.GameSettings.PCBlowoutPerChannel2ndStartRatio);
      // reshade::get_config_value(runtime, NAME, "PCBlowoutPerChannel2ndEnd", cb_luma_global_settings.GameSettings.PCBlowoutPerChannel2ndEnd);
      
      reshade::get_config_value(runtime, NAME, "FakeBT2020Chroma", cb_luma_global_settings.GameSettings.FakeBT2020Chroma);
      reshade::get_config_value(runtime, NAME, "FakeBT2020Luma", cb_luma_global_settings.GameSettings.FakeBT2020Luma);
      
      reshade::get_config_value(runtime, NAME, "UpscaleMovPumboPow", cb_luma_global_settings.GameSettings.UpscaleMovPumboPow);
      reshade::get_config_value(runtime, NAME, "UpscaleBGSpritesMax", cb_luma_global_settings.GameSettings.UpscaleBGSpritesMax);
      reshade::get_config_value(runtime, NAME, "UpscaleBGSpritesExp", cb_luma_global_settings.GameSettings.UpscaleBGSpritesExp);
      reshade::get_config_value(runtime, NAME, "UpscaleToonMax", cb_luma_global_settings.GameSettings.UpscaleToonMax);
      reshade::get_config_value(runtime, NAME, "UpscaleToonExp", cb_luma_global_settings.GameSettings.UpscaleToonExp);

      reshade::get_config_value(runtime, NAME, "HUDBrightnessHealthBar", cb_luma_global_settings.GameSettings.HUDBrightnessHealthBar);
      reshade::get_config_value(runtime, NAME, "HUDBrightnessHealthBarDelta", cb_luma_global_settings.GameSettings.HUDBrightnessHealthBarDelta);
      reshade::get_config_value(runtime, NAME, "HUDBrightnessProgressBar", cb_luma_global_settings.GameSettings.HUDBrightnessProgressBar);
      reshade::get_config_value(runtime, NAME, "HUDBrightnessCommonIcons", cb_luma_global_settings.GameSettings.HUDBrightnessCommonIcons);
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
      
      reshade::get_config_value(runtime, NAME, "XeGTAOFinalPower", cb_luma_global_settings.GameSettings.XeGTAOFinalPower);
      
      reshade::get_config_value(runtime, NAME, "SSSRadius", cb_luma_global_settings.GameSettings.SSSRadius);
      reshade::get_config_value(runtime, NAME, "SSSEyeDodge", cb_luma_global_settings.GameSettings.SSSEyeDodge);
      
      reshade::get_config_value(runtime, NAME, "FrameBlendRatio", cb_luma_global_settings.GameSettings.FrameBlendRatio);
      
      reshade::get_config_value(runtime, NAME, "NPRContourStrength", cb_luma_global_settings.GameSettings.NPRContourStrength);
      reshade::get_config_value(runtime, NAME, "NPRContourRadius", cb_luma_global_settings.GameSettings.NPRContourRadius);
      
      reshade::get_config_value(runtime, NAME, "IsUI", GlobalsMegaMix::is_draw_game_ui);
      reshade::get_config_value(runtime, NAME, "IsUIText", GlobalsMegaMix::is_draw_game_ui_text);

      reshade::get_config_value(runtime, NAME, "UIIsAdvanced", GlobalsMegaMix::imgui_show_advanced);
      reshade::get_config_value(runtime, NAME, "UIIsReadmeDone", GlobalsMegaMix::imgui_readme_done);
      
      reshade::get_config_value(runtime, NAME, "HighFPS_enabled", HighFPS::enabled);
      reshade::get_config_value(runtime, NAME, "HighFPS_limit", HighFPS::limit);
      reshade::get_config_value(runtime, NAME, "HighFPS_menu_clamp", HighFPS::menu_clamp);

      reshade::get_config_value(runtime, NAME, "IsGammaCorrectionSyncPaperWhite", GlobalsMegaMix::is_gamma_correction_sync_paper_white);
      if (GlobalsMegaMix::is_gamma_correction_sync_paper_white) cb_luma_global_settings.GameSettings.GammaCorrection22PaperWhite = cb_luma_global_settings.ScenePaperWhite;

      AutoExposureFix::OnLoad(runtime);

      ProgressBar::OnLoad(runtime);

      IndividualPVTuning::OnLoad(runtime);

      SeparateUIBrightness::OnLoad(runtime);

      XeGTAO::OnLoad(runtime);

      SSS::OnLoad(runtime);

      Bloom::OnLoad(runtime);

      SpotLightShadows::OnLoad(runtime);

      AntiAliasing::OnLoad(runtime);

      DepthOfField::OnLoad(runtime);

      ShadowMap::OnLoad(runtime);

      DuplicateWorldReflections::OnLoad(runtime);
   }

   void OnShaderDefinesChanged()
   {
      ShaderDefineInfo::OnShaderDefinesChanged();
   }

   void DrawImGuiSettings(DeviceData& device_data) override
   {
      reshade::api::effect_runtime* runtime = nullptr;
      
      bool is_disabled; //for Begin/EndDisabled();

      //CUSTOM_SDR sync
      bool is_sdr = cb_luma_global_settings.DisplayMode != DisplayModeType::HDR;
      ShaderDefineInfo::Set(ShaderDefineInfo::CUSTOM_SDR_1, is_sdr ? 1 : 0);

      //SWAPCHAIN_TEST_USER_PEAK
      {
         if (cb_luma_global_settings.DisplayMode != DisplayModeType::SDR) ShaderDefineInfo::UIToggleCheckmark(ShaderDefineInfo::SWAPCHAIN_TEST_USER_PEAK, "Test Display Peak", "3 rectangles within a bigger one.\n\nTo find display maximum, set to:\n- Left: Not Visible (2x Peak)\n- Middle: Barely Visible (1x Peak)\n- Right: Easily Visible (0.5x Peak)\n\nOtherwise, just don't let Middle fully disappear/clip!");
         else ShaderDefineInfo::Set(ShaderDefineInfo::SWAPCHAIN_TEST_USER_PEAK, 0); //force off in SDR
      }

      // ui_brightness_slider_enabled
#ifdef DAV_CORE
      ui_brightness_slider_enabled = is_sdr || !SeparateUIBrightness::enabled;
#endif

      // CUSTOM_SSS_EYEDODGE
      ShaderDefineInfo::Set(ShaderDefineInfo::CUSTOM_SSS_EYEDODGE, SSS::enabled && cb_luma_global_settings.GameSettings.SSSEyeDodge > 0.f);

      // Default Built-in force
      ShaderDefineInfo::Set(POST_PROCESS_SPACE_TYPE_HASH, 1);
      ShaderDefineInfo::Set(EARLY_DISPLAY_ENCODING_HASH, 0);
      ShaderDefineInfo::Set(VANILLA_ENCODING_TYPE_HASH, 0);
      ShaderDefineInfo::Set(GAMMA_CORRECTION_TYPE_HASH, 0);
      ShaderDefineInfo::Set(UI_DRAW_TYPE_HASH, 2);

      // Info
      if (GlobalsMegaMix::imgui_show_advanced)
      {
         ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.f));
         
         // Stops
         float stops = log2(cb_luma_global_settings.ScenePeakWhite / cb_luma_global_settings.ScenePaperWhite);
         std::string label_hdr_stops_plural = stops > 1.f ? "s" : "";
         std::string label_hdr_stops_sign = stops > 0.f ? "+" : "";
         ImGui::BulletText(std::format("HDR Stop{}: {}{:.2f}", label_hdr_stops_plural, label_hdr_stops_sign, stops).c_str());

         // OutputHandling
#if DEVELOPMENT
         ImGui::BulletText("Output: %s (Luma Display Composition: %s)", OutputHandling::IsHDR10() ? "HDR10 (10-bit)" : OutputHandling::IsSDR8bit() ? "SDR (8-bit)" : "scRGB (16-bit)", force_disable_display_composition ? "Disabled" : "Enabled");
#else
         ImGui::BulletText("Output: %s", OutputHandling::IsHDR10() ? "HDR10 (10-bit)" : OutputHandling::IsSDR8bit() ? "SDR (8-bit)" : "scRGB (16-bit)");
#endif

         // Render Resolution
#if DEVELOPMENT
         ImGui::BulletText("Render Resolution: %dx%d (Color: %s) (Depth: %s)", RenderTracker::res_main_color_size.x, RenderTracker::res_main_color_size.y, RenderTracker::res_main_color ? "Yes" : "No", RenderTracker::res_main_depth ? "Yes" : "No");
#else
         ImGui::BulletText("Render Resolution: %dx%d", RenderTracker::res_main_color_size.x, RenderTracker::res_main_color_size.y);
#endif
         
         ImGui::PopStyleColor();
      }
      
      if (!GlobalsMegaMix::imgui_readme_done)
      {
         ImGui::Separator(); ////////////////////////////////////////////////////////////////////////////////////

         auto p = GetPulseMultiplier(0.05);
         ImGui::TextColored(ImVec4(1.f * p, 0.5f * p, 0.9f * p, 1.f), "[Thanks for downloading the mod!]");
         
         ImGui::NewLine();
         
         DrawColoredSubHeader("README");

         ImGui::Bullet(); ImGui::SameLine(); ImGui::TextWrapped("For those new to Dear ImGUI (library for ReShade UI), CTRL click a slider for keyboard input.");
         
         ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 1.f, 0.5f, 1.f));
         ImGui::Bullet(); ImGui::SameLine(); ImGui::TextWrapped("(HDR Users) This mod is tuned for a faithful at +1 HDR Stop (e.g. 200 Paper & 400 Peak, 300 Paper & 600 Peak, etc.)."
                                                                "\nFor some PVs, going higher looks exceptional! But for many others, intentional blowout & white clip will be lost."
                                                                "\nIn other words, deliberate camera/filmic emulation is ruined as tonemapping algorithms become strained when greater than +1 HDR Stop.");
         ImGui::PopStyleColor();
         
         ImGui::Bullet(); ImGui::SameLine(); ImGui::TextWrapped("(HDR Users) UI elems of PV (e.g. lens flare) can be drawn after HDR tonemap, affected by UI Brightness slider.");
         ImGui::Bullet(); ImGui::SameLine(); ImGui::TextWrapped("(HDR Users) Toon shading (Non-Physical Rendering) is clamped to SDR (+0 Stops) unless changed otherwise (for the worst tbh).");

         // ImGui::NewLine(); //////
         //
         // DrawColoredSubHeader("Recommended Mods");
         //
         // if (ImGui::Button("Clean Interface: Remove all but the notes."))
         //    Website::OpenWebsite("https://gamebanana.com/mods/524644");
         //
         // if (ImGui::Button("Remove Forced Toon Shader: Toon shading sucks!"))
         //    Website::OpenWebsite("https://gamebanana.com/mods/578377");
         //
         // if (ImGui::Button("Future Tone Customization: Toon shading sucks!"))
         //    Website::OpenWebsite("https://gamebanana.com/mods/386869");
         
         ImGui::NewLine(); //////

         //close readme
         if (ImGui::Button("Dismiss"))
         {
            GlobalsMegaMix::imgui_readme_done = true;
            reshade::set_config_value(runtime, NAME, "UIIsReadmeDone", GlobalsMegaMix::imgui_readme_done);
         }
      }

      // ImGui::Separator(); ////////////////////////////////////////////////////////////////////////////////////
      
      if (DrawCollapsingHeaderEnabledColored("Gamma", (!is_sdr && ShaderDefineInfo::GetB(ShaderDefineInfo::CUSTOM_GAMMACORRECT22)) || (ShaderDefineInfo::GetB(ShaderDefineInfo::CUSTOM_HDTVREC709_1))))
      {
         if (!is_sdr)
         {
            DrawColoredSubHeader("Reintroduce SDR's gamma mismatch to lower shadows matching original intent.");
         
            // sync
            if (ImGui::Checkbox("Sync to Scene Paper White", &GlobalsMegaMix::is_gamma_correction_sync_paper_white))
            {
               reshade::set_config_value(runtime, NAME, "IsGammaCorrectionSyncPaperWhite", GlobalsMegaMix::is_gamma_correction_sync_paper_white);
               if (GlobalsMegaMix::is_gamma_correction_sync_paper_white) cb_luma_global_settings.GameSettings.GammaCorrection22PaperWhite = cb_luma_global_settings.ScenePaperWhite;
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Encodes weaker sRGB and decodes stronger 2.2, lowering shadows like SDR.");
            
            //paper white
            // if (GlobalsMegaMix::IsGammaCorrectionSyncPaperWhite) ImGui::BeginDisabled();
            if (!GlobalsMegaMix::is_gamma_correction_sync_paper_white)
            {
               if (ImGui::SliderFloat("EOTF / Gamma Correction 2.2", &cb_luma_global_settings.GameSettings.GammaCorrection22PaperWhite, 0.f, 500.f, "%.0f"))
                  reshade::set_config_value(runtime, NAME, "GammaCorrection22PaperWhite", cb_luma_global_settings.GameSettings.GammaCorrection22PaperWhite);
               if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("This is the threshold, so values only needed/lower are affected.");
               DrawResetButton(cb_luma_global_settings.GameSettings.GammaCorrection22PaperWhite, 203.f, "GammaCorrection22PaperWhite", runtime);
            }
            // if (GlobalsMegaMix::IsGammaCorrectionSyncPaperWhite) ImGui::EndDisabled();
            
            //link test
            if (ImGui::Button("Further Explanation (Google Slides)"))
               Website::OpenWebsite("https://docs.google.com/presentation/d/e/2PACX-1vSXeLHlbm6repcS7fels1-SXYGRmzziRrnuJ8nDO8J5rsWV3dT1-nVyCKp0Tj_stwx-9qlCI-N6rYIT/pub?start=false&loop=false&slide=id.g3e007eafba8_0_0");

            if (GlobalsMegaMix::imgui_show_advanced)
            {
               ImGui::NewLine();////////////////
            
               //mode
               is_disabled = ShaderDefineInfo::Get(ShaderDefineInfo::CUSTOM_GAMMACORRECT22) == 0; 
               if (is_disabled) ImGui::BeginDisabled();
               {            
                  //CUSTOM_GAMMA_CORRECTION_MODE dropdown
                  {
                     ShaderDefineInfo::UIDropDown(ShaderDefineInfo::CUSTOM_GAMMA_CORRECTION_MODE, "Gamma Correction Mode",
                         { "Per-Channel (Hue Shifts)", "Perceptual (Hue Corrected)" },
                         "How should the gamma correction operate?\n\nPer-Channel hue shifts shadows.\nPerceptual retains the hues of the original sRGB gamma output, only darkening luminance.");
                  }

                  //GammaPerceptualChrominanceCorrect
                  bool is_disabled_perceptual = ShaderDefineInfo::Get(ShaderDefineInfo::CUSTOM_GAMMA_CORRECTION_MODE) != 1;
                  if (is_disabled_perceptual) ImGui::BeginDisabled();
                  {
                     if (ImGui::SliderFloat("Perceptual Chrominance Gain Reduction", &cb_luma_global_settings.GameSettings.GammaPerceptualChrominanceCorrect, 0.f, 1.f, "%.4f"))
                        reshade::set_config_value(runtime, NAME, "GammaPerceptualChrominanceCorrect", cb_luma_global_settings.GameSettings.GammaPerceptualChrominanceCorrect);
                     if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Reduce chrominance/saturation increase from Gamma Correction in Perceptual mode,\npreventing it from becoming too artificial.");
                     DrawResetButton(cb_luma_global_settings.GameSettings.GammaPerceptualChrominanceCorrect, default_luma_global_game_settings.GammaPerceptualChrominanceCorrect, "GammaPerceptualChrominanceCorrect", runtime);
                  }
                  if (is_disabled_perceptual) ImGui::EndDisabled();
               }
               if (is_disabled) ImGui::EndDisabled();
            }
            
            ImGui::NewLine();////////////////
         }
         
         DrawColoredSubHeader("PS4 Gamma");

         //CUSTOM_HDTVREC709_1
         {
            bool b = ShaderDefineInfo::GetB(ShaderDefineInfo::CUSTOM_HDTVREC709_1);
            if (ImGui::Checkbox("HDTV Rec. 709 Gamma", &b)) ShaderDefineInfo::ToggleBool(ShaderDefineInfo::CUSTOM_HDTVREC709_1);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Do aggressive HDTV Rec. 709 gamma like PS4 Future Tone."
                                                                                             "\n"
                                                                                             "\nBtw, since the original arcade on Sega RingEdge & Nu are Windows based,"
                                                                                             "\nit is PS4's Rec. 709 gamma curve that is the outlier."
                                                                                             "\n"
                                                                                             "\nAnother btw, the Rec. 709 gamma curve is for SDR display gamma BT.1886 (2.4) as recommended by ITU,"
                                                                                             "\nthough this will completely deep fry shadows.");
         }
      }

      //set CUSTOM_GAMMACORRECT22 define based on if paper white is above 0 or not
      ShaderDefineInfo::Set(ShaderDefineInfo::CUSTOM_GAMMACORRECT22, cb_luma_global_settings.GameSettings.GammaCorrection22PaperWhite > 0.f);

      // sync?
      if (GlobalsMegaMix::is_gamma_correction_sync_paper_white) cb_luma_global_settings.GameSettings.GammaCorrection22PaperWhite = cb_luma_global_settings.ScenePaperWhite;

      // ImGui::Separator(); ////////////////////////////////////////////////////////////////////////////////////
      
      ImGui::PushID("###SeparateUIBrightness");
      SeparateUIBrightness::OnUIAlways(runtime);
      if (!is_sdr && DrawCollapsingHeaderEnabledColored("Separate UI Brightness", SeparateUIBrightness::enabled))
      {
         DrawColoredSubHeader("Detects when in gameplay to change UI Brightness accordingly.");
         SeparateUIBrightness::OnUI(runtime);
      }
      ImGui::PopID();
      
      // ImGui::Separator(); ////////////////////////////////////////////////////////////////////////////////////
      
      if (DrawCollapsingHeaderEnabledColored("Individual UI Brightness", ShaderDefineInfo::Get(ShaderDefineInfo::CUSTOM_HUDBRIGHTNESS) > 0))
      {
         DrawColoredSubHeader("Specifically target certain UI elements that are too bright when unclamped to HDR.");

         ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.5f, 0.5f, 1.f));
         ImGui::Bullet(); ImGui::SameLine(); ImGui::TextWrapped("By needing to sample the texture multiple times to identify, this may spike VRAM usage.\nThe best solution is to download some UI texture mod.");
         ImGui::PopStyleColor();

         {
            int def = ShaderDefineInfo::UIDropDown(ShaderDefineInfo::CUSTOM_HUDBRIGHTNESS, "Custom HUD Brightness",
               { "Off", "Vanilla", "Simple UI (simple_ui_v115.zip)"/*, "Clean Interface ()" */},
               "These samples for the specific vanilla textures for identification, so mods that change UI textures will make it miss.");
            is_disabled = def == 0;
         }
         if (is_disabled) ImGui::BeginDisabled(); 
         {
            if (ImGui::SliderFloat("HUD Brightness: Health Bar", &cb_luma_global_settings.GameSettings.HUDBrightnessHealthBar, 0.f, 1.f))
               reshade::set_config_value(runtime, NAME, "HUDBrightnessHealthBar", cb_luma_global_settings.GameSettings.HUDBrightnessHealthBar);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Brightness multiplier for Health Bar.");
            DrawResetButton(cb_luma_global_settings.GameSettings.HUDBrightnessHealthBar, default_luma_global_game_settings.HUDBrightnessHealthBar, "HUDBrightnessHealthBar", runtime);

            if (ImGui::SliderFloat("HUD Brightness: Health Bar Delta", &cb_luma_global_settings.GameSettings.HUDBrightnessHealthBarDelta, 0.f, 1.f))
               reshade::set_config_value(runtime, NAME, "HUDBrightnessHealthBarDelta", cb_luma_global_settings.GameSettings.HUDBrightnessHealthBarDelta);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Brightness multiplier for Health Bar Delta (piece that lingers on change).");
            DrawResetButton(cb_luma_global_settings.GameSettings.HUDBrightnessHealthBarDelta, default_luma_global_game_settings.HUDBrightnessHealthBarDelta, "HUDBrightnessHealthBarDelta", runtime);

            if (ImGui::SliderFloat("HUD Brightness: Progress Bar", &cb_luma_global_settings.GameSettings.HUDBrightnessProgressBar, 0.f, 1.f))
               reshade::set_config_value(runtime, NAME, "HUDBrightnessProgressBar", cb_luma_global_settings.GameSettings.HUDBrightnessProgressBar);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Brightness multiplier for bottom Progress Bar fill.");
            DrawResetButton(cb_luma_global_settings.GameSettings.HUDBrightnessProgressBar, default_luma_global_game_settings.HUDBrightnessProgressBar, "HUDBrightnessProgressBar", runtime);

            if (ImGui::SliderFloat("HUD Brightness: Common Misc.", &cb_luma_global_settings.GameSettings.HUDBrightnessCommonIcons, 0.f, 1.f))
               reshade::set_config_value(runtime, NAME, "HUDBrightnessCommonIcons", cb_luma_global_settings.GameSettings.HUDBrightnessCommonIcons);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Brightness multiplier for misc. common icons.");
            DrawResetButton(cb_luma_global_settings.GameSettings.HUDBrightnessCommonIcons, default_luma_global_game_settings.HUDBrightnessCommonIcons, "HUDBrightnessCommonIcons", runtime);
            
            if (ImGui::SliderFloat("HUD Brightness: Note Response", &cb_luma_global_settings.GameSettings.HUDBrightnessNoteResponse, 0.f, 1.f))
               reshade::set_config_value(runtime, NAME, "HUDBrightnessNoteResponse", cb_luma_global_settings.GameSettings.HUDBrightnessNoteResponse);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Brightness multiplier for the \"boom\" fx when hitting a note.");
            DrawResetButton(cb_luma_global_settings.GameSettings.HUDBrightnessNoteResponse, default_luma_global_game_settings.HUDBrightnessNoteResponse, "HUDBrightnessNoteResponse", runtime);

            if (ImGui::SliderFloat("HUD Brightness: Hold Combo BG", &cb_luma_global_settings.GameSettings.HUDBrightnessHoldComboBg, 0.f, 1.f))
               reshade::set_config_value(runtime, NAME, "HUDBrightnessHoldComboBg", cb_luma_global_settings.GameSettings.HUDBrightnessHoldComboBg);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Brightness multiplier for the background of the Hold Combo popup.");
            DrawResetButton(cb_luma_global_settings.GameSettings.HUDBrightnessHoldComboBg, default_luma_global_game_settings.HUDBrightnessHoldComboBg, "HUDBrightnessHoldComboBg", runtime);

            if (ImGui::SliderFloat("HUD Brightness: PJD Logo", &cb_luma_global_settings.GameSettings.HUDBrightnessPJDLogo, 0.f, 1.f))
               reshade::set_config_value(runtime, NAME, "HUDBrightnessPJDLogo", cb_luma_global_settings.GameSettings.HUDBrightnessPJDLogo);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Brightness multiplier for goofy Music Video logo top right.");
            DrawResetButton(cb_luma_global_settings.GameSettings.HUDBrightnessPJDLogo, default_luma_global_game_settings.HUDBrightnessPJDLogo, "HUDBrightnessPJDLogo", runtime);
         }
         if (is_disabled) ImGui::EndDisabled();
      }

      // ImGui::Separator(); ////////////////////////////////////////////////////////////////////////////////////
      
      if (!is_sdr && DrawCollapsingHeaderEnabledColored("Individual PV Peak Brightness", IndividualPVTuning::current_pv.item != nullptr))
      {
         IndividualPVTuning::OnUI(runtime);
      }
      
      // ImGui::Separator(); ////////////////////////////////////////////////////////////////////////////////////

      if (is_sdr && DrawCollapsingHeaderEnabledColored("HDR Tonemap in SDR", ShaderDefineInfo::GetB(ShaderDefineInfo::CUSTOM_HDRTONEMAPONSDR)))
      {
         DrawColoredSubHeader("Use the new HDR tonemap even in SDR.");

         ShaderDefineInfo::UIToggleCheckmark(ShaderDefineInfo::CUSTOM_HDRTONEMAPONSDR, "HDR Tonemap In SDR", "Use the new HDR Tonemap with it's HQ Bezold-Brucke shift in SDR.\nWithout higher HDR Stops headroom, this may look meh.");
      }

      ImGui::Separator(); ////////////////////////////////////////////////////////////////////////////////////

      // XEGTAO_MANUALSIZE auto toggle
      if (XeGTAO::FoundResource::IsSizeValid())
      {
         bool isSwapchainSized = XeGTAO::FoundResource::size.x == static_cast<uint>(cb_luma_global_settings.SwapchainSize.x) &&
                                 XeGTAO::FoundResource::size.y == static_cast<uint>(cb_luma_global_settings.SwapchainSize.y);
         ShaderDefineInfo::Set(ShaderDefineInfo::XEGTAO_MANUALSIZE, !isSwapchainSized);
      }

      ImGui::PushID("###XeGTAO");
      if (DrawCollapsingHeaderEnabledColored("Ambient Occlusion", XeGTAO::enabled))
      {
         DrawColoredSubHeader("Insert a pass for indirect shadowing.");
         
         ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 1.f, 0.5f, 1.f));
         ImGui::Bullet(); ImGui::SameLine(); ImGui::TextWrapped("Requires Future Tone PBR (primarily SSS) to initialize.");
         ImGui::PopStyleColor();
         
         ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.f));
         ImGui::Bullet(); ImGui::SameLine(); ImGui::TextWrapped("There are errors (e.g. occlusion in fog & sky).\nI might account per PV if it's too noticeable.");
         ImGui::PopStyleColor();
         
         if (ImGui::Checkbox("Enabled", &XeGTAO::enabled))
            reshade::set_config_value(runtime, NAME, XeGTAO::reshadesave_enabled, XeGTAO::enabled);
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Insert Intel's implementation (XeGTAO) of Ground Truth Ambient Occlusion (GTAO).\n\nDebuting in Call of Duty: Black Ops 3 (or Advanced Warfare?),\nit's a screen space AO solution estimating path tracing level quality at a fraction of the cost.\n\nHere, the pass is inserted before transparency rendering & post FX, so no overlapping like generic ReShade FX solutions.\n(Please report if otherwise, especially concerning mod support.)");

         if (ImGui::Checkbox("Disallow for MegaMix", &XeGTAO::disallow_for_npr))
            reshade::set_config_value(runtime, NAME, XeGTAO::reshadesave_disallow_for_npr, XeGTAO::disallow_for_npr);
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("When MegaMix NPR detected, this will not draw.\n\n(This doesn't discern the style of a scene, e.g. \"Two-Faced Lovers\".\nAll 3D will still get occlusion.)");

         // TODO: presets

         ImGui::NewLine();
         DrawColoredSubHeader("Parameters");
         
         if (ImGui::SliderFloat("Final Power", &cb_luma_global_settings.GameSettings.XeGTAOFinalPower, 0.f, 2.f))
            reshade::set_config_value(runtime, NAME, "XeGTAOFinalPower", cb_luma_global_settings.GameSettings.XeGTAOFinalPower);
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Final power of the AO effect, after sample accumulation.\n\n(Increasing this will reveal noise, so you will probably need to increase quality.)");
         DrawResetButton(cb_luma_global_settings.GameSettings.XeGTAOFinalPower, default_luma_global_game_settings.XeGTAOFinalPower, "XeGTAOFinalPower", runtime);
         
         ShaderDefineInfo::UIDropDown(ShaderDefineInfo::XEGTAO_SLICECOUNT, "Slice Count", { "(3) Easy", "(6) Normal", "(8) Hard (Worth for 4K?)", "(12) Extreme", "(16) Extra Extreme", "(24) Uhhh", "(32) ..." }, "More samples = less noise.\n\n(Perhaps more denoise passes can achieve similar results?)");
         if (GlobalsMegaMix::imgui_show_advanced) ShaderDefineInfo::UIDropDown(ShaderDefineInfo::XEGTAO_STEPSPERSLICE, "Steps Per Slice", { "(3) Normal", "(4) Hard" }, "Within a slice, how many search steps.\nIncrease to have more darkening for harder to reach small crevices, but at a great cost to performance.");

         if (GlobalsMegaMix::imgui_show_advanced) ShaderDefineInfo::UIDropDown(ShaderDefineInfo::XEGTAO_NORMALSMOOTH_QUALITY, "Smooth Normals", { "Very Low", "Low"/*, "Normal"*/ }, "Surface normal map doesn't exist natively. Instead it's generated from depth.\nSmoothing is required to mask low poly models.");

         // if (GlobalsMegaMix::UIIsAdvanced)
         {
            int denoise_prev = XeGTAO::denoise_count;
            ImGui::SliderInt("Denoise", &XeGTAO::denoise_count, 0, !ShaderDefineInfo::GetB(ShaderDefineInfo::XEGTAO_CHECKBOARD) ? 8 : 2, "%d");
            XeGTAO::denoise_count = max(XeGTAO::denoise_count, 0);
            if (ShaderDefineInfo::GetB(ShaderDefineInfo::XEGTAO_CHECKBOARD) && XeGTAO::denoise_count > 2) XeGTAO::denoise_count = 2;
            if (XeGTAO::denoise_count != denoise_prev) reshade::set_config_value(runtime, NAME, XeGTAO::reshadesave_denoise, XeGTAO::denoise_count);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
               ImGui::SetTooltip("After AO, do denoising passes to blur out noise while respecting edges.");
            DrawResetButton(XeGTAO::denoise_count, 3, XeGTAO::reshadesave_denoise, runtime);
         }

         ShaderDefineInfo::UIDropDown(ShaderDefineInfo::XEGTAO_NOISE, "Noise", { "Unclamped Phases", "Static", "2 Phases", "3 Phases", "4 Phases", "5 Phases", "6 Phases", "7 Phases", "8 Phases (Better for 120 FPS?)" }, "Noise allow samples to evenly shoot out in all direction.\nInstead of staying static, allow noise to jitter so that it can perceptually mask individual grains.");
         
         ShaderDefineInfo::UIDropDown(ShaderDefineInfo::XEGTAO_CHECKBOARD, "Rate", { "Full", "Half (Unnoticeable, especially 120 FPS?)", "Quarter (Rather unusable smearing.)" }, "Render every other pixel to save performance."); 
         
         ImGui::NewLine();
         DrawColoredSubHeader("Half Resolution");
         bool is_halfres = ShaderDefineInfo::GetB(ShaderDefineInfo::XEGTAO_HALFRES);
         ImGui::PushStyleColor(ImGuiCol_Text, !is_halfres ? ImVec4(1.f, 0.4f, 0.4f, 1.f) : ImVec4(0.4f, 1.f, 0.4f, 1.f));
         ShaderDefineInfo::UIToggleCheckmark(ShaderDefineInfo::XEGTAO_HALFRES, "Half Resolution", "Render AO at half resolution to GREATLY save performance.\n\n(Full resolution not only hits the GPU's ALU, but VRAM!\nHigh FPS will scale nearly exponentially.)");
         ImGui::PopStyleColor();
         bool is_halfres_after = ShaderDefineInfo::GetB(ShaderDefineInfo::XEGTAO_HALFRES);
         if (is_halfres != is_halfres_after) XeGTAO::ResetCreatedResource();

         if (GlobalsMegaMix::imgui_show_advanced)
         {
            if (!is_halfres_after) ImGui::BeginDisabled();
            ShaderDefineInfo::UIToggleCheckmark(ShaderDefineInfo::XEGTAO_UPSAMPLE, "Joint Bilateral Upsample", "Upscale AO results while preventing leaks by using the spatial difference between half vs full res.");
            if (!is_halfres_after) ImGui::EndDisabled();
         }

         if (GlobalsMegaMix::imgui_show_advanced)
         {
            ImGui::NewLine();
            DrawColoredSubHeader("Thread Groups");
            ShaderDefineInfo::UIDropDown(ShaderDefineInfo::XEGTAO_THREADS_NORMALSGEN, "Threads: Normals Generation", { "8", "16" }, "Thread groups for compute shaders.\nDefault should be fastest.");
            ShaderDefineInfo::UIDropDown(ShaderDefineInfo::XEGTAO_THREADS_NORMALSSMOOTH, "Threads: Normals Smoothing", { "8", "16" }, "Thread groups for compute shaders.\nDefault should be fastest.");
            ShaderDefineInfo::UIDropDown(ShaderDefineInfo::XEGTAO_THREADS_AO, "Threads: GTAO", { "8", "16" }, "Thread groups for compute shaders.\nDefault should be fastest.");
            ShaderDefineInfo::UIDropDown(ShaderDefineInfo::XEGTAO_THREADS_DENOISE, "Threads: Denoise", { "8", "16" }, "Thread groups for compute shaders.\nDefault should be fastest.");
         }

         if (GlobalsMegaMix::imgui_show_advanced)
         {
            ImGui::NewLine();
            DrawColoredSubHeader("Auxiliary Resources");
         
            ImGui::PushStyleColor(ImGuiCol_Text, XeGTAO::FoundResource::IsSizeValid() ? ImVec4(0.4f, 0.8f, 0.4f, 1.f) : ImVec4(0.8f, 0.4f, 0.4f, 1.f));
            std::string status;
            if (XeGTAO::FoundResource::IsSizeValid()) status = std::format("Yes ({}x{})", XeGTAO::FoundResource::size.x, XeGTAO::FoundResource::size.y);
            else if (RenderTracker::res_main_color_handle > 0)  status = "No (Color found, pending Depth)";
            else status = "No";
            ImGui::TextWrapped("Ready: %s",  status.c_str());
            ImGui::PopStyleColor();

            ImGui::SameLine();

            if (ImGui::Button("Reset Resources"))
               XeGTAO::HardReset();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
               ImGui::SetTooltip("Reset XeGTAO resources to then recreate.\nShould not be needed unless you change the game's resolution or something.");

            if (GlobalsMegaMix::imgui_show_advanced) {ImGui::Bullet(); ImGui::SameLine(); ImGui::TextWrapped("Size same as swapchain: %s", ShaderDefineInfo::GetB(ShaderDefineInfo::XEGTAO_MANUALSIZE) ? "No" : "Yes");}
         
            int _debug_out = XeGTAO::debug_out;
            ImGui::Combo("Debug View", &_debug_out, "None\0AO\0Normals\0Depth");
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Draw various debug views that is used by AO.");
            XeGTAO::debug_out = static_cast<XeGTAO::DebugOut>(_debug_out);
         }

#if DEVELOPMENT
         ImGui::NewLine();
         DrawColoredSubHeader("DEVELOPMENT");
         ImGui::Checkbox("Debug Late", &XeGTAO::debug_late);
         ImGui::Checkbox("debug_skip_smooth", &XeGTAO::debug_skipsmooth);
         ImGui::Checkbox("Fog Dodge", &XeGTAO::is_fog_dodge);
#endif
      }
      ImGui::PopID();

      // ImGui::Separator(); ////////////////////////////////////////////////////////////////////////////////////

      ImGui::PushID("###SSS");
      if (DrawCollapsingHeaderEnabledColored("Characters", SSS::enabled))
      {
         DrawColoredSubHeader("FutureTone Sub-Surface Scattering (SSS): Simulating glow from light scattering under skin.");

         // enabled checkbox
         if (ImGui::Checkbox("Full Resolution", &SSS::enabled))
            reshade::set_config_value(runtime, NAME, SSS::reshadesave_enabled, SSS::enabled);
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Make the screen-space SSS algorithm full resolution, stopping its bottle neck on shadow maps."
                              "\n"
                              "\n(The first sample is from full res, while the rest is from downsample for performance.)");

         ImGui::BeginDisabled(!SSS::enabled);
         ShaderDefineInfo::UIToggleCheckmark(ShaderDefineInfo::CUSTOM_SSS_HQBLUR, "Extreme Quality Blur", "Greatly increase sample count by adding in-between sample positions,"
                                                                                                                        "\nsuppressing the rare cases where SSS generate strips from bright rim light.");
         ImGui::EndDisabled();
         
         ImGui::BeginDisabled(!SSS::enabled);
         if (ImGui::SliderFloat("Eye Dodge", &cb_luma_global_settings.GameSettings.SSSEyeDodge, 0.f, 1.f))
            reshade::set_config_value(runtime, NAME, "SSSEyeDodge", cb_luma_global_settings.GameSettings.SSSEyeDodge);
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Reduce SSS for eyes to stop letting them look bloodshot.");
         DrawResetButton(cb_luma_global_settings.GameSettings.SSSEyeDodge, default_luma_global_game_settings.SSSEyeDodge, "SSSEyeDodge", runtime);
         ImGui::EndDisabled();
         
         if (GlobalsMegaMix::imgui_show_advanced)
         {
            if (ImGui::SliderFloat("SSS Radius", &cb_luma_global_settings.GameSettings.SSSRadius, 0.5f, 1.5f))
               reshade::set_config_value(runtime, NAME, "SSSRadius", cb_luma_global_settings.GameSettings.SSSRadius);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Radius / reach of the effect.");
            DrawResetButton(cb_luma_global_settings.GameSettings.SSSRadius, default_luma_global_game_settings.SSSRadius, "SSSRadius", runtime);
         }

         ImGui::NewLine();
         DrawColoredSubHeader("MegaMix Contour: Added edges mimicking drawn art.");
         
         if (ImGui::SliderFloat("Strength", &cb_luma_global_settings.GameSettings.NPRContourStrength, 0.f, 2.f))
            reshade::set_config_value(runtime, NAME, "NPRContourStrength", cb_luma_global_settings.GameSettings.NPRContourStrength);
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Final multiplier on contour lines.");
         DrawResetButton(cb_luma_global_settings.GameSettings.NPRContourStrength, default_luma_global_game_settings.NPRContourStrength, "NPRContourStrength", runtime);

         if (ImGui::SliderFloat("Radius", &cb_luma_global_settings.GameSettings.NPRContourRadius, 0.f, 2.f))
            reshade::set_config_value(runtime, NAME, "NPRContourRadius", cb_luma_global_settings.GameSettings.NPRContourRadius);
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Edge search radius of the contour lines.");
         DrawResetButton(cb_luma_global_settings.GameSettings.NPRContourRadius, default_luma_global_game_settings.NPRContourRadius, "NPRContourRadius", runtime);

         if (GlobalsMegaMix::imgui_show_advanced)
         {
            ImGui::NewLine();
            DrawColoredSubHeader("SSS Auxiliary Resources");
            
            for (int i = 0; i < SSS::Resources::items.size(); i++)
            {
               SSS::Resources::Item* item = &SSS::Resources::items[i];
               ImGui::Bullet(); ImGui::SameLine(); ImGui::TextWrapped("%d. %dx%d (%s)", i, item->size.x, item->size.y, item->size.x == 0 ? "" : (item->IsOriginalMainColor() ? "Main Color" : "X Song Pack Full Resolution Reflections"));
            }
         }

#if DEVELOPMENT
         ImGui::NewLine();
         DrawColoredSubHeader("DEVELOPMENT");
         if (ImGui::Button("SSS Preview Toggle")) PreviewShaderOutput(device_data, 0x54415551);
#endif
      }
      ImGui::PopID();
      
      // ImGui::Separator(); ////////////////////////////////////////////////////////////////////////////////////

      // grant dirty check token
      ShadowMap::Resources::is_dirty_check_token = true;
      
      ImGui::PushID("###ShadowMap");
      if (DrawCollapsingHeaderEnabledColored("Exponential Shadow Map", ShadowMap::IsEnabled()))
      {
         DrawColoredSubHeader("Blurred silhouettes from light source position/angle to cast soft shadows of characters.");

         ShaderDefineInfo::UIDropDown(ShaderDefineInfo::CUSTOM_ESM_DEPTH_NEW_SIZE, "Base Resolution",
            { "(Off) FutureTone: 2048x2048, MegaMix: 1024x1024", "(Low) 1024x1024", "(Recommended) 2048x2048", "(High) 4096x4096", "(Extreme) 8192x8192" },
            "The raw size of the texture to draw silhouettes.\nEven with 2048x2048, shadows casting onto the world is improved with gaussian blurring.");

         ImGui::BeginDisabled(ShaderDefineInfo::Get(ShaderDefineInfo::CUSTOM_ESM_DEPTH_NEW_SIZE) <= 2);
         {
            ShaderDefineInfo::UIDropDown(ShaderDefineInfo::CUSTOM_ESM_DEPTH_FILTERREDUCE, "Characters: Blur Sample Count Reduce", { "0 (Perfect Extension Soft)", "1 (Essentially Vanilla Soft)", "2", "3 (Harder, but SSS may posterize)", "4", "5", "6", "7 (If not already, SSS is broken)" },
               "(The raw texture is duplicated for character rendering (i.e. SSS).)\n\nIf > 2048x2048, sample count are mathematically increased for the higher resolution, but it's nice to be frugal.\nHowever, too frugal and shadows are no longer soft as intended, leading SSS to posterize.");
         }
         ImGui::EndDisabled();
         
         if (GlobalsMegaMix::imgui_show_advanced)
         {
            ImGui::BeginDisabled(!ShaderDefineInfo::GetB(ShaderDefineInfo::CUSTOM_ESM_DEPTH_NEW_SIZE));
            {
               ShaderDefineInfo::UIDropDown(ShaderDefineInfo::CUSTOM_ESM_DEPTH_SIGMAINCREASE, "Characters: Blur Softness", { "1", "2", "3", "4", "5", "6", "7", "8", "9", "10" },
                  "(The raw texture is duplicated for character rendering (i.e. SSS).)\n\nChange the gaussian kernel's sigma to control softness.\nWill need \"Blur Sample Count Reduce\" at 0 for higher/wider values.");
            }
            ImGui::EndDisabled();
         
            if (ImGui::SliderInt("World: Blur Mips", &ShadowMap::enabled_color_mips, 1, ShadowMap::Resources::color_aux_x_srvs.size()))
               reshade::set_config_value(runtime, NAME, ShadowMap::reshadesave_enabled_color_mips, ShadowMap::enabled_color_mips);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("(The raw texture is also duplicate for static world rendering.)\n\nOriginally, it skips down to 512x512 before a quick 4-tap box blur.\nHere, you can have an unbroken mip chain for higher quality softness.");
            DrawResetButton(ShadowMap::enabled_color_mips, ShadowMap::enabled_color_mips_def, ShadowMap::reshadesave_enabled_color_mips, runtime);

            if (ImGui::SliderInt("World: Blur Mips Skip", &ShadowMap::color_mips_skip, -1, ShadowMap::Resources::color_aux_x_srvs.size(), ShadowMap::color_mips_skip >= 0 ? "%d" : "Auto"))
               reshade::set_config_value(runtime, NAME, ShadowMap::reshadesave_color_mips_skip, ShadowMap::color_mips_skip);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("(The raw texture is also duplicate for static world rendering.)\n\nFor the first downsample (which is also final result), how many mips to skip?\nThis greatly improves performance later if world geometry is complex.");
            DrawResetButton(ShadowMap::color_mips_skip, ShadowMap::color_mips_skip_def, ShadowMap::reshadesave_color_mips_skip, runtime);
         }

         ShaderDefineInfo::UIDropDown(ShaderDefineInfo::CUSTOM_ESM_COLOR_SIGMA, "World: Blur Softness", { "-4", "-3", "-2", "-1", "0", "1", "2", "3", "4", "5" },
            "(The raw texture is also duplicate for static world rendering.)\n\nChange the gaussian kernel's sigma to control softness.");

         if (GlobalsMegaMix::imgui_show_advanced)
         {
            ImGui::NewLine();
            DrawColoredSubHeader("Original Resources");

            constexpr auto c_green = ImVec4(0.4f, 1.f, 0.4f, 1.f);
            constexpr auto c_red = ImVec4(1.f, 0.4f, 0.4f, 1.f);
            constexpr auto c_yellow = ImVec4(1.f, 1.f, 0.4f, 1.f);
            constexpr auto c_gray = ImVec4(0.5f, 0.5f, 0.5f, 1.f);

            ImGui::Bullet(); ImGui::SameLine(); ImGui::TextWrapped("Characters: %dx%d", ShadowMap::Resources::depth_size_orig, ShadowMap::Resources::depth_size_orig);
            ImGui::Bullet(); ImGui::SameLine(); ImGui::TextWrapped("World: %dx%d", ShadowMap::Resources::tex_size_orig, ShadowMap::Resources::tex_size_orig);

            ImGui::PushStyleColor(ImGuiCol_Text, ShadowMap::Resources::orig_depthdraw_handle ? c_green : c_red);
            ImGui::Bullet(); ImGui::SameLine(); ImGui::TextWrapped("Depth Draw: %s", ShadowMap::Resources::orig_depthdraw_handle ? "Found" : "Not Found");
            ImGui::PopStyleColor();

            ImGui::PushStyleColor(ImGuiCol_Text, ShadowMap::Resources::orig_depthuse_handle > 0 ? c_green : c_red);
            ImGui::Bullet(); ImGui::SameLine(); ImGui::TextWrapped("Depth Use: %s", ShadowMap::Resources::orig_depthuse_handle ? "Found" : "Not Found");
            ImGui::PopStyleColor();

            ImGui::PushStyleColor(ImGuiCol_Text, ShadowMap::Resources::orig_depthusedown_handle > 0 ? c_green : c_red);
            ImGui::Bullet(); ImGui::SameLine(); ImGui::TextWrapped("Depth Downsample: %s", ShadowMap::Resources::orig_depthusedown_handle ? "Found" : "Not Found");
            ImGui::PopStyleColor();

            ImGui::PushStyleColor(ImGuiCol_Text, ShadowMap::Resources::orig_tex_handle > 0 ? c_green : c_red);
            ImGui::Bullet(); ImGui::SameLine(); ImGui::TextWrapped("Color: %s", ShadowMap::Resources::orig_tex_handle ? "Found" : "Not Found");
            ImGui::PopStyleColor();

            // ImGui::PushStyleColor(ImGuiCol_Text, ShadowMap::Resources::orig_texdowna_handle > 0 ? c_green : c_red);
            // ImGui::Bullet(); ImGui::SameLine(); ImGui::TextWrapped("Shadow Map Downsample A: %s", ShadowMap::Resources::orig_texdowna_handle ? "Found" : "Not Found");
            // ImGui::PopStyleColor();
            //
            // ImGui::PushStyleColor(ImGuiCol_Text, ShadowMap::Resources::orig_texdownb_handle > 0 ? c_green : c_yellow);
            // ImGui::Bullet(); ImGui::SameLine(); ImGui::TextWrapped("Shadow Map Downsample B (Multiple Characters/Objects): %s", ShadowMap::Resources::orig_texdownb_handle ? "Found" : "Not Found");
            // ImGui::PopStyleColor();

            if (ImGui::Button("Forget")) ShadowMap::Resources::Reset_Handles();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Forget to then begin search again.\nShould not be needed unless you change the game's resolution or something.");

            ImGui::NewLine();
            DrawColoredSubHeader("Auxiliary Resources");

            ImGui::PushStyleColor(ImGuiCol_Text, ShadowMap::Resources::IsDepthNewReady() ? c_green : c_yellow);
            ImGui::Bullet(); ImGui::SameLine(); ImGui::TextWrapped("Character: %s", ShadowMap::Resources::IsDepthNewReady() ? "Ready" : "Not Ready");
            ImGui::PopStyleColor();

            ImGui::PushStyleColor(ImGuiCol_Text, ShadowMap::Resources::IsColorNewReady() ? c_green : c_yellow);
            ImGui::Bullet(); ImGui::SameLine(); ImGui::TextWrapped("World: %s", ShadowMap::Resources::IsColorNewReady() ? "Ready" : "Not Ready");
            ImGui::PopStyleColor();
            
            if (ImGui::Button("Reset")) ShadowMap::Resources::Reset_Resources();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Release new texture resources to recreate them.\nShould not be needed unless you change the game's resolution or something.");

            if (DEVELOPMENT)
            {
               ImGui::NewLine();
               DrawColoredSubHeader("DEVELOPMENT");

               // Tex A/B usage
               ImGui::PushStyleColor(ImGuiCol_Text, ShadowMap::state_texa_drawn_imgui ? c_green : c_gray);
               ImGui::Bullet(); ImGui::SameLine(); ImGui::TextWrapped("TexA Drawn: %s", ShadowMap::state_texa_drawn_imgui ? "Yes" : "No");
               ImGui::PopStyleColor();
               
               ImGui::PushStyleColor(ImGuiCol_Text, ShadowMap::state_texb_drawn_imgui ? c_green : c_gray);
               ImGui::Bullet(); ImGui::SameLine(); ImGui::TextWrapped("TexB Drawn: %s", ShadowMap::state_texb_drawn_imgui ? "Yes" : "No");
               ImGui::PopStyleColor();
            }
         }
      }
      ImGui::PopID();

      // ImGui::Separator(); ////////////////////////////////////////////////////////////////////////////////////

      ImGui::PushID("###Bloom");
      if (DrawCollapsingHeaderEnabledColored("Bloom", Bloom::enabled || ShaderDefineInfo::GetB(ShaderDefineInfo::CUSTOM_BLOOM_THRESHOLD_1)))
      {
         DrawColoredSubHeader("Bleeding from bright colors simulating glow from camera lens diffraction.");

         if (GlobalsMegaMix::imgui_show_advanced)
         {
            if (ImGui::SliderFloat("Strength: Level 0", &cb_luma_global_settings.GameSettings.BloomStrengths.x, 0.f, 2.f))
               reshade::set_config_value(runtime, NAME, "BloomStrengthsX", cb_luma_global_settings.GameSettings.BloomStrengths.x);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Bloom Level 0 Strength: Tightest blur level/radius.");
            DrawResetButton(cb_luma_global_settings.GameSettings.BloomStrengths.x, default_luma_global_game_settings.BloomStrengths.x, "BloomStrengthsX", runtime);
            
            if (ImGui::SliderFloat("Strength: Level 1", &cb_luma_global_settings.GameSettings.BloomStrengths.y, 0.f, 2.f))
               reshade::set_config_value(runtime, NAME, "BloomStrengthsY", cb_luma_global_settings.GameSettings.BloomStrengths.y);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Bloom Level 1 Strength: Medium blur level/radius.");
            DrawResetButton(cb_luma_global_settings.GameSettings.BloomStrengths.y, default_luma_global_game_settings.BloomStrengths.y, "BloomStrengthsY", runtime);
            
            if (ImGui::SliderFloat("Strength: Level 2", &cb_luma_global_settings.GameSettings.BloomStrengths.z, 0.f, 2.f))
               reshade::set_config_value(runtime, NAME, "BloomStrengthsZ", cb_luma_global_settings.GameSettings.BloomStrengths.z);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Bloom Level 2 Strength: Wide blur level/radius.");
            DrawResetButton(cb_luma_global_settings.GameSettings.BloomStrengths.z, default_luma_global_game_settings.BloomStrengths.z, "BloomStrengthsZ", runtime);
            
            if (ImGui::SliderFloat("Strength: Level 3", &cb_luma_global_settings.GameSettings.BloomStrengths.w, 0.f, 2.f))
               reshade::set_config_value(runtime, NAME, "BloomStrengthsW", cb_luma_global_settings.GameSettings.BloomStrengths.w);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Bloom Level 3 Strength: Widest blur level/radius.");
            DrawResetButton(cb_luma_global_settings.GameSettings.BloomStrengths.w, default_luma_global_game_settings.BloomStrengths.w, "BloomStrengthsW", runtime);
         }
         
         if (ImGui::SliderFloat("Strength: Final", &cb_luma_global_settings.GameSettings.BloomStrength, 0.f, 2.f))
            reshade::set_config_value(runtime, NAME, "BloomStrength", cb_luma_global_settings.GameSettings.BloomStrength);
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Bloom final multiplier.");
         DrawResetButton(cb_luma_global_settings.GameSettings.BloomStrength, default_luma_global_game_settings.BloomStrength, "BloomStrength", runtime);

         ImGui::NewLine();
         
         ImGui::PushID("###BloomThreshold");
         DrawColoredSubHeader("Threshold / High Pass Filter");
         ShaderDefineInfo::UIDropDown(ShaderDefineInfo::CUSTOM_BLOOM_THRESHOLD_1, "Mode",
            { "Vanilla (Crude)", "Slightly Neutral (Recommended)", "More Neutral (Alternative Style)" },
            "The high pass filter for bloom. How should it operate?"
            "\n"
            "\nThe original is a crude per-channel subtraction, horribly shifting hues and boosting saturation."
            "\nOne can say it's mimicking camera halation, but an example failure is \"When First Love Ends\", where red blobs of bloom ruins close ups of skin."
            "\n"
            "\nWe can do better by using luminance to blend towards neutral."
            "\n(Deliberate tinting still applies afterwards.)", false);
         ImGui::PopID();

         ImGui::NewLine();
         DrawColoredSubHeader("Alternative High Quality Blurring");

         if (ImGui::Checkbox("Enable", &Bloom::enabled))
         {
            reshade::set_config_value(runtime, NAME, Bloom::reshadesave_enabled, Bloom::enabled);
            if (!Bloom::enabled) Bloom::HardReset();
         }
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Reduce flickering and blockiness by using high quality gaussian blurring to downsample with an unbroken chain of mipmaps."
                                                                                          "\n"
                                                                                          "\n(Though impossible to be a 100%% direct vanilla upgrade due to new weights, it's tuned to be respectful."
                                                                                          "\nThere's also slight inefficiency decoupling from Auto-Exposure downsampling.)");

         if (GlobalsMegaMix::imgui_show_advanced)
         {
            if (!Bloom::enabled) ImGui::BeginDisabled();
            {
               if (ImGui::SliderFloat("Blur Softness Initial", &Bloom::sigma, 0.1f, 1.f))
                  reshade::set_config_value(runtime, NAME, Bloom::reshadesave_sigma, Bloom::sigma);
               if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Initial sigma for gaussian blur, where higher means wider radius."
                                                                                                "\n"
                                                                                                "\nIncreasing will suppress tiny highlights that cause bloom flickering,"
                                                                                                "\nbut also cost a bit of performance as texture sampling count increases.");
               DrawResetButton(Bloom::sigma, Bloom::sigma_def, Bloom::reshadesave_sigma, runtime);

               if (ImGui::SliderFloat("Blur Softness Increase", &Bloom::sigma_increase, 0.f, 1.f))
                  reshade::set_config_value(runtime, NAME, Bloom::reshadesave_sigma_increase, Bloom::sigma_increase);
               if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Additional sigma increase per deeper mipmap level."
                                                                                                "\nToo low and blockiness will reappear.");
               DrawResetButton(Bloom::sigma_increase, Bloom::sigma_increase_def, Bloom::reshadesave_sigma_increase, runtime);

               if (ImGui::Checkbox("Combine Using Highest Mip", &Bloom::use_highest_mip))
                  reshade::set_config_value(runtime, NAME, Bloom::reshadesave_use_highest_mip, Bloom::use_highest_mip);
               if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Prefer highest available mipmap level for final bloom combined output."
                                                                                                "\n(There should be no difference besides worse performance if on.)");
               DrawResetButton(Bloom::use_highest_mip, Bloom::use_highest_mip_def, Bloom::reshadesave_use_highest_mip, runtime);
            }
            if (!Bloom::enabled) ImGui::EndDisabled();
         }
         
         if (GlobalsMegaMix::imgui_show_advanced)
         {
            ImGui::NewLine();
            DrawColoredSubHeader("Auxiliary Resources");
            
            ImGui::TextWrapped("Mipmaps: %d", Bloom::Resources::nmips);
            for (int i = 0; i < Bloom::Resources::rtv_mips_y_viewports.size(); i++)
            {
               auto vp = Bloom::Resources::rtv_mips_y_viewports[i];
               if (vp.Width == 0) break; // reached end
               ImGui::Bullet(); ImGui::SameLine(); ImGui::TextWrapped("%d. %dx%d", i, (uint)vp.Width, (uint)vp.Height);
            }
         }
         
#if DEVELOPMENT
         ImGui::NewLine();
         DrawColoredSubHeader("DEVELOPMENT");
         if (ImGui::Button("Preview Toggle")) PreviewShaderOutput(device_data, 0xCD83E95E);
         ImGui::Checkbox("is_vanilla_bloom_blur_rtv_hq", &Bloom::is_vanilla_bloom_blur_rtv_hq);
#endif
      }
      ImGui::PopID();

      // ImGui::Separator(); ////////////////////////////////////////////////////////////////////////////////////

      ImGui::PushID("###AntiAliasing");
      if (DrawCollapsingHeaderEnabledColored("Anti-Aliasing", AntiAliasing::enabled > AntiAliasing::Vanilla))
      {
         DrawColoredSubHeader("Blur edges that should be smooth given more sub-pixel information.");

         // dropdown Vanilla, Disable, DLAA
         auto curr = static_cast<int>(AntiAliasing::enabled);
         if (ImGui::Combo("Mode", &curr, "(Weak) Morphological Anti-Aliasing (MLAA)\0Disallow\0(Strong) Directional Localized Anti-Aliasing (DLAA)\0(Balanced) Subpixel Morphological Anti-Aliasing (SMAA) w/ Depth Predication\0"))
         {
            reshade::set_config_value(runtime, NAME, AntiAliasing::reshadesave_enabled, curr);
            AntiAliasing::enabled = static_cast<AntiAliasing::Enabled>(curr);
         }
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Options for Anti-Aliasing."
                                                                                          "\n"
                                                                                          "\n[Directional Localized Anti-Aliasing (DLAA)]"
                                                                                          "\nMore lenient, DLAA will result in a more blurred/filmic look."
                                                                                          "\nDLAA was created to address difficulties implementing MLAA, resulting in an algorithm that blurs along the direction of edges."
                                                                                          "\nDebuting in Star Wars: The Force Unleashed II, it's one of the last AA methods before temporal solutions took over."
                                                                                          "\n"
                                                                                          "\n[Subpixel Morphological Anti-Aliasing (SMAA)]"
                                                                                          "\nSeen in CryTek & Call of Duty, it's another response to MLAA with more finer & reliable edge detection."
                                                                                          "\nIt'll blur stronger than MLAA, but won't overdo like DLAA."
                                                                                          "\n(This is the most taxing option for now, also taking in 3D depth to find edges.)");

         ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 0.5f, 1.f));
         ImGui::Bullet(); ImGui::SameLine(); ImGui::TextWrapped("Requires MLAA selected in graphics settings!");
         ImGui::PopStyleColor();

         ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.f));
         ImGui::Bullet(); ImGui::SameLine(); ImGui::TextWrapped("Btw, FXAA never seems to draw.");
         ImGui::PopStyleColor();

         ImGui::NewLine();
         DrawColoredSubHeader("Super Sampling Anti-Aliasing (SSAA): Bruteforce higher internal resolution.");

         if (!SSAA::enabled && ImGui::Button("Download SSAA Enabled by korenkonder")) Website::OpenWebsite("https://github.com/korenkonder/MMPlusMods/releases#release-SSAAEnable-1.03");
         
         if (!SSAA::enabled) ImGui::BeginDisabled();
         {
            ShaderDefineInfo::UIDropDown(ShaderDefineInfo::CUSTOM_SSAA_FILTER, "Filter", { "Point (Vanilla / Top Left Inner Pixel)", "Box (All 4 Inner Pixel Average)", "Mitchell-Netravali (4x4)" }, "The filter used to downsample the SSAA results to the final output.");
         }
         if (!SSAA::enabled) ImGui::EndDisabled();
         ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.f));
         ImGui::Bullet(); ImGui::SameLine(); ImGui::TextWrapped("Render Resolution: %dx%d", RenderTracker::res_main_color_size.x,  RenderTracker::res_main_color_size.y);
         ImGui::PopStyleColor();
      }
      ImGui::PopID();

      // ImGui::Separator(); ////////////////////////////////////////////////////////////////////////////////////

      ImGui::PushID("###DepthOfField");
      if (DrawCollapsingHeaderEnabledColored("Depth of Field", DepthOfField::enabled > DepthOfField::Vanilla))
      {
         DrawColoredSubHeader("Blurring of areas out of focus.");

         // dropdown Vanilla, Disable, DLAA
         auto curr = static_cast<int>(DepthOfField::enabled);
         if (ImGui::Combo("Mode", &curr, "Vanilla\0Disallow\0"))
         {
            reshade::set_config_value(runtime, NAME, DepthOfField::reshadesave_enabled, curr);
            DepthOfField::enabled = static_cast<DepthOfField::Enabled>(curr);
         }
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Options for Depth of Field."
                                                                                          "\n"
                                                                                          "\nTODO");

         ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.f));
         ImGui::Bullet(); ImGui::SameLine(); ImGui::TextWrapped("(HQ DoF is WIP.)");
         ImGui::PopStyleColor();
      }
      ImGui::PopID();

      // ImGui::Separator(); ////////////////////////////////////////////////////////////////////////////////////

      DuplicateWorldReflections::Resources::is_dirty_token = true; // grant dirty check token
      
      ImGui::PushID("###DuplicateWorldReflections");
      if (DrawCollapsingHeaderEnabledColored("Duplicate World Reflections", DuplicateWorldReflections::enabled))
      {
         DrawColoredSubHeader("Renders the scene again at the reflection angle/point for planar reflections.");

         if (ImGui::Checkbox("Enable", &DuplicateWorldReflections::enabled))
            reshade::set_config_value(runtime, NAME, DuplicateWorldReflections::reshadesave_enabled, DuplicateWorldReflections::enabled);
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Hijack rendering to enable settings below.\nEven if set at the original 512x256, this improves it by gaussian blurring.");

         ImGui::BeginDisabled(!DuplicateWorldReflections::enabled);
         {
            // res
            static int2 desired_resolution = {0,0};
            desired_resolution = int2(DuplicateWorldReflections::desired_resolution.x, DuplicateWorldReflections::desired_resolution.y);

            static float2 prev_output_resolution = { 0, 0 };
            static std::array<uint2, 4> res_presets = {};
            auto DrawTooltipForPreset = []() { if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Higher = sharper.\n(Based on render resolution.)"); };
            if (prev_output_resolution.x != RenderTracker::res_main_color_size.x || prev_output_resolution.y != RenderTracker::res_main_color_size.y)
            {
               prev_output_resolution = device_data.output_resolution;
               res_presets[0] = uint2(RenderTracker::res_main_color_size.x / 8.f, RenderTracker::res_main_color_size.y / 8.f);
               res_presets[1] = uint2(RenderTracker::res_main_color_size.x / 4.f, RenderTracker::res_main_color_size.y / 4.f);
               res_presets[2] = uint2(RenderTracker::res_main_color_size.x / 2.f, RenderTracker::res_main_color_size.y / 2.f);
               res_presets[3] = uint2(RenderTracker::res_main_color_size.x, RenderTracker::res_main_color_size.y); 
            }
            bool is_changed_by_preset = false;
            ImGui::SameLine(); if (ImGui::Button("512x256")) { desired_resolution = int2(512, 256); is_changed_by_preset = true; } DrawTooltipForPreset();
            ImGui::SameLine(); if (ImGui::Button(std::format("(Low) {}x{}", res_presets[1].x, res_presets[1].y).c_str())) { desired_resolution = int2(res_presets[1].x, res_presets[1].y); is_changed_by_preset = true; } DrawTooltipForPreset();
            ImGui::SameLine(); if (ImGui::Button(std::format("(Recommended) {}x{}", res_presets[2].x, res_presets[2].y).c_str())) { desired_resolution = int2(res_presets[2].x, res_presets[2].y); is_changed_by_preset = true; } DrawTooltipForPreset();
            ImGui::SameLine(); if (ImGui::Button(std::format("(High) {}x{}", res_presets[3].x, res_presets[3].y).c_str())) { desired_resolution = int2(res_presets[3].x, res_presets[3].y); is_changed_by_preset = true; } DrawTooltipForPreset();
            
            int max_resolution = max(device_data.output_resolution.x, device_data.output_resolution.y);
            if (ImGui::SliderInt2("Resolution", &desired_resolution.x, 16, max_resolution) || is_changed_by_preset)
            {
               desired_resolution.x = max(16, desired_resolution.x);
               desired_resolution.y = max(16, desired_resolution.y);
               DuplicateWorldReflections::desired_resolution = uint2(desired_resolution.x, desired_resolution.y);
               reshade::set_config_value(runtime, NAME, DuplicateWorldReflections::reshadesave_desired_resolution_x, DuplicateWorldReflections::desired_resolution.x);
               reshade::set_config_value(runtime, NAME, DuplicateWorldReflections::reshadesave_desired_resolution_y, DuplicateWorldReflections::desired_resolution.y);
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("New resolution for reflections.\n(The original is 512x256.)");
            
            if (GlobalsMegaMix::imgui_show_advanced)
            {
               // mips
               if (ImGui::SliderInt("Blur Mips", &DuplicateWorldReflections::blur_mips, 1, DuplicateWorldReflections::Resources::srvs_x.size()))
                  reshade::set_config_value(runtime, NAME, DuplicateWorldReflections::reshadesave_blur_mips, DuplicateWorldReflections::blur_mips);
               if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("The number of mips in the chain used to blur.\n(The original is a quick 4-tap box blur at 512x256.)");
               DrawResetButton(DuplicateWorldReflections::blur_mips, DuplicateWorldReflections::blur_mips_def, DuplicateWorldReflections::reshadesave_blur_mips, runtime);

               // sigma
               ShaderDefineInfo::UIDropDown(ShaderDefineInfo::CUSTOM_DWR_SIGMA, "Blur Softness", { "-4", "-3", "-2", "-1", "0", "1", "2", "3", "4", "5" },
                  "Change the gaussian kernel's sigma to control the material's roughness.");
            }

            if (ImGui::Checkbox("X Song Pack \"reflect_full\" Override", &DuplicateWorldReflections::override_full_resolution))
               reshade::set_config_value(runtime, NAME, DuplicateWorldReflections::reshadesave_override_full_resolution, DuplicateWorldReflections::override_full_resolution);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Allow override even for full resolution path.\n\n(This also skips its Depth of Field, which is honestly a waste.)");
            DrawResetButton(DuplicateWorldReflections::override_full_resolution, DuplicateWorldReflections::override_full_resolution_def, DuplicateWorldReflections::reshadesave_override_full_resolution, runtime);
         }
         ImGui::EndDisabled();

         if (GlobalsMegaMix::imgui_show_advanced)
         {
            ImGui::NewLine();
            DrawColoredSubHeader("Original Resources");

            for (int i = 0; i < DuplicateWorldReflections::Resources::handles.size(); i++)
            {
               DuplicateWorldReflections::Resources::HandleItem* item = &DuplicateWorldReflections::Resources::handles[i];
               ImGui::PushStyleColor(ImGuiCol_Text, item->is_verified ? ImVec4(0.4f, 1.f, 0.4f, 1.f) : ImVec4(1.f, 1.f, 0.4f, 1.f));
               ImGui::Bullet(); ImGui::SameLine(); ImGui::TextWrapped("%d: %s (%s)", i, item->is_verified ? "Yes" : "No", item->is_verified ? (!item->is_full_resolution ? "512x256" : "X Song Pack reflect_full") : "");
               ImGui::PopStyleColor();
            }
            
            if (ImGui::Button("Forget")) DuplicateWorldReflections::Resources::Reset_Handles();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Forget to then begin search again.\nShould not be needed unless you change the game's resolution or something.");

            ImGui::NewLine();
            DrawColoredSubHeader("Auxiliary Resources");
            
            ImGui::PushStyleColor(ImGuiCol_Text, DuplicateWorldReflections::Resources::IsReadyResource() ? ImVec4(0.4f, 1.f, 0.4f, 1.f) : ImVec4(1.f, 1.f, 0.4f, 1.f));
            ImGui::Bullet(); ImGui::SameLine(); ImGui::TextWrapped("New Ready: %s", DuplicateWorldReflections::Resources::IsReadyResource() ? "Yes" : "No");
            ImGui::PopStyleColor();

            ImGui::SameLine();
            if (ImGui::Button("Reset")) DuplicateWorldReflections::Resources::Reset_Resources();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Release new texture resources to recreate them.\nShould not be needed unless you change the game's resolution or something.");
         }
      }
      ImGui::PopID();

      // ImGui::Separator(); ////////////////////////////////////////////////////////////////////////////////////

      ImGui::PushID("###SpotLightShadows");
      if (DrawCollapsingHeaderEnabledColored("Spotlight Lighting Pass", SpotLightShadows::enabled))
      {
         DrawColoredSubHeader("A separate pass for many accurate spotlights.");

         if (ImGui::Checkbox("Full Resolution", &SpotLightShadows::enabled))
            reshade::set_config_value(runtime, NAME, SpotLightShadows::reshadesave_enabled, SpotLightShadows::enabled);
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Make the separated spotlights lighting pass resolve to a full resolution color buffer.");

         ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.f));
         ImGui::Bullet(); ImGui::SameLine(); ImGui::TextWrapped("This FX is rather rare. 2 PVs using this are Meiteki Cybernetics & Gaikotsu Gakudan to Riria.");
         ImGui::PopStyleColor();
      }
      ImGui::PopID();
      
      // ImGui::Separator(); ////////////////////////////////////////////////////////////////////////////////////

      ImGui::PushID("###PS4Blur");
      if (DrawCollapsingHeaderEnabledColored("PS3/PS4 Frame Blending", ShaderDefineInfo::GetB(ShaderDefineInfo::CUSTOM_PS4BLUR_1)))
      {
         DrawColoredSubHeader("Insert previous frame blending/ghosting seen in Dreamy Theater (PS3) & Future Tone (PS4).");

         auto d = ShaderDefineInfo::UIDropDown(ShaderDefineInfo::CUSTOM_PS4BLUR_1, "Mode", { "Off", "On", "Horizontal Interlacing" }, "(Interlacing doesn't save performance.)");

         if (d != 1) ImGui::BeginDisabled();
         {
            if (ImGui::SliderFloat("Blend Ratio", &cb_luma_global_settings.GameSettings.FrameBlendRatio, 0.f, 0.5f))
               reshade::set_config_value(runtime, NAME, "FrameBlendRatio", cb_luma_global_settings.GameSettings.FrameBlendRatio);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("The percentage of the previous frame to blend into the current."
                                                                                             "\n"
                                                                                             "\n(This should be set by the PV, usually around 0.25-0.5,"
                                                                                             "\nbut the shader from PS4 never draws on PC to provide the intended value.)");
            DrawResetButton(cb_luma_global_settings.GameSettings.FrameBlendRatio, default_luma_global_game_settings.FrameBlendRatio, "FrameBlendRatio", runtime);
         }
         if (d != 1) ImGui::EndDisabled();

         ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.f));
         ImGui::Bullet(); ImGui::SameLine(); ImGui::TextWrapped("For 60 FPS, and doesn't affect UI like original.");
         ImGui::Bullet(); ImGui::SameLine(); ImGui::TextWrapped("(This is barf inducing lol, but it's here for preservation.)");
         ImGui::PopStyleColor();
      }
      ImGui::PopID();

      // ImGui::Separator(); ////////////////////////////////////////////////////////////////////////////////////

      ImGui::PushID("###AutoExposure");
      if (DrawCollapsingHeaderEnabledColored("Auto-Exposure", AutoExposureFix::fps_limit > 0))
      {
         DrawColoredSubHeader("Limit how fast Auto-Exposure history is written, reducing rapid exposure changes on high FPS.");
         
         if (ImGui::SliderInt("Rate", &AutoExposureFix::fps_limit, 0, 60, AutoExposureFix::fps_limit > 0 ? "%d FPS" : "No Limit"))
         {
            reshade::set_config_value(runtime, NAME, AutoExposureFix::reshadesave, AutoExposureFix::fps_limit);
            AutoExposureFix::OnUserSettingsChange();
         }
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Auto-Exposure history (32px ring buffer) is done per-frame.\nOn high FPS, this causes rapid exposure changes as older history is rapidly overriden.\n\nThis feature will limit Auto-Exposure rate,\nwhile allowing camera cuts to clear history.");
         DrawResetButton(AutoExposureFix::fps_limit, 60, AutoExposureFix::reshadesave, runtime);
      }
      ImGui::PopID();

      // ImGui::Separator(); ////////////////////////////////////////////////////////////////////////////////////

      ImGui::PushID("###ProgressBar");
      if (DrawCollapsingHeaderEnabledColored("Progress Bar", ShaderDefineInfo::Get(ShaderDefineInfo::CUSTOM_PROGRESSBAR) > 0))
      {
         ProgressBar::OnUI(runtime);
      }
      ImGui::PopID();
      
      // ImGui::Separator(); ////////////////////////////////////////////////////////////////////////////////////

      if (DrawCollapsingHeaderEnabledColored("UI", !GlobalsMegaMix::is_draw_game_ui || !GlobalsMegaMix::is_draw_game_ui_text))
      {
         if (ImGui::Checkbox("Draw UI", &GlobalsMegaMix::is_draw_game_ui))
            reshade::set_config_value(runtime, NAME, "IsUI", GlobalsMegaMix::is_draw_game_ui);
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Toggle UI.\nIf off, will discard all UI sprite shaders after scene rendering.");
         DrawResetButton(GlobalsMegaMix::is_draw_game_ui, true, "IsUI", runtime);
         
         if (ImGui::Checkbox("Draw UI Text", &GlobalsMegaMix::is_draw_game_ui_text))
            reshade::set_config_value(runtime, NAME, "IsUIText", GlobalsMegaMix::is_draw_game_ui_text);
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Skips text (at least subtitles) after scene rendering.");
         DrawResetButton(GlobalsMegaMix::is_draw_game_ui_text, true, "IsUIText", runtime);
      }

      // ImGui::Separator(); ////////////////////////////////////////////////////////////////////////////////////
      
      //show advanced
      if (!GlobalsMegaMix::imgui_show_advanced)
      {
         ImGui::Separator(); ////////////////////////////////////////////////////////////////////////////////////
         if (ImGui::Checkbox("Show Advanced Settings", &GlobalsMegaMix::imgui_show_advanced))
            reshade::set_config_value(runtime, NAME, "UIIsAdvanced", GlobalsMegaMix::imgui_show_advanced);
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Reveal hidden complex settings requiring more reading.");

#if DEVELOPMENT
         ImGui::Separator();
#endif
         return;
      }
      else
      {
         ImGui::Separator();
      }
      
      // ImGui::Separator(); ////////////////////////////////////////////////////////////////////////////////////

      ImGui::PushID("###LUTBlowoutReduction");
      if (!is_sdr && DrawCollapsingHeaderEnabledColored("LUT Blowout Reduction", ShaderDefineInfo::GetB(ShaderDefineInfo::CUSTOM_LUT_BLOWOUT_GAUSSIAN)))
      {
         DrawColoredSubHeader("Regain chrominance for HDR highlights by creating bias on blowout curve via gaussian weighted sampling.");
         
         is_disabled = !ShaderDefineInfo::UIToggleCheckmark(ShaderDefineInfo::CUSTOM_LUT_BLOWOUT_GAUSSIAN, "Enabled", "Sample the YCbCr LUT in charged of causing blowout with a gaussian blur,\nbiased towards higher chrominance, helping reduce steep chrominance falloff for HDR's additional stops.");
         if (is_disabled) ImGui::BeginDisabled();
         {
            ShaderDefineInfo::UIToggleCheckmark(ShaderDefineInfo::CUSTOM_LUT_BLOWOUT_GAUSSIAN_STOPS, "Respond to HDR Stops", "Increases step size as HDR stops increases.");
            
            if (ImGui::SliderFloat("Step", &cb_luma_global_settings.GameSettings.LUTGaussianBlurStep, 1.f, 80.f, "%.1f"))
               reshade::set_config_value(runtime, NAME, "LUTGaussianBlurStep", cb_luma_global_settings.GameSettings.LUTGaussianBlurStep);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("The step size for the gaussian blur when sampling the LUT for blowout reduction.\nHigher values will be recover and smooth out chrominance falloff.");
            DrawResetButton(cb_luma_global_settings.GameSettings.LUTGaussianBlurStep, default_luma_global_game_settings.LUTGaussianBlurStep, "LUTGaussianBlurStep", runtime);
            
            if (ImGui::SliderFloat("Bias", &cb_luma_global_settings.GameSettings.LUTGaussianBlurBias, 0.f, 10.f, "%.4f"))
               reshade::set_config_value(runtime, NAME, "LUTGaussianBlurBias", cb_luma_global_settings.GameSettings.LUTGaussianBlurBias);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("The bias for the gaussian blur when sampling the LUT for blowout reduction.\nHigher values will bias the sampling towards higher chrominance, which recovers chrominance.");
            DrawResetButton(cb_luma_global_settings.GameSettings.LUTGaussianBlurBias, default_luma_global_game_settings.LUTGaussianBlurBias, "LUTGaussianBlurBias", runtime); 
         }
         if (is_disabled) ImGui::EndDisabled();
      }
      ImGui::PopID();

      // ImGui::Separator(); ////////////////////////////////////////////////////////////////////////////////////

      //HDR Tonemapper Settings
      if (!is_sdr && ImGui::CollapsingHeader("HDR Display-mapping"))
      {
         DrawColoredSubHeader("Controls how colors are compressed down to fit display.");

         {
            if (ImGui::SliderFloat("HDR Tonemapper Expected Max", &CachedCB::white_clip, 0.0001f, 0.2f, "%.4f"))
               reshade::set_config_value(runtime, NAME, "TonemapperMaxExpected", CachedCB::white_clip);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("HDR tonemapper's expected max nits (this is a multiplier to an internal value).\nReduce to cause white clipping.");
            DrawResetButton(CachedCB::white_clip, CachedCB::white_clip_def, "TonemapperMaxExpected", runtime);
         }

         // is_disabled = tonemap_def == 0;
         {
            ShaderDefineInfo::UIDropDown(ShaderDefineInfo::CUSTOM_TONEMAP_SCALING, "HDR Tonemapper Scaling",
               { "Luminance (Natural / Vanilla)", "Max Channel (Unnatural Saturation Preserve?)" },
               "The pivot for the tonemapper to use and scale color.");
         }

         {
            //CUSTOM_PCC_QUALITY
            bool def = ShaderDefineInfo::UIDropDown(ShaderDefineInfo::CUSTOM_PCC_QUALITY, "Per-Channel Blowout: Quality", { "Normal (Luminance Revert)", "High (UCS Blend)" }, "Low simply reverts luminance to maintain hue/chrominance change.\nHigh will use UCS to blend to new hue/chrominance.");
         }

         //CUSTOM_PERCHANNELLUMAEMULATE
         {
            if (ImGui::SliderFloat("Per-Channel Luminance Reduction: Strength", &cb_luma_global_settings.GameSettings.PerChannelLuminanceReductionEmulateStrength, 0.f, 1.f, "%.4f"))
               reshade::set_config_value(runtime, NAME, "PerChannelLuminanceReductionEmulateStrength", cb_luma_global_settings.GameSettings.PerChannelLuminanceReductionEmulateStrength);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Emulate the luminance loss from LDR per-channel tonemapping on bright single channel colors.");
            DrawResetButton(cb_luma_global_settings.GameSettings.PerChannelLuminanceReductionEmulateStrength, default_luma_global_game_settings.PerChannelLuminanceReductionEmulateStrength, "PerChannelLuminanceReductionEmulateStrength", runtime);

            ShaderDefineInfo::Set(ShaderDefineInfo::CUSTOM_PERCHANNELLUMAEMULATE, cb_luma_global_settings.GameSettings.PerChannelLuminanceReductionEmulateStrength > 0);
         }

         // is_disabled = tonemap_def == 0 || scaling_def != 0;
         {
            ShaderDefineInfo::UIDropDown(ShaderDefineInfo::CUSTOM_TONEMAP_CLAMP, "HDR Tonemapper Clamp",
               { "Unclamped (Up to Display)", "Per-Channel Clamp (Blows Out / Vanilla)", "Max Channel Clamp (Unnatural Saturation Preserve?)" },
               "How should overshoots from HDR tonemap be handled.");
         }

         ShaderDefineInfo::UIDropDown(ShaderDefineInfo::CUSTOM_CLAMP_PEAK, "Output Clamp Peak",
            { "Unclamped (Up to Display)", "Per-Channel Clamp (Blows Out / Vanilla)", "Max Channel Clamp (Unnatural Saturation Preserve?)", "Per-Channel Rolloff Slightly Above Peak (Blows Out / Vanilla+)" },
            "Clamp of the very final output color to display.");
      }
      
      // ImGui::Separator(); ////////////////////////////////////////////////////////////////////////////////////
      
      if (!is_sdr && DrawCollapsingHeaderEnabledColored("Fake BT2020 (Gamut Expansion)", ShaderDefineInfo::GetB(ShaderDefineInfo::CUSTOM_FAKEBT2020)))
      {
         DrawColoredSubHeader("Fake saturation to from gamma tomfoolery.");

         ImGui::Bullet(); ImGui::SameLine(); ImGui::TextWrapped("if on, you'll want Gamma Correction \"Perceptual\" mode to control shadows.");
         
         bool def = ShaderDefineInfo::UIToggleCheckmark(ShaderDefineInfo::CUSTOM_FAKEBT2020, "Fake BT2020", "A gamma utilizing gamut expansion.");
         
         is_disabled = !def;
         if (is_disabled) ImGui::BeginDisabled();
         {
            if (ImGui::SliderFloat("Fake BT2020: Chrominance", &cb_luma_global_settings.GameSettings.FakeBT2020Chroma, 0.f, 1.f, "%.4f"))
               reshade::set_config_value(runtime, NAME, "FakeBT2020Chroma", cb_luma_global_settings.GameSettings.FakeBT2020Chroma);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("A gamma utilizing gamut expansion.\nThis is the amount of chrominance/saturation boost.");
            DrawResetButton(cb_luma_global_settings.GameSettings.FakeBT2020Chroma, default_luma_global_game_settings.FakeBT2020Chroma, "FakeBT2020Chroma", runtime);

            if (ImGui::SliderFloat("Fake BT2020: Luminance", &cb_luma_global_settings.GameSettings.FakeBT2020Luma, 0.f, 1.f, "%.4f"))
               reshade::set_config_value(runtime, NAME, "FakeBT2020Luma", cb_luma_global_settings.GameSettings.FakeBT2020Luma);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("A gamma utilizing gamut expansion.\nExpansion darkens/deepens color, and this is the amount.");
            DrawResetButton(cb_luma_global_settings.GameSettings.FakeBT2020Luma, default_luma_global_game_settings.FakeBT2020Luma, "FakeBT2020Luma", runtime);
         }
         if (is_disabled) ImGui::EndDisabled(); 
      }

      // ImGui::Separator(); ////////////////////////////////////////////////////////////////////////////////////
      if (!is_sdr && DrawCollapsingHeaderEnabledColored("HDR Color Grading", ShaderDefineInfo::GetB(ShaderDefineInfo::CUSTOM_COLORGRADE)))
      {
         DrawColoredSubHeader("RenoDX luminance color grading, like user audio equalizer but for color.");
         
         int def = ShaderDefineInfo::UIDropDown(ShaderDefineInfo::CUSTOM_COLORGRADE, "Color Grading: Mode", { "Off", "Before UI", "After UI" }, "If inserted, when?");
      
         is_disabled = !def;
         if (is_disabled) ImGui::BeginDisabled(); 
         {
            if (ImGui::SliderFloat("Color Grading: Contrast", &cb_luma_global_settings.GameSettings.CGContrast, 0.f, 2.f, "%.4f"))
               reshade::set_config_value(runtime, NAME, "CGContrast", cb_luma_global_settings.GameSettings.CGContrast);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("RenoDX power based contrast.");
            DrawResetButton(cb_luma_global_settings.GameSettings.CGContrast, default_luma_global_game_settings.CGContrast, "CGContrast", runtime);
         
            if (ImGui::SliderFloat("Color Grading: Contrast Mid Gray", &cb_luma_global_settings.GameSettings.CGContrastMidGray, 0.f, 500.f))
               reshade::set_config_value(runtime, NAME, "CGContrastMidGray", cb_luma_global_settings.GameSettings.CGContrastMidGray);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Contrast's mid gray value to stretch in/out luminance.");
            DrawResetButton(cb_luma_global_settings.GameSettings.CGContrastMidGray, default_luma_global_game_settings.CGContrastMidGray, "CGContrastMidGray", runtime);
         
            if (ImGui::SliderFloat("Color Grading: Highlights", &cb_luma_global_settings.GameSettings.CGHighlightsStrength, 0.f, 2.f))
               reshade::set_config_value(runtime, NAME, "CGHighlightsStrength", cb_luma_global_settings.GameSettings.CGHighlightsStrength);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("RenoDX highlights boost/compress.");
            DrawResetButton(cb_luma_global_settings.GameSettings.CGHighlightsStrength, default_luma_global_game_settings.CGHighlightsStrength, "CGHighlightsStrength", runtime);
         
            if (ImGui::SliderFloat("Color Grading: Highlights Mid Gray", &cb_luma_global_settings.GameSettings.CGHighlightsMidGray, 0.f, 500.f))
               reshade::set_config_value(runtime, NAME, "CGHighlightsMidGray", cb_luma_global_settings.GameSettings.CGHighlightsMidGray);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Highlights mid gray / threshold value to manipulate luminance around.");
            DrawResetButton(cb_luma_global_settings.GameSettings.CGHighlightsMidGray, default_luma_global_game_settings.CGHighlightsMidGray, "CGHighlightsMidGray", runtime);
     
            if (ImGui::SliderFloat("Color Grading: Shadows", &cb_luma_global_settings.GameSettings.CGShadowsStrength, 0.f, 2.f))
               reshade::set_config_value(runtime, NAME, "CGShadowsStrength", cb_luma_global_settings.GameSettings.CGShadowsStrength);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("RenoDX shadows boost/compress.");
            DrawResetButton(cb_luma_global_settings.GameSettings.CGShadowsStrength, default_luma_global_game_settings.CGShadowsStrength, "CGShadowsStrength", runtime);
         
            if (ImGui::SliderFloat("Color Grading: Shadows Mid Gray", &cb_luma_global_settings.GameSettings.CGShadowsMidGray, 0.f, 500.f))
               reshade::set_config_value(runtime, NAME, "CGShadowsMidGray", cb_luma_global_settings.GameSettings.CGShadowsMidGray);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Shadows mid gray / threshold value to manipulate luminance around.");
            DrawResetButton(cb_luma_global_settings.GameSettings.CGShadowsMidGray, default_luma_global_game_settings.CGShadowsMidGray, "CGShadowsMidGray", runtime);
         }
         if (is_disabled) ImGui::EndDisabled();

         ImGui::NewLine(); ///////////

         int cg_def_sat = ShaderDefineInfo::UIDropDown(ShaderDefineInfo::CUSTOM_COLORGRADE_SATORDER, "Color Grading: Saturation Order",
            { "Off", "Before UI (In standard BT709)", "After UI (In wide BT2020)" },
            nullptr);
         is_disabled = cg_def_sat == 0;
         if (is_disabled) ImGui::BeginDisabled(); 
         if (ImGui::SliderFloat("Color Grading: Saturation", &cb_luma_global_settings.GameSettings.CGSaturation, 0.f, 2.f, "%.4f"))
            reshade::set_config_value(runtime, NAME, "CGSaturation", cb_luma_global_settings.GameSettings.CGSaturation);
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Global multiplier for chrominance/saturation.");
         DrawResetButton(cb_luma_global_settings.GameSettings.CGSaturation, default_luma_global_game_settings.CGSaturation, "CGSaturation", runtime);
         if (is_disabled) ImGui::EndDisabled();
      }

      // ImGui::Separator(); ////////////////////////////////////////////////////////////////////////////////////
      if (DrawCollapsingHeaderEnabledColored("FPS Limiter", HighFPS::enabled))
      {
         DrawColoredSubHeader("Remove/Replace FPS limit.");
         
         ImGui::BulletText("Don't use with HighFPS mod (which is better because high resolution timers?).");
         
         if (ImGui::Checkbox("High FPS: Active", &HighFPS::enabled))
         {
            reshade::set_config_value(runtime, NAME, "HighFPS_enabled", HighFPS::enabled);
            if (!HighFPS::enabled) HighFPS::Unpatch();
         }
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Continuously patches the game's memory to set a new limit.");
      
         is_disabled = !HighFPS::enabled;
         if (is_disabled) ImGui::BeginDisabled(is_disabled);
         {
            if (ImGui::Checkbox("High FPS: Limit Menus", &HighFPS::menu_clamp))
               reshade::set_config_value(runtime, NAME, "HighFPS_menu_clamp", HighFPS::menu_clamp);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Limit menus to 60FPS.\n\nBtw, unclamping allows for faster UI navigation and decreased load times (loading has frame rate dependent camera spinning phase to warm up level).");
      
            if (ImGui::SliderInt("High FPS: New Limit", &HighFPS::limit, 0, 1000))
            {
               if (HighFPS::limit > 0 && HighFPS::limit < 15) HighFPS::limit = 15; //minimum 15 FPS
               reshade::set_config_value(runtime, NAME, "HighFPS_limit", HighFPS::limit);
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("The new limit.\nSet 0 for none.");
         }
         if (is_disabled) ImGui::EndDisabled();
      }

      // ImGui::Separator(); ////////////////////////////////////////////////////////////////////////////////////
      
      if (!is_sdr && ImGui::CollapsingHeader("Fake/Auto HDR (DEPRECATED)"))
      {
         DrawColoredSubHeader("Now deprecated, this fakes HDR extension for some SDR content.");
         
         {
            bool def = ShaderDefineInfo::UIToggleCheckmark(ShaderDefineInfo::CUSTOM_UPSCALE_MOV, "Upscale FMV", "Apply an inverse tonemapper to SDR movies.");
            is_disabled = !def;
         }
         if (is_disabled) ImGui::BeginDisabled(); 
         {
            if (ImGui::SliderFloat("Upscale FMV: Shoulder Power", &cb_luma_global_settings.GameSettings.UpscaleMovPumboPow, 0.f, 5.f))
               reshade::set_config_value(runtime, NAME, "UpscaleMovPumboPow", cb_luma_global_settings.GameSettings.UpscaleMovPumboPow);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("FMV PumboAutoHDR shoulder power.");
            DrawResetButton(cb_luma_global_settings.GameSettings.UpscaleMovPumboPow, default_luma_global_game_settings.UpscaleMovPumboPow, "UpscaleMovPumboPow", runtime);
         }
         if (is_disabled) ImGui::EndDisabled();
      
         ImGui::NewLine(); ///////////
         
         {
            bool def = ShaderDefineInfo::UIToggleCheckmark(ShaderDefineInfo::CUSTOM_UPSCALE_BGSPRITES, "Upscale BG Sprites", "Apply an inverse tonemapper to SDR limited background sprites.\n\nThis help balance it with unclamped 3D HDR elements render atop.\nFalse positives may include Amatsu Kitsune's moon at ending if this is tuned too high.");
            is_disabled = !def;
         }
         if (is_disabled) ImGui::BeginDisabled(); 
         {
            if (ImGui::SliderFloat("Upscale BG Sprites: Max Input", &cb_luma_global_settings.GameSettings.UpscaleBGSpritesMax, 1.f, 6.f))
               reshade::set_config_value(runtime, NAME, "UpscaleBGSpritesMax", cb_luma_global_settings.GameSettings.UpscaleBGSpritesMax);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Max value expected by inverse tonemap for SDR background sprites in complex scenes.");
            DrawResetButton(cb_luma_global_settings.GameSettings.UpscaleBGSpritesMax, default_luma_global_game_settings.UpscaleBGSpritesMax, "UpscaleBGSpritesMax", runtime);
      
            if (ImGui::SliderFloat("Upscale BG Sprites: Exposure", &cb_luma_global_settings.GameSettings.UpscaleBGSpritesExp, 0.f, 1.f))
               reshade::set_config_value(runtime, NAME, "UpscaleBGSpritesExp", cb_luma_global_settings.GameSettings.UpscaleBGSpritesExp);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Max value expected by inverse tonemap for SDR background sprites in complex scenes.");
            DrawResetButton(cb_luma_global_settings.GameSettings.UpscaleBGSpritesExp, default_luma_global_game_settings.UpscaleBGSpritesExp, "UpscaleBGSpritesExp", runtime);
         }
         if (is_disabled) ImGui::EndDisabled(); 
      
         ImGui::NewLine(); ///////////
         
         {
            ShaderDefineInfo::UIDropDown(ShaderDefineInfo::CUSTOM_UPSCALE_TOON, "Upscale Toon Mode",
               { "Forced SDR", "Off / Treat as Complex", "On", "On (Ignore Customization Menu)" },
               "Apply an inverse tonemapper to SDR limited toon shaded scenes.");
         }
         is_disabled = ShaderDefineInfo::Get(ShaderDefineInfo::CUSTOM_UPSCALE_TOON) <= 1;
         if (is_disabled) ImGui::BeginDisabled(); 
         {
            ImGui::PushID("Upscale Toon: 0");
            /*ImGui::SameLine();*/ if (ImGui::Button("(Light)"))
            {
               cb_luma_global_settings.GameSettings.UpscaleToonMax = default_luma_global_game_settings.UpscaleToonMax; 
               cb_luma_global_settings.GameSettings.UpscaleToonExp = default_luma_global_game_settings.UpscaleToonExp;
               reshade::set_config_value(runtime, NAME, "UpscaleToonMax", cb_luma_global_settings.GameSettings.UpscaleToonMax);
               reshade::set_config_value(runtime, NAME, "UpscaleToonExp", cb_luma_global_settings.GameSettings.UpscaleToonExp);
            }
            ImGui::PopID();
      
            ImGui::PushID("Upscale Toon: 1");
            ImGui::SameLine(); if (ImGui::Button("(Aggressive)"))
            {
               cb_luma_global_settings.GameSettings.UpscaleToonMax = default_luma_global_game_settings.UpscaleToonMax; 
               cb_luma_global_settings.GameSettings.UpscaleToonExp = 0.36f;
               reshade::set_config_value(runtime, NAME, "UpscaleToonMax", cb_luma_global_settings.GameSettings.UpscaleToonMax);
               reshade::set_config_value(runtime, NAME, "UpscaleToonExp", cb_luma_global_settings.GameSettings.UpscaleToonExp);
            }
            ImGui::PopID();
         }
         if (ImGui::SliderFloat("Upscale Toon: Max Input", &cb_luma_global_settings.GameSettings.UpscaleToonMax, 1.f, 2.f))
            reshade::set_config_value(runtime, NAME, "UpscaleToonMax", cb_luma_global_settings.GameSettings.UpscaleToonMax);
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Max input brightness expected by inverse tonemap for toon shading scenes.");
         DrawResetButton(cb_luma_global_settings.GameSettings.UpscaleToonMax, default_luma_global_game_settings.UpscaleToonMax, "UpscaleToonMax", runtime);
      
         if (ImGui::SliderFloat("Upscale Toon: Exposure", &cb_luma_global_settings.GameSettings.UpscaleToonExp, 0.f, 1.f))
            reshade::set_config_value(runtime, NAME, "UpscaleToonExp", cb_luma_global_settings.GameSettings.UpscaleToonExp);
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Exposure multiplier for color before inverse tonemap for toon shading scenes.");
         DrawResetButton(cb_luma_global_settings.GameSettings.UpscaleToonExp, default_luma_global_game_settings.UpscaleToonExp, "UpscaleToonExp", runtime);
         
         if (is_disabled) ImGui::EndDisabled(); 
      }
      
      // ImGui::Separator(); ////////////////////////////////////////////////////////////////////////////////////
      if (ImGui::CollapsingHeader("Miscellaneous Pipeline Options (Debug)"))
      {
         DrawColoredSubHeader("Various debug views.");
         
         // if (ImGui::SliderFloat("UI Transparency", &cb_luma_global_settings.GameSettings.UITransparency, 0.f, 1.f))
         //    reshade::set_config_value(runtime, NAME, "UITransparency", cb_luma_global_settings.GameSettings.UITransparency);
         // if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Do some crazy backend RTV switcheroo to separate out UI.\nMay cost performance.");
         // DrawResetButton(cb_luma_global_settings.GameSettings.UITransparency, default_luma_global_game_settings.UITransparency, "UITransparency", runtime);
         // ShaderDefineInfo::Set(ShaderDefineInfo::CUSTOM_UITRANSPARENCY, cb_luma_global_settings.GameSettings.UITransparency < 1.f);

         // {"CUSTOM_TESTBGSPRITES", '0', true, false, "Test BG Sprites layering.", 2},
         {
            ShaderDefineInfo::UIDropDown(ShaderDefineInfo::CUSTOM_TESTBGSPRITES, "BG Sprites Test",
               { "Off", "BG Sprites Only", "3D Only" },
               "For testing Background Sprite layering.");
         }

         //CUSTOM_UPGRADE_DEBUG
         {
            ShaderDefineInfo::UIDropDown(ShaderDefineInfo::CUSTOM_UPGRADE_DEBUG, "UpgradeToneMap() Inputs",
               { "Off", "Raw HDR", "Neutral SDR", "Graded SDR (Unclamped)" },
               "Toggle between various inputs used in RenoDX's UpgradeToneMap() algorithm to map HDR luminance onto SDR chrominance used to neutralizes rolloff curve.");
         }

         //CUSTOM_TONEMAP_IDENTIFY
         ShaderDefineInfo::UIToggleCheckmark(ShaderDefineInfo::CUSTOM_TONEMAP_IDENTIFY, "Tonemap Variant Identify", "At the top of the screen, draw which tonemap variant is being used this frame using the newly given ID in binary representation.");
      }
      
      // ImGui::Separator(); ////////////////////////////////////////////////////////////////////////////////////
      if (ImGui::CollapsingHeader("(Debug) Info"))
      {
         DrawColoredSubHeader("Various debug values/stats.");
         
         const int ti = TonemapInfo::GetIndexOnlyIfDrawn(TonemapInfo::tonemapinfo_prev);
         
         std::string s = "Tonemap Uber Variant: " + std::to_string(ti);
         ImGui::BulletText(s.c_str());
         
         std::string s99 = "Tonemap Debug Info: " + (TonemapInfo::GetIndexOnlyIfDrawn(TonemapInfo::tonemapinfo_prev) >= 0 ? static_cast<std::string>(TonemapInfo::TonemapDebugInfo[ti]) : "N/A");
         ImGui::BulletText(s99.c_str());
         
         std::string s1 = "Drawn Final: " + std::to_string(TonemapInfo::GetDrawnFinal(TonemapInfo::tonemapinfo_prev));
         ImGui::BulletText(s1.c_str());
         
         std::string s6 = "Drawn Sprites HPBarDelta: " + std::to_string(TonemapInfo::GetDrawnHPBarDelta(TonemapInfo::tonemapinfo_prev));
         ImGui::BulletText(s6.c_str());
         
         // std::string s5 = "FMV Mode Detected: " + std::to_string(TonemapInfo::GetIsFMV(Globals::TonemapInfoBackup));
         // ImGui::BulletText(s5.c_str());

         // std::string s7 = "MLAA Detected: " + std::to_string(ResourceTracker::IsDrawnMLAAPrev);
         // ImGui::BulletText(s7.c_str());
         
         std::string s4 = "Swapchain Change Count: " + std::to_string(GlobalsMegaMix::swapchain_change_count);
         ImGui::BulletText(s4.c_str());
         
         // std::string s7 = "Auto-Exposure Fix Is Allow Draw: " + std::to_string(AutoExposureFix::);
         // ImGui::BulletText(s7.c_str());

         std::string s9 = "Auto-Exposure Fix Ring Buffer Index Override: " + std::to_string(AutoExposureFix::viewport_curr_index);
         ImGui::BulletText(s9.c_str());
         
         // std::string s2 = "SK Mode: " + std::to_string(Globals::IsSKMode);
         // ImGui::BulletText(s2.c_str());

         std::string s3 = "Tonemapper Peak Cached: " + std::to_string(cb_luma_global_settings.GameSettings.TonemapperPeakCached);
         ImGui::BulletText(s3.c_str());
         
         std::string s8 = "Tonemapper Max Expected Cached: " + std::to_string(cb_luma_global_settings.GameSettings.TonemapperMaxExpectedCached);
         ImGui::BulletText(s8.c_str());

         std::string s10 = "Intermediate Scaling Cached: " + std::to_string(cb_luma_global_settings.GameSettings.IntermediateScalingCached);
         ImGui::BulletText(s10.c_str());

         std::string s11 = "OS Time ms: " + std::to_string(TimerTimestamp::ms_curr);
         ImGui::BulletText(s11.c_str());
      }
      
      // ImGui::Separator(); ////////////////////////////////////////////////////////////////////////////////////

#if DEVELOPMENT
      if (ImGui::CollapsingHeader("(DEVELOPMENT) LUTBuilderScan"))
      {
         // for each list scanned_lut_res
         ImGui::Text("LUT Resource Handles:");
         for (const uint64_t lut_res : LUTBuilderScan::scanned_lut_res)
         {
            if (LUTBuilderScan::prev_used_res == lut_res) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 1.0f, 0.0f, 1.0f));
            ImGui::BulletText(std::to_string(lut_res).c_str());
            if (LUTBuilderScan::prev_used_res == lut_res) ImGui::PopStyleColor();
         }
      }
#endif
      
      ImGui::Separator(); ////////////////////////////////////////////////////////////////////////////////////
      
      if (ImGui::Checkbox("Show Advanced Settings", &GlobalsMegaMix::imgui_show_advanced)) reshade::set_config_value(runtime, NAME, "UIIsAdvanced", GlobalsMegaMix::imgui_show_advanced);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Reveal hidden complex settings requiring more reading.");

      if (ImGui::Checkbox("Hide README", &GlobalsMegaMix::imgui_readme_done)) reshade::set_config_value(runtime, NAME, "UIIsReadmeDone", GlobalsMegaMix::imgui_readme_done);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Show README again.");

      if (ImGui::Button("Panic Reset")) OnInitSwapchain_ResetEvent(false);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Act as if the game had just changed resolution, resetting the states of enhancements.");
      
   }

#if DEVELOPMENT
   void DrawImGuiDevSettings(DeviceData& device_data) override
   {
      ImGui::Separator();
   }
#endif
   
   void PrintImGuiAbout() override
   {
      // ImGui::Separator(); ////////////////////////////////////////////////////////////////////////////////////

      ImGui::Text("Build Date:");
      ImGui::BulletText(__DATE__);
      ImGui::BulletText(__TIME__);
      
      ImGui::NewLine();

      ImGui::Text("Credits:");
      ImGui::BulletText("Luma: Pumbo (Filoppi)");
      ImGui::BulletText("RenoDX: clshortfuse");
      ImGui::BulletText("Mod: XgarhontX");
      ImGui::BulletText("Development Help & Bug Hunter: MLGSmallSmoke35");
      ImGui::BulletText("Bug Hunter, Benchmarker, and Tester: Pikota");
      ImGui::BulletText("Bug Hunter: Pino");
      ImGui::BulletText("Testing & Suggestions: neocodex");
      ImGui::BulletText("Bug Hunter: Jorge");
      ImGui::BulletText("Bug Hunter: ch6rlez");
      ImGui::BulletText("Bug Hunter: ChugTheCreativityJuice");

      ImGui::NewLine();
      
      ImGui::Text("Third Party:");
      ImGui::BulletText("ReShade");
      ImGui::SameLine(); if (ImGui::Button("Open Site")) Website::OpenWebsite("https://reshade.me/");
      ImGui::BulletText("ImGui");
      ImGui::BulletText("RenoDX");
      ImGui::SameLine(); if (ImGui::Button("Open GitHub Link")) Website::OpenWebsite("https://github.com/clshortfuse/renodx");
      ImGui::BulletText("3Dmigoto");
      ImGui::BulletText("Oklab");
      ImGui::BulletText("JzAzBz");
      ImGui::BulletText("Dolby");
      ImGui::BulletText("NVIDIA");
      ImGui::BulletText("AMD");
      ImGui::BulletText("DICE");
      ImGui::BulletText("Intel");
      ImGui::BulletText("RenderDoc");
      ImGui::BulletText("shadPS4");
      
      ImGui::NewLine();

      ImGui::Text("Referenced Shader Source Code:");
      ImGui::BulletText("XeGTAO"); ImGui::SameLine(); if (ImGui::Button("Open GitHub Link")) Website::OpenWebsite("https://github.com/GameTechDev/XeGTAO");
      ImGui::BulletText("Barbatos XeGTAO (for JointBilateralUpsample() weights)");  ImGui::SameLine(); if (ImGui::Button("Open GitHub Link")) Website::OpenWebsite("https://github.com/BarbatosAWLS/Reshade-Shaders/blob/main/Shaders/BaBa_XeGTAO.fx");
      ImGui::BulletText("Barbatos DLAA");  ImGui::SameLine(); if (ImGui::Button("Open GitHub Link")) Website::OpenWebsite("https://github.com/BarbatosAWLS/Reshade-Shaders/blob/main/Shaders/BaBa_DLAA-T.fx");

      ImGui::NewLine();
      
      ImGui::Text("High FPS:");
      ImGui::BulletText("SpecialK (memory addresses)"); ImGui::SameLine(); if (ImGui::Button("Open GitHub Link")) Website::OpenWebsite("https://github.com/SpecialKO/SpecialK/blob/6fe51ee1eca4aee26a59e227ee5402ad3b55fcc0/src/plugins/unclassified.cpp#L1264");
      ImGui::BulletText("Display Commander (limit replacement)"); ImGui::SameLine(); if (ImGui::Button("Open GitHub Link")) Website::OpenWebsite("https://github.com/pmnoxx/display-commander");
   }
};

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
   if (ul_reason_for_call == DLL_PROCESS_ATTACH)
   {
      // name
      Globals::SetGlobals(PROJECT_NAME, "Hatsune Miku: Project DIVA Mega Mix+ - Luma Mod");
      Globals::VERSION = 0; // TODO: for some reason, this is not being saved to ReShade.ini, so anything > than 0 may reset user settings

      // fuillscreen state
      prevent_fullscreen_state = true;
      force_borderless = false;
      
      // swapchain upgrade
      OutputHandling::OnDLL();

      // texture upgrade
      texture_format_upgrades_type   = TextureFormatUpgradesType::AllowedEnabled;
      texture_upgrade_formats = {
         reshade::api::format::r8g8b8a8_unorm
      };

      texture_format_upgrades_2d_size_filters = 0 | (uint32_t)TextureFormatUpgrades2DSizeFilters::CustomAspectRatio | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainAspectRatio;
      texture_format_upgrades_2d_custom_aspect_ratios = { 16.f / 9.f }; 
      texture_format_upgrades_2d_aspect_ratio_pixel_threshold = 32; //leeway
      
      // game
      game = new ProjectDivaMegaMix();
   }

   // Luma core.hpp
   CoreMain(hModule, ul_reason_for_call, lpReserved);

   // ResourceTracker
   RenderTracker::OnDLLMainAfterCore(ul_reason_for_call);

   return TRUE;
} 