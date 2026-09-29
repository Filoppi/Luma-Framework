#define GAME_PROJECT_DIVA_MEGA_MIX 1

#define ALLOW_SHADERS_DUMPING 0
#define DISABLE_AUTO_DEBUGGER 1
#define ENABLE_BLOOM 1 // TODO: since we load our own and have Bloom pass copy-pasted here, dont rely on ENABLE_BLOOM?
// #define ENABLE_POST_DRAW_DISPATCH_CALLBACK 0
// #define DISABLE_SWAPCHAIN_FLIP_MODEL 1
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
   
namespace TonemapInfo
{
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
   constexpr const char* Luma_ToSwapchain = "Luma_ToSwapchain"; // file name & shader variant

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
      native_shaders_definitions.emplace(CompileTimeStringHash(Luma_ToSwapchain), ShaderDefinition{ Luma_ToSwapchain, reshade::api::pipeline_subobject_type::pixel_shader,   nullptr, "main", {{ "CUSTOM_TOSWAPCHAIN", def_val}}});

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
      native_device_context->PSSetShader(device_data.native_pixel_shaders.at(CompileTimeStringHash(Luma_ToSwapchain)).get(), nullptr, 0);

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
   constexpr uint32_t Downsample0 = 0x68722F15; //Generic downsample for bloom and autoexposure
   constexpr uint32_t Downsample1 = 0x7B4E4533; //downsample all the way down to near 1x1 for autoexposure
   constexpr uint32_t AutoExposure0 = 0xA58C1868; //auto exposure ring buffer write 
   constexpr uint32_t AutoExposure1 = 0xDF1AC023; //sample each ring buffer to compute avg (output is used by CPU to set cb value for tonemap v3.y)
   
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

namespace GlobalsMegaMix
{
   bool IsUI = true;
   // bool IsFullscreenOverlayFx = true;
   int TonemapInfoBackup = 0;
   int SwapchainChangeCount = 0;
   bool IsUIText = true;
   bool UIIsReadmeDone = false;
   bool UIIsAdvanced = false;
   bool IsGammaCorrectionSyncPaperWhite = true;
}

namespace DrawingState
{
   bool IsDrawnToSwapchain = false;
   bool IsDrawnAutoExposure0 = false;
   // bool IsDrawnMLAA = false;
   // bool IsDrawnMLAAPrev = false;

   void ResetOnPresent()
   {
      IsDrawnToSwapchain = false;
      IsDrawnAutoExposure0 = false;
      // IsAutoExposure0ClearingHistory = false; //dont need to reset
      // IsDrawnMLAAPrev = IsDrawnMLAA;
      // IsDrawnMLAA = false;
   }
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
   constexpr uint32_t CUSTOM_BLOOM_THRESHOLD            = char_ptr_crc32("CUSTOM_BLOOM_THRESHOLD");
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
   constexpr uint32_t CUSTOM_HDRTONEMAPSDR              = char_ptr_crc32("CUSTOM_HDRTONEMAPSDR");

   void OnInit()
   {
      std::vector<ShaderDefineData> game_shader_defines_data = {
         {"GAMMA_CORRECTION_RANGE_TYPE", '0', true, !DEVELOPMENT, "0 - Full range.\n1 - 0-1 only.", 1},
         // {"SWAPCHAIN_CLAMP_PEAK", '0', true, false, "Clamp the absolute final color.\n0 - Unclamped (up to display).\n1 - Per channel clamp (blows out).\n2 - Scale down by max channel (sat preserving).", 2},
         {"SWAPCHAIN_CLAMP_COLORSPACE", '0', true, !DEVELOPMENT, "Clamp colorspace against invalid colors.\n(Really only for OCD, as it should only be inconsequential black.)\n0 - Unclamped.\n1 - BT2020.", 1},
         {"SWAPCHAIN_TEST_USER_PEAK", '0', true, false, "Show a simple white rectangle peak test.", 1},
         // {"_____CUSTOM_____", '0', true, false, "Just a divider.", 1},
         {"CUSTOM_SDR_1", '0', true, false, "SDR path.", 1},
         {"CUSTOM_TONEMAP_SCALING", '0', true, false, "HDR tonemap scaling.\n0 - Luminance (natural)\n1 - Max-Channel (saturation preserve)", 1},
         {"CUSTOM_TONEMAP_CLAMP", '1', true, false, "(Only if CUSTOM_TONEMAP_SCALING is luminance scaled.)\nClamp overshoot from luma scaled HDR tonemap.\n0 - Unclamped (up to display).\n1 - Per channel clamp (blows out).\n2 - Scale down by max channel (sat preserving).", 2},
         {"CUSTOM_CLAMP_PEAK", '1', true, false, "Clamp the absolute final color.\n0 - Unclamped (up to display).\n1 - Per channel clamp (blows out).\n2 - Scale down by max channel (sat preserving).\n3 - Per channel rolloff slightly above peak (blows out).", 3},
         {"CUSTOM_TONEMAP_TRYIGNOREUI", '0', true, false, "If only UI is rendering, deactivates HDR tonemapper.", 1},
         {"CUSTOM_GAMMA_CORRECTION_MODE", '0', true, true, "0 - Per-Channel.\n1 - Perceptual.", 1},
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
         {"CUSTOM_BLOOM_THRESHOLD", '0', true, false, "Bloom threshold mode.", 2},
         {"CUSTOM_HDRTONEMAPSDR", '0', true, false, "Use new HDR tonemapping in SDR path.", 1},
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
         {"CUSTOM_TONEMAP_IDENTIFY", '0', true, !DEVELOPMENT, "Draw binary representation of tonemap uber variant number.", 1},
         {"CUSTOM_UPSCALE_MOV", '0', true, false, "PumboAutoHDR for FMV.\n0 - Off\n1 - On", 1},
         {"CUSTOM_UPSCALE_BGSPRITES", '0', true, false, "Auto HDR (Inverse Tonemap) for background 2D sprites in complex \"Future Tone\" scenes (e.g. Torinoko City).", 1},
         {"CUSTOM_UPSCALE_TOON", '0', true, false, "Auto HDR for flat toon scenes (e.g. Catch the Wave, Deep Sea City Underground, etc.).\n0 - Forced SDR\n1 - Treat as Complex\n2 - On\n3 - On (Ignore Customization Menu)", 3},
         {"CUSTOM_UPGRADE_DEBUG", '0', true, false, "Show inputs into UpgradeToneMap().", 5},
      };
      shader_defines_data.append_range(game_shader_defines_data);
      auto_recompile_defines = true; //force
      assert(shader_defines_data.size() < MAX_SHADER_DEFINES);
      
      // Default built-in
      GetShaderDefineData(POST_PROCESS_SPACE_TYPE_HASH).SetDefaultValue('1'); GetShaderDefineData(POST_PROCESS_SPACE_TYPE_HASH).SetValue('1'); GetShaderDefineData(GAMMA_CORRECTION_TYPE_HASH).SetValueFixed(true);
      GetShaderDefineData(EARLY_DISPLAY_ENCODING_HASH).SetDefaultValue('0'); GetShaderDefineData(EARLY_DISPLAY_ENCODING_HASH).SetValue('0'); GetShaderDefineData(EARLY_DISPLAY_ENCODING_HASH).SetValueFixed(true);
      GetShaderDefineData(VANILLA_ENCODING_TYPE_HASH).SetDefaultValue('0'); GetShaderDefineData(VANILLA_ENCODING_TYPE_HASH).SetValue('0'); GetShaderDefineData(GAMMA_CORRECTION_TYPE_HASH).SetValueFixed(true);
      GetShaderDefineData(GAMMA_CORRECTION_TYPE_HASH).SetDefaultValue('0'); GetShaderDefineData(GAMMA_CORRECTION_TYPE_HASH).SetValue('0'); GetShaderDefineData(GAMMA_CORRECTION_TYPE_HASH).SetValueFixed(true);
      GetShaderDefineData(UI_DRAW_TYPE_HASH).SetDefaultValue('2'); GetShaderDefineData(UI_DRAW_TYPE_HASH).SetValue('2'); GetShaderDefineData(UI_DRAW_TYPE_HASH).SetValueFixed(true);
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
   int rate_replacement = 60;
   constexpr auto reshadesave = "AutoExposureFix";

   int vp_curr_i = 0;

   double time_last_ae_allow;
   double time_curr;
   float GetTimeBetweenAllowedDraws() { return 1000.0f / rate_replacement; }

   double MillisecondsNow()
   {
      // static LARGE_INTEGER s_frequency;
      // static BOOL s_use_qpc = QueryPerformanceFrequency(&s_frequency);
      // double milliseconds = 0;
      // if (s_use_qpc)
      // {
      //    LARGE_INTEGER now;
      //    QueryPerformanceCounter(&now);
      //    milliseconds = double(1000.0 * now.QuadPart) / s_frequency.QuadPart;
      // }
      // else
      // {
      //    milliseconds = double(GetTickCount64());
      // }
      // return milliseconds;
      return static_cast<double>(GetTickCount64());
   }

   bool Update_IsDraw()
   {
      //update
      time_curr = MillisecondsNow();

      //FALSE: not enough time since last allow
      if (time_curr - time_last_ae_allow < GetTimeBetweenAllowedDraws()) return false;

      //TRUE: allow 
      time_last_ae_allow = time_curr; //new timestamp
      return true;
   }

   void OnLoad(reshade::api::effect_runtime* runtime)
   {
      reshade::get_config_value(runtime, NAME, AutoExposureFix::reshadesave, AutoExposureFix::rate_replacement);
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

   void Update()
   {
      //changed?
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

         //update
         cb_luma_global_settings.GameSettings.TonemapperPeakCached = CalcPeak(cb_luma_global_settings.ScenePeakWhite, cb_luma_global_settings.ScenePaperWhite, is_rec709);
         cb_luma_global_settings.GameSettings.TonemapperMaxExpectedCached = CalcWhiteClip(cb_luma_global_settings.ScenePeakWhite, cb_luma_global_settings.ScenePaperWhite, white_clip);
         cb_luma_global_settings.GameSettings.IntermediateScalingCached = CalcIntScaling(cb_luma_global_settings.ScenePaperWhite, cb_luma_global_settings.UIPaperWhite, is_rec709);

         if (DEVELOPMENT) reshade::log::message(reshade::log::level::info, std::format("CachedCB: Peak: {}, Paper: {}, UI Paper: {}, WhiteClip: {}, Rec709: {}",
            cb_luma_global_settings.GameSettings.TonemapperPeakCached,
            cb_luma_global_settings.GameSettings.TonemapperMaxExpectedCached,
            cb_luma_global_settings.GameSettings.IntermediateScalingCached,
            white_clip,
            is_rec709).c_str());
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
   float brightness_menu = brightness_menu_def;
   float brightness_game = brightness_game_def;

   constexpr auto reshadesave_enabled = "SeparateUIBrightnessEnabled";
   constexpr auto reshadesave_menu = "SeparateUIBrightnessMenu";
   constexpr auto reshadesave_game = "SeparateUIBrightnessGame";

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
#ifdef DAV_CORE
         ui_brightness_slider_enabled = !enabled;
#endif
      }
      
      bool is_disabled = !enabled;
      if (is_disabled) ImGui::BeginDisabled();
      {
         ImGui::PushID("Separate UI Brightness: Menu");
         if (ImGui::SliderFloat("Menu Brightness", &brightness_menu, 1.f, 1000.f, "%.0f nits"))
            reshade::set_config_value(runtime, NAME, reshadesave_menu, brightness_menu);
         ImGui::PopID();
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("UI paper white when browsing menus.");
         DrawResetButton(brightness_menu, brightness_menu_def, reshadesave_menu, runtime);

         ImGui::PushID("Separate UI Brightness: Gameplay");
         if (ImGui::SliderFloat("Game Brightness", &brightness_game, 1.f, 1000.f, "%.0f nits"))
            reshade::set_config_value(runtime, NAME, reshadesave_game, brightness_game);
         ImGui::PopID();
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("UI paper white when playing a PV / in gameplay.");
         DrawResetButton(brightness_game, brightness_game_def, reshadesave_game, runtime);
      }
      if (is_disabled) ImGui::EndDisabled();
   }

   void OnPresent()
   {
      if (!enabled) return;
      
      if (cb_luma_global_settings.DisplayMode == DisplayModeType::SDR)
      {
         cb_luma_global_settings.UIPaperWhite = 80;
         return;
      }
      
      cb_luma_global_settings.UIPaperWhite = MemoryHack::IsMenu() ? brightness_menu : brightness_game;
   }

   void OnLoad(reshade::api::effect_runtime* runtime)
   {
      reshade::get_config_value(runtime, NAME, reshadesave_enabled, enabled);
#ifdef DAV_CORE
      ui_brightness_slider_enabled = !enabled;
#endif

      if (use_os_reference_white_level) enabled = false; // conflicts if not.
      
      reshade::get_config_value(runtime, NAME, reshadesave_menu, brightness_menu);
      reshade::get_config_value(runtime, NAME, reshadesave_game, brightness_game);
   }
}

namespace XeGTAO
{
   bool enabled = false; //TODO: user settings
      constexpr const char* reshadesave_enabled = "XeGTAOEnabled";

   PUBLISHING_CONSTEXPR bool is_fog_dodge = true; // use fog dodging variant

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

      // found by Tonemap shader (used to cross reference with SSS)
      uint64_t correct_main_color_res_handle = 0;
      
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
         correct_main_color_res_handle = 0;
         
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
         if (FoundResource::correct_main_color_res_handle == 0 && ShaderHashesLists::Tonemaps.contains(ps))
         {
            // SRV0 is main color, get RES from it
            ComPtr<ID3D11ShaderResourceView> main_color_srv = nullptr;
            native_device_context->PSGetShaderResources(0, 1, main_color_srv.put());
            ASSERT_MSG(main_color_srv != nullptr, "XeGTAO::TrySetFromViews() Tonemap SRV0 is nullptr!");
            ComPtr<ID3D11Resource> main_color_res = nullptr;
            main_color_srv->GetResource(main_color_res.put());
            FoundResource::correct_main_color_res_handle = reinterpret_cast<uint64_t>(main_color_res.get());
         }

         // failed: still not found
         if (FoundResource::correct_main_color_res_handle == 0) return false;
         
         ///////////////////////////////////////
         // Stage 2: SSS for color, depth, cb //
         ///////////////////////////////////////
         // gatekeep: not relevant shader
         if (ps != 0x93881580) return false;
    
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
         if (reinterpret_cast<uint64_t>(color_res.get()) != FoundResource::correct_main_color_res_handle) return false;

         // Depth
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
      if (DEVELOPMENT && !IsModEnabled()) return true;

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
         case Unknown:
         {
            // try set/saving original resources
            if (TrySetFromViews(native_device, native_device_context, cmd_list_data, device_data, ps))
               state = Ready; //next state

            break; 
         }
         case Ready:
         {
            // failed: in UI
            // TODO: false positive for character customization screen
            
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
      reshade::get_config_value(runtime, NAME, reshadesave_denoise, denoise_count);
   }

   void OnInitSwapchain(bool is_resolution_changed)
   {
      if (is_resolution_changed) HardReset();
   }
}

namespace SSS
{
   bool enabled = false;
      constexpr const char* reshadesave_enabled = "SSSEnabled";

   enum State : uint8_t
   {
      Unknown, // start
      Setup,  // Setup skin surface buffer 0x93881580 drawn
      PBR_Downsample,
      PBR_Widen,
      NPR_Edges,
      NPR_Downsample,
      NPR_SSSPrep,
      SSS,
      Done, // done for this frame
   };
   State state = Unknown; // denotes which shader has drawn prev

   uint32_t curr_pv_id = 0;

   constexpr const char* Luma_NPRPreSSS = "Luma_NPRPreSSS";

   namespace Resources
   {
      struct Item
      {
         ComPtr<ID3D11Resource> original_res; // original full res setup RES
         ComPtr<ID3D11Resource> replacement_res = 0; // SRV16 to replace
         uint2 size = { 0, 0 };

         ComPtr<ID3D11ShaderResourceView> original_srv; // our own created to original

         // dual interchanging ring buffer like usage
         bool current_new = false;
         ComPtr<ID3D11ShaderResourceView> new_srv0;
         ComPtr<ID3D11RenderTargetView> new_rtv0;
         ComPtr<ID3D11ShaderResourceView> new_srv1;
         ComPtr<ID3D11RenderTargetView> new_rtv1;
         void IncrementNew() { current_new = !current_new; }
         
         uint64_t GetOriginalResHandle() const { return reinterpret_cast<uint64_t>(original_res.get()); }
         uint64_t GetReplacementResHandle() const { return reinterpret_cast<uint64_t>(replacement_res.get()); }
         
         bool IsValid() const { return original_res.get(); }
         void Reset()
         {
            size = { 0, 0 };
            original_res.reset(); replacement_res.reset();
            new_srv0.reset(); new_rtv0.reset();
         }
      };

      std::array<Item, 2> items = { };
      int GetItemCount() { return std::count_if(items.begin(), items.end(), [](const Item& item) { return item.IsValid(); }); }

      Item* TryGetItemForOriginal(uint64_t original_res_handle)
      {
         for (auto& item : items)
            if (reinterpret_cast<uint64_t>(item.original_res.get()) == original_res_handle)
               return &item;
         return nullptr;
      }

      Item* TryGetItemForReplace(uint64_t replacement_res_handle)
      {
         for (auto& item : items)
            if (reinterpret_cast<uint64_t>(item.replacement_res.get()) == replacement_res_handle)
               return &item;
         return nullptr;
      }

      Item* TryGetEmptyItem()
      {
         for (auto& item : items)
            if (!item.IsValid())
               return &item;
         return nullptr;
      }

      void Reset()
      {
         for (auto& item : items) item.Reset();
      }
   }

   // return only Skip or None (continues normal exec)
   DrawOrDispatchOverrideType OnDrawOrDispatchOverride(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, uint32_t ps)
   {
      if (DEVELOPMENT && !IsModEnabled()) return DrawOrDispatchOverrideType::None;
      
      /*
         PBR
         0x93881580: geo setup
         0x26AF16B8: downsample
         0xF2254A92: edges wider
         0x54415551: sss0

         NPR
         0x93881580: geo setup (with edge outline precompute)
         0x8E7027B5: edge outline resolve (slightly unrelated)
         0x26AF16B8: downsample
         0x086EEB5C: geo setup resolved to sss0 usable
         0x54415551: sss0
       */
      if (!enabled) return DrawOrDispatchOverrideType::None;

      // if SSS setup shader, force state
      if (ps == 0x93881580) state = Setup;

      static Resources::Item* current_item = nullptr; // acts like token
      static int replace_remaining = 0; // acts like token
      
      switch (state)
      {
         case Unknown: return DrawOrDispatchOverrideType::None;
         case Setup:
         {
            // PBR_Downsample
            if (ps == 0x26AF16B8) // "sprite simple"
            {
               state = PBR_Downsample;
               return DrawOrDispatchOverrideType::Skip; // allow this draw to continue
            }

            // NPR_Edges
            if (ps == 0x8E7027B5) // edge outlines
            {
               state = NPR_Edges;
               return DrawOrDispatchOverrideType::None; // allow this draw to continue
            }

            // init item (only once per set)
            if (!current_item) 
            {
               // get RTV0
               ComPtr<ID3D11RenderTargetView> rtv0 = nullptr;
               native_device_context->OMGetRenderTargets(1, rtv0.put(), nullptr);
               ASSERT_MSG(rtv0 != nullptr, "SSS::OnDrawOrDispatchOverride() rtv0 is nullptr");
         
               // get RES from RTV0
               ComPtr<ID3D11Resource> res0 = nullptr;
               rtv0->GetResource(res0.put());
         
               // RES handle
               auto res_handle = reinterpret_cast<uint64_t>(res0.get());
         
               // get item
               current_item = Resources::TryGetItemForOriginal(res_handle);
         
               // create new
               [[unlikely]] if (!current_item)
               {
                  // query TEX
                  ComPtr<ID3D11Texture2D> tex0 = nullptr;
                  auto hr0 = res0->QueryInterface(tex0.put());
                  ASSERT_MSG(SUCCEEDED(hr0), "SSS::OnDrawOrDispatchOverride() res0->QueryInterface(tex0) failed");
         
                  // get DESC for size
                  D3D11_TEXTURE2D_DESC tex_desc = {};
                  tex0->GetDesc(&tex_desc);
                  uint2 size = { tex_desc.Width, tex_desc.Height };
                  
                  // create item
                  current_item = Resources::TryGetEmptyItem();
                  ASSERT_MSG(current_item != nullptr, "SSS::Resources::CreateItem() no empty item slot");

                  // tex setup
                  current_item->size = size;
                  ComPtr<ID3D11Texture2D> replacement_tex = nullptr;
                  tex_desc.Width = current_item->size.x;
                  tex_desc.Height = current_item->size.y;
                  tex_desc.MipLevels = 1;
                  tex_desc.ArraySize = 1;
                  tex_desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
                  tex_desc.SampleDesc.Count = 1;
                  tex_desc.SampleDesc.Quality = 0;
                  tex_desc.Usage = D3D11_USAGE_DEFAULT;
                  tex_desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
                  tex_desc.CPUAccessFlags = 0;
                  tex_desc.MiscFlags = 0;
                  
                  // new 0
                  /*auto*/ hr0 = native_device->CreateTexture2D(&tex_desc, nullptr, replacement_tex.put());
                  ASSERT_MSG(SUCCEEDED(hr0), "SSS::Resources::Item::CreateReplacement() hr0");
                  auto hr1 = native_device->CreateShaderResourceView(replacement_tex.get(), nullptr, current_item->new_srv0.put());
                  ASSERT_MSG(SUCCEEDED(hr1), "SSS::Resources::Item::CreateReplacement() hr1");
                  auto hr2 = native_device->CreateRenderTargetView(replacement_tex.get(), nullptr, current_item->new_rtv0.put());
                  ASSERT_MSG(SUCCEEDED(hr2), "SSS::Resources::Item::CreateReplacement() hr2");

                  // new 1
                  auto hr3 = native_device->CreateTexture2D(&tex_desc, nullptr, replacement_tex.put());
                  ASSERT_MSG(SUCCEEDED(hr3), "SSS::Resources::Item::CreateReplacement() hr3");
                  auto hr4 = native_device->CreateShaderResourceView(replacement_tex.get(), nullptr, current_item->new_srv1.put());
                  ASSERT_MSG(SUCCEEDED(hr4), "SSS::Resources::Item::CreateReplacement() hr4");
                  auto hr5 = native_device->CreateRenderTargetView(replacement_tex.get(), nullptr, current_item->new_rtv1.put());
                  ASSERT_MSG(SUCCEEDED(hr5), "SSS::Resources::Item::CreateReplacement() hr5");

                  // original RES & SRV
                  current_item->original_res.attach(res0.detach());
                  auto hr6 = native_device->CreateShaderResourceView(current_item->original_res.get(), nullptr, current_item->original_srv.put());
                  ASSERT_MSG(SUCCEEDED(hr6), "SSS::Resources::Item::CreateReplacement() hr6");
               }
            }
            
            return DrawOrDispatchOverrideType::None;
         }
         case PBR_Downsample:
         {
            // PBR_Widen
            if (ps == 0xF2254A92)
            {
               state = PBR_Widen;
#if 0
               ASSERT_MSG(current_item, "SSS::OnDrawOrDispatchOverride() PBR_Downsample current_item is nullptr");
               
               native_device_context->PSSetShaderResources(0, 1, &current_item->original_srv); // SRV0 full res
               native_device_context->OMSetRenderTargets(1, !current_item->current_new ? &current_item->new_rtv0 : &current_item->new_rtv1, nullptr); // RTV0 new res
               
               // viewport full
               D3D11_VIEWPORT viewport = {0, 0, static_cast<FLOAT>(current_item->size.x), static_cast<FLOAT>(current_item->size.y), 0.f, 1.f};
               native_device_context->RSSetViewports(1, &viewport);

#else
               return DrawOrDispatchOverrideType::Skip;
#endif
            }
            return DrawOrDispatchOverrideType::None;
         }
         case NPR_Edges:
         {
            // NPR_Downsample
            if (ps == 0x26AF16B8)
            {
               state = NPR_Downsample;
               return DrawOrDispatchOverrideType::Skip;
            }
            
            // should not happen, but just in case
            ASSERT_MSG(false, "SSS::OnDrawOrDispatchOverride() NPR_Edges unexpected ps");
            return DrawOrDispatchOverrideType::None; 
         }
         case NPR_Downsample:
         {
            // NPR_SSSPrep
            if (ps == 0x086EEB5C) //TODO: skip this and make SSS sample only x & w.
            {
               native_device_context->PSSetShader(device_data.native_pixel_shaders.at(CompileTimeStringHash(Luma_NPRPreSSS)).get(), nullptr, 0);
               native_device_context->PSSetShaderResources(0, 1, &current_item->original_srv); // SRV0 full res
               native_device_context->OMSetRenderTargets(1, !current_item->current_new ? &current_item->new_rtv0 : &current_item->new_rtv1, nullptr); // RTV0 new res

               // viewport full
               D3D11_VIEWPORT viewport = {0, 0, static_cast<FLOAT>(current_item->size.x), static_cast<FLOAT>(current_item->size.y), 0.f, 1.f};
               native_device_context->RSSetViewports(1, &viewport);
               
               state = NPR_SSSPrep;
            }
            return DrawOrDispatchOverrideType::None;
         }
         case NPR_SSSPrep: 
         case PBR_Widen:
         {
            // SSS resolve
            if (ps == 0x54415551)
            {
               ASSERT_MSG(current_item, "SSS::OnDrawOrDispatchOverride() PBR_Widen current_item is nullptr");

               // get RTV0 RES for replacement
               ComPtr<ID3D11RenderTargetView> rtv0 = nullptr;
               native_device_context->OMGetRenderTargets(1, rtv0.put(), nullptr);
               rtv0->GetResource(current_item->replacement_res.put());

               // set SRV0 & RTV0 as full res
               ID3D11ShaderResourceView* const* srv;
               if (state == PBR_Widen) srv = &current_item->original_srv; // use original before downsample
               else
               {
                  srv = !current_item->current_new
                        ? &current_item->new_srv0
                        : &current_item->new_srv1;
                  current_item->IncrementNew(); // flipflop
               }
               native_device_context->PSSetShaderResources(0, 1, srv);
               native_device_context->OMSetRenderTargets(1, !current_item->current_new ? &current_item->new_rtv0 : &current_item->new_rtv1, nullptr);

               // viewport full
               D3D11_VIEWPORT viewport = {0, 0, static_cast<FLOAT>(current_item->size.x), static_cast<FLOAT>(current_item->size.y), 0.f, 1.f};
               native_device_context->RSSetViewports(1, &viewport);

               // next state
               current_item = nullptr; // consume
               replace_remaining = Resources::GetItemCount(); // rearm
               state = SSS;
            }
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
            ComPtr<ID3D11ShaderResourceView> srv16 = nullptr;
            native_device_context->PSGetShaderResources(16, 1, srv16.put());

            // skip: null SRV16
            if (!srv16) break;

            // get RES from SRV16
            ComPtr<ID3D11Resource> res16 = nullptr;
            srv16->GetResource(res16.put());

            // replace?
            Resources::Item* item = Resources::TryGetItemForReplace(reinterpret_cast<uint64_t>(res16.get()));
            if (item)
            {
               // set SRV16 as our new
               native_device_context->PSSetShaderResources(16, 1, !item->current_new ? &item->new_srv0 : &item->new_srv1);
               replace_remaining--; // use
            }

            // TODO: Project X guarantees "swapchain final" shader to draw for HQ mirrored world reflections before switching sides
            
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
      native_shaders_definitions.emplace(CompileTimeStringHash(Luma_NPRPreSSS), ShaderDefinition{ Luma_NPRPreSSS, reshade::api::pipeline_subobject_type::pixel_shader, nullptr, "main", {}});
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

      // on PV change, purge if multiple SSS items
      uint32_t new_pv_id = *MemoryHack::addr_pvID;
      if (new_pv_id != curr_pv_id)
      {
         curr_pv_id = new_pv_id; // update
         if (Resources::GetItemCount() > 1) // multiple?
         {
            Resources::Reset(); // purge
         }
      }
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
   
   constexpr const char* Bloom_Downsample1_PS = "Bloom Downsample1 PS";
   constexpr const char* Bloom_Combine_PS = "Bloom Combine PS";
   constexpr const char* Bloom_Blur0_PS = "Bloom Blur0 PS";
   constexpr const char* Bloom_Blur0_VS = "Bloom Blur0 VS";
   
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
               // constexpr float sigmas[nmips] = { 1.46f, 1.f, 1.f };
               auto& managed_resources = device_data.managed_resources;
            
               // Backup IA. //TODO: needed?
               D3D11_PRIMITIVE_TOPOLOGY primitive_topology_original;
               native_device_context->IAGetPrimitiveTopology(&primitive_topology_original);
            
               // Backup VS.
               ComPtr<ID3D11VertexShader> vs_original;
               native_device_context->VSGetShader(vs_original.put(), nullptr, nullptr);
            
               // Backup PS.
               ComPtr<ID3D11PixelShader> ps_original;
               native_device_context->PSGetShader(ps_original.put(), nullptr, nullptr);
               ComPtr<ID3D11Buffer> cb_orginal;
               native_device_context->PSGetConstantBuffers(11, 1, cb_orginal.put());
               ComPtr<ID3D11SamplerState> ps_sampler_original;
               native_device_context->PSGetSamplers(0, 1, ps_sampler_original.put());
               ComPtr<ID3D11ShaderResourceView> ps_srv_original;
               native_device_context->PSGetShaderResources(0, 1, ps_srv_original.put());
            
               // Backup Viewports.
               UINT num_viewports;
               native_device_context->RSGetViewports(&num_viewports, nullptr);
               std::vector<D3D11_VIEWPORT> viewports_original(num_viewports);
               native_device_context->RSGetViewports(&num_viewports, viewports_original.data());
            
               // Backup Rasterizer. //TODO: needed?
               ComPtr<ID3D11RasterizerState> rasterizer_original;
               native_device_context->RSGetState(rasterizer_original.put());
            
               // Backup Blend.
               ComPtr<ID3D11BlendState> blend_original;
               FLOAT blend_factor_original[4];
               UINT sample_mask_original;
               native_device_context->OMGetBlendState(blend_original.put(), blend_factor_original, &sample_mask_original);
            
               // Backup RTs.
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
            
               [[unlikely]] if (!managed_resources.buffers["luma_bloom_cb"_h])
               {
                  D3D11_BUFFER_DESC buffer_desc = {};
                  buffer_desc.ByteWidth = sizeof(CBLumaBloomData);
                  buffer_desc.Usage = D3D11_USAGE_DYNAMIC;
                  buffer_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
                  buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
                  auto hr = native_device->CreateBuffer(&buffer_desc, nullptr, managed_resources.buffers["luma_bloom_cb"_h].put());
                  ASSERT_MSG(SUCCEEDED(hr), "Bloom hr bloom_cb");
                  reshade::log::message(reshade::log::level::info, "Bloom: created constant buffer");
               }
            
               CBLumaBloomData cb_data;
            
               auto update_constant_buffer = [&]()
               {
                  D3D11_MAPPED_SUBRESOURCE mapped_subresource;
                  auto hr = native_device_context->Map(managed_resources.buffers["luma_bloom_cb"_h].get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_subresource);
                  ASSERT_MSG(SUCCEEDED(hr), "Bloom hr bloom_cb map");
                  std::memcpy(mapped_subresource.pData, &cb_data, sizeof(CBLumaBloomData));
                  native_device_context->Unmap(managed_resources.buffers["luma_bloom_cb"_h].get(), 0);
               };
            
               //
               // Prefilter + downsample pass
               //
            
               D3D11_VIEWPORT viewport_x = {};
               viewport_x.Width = x_mip0_width;
               viewport_x.Height = x_mip0_height;
            
               // Update CB.
               cb_data.src_size = float2(scene_width, scene_height);
               cb_data.inv_src_size = float2(1.0f / cb_data.src_size.x, 1.0f / cb_data.src_size.y);
               cb_data.axis = float2(1.0f, 0.0f);
               cb_data.sigma = sigma;
               update_constant_buffer();
            
               // Bindings.
               native_device_context->OMSetRenderTargets(1, &Resources::rtv_mips_x[0], nullptr);
               native_device_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
               native_device_context->VSSetShader(device_data.native_vertex_shaders.at(Math::CompileTimeStringHash("Bloom VS")).get(), nullptr, 0);
               native_device_context->PSSetShader(device_data.native_pixel_shaders.at(Math::CompileTimeStringHash(Bloom_Downsample1_PS)).get(), nullptr, 0);
               native_device_context->PSSetConstantBuffers(11, 1, &managed_resources.buffers["luma_bloom_cb"_h]);
               const std::array ps_samplers = { device_data.sampler_state_linear.get() };
               native_device_context->PSSetSamplers(0, ps_samplers.size(), ps_samplers.data());
               native_device_context->PSSetShaderResources(0, 1, &Resources::orig_full_srv);
               native_device_context->RSSetViewports(1, &viewport_x);
               native_device_context->RSSetState(nullptr);
            
               // Draw X pass.
               native_device_context->Draw(3, 0);
            
               std::vector<D3D11_VIEWPORT> viewports_y(Resources::nmips);
               viewports_y[0].Width = y_mip0_width;
               viewports_y[0].Height = y_mip0_height;
            
               // Update CB.
               cb_data.src_size = float2(x_mip0_width, x_mip0_height);
               cb_data.inv_src_size = float2(1.0f / cb_data.src_size.x, 1.0f / cb_data.src_size.y);
               cb_data.axis = float2(0.0f, 1.0f);
               update_constant_buffer();
            
               // Bindings.
               native_device_context->OMSetRenderTargets(1, &Resources::rtv_mips_y[0], nullptr);
               native_device_context->PSSetShader(device_data.native_pixel_shaders.at("Bloom Prefilter PS"_h).get(), nullptr, 0);
               native_device_context->PSSetShaderResources(0, 1, &Resources::srv_mips_x[0]);
               native_device_context->RSSetViewports(1, &viewports_y[0]);
            
               // Draw Y pass.
               native_device_context->Draw(3, 0);
            
               //
               // Downsample passes
               //
            
               // Render downsample passes.
               native_device_context->PSSetShader(device_data.native_pixel_shaders.at("Bloom Downsample PS"_h).get(), nullptr, 0); // 1st special downsample
               for (UINT i = 1; i < Resources::nmips; i++)
               {
                  // view port
                  viewport_x.Width = max(1u, x_mip0_width >> i);
                  viewport_x.Height = max(1u, x_mip0_height >> i);
            
                  // Update CB.
                  cb_data.src_size = float2(viewports_y[i - 1].Width, viewports_y[i - 1].Height);
                  cb_data.axis = float2(1.0f, 0.0f);
                  cb_data.inv_src_size = float2(1.0f / cb_data.src_size.x, 1.0f / cb_data.src_size.y);
                  cb_data.sigma += sigma_increase;
                  update_constant_buffer();
            
                  // Bindings.
                  native_device_context->OMSetRenderTargets(1, &Resources::rtv_mips_x[i], nullptr);
                  native_device_context->PSSetShaderResources(0, 1, &Resources::srv_mips_y[i - 1]);
                  native_device_context->RSSetViewports(1, &viewport_x);
            
                  // Draw X pass.
                  native_device_context->Draw(3, 0);
            
                  viewports_y[i].Width = max(1u, y_mip0_width >> i);
                  viewports_y[i].Height = max(1u, y_mip0_height >> i);
            
                  // Update CB.
                  cb_data.src_size = float2(viewport_x.Width, viewport_x.Height);
                  cb_data.axis = float2(0.0f, 1.0f);
                  cb_data.inv_src_size = float2(1.0f / cb_data.src_size.x, 1.0f / cb_data.src_size.y);
                  update_constant_buffer();
            
                  // Bindings.
                  native_device_context->OMSetRenderTargets(1, &Resources::rtv_mips_y[i], nullptr);
                  native_device_context->PSSetShaderResources(0, 1, &Resources::srv_mips_x[i]);
                  native_device_context->RSSetViewports(1, &viewports_y[i]);
            
                  // Draw Y pass.
                  native_device_context->Draw(3, 0);
               }
            
               //
               // Upsample passes
               //

               // blend
               [[unlikely]] if (!managed_resources.blends["luma_bloom_blend"_h])
               {
                  CD3D11_BLEND_DESC blend_desc(D3D11_DEFAULT);
                  blend_desc.RenderTarget[0].BlendEnable = TRUE;
                  blend_desc.RenderTarget[0].SrcBlend = D3D11_BLEND_BLEND_FACTOR;
                  blend_desc.RenderTarget[0].DestBlend = D3D11_BLEND_BLEND_FACTOR;
                  ensure(native_device->CreateBlendState(&blend_desc, managed_resources.blends["luma_bloom_blend"_h].put()), >= 0);
               }

               // upsample 4 mips up
               uint8_t budget = 4;
               native_device_context->PSSetShader(device_data.native_pixel_shaders.at("Bloom Upsample PS"_h).get(), nullptr, 0);
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
                  native_device_context->OMSetBlendState(managed_resources.blends["luma_bloom_blend"_h].get(), blend_factor, UINT_MAX);
               
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
            native_device_context->VSSetShader(device_data.native_vertex_shaders.at(CompileTimeStringHash(Bloom_Blur0_VS)).get(), nullptr, 0);
            native_device_context->PSSetShader(device_data.native_pixel_shaders.at(CompileTimeStringHash(Bloom_Blur0_PS)).get(), nullptr, 0);

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
            native_device_context->PSSetShader(device_data.native_pixel_shaders.at(CompileTimeStringHash(Bloom_Combine_PS)).get(), nullptr, 0);
            
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
      // TODO: since we load our own and have Bloom pass copy-pasted here, dont rely on ENABLE_BLOOM?
      native_shaders_definitions.emplace(CompileTimeStringHash(Bloom_Downsample1_PS), ShaderDefinition("Luma_Bloom_impl", reshade::api::pipeline_subobject_type::pixel_shader, nullptr, "bloom_downsample1_ps"));
      native_shaders_definitions.emplace(CompileTimeStringHash(Bloom_Combine_PS), ShaderDefinition("Luma_Bloom_impl", reshade::api::pipeline_subobject_type::pixel_shader, nullptr, "bloom_combine_ps"));
      native_shaders_definitions.emplace(CompileTimeStringHash(Bloom_Blur0_PS), ShaderDefinition("Luma_Bloom_impl", reshade::api::pipeline_subobject_type::pixel_shader, nullptr, "bloom_blur0_ps"));
      native_shaders_definitions.emplace(CompileTimeStringHash(Bloom_Blur0_VS), ShaderDefinition("Luma_Bloom_impl", reshade::api::pipeline_subobject_type::vertex_shader, nullptr, "bloom_blur0_vs"));
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

      uint2 main_color_size = { 0, 0 };

      void Reset()
      {
         depth_res_handle = 0;
         newcolor_srv.reset();
         newcolor_rtv.reset();
         main_color_size = { 0, 0 };
      }
   }

   void OnDrawOrDispatchOverride(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, uint32_t ps)
   {
      if (!enabled) return;
      if (Resources::main_color_size.x == 0) return;
      if (DEVELOPMENT && !IsModEnabled()) return;

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
            // if (ps != 0xC1A00F28) break;
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
            viewport.Width = static_cast<float>(Resources::main_color_size.x);
            viewport.Height = static_cast<float>(Resources::main_color_size.y);
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
      // initialize from RTV0
      [[unlikely]] if (enabled && Resources::main_color_size.x == 0)
      {
         // get size
         ComPtr<ID3D11RenderTargetView> rtv0;
         native_device_context->OMGetRenderTargets(1, rtv0.put(), nullptr);
         ASSERT_MSG(rtv0 != nullptr, "SpotLightShadows: RTV0 is null in OnTonemapDraw");
         
         ComPtr<ID3D11Resource> rtv0_resource;
         rtv0->GetResource(rtv0_resource.put());
         
         ComPtr<ID3D11Texture2D> rtv0_texture;
         auto hr = rtv0_resource->QueryInterface(rtv0_texture.put());
         ASSERT_MSG(SUCCEEDED(hr), "SpotLightShadows: OnTonemapDraw hr");
         
         D3D11_TEXTURE2D_DESC tex_desc;
         rtv0_texture->GetDesc(&tex_desc);
         
         Resources::main_color_size = { tex_desc.Width, tex_desc.Height };
         reshade::log::message(reshade::log::level::info, std::format("SpotLightShadows: main color size {}x{}", Resources::main_color_size.x, Resources::main_color_size.y).c_str());

         // create Resources
         {
            ComPtr<ID3D11Texture2D> new_texture;
            
            // newcolor
            D3D11_TEXTURE2D_DESC new_tex_desc;
            new_tex_desc.Width = Resources::main_color_size.x;
            new_tex_desc.Height = Resources::main_color_size.y;
            new_tex_desc.MipLevels = 1;
            new_tex_desc.ArraySize = 1;
            new_tex_desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
            new_tex_desc.SampleDesc.Count = 1;
            new_tex_desc.SampleDesc.Quality = 0;
            new_tex_desc.Usage = D3D11_USAGE_DEFAULT;
            new_tex_desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
            new_tex_desc.CPUAccessFlags = 0;
            new_tex_desc.MiscFlags = 0;
            
            auto hr2 = native_device->CreateTexture2D(&new_tex_desc, nullptr, new_texture.put());
            ASSERT_MSG(SUCCEEDED(hr2), "SpotLightShadows: OnTonemapDraw hr2");

            auto hr3 = native_device->CreateRenderTargetView(new_texture.get(), nullptr, Resources::newcolor_rtv.put());
            ASSERT_MSG(SUCCEEDED(hr3), "SpotLightShadows: OnTonemapDraw hr3");

            auto hr4 = native_device->CreateShaderResourceView(new_texture.get(), nullptr, Resources::newcolor_srv.put());
            ASSERT_MSG(SUCCEEDED(hr4), "SpotLightShadows: OnTonemapDraw hr4");

            // newdsv
            D3D11_TEXTURE2D_DESC new_dsv_tex_desc;
            new_dsv_tex_desc.Width = Resources::main_color_size.x;
            new_dsv_tex_desc.Height = Resources::main_color_size.y;
            new_dsv_tex_desc.MipLevels = 1;
            new_dsv_tex_desc.ArraySize = 1;
            new_dsv_tex_desc.Format = DXGI_FORMAT_R32_TYPELESS;
            new_dsv_tex_desc.SampleDesc.Count = 1;
            new_dsv_tex_desc.SampleDesc.Quality = 0;
            new_dsv_tex_desc.Usage = D3D11_USAGE_DEFAULT;
            new_dsv_tex_desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
            new_dsv_tex_desc.CPUAccessFlags = 0;
            new_dsv_tex_desc.MiscFlags = 0;

            auto hr5 = native_device->CreateTexture2D(&new_dsv_tex_desc, nullptr, new_texture.put());
            ASSERT_MSG(SUCCEEDED(hr5), "SpotLightShadows: OnTonemapDraw hr5");

            D3D11_DEPTH_STENCIL_VIEW_DESC dsv_desc = {};
            dsv_desc.Format = DXGI_FORMAT_D32_FLOAT;
            dsv_desc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
            auto hr6 = native_device->CreateDepthStencilView(new_texture.get(), &dsv_desc, Resources::newdsv.put());
            ASSERT_MSG(SUCCEEDED(hr6), "SpotLightShadows: OnTonemapDraw hr6");
         }
         reshade::log::message(reshade::log::level::info, "SpotLightShadows: created new color buffer and views");

         // skip 1st frame
         return;
      }

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
      

   constexpr const char* Luma_DLAA = "Luma_DLAA";
   constexpr const char* Luma_DLAA_VS = "Luma_DLAA_VS";
   constexpr const char* Luma_DLAA_PreFilter = "Luma_DLAA_PreFilter";
   constexpr const char* Luma_DLAA_Resolve = "Luma_DLAA_Resolve";

   namespace Resources
   {
      uint2 size = { 0, 0 };
      
      ComPtr<ID3D11ShaderResourceView> srv = nullptr;
      ComPtr<ID3D11RenderTargetView> rtv = nullptr;

      void Reset()
      {
         srv.reset();
         rtv.reset();
         size = { 0, 0 };
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
            
            if (enabled == DLAA) // DLAA
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
            
                  Resources::size = { tex_desc.Width, tex_desc.Height };
                  reshade::log::message(reshade::log::level::info, std::format("AntiAliasing: creating Resources for size {}x{}", Resources::size.x, Resources::size.y).c_str());
            
                  // create
                  D3D11_TEXTURE2D_DESC new_tex_desc;
                  new_tex_desc.Width = Resources::size.x;
                  new_tex_desc.Height = Resources::size.y;
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
               native_device_context->VSSetShader(device_data.native_vertex_shaders.at(CompileTimeStringHash(Luma_DLAA_VS)).get(), nullptr, 0);
            
               // prefilter
               native_device_context->OMSetRenderTargets(1, &Resources::rtv, nullptr);
               native_device_context->PSSetShader(device_data.native_pixel_shaders.at(CompileTimeStringHash(Luma_DLAA_PreFilter)).get(), nullptr, 0);
               native_device_context->Draw(4,0);
            
               // resolve
               native_device_context->OMSetRenderTargets(1, &rtv0, nullptr);
               native_device_context->PSSetShaderResources(0, 1, &Resources::srv);
               native_device_context->PSSetShader(device_data.native_pixel_shaders.at(CompileTimeStringHash(Luma_DLAA_Resolve)).get(), nullptr, 0);
               native_device_context->Draw(4,0);
            }
            else if (enabled == Disable) // Disable
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
      native_shaders_definitions.emplace(CompileTimeStringHash(Luma_DLAA_VS), ShaderDefinition(Luma_DLAA, reshade::api::pipeline_subobject_type::vertex_shader, nullptr, "fullscreen_vs"));
      native_shaders_definitions.emplace(CompileTimeStringHash(Luma_DLAA_PreFilter), ShaderDefinition(Luma_DLAA, reshade::api::pipeline_subobject_type::pixel_shader, nullptr, "prefilter_ps"));
      native_shaders_definitions.emplace(CompileTimeStringHash(Luma_DLAA_Resolve), ShaderDefinition(Luma_DLAA, reshade::api::pipeline_subobject_type::pixel_shader, nullptr, "resolve_ps"));
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

      // get RTV0 size
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
   constexpr const char* Luma_LUTBiasCached = "Luma_LUTBiasCached";

   constexpr int LUT_OUTPUT_SIZE = 2048;
   
   namespace Resources
   {
      ComPtr<ID3D11ShaderResourceView> srv = nullptr;
      ComPtr<ID3D11UnorderedAccessView> uav = nullptr;
   }

   void OnTonemapDraw(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data)
   {
      // gatekeep
      if (cb_luma_global_settings.DisplayMode != DisplayModeType::HDR) return;
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
         tex_desc.Width = LUT_OUTPUT_SIZE;
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
      native_device_context->CSSetShader(device_data.native_compute_shaders.at(CompileTimeStringHash(Luma_LUTBiasCached)).get(), nullptr, 0);
      native_device_context->Dispatch((LUT_OUTPUT_SIZE + 63) / 64, 1, 1);

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
      native_shaders_definitions.emplace(CompileTimeStringHash(Luma_LUTBiasCached), ShaderDefinition(Luma_LUTBiasCached, reshade::api::pipeline_subobject_type::compute_shader));
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
   }

   void OnInitSwapchain()
   {
      scanned_lut_res.clear();
      reshade::log::message(reshade::log::level::info, "LUTBuilderScan: Cleared resource_hashes on swapchain init.");
   }
}
#endif

} // unnamed namespace

class ProjectDivaMegaMix final : public Game
{
public:
   void OnInit(bool async) override
   {
      // log
      message(reshade::log::level::info, "OnInit()");

      // OutputHandling
      OutputHandling::OnInit();
      
      // ShaderDefines
      ShaderDefineInfo::OnInit();
      if (!DEVELOPMENT) ShaderDefineInfo::Set(DEVELOPMENT_HASH, false);

      // cb
      luma_settings_cbuffer_index = 13;
      luma_data_cbuffer_index = 12;

      // XeGTAO
      XeGTAO::OnInit();

      // SSS
      SSS::OnInit();

      // Bloom
      Bloom::OnInit();

      // AntiAliasing
      AntiAliasing::OnInit();

      // LUTBiasCache
      LUTBiasCached::OnInit();

      // Global default
      use_os_reference_white_level = false;
      
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

      default_luma_global_game_settings.FrameBlendRatio = cb_luma_global_settings.GameSettings.FrameBlendRatio = 0.33f;

#if DEVELOPMENT
      // debug_draw_options edit
      debug_draw_options = debug_draw_options & ~(uint)DebugDrawTextureOptionsMask::Tonemap;
#endif
   }
   
   void OnCreateDevice(ID3D11Device* native_device, DeviceData& device_data) override
   {
      // log
      message(reshade::log::level::info, "OnCreateDevice()");
      
      // HighFPS
      MemoryHack::Init();
   }

   void OnInitSwapchain(reshade::api::swapchain* swapchain)
   {
      // log
      message(reshade::log::level::info, "OnInitSwapchain()");
      
      auto& device_data = *swapchain->get_device()->get_private_data<DeviceData>();

      // resolution changed?
      static uint2 last_size = {};
      uint2 size = uint2(device_data.output_resolution.x, device_data.output_resolution.y);
      bool is_resolution_changed = size != last_size;

      // XeGTAO
      XeGTAO::OnInitSwapchain(is_resolution_changed);

      // SSS
      SSS::HardReset();

      // Bloom
      Bloom::HardReset();

      // SpotLightShadows
      SpotLightShadows::HardReset();

      // AntiAliasing
      AntiAliasing::HardReset();

      // DepthOfField
      DepthOfField::HardReset();

      // PS4Blur
      PS4Blur::HardReset();

      // LUTBuilderScan
#if DEVELOPMENT
      LUTBuilderScan::OnInitSwapchain();
#endif

      // // UISeparation
      // UISeparation::ResetOnSwapchain();
      
      // SwapchainChangeCount
      GlobalsMegaMix::SwapchainChangeCount++;
   }

   DrawOrDispatchOverrideType OnDrawOrDispatch(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, reshade::api::shader_stage stages, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, bool is_custom_pass, bool& updated_cbuffers, std::function<void()>* original_draw_dispatch_func) override
   {      
      auto ps = original_shader_hashes.pixel_shaders[0];
      // auto cs = original_shader_hashes.compute_shaders[0];

      // // skip not ps
      // [[unlikely]]
      // if (ps == 0) return DrawOrDispatchOverrideType::None;

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

      // SSS ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
      {
         auto r = SSS::OnDrawOrDispatchOverride(native_device, native_device_context, cmd_list_data, device_data, ps);
         if (r != DrawOrDispatchOverrideType::None) return r;
      }
      // Bloom ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
      {
         auto r = Bloom::OnDrawOrDispatchOverride(native_device, native_device_context, cmd_list_data, device_data, ps);
         if (r != DrawOrDispatchOverrideType::None) return r;
      }
      // AUTO EXPOSURE FIX ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
      if (AutoExposureFix::rate_replacement > 0 &&
         !DrawingState::IsDrawnAutoExposure0 &&
         !TonemapInfo::GetDrawnTonemap(cb_luma_global_settings.GameSettings.TonemapInfo) &&
         ps == ShaderHashesLists::AutoExposure0)
      {
         //progress
         DrawingState::IsDrawnAutoExposure0 = true;

         //detect if writing to all 32x1 (clears history) or just 1x1 (preserves history)
         D3D11_VIEWPORT vp{};
         UINT num_vp = 1;
         native_device_context->RSGetViewports(&num_vp, &vp);
         bool is_clear = num_vp > 0 && vp.Width > 1.f;

         //cleared, so reset our index
         if (is_clear) AutoExposureFix::vp_curr_i = 0; 

         //allow draw if: we allow or original request clear
         const bool allow_draw = AutoExposureFix::Update_IsDraw() || is_clear;

         //redirect index to ours
         if (allow_draw && !is_clear)
         {
            vp.TopLeftX = static_cast<float>(AutoExposureFix::vp_curr_i);
            native_device_context->RSSetViewports(1, &vp);
            AutoExposureFix::vp_curr_i = (AutoExposureFix::vp_curr_i + 1) % 32;
         }

         return allow_draw ? DrawOrDispatchOverrideType::None : DrawOrDispatchOverrideType::Skip;
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
         ps == ShaderHashesLists::Final)
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
         PS4Blur::OnDrawFinal(native_device, native_device_context, cmd_list_data, device_data);
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
         !DrawingState::IsDrawnToSwapchain &&
         ps == ShaderHashesLists::ToSwapchain)
      {
         DrawingState::IsDrawnToSwapchain = true;

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
      //    if (!DrawingState::IsDrawnMLAAPrev) return DrawOrDispatchOverrideType::Skip;
      //
      //    //case: MLAA
      //    if (DrawingState::IsDrawnMLAA) return DrawOrDispatchOverrideType::Skip;
      //
      //    //case: wait until MLAA
      //    return DrawOrDispatchOverrideType::None;
      // }
      
      //UI
      if (TonemapInfo::GetDrawnFinal(cb_luma_global_settings.GameSettings.TonemapInfo) &&
         !DrawingState::IsDrawnToSwapchain &&
         ps != ShaderHashesLists::Mov)
      {
         //skip IsUI
         if (!GlobalsMegaMix::IsUI) return DrawOrDispatchOverrideType::Skip;

         //skip SpritesText
         if (!GlobalsMegaMix::IsUIText
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
      // reset TonemapInfo
      GlobalsMegaMix::TonemapInfoBackup = cb_luma_global_settings.GameSettings.TonemapInfo;
      cb_luma_global_settings.GameSettings.TonemapInfo = TonemapInfo::GetDefaultReset();

      // reset game/device_data
      DrawingState::ResetOnPresent();
      device_data.has_drawn_main_post_processing = false;

      // CachedCB
      CachedCB::Update(); 

      // IndividualPVTuning
      IndividualPVTuning::OnPresent();
      
      // HighFPS
      HighFPS::Patch();

      // ProgressBar
      ProgressBar::OnPresent();

      // SeparateUIBrightness
      SeparateUIBrightness::OnPresent();
      
      // XeGTAO 
      XeGTAO::OnPresent();

      // SSS
      SSS::OnPresent();

      // Bloom
      Bloom::OnPresent();

      // SpotLightShadows
      SpotLightShadows::OnPresent();

      // AntiAliasing
      AntiAliasing::OnPresent();

      // DepthOfField
      DepthOfField::OnPresent();
   }

   void LoadConfigs() override
   {
      //log
      message(reshade::log::level::info, "LoadConfigs()");
      
      reshade::api::effect_runtime* runtime = nullptr;

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
      
      reshade::get_config_value(runtime, NAME, "FrameBlendRatio", cb_luma_global_settings.GameSettings.FrameBlendRatio);
      
      reshade::get_config_value(runtime, NAME, "IsUI", GlobalsMegaMix::IsUI);
      reshade::get_config_value(runtime, NAME, "IsUIText", GlobalsMegaMix::IsUIText);

      reshade::get_config_value(runtime, NAME, "UIIsAdvanced", GlobalsMegaMix::UIIsAdvanced);
      reshade::get_config_value(runtime, NAME, "UIIsReadmeDone", GlobalsMegaMix::UIIsReadmeDone);
      
      reshade::get_config_value(runtime, NAME, "HighFPS_enabled", HighFPS::enabled);
      reshade::get_config_value(runtime, NAME, "HighFPS_limit", HighFPS::limit);
      reshade::get_config_value(runtime, NAME, "HighFPS_menu_clamp", HighFPS::menu_clamp);

      reshade::get_config_value(runtime, NAME, "IsGammaCorrectionSyncPaperWhite", GlobalsMegaMix::IsGammaCorrectionSyncPaperWhite);
      if (GlobalsMegaMix::IsGammaCorrectionSyncPaperWhite) cb_luma_global_settings.GameSettings.GammaCorrection22PaperWhite = cb_luma_global_settings.ScenePaperWhite;

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

      // Info
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.f));
      {
         // Stops
         float stops = log2(cb_luma_global_settings.ScenePeakWhite / cb_luma_global_settings.ScenePaperWhite);
         std::string label_hdr_stops_plural = stops > 1.f ? "s" : "";
         std::string label_hdr_stops_sign = stops > 0.f ? "+" : "";
         ImGui::BulletText(std::format("HDR Stop{}: {}{:.2f}", label_hdr_stops_plural, label_hdr_stops_sign, stops).c_str());

         // OutputHandling
         ImGui::BulletText("Output: %s", OutputHandling::IsHDR10() ? "HDR10 (10-bit)" : OutputHandling::IsSDR8bit() ? "SDR (8-bit)" : "scRGB (16-bit)");
      }
      ImGui::PopStyleColor();
      
      if (!GlobalsMegaMix::UIIsReadmeDone)
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
            GlobalsMegaMix::UIIsReadmeDone = true;
            reshade::set_config_value(runtime, NAME, "UIIsReadmeDone", GlobalsMegaMix::UIIsReadmeDone);
         }
      }

      // ImGui::Separator(); ////////////////////////////////////////////////////////////////////////////////////
      
      if (DrawCollapsingHeaderEnabledColored("Gamma", (!is_sdr && ShaderDefineInfo::GetB(ShaderDefineInfo::CUSTOM_GAMMACORRECT22)) || (ShaderDefineInfo::GetB(ShaderDefineInfo::CUSTOM_HDTVREC709_1))))
      {
         if (!is_sdr)
         {
            DrawColoredSubHeader("Reintroduce SDR's gamma mismatch to lower shadows matching original intent.");
         
            // sync
            if (ImGui::Checkbox("Sync to Scene Paper White", &GlobalsMegaMix::IsGammaCorrectionSyncPaperWhite))
            {
               reshade::set_config_value(runtime, NAME, "IsGammaCorrectionSyncPaperWhite", GlobalsMegaMix::IsGammaCorrectionSyncPaperWhite);
               if (GlobalsMegaMix::IsGammaCorrectionSyncPaperWhite) cb_luma_global_settings.GameSettings.GammaCorrection22PaperWhite = cb_luma_global_settings.ScenePaperWhite;
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Encodes weaker sRGB and decodes stronger 2.2, lowering shadows like SDR.");
            
            //paper white
            // if (GlobalsMegaMix::IsGammaCorrectionSyncPaperWhite) ImGui::BeginDisabled();
            if (!GlobalsMegaMix::IsGammaCorrectionSyncPaperWhite)
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

            if (GlobalsMegaMix::UIIsAdvanced)
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
      if (GlobalsMegaMix::IsGammaCorrectionSyncPaperWhite) cb_luma_global_settings.GameSettings.GammaCorrection22PaperWhite = cb_luma_global_settings.ScenePaperWhite;

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

      ImGui::Separator(); ////////////////////////////////////////////////////////////////////////////////////

      // XEGTAO_MANUALSIZE auto toggle
      if (XeGTAO::FoundResource::IsSizeValid())
      {
         bool isSwapchainSized = XeGTAO::FoundResource::size.x == static_cast<uint>(cb_luma_global_settings.SwapchainSize.x) &&
                                 XeGTAO::FoundResource::size.y == static_cast<uint>(cb_luma_global_settings.SwapchainSize.y);
         ShaderDefineInfo::Set(ShaderDefineInfo::XEGTAO_MANUALSIZE, !isSwapchainSized);
      }

      ImGui::PushID("###XeGTAO");
      if (DrawCollapsingHeaderEnabledColored("Ambient Occlusion (XeGTAO)", XeGTAO::enabled))
      {
         DrawColoredSubHeader("Insert Ground Truth Ambient Occlusion for indirect shadowing.");

         ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.f));
         ImGui::Bullet(); ImGui::SameLine(); ImGui::TextWrapped("Though nowhere near the cost of generic ReShade FX solutions, this is not free.");
         ImGui::Bullet(); ImGui::SameLine(); ImGui::TextWrapped("There are slight errors (e.g. occlusion in fog & sky). I might account per PV if it's too noticeable.");
         ImGui::PopStyleColor();
         
         if (ImGui::Checkbox("Enabled", &XeGTAO::enabled))
            reshade::set_config_value(runtime, NAME, XeGTAO::reshadesave_enabled, XeGTAO::enabled);
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Insert Intel's implementation of Ground Truth Ambient Occlusion (GTAO).\n\nDebuting in Call of Duty: Black Ops 3 (or Advanced Warfare?),\nit's a screen space AO solution estimating path tracing level quality at a fraction of the cost.\n\nHere, the pass is inserted before transparency rendering & post FX, so no overlapping like generic ReShade FX solutions.\n(Please report if otherwise, especially concerning mod support.)");

         // TODO: presets

         ImGui::NewLine();
         DrawColoredSubHeader("Parameters");
         
         if (ImGui::SliderFloat("Final Power", &cb_luma_global_settings.GameSettings.XeGTAOFinalPower, 0.f, 2.f))
            reshade::set_config_value(runtime, NAME, "XeGTAOFinalPower", cb_luma_global_settings.GameSettings.XeGTAOFinalPower);
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Final power of the AO effect, after sample accumulation.\n\n(Increasing this will reveal noise, so you will probably need to increase quality.)");
         DrawResetButton(cb_luma_global_settings.GameSettings.XeGTAOFinalPower, default_luma_global_game_settings.XeGTAOFinalPower, "XeGTAOFinalPower", runtime);
         
         ShaderDefineInfo::UIDropDown(ShaderDefineInfo::XEGTAO_SLICECOUNT, "Slice Count", { "(3) Easy", "(6) Normal", "(8) Hard (Worth for 4K?)", "(12) Extreme", "(16) Extra Extreme", "(24) Uhhh", "(32) ..." }, "More samples = less noise.\n\n(Perhaps more denoise passes can achieve similar results?)");
         if (GlobalsMegaMix::UIIsAdvanced) ShaderDefineInfo::UIDropDown(ShaderDefineInfo::XEGTAO_STEPSPERSLICE, "Steps Per Slice", { "(3) Normal", "(4) Hard" }, "Within a slice, how many search steps.\nIncrease to have more darkening for harder to reach small crevices, but at a great cost to performance.");

         if (GlobalsMegaMix::UIIsAdvanced) ShaderDefineInfo::UIDropDown(ShaderDefineInfo::XEGTAO_NORMALSMOOTH_QUALITY, "Smooth Normals", { "Very Low", "Low"/*, "Normal"*/ }, "Surface normal map doesn't exist natively. Instead it's generated from depth.\nSmoothing is required to mask low poly models.");

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

         if (GlobalsMegaMix::UIIsAdvanced)
         {
            if (!is_halfres_after) ImGui::BeginDisabled();
            ShaderDefineInfo::UIToggleCheckmark(ShaderDefineInfo::XEGTAO_UPSAMPLE, "Joint Bilateral Upsample", "Upscale AO results while preventing leaks by using the spatial difference between half vs full res.");
            if (!is_halfres_after) ImGui::EndDisabled();
         }

         if (GlobalsMegaMix::UIIsAdvanced)
         {
            ImGui::NewLine();
            DrawColoredSubHeader("Thread Groups");
            ShaderDefineInfo::UIDropDown(ShaderDefineInfo::XEGTAO_THREADS_NORMALSGEN, "Threads: Normals Generation", { "8", "16" }, "Thread groups for compute shaders.\nDefault should be fastest.");
            ShaderDefineInfo::UIDropDown(ShaderDefineInfo::XEGTAO_THREADS_NORMALSSMOOTH, "Threads: Normals Smoothing", { "8", "16" }, "Thread groups for compute shaders.\nDefault should be fastest.");
            ShaderDefineInfo::UIDropDown(ShaderDefineInfo::XEGTAO_THREADS_AO, "Threads: GTAO", { "8", "16" }, "Thread groups for compute shaders.\nDefault should be fastest.");
            ShaderDefineInfo::UIDropDown(ShaderDefineInfo::XEGTAO_THREADS_DENOISE, "Threads: Denoise", { "8", "16" }, "Thread groups for compute shaders.\nDefault should be fastest.");
         }
         
         ImGui::NewLine();
         DrawColoredSubHeader("Auxiliary Resources");
         
         ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.f));
         ImGui::Bullet(); ImGui::SameLine(); ImGui::TextWrapped("These are created when Tonemap & skin Sub-Surface Scattering pass is found.");
         ImGui::PopStyleColor();
         
         ImGui::PushStyleColor(ImGuiCol_Text, XeGTAO::FoundResource::IsSizeValid() ? ImVec4(0.4f, 0.8f, 0.4f, 1.f) : ImVec4(0.8f, 0.4f, 0.4f, 1.f));
         std::string status;
         if (XeGTAO::FoundResource::IsSizeValid()) status = std::format("Yes ({}x{})", XeGTAO::FoundResource::size.x, XeGTAO::FoundResource::size.y);
         else if (XeGTAO::FoundResource::correct_main_color_res_handle > 0)  status = "No (Color found, pending Depth)";
         else status = "No";
         ImGui::TextWrapped("Ready: %s",  status.c_str());
         ImGui::PopStyleColor();

         ImGui::SameLine();

         if (ImGui::Button("Reset Resources"))
            XeGTAO::HardReset();
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Reset XeGTAO resources to then recreate.\nShould not be needed unless you change the game's resolution or something.");

         if (GlobalsMegaMix::UIIsAdvanced) {ImGui::Bullet(); ImGui::SameLine(); ImGui::TextWrapped("Size same as swapchain: %s", ShaderDefineInfo::GetB(ShaderDefineInfo::XEGTAO_MANUALSIZE) ? "No" : "Yes");}
         
         int _debug_out = XeGTAO::debug_out;
         ImGui::Combo("Debug View", &_debug_out, "None\0AO\0Normals\0Depth");
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Draw various debug views that is used by AO.");
         XeGTAO::debug_out = static_cast<XeGTAO::DebugOut>(_debug_out);

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
      if (DrawCollapsingHeaderEnabledColored("Skin Rendering (Sub-Surface Scattering)", SSS::enabled))
      {
         DrawColoredSubHeader("Sub-Surface Scattering (SSS) customization.");

         // enabled checkbox
         if (ImGui::Checkbox("Full Resolution", &SSS::enabled))
            reshade::set_config_value(runtime, NAME, SSS::reshadesave_enabled, SSS::enabled);
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Allow SSS to compute in full resolution."
                              "\nThis greatly reduces shadow flickering and blockiness on skin in motion"
                              "\nMaybe has a slight performance cost.");

         if (ImGui::SliderFloat("SSS Radius", &cb_luma_global_settings.GameSettings.SSSRadius, 0.5f, 1.5f))
            reshade::set_config_value(runtime, NAME, "SSSRadius", cb_luma_global_settings.GameSettings.SSSRadius);
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Radius / reach of the glow effect.");
         DrawResetButton(cb_luma_global_settings.GameSettings.SSSRadius, default_luma_global_game_settings.SSSRadius, "SSSRadius", runtime);

         if (GlobalsMegaMix::UIIsAdvanced)
         {
            ImGui::NewLine();
            DrawColoredSubHeader("Auxiliary Resources");
            
            for (int i = 0; i < SSS::Resources::items.size(); i++)
            {
               SSS::Resources::Item* item = &SSS::Resources::items[i];
               ImGui::Bullet(); ImGui::SameLine(); ImGui::TextWrapped("%d. %dx%d", i, item->size.x, item->size.y);
            }
         }

#if DEVELOPMENT
         ImGui::NewLine();
         DrawColoredSubHeader("DEVELOPMENT");
         if (ImGui::Button("Preview Toggle")) PreviewShaderOutput(device_data, 0x54415551);
#endif
      }
      ImGui::PopID();

      // ImGui::Separator(); ////////////////////////////////////////////////////////////////////////////////////

      ImGui::PushID("###Bloom");
      if (DrawCollapsingHeaderEnabledColored("Bloom", Bloom::enabled || ShaderDefineInfo::GetB(ShaderDefineInfo::CUSTOM_BLOOM_THRESHOLD)))
      {
         ImGui::PushID("###BloomThreshold");
         DrawColoredSubHeader("Threshold");
         ShaderDefineInfo::UIDropDown(ShaderDefineInfo::CUSTOM_BLOOM_THRESHOLD, "Mode",
            { "Vanilla (Crude)", "Slightly Neutral (Recommended)", "More Neutral (Alternative Style)" },
            "The high pass filter for bloom. How should it operate?"
            "\n"
            "\nThe original is a crude per-channel subtraction, horribly shifting hues and boosting saturation."
            "\nA prime example is \"When First Love Ends\", where red blobs of bloom ruins close ups of skin."
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
                                                                                          "\nThough impossible to be a 100%% direct vanilla upgrade due to new weights, it's tuned to be respectful."
                                                                                          "\n"
                                                                                          "\n(There's slight inefficiency decoupling from Auto-Exposure downsampling.)");

         if (GlobalsMegaMix::UIIsAdvanced)
         {
            if (!Bloom::enabled) ImGui::BeginDisabled();
            {
               if (ImGui::SliderFloat("Gaussian Sigma", &Bloom::sigma, 0.1f, 1.f))
                  reshade::set_config_value(runtime, NAME, Bloom::reshadesave_sigma, Bloom::sigma);
               if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Initial sigma for gaussian blur, where higher means wider radius."
                                                                                                "\n"
                                                                                                "\nIncreasing will suppress tiny highlights that cause bloom flickering,"
                                                                                                "\nbut also cost a bit of performance as texture sampling count increases.");
               DrawResetButton(Bloom::sigma, Bloom::sigma_def, Bloom::reshadesave_sigma, runtime);

               if (ImGui::SliderFloat("Gaussian Sigma Increase", &Bloom::sigma_increase, 0.f, 1.f))
                  reshade::set_config_value(runtime, NAME, Bloom::reshadesave_sigma_increase, Bloom::sigma_increase);
               if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Additional sigma (blur radius) increase per deeper mipmap level."
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

         ImGui::NewLine();
         DrawColoredSubHeader("Multipliers");

         if (GlobalsMegaMix::UIIsAdvanced)
         {
            if (ImGui::SliderFloat("Level 0", &cb_luma_global_settings.GameSettings.BloomStrengths.x, 0.f, 2.f))
               reshade::set_config_value(runtime, NAME, "BloomStrengthsX", cb_luma_global_settings.GameSettings.BloomStrengths.x);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Bloom Level 0 Strength: Tightest blur level/radius.");
            DrawResetButton(cb_luma_global_settings.GameSettings.BloomStrengths.x, default_luma_global_game_settings.BloomStrengths.x, "BloomStrengthsX", runtime);
            
            if (ImGui::SliderFloat("Level 1", &cb_luma_global_settings.GameSettings.BloomStrengths.y, 0.f, 2.f))
               reshade::set_config_value(runtime, NAME, "BloomStrengthsY", cb_luma_global_settings.GameSettings.BloomStrengths.y);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Bloom Level 1 Strength: Medium blur level/radius.");
            DrawResetButton(cb_luma_global_settings.GameSettings.BloomStrengths.y, default_luma_global_game_settings.BloomStrengths.y, "BloomStrengthsY", runtime);
            
            if (ImGui::SliderFloat("Level 2", &cb_luma_global_settings.GameSettings.BloomStrengths.z, 0.f, 2.f))
               reshade::set_config_value(runtime, NAME, "BloomStrengthsZ", cb_luma_global_settings.GameSettings.BloomStrengths.z);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Bloom Level 2 Strength: Wide blur level/radius.");
            DrawResetButton(cb_luma_global_settings.GameSettings.BloomStrengths.z, default_luma_global_game_settings.BloomStrengths.z, "BloomStrengthsZ", runtime);
            
            if (ImGui::SliderFloat("Level 3", &cb_luma_global_settings.GameSettings.BloomStrengths.w, 0.f, 2.f))
               reshade::set_config_value(runtime, NAME, "BloomStrengthsW", cb_luma_global_settings.GameSettings.BloomStrengths.w);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Bloom Level 3 Strength: Widest blur level/radius.");
            DrawResetButton(cb_luma_global_settings.GameSettings.BloomStrengths.w, default_luma_global_game_settings.BloomStrengths.w, "BloomStrengthsW", runtime);
         }
         
         if (ImGui::SliderFloat("Final", &cb_luma_global_settings.GameSettings.BloomStrength, 0.f, 2.f))
            reshade::set_config_value(runtime, NAME, "BloomStrength", cb_luma_global_settings.GameSettings.BloomStrength);
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Bloom final multiplier.");
         DrawResetButton(cb_luma_global_settings.GameSettings.BloomStrength, default_luma_global_game_settings.BloomStrength, "BloomStrength", runtime);

         if (GlobalsMegaMix::UIIsAdvanced)
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
         DrawColoredSubHeader("Anti-Aliasing Customization");

         // dropdown Vanilla, Disable, DLAA
         auto curr = static_cast<int>(AntiAliasing::enabled);
         if (ImGui::Combo("Mode", &curr, "Vanilla Morphological Anti-Aliasing (MLAA)\0Disallow\0Directional Localized Anti-Aliasing (DLAA)\0"))
         {
            reshade::set_config_value(runtime, NAME, AntiAliasing::reshadesave_enabled, curr);
            AntiAliasing::enabled = static_cast<AntiAliasing::Enabled>(curr);
         }
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Options for Anti-Aliasing."
                                                                                          "\n"
                                                                                          "\n[Directional Localized Anti-Aliasing (DLAA)]"
                                                                                          "\nMore lenient, DLAA will result in a more blurred/filmic look."
                                                                                          "\nDLAA was created to address difficulties implementing MLAA, resulting in an algorithm that blurs along the direction of edges for massive smoothing."
                                                                                          "\nDebuting in Star Wars: The Force Unleashed II, it's one of the last AA methods before temporal solutions took over.");

         ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 0.5f, 1.f));
         ImGui::Bullet(); ImGui::SameLine(); ImGui::TextWrapped("Requires MLAA selected in graphics settings!");
         ImGui::PopStyleColor();

         ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.f));
         ImGui::Bullet(); ImGui::SameLine(); ImGui::TextWrapped("Btw, FXAA never seems to draw.");
         ImGui::PopStyleColor();
      }
      ImGui::PopID();

      // ImGui::Separator(); ////////////////////////////////////////////////////////////////////////////////////

      ImGui::PushID("###DepthOfField");
      if (DrawCollapsingHeaderEnabledColored("Depth of Field", DepthOfField::enabled > DepthOfField::Vanilla))
      {
         DrawColoredSubHeader("Depth of Field Customization");

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

      ImGui::PushID("###SpotLightShadows");
      if (DrawCollapsingHeaderEnabledColored("Spotlight Shadows", SpotLightShadows::enabled))
      {
         DrawColoredSubHeader("Full Resolution Spotlight Pass Resolve");

         if (ImGui::Checkbox("Enable", &SpotLightShadows::enabled))
            reshade::set_config_value(runtime, NAME, SpotLightShadows::reshadesave_enabled, SpotLightShadows::enabled);
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Make the separated spotlight shadows pass resolve to a full resolution color buffer.\nProbably has some performance cost.");

         ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.f));
         ImGui::Bullet(); ImGui::SameLine(); ImGui::TextWrapped("This FX is rather rare. 2 PVs using this are Meiteki Cybernetics & Gaikotsu Gakudan to Riria.");
         ImGui::PopStyleColor();
      }
      ImGui::PopID();
      
      // ImGui::Separator(); ////////////////////////////////////////////////////////////////////////////////////

      ImGui::PushID("###PS4Blur");
      if (DrawCollapsingHeaderEnabledColored("PS3/PS4 Frame Blending", ShaderDefineInfo::GetB(ShaderDefineInfo::CUSTOM_PS4BLUR_1)))
      {
         DrawColoredSubHeader("Insert 1-frame delay blending/ghosting seen in Dreamy Theater (PS3) & Future Tone (PS4).");

         auto d = ShaderDefineInfo::UIDropDown(ShaderDefineInfo::CUSTOM_PS4BLUR_1, "Mode", { "Off", "On", "Horizontal Interlacing" }, "Interlacing doesn't save performance.");

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
      if (DrawCollapsingHeaderEnabledColored("Auto-Exposure", AutoExposureFix::rate_replacement > 0))
      {
         DrawColoredSubHeader("Limit how fast Auto-Exposure history is written, reducing rapid exposure changes on high FPS.");
         
         if (ImGui::SliderInt("Rate", &AutoExposureFix::rate_replacement, 0, 60, "%d FPS"))
            reshade::set_config_value(runtime, NAME, AutoExposureFix::reshadesave, AutoExposureFix::rate_replacement);
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Auto-Exposure history (32px ring buffer) is done per-frame.\nOn high FPS, this causes rapid exposure changes as older history is rapidly overriden.\n\nThis feature will limit Auto-Exposure rate,\nwhile allowing camera cuts to clear history.");
         DrawResetButton(AutoExposureFix::rate_replacement, 60, AutoExposureFix::reshadesave, runtime);
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

      if (DrawCollapsingHeaderEnabledColored("UI", !GlobalsMegaMix::IsUI || !GlobalsMegaMix::IsUIText))
      {
         if (ImGui::Checkbox("Draw UI", &GlobalsMegaMix::IsUI))
            reshade::set_config_value(runtime, NAME, "IsUI", GlobalsMegaMix::IsUI);
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Toggle UI.\nIf off, will discard all UI sprite shaders after the final shader.");
         DrawResetButton(GlobalsMegaMix::IsUI, true, "IsUI", runtime);
         
         if (ImGui::Checkbox("Draw UI Text", &GlobalsMegaMix::IsUIText))
            reshade::set_config_value(runtime, NAME, "IsUIText", GlobalsMegaMix::IsUIText);
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Skips text (at least subtitles) after final shader has drawn.");
         DrawResetButton(GlobalsMegaMix::IsUIText, true, "IsUIText", runtime);
      }

      // ImGui::Separator(); ////////////////////////////////////////////////////////////////////////////////////
      
      //show advanced
      if (!GlobalsMegaMix::UIIsAdvanced)
      {
         ImGui::Separator(); ////////////////////////////////////////////////////////////////////////////////////
         if (ImGui::Checkbox("Show Advanced Settings", &GlobalsMegaMix::UIIsAdvanced))
            reshade::set_config_value(runtime, NAME, "UIIsAdvanced", GlobalsMegaMix::UIIsAdvanced);

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
         DrawColoredSubHeader("Regain chrominance for HDR highlights by creating bias on blowout curve through gaussian weighted sampling.");
         
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
         DrawColoredSubHeader("Miscellaneous Settings for HDR Display-mapping");

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
         DrawColoredSubHeader("Fake saturation to decrease BT.709 chrominance clipping.");

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
         
         const int ti = TonemapInfo::GetIndexOnlyIfDrawn(GlobalsMegaMix::TonemapInfoBackup);
         
         std::string s = "Tonemap Uber Variant: " + std::to_string(ti);
         ImGui::BulletText(s.c_str());
         
         std::string s99 = "Tonemap Debug Info: " + (TonemapInfo::GetIndexOnlyIfDrawn(GlobalsMegaMix::TonemapInfoBackup) >= 0 ? static_cast<std::string>(TonemapInfo::TonemapDebugInfo[ti]) : "N/A");
         ImGui::BulletText(s99.c_str());
         
         std::string s1 = "Drawn Final: " + std::to_string(TonemapInfo::GetDrawnFinal(GlobalsMegaMix::TonemapInfoBackup));
         ImGui::BulletText(s1.c_str());
         
         std::string s6 = "Drawn Sprites HPBarDelta: " + std::to_string(TonemapInfo::GetDrawnHPBarDelta(GlobalsMegaMix::TonemapInfoBackup));
         ImGui::BulletText(s6.c_str());
         
         // std::string s5 = "FMV Mode Detected: " + std::to_string(TonemapInfo::GetIsFMV(Globals::TonemapInfoBackup));
         // ImGui::BulletText(s5.c_str());

         // std::string s7 = "MLAA Detected: " + std::to_string(DrawingState::IsDrawnMLAAPrev);
         // ImGui::BulletText(s7.c_str());
         
         std::string s4 = "Swapchain Change Count: " + std::to_string(GlobalsMegaMix::SwapchainChangeCount);
         ImGui::BulletText(s4.c_str());
         
         std::string s7 = "Auto-Exposure Fix Is Allow Draw: " + std::to_string(AutoExposureFix::Update_IsDraw());
         ImGui::BulletText(s7.c_str());

         std::string s9 = "Auto-Exposure Fix Ring Buffer Index Override: " + std::to_string(AutoExposureFix::vp_curr_i);
         ImGui::BulletText(s9.c_str());
         
         // std::string s2 = "SK Mode: " + std::to_string(Globals::IsSKMode);
         // ImGui::BulletText(s2.c_str());

         std::string s3 = "Tonemapper Peak Cached: " + std::to_string(cb_luma_global_settings.GameSettings.TonemapperPeakCached);
         ImGui::BulletText(s3.c_str());
         
         std::string s8 = "Tonemapper Max Expected Cached: " + std::to_string(cb_luma_global_settings.GameSettings.TonemapperMaxExpectedCached);
         ImGui::BulletText(s8.c_str());

         std::string s10 = "Intermediate Scaling Cached: " + std::to_string(cb_luma_global_settings.GameSettings.IntermediateScalingCached);
         ImGui::BulletText(s10.c_str());
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
      
      if (ImGui::Checkbox("Show Advanced Settings", &GlobalsMegaMix::UIIsAdvanced))
         reshade::set_config_value(runtime, NAME, "UIIsAdvanced", GlobalsMegaMix::UIIsAdvanced);
      
      if (ImGui::Checkbox("Hide README", &GlobalsMegaMix::UIIsReadmeDone))
         reshade::set_config_value(runtime, NAME, "UIIsReadmeDone", GlobalsMegaMix::UIIsReadmeDone);
      
      static double exit_armed_time = 0.0;
      if (exit_armed_time == -10000.f)
      {
         ImGui::Button("Exiting...");
      }
      else if (exit_armed_time <= 0)
      {
         if (ImGui::Button("\"exit(0)\"")) exit_armed_time = static_cast<double>(GetTickCount64());
      }
      else
      {
         double exit_armed_time_left = 3000.0 - (static_cast<double>(GetTickCount64()) - exit_armed_time);
         if (exit_armed_time_left > 0)
         {
            if (ImGui::Button(std::format("Confirm Exit {:.1f}s", exit_armed_time_left / 1000.0).c_str()))
            {
               exit(0);
               exit_armed_time = -10000.f;
            }
         }
         else exit_armed_time = 0.0;
      }
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Exit the game via Windows process termination, avoiding the flash bang screen when exiting normally.");
      
#if DEVELOPMENT
      ImGui::Separator();
#endif
   }

   void PrintImGuiAbout() override
   {
      // ImGui::Separator(); ////////////////////////////////////////////////////////////////////////////////////

      ImGui::Text("Build Date:");
      ImGui::BulletText(__DATE__);
      ImGui::BulletText(__TIME__);
      
      ImGui::Separator(); ////////////////////////////////////////////////////////////////////////////////////

      ImGui::Text("Credits:");
      ImGui::BulletText("Luma: Pumbo (Filoppi)");
      ImGui::BulletText("RenoDX: clshortfuse");
      ImGui::BulletText("Mod: XgarhontX");
      ImGui::BulletText("Development Help & Bug Hunter: MLGSmallSmoke35");
      ImGui::BulletText("Bug Hunter, Benchmarker, and Tester: Pikota");
      ImGui::BulletText("Bug Hunter: Pino");
      ImGui::BulletText("Testing & Suggestions: neocodex");
      ImGui::BulletText("Bug Hunter: Jorge");

      ImGui::Separator(); ////////////////////////////////////////////////////////////////////////////////////
      
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
      
      ImGui::Separator(); ////////////////////////////////////////////////////////////////////////////////////

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
      Globals::VERSION = 1;

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

   CoreMain(hModule, ul_reason_for_call, lpReserved);

   return TRUE;
} 