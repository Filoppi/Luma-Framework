
#define GAME_CALL_OF_DUTY_BLACK_OPS_III 1

#define ALLOW_SHADERS_DUMPING 0
#define ENABLE_NGX 1
#define ENABLE_FIDELITY_SK 0

#include "..\..\Core\core.hpp"

namespace ShaderHashesLists
{
   static ShaderHashesList ProbeCulling;
   static ShaderHashesList LensFlare;
   static ShaderHashesList Tonemap;
   static ShaderHashesList SMAAT2X;
   static ShaderHashesList SMAAT2XPrep;
   static ShaderHashesList SMAAResolveH;
   // static ShaderHashesList AAStart;
   static ShaderHashesList FullscreenBlur;
   static ShaderHashesList Final;
   static ShaderHashesList Rec709;
}
void ShaderHashesLists_Setup()
{
   ShaderHashesLists::ProbeCulling.compute_shaders.emplace(std::stoul("0x6759EF9E", nullptr, 16)); //Light
   ShaderHashesLists::ProbeCulling.compute_shaders.emplace(std::stoul("0xDA63105C", nullptr, 16)); //Reflection
   
   ShaderHashesLists::LensFlare.pixel_shaders.emplace(std::stoul("0x7740A983", nullptr, 16));
   
   ShaderHashesLists::Tonemap.pixel_shaders.emplace(std::stoul("0x59F328E3", nullptr, 16)); //game
   ShaderHashesLists::Tonemap.pixel_shaders.emplace(std::stoul("0x1744B1D4", nullptr, 16)); //menu (CA)
   
   ShaderHashesLists::SMAAT2X.pixel_shaders.emplace(std::stoul("0xD9288CF8", nullptr, 16));
   //ShaderHashesLists::SMAAT2X.pixel_shaders.emplace(std::stoul("0x15FF4E6D", nullptr, 16)); // T2XF

   // ShaderHashesLists::AAStart.compute_shaders.emplace(std::stoul("0x6312E037", nullptr, 16)); // FXAA Edge
   // ShaderHashesLists::AAStart.compute_shaders.emplace(std::stoul("0x6240554C", nullptr, 16)); // SMAA Edge

   ShaderHashesLists::SMAAT2XPrep.compute_shaders.emplace(std::stoul("0x6240554C", nullptr, 16)); // SMAA Edge
   ShaderHashesLists::SMAAT2XPrep.compute_shaders.emplace(std::stoul("0xB037D915", nullptr, 16)); // SMAA Prep Idk
   ShaderHashesLists::SMAAT2XPrep.compute_shaders.emplace(std::stoul("0x3B3C41EF", nullptr, 16)); // SMAA Resolve V
   ShaderHashesLists::SMAAT2XPrep.compute_shaders.emplace(std::stoul("0xCDEFC09A", nullptr, 16)); // SMAA Resolve H
   
   ShaderHashesLists::SMAAResolveH.compute_shaders.emplace(std::stoul("0xCDEFC09A", nullptr, 16)); // SMAA Resolve H
   
   ShaderHashesLists::Final.pixel_shaders.emplace(std::stoul("0x3D461B1A", nullptr, 16)); //game
   ShaderHashesLists::Final.pixel_shaders.emplace(std::stoul("0x224A8BF5", nullptr, 16)); //menu (noise, dither)
   
   ShaderHashesLists::FullscreenBlur.pixel_shaders.emplace(std::stoul("0xDA908072", nullptr, 16));
   
   ShaderHashesLists::Rec709.pixel_shaders.emplace(std::stoul("0x8324B585", nullptr, 16));
}

namespace Globals
{
   static bool UIIsAdvanced = false;
   //OnDraw settings
   static bool IsUi = true;
   //static bool IsFullscreenOverlayFx = true;
   static bool IsFullscreenBlur = true;
   // static bool SRIsDepthInverse = true;
   static bool SRIsHDR = true;
   static float SRExposure = 0.f;
   static bool SRAutoExposure = true;
   static float SRSharpness = 1.f;
   // static float SRNearPlane = 0.f;
   // static float SRFarPlane = 1.f;
   // static float2 SRJitterMultiplier = float2(1.f, 1.f); 
   // static float SRMvsScale = -20.0f;
   static float SRVertCameraFOV = 1.5708f;
   static bool SRMvsJittered = false;
   static uint SRSuccessCount = 0;
   static bool SRIsSwapchainOutputSize = true;
   //static bool SR1 = false;
   //static bool SR2 = false;
   //static bool SR3 = false;
#if DEVELOPMENT
   static uint CountSwapchainChange = 0;
   static uint CountSRQueryInterface = 0;
   static uint CountSRTexChange = 0;
   static uint CountSRJitterContinued = 0;
   static uint CountSRJitterErrorRepeat = 0;
   static bool IsSkipManualReloadShaders = true;
   static bool IsSkipOnDrawOrDispatch = false;
   static bool IsSkipDLSSDraw = false;
   static bool IsSuperDebug = false;
   static bool IsSkipImGUI = false;
#endif

}

struct CallOfDutyBlackOps3GameDeviceData final : public GameDeviceData
{
   //jitter
   float2 jitter = float2(0);
   float2 jitter_prev = float2(0);
   // float CustomJitterCheese = 0.f;

   // //viewprojmatrix
   // DirectX::XMFLOAT4X4 viewprojmat = {};
   
   // //jitter CB slow copying
   // static constexpr int CB_STAGING_COUNT = 2;
   // com_ptr<ID3D11Buffer> cb_jitter_staging[CB_STAGING_COUNT];
   // int cb_staging_write_idx = 0;                              // slot we CopyResource into this frame
   // bool cb_staging_valid[CB_STAGING_COUNT] = {false, false};  // true once a copy has landed in that slot

   //sr
   com_ptr<ID3D11ShaderResourceView> sr_output_color_resource_view = nullptr;
   //com_ptr<D3D11_TEXTURE2D_DESC> sr_output_color_desc;
   com_ptr<ID3D11ShaderResourceView> tex_color_srv, tex_velocity_srv, tex_depth_srv;
   com_ptr<ID3D11Resource> tex_color_res, tex_depth_res, tex_velocity_res;
   com_ptr<ID3D11Texture2D> tex_color_tex2d;
   D3D11_TEXTURE2D_DESC tex_color_desc;
   ID3D11Resource* tex_color_res_prev = nullptr; // raw (non-owning) — only for same-pointer check
   
   //draw/pipeline progress
   bool drawn_probecull = false;
   // bool drawn_firstvertex = false;
   // bool drawn_firstvertex_prev = false;
   bool drawn_tonemap = false;
   bool drawn_tonemap_prev = false;
   bool drawn_smaat2xprep = false; //not filmic
   bool drawn_smaat2x = false; //not filmic
   bool drawn_smaat2x_prev = false; //prev frame
   bool drawn_final = false;
   bool drawn_hdtv = false;
   bool drawn_hdtv_prev = false; //prev frame

   //res
   uint2 res_height_render_vs_output = uint2(-1, -1);
   
   void Reset(bool isSRReset)
   {
      drawn_probecull = false;
      // drawn_firstvertex_prev = drawn_firstvertex;
      // drawn_firstvertex = false;
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
         Globals::SRSuccessCount = 0;
         res_height_render_vs_output = uint2(-1, -1);
      }
   }
   
   static bool IsValidJitter(float2 jitter) {return jitter != float2(0);}
   static bool IsValidJitter(float4 jitter) {return IsValidJitter(float2(jitter.x, jitter.y));}

   static void HardResetSR(DeviceData& device_data, CallOfDutyBlackOps3GameDeviceData& game_device_data)
   {
      device_data.sr_output_color = nullptr;
      game_device_data.sr_output_color_resource_view = nullptr;
      game_device_data.tex_color_srv = nullptr;
      game_device_data.tex_velocity_srv = nullptr;
      game_device_data.tex_depth_srv = nullptr;
      game_device_data.tex_color_res = nullptr;
      game_device_data.tex_depth_res = nullptr;
      game_device_data.tex_velocity_res = nullptr;
      game_device_data.tex_color_tex2d = nullptr;
      game_device_data.tex_color_res_prev = nullptr;
   }
};

namespace ShaderDefineInfo
{
   constexpr uint32_t CUSTOM_TONEMAP                   = char_ptr_crc32("CUSTOM_TONEMAP");
   constexpr uint32_t CUSTOM_TONEMAP_SCALING           = char_ptr_crc32("CUSTOM_TONEMAP_SCALING");
   constexpr uint32_t CUSTOM_TONEMAP_CLAMP             = char_ptr_crc32("CUSTOM_TONEMAP_CLAMP");
   constexpr uint32_t CUSTOM_HDTVREC709                = char_ptr_crc32("CUSTOM_HDTVREC709");
   constexpr uint32_t CUSTOM_RCAS                      = char_ptr_crc32("CUSTOM_RCAS");
   constexpr uint32_t CUSTOM_LUTBUILDER_SATBOOST       = char_ptr_crc32("CUSTOM_LUTBUILDER_SATBOOST");
   constexpr uint32_t CUSTOM_LUTBUILDER_NEUTRAL        = char_ptr_crc32("CUSTOM_LUTBUILDER_NEUTRAL");
   constexpr uint32_t CUSTOM_LUTBUILDER_NEUTRAL_LUMA   = char_ptr_crc32("CUSTOM_LUTBUILDER_NEUTRAL_LUMA");
   constexpr uint32_t CUSTOM_PCC                       = char_ptr_crc32("CUSTOM_PCC");
   constexpr uint32_t CUSTOM_COLORGRADE                = char_ptr_crc32("CUSTOM_COLORGRADE");
   constexpr uint32_t CUSTOM_UPSCALE_MOV               = char_ptr_crc32("CUSTOM_UPSCALE_MOV");
   constexpr uint32_t CUSTOM_SR                        = char_ptr_crc32("CUSTOM_SR");
   constexpr uint32_t CUSTOM_SDR                       = char_ptr_crc32("CUSTOM_SDR");
   constexpr uint32_t CUSTOM_MB_QUALITY                = char_ptr_crc32("CUSTOM_MB_QUALITY");
   constexpr uint32_t CUSTOM_CHROMABER                 = char_ptr_crc32("CUSTOM_CHROMABER");
   constexpr uint32_t CUSTOM_UCS_TYPE                  = char_ptr_crc32("CUSTOM_UCS_TYPE");
   constexpr uint32_t CUSTOM_BLOOM_SATURATIONPRESERVE  = char_ptr_crc32("CUSTOM_BLOOM_SATURATIONPRESERVE");
   constexpr uint32_t CUSTOM_GAMMA_CORRECTION_MODE     = char_ptr_crc32("CUSTOM_GAMMA_CORRECTION_MODE");

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

namespace DLSSJitter
{
   // namespace Scanner
   // {
   //    //list of result ptrs
   //    static std::vector<uintptr_t> result_ptrs;
   //    static int last_scan_count = -1;
   //
   //    static int Scan(uintptr_t base) //AI moment
   //    {
   //       result_ptrs.clear();
   //
   //       const uint32_t pattern[4] = {
   //          0xBE800000, // -0.25f
   //          0x3E800000, //  0.25f
   //          0x3E800000, //  0.25f
   //          0xBE800000  // -0.25f
   //      };
   //       constexpr size_t pattern_size = sizeof(pattern); // 16 bytes
   //
   //       MEMORY_BASIC_INFORMATION mbi{};
   //       uintptr_t addr = 0;
   //
   //       while (VirtualQuery(reinterpret_cast<LPCVOID>(addr), &mbi, sizeof(mbi)) == sizeof(mbi))
   //       {
   //          // Only scan committed, readable, non-guard pages
   //          bool readable = (mbi.State   == MEM_COMMIT)
   //                       && (mbi.Protect != PAGE_NOACCESS)
   //                       && !(mbi.Protect & PAGE_GUARD);
   //
   //          if (readable)
   //          {
   //             auto* region_start = reinterpret_cast<const uint8_t*>(mbi.BaseAddress);
   //             size_t region_size = mbi.RegionSize;
   //
   //             // Walk the region in 4-byte aligned steps
   //             for (size_t offset = 0; offset + pattern_size <= region_size; offset += 4)
   //             {
   //                const auto* candidate = reinterpret_cast<const uint32_t*>(region_start + offset);
   //
   //                __try
   //                {
   //                   if (candidate[0] == pattern[0] &&
   //                       candidate[1] == pattern[1] &&
   //                       candidate[2] == pattern[2] &&
   //                       candidate[3] == pattern[3])
   //                   {
   //                      result_ptrs.push_back(reinterpret_cast<uintptr_t>(candidate));
   //                   }
   //                }
   //                __except (EXCEPTION_EXECUTE_HANDLER) { /* skip unreadable page */ }
   //             }
   //          }
   //
   //          // Advance past this region; guard against infinite loop on 0-size
   //          uintptr_t next = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
   //          if (next <= addr) break;
   //          addr = next;
   //       }
   //
   //       last_scan_count = static_cast<int>(result_ptrs.size());
   //       return last_scan_count;
   //    }
   // }

   // //From Izueh
   // constexpr std::array<float2, 64> precomputed_jitters_64 = []() 
   // {
   //    std::array<float2, 64> entries{};
   //    for (unsigned int i = 0; i < entries.size(); i++) entries[i] = float2{SR::HaltonSequence(i, 2), SR::HaltonSequence(i, 3)};
   //    return entries;
   // }();
   // constexpr std::array<float2, 32> precomputed_jitters_32 = []()
   // {
   //    std::array<float2, 32> entries{};
   //    for (unsigned int i = 0; i < entries.size(); i++) entries[i] = float2{SR::HaltonSequence(i, 2), SR::HaltonSequence(i, 3)};
   //    return entries;
   // }();
   // constexpr std::array<float2, 24> precomputed_jitters_24 = []()
   // {
   //    std::array<float2, 24> entries{};
   //    for (unsigned int i = 0; i < entries.size(); i++) entries[i] = float2{SR::HaltonSequence(i, 2), SR::HaltonSequence(i, 3)};
   //    return entries;
   // }();
   // constexpr std::array<float2, 16> precomputed_jitters_16 = []()
   // {
   //       std::array<float2, 16> entries{};
   //       for (unsigned int i = 0; i < entries.size(); i++) entries[i] = float2{SR::HaltonSequence(i, 2), SR::HaltonSequence(i, 3)};
   //       return entries;
   // }();
   //    constexpr std::array<float2, 8> precomputed_jitters_8 = []()
   // {
   //       std::array<float2, 8> entries{};
   //       for (unsigned int i = 0; i < entries.size(); i++) entries[i] = float2{SR::HaltonSequence(i, 2), SR::HaltonSequence(i, 3)};
   //       return entries;
   // }();
   static float2 jitter = float2(0);
   static int phases_sel = 0;

   static uintptr_t addr_base;
   static uint32_t* addr_0;
   static uint32_t* addr_4;
   static uint32_t* addr_8;
   static uint32_t* addr_12;
   static uint frame = 0;
   static int phases = 0;
   // static bool debug_AHH = false;
   static bool flip_set = false;
   static bool flip_condition = false;
   
   //DONT SET THIS DIRECTLY
   static bool is_active = false;
   static bool is_boot_autorun_token = false;
   static const char* fail_reason = nullptr;
   static uint offset_input = 0; 

   //if true, you can set jitter OnPresent
   static bool IsReady()
   {
      return is_active && addr_0 != nullptr /*&& addr4 != nullptr && addr8 != nullptr && addr12 != nullptr*/;
   }

   //clear everything
   static void Reset(const char* reason = nullptr)
   {
      fail_reason = reason;
      addr_0 = nullptr;
      addr_4 = nullptr;
      addr_8 = nullptr;
      addr_12 = nullptr;
   }
   
   static bool IsVerifiedJitterArray(uintptr_t addr)
   {
      const static uint32_t n025 = 0xBE800000; //-0.25
      const static uint32_t p025 = 0x3E800000; //0.25
      
      //addr+0 = -0.25
      addr_0 = reinterpret_cast<uint32_t*>(addr);
      if (*addr_0 != n025) {addr_0 = nullptr; return false;}
      
      //addr+4 = 0.25
      addr_4 = reinterpret_cast<uint32_t*>(addr + 4);
      if (*addr_4 != p025) {addr_0 = nullptr; return false;}
      
      //addr+8 = 0.25
      addr_8 = reinterpret_cast<uint32_t*>(addr + 8);
      if (*addr_8 != p025) {addr_0 = nullptr; return false;}

      //addr+12 = -0.25
      addr_12 = reinterpret_cast<uint32_t*>(addr + 12);
      if (*addr_12 != n025) {addr_0 = nullptr; return false;}

      return true;
   }

   static bool SetJitter(float2 jitter)
   {
      //float bits as uint
      uint32_t jitter_x_as_uint, jitter_y_as_uint;
      memcpy(&jitter_x_as_uint, &jitter.x, sizeof(uint32_t));
      memcpy(&jitter_y_as_uint, &jitter.y, sizeof(uint32_t));

      //REQUIRED or crash
      DWORD old_protect;
      if (!VirtualProtect(addr_0, 16, PAGE_EXECUTE_READWRITE, &old_protect))
      {
         Reset("Failed to change memory protection with VirtualProtect.");
         return false;
      }

      int f = frame;
      // if (debug_AHH) f += 1;
      if (f % 2 == 0)
      {
         *addr_0  = jitter_x_as_uint;
         *addr_4  = jitter_y_as_uint;
      }
      else
      {
         *addr_8  = jitter_x_as_uint;
         *addr_12 = jitter_y_as_uint;
      }
      
      VirtualProtect(addr_0, 16, old_protect, &old_protect);
      return true;
   }
   static bool SetJitterReset()
   {
      //float bits as uint
      uint32_t jitter_neg, jitter_pos;
      const static float2 jitter_reset = float2(-0.25f, 0.25f);
      memcpy(&jitter_neg, &jitter_reset.x, sizeof(uint32_t));
      memcpy(&jitter_pos, &jitter_reset.y, sizeof(uint32_t));

      //REQUIRED or crash
      DWORD old_protect;
      if (!VirtualProtect(addr_0, 16, PAGE_EXECUTE_READWRITE, &old_protect)) return false;

      *addr_0  = jitter_neg;
      *addr_4  = jitter_pos;
      *addr_8  = jitter_pos;
      *addr_12 = jitter_neg;
      
      VirtualProtect(addr_0, 16, old_protect, &old_protect);
      return true;
   }

   static bool TrySetActive(uint32_t offset, DeviceData& device_data, CallOfDutyBlackOps3GameDeviceData& game_device_data, reshade::api::effect_runtime* runtime = nullptr, bool replace_ui = true)
   {
      //clean
      offset = max(0, offset);
      if (replace_ui) offset_input = offset; //replace UI too

      //RETURN: deactivate
      if (offset == 0) 
      {
         if (IsReady()) SetJitterReset();
         Reset();
         is_active = false;
         fail_reason = nullptr;
         reshade::set_config_value(runtime, NAME, "JitterMemoryOffset", offset);
         return true;
      }

      //try
      is_active = IsVerifiedJitterArray(addr_base + offset);

      //success
      if (is_active)
      {
         frame = 0;
         CallOfDutyBlackOps3GameDeviceData::HardResetSR(device_data, game_device_data);
         reshade::set_config_value(runtime, NAME, "JitterMemoryOffset", offset);
         is_boot_autorun_token = false;
      }

      //fail reason
      fail_reason = is_active ? nullptr : "Failed to find and verify jitter array in memory. Please report the version of the game you are using.\n(BO3Enhanced was easiest to find, with obfuscation like Anti-Cheat and DRM stripped out.)";

      message(reshade::log::level::info, std::string("DLSSJitter::SetMode(): +" + std::to_string(offset) + ", " + (is_active ? "Success" : "Failed")).c_str());
      return true;
   }

   static void Init()
   {
      addr_base = reinterpret_cast<uintptr_t>(GetModuleHandleA("BlackOps3.exe"));
      message(reshade::log::level::info, std::string("DLSSJitter::Init(): Base Address: 0x" + std::to_string(addr_base)).c_str());
   }

   //Update loop
   static void OnPresent(DeviceData& device_data, CallOfDutyBlackOps3GameDeviceData& game_device_data)
   {
      if (is_boot_autorun_token) is_boot_autorun_token = is_active ? false : !TrySetActive(offset_input, device_data, game_device_data);

      if (device_data.force_reset_sr) return;
      if (!IsReady()) return;

      //++
      frame++;

      //new jitters
      if (phases_sel == 0 && device_data.sr_type != SR::Type::None)
      {
         phases = SR::GetDefaultJitterPhases();
         auto* sr_instance_data = device_data.GetSRInstanceData();
         phases = sr_implementations[device_data.sr_type]->GetJitterPhases(sr_instance_data);
      }
      else phases = phases_sel;
      
      int temporal_frame = cb_luma_global_settings.FrameIndex % phases;
      jitter = float2(SR::HaltonSequence(temporal_frame, 2), SR::HaltonSequence(temporal_frame, 3));

      // jitter.x *= Globals::SRJitterMultiplier.x;
      // jitter.y *= Globals::SRJitterMultiplier.y;

      //set
      // message(reshade::log::level::info, std::string("DLSSJitter::OnPresent(): Frame: " + std::to_string(frame) + ", Jitter: (" + std::to_string(jitter.x) + ", " + std::to_string(jitter.y) + ")").c_str());
      SetJitter(jitter);
   }

   static void OnUI(reshade::api::effect_runtime* runtime, DeviceData& device_data, CallOfDutyBlackOps3GameDeviceData& game_device_data)
   {
      //memory offset entry
      if (is_active) ImGui::BeginDisabled();
      {
        ImGui::InputScalar("Jitter Memory Offset (Hex)", ImGuiDataType_U32, &offset_input, nullptr, nullptr, "%X");
         if (ImGui::IsItemHovered()) ImGui::SetTooltip("Enter the memory offset in hex where the jitter array is located.");
      }
      if (is_active) ImGui::EndDisabled();

      //try start/stop
      if (!is_active)
      {
         if (ImGui::Button("Try Start with Given Offset")) TrySetActive(offset_input, device_data, game_device_data);
         if (ImGui::IsItemHovered()) ImGui::SetTooltip("Verify the jitter array at the given offset and start patching if successful.");
      } else
      {
         if (ImGui::Button("Stop and Restore Original")) TrySetActive(0, device_data, game_device_data, runtime);
      }
      
      //BO3Enhanced base+0x2F896C0
      if (!is_active && ImGui::Button("Try Start for BO3Enhanced Offset (0x2F896C0)"))
      {
         offset_input = 0x2F896C0;
         TrySetActive(offset_input, device_data, game_device_data);
      }
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("Verify the jitter array found in the older DRM-less Windows Store version used by BO3Enhanced.\nThe jitter array is created on the stack during launch, consistently at this offset.");

      //status
      if (fail_reason)
      {
         //red
         ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 100, 100, 255));
         ImGui::BulletText("Failed: %s", fail_reason);
         ImGui::PopStyleColor();
      }
      else if (!IsReady())
      {
         if (is_active)
         {
            //yellow
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 100, 255));
            ImGui::BulletText("Waiting for SMAA T2X...");
            ImGui::PopStyleColor();
         }
      }
      else
      {
         //green
         ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(100, 255, 100, 255));
         ImGui::BulletText("ACTIVE AND PATCHING! (%.5f, %.5f)", jitter.x, jitter.y, frame % 2 != 0);
         ImGui::BulletText("Writing to index: %s", (frame % 2 == 0) ? "0" : "1");
         // ImGui::BulletText("flip_condition: %s", flip_condition ? "true" : "false");
         ImGui::PopStyleColor();
      }

      // ImGui::NewLine(); ///////////////////////
      //
      // //scan
      // if (ImGui::Button("Scan for Jitter Array in Memory (FREEZES GAME!)"))
      // {
      //    int found = Scanner::Scan(addr_base);
      //    if (found > 0)
      //    {
      //       message(reshade::log::level::info, std::string("DLSSJitter::OnUI(): Scan found " + std::to_string(found) + " candidates. Verifying...").c_str());
      //       bool verified = false;
      //       for (uintptr_t addr : Scanner::result_ptrs)
      //       {
      //          if (IsVerifiedJitterArray(addr))
      //          {
      //             message(reshade::log::level::info, std::string("DLSSJitter::OnUI(): Verified candidate at address offset: 0x" + std::to_string(addr - addr_base)).c_str());
      //             verified = true;
      //          }
      //       }
      //       if (!verified) message(reshade::log::level::warning, "DLSSJitter::OnUI(): Failed to verify any candidates from scan.");
      //    }
      //    else message(reshade::log::level::warning, "DLSSJitter::OnUI(): Scan failed to find any candidates in memory.");
      // }
      //
      // //scan: list
      // if (Scanner::last_scan_count >= 0)
      // {
      //    ImGui::Text("Offset Scan Results: %d", Scanner::last_scan_count);
      //    for (uintptr_t addr : Scanner::result_ptrs)
      //    {
      //       ImGui::BulletText("0x%X", addr - addr_base);
      //       ImGui::SameLine();
      //       if (ImGui::SmallButton("Try Start"))
      //       {
      //          offset_input = addr - addr_base;
      //          TrySetActive(offset_input, device_data, game_device_data);
      //       }
      //    }
      // }
      
      ImGui::NewLine(); ///////////////////////

      //DLSSJitter::jitter_sel
      if (ImGui::SliderInt("Jitter Phases", &DLSSJitter::phases_sel, 0, 32)) //TODO save
      {
         device_data.force_reset_sr = true; //soft reset for new jitters
         // CallOfDutyBlackOps3GameDeviceData::HardResetSR(device_data, game_device_data);
      }
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("Set the amount of offsets before repeating.\n\nHighest isn't automatically best, especially for DLAA,\nsince it'll takes more time to return and update that subpixel position."); 
      ImGui::BulletText("Active: %d", is_active ? phases : -1);
      
      ImGui::NewLine(); ///////////////////////
      
      ImGui::Checkbox("Fix Judder (SHOULDN'T BE NEEDED!)", &flip_set);
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("(SHOULDN'T BE NEEDED! Please report if otherwise.)\nToggle this to maybe fix the bugged judder seen when standing still.\n\nExplanation: There are 2 jitter offset. We can write to the one being used (wrong), or the one next up to be used (correct).\nThis changes which one that'll be set.\nEditing both at once causes async artefacts.");
      // if (ImGui::Button("Fix Judder (Once)")) frame--;
   }
   
   void OnDrawCorrectDesync(CallOfDutyBlackOps3GameDeviceData& game_device_data)
   {
      flip_condition = std::abs(game_device_data.jitter.x - jitter.x) < 0.0001f && std::abs(-game_device_data.jitter.y - jitter.y) < 0.0001f;
      if (flip_set) flip_condition = !flip_condition;
      if (IsReady() && flip_condition) frame--;
   }
   
   void OnLoadSettings(reshade::api::effect_runtime* runtime)
   {
      reshade::get_config_value<uint32_t>(runtime, NAME, "JitterMemoryOffset", offset_input);
      if (offset_input != 0)
      {
         message(reshade::log::level::info, std::string("DLSSJitter::OnLoadSettings(): Found saved jitter memory offset and will autorun: 0x" + std::to_string(offset_input)).c_str());
         is_boot_autorun_token = true;
      }
   }
}

class CallOfDutyBlackOps3 final : public Game
{
   // Helper to hide ugly casts
   static CallOfDutyBlackOps3GameDeviceData& GetGameDeviceData(DeviceData& device_data)
   {
      return *static_cast<CallOfDutyBlackOps3GameDeviceData*>(device_data.game);
   }
   
public:
   void OnInit(bool async) override
   {
      //log
      message(reshade::log::level::info, "OnInit()");
      
      //Def
      std::vector<ShaderDefineData> game_shader_defines_data = {
         {"GAMMA_CORRECTION_RANGE_TYPE", '0', true, true, "0 - Full range.\n1 - 0-1 only.", 1},
         {"SWAPCHAIN_CLAMP_PEAK", '0', true, false, "Final color clamp before present.\n0 - Unclamped (up to display).\n1 - Per channel clamp (blows out).\n2 - Scale down by max channel (sat preserving).", 2},
         {"SWAPCHAIN_CLAMP_COLORSPACE", '0', true, false, "Clamp colorspace against invalid colors.\n(Really only for OCD, as it should only be inconsequential black from RCAS.)\n0 - Unclamped.\n1 - BT2020.", 1},
         {"CUSTOM_GAMMA_CORRECTION_MODE", '1', true, true, "0 - Per-Channel.\n1 - Perceptual.", 1},
         {"CUSTOM_HDTVREC709", '0', true, false, "Decode color and swapchain to HDTV (rec.709) setting.", 1},
         {"CUSTOM_TONEMAP", '2', true, false, "HDR tonemapper, primarily the shoulder.\n0 - Off (Unclamped).\n1 - Reinhard Piecewise\n2 - Hermite Spline", 2},
         {"CUSTOM_TONEMAP_SCALING", '0', true, false, "HDR tonemap scaling.\n0 - Luminance (natural)\n1 - Max-Channel (saturation preserve)", 1},
         {"CUSTOM_TONEMAP_CLAMP", '0', true, false, "(Only if CUSTOM_TONEMAP_SCALING is luminance scaled.)\nClamp overshoot from luma scaled HDR tonemap.\n0 - Unclamped (up to display).\n1 - Per channel clamp (blows out).\n2 - Scale down by max channel (sat preserving).", 2},
         {"CUSTOM_RCAS", '0', true, false, "Enable Robust (4 instead of 8 samples) Contrast Adaptive Sharpening.", 1},
         {"RCAS_DENOISE", '0', true, false, "Denoising sharpening.\nMore smooth, but helps against dither pattern of shadows and AO.", 1},
         {"RCAS_LUMINANCE_BASED", '0', true, false, "Luma based sharpening. Apparently meh.", 1},
         {"CUSTOM_LUTBUILDER_SATBOOST", '0', true, false, "Boost LUT chrominance by doing some goofy perceptual color space hacks.", 1},
         {"CUSTOM_LUTBUILDER_NEUTRAL", '1', true, false, "If LUT texture color is desaturated, blend to neutral color.\n0 - Off.\n1 - If desaturated.\n2 - Forcefully.", 2 },
         {"CUSTOM_LUTBUILDER_NEUTRAL_LUMA", '0', true, false, "Neutral blend type.\n0 - High passed.\n1 - Forcefully.", 2 },
         {"CUSTOM_LUT_MAXCHANNELCLAMPINPUT", '0', true, false, "Max channel scale down to 1 before sampling LUT to saved clipped extreme highlights.\nWill probably remove too move blowout.", 1},
         {"CUSTOM_PCC", '1', true, false, "Do per channel correction on SDR tonemapped colors to reduce blowout.", 1},
         {"CUSTOM_UPGRADE_HUE_CORRECTION", '1', true, false, "When mapping HDR luminance onto color graded SDR color, should we spend some computation to correct the new color's hue to the original?\nRather unnoticeable difference.", 1},
         {"CUSTOM_UPGRADE_DEBUG", '0', true, false, "0 - Final upgraded HDR color.\n1 - Raw HDR color.\n2 - Neutral/Baseline SDR color before grading.\n3 - Vanilla Ungraded SDR color.\n4 - Vanilla Graded SDR color.\n5 - (Development Version Only) Select by DV10", DEVELOPMENT == 1 ? 5 : 4},
         {"CUSTOM_UCS_TYPE", '2', true, false, "Working perceptual/uniform color space.\nUsed to perceptually blend between colors, each gives slightly different hues and chrominance.\n\n0 - JzAzBz\n1 - OkLAB\n2 - ICtCp", 2},
         {"CUSTOM_COLORGRADE", '0', true, false, "Enable Color Grading right before HDR tonemapping.", 1},
         {"CUSTOM_BLOOM_SATURATIONPRESERVE", '0', true, false, "Blend the normal bloom sample with a lower exposure bloom sample to restore some chrominance.", 1},
         {"CUSTOM_UPSCALE_MOV", '0', true, false, "Auto HDR for movies.", 1},
         {"CUSTOM_CHROMABER", '1', true, false, "Allow chromatic aberration.\nSeldom usage, only on the main menu.", 1},
         {"CUSTOM_MB_QUALITY", '0', true, false, "Motion blur sample count (pairs of forward and reverse).\n0 - 6 (Original)\n1 - 16\n2 - 24 (High)\n3 - 32\n4 - 48\n5 - 64\n6 - 128 (Uhhh)", 6},
         {"CUSTOM_SR", '1', true, false, "(AUTOMATICALLY HANDLED) Recompiles SMAA T2X (not filmic) according to SR type.", 1},
         {"CUSTOM_SDR", '1', true, false, "(AUTOMATICALLY HANDLED) Turn off FSFX HDR tradeoff encoding stuff.", 1},
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
      
      //cb
      luma_settings_cbuffer_index = 13;
      luma_data_cbuffer_index = 12;
      luma_ui_cbuffer_index = -1; 

      //GameSettings default
      default_luma_global_game_settings.TonemapperRolloffStart = cb_luma_global_settings.GameSettings.TonemapperRolloffStart = 36.f;
      default_luma_global_game_settings.TonemapperMaxExpected = cb_luma_global_settings.GameSettings.TonemapperMaxExpected = 400000.f;
      default_luma_global_game_settings.Bloom = cb_luma_global_settings.GameSettings.Bloom = 1.f;
      default_luma_global_game_settings.LensFlare = cb_luma_global_settings.GameSettings.LensFlare = 1.f;
      default_luma_global_game_settings.SlideLensDirt = cb_luma_global_settings.GameSettings.SlideLensDirt = 1.f;
      default_luma_global_game_settings.ADSSights = cb_luma_global_settings.GameSettings.ADSSights = 1.0f;
      default_luma_global_game_settings.XrayOutline = cb_luma_global_settings.GameSettings.XrayOutline = 1.f;
      default_luma_global_game_settings.MotionBlur = cb_luma_global_settings.GameSettings.MotionBlur = 1.f;
      default_luma_global_game_settings.VolumetricFog = cb_luma_global_settings.GameSettings.VolumetricFog = 1.f;
      default_luma_global_game_settings.RCAS = cb_luma_global_settings.GameSettings.RCAS = 0.5f;
      default_luma_global_game_settings.SDRTonemapFloorRaiseScale = cb_luma_global_settings.GameSettings.SDRTonemapFloorRaiseScale = 1.0f;
      default_luma_global_game_settings.LUTBuilderExpansionChrominance = cb_luma_global_settings.GameSettings.LUTBuilderExpansionChrominance = 0.25f;
      default_luma_global_game_settings.LUTBuilderExpansionLuminance = cb_luma_global_settings.GameSettings.LUTBuilderExpansionLuminance = 0.25f;
      default_luma_global_game_settings.LUTBuilderHighlightSat = cb_luma_global_settings.GameSettings.LUTBuilderHighlightSat = 0.125f;
      default_luma_global_game_settings.LUTBuilderHighlightSatHighlightsOnly = cb_luma_global_settings.GameSettings.LUTBuilderHighlightSatHighlightsOnly = 3.2f;
      default_luma_global_game_settings.LUTBuilderNeutralHue = cb_luma_global_settings.GameSettings.LUTBuilderNeutralHue = 0.15f;
      default_luma_global_game_settings.LUTBuilderNeutralChrominance = cb_luma_global_settings.GameSettings.LUTBuilderNeutralChrominance = 0.25f;
      default_luma_global_game_settings.LUTBuilderNeutralLuma = cb_luma_global_settings.GameSettings.LUTBuilderNeutralLuma = 1.0f;
      // default_luma_global_game_settings.LUTBuilderNeutralLumaHPStart = cb_luma_global_settings.GameSettings.LUTBuilderNeutralLumaHPStart = 0.36f;
      default_luma_global_game_settings.LUTBuilderGradeSMH = cb_luma_global_settings.GameSettings.LUTBuilderGradeSMH = 1.0f;
      default_luma_global_game_settings.LUTBuilderGradeTint = cb_luma_global_settings.GameSettings.LUTBuilderGradeTint = 1.0f;
      default_luma_global_game_settings.LUTBuilderGradeSat = cb_luma_global_settings.GameSettings.LUTBuilderGradeSat = 1.0f;
      default_luma_global_game_settings.PCCLookback = cb_luma_global_settings.GameSettings.PCCLookback = 0.225f;
      default_luma_global_game_settings.PCCHue = cb_luma_global_settings.GameSettings.PCCHue = 0.25f;
      default_luma_global_game_settings.PCCChrominance = cb_luma_global_settings.GameSettings.PCCChrominance = 0.375f;
      default_luma_global_game_settings.PCCChrominanceBoost = cb_luma_global_settings.GameSettings.PCCChrominanceBoost = 1.025f;
      // default_luma_global_game_settings.PCCGuaranteed = cb_luma_global_settings.GameSettings.PCCGuaranteed = 0.25f;
      default_luma_global_game_settings.CGContrast = cb_luma_global_settings.GameSettings.CGContrast = 1.f;
      default_luma_global_game_settings.CGContrastMidGray = cb_luma_global_settings.GameSettings.CGContrastMidGray = 36.f;
      default_luma_global_game_settings.CGSaturation = cb_luma_global_settings.GameSettings.CGSaturation = 1.f;
      default_luma_global_game_settings.CGHighlightsStrength = cb_luma_global_settings.GameSettings.CGHighlightsStrength = 1.f;
      default_luma_global_game_settings.CGHighlightsMidGray = cb_luma_global_settings.GameSettings.CGHighlightsMidGray = 36.f;
      default_luma_global_game_settings.CGShadowsStrength = cb_luma_global_settings.GameSettings.CGShadowsStrength = 1.f;
      default_luma_global_game_settings.CGShadowsMidGray = cb_luma_global_settings.GameSettings.CGShadowsMidGray = 36.f;
      default_luma_global_game_settings.Exposure = cb_luma_global_settings.GameSettings.Exposure = 1.f;
      default_luma_global_game_settings.GammaInfluence = cb_luma_global_settings.GameSettings.GammaInfluence = 1.f;
      default_luma_global_game_settings.MovPeakRatio = cb_luma_global_settings.GameSettings.MovPeakRatio = 1.f;
      default_luma_global_game_settings.MovShoulderPow = cb_luma_global_settings.GameSettings.MovShoulderPow = 3.6f;
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

      //DLSSJitter
      DLSSJitter::Init();
   }

   void OnInitSwapchain(reshade::api::swapchain* swapchain) override
   {
      //log
      message(reshade::log::level::info, "OnInitSwapchain()");
      
      auto& device_data = *swapchain->get_device()->get_private_data<DeviceData>();
#if DEVELOPMENT
      Globals::CountSwapchainChange++;
#endif
      
      // device_data.cb_luma_global_settings_dirty = true;
      // defines_need_recompilation = true;
   }

   DrawOrDispatchOverrideType OnDrawOrDispatch(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, reshade::api::shader_stage stages, const ShaderHashesList<OneShaderPerPipeline>&  original_shader_hashes, bool is_custom_pass, bool& updated_cbuffers, std::function<void()>* original_draw_dispatch_func) override
   {
#if DEVELOPMENT
      //debug skip
      if (Globals::IsSkipOnDrawOrDispatch) return DrawOrDispatchOverrideType::None;
#endif
      
      //game_device_data
      auto& game_device_data = GetGameDeviceData(device_data);
      
      //case: dispatching Light/Reflection Probe Culling
      if (!game_device_data.drawn_probecull && original_shader_hashes.Contains(ShaderHashesLists::ProbeCulling))
      {
         //progress
         game_device_data.drawn_probecull = true;

 #if ENABLE_SR > 0
         #if !DEVELOPMENT
         {
            //disabled: no SR
            if (device_data.sr_type == SR::Type::None || device_data.sr_suppressed) return DrawOrDispatchOverrideType::None;
         }
         #endif
         
         // //Skip: already replaced CPU side
         // if (DLSSJitter::IsReady()) return DrawOrDispatchOverrideType::None;
         
         //get and save PerSceneConsts's subpixelOffset.xy
         ID3D11Buffer* cb_buffer;
         native_device_context->CSGetConstantBuffers(1, 1, &cb_buffer); //Start @ index 1, get 1.

         //failed: no cb
         if (cb_buffer == nullptr)
         {
            ASSERT_MSG(false, "FAILED jitter get");
            return DrawOrDispatchOverrideType::None;
         }

         //get desc
         D3D11_BUFFER_DESC cb_desc = {};
         cb_buffer->GetDesc(&cb_desc);
         const D3D11_USAGE cb_original_usage = cb_desc.Usage; // save before overwrite

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
               // const float4* cb_floats = reinterpret_cast<const float4*>(mapped_cb.pData);
               auto cb_floats = static_cast<const PerSceneConsts*>(mapped_cb.pData);
               
               //@ index 71
               // float2 jitter = float2(cb_floats[71].x, cb_floats[71].y);
               float2 jitter = float2(cb_floats->subpixelOffset.x, cb_floats->subpixelOffset.y);
               /*if (CallOfDutyBlackOps3GameDeviceData::IsValidJitter(jitter)) */ game_device_data.jitter = jitter;

               // //replacement
               // if (game_device_data.CustomJitterCheese > 0.f)
               // {
               //    std::vector<uint8_t> cb_copy(cb_desc.ByteWidth);
               //    memcpy(cb_copy.data(), mapped_cb.pData, cb_desc.ByteWidth);
               //    auto* cb_mutable = reinterpret_cast<PerSceneConsts*>(cb_copy.data());
               //    cb_mutable->subpixelOffset.x = game_device_data.CustomJitterCheese;
               //    cb_mutable->subpixelOffset.y = game_device_data.CustomJitterCheese;
               //    game_device_data.jitter = float2(game_device_data.CustomJitterCheese, game_device_data.CustomJitterCheese);
               //
               //    native_device_context->Unmap(staging_cb, 0);
               //
               //    //dynamic
               //    D3D11_MAPPED_SUBRESOURCE mapped_write = {};
               //    if (SUCCEEDED(native_device_context->Map(cb_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_write)))
               //    {
               //       memcpy(mapped_write.pData, cb_copy.data(), cb_desc.ByteWidth);
               //       native_device_context->Unmap(cb_buffer, 0);
               //    }
               // }
               
               native_device_context->Unmap(staging_cb, 0);
               native_device_context->Unmap(cb_buffer, 0);
            }
         }

         //correct desynced
         DLSSJitter::OnDrawCorrectDesync(game_device_data);

         if (cb_buffer != nullptr) cb_buffer->Release();
         if (staging_cb != nullptr) staging_cb->Release();
#endif
         
         return DrawOrDispatchOverrideType::None;
      }
      
      ////////////////////////////////////////////////////////////////////////////////////////////////////////////
      
      // //case: Lens Flare
      // if (!game_device_data.drawn_tonemap && game_device_data.drawn_probecull && original_shader_hashes.Contains(ShaderHashesLists::LensFlare))
      // {
      //    
      //    return DrawOrDispatchOverrideType::None;
      // }

      ////////////////////////////////////////////////////////////////////////////////////////////////////////////

      //case: drawing Tonemap
      if (!game_device_data.drawn_tonemap && original_shader_hashes.Contains(ShaderHashesLists::Tonemap))
      {
         //progress
         game_device_data.drawn_tonemap = true;
         
         return DrawOrDispatchOverrideType::None;
      }
      
      ////////////////////////////////////////////////////////////////////////////////////////////////////////////

#if ENABLE_SR > 0
      //case: disable SMAA calculations when SR
      if (!game_device_data.drawn_smaat2x && game_device_data.drawn_tonemap &&
         original_shader_hashes.Contains(ShaderHashesLists::SMAAT2XPrep))
      {
         //progress
         game_device_data.drawn_smaat2xprep = true;

         //skip if SR
         return (device_data.sr_type != SR::Type::None && !device_data.sr_suppressed) ? DrawOrDispatchOverrideType::Skip : DrawOrDispatchOverrideType::None;
      }
#endif
      
      ////////////////////////////////////////////////////////////////////////////////////////////////////////////

      //case: drawing SMAA T2X (SR replacement?)
      if (game_device_data.drawn_smaat2xprep && !game_device_data.drawn_smaat2x && original_shader_hashes.Contains(ShaderHashesLists::SMAAT2X))
      {
         //progress
         game_device_data.drawn_smaat2x = true;
         device_data.taa_detected = true;

#if ENABLE_SR > 0
         //disabled: SR
         if (device_data.sr_type == SR::Type::None || device_data.sr_suppressed) return DrawOrDispatchOverrideType::None;

#if DEVELOPMENT
         //repeating jitter
         if (/*!DLSSJitter::IsReady() && */game_device_data.jitter_prev.x == game_device_data.jitter.x)
         {
            Globals::CountSRJitterErrorRepeat++;
         }
#endif
         
         //no T2X jitter for SR: start continuing
         if (!DLSSJitter::IsReady() && !CallOfDutyBlackOps3GameDeviceData::IsValidJitter(game_device_data.jitter))
         {
            float n = -game_device_data.jitter_prev.x;
            game_device_data.jitter = float2(n, n);
#if DEVELOPMENT
            Globals::CountSRJitterContinued++;
#endif
         }
         
         //get tex
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
         //view fetch 0
         native_device_context->PSGetShaderResources(0, 1, &game_device_data.tex_color_srv);

         //view fetch 11-14
         // native_device_context->PSGetShaderResources(11, 1, &game_device_data.tex_velocity_srv);
         // native_device_context->PSGetShaderResources(14, 1, &game_device_data.tex_depth_srv);
         {
            ID3D11ShaderResourceView* tmp[4];
            native_device_context->PSGetShaderResources(11, 4, tmp);
            game_device_data.tex_velocity_srv.reset(tmp[0]);
            game_device_data.tex_depth_srv.reset(tmp[3]);
            // if (tmp[0]) tmp[0]->Release();
            if (tmp[1]) tmp[1]->Release();
            if (tmp[2]) tmp[2]->Release();
            // if (tmp[3]) tmp[3]->Release();
         }
         
         //view get resource
         game_device_data.tex_color_srv->GetResource(&game_device_data.tex_color_res);
         game_device_data.tex_velocity_srv->GetResource(&game_device_data.tex_velocity_res);
         game_device_data.tex_depth_srv->GetResource(&game_device_data.tex_depth_res);
         
         //resource get tex — compare by resource pointer (stable), not SRV pointer (new ref each call)
         // if (game_device_data.tex_color_res.get() != game_device_data.tex_color_res_prev) //TODO: doesn't work
         // {
            auto hr = game_device_data.tex_color_res->QueryInterface(&game_device_data.tex_color_tex2d);
            ASSERT_ONCE(SUCCEEDED(hr));
            game_device_data.tex_color_tex2d->GetDesc(&game_device_data.tex_color_desc);
#if DEVELOPMENT
            Globals::CountSRQueryInterface++;
#endif
         // }
         // game_device_data.tex_color_res_prev = game_device_data.tex_color_res.get();

         //sr_instance_data
         auto* sr_instance_data = device_data.GetSRInstanceData();
         ASSERT_ONCE(sr_instance_data);
         
         //skip by too small?
         bool skip_dlss = game_device_data.tex_color_desc.Width  < sr_instance_data->min_resolution ||
                          game_device_data.tex_color_desc.Height < sr_instance_data->min_resolution;

         //check desired output resolution
         uint output_resolution_x = Globals::SRIsSwapchainOutputSize ? (uint)device_data.output_resolution.x : game_device_data.tex_color_desc.Width;
         uint output_resolution_y = Globals::SRIsSwapchainOutputSize ? (uint)device_data.output_resolution.y : game_device_data.tex_color_desc.Height;
         bool is_internal_res_bigger_than_swapchain = output_resolution_x < game_device_data.tex_color_desc.Width && output_resolution_y < game_device_data.tex_color_desc.Height;
         if (is_internal_res_bigger_than_swapchain) //DLSS/DLAA doesnt support output > render, so max().
         {
            output_resolution_x =  game_device_data.tex_color_desc.Width;
            output_resolution_y =  game_device_data.tex_color_desc.Height;
         }
            
         //if exists, then check if prev is valid
         D3D11_TEXTURE2D_DESC dlss_output_texture_desc;
         constexpr bool dlss_output_changed = false; //TODO: remove
         bool sr_output_color_get = device_data.sr_output_color.get() != nullptr;
         // if (sr_output_color_get) //TODO: doesn't work
         // {
         //    //get desc
         //    device_data.sr_output_color->GetDesc(&dlss_output_texture_desc);
         //
         //    //dlss_output_changed: res
         //    if (!is_internal_res_bigger_than_swapchain)
         //       sr_output_color_get = dlss_output_texture_desc.Width == output_resolution_x && dlss_output_texture_desc.Height == output_resolution_y;
         //    
         //    // //dlss_output_changed: miss match format
         //    // dlss_output_changed |= dlss_output_texture_desc.Format != game_device_data.tex_color_desc.Format;
         // }
         
         //null or dlss_output_changed
         if (dlss_output_changed || !sr_output_color_get)
         {
            //release prev
            CallOfDutyBlackOps3GameDeviceData::HardResetSR(device_data, game_device_data);
            
            //copy (like an idiot) and change for SR
            dlss_output_texture_desc.Width          = output_resolution_x;
            dlss_output_texture_desc.Height         = output_resolution_y;
            dlss_output_texture_desc.MipLevels      =  game_device_data.tex_color_desc.MipLevels; 
            dlss_output_texture_desc.ArraySize      =  game_device_data.tex_color_desc.ArraySize;
            dlss_output_texture_desc.Format         =  game_device_data.tex_color_desc.Format;
            dlss_output_texture_desc.SampleDesc     =  game_device_data.tex_color_desc.SampleDesc;
            dlss_output_texture_desc.Usage          =  game_device_data.tex_color_desc.Usage;
            dlss_output_texture_desc.BindFlags      =  game_device_data.tex_color_desc.BindFlags | D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET /*| D3D11_BIND_DECODER*/; //TODO: Going schizo. What is needed?
            dlss_output_texture_desc.CPUAccessFlags =  game_device_data.tex_color_desc.CPUAccessFlags;
            dlss_output_texture_desc.MiscFlags      =  game_device_data.tex_color_desc.MiscFlags;
            
            //create new output texture with correct res and uav support for SR.
            auto hr1 = native_device->CreateTexture2D(&dlss_output_texture_desc, nullptr, &device_data.sr_output_color);
            ASSERT_ONCE(SUCCEEDED(hr1));
            
            //create new ShaderResourceView for shader input binding.
            auto hr2 = native_device->CreateShaderResourceView(device_data.sr_output_color.get(), nullptr, &game_device_data.sr_output_color_resource_view);
            ASSERT_ONCE(SUCCEEDED(hr2));

            //stats
#if DEVELOPMENT
            Globals::CountSRTexChange++;
#endif
         }

         //res_height_render_vs_output
         game_device_data.res_height_render_vs_output = uint2( game_device_data.tex_color_desc.Height, output_resolution_y);

         //skip (if failed, or changed output)
         if (skip_dlss)
         {
            device_data.has_drawn_sr = false;
            return DrawOrDispatchOverrideType::None;
         }

         //SettingsData
         SR::SettingsData settings_data;
         settings_data.output_width  = output_resolution_x; // game_device_data.sr_output_color_desc.Width;
         settings_data.output_height = output_resolution_y; // game_device_data.sr_output_color_desc.Height;
         settings_data.render_width  =  game_device_data.tex_color_desc.Width;
         settings_data.render_height =  game_device_data.tex_color_desc.Height;
         settings_data.inverted_depth = true/*Globals::SRIsDepthInverse*/;
         settings_data.hdr = cb_luma_global_settings.DisplayMode != DisplayModeType::SDR;
            Globals::SRIsHDR = settings_data.hdr;
         settings_data.mvs_x_scale = -20/*Globals::SRMvsScale*/;
         settings_data.mvs_y_scale = -20/*Globals::SRMvsScale*/;
         settings_data.mvs_jittered = Globals::SRMvsJittered;
         settings_data.auto_exposure = Globals::SRAutoExposure;
         settings_data.render_preset = dlss_render_preset;
         sr_implementations[device_data.sr_type]->UpdateSettings(sr_instance_data, native_device_context, settings_data);

         //DrawData
         SR::SuperResolutionImpl::DrawData draw_data;
         draw_data.render_width = settings_data.render_width;
         draw_data.render_height = settings_data.render_height;
         draw_data.near_plane = 0/*Globals::SRNearPlane*/;
         draw_data.far_plane = 1/*Globals::SRFarPlane*/;
         draw_data.source_color = game_device_data.tex_color_res.get(); //TODO: inefficient if different?
         draw_data.output_color = device_data.sr_output_color.get(); //TODO: inefficient if different?
         draw_data.motion_vectors = game_device_data.tex_velocity_res.get();
         draw_data.depth_buffer = game_device_data.tex_depth_res.get();
         const bool dlss_jitter = false ;/*DLSSJitter::IsReady();*/
         draw_data.jitter_x = (dlss_jitter ? DLSSJitter::jitter.x : game_device_data.jitter.x) /** Globals::SRJitterMultiplier.x*/;
         draw_data.jitter_y = (dlss_jitter ? DLSSJitter::jitter.y : game_device_data.jitter.y) /** Globals::SRJitterMultiplier.y*/;
         draw_data.pre_exposure = Globals::SRExposure;
         draw_data.user_sharpness = Globals::SRSharpness;
         // draw_data.vert_fov = Globals::SRVertCameraFOV;
         
         //force reset?
         bool reset_dlss = device_data.force_reset_sr || dlss_output_changed;
         draw_data.reset = reset_dlss;

         //reset the reset
         device_data.force_reset_sr = false;

         //skip: hasn't succeed back to back.
         Globals::SRSuccessCount++;
         if (Globals::SRSuccessCount < 60) return DrawOrDispatchOverrideType::None;

         //DRAW!
#if DEVELOPMENT
         if (!Globals::IsSkipDLSSDraw)
#endif
            device_data.has_drawn_sr = sr_implementations[device_data.sr_type]->Draw(sr_instance_data, native_device_context, draw_data);

         //SUCCESS:
         if (device_data.has_drawn_sr)
         {            
            //replace "colorTex" t0
            native_device_context->PSSetShaderResources(0, 1, &game_device_data.sr_output_color_resource_view);
         } 
         // FAILED:
         else device_data.force_reset_sr = true;
#endif

         //let the SMAA shader draw (for Tradeoff encoding)
         return DrawOrDispatchOverrideType::None;
      }

      ////////////////////////////////////////////////////////////////////////////////////////////////////////////

      //case: drawing final
      if (!game_device_data.drawn_final && original_shader_hashes.Contains(ShaderHashesLists::Final))
      {
         //progress
         game_device_data.drawn_final = true;
         device_data.has_drawn_main_post_processing = true;
         
         return DrawOrDispatchOverrideType::None;
      }

      ////////////////////////////////////////////////////////////////////////////////////////////////////////////

      //HDTV rec.709 decode
      if (game_device_data.drawn_final && original_shader_hashes.Contains(ShaderHashesLists::Rec709))
      {
         //progress
         game_device_data.drawn_hdtv = true;
         
         return DrawOrDispatchOverrideType::Skip; //Skip this trash! It creates another 8bit tex to decode srgb and encode rec709.
      }
      
      ////////////////////////////////////////////////////////////////////////////////////////////////////////////
      
      //case: No Fullscreen Blur
      if (!Globals::IsFullscreenBlur && original_shader_hashes.Contains(ShaderHashesLists::FullscreenBlur)) 
          return DrawOrDispatchOverrideType::Skip;

      //case: No UI after final.
      if (!Globals::IsUi && game_device_data.drawn_final) 
         return DrawOrDispatchOverrideType::Skip;

      //case: normal
      return DrawOrDispatchOverrideType::None;
   }

   void OnPresent(ID3D11Device* native_device, DeviceData& device_data) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);

      //mipmap offset
      if (!custom_texture_mip_lod_bias_offset && device_data.has_drawn_sr)
         device_data.texture_mip_lod_bias_offset = SR::GetMipLODBias(game_device_data.res_height_render_vs_output.x, game_device_data.res_height_render_vs_output.y);
      device_data.texture_mip_lod_bias_offset = 0;

      //sr enabled?
      bool sr_enabled = device_data.sr_type != SR::Type::None;

      // //force shader def
      // ShaderDefineInfo::Set(ShaderDefineInfo::CUSTOM_SR, sr_enabled); //TODO: doesnt do anything!

      // //CUSTOM_SR
      // {
      //    bool isOnSetting = device_data.sr_type != SR::Type::None;
      //    bool isOnDef = ShaderDefineInfo::Get(ShaderDefineInfo::CUSTOM_SR) > 0;
      //    if (isOnSetting != isOnDef)
      //    {
      //       char def_char = isOnSetting ? '1' : '0';
      //       ShaderDefineInfo::Set(ShaderDefineInfo::CUSTOM_SR, def_char);
      //
      //       //force reset
      //       if (isOnSetting) device_data.force_reset_sr = true;
      //    }
      // }
      //
      // //CUSTOM_SDRSR
      // {
      //    bool isOnSetting = cb_luma_global_settings.DisplayMode == DisplayModeType::SDR;
      //    bool isOnDef = ShaderDefineInfo::Get(ShaderDefineInfo::CUSTOM_SDR) > 0;
      //    if (isOnSetting != isOnDef)
      //    {
      //       char def_char = isOnSetting ? '1' : '0';
      //       ShaderDefineInfo::Set(ShaderDefineInfo::CUSTOM_SDR, def_char);
      //    }
      // }
      
      //clear SR
      if (sr_enabled && !game_device_data.drawn_smaat2x) device_data.force_reset_sr = true;
      
      //reset
      game_device_data.Reset(device_data.force_reset_sr);

      //DLSSJitter
      DLSSJitter::OnPresent(device_data, game_device_data);
   }

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
      reshade::get_config_value(runtime, NAME, "SDRTonemapFloorRaiseScale", cb_luma_global_settings.GameSettings.SDRTonemapFloorRaiseScale);
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
      reshade::get_config_value(runtime, NAME, "PCCLookback", cb_luma_global_settings.GameSettings.PCCLookback);
      reshade::get_config_value(runtime, NAME, "PCCHue", cb_luma_global_settings.GameSettings.PCCHue);
      reshade::get_config_value(runtime, NAME, "PCCChrominance", cb_luma_global_settings.GameSettings.PCCChrominance);
      reshade::get_config_value(runtime, NAME, "PCCChrominanceBoost", cb_luma_global_settings.GameSettings.PCCChrominanceBoost);
      // reshade::get_config_value(runtime, NAME, "PCCGuaranteed", cb_luma_global_settings.GameSettings.PCCGuaranteed);
      reshade::get_config_value(runtime, NAME, "CGContrast", cb_luma_global_settings.GameSettings.CGContrast);
      reshade::get_config_value(runtime, NAME, "CGContrastMidGray", cb_luma_global_settings.GameSettings.CGContrastMidGray);
      reshade::get_config_value(runtime, NAME, "CGSaturation", cb_luma_global_settings.GameSettings.CGSaturation);
      reshade::get_config_value(runtime, NAME, "CGHighlightsStrength", cb_luma_global_settings.GameSettings.CGHighlightsStrength);
      reshade::get_config_value(runtime, NAME, "CGHighlightsMidGray", cb_luma_global_settings.GameSettings.CGHighlightsMidGray);
      reshade::get_config_value(runtime, NAME, "CGShadowsStrength", cb_luma_global_settings.GameSettings.CGShadowsStrength);
      reshade::get_config_value(runtime, NAME, "CGShadowsMidGray", cb_luma_global_settings.GameSettings.CGShadowsMidGray);
      reshade::get_config_value(runtime, NAME, "Exposure", cb_luma_global_settings.GameSettings.Exposure);
      reshade::get_config_value(runtime, NAME, "GammaInfluence", cb_luma_global_settings.GameSettings.GammaInfluence);
      reshade::get_config_value(runtime, NAME, "MovPeakRatio", cb_luma_global_settings.GameSettings.MovPeakRatio);
      reshade::get_config_value(runtime, NAME, "MovShoulderPow", cb_luma_global_settings.GameSettings.MovShoulderPow);

      reshade::get_config_value(runtime, NAME, "UIIsAdvanced", Globals::UIIsAdvanced);
      reshade::get_config_value(runtime, NAME, "IsUi", Globals::IsUi);
      reshade::get_config_value(runtime, NAME, "IsFullscreenBlur", Globals::IsFullscreenBlur);
      // reshade::get_config_value(runtime, NAME, "SRIsDepthInverse", Globals::SRIsDepthInverse);
      reshade::get_config_value(runtime, NAME, "SRIsHDR", Globals::SRIsHDR);
      reshade::get_config_value(runtime, NAME, "SRExposure", Globals::SRExposure);
      reshade::get_config_value(runtime, NAME, "SRAutoExposure", Globals::SRAutoExposure);
      reshade::get_config_value(runtime, NAME, "SRSharpness", Globals::SRSharpness);
      // reshade::get_config_value(runtime, NAME, "SRNearPlane", Globals::SRNearPlane);
      // reshade::get_config_value(runtime, NAME, "SRFarPlane", Globals::SRFarPlane);
      // reshade::get_config_value(runtime, NAME, "SRJitterMultiplier", Globals::SRJitterMultiplier);
      // reshade::get_config_value(runtime, NAME, "SRMvsScale", Globals::SRMvsScale);
      reshade::get_config_value(runtime, NAME, "SRMvsJittered", Globals::SRMvsJittered);
      // reshade::get_config_value(runtime, NAME, "SRVertCameraFOV", Globals::SRVertCameraFOV);
      reshade::get_config_value(runtime, NAME, "SRIsSwapchainOutputSize", Globals::SRIsSwapchainOutputSize);

      DLSSJitter::OnLoadSettings(runtime);

      if (custom_sdr_gamma == 0) custom_sdr_gamma = 2.2f;
      reshade::get_config_value(runtime, NAME, "EOTFGammaCorrection", custom_sdr_gamma);
      ShaderDefineInfo::Set(GAMMA_CORRECTION_TYPE_HASH, custom_sdr_gamma > 0 ? '1' : '0');

      ShaderDefineInfo::Set(ShaderDefineInfo::CUSTOM_SR, sr_user_type != SR::UserType::None);
      ShaderDefineInfo::Set(ShaderDefineInfo::CUSTOM_SDR, cb_luma_global_settings.DisplayMode == DisplayModeType::SDR);
      
      message(reshade::log::level::info, ("LoadConfigs() GAMMA_CORRECTION_TYPE: " + std::to_string(ShaderDefineInfo::Get(GAMMA_CORRECTION_TYPE_HASH))).c_str());
      message(reshade::log::level::info, ("LoadConfigs() CUSTOM_SR: " + std::to_string(ShaderDefineInfo::Get(ShaderDefineInfo::CUSTOM_SR))).c_str());
      message(reshade::log::level::info, ("LoadConfigs() CUSTOM_SDR: " + std::to_string(ShaderDefineInfo::Get(ShaderDefineInfo::CUSTOM_SDR))).c_str());
      
      defines_need_recompilation = true;
   }
   
   void DrawImGuiSettings(DeviceData& device_data) override
   {
      reshade::api::effect_runtime* runtime = nullptr;

      auto& game_device_data = GetGameDeviceData(device_data);
      bool is_disabled; //for Begin/EndDisabled();

      //////////////////////////////////////////////////////////////////////////////////////////////////////
      
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
      
      ImGui::Separator(); ////////////////////////////////////////////////////////////////////////////////////
      
      ImGui::Text("Notice:");

      //SR on but not SMAA T2X
      if (device_data.sr_type != SR::Type::None && game_device_data.drawn_tonemap_prev && !game_device_data.drawn_smaat2x_prev)
      {
         ImGui::Bullet(); ImGui::SameLine();
         ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
         ImGui::SmallButton(ICON_FK_WARNING); ImGui::SameLine();
         ImGui::SmallButton(ICON_FK_WARNING); ImGui::SameLine();
         ImGui::SmallButton(ICON_FK_WARNING); ImGui::SameLine();
         ImGui::PopStyleColor();
         ImGui::Text("Super Resolution is on, but SMAA T2x (NOT Filmic) isn't selected in-game!");
      }

      //HDTV vs sRGB
      if (game_device_data.drawn_tonemap_prev)
      {
         auto def_hdtv = ShaderDefineInfo::Get(ShaderDefineInfo::CUSTOM_HDTVREC709);
         if (!game_device_data.drawn_hdtv_prev && def_hdtv)
         {
            ImGui::Bullet(); ImGui::SameLine();
            if (ImGui::Button("Turn off CUSTOM_HDTVREC709")) ShaderDefineInfo::Set(ShaderDefineInfo::CUSTOM_HDTVREC709, '0'); ImGui::SameLine();
            ImGui::Text("In-game Display Gamma settings is sRGB.");
         }
         else if (game_device_data.drawn_hdtv_prev && !def_hdtv)
         {
            ImGui::Bullet(); ImGui::SameLine();
            if (ImGui::Button("Turn on CUSTOM_HDTVREC709")) ShaderDefineInfo::Set(ShaderDefineInfo::CUSTOM_HDTVREC709, '1'); ImGui::SameLine();
            ImGui::Text("In-game Display Gamma settings is rec.709.");
         }
      }

      //SDR Mode
      if (cb_luma_global_settings.DisplayMode == DisplayModeType::SDR)
      {
         ImGui::Bullet(); ImGui::SameLine();
         ImGui::Text("Fallback SDR mode on.\n\t\t- In-game Brightness slider settings will apply (where 0 = exactly sRGB).\n\t\t- Swapchain is still HDR, so ReShade UI will be blown out if not darkened to 80 nits.");
      }

      //SR Reset
      if (device_data.sr_type != SR::Type::None)
      {
         ImGui::Bullet(); ImGui::SameLine();
         ImGui::Text("Super Resolution On:");
         // ImGui::SameLine(); if (ImGui::Button("Reset (Soft)")) device_data.force_reset_sr = true; ImGui::SameLine();
         ImGui::SameLine(); if (ImGui::Button("Reset")) { device_data.force_reset_sr = true; CallOfDutyBlackOps3GameDeviceData::HardResetSR(device_data, game_device_data); }

         const std::string s = std::string(Globals::SRIsSwapchainOutputSize ? "Internal --> Output" : "Internal --> Internal") + " (" + std::to_string((int)game_device_data.res_height_render_vs_output.x).c_str() + std::string("p --> ") + std::to_string((int)game_device_data.res_height_render_vs_output.y).c_str() + "p)";
         ImGui::SameLine(); if (ImGui::Button(s.c_str()))
         {
            Globals::SRIsSwapchainOutputSize = !Globals::SRIsSwapchainOutputSize;
            reshade::set_config_value(runtime, NAME, "SRIsSwapchainOutputSize", Globals::SRIsSwapchainOutputSize);
            CallOfDutyBlackOps3GameDeviceData::HardResetSR(device_data, game_device_data);
         }
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Toggle whether the super resolution output should compute the upscaling (Internal --> Output), or just rely on texture sampling to upscale (Internal --> Internal, inferior but faster, probably only good for debug).");

         //bold text
         ImGui::TextColored(ImVec4(255.f/255.f, 230.f/255.f, 120.f/255.f, 1.0f), "\t\t- RESPONDS TO IN-GAME INTERNAL RESOLUTION DROPDOWN!!!\n\t\t- See \"Super Resolution Additional Jitter\" section below.");
         
         ImGui::BulletText("Super Resolution Misc Info:");
         ImGui::Text("\t\t- Crash prone?\n\t\t- SMAA T2x Filmic may be good enough great at 4k.\n\t\t- Use OptiScaler in conjunction for FSR (rn, it breaks output until restart).\n\t\t- Motion Blur will cause fireflies.");
      }

      //Intended compilation error
      // if (device_data.sr_type == SR::Type::None) ImGui::BulletText("Super Resolution is off. Please ignore \"Reload Shaders\" errors above.");
      // if (cb_luma_global_settings.DisplayMode == DisplayModeType::SDR) ImGui::BulletText("Fallback SDR mode is on. Please ignore \"Reload Shaders\" errors above.");

      ImGui::Separator(); ////////////////////////////////////////////////////////////////////////////////////

      if (ImGui::CollapsingHeader("Gamma"))
      {
         //link test
         if (ImGui::Button("Gamma Mismatch Explanation & Correction Test (Google Slides)"))
            Website::OpenWebsite("https://docs.google.com/presentation/d/e/2PACX-1vSXeLHlbm6repcS7fels1-SXYGRmzziRrnuJ8nDO8J5rsWV3dT1-nVyCKp0Tj_stwx-9qlCI-N6rYIT/pub?start=true&loop=false&slide=id.g3e007eafba8_0_0");
         
         //Gamma Correction      
         bool is_disabled_sdr = cb_luma_global_settings.DisplayMode == DisplayModeType::SDR; //completely disable for SDR
         if (is_disabled_sdr) ImGui::BeginDisabled();
         {
            //save prev
            float gamma_prev = custom_sdr_gamma;

            //if SDR mode, force off!
            if (is_disabled_sdr && ShaderDefineInfo::Get(GAMMA_CORRECTION_TYPE_HASH) == 1)
            {
               custom_sdr_gamma = 0.f;
               ShaderDefineInfo::Set(GAMMA_CORRECTION_TYPE_HASH, 0);
            }

            //toggle
            bool is_on = custom_sdr_gamma > 0.f;
            if (ImGui::Checkbox("EOTF / Gamma Correction Enabled", &is_on)) custom_sdr_gamma = is_on ? 2.2f : 0.f;
      
            is_disabled = ShaderDefineInfo::Get(GAMMA_CORRECTION_TYPE_HASH) == 0;
            if (is_disabled) ImGui::BeginDisabled();
            {
               //slider
               bool changed_gamma = ImGui::SliderFloat("Correction Gamma", &custom_sdr_gamma, 2.2f, 3.0f, "%.1f");
               if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("EOTF / Gamma Correction.\nUse if shadows are raised, and display or OS (Windows) doesn't lower already.\n2.2 will match SDR; Shouldn't need anything more honestly.");

               //save custom_sdr_gamma & recompile if changed
               changed_gamma |= DrawResetButton<float, false>(custom_sdr_gamma, 2.2f, nullptr, runtime);
               changed_gamma |= gamma_prev != custom_sdr_gamma; //bruh case
               if (changed_gamma)
               {
                  defines_need_recompilation = true;
                  reshade::set_config_value(runtime, NAME, "EOTFGammaCorrection", custom_sdr_gamma);
               }

               //force define because its trolling sometimes
               if (bool is_define_on = ShaderDefineInfo::Get(GAMMA_CORRECTION_TYPE_HASH) == 1; is_define_on != (custom_sdr_gamma > 0.f))
                  ShaderDefineInfo::Set(GAMMA_CORRECTION_TYPE_HASH, is_define_on ? '0' : '1');
            
               //CUSTOM_GAMMA_CORRECTION_MODE dropdown
               {
                  ShaderDefineInfo::UIDropDown(ShaderDefineInfo::CUSTOM_GAMMA_CORRECTION_MODE, "Gamma Correction Mode",
                     {"Per-Channel (Hue Shifts / Vanilla)", "Perceptual (Hue Corrected)"},
                     "How should the gamma correction operate?\n\nPer-Channel is Vanilla Brightness Slider-like, with hue shifting shadows.\nPerceptual retains the hues of the original sRGB gamma output, so only darkening luminance.");
               }
            
               //Gamma Influence
               if (ImGui::SliderFloat("Gamma Influence", &cb_luma_global_settings.GameSettings.GammaInfluence, 0.f, 3.f))
                  reshade::set_config_value(runtime, NAME, "GammaInfluence", cb_luma_global_settings.GameSettings.GammaInfluence);
               if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Replacement for the in-game Brightness slider for HDR.\n(Scales color to value, decodes sRGB or rec.709, then inverse the scale back.)");
               DrawResetButton(cb_luma_global_settings.GameSettings.GammaInfluence, default_luma_global_game_settings.GammaInfluence, "GammaInfluence", runtime);

            }
            if (is_disabled) ImGui::EndDisabled();
         }
         if (is_disabled_sdr) ImGui::EndDisabled();

         //CUSTOM_HDTVREC709
         {
            bool b = ShaderDefineInfo::Get(ShaderDefineInfo::CUSTOM_HDTVREC709) == 1;
            if (ImGui::Checkbox("HDTV rec.709", &b)) ShaderDefineInfo::ToggleBool(ShaderDefineInfo::CUSTOM_HDTVREC709);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Replacement for in-game Display Mode setting.");
         }

         if (ImGui::SliderFloat("SDR Tonemap Black Floor Raise", &cb_luma_global_settings.GameSettings.SDRTonemapFloorRaiseScale, 0.8f, 1.f, "%.4f"))
            reshade::set_config_value(runtime, NAME, "SDRTonemapFloorRaiseScale", cb_luma_global_settings.GameSettings.SDRTonemapFloorRaiseScale);
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Scaler/Multiplier on the black floor raise value of the tonemapper, where 1 is vanilla.\n\nMost vanilla map has the black floor raised a specific amount by LUT, but modded maps can do whatever.\nYou can use this to combat raised blacks per map.");
         DrawResetButton(cb_luma_global_settings.GameSettings.SDRTonemapFloorRaiseScale, default_luma_global_game_settings.SDRTonemapFloorRaiseScale, "SDRTonemapFloorRaiseScale", runtime);
      }
      
      // ImGui::NewLine(); ////////////////////////////////////////////////////////////////////////////////////

      if (ImGui::CollapsingHeader("General Post Processing"))
      {
         if (ImGui::SliderFloat("Bloom", &cb_luma_global_settings.GameSettings.Bloom, 0.f, 2.f))
            reshade::set_config_value(runtime, NAME, "Bloom", cb_luma_global_settings.GameSettings.Bloom);
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Bloom multiplier.\n\nDisclaimer: This effect is composited SDR!\nThe game's hue for highlights is crucially basked by bloom (see zombies menu campfire).\nCompositing it in HDR will sadly ruin it.\nCustom maps with increased highlights contrast will start clipping.");
         DrawResetButton(cb_luma_global_settings.GameSettings.Bloom, default_luma_global_game_settings.Bloom, "Bloom", runtime);
      
         if (ImGui::SliderFloat("Lens Dirt / Flare", &cb_luma_global_settings.GameSettings.LensFlare, 0.f, 2.f))
            reshade::set_config_value(runtime, NAME, "LensFlare", cb_luma_global_settings.GameSettings.LensFlare);
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Lens Dirt / Flare billboards and overlays strength.\n\nDisclaimer: This effect is composited SDR!\nThe worst case is the sun being a billboard.\nIn SDR, it'll clip as intended, but HDR will suck.\nThere is no fix except manually editing for each flare, which is an unfeasible task because mods.");
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
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Motion blur strength.");
         DrawResetButton(cb_luma_global_settings.GameSettings.MotionBlur, default_luma_global_game_settings.MotionBlur, "MotionBlur", runtime);

         //CUSTOM_MB_QUALITY
         {
            ShaderDefineInfo::UIDropDown(ShaderDefineInfo::CUSTOM_MB_QUALITY, "Motion Blur Quality",
               {"6 Samples (Original)", "16 Samples", "24 Samples (High)", "32 Samples", "48 Samples", "64 Samples", "128 Samples (Uhhh)"},
               "Motion blur quality. Increase to try and reduce fireflies with a performance cost.");
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
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("RCAS sharpening strength, done after anti-aliasing.");
         DrawResetButton(cb_luma_global_settings.GameSettings.RCAS, default_luma_global_game_settings.RCAS, "RCAS", runtime);

         //CUSTOM_CHROMABER
         ShaderDefineInfo::UIToggleCheckmark(ShaderDefineInfo::CUSTOM_CHROMABER, "Chromatic Aberration",
            "Toggle chromatic aberration.\nSeldom usage, only in main menu.");
      
         // //ucs operator
         // {
         //    int sel = ShaderDefineInfo::Get(ShaderDefineInfo::CUSTOM_UCS_TYPE);
         //    const char* items[] = { "OkLAB", "JzAzBz", "ICtCp" };
         //    if (ImGui::Combo("Uniform Color Space", &sel, items, IM_ARRAYSIZE(items)))
         //       ShaderDefineInfo::Set(ShaderDefineInfo::CUSTOM_UCS_TYPE, sel);
         //    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Working perceptual/uniform color space.\nUsed to perceptually blend between colors, each gives slightly different hues and chrominance.");
         // }
      }

      // ImGui::NewLine(); ////////////////////////////////////////////////////////////////////////////////////

      if (ImGui::CollapsingHeader("Super Resolution Additional Jitter"))
         DLSSJitter::OnUI(runtime, device_data, game_device_data);


      // ImGui::NewLine(); ////////////////////////////////////////////////////////////////////////////////////
      
      if (!Globals::UIIsAdvanced)
      {
         ImGui::Separator(); ////////////////////////////////////////////////////////////////////////////////////
         if (ImGui::Checkbox("Show Advanced Settings", &Globals::UIIsAdvanced))
            reshade::set_config_value(runtime, NAME, "UIIsAdvanced", Globals::UIIsAdvanced);

#if DEVELOPMENT
         ImGui::Separator();
#endif
         return;
      }
      
      if (ImGui::CollapsingHeader("HDR Tonemapper"))
      {
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
            if (ImGui::SliderFloat("HDR Tonemapper Expected Max", &cb_luma_global_settings.GameSettings.TonemapperMaxExpected, 10000.f, tonemap_def == 1 ? 100000.f : 1000000.f, "%.0f"))
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

         is_disabled = tonemap_def == 0;
         int scaling_def = ShaderDefineInfo::Get(ShaderDefineInfo::CUSTOM_TONEMAP_SCALING);
         if (!is_disabled)
         {
            const char* items[] = { "Luminance (Natural)", "Max Channel (Saturation Preserve)" };
            if (ImGui::Combo("HDR Tonemapper Scaling", &scaling_def, items, IM_ARRAYSIZE(items)))
               ShaderDefineInfo::Set(ShaderDefineInfo::CUSTOM_TONEMAP_SCALING, scaling_def);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("The \"pivot\" of for the tonemapper to use and scale color.");
         }

         is_disabled = tonemap_def == 0 || scaling_def != 0;
         if (!is_disabled)
         {
            int def = ShaderDefineInfo::Get(ShaderDefineInfo::CUSTOM_TONEMAP_CLAMP);
            const char* items[] = { "Clip (Off)", "Clip (via Clamp)", "Max Channel Scale Down (Band-aid Saturation Preserve)" };
            if (ImGui::Combo("HDR Tonemapper Luminance Scaling Clamp", &def, items, IM_ARRAYSIZE(items)))
               ShaderDefineInfo::Set(ShaderDefineInfo::CUSTOM_TONEMAP_CLAMP, def);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("How should overshoots from luminance scaling be handled.");
         }

         //Exposure
         is_disabled = cb_luma_global_settings.DisplayMode == DisplayModeType::SDR;
         if (is_disabled) { ImGui::BeginDisabled(); cb_luma_global_settings.GameSettings.Exposure = 1.f; }
         {
            if (ImGui::SliderFloat("Exposure", &cb_luma_global_settings.GameSettings.Exposure, 0.f, 3.f))
               reshade::set_config_value(runtime, NAME, "Exposure", cb_luma_global_settings.GameSettings.Exposure);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Exposure before HDR tonemap.\nAlternative to Scene Paper White without shifting EOTF / Gamma Correction influence range.");
            DrawResetButton(cb_luma_global_settings.GameSettings.Exposure, default_luma_global_game_settings.Exposure, "Exposure", runtime);
         }
         if (is_disabled) ImGui::EndDisabled();
      }

      // ImGui::NewLine(); ////////////////////////////////////////////////////////////////////////////////////

      if (ShaderDefineInfo::Get(ShaderDefineInfo::CUSTOM_SDR) == 0)
         if (ImGui::CollapsingHeader("RenoDX HDR Color Grading"))
         {
            // if (ImGui::Button("Toggle Color Grading")) ShaderDefineInfo::ToggleBool(ShaderDefineInfo::CUSTOM_COLORGRADE);
            bool def = ShaderDefineInfo::UIToggleCheckmark(ShaderDefineInfo::CUSTOM_COLORGRADE, "RenoDX Pre-UI Luminance Color Grading", "Custom color grading from RenoDX.\nKinda like an audio equalizer but for luminance.");
            
            is_disabled = ShaderDefineInfo::Get(ShaderDefineInfo::CUSTOM_COLORGRADE) == 0;
            if (is_disabled) ImGui::BeginDisabled();
      
            if (ImGui::SliderFloat("CG: Saturation", &cb_luma_global_settings.GameSettings.CGSaturation, 0.f, 2.f, "%.4f"))
               reshade::set_config_value(runtime, NAME, "CGSaturation", cb_luma_global_settings.GameSettings.CGSaturation);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Final saturation before HDR tonemap.");
            DrawResetButton(cb_luma_global_settings.GameSettings.CGSaturation, default_luma_global_game_settings.CGSaturation, "CGSaturation", runtime);
      
            if (ImGui::SliderFloat("CG: Contrast", &cb_luma_global_settings.GameSettings.CGContrast, 0.f, 2.f, "%.4f"))
               reshade::set_config_value(runtime, NAME, "CGContrast", cb_luma_global_settings.GameSettings.CGContrast);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("RenoDX power based contrast before HDR tonemap.");
            DrawResetButton(cb_luma_global_settings.GameSettings.CGContrast, default_luma_global_game_settings.CGContrast, "CGContrast", runtime);
            
            if (ImGui::SliderFloat("CG: Contrast Mid Gray", &cb_luma_global_settings.GameSettings.CGContrastMidGray, 0.f, 500.f))
               reshade::set_config_value(runtime, NAME, "CGContrastMidGray", cb_luma_global_settings.GameSettings.CGContrastMidGray);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Contrast's mid gray value to stretch in/out luminance.");
            DrawResetButton(cb_luma_global_settings.GameSettings.CGContrastMidGray, default_luma_global_game_settings.CGContrastMidGray, "CGContrastMidGray", runtime);
            
            if (ImGui::SliderFloat("CG: Highlights", &cb_luma_global_settings.GameSettings.CGHighlightsStrength, 0.f, 2.f))
               reshade::set_config_value(runtime, NAME, "CGHighlightsStrength", cb_luma_global_settings.GameSettings.CGHighlightsStrength);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("RenoDX highlights boost/compress.");
            DrawResetButton(cb_luma_global_settings.GameSettings.CGHighlightsStrength, default_luma_global_game_settings.CGHighlightsStrength, "CGHighlightsStrength", runtime);
            
            if (ImGui::SliderFloat("CG: Highlights Mid Gray", &cb_luma_global_settings.GameSettings.CGHighlightsMidGray, 0.f, 500.f))
               reshade::set_config_value(runtime, NAME, "CGHighlightsMidGray", cb_luma_global_settings.GameSettings.CGHighlightsMidGray);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Highlights mid gray / threshold value to manipulate luminance around.");
            DrawResetButton(cb_luma_global_settings.GameSettings.CGHighlightsMidGray, default_luma_global_game_settings.CGHighlightsMidGray, "CGHighlightsMidGray", runtime);
      
            if (ImGui::SliderFloat("CG: Shadows", &cb_luma_global_settings.GameSettings.CGShadowsStrength, 0.f, 2.f))
               reshade::set_config_value(runtime, NAME, "CGShadowsStrength", cb_luma_global_settings.GameSettings.CGShadowsStrength);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("RenoDX shadows boost/compress.");
            DrawResetButton(cb_luma_global_settings.GameSettings.CGShadowsStrength, default_luma_global_game_settings.CGShadowsStrength, "CGShadowsStrength", runtime);
            
            if (ImGui::SliderFloat("CG: Shadows Mid Gray", &cb_luma_global_settings.GameSettings.CGShadowsMidGray, 0.f, 500.f))
               reshade::set_config_value(runtime, NAME, "CGShadowsMidGray", cb_luma_global_settings.GameSettings.CGShadowsMidGray);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Shadows mid gray / threshold value to manipulate luminance around.");
            DrawResetButton(cb_luma_global_settings.GameSettings.CGShadowsMidGray, default_luma_global_game_settings.CGShadowsMidGray, "CGShadowsMidGray", runtime);
      
            if (is_disabled) ImGui::EndDisabled();
         }
      
      // ImGui::NewLine(); ////////////////////////////////////////////////////////////////////////////////////

      if (ShaderDefineInfo::Get(ShaderDefineInfo::CUSTOM_SDR) == 0)
         if (ImGui::CollapsingHeader("FMVs"))
         {
            ShaderDefineInfo::UIToggleCheckmark(ShaderDefineInfo::CUSTOM_UPSCALE_MOV, "PumboAutoHDR for FMVs",
               "Do inverse tonemap for FMVs.");
         
            if (ImGui::SliderFloat("Brightness Ratio", &cb_luma_global_settings.GameSettings.MovPeakRatio, 0.f, 2.f, "%.4f"))
               reshade::set_config_value(runtime, NAME, "MovPeakRatio", cb_luma_global_settings.GameSettings.MovPeakRatio);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Multiplier for peak output.\n(Useful for AAE users at main menu.)");
            DrawResetButton(cb_luma_global_settings.GameSettings.MovPeakRatio, default_luma_global_game_settings.MovPeakRatio, "MovPeakRatio", runtime);
   
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
      
      // ImGui::NewLine(); ////////////////////////////////////////////////////////////////////////////////////
      
      if (ImGui::CollapsingHeader("UI Toggles (for screenshots)"))
      {
         if (ImGui::Checkbox("Fullscreen Blur", &Globals::IsFullscreenBlur))
            reshade::set_config_value(runtime, NAME, "IsFullscreenBlur", Globals::IsFullscreenBlur);
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Toggle fullscreen blurring.\nSeen in pause menu and crawler fart gas.");
         DrawResetButton(Globals::IsFullscreenBlur, true, "IsFullscreenBlur", runtime);
      
         if (ImGui::Checkbox("Draw UI", &Globals::IsUi))
            reshade::set_config_value(runtime, NAME, "IsUi", Globals::IsUi);
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Toggle UI drawing.\nWill discard all shaders after final composition shader.");
         DrawResetButton(Globals::IsUi, true, "IsUi", runtime);
      }

      // ImGui::NewLine(); ////////////////////////////////////////////////////////////////////////////////////

      if (ShaderDefineInfo::Get(ShaderDefineInfo::CUSTOM_SDR) == 0)
         if (ImGui::CollapsingHeader("LUT Builder - Force Neutral LUT (reduces embedded blowout and contrast)"))
         {
            int def = ShaderDefineInfo::UIDropDown(ShaderDefineInfo::CUSTOM_LUTBUILDER_NEUTRAL, "Neutral LUT Builder",
               {"Off", "If Blown Out", "Forced"},
               "How should we blend back to neutral color from a custom LUT?\n\nVanilla maps are neutral, but many modded maps loves to load a blown out or overly saturated LUT texture.");
         
            is_disabled = def == 0;
            if (is_disabled) ImGui::BeginDisabled();
            {
               if (ImGui::SliderFloat("Neutral Hue", &cb_luma_global_settings.GameSettings.LUTBuilderNeutralHue, 0.01f, 1.f))
                  reshade::set_config_value(runtime, NAME, "LUTBuilderNeutralHue", cb_luma_global_settings.GameSettings.LUTBuilderNeutralHue);
               if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("The strength of blending back hue to neutral color from a custom LUT.\n\nRaising it will remove embedded color grading tint.");
               DrawResetButton(cb_luma_global_settings.GameSettings.LUTBuilderNeutralHue, default_luma_global_game_settings.LUTBuilderNeutralHue, "LUTBuilderNeutralHue", runtime);
         
               if (ImGui::SliderFloat("Neutral Chrominance", &cb_luma_global_settings.GameSettings.LUTBuilderNeutralChrominance, 0.f, 1.f))
                  reshade::set_config_value(runtime, NAME, "LUTBuilderNeutralChrominance", cb_luma_global_settings.GameSettings.LUTBuilderNeutralChrominance);
               if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("The strength of blending back chrominance to neutral color from a custom LUT.\n\nRaising it will reduce embedded saturation change.");
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
         }
      
      
      // ImGui::NewLine(); ////////////////////////////////////////////////////////////////////////////////////
      
      if (ImGui::CollapsingHeader("LUT Builder - Vanilla Color Grading"))
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
            reshade::set_config_value(runtime, NAME, "LUTBuilderGradeSMH", cb_luma_global_settings.GameSettings.LUTBuilderGradeSat);
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Strength of vanilla de/saturation.\nSeldom, idk.");
         DrawResetButton(cb_luma_global_settings.GameSettings.LUTBuilderGradeSat, default_luma_global_game_settings.LUTBuilderGradeSat, "LUTBuilderGradeSat", runtime);
      }
      
      // ImGui::NewLine(); ////////////////////////////////////////////////////////////////////////////////////
      
      if (ShaderDefineInfo::Get(ShaderDefineInfo::CUSTOM_SDR) == 0)
         if (ImGui::CollapsingHeader("LUT Builder - Gamut Expansion"))
         {
            bool is_on = ShaderDefineInfo::UIToggleCheckmark(ShaderDefineInfo::CUSTOM_LUTBUILDER_SATBOOST, "LUT Builder FakeBT2020 Gamut Expansion",
               "A gamma utilizing gamut expansion.");
            
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
         
               if (ImGui::SliderFloat("Expansion Highlight Saturation", &cb_luma_global_settings.GameSettings.LUTBuilderHighlightSat, 0.f, 0.5f))
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
      
      // ImGui::NewLine(); ////////////////////////////////////////////////////////////////////////////////////

      if (ShaderDefineInfo::Get(ShaderDefineInfo::CUSTOM_SDR) == 0)
         if (ImGui::CollapsingHeader("SDR Tonemapper - Per-Channel Correction"))
         {
            bool is_on = ShaderDefineInfo::UIToggleCheckmark(ShaderDefineInfo::CUSTOM_PCC, "Per-Channel Correction",
               "A correction before tonemapping to reduce blown out highlights' blowout by sampling the tone mapper at a lower exposure and blending it back.\nCan be used with or without RenoDX's color grading.");
         
            is_disabled = !is_on;
            if (is_disabled) ImGui::BeginDisabled();
            {
               if (ImGui::SliderFloat("PCC Look-back Exposure", &cb_luma_global_settings.GameSettings.PCCLookback, 0.01f, 0.5f, "%.4f"))
                  reshade::set_config_value(runtime, NAME, "PCCStrength", cb_luma_global_settings.GameSettings.PCCLookback);
               if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Sample the SDR tone mapper at a lower exposure to get a less blown out color.\nLower to obtain a less blown out color.");
               DrawResetButton(cb_luma_global_settings.GameSettings.PCCLookback, default_luma_global_game_settings.PCCLookback, "PCCStrength", runtime);
         
               if (ImGui::SliderFloat("PCC Hue Influence", &cb_luma_global_settings.GameSettings.PCCHue, 0.f, 1.f, "%.4f"))
                  reshade::set_config_value(runtime, NAME, "PCCHue", cb_luma_global_settings.GameSettings.PCCHue);
               if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Effect on of less blown out color on hue.\nSetting it too high will make stuff like fire red/orange.");
               DrawResetButton(cb_luma_global_settings.GameSettings.PCCHue, default_luma_global_game_settings.PCCHue, "PCCHue", runtime);
         
               if (ImGui::SliderFloat("PCC Chrominance Influence", &cb_luma_global_settings.GameSettings.PCCChrominance, 0.f, 1.f, "%.4f"))
                  reshade::set_config_value(runtime, NAME, "PCCChrominance", cb_luma_global_settings.GameSettings.PCCChrominance);
               if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Effect on of less blown out color on chrominance/saturation.\nSetting it too may color things that's supposed to be blown out (though it should be fine in this game).");
               DrawResetButton(cb_luma_global_settings.GameSettings.PCCChrominance, default_luma_global_game_settings.PCCChrominance, "PCCChrominance", runtime);
         
               if (ImGui::SliderFloat("PCC Chrominance Boost", &cb_luma_global_settings.GameSettings.PCCChrominanceBoost, 1.f, 1.5f, "%.4f"))
                  reshade::set_config_value(runtime, NAME, "PCCChrominanceBoost", cb_luma_global_settings.GameSettings.PCCChrominanceBoost);
               if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Multiplier on chrominance of less blown out color to further boost correction.");
               DrawResetButton(cb_luma_global_settings.GameSettings.PCCChrominanceBoost, default_luma_global_game_settings.PCCChrominanceBoost, "PCCChrominanceBoost", runtime);

               // if (ImGui::SliderFloat("PCC Guaranteed", &cb_luma_global_settings.GameSettings.PCCGuaranteed, 0.f, 1.f, "%.4f"))
               //    reshade::set_config_value(runtime, NAME, "PCCGuaranteed", cb_luma_global_settings.GameSettings.PCCGuaranteed);
               // if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Influence of a secondary fixed low exposure sample for guaranteed chrominance.");
               // DrawResetButton(cb_luma_global_settings.GameSettings.PCCGuaranteed, default_luma_global_game_settings.PCCGuaranteed, "PCCGuaranteed", runtime);
            }
            if (is_disabled) ImGui::EndDisabled();
         }
      
      // ImGui::NewLine(); ////////////////////////////////////////////////////////////////////////////////////

      if (ImGui::CollapsingHeader("Super Resolution (Advanced)"))
      {
         is_disabled = device_data.sr_type == SR::Type::None;
         if (is_disabled) ImGui::BeginDisabled();
         {
            if (ImGui::Checkbox("SR Auto Exposure", &Globals::SRAutoExposure))
               reshade::set_config_value(runtime, NAME, "SRAutoExposure", Globals::SRAutoExposure);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
               ImGui::SetTooltip("SR will pick the best exposure value for itself internally resulting in more smoothed highlights that may be preferred.");
            DrawResetButton(Globals::SRAutoExposure, true, "SRAutoExposure", runtime);

            if (ImGui::SliderFloat("SR Sharpness", &Globals::SRSharpness, 0.f, 1.f, "%.3f"))
               reshade::set_config_value(runtime, NAME, "SRSharpness", Globals::SRSharpness);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
               ImGui::SetTooltip("Internal Super Resolution sharpness.\nOnly on FSR, idk bugged or what...");
            DrawResetButton(Globals::SRSharpness, 1.f, "SRSharpness", runtime);
      
            if (ImGui::Checkbox("SR Is Swapchain Output Size", &Globals::SRIsSwapchainOutputSize))
               reshade::set_config_value(runtime, NAME, "SRIsSwapchainOutputSize", Globals::SRIsSwapchainOutputSize);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
               ImGui::SetTooltip("Should the SR output tex be swapchain size instead of downscaled like internal resolution?");
            DrawResetButton(Globals::SRIsSwapchainOutputSize, true, "SRIsSwapchainOutputSize", runtime);

            if (ImGui::SliderFloat("SR Exposure", &Globals::SRExposure, 0.f, 2.f, "%.4f"))
               reshade::set_config_value(runtime, NAME, "SRExposure", Globals::SRExposure);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
               ImGui::SetTooltip("The exposure value for edge weights calculations.\nSet 0 for auto.");
            DrawResetButton(Globals::SRExposure, 0.f, "SRExposure", runtime);

            ImGui::BeginDisabled();
            {
               if (ImGui::Checkbox("SR HDR", &Globals::SRIsHDR))
                  reshade::set_config_value(runtime, NAME, "SRIsHDR", Globals::SRIsHDR);
               if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                  ImGui::SetTooltip("Allow HDR values?");
               DrawResetButton(Globals::SRIsHDR, true, "SRIsHDR", runtime);
            }
            ImGui::EndDisabled();

            // if (ImGui::SliderFloat("SR Vertical Camera FOV Radians", &Globals::SRVertCameraFOV, 0.f, 3.14f, "%.3f"))
            //    reshade::set_config_value(runtime, NAME, "SRVertCameraFOV", Globals::SRVertCameraFOV);
            // if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            //    ImGui::SetTooltip("Vertical camera FOV for SR to use for calculations");
            // DrawResetButton(Globals::SRVertCameraFOV, 0.f, "SRVertCameraFOV", runtime);

            
            // ImGui::SliderFloat("SR Jitter Multiplier X", &Globals::SRJitterMultiplier.x, 0.f, 3.f, "%.4f");
            // ImGui::SliderFloat("SR Jitter Multiplier Y", &Globals::SRJitterMultiplier.y, 0.f, 3.f, "%.4f");
            
            // if (ImGui::Checkbox("SR Motion Vectors Jittered", &Globals::SRMvsJittered))
            //    reshade::set_config_value(runtime, NAME, "SRMvsJittered", Globals::SRMvsJittered);
            // if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            //    ImGui::SetTooltip("Are motion vector jittered?");
            // DrawResetButton(Globals::SRMvsJittered, false, "SRMvsJittered", runtime);
            //
            // if (ImGui::SliderFloat("SR Motion Vectors Scale", &Globals::SRMvsScale, -32.f, 32.f, "%.16f"))
            //    reshade::set_config_value(runtime, NAME, "SRMvsScale", Globals::SRMvsScale);
            // if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            //    ImGui::SetTooltip("Motion vector total scale.");
            // DrawResetButton(Globals::SRMvsScale, -20.f, "SRMvsScale", runtime);
            //
            // if (ImGui::Checkbox("SR Depth Inverse", &Globals::SRIsDepthInverse))
            //    reshade::set_config_value(runtime, NAME, "SRIsDepthInverse", Globals::SRIsDepthInverse);
            // if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            //    ImGui::SetTooltip("Is the depth inverted?");
            // DrawResetButton(Globals::SRIsDepthInverse, true, "SRIsDepthInverse", runtime);
            //
            // if (ImGui::SliderFloat("SR Depth Near Plane", &Globals::SRNearPlane, 0.f, 1.f, "%.4f"))
            //    reshade::set_config_value(runtime, NAME, "SRNearPlane", Globals::SRNearPlane);
            // if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            //    ImGui::SetTooltip("Min depth value.");
            // DrawResetButton(Globals::SRNearPlane, 0.f, "SRNearPlane", runtime);
            //
            // if (ImGui::SliderFloat("SR Depth Far Plane", &Globals::SRFarPlane, 0.f, 4.f, "%.4f"))
            //    reshade::set_config_value(runtime, NAME, "SRFarPlane", Globals::SRFarPlane);
            // if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            //    ImGui::SetTooltip("Max depth value.");
            // DrawResetButton(Globals::SRFarPlane, 1.f, "SRFarPlane", runtime);
         }
         if (is_disabled) ImGui::EndDisabled();
      }
      
      ImGui::Separator(); ////////////////////////////////////////////////////////////////////////////////////
      
      if (ImGui::Checkbox("Show Advanced Settings", &Globals::UIIsAdvanced))
         reshade::set_config_value(runtime, NAME, "UIIsAdvanced", Globals::UIIsAdvanced);
      
      // ImGui::SliderFloat("Custom Jitter Cheese", &game_device_data.CustomJitterCheese, -10.f, 10.f);
      // if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Override jitter offset (0 = disabled, uses game value).");

 #if DEVELOPMENT
      ImGui::Separator();

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

         float2 jitter_prev = false /*DLSSJitter::IsReady()*/ ? DLSSJitter::jitter : game_device_data.jitter_prev;
         const std::string s4 = "SR Jitter: " + std::to_string(jitter_prev.x) + ", " + std::to_string(jitter_prev.y);
         ImGui::BulletText(s4.c_str());
         
         //toggle IsSkipManualReloadShaders
         if (ImGui::Checkbox("Skip Manual Initial Reload Shaders", &Globals::IsSkipManualReloadShaders))
            reshade::set_config_value(runtime, NAME, "IsSkipManualReloadShaders", Globals::IsSkipManualReloadShaders);
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Skip the manual reload shaders requirement on start.");

         //toggle IsSkipOnDrawOrDispatch
         if (ImGui::Checkbox("Skip OnDrawOrDispatch()", &Globals::IsSkipOnDrawOrDispatch))
            reshade::set_config_value(runtime, NAME, "IsSkipOnDrawOrDispatch", Globals::IsSkipOnDrawOrDispatch);
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Debug to skip all custom scanning to grab info for DLSS, UI toggle, and other misc stuff.");

         //toggle IsSkipOnDrawOrDispatch
         if (ImGui::Checkbox("Skip DLSS Draw", &Globals::IsSkipDLSSDraw))
            reshade::set_config_value(runtime, NAME, "", Globals::IsSkipDLSSDraw);
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("When we ever reach the DLSS draw call. Should it be skipped, even though all info is gathered?");

         //IsSuperDebug
         ImGui::Checkbox("Super Debug", &Globals::IsSuperDebug);
      }
#endif

#if DEVELOPMENT
      ImGui::Separator();
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
      ImGui::BulletText("Jitter CPU Reversal: z1rp");
      ImGui::BulletText("Bug Hunter & Researcher: NikkMann");
      ImGui::BulletText("Bug Hunter: sinical");
      ImGui::BulletText("Bug Hunter: marsan031");
      ImGui::BulletText("Bug Hunter: TJS");
      ImGui::BulletText("Bug Hunter: Kobefreak42");

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
   }
};

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
   if (ul_reason_for_call == DLL_PROCESS_ATTACH)
   {
      Globals::SetGlobals(PROJECT_NAME, "Call of Duty: Black Ops III - Luma");
      Globals::VERSION = 1;

      //SetupShaderHashesLists
      ShaderHashesLists_Setup();

      //swapchain upgrade
      swapchain_format_upgrade_type  = TextureFormatUpgradesType::AllowedEnabled;
      swapchain_upgrade_type         = SwapchainUpgradeType::scRGB;
      // prevent_fullscreen_state       = false;

      //texture upgrade
      texture_format_upgrades_type   = TextureFormatUpgradesType::AllowedEnabled;
      //enable_indirect_texture_format_upgrades = true;
      //enable_automatic_indirect_texture_format_upgrades = true;
      texture_upgrade_formats = {
         reshade::api::format::r11g11b10_float,
      };
      texture_format_upgrades_2d_size_filters = (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainAspectRatio;

      //texture upgrade: LUT (r11g11b10_float too)
      texture_format_upgrades_lut_dimensions = LUTDimensions::_3D;
      texture_format_upgrades_lut_size = 32;

      //sampler upgrade
      enable_samplers_upgrade = true;

// #if DEVELOPMENT // If you want to track any shader names over time, you can hardcode them here by hash (they can be a useful reference in the pipeline)
//       forced_shader_names.emplace(std::stoul("FD2925B4", nullptr, 16), "Tracked Shader Name");
// #endif

#if !DEVELOPMENT // Put shaders that a previous version of the mod used but has ever since been deleted here
      old_shader_file_names.emplace("!tonemapper_mainmenu_0x1744B1D4.ps_5_0.hlsl");
      old_shader_file_names.emplace("!tonemapper_game_0x59F328E3.ps_5_0.hlsl");
      old_shader_file_names.emplace("!final_mainmenu_0x224A8BF5.ps_5_0.hlsl");
      old_shader_file_names.emplace("!final_game_0x3D461B1A.ps_5_0.hlsl");
      old_shader_file_names.emplace("bloom_0x57CF6767.ps_5_0.hlsl");
#endif
      
      game = new CallOfDutyBlackOps3();
   }

   CoreMain(hModule, ul_reason_for_call, lpReserved);

   return TRUE;
}
