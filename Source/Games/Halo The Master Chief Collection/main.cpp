#define GAME_HALOTMCC 1

// #define ENABLE_AUTO_CBUFFER_RESTORATION 1 //When off, conflicts H2A, but not detrimental
#define DISABLE_AUTO_DEBUGGER 1
#define ENABLE_BLOOM 1
#include "..\..\Core\core.hpp"

#define HALO_UPGRADE_SAMPLERS 0 // On is volatile for H2C

// ImGui util to edit Shader Defines
namespace ShaderDefines
{
   constexpr uint32_t ALLOW_AA = char_ptr_crc32("ALLOW_AA");
   constexpr uint32_t ALLOW_COLORGRADE = char_ptr_crc32("ALLOW_COLORGRADE");
   constexpr uint32_t HALO3_BLOOM = char_ptr_crc32("HALO3_BLOOM");
   constexpr uint32_t HALO1_BLOOM = char_ptr_crc32("HALO1_BLOOM");
   constexpr uint32_t HALO2_AO = char_ptr_crc32("HALO2_AO");
   constexpr uint32_t HALO2_GTAO = char_ptr_crc32("HALO2_GTAO");
   constexpr uint32_t HALO2_GTAO_NOISE = char_ptr_crc32("HALO2_GTAO_NOISE");
   constexpr uint32_t HALO2_GTAO_FULLRES = char_ptr_crc32("HALO2_GTAO_FULLRES");
   constexpr uint32_t HALOR_AO = char_ptr_crc32("HALOR_AO");
   constexpr uint32_t HALOR_FILMGRAIN_SCALE = char_ptr_crc32("HALOR_FILMGRAIN_SCALE");
   constexpr uint32_t XBOX360_CURVE = char_ptr_crc32("XBOX360_CURVE");
   constexpr uint32_t SWAPCHAIN_TEST_PEAK = char_ptr_crc32("SWAPCHAIN_TEST_PEAK");

   void OnInitAddNewDefines()
   {
      std::vector<ShaderDefineData> game_shader_defines_data = {
         {"GAMMA_CORRECTION_RANGE_TYPE", '0', true, !DEVELOPMENT, "0 - Full range.\n1 - 0-1 only.", 1},
         {"ALLOW_AA", '1', true, false, "Allow original anti-alias.", 1},
         {"ALLOW_COLORGRADE", '1', true, false, "Allow original color grading.", 1},
         {"HALO3_BLOOM", '1', true, false, "Halo 3 bloom mode.", 1},
         {"HALO1_BLOOM", '1', true, false, "Halo 1 Classic bloom.", 1},
         {"HALO2_AO", '1', true, false, "Halo 2 Anniversary AO quality.", 4},
         {"HALO2_GTAO", '1', true, false, "Halo 2 Anniversary GTAO replacement.", 1},
         {"HALO2_GTAO_NOISE", '1', true, false, "Halo 2 Anniversary GTAO noise movement.", 1},
         {"HALO2_GTAO_FULLRES", '1', true, false, "Halo 2 Anniversary GTAO do full resolution.", 1},
         {"HALOR_AO", '2', true, false, "Halo Reach GTAO replacement.", 5},
         {"HALOR_FILMGRAIN_SCALE", '1', true, false, "Halo Reach film grain scale.", 1},
         {"XBOX360_CURVE", '1', true, false, "Xbox 360 gamma perceptual correction.", 1},
         {"SWAPCHAIN_TEST_PEAK", '0', true, false, "Test pattern", 1},
      };
      shader_defines_data.append_range(game_shader_defines_data);
      assert(shader_defines_data.size() < MAX_SHADER_DEFINES);
   }
   
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

   // out: def, changed
   static std::pair<bool, bool> UIToggleCheckmark(uint32_t d, const char* label, const char* tooltip)
   {
      bool def = GetBool(d);
      
      ImGui::PushID(std::string(label).append("_").append(std::to_string(d)).c_str());
      bool c = ImGui::Checkbox(label, &def);
      ImGui::PopID();

      if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip(tooltip);
      
      if (c) ToggleBool(d);
      
      UIResetButton(d);
      return {def, c};
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

namespace 
{
   // // Buffer
   // struct Buffer
   // {
   //    D3D11_TEXTURE2D_DESC desc;
   //    ComPtr<ID3D11Texture2D> tex;
   //    ComPtr<ID3D11Resource> res;
   //    ComPtr<ID3D11ShaderResourceView> srv;
   //    ComPtr<ID3D11RenderTargetView> rtv;
   //    ComPtr<ID3D11UnorderedAccessView> uav;
   //
   //    uint2 GetDimensions() const
   //    {
   //       return { desc.Width, desc.Height };
   //    }
   //    
   //    void Reset()
   //    {
   //       desc = {};
   //       res.reset();
   //       tex.reset();
   //       srv.reset();
   //       rtv.reset();
   //       uav.reset();
   //    }
   //
   //    void CreateWithCurrentDesc(ID3D11Device* device)
   //    {
   //       auto hr0 = device->CreateTexture2D(&desc, nullptr, tex.put());
   //       if (DEVELOPMENT) ASSERT_ONCE(SUCCEEDED(hr0));
   //
   //       auto hr1 = tex->QueryInterface(res.put());
   //       if (DEVELOPMENT) ASSERT_ONCE(SUCCEEDED(hr1));
   //
   //       auto hr2 = device->CreateRenderTargetView(tex.get(), nullptr, rtv.put());
   //       if (DEVELOPMENT) ASSERT_ONCE(SUCCEEDED(hr2));
   //    
   //       auto hr3 = device->CreateShaderResourceView(tex.get(), nullptr, srv.put());
   //       if (DEVELOPMENT) ASSERT_ONCE(SUCCEEDED(hr3));
   //    }
   //
   //    static D3D11_TEXTURE2D_DESC GetDefaultDesc(uint2 output_resolution, DXGI_FORMAT format = DXGI_FORMAT_R16G16B16A16_FLOAT)
   //    {
   //       D3D11_TEXTURE2D_DESC desc = {};
   //       desc.Width = output_resolution.x;
   //       desc.Height = output_resolution.y;
   //       desc.MipLevels = 1;
   //       desc.ArraySize = 1;
   //       desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET | D3D11_BIND_UNORDERED_ACCESS;
   //       desc.Usage = D3D11_USAGE_DEFAULT;
   //       desc.Format = format;
   //       desc.SampleDesc = {1, 0};
   //       desc.CPUAccessFlags = 0;
   //       desc.MiscFlags = 0;
   //       return desc;
   //    }
   //
   //    void SetDefaultDesc(uint2 output_resolution, DXGI_FORMAT format = DXGI_FORMAT_R16G16B16A16_FLOAT)
   //    {
   //       desc = GetDefaultDesc(output_resolution, format);
   //    }
   // };
   
   ///////////////////////////////////
   
   // SubGame
   enum SubGame : uint8_t
   {
      Unknown,
      Halo1Classic,
      Halo1Anniversary,
      Halo2Classic,
      Halo2Anniversary,
      Halo2AnniversaryMP,
      Halo3,
      Halo3ODST,
      HaloReach,
      Halo4,
   };
   const char* SubGameToString(SubGame state, bool is_space = true)
   {
      switch (state)
      {
         case Unknown: return is_space ? "Menus (Unknown)" : "Unknown";
         case Halo1Classic: return is_space ? "Halo 1 Classic" : "Halo1Classic";
         case Halo1Anniversary: return is_space ? "Halo 1 Anniversary" : "Halo1Anniversary";
         case Halo2Classic: return is_space ? "Halo 2 Classic" : "Halo2Classic";
         case Halo2Anniversary: return is_space ? "Halo 2 Anniversary" : "Halo2Anniversary";
         case Halo2AnniversaryMP: return is_space ? "Halo 2 Anniversary Multiplayer" : "Halo2AnniversaryMP";
         case Halo3: return is_space ? "Halo 3" : "Halo3";
         case Halo3ODST: return is_space ? "Halo 3 ODST" : "Halo3ODST";
         case HaloReach: return is_space ? "Halo Reach" : "HaloReach";
         case Halo4: return is_space ? "Halo 4" : "Halo4";
         default: return "Unknown";
      }
   }
   
   ///////////////////////////////////////////////

   // XeGTAO insertion
   namespace XeGTAOHandler //stolen from BioShock mod
   {
      constexpr const char* Luma_H2A_XeGTAO = "Luma_H2A_XeGTAO"; //file name
      constexpr const char* Luma_H2A_XeGTAO_Prefilter = "XeGTAO H2A Prefilter Depths CS";
      constexpr const char* Luma_H2A_XeGTAO_MainPass = "XeGTAO H2A Main Pass CS";
      constexpr const char* Luma_H2A_XeGTAO_DenoisePass1 = "XeGTAO HH2A Denoise Pass 1 CS";
      constexpr const char* Luma_H2A_XeGTAO_DenoisePass2 = "XeGTAO H2A Denoise Pass 2 CS";

      constexpr const char* Luma_H3_XeGTAO = "Luma_H3_XeGTAO"; //file name
      
      constexpr size_t DEPTH_MIP_LEVELS = 5;
      constexpr UINT NUMTHREADS_X = 8;
      constexpr UINT NUMTHREADS_Y = 8;
      
      void OnInit()
      {
         native_shaders_definitions.emplace(CompileTimeStringHash(Luma_H2A_XeGTAO_Prefilter),    ShaderDefinition{ Luma_H2A_XeGTAO, reshade::api::pipeline_subobject_type::compute_shader, nullptr, "prefilter_depths16x16_cs" });
         native_shaders_definitions.emplace(CompileTimeStringHash(Luma_H2A_XeGTAO_MainPass),     ShaderDefinition{ Luma_H2A_XeGTAO, reshade::api::pipeline_subobject_type::compute_shader, nullptr, "main_pass_cs" });
         native_shaders_definitions.emplace(CompileTimeStringHash(Luma_H2A_XeGTAO_DenoisePass1), ShaderDefinition{ Luma_H2A_XeGTAO, reshade::api::pipeline_subobject_type::compute_shader, nullptr, "denoise_pass_cs", { { "XE_GTAO_FINAL_APPLY", "0" } } });
         native_shaders_definitions.emplace(CompileTimeStringHash(Luma_H2A_XeGTAO_DenoisePass2), ShaderDefinition{ Luma_H2A_XeGTAO, reshade::api::pipeline_subobject_type::compute_shader, nullptr, "denoise_pass_cs", { { "XE_GTAO_FINAL_APPLY", "1" } } });
      }

      namespace H2A
      {
         bool initialized = false;
         
         constexpr const char* denoise_reshadesave = "XeGTAOH2Denoise"; 
         constexpr int denoise_default = 1;
         int denoise = denoise_default;

         // Depth Prefilter
         D3D11_TEXTURE2D_DESC depthpre_texdesc = {};
         ComPtr<ID3D11Texture2D> depthpre_tex;
         
         D3D11_UNORDERED_ACCESS_VIEW_DESC depthpre_uavdesc = {};
         std::array<ID3D11UnorderedAccessView*, DEPTH_MIP_LEVELS> depthpre_uavs;
         
         ComPtr<ID3D11ShaderResourceView> depthpre_srv;

         // Main Pass (Terms & Edges)
         D3D11_TEXTURE2D_DESC main_texdesc = {};
         ComPtr<ID3D11Texture2D> main0_tex;
         ComPtr<ID3D11Texture2D> main1_tex;

         D3D11_UNORDERED_ACCESS_VIEW_DESC main_uavdesc = {};
         ComPtr<ID3D11UnorderedAccessView> main0_uav;
         ComPtr<ID3D11UnorderedAccessView> main1_uav;

         D3D11_SHADER_RESOURCE_VIEW_DESC main_srvdesc = {};
         ComPtr<ID3D11ShaderResourceView> main0_srv;
         ComPtr<ID3D11ShaderResourceView> main1_srv;

         D3D11_RENDER_TARGET_VIEW_DESC main_rtvdesc = {};
         ComPtr<ID3D11RenderTargetView> main0_rtv;
         ComPtr<ID3D11RenderTargetView> main2_rtv;

         // final after denoise fipflop
         ID3D11ShaderResourceView* ao_srv; 

         void Reset()
         {
            // gatekeep: state
            if (!initialized) return; 
            initialized = false;

            depthpre_tex.reset();
            for (auto& uav : depthpre_uavs) uav = nullptr;
            depthpre_srv.reset();

            main0_tex.reset();
            main1_tex.reset();
            main0_uav.reset();
            main1_uav.reset();
            main0_srv.reset();
            main1_srv.reset();
            main0_rtv.reset();
            main2_rtv.reset();

            // denoise1_tex.reset();
            // denoise1_uav.reset();
            // denoise1_srv.reset();
         }

         DrawOrDispatchOverrideType Draw0(DeviceData& device_data, ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data,
            ID3D11ShaderResourceView* srv_depth, ID3D11ShaderResourceView* srv_normals)
         {
            // CB: cb7
            ID3D11Buffer* cb7 = nullptr;
            native_device_context->PSGetConstantBuffers(7, 1, &cb7);
            native_device_context->CSSetConstantBuffers(0, 1, &cb7);

            // CB: Luma
            SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, reshade::api::shader_stage::compute, LumaConstantBufferType::LumaSettings);

            // samplers
            const std::array<ID3D11SamplerState*, 2> samplers = { device_data.sampler_state_point.get(), device_data.sampler_state_linear.get() };
            native_device_context->CSSetSamplers(0, samplers.size(), samplers.data());
            
            // Depth Prefilter: create tex, uavs, srv
            [[unlikely]]
            if (!initialized)
            {
               // tex desc
               depthpre_texdesc = {};
               depthpre_texdesc.Width = device_data.output_resolution.x;
               depthpre_texdesc.Height = device_data.output_resolution.y;
               depthpre_texdesc.MipLevels = DEPTH_MIP_LEVELS; //pre pass mips!
               depthpre_texdesc.ArraySize = 1;
               depthpre_texdesc.Format = DXGI_FORMAT_R32_FLOAT;
               depthpre_texdesc.SampleDesc.Count = 1;
               depthpre_texdesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
            
               // tex
               auto hr0 = native_device->CreateTexture2D(&depthpre_texdesc, nullptr, depthpre_tex.put());
               ASSERT_MSG(SUCCEEDED(hr0), "depthpre hr0");

               // uav desc
               depthpre_uavdesc.Format = DXGI_FORMAT_R32_FLOAT;
               depthpre_uavdesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
            
               // uavs
               for (int i = 0; i < depthpre_uavs.size(); ++i)
               {
                  depthpre_uavdesc.Texture2D.MipSlice = i;
                  auto hr = native_device->CreateUnorderedAccessView(depthpre_tex.get(), &depthpre_uavdesc, &depthpre_uavs[i]);
                  ASSERT_MSG(SUCCEEDED(hr), "depthpre loop hr");
               }
            
               // srv
               auto hr1 = native_device->CreateShaderResourceView(depthpre_tex.get(), nullptr, depthpre_srv.put());
               ASSERT_MSG(SUCCEEDED(hr1), "depthpre hr1");
            }
            
            // Depth Prefilter: bind & draw
            native_device_context->CSSetUnorderedAccessViews(0, depthpre_uavs.size(), depthpre_uavs.data(), nullptr); //out: prefiltered depth mips
            native_device_context->CSSetShader(device_data.native_compute_shaders.at(CompileTimeStringHash(Luma_H2A_XeGTAO_Prefilter)).get(), nullptr, 0);
            native_device_context->CSSetShaderResources(0, 1, &srv_depth); //in: depth
            native_device_context->Dispatch((depthpre_texdesc.Width + 16 - 1) / 16, (depthpre_texdesc.Height + 16 - 1) / 16, 1);
            
            // Depth Prefilter: unbind uavs
            constexpr ID3D11UnorderedAccessView* null_uavs[DEPTH_MIP_LEVELS] = {};
            native_device_context->CSSetUnorderedAccessViews(0,DEPTH_MIP_LEVELS, null_uavs, nullptr);
            
            // Main Pass: create AO terms & edges buffer
            [[unlikely]]
            if (!initialized)
            {
               // tex desc
               main_texdesc = {};
               const float scale = ShaderDefines::GetBool(ShaderDefines::HALO2_GTAO_FULLRES) ? 1.0f : 0.5f;
               main_texdesc.Width = device_data.output_resolution.x * scale;
               main_texdesc.Height = device_data.output_resolution.y * scale;
               main_texdesc.MipLevels = 1;
               main_texdesc.ArraySize = 1;
               main_texdesc.Format = DXGI_FORMAT_R8G8_UNORM;
               main_texdesc.SampleDesc.Count = 1;
               main_texdesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE /*| D3D11_BIND_RENDER_TARGET*/;
            
               // tex
               auto hr0a = native_device->CreateTexture2D(&main_texdesc, nullptr, main0_tex.put());
               ASSERT_MSG(SUCCEEDED(hr0a), "main hr0a");
               auto hr0b = native_device->CreateTexture2D(&main_texdesc, nullptr, main1_tex.put());
               ASSERT_MSG(SUCCEEDED(hr0b), "main hr0b");
               
               // uav desc
               main_uavdesc.Format = DXGI_FORMAT_R8G8_UNORM;
               main_uavdesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
               
               // uav
               auto hr1a = native_device->CreateUnorderedAccessView(main0_tex.get(), &main_uavdesc, main0_uav.put());
               ASSERT_MSG(SUCCEEDED(hr1a), "main hr1a");
               auto hr1b = native_device->CreateUnorderedAccessView(main1_tex.get(), &main_uavdesc, main1_uav.put());
               ASSERT_MSG(SUCCEEDED(hr1b), "main hr1b");
            
               // srv desc
               main_srvdesc = {};
               main_srvdesc.Format = DXGI_FORMAT_R8G8_UNORM;
               main_srvdesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
               main_srvdesc.Texture2D.MostDetailedMip = 0;
               main_srvdesc.Texture2D.MipLevels = 1;
               
               // srv
               auto hr2a = native_device->CreateShaderResourceView(main0_tex.get(), /*&H2A::main_srvdesc*/nullptr, main0_srv.put());
               ASSERT_MSG(SUCCEEDED(hr2a), "main hr2a");
               auto hr2b = native_device->CreateShaderResourceView(main1_tex.get(), /*&H2A::main_srvdesc*/nullptr, main1_srv.put());
               ASSERT_MSG(SUCCEEDED(hr2b), "main hr2b");
            }
            
            // Main: bind and draw
            native_device_context->CSSetUnorderedAccessViews(0, 1, &main0_uav, nullptr); //out: AO term and Edges
            native_device_context->CSSetShader(device_data.native_compute_shaders.at(CompileTimeStringHash(Luma_H2A_XeGTAO_MainPass)).get(), nullptr, 0);
            const std::array<ID3D11ShaderResourceView*, 2> srvs_main_pass = { depthpre_srv.get(), srv_normals }; //in: prefiltered depth mips & normals
            native_device_context->CSSetShaderResources(0, srvs_main_pass.size(), srvs_main_pass.data());
            native_device_context->Dispatch((main_texdesc.Width + NUMTHREADS_X - 1) / NUMTHREADS_X, (main_texdesc.Height + NUMTHREADS_Y - 1) / NUMTHREADS_Y, 1);

            // Denoise bind and draw loop
            bool ao_flipflop = false;
            for (int i = 0; i < denoise; i++)
            {
               // flipflop
               ID3D11ShaderResourceView*  ao_in  = !ao_flipflop ? main0_srv.get() : main1_srv.get();
               ID3D11UnorderedAccessView* ao_out = !ao_flipflop ? main1_uav.get() : main0_uav.get();
               ao_flipflop = !ao_flipflop;
         
               // final?
               auto cs = i < denoise - 1 ? CompileTimeStringHash(Luma_H2A_XeGTAO_DenoisePass1) : CompileTimeStringHash(Luma_H2A_XeGTAO_DenoisePass2);

               // bind & draw
               native_device_context->CSSetUnorderedAccessViews(0, 1, &ao_out, nullptr); //out: denoised
               native_device_context->CSSetShader(device_data.native_compute_shaders.at(cs).get(), nullptr, 0);
               native_device_context->CSSetShaderResources(0, 1, &ao_in); //in: AO term and edges
               native_device_context->Dispatch((main_texdesc.Width + (NUMTHREADS_X * 2) - 1) / (NUMTHREADS_X * 2), (main_texdesc.Height + NUMTHREADS_Y - 1) / NUMTHREADS_Y,1); // half width, but cs does 2 pixels
            }
            ao_srv = !ao_flipflop ? main0_srv.get() : main1_srv.get();

            // unbind all!
            constexpr std::array<ID3D11UnorderedAccessView*, 1> null_1uavs = { };
            native_device_context->CSSetUnorderedAccessViews(0, null_1uavs.size(), null_1uavs.data(), nullptr);
            
            constexpr std::array<ID3D11ShaderResourceView*, 2> null_2srvs = { };
            native_device_context->CSSetShaderResources(0, null_2srvs.size(), null_2srvs.data());

            constexpr std::array<ID3D11Buffer*, 1> null_1cb = { };
            native_device_context->CSSetConstantBuffers(0, null_1cb.size(), null_1cb.data());
            native_device_context->CSSetConstantBuffers(luma_data_cbuffer_index, null_1cb.size(), null_1cb.data());

            constexpr ID3D11ComputeShader* null_cs = nullptr;
            native_device_context->CSSetShader(null_cs, nullptr, 0);

            constexpr std::array<ID3D11SamplerState*, 2> null_1samplers = { };
            native_device_context->CSSetSamplers(0, null_1samplers.size(), null_1samplers.data());

            // // set RTV 0 to main rtv
            // native_device_context->OMSetRenderTargets(1, &H2A::main_rtv, nullptr);

            // state
            initialized = true;

            // skip orig since we did its job
            return DrawOrDispatchOverrideType::Replaced;
         }

         DrawOrDispatchOverrideType Draw1(DeviceData& device_data, ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data,
            ID3D11ShaderResourceView* srv_depth, ID3D11ShaderResourceView* srv_normals)
         {
            // ps: main bind t6 srv
            native_device_context->PSSetShaderResources(6, 1, &ao_srv);

            // let ps act as copy
            return DrawOrDispatchOverrideType::None;
         }
      }

      namespace HR
      {
         namespace FoundResources
         {
            // CB
            ComPtr<ID3D11Buffer> viewvs_cb = nullptr;
            // cbuffer ViewVS
            // {
            //   float4x4 View_Projection;
            //   float4x4 Camera_To_World;
            //   float4 v_clip_plane;
            // }

            // WorldNormals
            ComPtr<ID3D11ShaderResourceView> worldnormals_srv = nullptr;
         }

         bool IsReady()
         {
            return FoundResources::worldnormals_srv && FoundResources::viewvs_cb;
         }

         void Reset()
         {
            FoundResources::viewvs_cb.reset();
            FoundResources::worldnormals_srv.reset();
         }

         void OnDraw(DeviceData& device_data, ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data,
            uint32_t vs, uint32_t ps)
         {
            // gatekeep: enabled
            if (ShaderDefines::Get(ShaderDefines::HALOR_AO) == 0) return;
            
            [[unlikely]]
            if (!IsReady())
            {
               // steal cb0 ViewVS
               const static std::unordered_set<uint32_t> viewvs_using_shaders = { //hashs that use ViewVS cb0
                  0x59333658,
                  0xC2CB44CD,
                  0xEF304905,
                  0x48D09462,
               };
               if (!FoundResources::viewvs_cb.get() && viewvs_using_shaders.contains(vs))
               {
                  native_device_context->VSGetConstantBuffers(0, 1, FoundResources::viewvs_cb.put());
                  ASSERT_MSG(XeGTAOHandler::HR::FoundResources::viewvs_cb.get(), "HaloReach Failed to get ViewVS CB 0");
               }

               // create our own SRV to normals
               const static std::unordered_map<uint32_t, int8_t> normals_using_shaders = { //hash to SRV index (if >= 0) or RTV index (if < 0, -1 = RTV0, -2 = RTV1, etc.)
                  {0x4C8A0B12, -1 - 1},
                  {0x04719B4D, -1 - 1},
                  {0x2CE86C20, -1 - 1},
                  {0x3649837C, -1 - 1},
                  {0x4E934D49, -1 - 1},
                  {0x92AD1942, -1 - 1},
                  {0xA9B18655, 2},
               };
               if (!FoundResources::worldnormals_srv.get())
               {
                  auto info = normals_using_shaders.find(ps);
                  if (info != normals_using_shaders.end())
                  {
                     // create our own SRV to normals
                     ComPtr<ID3D11Resource> res;
                     int i = info->second;
                     if (i >= 0) // SRV
                     {
                        ID3D11ShaderResourceView* srv;
                        native_device_context->PSGetShaderResources(i, 1, &srv);
                        ASSERT_MSG(srv, "HaloReach Failed to get WorldNormals SRV >= 0");
                        srv->GetResource(res.put());
                     }
                     else //RTV
                     {
                        constexpr int amount = 2;
                        i = std::abs(i) - 1; // parsed RTV index

                        // fetch
                        std::array<ID3D11RenderTargetView*, amount> rtvs;
                        ID3D11DepthStencilView* dsv;
                        native_device_context->OMGetRenderTargets(amount, rtvs.data(), &dsv);

                        // take
                        ID3D11RenderTargetView* rtv;
                        rtv = rtvs[i];

                        // RES
                        ASSERT_MSG(rtv, "HaloReach Failed to get WorldNormals RTV < 0");
                        rtv->GetResource(res.put());

                        // release
                        for (int j = 0; j < amount; j++) if (rtvs[j]) rtvs[j]->Release();
                     }
                     ASSERT_MSG(res, "HaloReach Failed to get WorldNormals Resource < 0");

                     // SRV
                     D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
                     srv_desc.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
                     srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                     srv_desc.Texture2D.MostDetailedMip = 0;
                     srv_desc.Texture2D.MipLevels = 1;
                     auto hr = native_device->CreateShaderResourceView(res.get(), &srv_desc, FoundResources::worldnormals_srv.put());
                     ASSERT_ONCE_MSG(SUCCEEDED(hr), "HaloReach Failed to create WorldNormals SRV BRUH");
                  }
               }
            }
            else
            {
               if (ps == 0x90E0D303)
               {
                  // Depth Prepass
                  // TODO: would CS insert be faster than PS?
                  
                  // pass in CB -> 6 & SRV -> 2
                  native_device_context->PSSetConstantBuffers(6, 1, &FoundResources::viewvs_cb);
                  native_device_context->PSSetShaderResources(2, 1, &FoundResources::worldnormals_srv);
               }
            }
         }
      }
      
      void OnLoadConfigs()
      {
         reshade::get_config_value(nullptr, NAME, H2A::denoise_reshadesave, H2A::denoise);
      }
      
      void OnSubGameChange(SubGame prev_game, SubGame new_game)
      {
         if (prev_game == Halo2Anniversary && new_game != Halo2Anniversary) H2A::Reset();
         if (prev_game == HaloReach && new_game != HaloReach) HR::Reset();
      }
   }

   namespace BloomHandler
   {
      void OnInit()
      {
      }

      namespace H1C
      {
         constexpr const char* sigma_reshadesave = "H1CBloomSigma";
         constexpr float sigma_default = 1.0f;
         float sigma = sigma_default;

         namespace Resource
         {
         }

         bool IsReady()
         {
         }
         
         void Reset()
         {
         }
         
         void OnDraw(DeviceData& device_data, ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data)
         {
            // gatekeep: enabled
            if (!ShaderDefines::Get(ShaderDefines::HALO1_BLOOM)) return;
            
            // get color
            ID3D11ShaderResourceView* srv = nullptr;
            native_device_context->PSGetShaderResources(0, 1, &srv);

            // draw
            constexpr int nmips = 5;
            const std::array<float, nmips> sigmas = { sigma * 1.5f, sigma, sigma, sigma, sigma };
            ComPtr<ID3D11ShaderResourceView> bloom_out;
            DrawBloom(native_device, native_device_context, device_data, srv, nmips, sigmas.data(), bloom_out.put());

            // bind out to SRV 1 (original PS will use)
            native_device_context->PSSetShaderResources(1, 1, &bloom_out);
         }
      }

      void OnLoadConfigs()
      {
         reshade::get_config_value(nullptr, NAME, H1C::sigma_reshadesave, H1C::sigma);
      }

      void OnSubGameChange(SubGame prev_game, SubGame new_game)
      {
         if (prev_game == Halo1Classic && new_game != Halo1Classic) H1C::Reset();
      }
   }
   
   ///////////////////////////////////

   // Clear Indirect Upgrades Handler
   namespace ClearIndirectUpgradesHandler
   {
      bool is_queued = false;
      void Request()
      {
         is_queued = true;
      }
      void OnReShadePresent(reshade::api::effect_runtime* runtime)
      {
         // return when not queued
         if (!is_queued) return;
         is_queued = false;

         // log
         message(reshade::log::level::info, "Clearing indirect upgrades");

         // clear old upgrades
         DeviceData& device_data = *runtime->get_device()->get_private_data<DeviceData>();
         std::unordered_map<uint64_t, uint64_t> original_resource_views_to_mirrored_upgraded_resource_views;
         std::unordered_map<uint64_t, uint64_t> original_resources_to_mirrored_upgraded_resources;
         std::shared_lock lock_device_read(device_data.mutex);

         // clear in runtime device
         if (!device_data.original_resources_to_mirrored_upgraded_resources.empty())
         {
            lock_device_read.unlock();
            {
               std::unique_lock lock_device_write(device_data.mutex);
               original_resource_views_to_mirrored_upgraded_resource_views = device_data.original_resource_views_to_mirrored_upgraded_resource_views;
               original_resources_to_mirrored_upgraded_resources = device_data.original_resources_to_mirrored_upgraded_resources;
               device_data.original_resource_views_to_mirrored_upgraded_resource_views.clear();
               device_data.original_resources_to_mirrored_upgraded_resources.clear();
            }
            for (const auto& original_resource_view_to_mirrored_upgraded_resource_view : original_resource_views_to_mirrored_upgraded_resource_views)
               runtime->get_device()->destroy_resource_view({ original_resource_view_to_mirrored_upgraded_resource_view.second });
            for (const auto& original_resource_to_mirrored_upgraded_resource : original_resources_to_mirrored_upgraded_resources)
               runtime->get_device()->destroy_resource({ original_resource_to_mirrored_upgraded_resource.second });
         }
      }
   }
   
   ///////////////////////////////////

   // Optimization to avoid heavy indirect upgrades scanning
   namespace WarmupDirectAndIndirectHandler
   {
      int warmup_frames = 0;
      ChainTextureFormatUpgradesType warmup_end = ChainTextureFormatUpgradesType::DirectDependencies;
      void Start(int frames = 4, ChainTextureFormatUpgradesType type = ChainTextureFormatUpgradesType::DirectAndIndirectDependencies, ChainTextureFormatUpgradesType end_type = ChainTextureFormatUpgradesType::DirectDependencies)
      {
         warmup_frames = frames;
         enable_chain_indirect_texture_format_upgrades = type;
         warmup_end = end_type;
      }
      void OnPresent()
      {
         if (warmup_frames > -1) warmup_frames--;
         if (warmup_frames == 0) enable_chain_indirect_texture_format_upgrades = warmup_end;
      }
   }
   
   ///////////////////////////////////

   // user settings for each SubGame, applied on change, and saved to ini
   namespace SubGameUserSettingsHandler
   {
      bool enabled = false;
      
      struct Settings
      {
         SubGame subgame;
         int peak;
         int paper_scene;
         int paper_ui;

         enum Compatibilty : uint8_t
         {
            NotSupported,
            WIP,
            Untested,
            Working,
         };
         Compatibilty compatibility;

         std::pair<const char*, ImVec4> GetCompatibility() const
         {
            switch (compatibility)
            {
               case NotSupported: return std::make_tuple("Not Supported", ImColor(255, 0, 0)); // Red
               case WIP: return std::make_tuple("WIP", ImColor(255, 165, 0)); // Orange
               case Untested: return std::make_tuple("Untested", ImColor(255, 255, 0)); // Yellow
               case Working: return std::make_tuple("Working", ImColor(0, 255, 0)); // Green
               default: return std::make_tuple("Unknown", ImColor(0, 0, 0));
            }
         }

         // Default constructor taking in name and compatibility
         Settings(SubGame subgame, Compatibilty compatibility)
            : subgame(subgame), peak(0), paper_scene(0), paper_ui(0), compatibility(compatibility) {}
      };

      std::vector<Settings> settings = {
         { Unknown, Settings::Working },
         { Halo1Classic, Settings::Working },
         { Halo1Anniversary, Settings::WIP },
         { Halo2Classic, Settings::WIP },
         { Halo2Anniversary,  Settings::Working },
         { Halo2AnniversaryMP, Settings::NotSupported },
         { Halo3,  Settings::Working },
         { Halo3ODST, Settings::Working },
         { HaloReach, Settings::Working },
         { Halo4, Settings::Untested },
      };

      Settings* GetSettings(SubGame subgame)
      {
         auto s = &settings[subgame];
         ASSERT_MSG(s, "Settings for SubGame not found");
         return s;
      }

      void OnSubGameChange(SubGame new_game)
      {
         if (!enabled) return;

         auto sg = GetSettings(new_game);
         if (!sg) return;

         message(reshade::log::level::info, std::format("SubGameUserSettingsHandler Applying SubGame settings for {}: Peak={}, ScenePaperWhite={}, GamePaperWhite={}", SubGameToString(new_game), sg->peak, sg->paper_scene, sg->paper_ui).c_str());

         if (sg->peak > 0) cb_luma_global_settings.ScenePeakWhite = sg->peak;
         if (sg->paper_scene > 0) cb_luma_global_settings.ScenePaperWhite = sg->paper_scene;
         if (sg->paper_ui > 0) cb_luma_global_settings.UIPaperWhite = sg->paper_ui;
      }

      const char* GetSubGameSaveKey(SubGame state, const char* setting_name)
      {
         return std::format("{}_{}", SubGameToString(state, false), setting_name).c_str();
      }

      constexpr const char* reshade_config_section = "SubGameUserSettingsHandler";
      void OnImGui(SubGame curr_game)
      {
         if (ImGui::Checkbox("Enable Per Game Settings", &enabled))
         {
            reshade::set_config_value(nullptr, reshade_config_section, "EnablePerGameSettings", enabled);
            OnSubGameChange(curr_game);
         }

         if (!enabled) ImGui::BeginDisabled();
         for (const auto& sg : settings)
         {
            ImGui::PushID(("###SubGameUserSettingsHandler" + std::to_string(sg.subgame)).c_str());
            if (ImGui::TreeNode(SubGameToString(sg.subgame)))
            {
               auto [compatibility_text, compatibility_color] = sg.GetCompatibility();
               ImGui::PushStyleColor(ImGuiCol_Text, compatibility_color);
               ImGui::TextWrapped("HDR Compatibility: %s", compatibility_text);
               ImGui::PopStyleColor();
               
               auto* s = GetSettings(sg.subgame);

               int peak = s->peak;
               int paper_scene = s->paper_scene;
               int paper_ui = s->paper_ui;

               /*ImGui::SameLine();*/ if (ImGui::Button("Set: Luma Defaults")) { s->peak = default_peak_white; s->paper_scene = default_paper_white; s->paper_ui = default_paper_white; }
               ImGui::SameLine(); if (ImGui::Button("Set: Current Settings")) { s->peak = cb_luma_global_settings.ScenePeakWhite; s->paper_scene = cb_luma_global_settings.ScenePaperWhite; s->paper_ui = cb_luma_global_settings.UIPaperWhite; }
               ImGui::SameLine(); if (ImGui::Button("Set: Disable")) { s->peak = 0; s->paper_scene = 0; s->paper_ui = 0; }

               auto GetFormat = [](int v) { return v > 0 ? "%d" : "%d (Inactive)"; };
               ImGui::SliderInt("Display Peak", &s->peak, 0, 4000, GetFormat(s->peak));
               ImGui::SliderInt("Scene Paper White", &s->paper_scene, 0, 500, GetFormat(s->paper_scene));
               ImGui::SliderInt("UI Paper White", &s->paper_ui, 0, 500, GetFormat(s->paper_ui));
               
               if (peak != s->peak) reshade::set_config_value(nullptr, reshade_config_section, GetSubGameSaveKey(sg.subgame, "Peak"), s->peak);
               if (paper_scene != s->paper_scene) reshade::set_config_value(nullptr, reshade_config_section, GetSubGameSaveKey(sg.subgame, "Scene"), s->paper_scene);
               if (paper_ui != s->paper_ui) reshade::set_config_value(nullptr, reshade_config_section, GetSubGameSaveKey(sg.subgame, "UI"), s->paper_ui);

               ImGui::TreePop();
            }
            ImGui::PopID();
         }
         if (!enabled) ImGui::EndDisabled();
      }

      void OnLoadConfigs()
      {
         reshade::get_config_value(nullptr, reshade_config_section, "EnablePerGameSettings", enabled);

         for (auto& sg : settings)
         {
            auto* s = GetSettings(sg.subgame);
            reshade::get_config_value(nullptr, reshade_config_section, GetSubGameSaveKey(sg.subgame, "Peak"), s->peak);
            reshade::get_config_value(nullptr, reshade_config_section, GetSubGameSaveKey(sg.subgame, "Scene"), s->paper_scene);
            reshade::get_config_value(nullptr, reshade_config_section, GetSubGameSaveKey(sg.subgame, "UI"), s->paper_ui);
         }

         // try load Unknown settings
         OnSubGameChange(Unknown);
      }
   }
   
   ///////////////////////////////////

   // SubGame Handler
   namespace SubGameHandler
   {
      // SubGame state
      SubGame curr = Unknown;
      SubGame over = Unknown;
      bool best_resource_unorm_disallow = false;
      void SetSubGame(SubGame new_game)
      {
         // wait for clean up...
         if (ClearIndirectUpgradesHandler::is_queued) return;
         
         // resolve override
         if (over != Unknown) new_game = over;
         
         // prev
         auto prev = curr;
         bool is_changed = prev != new_game;

         // last
         static auto last_game = Unknown; // never Unknown
         bool is_completely_changed = false;
         if (new_game != Unknown)
         {
            is_completely_changed = last_game != new_game;
            last_game = new_game;
         }

         // completely changed, so clear indirect upgrades
         if (is_completely_changed)
         {
            ClearIndirectUpgradesHandler::Request();
            curr = Unknown;
            return;
         }

         // set
         curr = new_game;
         cb_luma_global_settings.GameSettings.SubGame = static_cast<int>(curr);

         // not changed, so useless to continue ////////////////////////////////////////////////////////
         if (!is_changed) return; 
         
         // log
         message(reshade::log::level::info, std::format("SubGame changed from {} to {}", SubGameToString(prev), SubGameToString(curr)).c_str());
         
         // SubGameUserSettingsHandler
         SubGameUserSettingsHandler::OnSubGameChange(curr);

         // XeGTAOHandler & BloomHandler
         XeGTAOHandler::OnSubGameChange(prev, curr);
         BloomHandler::OnSubGameChange(prev, curr);
         
         // reset upgrades params
         enable_chain_indirect_texture_format_upgrades = ChainTextureFormatUpgradesType::DirectDependencies;
#if DAV_CORE
         best_resource_unorm = false; //TODO: Luma needs this or something better for core.hpp!
#endif
         ignore_upgraded_samplers = true;
         
         // clear old hashes
         auto_texture_format_upgrade_shader_hashes.clear();
         
         // set new Indirect Upgrades hashes
         auto_texture_format_upgrade_shader_hashes[0x5B190892] = std::pair{ std::vector<uint8_t>{ 0 }, std::vector<uint8_t>() }; //ui blurdown00
         auto_texture_format_upgrade_shader_hashes[0x3CC502A9] = std::pair{ std::vector<uint8_t>{ 0 }, std::vector<uint8_t>() }; //ui blurdown00 subsequent blurring
         auto_texture_format_upgrade_shader_hashes[0xF207E935] = std::pair{ std::vector<uint8_t>{ 0 }, std::vector<uint8_t>() }; //ui blur settings 0
         auto_texture_format_upgrade_shader_hashes[0xE45B4EB7] = std::pair{ std::vector<uint8_t>{ 0 }, std::vector<uint8_t>() }; //ui blur settings subsequent downsample
         switch (curr)
         {
            case Halo1Classic:
               auto_texture_format_upgrade_shader_hashes[0xB70CC18B] = std::pair{ std::vector<uint8_t>{ 0 }, std::vector<uint8_t>() }; //fxaa
               // if (!best_resource_unorm_disallow) best_resource_unorm = true; //TODO: Luma needs this or something better for core.hpp!
               WarmupDirectAndIndirectHandler::Start();
               break;
            case Halo1Anniversary:
               // auto_texture_format_upgrade_shader_hashes[0xDCC32775] = std::pair{ std::vector<uint8_t>{ 0 }, std::vector<uint8_t>() }; //transparency combine
               // auto_texture_format_upgrade_shader_hashes[0x700325CF] = std::pair{ std::vector<uint8_t>{ 0 }, std::vector<uint8_t>() }; //downsample (exposure)
               // auto_texture_format_upgrade_shader_hashes[0xB70CC18B] = std::pair{ std::vector<uint8_t>{ 0 }, std::vector<uint8_t>() }; //fxaa
#if DAV_CORE
               best_resource_unorm = true;
#endif
               break;
            case Halo2Classic:
               auto_texture_format_upgrade_shader_hashes[0xD39821CB] = std::pair{ std::vector<uint8_t>{ 0 }, std::vector<uint8_t>() }; //blit
#if DAV_CORE
               if (!best_resource_unorm_disallow) best_resource_unorm = true; //TODO: Luma needs this or something better for core.hpp!
#endif
               break;
            case Halo2Anniversary:
               auto_texture_format_upgrade_shader_hashes[0x87F940A3] = std::pair{ std::vector<uint8_t>{ 0 }, std::vector<uint8_t>() }; //dof down 0
               auto_texture_format_upgrade_shader_hashes[0xBF5A726E] = std::pair{ std::vector<uint8_t>{ 0 }, std::vector<uint8_t>() }; //dof down 1
               auto_texture_format_upgrade_shader_hashes[0xC5A027C1] = std::pair{ std::vector<uint8_t>{ 0 }, std::vector<uint8_t>() }; //dof final
               auto_texture_format_upgrade_shader_hashes[0x8D11B112] = std::pair{ std::vector<uint8_t>{ 0 }, std::vector<uint8_t>() }; //t00
               auto_texture_format_upgrade_shader_hashes[0xBDDD9A3C] = std::pair{ std::vector<uint8_t>{ 0 }, std::vector<uint8_t>() }; //t01
               auto_texture_format_upgrade_shader_hashes[0xE5A32080] = std::pair{ std::vector<uint8_t>{ 0 }, std::vector<uint8_t>() }; //t02
               auto_texture_format_upgrade_shader_hashes[0x60449413] = std::pair{ std::vector<uint8_t>{ 0 }, std::vector<uint8_t>() }; //t03
               auto_texture_format_upgrade_shader_hashes[0xB5D334B0] = std::pair{ std::vector<uint8_t>{ 0 }, std::vector<uint8_t>() }; //aa
               auto_texture_format_upgrade_shader_hashes[0x9EC6DFC8] = std::pair{ std::vector<uint8_t>{ 0 }, std::vector<uint8_t>() }; //blit
               auto_texture_format_upgrade_shader_hashes[0xBF5A726E] = std::pair{ std::vector<uint8_t>{ 0 }, std::vector<uint8_t>() }; //after blit downsample blur (concussion)
               auto_texture_format_upgrade_shader_hashes[0x3D30DAB7] = std::pair{ std::vector<uint8_t>{ 0 }, std::vector<uint8_t>() }; //some generic copy that runs after CopyResource()
               ignore_upgraded_samplers = false;
               break;
            case Halo3:
               // 0x01262530: color diffuse copy (SRV0 is used by regular opaque. RTV0 is used by small and cutout stuff (vegetation))
               auto_texture_format_upgrade_shader_hashes[0xEEB815BC] = std::pair{ std::vector<uint8_t>{ 0 }, std::vector<uint8_t>() }; //t00 
               auto_texture_format_upgrade_shader_hashes[0x7D41B2E6] = std::pair{ std::vector<uint8_t>{ 0 }, std::vector<uint8_t>() }; //t01 
               auto_texture_format_upgrade_shader_hashes[0x9EC6DFC8] = std::pair{ std::vector<uint8_t>{ 0 }, std::vector<uint8_t>() }; //fxaa
               WarmupDirectAndIndirectHandler::Start();
               break;
            case Halo3ODST:
               auto_texture_format_upgrade_shader_hashes[0xADADBE3D] = std::pair{ std::vector<uint8_t>{ 0 }, std::vector<uint8_t>() }; //t00
               auto_texture_format_upgrade_shader_hashes[0x2193CAB5] = std::pair{ std::vector<uint8_t>{ 0 }, std::vector<uint8_t>() }; //t01 
               auto_texture_format_upgrade_shader_hashes[0x9EC6DFC8] = std::pair{ std::vector<uint8_t>{ 0 }, std::vector<uint8_t>() }; //fxaa
               auto_texture_format_upgrade_shader_hashes[0x03B68268] = std::pair{ std::vector<uint8_t>{ 0 }, std::vector<uint8_t>() }; //noise overlay
               WarmupDirectAndIndirectHandler::Start();
               break;
            case HaloReach:
               auto_texture_format_upgrade_shader_hashes[0x6A2F1FE6] = std::pair{ std::vector<uint8_t>{ 0 }, std::vector<uint8_t>() }; //downsample
               auto_texture_format_upgrade_shader_hashes[0xC1FF277A] = std::pair{ std::vector<uint8_t>{ 0 }, std::vector<uint8_t>() }; //t00
               auto_texture_format_upgrade_shader_hashes[0x363648B2] = std::pair{ std::vector<uint8_t>{ 0 }, std::vector<uint8_t>() }; //t01
               auto_texture_format_upgrade_shader_hashes[0x924D7B98] = std::pair{ std::vector<uint8_t>{ 0 }, std::vector<uint8_t>() }; //t02
               auto_texture_format_upgrade_shader_hashes[0x0EFB2B17] = std::pair{ std::vector<uint8_t>{ 0 }, std::vector<uint8_t>() }; //fxaa
               auto_texture_format_upgrade_shader_hashes[0xBD7AE2AF] = std::pair{ std::vector<uint8_t>{ 0 }, std::vector<uint8_t>() }; //health pickup fx: down
               auto_texture_format_upgrade_shader_hashes[0x270131A1] = std::pair{ std::vector<uint8_t>{ 0 }, std::vector<uint8_t>() }; //health pickup fx: kernel blur
               break;
            case Halo4:
               auto_texture_format_upgrade_shader_hashes[0x3A2F6CF7] = std::pair{ std::vector<uint8_t>{ 0 }, std::vector<uint8_t>() }; //t00
               auto_texture_format_upgrade_shader_hashes[0xB38416A2] = std::pair{ std::vector<uint8_t>{ 0 }, std::vector<uint8_t>() }; //t01
               auto_texture_format_upgrade_shader_hashes[0xCCC24837] = std::pair{ std::vector<uint8_t>{ 0 }, std::vector<uint8_t>() }; //fxaa
               break;
            default: ;
         }
      }
      
      // Safe way to switch in OnDrawOrDispatch()
      SubGame queued = Unknown;
      void Enqueue(SubGame game, bool is_override = false)
      {
         if (!is_override && queued != Unknown) return;
         queued = game;
      }
      // Dequeued on OnPresent()
      void Deque(DeviceData& device_data)
      {
         SetSubGame(queued);
         queued = Unknown;
      }

      // full reset!
      void Reinit()
      {
         queued = Unknown;
         over = Unknown;
         SetSubGame(Unknown);
      }
   };
   
   ///////////////////////////////////////////////

#if HALO_UPGRADE_SAMPLERS
   // mip_lod_bias_offset
   float mip_lod_bias_offset = 0.f; //allow user to set offset //TODO: works, but CAUSES CRASH FOR H2C!!!
   void SetMipLodBiasOffset(float offset, bool save_to_config = true)
   {
      mip_lod_bias_offset = offset;
      if (save_to_config) reshade::set_config_value(nullptr, NAME, "mip_lod_bias_offset", mip_lod_bias_offset);
   }
#endif
   
   ///////////////////////////////////////////////

   // allow_gamma_slider
   bool allow_gamma_slider = false;

   // allow_pause_screen
   bool allow_pause_screen = true;
   bool allow_pause_screen_skiptoken = false;

   // seen_readme
   bool seen_readme = false;
   
   ///////////////////////////////////////////////

   // GetPulsingColor
   ImVec4 GetPulsingColor(ImColor color, float speed = 0.1f, float amount = 0.25f)
   {
      float m = (1.f-amount/2) + amount/2 * sinf(cb_luma_global_settings.FrameIndex * speed);
      color.Value.x *= m;
      color.Value.y *= m;
      color.Value.z *= m;
      return color;
   }
}

// struct GameDeviceDataHaloTMCC : GameDeviceData
// {   
//
// };

class GameHaloTMCC final : public Game
{   
public:
   void OnInit(bool async) override
   {
      // cb
      luma_settings_cbuffer_index = 9;  //though conflict H2A, no problem
      luma_data_cbuffer_index     = 10; //though conflict H2A, no problem

      // cb init
      default_luma_global_game_settings.Bloom = cb_luma_global_settings.GameSettings.Bloom = 1.f;
      default_luma_global_game_settings.FilmGrain = cb_luma_global_settings.GameSettings.FilmGrain = 1.f;
      default_luma_global_game_settings.WhiteClip = cb_luma_global_settings.GameSettings.WhiteClip = 1.f;
      default_luma_global_game_settings.AmbientOcclusion = cb_luma_global_settings.GameSettings.AmbientOcclusion = 1.f;
      default_luma_global_game_settings.MotionBlur = cb_luma_global_settings.GameSettings.MotionBlur = 1.f;

      // Shader Defines
      ShaderDefines::OnInitAddNewDefines();
      GetShaderDefineData(UI_DRAW_TYPE_HASH).SetDefaultValue('3');
      auto_recompile_defines = true;

      // Native Shaders: Display Composition replacement
      native_shaders_definitions.erase(CompileTimeStringHash("Display Composition"));
      native_shaders_definitions.emplace(CompileTimeStringHash("Display Composition"), ShaderDefinition{"Luma_DisplayComposition1", reshade::api::pipeline_subobject_type::pixel_shader});

      // XeGTAOHandler & BloomHandler
      XeGTAOHandler::OnInit();
      BloomHandler::OnInit();

      // OnReShadePresent
      reshade::register_event<reshade::addon_event::reshade_present>(ClearIndirectUpgradesHandler::OnReShadePresent);
   }

   void OnInitSwapchain(reshade::api::swapchain* swapchain) override
   {
      // SubGame reset
      SubGameHandler::Reinit();

      // XeGTAOHandler
      XeGTAOHandler::H2A::Reset();
   }

   DrawOrDispatchOverrideType OnDrawOrDispatch(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, reshade::api::shader_stage stages, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, bool is_custom_pass, bool& updated_cbuffers, std::function<void()>* original_draw_dispatch_func) override
   {
      // Setup
      // GameDeviceDataHaloTMCC* game_device_data = static_cast<GameDeviceDataHaloTMCC*>(device_data.game);
      const auto vs = original_shader_hashes.vertex_shaders[0];
      const auto ps = original_shader_hashes.pixel_shaders[0];
      const auto cs = original_shader_hashes.compute_shaders[0];

      // DEVELOPMENT mod active
      if (!custom_shaders_enabled && ignore_indirect_upgraded_textures)
      {
         SubGameHandler::SetSubGame(SubGame::Unknown);
         return DrawOrDispatchOverrideType::None;
      }
      
      ////////////////////////////////////////////////////////////
      
      // Halo1Anniversary: transparency combine
      if (ps == 0xDCC32775)
      {
         SubGameHandler::Enqueue(Halo1Anniversary, true);
         return DrawOrDispatchOverrideType::None;
      }

      // Halo1Classic: fxaa
      if (!device_data.has_drawn_main_post_processing && ps == 0xB70CC18B)
      {
         device_data.has_drawn_main_post_processing = true;
         SubGameHandler::Enqueue(Halo1Classic);
         if (SubGameHandler::curr == Halo1Classic) BloomHandler::H1C::OnDraw(device_data, native_device, native_device_context, cmd_list_data);
         return DrawOrDispatchOverrideType::None;
      }
      
      // Halo2Anniversary: AO
      static ComPtr<ID3D11ShaderResourceView> h2a_ao_depth;
      static ComPtr<ID3D11ShaderResourceView> h2a_ao_normals;
      if (!device_data.has_drawn_main_post_processing && SubGameHandler::curr == Halo2Anniversary && ShaderDefines::GetBool(ShaderDefines::HALO2_GTAO))
      {
         if (ps == 0x65E212E2)
         {
            // fetch 3
            std::array<ID3D11ShaderResourceView*, 3> tmp;
            native_device_context->PSGetShaderResources(0, tmp.size(), tmp.data()); //get
            h2a_ao_normals.attach(tmp[0]); //normals
            h2a_ao_depth.attach(tmp[2]); //depth
            if (tmp[1]) tmp[1]->Release(); //release unused

            return DrawOrDispatchOverrideType::None; //let downsample continue
         }

         static bool drawn_ao0 = false; // token
         if (ps == 0x5ED3BA5A) // ao0: create Visibility and Edges
         {
            // token give
            drawn_ao0 = true;

            // GTAO override 
            return XeGTAOHandler::H2A::Draw0(device_data, native_device, native_device_context, cmd_list_data, h2a_ao_depth.get(), h2a_ao_normals.get());
         }

         if (ps == 0x4C2C530E && drawn_ao0) // ao1: resolve to AO
         {
            // token consume
            drawn_ao0 = false;

            // GTAO override
            return XeGTAOHandler::H2A::Draw1(device_data, native_device, native_device_context, cmd_list_data, h2a_ao_depth.get(), h2a_ao_normals.get());
         }
      }
      
      // Halo2Anniversary: blit
      if (!device_data.has_drawn_main_post_processing && ps == 0x9275F36F)
      {
         device_data.has_drawn_main_post_processing = true;
         SubGameHandler::Enqueue(Halo2Anniversary);
         return DrawOrDispatchOverrideType::None;
      }

      // Halo2Classic: blit
      if (!device_data.has_drawn_main_post_processing && ps == 0xD39821CB)
      {
         device_data.has_drawn_main_post_processing = true;
         SubGameHandler::Enqueue(Halo2Classic);
         return DrawOrDispatchOverrideType::None;
      }

      // Halo3
      constexpr std::array<uint64_t, 2> halo3_ps_hashes = { 0xEEB815BC, 0x7D41B2E6 }; //TODO: more variants?
      if (std::ranges::contains(halo3_ps_hashes, ps))
      {
         SubGameHandler::Enqueue(Halo3);
         return DrawOrDispatchOverrideType::None;
      }
      // Halo3ODST
      constexpr std::array<uint64_t, 3> halo3odst_ps_hashes = { 0xADADBE3D, 0x2193CAB5, 0x03B68268 }; //TODO: more variants?
      if (std::ranges::contains(halo3odst_ps_hashes, ps))
      {
         SubGameHandler::Enqueue(Halo3ODST);
         return DrawOrDispatchOverrideType::None;
      }
      // Halo3/ODST: fxaa
      if (!device_data.has_drawn_main_post_processing && ps == 0x9EC6DFC8)
      {
         device_data.has_drawn_main_post_processing = true;
         return DrawOrDispatchOverrideType::None;
      }

      // HaloReach: AO
      if (SubGameHandler::curr == HaloReach)
         XeGTAOHandler::HR::OnDraw(device_data, native_device, native_device_context, cmd_list_data, vs, ps);
      // HaloReach: fxaa
      if (!device_data.has_drawn_main_post_processing && ps == 0x0EFB2B17)
      {
         device_data.has_drawn_main_post_processing = true;
         SubGameHandler::Enqueue(HaloReach);
         return DrawOrDispatchOverrideType::None;
      }

      // Halo4: FXAA
      if (!device_data.has_drawn_main_post_processing && ps == 0xCCC24837)
      {
         device_data.has_drawn_main_post_processing = true;
         SubGameHandler::Enqueue(Halo4);
         return DrawOrDispatchOverrideType::None;
      }

      ////////////////////////////////////////////////////////////

      // Pause Screen
      if (allow_pause_screen)
      {
         // ui_blurdown00
         if (ps == 0x5B190892) 
         {
            if (cb_luma_global_settings.GameSettings.UIBlurDown0Count < 2) // not useful > 2.
            {
               cb_luma_global_settings.GameSettings.UIBlurDown0Count++;
               device_data.cb_luma_global_settings_dirty = true; // must update for immediate subsequent pass
            }
            return DrawOrDispatchOverrideType::None;
         }
         
         // In-game Gamma Slider
         if (ps == 0x82BED845 && !allow_gamma_slider) return DrawOrDispatchOverrideType::Skip;
      }
      else [[unlikely]]
      {
         // token givers / guaranteed ui shaders
         static const std::unordered_set<uint64_t> token_givers = { 0x5B190892, 0xC38B68F9, 0x72826F5B, 0x7560E408, 0xF1A79FBF, 0x625AF8B9, 0x2C49003F, 0x335B9229, 0xC38B68F9, 0xCC0C2DF3, 0xB95A4E01, 0xB95A4E01 };
         if (ps == 0x5B190892 /*token_givers.contains(ps)*/)
         {
            allow_pause_screen_skiptoken = true;
            return DrawOrDispatchOverrideType::Skip;
         }
         
         // In-game Gamma Slider
         if (ps == 0x82BED845 && !allow_gamma_slider) return DrawOrDispatchOverrideType::Skip;
         
         // Skip!
         static std::unordered_set<uint64_t> allow_pause_screen_skiphashes = {
            //TODO: add
         };
         auto i = allow_pause_screen_skiphashes.find(ps);
         bool is_seen = i != allow_pause_screen_skiphashes.end();
         if (allow_pause_screen_skiptoken || is_seen)
         {
            if (!is_seen)
            {
               reshade::log::message(reshade::log::level::info, std::format("Skipping Pause Screen Shader: {:08X}", ps).c_str());
               allow_pause_screen_skiphashes.insert(ps);
            }
            return DrawOrDispatchOverrideType::Skip;
         }
      }

      return DrawOrDispatchOverrideType::None;
   }

   void OnPresent(ID3D11Device* native_device, DeviceData& device_data) override
   {
      // GameDeviceDataHaloTMCC* game_device_data = static_cast<GameDeviceDataHaloTMCC*>(device_data.game);

#if HALO_UPGRADE_SAMPLERS
      // mip_lod_bias_offset
      device_data.texture_mip_lod_bias_offset = mip_lod_bias_offset;
#endif

      // SubGameHandler
      SubGameHandler::Deque(device_data);

      // WarmupDirectAndIndirectHandler
      WarmupDirectAndIndirectHandler::OnPresent();

      // reset cb_luma_global_settings.GameSettings.UIBlurDown0Count
      cb_luma_global_settings.GameSettings.UIBlurDown0Count = 0; // will apply start of next frame
      
      // reset device_data.has_drawn_main_post_processing
      device_data.has_drawn_main_post_processing = false;

      // allow_pause_screen_skiptoken
      allow_pause_screen_skiptoken = false;
   }

   void LoadConfigs() override
   {
      // cb
      reshade::get_config_value(nullptr, NAME, "Bloom", cb_luma_global_settings.GameSettings.Bloom);
      reshade::get_config_value(nullptr, NAME, "FilmGrain", cb_luma_global_settings.GameSettings.FilmGrain);
      reshade::get_config_value(nullptr, NAME, "WhiteClip", cb_luma_global_settings.GameSettings.WhiteClip);
      reshade::get_config_value(nullptr, NAME, "AmbientOcclusion", cb_luma_global_settings.GameSettings.AmbientOcclusion);
      reshade::get_config_value(nullptr, NAME, "MotionBlur", cb_luma_global_settings.GameSettings.MotionBlur);

#if HALO_UPGRADE_SAMPLERS
      // mip_lod_bias_offset
      reshade::get_config_value(nullptr, NAME, "mip_lod_bias_offset", mip_lod_bias_offset);
      SetMipLodBiasOffset(mip_lod_bias_offset, false);
#endif
      
      // allow_gamma_slider
      reshade::get_config_value(nullptr, NAME, "allow_gamma_slider", allow_gamma_slider);

      // custom_sdr_gamma
      reshade::get_config_value(nullptr, NAME, "custom_sdr_gamma", custom_sdr_gamma);
      ShaderDefines::Set(GAMMA_CORRECTION_TYPE_HASH, custom_sdr_gamma > 0);
      defines_need_recompilation = true;

      // Reset ALLOW_COLORGRADE
      ShaderDefines::Set(ShaderDefines::ALLOW_COLORGRADE, true);

      // SubGameUserSettingsHandler
      SubGameUserSettingsHandler::OnLoadConfigs();

      // XeGTAOHandler & BloomHandler
      XeGTAOHandler::OnLoadConfigs();
      BloomHandler::OnLoadConfigs();

      //try force 400 nits
      if (!reshade::get_config_value(nullptr, NAME, "ScenePeakWhite", cb_luma_global_settings.ScenePeakWhite)) cb_luma_global_settings.ScenePeakWhite = 400.f;

      // seen_readme
      reshade::get_config_value(nullptr, NAME, "seen_readme", seen_readme);
   }
   
   void PrintImGuiAbout() override
   {
      ImGui::Text("Build Date:");
      ImGui::Text(__DATE__);
      ImGui::Text(__TIME__);
      ImGui::NewLine();

      ImGui::Text("wort wort wort");
   }

   void DrawImGuiSettings(DeviceData& device_data) override
   {
      auto DrawColoredSubHeader = [](const char* label, const ImVec4& color = ImColor(128, 255, 255, 255))
      {
         ImGui::PushStyleColor(ImGuiCol_Text, color);
         ImGui::Text("[%s]", label);
         ImGui::PopStyleColor();
      };
      
      // GameDeviceDataHaloTMCC* game_device_data = static_cast<GameDeviceDataHaloTMCC*>(device_data.game);

      // SWAPCHAIN_TEST_PEAK
      ShaderDefines::UIToggleCheckmark(ShaderDefines::SWAPCHAIN_TEST_PEAK, std::format("Display Test Peak (+{:.2f} HDR Stops)", std::log2f(cb_luma_global_settings.ScenePeakWhite / cb_luma_global_settings.ScenePaperWhite)).c_str(),
         "3 rectangles within a bigger one.\n- Left: Invisible (2x Peak)\n- Middle: Barely Visible (1x Peak)\n- Right: Easily Visible (0.5x Peak).\nSo whatever you do, don't let Middle disappear/clip!");

      ImGui::Separator();

      if (!seen_readme)
      {
         ImGui::PushID("###README");
         
         DrawColoredSubHeader("README: About HDR Stops");
         ImGui::PushStyleColor(ImGuiCol_Text, GetPulsingColor(ImColor(128, 255, 128, 255), 0.05));
         ImGui::Bullet(); ImGui::SameLine(); ImGui::TextWrapped("This mod is designed for +1 HDR Stops. (e.g. 400 Peak / 200 Paper, 600 Peak / 300 Paper, etc.).");
         ImGui::PopStyleColor();
         ImGui::Bullet(); ImGui::SameLine(); ImGui::TextWrapped("For Halo 3/ODST & Reach: White clip is faithful to original, and sparse extreme highlights are prevented from ruining luminance composition.");
         ImGui::Bullet(); ImGui::SameLine(); ImGui::TextWrapped("For Halo 2 Anniversary: Sunny outdoors are designed to be compressed to blow out, becoming awkwardly forced/blinding otherwise.");
         ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 1.f, 0.5f, 1.f));
         ImGui::Bullet(); ImGui::SameLine(); ImGui::TextWrapped("TLDR, don't validate: \"Ewww! The pipeline is designed for SDR, and HDR is just a trash blinding contrast filter!\"");
         ImGui::PopStyleColor();

         if (ImGui::Button("Dismiss"))
         {
            seen_readme = true;
            reshade::set_config_value(nullptr, NAME, "seen_readme", seen_readme);
         }

         ImGui::Separator();
         ImGui::PopID();
      }
      
      // Anti-Aliasing
      static const bool seen_readme_init = seen_readme;
      if (ImGui::CollapsingHeader("Anti-Aliasing", !seen_readme_init ? ImGuiTreeNodeFlags_DefaultOpen : ImGuiTreeNodeFlags_None))
      {
         DrawColoredSubHeader("Turn it on!");
         ImGui::PushStyleColor(ImGuiCol_Text, GetPulsingColor(ImColor(255, 255, 128, 255), 0.05));
         ImGui::Bullet(); ImGui::SameLine(); ImGui::TextWrapped("Please turn on in-game Anti-Aliasing! Then use toggle below.");
         ImGui::PopStyleColor();
         ShaderDefines::UIToggleCheckmark(ShaderDefines::ALLOW_AA, "Allow Anti-Aliasing (FXAA)", "Disable will skip original FXAA code (while letting the original shader draw, used to detect Sub Games and do UI Paper White scaling).");
      }

      // Gamma
      if (ImGui::CollapsingHeader("Gamma"))
      {
         DrawColoredSubHeader("In-Game Gamma Sliders");
         ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.5f, 0.5f, 1.f));
         ImGui::Bullet(); ImGui::SameLine(); ImGui::TextWrapped("If allowed, any value besides 6.0 will shift Peak (just like SDR)!");
         ImGui::PopStyleColor();

         if (ImGui::Checkbox("Allow In-Game Gamma Sliders", &allow_gamma_slider))
            reshade::set_config_value(nullptr, NAME, "allow_gamma_slider", allow_gamma_slider);
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Leaving this disabled will let main color be unaltered sRGB.");
         DrawResetButton(allow_gamma_slider, false, "allow_gamma_slider", nullptr);

         ImGui::NewLine();
         
         DrawColoredSubHeader("Gamma Correct / SDR EOTF Emulation");
         ImGui::Bullet(); ImGui::SameLine(); ImGui::TextWrapped("Reintroduce SDR gamma mismatch to lower shadows, probably matching original intent.");

         // custom_sdr_gamma
         const char* const items[] = { "Off (sRGB)", "Match SDR (2.2)", "Stronger (2.4)" };
         int custom_sdr_gamma_index = 0;
         if (custom_sdr_gamma > 0) { //detect
            if (custom_sdr_gamma == 2.2f) custom_sdr_gamma_index = 1;
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
            reshade::set_config_value(nullptr, NAME, "custom_sdr_gamma", custom_sdr_gamma);
         }
         ImGui::PopID();

         ImGui::NewLine();

         // XBOX360_CURVE
         DrawColoredSubHeader("Xbox 360 Gamma Curve");
         ImGui::Bullet(); ImGui::SameLine(); ImGui::TextWrapped("Emulate the Xbox 360's more aggressive gamma curve.");
         ShaderDefines::UIDropDown(ShaderDefines::XBOX360_CURVE, "Perceptual Emulation", { "Off", "If Applicable" }, "This is emulation as the actual curve is a wonky jagged piecewise curve for performance reasons.\nThis is also perceptual, keeping dark colors from being overly saturated & hue shifted).");
      }
      ShaderDefines::Set(GAMMA_CORRECTION_TYPE_HASH, custom_sdr_gamma > 0); // Forced

#if HALO_UPGRADE_SAMPLERS
      // Upgraded Samplers
      if (ImGui::CollapsingHeader("Upgraded Samplers"))
      {
         DrawColoredSubHeader("Increase Texture Sharpness at a Distance");
         
         if (ImGui::SliderFloat("Mip LOD Bias Offset", &mip_lod_bias_offset, 0.f, -10.f, "%.2f"))
            SetMipLodBiasOffset(mip_lod_bias_offset, true);
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Negative offset bias sharpens.\nMight make the game too coarse");
         DrawResetButton(mip_lod_bias_offset, 0.f, "mip_lod_bias_offset", nullptr);
      }
#endif
      
      // Miscellaneous
      if (ImGui::CollapsingHeader("Miscellaneous"))
      {
         DrawColoredSubHeader("Bloom");

         //Bloom
         auto GetMaxBloomBySubGame = [](SubGame state) -> float
         {
            switch (state)
            {
               case Halo2Classic: return 1.f;
               default: return 2.f;
            }
         };
         if (ImGui::SliderFloat("Bloom", &cb_luma_global_settings.GameSettings.Bloom, 0.f, GetMaxBloomBySubGame(SubGameHandler::curr), "%.2f"))
            reshade::set_config_value(nullptr, NAME, "Bloom", cb_luma_global_settings.GameSettings.Bloom);
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Multiplier on Bloom strength when applicable.");
         DrawResetButton(cb_luma_global_settings.GameSettings.Bloom, 1.f, "Bloom", nullptr);

         ShaderDefines::UIDropDown(ShaderDefines::HALO1_BLOOM, "Halo 1 Classic: Bloom", { "Off", "On", "Bloom Only (Debug)" }, "Add a subtle bloom pass for Halo 1 Classic to aid the original sprites trying to convey the effect.");

         if (ImGui::SliderFloat("Halo 1 Classic: Bloom Sigma", &BloomHandler::H1C::sigma, 0.5f, 3.f, "%.2f"))
            reshade::set_config_value(nullptr, NAME, BloomHandler::H1C::sigma_reshadesave, BloomHandler::H1C::sigma);
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Higher means more spread.");
         DrawResetButton(BloomHandler::H1C::sigma, BloomHandler::H1C::sigma_default, BloomHandler::H1C::sigma_reshadesave, nullptr);
         
         ShaderDefines::UIDropDown(ShaderDefines::HALO3_BLOOM, "Halo 3: Bloom Mode", { "Saturation Preserved", "Blown Out (Vanilla)" }, "How should bloom be processed in Halo 3?\nSince bloom bathes the screen, this can the change the hues of the whole image.");

         ImGui::NewLine(); //////////////
         
         DrawColoredSubHeader("Ambient Occlusion");

         // AmbientOcclusion
         if (ImGui::SliderFloat("Ambient Occlusion", &cb_luma_global_settings.GameSettings.AmbientOcclusion, 0.f, 2.f, "%.2f"))
            reshade::set_config_value(nullptr, NAME, "AmbientOcclusion", cb_luma_global_settings.GameSettings.AmbientOcclusion);
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Multiplier on AO strength when applicable.");
         DrawResetButton(cb_luma_global_settings.GameSettings.AmbientOcclusion, 1.f, "AmbientOcclusion", nullptr);

         // HALO2_AO
         ShaderDefines::UIDropDown(ShaderDefines::HALO2_AO, "Halo 2 Anniversary: Quality", { "Easy (Vanilla if SSAO)", "Normal", "Heroic", "Legendary", "LASO" }, "The quality of Halo 2 Ambient Occlusion for either SSAO or GTAO.");

         // HALO2_GTAO
         auto gtao_enabled = ShaderDefines::UIToggleCheckmark(ShaderDefines::HALO2_GTAO, "Halo 2 Anniversary: GTAO", "Replaces Halo 2 Anniversary's SSAO for modern GTAO.\nThough costing more performance, it's highly recommended as AO on the viewmodel is also fixed.");
         if (!gtao_enabled.first) ImGui::BeginDisabled();
         {
            // HALO2_GTAO_NOISE
            ShaderDefines::UIToggleCheckmark(ShaderDefines::HALO2_GTAO_NOISE, "Halo 2 Anniversary: GTAO Dynamic Noise", "Let noise jitter randomly to mask static pattern, hopefully unnoticeable at high FPS.");

            // HALO2_GTAO_FULLRES
            auto gtao_fullres_enabled = ShaderDefines::UIToggleCheckmark(ShaderDefines::HALO2_GTAO_FULLRES, "Halo 2 Anniversary: GTAO Full Res", "Instead of original 0.5x, run GTAO at 1x resolution for more precise edge detection.");
            if (gtao_fullres_enabled.second) XeGTAOHandler::H2A::Reset();

            // Denoise count
            if (ImGui::SliderInt("Halo 2 Anniversary: GTAO Denoise", &XeGTAOHandler::H2A::denoise, 0, 4, "%d", ImGuiSliderFlags_AlwaysClamp))
               reshade::set_config_value(nullptr, NAME, XeGTAOHandler::H2A::denoise_reshadesave, XeGTAOHandler::H2A::denoise);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
               ImGui::SetTooltip("The number of denoise passes to apply to GTAO.\nMore passes = smoother noise at a performance cost.");
            DrawResetButton(XeGTAOHandler::H2A::denoise, XeGTAOHandler::H2A::denoise_default, XeGTAOHandler::H2A::denoise_reshadesave, nullptr);
         }
         if (!gtao_enabled.first) ImGui::EndDisabled();

         // Halo Reach GTAO status
         ShaderDefines::UIDropDown(ShaderDefines::HALOR_AO, "Halo Reach: Mode", { "Vanilla (Broken) SSAO", "GTAO Easy", "GTAO Normal", "GTAO Heroic", "GTAO Legendary", "GTAO LASO" }, "The quality of Halo Reach's Ambient Occlusion.\nThough costing more performance, GTAO is highly recommended since the original was never scaled up >720p resolutions.");
         if (DEVELOPMENT)
         {
            ImGui::NewLine();
            DrawColoredSubHeader("Halo Reach GTAO Status (DEVELOPMENT)");
            ImGui::Bullet(); ImGui::SameLine(); ImGui::TextWrapped("CB %s", XeGTAOHandler::HR::FoundResources::viewvs_cb ? "Ready" : "Not Ready");
            ImGui::Bullet(); ImGui::SameLine(); ImGui::TextWrapped("SRV %s", XeGTAOHandler::HR::FoundResources::worldnormals_srv ? "Ready" : "Not Ready");
         }
         
         ImGui::NewLine(); //////////////

         DrawColoredSubHeader("Miscellaneous");

         // WhiteClip
         if (SubGameHandler::curr == Halo4) ImGui::BeginDisabled();
         if (ImGui::SliderFloat("White Clip", &cb_luma_global_settings.GameSettings.WhiteClip, 0.f, 2.f, "%.2f"))
            reshade::set_config_value(nullptr, NAME, "WhiteClip", cb_luma_global_settings.GameSettings.WhiteClip);
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Increase to straighten tonemap rolloff,\nmaking highlights more aggressive/clipped.\n(Not available for Halo 4.)");
         DrawResetButton(cb_luma_global_settings.GameSettings.WhiteClip, 1.f, "WhiteClip", nullptr);
         if (SubGameHandler::curr == Halo4) ImGui::EndDisabled();

         // FilmGrain
         if (ImGui::SliderFloat("Film Grain", &cb_luma_global_settings.GameSettings.FilmGrain, 0.f, 1.f, "%.2f"))
            reshade::set_config_value(nullptr, NAME, "FilmGrain", cb_luma_global_settings.GameSettings.FilmGrain);
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Multiplier on Film Grain strength when applicable.");
         DrawResetButton(cb_luma_global_settings.GameSettings.FilmGrain, 1.f, "FilmGrain", nullptr);

         // HALOR_FILMGRAIN_SCALE
         ShaderDefines::UIDropDown(ShaderDefines::HALOR_FILMGRAIN_SCALE, "Halo Reach: Film Grain Scale", { "Native (Vanilla)", "720p (Xbox 360)", "1080p", "1440p" }, "Scales Halo Reach's film grain size by target resolution.\nThe lower the target, the bigger the grain.");

         // MotionBlur
         if (ImGui::SliderFloat("Motion Blur", &cb_luma_global_settings.GameSettings.MotionBlur, 0.f, 1.f, "%.2f"))
            reshade::set_config_value(nullptr, NAME, "MotionBlur", cb_luma_global_settings.GameSettings.MotionBlur);
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Multiplier on Motion Blur strength when applicable.");
         DrawResetButton(cb_luma_global_settings.GameSettings.MotionBlur, 1.f, "MotionBlur", nullptr);
         
         // ALLOW_COLORGRADE
         ShaderDefines::UIToggleCheckmark(ShaderDefines::ALLOW_COLORGRADE, "Color Grading (Debug)", "Disable to skip color grading,\nexposing the raw HDR input.");

         // UIToggle
         ImGui::Checkbox("Pause Screen (Read Tooltip)", &allow_pause_screen);
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Useful for screenshots.\nThis seems to only work for Settings screen?");
         DrawResetButton(allow_pause_screen, true, nullptr, nullptr);
      }
      
      // SubGame
      if (ImGui::CollapsingHeader("Sub Game"))
      {
         DrawColoredSubHeader("Detects which Halo is actually running.");

         ImGui::Bullet(); ImGui::SameLine(); ImGui::TextWrapped("Current: %s", SubGameToString(SubGameHandler::curr));

         auto compat = SubGameUserSettingsHandler::GetSettings(SubGameHandler::curr)->GetCompatibility();
         ImGui::PushStyleColor(ImGuiCol_Text, compat.second);
         ImGui::Bullet(); ImGui::SameLine(); ImGui::TextWrapped("HDR Compatibility: %s", compat.first);
         ImGui::PopStyleColor();

         //drop down list to override
         static std::vector<const char*> subgame_items = {
            "No Override",
            SubGameToString(Halo1Classic),
            SubGameToString(Halo1Anniversary),
            SubGameToString(Halo2Classic),
            SubGameToString(Halo2Anniversary),
            SubGameToString(Halo2AnniversaryMP),
            SubGameToString(Halo3),
            SubGameToString(Halo3ODST),
            SubGameToString(HaloReach),
            SubGameToString(Halo4),
         };
         int subgame_index = SubGameHandler::over;
         if (ImGui::Combo("Override Sub Game", &subgame_index, subgame_items.data(), static_cast<int>(subgame_items.size())))
         {
            SubGameHandler::over = static_cast<SubGame>(subgame_index);
            SubGameHandler::SetSubGame(static_cast<SubGame>(subgame_index));
         }

         // best_resource_unorm_disallow
         if (DEVELOPMENT)
         {
            ImGui::Checkbox("(DEVELOPMENT) best_resource_unorm_disallow", &SubGameHandler::best_resource_unorm_disallow);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
               ImGui::SetTooltip("Disable GetBestResourceUpgradeFormat() UNORM for all Sub Games.");
         }

         ImGui::NewLine();
         
         // SubGameUserSettingsHandler
         DrawColoredSubHeader("Settings Override On Change");
         SubGameUserSettingsHandler::OnImGui(SubGameHandler::curr);
      }

      ImGui::Separator();

      // Reset button
      if (ImGui::Button("Panic Reset")) SubGameHandler::Reinit();
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("If anything is wonky (black screen, AO not same res, SDR clamping) click this!\n(Full reset Sub Game detection, clearing all HDR upgraded resources.)");

      // Unhide README
      if (seen_readme)
      {
         ImGui::SameLine();
         if (ImGui::Button("Show README"))
         {
            seen_readme = false;
            reshade::set_config_value(nullptr, NAME, "seen_readme", seen_readme);
         }
      }

      if (DEVELOPMENT) ImGui::Separator();
   }
};

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
   switch (ul_reason_for_call)
   {
      case DLL_PROCESS_ATTACH:
         Globals::SetGlobals(PROJECT_NAME, "Halo: The Master Chief Collection - Luma mod"); 
         Globals::VERSION = 1;

         // swapchain
         swapchain_format_upgrade_type  = TextureFormatUpgradesType::AllowedEnabled;
         swapchain_upgrade_type = SwapchainUpgradeType::scRGB;

         // swapchain compatibility/safety
         prevent_fullscreen_state = false;
         force_borderless = false;

         // resources
         texture_format_upgrades_type = TextureFormatUpgradesType::AllowedEnabled; //see SubGameHandler::SetSubGame()

#if HALO_UPGRADE_SAMPLERS
         // samplers
         enable_samplers_upgrade = true;
#endif
      
         // // gamma ramp
         // allow_disabling_gamma_ramp = true;

         // // ui separation
         // enable_ui_separation = true;
         // ui_separation_format = DXGI_FORMAT_R16G16B16A16_FLOAT;

         // CREATE!
         game = new GameHaloTMCC();
         break;
      case DLL_PROCESS_DETACH:
         break;
      default:
         break;
   }

   CoreMain(hModule, ul_reason_for_call, lpReserved);

   return TRUE;
}