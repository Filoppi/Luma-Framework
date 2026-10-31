#pragma once
#include "..\..\..\Core\includes\shader_patching.h"

struct RDEFHeader
{
   char chunk_name[4]; // 'RDEF'
   uint32_t chunk_size;
   uint32_t constant_buffer_count;
   uint32_t constant_buffer_offset;
   uint32_t resource_binding_count;
   uint32_t resource_binding_offset;
   uint8_t version_minor;
   uint8_t version_major;
   uint16_t program_type;
   uint32_t flags;
   uint32_t creator_string_offset;
};

struct ConstantBufferDesc
{
   uint32_t name_offset;
   uint32_t variable_count;
   uint32_t variable_desc_offset;
   uint32_t size;
   uint32_t flags;
   uint32_t cbuffer_type;
};

struct ResourceBindingDesc
{
   uint32_t name_offset;
   uint32_t input_type;
   uint32_t resource_return_type;
   uint32_t view_dimension;
   uint32_t sample_count;
   uint32_t bind_point;
   uint32_t bind_count;
   uint32_t flags;
};

struct VariableDesc
{
   uint32_t name_offset;
   uint32_t data_offset;
   uint32_t size;
   uint32_t flags;
   uint32_t type_offset;
   uint32_t default_value_offset;
   uint32_t start_texture;
   uint32_t texture_size;
   uint32_t start_sampler;
   uint32_t sampler_size;
};

struct VariableTypeDesc
{
   uint16_t variable_class;
   uint16_t variable_type;
   uint16_t row_count;
   uint16_t column_count;
   uint16_t element_count;
   uint16_t member_count;
   uint32_t member_offset;
   uint8_t reserved[16];
   uint32_t name_offset;
};

struct DXBCSignatureEntry
{
   uint32_t name_offset;
   uint32_t semantic_index;
   uint32_t value_type;
   uint32_t component_type;
   uint32_t reg;
   uint8_t component_mask;
   uint8_t read_write_mask;
};

struct SHEXHeader
{
   char chunk_name[4]; // 'SHEX'
   uint32_t chunk_size;
   uint8_t version;
   uint16_t type;
   uint32_t dword_count;
};

// Fog calculations divide screen pixel position by the render target size instead of the size
// of the viewport used by dynamic resolution scaling causing sampling the sky texture out of bounds
// patch the affected pixel shaders to use the correct scaling factor
bool PatchPixelShaderFogScaling(std::vector<std::byte>& shader_code)
{
   DXBCHeader* dxbc_header = (DXBCHeader*)&shader_code[0];

   uint32_t system_cb_scene_register = 0xFFFFFFFF;
   uint32_t system_cb_dynamic_resolution_register = 0xFFFFFFFF;
   uint32_t screen_size_offset = 0xFFFFFFFF;
   uint32_t dynamic_resolution_size_offset = 0xFFFFFFFF;

   for (uint32_t i = 0; i < dxbc_header->chunk_count; ++i)
   {
      if (strncmp((const char*)&shader_code[dxbc_header->chunk_offsets[i]], "RDEF", 4) == 0)
      {
         const std::byte* rdef = &shader_code[dxbc_header->chunk_offsets[i]];

         const RDEFHeader* rdef_header = (const RDEFHeader*)rdef;

         for (uint32_t j = 0; j < rdef_header->resource_binding_count; ++j)
         {
            const ResourceBindingDesc* resource_binding_desc = (ResourceBindingDesc*)(rdef + 8 + rdef_header->resource_binding_offset + j * sizeof(ResourceBindingDesc));
            if (resource_binding_desc->input_type == 0 &&
                strncmp((const char*)(rdef + 8 + resource_binding_desc->name_offset), "SystemCbScene", 13) == 0)
            {
               system_cb_scene_register = resource_binding_desc->bind_point;
            }
            else if (resource_binding_desc->input_type == 0 &&
                     strncmp((const char*)(rdef + 8 + resource_binding_desc->name_offset), "SystemCbDynamicResolution", 25) == 0)
            {
               system_cb_dynamic_resolution_register = resource_binding_desc->bind_point;
            }
         }

         for (uint32_t j = 0; j < rdef_header->constant_buffer_count; ++j)
         {
            const ConstantBufferDesc* constant_buffer_desc = (ConstantBufferDesc*)(rdef + 8 + rdef_header->constant_buffer_offset + j * sizeof(ConstantBufferDesc));
            if (strncmp((const char*)(rdef + 8 + constant_buffer_desc->name_offset), "SystemCbScene", 13) == 0)
            {
               for (uint32_t k = 0; k < constant_buffer_desc->variable_count; ++k)
               {
                  const VariableDesc* variable = (const VariableDesc*)(rdef + 8 + constant_buffer_desc->variable_desc_offset + k * sizeof(VariableDesc));
                  if (strncmp((const char*)(rdef + 8 + variable->name_offset), "gScreenSize", 11) == 0)
                  {
                     screen_size_offset = variable->data_offset;
                  }
               }
            }
            else if (strncmp((const char*)(rdef + 8 + constant_buffer_desc->name_offset), "SystemCbDynamicResolution", 25) == 0)
            {
               for (uint32_t k = 0; k < constant_buffer_desc->variable_count; ++k)
               {
                  const VariableDesc* variable = (const VariableDesc*)(rdef + 8 + constant_buffer_desc->variable_desc_offset + k * sizeof(VariableDesc));
                  if (strncmp((const char*)(rdef + 8 + variable->name_offset), "dynamic_resolution_size", 23) == 0)
                  {
                     dynamic_resolution_size_offset = variable->data_offset;
                  }
               }
            }
         }
      }
   }

   if (system_cb_scene_register == 0xFFFFFFFF ||
       system_cb_dynamic_resolution_register == 0xFFFFFFFF ||
       screen_size_offset == 0xFFFFFFFF ||
       dynamic_resolution_size_offset == 0xFFFFFFFF)
   {
      return false;
   }

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
            const uint32_t len = max(DECODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(*(uint32_t*)(shex + pos)), 1);

            if (opcode_type == D3D10_SB_OPCODE_DIV &&
                system_cb_scene_register != 0xFFFFFFFF &&
                system_cb_dynamic_resolution_register != 0xFFFFFFFF &&
                screen_size_offset != 0xFFFFFFFF &&
                dynamic_resolution_size_offset != 0xFFFFFFFF)
            {
               D3D10_SB_OPERAND_TYPE operand_type = DECODE_D3D10_SB_OPERAND_TYPE(*(uint32_t*)(shex + pos + 20));

               if (operand_type == D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER)
               {
                  D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE selection_mode = DECODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(*(uint32_t*)(shex + pos + 20));
                  D3D10_SB_4_COMPONENT_NAME comp_0 = DECODE_D3D10_SB_OPERAND_4_COMPONENT_SWIZZLE_SOURCE(*(uint32_t*)(shex + pos + 20), 0);
                  uint32_t operand_binding = *(uint32_t*)(shex + pos + 24);
                  uint32_t operand_offset = *(uint32_t*)(shex + pos + 28);
                  if (selection_mode == D3D10_SB_OPERAND_4_COMPONENT_SWIZZLE_MODE &&
                      comp_0 == D3D10_SB_4_COMPONENT_Z &&
                      operand_binding == system_cb_scene_register && operand_offset * 16 + 8 == screen_size_offset)
                  {
                     *(uint32_t*)(shex + pos + 20) =
                        ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_4_COMPONENT) |
                        ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(D3D10_SB_OPERAND_4_COMPONENT_SWIZZLE_MODE) |
                        ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SWIZZLE(D3D10_SB_4_COMPONENT_X, D3D10_SB_4_COMPONENT_Y, D3D10_SB_4_COMPONENT_Z, D3D10_SB_4_COMPONENT_W) |
                        ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER) |
                        ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_2D) |
                        ENCODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(0, D3D10_SB_OPERAND_INDEX_IMMEDIATE32);

                     *(uint32_t*)(shex + pos + 24) = system_cb_dynamic_resolution_register;
                     *(uint32_t*)(shex + pos + 28) = dynamic_resolution_size_offset / 16;
                  }
               }
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

   return true;
}

// Add write to motion vector texture to pixel shader
bool PatchPixelShaderMotionVectors(std::vector<std::byte>& shader_code)
{
   DXBCHeader* dxbc_header = (DXBCHeader*)&shader_code[0];
   uint32_t coord_input_register = 0xFFFFFFFF;
   uint32_t prev_coord_input_register = 0xFFFFFFFF;

   for (uint32_t i = 0; i < dxbc_header->chunk_count; ++i)
   {
      if (strncmp((const char*)&shader_code[dxbc_header->chunk_offsets[i]], "ISGN", 4) == 0)
      {
         std::byte* isgn = &shader_code[dxbc_header->chunk_offsets[i]];
         uint32_t isgn_entry_count = *(uint32_t*)(isgn + 8);
         DXBCSignatureEntry* isgn_entries = (DXBCSignatureEntry*)(isgn + 16);

         for (uint32_t j = 0; j < isgn_entry_count; ++j)
         {
            if (strncmp((const char*)(isgn + 8 + isgn_entries[j].name_offset), "CURR_POSITION_CS", 16) == 0)
            {
               coord_input_register = j;
               isgn_entries[j].read_write_mask = 11;
            }
            else if (strncmp((const char*)(isgn + 8 + isgn_entries[j].name_offset), "PREV_POSITION_CS", 16) == 0)
            {
               prev_coord_input_register = j;
               isgn_entries[j].read_write_mask = 11;
            }
         }
      }
   }

   if (prev_coord_input_register == 0xFFFFFFFF ||
       coord_input_register == 0xFFFFFFFF)
   {
      return false;
   }

   for (uint32_t i = 0; i < dxbc_header->chunk_count; ++i)
   {
      if (strncmp((const char*)&shader_code[dxbc_header->chunk_offsets[i]], "OSGN", 4) == 0)
      {
         // add motion vector output on semantic index 5
         std::byte* osgn = &shader_code[dxbc_header->chunk_offsets[i]];
         uint32_t osgn_entry_count = *(uint32_t*)(osgn + 8);
         DXBCSignatureEntry* osgn_entries = (DXBCSignatureEntry*)(osgn + 16);
         for (uint32_t j = 0; j < osgn_entry_count; ++j)
         {
            osgn_entries[j].name_offset += sizeof(DXBCSignatureEntry);
         }
         DXBCSignatureEntry motion_vector_signature;
         motion_vector_signature.name_offset = osgn_entries[0].name_offset;
         motion_vector_signature.semantic_index = 5;
         motion_vector_signature.value_type = 0;
         motion_vector_signature.component_type = 3;
         motion_vector_signature.reg = 5;
         motion_vector_signature.component_mask = 0xF;
         motion_vector_signature.read_write_mask = 0;

         *(uint32_t*)(osgn + 4) += sizeof(DXBCSignatureEntry);
         *(uint32_t*)(osgn + 8) += 1;
         shader_code.insert(shader_code.begin() + dxbc_header->chunk_offsets[i] + 16 + osgn_entry_count * sizeof(DXBCSignatureEntry),
            (std::byte*)&motion_vector_signature,
            (std::byte*)&motion_vector_signature + sizeof(DXBCSignatureEntry));
         dxbc_header = (DXBCHeader*)&shader_code[0];
         for (uint32_t j = i + 1; j < dxbc_header->chunk_count; ++j)
         {
            dxbc_header->chunk_offsets[j] += sizeof(DXBCSignatureEntry);
         }
      }
      else if (strncmp((const char*)&shader_code[dxbc_header->chunk_offsets[i]], "SHEX", 4) == 0)
      {
         std::byte* shex = &shader_code[dxbc_header->chunk_offsets[i]];
         SHEXHeader* shex_header = (SHEXHeader*)shex;

         uint32_t pos = 16;

         bool alpha_written = false;
         bool coord_input_defined = false;
         bool prev_coord_input_defined = false;

         D3D10_SB_OPCODE_TYPE prev_opcode_type = D3D10_SB_NUM_OPCODES;
         for (;;)
         {
            D3D10_SB_OPCODE_TYPE opcode_type = DECODE_D3D10_SB_OPCODE_TYPE(*(uint32_t*)(shex + pos));
            const uint32_t len = max(DECODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(*(uint32_t*)(shex + pos)), 1);

            if (opcode_type == D3D10_SB_OPCODE_DCL_INPUT_PS)
            {
               uint32_t reg = *(uint32_t*)(shex + pos + 8);
               if (reg == coord_input_register ||
                   reg == prev_coord_input_register)
               {
                  *(uint32_t*)(shex + pos + 4) |= ENCODE_D3D10_SB_OPERAND_4_COMPONENT_MASK(D3D10_SB_OPERAND_4_COMPONENT_MASK_X | D3D10_SB_OPERAND_4_COMPONENT_MASK_Y | D3D10_SB_OPERAND_4_COMPONENT_MASK_W);
                  coord_input_defined |= (reg == coord_input_register);
                  prev_coord_input_defined |= (reg == prev_coord_input_register);
               }
            }

            if (prev_opcode_type == D3D10_SB_OPCODE_DCL_INPUT_PS &&
                opcode_type != D3D10_SB_OPCODE_DCL_INPUT_PS)
            {
                std::vector<uint32_t> shader_patch;
                if (!coord_input_defined)
                {
                    uint32_t opcode_token =
                        ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DCL_INPUT_PS) |
                        ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3) |
                        ENCODE_D3D10_SB_INPUT_INTERPOLATION_MODE(D3D10_SB_INTERPOLATION_LINEAR);
                    uint32_t operand_token =
                        ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_4_COMPONENT) |
                        ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(D3D10_SB_OPERAND_4_COMPONENT_MASK_MODE) |
                        ENCODE_D3D10_SB_OPERAND_4_COMPONENT_MASK(D3D10_SB_OPERAND_4_COMPONENT_MASK_X | D3D10_SB_OPERAND_4_COMPONENT_MASK_Y | D3D10_SB_OPERAND_4_COMPONENT_MASK_W) |
                        ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_INPUT) |
                        ENCODE_D3D10_SB_INPUT_INTERPOLATION_MODE(D3D10_SB_INTERPOLATION_LINEAR) |
                        ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_1D) |
                        ENCODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(0, D3D10_SB_OPERAND_INDEX_IMMEDIATE32);
                    shader_patch.insert(shader_patch.end(), {
                       opcode_token,
                       operand_token, coord_input_register });
                }
                if (!prev_coord_input_defined)
                {
                    uint32_t opcode_token =
                        ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DCL_INPUT_PS) |
                        ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3) |
                        ENCODE_D3D10_SB_INPUT_INTERPOLATION_MODE(D3D10_SB_INTERPOLATION_LINEAR);
                    uint32_t operand_token =
                        ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_4_COMPONENT) |
                        ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(D3D10_SB_OPERAND_4_COMPONENT_MASK_MODE) |
                        ENCODE_D3D10_SB_OPERAND_4_COMPONENT_MASK(D3D10_SB_OPERAND_4_COMPONENT_MASK_X | D3D10_SB_OPERAND_4_COMPONENT_MASK_Y | D3D10_SB_OPERAND_4_COMPONENT_MASK_W) |
                        ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_INPUT) |
                        ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_1D) |
                        ENCODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(0, D3D10_SB_OPERAND_INDEX_IMMEDIATE32);
                    shader_patch.insert(shader_patch.end(), {
                       opcode_token,
                       operand_token, prev_coord_input_register });
                }
                shader_code.insert(shader_code.begin() + dxbc_header->chunk_offsets[i] + pos, (std::byte*)&shader_patch[0], (std::byte*)(&shader_patch[0] + shader_patch.size()));
                dxbc_header = (DXBCHeader*)&shader_code[0];
                shex = &shader_code[dxbc_header->chunk_offsets[i]];
                shex_header = (SHEXHeader*)shex;
                shex_header->chunk_size += shader_patch.size() * sizeof(uint32_t);
                shex_header->dword_count += shader_patch.size();
                for (uint32_t j = i + 1; j < dxbc_header->chunk_count; ++j)
                {
                    dxbc_header->chunk_offsets[j] += shader_patch.size() * sizeof(uint32_t);
                }
                prev_opcode_type = D3D10_SB_NUM_OPCODES;
                pos += shader_patch.size() * sizeof(uint32_t);
                continue;
            }

            if (prev_opcode_type == D3D10_SB_OPCODE_DCL_OUTPUT &&
                opcode_type != D3D10_SB_OPCODE_DCL_OUTPUT)
            {
               uint32_t opcode_token =
                  ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DCL_OUTPUT) |
                  ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3) |
                  ENCODE_D3D10_SB_INSTRUCTION_SATURATE(false);
               uint32_t operand_token =
                  ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_4_COMPONENT) |
                  ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(D3D10_SB_OPERAND_4_COMPONENT_MASK_MODE) |
                  ENCODE_D3D10_SB_OPERAND_4_COMPONENT_MASK(D3D10_SB_OPERAND_4_COMPONENT_MASK_X | D3D10_SB_OPERAND_4_COMPONENT_MASK_Y | D3D10_SB_OPERAND_4_COMPONENT_MASK_W) |
                  ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_OUTPUT) |
                  ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_1D) |
                  ENCODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(0, D3D10_SB_OPERAND_INDEX_IMMEDIATE32);

               std::vector<uint32_t> shader_patch{
                  opcode_token,
                  operand_token, 5};
               shader_code.insert(shader_code.begin() + dxbc_header->chunk_offsets[i] + pos, (std::byte*)&shader_patch[0], (std::byte*)(&shader_patch[0] + shader_patch.size()));
               dxbc_header = (DXBCHeader*)&shader_code[0];
               shex = &shader_code[dxbc_header->chunk_offsets[i]];
               shex_header = (SHEXHeader*)shex;
               shex_header->chunk_size += shader_patch.size() * sizeof(uint32_t);
               shex_header->dword_count += shader_patch.size();
               for (uint32_t j = i + 1; j < dxbc_header->chunk_count; ++j)
               {
                  dxbc_header->chunk_offsets[j] += shader_patch.size() * sizeof(uint32_t);
               }
               prev_opcode_type = D3D10_SB_NUM_OPCODES;
               pos += shader_patch.size() * sizeof(uint32_t);
               continue;
            }

            // most (all?) transparent shaders output color with min o0.xyzw, r0.xyzw, l(65504.000000, 65504.000000, 65504.000000, 65504.000000)
            // copy alpha for motion vectors with
            // r0.w = 0.5 < r0.w;
            // o5.w = r0.w ? 0 : 1;
            if (opcode_type == D3D10_SB_OPCODE_MIN && len == 10)
            {
               D3D10_SB_OPERAND_TYPE operand_type_output = DECODE_D3D10_SB_OPERAND_TYPE(*(uint32_t*)(shex + pos + 4));
               D3D10_SB_OPERAND_TYPE operand_type_src_0 = DECODE_D3D10_SB_OPERAND_TYPE(*(uint32_t*)(shex + pos + 12));
               D3D10_SB_OPERAND_TYPE operand_type_src_1 = DECODE_D3D10_SB_OPERAND_TYPE(*(uint32_t*)(shex + pos + 20));

               if (operand_type_output == D3D10_SB_OPERAND_TYPE_OUTPUT &&
                   *(uint32_t*)(shex + pos + 8) == 0 &&
                   operand_type_src_0 == D3D10_SB_OPERAND_TYPE_TEMP &&
                   operand_type_src_1 == D3D10_SB_OPERAND_TYPE_IMMEDIATE32)
               {
                  std::vector<uint32_t> shader_patch;
                  {
                     uint32_t opcode_token =
                        ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_LT) |
                        ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7) |
                        ENCODE_D3D10_SB_INSTRUCTION_SATURATE(false);

                     // Dest0 operand
                     uint32_t dest_0_operand_token =
                        ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_4_COMPONENT) |
                        ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(D3D10_SB_OPERAND_4_COMPONENT_MASK_MODE) |
                        ENCODE_D3D10_SB_OPERAND_4_COMPONENT_MASK(D3D10_SB_OPERAND_4_COMPONENT_MASK_W) |
                        ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_TEMP) |
                        ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_1D) |
                        ENCODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(0, D3D10_SB_OPERAND_INDEX_IMMEDIATE32);
                     // Src0 operand
                     uint32_t src_0_operand_token =
                        ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_IMMEDIATE32) |
                        ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_1_COMPONENT);
                     // Src1 operand
                     uint32_t src_1_operand_token =
                        ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_4_COMPONENT) |
                        ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(D3D10_SB_OPERAND_4_COMPONENT_MASK_MODE) |
                        ENCODE_D3D10_SB_OPERAND_4_COMPONENT_MASK(D3D10_SB_OPERAND_4_COMPONENT_MASK_W) |
                        ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_TEMP) |
                        ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_1D) |
                        ENCODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(0, D3D10_SB_OPERAND_INDEX_IMMEDIATE32);

                     shader_patch.insert(shader_patch.end(), {opcode_token,
                                                                dest_0_operand_token, *(uint32_t*)(shex + pos + 16),
                                                                src_0_operand_token, 0x3F000000, /* float-value 0.5f */
                                                                src_1_operand_token, *(uint32_t*)(shex + pos + 16)});
                  }
                  {
                     uint32_t opcode_token =
                        ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                        ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9) |
                        ENCODE_D3D10_SB_INSTRUCTION_SATURATE(false);

                     // Dest0 operand
                     uint32_t dest_0_operand_token =
                        ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_4_COMPONENT) |
                        ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(D3D10_SB_OPERAND_4_COMPONENT_MASK_MODE) |
                        ENCODE_D3D10_SB_OPERAND_4_COMPONENT_MASK(D3D10_SB_OPERAND_4_COMPONENT_MASK_W) |
                        ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_OUTPUT) |
                        ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_1D) |
                        ENCODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(0, D3D10_SB_OPERAND_INDEX_IMMEDIATE32);
                     // Src0 operand
                     uint32_t src_0_operand_token =
                        ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_4_COMPONENT) |
                        ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(D3D10_SB_OPERAND_4_COMPONENT_MASK_MODE) |
                        ENCODE_D3D10_SB_OPERAND_4_COMPONENT_MASK(D3D10_SB_OPERAND_4_COMPONENT_MASK_W) |
                        ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_TEMP) |
                        ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_1D) |
                        ENCODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(0, D3D10_SB_OPERAND_INDEX_IMMEDIATE32);
                     // Src1 operand
                     uint32_t src_1_operand_token =
                        ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_IMMEDIATE32) |
                        ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_1_COMPONENT);
                     // Src2 operand
                     uint32_t src_2_operand_token =
                        ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_IMMEDIATE32) |
                        ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_1_COMPONENT);

                     shader_patch.insert(shader_patch.end(), {opcode_token,
                                                                dest_0_operand_token, 5,
                                                                src_0_operand_token, *(uint32_t*)(shex + pos + 16),
                                                                src_1_operand_token, 0x3F800000 /* float-value 1.0f */,
                                                                src_1_operand_token, 0});
                  }

                  shader_code.insert(shader_code.begin() + dxbc_header->chunk_offsets[i] + pos + len * sizeof(uint32_t), (std::byte*)&shader_patch[0], (std::byte*)(&shader_patch[0] + shader_patch.size()));
                  dxbc_header = (DXBCHeader*)&shader_code[0];
                  shex = &shader_code[dxbc_header->chunk_offsets[i]];
                  shex_header = (SHEXHeader*)shex;
                  shex_header->chunk_size += shader_patch.size() * sizeof(uint32_t);
                  shex_header->dword_count += shader_patch.size();
                  for (uint32_t j = i + 1; j < dxbc_header->chunk_count; ++j)
                  {
                     dxbc_header->chunk_offsets[j] += shader_patch.size() * sizeof(uint32_t);
                  }
                  prev_opcode_type = D3D10_SB_NUM_OPCODES;
                  pos += len * sizeof(uint32_t);

                  alpha_written = true;

                  continue;
               }
            }

            // r0.xy = v[coord_input_register].xy / v[coord_input_register].ww;
            // r1.xy = v[prev_coord_input_register].xy / v[prev_coord_input_register].ww;
            // o5.xy = r0.xy + -r1.xy;
            if (pos + len * 4 >= shex_header->chunk_size + 8)
            {
               std::vector<uint32_t> shader_patch;
               {
                  uint32_t opcode_token =
                     ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DIV) |
                     ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7) |
                     ENCODE_D3D10_SB_INSTRUCTION_SATURATE(false);
                  // Dest0 operand
                  uint32_t dest_0_operand_token =
                     ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_4_COMPONENT) |
                     ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(D3D10_SB_OPERAND_4_COMPONENT_MASK_MODE) |
                     ENCODE_D3D10_SB_OPERAND_4_COMPONENT_MASK(D3D10_SB_OPERAND_4_COMPONENT_MASK_X | D3D10_SB_OPERAND_4_COMPONENT_MASK_Y) |
                     ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_TEMP) |
                     ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_1D) |
                     ENCODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(0, D3D10_SB_OPERAND_INDEX_IMMEDIATE32);
                  // Src0 operand
                  uint32_t src_0_operand_token =
                     ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_4_COMPONENT) |
                     ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(D3D10_SB_OPERAND_4_COMPONENT_SWIZZLE_MODE) |
                     ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SWIZZLE(D3D10_SB_4_COMPONENT_X, D3D10_SB_4_COMPONENT_Y, D3D10_SB_4_COMPONENT_X, D3D10_SB_4_COMPONENT_X) |
                     ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_INPUT) |
                     ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_1D) |
                     ENCODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(0, D3D10_SB_OPERAND_INDEX_IMMEDIATE32);
                  uint32_t src_1_operand_token =
                     ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_4_COMPONENT) |
                     ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(D3D10_SB_OPERAND_4_COMPONENT_SWIZZLE_MODE) |
                     ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SWIZZLE(D3D10_SB_4_COMPONENT_W, D3D10_SB_4_COMPONENT_W, D3D10_SB_4_COMPONENT_W, D3D10_SB_4_COMPONENT_W) |
                     ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_INPUT) |
                     ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_1D) |
                     ENCODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(0, D3D10_SB_OPERAND_INDEX_IMMEDIATE32);

                  shader_patch.insert(shader_patch.end(), {opcode_token,
                                                             dest_0_operand_token, 0,
                                                             src_0_operand_token, coord_input_register,
                                                             src_1_operand_token, coord_input_register});
               }
               {
                  uint32_t opcode_token =
                     ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DIV) |
                     ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7) |
                     ENCODE_D3D10_SB_INSTRUCTION_SATURATE(false);
                  // Dest0 operand
                  uint32_t dest_0_operand_token =
                     ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_4_COMPONENT) |
                     ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(D3D10_SB_OPERAND_4_COMPONENT_MASK_MODE) |
                     ENCODE_D3D10_SB_OPERAND_4_COMPONENT_MASK(D3D10_SB_OPERAND_4_COMPONENT_MASK_X | D3D10_SB_OPERAND_4_COMPONENT_MASK_Y) |
                     ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_TEMP) |
                     ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_1D) |
                     ENCODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(0, D3D10_SB_OPERAND_INDEX_IMMEDIATE32);
                  // Src0 operand
                  uint32_t src_0_operand_token =
                     ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_4_COMPONENT) |
                     ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(D3D10_SB_OPERAND_4_COMPONENT_SWIZZLE_MODE) |
                     ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SWIZZLE(D3D10_SB_4_COMPONENT_X, D3D10_SB_4_COMPONENT_Y, D3D10_SB_4_COMPONENT_X, D3D10_SB_4_COMPONENT_X) |
                     ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_INPUT) |
                     ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_1D) |
                     ENCODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(0, D3D10_SB_OPERAND_INDEX_IMMEDIATE32);
                  uint32_t src_1_operand_token =
                     ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_4_COMPONENT) |
                     ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(D3D10_SB_OPERAND_4_COMPONENT_SWIZZLE_MODE) |
                     ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SWIZZLE(D3D10_SB_4_COMPONENT_W, D3D10_SB_4_COMPONENT_W, D3D10_SB_4_COMPONENT_W, D3D10_SB_4_COMPONENT_W) |
                     ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_INPUT) |
                     ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_1D) |
                     ENCODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(0, D3D10_SB_OPERAND_INDEX_IMMEDIATE32);

                  shader_patch.insert(shader_patch.end(), {opcode_token,
                                                             dest_0_operand_token, 1,
                                                             src_0_operand_token, prev_coord_input_register,
                                                             src_1_operand_token, prev_coord_input_register});
               }
               {
                  uint32_t opcode_token =
                     ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ADD) |
                     ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(8) |
                     ENCODE_D3D10_SB_INSTRUCTION_SATURATE(false);
                  // Dest0 operand
                  uint32_t dest_0_operand_token =
                     ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_4_COMPONENT) |
                     ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(D3D10_SB_OPERAND_4_COMPONENT_MASK_MODE) |
                     ENCODE_D3D10_SB_OPERAND_4_COMPONENT_MASK(D3D10_SB_OPERAND_4_COMPONENT_MASK_X | D3D10_SB_OPERAND_4_COMPONENT_MASK_Y) |
                     ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_OUTPUT) |
                     ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_1D) |
                     ENCODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(0, D3D10_SB_OPERAND_INDEX_IMMEDIATE32);
                  // Src0 operand
                  uint32_t src_0_operand_token =
                     ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_4_COMPONENT) |
                     ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(D3D10_SB_OPERAND_4_COMPONENT_SWIZZLE_MODE) |
                     ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SWIZZLE(D3D10_SB_4_COMPONENT_X, D3D10_SB_4_COMPONENT_Y, D3D10_SB_4_COMPONENT_X, D3D10_SB_4_COMPONENT_X) |
                     ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_TEMP) |
                     ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_1D) |
                     ENCODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(0, D3D10_SB_OPERAND_INDEX_IMMEDIATE32) |
                     ENCODE_D3D10_SB_OPERAND_EXTENDED(true);
                  uint32_t src_1_operand_token =
                     ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_4_COMPONENT) |
                     ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(D3D10_SB_OPERAND_4_COMPONENT_SWIZZLE_MODE) |
                     ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SWIZZLE(D3D10_SB_4_COMPONENT_X, D3D10_SB_4_COMPONENT_Y, D3D10_SB_4_COMPONENT_X, D3D10_SB_4_COMPONENT_X) |
                     ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_TEMP) |
                     ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_1D) |
                     ENCODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(0, D3D10_SB_OPERAND_INDEX_IMMEDIATE32);

                  shader_patch.insert(shader_patch.end(), {opcode_token,
                                                             dest_0_operand_token, 5,
                                                             src_0_operand_token, ENCODE_D3D10_SB_EXTENDED_OPERAND_MODIFIER(D3D10_SB_OPERAND_MODIFIER_NEG), 1,
                                                             src_1_operand_token, 0});
               }

               // opaque material - not alpha blended
               // mov o5.w, l(0)
               if (!alpha_written)
               {
                  uint32_t opcode_token =
                     ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
                     ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5) |
                     ENCODE_D3D10_SB_INSTRUCTION_SATURATE(false);

                  // Dest0 operand
                  uint32_t dest_0_operand_token =
                     ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_4_COMPONENT) |
                     ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(D3D10_SB_OPERAND_4_COMPONENT_MASK_MODE) |
                     ENCODE_D3D10_SB_OPERAND_4_COMPONENT_MASK(D3D10_SB_OPERAND_4_COMPONENT_MASK_W) |
                     ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_OUTPUT) |
                     ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_1D) |
                     ENCODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(0, D3D10_SB_OPERAND_INDEX_IMMEDIATE32);
                  // Src operand
                  uint32_t src_operand_token =
                     ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_IMMEDIATE32) |
                     ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_1_COMPONENT);

                  shader_patch.insert(shader_patch.end(), {opcode_token,
                                                             dest_0_operand_token, 5,
                                                             src_operand_token, 0});
               }
               if (opcode_type == D3D10_SB_OPCODE_RET)
               {
                  shader_code.insert(shader_code.begin() + dxbc_header->chunk_offsets[i] + pos, (std::byte*)&shader_patch[0], (std::byte*)(&shader_patch[0] + shader_patch.size()));
               }
               else
               {
                  shader_code.insert(shader_code.begin() + dxbc_header->chunk_offsets[i] + pos + len * 4, (std::byte*)&shader_patch[0], (std::byte*)(&shader_patch[0] + shader_patch.size()));
               }
               shex = &shader_code[dxbc_header->chunk_offsets[i]];
               shex_header = (SHEXHeader*)shex;
               shex_header->chunk_size += shader_patch.size() * sizeof(uint32_t);
               shex_header->dword_count += shader_patch.size();
               dxbc_header = (DXBCHeader*)&shader_code[0];
               for (uint32_t j = i + 1; j < dxbc_header->chunk_count; ++j)
               {
                  dxbc_header->chunk_offsets[j] += shader_patch.size() * sizeof(uint32_t);
               }
               break;
            }

            prev_opcode_type = opcode_type;
            pos += len * 4;
         }
      }
   }
   dxbc_header->file_size = shader_code.size();
   Hash::MD5::Digest md5_digest = CalcDXBCHash(shader_code.data(), shader_code.size());
   std::memcpy(&dxbc_header->hash, &md5_digest.data, DXBCHeader::hash_size);

   return true;
}

std::unique_ptr<std::byte[]> PatchConstantVertexShader(const std::byte* code, size_t size, const std::byte* shader_object, size_t shader_object_size)
{
   const DXBCHeader* dxbc_header = (const DXBCHeader*)shader_object;
   uint32_t system_cb_scene_register = 0xFFFFFFFF;
   uint32_t prev_proj_offset = 0xFFFFFFFF;
   uint32_t prev_view_proj_offset = 0xFFFFFFFF;

   for (uint32_t i = 0; i < dxbc_header->chunk_count; ++i)
   {
      if (strncmp((const char*)&shader_object[dxbc_header->chunk_offsets[i]], "RDEF", 4) == 0)
      {
         const std::byte* rdef = &shader_object[dxbc_header->chunk_offsets[i]];

         const RDEFHeader* rdef_header = (const RDEFHeader*)rdef;

         for (uint32_t j = 0; j < rdef_header->resource_binding_count; ++j)
         {
            const ResourceBindingDesc* resource_binding_desc = (ResourceBindingDesc*)(rdef + 8 + rdef_header->resource_binding_offset + j * sizeof(ResourceBindingDesc));
            if (resource_binding_desc->input_type == 0 &&
                strncmp((const char*)(rdef + 8 + resource_binding_desc->name_offset), "SystemCbScene", 13) == 0)
            {
               system_cb_scene_register = resource_binding_desc->bind_point;
            }
         }

         for (uint32_t j = 0; j < rdef_header->constant_buffer_count; ++j)
         {
            const ConstantBufferDesc* constant_buffer_desc = (ConstantBufferDesc*)(rdef + 8 + rdef_header->constant_buffer_offset + j * sizeof(ConstantBufferDesc));
            if (strncmp((const char*)(rdef + 8 + constant_buffer_desc->name_offset), "SystemCbScene", 13) == 0)
            {
               for (uint32_t k = 0; k < constant_buffer_desc->variable_count; ++k)
               {
                  const VariableDesc* variable = (const VariableDesc*)(rdef + 8 + constant_buffer_desc->variable_desc_offset + k * sizeof(VariableDesc));
                  if (strncmp((const char*)(rdef + 8 + variable->name_offset), "gMtxPrevProj", 12) == 0)
                  {
                     prev_proj_offset = variable->data_offset;
                  }
                  else if (strncmp((const char*)(rdef + 8 + variable->name_offset), "gMtxPrevViewProj", 16) == 0)
                  {
                     prev_view_proj_offset = variable->data_offset;
                  }
               }
            }
         }
      }
   }

   if (system_cb_scene_register == 0xFFFFFFFF ||
       prev_proj_offset == 0xFFFFFFFF ||
       prev_view_proj_offset == 0xFFFFFFFF)
   {
      return nullptr;
   }

   std::unique_ptr<std::byte[]> patched_shex(std::make_unique<std::byte[]>(size));
   memcpy(patched_shex.get(), code, size);

   uint32_t pos = 0;

   for (;;)
   {
      D3D10_SB_OPCODE_TYPE opcode_type = DECODE_D3D10_SB_OPCODE_TYPE(*(uint32_t*)(patched_shex.get() + pos));
      const uint32_t len = max(DECODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(*(uint32_t*)(patched_shex.get() + pos)), 1);

      if (opcode_type == D3D10_SB_OPCODE_DP4)
      {
         D3D10_SB_OPERAND_TYPE operand_type = DECODE_D3D10_SB_OPERAND_TYPE(*(uint32_t*)(patched_shex.get() + pos + 20));

         if (operand_type == D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER)
         {
            uint32_t operand_binding = *(uint32_t*)(patched_shex.get() + pos + 24);
            uint32_t operand_offset = *(uint32_t*)(patched_shex.get() + pos + 28);
            if (operand_binding == system_cb_scene_register && operand_offset * 16 == prev_view_proj_offset)
            {
               *(uint32_t*)(patched_shex.get() + pos + 28) = prev_proj_offset / 16;
            }
            else if (operand_binding == system_cb_scene_register && operand_offset * 16 == prev_view_proj_offset + 16)
            {
               *(uint32_t*)(patched_shex.get() + pos + 28) = (prev_proj_offset / 16) + 1;
            }
            else if (operand_binding == system_cb_scene_register && operand_offset * 16 == prev_view_proj_offset + 32)
            {
               *(uint32_t*)(patched_shex.get() + pos + 28) = (prev_proj_offset / 16) + 2;
            }
            else if (operand_binding == system_cb_scene_register && operand_offset * 16 == prev_view_proj_offset + 48)
            {
               *(uint32_t*)(patched_shex.get() + pos + 28) = (prev_proj_offset / 16) + 3;
            }
         }
      }

      if (pos + len * 4 >= size)
      {
         break;
      }

      pos += len * 4;
   }

   return patched_shex;
}