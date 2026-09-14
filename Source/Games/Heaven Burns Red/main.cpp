#define GAME_HEAVEN_BURNS_RED 1

#define DISABLE_AUTO_DEBUGGER 1
#define DISABLE_FOCUS_LOSS_SUPPRESSION 1
#define AVOID_INPUT_LOSS 1
#define CHECK_GRAPHICS_API_COMPATIBILITY 1
#define DISABLE_SWAPCHAIN_FLIP_MODEL 1
#define ENABLE_POST_DRAW_DISPATCH_CALLBACK 1
#define ALLOW_SHADERS_DUMPING_WITH_NAME 1
#define CUSTOM_MSAA_RESOLVE 0

#include "..\..\Core\core.hpp"
#include "..\..\Core\includes\shader_patching.h"

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

void PatchCharacterPixelShader(std::vector<std::byte>& shader_code)
{
   DXBCHeader* dxbc_header = (DXBCHeader*)&shader_code[0];

   for (uint32_t i = 0; i < dxbc_header->chunk_count; ++i)
   {
      if (strncmp((const char*)&shader_code[dxbc_header->chunk_offsets[i]], "SHEX", 4) == 0)
      {
         std::byte* shex = &shader_code[dxbc_header->chunk_offsets[i]];
         SHEXHeader* shex_header = (SHEXHeader*)shex;
         
         uint32_t pos = 16;
         
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
            
            if (opcode_type == D3D10_SB_OPCODE_DCL_OUTPUT)
            {
               break;
            }
            
            if (opcode_type == D3D10_SB_OPCODE_DCL_INPUT_PS)
            {
               uint32_t opcode = *(uint32_t*)(shex + pos);
               D3D10_SB_INTERPOLATION_MODE interpolation_mode = DECODE_D3D10_SB_INPUT_INTERPOLATION_MODE(opcode);
               switch (interpolation_mode)
               {
                  case D3D10_SB_INTERPOLATION_LINEAR:
                     interpolation_mode = D3D10_SB_INTERPOLATION_LINEAR_SAMPLE;
                     break;
                  case D3D10_SB_INTERPOLATION_LINEAR_NOPERSPECTIVE:
                     interpolation_mode = D3D10_SB_INTERPOLATION_LINEAR_NOPERSPECTIVE_SAMPLE;
                     break;
                  default:
                     break;
               }
               
               opcode &= ~D3D10_SB_INPUT_INTERPOLATION_MODE_MASK;
               opcode |= ENCODE_D3D10_SB_INPUT_INTERPOLATION_MODE(interpolation_mode);
               
               *(uint32_t*)(shex + pos) = opcode;
            }
            
            if (pos + len * 4 >= shex_header->chunk_size + 8)
            {
               break;
            }

            pos += len * 4;
         }
      }
   }

   dxbc_header->file_size = shader_code.size();
   Hash::MD5::Digest md5_digest = CalcDXBCHash(shader_code.data(), shader_code.size());
   std::memcpy(&dxbc_header->hash, &md5_digest.data, DXBCHeader::hash_size);
}

namespace
{
   static const int msaa_values[] = { 2, 4, 8 };
   int msaa_index = 0;
   int enable_alpha_to_coverage = 0;
   int enable_character_supersampling = 0;
   
   ShaderHashesList shader_hashes_skinning = {};
   ShaderHashesList shader_hashes_non_skinning = {};
}

struct CachedRenderTargetResource
{
   ComPtr<ID3D11Texture2D> texture = nullptr;
   ComPtr<ID3D11RenderTargetView> rtv = nullptr;
   ComPtr<ID3D11ShaderResourceView> srv = nullptr;
   D3D11_TEXTURE2D_DESC desc{};
};

struct GameDeviceHeavenBurnsRed final : public GameDeviceData
{
   std::unordered_map<ID3D11BlendState*, ComPtr<ID3D11BlendState>> alpha_blend_states;
   std::unordered_map<uint32_t, ComPtr<ID3D11PixelShader>> modified_atoc_pixel_shaders;
   std::unordered_map<uint32_t, ComPtr<ID3D11PixelShader>> modified_character_pixel_shaders;
   std::unordered_map<uint32_t, std::vector<std::byte>> pixel_shader_code;
   
   std::unordered_set<ID3D11Buffer*> skinned_vertex_buffers;

   ComPtr<ID3D11Resource> tracked_color_resource;
   ComPtr<ID3D11RenderTargetView> tracked_color_rtv;
   ComPtr<ID3D11Resource> tracked_depth_resource;
   ComPtr<ID3D11DepthStencilView> tracked_depth_dsv;
   
   // Immediate context only
   bool is_current_rtv_ms = false;
   bool has_3d_scene_drawn = false;
   CachedRenderTargetResource cached_source_color;
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
#if CUSTOM_MSAA_RESOLVE
      native_shaders_definitions.emplace(CompileTimeStringHash("MSAA Filter VS"), 
         ShaderDefinition{"Luma_MSAAResolveFilter", reshade::api::pipeline_subobject_type::vertex_shader, nullptr, nullptr, 
            {{"VERTEXSHADER", "1"}}});
      
      native_shaders_definitions.emplace(CompileTimeStringHash("MSAA Filter PS"), 
         ShaderDefinition{"Luma_MSAAResolveFilter", reshade::api::pipeline_subobject_type::pixel_shader, nullptr, nullptr, 
            {{"MSAA_SAMPLE", "8"}}});
      
      reshade::register_event<reshade::addon_event::resolve_texture_region>(HeavenBurnsRed::OnResolveTextureRegion);
#endif
      reshade::register_event<reshade::addon_event::create_resource>(HeavenBurnsRed::OnCreateResource);
      reshade::register_event<reshade::addon_event::init_resource>(HeavenBurnsRed::OnInitResource);
      reshade::register_event<reshade::addon_event::destroy_resource>(HeavenBurnsRed::OnDestroyResource);
      reshade::register_event<reshade::addon_event::init_resource_view>(HeavenBurnsRed::OnInitResourceView);
      reshade::register_event<reshade::addon_event::create_pipeline>(HeavenBurnsRed::OnCreatePipeline);
      reshade::register_event<reshade::addon_event::create_sampler>(HeavenBurnsRed::OnCreateSampler);
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

   static void OnInitResource(
      reshade::api::device* device,
      const reshade::api::resource_desc& desc,
      const reshade::api::subresource_data* initial_data,
      reshade::api::resource_usage initial_state,
      reshade::api::resource resource)
   {
      if (desc.type != reshade::api::resource_type::texture_2d)
         return;
      
      if (desc.texture.samples <= 1)
         return;
      
      auto& device_data = *device->get_private_data<DeviceData>();
      
      if (desc.texture.width != device_data.output_resolution.x || desc.texture.height != device_data.output_resolution.y)
         return;
      
      auto& game_device_data = GetGameDeviceData(device_data);
      
      if ((desc.usage & reshade::api::resource_usage::depth_stencil) != reshade::api::resource_usage::undefined)
      {
         game_device_data.tracked_depth_resource = reinterpret_cast<ID3D11Resource*>(resource.handle);
         game_device_data.tracked_depth_dsv = nullptr;
      }
      else if ((desc.usage & reshade::api::resource_usage::render_target) != reshade::api::resource_usage::undefined)
      {
         game_device_data.tracked_color_resource = reinterpret_cast<ID3D11Resource*>(resource.handle);
         game_device_data.tracked_color_rtv = nullptr;
      }
   }
   
   static void OnDestroyResource(
      reshade::api::device* device,
      reshade::api::resource resource)
   {
      auto& device_data = *device->get_private_data<DeviceData>();
      auto& game_device_data = GetGameDeviceData(device_data);
      
      ID3D11Resource* native_resource = reinterpret_cast<ID3D11Resource*>(resource.handle);
      
      if (game_device_data.tracked_color_resource.get() == native_resource)
      {
         game_device_data.tracked_color_resource = nullptr;
         game_device_data.tracked_color_rtv = nullptr;
      }
      else if (game_device_data.tracked_depth_resource.get() == native_resource)
      {
         game_device_data.tracked_depth_resource = nullptr;
         game_device_data.tracked_depth_dsv = nullptr;
      }
   }
   
   static void OnInitResourceView(
      reshade::api::device* device,
      reshade::api::resource resource,
      reshade::api::resource_usage usage_type,
      const reshade::api::resource_view_desc& desc,
      reshade::api::resource_view view)
   {
      auto& device_data = *device->get_private_data<DeviceData>();
      auto& game_device_data = GetGameDeviceData(device_data);
      
      ID3D11Resource* native_resource = reinterpret_cast<ID3D11Resource*>(resource.handle);
      
      if (usage_type == reshade::api::resource_usage::render_target && game_device_data.tracked_color_resource.get() == native_resource)
      {
         game_device_data.tracked_color_rtv = reinterpret_cast<ID3D11RenderTargetView*>(view.handle);
      }
      else if (usage_type == reshade::api::resource_usage::depth_stencil && game_device_data.tracked_depth_resource.get() == native_resource)
      {
         game_device_data.tracked_depth_dsv = reinterpret_cast<ID3D11DepthStencilView*>(view.handle);
      }
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
               
               std::vector<std::byte> code;
               code.resize(original_shader_desc->code_size);
               memcpy(&code[0], original_shader_desc->code, original_shader_desc->code_size);
               game_device_data.pixel_shader_code[hash] = std::move(code);
               
               // Better build the shader list here than check per draw
               bool is_alpha_tested_shader = PatchPixelShader(shader_code);
               
               if (!is_alpha_tested_shader)
                  return false;

               ID3D11Device* native_device = (ID3D11Device*)(device->get_native());
               com_ptr<ID3D11PixelShader> patched_shader;
               native_device->CreatePixelShader(shader_code.data(), shader_code.size(), nullptr, &patched_shader);

               game_device_data.modified_atoc_pixel_shaders[hash] = patched_shader.get();
            }
         }
      }
      return false;
   }
   
   static bool OnCreateSampler(
      reshade::api::device* device,
      reshade::api::sampler_desc &desc)
   {
      if (desc.max_anisotropy <= 1.f)
         return false;
      
      // The game already uses aniso samplers for most of the texture samplings, but for lightmap it's 3x and texture 16x/10x/4x
      desc.max_anisotropy = D3D11_REQ_MAXANISOTROPY;
      return true;
   }
   
   static void OnBindRenderTargetsAndDepthStencil(
      reshade::api::command_list* cmd_list,
      uint32_t count,
      const reshade::api::resource_view *rtvs,
      reshade::api::resource_view dsv)
   {
      auto& device_data = *cmd_list->get_device()->get_private_data<DeviceData>();
      auto& game_device_data = GetGameDeviceData(device_data);
      
      game_device_data.is_current_rtv_ms =
         count == 1 &&
         rtvs[0].handle != 0 &&
         game_device_data.tracked_color_rtv != nullptr &&
         rtvs[0].handle == reinterpret_cast<uint64_t>(game_device_data.tracked_color_rtv.get());
   }
   
#if CUSTOM_MSAA_RESOLVE
   static bool OnResolveTextureRegion(
      reshade::api::command_list* cmd_list,
      reshade::api::resource source,
      uint32_t source_subresource,
      const reshade::api::subresource_box* source_box,
      reshade::api::resource dest,
      uint32_t dest_subresource,
      uint32_t dest_x,
      uint32_t dest_y,
      uint32_t dest_z,
      reshade::api::format format)
   {
      if (source.handle == 0)
         return false;

      if (dest.handle == 0)
         return false;

      auto& device_data = *cmd_list->get_device()->get_private_data<DeviceData>();
      auto& game_device_data = GetGameDeviceData(device_data);

      if (!game_device_data.has_3d_scene_drawn)
      {
#if 0
         reshade::log::message(reshade::log::level::debug, "Fail to detect 3d scene.");
#endif
         return false;
      }
      
      ComPtr<ID3D11Resource> src_tex;
      HRESULT hr_src = reinterpret_cast<ID3D11Resource*>(source.handle)->QueryInterface(src_tex.put());
      if (FAILED(hr_src))
      {
#if DEVELOPMENT
         reshade::log::message(reshade::log::level::debug, "Fail to resolve incompatible source.");
#endif
         return false;
      }
      
      if (game_device_data.cached_source_color.texture.get() == src_tex.get())
      {
         if (test_index == 14)
         {
            ComPtr<ID3D11DeviceContext> native_device_context;
            ID3D11DeviceChild* device_child = (ID3D11DeviceChild*)(cmd_list->get_native());
            HRESULT hr = device_child->QueryInterface(native_device_context.put());
            if (FAILED(hr))
            {
#if DEVELOPMENT
               reshade::log::message(reshade::log::level::debug, "Fail to get context device.");
#endif
               return false;
            }

            ID3D11Device* native_device = (ID3D11Device*)(cmd_list->get_device()->get_native());

            ComPtr<ID3D11RenderTargetView> render_target_view;
            {
               ComPtr<ID3D11Resource> dest_tex;
               HRESULT hr_dest = reinterpret_cast<ID3D11Resource*>(dest.handle)->QueryInterface(dest_tex.put());
               if (FAILED(hr_dest))
               {
#if DEVELOPMENT
                  reshade::log::message(reshade::log::level::debug, "Fail to resolve incompatible dest.");
#endif
                  return false;
               }

               D3D11_RENDER_TARGET_VIEW_DESC rtv_desc = {};
               rtv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
               rtv_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
               rtv_desc.Texture2D.MipSlice = 0;

               hr = native_device->CreateRenderTargetView(dest_tex.get(), &rtv_desc, render_target_view.put());
               if (FAILED(hr))
               {
#if DEVELOPMENT
                  reshade::log::message(reshade::log::level::debug, "Fail to create dest render target.");
#endif
                  return false;
               }
            }

            DrawStateStack<DrawStateStackType::FullGraphics> draw_state_stack;
            draw_state_stack.Cache(native_device_context.get(), device_data.uav_max_count);

   #if 0
            reshade::log::message(reshade::log::level::debug, "Drawing custom resolve.");
   #endif

            // Set the new resources/states:
            constexpr FLOAT blend_factor_alpha[4] = { 1.f, 1.f, 1.f, 1.f };
            constexpr FLOAT blend_factor[4] = { 1.f, 1.f, 1.f, 0.f }; // TODO: this makes no sense as the blend state is unlikely to use it, use write mask instead
            native_device_context->OMSetBlendState(device_data.default_blend_state.get(), false ? blend_factor_alpha : blend_factor, 0xFFFFFFFF);
            native_device_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            native_device_context->RSSetScissorRects(0, nullptr);
            D3D11_VIEWPORT viewport;
            viewport.TopLeftX = 0;
            viewport.TopLeftY = 0;
            viewport.Width = game_device_data.cached_source_color.desc.Width;
            viewport.Height = game_device_data.cached_source_color.desc.Height;
            viewport.MinDepth = 0;
            viewport.MaxDepth = 1;
            native_device_context->RSSetViewports(1, &viewport);
            native_device_context->OMSetRenderTargets(1, render_target_view.get_addressof(), nullptr);
            native_device_context->PSSetShaderResources(0, 1, game_device_data.cached_source_color.srv.get_addressof());
            native_device_context->OMSetDepthStencilState(nullptr, 0);
            ID3D11VertexShader* vs = device_data.native_vertex_shaders[CompileTimeStringHash("MSAA Filter VS")].get();
            ID3D11PixelShader* ps = device_data.native_pixel_shaders[CompileTimeStringHash("MSAA Filter PS")].get();
            native_device_context->VSSetShader(vs, nullptr, 0);
            native_device_context->PSSetShader(ps, nullptr, 0);
            native_device_context->IASetInputLayout(nullptr);
            native_device_context->RSSetState(nullptr);

            // Finally draw:
            native_device_context->Draw(3, 0);

            draw_state_stack.Restore(native_device_context.get());
            return true;
         }
      }
      else
      {
#if 0
         reshade::log::message(reshade::log::level::debug, "Resolve source isn't cached.");
#endif
      }
      return false;
   }
#endif
   
   static ID3D11PixelShader* GetCharacterPixelShader(
      uint32_t pixel_shader_hash,
      ID3D11Device* native_device,
      GameDeviceHeavenBurnsRed& game_device_data)
   {
      auto shader_it = game_device_data.modified_character_pixel_shaders.find(pixel_shader_hash);
   
      if (shader_it != game_device_data.modified_character_pixel_shaders.end())
      {
         return shader_it->second.get();
      }
      else
      {
         const auto shader_code_it = game_device_data.pixel_shader_code.find(pixel_shader_hash);
         if (shader_code_it == game_device_data.pixel_shader_code.cend())
         {
            return nullptr;
         }

         std::vector<std::byte> shader_code = shader_code_it->second;
         
         PatchCharacterPixelShader(shader_code);
         
#if DEVELOPMENT
         reshade::log::message(reshade::log::level::debug, std::format("Character Pixel Shader Patched: 0x{:08X}", pixel_shader_hash).c_str());
#endif

         HRESULT hr = native_device->CreatePixelShader(shader_code.data(), shader_code.size(), nullptr, game_device_data.modified_character_pixel_shaders[pixel_shader_hash].put());
         if (FAILED(hr))
         {
            game_device_data.modified_character_pixel_shaders.erase(pixel_shader_hash);
            return nullptr;
         }
         return game_device_data.modified_character_pixel_shaders[pixel_shader_hash].get();
      }
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
      auto& game_device_data = GetGameDeviceData(device_data);
      if (enable_character_supersampling && (stages & reshade::api::shader_stage::compute) != 0)
      {
         bool is_skinning = false;
         while (true)
         {
            if (original_shader_hashes.Contains(shader_hashes_non_skinning))
               break;
            
            is_skinning = original_shader_hashes.Contains(shader_hashes_skinning);
            
            if (!is_skinning)
            {
               ComPtr<ID3D11ComputeShader> shader;
               native_device_context->CSGetShader(shader.put(), nullptr, nullptr);

               std::optional<std::string> optional_name = std::nullopt;//GetD3DNameW(shader.get());
               byte data[128] = {};
               UINT size = sizeof(data);
               if (shader.get()->GetPrivateData(WKPDID_D3DDebugObjectName, &size, data) == S_OK)
               {
                  if (size > 0)
                     optional_name = std::string{ data, data + size };
               }
               
               if (optional_name.has_value() && !optional_name->empty())
               {
                  if (optional_name->contains("Skinning"))
                  {
#if DEVELOPMENT
                     reshade::log::message(reshade::log::level::debug, std::format("Skinning Compute Shader: 0x{:08X}", original_shader_hashes.compute_shaders[0]).c_str());
#endif
                     is_skinning = true;
                     shader_hashes_skinning.compute_shaders.emplace(original_shader_hashes.compute_shaders[0]);
                  }
               }
            }
            
            if (!is_skinning)
            {
               shader_hashes_non_skinning.compute_shaders.emplace(original_shader_hashes.compute_shaders[0]);
            }
            break;
         }
            
         // it seems like almost all dynamic objects will be animated by skinning shaders
         if (is_skinning)
         {
            ComPtr<ID3D11UnorderedAccessView> uavs[5] = {};
            // batch skin supports max of 5
            native_device_context->CSGetUnorderedAccessViews(0, 5, reinterpret_cast<ID3D11UnorderedAccessView**>(uavs));

            for (const auto& uav : uavs)
            {
               if (!uav)
                  continue;

               ComPtr<ID3D11Resource> resource;
               uav->GetResource(resource.put());

               ComPtr<ID3D11Buffer> buffer;
               if (SUCCEEDED(resource->QueryInterface(buffer.put())))
                  game_device_data.skinned_vertex_buffers.insert(buffer.get());
            }
         }
         
         return DrawOrDispatchOverrideType::None;
      }
      
      if ((stages & reshade::api::shader_stage::pixel) == 0)
         return DrawOrDispatchOverrideType::None;
      
      if (original_shader_hashes.pixel_shaders.empty())
         return DrawOrDispatchOverrideType::None;
      
      if (game_device_data.is_current_rtv_ms)
      {
         if (!game_device_data.has_3d_scene_drawn)
         {
            game_device_data.has_3d_scene_drawn = true;

            if (game_device_data.tracked_color_rtv.get() != game_device_data.cached_source_color.rtv.get())
            {
               game_device_data.cached_source_color.rtv = game_device_data.tracked_color_rtv;

               if (game_device_data.tracked_color_resource &&
                   SUCCEEDED(game_device_data.tracked_color_resource->QueryInterface(game_device_data.cached_source_color.texture.put())))
               {
                  game_device_data.cached_source_color.texture->GetDesc(&game_device_data.cached_source_color.desc);

                  D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
                  srv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                  srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DMS;
                  // D3D11_TEX2DMS_SRV has no fields to set

                  const HRESULT hr = native_device->CreateShaderResourceView(game_device_data.cached_source_color.texture.get(), &srv_desc, game_device_data.cached_source_color.srv.put());
               }
            }
         }
         
         if (enable_alpha_to_coverage)
         {
            auto shader_it = game_device_data.modified_atoc_pixel_shaders.find(original_shader_hashes.pixel_shaders[0]);
         
            if (shader_it != game_device_data.modified_atoc_pixel_shaders.end())
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
         
         if (enable_character_supersampling)
         {
            auto shader_it = game_device_data.modified_character_pixel_shaders.find(original_shader_hashes.pixel_shaders[0]);
            if (shader_it != game_device_data.modified_character_pixel_shaders.end())
            {
               native_device_context->PSSetShader(shader_it->second.get(), nullptr, 0);
#if DEVELOPMENT
               if (test_index == 13)
               {
                  return DrawOrDispatchOverrideType::Skip;
               }
#endif
            }
            else
            {
               ComPtr<ID3D11Buffer> vertex_buffer;
               native_device_context->IAGetVertexBuffers(0, 1, vertex_buffer.put(), nullptr, nullptr);
               if (game_device_data.skinned_vertex_buffers.contains(vertex_buffer.get()))
               {
                  native_device_context->PSSetShader(GetCharacterPixelShader(original_shader_hashes.pixel_shaders[0], native_device, game_device_data), nullptr, 0);
               }
            }
            return DrawOrDispatchOverrideType::None;
         }
      }
      
      return DrawOrDispatchOverrideType::None;
   }
   
   void OnPresent(
      ID3D11Device* native_device,
      DeviceData& device_data) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);
      
      game_device_data.skinned_vertex_buffers.clear();
      
      game_device_data.is_current_rtv_ms = false;
      game_device_data.has_3d_scene_drawn = false;
   }
   
   void LoadConfigs() override
   {
      reshade::api::effect_runtime* runtime = nullptr;
      reshade::get_config_value(runtime, NAME, "MSAA", msaa_index);
      reshade::get_config_value(runtime, NAME, "AlphaToCoverage", enable_alpha_to_coverage);
      reshade::get_config_value(runtime, NAME, "SuperSampling", enable_character_supersampling);
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
      
      if (ImGui::SliderInt("Character Supersampling", &enable_character_supersampling, 0, 1, labels_toggle[enable_character_supersampling]))
      {
         reshade::set_config_value(runtime, NAME, "SuperSampling", enable_character_supersampling);
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
#if CUSTOM_MSAA_RESOLVE
      reshade::unregister_event<reshade::addon_event::resolve_texture_region>(HeavenBurnsRed::OnResolveTextureRegion);
#endif
      reshade::unregister_event<reshade::addon_event::create_resource>(HeavenBurnsRed::OnCreateResource);
      reshade::unregister_event<reshade::addon_event::init_resource>(HeavenBurnsRed::OnInitResource);
      reshade::unregister_event<reshade::addon_event::destroy_resource>(HeavenBurnsRed::OnDestroyResource);
      reshade::unregister_event<reshade::addon_event::init_resource_view>(HeavenBurnsRed::OnInitResourceView);
      reshade::unregister_event<reshade::addon_event::create_pipeline>(HeavenBurnsRed::OnCreatePipeline);
      reshade::unregister_event<reshade::addon_event::create_sampler>(HeavenBurnsRed::OnCreateSampler);
      reshade::unregister_event<reshade::addon_event::bind_render_targets_and_depth_stencil>(HeavenBurnsRed::OnBindRenderTargetsAndDepthStencil);
   }

   CoreMain(hModule, ul_reason_for_call, lpReserved);
   
   return TRUE;
}