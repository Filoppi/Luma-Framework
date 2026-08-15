#define GAME_CALL_OF_DUTY_BLACK_OPS_III 1

#if CUSTOM_FAST == 0
   #define ENABLE_NGX 1
   #define ENABLE_FIDELITY_SK 0 //TODO: still dont know why FSR complete ruin main color Resource on draw...
#endif

#define DISABLE_AUTO_DEBUGGER  1

// if DLSS, do DLSS right before final shader.
#define CUSTOM_DLSS_KMS_1

#include "..\..\Core\core.hpp"

namespace Globals
{
   static auto SDR8Bit = false;
   static bool UIIsAdvanced = false;
#if ENABLE_SR == 1
   static bool IsUi = true;
   static bool IsFullscreenBlur = true;
   static float SRPreExposure = 0.f;
   static bool SRAutoExposure = true;
   // static bool SRSetMipLodBias = true;
   // static float SRSetMipLodBiasManual = 0;
   // static bool SRIgnoreSamplerUpgrade = false;
#endif

#if DEVELOPMENT
   static uint CountSwapchainChange = 0;
   static uint CountSRQueryInterface = 0;
   static uint CountSRTexChange = 0;
   static uint CountSRJitterContinued = 0;
   static uint CountSRJitterErrorRepeat = 0;
   static bool IsSkipOnDrawOrDispatch = false;
   static bool IsSkipDLSSDraw = false;
   static bool IsSkipDLSSFinalLinearize = false;
#endif

   static float InverseLerp(float a, float b, float v)
   {
      if (a == b) return 0.f;
      return (v - a) / (b - a);
   }

}

// void ShaderHashesLists_Setup() //TODO: del
// {
//    ShaderHashesLists::ProbeCulling.compute_shaders.emplace(std::stoul("0x6759EF9E", nullptr, 16)); //Light
//    ShaderHashesLists::ProbeCulling.compute_shaders.emplace(std::stoul("0xDA63105C", nullptr, 16)); //Reflection
//    
//    ShaderHashesLists::MotionVectors.pixel_shaders.emplace(std::stoul("0xB8A51615", nullptr, 16));
//    
//    ShaderHashesLists::GTAOUltra0.pixel_shaders.emplace(std::stoul("0x8FC85155", nullptr, 16));
//    
//    ShaderHashesLists::LensFlare.pixel_shaders.emplace(std::stoul("0x7740A983", nullptr, 16));
//    
//    ShaderHashesLists::Tonemap.pixel_shaders.emplace(std::stoul("0x59F328E3", nullptr, 16)); //game
//    ShaderHashesLists::Tonemap.pixel_shaders.emplace(std::stoul("0x1744B1D4", nullptr, 16)); //menu (CA)
//    
//    ShaderHashesLists::SMAAT2X.pixel_shaders.emplace(std::stoul("0xD9288CF8", nullptr, 16)); //final SMAA T2X (NOT FILMIC) resolve
//    //ShaderHashesLists::SMAAT2X.pixel_shaders.emplace(std::stoul("0x15FF4E6D", nullptr, 16)); // T2XF
//
//    // ShaderHashesLists::AAStart.compute_shaders.emplace(std::stoul("0x6312E037", nullptr, 16)); // FXAA Edge
//    // ShaderHashesLists::AAStart.compute_shaders.emplace(std::stoul("0x6240554C", nullptr, 16)); // SMAA Edge
//
//    ShaderHashesLists::SMAAT2XPrep.compute_shaders.emplace(std::stoul("0x6240554C", nullptr, 16)); // SMAA Edge
//    ShaderHashesLists::SMAAT2XPrep.compute_shaders.emplace(std::stoul("0xB037D915", nullptr, 16)); // SMAA Prep Idk
//    ShaderHashesLists::SMAAT2XPrep.compute_shaders.emplace(std::stoul("0x3B3C41EF", nullptr, 16)); // SMAA Resolve V
//    ShaderHashesLists::SMAAT2XPrep.compute_shaders.emplace(std::stoul("0xCDEFC09A", nullptr, 16)); // SMAA Resolve H
//    
//    ShaderHashesLists::SMAAResolveH.compute_shaders.emplace(std::stoul("0xCDEFC09A", nullptr, 16)); // SMAA Resolve H
//    
//    ShaderHashesLists::Final.pixel_shaders.emplace(std::stoul("0x3D461B1A", nullptr, 16)); //game
//    ShaderHashesLists::Final.pixel_shaders.emplace(std::stoul("0x224A8BF5", nullptr, 16)); //menu (noise, dither)
//    
//    ShaderHashesLists::FullscreenBlurLast.pixel_shaders.emplace(std::stoul("0xDA908072", nullptr, 16)); //last fullscreen blur that upsamples from lower res
//    
//    ShaderHashesLists::Rec709.pixel_shaders.emplace(std::stoul("0x8324B585", nullptr, 16)); //right-before-swapchain trash rec709, where rtv is 8bit texture
//
//    ShaderHashesLists::Exposure.compute_shaders.emplace(std::stoul("0x14F64B50", nullptr, 16)); //(exposure tex 9x1 UAV 1)
//
//    ShaderHashesLists::VolumetricFogDraw.pixel_shaders.emplace(std::stoul("0x48AE98F9", nullptr, 16)); //opposed to composite (OIT SRV 5)
//    ShaderHashesLists::VolumetricFogDraw.pixel_shaders.emplace(std::stoul("0xDFC82E40", nullptr, 16));
//    
//    ShaderHashesLists::Sprites.compute_shaders.emplace(std::stoul("0xCAF61E7A", nullptr, 16)); //flame, smoke, etc. particles (OIT UAV 4)
// }

namespace ShaderDefineInfo
{
   constexpr uint32_t SWAPCHAIN_CLAMP_PEAK             = char_ptr_crc32("SWAPCHAIN_CLAMP_PEAK");
   constexpr uint32_t SWAPCHAIN_TEST_USER_PEAK         = char_ptr_crc32("SWAPCHAIN_TEST_USER_PEAK");
   constexpr uint32_t CUSTOM_TONEMAP                   = char_ptr_crc32("CUSTOM_TONEMAP");
   constexpr uint32_t CUSTOM_TONEMAP_SCALING           = char_ptr_crc32("CUSTOM_TONEMAP_SCALING");
   constexpr uint32_t CUSTOM_TONEMAP_CLAMP             = char_ptr_crc32("CUSTOM_TONEMAP_CLAMP");
   constexpr uint32_t CUSTOM_FINAL_CLAMP               = char_ptr_crc32("CUSTOM_FINAL_CLAMP");
   constexpr uint32_t CUSTOM_HDTVREC709                = char_ptr_crc32("CUSTOM_HDTVREC709");
   constexpr uint32_t CUSTOM_RCAS                      = char_ptr_crc32("CUSTOM_RCAS");
   constexpr uint32_t CUSTOM_LUTBUILDER_COLORSPACE     = char_ptr_crc32("CUSTOM_LUTBUILDER_COLORSPACE");
   constexpr uint32_t CUSTOM_LUTBUILDER_VANILLA        = char_ptr_crc32("CUSTOM_LUTBUILDER_VANILLA");
   constexpr uint32_t CUSTOM_LUTBUILDER_SATBOOST       = char_ptr_crc32("CUSTOM_LUTBUILDER_SATBOOST");
   constexpr uint32_t CUSTOM_LUTBUILDER_NEUTRAL        = char_ptr_crc32("CUSTOM_LUTBUILDER_NEUTRAL");
   constexpr uint32_t CUSTOM_LUTBUILDER_NEUTRAL_LUMA   = char_ptr_crc32("CUSTOM_LUTBUILDER_NEUTRAL_LUMA");
   constexpr uint32_t CUSTOM_PCC                       = char_ptr_crc32("CUSTOM_PCC");
   constexpr uint32_t CUSTOM_PCC_ROLLOFF               = char_ptr_crc32("CUSTOM_PCC_ROLLOFF");
   constexpr uint32_t CUSTOM_COLORGRADE                = char_ptr_crc32("CUSTOM_COLORGRADE");
   constexpr uint32_t CUSTOM_UPSCALE_MOV               = char_ptr_crc32("CUSTOM_UPSCALE_MOV");
   constexpr uint32_t CUSTOM_SR                        = char_ptr_crc32("CUSTOM_SR");
   constexpr uint32_t CUSTOM_SDR                       = char_ptr_crc32("CUSTOM_SDR");
   constexpr uint32_t CUSTOM_MB_QUALITY                = char_ptr_crc32("CUSTOM_MB_QUALITY");
   constexpr uint32_t CUSTOM_CHROMABER                 = char_ptr_crc32("CUSTOM_CHROMABER");
   constexpr uint32_t CUSTOM_UCS_TYPE                  = char_ptr_crc32("CUSTOM_UCS_TYPE");
   constexpr uint32_t CUSTOM_BLOOM_TONEMAP             = char_ptr_crc32("CUSTOM_BLOOM_TONEMAP");
   constexpr uint32_t CUSTOM_GAMMA_CORRECTION_MODE     = char_ptr_crc32("CUSTOM_GAMMA_CORRECTION_MODE");
   constexpr uint32_t CUSTOM_BLACKFLOOR_LUT            = char_ptr_crc32("CUSTOM_BLACKFLOOR_LUT");
   constexpr uint32_t CUSTOM_PERCHANNELLUMAEMULATE     = char_ptr_crc32("CUSTOM_PERCHANNELLUMAEMULATE");
   
   static char InvertCharBool(char b)
   {
      return b == '0' ? '1' : '0'; 
   }
   
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
      ImGui::PushID(std::string(label).append("_").append(std::to_string(d)).c_str());
      bool c = ImGui::Combo(label, &def, items.data(), static_cast<int>(items.size()));
      ImGui::PopID();
      if (c) Set(d, def);
      if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip(tooltip);
      UIResetButton(d);
      return def;
   }
}

#if ENABLE_SR == 1
namespace MemoryHack
{
   enum ExeType
   {
      Unknown,
      BO3Enhanced,
      Steam_Feb2026_21201493,
      Steam_2023_BOIII,
   };
   static ExeType exe_type = Unknown;

   static const char* GetExeTypeName(ExeType type)
   {
      switch (type)
      {
         case BO3Enhanced: return "BO3Enhanced";
         case Steam_Feb2026_21201493: return "Steam, Feb. 2026, BuildID 21201493";
         case Steam_2023_BOIII: return "Steam, 2023, used by BOIII";
         default: return "Unknown";
      }
   }

   static uintptr_t base; //BlackOps3.exe
   
   static void OnInit()
   {
      //reset
      exe_type = Unknown;

      //base
      base = reinterpret_cast<uintptr_t>(GetModuleHandleA("BlackOps3.exe"));
      if (!base) return;
      
      //BO3Enhanced
      if (exe_type == Unknown)
      {
         //log
         message(reshade::log::level::info, std::format("MemoryHack::OnInit: Checking for ({})", GetExeTypeName(BO3Enhanced)).c_str());

         //expectations
         constexpr uintptr_t offset = 0x2cfe874;
         auto pattern = std::vector<uint8_t>{0x48, 0x83, 0xec, 0x28, 0xe8, 0x53, 0x06, 0x00, 0x00, 0x48, 0x83, 0xc4, 0x28, 0xe9, 0x7a, 0xfe, 0xff, 0xff};
         
         //match
         for (uintptr_t i = 0; i < pattern.size(); i++)
         {
            auto a = std::bit_cast<uint8_t*>(base + offset + i);
            if (!a || *a != pattern[i]) goto AfterBO3Enhanced;
         }

         //success
         exe_type = BO3Enhanced;
      }
      AfterBO3Enhanced:;
      
      //Steam_Feb2026_21201493
      if (exe_type == Unknown)
      {
         //log
         message(reshade::log::level::info, std::format("MemoryHack::OnInit: Checking for ({})", GetExeTypeName(Steam_Feb2026_21201493)).c_str());
         
         //expectations
         constexpr uintptr_t offset = 0x1CDE2FC;
         auto pattern = std::vector<uint8_t>{ //aka the pattern of DLSS Jitter1 Function
             0xc7, 0x44, 0x24, 0x20, 0x00, 0x00, 0x80, 0xbe,
             0xc7, 0x44, 0x24, 0x24, 0x00, 0x00, 0x80, 0x3e,
             0x83, 0xe2, 0x01,
             0xc7, 0x44, 0x24, 0x28, 0x00, 0x00, 0x80, 0x3e,
             0xc7, 0x44, 0x24, 0x2c, 0x00, 0x00, 0x80, 0xbe
         };
         
         //match
         for (uintptr_t i = 0; i < pattern.size(); i++)
         {
            auto a = std::bit_cast<uint8_t*>(base + offset + i);
            if (!a || *a != pattern[i]) goto AfterSteamFeb21201493;
         }

         //success
         exe_type = Steam_Feb2026_21201493;
      }
      AfterSteamFeb21201493:;

      //Steam_2023_BOIII
      if (exe_type == Unknown)
      {
         //log
         message(reshade::log::level::info, std::format("MemoryHack::OnInit: Checking for ({})", GetExeTypeName(Steam_2023_BOIII)).c_str());
         
         //expectations
         constexpr uintptr_t offset = 0x1CEA5F8;
         auto pattern = std::vector<uint8_t>{ //aka the pattern of DLSS Jitter0 Function
            0xc7, 0x44, 0x24, 0x20, 0x00, 0x00, 0x80, 0xbe,
            0xc7, 0x44, 0x24, 0x24, 0x00, 0x00, 0x80, 0x3e,
            0xc7, 0x44, 0x24, 0x28, 0x00, 0x00, 0x80, 0x3e,
            0xc7, 0x44, 0x24, 0x2c, 0x00, 0x00, 0x80, 0xbe
         };

         //match
         for (uintptr_t i = 0; i < pattern.size(); i++)
         {
            auto a = std::bit_cast<uint8_t*>(base + offset + i);
            if (!a || *a != pattern[i]) goto AfterSteam2023BOIII;
         }

         //success
         exe_type = Steam_2023_BOIII;
      }
      AfterSteam2023BOIII:;
      
      //log 
      message(reshade::log::level::info, std::format("MemoryHack::OnInit: Done. ({})", GetExeTypeName(exe_type)).c_str());
   }
}
#endif

struct CallOfDutyBlackOps3GameDeviceData final : public GameDeviceData
{
   static CallOfDutyBlackOps3GameDeviceData& GetGameDeviceData(DeviceData& device_data)
   {
      return *static_cast<CallOfDutyBlackOps3GameDeviceData*>(device_data.game);
   }
   
#if ENABLE_SR == 1   
   //jitter
   float2 jitter = float2(0);
   float2 jitter_prev = float2(0);

   //For organization, not all is guaranteed.
   struct Resources
   {
      D3D11_TEXTURE2D_DESC desc;
      ComPtr<ID3D11Texture2D> tex;
      ComPtr<ID3D11Resource> res;
      ComPtr<ID3D11ShaderResourceView> srv;
      ComPtr<ID3D11RenderTargetView> rtv;
      ComPtr<ID3D11UnorderedAccessView> uav;
   };

   //nullptr everything
   static void ResetResources(Resources& resources)
   {
      resources.desc = {};
      resources.res.reset();
      resources.tex.reset();
      resources.srv.reset();
      resources.rtv.reset();
      resources.uav.reset();
   }
   
   //The default desc for all FullScreenFX buffers, found by RenderDoc.
   static D3D11_TEXTURE2D_DESC GetDefaultDesc(uint2 output_resolution)
   {
      D3D11_TEXTURE2D_DESC desc = {};
      desc.Width = output_resolution.x;
      desc.Height = output_resolution.y;
      desc.MipLevels = 1;
      desc.ArraySize = 1;
      desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET | D3D11_BIND_UNORDERED_ACCESS;
      desc.Usage = D3D11_USAGE_DEFAULT;
      desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
      desc.SampleDesc = {1, 0};
      desc.CPUAccessFlags = 0;
      desc.MiscFlags = 0;
      return desc;
   }

   //Create desc, tex, srv, rtv
   static void SetupResource(ID3D11Device* device, Resources &resources)
   {
      auto hr0 = device->CreateTexture2D(&resources.desc, nullptr, resources.tex.put());
         #if DEVELOPMENT
            ASSERT_ONCE(SUCCEEDED(hr0));
         #endif

      auto hr3 = resources.tex->QueryInterface(resources.res.put());
         #if DEVELOPMENT
            ASSERT_ONCE(SUCCEEDED(hr3));
         #endif
      
      auto hr1 = device->CreateRenderTargetView(resources.tex.get(), nullptr, resources.rtv.put());
         #if DEVELOPMENT
            ASSERT_ONCE(SUCCEEDED(hr1));
         #endif
      
      auto hr2 = device->CreateShaderResourceView(resources.tex.get(), nullptr, resources.srv.put());
         #if DEVELOPMENT
            ASSERT_ONCE(SUCCEEDED(hr2));
         #endif
   }

   // TODO: use device_data.ManagedResources
   Resources resources_sr_output; //there is device_date.sr_output_color for use, but screw that.
   Resources resources_smaa_color_input;
   Resources resources_tonemap_color_input;
   Resources resources_velocity;
   Resources resources_velocity_linearize;
   Resources resources_depth;
   // Resources resources_oit;
   // Resources resources_exposure;
   Resources resources_dlss_linearize;
   // Resources resources_dlss_exposure;

   //sr input structs/datas
   SR::SettingsData sr_settings_data = {};
   SR::SuperResolutionImpl::DrawData sr_draw_data = {};
   
   //draw/pipeline progress
   bool drawn_probecull = false;
   bool drawn_motionvectors = false;
   bool drawn_ao0 = false;
   bool drawn_ao1 = false;
   bool drawn_csdeferredresolve = false;
   bool drawn_sprites = false;
   bool drawn_volumetricfogdraw = false;
   bool drawn_expousure = false;
   bool drawn_tonemap = false;
   bool drawn_tonemap_prev = false;
   bool drawn_smaat2xprep = false; //not filmic
   bool drawn_smaat2x = false; //not filmic
   bool drawn_smaat2x_prev = false; //prev frame
   bool drawn_final = false;
   bool drawn_hdtv = false;
   bool drawn_hdtv_prev = false; //prev frame

   //res
   bool IsDLAA()
   {
      // return static_cast<uint>(cb_luma_global_settings.SwapchainSize.x) == smaa_colorinput_resources.desc.Width &&
      //        static_cast<uint>(cb_luma_global_settings.SwapchainSize.y) == smaa_colorinput_resources.desc.Height;
      return /*smaa_colorinput_resources.desc.Width  == sr_output_resources.desc.Width &&*/
             resources_smaa_color_input.desc.Height == resources_sr_output.desc.Height;
   }
   
   void Reset(bool isSRReset)
   {
      drawn_probecull = false;
      drawn_motionvectors = false;
      drawn_ao0 = false;
      drawn_ao1 = false;
      drawn_csdeferredresolve = false;
      drawn_sprites = false;
      drawn_volumetricfogdraw = false;
      drawn_expousure = false;
      drawn_tonemap_prev = drawn_tonemap;
      drawn_tonemap = false;
      drawn_smaat2xprep = false;
      drawn_smaat2x_prev = drawn_smaat2x;
      drawn_smaat2x = false;
      drawn_final = false;
      drawn_hdtv_prev = drawn_hdtv;
      drawn_hdtv = false;
      
      jitter_prev = jitter;
      jitter = float2(0);

      if (isSRReset)
      {
         // Globals::SRSuccessCount = 0;
         // resolution_height_render_vs_output = uint2(-1, -1);
      }
   }
   
   static bool IsValidJitter(float2 jitter) {return jitter != float2(0);}
   static bool IsValidJitter(float4 jitter) {return IsValidJitter(float2(jitter.x, jitter.y));}

   void HardResetSR()
   {
      ResetResources(resources_sr_output); //null this and everything else will be recreated.
   }

   void ClearAllResourcesSR()
   {
      ResetResources(resources_sr_output);
      ResetResources(resources_smaa_color_input);
      ResetResources(resources_tonemap_color_input);
      ResetResources(resources_velocity);
      ResetResources(resources_velocity_linearize);
      ResetResources(resources_depth);
      ResetResources(resources_dlss_linearize);
   }
#endif
};

namespace BlackFloorSDRTonemap
{
   //between 0.88765 and 1.0
   constexpr float low = 0.88765;
   constexpr float high = 1.f;
   
   static float ToReal(float ratio)
   {
      ratio = std::clamp(ratio, 0.f, 1.f);
      ratio = std::lerp(high, low, ratio);
      return ratio;
   }

   static float ToRatio(float real)
   {
      real = Globals::InverseLerp(high, low, real);
      real = std::clamp(real, 0.f, 1.f);
      real = abs(real);
      return real;
   }
}

namespace Website
{
   static void OpenWebsite(const char* url) {
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

#if ENABLE_SR == 1
namespace DLSSJitter
{
   static bool enabled = true;
   
   constexpr float4 expected_jitter = float4(-0.25f, 0.25f, 0.25f, -0.25f);
   static float2 jitter = float2(0);
   static float2 jitter_prev = float2(0);
   // static float jitter_multiplier = 1.f;
   
   static int phases_sel = 0; //ui selection
   static int phases = 0; //effective
   static uint frame = 0;
   
   static bool flip_condition = false;
   constexpr static bool is_flip_use = true; //either use newly set OnPresent(), or use prev

   static bool test_sync = false;
   
   //addresses
   struct JitterAddress
   {
      float* a0x;
      float* a0y;
      float* a1x;
      float* a1y;

      void Clear()
      {
         a0x = nullptr;
         a0y = nullptr;
         a1x = nullptr;
         a1y = nullptr;
      }

      bool IsValid() const
      {
         return a0x;
      }

      bool IsExpected() const
      {
         if (!IsValid()) return false;
         return *a0x == expected_jitter.x && *a0y == expected_jitter.y &&
                *a1x == expected_jitter.z && *a1y == expected_jitter.w;
      }

      void Set0(float2 jitter0) const
      {
         *a0x = jitter0.x;
         *a0y = jitter0.y;
      }

      void Set1(float2 jitter1) const
      {
         *a1x = jitter1.x;
         *a1y = jitter1.y;
      }

      void SetBoth(float2 jitter0, float2 jitter1) const
      {
         *a0x = jitter0.x;
         *a0y = jitter0.y;
         *a1x = jitter1.x;
         *a1y = jitter1.y;
      }

      void SetExpected() const
      {
         *a0x = expected_jitter.x;
         *a0y = expected_jitter.y;
         *a1x = expected_jitter.z;
         *a1y = expected_jitter.w;
      }
   };
   static JitterAddress addr0;
   static JitterAddress addr1;

   constexpr byte is_sync_needed_max = 4;
   static byte is_sync_needed = is_sync_needed_max;
   static void SetSyncNeeded() { is_sync_needed = is_sync_needed_max; }

   //if true, you can set jitter OnPresent
   static bool IsReady()
   {
      return enabled && addr0.IsValid();
   }
   
   static void OnInit(MemoryHack::ExeType exe_type)
   {
      message(reshade::log::level::info, std::format("DLSSJitter::OnInit: Initializing for executable type: ({})", MemoryHack::GetExeTypeName(exe_type)).c_str());
      DWORD old_protect;
      bool is_found0 = false;
      bool is_found1 = false;
      switch (exe_type)
      {
         case MemoryHack::BO3Enhanced:
            //replace binary hardcoded
            addr0.a0x = std::bit_cast<float*>(MemoryHack::base + 0x2F896C0);
            addr0.a0y = addr0.a0x + 1;
            addr0.a1x = addr0.a0x + 2;
            addr0.a1y = addr0.a0x + 3;
            VirtualProtect(addr0.a0x, 16, PAGE_READWRITE, &old_protect);
            is_found0 = true;
            break;
         case MemoryHack::Steam_Feb2026_21201493:
            //Jitter1 Function (Dynamic Objects)
            /*
                +1CDE2FC     c7 44 24        MOV        dword ptr [RSP + 0x20],0xbe800000
                             20 (00 00 
                             80 be)
                +1CDE304     c7 44 24        MOV        dword ptr [RSP + 0x24],0x3e800000
                             24 (00 00 
                             80 3e)
                +1CDE30C     83 e2 01        AND        EDX,0x1
                +1CDE30C     c7 44 24        MOV        dword ptr [RSP + 0x28],0x3e800000
                             28 (00 00 
                             80 3e)
                +1CDE317     c7 44 24        MOV        dword ptr [RSP + 0x2c],0xbe800000
                             2c (00 00
                             80 be)
            */
            addr0.a0x = std::bit_cast<float*>(MemoryHack::base + 0x1CDE2FC + 4);
            addr0.a0y = std::bit_cast<float*>(MemoryHack::base + 0x1CDE304 + 4);
            //0x1CDE30C AND
            addr0.a1x = std::bit_cast<float*>(MemoryHack::base + 0x1CDE30F + 4);
            addr0.a1y = std::bit_cast<float*>(MemoryHack::base + 0x1CDE317 + 4);
            VirtualProtect(addr0.a0x, 0x1F, PAGE_EXECUTE_READWRITE, &old_protect);
            //Jitter0 Function (Static Objects)
            /*
               base: 7FF6902E0000
               7ff691fbe228 c7 44 24        MOV        dword ptr [RSP + 0x20],0xbe800000
                            20 00 00 
                            80 be
               7ff691fbe230 c7 44 24        MOV        dword ptr [RSP + 0x24],0x3e800000
                            24 00 00 
                            80 3e
               7ff691fbe238 c7 44 24        MOV        dword ptr [RSP + 0x28],0x3e800000
                            28 00 00 
                            80 3e
               7ff691fbe240 c7 44 24        MOV        dword ptr [RSP + 0x2c],0xbe800000
                            2c 00 00 
                            80 be
             */
            addr1.a0x = std::bit_cast<float*>(MemoryHack::base + 0x1CDE228 + 4);
            addr1.a0y = std::bit_cast<float*>(MemoryHack::base + 0x1CDE230 + 4);
            addr1.a1x = std::bit_cast<float*>(MemoryHack::base + 0x1CDE238 + 4);
            addr1.a1y = std::bit_cast<float*>(MemoryHack::base + 0x1CDE240 + 4);
            VirtualProtect(addr1.a0x, 0x1C, PAGE_EXECUTE_READWRITE, &old_protect);
            is_found0 = is_found1 = true;
            break;
         case MemoryHack::Steam_2023_BOIII:
            addr0.a0x = std::bit_cast<float*>(MemoryHack::base + 0x1CEA6CC + 4);
            addr0.a0y = std::bit_cast<float*>(MemoryHack::base + 0x1CEA6D4 + 4);
            //0x1CEA6DC AND 
            addr0.a1x = std::bit_cast<float*>(MemoryHack::base + 0x1CEA6DF + 4);
            addr0.a1y = std::bit_cast<float*>(MemoryHack::base + 0x1CEA6E7 + 4);
            VirtualProtect(addr0.a0x, 0x1F, PAGE_EXECUTE_READWRITE, &old_protect);
            addr1.a0x = std::bit_cast<float*>(MemoryHack::base + 0x1CEA5F8 + 4);
            addr1.a0y = std::bit_cast<float*>(MemoryHack::base + 0x1CEA600 + 4);
            addr1.a1x = std::bit_cast<float*>(MemoryHack::base + 0x1CEA608 + 4);
            addr1.a1y = std::bit_cast<float*>(MemoryHack::base + 0x1CEA610 + 4);
            VirtualProtect(addr1.a0x, 0x1C, PAGE_EXECUTE_READWRITE, &old_protect);
            is_found0 = is_found1 = true;
            break;
         default:
            //TODO scan for either harcoded or the 2 functions
            break;
      }

      //check if default?
      if (is_found0)
      {
         bool s = addr0.IsExpected();
         if (!s)
         {
            ASSERT_MSG(s, "DLSSJitter::OnInit: Detected usable executable to hack, but jitter array 0 are not as expected!");
            addr0.Clear();
            addr1.Clear();
         }
      }
      if (is_found1)
      {
         bool s = addr1.IsExpected();
         if (!s)
         {
            ASSERT_MSG(s, "DLSSJitter::OnInit: Detected usable executable to hack, but jitter array 1 are not as expected!");
            addr0.Clear();
            addr1.Clear();
         }
      }
      
      message(reshade::log::level::info, "DLSSJitter::OnInit: Initializing done!");
   }

   static bool SetJitter(float2 jitter, uint f)
   {
      if (DEVELOPMENT) ASSERT_MSG(addr0.IsValid(), "SetJitter(): addr_0x is null!");
      if (f % 2 == 0)
      {
         addr0.Set0(jitter);
         if (addr1.IsValid()) addr1.Set0(jitter);
      }
      else
      {
         addr0.Set1(jitter);
         if (addr1.IsValid()) addr1.Set1(jitter);
      }
      return true;
   }
   
   static bool SetJitterReset()
   {
      if (DEVELOPMENT) ASSERT_MSG(addr0.IsValid(), "SetJitterReset(): addr_0x is null!");
      addr0.SetExpected();
      if (addr1.IsValid()) addr1.SetExpected();
      return true;
   }

   //Update loop
   static void OnPresent(DeviceData& device_data, CallOfDutyBlackOps3GameDeviceData& game_device_data)
   {
      //gatekeep
      if (!addr0.IsValid()) return;
      
      //case: valid, so replace next
      if (enabled && sr_user_type != SR::UserType::None && game_device_data.drawn_smaat2x)
      {
         //++
         frame++;

         //phases
         if (phases_sel == 0 && device_data.sr_type != SR::Type::None)
         {
            phases = SR::GetDefaultJitterPhases();
            auto* sr_instance_data = device_data.GetSRInstanceData();
            phases = sr_implementations[device_data.sr_type]->GetJitterPhases(sr_instance_data);
         }
         else phases = phases_sel;
         
         //new jitters
         const uint temporal_frame = cb_luma_global_settings.FrameIndex % phases;
         jitter_prev = jitter; //save prev
         jitter = float2(SR::HaltonSequence(temporal_frame, 2), SR::HaltonSequence(temporal_frame, 3)); //new

         //test_sync 4x
         if (test_sync)
         {
            jitter.x *= 4.f;
            jitter.y *= 4.f;
         }
         
         //set
         SetJitter(jitter, frame);
      }
      //case: reset
      else
      {
         SetJitterReset();
      }
   }

   static void OnUI(reshade::api::effect_runtime* runtime, DeviceData& device_data, CallOfDutyBlackOps3GameDeviceData& game_device_data)
   {
      // gatekeep: Unknown
      if (!addr0.IsValid())
      {
         // red
         ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 100, 100, 255));
         ImGui::BulletText("Unsupported version of the game detected.");
         ImGui::PopStyleColor();
      }
      else
      {
         // checkmark enabled
         ImGui::PushID("DLSSJitterEnabled");
         if (ImGui::Checkbox("Enable", &enabled))
         {
            SetSyncNeeded();
            reshade::set_config_value(runtime, NAME, "DLSSJitterEnabled", enabled);
         }
      
         // status
         if (IsReady())
         {
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(100, 255, 100, 255));
            ImGui::BulletText("ACTIVE AND PATCHING! (%.5f, %.5f)", jitter.x, jitter.y, frame % 2 != 0);
            ImGui::PopStyleColor();
         }

         if (Globals::UIIsAdvanced)
         {
            ImGui::NewLine(); ///////////////////////
      
            // DLSSJitter::jitter_sel
            if (ImGui::SliderInt("Jitter Phases (Debug)", &phases_sel, 0, 32)) // TODO save
            {
               device_data.force_reset_sr = true; // soft reset for new jitters
               // CallOfDutyBlackOps3GameDeviceData::HardResetSR(device_data, game_device_data);
            }
            if (ImGui::IsItemHovered())
               ImGui::SetTooltip("Set the amount of offsets before repeating.\n\nHighest isn't automatically best,\nsince it'll takes more time to loop.");
            ImGui::BulletText("Active: %d", enabled ? phases : -1);
         }
      }
      
      if (Globals::UIIsAdvanced)
      {
         ImGui::NewLine(); ///////////////////////
         
         if (ImGui::Button("Fix: Force Resync")) SetSyncNeeded();
         if (ImGui::IsItemHovered()) ImGui::SetTooltip("(SHOULDN'T BE NEEDED! Please report if otherwise.)\nForce a rescan to see which of the 2 position is used next.\n\nExplanation: There are 2 jitter offset;\nThe one used or will be used.\nWe can only override the one that will be used.");
         
         if (is_sync_needed == 0)
         {
            if (ImGui::Button("Fix: Force Index Flip/Change"))
               frame++;
            if (ImGui::IsItemHovered())
               ImGui::SetTooltip("(SHOULDN'T BE NEEDED! Please report if otherwise.)\nIf Test Sync causes an \"earthquake\", this should fix it.\n\nExplanation: There are 2 jitter offset;\nThe one used or will be used.\nWe can only override the one that will be used.");
         }
         ImGui::Checkbox("Test Sync", &test_sync);
         if (ImGui::IsItemHovered()) ImGui::SetTooltip("(SHOULDN'T BE NEEDED! Please report if otherwise.)\nScales jitter 4x!\nIf desynced, it'll look like an earthquake.");

         ImGui::BulletText("Sync Tokens: %d", is_sync_needed);
         ImGui::BulletText("Writing to index: %s", (frame % 2 == 0) ? "0" : "1");
      }
      
      // ImGui::NewLine();
      // ImGui::Checkbox("is_flip_use", &is_flip_use);
   }

   static void OnUIForced() {
      //when ImGUI open, force resync
      if (!DEVELOPMENT) SetSyncNeeded();
   }
   
   static void OnDrawCorrectDesync(CallOfDutyBlackOps3GameDeviceData& game_device_data)
   {
      flip_condition = std::abs(game_device_data.jitter.x - jitter.x) < 0.001f && std::abs(-game_device_data.jitter.y - jitter.y) < 0.001f;
      if (IsReady() && flip_condition) frame--;
   }
   
   static void OnLoadSettings(reshade::api::effect_runtime* runtime)
   {
      if (MemoryHack::exe_type == MemoryHack::Unknown)
      {
         enabled = false;
         return;
      }
      
      reshade::get_config_value(runtime, NAME, "DLSSJitterEnabled", enabled);
   }
}
#endif

#if ENABLE_SR == 1
namespace ForcedLODBias //TODO: other executables besides BO3Enhanced
{
   //user
   static float r_lodBiasRigid         = 0.f;
   static float r_modelLodBias         = 1.f;
   static int   r_lightingShadowFilter = -1;
   
   //BO3Enhanced r_lodBiasRigid
   constexpr static uint8_t lodBiasRigid_movups_orig[4] = {0x0F, 0x11, 0x57, 0x10};
   constexpr static uint8_t lodBiasRigid_movups_noop[4] = {0x90, 0x90, 0x90, 0x90};
   constexpr uint lodBiasRigid_offset_movups = 0x1DDFB5B;
   constexpr uint lodBiasRigid_offset_value = 0xA333EF4;
   uint8_t* lodBiasRigid_ptr_mov;
   float* lodBiasRigid_ptr_value;

   //BO3Enhanced r_modellodbias
   constexpr uint modelLodBias_offset_value0 = 0x19A3B138;
   constexpr uint modelLodBias_offset_value1 = 0x19A3B158;
   float* modelLodBias_ptr_value0;
   float* modelLodBias_ptr_value1;
    
#if DEVELOPMENT
   //BO3Enhanced r_lightingShadowFilter
   //41 89 85 a4 03 00 00
   constexpr uint lightingShadowFilter_offset_mov = 0x1DB9854;
   constexpr uint8_t lightingShadowFilter_mov_orig[7] = {0x41, 0x89, 0x85, 0xA4, 0x03, 0x00, 0x00};
   constexpr uint8_t lightingShadowFilter_mov_noop[7] = {0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90};
   constexpr uint lightingShadowFilter_offset_value0 = 0xA5047B4;
   constexpr uint lightingShadowFilter_offset_value1 = 0xA505954;
   constexpr uint lightingShadowFilter_offset_value2 = 0xA506AF4;
   constexpr uint lightingShadowFilter_offset_value3 = 0xA507C94;
   uint8_t* lightingShadowFilter_ptr_mov;
   uint* lightingShadowFilter_ptr_value0;
   uint* lightingShadowFilter_ptr_value1;
   uint* lightingShadowFilter_ptr_value2;
   uint* lightingShadowFilter_ptr_value3;
#endif
   
   static bool IsActive()
   {
      return MemoryHack::exe_type == MemoryHack::BO3Enhanced &&
         (
            r_lodBiasRigid != 0 ||
            r_modelLodBias != 1 ||
            r_lightingShadowFilter != -1
         );
   }

   static void OnInit(MemoryHack::ExeType exe_type)
   {
      message(reshade::log::level::info, std::format("ForcedLODBias::OnInit: Initializing for executable type: ({})", MemoryHack::GetExeTypeName(exe_type)).c_str());
      switch (exe_type)
      {
         case MemoryHack::BO3Enhanced:
            DWORD old_protect;

            lodBiasRigid_ptr_mov = std::bit_cast<uint8_t*>(MemoryHack::base + lodBiasRigid_offset_movups);
               VirtualProtect(lodBiasRigid_ptr_mov, 4, PAGE_EXECUTE_READWRITE, &old_protect);
               FlushInstructionCache(GetCurrentProcess(), lodBiasRigid_ptr_mov, 4);
            lodBiasRigid_ptr_value = std::bit_cast<float*>(MemoryHack::base + lodBiasRigid_offset_value);

            modelLodBias_ptr_value0 = std::bit_cast<float*>(MemoryHack::base + modelLodBias_offset_value0);
            modelLodBias_ptr_value1 = std::bit_cast<float*>(MemoryHack::base + modelLodBias_offset_value1);
#if DEVELOPMENT
            lightingShadowFilter_ptr_mov = std::bit_cast<uint8_t*>(MemoryHack::base + lightingShadowFilter_offset_mov);
               VirtualProtect(lightingShadowFilter_ptr_mov, 7, PAGE_EXECUTE_READWRITE, &old_protect);
               FlushInstructionCache(GetCurrentProcess(), lightingShadowFilter_ptr_mov, 7);
            lightingShadowFilter_ptr_value0 = std::bit_cast<uint*>(MemoryHack::base + lightingShadowFilter_offset_value0);
            lightingShadowFilter_ptr_value1 = std::bit_cast<uint*>(MemoryHack::base + lightingShadowFilter_offset_value1);
            lightingShadowFilter_ptr_value2 = std::bit_cast<uint*>(MemoryHack::base + lightingShadowFilter_offset_value2);
            lightingShadowFilter_ptr_value3 = std::bit_cast<uint*>(MemoryHack::base + lightingShadowFilter_offset_value3);
#endif
            break;
         default:
            break;
      }
      message(reshade::log::level::info, "ForcedLODBias::OnInit: Initializing done!");
   }
   
   static bool SetLodBiasRigid(reshade::api::effect_runtime* runtime, float bias)
   {
      //not supported
      if (MemoryHack::exe_type != MemoryHack::BO3Enhanced) return false;

      //hack orig set
      memcpy(lodBiasRigid_ptr_mov, bias != 0 ? lodBiasRigid_movups_noop : lodBiasRigid_movups_orig, sizeof(uint8_t) * 4);

      //set
      r_lodBiasRigid = bias;

      //save
      reshade::set_config_value(runtime, NAME, "r_lodBiasRigid", r_lodBiasRigid);

      return true;
   }

   static bool SetModelLodBias(reshade::api::effect_runtime* runtime, float bias)
   {
      //not supported
      if (MemoryHack::exe_type != MemoryHack::BO3Enhanced) return false;

      //set
      r_modelLodBias = bias;

      //save
      reshade::set_config_value(runtime, NAME, "r_modelLodBias", r_modelLodBias);

      return true;
   }

#if DEVELOPMENT
   static bool SetLightingShadowFilter(reshade::api::effect_runtime* runtime, int filter)
   {
      //not supported
      if (MemoryHack::exe_type != MemoryHack::BO3Enhanced) return false;
   
      //hack orig set
      memcpy(lightingShadowFilter_ptr_mov, filter != -1 ? lightingShadowFilter_mov_noop : lightingShadowFilter_mov_orig, sizeof(uint8_t) * 7);
   
      //state
      r_lightingShadowFilter = filter;
   
      //save
      reshade::set_config_value(runtime, NAME, "r_lightingShadowFilter", r_lightingShadowFilter);
   
      return true;
   }
#endif
   
   static void OnPresent()
   {
      //not supported
      if (MemoryHack::exe_type != MemoryHack::BO3Enhanced) return;
      
      //r_lodBiasRigid
      if (r_lodBiasRigid != 0.f)
      {
         *lodBiasRigid_ptr_value = r_lodBiasRigid;
      }

      //r_modelLodBias
      if (r_modelLodBias != 1.f)
      {
         *modelLodBias_ptr_value0 = r_modelLodBias;
         *modelLodBias_ptr_value1 = r_modelLodBias;
      }

#if DEVELOPMENT
      //r_lightingShadowFilter
      if (r_lightingShadowFilter != -1)
      {
         *lightingShadowFilter_ptr_value0 = r_lightingShadowFilter;
         *lightingShadowFilter_ptr_value1 = r_lightingShadowFilter;
         *lightingShadowFilter_ptr_value2 = r_lightingShadowFilter;
         *lightingShadowFilter_ptr_value3 = r_lightingShadowFilter;
      }
#endif
   }

   static void OnLoadSettings(reshade::api::effect_runtime* runtime)
   {
      if (MemoryHack::exe_type != MemoryHack::BO3Enhanced) return;
      
      reshade::get_config_value(runtime, NAME, "r_lodBiasRigid", r_lodBiasRigid);
      if (r_lodBiasRigid != 0.f) SetLodBiasRigid(runtime, r_lodBiasRigid);
      
      reshade::get_config_value(runtime, NAME, "r_modelLodBias", r_modelLodBias);
      if (r_modelLodBias != 1.f) SetModelLodBias(runtime, r_modelLodBias);
      
#if DEVELOPMENT
      reshade::get_config_value(runtime, NAME, "r_lightingShadowFilter", r_lightingShadowFilter);
      if (r_lightingShadowFilter != -1) SetLightingShadowFilter(runtime, r_lightingShadowFilter);
#endif
   }

   static void OnUI(reshade::api::effect_runtime* runtime)
   {
      //gatekeep: Unknown
      if (MemoryHack::exe_type != MemoryHack::BO3Enhanced)
      {
         //red
         ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 100, 100, 255));
         ImGui::BulletText("Unsupported version of the game detected.");
         ImGui::PopStyleColor();
         return;
      }
      
      if (ImGui::SliderFloat("r_lodBiasRigid", &r_lodBiasRigid, 0.f, -1000.f)) SetLodBiasRigid(runtime, r_lodBiasRigid);
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("Negative bias increases LOD of static meshes at a distance.");
      DrawResetButton(r_lodBiasRigid, 0.f, "r_lodBiasRigid", runtime);
      
      if (ImGui::SliderFloat("r_modelLodBias", &r_modelLodBias, 1.f, 10.f)) SetModelLodBias(runtime, r_modelLodBias);
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("(Unverified, can't tell difference.)\nBias multiplier for model LOD. Higher values increase LOD of models at a distance.");
      DrawResetButton(r_modelLodBias, 1.f, "r_modelLodBias", runtime);

#if DEVELOPMENT
      if (ImGui::SliderInt("r_lightingShadowFilter", &r_lightingShadowFilter, -1, 1)) SetLightingShadowFilter(runtime, r_lightingShadowFilter);
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("Toggle Shadow Dither/Filtering. (The game heavily relies on this!)");
      DrawResetButton(r_lightingShadowFilter, -1, "r_lightingShadowFilter", runtime);
#endif
   }
}
#endif

#if DEVELOPMENT
namespace FSFXTracking
{
   static bool enabled = false;

   //hash set of uint for shader hashes that are encountered
   static std::unordered_set<uint32_t> encountered_hashes;
   
   static void OnDraw(uint32_t hash, const char* shader_type)
   {
      if (!enabled) return;

      //return: seen
      if (encountered_hashes.find(hash) != encountered_hashes.end()) return;
      
      //hex & log
      std::string hash_as_hex = std::format("0x{:08X}", hash);
      message(reshade::log::level::info, std::string("FSFXTracking::OnDraw(): Encountered new " + hash_as_hex).c_str());
      
      //insert
      encountered_hashes.insert(hash);
      
      //paths
      std::string folder = "FSFXTracking";
      std::string filename = hash_as_hex + "." + shader_type + "_5_0.cso";
      std::string filepath = folder + "/" + filename;
      std::string filepathtmp = folder + "/" + filename + ".tmp";
      std::string dump = "Luma/Call of Duty Black Ops 3/Dump/" + filename;
      
      //create output folder if not already
      if (!std::filesystem::exists(folder)) std::filesystem::create_directories(folder);
      
      //copy from dump?
      if (std::filesystem::exists(dump) && !std::filesystem::exists(filepath)){
         copy_file(dump, filepath, std::filesystem::copy_options::overwrite_existing);
         message(reshade::log::level::info, std::string("FSFXTracking::OnDraw(): Copied shader from dump for " + hash_as_hex).c_str());
      }
      else if (!std::filesystem::exists(filepathtmp)) //make empty file in folder
      {
         std::filesystem::create_directories(folder);
         std::ofstream file(filepathtmp);
         file.close();
      }
   }
}

namespace TiledDeferredCSTracking //TODO: reduce duplicate code
{
   static bool enabled = false;
   
   //hash set of uint for shader hashes that are encountered
   static std::unordered_set<uint32_t> encountered_hashes;
   
   static void OnDraw(uint32_t hash, const char* shader_type)
   {
      if (!enabled) return;
      
      //return: seen
      if (encountered_hashes.find(hash) != encountered_hashes.end()) return;
      
      //hex & log
      std::string hash_as_hex = std::format("0x{:08X}", hash);
      message(reshade::log::level::info, std::string("TiledDeferredCSTracking::OnDraw(): Encountered new " + hash_as_hex).c_str());
      
      //insert
      encountered_hashes.insert(hash);
      
      //paths
      std::string folder = "TiledDeferredCSTracking";
      std::string filename = hash_as_hex + "." + shader_type + "_5_0.cso";
      std::string filepath = folder + "/" + filename;
      std::string filepathtmp = folder + "/" + filename + ".tmp";
      std::string dump = "Luma/Call of Duty Black Ops 3/Dump/" + filename;
      
      //create output folder if not already
      if (!std::filesystem::exists(folder)) std::filesystem::create_directories(folder);
      
      //copy from dump?
      if (std::filesystem::exists(dump) && !std::filesystem::exists(filepath)){
         copy_file(dump, filepath, std::filesystem::copy_options::overwrite_existing);
         message(reshade::log::level::info, std::string("TiledDeferredCSTracking::OnDraw(): Copied shader from dump for " + hash_as_hex).c_str());
      }
      else if (!std::filesystem::exists(filepathtmp)) //make empty file in folder
      {
         std::filesystem::create_directories(folder);
         std::ofstream file(filepathtmp);
         file.close();
      }
   }
}
#endif

namespace SectionedImGui 
{   
   namespace Templates
   {
      static bool is_disabled = false;
      
      static float GetPulseMultiplier(float speed = 0.1f, float amount = 0.25f)
      {
         return (1.f-amount/2) + amount/2 * sinf(cb_luma_global_settings.FrameIndex * speed);
      }

      static bool IsSDRMode()
      {
         return cb_luma_global_settings.DisplayMode == DisplayModeType::SDR;
      }

      static void DrawCompletelyUselessButton(const char* text = nullptr)
      {
         ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.75f, 0.75f, 0.75f, 1.f));
         ImGui::TextWrapped(!text ? "This section is useless." : text);
         ImGui::PopStyleColor();
      }
      
      static void README(DeviceData& device_data, reshade::api::effect_runtime* runtime)
      {
         // pulsing text
         auto pulse = GetPulseMultiplier();
         auto green = float3(0.75f, 1.f, 0.75f);
         auto color = ImVec4(green.x * pulse, green.y * pulse, green.z * pulse, 1.f);
         ImGui::PushStyleColor(ImGuiCol_Text, color);
         ImGui::TextWrapped("[Thanks For Downloading!]");
         ImGui::PopStyleColor();
         
         // Info
         ImGui::Bullet(); ImGui::SameLine(); ImGui::TextWrapped("This acts as a wizard to fully configure.");
         ImGui::Bullet(); ImGui::SameLine(); ImGui::TextWrapped("Ordered by importance, please at least view all non-Advanced sections!");
#if ENABLE_SR == 1
         ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.75f, 0.75f, 0.75f, 1.f));
         ImGui::Bullet(); ImGui::SameLine(); ImGui::TextWrapped(std::format("Detected Executable: {}", GetExeTypeName(MemoryHack::exe_type)).c_str());
         ImGui::PopStyleColor();
#endif
         
         if (Globals::UIIsAdvanced)
         {
            ImGui::Separator(); ////////////////////

            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.75f, 1.f, 1.f, 1.f));
            ImGui::TextWrapped("[Advanced settings are showing!]");
            ImGui::PopStyleColor();
            
            ImGui::Bullet(); ImGui::SameLine(); ImGui::TextWrapped("Be ready for some reading!");
         }
      }
      
      static void GammaCorrection(DeviceData& device_data, reshade::api::effect_runtime* runtime)
      {
         if (IsSDRMode())
         {
            DrawCompletelyUselessButton();
            return;
         }
         
         ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.75f, 1.f, 1.f, 1.f));
         ImGui::TextWrapped("[Reintroduce SDR's gamma mismatch to lower shadows.]");
         ImGui::PopStyleColor();

         //save prev
         float gamma_prev = custom_sdr_gamma;

         // Dropdown for custom_sdr_gamma
         const char* const items[] = { "Off (sRGB)", "Match SDR (2.2)", "Stronger (2.4)" };
         int custom_sdr_gamma_index = 0;
         if (custom_sdr_gamma > 0) { //detect
            if (abs(custom_sdr_gamma - 2.2f) < 0.001f) custom_sdr_gamma_index = 1;
            else if (custom_sdr_gamma == 2.4f) custom_sdr_gamma_index = 2;
         }
         ImGui::PushID("GammaCorrection custom_sdr_gamma");
         if (ImGui::Combo("Correction", &custom_sdr_gamma_index, items, IM_ARRAYSIZE(items))) //user set & save
         {
            switch (custom_sdr_gamma_index)
            {
            default: custom_sdr_gamma = 0.f; break;
            case 1: custom_sdr_gamma = 2.2f; break;
            case 2: custom_sdr_gamma = 2.4f; break;
            }
            ShaderDefineInfo::Set(GAMMA_CORRECTION_TYPE_HASH, custom_sdr_gamma > 0);
            defines_need_recompilation = true;
            reshade::set_config_value(runtime, NAME, "custom_sdr_gamma", custom_sdr_gamma);
         }
         ImGui::PopID();

         //link
         if (ImGui::Button("More Info & Test (Google Slides)"))
            Website::OpenWebsite("https://docs.google.com/presentation/d/e/2PACX-1vSXeLHlbm6repcS7fels1-SXYGRmzziRrnuJ8nDO8J5rsWV3dT1-nVyCKp0Tj_stwx-9qlCI-N6rYIT/pub?start=false&loop=false&slide=id.g3e007eafba8_0_0");

         //force define because its trolling sometimes
         if (bool is_define_on = ShaderDefineInfo::Get(GAMMA_CORRECTION_TYPE_HASH) == 1; is_define_on != (custom_sdr_gamma > 0.f))
            ShaderDefineInfo::Set(GAMMA_CORRECTION_TYPE_HASH, is_define_on ? '0' : '1');

         is_disabled = custom_sdr_gamma == 0.f;
         if (is_disabled) ImGui::BeginDisabled();
         {            
            ImGui::Separator(); ////////////////////
            
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.75f, 1.f, 1.f, 1.f));
            ImGui::TextWrapped("[\"Brightness\" slider replacement, due to the algorithm's SDR limitations.]");
            ImGui::PopStyleColor();
            
            //Gamma Influence
            if (ImGui::SliderFloat("Gamma Brightness", &cb_luma_global_settings.GameSettings.GammaInfluence, 0.f, 2.f))
               reshade::set_config_value(runtime, NAME, "GammaInfluence", cb_luma_global_settings.GameSettings.GammaInfluence);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Lower to let the gamma correction influence more of the image, and decreasing overall brightness.");
            DrawResetButton(cb_luma_global_settings.GameSettings.GammaInfluence, default_luma_global_game_settings.GammaInfluence, "GammaInfluence", runtime);
            
            if (Globals::UIIsAdvanced)
            {
               ImGui::Separator(); ////////////////////
            
               ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.75f, 1.f, 1.f, 1.f));
               ImGui::TextWrapped("[Correction may become artificial without limits.]");
               ImGui::PopStyleColor();

               //CUSTOM_GAMMA_CORRECTION_MODE dropdown
               bool is_disabled_mode = ShaderDefineInfo::Get(ShaderDefineInfo::CUSTOM_LUTBUILDER_COLORSPACE) == 0;
               if (is_disabled_mode) ImGui::BeginDisabled();
               {
                  auto a = "Per-Channel (Hue Shifts / Vanilla)";
                  auto b = "Perceptual (Hue Corrected)";
                  ShaderDefineInfo::UIDropDown(ShaderDefineInfo::CUSTOM_GAMMA_CORRECTION_MODE, "Gamma Correction Mode",
                     {a, is_disabled_mode ? a : b},
                     "(Only available when LUT Builder is upgraded to output wider in BT.2020!)\nHow should the gamma correction operate?\n\nPer-Channel is Vanilla Brightness Slider-like, with hue shifting shadows.\nPerceptual retains the hues of the original sRGB gamma output, only darkening luminance.");
               }
               if (is_disabled_mode) ImGui::EndDisabled();

               //GammaPerceptualChrominanceCorrect
               bool is_disabled_perceptual = ShaderDefineInfo::Get(ShaderDefineInfo::CUSTOM_GAMMA_CORRECTION_MODE) != 1 || ShaderDefineInfo::Get(ShaderDefineInfo::CUSTOM_LUTBUILDER_COLORSPACE) == 0;
               if (is_disabled_perceptual) ImGui::BeginDisabled();
               {
                  if (ImGui::SliderFloat("Perceptual Chrominance Gain Reduction", &cb_luma_global_settings.GameSettings.GammaPerceptualChrominanceCorrect, 0.f, 1.f, "%.4f"))
                     reshade::set_config_value(runtime, NAME, "GammaPerceptualChrominanceCorrect", cb_luma_global_settings.GameSettings.GammaPerceptualChrominanceCorrect);
                  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Reduce chrominance/saturation increase from Gamma Correction in the Perceptual mode, preventing it from becoming too artificial.");
                  DrawResetButton(cb_luma_global_settings.GameSettings.GammaPerceptualChrominanceCorrect, default_luma_global_game_settings.GammaPerceptualChrominanceCorrect, "GammaPerceptualChrominanceCorrect", runtime);
               }
               if (is_disabled_perceptual) ImGui::EndDisabled();
            }
         }
         if (is_disabled) ImGui::EndDisabled();
         
         ImGui::Separator(); ////////////////////

         //CUSTOM_HDTVREC709
         ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.75f, 1.f, 1.f, 1.f));
         ImGui::TextWrapped("[\"Display Mode\" replacement, due to technical difficulties otherwise.]");
         ImGui::PopStyleColor();
         {
            bool b = ShaderDefineInfo::Get(ShaderDefineInfo::CUSTOM_HDTVREC709) == 1;
            std::string label = "HDTV Rec.709 Gamma";
            if (ImGui::Checkbox(label.c_str(), &b)) ShaderDefineInfo::ToggleBool(ShaderDefineInfo::CUSTOM_HDTVREC709);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("This is the gamma mode seen on PS4.\nBEWARE: Rather insane, so maybe turn off Gamma Correction above first.");
         }
      }

      static void Brightness(DeviceData& device_data, reshade::api::effect_runtime* runtime)
      {
         if (IsSDRMode())
         {
            DrawCompletelyUselessButton();
            return;
         }
         
         ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.75f, 1.f, 1.f, 1.f));
         ImGui::TextWrapped("[Configure values to display limits or personal preference.]");
         ImGui::PopStyleColor();

         ImGui::Bullet(); ImGui::SameLine(); ImGui::TextWrapped("(Use Luma default sliders above!)");

         if (Globals::UIIsAdvanced)
         {
            ImGui::Bullet(); ImGui::SameLine(); ImGui::TextWrapped("HDR Stops based on Peak & Paper White: +%.2f", std::log2(cb_luma_global_settings.ScenePeakWhite / cb_luma_global_settings.ScenePaperWhite));
         }
         
         if (cb_luma_global_settings.DisplayMode != DisplayModeType::SDR) ShaderDefineInfo::UIToggleCheckmark(ShaderDefineInfo::SWAPCHAIN_TEST_USER_PEAK, "Test Display Peak", "3 rectangles inside a big one.\n- Left should disappear (2x Peak).\n- Middle should be barely visible (Peak).\n- Right should be easy to see (0.5x Peak).\n\n(Or set to personal preference, just don't let middle disappear!)");
         
         ImGui::Separator();

         ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.75f, 1.f, 1.f, 1.f));
         ImGui::TextWrapped("[FMVs Brightness]");
         ImGui::PopStyleColor();

         if (Globals::UIIsAdvanced)
         {
            ShaderDefineInfo::UIToggleCheckmark(ShaderDefineInfo::CUSTOM_UPSCALE_MOV, "PumboAutoHDR for FMVs",
               "Do inverse tonemap for FMVs.");
         }
         
         if (ImGui::SliderFloat("Brightness Ratio", &cb_luma_global_settings.GameSettings.MovPeakRatio, 0.f, 2.f, "%.4f"))
            reshade::set_config_value(runtime, NAME, "MovPeakRatio", cb_luma_global_settings.GameSettings.MovPeakRatio);
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Multiplier for peak output.\n(Useful for AAE users at main menu.)");
         DrawResetButton(cb_luma_global_settings.GameSettings.MovPeakRatio, default_luma_global_game_settings.MovPeakRatio, "MovPeakRatio", runtime);

         if (Globals::UIIsAdvanced)
         {
            is_disabled = ShaderDefineInfo::Get(ShaderDefineInfo::CUSTOM_UPSCALE_MOV) == 0;
            if (is_disabled) ImGui::BeginDisabled();
            {
               if (ImGui::SliderFloat("Shoulder Power", &cb_luma_global_settings.GameSettings.MovShoulderPow, 1.f, 10.f, "%.4f"))
                  reshade::set_config_value(runtime, NAME, "MovShoulderPow", cb_luma_global_settings.GameSettings.MovShoulderPow);
               if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Highlights contrast of PumboAutoHDR on movies.");
               DrawResetButton(cb_luma_global_settings.GameSettings.MovShoulderPow, default_luma_global_game_settings.MovShoulderPow, "MovShoulderPow", runtime);
            }
            if (is_disabled) ImGui::EndDisabled();
         }
      }

      static void PostProcessing(DeviceData& device_data, reshade::api::effect_runtime* runtime)
      {
         ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.75f, 1.f, 1.f, 1.f));
         ImGui::TextWrapped("[Various multipliers and whatnot.]");
         ImGui::PopStyleColor();
         
         if (Globals::UIIsAdvanced)
         {
            if (cb_luma_global_settings.DisplayMode == DisplayModeType::SDR) ImGui::BeginDisabled();
            {
               auto z = "Reinhard Per-Channel (Blowout)";
               auto a = "Reinhard Max-Channel (Saturation Preserve)";
               auto b = "Reinhard-Jodie (Some Blowout) (DEPRECATED/BROKEN!!!)";
               auto i0 = {a, b};
               auto i1 = {z, z};
               ShaderDefineInfo::UIDropDown(ShaderDefineInfo::CUSTOM_BLOOM_TONEMAP, "Bloom Tonemap",
                  cb_luma_global_settings.DisplayMode == DisplayModeType::SDR ? i1 : i0,
                  "After Bloom is aggregated, it gets SDR tonemapped before compositing on SDR tonemapped color.\nOriginal is Reinhard Per-Channel, which blows out hard, suitable for SDR.\nFor HDR, I found it better to still do it then inverse it after, but we don't have to blowout.");
            }
            if (cb_luma_global_settings.DisplayMode == DisplayModeType::SDR) ImGui::EndDisabled();
         }
         
         if (ImGui::SliderFloat("Bloom", &cb_luma_global_settings.GameSettings.Bloom, 0.f, 2.f))
            reshade::set_config_value(runtime, NAME, "Bloom", cb_luma_global_settings.GameSettings.Bloom);
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Bloom multiplier.\n(The game's hues are crucially basked by bloom, e.g. zombies menu campfire.)");
         DrawResetButton(cb_luma_global_settings.GameSettings.Bloom, default_luma_global_game_settings.Bloom, "Bloom", runtime);
         
         if (ImGui::SliderFloat("Lens Dirt / Flare", &cb_luma_global_settings.GameSettings.LensFlare, 0.f, 2.f))
            reshade::set_config_value(runtime, NAME, "LensFlare", cb_luma_global_settings.GameSettings.LensFlare);
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Lens Dirt / Flare billboards and overlays strength.\n(Don't expect great results when map uses lens flare for the sun.)");
         DrawResetButton(cb_luma_global_settings.GameSettings.LensFlare, default_luma_global_game_settings.LensFlare, "LensFlare", runtime);

         if (ImGui::SliderFloat("Slide Lens Dirt", &cb_luma_global_settings.GameSettings.SlideLensDirt, 0.f, 2.f))
            reshade::set_config_value(runtime, NAME, "SlideLensDirt", cb_luma_global_settings.GameSettings.SlideLensDirt);
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Bottom of screen lens dirt from slide.");
         DrawResetButton(cb_luma_global_settings.GameSettings.SlideLensDirt, default_luma_global_game_settings.SlideLensDirt, "SlideLensDirt", runtime);

         if (ImGui::SliderFloat("ADS Sights", &cb_luma_global_settings.GameSettings.ADSSights, 0.f, 2.f))
            reshade::set_config_value(runtime, NAME, "ADSSights", cb_luma_global_settings.GameSettings.ADSSights);
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("ADS holographic overlay sights.");
         DrawResetButton(cb_luma_global_settings.GameSettings.ADSSights, default_luma_global_game_settings.ADSSights, "ADSSights", runtime);

         if (ImGui::SliderFloat("Xray Outline", &cb_luma_global_settings.GameSettings.XrayOutline, 0.f, 2.f))
            reshade::set_config_value(runtime, NAME, "XrayOutline", cb_luma_global_settings.GameSettings.XrayOutline);
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Xray outline for objectives and players.");
         DrawResetButton(cb_luma_global_settings.GameSettings.XrayOutline, default_luma_global_game_settings.XrayOutline, "XrayOutline", runtime);

         if (ImGui::SliderFloat("Motion Blur", &cb_luma_global_settings.GameSettings.MotionBlur, 0.f, 2.f))
            reshade::set_config_value(runtime, NAME, "MotionBlur", cb_luma_global_settings.GameSettings.MotionBlur);
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Motion blur length multiplier.");
         DrawResetButton(cb_luma_global_settings.GameSettings.MotionBlur, default_luma_global_game_settings.MotionBlur, "MotionBlur", runtime);

         //CUSTOM_MB_QUALITY
         {
            ShaderDefineInfo::UIDropDown(ShaderDefineInfo::CUSTOM_MB_QUALITY, "Motion Blur Quality",
               {"6 Samples (Original)", "16 Samples", "24 Samples (High)", "32 Samples", "48 Samples", "64 Samples", "128 Samples (Uhhh)"},
               "Motion blur quality; The amount of samples for its trails. Increase to try and reduce fireflies at a performance cost.");
         }

         if (ImGui::SliderFloat("Volumetric Fog", &cb_luma_global_settings.GameSettings.VolumetricFog, 0.f, 2.f))
            reshade::set_config_value(runtime, NAME, "VolumetricFog", cb_luma_global_settings.GameSettings.VolumetricFog);
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Volumetric fog multiplier.");
         DrawResetButton(cb_luma_global_settings.GameSettings.VolumetricFog, default_luma_global_game_settings.VolumetricFog, "VolumetricFog", runtime);
         
         if (ImGui::SliderFloat("RCAS Sharpening", &cb_luma_global_settings.GameSettings.RCAS, 0.f, 1.f))
            reshade::set_config_value(runtime, NAME, "RCAS", cb_luma_global_settings.GameSettings.RCAS);
         {
            bool isOn = ShaderDefineInfo::Get(ShaderDefineInfo::CUSTOM_RCAS) == 1;
            if (!isOn && cb_luma_global_settings.GameSettings.RCAS > 0.f) ShaderDefineInfo::Set(ShaderDefineInfo::CUSTOM_RCAS, '1');
            else if (isOn && cb_luma_global_settings.GameSettings.RCAS == 0.f) ShaderDefineInfo::Set(ShaderDefineInfo::CUSTOM_RCAS, '0');
         }
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Robust (4 instead of 8 additional samples) Contrast Adaptive Sharpening strength, done after anti-aliasing.");
         DrawResetButton(cb_luma_global_settings.GameSettings.RCAS, default_luma_global_game_settings.RCAS, "RCAS", runtime);

         //CUSTOM_CHROMABER
         ShaderDefineInfo::UIToggleCheckmark(ShaderDefineInfo::CUSTOM_CHROMABER, "Chromatic Aberration",
            "Toggle chromatic aberration.\nSeldom usage, only in main menu and campaign cutscenes.");

#if ENABLE_SR == 1
         ImGui::Separator();

         ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.75f, 1.f, 1.f, 1.f));
         ImGui::TextWrapped("[UI Toggles]");
         ImGui::PopStyleColor();
         
         if (ImGui::Checkbox("Fullscreen Blur (Cheats)", &Globals::IsFullscreenBlur))
            reshade::set_config_value(runtime, NAME, "IsFullscreenBlur", Globals::IsFullscreenBlur);
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Toggle fullscreen blurring.\nSeen in pause menu and Nova 6 Crawler fart gas.");
         DrawResetButton(Globals::IsFullscreenBlur, true, "IsFullscreenBlur", runtime);
               
         if (ImGui::Checkbox("Draw UI", &Globals::IsUi))
            reshade::set_config_value(runtime, NAME, "IsUi", Globals::IsUi);
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Toggle UI drawing.\nWill discard all shaders after final composition shader.");
         DrawResetButton(Globals::IsUi, true, "IsUi", runtime);
#endif
      }

      static void BlackFloorLower(DeviceData& device_data, reshade::api::effect_runtime* runtime)
      {
         ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.75f, 1.f, 1.f, 1.f));
         ImGui::TextWrapped("[The SDR tonemap rolloff is raised, though lowering makes it harder to see.]");
         ImGui::PopStyleColor();
         
         float ratio = BlackFloorSDRTonemap::ToRatio(cb_luma_global_settings.GameSettings.BlackFloorSDRTonemap);
         if (ImGui::SliderFloat("SDR Rolloff Lowering", &ratio, 0.f, 1.f))
         {
            cb_luma_global_settings.GameSettings.BlackFloorSDRTonemap = BlackFloorSDRTonemap::ToReal(ratio);
            reshade::set_config_value(runtime, NAME, "BlackFloorSDRTonemap", cb_luma_global_settings.GameSettings.BlackFloorSDRTonemap);
         }
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Increase to lower minimum output to 0.");
         DrawResetButton(cb_luma_global_settings.GameSettings.BlackFloorSDRTonemap, default_luma_global_game_settings.BlackFloorSDRTonemap, "BlackFloorSDRTonemap", runtime);

         if (ImGui::Button("Nerd Explanation (Desmos Graph)")) Website::OpenWebsite("https://www.desmos.com/calculator/1hmlnb6z1m");

         if (Globals::UIIsAdvanced)
         {
            ImGui::Separator();

            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.75f, 1.f, 1.f, 1.f));
            ImGui::TextWrapped("[A handful of color grading results are raised, though lowering is unintended.]");
            ImGui::PopStyleColor();
            
            auto def = ShaderDefineInfo::UIDropDown(ShaderDefineInfo::CUSTOM_BLACKFLOOR_LUT, "LUT Correction Mode",
               {"Off", "Luminance (no hue shift and chrominance gain)", "Per-Channel (hue shift and chrominance gain)"},
               "(Seldom needed) Lower the raised floor from LUT color grading results.\nUseful for maps like The Giant, though probably unintended.");

            is_disabled = def == 0;
            if (is_disabled) ImGui::BeginDisabled();
            {
               if (ImGui::SliderFloat("LUT Correction", &cb_luma_global_settings.GameSettings.BlackFloorLUT, 0.f, 1.f))
                  reshade::set_config_value(runtime, NAME, "BlackFloorLUT", cb_luma_global_settings.GameSettings.BlackFloorLUT);
               if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("The amount to rescale the raised black floor from LUT down.\nUseful for maps like The Giant, though probably unintended.");
               DrawResetButton(cb_luma_global_settings.GameSettings.BlackFloorLUT, default_luma_global_game_settings.BlackFloorLUT, "BlackFloorLUT", runtime);
            }
            if (is_disabled) ImGui::EndDisabled();
         }
      }

#if ENABLE_SR == 1
      static void SuperResolution(DeviceData& device_data, reshade::api::effect_runtime* runtime)
      {
         auto& game_device_data = CallOfDutyBlackOps3GameDeviceData::GetGameDeviceData(device_data);
         
         ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.75f, 1.f, 1.f, 1.f));
         ImGui::TextWrapped("[Use for a more filmic look, though with the usual temporal artifacts.]");
         ImGui::PopStyleColor();

         if (device_data.sr_type == SR::Type::None)
         {
            DrawCompletelyUselessButton("Select/Enable Super Resolution near the top to use this section.");
            return;
         }

         auto pulse = GetPulseMultiplier();
         auto colors = float3(1.f, 0.75f, 0.0f);
         auto colord = ImVec4(colors.x * pulse, colors.y * pulse, colors.z * pulse, 1.f);
         ImGui::PushStyleColor(ImGuiCol_Text, colord);
         ImGui::Bullet(); ImGui::SameLine(); ImGui::TextWrapped("You must have SMAA T2x (Not Filmic) selected!");
         ImGui::PopStyleColor();
         
         ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 1.f, 0.75f, 1.0f));
         ImGui::Bullet(); ImGui::SameLine(); ImGui::TextWrapped("Responds to \"Render Resolution\" in-game settings dropdown!\nGreater than 100%% is untested.");
         ImGui::PopStyleColor();

         {
            float ratio = static_cast<float>(game_device_data.resources_smaa_color_input.desc.Height) / static_cast<float>(game_device_data.resources_sr_output.desc.Height) * 100.f;
            std::string ratio_formatted = std::to_string(static_cast<int>(ratio)) + "%%";
            std::string internal_vs_output_str = std::to_string(game_device_data.resources_smaa_color_input.desc.Height) + "p -> " + std::to_string(game_device_data.resources_sr_output.desc.Height) + "p";
            std::string is_dlaa_str = game_device_data.IsDLAA() ? "(Native/DLAA)" : "(Upscaled/DLSS)";
            std::string combined_str = ratio_formatted + ", " + internal_vs_output_str + " " + is_dlaa_str;
            ImGui::Bullet(); ImGui::SameLine(); ImGui::TextWrapped(combined_str.c_str());
         }

         if (ImGui::Button("Panic/Force Reset")) { device_data.force_reset_sr = true; game_device_data.HardResetSR(); }
         if (ImGui::IsItemHovered()) ImGui::SetTooltip("Reset Super Resolution resources and clear its history.");

         ImGui::Separator();

         ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.75f, 1.f, 1.f, 1.f));
         ImGui::TextWrapped("[Inject additional subpixel positions for better temporal aggregation.]");
         ImGui::PopStyleColor();
         DLSSJitter::OnUI(runtime, device_data, game_device_data);
         
         ImGui::Separator();

         ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.75f, 1.f, 1.f, 1.f));
         ImGui::TextWrapped("[Exposure Quirk]");
         ImGui::PopStyleColor();
         
         ImGui::PushID("SR: Auto Exposure");
         if (ImGui::Checkbox("Auto Exposure", &Globals::SRAutoExposure))
            reshade::set_config_value(runtime, NAME, "SRAutoExposure", Globals::SRAutoExposure);
         ImGui::PopID();
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("DLSS will adapt to the input, usually resulting in more smoothed highlights.\nOtherwise, the exposure null, resulting in sharper highlights but probably more smearing.\n\nPreset L/M seems to ignore this.");
         DrawResetButton(Globals::SRAutoExposure, true, "SRAutoExposure", runtime);

         if (Globals::UIIsAdvanced)
         {
            ImGui::PushID("SR: Pre Exposure");
            if (ImGui::SliderFloat("Pre Exposure", &Globals::SRPreExposure, 0.f, 3.f, "%.4f"))
               reshade::set_config_value(runtime, NAME, "SRPreExposure", Globals::SRPreExposure);
            ImGui::PopID();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
               ImGui::SetTooltip("The pre-exposure value to multiply color for edge weights calculations.\nSet 0 for auto.\n\nI think this is supposed to be the inverse of a pipeline's exposure compensation or whatnot.\nIf you change manually, you need really small/large numbers to have an effect.");
            DrawResetButton(Globals::SRPreExposure, 0.f, "SRPreExposure", runtime);
         }

         // if (Globals::UIIsAdvanced)
         // {
         //    ImGui::Separator();
         //
         //    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.75f, 1.f, 1.f, 1.f));
         //    ImGui::TextWrapped("[Sharpen distant texture sampling, though probably with unintended side effects.]");
         //    ImGui::PopStyleColor();
         //
         //    ImGui::PushID("SR Sampler Upgrade Bypass");
         //    if (ImGui::Checkbox("Bypass", &Globals::SRIgnoreSamplerUpgrade))
         //       reshade::set_config_value(runtime, NAME, "SRIgnoreSamplerUpgrade", Globals::SRIgnoreSamplerUpgrade);
         //    ImGui::PopID();
         //
         //    if (!Globals::SRIgnoreSamplerUpgrade)
         //    {
         //       ImGui::PushID("SR: Set Mipmaps LOD Bias");
         //       ImGui::Checkbox("Set Mipmaps LOD Bias", &Globals::SRSetMipLodBias);
         //       reshade::set_config_value(runtime, NAME, "SRSetMipLodBias", Globals::SRSetMipLodBias);
         //       ImGui::PopID();
         //       if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         //          ImGui::SetTooltip("Change texture Mipmaps LOD bias to negative every frame for DLSS jitter to sample the correct texel to aggregate.\n(A negative LOD bias forces sharper textures even at a distance.)\nBest to leave on, unless there is outstanding shadow map issues (I saw this once or twice, don't know if this is even cause).");
         //       DrawResetButton(Globals::SRSetMipLodBias, true, "SRSetMipLodBias", runtime);
         //
         //       ImGui::PushID("SR: Mipmaps LOD Bias Manual Override");
         //       if (ImGui::SliderFloat("Mipmaps LOD Bias Manual", &Globals::SRSetMipLodBiasManual, -4.f, 0.f, "%.2f"))
         //          reshade::set_config_value(runtime, NAME, "SRSetMipLodBiasManual", Globals::SRSetMipLodBiasManual);
         //       ImGui::PopID();
         //       if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         //          ImGui::SetTooltip("Engaged if less than 0.");
         //       DrawResetButton(Globals::SRSetMipLodBiasManual, 0.f, "SRSetMipLodBiasManual", runtime);
         //
         //       ImGui::BulletText("Effective: %f", device_data.texture_mip_lod_bias_offset);
         //    }
         // }
      }

      static void ForcedLODBias(DeviceData& device_data, reshade::api::effect_runtime* runtime)
      {
         ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.75f, 1.f, 1.f, 1.f));
         ImGui::TextWrapped("[Counteract decrease of distant object quality at lower internal resolutions.]");
         ImGui::PopStyleColor();
         
         ImGui::Bullet(); ImGui::SameLine(); ImGui::TextWrapped("(Useless if you have access console and can change r_lodBiasRigid.)");

         ForcedLODBias::OnUI(runtime);
         if (ForcedLODBias::IsActive())
         {
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(100, 255, 100, 255));
            ImGui::BulletText("ACTIVE AND PATCHING!");
            ImGui::PopStyleColor();
         }
      }
#endif

      static void SimpleAdvancedSettings(DeviceData& device_data, reshade::api::effect_runtime* runtime)
      {
         if (IsSDRMode())
         {
            DrawCompletelyUselessButton();
            return;
         }
         
         bool is_clicked;
         
         //CUSTOM_LUTBUILDER_COLORSPACE
         ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.75f, 1.f, 1.f, 1.f));
         ImGui::TextWrapped("[Fake Wide Color Gamut]");
         ImGui::PopStyleColor();
         ImGui::Bullet(); ImGui::SameLine(); ImGui::TextWrapped("Helps perceptually bring back mid-high orange, but may make everything too \"10000%% Digital Vibrance\" looking.");
         ImGui::Bullet(); ImGui::SameLine(); ImGui::TextWrapped("(This is purely tomfoolery, as the original is non-wide BT.709 primaries. You need deep changes and regrading, e.g. Black Ops 4.)");
         
         ImGui::PushID("Preset BT2020 Off");
         is_clicked = ImGui::Button("Off");
         ImGui::PopID();
         if (is_clicked)
         {
            ShaderDefineInfo::Set(ShaderDefineInfo::CUSTOM_LUTBUILDER_COLORSPACE, 0);
         }
         
         ImGui::SameLine();
         ImGui::PushID("Preset BT2020 On 0");
         is_clicked = ImGui::Button("Low");
         ImGui::PopID();
         if (is_clicked)
         {
            ShaderDefineInfo::Set(ShaderDefineInfo::CUSTOM_LUTBUILDER_COLORSPACE, 1);
            ShaderDefineInfo::Set(ShaderDefineInfo::CUSTOM_LUTBUILDER_SATBOOST, 1);
            cb_luma_global_settings.GameSettings.LUTBuilderExpansionChrominance = default_luma_global_game_settings.LUTBuilderExpansionChrominance;
            cb_luma_global_settings.GameSettings.LUTBuilderExpansionLuminance = default_luma_global_game_settings.LUTBuilderExpansionLuminance;
            cb_luma_global_settings.GameSettings.LUTBuilderHighlightSat = default_luma_global_game_settings.LUTBuilderHighlightSat;
            cb_luma_global_settings.GameSettings.LUTBuilderHighlightSatHighlightsOnly = default_luma_global_game_settings.LUTBuilderHighlightSatHighlightsOnly;
            reshade::set_config_value(runtime, NAME, "LUTBuilderExpansionChrominance", cb_luma_global_settings.GameSettings.LUTBuilderExpansionChrominance);
            reshade::set_config_value(runtime, NAME, "LUTBuilderExpansionLuminance", cb_luma_global_settings.GameSettings.LUTBuilderExpansionLuminance);
            reshade::set_config_value(runtime, NAME, "LUTBuilderHighlightSat", cb_luma_global_settings.GameSettings.LUTBuilderHighlightSat);
            reshade::set_config_value(runtime, NAME, "LUTBuilderHighlightSatHighlightsOnly", cb_luma_global_settings.GameSettings.LUTBuilderHighlightSatHighlightsOnly);
         }
         
         ImGui::SameLine();
         ImGui::PushID("Preset BT2020 On 1");
         is_clicked = ImGui::Button("High");
         ImGui::PopID();
         if (is_clicked)
         {
            ShaderDefineInfo::Set(ShaderDefineInfo::CUSTOM_LUTBUILDER_COLORSPACE, 1);
            ShaderDefineInfo::Set(ShaderDefineInfo::CUSTOM_LUTBUILDER_SATBOOST, 1);
            cb_luma_global_settings.GameSettings.LUTBuilderExpansionChrominance = 0.85f;
            cb_luma_global_settings.GameSettings.LUTBuilderExpansionLuminance = 0.25f;
            cb_luma_global_settings.GameSettings.LUTBuilderHighlightSat = 0.2f;
            cb_luma_global_settings.GameSettings.LUTBuilderHighlightSatHighlightsOnly = default_luma_global_game_settings.LUTBuilderHighlightSatHighlightsOnly;
            reshade::set_config_value(runtime, NAME, "LUTBuilderExpansionChrominance", cb_luma_global_settings.GameSettings.LUTBuilderExpansionChrominance);
            reshade::set_config_value(runtime, NAME, "LUTBuilderExpansionLuminance", cb_luma_global_settings.GameSettings.LUTBuilderExpansionLuminance);
            reshade::set_config_value(runtime, NAME, "LUTBuilderHighlightSat", cb_luma_global_settings.GameSettings.LUTBuilderHighlightSat);
            reshade::set_config_value(runtime, NAME, "LUTBuilderHighlightSatHighlightsOnly", cb_luma_global_settings.GameSettings.LUTBuilderHighlightSatHighlightsOnly);
         }

         ImGui::Separator();

         //CUSTOM_LUTBUILDER_NEUTRAL
         ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.75f, 1.f, 1.f, 1.f));
         ImGui::TextWrapped("[Force Neutral LUT Color Grading]");
         ImGui::PopStyleColor();
         ImGui::Bullet(); ImGui::SameLine(); ImGui::TextWrapped("Reduce forced extreme blowout and contrast by some custom maps, though unintended.");
         ImGui::Bullet(); ImGui::SameLine(); ImGui::TextWrapped("Example maps: Kowloon, Die Rise, CTown, Gulag Zombies.");
         
         ImGui::PushID("Preset Neutral Off");
         is_clicked = ImGui::Button("Off");
         ImGui::PopID();
         if (is_clicked)
         {
            ShaderDefineInfo::Set(ShaderDefineInfo::CUSTOM_LUTBUILDER_NEUTRAL, 0);
         }

         ImGui::SameLine();
         ImGui::PushID("Preset Neutral On 0");
         is_clicked = ImGui::Button("Low");
         ImGui::PopID();
         if (is_clicked)
         {
            ShaderDefineInfo::Set(ShaderDefineInfo::CUSTOM_LUTBUILDER_NEUTRAL, 1);
            ShaderDefineInfo::Set(ShaderDefineInfo::CUSTOM_LUTBUILDER_NEUTRAL_LUMA, 1);
            cb_luma_global_settings.GameSettings.LUTBuilderNeutralHue = default_luma_global_game_settings.LUTBuilderNeutralHue;
            cb_luma_global_settings.GameSettings.LUTBuilderNeutralChrominance = default_luma_global_game_settings.LUTBuilderNeutralChrominance;
            cb_luma_global_settings.GameSettings.LUTBuilderNeutralLuma = default_luma_global_game_settings.LUTBuilderNeutralLuma;
            reshade::set_config_value(runtime, NAME, "LUTBuilderNeutralHue", cb_luma_global_settings.GameSettings.LUTBuilderNeutralHue);
            reshade::set_config_value(runtime, NAME, "LUTBuilderNeutralChrominance", cb_luma_global_settings.GameSettings.LUTBuilderNeutralChrominance);
            reshade::set_config_value(runtime, NAME, "LUTBuilderNeutralLuma", cb_luma_global_settings.GameSettings.LUTBuilderNeutralLuma);
         }

         ImGui::SameLine();
         ImGui::PushID("Preset Neutral On 1");
         is_clicked = ImGui::Button("High");
         ImGui::PopID();
         if (is_clicked)
         {
            ShaderDefineInfo::Set(ShaderDefineInfo::CUSTOM_LUTBUILDER_NEUTRAL, 1);
            ShaderDefineInfo::Set(ShaderDefineInfo::CUSTOM_LUTBUILDER_NEUTRAL_LUMA, 1);
            cb_luma_global_settings.GameSettings.LUTBuilderNeutralHue = default_luma_global_game_settings.LUTBuilderNeutralHue;
            cb_luma_global_settings.GameSettings.LUTBuilderNeutralChrominance = 0.9f;
            cb_luma_global_settings.GameSettings.LUTBuilderNeutralLuma = default_luma_global_game_settings.LUTBuilderNeutralLuma;
            reshade::set_config_value(runtime, NAME, "LUTBuilderNeutralHue", cb_luma_global_settings.GameSettings.LUTBuilderNeutralHue);
            reshade::set_config_value(runtime, NAME, "LUTBuilderNeutralChrominance", cb_luma_global_settings.GameSettings.LUTBuilderNeutralChrominance);
            reshade::set_config_value(runtime, NAME, "LUTBuilderNeutralLuma", cb_luma_global_settings.GameSettings.LUTBuilderNeutralLuma);
         }

         ImGui::Separator();

         //CUSTOM_PCC
         ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.75f, 1.f, 1.f, 1.f));
         ImGui::TextWrapped("[Chrominance & Hue Recovery via SDR Rolloff Extension]");
         ImGui::PopStyleColor();
         ImGui::Bullet(); ImGui::SameLine(); ImGui::TextWrapped("Essential for HDR!");
         ImGui::Bullet(); ImGui::SameLine(); ImGui::TextWrapped("Some overexposed sunny maps may require on Low to better blowout.");
         
         ImGui::PushID("Preset PCC On 1");
         is_clicked = ImGui::Button("High (Recommended)");
         ImGui::PopID();
         if (is_clicked)
         {
            ShaderDefineInfo::Set(ShaderDefineInfo::CUSTOM_PCC, 1);
            cb_luma_global_settings.GameSettings.PCCHue = default_luma_global_game_settings.PCCHue;
            cb_luma_global_settings.GameSettings.PCCChrominance = default_luma_global_game_settings.PCCChrominance;
            // cb_luma_global_settings.GameSettings.PCCPeak = default_luma_global_game_settings.PCCPeak;
            reshade::set_config_value(runtime, NAME, "PCCHue", cb_luma_global_settings.GameSettings.PCCHue);
            reshade::set_config_value(runtime, NAME, "PCCChrominance", cb_luma_global_settings.GameSettings.PCCChrominance);
            // reshade::set_config_value(runtime, NAME, "PCCPeak", cb_luma_global_settings.GameSettings.PCCPeak);
         }

         ImGui::SameLine();
         ImGui::PushID("Preset PCC On 0");
         is_clicked = ImGui::Button("Low (Fallback)");
         ImGui::PopID();
         if (is_clicked)
         {
            ShaderDefineInfo::Set(ShaderDefineInfo::CUSTOM_PCC, 1);
            cb_luma_global_settings.GameSettings.PCCHue = default_luma_global_game_settings.PCCHue;
            cb_luma_global_settings.GameSettings.PCCChrominance = default_luma_global_game_settings.PCCChrominance * 0.5f;
            // cb_luma_global_settings.GameSettings.PCCPeak = default_luma_global_game_settings.PCCPeak;
            reshade::set_config_value(runtime, NAME, "PCCHue", cb_luma_global_settings.GameSettings.PCCHue);
            reshade::set_config_value(runtime, NAME, "PCCChrominance", cb_luma_global_settings.GameSettings.PCCChrominance);
            // reshade::set_config_value(runtime, NAME, "PCCPeak", cb_luma_global_settings.GameSettings.PCCPeak);
         }
      }

      static void HDRTonemapping(DeviceData& device_data, reshade::api::effect_runtime* runtime)
      {
         if (IsSDRMode())
         {
            DrawCompletelyUselessButton();
            return;
         }

         ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.75f, 1.f, 1.f, 1.f));
         ImGui::TextWrapped("[Control how HDR luminance is rolled off / compressed.]");
         ImGui::PopStyleColor();

         //Exposure
         is_disabled = cb_luma_global_settings.DisplayMode == DisplayModeType::SDR;
         if (is_disabled) { ImGui::BeginDisabled(); cb_luma_global_settings.GameSettings.Exposure = 1.f; }
         {
            if (ImGui::SliderFloat("Exposure", &cb_luma_global_settings.GameSettings.Exposure, 0.f, 3.f))
               reshade::set_config_value(runtime, NAME, "Exposure", cb_luma_global_settings.GameSettings.Exposure);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Exposure before HDR tonemap.\nAlternative to Scene Paper White without shifting Gamma Correction influence range.");
            DrawResetButton(cb_luma_global_settings.GameSettings.Exposure, default_luma_global_game_settings.Exposure, "Exposure", runtime);
         }
         if (is_disabled) ImGui::EndDisabled();
         
         //Selection
         int tonemap_def = ShaderDefineInfo::UIDropDown(ShaderDefineInfo::CUSTOM_TONEMAP, "HDR Tonemapper Type",
            {"Off", "Reinhard Piecewise (Gradual)", "Hermite Spline (Scalable)"},
            "Select HDR tonemapping to preference.\nEach has a different curve and feel.");
         
         //Shoulder Start
         is_disabled = !(tonemap_def == 1);
         if (!is_disabled)
         {
            if (ImGui::SliderFloat("HDR Tonemapper Rolloff Start", &cb_luma_global_settings.GameSettings.TonemapperRolloffStart, 20.f, 500.f, "%.0f"))
               reshade::set_config_value(runtime, NAME, "TonemapperRolloffStart", cb_luma_global_settings.GameSettings.TonemapperRolloffStart);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("HDR tonemapper's rolloff/shoulder start in nits.");
            if (!is_disabled && cb_luma_global_settings.GameSettings.TonemapperRolloffStart > cb_luma_global_settings.ScenePeakWhite)
            {
               ImGui::SameLine();
               ImGui::SmallButton(ICON_FK_WARNING);
               if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("This is higher than peak!");
            }
            DrawResetButton(cb_luma_global_settings.GameSettings.TonemapperRolloffStart, default_luma_global_game_settings.TonemapperRolloffStart, "TonemapperRolloffStart", runtime);
         }

         is_disabled = !(tonemap_def > 0);
         if (!is_disabled)
         {
            if (ImGui::SliderFloat("HDR Tonemapper Expected Max", &cb_luma_global_settings.GameSettings.TonemapperMaxExpected, 20000, tonemap_def == 1 ? 100000.f : 200000.f, "%.0f"))
               reshade::set_config_value(runtime, NAME, "TonemapperMaxExpected", cb_luma_global_settings.GameSettings.TonemapperMaxExpected);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("HDR tonemapper's expected max nits. Reduce to cause clipping.");
            if (!is_disabled && cb_luma_global_settings.GameSettings.TonemapperMaxExpected < cb_luma_global_settings.ScenePeakWhite)
            {
               ImGui::SameLine();
               ImGui::SmallButton(ICON_FK_WARNING);
               if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("This is lower than peak!");
            }
            DrawResetButton(cb_luma_global_settings.GameSettings.TonemapperMaxExpected, default_luma_global_game_settings.TonemapperMaxExpected, "TonemapperMaxExpected", runtime);
         }

         auto def = ShaderDefineInfo::UIToggleCheckmark(ShaderDefineInfo::CUSTOM_PERCHANNELLUMAEMULATE, "Per Channel Luminance Reduction Emulate",
            "Emulate how bright single channel colors loses luminance after per-channel tonemapping,\nreducing glowing reds and whatnot,\nperhaps gluing together fire highlights as intended in SDR.");

         ImGui::Separator();

         is_disabled = tonemap_def == 0;
         int scaling_def = ShaderDefineInfo::Get(ShaderDefineInfo::CUSTOM_TONEMAP_SCALING);
         if (!is_disabled)
         {
            auto def = ShaderDefineInfo::UIDropDown(ShaderDefineInfo::CUSTOM_TONEMAP_SCALING, "HDR Tonemapper Scaling",
                  {"Luminance (Natural)", "Max Channel (Saturation Preserve / Not Recommended)"},
                  "The \"pivot\" of for the tonemapper to use and scale color.");
         }

         is_disabled = tonemap_def == 0 || scaling_def != 0;
         if (!is_disabled)
         {
            auto def = ShaderDefineInfo::UIDropDown(ShaderDefineInfo::CUSTOM_TONEMAP_CLAMP, "HDR Tonemapper Luminance Scaling Clamp",
               {"Clip (Off)", "Clip (via Clamp)", "Max Channel Scale Down (Band-aid Saturation Preserve / Not Recommended)"},
               "How should overshoots from luminance scaling be handled.");
         }

         //Final Clamp
         {
            auto def = ShaderDefineInfo::UIDropDown(ShaderDefineInfo::CUSTOM_FINAL_CLAMP, "Before UI Clamp",
              {"Off", "Clip (via Clamp)", "Max Channel Scale Down (Band-aid Saturation Preserve / Not Recommended)"},
              "Clamp color overshoots from FXs after tonemap.");
         }

         //Swapchain Clamp
         {
            auto def = ShaderDefineInfo::UIDropDown(ShaderDefineInfo::SWAPCHAIN_CLAMP_PEAK, "Swapchain Clamp",
               {"Off", "Clip (via Clamp)", "Max Channel Scale Down (Band-aid Saturation Preserve / Not Recommended)"},
               "Clamp color overshoot at the very end, including UI.");
         }
      }

      static void RenoDXColorGrade(DeviceData& device_data, reshade::api::effect_runtime* runtime)
      {
         if (IsSDRMode())
         {
            DrawCompletelyUselessButton();
            return;
         }

         ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.75f, 1.f, 1.f, 1.f));
         ImGui::TextWrapped("[Luminance color grading from RenoDX.]");
         ImGui::PopStyleColor();

         // if (ImGui::Button("Toggle Color Grading")) ShaderDefineInfo::ToggleBool(ShaderDefineInfo::CUSTOM_COLORGRADE);
         bool def = ShaderDefineInfo::UIToggleCheckmark(ShaderDefineInfo::CUSTOM_COLORGRADE, "RenoDX Pre-UI Luminance Color Grading", "Custom color grading from RenoDX.\nKinda like an audio equalizer but for luminance.");
         
         is_disabled = ShaderDefineInfo::Get(ShaderDefineInfo::CUSTOM_COLORGRADE) == 0;
         if (is_disabled) ImGui::BeginDisabled();

         ImGui::PushID("CG: Contrast");
         if (ImGui::SliderFloat("Contrast", &cb_luma_global_settings.GameSettings.CGContrast, 0.f, 2.f, "%.4f"))
            reshade::set_config_value(runtime, NAME, "CGContrast", cb_luma_global_settings.GameSettings.CGContrast);
         ImGui::PopID();
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("RenoDX power based contrast before HDR tonemap.");
         DrawResetButton(cb_luma_global_settings.GameSettings.CGContrast, default_luma_global_game_settings.CGContrast, "CGContrast", runtime);

         ImGui::PushID("CG: Contrast Mid Gray");
         if (ImGui::SliderFloat("Contrast Mid Gray", &cb_luma_global_settings.GameSettings.CGContrastMidGray, 0.f, 500.f))
            reshade::set_config_value(runtime, NAME, "CGContrastMidGray", cb_luma_global_settings.GameSettings.CGContrastMidGray);
         ImGui::PopID();
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Contrast's mid gray value to stretch in/out luminance.");
         DrawResetButton(cb_luma_global_settings.GameSettings.CGContrastMidGray, default_luma_global_game_settings.CGContrastMidGray, "CGContrastMidGray", runtime);

         ImGui::PushID("CG: Highlights");
         if (ImGui::SliderFloat(" Highlights", &cb_luma_global_settings.GameSettings.CGHighlightsStrength, 0.f, 2.f))
            reshade::set_config_value(runtime, NAME, "CGHighlightsStrength", cb_luma_global_settings.GameSettings.CGHighlightsStrength);
         ImGui::PopID();
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("RenoDX highlights boost/compress.");
         DrawResetButton(cb_luma_global_settings.GameSettings.CGHighlightsStrength, default_luma_global_game_settings.CGHighlightsStrength, "CGHighlightsStrength", runtime);

         ImGui::PushID("CG: Highlights Mid Gray");
         if (ImGui::SliderFloat("Highlights Mid Gray", &cb_luma_global_settings.GameSettings.CGHighlightsMidGray, 0.f, 500.f))
            reshade::set_config_value(runtime, NAME, "CGHighlightsMidGray", cb_luma_global_settings.GameSettings.CGHighlightsMidGray);
         ImGui::PopID();
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Highlights mid gray / threshold value to manipulate luminance around.");
         DrawResetButton(cb_luma_global_settings.GameSettings.CGHighlightsMidGray, default_luma_global_game_settings.CGHighlightsMidGray, "CGHighlightsMidGray", runtime);

         ImGui::PushID("CG: Shadows");
         if (ImGui::SliderFloat("Shadows", &cb_luma_global_settings.GameSettings.CGShadowsStrength, 0.f, 2.f))
            reshade::set_config_value(runtime, NAME, "CGShadowsStrength", cb_luma_global_settings.GameSettings.CGShadowsStrength);
         ImGui::PopID();
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("RenoDX shadows boost/compress.");
         DrawResetButton(cb_luma_global_settings.GameSettings.CGShadowsStrength, default_luma_global_game_settings.CGShadowsStrength, "CGShadowsStrength", runtime);

         ImGui::PushID("CG: Shadows Mid Gray");
         if (ImGui::SliderFloat("Shadows Mid Gray", &cb_luma_global_settings.GameSettings.CGShadowsMidGray, 0.f, 500.f))
            reshade::set_config_value(runtime, NAME, "CGShadowsMidGray", cb_luma_global_settings.GameSettings.CGShadowsMidGray);
         ImGui::PopID();
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Shadows mid gray / threshold value to manipulate luminance around.");
         DrawResetButton(cb_luma_global_settings.GameSettings.CGShadowsMidGray, default_luma_global_game_settings.CGShadowsMidGray, "CGShadowsMidGray", runtime);

         ImGui::PushID("CG: Saturation");
         if (ImGui::SliderFloat("Saturation", &cb_luma_global_settings.GameSettings.CGSaturation, 0.f, 2.f, "%.4f"))
            reshade::set_config_value(runtime, NAME, "CGSaturation", cb_luma_global_settings.GameSettings.CGSaturation);
         ImGui::PopID();
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Final saturation before HDR tonemap.");
         DrawResetButton(cb_luma_global_settings.GameSettings.CGSaturation, default_luma_global_game_settings.CGSaturation, "CGSaturation", runtime);
      
         if (is_disabled) ImGui::EndDisabled();
      }

      static void NeutralLUT(DeviceData& device_data, reshade::api::effect_runtime* runtime)
      {
         if (IsSDRMode())
         {
            DrawCompletelyUselessButton();
            return;
         }

         ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.75f, 1.f, 1.f, 1.f));
         ImGui::TextWrapped("[Useful for a select few custom maps, neutralize forced extreme blowout and contrast, though unintended.]");
         ImGui::PopStyleColor();
         
         int def = ShaderDefineInfo::UIDropDown(ShaderDefineInfo::CUSTOM_LUTBUILDER_NEUTRAL, "Neutral LUT Builder",
            {"Off (Vanilla)", "If Blown Out (Unintended but more \"HDR\")", "Forced (Debug)"},
            "How should we blend back to neutral color from a custom LUT?\n\nVanilla maps are neutral, but many modded maps loves to load a blown out or overly saturated LUT texture.");
         
         is_disabled = def == 0;
         if (is_disabled) ImGui::BeginDisabled();
         {
            if (ImGui::SliderFloat("Neutral Hue", &cb_luma_global_settings.GameSettings.LUTBuilderNeutralHue, 0.01f, 1.f))
               reshade::set_config_value(runtime, NAME, "LUTBuilderNeutralHue", cb_luma_global_settings.GameSettings.LUTBuilderNeutralHue);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("The strength of blending back hue to neutral color from a custom LUT.\nRaising it will remove embedded color grading tint.");
            DrawResetButton(cb_luma_global_settings.GameSettings.LUTBuilderNeutralHue, default_luma_global_game_settings.LUTBuilderNeutralHue, "LUTBuilderNeutralHue", runtime);
         
            if (ImGui::SliderFloat("Neutral Chrominance", &cb_luma_global_settings.GameSettings.LUTBuilderNeutralChrominance, 0.f, 1.f))
               reshade::set_config_value(runtime, NAME, "LUTBuilderNeutralChrominance", cb_luma_global_settings.GameSettings.LUTBuilderNeutralChrominance);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("The strength of blending back chrominance to neutral color from a custom LUT.\nRaising it will reduce embedded saturation change.\n\nProbably the most controversial settings for custom maps, since some uses heavy blown out LUTs!\nWithout reducing blowout, HDR luminance will be mapped on white, which looks dumb.\nWith reducing blowout, it will peel back the curtains, though it should look better in HDR.");
            DrawResetButton(cb_luma_global_settings.GameSettings.LUTBuilderNeutralChrominance, default_luma_global_game_settings.LUTBuilderNeutralChrominance, "LUTBuilderNeutralChrominance", runtime);

            ShaderDefineInfo::UIDropDown(ShaderDefineInfo::CUSTOM_LUTBUILDER_NEUTRAL_LUMA, "Neutral Luminance Blend",
               {"High Passed (Only Blown Out)", "Forced"},
               "How should we blend back to neutral luminance from a custom LUT?\n\nVanilla maps are neutral, but many modded maps loves to add boosted to sometimes clipping highlights.");
            
            if (ImGui::SliderFloat("Neutral Luminance", &cb_luma_global_settings.GameSettings.LUTBuilderNeutralLuma, 0.f, 1.f))
               reshade::set_config_value(runtime, NAME, "LUTBuilderNeutralLuma", cb_luma_global_settings.GameSettings.LUTBuilderNeutralLuma);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("The strength of blending back luminance to neutral color from a custom LUT.\n\nRaising it will reduce embedded contrast curves.");
            DrawResetButton(cb_luma_global_settings.GameSettings.LUTBuilderNeutralLuma, default_luma_global_game_settings.LUTBuilderNeutralLuma, "LUTBuilderNeutralLuma", runtime);

            // is_disabled = ShaderDefineInfo::Get(ShaderDefineInfo::CUSTOM_LUTBUILDER_NEUTRAL) == 0 || ShaderDefineInfo::Get(ShaderDefineInfo::CUSTOM_LUTBUILDER_NEUTRAL_LUMA) != 0;
            // if (is_disabled) ImGui::BeginDisabled();
            // {
            //    if (ImGui::SliderFloat("Neutral Luminance Start", &cb_luma_global_settings.GameSettings.LUTBuilderNeutralLumaHPStart, 0.f, 1.f))
            //       reshade::set_config_value(runtime, NAME, "LUTBuilderNeutralLumaHPStart", cb_luma_global_settings.GameSettings.LUTBuilderNeutralLumaHPStart);
            //    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Luminance lower than this will be ignored by neutralization.");
            //    DrawResetButton(cb_luma_global_settings.GameSettings.LUTBuilderNeutralLumaHPStart, default_luma_global_game_settings.LUTBuilderNeutralLumaHPStart, "LUTBuilderNeutralLumaHPStart", runtime);
            // }
            // if (is_disabled) ImGui::EndDisabled();
         }
         if (is_disabled) ImGui::EndDisabled();

         if (!is_disabled && cb_luma_global_settings.GameSettings.LUTBuilderNeutralLuma == 1.f && cb_luma_global_settings.GameSettings.LUTBuilderNeutralHue == 1.f && cb_luma_global_settings.GameSettings.LUTBuilderNeutralChrominance == 1.f)
         {
            ImGui::BulletText("Full Neutral Mode Active.");
         }
      }
      
      static void BypassVanillaColorGrade(DeviceData& device_data, reshade::api::effect_runtime* runtime)
      {
         if (IsSDRMode())
         {
            DrawCompletelyUselessButton();
            return;
         }

         ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.75f, 1.f, 1.f, 1.f));
         ImGui::TextWrapped("[For the curious, bypass vanilla color grading in the LUT builder.]");
         ImGui::PopStyleColor();

         auto def = ShaderDefineInfo::UIToggleCheckmark(ShaderDefineInfo::CUSTOM_LUTBUILDER_VANILLA, "LUT Builder Vanilla Grade",
            "Track to allow interpolating between ungraded and graded color.");
         
         is_disabled = def == 0;
         if (is_disabled) ImGui::BeginDisabled();
         {
            if (ImGui::SliderFloat("Vanilla Shadows/Midtones/Highlights", &cb_luma_global_settings.GameSettings.LUTBuilderGradeSMH, 0.f, 1.f))
               reshade::set_config_value(runtime, NAME, "LUTBuilderGradeSMH", cb_luma_global_settings.GameSettings.LUTBuilderGradeSMH);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Strength of vanilla shadows/midtones/highlights grading in LUT builder.\nSome maps like Shangri-La can benefit with this reduced a bit.");
            DrawResetButton(cb_luma_global_settings.GameSettings.LUTBuilderGradeSMH, default_luma_global_game_settings.LUTBuilderGradeSMH, "LUTBuilderGradeSMH", runtime);
      
            if (ImGui::SliderFloat("Vanilla Tint", &cb_luma_global_settings.GameSettings.LUTBuilderGradeTint, 0.f, 1.f))
               reshade::set_config_value(runtime, NAME, "LUTBuilderGradeTint", cb_luma_global_settings.GameSettings.LUTBuilderGradeTint);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Strength of vanilla luma based tint.\ne.g. SoE Beast Mode's green tint and Ascension no power desaturation.");
            DrawResetButton(cb_luma_global_settings.GameSettings.LUTBuilderGradeTint, default_luma_global_game_settings.LUTBuilderGradeTint, "LUTBuilderGradeTint", runtime);
      
            if (ImGui::SliderFloat("Vanilla Saturation", &cb_luma_global_settings.GameSettings.LUTBuilderGradeSat, 0.f, 1.f))
               reshade::set_config_value(runtime, NAME, "LUTBuilderGradeSat", cb_luma_global_settings.GameSettings.LUTBuilderGradeSat);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Strength of vanilla de/saturation.\nSeldom usage, idk.");
            DrawResetButton(cb_luma_global_settings.GameSettings.LUTBuilderGradeSat, default_luma_global_game_settings.LUTBuilderGradeSat, "LUTBuilderGradeSat", runtime);
         }
         if (is_disabled) ImGui::EndDisabled();
      }

      static void FakeWCG(DeviceData& device_data, reshade::api::effect_runtime* runtime)
      {
         if (IsSDRMode())
         {
            DrawCompletelyUselessButton();
            return;
         }
         
         ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.75f, 1.f, 1.f, 1.f));
         ImGui::TextWrapped("[Do some fake boost tomfoolery to push colors out wide.]");
         ImGui::PopStyleColor();

         bool is_on = ShaderDefineInfo::UIToggleCheckmark(ShaderDefineInfo::CUSTOM_LUTBUILDER_COLORSPACE, "LUT Builder BT2020",
            "Make the LUT Builder (and other stuff after) work in wider BT2020 to allowing for wider colors from chrominance boosts.");

         bool is_on1 = is_on;
         if (!is_on1) ImGui::BeginDisabled(); 
         is_on &= ShaderDefineInfo::UIToggleCheckmark(ShaderDefineInfo::CUSTOM_LUTBUILDER_SATBOOST, "LUT Builder FakeBT2020 Gamut Expansion",
            "A gamma utilizing gamut expansion.");
         if (!is_on1) ImGui::EndDisabled();
         
         is_disabled = !is_on;
         if (is_disabled) ImGui::BeginDisabled();
         {
            if (ImGui::SliderFloat("Expansion Chrominance", &cb_luma_global_settings.GameSettings.LUTBuilderExpansionChrominance, 0.f, 1.f))
               reshade::set_config_value(runtime, NAME, "LUTBuilderExpansionChrominance", cb_luma_global_settings.GameSettings.LUTBuilderExpansionChrominance);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Already saturated colors will be boosted more.");
            DrawResetButton(cb_luma_global_settings.GameSettings.LUTBuilderExpansionChrominance, default_luma_global_game_settings.LUTBuilderExpansionChrominance, "LUTBuilderExpansionChrominance", runtime);

            if (ImGui::SliderFloat("Expansion Luminance", &cb_luma_global_settings.GameSettings.LUTBuilderExpansionLuminance, 0.f, 1.f))
               reshade::set_config_value(runtime, NAME, "LUTBuilderExpansionLuminance", cb_luma_global_settings.GameSettings.LUTBuilderExpansionLuminance);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Gamut expansion will deepen colors.\nDecrease if too metallic feeling.");
            DrawResetButton(cb_luma_global_settings.GameSettings.LUTBuilderExpansionLuminance, default_luma_global_game_settings.LUTBuilderExpansionLuminance, "LUTBuilderExpansionLuminance", runtime);
         
            if (ImGui::SliderFloat("Expansion Highlight Saturation", &cb_luma_global_settings.GameSettings.LUTBuilderHighlightSat, 0.f, 0.5f, "%.4f"))
               reshade::set_config_value(runtime, NAME, "LUTBuilderHighlightSat", cb_luma_global_settings.GameSettings.LUTBuilderHighlightSat);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Boost saturation for LUT highlights.");
            DrawResetButton(cb_luma_global_settings.GameSettings.LUTBuilderHighlightSat, default_luma_global_game_settings.LUTBuilderHighlightSat, "LUTBuilderHighlightSat", runtime);

            if (ImGui::SliderFloat("Expansion Highlight Saturation High Pass", &cb_luma_global_settings.GameSettings.LUTBuilderHighlightSatHighlightsOnly, 1.f, 5.f))
               reshade::set_config_value(runtime, NAME, "LUTBuilderHighlightSatHighlightsOnly", cb_luma_global_settings.GameSettings.LUTBuilderHighlightSatHighlightsOnly);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Increase to target highlights only.");
            DrawResetButton(cb_luma_global_settings.GameSettings.LUTBuilderHighlightSatHighlightsOnly, default_luma_global_game_settings.LUTBuilderHighlightSatHighlightsOnly, "LUTBuilderHighlightSatHighlightsOnly", runtime);
         }
         if (is_disabled) ImGui::EndDisabled();
      }

      static void PerChannelCorrection(DeviceData& device_data, reshade::api::effect_runtime* runtime)
      {
         if (IsSDRMode())
         {
            DrawCompletelyUselessButton();
            return;
         }
         
         ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.75f, 1.f, 1.f, 1.f));
         ImGui::TextWrapped("[Extend the SDR rolloff to a new peak to steal its color for highlights recovery.]");
         ImGui::PopStyleColor();

         bool is_on = ShaderDefineInfo::UIToggleCheckmark(ShaderDefineInfo::CUSTOM_PCC, "Per-Channel Correction",
            "Use an extended per-channel tonemap curve with less blowout and blend its color onto the vanilla SDR tonemap.");

         if (ImGui::Button("Nerd Explanation (Desmos Graph)")) Website::OpenWebsite("https://www.desmos.com/calculator/lk9bpn4wkz");
         
         is_disabled = !is_on;
         if (is_disabled) ImGui::BeginDisabled();
         {
            // ImGui::PushID("PCC: Peak");    
            // if (ImGui::SliderFloat("Peak", &cb_luma_global_settings.GameSettings.PCCPeak, 1.f, 8.f, "%.4f"))
            //    reshade::set_config_value(runtime, NAME, "PCCPeak", cb_luma_global_settings.GameSettings.PCCPeak);
            // ImGui::PopID();
            // if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("The peak of the internal per-channel tonemap.\nIncrease to allow even more chrominance in highlights, though it may look unintended/unnatural.");
            // DrawResetButton(cb_luma_global_settings.GameSettings.PCCPeak, default_luma_global_game_settings.PCCPeak, "PCCPeak", runtime);
            //
            // ImGui::NewLine(); //////////

            ImGui::PushID("PCC: Rolloff Type");
            int def = ShaderDefineInfo::UIDropDown(ShaderDefineInfo::CUSTOM_PCC_ROLLOFF, "Rolloff Type",
               {"Reinhard (Gradual)", "NeuTwo (Straighter)"},
               "The rolloff's extended shoulder replacement.");
            ImGui::PopID();
            
            ImGui::PushID("PCC: Hue Influence");
            if (ImGui::SliderFloat("Hue Influence", &cb_luma_global_settings.GameSettings.PCCHue, 0.f, 1.f, "%.4f"))
               reshade::set_config_value(runtime, NAME, "PCCHue", cb_luma_global_settings.GameSettings.PCCHue);
            ImGui::PopID();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Effect on of less blown out color on hue.\nSetting it too high will make stuff like fire red/orange.");
            DrawResetButton(cb_luma_global_settings.GameSettings.PCCHue, default_luma_global_game_settings.PCCHue, "PCCHue", runtime);

            ImGui::PushID("PCC: Chrominance Influence");
            if (ImGui::SliderFloat("Chrominance Influence", &cb_luma_global_settings.GameSettings.PCCChrominance, 0.f, 1.f, "%.4f"))
               reshade::set_config_value(runtime, NAME, "PCCChrominance", cb_luma_global_settings.GameSettings.PCCChrominance);
            ImGui::PopID();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Effect on of less blown out color on chrominance/saturation.\nSetting it too may color things that's supposed to be blown out (though it should be fine in this game).");
            DrawResetButton(cb_luma_global_settings.GameSettings.PCCChrominance, default_luma_global_game_settings.PCCChrominance, "PCCChrominance", runtime);
         }
         if (is_disabled) ImGui::EndDisabled();
      }
   }
   
   // section struct
   using SectionFunc = std::function<void(DeviceData&, reshade::api::effect_runtime*)>;
   struct SectionEntry { const char* name; SectionFunc draw; }; 

   // list of sections
   static inline std::vector<SectionEntry> sections =
   {
      {"README (Scroll Me!)",                       [](DeviceData& d, reshade::api::effect_runtime* r){Templates::README(d, r); }},
      {"Gamma Correction",                          [](DeviceData& d, reshade::api::effect_runtime* r){Templates::GammaCorrection(d, r); }},
      {"Brightness",                                [](DeviceData& d, reshade::api::effect_runtime* r){Templates::Brightness(d, r); }},
      {"Black Floor Lower",                         [](DeviceData& d, reshade::api::effect_runtime* r){Templates::BlackFloorLower(d, r); }},
#if ENABLE_SR == 1
      {"Super Resolution",                          [](DeviceData& d, reshade::api::effect_runtime* r){Templates::SuperResolution(d, r); }},
      {"Forced LOD Bias",                           [](DeviceData& d, reshade::api::effect_runtime* r){Templates::ForcedLODBias(d, r); }},
#endif
      {"Miscellaneous",                             [](DeviceData& d, reshade::api::effect_runtime* r){Templates::PostProcessing(d, r); }},
      {"Simpler Advanced Settings",                 [](DeviceData& d, reshade::api::effect_runtime* r){Templates::SimpleAdvancedSettings(d, r); }},
      
      {"(Advanced) Luminance Extension",            [](DeviceData& d, reshade::api::effect_runtime* r){Templates::HDRTonemapping(d, r); }},
      {"(Advanced) RenoDX Color Grading",           [](DeviceData& d, reshade::api::effect_runtime* r){Templates::RenoDXColorGrade(d, r); }},
      {"(Advanced) Force Neutral LUT",              [](DeviceData& d, reshade::api::effect_runtime* r){Templates::NeutralLUT(d, r); }},
      {"(Advanced) Bypass Color Grade",             [](DeviceData& d, reshade::api::effect_runtime* r){Templates::BypassVanillaColorGrade(d, r); }},
      {"(Advanced) Fake Wide Color Gamut",          [](DeviceData& d, reshade::api::effect_runtime* r){Templates::FakeWCG(d, r); }},
      {"(Advanced) Chrominance Extension",          [](DeviceData& d, reshade::api::effect_runtime* r){Templates::PerChannelCorrection(d, r); }},
   };
   static int sections_adv_start = 0;
   
   //current section index
   static int index = 0;

   static void OnDrawContents(int i, DeviceData& device_data, reshade::api::effect_runtime* runtime)
   {
      if (i >= 0 && i < static_cast<int>(sections.size()) && sections[i].draw) //range & null check
         sections[i].draw(device_data, runtime);
   }

   static bool IndexGetMin() {return index <= 0;}
   static bool IndexGetMax() {return index >= static_cast<int>(sections.size()) - 1;}
   static void IndexSaveConfig(reshade::api::effect_runtime* runtime) {reshade::set_config_value(runtime, NAME, "SectionedImGuiIndex", index);}
   static void IndexLoadConfig(reshade::api::effect_runtime* runtime) {reshade::get_config_value(runtime, NAME, "SectionedImGuiIndex", index);}

   static void OnLoadConfigs(reshade::api::effect_runtime* runtime)
   {
      IndexLoadConfig(runtime);

      // find when sections_adv_start
      for (int i = 0; i < static_cast<int>(sections.size()); ++i)
      {
         if (std::string(sections[i].name).find("(Advanced)") != std::string::npos)
         {
            sections_adv_start = i;
            break;
         }
      }
   }
   
   static void OnDraw(reshade::api::effect_runtime* runtime, DeviceData& device_data)
   {
      // FORCED SETTINGS
      {
         //DLSSJitter OnUIForced();
         #if ENABLE_SR == 1
            DLSSJitter::OnUIForced();
         #endif

         #if ENABLE_SR == 1
         //CUSTOM_SR
         {
            bool isOnSetting = device_data.sr_type != SR::Type::None;
            bool isOnDef = ShaderDefineInfo::Get(ShaderDefineInfo::CUSTOM_SR) > 0;
            if (isOnSetting != isOnDef)
            {
               char def_char = isOnSetting ? '1' : '0';
               ShaderDefineInfo::Set(ShaderDefineInfo::CUSTOM_SR, def_char);
         
               //force reset
               if (isOnSetting) device_data.force_reset_sr = true;
            }
         }
         #else
            ShaderDefineInfo::Set(ShaderDefineInfo::CUSTOM_SR, 0);
         #endif
      
         //CUSTOM_SDR
         {
            bool isOnSetting = cb_luma_global_settings.DisplayMode == DisplayModeType::SDR;
            bool isOnDef = ShaderDefineInfo::Get(ShaderDefineInfo::CUSTOM_SDR) > 0;
            if (isOnSetting != isOnDef)
            {
               char def_char = isOnSetting ? '1' : '0';
               ShaderDefineInfo::Set(ShaderDefineInfo::CUSTOM_SDR, def_char);
            }
         }

         //force no gamma correct in SDR
         if (cb_luma_global_settings.DisplayMode == DisplayModeType::SDR && ShaderDefineInfo::Get(GAMMA_CORRECTION_TYPE_HASH) == 1)
         {
            custom_sdr_gamma = 0.f;
            ShaderDefineInfo::Set(GAMMA_CORRECTION_TYPE_HASH, 0);
         }
      }

      /////////////////////////////////////////////////////////////////////////////////////
      
      // index setup
      int index_prev = index;
      int index_max = !Globals::UIIsAdvanced ? sections_adv_start - 1 : static_cast<int>(sections.size()) - 1;
      index = std::clamp(index, 0, index_max);
      
      // <-- button
      if (IndexGetMin()) ImGui::BeginDisabled();
      ImGui::PushID("SectionedImGui_Left");
      if (ImGui::ArrowButton("##left", ImGuiDir_Left)) index--;
      ImGui::PopID();
      if (IndexGetMin()) ImGui::EndDisabled();

      // current section dropdown
      ImGui::SameLine();
      ImGui::PushID("SectionedImGui_Combo");
      {
         const std::vector<const char*> section_names = [](){
            std::vector<const char*> names;
            for (int i = 0; i < static_cast<int>(sections.size()); ++i)
            {
               if (i == sections_adv_start && !Globals::UIIsAdvanced) break; 
               names.push_back(sections[i].name);
            }
            return names;
         }();
         ImGui::Combo("##SectionedImGui_Combo", &index, section_names.data(), static_cast<int>(section_names.size()));
         if (ImGui::IsItemHovered())
         {
            const float wheel = ImGui::GetIO().MouseWheel;
            if (wheel > 0.f) index--;
            else if (wheel < 0.f) index++;
            index = std::clamp(index, 0, static_cast<int>(sections.size()) - 1);
         }
      }
      ImGui::PopID();
      
      // --> button
      ImGui::SameLine();
      if (/*IndexGetMax()*/ index >= index_max) ImGui::BeginDisabled();
      ImGui::PushID("SectionedImGui_Right");
      if (ImGui::ArrowButton("##right", ImGuiDir_Right)) index++;
      ImGui::PopID();
      if (/*IndexGetMax()*/ index >= index_max) ImGui::EndDisabled();

      //index setup
      index = std::clamp(index, 0, index_max);

      // Contents with bevel border
      ImGui::Spacing();
      ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.25f));
      ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
      ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
      ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 8.0f));
      ImGui::BeginChild("##SectionContents", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders);
      ImGui::PushItemWidth(512); 
      OnDrawContents(index, device_data, runtime); // Draw!
      ImGui::PopItemWidth();
      ImGui::EndChild();
      {const ImVec2 min = ImGui::GetItemRectMin();
         const ImVec2 max = ImGui::GetItemRectMax();
         ImDrawList* dl   = ImGui::GetWindowDrawList();
         constexpr float r = 4.0f;
         dl->AddRect(min, max,
            IM_COL32(0, 0, 0, 180), r, 0, 2.f);
         dl->AddRect({min.x + 1.f, min.y + 1.f}, {max.x - 1.f, max.y - 1.f},
            IM_COL32(255, 255, 255, 25), r);
      }
      ImGui::PopStyleVar(2);
      ImGui::PopStyleColor(2);

      // Save index
      if (index_prev != index) IndexSaveConfig(runtime);

      // Advanced Settings Toggle
      if (ImGui::Checkbox("Advanced Settings", &Globals::UIIsAdvanced))
         reshade::set_config_value(runtime, NAME, "UIIsAdvanced", Globals::UIIsAdvanced);
   }
}

class CallOfDutyBlackOps3 final : public Game
{
public:
   void OnInit(bool async) override
   {
      //log
      message(reshade::log::level::info, "OnInit()");
      
      //Def
      std::vector<ShaderDefineData> game_shader_defines_data = {
         {"GAMMA_CORRECTION_RANGE_TYPE", '0', true, true, "0 - Full range.\n1 - 0-1 only.", 1},
         {"SWAPCHAIN_CLAMP_PEAK", '1', true, false, "Final color clamp before present.\n0 - Unclamped (up to display).\n1 - Per channel clamp (blows out).\n2 - Scale down by max channel (sat preserving).", 2},
         {"SWAPCHAIN_CLAMP_COLORSPACE", '0', true, false, "Clamp colorspace against invalid colors.\n(Really only for OCD, as it should only be inconsequential black from RCAS.)\n0 - Unclamped.\n1 - BT2020.", 1},
         {"SWAPCHAIN_TEST_USER_PEAK", '0', true, false, "Show a simple white rectangle peak test.", 1},
         {"CUSTOM_GAMMA_CORRECTION_MODE", '1', true, true, "0 - Per-Channel.\n1 - Perceptual.", 1},
         {"CUSTOM_HDTVREC709", '0', true, false, "Decode color and swapchain to HDTV (rec.709) setting.", 1},
         {"CUSTOM_TONEMAP", '2', true, false, "HDR tonemapper, primarily the shoulder.\n0 - Off (Unclamped).\n1 - Reinhard Piecewise\n2 - Hermite Spline", 2},
         {"CUSTOM_TONEMAP_SCALING", '0', true, false, "HDR tonemap scaling.\n0 - Luminance (natural)\n1 - Max-Channel (saturation preserve)", 1},
         {"CUSTOM_TONEMAP_CLAMP", '0', true, false, "Clamp overshoot from luma scaled HDR tonemap.\n0 - Unclamped (up to display).\n1 - Per channel clamp (blows out).\n2 - Scale down by max channel (sat preserving).", 2},
         {"CUSTOM_FINAL_CLAMP", '1', true, false, "Clamp overshoot from luma scaled HDR tonemap.\n0 - Unclamped (up to display).\n1 - Per channel clamp (blows out).\n2 - Scale down by max channel (sat preserving).", 2},
         {"CUSTOM_RCAS", '0', true, false, "Enable Robust (4 instead of 8 samples) Contrast Adaptive Sharpening.", 1},
         {"RCAS_DENOISE", '0', true, false, "Denoising sharpening.\nMore smooth, but helps against dither pattern of shadows and AO.", 1},
         {"RCAS_LUMINANCE_BASED", '0', true, false, "Luma based sharpening. Apparently meh.", 1},
         {"CUSTOM_LUTBUILDER_COLORSPACE", '0', true, false, "Change the LUT output (and other things after) to BT.2020 primaries for wider color from upgraded post processing reach final.", 1},
         {"CUSTOM_LUTBUILDER_VANILLA", '0', true, false, "Allow changing the strength of vanilla grading effects.", 1},
         {"CUSTOM_LUTBUILDER_SATBOOST", '1', true, false, "Boost LUT chrominance by doing some goofy perceptual color space hacks.", 1},
         {"CUSTOM_LUTBUILDER_NEUTRAL", '0', true, false, "If LUT texture color is desaturated, blend to neutral color.\n0 - Off.\n1 - If desaturated.\n2 - Forcefully.", 2 },
         {"CUSTOM_LUTBUILDER_NEUTRAL_LUMA", '0', true, false, "Neutral blend type.\n0 - High passed.\n1 - Forcefully.", 2 },
         {"CUSTOM_PCC", '1', true, false, "Do per channel correction on SDR tonemapped colors to reduce blowout.", 1},
         {"CUSTOM_PCC_ROLLOFF", '0', true, false, "The shoulder replacement.", 1},
         {"CUSTOM_UPGRADE_DEBUG", '0', true, false, "0 - Final upgraded HDR color.\n1 - Raw HDR color.\n2 - Neutral/Baseline SDR color before grading.\n3 - Vanilla Ungraded SDR color.\n4 - Vanilla Graded SDR color.\n5 - (Development Version Only) Select by DV10", DEVELOPMENT == 1 ? 5 : 4},
         {"CUSTOM_UCS_TYPE", '2', true, false, "Working perceptual/uniform color space.\nUsed to perceptually blend between colors, each gives slightly different hues and chrominance.\n\n0 - JzAzBz\n1 - OkLAB\n2 - ICtCp", 2},
         {"CUSTOM_COLORGRADE", '0', true, false, "Enable Color Grading right before HDR tonemapping.", 1},
         {"CUSTOM_UPSCALE_MOV", '0', true, false, "Auto HDR for movies.", 1},
         {"CUSTOM_CHROMABER", '1', true, false, "Allow chromatic aberration.\nSeldom usage, only on the main menu.", 1},
         {"CUSTOM_MB_QUALITY", '0', true, false, "Motion blur sample count (pairs of forward and reverse).\n0 - 6 (Original)\n1 - 16\n2 - 24 (High)\n3 - 32\n4 - 48\n5 - 64\n6 - 128 (Uhhh)", 6},
         {"CUSTOM_BLACKFLOOR_LUT", '0', true, false, "Fix raised floor from LUT?", 2},
         {"CUSTOM_PERCHANNELLUMAEMULATE", '0', true, false, "Emulate how a per-channel tonemapper lowers luminance on bright single channel colors?", 2},
         {"CUSTOM_BLOOM_TONEMAP", '0', true, false, "Bloom final tonemapper after downsample aggregate.", 1},
         {"CUSTOM_SR", ENABLE_SR == 1 ? '1' : '0', true, false, "(AUTOMATICALLY HANDLED) Recompiles SMAA T2X (not filmic) according to SR type.", 1},
         {"CUSTOM_SDR", '0', true, false, "(AUTOMATICALLY HANDLED) Turn off FSFX HDR tradeoff encoding stuff.", 1},
         {"CUSTOM_LUMA", '1', true, true, "Denotes shaders to compile for Luma.", 1},
      };
      shader_defines_data.append_range(game_shader_defines_data);
      assert(shader_defines_data.size() < MAX_SHADER_DEFINES);
      auto_recompile_defines = true; //force
      
      //Default built-in
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

#if ENABLE_SR == 1
      //native_shaders_definitions
      native_shaders_definitions.emplace(CompileTimeStringHash("CoDBO3 PreSR"), ShaderDefinition{"Luma_CoDBO3_PreSR", reshade::api::pipeline_subobject_type::pixel_shader});
      native_shaders_definitions.emplace(CompileTimeStringHash("CoDBO3 LinearizeVelocity"), ShaderDefinition{"Luma_CoDBO3_LinearizeVelocity", reshade::api::pipeline_subobject_type::pixel_shader});
#endif

      // Native Shaders: Display Composition replacement
      native_shaders_definitions.erase(CompileTimeStringHash("Display Composition"));
      native_shaders_definitions.emplace(CompileTimeStringHash("Display Composition"), ShaderDefinition{"Luma_CoDBO3_DisplayComposition", reshade::api::pipeline_subobject_type::pixel_shader});
      
      //cb
      luma_settings_cbuffer_index = 13;
      luma_data_cbuffer_index = 12;
      luma_ui_cbuffer_index = -1; 

      //GameSettings default
      default_luma_global_game_settings.TonemapperRolloffStart = cb_luma_global_settings.GameSettings.TonemapperRolloffStart = 36.f;
      default_luma_global_game_settings.TonemapperMaxExpected = cb_luma_global_settings.GameSettings.TonemapperMaxExpected = 50000.f;
      default_luma_global_game_settings.Bloom = cb_luma_global_settings.GameSettings.Bloom = 1.f;
      default_luma_global_game_settings.LensFlare = cb_luma_global_settings.GameSettings.LensFlare = 1.f;
      default_luma_global_game_settings.SlideLensDirt = cb_luma_global_settings.GameSettings.SlideLensDirt = 1.f;
      default_luma_global_game_settings.ADSSights = cb_luma_global_settings.GameSettings.ADSSights = 1.0f;
      default_luma_global_game_settings.XrayOutline = cb_luma_global_settings.GameSettings.XrayOutline = 1.f;
      default_luma_global_game_settings.MotionBlur = cb_luma_global_settings.GameSettings.MotionBlur = 1.f;
      default_luma_global_game_settings.VolumetricFog = cb_luma_global_settings.GameSettings.VolumetricFog = 1.f;
      default_luma_global_game_settings.RCAS = cb_luma_global_settings.GameSettings.RCAS = 0.f;
      default_luma_global_game_settings.LUTBuilderExpansionChrominance = cb_luma_global_settings.GameSettings.LUTBuilderExpansionChrominance = 0.115f;
      default_luma_global_game_settings.LUTBuilderExpansionLuminance = cb_luma_global_settings.GameSettings.LUTBuilderExpansionLuminance = 0.1f;
      default_luma_global_game_settings.LUTBuilderHighlightSat = cb_luma_global_settings.GameSettings.LUTBuilderHighlightSat = 0.055f;
      default_luma_global_game_settings.LUTBuilderHighlightSatHighlightsOnly = cb_luma_global_settings.GameSettings.LUTBuilderHighlightSatHighlightsOnly = 3.2f;
      default_luma_global_game_settings.LUTBuilderNeutralHue = cb_luma_global_settings.GameSettings.LUTBuilderNeutralHue = 0.15f;
      default_luma_global_game_settings.LUTBuilderNeutralChrominance = cb_luma_global_settings.GameSettings.LUTBuilderNeutralChrominance = 0.2f;
      default_luma_global_game_settings.LUTBuilderNeutralLuma = cb_luma_global_settings.GameSettings.LUTBuilderNeutralLuma = 1.0f;
      // default_luma_global_game_settings.LUTBuilderNeutralLumaHPStart = cb_luma_global_settings.GameSettings.LUTBuilderNeutralLumaHPStart = 0.36f;
      default_luma_global_game_settings.LUTBuilderGradeSMH = cb_luma_global_settings.GameSettings.LUTBuilderGradeSMH = 1.0f;
      default_luma_global_game_settings.LUTBuilderGradeTint = cb_luma_global_settings.GameSettings.LUTBuilderGradeTint = 1.0f;
      default_luma_global_game_settings.LUTBuilderGradeSat = cb_luma_global_settings.GameSettings.LUTBuilderGradeSat = 1.0f;
      // default_luma_global_game_settings.PCCLookback = cb_luma_global_settings.GameSettings.PCCLookback = 0.235f;
      default_luma_global_game_settings.PCCHue = cb_luma_global_settings.GameSettings.PCCHue = 0.475f;
      default_luma_global_game_settings.PCCChrominance = cb_luma_global_settings.GameSettings.PCCChrominance = 0.8f;
      // default_luma_global_game_settings.PCCPeak = cb_luma_global_settings.GameSettings.PCCPeak = 5.0f;
      // default_luma_global_game_settings.PCCChrominanceBoost = cb_luma_global_settings.GameSettings.PCCChrominanceBoost = 1.125f;
      // default_luma_global_game_settings.PCCGuaranteed = cb_luma_global_settings.GameSettings.PCCGuaranteed = 0.25f;
      default_luma_global_game_settings.BlackFloorSDRTonemap = cb_luma_global_settings.GameSettings.BlackFloorSDRTonemap = 1.0f; //this is precalced, where 1 is disabled
      default_luma_global_game_settings.BlackFloorLUT = cb_luma_global_settings.GameSettings.BlackFloorLUT = 0.0f;
      default_luma_global_game_settings.CGContrast = cb_luma_global_settings.GameSettings.CGContrast = 1.f;
      default_luma_global_game_settings.CGContrastMidGray = cb_luma_global_settings.GameSettings.CGContrastMidGray = 36.f;
      default_luma_global_game_settings.CGSaturation = cb_luma_global_settings.GameSettings.CGSaturation = 1.f;
      default_luma_global_game_settings.CGHighlightsStrength = cb_luma_global_settings.GameSettings.CGHighlightsStrength = 1.f;
      default_luma_global_game_settings.CGHighlightsMidGray = cb_luma_global_settings.GameSettings.CGHighlightsMidGray = 36.f;
      default_luma_global_game_settings.CGShadowsStrength = cb_luma_global_settings.GameSettings.CGShadowsStrength = 1.f;
      default_luma_global_game_settings.CGShadowsMidGray = cb_luma_global_settings.GameSettings.CGShadowsMidGray = 36.f;
      default_luma_global_game_settings.Exposure = cb_luma_global_settings.GameSettings.Exposure = 1.f;
      default_luma_global_game_settings.GammaInfluence = cb_luma_global_settings.GameSettings.GammaInfluence = 1.f;
      default_luma_global_game_settings.GammaPerceptualChrominanceCorrect = cb_luma_global_settings.GameSettings.GammaPerceptualChrominanceCorrect = 0.21f;
      default_luma_global_game_settings.MovPeakRatio = cb_luma_global_settings.GameSettings.MovPeakRatio = 1.f;
      default_luma_global_game_settings.MovShoulderPow = cb_luma_global_settings.GameSettings.MovShoulderPow = 3.6f;
      
      //MemoryHack
#if ENABLE_SR == 1
      MemoryHack::OnInit();
      DLSSJitter::OnInit(MemoryHack::exe_type);
      ForcedLODBias::OnInit(MemoryHack::exe_type);
#endif

      //delete blackops3.start
      {
         std::error_code ec;
         std::filesystem::remove("blackops3.start", ec);
      }

      //log Globals::SDR8Bit
      if (Globals::SDR8Bit) message(reshade::log::level::info, "OnInit(): SDR8Bit is enabled.");
   }

   // void OnLoad(std::filesystem::path& file_path, bool failed) override
   // {
   //    //memory searching stuff here.
   // }

   // // On Shader Define Changed
   // void OnShaderDefinesChanged()
   // {
   //
   // }

   // This needs to be overridden with your own "GameDeviceData" sub-class (destruction is automatically handled)
   void OnCreateDevice(ID3D11Device* native_device, DeviceData& device_data) override
   {
      //log
      message(reshade::log::level::info, "OnCreateDevice()");
      
      //device_data.game
      device_data.game = new CallOfDutyBlackOps3GameDeviceData;
   }

   void OnInitSwapchain(reshade::api::swapchain* swapchain) override
   {
      //log
      message(reshade::log::level::info, "OnInitSwapchain()");
      
      auto& device_data = *swapchain->get_device()->get_private_data<DeviceData>();
      auto& game_device_data = CallOfDutyBlackOps3GameDeviceData::GetGameDeviceData(device_data);
#if DEVELOPMENT
      Globals::CountSwapchainChange++;
#endif

#if ENABLE_SR == 1
      //force reset DLSS
      device_data.force_reset_sr = true;
      game_device_data.HardResetSR();
#endif
      
      // device_data.cb_luma_global_settings_dirty = true;
      // defines_need_recompilation = true;
   }

   // std::unique_ptr<std::byte[]> ModifyShaderByteCode(const std::byte* code, size_t& size, reshade::api::pipeline_subobject_type type, uint64_t shader_hash, const std::byte* shader_object, size_t shader_object_size) override
   // {
   //    //gatekeep: not ps & not 0x1B4B234D
   //    if (type != reshade::api::pipeline_subobject_type::pixel_shader || shader_hash != static_cast<uint64_t>(0x1B4B234D))
   //       return nullptr;
   //    message(reshade::log::level::info, "ModifyShaderByteCode(): In!");
   //
   //    std::unique_ptr<std::byte[]> new_code = nullptr;
   //
   //    /*
   //       2585  0x00012000: mul r0.xyz, r1.xyzx, cb1[85].yyyy
   //       2586  0x00012020: ge r1.xyz, r0.xyzx, l(0.000061, 0.000061, 0.000061, 0.000000)
   //       2587  0x00012048: and r0.xyz, r0.xyzx, r1.xyzx
   //       2588  0x00012064: min o0.xyz, r0.xyzx, l(65024.000000, 65024.000000, 64512.000000, 0.000000)
   //       2589  0x0001208C: ret 
   //     */
   //    //append max o0.xyz o0.xyz l(0.0, 0.0, 0.0, 0.0) before the return
   //    std::vector<uint8_t> patch = {
   //       0x34, 0x00, 0x00, 0x0A,  // max, length=10
   //       0x72, 0x20, 0x10, 0x00,  // dest: o0.xyz (output, mask xyz)
   //       0x00, 0x00, 0x00, 0x00,  // register index 0
   //       0x46, 0x22, 0x10, 0x00,  // src1: o0.xyzx (output, swizzle xyzx)
   //       0x00, 0x00, 0x00, 0x00,  // register index 0
   //       0x46, 0x4E, 0x00, 0x00,  // src2: l() immediate32, swizzle xyzw
   //       0x00, 0x00, 0x00, 0x00,  // 0.0f
   //       0x00, 0x00, 0x00, 0x00,  // 0.0f
   //       0x00, 0x00, 0x00, 0x00,  // 0.0f
   //       0x00, 0x00, 0x00, 0x00,  // 0.0f
   //    };
   //    
   //    size_t insert_byte_pos = size - sizeof(uint32_t); //ret is always last, always 1 dword
   //    size_t new_size = size + patch.size();
   //
   //    new_code = std::make_unique<std::byte[]>(new_size);
   //
   //    std::memcpy(new_code.get(), code, insert_byte_pos);
   //    std::memcpy(new_code.get() + insert_byte_pos, patch.data(), patch.size());
   //    std::memcpy(new_code.get() + insert_byte_pos + patch.size(), code + insert_byte_pos, sizeof(uint32_t));
   //
   //    size = new_size;
   //    message(reshade::log::level::info, "ModifyShaderByteCode(): Success!");
   //
   //    return new_code;
   // }

#if ENABLE_SR == 1
   DrawOrDispatchOverrideType OnDrawOrDispatch(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, reshade::api::shader_stage stages, const ShaderHashesList<OneShaderPerPipeline>&  original_shader_hashes, bool is_custom_pass, bool& updated_cbuffers, std::function<void()>* original_draw_dispatch_func) override
   {
#if DEVELOPMENT
      //debug skip
      if (Globals::IsSkipOnDrawOrDispatch) return DrawOrDispatchOverrideType::None;
#endif

      auto ps = original_shader_hashes.pixel_shaders[0];
      auto cs = original_shader_hashes.compute_shaders[0];
      
      //game_device_data
      auto& game_device_data = CallOfDutyBlackOps3GameDeviceData::GetGameDeviceData(device_data);
      
      ////////////////////////////////////////////////////////////////////////////////////////////////////////////

      //case: Light/Reflection Probe Culling
      if (!game_device_data.drawn_probecull && (cs == 0x6759EF9E || cs == 0xDA63105C))
      {
         //progress
         game_device_data.drawn_probecull = true;
         
         //disabled: no SR
         if (device_data.sr_type == SR::Type::None || device_data.sr_suppressed) return DrawOrDispatchOverrideType::None;

         //disabled: uneeded by new jitter
         if (DLSSJitter::is_sync_needed == 0) return DrawOrDispatchOverrideType::None;
         DLSSJitter::is_sync_needed--;
         
         //get and save PerSceneConsts's subpixelOffset.xy
         ID3D11Buffer* cb_buffer;
         native_device_context->CSGetConstantBuffers(1, 1, &cb_buffer); //Start @ index 1, get 1.

         //failed: no cb
         #if DEVELOPMENT
            if (cb_buffer == nullptr)
            {
               ASSERT_MSG(false, "FAILED jitter get");
               return DrawOrDispatchOverrideType::None;
            }
         #endif

         //get desc
         D3D11_BUFFER_DESC cb_desc = {};
         cb_buffer->GetDesc(&cb_desc);

         //extract by copying buffer
         ID3D11Buffer* staging_cb = cb_buffer;
         com_ptr<ID3D11Buffer> staging_cb_buf;
         cb_desc.Usage = D3D11_USAGE_STAGING;
         cb_desc.BindFlags = 0;
         cb_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
         cb_desc.MiscFlags = 0;
         cb_desc.StructureByteStride = 0;
         HRESULT hr_staging = native_device->CreateBuffer(&cb_desc, nullptr, &staging_cb_buf);
         if (SUCCEEDED(hr_staging))
         {
            native_device_context->CopyResource(staging_cb_buf.get(), cb_buffer);
            staging_cb = staging_cb_buf.get();
            D3D11_MAPPED_SUBRESOURCE mapped_cb = {};
            if (SUCCEEDED(native_device_context->Map(staging_cb, 0, D3D11_MAP_READ, 0, &mapped_cb)))
            {
               //ByteWidth 3024
               auto cb_floats = static_cast<const PerSceneConsts*>(mapped_cb.pData);
               
               //@ index 71
               float2 jitter = float2(cb_floats->subpixelOffset.x, cb_floats->subpixelOffset.y);
               game_device_data.jitter = jitter;
               
               native_device_context->Unmap(staging_cb, 0);
            }
         }

         //correct desynced
         DLSSJitter::OnDrawCorrectDesync(game_device_data);

         if (cb_buffer != nullptr) cb_buffer->Release();
         
         return DrawOrDispatchOverrideType::None;
      }

      ////////////////////////////////////////////////////////////////////////////////////////////////////////////

      //case: Motion Vectors final for static world geo

      
      //case: AO
      if (!game_device_data.drawn_tonemap && !game_device_data.drawn_ao0 && (ps == 0x04ED8F94 /*|| ps == 0x312EC494*/)) //computation
      {
         //progress
         game_device_data.drawn_ao0 = true;
         
         return DrawOrDispatchOverrideType::None;
      }
      if (!game_device_data.drawn_tonemap && !game_device_data.drawn_ao1 && (ps == 0x8FC85155 /*|| ps == 0x6A8BE686*/)) //blur
      {
         //progress
         game_device_data.drawn_ao1 = true;
         
         return DrawOrDispatchOverrideType::None;
      }

#if DEVELOPMENT
      //case: CS deferred resolve
      if (game_device_data.drawn_ao1 && !game_device_data.drawn_csdeferredresolve)
      {
         if (cs != 0) TiledDeferredCSTracking::OnDraw(cs, "cs");
         else game_device_data.drawn_csdeferredresolve = true;
      }
#endif
      
      ////////////////////////////////////////////////////////////////////////////////////////////////////////////

      //case: Tonemap
      if (!game_device_data.drawn_tonemap && !game_device_data.drawn_tonemap && (ps == 0x59F328E3 || ps == 0x1744B1D4))
      {
         //progress
         game_device_data.drawn_tonemap = true;
         
         return DrawOrDispatchOverrideType::None;
      }
      
      ////////////////////////////////////////////////////////////////////////////////////////////////////////////
      
      //case: SMAA T2X prep (SR)
      if (game_device_data.drawn_tonemap && !game_device_data.drawn_smaat2x &&
         (cs == 0x6240554C ||
          cs == 0xB037D915 ||
          cs == 0x3B3C41EF ||
          cs == 0xCDEFC09A))
      {
         //progress
         game_device_data.drawn_smaat2xprep = true;

         //disabled: no SR
         if (device_data.sr_type == SR::Type::None || device_data.sr_suppressed) return DrawOrDispatchOverrideType::None;

         //skip if SR
         return DrawOrDispatchOverrideType::Skip;
      }

      ////////////////////////////////////////////////////////////////////////////////////////////////////////////

      //case: SMAA T2XF (SR)
      if (game_device_data.drawn_smaat2xprep && !game_device_data.drawn_smaat2x && ps == 0x15FF4E6D)
      {
         //progress
         game_device_data.drawn_smaat2x = true;

         //error: on when SR
         if (device_data.sr_type != SR::Type::None)
         {
            static bool has_warned = false;
            if (!has_warned) MessageBoxA(nullptr, "Don't use SMAA T2x Filmic!\nPlease switch to the normal SMAA T2x for Super Resolution.", "Super Resolution", MB_ICONWARNING);
            has_warned = true;
         }

         return DrawOrDispatchOverrideType::None;
      }

      ////////////////////////////////////////////////////////////////////////////////////////////////////////////
      
      //case: SMAA T2X (SR)
      if (game_device_data.drawn_smaat2xprep && !game_device_data.drawn_smaat2x && ps == 0xD9288CF8)
      {
         //progress
         game_device_data.drawn_smaat2x = true;
         device_data.taa_detected = true;
         
         //disabled: no SR
         if (device_data.sr_type == SR::Type::None || device_data.sr_suppressed) return DrawOrDispatchOverrideType::None;

#if DEVELOPMENT
         //repeating jitter
         if (!DLSSJitter::IsReady() && game_device_data.jitter_prev.x == game_device_data.jitter.x)
         {
            Globals::CountSRJitterErrorRepeat++;
         }
#endif
         
         //no T2X jitter for SR: start continuing
         if (!DLSSJitter::IsReady() && !CallOfDutyBlackOps3GameDeviceData::IsValidJitter(game_device_data.jitter))
         {
            //take prev, but negate
            float j = -game_device_data.jitter_prev.x;

            //if not 0.25, steal sign onto 0.25
            if (std::abs(j) < 0.25f)
            {
               DLSSJitter::SetSyncNeeded();
               j = std::copysign(0.25f, j);
            }

            //set
            game_device_data.jitter = float2(j, j);
            
#if DEVELOPMENT
            //stats
            Globals::CountSRJitterContinued++;
#endif
         }
         
         /*
            Texture2D<float4> colorTex : register(t0);                   //color
            Texture2D<float4> temporalHistoryTex1 : register(t6);        //prev
            Texture2D<float4> temporalHistoryLumaTex1 : register(t7);    //aggregate luma
            Texture2D<float4> temporalHistoryLumaTex2 : register(t9);    //aggregate luma
            Texture2D<float4> temporalHistoryLumaTex3 : register(t10);   //aggregate luma
            Texture2D<float4> velocityTex0 : register(t11);              //motion vectors
            Texture2D<float4> velocityTex1 : register(t12);              //prev motion vectors
            Texture2D<float4> depthTex : register(t14);                  //depth (for filmic, this is replaced with the AA resolved prev tex and depth is push further to 15). 
          */
         
         //fetch srv 0
         native_device_context->PSGetShaderResources(0, 1, game_device_data.resources_smaa_color_input.srv.put());
         
         //fetch srv 11-14
         {
            std::array<ID3D11ShaderResourceView*, 4> tmp;
            native_device_context->PSGetShaderResources(11, tmp.size(), tmp.data()); //get
            game_device_data.resources_velocity.srv.attach(tmp[0]);
            game_device_data.resources_depth.srv.attach(tmp[3]);
            if (tmp[1]) tmp[1]->Release();
            if (tmp[2]) tmp[2]->Release();
         }
         
         //get res from srv
         game_device_data.resources_smaa_color_input.srv->GetResource(game_device_data.resources_smaa_color_input.res.put());
         game_device_data.resources_velocity.srv->GetResource(game_device_data.resources_velocity.res.put());
         game_device_data.resources_depth.srv->GetResource(game_device_data.resources_depth.res.put());

         //backup prev game_device_data.smaa_colorinput_resources.desc.Height
         uint2 prev_smaa_colorinput_dimensions = uint2(game_device_data.resources_smaa_color_input.desc.Width, game_device_data.resources_smaa_color_input.desc.Height);
         
         //if SRV change, internal resolution change probably changed, so get new by QueryInterface & GetDesc.
         if (game_device_data.resources_sr_output.tex == nullptr)
         {
            //get tex from res
            ID3D11Texture2D* tex = nullptr;
            auto hr = game_device_data.resources_smaa_color_input.res->QueryInterface(game_device_data.resources_smaa_color_input.tex.put()); //get
               #if DEVELOPMENT
                  ASSERT_ONCE(SUCCEEDED(hr));
               #endif

            //get desc from tex
            game_device_data.resources_smaa_color_input.tex->GetDesc(&game_device_data.resources_smaa_color_input.desc); //get
            
            #if DEVELOPMENT
               Globals::CountSRQueryInterface++;
            #endif
         }

         //sr_instance_data
         auto* sr_instance_data = device_data.GetSRInstanceData();
         #if DEVELOPMENT
            if (!sr_instance_data)
            {
               ASSERT_ONCE_MSG(false, "sr_instance_data is null!?!?!");
               return DrawOrDispatchOverrideType::Skip;
            }
         #endif
         
         //failed: too small output
         if (game_device_data.resources_smaa_color_input.desc.Width  < sr_instance_data->min_resolution ||
             game_device_data.resources_smaa_color_input.desc.Height < sr_instance_data->min_resolution)
         {
            device_data.has_drawn_sr = false;
            device_data.force_reset_sr = true;
            return DrawOrDispatchOverrideType::None;
         }

         //output_resolution (DLSS doesnt downsample (internal > output), so force biggest)
         uint2 output_resolution = uint2(static_cast<uint>(device_data.output_resolution.x), static_cast<uint>(device_data.output_resolution.y));
         output_resolution.x = output_resolution.x < game_device_data.resources_smaa_color_input.desc.Width ? game_device_data.resources_smaa_color_input.desc.Width : output_resolution.x;
         output_resolution.y = output_resolution.y < game_device_data.resources_smaa_color_input.desc.Height ? game_device_data.resources_smaa_color_input.desc.Height : output_resolution.y;
         
         //null or dlss_output_changed
         if (game_device_data.resources_sr_output.tex == nullptr ||
            prev_smaa_colorinput_dimensions != uint2(game_device_data.resources_smaa_color_input.desc.Width, game_device_data.resources_smaa_color_input.desc.Height))
         {
            //release prev
            game_device_data.HardResetSR();
            
            //setup resources_sr_output
            game_device_data.resources_sr_output.desc = CallOfDutyBlackOps3GameDeviceData::GetDefaultDesc(output_resolution);
            CallOfDutyBlackOps3GameDeviceData::SetupResource(native_device, game_device_data.resources_sr_output);
            
            //setup resources_dlss_linearize
            game_device_data.resources_dlss_linearize.desc = CallOfDutyBlackOps3GameDeviceData::GetDefaultDesc(output_resolution);
            CallOfDutyBlackOps3GameDeviceData::SetupResource(native_device, game_device_data.resources_dlss_linearize);

            //setup resources_velocity_linearize
            game_device_data.resources_velocity_linearize.desc = CallOfDutyBlackOps3GameDeviceData::GetDefaultDesc(uint2(game_device_data.resources_smaa_color_input.desc.Width, game_device_data.resources_smaa_color_input.desc.Height));
            game_device_data.resources_velocity_linearize.desc.Format = DXGI_FORMAT_R16G16_FLOAT;
            CallOfDutyBlackOps3GameDeviceData::SetupResource(native_device, game_device_data.resources_velocity_linearize);

            //stats
            #if DEVELOPMENT
               Globals::CountSRTexChange++;
            #endif
         }
         
         //SettingsData
         game_device_data.sr_settings_data.output_width  = output_resolution.x;
         game_device_data.sr_settings_data.output_height = output_resolution.y;
         game_device_data.sr_settings_data.render_width  =  game_device_data.resources_smaa_color_input.desc.Width;
         game_device_data.sr_settings_data.render_height =  game_device_data.resources_smaa_color_input.desc.Height;
         game_device_data.sr_settings_data.inverted_depth = true /*Globals::SRIsDepthInverse*/;
         game_device_data.sr_settings_data.hdr = cb_luma_global_settings.DisplayMode != DisplayModeType::SDR;
         game_device_data.sr_settings_data.mvs_x_scale = 1/*Globals::SRMvsScale*/;
         game_device_data.sr_settings_data.mvs_y_scale = 1/*Globals::SRMvsScale*/;
         game_device_data.sr_settings_data.mvs_jittered = false /*Globals::SRMvsJittered*/;
         game_device_data.sr_settings_data.auto_exposure = Globals::SRAutoExposure;
         game_device_data.sr_settings_data.render_preset = dlss_render_preset;
         // game_device_data.sr_settings_data.alpha_upscaling = Globals::SRAlphaUpscaling;
         sr_implementations[device_data.sr_type]->UpdateSettings(sr_instance_data, native_device_context, game_device_data.sr_settings_data);

         //DrawData
         // game_device_data.sr_draw_data.user_sharpness = Globals::SRSharpness;
         game_device_data.sr_draw_data.pre_exposure = Globals::SRPreExposure;
         game_device_data.sr_draw_data.render_width = game_device_data.sr_settings_data.render_width;
         game_device_data.sr_draw_data.render_height = game_device_data.sr_settings_data.render_height;
         game_device_data.sr_draw_data.near_plane = 0;
         game_device_data.sr_draw_data.far_plane = 1;
         game_device_data.sr_draw_data.source_color = game_device_data.resources_smaa_color_input.res.get();
         game_device_data.sr_draw_data.output_color = game_device_data.resources_sr_output.res.get();
         game_device_data.sr_draw_data.motion_vectors = game_device_data.resources_velocity_linearize.res.get();
         game_device_data.sr_draw_data.depth_buffer = game_device_data.resources_depth.res.get();
         //game_device_data.sr_draw_data.exposure = nullptr;

         // game_device_data.sr_draw_data.transparency_alpha = game_device_data.resources_oit.res.get();

#if ENABLE_FIDELITY_SK == 1
         constexpr float radians_60 = 1.0472f; //TODO: bruh
         game_device_data.sr_draw_data.vert_fov = sr_user_type == SR::UserType::DLSS ? 0.f : radians_60;
#endif

         if (DLSSJitter::IsReady())
         {
            game_device_data.sr_draw_data.jitter_x =  (!DLSSJitter::is_flip_use ? DLSSJitter::jitter.x : DLSSJitter::jitter_prev.x);
            game_device_data.sr_draw_data.jitter_y = -(!DLSSJitter::is_flip_use ? DLSSJitter::jitter.y : DLSSJitter::jitter_prev.y);
         }
         else
         {
            game_device_data.sr_draw_data.jitter_x = game_device_data.jitter.x;
            game_device_data.sr_draw_data.jitter_y = game_device_data.jitter.y;
         }
         
         //force reset?
         bool reset_dlss = device_data.force_reset_sr;
         game_device_data.sr_draw_data.reset = reset_dlss;

         //reset the reset
         device_data.force_reset_sr = false;

         ///////////////// SPECIAL: DLSS, so save for final ////////////////////
         if (!game_device_data.IsDLAA()) return DrawOrDispatchOverrideType::None;

         //linearize motion vectors
         {
            //cache
            DrawStateStack<DrawStateStackType::SimpleGraphics> draw_state_stack;
            draw_state_stack.Cache(native_device_context, 0 /*device_data.uav_max_count*/);
            
            //get shaders
            auto vs = device_data.native_vertex_shaders.find(Math::CompileTimeStringHash("Copy VS"));
            auto ps = device_data.native_pixel_shaders.find(CompileTimeStringHash("CoDBO3 LinearizeVelocity"));
            if (vs == device_data.native_vertex_shaders.end() || !vs->second.get() || ps == device_data.native_pixel_shaders.end() || !ps->second.get()) goto AfterMVLinearize;
            
            //motion vectors: resources_velocity srv --> resources_velocity_linearize rtv
            DrawCustomPixelShader(native_device_context, nullptr, nullptr, device_data.sampler_state_point.get(), vs->second.get(), ps->second.get(),
               game_device_data.resources_velocity.srv.get(), game_device_data.resources_velocity_linearize.rtv.get(), game_device_data.resources_velocity_linearize.desc.Width, game_device_data.resources_velocity_linearize.desc.Height);

            //restore
            draw_state_stack.Restore(native_device_context, true, true);
         }
         AfterMVLinearize:

         //DRAW!
         #if DEVELOPMENT
            if (!Globals::IsSkipDLSSDraw)
         #endif
               device_data.has_drawn_sr = sr_implementations[device_data.sr_type]->Draw(sr_instance_data, native_device_context, game_device_data.sr_draw_data);

         //FAILED:
         [[unlikely]] if (!device_data.has_drawn_sr)
         {
            //reset
            device_data.force_reset_sr = true;
            
            //warn
#if DEVELOPMENT
            ASSERT_ONCE_MSG(false, "SR Draw failed!");
#endif

            //give up
            return DrawOrDispatchOverrideType::Skip;
         }
         
         //replace smaa newest color srv 0
         auto srv = game_device_data.resources_sr_output.srv.get();
         native_device_context->PSSetShaderResources(0, 1, &srv);

         //let SMAA shader draw (for Tradeoff encoding)
         return DrawOrDispatchOverrideType::None;
      }

      ////////////////////////////////////////////////////////////////////////////////////////////////////////////

      //case: Final
      if (!game_device_data.drawn_final && (ps == 0x3D461B1A || ps == 0x224A8BF5))
      {
         //progress
         game_device_data.drawn_final = true;
         device_data.has_drawn_main_post_processing = true;

         //disabled: no SR
         if (device_data.sr_type == SR::Type::None || device_data.sr_suppressed) return DrawOrDispatchOverrideType::None;
         
         //DLSS linearize for final
         if (!game_device_data.IsDLAA())
         {
            //get and save original SRV 0 and RTV 0
            ID3D11ShaderResourceView* final_srv = nullptr;
            native_device_context->PSGetShaderResources(0, 1, &final_srv);
            ID3D11Resource* final_res = nullptr;
            final_srv->GetResource(&final_res); //This is the Luma replaced SRV (16f)

            //linearize color and motion vectors
            DrawStateStack<DrawStateStackType::SimpleGraphics> draw_state_stack;
            #if DEVELOPMENT
               if (Globals::IsSkipDLSSFinalLinearize) goto AfterLinearize;
            #endif
            draw_state_stack.Cache(native_device_context, 0/*device_data.uav_max_count*/); //cache
            {               
               //get shaders
               auto vs = device_data.native_vertex_shaders.find(Math::CompileTimeStringHash("Copy VS"));
               auto ps = device_data.native_pixel_shaders.find(CompileTimeStringHash("CoDBO3 PreSR"));
               if (vs == device_data.native_vertex_shaders.end() || !vs->second.get() || ps == device_data.native_pixel_shaders.end() || !ps->second.get()) goto AfterLinearize;
               
               //color: final's srv --> resources_dlss_linearize
               DrawCustomPixelShader(native_device_context, nullptr, nullptr, device_data.sampler_state_point.get(), vs->second.get(), ps->second.get(),
                  final_srv, game_device_data.resources_dlss_linearize.rtv.get(), game_device_data.resources_dlss_linearize.desc.Width, game_device_data.resources_dlss_linearize.desc.Height);
               
               //get shaders
               ps = device_data.native_pixel_shaders.find(CompileTimeStringHash("CoDBO3 LinearizeVelocity"));
               if (ps == device_data.native_pixel_shaders.end() || !ps->second.get()) goto AfterLinearize;
               
               //motion vectors: resources_velocity srv --> resources_velocity_linearize rtv
               DrawCustomPixelShader(native_device_context, nullptr, nullptr, device_data.sampler_state_point.get(), vs->second.get(), ps->second.get(),
                  game_device_data.resources_velocity.srv.get(), game_device_data.resources_velocity_linearize.rtv.get(), game_device_data.resources_velocity_linearize.desc.Width, game_device_data.resources_velocity_linearize.desc.Height);
            }
            draw_state_stack.Restore(native_device_context, true, true); //restore
            AfterLinearize:

            //set correct sr src color (resources_dlss_linearize --> resources_sr_output)
            game_device_data.sr_draw_data.source_color = game_device_data.resources_dlss_linearize.res.get();

            //set correct sr motion vectors ALREADY SET PRIOR IN SMAA
            // game_device_data.sr_draw_data.motion_vectors = game_device_data.resources_velocity_linearize.res.get();
            
            //get sr_instance_data
            auto* sr_instance_data = device_data.GetSRInstanceData();
               #if DEVELOPMENT
                  if (!sr_instance_data)
                  {
                     ASSERT_ONCE_MSG(false, "sr_instance_data is null!?!?!");
                     return DrawOrDispatchOverrideType::Skip;
                  }
               #endif
            
            //DLSS DRAW!
            #if DEVELOPMENT
               if (!Globals::IsSkipDLSSDraw)
            #endif
                  device_data.has_drawn_sr = sr_implementations[device_data.sr_type]->Draw(sr_instance_data, native_device_context, game_device_data.sr_draw_data);

            //FAILED:
            [[unlikely]] if (!device_data.has_drawn_sr)
            {
               //reset
               device_data.force_reset_sr = true;
            
               //warn
               #if DEVELOPMENT
                  ASSERT_ONCE_MSG(false, "SR Draw failed!");
               #endif

               //give up
               return DrawOrDispatchOverrideType::Skip;
            }

            //set final's srv 0 as sr output color (resources_sr_output --> final's rtv)
            native_device_context->PSSetShaderResources(0, 1, &game_device_data.resources_sr_output.srv);
         } AfterDLSS:
         
         //draw
         return DrawOrDispatchOverrideType::None;
      }

      ////////////////////////////////////////////////////////////////////////////////////////////////////////////

      //case: No Fullscreen Blur
      if (!Globals::IsFullscreenBlur && ps == 0xDA908072) 
         return DrawOrDispatchOverrideType::Skip;

      //case: FSFX
#if DEVELOPMENT
      if (game_device_data.drawn_tonemap && !game_device_data.drawn_final) //between tonemap and Final (will false positive SMAA, but whatever)
      {
         if (original_shader_hashes.pixel_shaders[0] != 0) FSFXTracking::OnDraw(original_shader_hashes.pixel_shaders[0], "ps");
         if (original_shader_hashes.vertex_shaders[0] != 0) FSFXTracking::OnDraw(original_shader_hashes.vertex_shaders[0], "vs");
         if (original_shader_hashes.compute_shaders[0] != 0) FSFXTracking::OnDraw(original_shader_hashes.compute_shaders[0], "cs");
      }
#endif

      //case: HDTV rec.709 decode
      if (game_device_data.drawn_final && ps == 0x8324B585)
      {
         //progress
         game_device_data.drawn_hdtv = true;
         
         return DrawOrDispatchOverrideType::Skip; //Skip this trash! It creates another 8bit tex to decode srgb and encode rec709.
      }

      //case: No UI after Final
      if (!Globals::IsUi && game_device_data.drawn_final) 
         return DrawOrDispatchOverrideType::Skip;

      //case: normal
      return DrawOrDispatchOverrideType::None;
   }

   void OnPresent(ID3D11Device* native_device, DeviceData& device_data) override
   {
      auto& game_device_data = CallOfDutyBlackOps3GameDeviceData::GetGameDeviceData(device_data);

      //sr enabled?
      const bool sr_enabled = device_data.sr_type != SR::Type::None;

      // //mipmap offset
      // if (!custom_texture_mip_lod_bias_offset && sr_enabled && Globals::SRSetMipLodBias)
      //    device_data.texture_mip_lod_bias_offset =
      //       Globals::SRSetMipLodBiasManual < 0 ?
      //          Globals::SRSetMipLodBiasManual :
      //          SR::GetMipLODBias(game_device_data.resources_smaa_color_input.desc.Height, static_cast<int>(cb_luma_global_settings.SwapchainSize.y));
      // else
      //    device_data.texture_mip_lod_bias_offset = 0;
      //
      // //SRIgnoreSamplerUpgrade
      // ignore_upgraded_samplers = !sr_enabled || Globals::SRIgnoreSamplerUpgrade;
      
      //clear SR
      if (sr_enabled && !game_device_data.drawn_smaat2x)
      {
         DLSSJitter::SetSyncNeeded();
         device_data.force_reset_sr = true;
      }
      
      //IsDLAA: pass to GPU
      if (sr_user_type != SR::UserType::None) cb_luma_global_settings.GameSettings.IsDLAA = game_device_data.IsDLAA();

      //DLSSJitter
      DLSSJitter::OnPresent(device_data, game_device_data);

      //ForcedLODBias
      ForcedLODBias::OnPresent();

      //reset
      game_device_data.Reset(device_data.force_reset_sr);
   }

   void CleanExtraSRResources(DeviceData& device_data) override
   {
      auto& game_device_data = CallOfDutyBlackOps3GameDeviceData::GetGameDeviceData(device_data);
      game_device_data.ClearAllResourcesSR();
   }
#endif

   void LoadConfigs() override
   {
      //log
      message(reshade::log::level::info, "LoadConfigs()");
      
      reshade::api::effect_runtime* runtime = nullptr;

      //Load ReShade settings
      reshade::get_config_value(runtime, NAME, "TonemapperRolloffStart", cb_luma_global_settings.GameSettings.TonemapperRolloffStart);
      reshade::get_config_value(runtime, NAME, "TonemapperMaxExpected", cb_luma_global_settings.GameSettings.TonemapperMaxExpected);
      reshade::get_config_value(runtime, NAME, "Bloom", cb_luma_global_settings.GameSettings.Bloom);
      reshade::get_config_value(runtime, NAME, "LensFlare", cb_luma_global_settings.GameSettings.LensFlare);
      reshade::get_config_value(runtime, NAME, "SlideLensDirt", cb_luma_global_settings.GameSettings.SlideLensDirt);
      reshade::get_config_value(runtime, NAME, "ADSSights", cb_luma_global_settings.GameSettings.ADSSights);
      reshade::get_config_value(runtime, NAME, "XrayOutline", cb_luma_global_settings.GameSettings.XrayOutline);
      reshade::get_config_value(runtime, NAME, "MotionBlur", cb_luma_global_settings.GameSettings.MotionBlur);
      reshade::get_config_value(runtime, NAME, "VolumetricFog", cb_luma_global_settings.GameSettings.VolumetricFog);
      reshade::get_config_value(runtime, NAME, "RCAS", cb_luma_global_settings.GameSettings.RCAS);
      reshade::get_config_value(runtime, NAME, "LUTBuilderExpansionChrominance", cb_luma_global_settings.GameSettings.LUTBuilderExpansionChrominance);
      reshade::get_config_value(runtime, NAME, "LUTBuilderExpansionLuminance", cb_luma_global_settings.GameSettings.LUTBuilderExpansionLuminance);
      reshade::get_config_value(runtime, NAME, "LUTBuilderHighlightSat", cb_luma_global_settings.GameSettings.LUTBuilderHighlightSat);
      reshade::get_config_value(runtime, NAME, "LUTBuilderHighlightSatHighlightsOnly", cb_luma_global_settings.GameSettings.LUTBuilderHighlightSatHighlightsOnly);
      reshade::get_config_value(runtime, NAME, "LUTBuilderNeutralChrominance", cb_luma_global_settings.GameSettings.LUTBuilderNeutralChrominance);
      reshade::get_config_value(runtime, NAME, "LUTBuilderNeutralHue", cb_luma_global_settings.GameSettings.LUTBuilderNeutralHue);
      reshade::get_config_value(runtime, NAME, "LUTBuilderNeutralLuma", cb_luma_global_settings.GameSettings.LUTBuilderNeutralLuma);
      // reshade::get_config_value(runtime, NAME, "LUTBuilderNeutralLumaHPStart", cb_luma_global_settings.GameSettings.LUTBuilderNeutralLumaHPStart);
      reshade::get_config_value(runtime, NAME, "LUTBuilderGradeSMH", cb_luma_global_settings.GameSettings.LUTBuilderGradeSMH);
      reshade::get_config_value(runtime, NAME, "LUTBuilderGradeTint", cb_luma_global_settings.GameSettings.LUTBuilderGradeTint);
      reshade::get_config_value(runtime, NAME, "LUTBuilderGradeSat", cb_luma_global_settings.GameSettings.LUTBuilderGradeSat);
      // reshade::get_config_value(runtime, NAME, "PCCLookback", cb_luma_global_settings.GameSettings.PCCLookback);
      reshade::get_config_value(runtime, NAME, "PCCHue", cb_luma_global_settings.GameSettings.PCCHue);
      reshade::get_config_value(runtime, NAME, "PCCChrominance", cb_luma_global_settings.GameSettings.PCCChrominance);
      // reshade::get_config_value(runtime, NAME, "PCCPeak", cb_luma_global_settings.GameSettings.PCCPeak);
      // reshade::get_config_value(runtime, NAME, "PCCChrominanceBoost", cb_luma_global_settings.GameSettings.PCCChrominanceBoost);
      // reshade::get_config_value(runtime, NAME, "PCCGuaranteed", cb_luma_global_settings.GameSettings.PCCGuaranteed);
      reshade::get_config_value(runtime, NAME, "BlackFloorSDRTonemap", cb_luma_global_settings.GameSettings.BlackFloorSDRTonemap);
      reshade::get_config_value(runtime, NAME, "BlackFloorLUT", cb_luma_global_settings.GameSettings.BlackFloorLUT);
      reshade::get_config_value(runtime, NAME, "CGContrast", cb_luma_global_settings.GameSettings.CGContrast);
      reshade::get_config_value(runtime, NAME, "CGContrastMidGray", cb_luma_global_settings.GameSettings.CGContrastMidGray);
      reshade::get_config_value(runtime, NAME, "CGSaturation", cb_luma_global_settings.GameSettings.CGSaturation);
      reshade::get_config_value(runtime, NAME, "CGHighlightsStrength", cb_luma_global_settings.GameSettings.CGHighlightsStrength);
      reshade::get_config_value(runtime, NAME, "CGHighlightsMidGray", cb_luma_global_settings.GameSettings.CGHighlightsMidGray);
      reshade::get_config_value(runtime, NAME, "CGShadowsStrength", cb_luma_global_settings.GameSettings.CGShadowsStrength);
      reshade::get_config_value(runtime, NAME, "CGShadowsMidGray", cb_luma_global_settings.GameSettings.CGShadowsMidGray);
      reshade::get_config_value(runtime, NAME, "Exposure", cb_luma_global_settings.GameSettings.Exposure);
      reshade::get_config_value(runtime, NAME, "GammaInfluence", cb_luma_global_settings.GameSettings.GammaInfluence);
      reshade::get_config_value(runtime, NAME, "GammaPerceptualChrominanceCorrect", cb_luma_global_settings.GameSettings.GammaPerceptualChrominanceCorrect);
      reshade::get_config_value(runtime, NAME, "MovPeakRatio", cb_luma_global_settings.GameSettings.MovPeakRatio);
      reshade::get_config_value(runtime, NAME, "MovShoulderPow", cb_luma_global_settings.GameSettings.MovShoulderPow);

      reshade::get_config_value(runtime, NAME, "UIIsAdvanced", Globals::UIIsAdvanced);
#if ENABLE_SR == 1
      reshade::get_config_value(runtime, NAME, "IsUi", Globals::IsUi);
      reshade::get_config_value(runtime, NAME, "IsFullscreenBlur", Globals::IsFullscreenBlur);
      // reshade::get_config_value(runtime, NAME, "SRIsDepthInverse", Globals::SRIsDepthInverse);
      reshade::get_config_value(runtime, NAME, "SRPreExposure", Globals::SRPreExposure);
      reshade::get_config_value(runtime, NAME, "SRAutoExposure", Globals::SRAutoExposure);
      // reshade::get_config_value(runtime, NAME, "SRSharpness", Globals::SRSharpness);
      // reshade::get_config_value(runtime, NAME, "SRAlphaUpscaling", Globals::SRAlphaUpscaling);
      // reshade::get_config_value(runtime, NAME, "SRNearPlane", Globals::SRNearPlane);
      // reshade::get_config_value(runtime, NAME, "SRFarPlane", Globals::SRFarPlane);
      // reshade::get_config_value(runtime, NAME, "SRJitterInputMultiplierX", Globals::SRJitterInputMultiplier.x);
      // reshade::get_config_value(runtime, NAME, "SRJitterInputMultiplierY", Globals::SRJitterInputMultiplier.y);
      // reshade::get_config_value(runtime, NAME, "SRMvsScale", Globals::SRMvsScale);
      // reshade::get_config_value(runtime, NAME, "SRMvsJittered", Globals::SRMvsJittered);
      // reshade::get_config_value(runtime, NAME, "SRVertCameraFOV", Globals::SRVertCameraFOV);
      // reshade::get_config_value(runtime, NAME, "SRSetMipLodBias", Globals::SRSetMipLodBias);
      // reshade::get_config_value(runtime, NAME, "SRIgnoreSamplerUpgrade", Globals::SRIgnoreSamplerUpgrade);
      DLSSJitter::OnLoadSettings(runtime);
      ForcedLODBias::OnLoadSettings(runtime);
#endif
      
      if (custom_sdr_gamma == 0) custom_sdr_gamma = 2.2f;
      reshade::get_config_value(runtime, NAME, "custom_sdr_gamma", custom_sdr_gamma);
      ShaderDefineInfo::Set(GAMMA_CORRECTION_TYPE_HASH, custom_sdr_gamma > 0);
      
      message(reshade::log::level::info, ("LoadConfigs() GAMMA_CORRECTION_TYPE: " + std::to_string(ShaderDefineInfo::Get(GAMMA_CORRECTION_TYPE_HASH))).c_str());
      // message(reshade::log::level::info, ("LoadConfigs() CUSTOM_SR: " + std::to_string(ShaderDefineInfo::Get(ShaderDefineInfo::CUSTOM_SR))).c_str());
      // message(reshade::log::level::info, ("LoadConfigs() CUSTOM_SDR: " + std::to_string(ShaderDefineInfo::Get(ShaderDefineInfo::CUSTOM_SDR))).c_str());


      //SectionedImGui
      SectionedImGui::OnLoadConfigs(runtime);
      
      defines_need_recompilation = true;
   }
   
   void DrawImGuiSettings(DeviceData& device_data) override
   {
      reshade::api::effect_runtime* runtime = nullptr;
      
      auto& game_device_data = CallOfDutyBlackOps3GameDeviceData::GetGameDeviceData(device_data);
      bool is_disabled; //for Begin/EndDisabled();

      ImGui::Separator();

      SectionedImGui::OnDraw(runtime, device_data);
      
      //////////////////////////////////////////////////////////////////////////////////////////////////////
      
      //Globals::SDR8Bit
      if (Globals::SDR8Bit)
      {
         ImGui::Bullet(); ImGui::SameLine();
         ImGui::TextWrapped("Forcing 8bit gamma SDR output.");
      }
      
      //SR on but not SMAA T2X
#if ENABLE_SR == 1
      if (device_data.sr_type != SR::Type::None && game_device_data.drawn_tonemap_prev && !game_device_data.drawn_smaat2x_prev)
      {
         auto pulse = SectionedImGui::Templates::GetPulseMultiplier();
         auto c = float3(1.f, 0.5f, 0.5f);
         auto color = ImVec4(c.x * pulse, c.y * pulse, c.z * pulse, 1.f);
         ImGui::PushStyleColor(ImGuiCol_Text, color);
         ImGui::Bullet(); ImGui::SameLine(); ImGui::TextWrapped("Super Resolution: Please select SMAA T2x (NOT Filmic) in-game!");
         ImGui::PopStyleColor();
      }
#endif

      //HDTV vs sRGB
#if ENABLE_SR == 1
      if (game_device_data.drawn_tonemap_prev)
      {
         auto def_hdtv = ShaderDefineInfo::Get(ShaderDefineInfo::CUSTOM_HDTVREC709);
         if (game_device_data.drawn_tonemap_prev && (game_device_data.drawn_hdtv_prev != (def_hdtv == 1)))
         {
            auto pulse = SectionedImGui::Templates::GetPulseMultiplier();
            auto c = float3(1.f, 1.f, 0.75f);
            auto color = ImVec4(c.x * pulse, c.y * pulse, c.z * pulse, 1.f);
            ImGui::PushStyleColor(ImGuiCol_Text, color);
            ImGui::Bullet();  ImGui::SameLine();
            if (game_device_data.drawn_hdtv_prev) ImGui::TextWrapped("Display Gamma: In-game settings seems to be HDTV Rec.709.");
            else                                  ImGui::TextWrapped("Display Gamma: In-game settings seems to be Computer sRGB.");
            ImGui::PopStyleColor();

            ImGui::SameLine();
            if (ImGui::Button("Apply for Luma")) ShaderDefineInfo::ToggleBool(ShaderDefineInfo::CUSTOM_HDTVREC709); 
         }
      }
      
#else
      //DO NOT TURN ON HDTV
      ImGui::SameLine(); ImGui::Bullet(); ImGui::TextWrapped("(Fast Mode)");
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.5f, 0.5f, 1.0f));
      ImGui::SameLine(); ImGui::Bullet(); ImGui::TextWrapped("Display Gamma: Make sure HDTV Rec.709 isn't selected!");
      ImGui::PopStyleColor();
#endif

#if DEVELOPMENT
      ImGui::Separator(); ////////////////////////////////////////////////////////////////////////////////////

      ImGui::Text("Debug:");
      {
         const std::string s0 = "Swapchain Changes: " + std::to_string(Globals::CountSwapchainChange);
         ImGui::BulletText(s0.c_str());

         const std::string s1 = "SR Output Tex Changes: " + std::to_string(Globals::CountSRTexChange);
         ImGui::BulletText(s1.c_str());
         
         const std::string s7 = "SR QueryInterface: " + std::to_string(Globals::CountSRQueryInterface);
         ImGui::BulletText(s7.c_str());

         const std::string s2 = "SR Continued Jitter: " + std::to_string(Globals::CountSRJitterContinued);
         ImGui::BulletText(s2.c_str());

         const std::string s3 = "SR Error Repeat Jitter Prev: " + std::to_string(Globals::CountSRJitterErrorRepeat);
         ImGui::BulletText(s3.c_str());

         float2 jitter_prev = DLSSJitter::IsReady() ? DLSSJitter::jitter : game_device_data.jitter_prev;
         const std::string s4 = "SR Jitter: " + std::to_string(jitter_prev.x) + ", " + std::to_string(jitter_prev.y);
         ImGui::BulletText(s4.c_str());

         const std::string s5 = "SR Is Sync Needed: " + std::to_string(DLSSJitter::is_sync_needed);
         ImGui::BulletText(s5.c_str());

         const std::string s11 = "ExeType: " + std::to_string(MemoryHack::exe_type);
         ImGui::BulletText(s11.c_str());

         const std::string s12 = "device_data.has_drawn_sr: " + std::to_string(device_data.has_drawn_sr);
         ImGui::BulletText(s12.c_str());

         const std::string s8 = "Is DLAA: " + std::to_string(game_device_data.IsDLAA());
         ImGui::BulletText(s8.c_str());

         if (ImGui::Checkbox("Skip OnDrawOrDispatch()", &Globals::IsSkipOnDrawOrDispatch))
            reshade::set_config_value(runtime, NAME, "IsSkipOnDrawOrDispatch", Globals::IsSkipOnDrawOrDispatch);
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Debug to skip all custom scanning to grab info for DLSS, UI toggle, and other misc stuff.");

         if (ImGui::Checkbox("Skip DLSS Draw", &Globals::IsSkipDLSSDraw))
            reshade::set_config_value(runtime, NAME, "IsSkipDLSSDraw", Globals::IsSkipDLSSDraw);
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("When we ever reach the DLSS draw call. Should it be skipped, even though all info is gathered?");

         if (ImGui::Checkbox("Skip DLSS Final Linearize", &Globals::IsSkipDLSSFinalLinearize))
            reshade::set_config_value(runtime, NAME, "IsSkipDLSSFinalLinearize", Globals::IsSkipDLSSFinalLinearize);
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Skip the custom linearize pass at final when DLSS.");

         ImGui::Checkbox("TiledDeferredCSTracking", &TiledDeferredCSTracking::enabled);
         ImGui::Checkbox("FSFXTracking", &FSFXTracking::enabled);
      }

      ImGui::Separator(); ////////////////////////////////////////////////////////////////////////////////////
#endif
   }

   void PrintImGuiAbout() override
   {
      ImGui::Text("Build Date:");
      ImGui::Text(__DATE__);
      ImGui::Text(__TIME__);
      ImGui::NewLine();

      ImGui::Text("Additional Credits:");
      ImGui::BulletText("Luma: Pumbo (Filoppi)");
      ImGui::BulletText("RenoDX: clshortfuse");
      ImGui::BulletText("HDR Consultant: Scrungus");
      ImGui::BulletText("Coding Help: Musa");
      ImGui::BulletText("Coding Help: Izueh");
      ImGui::BulletText("Jitter CPU Reversal: z1rp");
      ImGui::BulletText("Jitter CPU Reversal: PromptGnostic1");
      ImGui::BulletText("Bug Hunter & Researcher: NikkMann");
      ImGui::BulletText("Bug Hunter: sinical");
      ImGui::BulletText("Bug Hunter: marsan031");
      ImGui::BulletText("Bug Hunter: TJS");
      ImGui::BulletText("Bug Hunter: Kobefreak42");
      ImGui::BulletText("Bug Hunter: adap");
      ImGui::BulletText("Bug Hunter: ʚଓ");
      ImGui::BulletText("Bug Hunter: RooniVarooni");
      ImGui::BulletText("Bug Hunter: soulshot96");
      ImGui::BulletText("Bug Hunter: KING");

      ImGui::NewLine();
      ImGui::Text("Third Party:");
      ImGui::BulletText("ReShade");
      ImGui::BulletText("ImGui");
      ImGui::BulletText("RenoDX");
      ImGui::BulletText("3Dmigoto");
      ImGui::BulletText("Oklab");
      ImGui::BulletText("JzAzBz");
      ImGui::BulletText("Dolby");
      ImGui::BulletText("NVIDIA");
      ImGui::BulletText("AMD");
      ImGui::BulletText("DICE");
      ImGui::BulletText("KsDumper");
      ImGui::BulletText("Ghidra");
   }
};

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
   if (ul_reason_for_call == DLL_PROCESS_ATTACH)
   {
      Globals::SetGlobals(PROJECT_NAME, "Call of Duty: Black Ops III - Luma");
      Globals::VERSION = 1;
      
      //swapchain upgrade
      Globals::SDR8Bit = std::filesystem::exists("Luma_8bitSDR");
      swapchain_format_upgrade_type  = !Globals::SDR8Bit ? TextureFormatUpgradesType::AllowedEnabled : TextureFormatUpgradesType::None;
      swapchain_upgrade_type         = SwapchainUpgradeType::scRGB;
      force_disable_display_composition = Globals::SDR8Bit;

      //texture upgrade
// #if ENABLE_SR == 1
      texture_format_upgrades_type   = TextureFormatUpgradesType::AllowedEnabled;
      //enable_indirect_texture_format_upgrades = true;
      //enable_automatic_indirect_texture_format_upgrades = true;
      texture_upgrade_formats = {
         #if ENABLE_SR == 1
            reshade::api::format::r16g16_snorm, //motion vectors
         #endif
         reshade::api::format::r11g11b10_float, //color
      };
      texture_format_upgrades_2d_size_filters = (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainAspectRatio;
      
      //texture upgrade: LUT (r11g11b10_float too)
      // texture_format_upgrades_lut_dimensions = LUTDimensions::_3D;
      // texture_format_upgrades_lut_size = 32;
// #else
//       texture_format_upgrades_type   = TextureFormatUpgradesType::None; //TODO: is r11g11b10_float enough already?
// #endif

// #if ENABLE_SR == 1
//       //sampler upgrade
//       enable_samplers_upgrade = true;
// #endif
      
// #if DEVELOPMENT // If you want to track any shader names over time, you can hardcode them here by hash (they can be a useful reference in the pipeline)
//       forced_shader_names.emplace(std::stoul("FD2925B4", nullptr, 16), "Tracked Shader Name");
// #endif

#if !DEVELOPMENT // Put shaders that a previous version of the mod used but has ever since been deleted here
      old_shader_file_names.emplace("!tonemapper_mainmenu_0x1744B1D4.ps_5_0.hlsl");
      old_shader_file_names.emplace("!tonemapper_game_0x59F328E3.ps_5_0.hlsl");
      old_shader_file_names.emplace("!final_mainmenu_0x224A8BF5.ps_5_0.hlsl");
      old_shader_file_names.emplace("!final_game_0x3D461B1A.ps_5_0.hlsl");
#endif
      
      game = new CallOfDutyBlackOps3();
   }

   CoreMain(hModule, ul_reason_for_call, lpReserved);

   return TRUE;
}