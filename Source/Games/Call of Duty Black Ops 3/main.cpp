
#define GAME_CALLOFDUTYBLACKOPS3 1

#define GEOMETRY_SHADER_SUPPORT 0
#define ALLOW_SHADERS_DUMPING 0
#define ENABLE_NGX 1
#define ENABLE_FIDELITY_SK 0

#include "..\..\Core\core.hpp"

namespace ShaderHashesLists
{
   static ShaderHashesList ProbeCulling;
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
   // static float SRJitterMultiplier = 1.f;
   // static float SRMvsScale = -20.0f;
   // static float SRVertCameraFOV = 0.f;
   static bool SRMvsJittered = false;
   static uint SRSuccessCount = 0;
   static bool SRIsSwapchainOutputSize = false;
   //static bool SR1 = false;
   //static bool SR2 = false;
   //static bool SR3 = false;
   static uint ChangeCountSwapchain = 0;
   static uint ChangeCountSRTex = 0;
}

struct CallOfDutyBlackOps3GameDeviceData final : public GameDeviceData
{
   float2 jitter = float2(0);
   float2 jitter_prev = float2(0);

   com_ptr<ID3D11ShaderResourceView> sr_output_color_resource_view = nullptr;
   //com_ptr<D3D11_TEXTURE2D_DESC> sr_output_color_desc;
   
   //draw/pipeline progress
   bool drawn_probecull = false;
   bool drawn_tonemap = false;
   bool drawn_tonemap_prev = false;
   bool drawn_smaat2xprep = false; //not filmic
   bool drawn_smaat2x = false; //not filmic
   bool drawn_smaat2x_prev = false; //prev frame
   bool drawn_final = false;
   bool drawn_hdtv = false;
   bool drawn_hdtv_prev = false; //prev frame
   
   void Reset(bool isSRReset)
   {
      drawn_probecull = false;
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

      if (isSRReset) Globals::SRSuccessCount = 0;
   }
   
   static bool IsValidJitter(float2 jitter) {return jitter != float2(0);}
   static bool IsValidJitter(float4 jitter) {return IsValidJitter(float2(jitter.x, jitter.y));}

   static void HardResetSR(DeviceData& device_data, CallOfDutyBlackOps3GameDeviceData& game_device_data)
   {
      device_data.sr_output_color = nullptr;
      game_device_data.sr_output_color_resource_view = nullptr;
   }
};

namespace ShaderDefineInfo
{
   constexpr uint32_t  CUSTOM_TONEMAP                   = char_ptr_crc32("CUSTOM_TONEMAP");
   constexpr uint32_t  CUSTOM_HDTVREC709                = char_ptr_crc32("CUSTOM_HDTVREC709");
   constexpr uint32_t  CUSTOM_RCAS                      = char_ptr_crc32("CUSTOM_RCAS");
   constexpr uint32_t  CUSTOM_LUTBUILDER_HIGHLIGHTSAT   = char_ptr_crc32("CUSTOM_LUTBUILDER_HIGHLIGHTSAT");
   constexpr uint32_t  CUSTOM_LUTBUILDER_DECODE         = char_ptr_crc32("CUSTOM_LUTBUILDER_DECODE");
   constexpr uint32_t  CUSTOM_LUTBUILDER_NEUTRAL        = char_ptr_crc32("CUSTOM_LUTBUILDER_NEUTRAL");
   constexpr uint32_t  CUSTOM_PCC                       = char_ptr_crc32("CUSTOM_PCC");
   constexpr uint32_t  CUSTOM_COLORGRADE                = char_ptr_crc32("CUSTOM_COLORGRADE");
   constexpr uint32_t  CUSTOM_UPSCALE_MOV               = char_ptr_crc32("CUSTOM_UPSCALE_MOV");
   constexpr uint32_t  CUSTOM_SR                        = char_ptr_crc32("CUSTOM_SR");
   constexpr uint32_t  CUSTOM_SDR                       = char_ptr_crc32("CUSTOM_SDR");

   static char InvertCharBool(char b)
   {
      return b == '0' ? '1' : '0'; 
   }
   
   //This feels dumb O(n) everytime, but it is the most consistent, and it's only on ImGUI openned.
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
      //Def
      std::vector<ShaderDefineData> game_shader_defines_data = {
         {"GAMMA_CORRECTION_RANGE_TYPE", '0', true, true, "0 - Full range.\n1 - 0-1 only.", 1},
         {"SWAPCHAIN_CLAMP_PEAK", '2', true, false, "Final color clamp before present.\n0 - Unclamped (up to display).\n1 - Per channel clamp (blows out).\n2 - Scale down by max channel (sat preserving).", 2},
         {"SWAPCHAIN_CLAMP_COLORSPACE", '0', true, false, "Clamp colorspace against invalid colors.\n(Really only for OCD, as it should only be inconsequential black from RCAS.)\n0 - Unclamped.\n1 - BT2020.", 1},
         {"_____CUSTOM_____", '0', true, false, "Just a divider.", 1},
         {"CUSTOM_HDTVREC709", '0', true, false, "Decode color and swapchain to HDTV (rec.709) setting.", 1},
         {"CUSTOM_TONEMAP", '1', true, false, "HDR tonemapper, primarily the shoulder.\n0 - Off (Unclamped).\n1 - Reinhard Piecewise (Gradual)\n2 - Frostbite Exponential Rolloff (Aggressive)", 2},
         {"CUSTOM_TONEMAP_PERCHANNEL", '0', true, false, "HDR tonemap scaling.\n(Only for the curious because as it's destructive, stacking on top of SDR Influence.)\n0 - Luminance\n1 - Per-Channel", 1},
         {"CUSTOM_TONEMAP_CLAMP", '2', true, false, "Clamp overshoot from luma scaled HDR tonemap.\n0 - Unclamped (up to display).\n1 - Per channel clamp (blows out).\n2 - Scale down by max channel (sat preserving).", 2},
         {"CUSTOM_RCAS", '1', true, false, "Enable Robust (4 instead of 8 samples) Contrast Adaptive Sharpening.", 1},
         {"RCAS_DENOISE", '0', true, false, "Denoising sharpening.\nMore smooth, but helps against dither pattern of shadows and AO.", 1},
         {"RCAS_LUMINANCE_BASED", '0', true, false, "Luma based sharpening. Apparently meh.", 1},
         {"CUSTOM_LUTBUILDER_HIGHLIGHTSAT", '1', true, false, "Do highlight saturation on the LUT output.", 1},
         {"CUSTOM_LUTBUILDER_DECODE", '1', true, false, "Decode LUT after converting to BT2020 then hue correct from original.\nAdds slightly more saturation and contrast.", 1},
         {"CUSTOM_LUTBUILDER_NEUTRAL", '1', true, false, "If LUT texture color is desaturated, blend to neutral color", 1},
         {"CUSTOM_PCC", '1', true, false, "Do per channel correction on SDR tonemapped colors to reduce blowout.", 1},
         {"CUSTOM_COLORGRADE", '0', true, false, "Enable Color Grading right before HDR tonemapping.", 1},
         {"CUSTOM_MB_QUALITY", '0', true, false, "Motion blur sample count (pairs of forward and reverse).\n0 - 6 (Original)\n1 - 16\n2 - 24 (High)\n3 - 32\n4 - 48\n5 - 64\n6 - 128 (Uhhh)", 6},
         {"CUSTOM_UPSCALE_MOV", '1', true, false, "Auto HDR for movies.", 1},
         {"CUSTOM_SR", '1', true, true, "(AUTOMATICALLY HANDLED) Recompiles SMAA T2X (not filmic) according to SR type.", 1},
         {"CUSTOM_SDR", '1', true, true, "(AUTOMATICALLY HANDLED) Turn off FSFX HDR tradeoff encoding stuff.", 1},
      };
      shader_defines_data.append_range(game_shader_defines_data);
      assert(shader_defines_data.size() < MAX_SHADER_DEFINES);
      
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
      default_luma_global_game_settings.Bloom = cb_luma_global_settings.GameSettings.Bloom = 1.f;
      default_luma_global_game_settings.SlideLensDirt = cb_luma_global_settings.GameSettings.SlideLensDirt = 1.f;
      default_luma_global_game_settings.ADSSights = cb_luma_global_settings.GameSettings.ADSSights = 1.0f;
      default_luma_global_game_settings.XrayOutline = cb_luma_global_settings.GameSettings.XrayOutline = 1.f;
      default_luma_global_game_settings.MotionBlur = cb_luma_global_settings.GameSettings.MotionBlur = 1.f;
      default_luma_global_game_settings.VolumetricFog = cb_luma_global_settings.GameSettings.VolumetricFog = 1.f;
      default_luma_global_game_settings.RCAS = cb_luma_global_settings.GameSettings.RCAS = 0.5f;
      default_luma_global_game_settings.SDRTonemapFloorRaiseScale = cb_luma_global_settings.GameSettings.SDRTonemapFloorRaiseScale = 1.0f;
      default_luma_global_game_settings.LUT = cb_luma_global_settings.GameSettings.LUT = 1.f;
      default_luma_global_game_settings.LUTBuilderExpansionChrominanceCorrect = cb_luma_global_settings.GameSettings.LUTBuilderExpansionChrominanceCorrect = 0.85f;
      default_luma_global_game_settings.LUTBuilderExpansionLuminanceCorrect = cb_luma_global_settings.GameSettings.LUTBuilderExpansionLuminanceCorrect = 0.0f;
      default_luma_global_game_settings.LUTBuilderHighlightSat = cb_luma_global_settings.GameSettings.LUTBuilderHighlightSat = 0.255f;
      default_luma_global_game_settings.LUTBuilderHighlightSatHighlightsOnly = cb_luma_global_settings.GameSettings.LUTBuilderHighlightSatHighlightsOnly = 2.2f;
      default_luma_global_game_settings.LUTBuilderNeutral = cb_luma_global_settings.GameSettings.LUTBuilderNeutral = 0.4f;
      default_luma_global_game_settings.PCCStrength = cb_luma_global_settings.GameSettings.PCCStrength = 0.25f;
      default_luma_global_game_settings.PCCHighlightsOnly = cb_luma_global_settings.GameSettings.PCCHighlightsOnly = 1.3f;
      default_luma_global_game_settings.CGContrast = cb_luma_global_settings.GameSettings.CGContrast = 1.f;
      default_luma_global_game_settings.CGContrastMidGray = cb_luma_global_settings.GameSettings.CGContrastMidGray = 36.f;
      default_luma_global_game_settings.CGSaturation = cb_luma_global_settings.GameSettings.CGSaturation = 1.f;
      default_luma_global_game_settings.CGHighlightsStrength = cb_luma_global_settings.GameSettings.CGHighlightsStrength = 1.f;
      default_luma_global_game_settings.CGHighlightsMidGray = cb_luma_global_settings.GameSettings.CGHighlightsMidGray = 36.f;
      default_luma_global_game_settings.CGShadowsStrength = cb_luma_global_settings.GameSettings.CGShadowsStrength = 1.f;
      default_luma_global_game_settings.CGShadowsMidGray = cb_luma_global_settings.GameSettings.CGShadowsMidGray = 36.f;
      default_luma_global_game_settings.Exposure = cb_luma_global_settings.GameSettings.Exposure = 1.f;
      default_luma_global_game_settings.PreExposure = cb_luma_global_settings.GameSettings.PreExposure = 1.f;
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
      //device_data.game
      device_data.game = new CallOfDutyBlackOps3GameDeviceData;
   }

   void OnInitSwapchain(reshade::api::swapchain* swapchain) override
   {
      auto& device_data = *swapchain->get_device()->get_private_data<DeviceData>();

      Globals::ChangeCountSwapchain++;
   }

   DrawOrDispatchOverrideType OnDrawOrDispatch(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, reshade::api::shader_stage stages, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, bool is_custom_pass, bool& updated_cbuffers, std::function<void()>* original_draw_dispatch_func) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);

      //will false if fail
      bool isDrawSR = true;

      //case: dispatching Light/Reflection Probe Culling
      if (!game_device_data.drawn_probecull && original_shader_hashes.Contains(ShaderHashesLists::ProbeCulling))
      {
         //progress
         game_device_data.drawn_probecull = true;

 #if ENABLE_SR > 0
         //disabled: no SR
         if (device_data.sr_type == SR::Type::None || device_data.sr_suppressed) return DrawOrDispatchOverrideType::None;
         
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

         //extract by copying buffer
         ID3D11Buffer* staging_cb = cb_buffer;
         com_ptr<ID3D11Buffer> staging_cb_buf;
         //Usage D3D11_USAGE_DYNAMIC, BindFlags D3D11_BIND_CONSTANT_BUFFER, CPUAccessFlags D3D11_CPU_ACCESS_WRITE
         if (cb_desc.Usage != D3D11_USAGE_STAGING || !(cb_desc.CPUAccessFlags & D3D11_CPU_ACCESS_READ)) //TODO: not needed check
         {
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
                  auto cb_floats = reinterpret_cast<const PerSceneConsts*>(mapped_cb.pData);
                  
                  //@ index 71
                  // float2 jitter = float2(cb_floats[71].x, cb_floats[71].y);
                  float2 jitter = float2(cb_floats->subpixelOffset.x, cb_floats->subpixelOffset.y);
                  if (CallOfDutyBlackOps3GameDeviceData::IsValidJitter(jitter)) game_device_data.jitter = jitter;

                  //also get renderTargetSize
                  // float2 renderTargetSize = float2(cb_floats->renderTargetSize.x, cb_floats->renderTargetSize.y);
                  // game_device_data.renderTargetSize = renderTargetSize;

                  //release
                  native_device_context->Unmap(staging_cb, 0);
                  staging_cb->Release();
               }
            }
            else ASSERT_MSG(false, "FAILED jitter get, found but can't access.");
         }

         if (cb_buffer != nullptr) cb_buffer->Release();
#endif
         return DrawOrDispatchOverrideType::None;
      }

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

         //no jitter for SR: start continuing
         if (!CallOfDutyBlackOps3GameDeviceData::IsValidJitter(game_device_data.jitter))
         {
            float n = -game_device_data.jitter_prev.x;
            game_device_data.jitter = float2(n, n);
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
         com_ptr<ID3D11ShaderResourceView> ps_shader_resources[15];
         native_device_context->PSGetShaderResources(0, ARRAYSIZE(ps_shader_resources), &ps_shader_resources[0]);
         
         //failed: check textures working?
         const bool dlss_inputs_valid = 
            ps_shader_resources[0].get() /*&&
            ps_shader_resources[11].get() && 
            ps_shader_resources[14].get()*/
         ;
         ASSERT_ONCE(dlss_inputs_valid);
         if (!dlss_inputs_valid) return DrawOrDispatchOverrideType::None;

         //setup SR
         auto* sr_instance_data = device_data.GetSRInstanceData();
         ASSERT_ONCE(sr_instance_data);

         //get res
         com_ptr<ID3D11Resource> tex_color;
         ps_shader_resources[0]->GetResource(&tex_color);

         //get tex
         com_ptr<ID3D11Texture2D> tex_color_tex2d;
         D3D11_TEXTURE2D_DESC tex_color_desc;
         {
            auto hr = tex_color->QueryInterface(&tex_color_tex2d);
            ASSERT_ONCE(SUCCEEDED(hr));
            tex_color_tex2d->GetDesc(&tex_color_desc);
         }

         //skip by too small?
         bool skip_dlss = tex_color_desc.Width  < sr_instance_data->min_resolution ||
                          tex_color_desc.Height < sr_instance_data->min_resolution;

         //check desired output resolution
         uint output_resolution_x = Globals::SRIsSwapchainOutputSize ? (uint)device_data.output_resolution.x : tex_color_desc.Width;
         uint output_resolution_y = Globals::SRIsSwapchainOutputSize ? (uint)device_data.output_resolution.y : tex_color_desc.Height;
         bool is_internal_res_bigger_than_swapchain = output_resolution_x < tex_color_desc.Width && output_resolution_y < tex_color_desc.Height;
         if (is_internal_res_bigger_than_swapchain) //DLSS/DLAA doesnt support output > render, so max().
         {
            output_resolution_x = tex_color_desc.Width;
            output_resolution_y = tex_color_desc.Height;
         }
            
         //if exists, then check if prev is valid
         constexpr bool dlss_output_changed = false;
         D3D11_TEXTURE2D_DESC dlss_output_texture_desc;
         bool sr_output_color_get = device_data.sr_output_color.get() != nullptr;
         // if (sr_output_color_get)
         // {
         //    //get desc
         //    device_data.sr_output_color->GetDesc(&dlss_output_texture_desc);
         //
         //    //dlss_output_changed: res
         //    if (is_internal_res_bigger_than_swapchain)
         //      dlss_output_changed =
         //         dlss_output_texture_desc.Width != output_resolution_x ||
         //         dlss_output_texture_desc.Height != output_resolution_y;
         //    
         //    //dlss_output_changed: miss match format
         //    dlss_output_changed |= dlss_output_texture_desc.Format != tex_color_desc.Format;
         // }
         
         //null or dlss_output_changed
         if (dlss_output_changed || !sr_output_color_get)
         {
            //release prev
            CallOfDutyBlackOps3GameDeviceData::HardResetSR(device_data, game_device_data);
            
            //copy (like an idiot) and change for SR
            dlss_output_texture_desc.Width          = output_resolution_x;
            dlss_output_texture_desc.Height         = output_resolution_y;
            dlss_output_texture_desc.MipLevels      = tex_color_desc.MipLevels; 
            dlss_output_texture_desc.ArraySize      = tex_color_desc.ArraySize;
            dlss_output_texture_desc.Format         = tex_color_desc.Format;
            dlss_output_texture_desc.SampleDesc     = tex_color_desc.SampleDesc;
            dlss_output_texture_desc.Usage          = tex_color_desc.Usage;
            dlss_output_texture_desc.BindFlags      = tex_color_desc.BindFlags | D3D11_BIND_UNORDERED_ACCESS;
            dlss_output_texture_desc.CPUAccessFlags = tex_color_desc.CPUAccessFlags;
            dlss_output_texture_desc.MiscFlags      = tex_color_desc.MiscFlags;
            
            //create new output texture with correct res and uav support for SR.
            auto hr1 = native_device->CreateTexture2D(&dlss_output_texture_desc, nullptr, &device_data.sr_output_color);
            ASSERT_ONCE(SUCCEEDED(hr1));
            
            //create new ShaderResourceView for shader input binding.
            auto hr2 = native_device->CreateShaderResourceView(device_data.sr_output_color.get(), nullptr, &game_device_data.sr_output_color_resource_view);
            ASSERT_ONCE(SUCCEEDED(hr2));

            //stats
            Globals::ChangeCountSRTex++;
         }
         
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
         settings_data.render_width  = tex_color_desc.Width;
         settings_data.render_height = tex_color_desc.Height;
         settings_data.inverted_depth = true/*Globals::SRIsDepthInverse*/;
         Globals::SRIsHDR = cb_luma_global_settings.DisplayMode != DisplayModeType::SDR; settings_data.hdr = Globals::SRIsHDR;
         settings_data.mvs_x_scale = -20/*Globals::SRMvsScale*/;
         settings_data.mvs_y_scale = -20/*Globals::SRMvsScale*/;
         settings_data.mvs_jittered = Globals::SRMvsJittered;
         settings_data.auto_exposure = Globals::SRAutoExposure;
         sr_implementations[device_data.sr_type]->UpdateSettings(sr_instance_data, native_device_context, settings_data);

         //depth and velocity tex
         com_ptr<ID3D11Resource> tex_depth;
         com_ptr<ID3D11Resource> tex_velocity;
         ps_shader_resources[11]->GetResource(&tex_velocity);
         ps_shader_resources[14]->GetResource(&tex_depth);

         //DrawData
         SR::SuperResolutionImpl::DrawData draw_data;
         draw_data.render_width = settings_data.render_width;
         draw_data.render_height = settings_data.render_height;
         draw_data.near_plane = 0/*Globals::SRNearPlane*/;
         draw_data.far_plane = 1/*Globals::SRFarPlane*/;
         draw_data.source_color = tex_color.get();
         draw_data.output_color = device_data.sr_output_color.get();
         draw_data.motion_vectors = tex_velocity.get();
         draw_data.depth_buffer = tex_depth.get();
         draw_data.jitter_x = game_device_data.jitter.x /** Globals::SRJitterMultiplier*/;
         draw_data.jitter_y = game_device_data.jitter.y /** Globals::SRJitterMultiplier*/;
         draw_data.pre_exposure = Globals::SRExposure;
         draw_data.user_sharpness = Globals::SRSharpness;
         draw_data.vert_fov = 0/*Globals::SRVertCameraFOV*/;
         
         //force reset?
         bool reset_dlss = device_data.force_reset_sr || dlss_output_changed;
         draw_data.reset = reset_dlss;

         //reset the reset
         device_data.force_reset_sr = false;

         //skip: hasn't succeed back to back.
         Globals::SRSuccessCount++;
         if (Globals::SRSuccessCount < 60) return DrawOrDispatchOverrideType::None;

         //DRAW!
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

      //clear SR
      if (!game_device_data.drawn_smaat2x) device_data.force_reset_sr = true;
      
      //reset
      game_device_data.Reset(device_data.force_reset_sr);
   }

   void LoadConfigs() override
   {
      reshade::api::effect_runtime* runtime = nullptr;

      //Load ReShade settings
      reshade::get_config_value(runtime, NAME, "TonemapperRolloffStart", cb_luma_global_settings.GameSettings.TonemapperRolloffStart);
      reshade::get_config_value(runtime, NAME, "Bloom", cb_luma_global_settings.GameSettings.Bloom);
      reshade::get_config_value(runtime, NAME, "SlideLensDirt", cb_luma_global_settings.GameSettings.SlideLensDirt);
      reshade::get_config_value(runtime, NAME, "ADSSights", cb_luma_global_settings.GameSettings.ADSSights);
      reshade::get_config_value(runtime, NAME, "XrayOutline", cb_luma_global_settings.GameSettings.XrayOutline);
      reshade::get_config_value(runtime, NAME, "MotionBlur", cb_luma_global_settings.GameSettings.MotionBlur);
      reshade::get_config_value(runtime, NAME, "VolumetricFog", cb_luma_global_settings.GameSettings.VolumetricFog);
      reshade::get_config_value(runtime, NAME, "SDRTonemapFloorRaiseScale", cb_luma_global_settings.GameSettings.SDRTonemapFloorRaiseScale);
      reshade::get_config_value(runtime, NAME, "RCAS", cb_luma_global_settings.GameSettings.RCAS);
      reshade::get_config_value(runtime, NAME, "LUT", cb_luma_global_settings.GameSettings.LUT);
      reshade::get_config_value(runtime, NAME, "LUTBuilderExpansionChrominanceCorrect", cb_luma_global_settings.GameSettings.LUTBuilderExpansionChrominanceCorrect);
      reshade::get_config_value(runtime, NAME, "LUTBuilderExpansionLuminanceCorrect", cb_luma_global_settings.GameSettings.LUTBuilderExpansionLuminanceCorrect);
      reshade::get_config_value(runtime, NAME, "LUTBuilderHighlightSat", cb_luma_global_settings.GameSettings.LUTBuilderHighlightSat);
      reshade::get_config_value(runtime, NAME, "LUTBuilderHighlightSatHighlightsOnly", cb_luma_global_settings.GameSettings.LUTBuilderHighlightSatHighlightsOnly);
      reshade::get_config_value(runtime, NAME, "LUTBuilderNeutral", cb_luma_global_settings.GameSettings.LUTBuilderNeutral);
      reshade::get_config_value(runtime, NAME, "PCCStrength", cb_luma_global_settings.GameSettings.PCCStrength);
      reshade::get_config_value(runtime, NAME, "PCCHighlightsOnly", cb_luma_global_settings.GameSettings.PCCHighlightsOnly);
      reshade::get_config_value(runtime, NAME, "CGContrast", cb_luma_global_settings.GameSettings.CGContrast);
      reshade::get_config_value(runtime, NAME, "CGContrastMidGray", cb_luma_global_settings.GameSettings.CGContrastMidGray);
      reshade::get_config_value(runtime, NAME, "CGSaturation", cb_luma_global_settings.GameSettings.CGSaturation);
      reshade::get_config_value(runtime, NAME, "CGHighlightsStrength", cb_luma_global_settings.GameSettings.CGHighlightsStrength);
      reshade::get_config_value(runtime, NAME, "CGHighlightsMidGray", cb_luma_global_settings.GameSettings.CGHighlightsMidGray);
      reshade::get_config_value(runtime, NAME, "CGShadowsStrength", cb_luma_global_settings.GameSettings.CGShadowsStrength);
      reshade::get_config_value(runtime, NAME, "CGShadowsMidGray", cb_luma_global_settings.GameSettings.CGShadowsMidGray);
      reshade::get_config_value(runtime, NAME, "Exposure", cb_luma_global_settings.GameSettings.Exposure);
      reshade::get_config_value(runtime, NAME, "PreExposure", cb_luma_global_settings.GameSettings.PreExposure);
      reshade::get_config_value(runtime, NAME, "GammaInfluence", cb_luma_global_settings.GameSettings.GammaInfluence);
      reshade::get_config_value(runtime, NAME, "MovPeakRatio", cb_luma_global_settings.GameSettings.MovPeakRatio);
      reshade::get_config_value(runtime, NAME, "MovShoulderPow", cb_luma_global_settings.GameSettings.MovShoulderPow);
      
      reshade::get_config_value(runtime, NAME, "IsHud", Globals::IsUi);
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

      //Shoulder Start
      is_disabled = ShaderDefineInfo::Get(ShaderDefineInfo::CUSTOM_TONEMAP) == 0;
      if (is_disabled) ImGui::BeginDisabled();
      if (ImGui::SliderFloat("HDR Tonemapper Rolloff Start", &cb_luma_global_settings.GameSettings.TonemapperRolloffStart, 20.f, 500.f, "%.0f"))
         reshade::set_config_value(runtime, NAME, "TonemapperRolloffStart", cb_luma_global_settings.GameSettings.TonemapperRolloffStart);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("HDR tonemapper's rolloff/shoulder start in nits.\nObviously, don't set exceeding Peak nits!");
      if (cb_luma_global_settings.GameSettings.TonemapperRolloffStart > cb_luma_global_settings.ScenePeakWhite)
      {
         ImGui::SameLine();
         ImGui::SmallButton(ICON_FK_WARNING);
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("This is higher than peak!!!");
      }
      DrawResetButton(cb_luma_global_settings.GameSettings.TonemapperRolloffStart, default_luma_global_game_settings.TonemapperRolloffStart, "TonemapperRolloffStart", runtime);
      if (is_disabled) ImGui::EndDisabled();
      
      //Exposure
      is_disabled = cb_luma_global_settings.DisplayMode == DisplayModeType::SDR;
      if (is_disabled) { ImGui::BeginDisabled(); cb_luma_global_settings.GameSettings.Exposure = 1.f; }
      if (ImGui::SliderFloat("Exposure", &cb_luma_global_settings.GameSettings.Exposure, 0.f, 3.f))
         reshade::set_config_value(runtime, NAME, "Exposure", cb_luma_global_settings.GameSettings.Exposure);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Exposure before HDR tonemap.\nAlternative to Scene Paper White without shifting EOTF / Gamma Correction influence range..");
      DrawResetButton(cb_luma_global_settings.GameSettings.Exposure, default_luma_global_game_settings.Exposure, "Exposure", runtime);
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

      //Gamma Influence
      is_disabled = cb_luma_global_settings.DisplayMode == DisplayModeType::SDR || ShaderDefineInfo::Get(GAMMA_CORRECTION_TYPE_HASH) == 0;
      if (is_disabled) ImGui::BeginDisabled();
      if (ImGui::SliderFloat("Gamma Influence", &cb_luma_global_settings.GameSettings.GammaInfluence, 0.f, 3.f))
         reshade::set_config_value(runtime, NAME, "GammaInfluence", cb_luma_global_settings.GameSettings.GammaInfluence);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("(GAMMA_CORRECTION_TYPE) Replacement for the in-game Brightness slider for HDR.\n(Scales color to value, decodes sRGB or rec.709, then inverse the scale back.)");
      DrawResetButton(cb_luma_global_settings.GameSettings.GammaInfluence, default_luma_global_game_settings.GammaInfluence, "GammaInfluence", runtime);
      if (is_disabled) ImGui::EndDisabled();

      //CUSTOM_HDTVREC709
      {
         bool b = ShaderDefineInfo::Get(ShaderDefineInfo::CUSTOM_HDTVREC709) == 1;
         if (ImGui::Checkbox("HDTV rec.709", &b)) ShaderDefineInfo::ToggleBool(ShaderDefineInfo::CUSTOM_HDTVREC709);
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Replacement for in-game Display Mode setting.");
      }
      
      ImGui::NewLine(); ////////////////////////////////////////////////////////////////////////////////////

      ImGui::Text("NOTICES:");

      //SR on but not SMAA T2X
      if (device_data.sr_type != SR::Type::None && game_device_data.drawn_tonemap_prev && !game_device_data.drawn_smaat2x_prev)
      {
         ImGui::Bullet(); ImGui::SameLine();
         ImGui::SmallButton(ICON_FK_WARNING); ImGui::SameLine();
         ImGui::Text("Super Resolution is on, but SMAA T2x (Not Filmic) isn't selected in-game.");
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
         // ImGui::SmallButton(ICON_FK_WARNING); ImGui::SameLine();
         ImGui::Text("SDR mode on.\nIn-game Brightness slider settings will apply (where 0 = exactly sRGB).\nSwapchain is still HDR, so ReShade UI will be blown out if not darkened.");
      }

      //SR Reset
      if (device_data.sr_type != SR::Type::None)
      {
         ImGui::Bullet(); ImGui::SameLine();
         ImGui::Text("Super Resolution On:");ImGui::SameLine();
         if (ImGui::Button("Reset (Soft)")) device_data.force_reset_sr = true; ImGui::SameLine();
         if (ImGui::Button("Reset (Hard)")) { device_data.force_reset_sr = true; CallOfDutyBlackOps3GameDeviceData::HardResetSR(device_data, game_device_data); } ImGui::SameLine();
         
         auto* s = Globals::SRIsSwapchainOutputSize ? "Swapchain" : "Internal";
         if (ImGui::Button(("Output Tex Size (" + std::string(s) + ")").c_str()))
         {
            Globals::SRIsSwapchainOutputSize = !Globals::SRIsSwapchainOutputSize;
            reshade::set_config_value(runtime, NAME, "SRIsSwapchainOutputSize", Globals::SRIsSwapchainOutputSize);
            CallOfDutyBlackOps3GameDeviceData::HardResetSR(device_data, game_device_data);
         } 
      }
      
      ImGui::NewLine(); ////////////////////////////////////////////////////////////////////////////////////

      ImGui::Text("README:");
      ImGui::BulletText("See \"Shader Defines\" tab to recompile shaders with different features.");
      ImGui::BulletText("Super Resolution will only work for SMAA T2x (NOT FILMIC)!"
                        "\nCrash prone. Maybe safer turning off before loading map?"
                        "\nMotion blur causes fire flies."
                        "\nAfter changing internal resolution, try reselect the SR option to refresh."
                        "\nFSR is completely broken for now.");
      ImGui::BulletText("Don't worry! Some shaders are purposefully broken for some settings.");

      ImGui::NewLine(); ////////////////////////////////////////////////////////////////////////////////////
      
      ImGui::Text("Options:");

      if (ImGui::SliderFloat("Bloom", &cb_luma_global_settings.GameSettings.Bloom, 0.f, 2.f))
         reshade::set_config_value(runtime, NAME, "Bloom", cb_luma_global_settings.GameSettings.Bloom);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Bloom strength.");
      DrawResetButton(cb_luma_global_settings.GameSettings.Bloom, default_luma_global_game_settings.Bloom, "Bloom", runtime);

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

      if (ImGui::SliderFloat("Volumetric Fog", &cb_luma_global_settings.GameSettings.VolumetricFog, 0.f, 2.f))
         reshade::set_config_value(runtime, NAME, "VolumetricFog", cb_luma_global_settings.GameSettings.VolumetricFog);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Volumetric fog multiplier.");
      DrawResetButton(cb_luma_global_settings.GameSettings.VolumetricFog, default_luma_global_game_settings.VolumetricFog, "VolumetricFog", runtime);

      is_disabled = ShaderDefineInfo::Get(ShaderDefineInfo::CUSTOM_RCAS) == 0;
      if (is_disabled) ImGui::BeginDisabled();
      if (ImGui::SliderFloat("Sharpening", &cb_luma_global_settings.GameSettings.RCAS, 0.f, 1.f))
         reshade::set_config_value(runtime, NAME, "RCAS", cb_luma_global_settings.GameSettings.RCAS);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("(CUSTOM_RCAS) RCAS sharpening strength, done after AA.");
      DrawResetButton(cb_luma_global_settings.GameSettings.RCAS, default_luma_global_game_settings.RCAS, "RCAS", runtime);
      if (is_disabled) ImGui::EndDisabled();
      
      ImGui::NewLine(); ////////////////////////////////////////////////////////////////////////////////////

      is_disabled = cb_luma_global_settings.DisplayMode == DisplayModeType::SDR;
      if (is_disabled) ImGui::BeginDisabled();
      if (ImGui::SliderFloat("SDR Influence", &cb_luma_global_settings.GameSettings.LUT, 0.f, 1.f))
         reshade::set_config_value(runtime, NAME, "LUT", cb_luma_global_settings.GameSettings.LUT);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("SDR LUT, per-channel blowout, and color grading influence.");
      DrawResetButton(cb_luma_global_settings.GameSettings.LUT, default_luma_global_game_settings.LUT, "LUT", runtime);
      if (is_disabled) ImGui::EndDisabled();

      if (ImGui::SliderFloat("SDR Tonemap Black Floor Raise", &cb_luma_global_settings.GameSettings.SDRTonemapFloorRaiseScale, 0.8f, 1.f, "%.4f"))
         reshade::set_config_value(runtime, NAME, "SDRTonemapFloorRaiseScale", cb_luma_global_settings.GameSettings.SDRTonemapFloorRaiseScale);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Scaler/Multiplier on the black floor raise value of the tonemapper.\n1 is vanilla.");
      DrawResetButton(cb_luma_global_settings.GameSettings.SDRTonemapFloorRaiseScale, default_luma_global_game_settings.SDRTonemapFloorRaiseScale, "SDRTonemapFloorRaiseScale", runtime);

      if (ImGui::SliderFloat("PreExposure", &cb_luma_global_settings.GameSettings.PreExposure, 0.8f, 1.0f))
         reshade::set_config_value(runtime, NAME, "PreExposure", cb_luma_global_settings.GameSettings.PreExposure);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Exposure on linear color right after sampling it from texture, before SDR tonemapping and LUT.");
      DrawResetButton(cb_luma_global_settings.GameSettings.PreExposure, default_luma_global_game_settings.PreExposure, "PreExposure", runtime);

      
      is_disabled = ShaderDefineInfo::Get(ShaderDefineInfo::CUSTOM_LUTBUILDER_DECODE) == 0;
      if (is_disabled) ImGui::BeginDisabled();
      if (ImGui::SliderFloat("LUT Builder: Expansion Chrominance Correct", &cb_luma_global_settings.GameSettings.LUTBuilderExpansionChrominanceCorrect, 0.f, 1.f))
         reshade::set_config_value(runtime, NAME, "LUTBuilderExpansionChrominanceCorrect", cb_luma_global_settings.GameSettings.LUTBuilderExpansionChrominanceCorrect);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("(CUSTOM_LUTBUILDER_DECODE) After gamut expansion, how much should the original chroma be respected?\nDecrease for more saturation, but it may be unatural.");
      DrawResetButton(cb_luma_global_settings.GameSettings.LUTBuilderExpansionChrominanceCorrect, default_luma_global_game_settings.LUTBuilderExpansionChrominanceCorrect, "LUTBuilderExpansionChrominanceCorrect", runtime);

      if (ImGui::SliderFloat("LUT Builder: Expansion Luminance Correct", &cb_luma_global_settings.GameSettings.LUTBuilderExpansionLuminanceCorrect, 0.f, 1.f))
         reshade::set_config_value(runtime, NAME, "LUTBuilderExpansionLuminanceCorrect", cb_luma_global_settings.GameSettings.LUTBuilderExpansionLuminanceCorrect);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("(CUSTOM_LUTBUILDER_DECODE) After gamut expansion, how much should the original luma be respected?\nDecrease for a more contrasty feel, but maybe more metallic looking.");
      DrawResetButton(cb_luma_global_settings.GameSettings.LUTBuilderExpansionLuminanceCorrect, default_luma_global_game_settings.LUTBuilderExpansionLuminanceCorrect, "LUTBuilderExpansionLuminanceCorrect", runtime);
      if (is_disabled) ImGui::EndDisabled();
      
      
      is_disabled = ShaderDefineInfo::Get(ShaderDefineInfo::CUSTOM_LUTBUILDER_HIGHLIGHTSAT) == 0;
      if (is_disabled) ImGui::BeginDisabled();

      if (ImGui::SliderFloat("LUT Builder: Highlight Saturation", &cb_luma_global_settings.GameSettings.LUTBuilderHighlightSat, 0.f, 1.f))
         reshade::set_config_value(runtime, NAME, "LUTBuilderHighlightSat", cb_luma_global_settings.GameSettings.LUTBuilderHighlightSat);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("(CUSTOM_LUTBUILDER_HIGHLIGHTSAT) Boost saturation for LUT highlights.");
      DrawResetButton(cb_luma_global_settings.GameSettings.LUTBuilderHighlightSat, default_luma_global_game_settings.LUTBuilderHighlightSat, "LUTBuilderHighlightSat", runtime);

      if (ImGui::SliderFloat("LUT Builder: Highlight Saturation Highlights Only", &cb_luma_global_settings.GameSettings.LUTBuilderHighlightSatHighlightsOnly, 1.f, 5.f))
         reshade::set_config_value(runtime, NAME, "LUTBuilderHighlightSatHighlightsOnly", cb_luma_global_settings.GameSettings.LUTBuilderHighlightSatHighlightsOnly);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("(CUSTOM_LUTBUILDER_HIGHLIGHTSAT) Target highlights only.");
      DrawResetButton(cb_luma_global_settings.GameSettings.LUTBuilderHighlightSatHighlightsOnly, default_luma_global_game_settings.LUTBuilderHighlightSatHighlightsOnly, "LUTBuilderHighlightSatHighlightsOnly", runtime);
      
      if (is_disabled) ImGui::EndDisabled();

      
      is_disabled = ShaderDefineInfo::Get(ShaderDefineInfo::CUSTOM_LUTBUILDER_NEUTRAL) == 0;
      if (is_disabled) ImGui::BeginDisabled();
      if (ImGui::SliderFloat("LUT Builder: Blowout Removal", &cb_luma_global_settings.GameSettings.LUTBuilderNeutral, 0.f, 1.f))
         reshade::set_config_value(runtime, NAME, "LUTBuilderNeutral", cb_luma_global_settings.GameSettings.LUTBuilderNeutral);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("(CUSTOM_LUTBUILDER_NEUTRAL) If LUT texture color is desaturated (embedded blowout), blend to neutral color to corrected back.");
      DrawResetButton(cb_luma_global_settings.GameSettings.LUTBuilderNeutral, default_luma_global_game_settings.LUTBuilderNeutral, "LUTBuilderNeutral", runtime);
      if (is_disabled) ImGui::EndDisabled();

      
      is_disabled = ShaderDefineInfo::Get(ShaderDefineInfo::CUSTOM_PCC) == 0;
      if (is_disabled) ImGui::BeginDisabled();
      if (ImGui::SliderFloat("Per-Channel Correct: Strength", &cb_luma_global_settings.GameSettings.PCCStrength, 0.f, 1.f, "%.4f"))
         reshade::set_config_value(runtime, NAME, "PCCStrength", cb_luma_global_settings.GameSettings.PCCStrength);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("(CUSTOM_PCC) From RenoDX, restore SDR tonemapper's blowout for extreme bright things like fire.");
      DrawResetButton(cb_luma_global_settings.GameSettings.PCCStrength, default_luma_global_game_settings.PCCStrength, "PCCStrength", runtime);
      
      if (ImGui::SliderFloat("Per-Channel Correct: Highlights Only", &cb_luma_global_settings.GameSettings.PCCHighlightsOnly, 0.f, 2.f, "%.4f"))
         reshade::set_config_value(runtime, NAME, "PCCHighlightsOnly", cb_luma_global_settings.GameSettings.PCCHighlightsOnly);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("(CUSTOM_PCC) Increase to target only highlights.");
      DrawResetButton(cb_luma_global_settings.GameSettings.PCCHighlightsOnly, default_luma_global_game_settings.PCCHighlightsOnly, "PCCHighlightsOnly", runtime);
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

      if (ImGui::SliderFloat("Movie AutoHDR: Peak Ratio", &cb_luma_global_settings.GameSettings.MovPeakRatio, 0.f, 1.f, "%.4f"))
         reshade::set_config_value(runtime, NAME, "MovPeakRatio", cb_luma_global_settings.GameSettings.MovPeakRatio);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("(CUSTOM_UPSCALE_MOV) Multiplier for peak of PumboAutoHDR on movies.");
      DrawResetButton(cb_luma_global_settings.GameSettings.MovPeakRatio, default_luma_global_game_settings.MovPeakRatio, "MovPeakRatio", runtime);
      
      if (ImGui::SliderFloat("Movie AutoHDR: Shoulder Power", &cb_luma_global_settings.GameSettings.MovShoulderPow, 1.f, 5.f, "%.4f"))
         reshade::set_config_value(runtime, NAME, "MovShoulderPow", cb_luma_global_settings.GameSettings.MovShoulderPow);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("(CUSTOM_UPSCALE_MOV) Contrast of PumboAutoHDR on movies.");
      DrawResetButton(cb_luma_global_settings.GameSettings.MovShoulderPow, default_luma_global_game_settings.MovShoulderPow, "MovShoulderPow", runtime);
      
      if (is_disabled) ImGui::EndDisabled();
      
      ImGui::NewLine(); ////////////////////////////////////////////////////////////////////////////////////

      if (ImGui::Checkbox("Fullscreen Blur", &Globals::IsFullscreenBlur))
         reshade::set_config_value(runtime, NAME, "IsFullscreenBlur", Globals::IsFullscreenBlur);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Toggle fullscreen blurring.\nSeen in pause menu and crawler fart gas.");
      DrawResetButton(Globals::IsFullscreenBlur, true, "IsFullscreenBlur", runtime);
      
      if (ImGui::Checkbox("HUD", &Globals::IsUi))
         reshade::set_config_value(runtime, NAME, "IsHud", Globals::IsUi);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("Toggle HUD.\nWill discard all shaders after final output.");
      DrawResetButton(Globals::IsUi, true, "IsHud", runtime);
      
      ImGui::NewLine(); ////////////////////////////////////////////////////////////////////////////////////

      ImGui::Text("Options: Super Resolution");
      
      if (device_data.sr_type == SR::Type::None) ImGui::BeginDisabled();

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
      
      ImGui::NewLine(); ////////////////////////////////////////////////////////////////////////////////////

      ImGui::Text("Options: Super Resolution Debug");

      // if (ImGui::SliderFloat("SR Vertical Camera FOV", &Globals::SRVertCameraFOV, 0.f, 1.f, "%.3f"))
      //    reshade::set_config_value(runtime, NAME, "SRVertCameraFOV", Globals::SRVertCameraFOV);
      // if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      //    ImGui::SetTooltip("Vertical camera FOV for SR to use for calculations");
      // DrawResetButton(Globals::SRVertCameraFOV, 0.f, "SRVertCameraFOV", runtime);

      if (ImGui::SliderFloat("SR Exposure", &Globals::SRExposure, 0.f, 2.f, "%.4f"))
         reshade::set_config_value(runtime, NAME, "SRExposure", Globals::SRExposure);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("The exposure value for edge threshold calculations.");
      DrawResetButton(Globals::SRExposure, 0.f, "SRExposure", runtime);

      ImGui::BeginDisabled();
      if (ImGui::Checkbox("SR HDR", &Globals::SRIsHDR))
         reshade::set_config_value(runtime, NAME, "SRIsHDR", Globals::SRIsHDR);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("Allow HDR values?");
      DrawResetButton(Globals::SRIsHDR, true, "SRIsHDR", runtime);
      ImGui::EndDisabled();
      
      // if (ImGui::SliderFloat("SR Jitter Multiplier", &Globals::SRJitterMultiplier, 0.f, 2.f, "%.4f"))
      //    reshade::set_config_value(runtime, NAME, "SRJitterMultiplier", Globals::SRJitterMultiplier);
      // if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      //    ImGui::SetTooltip("Multiplier for jitter value.");
      // DrawResetButton(Globals::SRJitterMultiplier, 1.f, "SRJitterMultiplier", runtime);
      //
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

      if (device_data.sr_type == SR::Type::None) ImGui::EndDisabled();

      ImGui::NewLine(); ////////////////////////////////////////////////////////////////////////////////////

      ImGui::Text("Stats:");
      {
         std::string s0 = "Swapchain Changes: " + std::to_string(Globals::ChangeCountSwapchain);
         ImGui::BulletText(s0.c_str());

         std::string s1 = "SR Output Tex Changes: " + std::to_string(Globals::ChangeCountSRTex);
         ImGui::BulletText(s1.c_str());
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
      ImGui::BulletText("Luma: Pumbo (Filoppi)");
      ImGui::BulletText("RenoDX: clshortfuse");
      ImGui::BulletText("Mod: XgarhontX");
      ImGui::BulletText("HDR Consultant: Scrungus");
      ImGui::BulletText("Coding Help: Musa");
      ImGui::BulletText("Bug Hunter & Researcher: NikkMann");

      ImGui::NewLine();
      ImGui::Text("Third Party:");
      ImGui::BulletText("ReShade");
      ImGui::BulletText("ImGui");
      ImGui::BulletText("RenoDX");
      ImGui::BulletText("3Dmigoto");
      ImGui::BulletText("Oklab");
      ImGui::BulletText("JzAzBz");
      ImGui::BulletText("NVIDIA");
      ImGui::BulletText("AMD");
      ImGui::BulletText("DICE");
   }
};

// This is where everything starts from, the very first call to the dll.
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
   if (ul_reason_for_call == DLL_PROCESS_ATTACH)
   {
      //name
      Globals::SetGlobals(PROJECT_NAME, "Call of Duty: Black Ops 3 - Luma");
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
      enable_samplers_upgrade = false;

// #if DEVELOPMENT // If you want to track any shader names over time, you can hardcode them here by hash (they can be a useful reference in the pipeline)
//       forced_shader_names.emplace(std::stoul("FD2925B4", nullptr, 16), "Tracked Shader Name");
// #endif
//
// #if !DEVELOPMENT // Put shaders that a previous version of the mod used but has ever since been deleted here, so that users updating the mod from an older version won't accidentally load them
//       old_shader_file_names.emplace("Bloom_0xDC9373A8.ps_5_0.hlsl");CallOfDutyBlackOps3GameDeviceData
// #endif
      
      game = new CallOfDutyBlackOps3();
   }

   CoreMain(hModule, ul_reason_for_call, lpReserved);

   return TRUE;
}