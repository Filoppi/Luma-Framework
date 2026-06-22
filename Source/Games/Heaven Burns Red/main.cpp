#define GAME_HEAVEN_BURNS_RED 1

#define DISABLE_AUTO_DEBUGGER 1
#define DISABLE_FOCUS_LOSS_SUPPRESSION 1
#define AVOID_INPUT_LOSS 1
#define CHECK_GRAPHICS_API_COMPATIBILITY 1
#define DISABLE_SWAPCHAIN_FLIP_MODEL 1
#define ENABLE_POST_DRAW_DISPATCH_CALLBACK 1

#include "..\..\Core\core.hpp"
#include "..\..\Core\includes\shader_patching.h"

namespace
{
   static const int msaa_values[] = { 2, 4, 8 };
   int msaa_index = 0;
   int enable_alpha_to_coverage = 0;
}

struct SHEXHeader
{
   char chunk_name[4]; // 'SHEX'
   uint32_t chunk_size;
   uint8_t version;
   uint16_t type;
   uint32_t dword_count;
};

bool PatchPixelShader(std::vector<std::byte>& shader_code)
{
   DXBCHeader* dxbc_header = (DXBCHeader*)&shader_code[0];
   
   bool is_alpha_tested = false;

   for (uint32_t i = 0; i < dxbc_header->chunk_count; ++i)
   {
      if (strncmp((const char*)&shader_code[dxbc_header->chunk_offsets[i]], "SHEX", 4) == 0)
      {
         std::byte* shex = &shader_code[dxbc_header->chunk_offsets[i]];
         SHEXHeader* shex_header = (SHEXHeader*)shex;
         
         uint32_t pos = 16;
         
         D3D10_SB_OPCODE_TYPE prev_opcode_type = D3D10_SB_NUM_OPCODES;
         uint32_t prev_opcode_len = 0;
         
         const uint32_t nop_opcode = ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1) | D3D10_SB_OPCODE_NOP;
         
         uint32_t temp_register[3];
         uint32_t insert_pos = 0;
         
         for (;;)
         {
            D3D10_SB_OPCODE_TYPE opcode_type = DECODE_D3D10_SB_OPCODE_TYPE(*(uint32_t*)(shex + pos));
            uint32_t len;
            if (opcode_type != D3D10_SB_OPCODE_CUSTOMDATA)
            {
               len = DECODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(*(uint32_t*)(shex + pos));
            }
            else
            {
               len = *(uint32_t*)(shex + pos + 4);
            }
            
            // discard_nz XX
            if (opcode_type == D3D10_SB_OPCODE_DISCARD && prev_opcode_type == D3D10_SB_OPCODE_LT)
            {
               D3D10_SB_OPCODE_TYPE next_opcode_type = DECODE_D3D10_SB_OPCODE_TYPE(*(uint32_t*)(shex + pos + len * 4));
               // mov
               if (next_opcode_type == D3D10_SB_OPCODE_MOV)
               {
                  uint32_t opcode_pos = pos + len * 4;
                  D3D10_SB_OPERAND_TYPE operand = DECODE_D3D10_SB_OPERAND_TYPE(*(uint32_t*)(shex + opcode_pos + 4));
                  D3D10_SB_OPERAND_NUM_COMPONENTS num_components = DECODE_D3D10_SB_OPERAND_NUM_COMPONENTS(*(uint32_t*)(shex + opcode_pos + 4));
                  uint32_t slot = *(uint32_t*)(shex + opcode_pos + 8);
                  // o0.xyzw
                  if (operand == D3D10_SB_OPERAND_TYPE_OUTPUT && num_components == D3D10_SB_OPERAND_4_COMPONENT && slot == 0)
                  {
                     D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE selection_mode = DECODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(*(uint32_t*)(shex + opcode_pos + 12));
                     D3D10_SB_OPERAND_TYPE next_operand = DECODE_D3D10_SB_OPERAND_TYPE(*(uint32_t*)(shex + opcode_pos + 12));
                     // rX.xyzw
                     if (next_operand == D3D10_SB_OPERAND_TYPE_TEMP && selection_mode == D3D10_SB_OPERAND_4_COMPONENT_SWIZZLE_MODE)
                     {
                        temp_register[0] = *(uint32_t*)(shex + opcode_pos + 16);
                        temp_register[1] = *(uint32_t*)(shex + pos - prev_opcode_len * 4 + 12);
                        temp_register[2] = *(uint32_t*)(shex + pos - prev_opcode_len * 4 + 16);
                        uint32_t next_len = DECODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(*(uint32_t*)(shex + opcode_pos));
                        insert_pos = opcode_pos + next_len * 4;
                        
                        for (uint32_t x = 0; x < len; x++)
                        {
                           *(uint32_t*)(shex + pos + x * 4) = nop_opcode;
                        }
                        
                        for (uint32_t x = 0; x < prev_opcode_len; x++)
                        {
                           *(uint32_t*)(shex + pos - prev_opcode_len * 4 + x * 4) = nop_opcode;
                        }
                        
                        is_alpha_tested = true;
                        break;
                     }
                  }
               }
            }
            else if (opcode_type == D3D10_SB_OPCODE_MOV)
            {
               D3D10_SB_OPERAND_TYPE operand = DECODE_D3D10_SB_OPERAND_TYPE(*(uint32_t*)(shex + pos + 4));
               D3D10_SB_OPERAND_NUM_COMPONENTS num_components = DECODE_D3D10_SB_OPERAND_NUM_COMPONENTS(*(uint32_t*)(shex + pos + 4));
               uint32_t slot = *(uint32_t*)(shex + pos + 8);
               // o0.xyzw
               if (operand == D3D10_SB_OPERAND_TYPE_OUTPUT && num_components == D3D10_SB_OPERAND_4_COMPONENT && slot == 0)
               {
                  // this op + ret
                  if (pos + len * 4 + 4 >= shex_header->chunk_size + 8)
                  {
                     break;
                  }
                  uint32_t next_opcode_pos = pos + len * 4;
                  D3D10_SB_OPCODE_TYPE next_opcode_type = DECODE_D3D10_SB_OPCODE_TYPE(*(uint32_t*)(shex + next_opcode_pos));
                  uint32_t next_len = DECODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(*(uint32_t*)(shex + next_opcode_pos));
                  
                  uint32_t next_next_opcode_pos = next_opcode_pos + next_len * 4;
                  D3D10_SB_OPCODE_TYPE next_next_opcode_type = DECODE_D3D10_SB_OPCODE_TYPE(*(uint32_t*)(shex + next_next_opcode_pos));
                  uint32_t next_next_len = DECODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(*(uint32_t*)(shex + next_next_opcode_pos));
                  
                  if (next_opcode_type == D3D10_SB_OPCODE_LT && next_next_opcode_type == D3D10_SB_OPCODE_DISCARD)
                  {
                     temp_register[0] = *(uint32_t*)(shex + pos + 16);
                     temp_register[1] = *(uint32_t*)(shex + next_opcode_pos + 12);
                     temp_register[2] = *(uint32_t*)(shex + next_opcode_pos + 16);
                     insert_pos = next_next_opcode_pos;
                     
                     for (uint32_t x = 0; x < next_next_len; x++)
                     {
                        *(uint32_t*)(shex + next_next_opcode_pos + x * 4) = nop_opcode;
                     }
                     
                     for (uint32_t x = 0; x < next_len; x++)
                     {
                        *(uint32_t*)(shex + next_opcode_pos + x * 4) = nop_opcode;
                     }
                     
                     is_alpha_tested = true;
                     break;
                  }
               }
            }
            
            if (pos + len * 4 >= shex_header->chunk_size + 8)
            {
               break;
            }
            
            prev_opcode_type = opcode_type;
            prev_opcode_len = len;
            pos += len * 4;
         }
         
         if (is_alpha_tested)
         {
            // From: Anti-aliased Alpha Test: The Esoteric Alpha To Coverage by Ben Golus
            std::vector<uint32_t> shader_patch = {
               0x0500007A, 0x00100012, temp_register[0], 0x0010003A, temp_register[0], // deriv_rtx_coarse r0.x, r0.w
               0x0500007C, 0x00100022, temp_register[0], 0x0010003A, temp_register[0], // deriv_rty_coarse r0.y, r0.w
               0x09000000, 0x00100012, temp_register[0], 0x8010001A, 0x00000081, temp_register[0], 0x8010000A, 0x00000081, temp_register[0], // add r0.x, |r0.y|, |r0.x|
               0x07000034, 0x00100012, temp_register[0], 0x0010000A, temp_register[0], 0x00004001, 0x38D1B717, // max r0.x, r0.x, l(0.000100)
               0x0700000E, 0x00100012, temp_register[0], temp_register[1], temp_register[2], 0x0010000A, temp_register[0], // div r0.x, (alpha - alpha_ref), r0.x
               0x07000000, 0x00102082, 0x00000000, 0x0010000A, temp_register[0], 0x00004001, 0x3F000000, // add o0.w, r0.x, l(0.500000)
            };
               
            shader_code.insert(shader_code.begin() + dxbc_header->chunk_offsets[i] + insert_pos, (std::byte*)&shader_patch[0], (std::byte*)(&shader_patch[0] + shader_patch.size()));
            shex = &shader_code[dxbc_header->chunk_offsets[i]];
            shex_header = (SHEXHeader*)shex;
            shex_header->chunk_size += shader_patch.size() * sizeof(uint32_t);
            shex_header->dword_count += shader_patch.size();
            dxbc_header = (DXBCHeader*)&shader_code[0];
            for (uint32_t j = i + 1; j < dxbc_header->chunk_count; ++j)
            {
               dxbc_header->chunk_offsets[j] += shader_patch.size() * sizeof(uint32_t);
            }
         }
      }
   }
   if (is_alpha_tested)
   {
      dxbc_header->file_size = shader_code.size();
      Hash::MD5::Digest md5_digest = CalcDXBCHash(shader_code.data(), shader_code.size());
      std::memcpy(&dxbc_header->hash, &md5_digest.data, DXBCHeader::hash_size);
   }
   
   return is_alpha_tested;
}

struct GameDeviceHeavenBurnsRed final : public GameDeviceData
{
   std::unordered_map<ID3D11BlendState*, ComPtr<ID3D11BlendState>> alpha_blend_states;
   std::unordered_map<uint32_t, com_ptr<ID3D11PixelShader>> modified_pixel_shaders;
   bool is_current_rtv_ms = false;
   bool blend_state_changed = false;
};

class HeavenBurnsRed final : public Game
{
   static GameDeviceHeavenBurnsRed& GetGameDeviceData(DeviceData& device_data)
   {
      return *static_cast<GameDeviceHeavenBurnsRed*>(device_data.game);
   }

   static const GameDeviceHeavenBurnsRed& GetGameDeviceData(const DeviceData& device_data)
   {
      return *static_cast<const GameDeviceHeavenBurnsRed*>(device_data.game);
   }
   
public:
   void OnInit(bool async) override
   {
      reshade::register_event<reshade::addon_event::create_resource>(HeavenBurnsRed::OnCreateResource);
      reshade::register_event<reshade::addon_event::create_pipeline>(HeavenBurnsRed::OnCreatePipeline);
      //reshade::register_event<reshade::addon_event::bind_pipeline>(HeavenBurnsRed::OnBindPipeline);
      reshade::register_event<reshade::addon_event::bind_render_targets_and_depth_stencil>(HeavenBurnsRed::OnBindRenderTargetsAndDepthStencil);
   }
   
   void OnLoad(std::filesystem::path& file_path, bool failed) override
   {
      if (!failed)
      {
         LoadConfigs();
      }
   }
   
   void OnCreateDevice(ID3D11Device* native_device, DeviceData& device_data) override
   {
      device_data.game = new GameDeviceHeavenBurnsRed;
   }
   
   static bool OnCreateResource(
      reshade::api::device* device,
      reshade::api::resource_desc& desc,
      reshade::api::subresource_data* initial_data,
      reshade::api::resource_usage initial_state)
   {
      if (desc.type != reshade::api::resource_type::texture_2d)
         return false;
      
      if (desc.texture.samples != 2)
         return false;
      
      if (desc.texture.width == desc.texture.height)
         return false;
      
      auto& device_data = *device->get_private_data<DeviceData>();
      
      if (desc.texture.width != device_data.output_resolution.x || desc.texture.height != device_data.output_resolution.y)
         return false;
      
      desc.texture.samples = msaa_values[msaa_index];
      return true;
   }
   
   static bool OnCreatePipeline(
      reshade::api::device* device,
      reshade::api::pipeline_layout layout,
      uint32_t subobject_count,
      const reshade::api::pipeline_subobject* subobjects)
   {
      auto& device_data = *device->get_private_data<DeviceData>();
      auto& game_device_data = GetGameDeviceData(device_data);
      for (uint32_t i = 0; i < subobject_count; ++i)
      {
         const auto& subobject = subobjects[i];
         for (uint32_t j = 0; j < subobject.count; ++j)
         {
            if (subobject.type == reshade::api::pipeline_subobject_type::pixel_shader)
            {
               const auto* original_shader_desc = static_cast<reshade::api::shader_desc*>(subobjects[i].data);
               std::vector<std::byte> shader_code((const std::byte*)original_shader_desc->code, ((const std::byte*)original_shader_desc->code) + original_shader_desc->code_size);

               uint32_t hash = Shader::BinToHash((const uint8_t*)original_shader_desc->code, original_shader_desc->code_size);

               bool is_alpha_tested_shader = PatchPixelShader(shader_code);
               
               if (!is_alpha_tested_shader)
                  return false;

               ID3D11Device* native_device = (ID3D11Device*)(device->get_native());
               com_ptr<ID3D11PixelShader> patched_shader;
               native_device->CreatePixelShader(shader_code.data(), shader_code.size(), nullptr, &patched_shader);

               game_device_data.modified_pixel_shaders[hash] = patched_shader;
            }
         }
      }
      return false;
   }
   
   static void OnBindPipeline(
      reshade::api::command_list *cmd_list,
      reshade::api::pipeline_stage stages,
      reshade::api::pipeline pipeline)
   {
      // only accept OM calls
      if ((stages & reshade::api::pipeline_stage::output_merger) == 0)
         return;

      auto& device_data = *cmd_list->get_device()->get_private_data<DeviceData>();
      auto& game_device_data = GetGameDeviceData(device_data);
      
      game_device_data.blend_state_changed = true;
   }
   
   static void OnBindRenderTargetsAndDepthStencil(
      reshade::api::command_list* cmd_list,
      uint32_t count,
      const reshade::api::resource_view *rtvs,
      reshade::api::resource_view dsv)
   {
      if (count != 1)
         return;
      
      if (rtvs[0].handle == 0)
         return;
      
      reshade::api::resource resource = cmd_list->get_device()->get_resource_from_view(rtvs[0]);
      reshade::api::resource_desc desc = cmd_list->get_device()->get_resource_desc(resource);
      
      auto& device_data = *cmd_list->get_device()->get_private_data<DeviceData>();
      auto& game_device_data = GetGameDeviceData(device_data);

      game_device_data.is_current_rtv_ms = desc.texture.samples > 1;
   }
   
   DrawOrDispatchOverrideType OnDrawOrDispatch(
      ID3D11Device* native_device,
      ID3D11DeviceContext* native_device_context,
      CommandListData& cmd_list_data, DeviceData& device_data,
      reshade::api::shader_stage stages,
      const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes,
      bool is_custom_pass, bool& updated_cbuffers,
      std::function<void()>* original_draw_dispatch_func) override
   {
      if (enable_alpha_to_coverage == 0)
         return DrawOrDispatchOverrideType::None;
      
      if ((stages & reshade::api::shader_stage::pixel) == 0)
         return DrawOrDispatchOverrideType::None;

      auto& game_device_data = GetGameDeviceData(device_data);
      
      auto shader_it = game_device_data.modified_pixel_shaders.find(original_shader_hashes.pixel_shaders[0]);
      
      if (shader_it != game_device_data.modified_pixel_shaders.end())
      {
         if (game_device_data.is_current_rtv_ms)
         {
            ComPtr<ID3D11BlendState> blend_state;
            FLOAT blend_factor[4];
            UINT sample_mask;

            native_device_context->OMGetBlendState(blend_state.put(), blend_factor, &sample_mask);
            const auto blend_state_replacement = game_device_data.alpha_blend_states.find(blend_state.get());
            if (blend_state_replacement != game_device_data.alpha_blend_states.end())
            {
               native_device_context->OMSetBlendState(blend_state_replacement->second.get(), blend_factor, sample_mask);
               //reshade::log::message(reshade::log::level::info, "Blend State: Replaced.");
            }
            else
            {
               D3D11_BLEND_DESC desc;
               blend_state->GetDesc(&desc);
               desc.AlphaToCoverageEnable = true;
               ComPtr<ID3D11BlendState> new_blend_state;
               native_device->CreateBlendState(&desc, new_blend_state.put());
               game_device_data.alpha_blend_states[blend_state.get()] = new_blend_state;
               native_device_context->OMSetBlendState(new_blend_state.get(), blend_factor, sample_mask);
            }
            
            native_device_context->PSSetShader(shader_it->second.get(), nullptr, 0);
            (*original_draw_dispatch_func)();
            native_device_context->OMSetBlendState(blend_state.get(), blend_factor, sample_mask);
            
            return DrawOrDispatchOverrideType::Replaced;
         }
      }
      
      return DrawOrDispatchOverrideType::None;
   }
   
   void OnPresent(
      ID3D11Device* native_device,
      DeviceData& device_data) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);
      
      game_device_data.is_current_rtv_ms = false;
      game_device_data.blend_state_changed = false;
   }
   
   void LoadConfigs() override
   {
      reshade::api::effect_runtime* runtime = nullptr;
      reshade::get_config_value(runtime, NAME, "MSAA", msaa_index);
      reshade::get_config_value(runtime, NAME, "AlphaToCoverage", enable_alpha_to_coverage);
   }
   
   void DrawImGuiSettings(DeviceData& device_data) override
   {
      reshade::api::effect_runtime* runtime = nullptr;
      const char* labels[] = { "2x", "4x", "8x" };

      if (ImGui::SliderInt("MSAA", &msaa_index, 0, 2, labels[msaa_index]))
      {
         reshade::set_config_value(runtime, NAME, "MSAA", msaa_index);
      }
      
      const char* labels_toggle[] = { "Off", "On" };
      
      if (ImGui::SliderInt("Alpha To Coverage", &enable_alpha_to_coverage, 0, 1, labels_toggle[enable_alpha_to_coverage]))
      {
         reshade::set_config_value(runtime, NAME, "AlphaToCoverage", enable_alpha_to_coverage);
      }
   }
   
   void PrintImGuiAbout() override
   {
      ImGui::Text("Heaven Burns Red Luma mod - about and credits section", "");
   }
};
   
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
   if (ul_reason_for_call == DLL_PROCESS_ATTACH)
   {
      Globals::SetGlobals(PROJECT_NAME, "Heaven Burns Red Luma mod");
      Globals::DEVELOPMENT_STATE = Globals::ModDevelopmentState::Playable;
      Globals::VERSION = 1;
      
      swapchain_format_upgrade_type = TextureFormatUpgradesType::None;
      swapchain_upgrade_type = SwapchainUpgradeType::None;
      texture_format_upgrades_type = TextureFormatUpgradesType::None;
      force_disable_display_composition = true;
      
      enable_samplers_upgrade = false; // Exit hang

      game = new HeavenBurnsRed();
   }
   else if (ul_reason_for_call == DLL_PROCESS_DETACH)
   {
      reshade::unregister_event<reshade::addon_event::create_resource>(HeavenBurnsRed::OnCreateResource);
      reshade::unregister_event<reshade::addon_event::create_pipeline>(HeavenBurnsRed::OnCreatePipeline);
      //reshade::unregister_event<reshade::addon_event::bind_pipeline>(HeavenBurnsRed::OnBindPipeline);
      reshade::unregister_event<reshade::addon_event::bind_render_targets_and_depth_stencil>(HeavenBurnsRed::OnBindRenderTargetsAndDepthStencil);
   }

   CoreMain(hModule, ul_reason_for_call, lpReserved);
   
   return TRUE;
}   