#define GAME_PROJECT_DIVA_MEGA_MIX 1

#define ALLOW_SHADERS_DUMPING 0
// #define ENABLE_POST_DRAW_DISPATCH_CALLBACK 0

#define SCENE_MAX_WHITE 4000.f

#include "..\..\Core\core.hpp"

namespace TonemapInfo
{
   static int FlagDrawnTonemap =     0x40000000; //1<<30
   // static int FlagSprites =          0x20000000; //1<<29
   // static int FlagComplex =          0x10000000; //1<<28
   static int FlagDrawnFinal =       0x08000000; //1<<27
   // static int FlagIsFMV =            0x04000000; //1<<26
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

   // int SetIsFMVTrue(int v) { return v | FlagIsFMV; }
   // bool GetIsFMV(int v) { return (v & FlagIsFMV) > 0; }

   int SetDrawnHPBarDeltaTrue(int v) { return v | FlagDrawnHPBarDelta; }
   bool GetDrawnHPBarDelta(int v) { return (v & FlagDrawnHPBarDelta) > 0; }
         
   int SetIndexAndDrawnTonemapTrue(int v, int i) { return v | FlagDrawnTonemap | (IndexBitMask & i); }
   int GetIndex(int v) { return v & IndexBitMask; }
   int GetIndexOnlyIfDrawn(int v) { return GetDrawnTonemap(v) ? v & IndexBitMask : -1; }

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

namespace ShaderHashesLists
{
   constexpr static UINT64 AutoExposure0 = 2777421928; //0xA58C1868

   // ShaderHashesList Tonemap0;
   // ShaderHashesList Tonemap1;
   // ShaderHashesList Tonemap2;
   // ShaderHashesList Tonemap3;
   // ShaderHashesList Tonemap4;
   // ShaderHashesList Tonemap5;
   // ShaderHashesList Tonemap6;
   // ShaderHashesList Tonemap7;
   // ShaderHashesList Tonemap8;
   // ShaderHashesList Tonemap9;
   // ShaderHashesList Tonemap10;
   constexpr static UINT64 Tonemap0  = 2096946970; //std::stoul("0x7CFCDF1A", nullptr, 16); //complex
   constexpr static UINT64 Tonemap1  = 1615316446; //std::stoul("0x6047C5DE", nullptr, 16); //complex sprite
   constexpr static UINT64 Tonemap2  = 2360049758; //std::stoul("0x8CAB805E", nullptr, 16); //complex (un-witnessed)
   constexpr static UINT64 Tonemap3  = 2268536438; //std::stoul("0x87371E76", nullptr, 16); //complex sprites (un-witnessed)
   constexpr static UINT64 Tonemap4  = 3005693432; //std::stoul("0xB3273DF8", nullptr, 16); //complex sprites (un-witnessed)
   constexpr static UINT64 Tonemap5  = 1432748576; //std::stoul("0x55660220", nullptr, 16); //fast
   constexpr static UINT64 Tonemap6  =  691043158; //std::stoul("0x29307B56", nullptr, 16); //fast (un-witnessed)
   constexpr static UINT64 Tonemap7  = 1519134748; //std::stoul("0x5A8C281C", nullptr, 16); //fast sprites (un-witnessed)
   constexpr static UINT64 Tonemap8  = 3417342325; //std::stoul("0xCBB08175", nullptr, 16); //fast sprites 
   constexpr static UINT64 Tonemap9  = 3570087662; //std::stoul("0xD4CB36EE", nullptr, 16); //fast (un-witnessed)
   constexpr static UINT64 Tonemap10 = 4139697716; //std::stoul("0xF6BEC634", nullptr, 16); //fast sprites (in customization)
   
   // ShaderHashesList MLAA;
   // ShaderHashesList MLAAFinal;
   
   // ShaderHashesList Final;
   constexpr static UINT64 Final = 1447312361; //std::stoul("0x56443BE9", nullptr, 16);
   
   // ShaderHashesList Mov;
   constexpr static UINT64 Mov = 1658229331; //std::stoul("0x62D69253", nullptr, 16);
   
   // ShaderHashesList UISpritesHPBarDelta;
   constexpr static UINT64 UISpritesHPBarDelta = 3491111817; //std::stoul("0xD0162389", nullptr, 16);
   
   // ShaderHashesList UISpritesText;
   constexpr static UINT64 UISpritesText = 2137820871; //std::stoul("0x7F6C8EC7", nullptr, 16);
   
   // ShaderHashesList ToSwapchain;
   constexpr static UINT64 ToSwapchain = 2717954418; //std::stoul("0xA200B172", nullptr, 16);
}
void ShaderHashesLists_Setup()
{
   // ShaderHashesLists::Tonemap0.pixel_shaders.emplace(std::stoul("0x7CFCDF1A", nullptr, 16)); //complex
   // ShaderHashesLists::Tonemap1.pixel_shaders.emplace(std::stoul("0x6047C5DE", nullptr, 16)); //complex sprite
   // ShaderHashesLists::Tonemap2.pixel_shaders.emplace(std::stoul("0x8CAB805E", nullptr, 16)); //complex (un-witnessed)
   // ShaderHashesLists::Tonemap3.pixel_shaders.emplace(std::stoul("0x87371E76", nullptr, 16)); //complex sprites (un-witnessed)
   // ShaderHashesLists::Tonemap4.pixel_shaders.emplace(std::stoul("0xB3273DF8", nullptr, 16)); //complex sprites (un-witnessed)
   // ShaderHashesLists::Tonemap5.pixel_shaders.emplace(std::stoul("0x55660220", nullptr, 16)); //fast
   // ShaderHashesLists::Tonemap6.pixel_shaders.emplace(std::stoul("0x29307B56", nullptr, 16)); //fast (un-witnessed)
   // ShaderHashesLists::Tonemap7.pixel_shaders.emplace(std::stoul("0x5A8C281C", nullptr, 16)); //fast sprites (un-witnessed)
   // ShaderHashesLists::Tonemap8.pixel_shaders.emplace(std::stoul("0xCBB08175", nullptr, 16)); //fast sprites 
   // ShaderHashesLists::Tonemap9.pixel_shaders.emplace(std::stoul("0xD4CB36EE", nullptr, 16)); //fast (un-witnessed)
   // ShaderHashesLists::Tonemap10.pixel_shaders.emplace(std::stoul("0xF6BEC634", nullptr, 16)); //fast sprites (in customization)
   
   // ShaderHashesLists::MLAA.pixel_shaders.emplace(std::stoul("0x3ACC6F7A", nullptr, 16)); //edge0
   // ShaderHashesLists::MLAA.pixel_shaders.emplace(std::stoul("0x5DA2FE05", nullptr, 16)); //edge1
   // ShaderHashesLists::MLAA.pixel_shaders.emplace(std::stoul("0x5C5FD160", nullptr, 16)); //resolve
   // ShaderHashesLists::MLAAFinal.pixel_shaders.emplace(std::stoul("0x5C5FD160", nullptr, 16)); //resolve
   
   // ShaderHashesLists::Final.pixel_shaders.emplace(std::stoul("0x56443BE9", nullptr, 16));
   
   // ShaderHashesLists::Mov.pixel_shaders.emplace(std::stoul("0x62D69253", nullptr, 16));
   
   // ShaderHashesLists::UISpritesHPBarDelta.pixel_shaders.emplace(std::stoul("0xD0162389", nullptr, 16));
   // ShaderHashesLists::UISpritesText.pixel_shaders.emplace(std::stoul("0x7F6C8EC7", nullptr, 16));
   
   // ShaderHashesLists::ToSwapchain.pixel_shaders.emplace(std::stoul("0xA200B172", nullptr, 16));
}

namespace Globals
{
   static bool IsUI = true;
   // static bool IsFullscreenOverlayFx = true;
   static int TonemapInfoBackup = 0;
   static int SwapchainChangeCount = 0;
   // static bool IsSkipUntilUI = false;
   static bool IsSkipTextAfterFinal = false;
   static bool UIIsReadmeDone = false;
   static bool UIIsAdvanced = false;
   // static bool IsSKMode = false;
}

struct ProjectDivaMegaMixGameDeviceData final : public GameDeviceData
{
   bool IsDrawnToSwapchain = false;
   bool IsDrawnAutoExposure0 = false;
   // bool IsDrawnMLAA = false;
   // bool IsDrawnMLAAPrev = false;

   //UI transparency
   com_ptr<ID3D11Texture2D> UIOutputTexOrig = nullptr;
   
   com_ptr<ID3D11Texture2D> UIOutputTex = nullptr;
   D3D11_TEXTURE2D_DESC UIOutputTexDesc;
   
   com_ptr<ID3D11RenderTargetView> UIOutputRtv = nullptr;
   D3D11_RENDER_TARGET_VIEW_DESC UIOutputRtvDesc;
   
   com_ptr<ID3D11ShaderResourceView> UIOutputSrv = nullptr;
   D3D11_RENDER_TARGET_VIEW_DESC UIOutputSrvDesc;
   
   bool IsFinalCopyToken = false;

   
   void ResetOnSwapchain()
   {
      //invalidate
      UIOutputTex = nullptr;
      UIOutputRtv = nullptr;
      UIOutputSrv = nullptr;
   }
   
   void ResetOnPresent()
   {
      IsDrawnToSwapchain = false;
      IsDrawnAutoExposure0 = false;
      // IsAutoExposure0ClearingHistory = false; //dont need to reset
      // IsDrawnMLAAPrev = IsDrawnMLAA;
      // IsDrawnMLAA = false;
      IsFinalCopyToken = false;
   }
};

namespace ShaderDefineInfo
{
   constexpr uint32_t SWAPCHAIN_TEST_USER_PEAK         = char_ptr_crc32("SWAPCHAIN_TEST_USER_PEAK");
   // constexpr uint32_t CUSTOM_TONEMAP                   = char_ptr_crc32("CUSTOM_TONEMAP");
   constexpr uint32_t CUSTOM_TONEMAP_SCALING           = char_ptr_crc32("CUSTOM_TONEMAP_SCALING");
   constexpr uint32_t CUSTOM_TONEMAP_CLAMP             = char_ptr_crc32("CUSTOM_TONEMAP_CLAMP");
   constexpr uint32_t CUSTOM_CLAMP_PEAK                = char_ptr_crc32("CUSTOM_CLAMP_PEAK");
   constexpr uint32_t CUSTOM_HDTVREC709_1              = char_ptr_crc32("CUSTOM_HDTVREC709_1");
   constexpr uint32_t CUSTOM_PCBLOWOUT                 = char_ptr_crc32("CUSTOM_PCBLOWOUT");
   constexpr uint32_t CUSTOM_FAKEBT2020                = char_ptr_crc32("CUSTOM_FAKEBT2020");
   constexpr uint32_t CUSTOM_LUT_BLOWOUT_REDUCTION     = char_ptr_crc32("CUSTOM_LUT_BLOWOUT_REDUCTION");
   constexpr uint32_t CUSTOM_LUT_BLOWOUT_GAUSSIAN      = char_ptr_crc32("CUSTOM_LUT_BLOWOUT_GAUSSIAN");
   constexpr uint32_t CUSTOM_COLORGRADE                = char_ptr_crc32("CUSTOM_COLORGRADE");
   constexpr uint32_t CUSTOM_COLORGRADE_SATORDER       = char_ptr_crc32("CUSTOM_COLORGRADE_SATORDER");
   constexpr uint32_t CUSTOM_UPSCALE_MOV               = char_ptr_crc32("CUSTOM_UPSCALE_MOV");
   constexpr uint32_t CUSTOM_UPSCALE_BGSPRITES         = char_ptr_crc32("CUSTOM_UPSCALE_BGSPRITES");
   constexpr uint32_t CUSTOM_UPSCALE_TOON              = char_ptr_crc32("CUSTOM_UPSCALE_TOON");
   // constexpr uint32_t CUSTOM_MLAA_PQ                   = char_ptr_crc32("CUSTOM_MLAA_PQ");
   constexpr uint32_t CUSTOM_HUDBRIGHTNESS             = char_ptr_crc32("CUSTOM_HUDBRIGHTNESS");
   constexpr uint32_t CUSTOM_HUDBRIGHTNESS_CUSTOM      = char_ptr_crc32("CUSTOM_HUDBRIGHTNESS_CUSTOM");
   constexpr uint32_t CUSTOM_GAMMA_CORRECTION_MODE     = char_ptr_crc32("CUSTOM_GAMMA_CORRECTION_MODE");
   constexpr uint32_t CUSTOM_GAMMACORRECT22            = char_ptr_crc32("CUSTOM_GAMMACORRECT22");
   constexpr uint32_t CUSTOM_UITRANSPARENCY            = char_ptr_crc32("CUSTOM_UITRANSPARENCY");
   constexpr uint32_t CUSTOM_TESTSDR                   = char_ptr_crc32("CUSTOM_TESTSDR");
   constexpr uint32_t CUSTOM_TESTBGSPRITES             = char_ptr_crc32("CUSTOM_TESTBGSPRITES");
   constexpr uint32_t CUSTOM_UPGRADE_DEBUG             = char_ptr_crc32("CUSTOM_UPGRADE_DEBUG");
   constexpr uint32_t CUSTOM_PROGRESSBAR               = char_ptr_crc32("CUSTOM_PROGRESSBAR");
   constexpr uint32_t CUSTOM_TONEMAP_IDENTIFY          = char_ptr_crc32("CUSTOM_TONEMAP_IDENTIFY");
   constexpr uint32_t CUSTOM_SDR                       = char_ptr_crc32("CUSTOM_SDR");
   constexpr uint32_t CUSTOM_PERCHANNELLUMAEMULATE     = char_ptr_crc32("CUSTOM_PERCHANNELLUMAEMULATE");

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

   static bool UIToggleCheckmark(uint32_t d, const char* label, const char* tooltip)
   {
      bool def = GetB(d);
      
      ImGui::PushID(std::string(label).append("_").append(std::to_string(d)).c_str());
      bool c = ImGui::Checkbox(label, &def);
      ImGui::PopID();

      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip(tooltip);
      
      if (c) ToggleBool(d);
      
      UIResetButton(d);
      return def;
   }
      
   int UIDropDown(uint32_t d, const char* label, const char* const items[], const char* tooltip)
   {
      int def = Get(d);
      bool c = ImGui::Combo(label, &def, items, IM_ARRAYSIZE(items));
      if (c) Set(d, def);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip(tooltip);
      UIResetButton(d);
      return def;
   }

   // Overload: pass items inline as braced args, e.g. {"A", "B", "C"}
   int UIDropDown(uint32_t d, const char* label, std::initializer_list<const char*> items_list, const char* tooltip)
   {
      std::vector<const char*> items(items_list);
      int def = Get(d);
      bool c = ImGui::Combo(label, &def, items.data(), static_cast<int>(items.size()));
      if (c) Set(d, def);
      if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip(tooltip);
      UIResetButton(d);
      return def;
   }
}

namespace AutoExposureFix
{
   static int rate_replacement = 60;
   constexpr auto reshade_save = "AutoExposureFix";
   // static uint count_history_clear = 

   static int vp_curr_i = 0;

   static double time_last_ae_allow;
   static double time_curr;
   static float GetTimeBetweenAllowedDraws() { return 1000.0f / rate_replacement; }

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
}

namespace CachedCB
{
   static bool is_dirty = true;
   
   constexpr static float white_clip_def = /*0.022f*/0.1650;
   static float white_clip = white_clip_def;
   static bool is_rec709;

   static float peak_prev;
   static float paper_prev;
   static float white_clip_prev;
   static bool is_rec709_prev;

   float Encode_sRGB(float x)
   {
      if (x <= 0.0031308f) return 12.92f * x;
      else return 1.055f * powf(x, 1.f / 2.4f) - 0.055f;
   }

   float Decode_sRGB(float x)
   {
      if (x <= 0.04045f) return x / 12.92f;
      else return powf((x + 0.055f) / 1.055f, 2.4f);
   }

   float Encode_Rec709(float x)
   {
      float r0, r1;
      r1 = x;
      r0 = pow(r1, 0.449999988);
      r0 = r0 * 1.09899998 + -0.0989999995;
      bool r2 = (0.0179999992 >= r1);
      r1 = 4.5 * r1;
      r0 = r2 ? r1 : r0;
      return r0;
   }

   float Decode_Rec709(float x)
   {
      float r0, r2, r4;
      r0 = x;
      r2 = 0.0989999995 + r0; 
      r2 = 0.909918129 * r2;
      r2 = pow(r2, 2.22222233);
      bool r3 = 0.0810000002 >= r0;
      r4 = 0.222222224 * r0;
      r2 = r3 ? r4 : r2;
      return r2;
   }
   
   float CalcWhiteClip(float p, float pw, float wc)
   {
      float bruh1 = (p / 1000.f);
      float bruh = bruh1;
      bruh = pow(bruh, bruh1 < 1.f ? 4.4f : 3.6f); // fudge
      return (wc / pw) * 6000000.f * bruh; //kms, this is the biggest bandaid of all bandaids. gamma lighting ahh
   }

   float CalcPeak(float p, float pw, bool rec709)
   {
      p /= pw;
      if (rec709)
      {
         p = Encode_sRGB(p);
         p = Decode_Rec709(p);
      }
      return p;
   }

   void Update(DeviceData& device_data, ProjectDivaMegaMixGameDeviceData& game_device_data)
   {
      //changed?
      is_rec709 = ShaderDefineInfo::GetB(ShaderDefineInfo::CUSTOM_HDTVREC709_1);
      if (cb_luma_global_settings.ScenePeakWhite != peak_prev || cb_luma_global_settings.ScenePaperWhite != paper_prev || white_clip != white_clip_prev || is_rec709 != is_rec709_prev)
      {
         is_dirty = true;
         peak_prev = cb_luma_global_settings.ScenePeakWhite;
         paper_prev = cb_luma_global_settings.ScenePaperWhite;
         white_clip_prev = white_clip;
         is_rec709_prev = is_rec709;
      }

      //gatekeep
      if (!is_dirty) return;
      is_dirty = false;

      //update
      cb_luma_global_settings.GameSettings.TonemapperPeakCached = CalcPeak(cb_luma_global_settings.ScenePeakWhite, cb_luma_global_settings.ScenePaperWhite, is_rec709);
      cb_luma_global_settings.GameSettings.TonemapperMaxExpectedCached = CalcWhiteClip(cb_luma_global_settings.ScenePeakWhite, cb_luma_global_settings.ScenePaperWhite, white_clip);
      device_data.cb_luma_global_settings_dirty = true;
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
   static uint32_t*  addr_puiGameLimit;
   static uintptr_t* addr_menuFlagPtr;
   static char*   addr_pvNameString;
   static uint32_t*  addr_pvID;
   static float_t*   addr_pvTimeSec;
   static float_t*   addr_pvTimeTotalSec;

   static void Init()
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
   static bool IsMenu() 
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
   static CurrentPV current_pv;

   struct PrevSettings
   {
      float peak = -1;
   };
   static PrevSettings prev_settings;
   
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
      if (is_dirty)
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
      if (is_dirty && prev_pv != nullptr)
      {
         //peak
         if (prev_pv->is_clamp_1_stop && roundf(cb_luma_global_settings.ScenePeakWhite) == roundf(cb_luma_global_settings.ScenePaperWhite * 2.f))
            cb_luma_global_settings.ScenePeakWhite = prev_settings.peak;
      }

      //save settings & apply new settings
      if (is_dirty && current_pv.item != nullptr)
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
      ImGui::Bullet(); ImGui::SameLine();
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 1.0f, 0.7f, 1.f));
      ImGui::TextWrapped("On a select few PVs, I decided to tune/nerf this mod to stay more faithful to the original.");
      ImGui::PopStyleColor();
      
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
      
      ImGui::Text("Current PV ID: %d", current_pv.id);
      ImGui::Text("Current PV Name (maybe): %s", name_str.c_str());

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

   static bool enabled = false;
   static int limit = 0;
   static bool menu_clamp = false;

   static bool IsReady()
   {
      return MemoryHack::addr_puiGameLimit != nullptr && MemoryHack::addr_menuFlagPtr != nullptr;
   }

   //must be per frame update/patch as the game forces and reset to 60
   static void Patch(const bool force_unclamp = false)
   {
      if (!enabled) return;
      if (!IsReady()) return;
      uint32_t target = static_cast<uint32_t>(limit);
      if (!force_unclamp && !menu_clamp && MemoryHack::IsMenu()) target = 60u;
      *MemoryHack::addr_puiGameLimit = target; //no need for VirtualProtect
   }

   static void Unpatch()
   {
      if (!IsReady()) return;
      *MemoryHack::addr_puiGameLimit = 60u;
   }
}

namespace ProgressBar
{
   static bool enabled = false;
   static float progress_ratio = -1.f;
   static float progress_ratio_prev = -1.f;

   void OnUI(reshade::api::effect_runtime* runtime)
   {
      //ui progress bar
      float progress_ratio_ui = *MemoryHack::addr_pvTimeSec / *MemoryHack::addr_pvTimeTotalSec;
      ImGui::ProgressBar(progress_ratio_ui);

      //ui stats
      ImGui::Text("Time: %.2f / %.2f", *MemoryHack::addr_pvTimeSec, *MemoryHack::addr_pvTimeTotalSec);
      ImGui::Text("Remaining: %.2f sec", *MemoryHack::addr_pvTimeTotalSec - *MemoryHack::addr_pvTimeSec);

      //cb
      bool enabled_prev = enabled;
      enabled = ShaderDefineInfo::UIDropDown(ShaderDefineInfo::CUSTOM_PROGRESSBAR, "HUD Progress Bar", {"Off", "Top", "Bottom"}, "Draw a simple progress bar for PVs.");
      if (!enabled) progress_ratio = -1.f;
      if (enabled_prev != enabled) reshade::set_config_value(nullptr, NAME, "ProgressBarEnabled", enabled);
   }

   void OnPresent()
   {
      if (!enabled) return;
      progress_ratio_prev = progress_ratio;
      progress_ratio = *MemoryHack::addr_pvTimeSec / *MemoryHack::addr_pvTimeTotalSec;
      cb_luma_global_settings.GameSettings.ProgressBarRatio = progress_ratio > progress_ratio_prev ? progress_ratio : -1;
   }

   void OnLoad(reshade::api::effect_runtime* runtime)
   {
      enabled = ShaderDefineInfo::Get(ShaderDefineInfo::CUSTOM_PROGRESSBAR) > 0;
      
      bool saved_enabled;
      reshade::get_config_value(runtime, NAME, "ProgressBarEnabled", saved_enabled);
      enabled |= saved_enabled;
      
      cb_luma_global_settings.GameSettings.ProgressBarRatio = -1.f;
      std::string s = "ProgressBar::OnLoad() enabled: " + std::to_string(enabled);
      message(reshade::log::level::info, s.c_str());
   }
}

namespace SeparateUIBrightness
{
   static bool enabled = true;
   constexpr float brightness_menu_def = 203.f;
   constexpr float brightness_game_def = 300.f;
   static float brightness_menu = brightness_menu_def;
   static float brightness_game = brightness_game_def;

   void OnUI(reshade::api::effect_runtime* runtime)
   {
      //enabled checkmark
      ImGui::PushID("Separate UI Brightness: Enabled");
      if (ImGui::Checkbox("Enabled", &enabled))
      {
         reshade::set_config_value(nullptr, NAME, "SeparateUIBrightnessEnabled", enabled);
#ifdef DAV_CORE
         ui_brightness_slider_enabled = !enabled;
#endif
      }
      ImGui::PopID();
      
      bool is_disabled = !enabled;
      if (is_disabled) ImGui::BeginDisabled();
      {
         ImGui::PushID("Separate UI Brightness: Menu");
         if (ImGui::SliderFloat("Menu Brightness", &brightness_menu, 1.f, 1000.f, "%.0f nits"))
            reshade::set_config_value(runtime, NAME, "SeparateUIBrightnessMenu", brightness_menu);
         ImGui::PopID();
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("UI paper white when browsing menus.");
         DrawResetButton(brightness_menu, brightness_menu_def, "SeparateUIBrightnessMenu", runtime);

         ImGui::PushID("Separate UI Brightness: Gameplay");
         if (ImGui::SliderFloat("Game Brightness", &brightness_game, 1.f, 1000.f, "%.0f nits"))
            reshade::set_config_value(runtime, NAME, "SeparateUIBrightnessGame", brightness_game);
         ImGui::PopID();
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("UI paper white when playing a PV / in gameplay.");
         DrawResetButton(brightness_game, brightness_game_def, "SeparateUIBrightnessGame", runtime);
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
      reshade::get_config_value(runtime, NAME, "SeparateUIBrightnessEnabled", enabled);
#ifdef DAV_CORE
      ui_brightness_slider_enabled = !enabled;
#endif
      
      reshade::get_config_value(runtime, NAME, "SeparateUIBrightnessMenu", brightness_menu);
      reshade::get_config_value(runtime, NAME, "SeparateUIBrightnessGame", brightness_game);
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
      //log
      message(reshade::log::level::info, "OnInit()");
      
      //Def
      std::vector<ShaderDefineData> game_shader_defines_data = {
         
         {"GAMMA_CORRECTION_RANGE_TYPE", '0', true, !DEVELOPMENT, "0 - Full range.\n1 - 0-1 only.", 1},
         {"SWAPCHAIN_SKIPALL", '0', true, false, "Skip majority of the swapchain proxy shader (DisplayComposite.hlsl).\nWill not decode gamma if shaders are disabled/unloaded.", 1},
         // {"SWAPCHAIN_CLAMP_PEAK", '0', true, false, "Clamp the absolute final color.\n0 - Unclamped (up to display).\n1 - Per channel clamp (blows out).\n2 - Scale down by max channel (sat preserving).", 2},
         {"SWAPCHAIN_CLAMP_COLORSPACE", '0', true, !DEVELOPMENT, "Clamp colorspace against invalid colors.\n(Really only for OCD, as it should only be inconsequential black.)\n0 - Unclamped.\n1 - BT2020.", 1},
         {"SWAPCHAIN_TEST_USER_PEAK", '0', true, false, "Show a simple white rectangle peak test.", 1},
         // {"_____CUSTOM_____", '0', true, false, "Just a divider.", 1},
         // {"CUSTOM_TONEMAP", '3', true, false, "HDR tonemapper, primarily for the shoulder.\n0 - Off (Unclamped).\n1 - Reinhard Piecewise (Gradual)\n2 - Frostbite Exponential Rolloff (Aggressive)\n3 - Hermite Spline (Scalable)\n4 - NeuTwo (Aggressive)"},
         {"CUSTOM_TONEMAP_SCALING", '0', true, false, "HDR tonemap scaling.\n0 - Luminance (natural)\n1 - Max-Channel (saturation preserve)", 1},
         {"CUSTOM_TONEMAP_CLAMP", '1', true, false, "(Only if CUSTOM_TONEMAP_SCALING is luminance scaled.)\nClamp overshoot from luma scaled HDR tonemap.\n0 - Unclamped (up to display).\n1 - Per channel clamp (blows out).\n2 - Scale down by max channel (sat preserving).", 2},
         {"CUSTOM_CLAMP_PEAK", '1', true, false, "Clamp the absolute final color.\n0 - Unclamped (up to display).\n1 - Per channel clamp (blows out).\n2 - Scale down by max channel (sat preserving).\n3 - Per channel rolloff slightly above peak (blows out).", 3},
         {"CUSTOM_TONEMAP_TRYIGNOREUI", '0', true, false, "If only UI is rendering, deactivates HDR tonemapper.", 1},
         {"CUSTOM_GAMMA_CORRECTION_MODE", '1', true, true, "0 - Per-Channel.\n1 - Perceptual.", 1},
         // {"CUSTOM_PCBLOWOUT", '3', true, false, "Choose how the per-channel blowout is done.\n0 - Clamp (Clip/Brickwall) NOT RECOMMENDED\n1 - Reinhard Piecewise (Gradual) DEPRECATED\n2 - Frostbite Exponential Rolloff (Aggressive) DEPRECATED\n3 - NeuTwo (Natural)", 3},
         {"CUSTOM_FAKEBT2020", '1', true, false, "Encode BT2020 before gamma decode to push colors out to wcg.", 1},
         // {"CUSTOM_LUT_BLOWOUT_REDUCTION", '0', true, false, "Enable YCbCr LUT blowout reduction.", 1},
         {"CUSTOM_LUT_BLOWOUT_GAUSSIAN", '1', true, false, "Enable YCbCr LUT biased gaussian blur to stop steep chrominance drop offs in the curve.", 1},
         {"CUSTOM_UPGRADE_DEBUG", '0', true, false, "Show inputs into UpgradeToneMap().", 5},
         {"CUSTOM_COLORGRADE", '0', true, false, "Enable HDR luminance color grading.", 1},
         {"CUSTOM_COLORGRADE_SATORDER", '2', true, false, "Enable HDR global saturation slider.\n0 - Off\n1 - BT709 Before UI\n2 - BT2020 After UI", 2},
         {"CUSTOM_UPSCALE_MOV", '0', true, false, "PumboAutoHDR for FMV.\n0 - Off\n1 - On", 1},
         {"CUSTOM_UPSCALE_BGSPRITES", '0', true, false, "Auto HDR (Inverse Tonemap) for background 2D sprites in complex \"Future Tone\" scenes (e.g. Torinoko City).", 1},
         {"CUSTOM_UPSCALE_TOON", '0', true, false, "Auto HDR for flat toon scenes (e.g. Catch the Wave, Deep Sea City Underground, etc.).\n0 - Forced SDR\n1 - Treat as Complex\n2 - On\n3 - On (Ignore Customization Menu)", 3},
         // {"CUSTOM_MLAA_PQ", '1', true, false, "Encode color into PQ for MLAA to better resolve.", 1},
         {"CUSTOM_HUDBRIGHTNESS", '1', true, false, "Sample shader texture resources to detect specific UI to change their brightness.\nElse, they are too bright.", 2},
         // {"CUSTOM_HUDBRIGHTNESS_CUSTOM", '1', true, false, "There is a texture for a bunch of HUD. It includes all notes and other stuff.", 2},
         {"CUSTOM_TONEMAP_IDENTIFY", '0', true, !DEVELOPMENT, "Draw binary representation of tonemap uber variant number.", 1},
         {"CUSTOM_HDTVREC709_1", '0', true, false, "Decode color and swapchain to HDTV rec.709, like PS4's display output.", 1},
         {"CUSTOM_GAMMACORRECT22", '1', true, false, "Enable Gamma Correction 2.2 for OS and displays missing it.", 1},
         {"CUSTOM_UITRANSPARENCY", '0', true, false, "Enable UI transparency.", 1},
         {"CUSTOM_TESTSDR", '0', true, false, "Disable HDR shaders.", 1},
         {"CUSTOM_TESTBGSPRITES", '0', true, false, "Test BG Sprites layering.", 2},
         {"CUSTOM_PROGRESSBAR", '0', true, false, "Play head progress bar.", 2},
         {"CUSTOM_PERCHANNELLUMAEMULATE", '1', true, false, "Emulate luminance loss from LDR per-channel tonemapping on single channel bright colors.", 1},
         {"CUSTOM_SDR", '0', true, false, "(Automatically managed) Compile shader without HDR upgrades.", 2},
      };
      shader_defines_data.append_range(game_shader_defines_data);
      auto_recompile_defines = true; //force
      // allow_disabling_gamma_ramp = true; //needed!
      assert(shader_defines_data.size() < MAX_SHADER_DEFINES);
      
      //Default built-in
      GetShaderDefineData(POST_PROCESS_SPACE_TYPE_HASH).SetDefaultValue('0');
      GetShaderDefineData(EARLY_DISPLAY_ENCODING_HASH).SetDefaultValue('0');
      GetShaderDefineData(VANILLA_ENCODING_TYPE_HASH).SetDefaultValue('1');
      GetShaderDefineData(GAMMA_CORRECTION_TYPE_HASH).SetDefaultValue('0'); GetShaderDefineData(GAMMA_CORRECTION_TYPE_HASH).SetValue('0'); GetShaderDefineData(GAMMA_CORRECTION_TYPE_HASH).SetValueFixed(true);
      GetShaderDefineData(UI_DRAW_TYPE_HASH).SetDefaultValue('2');
      if (!DEVELOPMENT)
      {
         ShaderDefineInfo::Set(DEVELOPMENT_HASH, false);
         // GetShaderDefineData(DEVELOPMENT_HASH).SetValueFixed(true);
         // GetShaderDefineData(DEVELOPMENT_HASH).SetValue(false);
         // GetShaderDefineData(DEVELOPMENT_HASH).editable = false;
         // GetShaderDefineData(TEST_SDR_HDR_SPLIT_VIEW_MODE_NATIVE_IMPL_HASH).SetValueFixed(true);
         // GetShaderDefineData(TEST_SDR_HDR_SPLIT_VIEW_MODE_NATIVE_IMPL_HASH).editable = false;
         // GetShaderDefineData(char_ptr_crc32("TEST_SDR_HDR_SPLIT_VIEW_MODE")).SetValueFixed(true);
         // GetShaderDefineData(char_ptr_crc32("TEST_SDR_HDR_SPLIT_VIEW_MODE")).editable = false;
      }
      
      //cb
      luma_settings_cbuffer_index = 13;
      luma_data_cbuffer_index = 12;

      //Global default
      use_os_reference_white_level = false;
      
      //GameSettings default
      // default_luma_global_game_settings.TonemapperRolloffStart = cb_luma_global_settings.GameSettings.TonemapperRolloffStart = 36.f;
      default_luma_global_game_settings.BloomStrength = cb_luma_global_settings.GameSettings.BloomStrength = 1.f;
      default_luma_global_game_settings.AAMultiplier = cb_luma_global_settings.GameSettings.AAMultiplier = 2.f;
      default_luma_global_game_settings.PerChannelLuminanceReductionEmulateStrength = cb_luma_global_settings.GameSettings.PerChannelLuminanceReductionEmulateStrength = 0.25f;
      
      default_luma_global_game_settings.GammaCorrection22PaperWhite = cb_luma_global_settings.GameSettings.GammaCorrection22PaperWhite = 203.f;
      default_luma_global_game_settings.GammaPerceptualChrominanceCorrect = cb_luma_global_settings.GameSettings.GammaPerceptualChrominanceCorrect = 0.25f;
      
      default_luma_global_game_settings.UITransparency = cb_luma_global_settings.GameSettings.UITransparency = 1.f;
      
      // default_luma_global_game_settings.SDRTonemapToeStrength = cb_luma_global_settings.GameSettings.SDRTonemapToeStrength = 2.f;
      // default_luma_global_game_settings.SDRTonemapToeLowPass = cb_luma_global_settings.GameSettings.SDRTonemapToeLowPass = 0.9f;
      
      // default_luma_global_game_settings.LUTNeutralize = cb_luma_global_settings.GameSettings.LUTNeutralize = 0.5f;
      // default_luma_global_game_settings.LUTBlowoutReduction = cb_luma_global_settings.GameSettings.LUTBlowoutReduction = 0.1685f;
      // default_luma_global_game_settings.LUTBlowoutReductionLookBack = cb_luma_global_settings.GameSettings.LUTBlowoutReductionLookBack = 0.525f;
      default_luma_global_game_settings.LUTScalingAndMakeUp = cb_luma_global_settings.GameSettings.LUTScalingAndMakeUp = 0.995f;
      default_luma_global_game_settings.LUTGaussianBlurStep = cb_luma_global_settings.GameSettings.LUTGaussianBlurStep = 40.f;
      default_luma_global_game_settings.LUTGaussianBlurBias = cb_luma_global_settings.GameSettings.LUTGaussianBlurBias = 3.1f;
      
      // default_luma_global_game_settings.PCBlowoutHue = cb_luma_global_settings.GameSettings.PCBlowoutHue = 1.f;
      // default_luma_global_game_settings.PCBlowoutChrom = cb_luma_global_settings.GameSettings.PCBlowoutChrom = 1.f;
      // default_luma_global_game_settings.PCBlowoutStart = cb_luma_global_settings.GameSettings.PCBlowoutStart = 1.385f; //also NueTwo clip
      // default_luma_global_game_settings.PCBlowoutEnd = cb_luma_global_settings.GameSettings.PCBlowoutEnd = 1.21f;
      default_luma_global_game_settings.PCBlowoutLumaEnd = cb_luma_global_settings.GameSettings.PCBlowoutLumaEnd = 2.016f;
      default_luma_global_game_settings.PCBlowoutPerChannelEnd = cb_luma_global_settings.GameSettings.PCBlowoutPerChannelEnd = 2.64f;
      default_luma_global_game_settings.PCBlowoutPerChannelClip = cb_luma_global_settings.GameSettings.PCBlowoutPerChannelClip = 3.918f;
      default_luma_global_game_settings.PCBlowoutPerChannel2ndStartRatio = cb_luma_global_settings.GameSettings.PCBlowoutPerChannel2ndStartRatio = 0.93f;
      default_luma_global_game_settings.PCBlowoutPerChannel2ndEnd = cb_luma_global_settings.GameSettings.PCBlowoutPerChannel2ndEnd = 2.517f;
      
      // default_luma_global_game_settings.FakeBT2020Gamma = cb_luma_global_settings.GameSettings.FakeBT2020Gamma = 1.5f;
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
   }

   // This needs to be overridden with your own "GameDeviceData" sub-class (destruction is automatically handled)
   void OnCreateDevice(ID3D11Device* native_device, DeviceData& device_data) override
   {
      //log
      message(reshade::log::level::info, "OnCreateDevice()");
      
      //device_data.game
      device_data.game = new ProjectDivaMegaMixGameDeviceData;

      //ResetGammaRamp
      // GammaRamp::ResetGammaRamp(device_data);

      //HighFPS
      MemoryHack::Init();
   }

   void OnInitSwapchain(reshade::api::swapchain* swapchain)
   {
      //log
      message(reshade::log::level::info, "OnInitSwapchain()");
      
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

      // AUTO EXPOSURE FIX ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
      if (AutoExposureFix::rate_replacement > 0 &&
         !game_device_data.IsDrawnAutoExposure0 &&
         !TonemapInfo::GetDrawnTonemap(cb_luma_global_settings.GameSettings.TonemapInfo) &&
         original_shader_hashes.pixel_shaders[0] == ShaderHashesLists::AutoExposure0)
      {
         //progress
         game_device_data.IsDrawnAutoExposure0 = true;

         //detect if writing to all 32x1 (clears history) or just 1x1 (preserves history)
         D3D11_VIEWPORT vp{};
         UINT num_vp = 1;
         native_device_context->RSGetViewports(&num_vp, &vp);
         bool is_clear = num_vp > 0 && vp.Width > 1.f;
         // if (is_clear) AutoExposureFix::count_history_clear++; //stats

         //cleared, so reset our index
         if (is_clear) AutoExposureFix::vp_curr_i = 0; 

         //allow draw?
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
      
      // TONEMAP UBER ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
      if (!TonemapInfo::GetDrawnFinal(cb_luma_global_settings.GameSettings.TonemapInfo) && //if final went, no tonemap possible
         !TonemapInfo::GetDrawnTonemap(cb_luma_global_settings.GameSettings.TonemapInfo))
      {
         //get
         int ti = cb_luma_global_settings.GameSettings.TonemapInfo;
         
         //complex (future tone)
         switch (original_shader_hashes.pixel_shaders[0])
         {
            case ShaderHashesLists::Tonemap0: //complex
               ti = TonemapInfo::SetIndexAndDrawnTonemapTrue(ti, 0); break;
            case ShaderHashesLists::Tonemap1: //complex sprite
               ti = TonemapInfo::SetIndexAndDrawnTonemapTrue(ti, 1); break;
            case ShaderHashesLists::Tonemap2: //complex (un-witnessed)
               ti = TonemapInfo::SetIndexAndDrawnTonemapTrue(ti, 2); break;
            case ShaderHashesLists::Tonemap3: //complex sprites (un-witnessed)
               ti = TonemapInfo::SetIndexAndDrawnTonemapTrue(ti, 3); break;
            case ShaderHashesLists::Tonemap4: //complex sprites (un-witnessed)
               ti = TonemapInfo::SetIndexAndDrawnTonemapTrue(ti, 4); break;
            case ShaderHashesLists::Tonemap5: //fast
               ti = TonemapInfo::SetIndexAndDrawnTonemapTrue(ti, 5); break;
            case ShaderHashesLists::Tonemap6: //fast (un-witnessed)
               ti = TonemapInfo::SetIndexAndDrawnTonemapTrue(ti, 6); break;
            case ShaderHashesLists::Tonemap7: //fast sprites (un-witnessed)
               ti = TonemapInfo::SetIndexAndDrawnTonemapTrue(ti, 7); break;
            case ShaderHashesLists::Tonemap8: //fast sprites 
               ti = TonemapInfo::SetIndexAndDrawnTonemapTrue(ti, 8); break;
            case ShaderHashesLists::Tonemap9: //fast (un-witnessed)
               ti = TonemapInfo::SetIndexAndDrawnTonemapTrue(ti, 9); break;
            case ShaderHashesLists::Tonemap10: //fast sprites (in customization)
               ti = TonemapInfo::SetIndexAndDrawnTonemapTrue(ti, 10); break;
            default: ; //noop
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

      // //Detect MLAA
      // if (!game_device_data.IsDrawnMLAA &&
      //    original_shader_hashes.Contains(ShaderHashesLists::MLAAFinal))
      // {
      //    game_device_data.IsDrawnMLAA = true;
      //
      //    return DrawOrDispatchOverrideType::None;
      // }
      
      // FINAL /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
      
      if (!TonemapInfo::GetDrawnFinal(cb_luma_global_settings.GameSettings.TonemapInfo) &&
         original_shader_hashes.pixel_shaders[0] == ShaderHashesLists::Final/*original_shader_hashes.Contains(ShaderHashesLists::Final)*/)
      {
         //drawn
         cb_luma_global_settings.GameSettings.TonemapInfo = TonemapInfo::SetDrawnFinalTrue(cb_luma_global_settings.GameSettings.TonemapInfo);
         device_data.has_drawn_main_post_processing = true;
         device_data.cb_luma_global_settings_dirty = true;

         //UI Transparency: IsFinalCopyToken
         if (cb_luma_global_settings.GameSettings.UITransparency < 1.f && game_device_data.UIOutputRtv.get() != nullptr)
         {
            //give token
            game_device_data.IsFinalCopyToken = true;
         }
         
         return DrawOrDispatchOverrideType::None;
      }

      // UI /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

      //See EXTRA
      // Includes PV's FXs but also HUD.

      // //Mov
      // if (TonemapInfo::GetDrawnFinal(cb_luma_global_settings.GameSettings.TonemapInfo) &&
      //    original_shader_hashes.pixel_shaders[0] == ShaderHashesLists::Mov/*original_shader_hashes.Contains(ShaderHashesLists::Mov)*/)
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
         original_shader_hashes.pixel_shaders[0] == ShaderHashesLists::UISpritesHPBarDelta /*original_shader_hashes.Contains(ShaderHashesLists::UISpritesHPBarDelta)*/)
      {
         //flag
         cb_luma_global_settings.GameSettings.TonemapInfo = TonemapInfo::SetDrawnHPBarDeltaTrue(cb_luma_global_settings.GameSettings.TonemapInfo);
         device_data.cb_luma_global_settings_dirty = true;
      }

      //UI Transparency: IsFinalCopyToken
      if (cb_luma_global_settings.GameSettings.UITransparency < 1.f && game_device_data.IsFinalCopyToken)
      {
         //use token
         game_device_data.IsFinalCopyToken = false;
      
         //error: not exist
         ASSERT(game_device_data.UIOutputTexOrig.get() != nullptr);
      
         //copy
         native_device_context->CopyResource(game_device_data.UIOutputTex.get(), game_device_data.UIOutputTexOrig.get());
      }

      // TO SWAPCHAIN /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
      
      if (TonemapInfo::GetDrawnFinal(cb_luma_global_settings.GameSettings.TonemapInfo) &&
         !game_device_data.IsDrawnToSwapchain &&
         original_shader_hashes.pixel_shaders[0] == ShaderHashesLists::ToSwapchain /*original_shader_hashes.Contains(ShaderHashesLists::ToSwapchain)*/)
      {
         game_device_data.IsDrawnToSwapchain = true;

         //UI Transparency
         if (cb_luma_global_settings.GameSettings.UITransparency < 1.f && game_device_data.UIOutputTex.get() == nullptr)
         {
            //shader res 0
            com_ptr<ID3D11ShaderResourceView> srv;
            native_device_context->PSGetShaderResources(0, 1, &srv);
            ASSERT(srv.get() != nullptr);
         
            //get resource
            com_ptr<ID3D11Resource> srv_res;
            srv->GetResource(&srv_res);
            ASSERT(srv_res.get() != nullptr);
            
            //get tex
            com_ptr<ID3D11Texture2D> srv_tex;
            auto hr0 = srv_res->QueryInterface(&srv_tex);
            ASSERT(SUCCEEDED(hr0));
            game_device_data.UIOutputTexOrig = srv_tex; //save for later
         
            //get desc
            D3D11_TEXTURE2D_DESC stv_tex_desc;
            srv_tex->GetDesc(&stv_tex_desc);
            
            //create desc unorm
            game_device_data.UIOutputTexDesc.Width          = stv_tex_desc.Width;
            game_device_data.UIOutputTexDesc.Height         = stv_tex_desc.Height;
            game_device_data.UIOutputTexDesc.MipLevels      = stv_tex_desc.MipLevels;
            game_device_data.UIOutputTexDesc.ArraySize      = stv_tex_desc.ArraySize;
            game_device_data.UIOutputTexDesc.Format         = stv_tex_desc.Format /*DXGI_FORMAT_R16G16B16A16_UNORM*/;
            game_device_data.UIOutputTexDesc.SampleDesc     = stv_tex_desc.SampleDesc;
            game_device_data.UIOutputTexDesc.Usage          = stv_tex_desc.Usage;
            game_device_data.UIOutputTexDesc.BindFlags      = stv_tex_desc.BindFlags;
            game_device_data.UIOutputTexDesc.CPUAccessFlags = stv_tex_desc.CPUAccessFlags;
            game_device_data.UIOutputTexDesc.MiscFlags      = stv_tex_desc.MiscFlags;
            
            //create tex
            auto hr1 = native_device->CreateTexture2D(&game_device_data.UIOutputTexDesc, nullptr, &game_device_data.UIOutputTex);
            ASSERT(SUCCEEDED(hr1));
            
            //create rtv for later
            auto hr2 = native_device->CreateRenderTargetView(game_device_data.UIOutputTex.get(), nullptr, &game_device_data.UIOutputRtv);
            ASSERT(SUCCEEDED(hr2));
            
            //create shader res for later
            auto hr3 = native_device->CreateShaderResourceView(game_device_data.UIOutputTex.get(), nullptr, &game_device_data.UIOutputSrv);
            ASSERT(SUCCEEDED(hr3));
         
            //skip so shader dont explode (just 1 frame)
            return DrawOrDispatchOverrideType::Skip;
         }
         
         //add ui tex as shader res
         if (cb_luma_global_settings.GameSettings.UITransparency < 1.f)
            native_device_context->PSSetShaderResources(1, 1, &game_device_data.UIOutputSrv);
         
         return DrawOrDispatchOverrideType::None;
      }

      // EXTRA /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

      // //FULLSCREEN OVERLAY FX
      // if (!Globals::IsFullscreenOverlayFx &&
      //    TonemapInfo::GetDrawnTonemap(cb_luma_global_settings.GameSettings.TonemapInfo) &&
      //    !TonemapInfo::GetDrawnFinal(cb_luma_global_settings.GameSettings.TonemapInfo))
      // {
      //    //case: FXAA
      //    if (!game_device_data.IsDrawnMLAAPrev) return DrawOrDispatchOverrideType::Skip;
      //
      //    //case: MLAA
      //    if (game_device_data.IsDrawnMLAA) return DrawOrDispatchOverrideType::Skip;
      //
      //    //case: wait until MLAA
      //    return DrawOrDispatchOverrideType::None;
      // }
      
      //UI
      if (TonemapInfo::GetDrawnFinal(cb_luma_global_settings.GameSettings.TonemapInfo) &&
         !game_device_data.IsDrawnToSwapchain &&
         original_shader_hashes.pixel_shaders[0] != ShaderHashesLists::Mov /*!original_shader_hashes.Contains(ShaderHashesLists::Mov)*/)
      {
         //skip IsUI
         if (!Globals::IsUI) return DrawOrDispatchOverrideType::Skip;

         //skip SpritesText
         if (Globals::IsSkipTextAfterFinal
            && original_shader_hashes.pixel_shaders[0] == ShaderHashesLists::UISpritesText /*original_shader_hashes.Contains(ShaderHashesLists::UISpritesText)*/)
            return DrawOrDispatchOverrideType::Skip; 
         
         //UI Transparency: Replace RTV
         if (cb_luma_global_settings.GameSettings.UITransparency < 1.f)
            native_device_context->OMSetRenderTargets(1, &game_device_data.UIOutputRtv, nullptr);
      }

      // //IsSkipUntilUI
      // if (Globals::IsSkipUntilUI &&
      //    !TonemapInfo::GetDrawnTonemap(cb_luma_global_settings.GameSettings.TonemapInfo) &&
      //    !TonemapInfo::GetDrawnFinal(cb_luma_global_settings.GameSettings.TonemapInfo))
      // {
      //    return DrawOrDispatchOverrideType::Skip;
      // }
      
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
      device_data.has_drawn_main_post_processing = false;

      //HighFPS
      HighFPS::Patch();

      //ProgressBar
      ProgressBar::OnPresent();

      //IndividualPVTuning
      IndividualPVTuning::OnPresent();

      //SeparateUIBrightness
      SeparateUIBrightness::OnPresent();

      //CachedCB
      CachedCB::Update(device_data, game_device_data);
   }

   void LoadConfigs() override
   {
      //log
      message(reshade::log::level::info, "LoadConfigs()");
      
      reshade::api::effect_runtime* runtime = nullptr;

      //try force 400 nits
      if (!reshade::get_config_value(runtime, NAME, "ScenePeakWhite", cb_luma_global_settings.ScenePeakWhite)) cb_luma_global_settings.ScenePeakWhite = 1000.f;

      //Load ReShade settings
      // reshade::get_config_value(runtime, NAME, "TonemapperRolloffStart", cb_luma_global_settings.GameSettings.TonemapperRolloffStart);
      reshade::get_config_value(runtime, NAME, "TonemapperMaxExpected", CachedCB::white_clip/*cb_luma_global_settings.GameSettings.TonemapperMaxExpected*/);
      reshade::get_config_value(runtime, NAME, "BloomStrength", cb_luma_global_settings.GameSettings.BloomStrength);
      reshade::get_config_value(runtime, NAME, "AAMultiplier", cb_luma_global_settings.GameSettings.AAMultiplier);
      reshade::get_config_value(runtime, NAME, "PerChannelLuminanceReductionEmulateStrength", cb_luma_global_settings.GameSettings.PerChannelLuminanceReductionEmulateStrength);
      
      reshade::get_config_value(runtime, NAME, "GammaCorrection22PaperWhite", cb_luma_global_settings.GameSettings.GammaCorrection22PaperWhite);
      reshade::get_config_value(runtime, NAME, "GammaPerceptualChrominanceCorrect", cb_luma_global_settings.GameSettings.GammaPerceptualChrominanceCorrect);

      reshade::get_config_value(runtime, NAME, "UITransparency", cb_luma_global_settings.GameSettings.UITransparency);
      
      // reshade::get_config_value(runtime, NAME, "SDRTonemapToeStrength", cb_luma_global_settings.GameSettings.SDRTonemapToeStrength);
      // reshade::get_config_value(runtime, NAME, "SDRTonemapToeLowPass", cb_luma_global_settings.GameSettings.SDRTonemapToeLowPass);

      // reshade::get_config_value(runtime, NAME, "LUTNeutralize", cb_luma_global_settings.GameSettings.LUTNeutralize);
      // reshade::get_config_value(runtime, NAME, "LUTBlowoutReduction", cb_luma_global_settings.GameSettings.LUTBlowoutReduction);
      // reshade::get_config_value(runtime, NAME, "LUTBlowoutReductionLookBack", cb_luma_global_settings.GameSettings.LUTBlowoutReductionLookBack);
      reshade::get_config_value(runtime, NAME, "LUTScalingAndMakeUp", cb_luma_global_settings.GameSettings.LUTScalingAndMakeUp);
      reshade::get_config_value(runtime, NAME, "LUTGaussianBlurStep", cb_luma_global_settings.GameSettings.LUTGaussianBlurStep);
      reshade::get_config_value(runtime, NAME, "LUTGaussianBlurBias", cb_luma_global_settings.GameSettings.LUTGaussianBlurBias);
      
      // reshade::get_config_value(runtime, NAME, "PCBlowoutHue", cb_luma_global_settings.GameSettings.PCBlowoutHue);
      // reshade::get_config_value(runtime, NAME, "PCBlowoutChrom", cb_luma_global_settings.GameSettings.PCBlowoutChrom);
      // reshade::get_config_value(runtime, NAME, "PCBlowoutStart", cb_luma_global_settings.GameSettings.PCBlowoutStart);
      // reshade::get_config_value(runtime, NAME, "PCBlowoutEnd", cb_luma_global_settings.GameSettings.PCBlowoutEnd);
      reshade::get_config_value(runtime, NAME, "PCBlowoutLumaEnd", cb_luma_global_settings.GameSettings.PCBlowoutLumaEnd);
      reshade::get_config_value(runtime, NAME, "PCBlowoutPerChannelClip", cb_luma_global_settings.GameSettings.PCBlowoutPerChannelClip);
      reshade::get_config_value(runtime, NAME, "PCBlowoutPerChannelEnd", cb_luma_global_settings.GameSettings.PCBlowoutPerChannelEnd);
      reshade::get_config_value(runtime, NAME, "PCBlowoutPerChannel2ndStartRatio", cb_luma_global_settings.GameSettings.PCBlowoutPerChannel2ndStartRatio);
      reshade::get_config_value(runtime, NAME, "PCBlowoutPerChannel2ndEnd", cb_luma_global_settings.GameSettings.PCBlowoutPerChannel2ndEnd);

      // reshade::get_config_value(runtime, NAME, "FakeBT2020Gamma", cb_luma_global_settings.GameSettings.FakeBT2020Gamma);
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
      
      // reshade::get_config_value(runtime, NAME, "IsFullscreenOverlayFx", Globals::IsFullscreenOverlayFx);
      reshade::get_config_value(runtime, NAME, "IsUI", Globals::IsUI);
      // reshade::get_config_value(runtime, NAME, "IsSkipUntilUI", Globals::IsSkipUntilUI);
      reshade::get_config_value(runtime, NAME, "IsSkipTextAfterFinal", Globals::IsSkipTextAfterFinal);

      reshade::get_config_value(runtime, NAME, "UIIsAdvanced", Globals::UIIsAdvanced);
      reshade::get_config_value(runtime, NAME, "UIIsReadmeDone", Globals::UIIsReadmeDone);
      reshade::get_config_value(runtime, NAME, AutoExposureFix::reshade_save, AutoExposureFix::rate_replacement);

      reshade::get_config_value(runtime, NAME, "HighFPS_enabled", HighFPS::enabled);
      reshade::get_config_value(runtime, NAME, "HighFPS_limit", HighFPS::limit);
      reshade::get_config_value(runtime, NAME, "HighFPS_menu_clamp", HighFPS::menu_clamp);

      ProgressBar::OnLoad(runtime);

      IndividualPVTuning::OnLoad(runtime);

      SeparateUIBrightness::OnLoad(runtime);
      
      // if (custom_sdr_gamma == 0) custom_sdr_gamma = 2.2f;
      // reshade::get_config_value(runtime, NAME, "EOTFGammaCorrection", custom_sdr_gamma);
      
      // defines_need_recompilation = true;
      // GetGameDeviceData(device_data).cb_luma_global_settings_dirty = true;
   }

   void DrawImGuiSettings(DeviceData& device_data) override
   {
      reshade::api::effect_runtime* runtime = nullptr;

      auto& game_device_data = GetGameDeviceData(device_data);
      bool is_disabled; //for Begin/EndDisabled();

      // //SpecialK mode
      // if (Globals::IsSKMode && ImGui::CollapsingHeader("SpecialK Mode README"))
      // {
      //    ImGui::BulletText("\"ReShade64.dll\" is detected in the game folder, meaning SpecialK mode is on!\n(Delete if false positive.)");
      //    ImGui::BulletText("Luma has somewhat relinquished control of the swapchain.");
      //    ImGui::BulletText("Please have SpecialK upgrade swapchain to scRGB in HDR Options submenu and choose the 3rd preset (scRGB native/passthrough, Shift+F3)!");
      // }

      //CUSTOM_SDR sync
      bool is_sdr = cb_luma_global_settings.DisplayMode == DisplayModeType::SDR;
      {
         auto def = ShaderDefineInfo::Get(ShaderDefineInfo::CUSTOM_SDR);
         bool is_dirty = def > 0 != is_sdr;
         if (is_dirty) ShaderDefineInfo::Set(ShaderDefineInfo::CUSTOM_SDR, is_sdr ? 1 : 0);
      }

      //SWAPCHAIN_TEST_USER_PEAK
      if (cb_luma_global_settings.DisplayMode != DisplayModeType::SDR) ShaderDefineInfo::UIToggleCheckmark(ShaderDefineInfo::SWAPCHAIN_TEST_USER_PEAK, "Test Display Peak", "Show a simple test pattern (2 Rectangles: 10000 nits outer VS user settings inner) to check if the display peak brightness is correctly set.\n\n- If display is set to HGiG, which hard clips, the technically best value is the lowest where the inner rectangle disappears.\n\n- If display is set to Static Tonemap, it will try to fit up to 10000 nits down, so you have to search up your model, or eye it by finding when the roll off starts.\n\nAlso consider other factors like chrominance loss at higher nits, Automatic Brightness Limiter (ABL), and personal preference.");

      if (!Globals::UIIsReadmeDone)
      {
         ImGui::Separator(); ////////////////////////////////////////////////////////////////////////////////////

         ImGui::Text("README:");
         ImGui::BulletText("UI elems of PV (e.g. lens flare) are done after HDR tonemap, affected by UI Brightness slider.");
         ImGui::BulletText("Toon shading (Non-Physical Rendering) sucks, and are clamped SDR unless changed otherwise.");

         ImGui::NewLine(); //////
      
         ImGui::BulletText("Recommended Mod (GameBanana):");
         ImGui::SameLine();
         if (ImGui::Button("Clean Interface: Remove all but the notes."))
            Website::OpenWebsite("https://gamebanana.com/mods/524644");

         ImGui::BulletText("Recommended Mod (GameBanana):");
         ImGui::SameLine();
         if (ImGui::Button("Remove Forced Toon Shader: Toon shading sucks!"))
            Website::OpenWebsite("https://gamebanana.com/mods/578377");
         
         ImGui::BulletText("Recommended Mod (GameBanana):");
         ImGui::SameLine();
         if (ImGui::Button("Future Tone Customization: Toon shading sucks!"))
            Website::OpenWebsite("https://gamebanana.com/mods/386869");


         ImGui::NewLine(); //////

         //close readme
         if (ImGui::Button("Ok & Dismiss"))
         {
            Globals::UIIsReadmeDone = true;
            reshade::set_config_value(runtime, NAME, "UIIsReadmeDone", Globals::UIIsReadmeDone);
         }
      }

      // ImGui::Separator(); ////////////////////////////////////////////////////////////////////////////////////
      if (!is_sdr && ImGui::CollapsingHeader("Gamma"))
      {
         //link test
         if (ImGui::Button("Gamma Mismatch Explanation & Correction Test (Google Slides)"))
            Website::OpenWebsite("https://docs.google.com/presentation/d/e/2PACX-1vSXeLHlbm6repcS7fels1-SXYGRmzziRrnuJ8nDO8J5rsWV3dT1-nVyCKp0Tj_stwx-9qlCI-N6rYIT/pub?start=true&loop=false&slide=id.g3e007eafba8_0_0");
         
         //paper white
         if (ImGui::SliderFloat("EOTF / Gamma Correction 2.2", &cb_luma_global_settings.GameSettings.GammaCorrection22PaperWhite, 0.f, 500.f, "%.0f"));
            reshade::set_config_value(runtime, NAME, "GammaCorrection22PaperWhite", cb_luma_global_settings.GameSettings.GammaCorrection22PaperWhite);
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("The threshold / paper white. Values lower are effected.\nUse if shadows are raised because OS (Windows) and display doesn't lower already.");
         DrawResetButton(cb_luma_global_settings.GameSettings.GammaCorrection22PaperWhite, 203.f, "GammaCorrection22PaperWhite", runtime);

         //set CUSTOM_GAMMACORRECT22 define based on if paper white is above 0 or not
         ShaderDefineInfo::Set(ShaderDefineInfo::CUSTOM_GAMMACORRECT22, cb_luma_global_settings.GameSettings.GammaCorrection22PaperWhite > 0.f);

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
               if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Reduce chrominance/saturation increase from Gamma Correction in the Perceptual mode, preventing it from becoming too artificial.\n(Or a high pass for chrominance boost.)");
               DrawResetButton(cb_luma_global_settings.GameSettings.GammaPerceptualChrominanceCorrect, default_luma_global_game_settings.GammaPerceptualChrominanceCorrect, "GammaPerceptualChrominanceCorrect", runtime);
            }
            if (is_disabled_perceptual) ImGui::EndDisabled();
         }
         if (is_disabled) ImGui::EndDisabled();

         ImGui::NewLine();////////////////

         //CUSTOM_HDTVREC709_1
         {
            bool b = ShaderDefineInfo::Get(ShaderDefineInfo::CUSTOM_HDTVREC709_1) == 1;
            if (ImGui::Checkbox("PS4 / Rec. 709 Gamma", &b)) ShaderDefineInfo::ToggleBool(ShaderDefineInfo::CUSTOM_HDTVREC709_1);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Do aggressive HDTV Rec. 709 gamma seen on PS4.\nWatch out for crushed shadows!");
         }
      }

      // ImGui::Separator(); ////////////////////////////////////////////////////////////////////////////////////
      if (!is_sdr)
      {
         if (SeparateUIBrightness::enabled) ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.4f, 0.4f, 0.8f, 1.f));
         if (ImGui::CollapsingHeader("Separate UI Brightness"))
            SeparateUIBrightness::OnUI(runtime);
         if (SeparateUIBrightness::enabled) ImGui::PopStyleColor();
      }
         
      // ImGui::Separator(); ////////////////////////////////////////////////////////////////////////////////////
      if (ImGui::CollapsingHeader("Individual UI Brightness"))
      {
         {
            int def = ShaderDefineInfo::UIDropDown(ShaderDefineInfo::CUSTOM_HUDBRIGHTNESS, "Custom HUD Brightness",
               { "Off", "Vanilla", "Simple UI (simple_ui_v115.zip)"/*, "Clean Interface ()" */},
               "Specifically target certain UI elements that are too bright when unclamped to HDR.\nThese check for the specific vanilla textures, so mods that change UI textures will break these.");
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
      {
         bool has_pv_tuning = IndividualPVTuning::current_pv.item != nullptr;
         if (has_pv_tuning) ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.4f, 0.4f, 0.8f, 1.f));
         if (!is_sdr && ImGui::CollapsingHeader("Individual PV Tuning")) IndividualPVTuning::OnUI(runtime);
         if (has_pv_tuning) ImGui::PopStyleColor();
      }

      // ImGui::Separator(); ////////////////////////////////////////////////////////////////////////////////////

      //show advanced
      if (!Globals::UIIsAdvanced)
      {
         ImGui::Separator(); ////////////////////////////////////////////////////////////////////////////////////
         if (ImGui::Checkbox("Show Advanced Settings", &Globals::UIIsAdvanced))
            reshade::set_config_value(runtime, NAME, "UIIsAdvanced", Globals::UIIsAdvanced);

#if DEVELOPMENT
         ImGui::Separator();
#endif
         return;
      } else
      {
         ImGui::Separator();
      }
      
      // ImGui::Separator(); ////////////////////////////////////////////////////////////////////////////////////

      //HDR Tonemapper Settings
      if (!is_sdr && ImGui::CollapsingHeader("HDR Tonemapper & Clamping"))
      {
         // int tonemap_def = ShaderDefineInfo::Get(ShaderDefineInfo::CUSTOM_TONEMAP);
         
         is_disabled = false;

         // is_disabled = !(tonemap_def == 1 /*|| tonemap_index == 2*/ || tonemap_def == 3 /*|| tonemap_index == 4*/);
         if (!is_disabled)
         {
            if (ImGui::SliderFloat("HDR Tonemapper Expected Max", &CachedCB::white_clip, 0.f, 0.2f, "%.4f"))
               reshade::set_config_value(runtime, NAME, "TonemapperMaxExpected", CachedCB::white_clip);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("HDR tonemapper's expected max nits (this is a multiplier to an internal value).\nReduce to cause white clipping.");
            DrawResetButton(CachedCB::white_clip, CachedCB::white_clip_def, "TonemapperMaxExpected", runtime);
         }

         // is_disabled = tonemap_def == 0;
         if (!is_disabled)
         {
            ShaderDefineInfo::UIDropDown(ShaderDefineInfo::CUSTOM_TONEMAP_SCALING, "HDR Tonemapper Scaling",
               { "Luminance (Natural / Vanilla)", "Max Channel (Unnatural Saturation Preserve?)" },
               "The pivot for the tonemapper to use and scale color.");
         }

         // is_disabled = tonemap_def == 0 || scaling_def != 0;
         if (!is_disabled)
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
      if (ImGui::CollapsingHeader("Upgraded Vanilla Tonemap & Color Grading"))
      {
         if (ImGui::SliderFloat("Bloom", &cb_luma_global_settings.GameSettings.BloomStrength, 0.f, 2.f))
            reshade::set_config_value(runtime, NAME, "BloomStrength", cb_luma_global_settings.GameSettings.BloomStrength);
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Bloom strength.");
         DrawResetButton(cb_luma_global_settings.GameSettings.BloomStrength, default_luma_global_game_settings.BloomStrength, "BloomStrength", runtime);
         
         if (ImGui::SliderInt("Auto-Exposure: History Write Rate", &AutoExposureFix::rate_replacement, 0, 120, "%d FPS"))
            reshade::set_config_value(runtime, NAME, AutoExposureFix::reshade_save, AutoExposureFix::rate_replacement);
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Auto-Exposure history (32px ring buffer) is done per-frame.\nOn high FPS, this cause rapid exposure changes as older history is rapidly overriden.\n\nThis feature will limit Auto-Exposure rate (60 FPS default),\nwhile still allowing history clearing on camera cut.");
         DrawResetButton(AutoExposureFix::rate_replacement, 60, AutoExposureFix::reshade_save, runtime);
         
         // ImGui::NewLine(); ///////////

         // ShaderDefineInfo::UIToggleCheckmark(ShaderDefineInfo::CUSTOM_MLAA_PQ, "MLAA PQ Encode", "Encode input in Perceptual Quantizer (PQ) before MLAA to maybe let it better detect edges.");

         if (is_sdr) goto AfterVanillaColorGrade; //skip if SDR
         
         if (ImGui::SliderFloat("MLAA Weights", &cb_luma_global_settings.GameSettings.AAMultiplier, 1.f, 5.f))
            reshade::set_config_value(runtime, NAME, "AAMultiplier", cb_luma_global_settings.GameSettings.AAMultiplier);
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Multiplier on the input color into MLAA.\nIncrease to have it detect more edges, but may cause false positives.");
         DrawResetButton(cb_luma_global_settings.GameSettings.AAMultiplier, default_luma_global_game_settings.AAMultiplier, "AAMultiplier", runtime);
         
         ImGui::NewLine(); ///////////

         // ShaderDefineInfo::UIToggleCheckmark(ShaderDefineInfo::CUSTOM_LUT_BLOWOUT_REDUCTION, "LUT Look Back (Fast)", "Sample the YCbCr LUT at a less blown out point to reduce blowout.");
         //
         // if (ShaderDefineInfo::Get(ShaderDefineInfo::CUSTOM_LUT_BLOWOUT_REDUCTION) > 0) {
         //    ImGui::PushID("LUT BR: 0");
         //    ImGui::SameLine(); if (ImGui::Button("(Low)"))
         //    {
         //       cb_luma_global_settings.GameSettings.LUTBlowoutReduction = 0.085f;
         //       cb_luma_global_settings.GameSettings.LUTBlowoutReductionLookBack = default_luma_global_game_settings.LUTBlowoutReductionLookBack;
         //       reshade::set_config_value(runtime, NAME, "LUTBlowoutReduction", cb_luma_global_settings.GameSettings.LUTBlowoutReduction);
         //       reshade::set_config_value(runtime, NAME, "LUTBlowoutReductionLookBack", cb_luma_global_settings.GameSettings.LUTBlowoutReductionLookBack);
         //    }
         //    ImGui::PopID();
         //
         //    ImGui::PushID("LUT BR: 1");
         //    ImGui::SameLine(); if (ImGui::Button("(Normal / Recommended)"))
         //    {
         //       cb_luma_global_settings.GameSettings.LUTBlowoutReduction = default_luma_global_game_settings.LUTBlowoutReduction;
         //       cb_luma_global_settings.GameSettings.LUTBlowoutReductionLookBack = default_luma_global_game_settings.LUTBlowoutReductionLookBack;
         //       reshade::set_config_value(runtime, NAME, "LUTBlowoutReduction", cb_luma_global_settings.GameSettings.LUTBlowoutReduction);
         //       reshade::set_config_value(runtime, NAME, "LUTBlowoutReductionLookBack", cb_luma_global_settings.GameSettings.LUTBlowoutReductionLookBack);
         //    }
         //    ImGui::PopID();
         //
         //    ImGui::PushID("LUT BR: 2");
         //    ImGui::SameLine(); if (ImGui::Button("(High)"))
         //    {
         //       cb_luma_global_settings.GameSettings.LUTBlowoutReduction = 0.25f;
         //       cb_luma_global_settings.GameSettings.LUTBlowoutReductionLookBack = 0.45f;
         //       reshade::set_config_value(runtime, NAME, "LUTBlowoutReduction", cb_luma_global_settings.GameSettings.LUTBlowoutReduction);
         //       reshade::set_config_value(runtime, NAME, "LUTBlowoutReductionLookBack", cb_luma_global_settings.GameSettings.LUTBlowoutReductionLookBack);
         //    }
         //    ImGui::PopID();
         //
         //    ImGui::PushID("LUT BR: 3");
         //    ImGui::SameLine(); if (ImGui::Button("(Extreme)"))
         //    {
         //       cb_luma_global_settings.GameSettings.LUTBlowoutReduction = 0.33f;
         //       cb_luma_global_settings.GameSettings.LUTBlowoutReductionLookBack = 0.35f;
         //       reshade::set_config_value(runtime, NAME, "LUTBlowoutReduction", cb_luma_global_settings.GameSettings.LUTBlowoutReduction);
         //       reshade::set_config_value(runtime, NAME, "LUTBlowoutReductionLookBack", cb_luma_global_settings.GameSettings.LUTBlowoutReductionLookBack);
         //    }
         //    ImGui::PopID();
         // }
         // is_disabled = ShaderDefineInfo::Get(ShaderDefineInfo::CUSTOM_LUT_BLOWOUT_REDUCTION) == 0;
         // if (is_disabled) ImGui::BeginDisabled(); 
         // {
         //    if (ImGui::SliderFloat("LUT Look Back", &cb_luma_global_settings.GameSettings.LUTBlowoutReduction, 0.f, 1.0f, "%.4f"))
         //       reshade::set_config_value(runtime, NAME, "LUTBlowoutReduction", cb_luma_global_settings.GameSettings.LUTBlowoutReduction);
         //    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("YCbCr LUT blowout reduction.\nToo high/strong will lead to coloring things that should be blown out white.");
         //    DrawResetButton(cb_luma_global_settings.GameSettings.LUTBlowoutReduction, default_luma_global_game_settings.LUTBlowoutReduction, "LUTBlowoutReduction", runtime);
         //
         //    if (ImGui::SliderFloat("LUT Look Back: Luma Multiplier", &cb_luma_global_settings.GameSettings.LUTBlowoutReductionLookBack, 0.f, 1.0f, "%.4f"))
         //       reshade::set_config_value(runtime, NAME, "LUTBlowoutReductionLookBack", cb_luma_global_settings.GameSettings.LUTBlowoutReductionLookBack);
         //    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Multiplier on luminance to sample the YCbCr LUT at a less blown out spot.\nToo low/far will lead to coloring things that should be blown out white!");
         //    DrawResetButton(cb_luma_global_settings.GameSettings.LUTBlowoutReductionLookBack, default_luma_global_game_settings.LUTBlowoutReductionLookBack, "LUTBlowoutReductionLookBack", runtime);
         // }
         // if (is_disabled) ImGui::EndDisabled();
         //
         // ImGui::NewLine(); ///////////
         
         is_disabled = !ShaderDefineInfo::UIToggleCheckmark(ShaderDefineInfo::CUSTOM_LUT_BLOWOUT_GAUSSIAN, "LUT Biased Gaussian Blur Sampling", "Sample the YCbCr LUT with a gaussian blur,\nbiased towards higher chrominance,\nwhich help reduces steep chrominance falloff.");
         if (is_disabled) ImGui::BeginDisabled(); 
         {
            if (ImGui::SliderFloat("LUT Gaussian Blur: Step", &cb_luma_global_settings.GameSettings.LUTGaussianBlurStep, 1.f, 80.f, "%.1f"))
               reshade::set_config_value(runtime, NAME, "LUTGaussianBlurStep", cb_luma_global_settings.GameSettings.LUTGaussianBlurStep);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("The step size for the gaussian blur when sampling the LUT for blowout reduction.\nHigher values will be recover and smooth out chrominance falloff.");
            DrawResetButton(cb_luma_global_settings.GameSettings.LUTGaussianBlurStep, default_luma_global_game_settings.LUTGaussianBlurStep, "LUTGaussianBlurStep", runtime);
            
            if (ImGui::SliderFloat("LUT Gaussian Blur: Bias", &cb_luma_global_settings.GameSettings.LUTGaussianBlurBias, 0.f, 10.f, "%.4f"))
               reshade::set_config_value(runtime, NAME, "LUTGaussianBlurBias", cb_luma_global_settings.GameSettings.LUTGaussianBlurBias);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("The bias for the gaussian blur when sampling the LUT for blowout reduction.\nHigher values will bias the sampling towards higher chrominance, which recovers chrominance.");
            DrawResetButton(cb_luma_global_settings.GameSettings.LUTGaussianBlurBias, default_luma_global_game_settings.LUTGaussianBlurBias, "LUTGaussianBlurBias", runtime); 
         }
         if (is_disabled) ImGui::EndDisabled();

         // ImGui::NewLine(); ///////////
         
         // if (ImGui::SliderFloat("LUT Luminance Neutralize", &cb_luma_global_settings.GameSettings.LUTNeutralize, 0.f, 1.f))
         //    reshade::set_config_value(runtime, NAME, "LUTNeutralize", cb_luma_global_settings.GameSettings.LUTNeutralize);
         // if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Neutralize upper luminance change and shoulder from LUT.\nUseful to better preserve the linear-ness of image.\n\nCreated in response to \"Calc.\" from PJD X Song Pack.\nVanilla LUTs are neutral already, and this will not cause a big difference.");
         // DrawResetButton(cb_luma_global_settings.GameSettings.LUTNeutralize, default_luma_global_game_settings.LUTNeutralize, "LUTNeutralize", runtime);

         if (ImGui::SliderFloat("LUT Scaling & Makeup: Multiplier", &cb_luma_global_settings.GameSettings.LUTScalingAndMakeUp, 0.8f, 1.0f, "%.4f"))
            reshade::set_config_value(runtime, NAME, "LUTScalingAndMakeUp", cb_luma_global_settings.GameSettings.LUTScalingAndMakeUp);
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Multiplier on LUT results and the makeup gain (reciprocal) afterwards.\nLower to give LUT lookup just a bit of headroom, increasing saturation from the YCbCr tonemap, which is usable by Per-Channel Blowout.");
         DrawResetButton(cb_luma_global_settings.GameSettings.LUTScalingAndMakeUp, default_luma_global_game_settings.LUTScalingAndMakeUp, "LUTScalingAndMakeUp", runtime);
         
         ImGui::NewLine(); ///////////
         
         {
            //red
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.7f, 0.7f, 1.f));
            ImGui::Text("(Extra Extreme Settings! Changing these will shift important hues like skin tones.)");
            ImGui::PopStyleColor();
            
            if (ImGui::SliderFloat("Per-Channel Blowout: Luminance Rolloff Peak", &cb_luma_global_settings.GameSettings.PCBlowoutLumaEnd, 1.f, 5.f, "%.5f"))
               reshade::set_config_value(runtime, NAME, "PCBlowoutLumaEnd", cb_luma_global_settings.GameSettings.PCBlowoutLumaEnd);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("After upgrade, rolloff luminance to shape input into the per-channel tonemapper.");
            DrawResetButton(cb_luma_global_settings.GameSettings.PCBlowoutLumaEnd, default_luma_global_game_settings.PCBlowoutLumaEnd, "PCBlowoutLumaEnd", runtime);
            
            if (ImGui::SliderFloat("Per-Channel Blowout: Per-Channel Rolloff Clip", &cb_luma_global_settings.GameSettings.PCBlowoutPerChannelClip, 1.f, 20.f, "%.5f"))
               reshade::set_config_value(runtime, NAME, "PCBlowoutPerChannelClip", cb_luma_global_settings.GameSettings.PCBlowoutPerChannelClip);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Controls the shoulder white clippiness of the per-channel tonemapper.");
            DrawResetButton(cb_luma_global_settings.GameSettings.PCBlowoutPerChannelClip, default_luma_global_game_settings.PCBlowoutPerChannelClip, "PCBlowoutPerChannelClip", runtime);

                        
            if (ImGui::SliderFloat("Per-Channel Blowout: Per-Channel Rolloff Peak", &cb_luma_global_settings.GameSettings.PCBlowoutPerChannelEnd, 1.f, 5.f, "%.5f"))
               reshade::set_config_value(runtime, NAME, "PCBlowoutPerChannelEnd", cb_luma_global_settings.GameSettings.PCBlowoutPerChannelEnd);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Controls the peak of the per-channel tonemapper.");
            DrawResetButton(cb_luma_global_settings.GameSettings.PCBlowoutPerChannelEnd, default_luma_global_game_settings.PCBlowoutPerChannelEnd, "PCBlowoutPerChannelEnd", runtime);

            if (ImGui::SliderFloat("Per-Channel Blowout 2nd: Start Ratio", &cb_luma_global_settings.GameSettings.PCBlowoutPerChannel2ndStartRatio, 0.f, 1.f, "%.4f"))
               reshade::set_config_value(runtime, NAME, "PCBlowoutPerChannel2ndStartRatio", cb_luma_global_settings.GameSettings.PCBlowoutPerChannel2ndStartRatio);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("A 2nd extremely aggressive pass.\nFrom 0 to peak, when should the shoulder start for the per-channel tonemapper?");
            DrawResetButton(cb_luma_global_settings.GameSettings.PCBlowoutPerChannel2ndStartRatio, default_luma_global_game_settings.PCBlowoutPerChannel2ndStartRatio, "PCBlowoutPerChannel2ndStartRatio", runtime);
            
            if (ImGui::SliderFloat("Per-Channel Blowout 2nd: Peak", &cb_luma_global_settings.GameSettings.PCBlowoutPerChannel2ndEnd, 1.f, 6.f, "%.5f"))
               reshade::set_config_value(runtime, NAME, "PCBlowoutPerChannel2ndEnd", cb_luma_global_settings.GameSettings.PCBlowoutPerChannel2ndEnd);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("A 2nd extremely aggressive pass.\nThe peak of the per-channel tonemapper.");
            DrawResetButton(cb_luma_global_settings.GameSettings.PCBlowoutPerChannel2ndEnd, default_luma_global_game_settings.PCBlowoutPerChannel2ndEnd, "PCBlowoutPerChannel2ndEnd", runtime);

            ImGui::NewLine(); //////////

            //CUSTOM_PERCHANNELLUMAEMULATE
            {
               auto def = ShaderDefineInfo::UIToggleCheckmark(ShaderDefineInfo::CUSTOM_PERCHANNELLUMAEMULATE, "Per-Channel Luminance Reduction Emulate", "Emulate the luminance loss from LDR per-channel tonemapping on bright single channel colors.");
            
               is_disabled = !def;
               if (is_disabled) ImGui::BeginDisabled(); 
               {
                  // cb_luma_global_settings.GameSettings.PerChannelLuminanceReductionEmulateStrength
                  if (ImGui::SliderFloat("Per-Channel Luminance Reduction: Strength", &cb_luma_global_settings.GameSettings.PerChannelLuminanceReductionEmulateStrength, 0.f, 1.f, "%.4f"))
                     reshade::set_config_value(runtime, NAME, "PerChannelLuminanceReductionEmulateStrength", cb_luma_global_settings.GameSettings.PerChannelLuminanceReductionEmulateStrength);
                  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Emulate the luminance loss from LDR per-channel tonemapping on bright single channel colors.");
                  DrawResetButton(cb_luma_global_settings.GameSettings.PerChannelLuminanceReductionEmulateStrength, default_luma_global_game_settings.PerChannelLuminanceReductionEmulateStrength, "PerChannelLuminanceReductionEmulateStrength", runtime);
               }
               if (is_disabled) ImGui::EndDisabled();
            }
         }
         

         // ImGui::NewLine(); ///////////
         //
         // if (ImGui::SliderFloat("Toe: Strength", &cb_luma_global_settings.GameSettings.SDRTonemapToeStrength, 0.0f, 4.f))
         //    reshade::set_config_value(runtime, NAME, "SDRTonemapToeStrength", cb_luma_global_settings.GameSettings.SDRTonemapToeStrength);
         // if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Controls the amount of influence of SDR toe (shadows) on HDR luminance.");
         // DrawResetButton(cb_luma_global_settings.GameSettings.SDRTonemapToeStrength, default_luma_global_game_settings.SDRTonemapToeStrength, "SDRTonemapToeStrength", runtime);
         //
         // if (ImGui::SliderFloat("Toe: Low Pass", &cb_luma_global_settings.GameSettings.SDRTonemapToeLowPass, 0.0f, 5.0f))
         //    reshade::set_config_value(runtime, NAME, "SDRTonemapToeLowPass", cb_luma_global_settings.GameSettings.SDRTonemapToeLowPass);
         // if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Increase to tighten the low pass and target only darker shadows.");
         // DrawResetButton(cb_luma_global_settings.GameSettings.SDRTonemapToeLowPass, default_luma_global_game_settings.SDRTonemapToeLowPass, "SDRTonemapToeLowPass", runtime);
      }

      AfterVanillaColorGrade:
      
      // ImGui::Separator(); ////////////////////////////////////////////////////////////////////////////////////
      
      if (!is_sdr && ImGui::CollapsingHeader("Fake BT2020 (Gamut Expansion)"))
      {
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
      if (!is_sdr && ImGui::CollapsingHeader("HDR Color Grading"))
      {
         bool def = ShaderDefineInfo::UIToggleCheckmark(ShaderDefineInfo::CUSTOM_COLORGRADE, "RenoDX Pre-UI Luminance Color Grading", "Custom color grading from RenoDX.\nKinda like an audio equalizer but for luminance.");
      
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
            { "Off", "In BT709 before UI", "In BT2020 after UI" },
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
      if (ImGui::CollapsingHeader("Simple PV Progress Bar"))
      {
         ProgressBar::OnUI(runtime);
      }

      // ImGui::Separator(); ////////////////////////////////////////////////////////////////////////////////////
      if (ImGui::CollapsingHeader("FPS Limiter (Fallback)"))
      {
         ImGui::BulletText("This is a fallback for when my DisplayCommander fork becomes outdated.");
         ImGui::BulletText("If 0 (unclamped), requires VSync off!");
         
         if (ImGui::Checkbox("High FPS: Active", &HighFPS::enabled))
         {
            reshade::set_config_value(runtime, NAME, "HighFPS_enabled", HighFPS::enabled);
            if (!HighFPS::enabled) HighFPS::Unpatch();
         }
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Continuously patches the game's memory to set a new limit.");
      
         is_disabled = !HighFPS::enabled;
         if (is_disabled) ImGui::BeginDisabled(is_disabled);
         {
            if (ImGui::Checkbox("High FPS: 60FPS Menus", &HighFPS::menu_clamp))
               reshade::set_config_value(runtime, NAME, "HighFPS_menu_clamp", HighFPS::menu_clamp);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("I found unclamping purely beneficial, allowing for fast UI navigation, and decreasing load times (warming phase)!");
      
            if (ImGui::SliderInt("High FPS: Limit", &HighFPS::limit, 0, 1000))
               reshade::set_config_value(runtime, NAME, "HighFPS_limit", HighFPS::limit);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("The new limit.\nSet 0 for none.");
         }
         if (is_disabled) ImGui::EndDisabled();
      }

      // ImGui::Separator(); ////////////////////////////////////////////////////////////////////////////////////
      if (!is_sdr && ImGui::CollapsingHeader("Fake/Auto HDR (DEPRECATED)"))
      {
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
            bool def = ShaderDefineInfo::UIToggleCheckmark(ShaderDefineInfo::CUSTOM_UPSCALE_BGSPRITES, "Upscale BG Sprites", "Apply an inverse tonemapper to SDR limited background sprites.\n\nThis help balance it with unclamped 3D HDR elements render atop.\nFalse posimaptives may include Amatsu Kitsune's moon at ending if this is tuned too high.");
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
         ImGui::Text("(FYI) Render Order: BG Sprites -> 3D -> Tonemap -> MLAA -> Final -> UI Sprites -> Swapchain");
         
         // if (ImGui::Checkbox("Fullscreen Overlay FX", &Globals::IsFullscreenOverlayFx))
         //    reshade::set_config_value(runtime, NAME, "IsFullscreenOverlayFx", Globals::IsFullscreenOverlayFx);
         // if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         //    ImGui::SetTooltip("Toggle IsFullscreenOverlayFx.\nWill discard all shaders after the tonemap shader up until the final shader.");
         // DrawResetButton(Globals::IsFullscreenOverlayFx, true, "IsFullscreenOverlayFx", runtime);
      
         if (ImGui::Checkbox("Draw UI", &Globals::IsUI))
            reshade::set_config_value(runtime, NAME, "IsUI", Globals::IsUI);
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Toggle UI.\nIf off, will discard all UI sprite shaders after the final shader.");
         DrawResetButton(Globals::IsUI, true, "IsUI", runtime);

         // if (ImGui::Checkbox("Skip Until UI", &Globals::IsSkipUntilUI))
         //    reshade::set_config_value(runtime, NAME, "IsSkipUntilUI", Globals::IsSkipUntilUI);
         // if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         //    ImGui::SetTooltip("Skip as much draw calls as possible until UI starts drawing.");
         // DrawResetButton(Globals::IsSkipUntilUI, false, "IsSkipUntilUI", runtime);

         if (ImGui::Checkbox("Skip UI Text (For Lyrics)", &Globals::IsSkipTextAfterFinal))
            reshade::set_config_value(runtime, NAME, "IsSkipTextAfterFinal", Globals::IsSkipTextAfterFinal);
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("For turning off lyrics, skips all text after final shader has drawn.");
         DrawResetButton(Globals::IsSkipTextAfterFinal, false, "IsSkipTextAfterFinal", runtime);
      
         if (ImGui::SliderFloat("UI Transparency", &cb_luma_global_settings.GameSettings.UITransparency, 0.f, 1.f))
            reshade::set_config_value(runtime, NAME, "UITransparency", cb_luma_global_settings.GameSettings.UITransparency);
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Do some crazy backend RTV switcheroo to separate out UI.\nMay cost performance.");
         DrawResetButton(cb_luma_global_settings.GameSettings.UITransparency, default_luma_global_game_settings.UITransparency, "UITransparency", runtime);
         ShaderDefineInfo::Set(ShaderDefineInfo::CUSTOM_UITRANSPARENCY, cb_luma_global_settings.GameSettings.UITransparency < 1.f);

         // {"CUSTOM_TESTBGSPRITES", '0', true, false, "Test BG Sprites layering.", 2},
         {
            ShaderDefineInfo::UIDropDown(ShaderDefineInfo::CUSTOM_TESTBGSPRITES, "BG Sprites Test",
               { "Off", "BG Sprites Only", "3D Only" },
               "For testing Background Sprite layering.");
         }

         // {"CUSTOM_TESTSDR", '0', true, false, "Disable HDR shaders.", 1},
         {
            ShaderDefineInfo::UIToggleCheckmark(ShaderDefineInfo::CUSTOM_TESTSDR, "Test SDR (Kinda & Requires 203 Paper White)", "Disable modded HDR tonemap shaders to compare against vanilla SDR output.\nEverything else is enabled to fix stuff broken by HDR pipeline upgrades.");
         }

         //CUSTOM_UPGRADE_DEBUG
         {
            ShaderDefineInfo::UIDropDown(ShaderDefineInfo::CUSTOM_UPGRADE_DEBUG, "UpgradeToneMap() Inputs",
               { "Off", "Raw HDR", "Neutral SDR", "Graded SDR (Unclamped)" },
               "Toggle between various inputs used in RenoDX's UpgradeToneMap() algorithm to map HDR luminance onto SDR chrominance, extending color.");
         }

         //CUSTOM_TONEMAP_IDENTIFY
         ShaderDefineInfo::UIToggleCheckmark(ShaderDefineInfo::CUSTOM_TONEMAP_IDENTIFY, "Tonemap Variant Identify", "At the top of the screen, draw which tonemap variant is being used this frame using the newly given ID in binary representation.");
      }
      
      // ImGui::Separator(); ////////////////////////////////////////////////////////////////////////////////////
      if (ImGui::CollapsingHeader("(Debug) Info"))
      {
         const int ti = TonemapInfo::GetIndexOnlyIfDrawn(Globals::TonemapInfoBackup);
         
         std::string s = "Tonemap Uber Variant: " + std::to_string(ti);
         ImGui::BulletText(s.c_str());
         
         std::string s99 = "Tonemap Debug Info: " + (TonemapInfo::GetIndexOnlyIfDrawn(Globals::TonemapInfoBackup) >= 0 ? static_cast<std::string>(TonemapInfo::TonemapDebugInfo[ti]) : "N/A");
         ImGui::BulletText(s99.c_str());
         
         std::string s1 = "Drawn Final: " + std::to_string(TonemapInfo::GetDrawnFinal(Globals::TonemapInfoBackup));
         ImGui::BulletText(s1.c_str());
         
         std::string s6 = "Drawn Sprites HPBarDelta: " + std::to_string(TonemapInfo::GetDrawnHPBarDelta(Globals::TonemapInfoBackup));
         ImGui::BulletText(s6.c_str());
         
         // std::string s5 = "FMV Mode Detected: " + std::to_string(TonemapInfo::GetIsFMV(Globals::TonemapInfoBackup));
         // ImGui::BulletText(s5.c_str());

         // std::string s7 = "MLAA Detected: " + std::to_string(game_device_data.IsDrawnMLAAPrev);
         // ImGui::BulletText(s7.c_str());
         
         std::string s4 = "Swapchain Change Count: " + std::to_string(Globals::SwapchainChangeCount);
         ImGui::BulletText(s4.c_str());
         
         // std::string s2 = "Auto-Exposure Fix History Clears: " + std::to_string(AutoExposureFix::count_history_clear);
         // ImGui::BulletText(s2.c_str());
         
         std::string s7 = "Auto-Exposure Fix Is Allow Draw: " + std::to_string(AutoExposureFix::Update_IsDraw());
         ImGui::BulletText(s7.c_str());

         std::string s9 = "Auto-Exposure Fix Ring Buffer Index Override: " + std::to_string(AutoExposureFix::vp_curr_i);
         ImGui::BulletText(s9.c_str());
         
         // std::string s2 = "SK Mode: " + std::to_string(Globals::IsSKMode);
         // ImGui::BulletText(s2.c_str());

         // cb_luma_global_settings.GameSettings.TonemapperPeakCached
         std::string s3 = "Tonemapper Peak Cached: " + std::to_string(cb_luma_global_settings.GameSettings.TonemapperPeakCached);
         ImGui::BulletText(s3.c_str());
         
         // cb_luma_global_settings.GameSettings.TonemapperMaxExpectedCached
         std::string s8 = "Tonemapper Max Expected Cached: " + std::to_string(cb_luma_global_settings.GameSettings.TonemapperMaxExpectedCached);
         ImGui::BulletText(s8.c_str());
      }

      ImGui::Separator(); ////////////////////////////////////////////////////////////////////////////////////
      
      if (ImGui::Checkbox("Show Advanced Settings", &Globals::UIIsAdvanced))
         reshade::set_config_value(runtime, NAME, "UIIsAdvanced", Globals::UIIsAdvanced);
      
      if (ImGui::Checkbox("Hide README", &Globals::UIIsReadmeDone))
         reshade::set_config_value(runtime, NAME, "UIIsReadmeDone", Globals::UIIsReadmeDone);
      
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
      
      ImGui::Separator(); ////////////////////////////////////////////////////////////////////////////////////

      ImGui::Text("High FPS:");
      ImGui::BulletText("SpecialK (memory addresses)");
      ImGui::SameLine(); if (ImGui::Button("Open GitHub Link")) Website::OpenWebsite("https://github.com/SpecialKO/SpecialK/blob/6fe51ee1eca4aee26a59e227ee5402ad3b55fcc0/src/plugins/unclassified.cpp#L1264");
      ImGui::BulletText("Display Commander (limit replacement)");
      ImGui::SameLine(); if (ImGui::Button("Open GitHub Link")) Website::OpenWebsite("https://github.com/pmnoxx/display-commander");
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
      swapchain_upgrade_type         = SwapchainUpgradeType::scRGB;
      swapchain_format_upgrade_type  = TextureFormatUpgradesType::AllowedEnabled;

      // //Globals::IsSKMode (check for ReShade64.dll file next to exe)
      // {
      //    std::filesystem::path dll_path = std::filesystem::current_path() / "ReShade64.dll";
      //    Globals::IsSKMode = std::filesystem::exists(dll_path);
      //    if (Globals::IsSKMode) swapchain_format_upgrade_type = TextureFormatUpgradesType::None;
      // }

      //texture upgrade
      texture_format_upgrades_type   = TextureFormatUpgradesType::AllowedEnabled;
      //enable_indirect_texture_format_upgrades = true;
      //enable_automatic_indirect_texture_format_upgrades = true;
      texture_upgrade_formats = {
         reshade::api::format::r8g8b8a8_unorm
      };

      texture_format_upgrades_2d_size_filters = 0 | (uint32_t)TextureFormatUpgrades2DSizeFilters::CustomAspectRatio | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainAspectRatio;
      texture_format_upgrades_2d_custom_aspect_ratios = { 16.f / 9.f }; 
      texture_format_upgrades_2d_aspect_ratio_pixel_threshold = 32; //leeway

      game = new ProjectDivaMegaMix();
   }

   CoreMain(hModule, ul_reason_for_call, lpReserved);

   return TRUE;
}