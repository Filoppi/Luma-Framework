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

// Add write to motion vector texture to pixel shader, we get the registers from the vertex shader
void PatchPixelShader(std::vector<std::byte>& shader_code, uint32_t coord_input_register, uint32_t prev_coord_input_register)
{
   DXBCHeader* dxbc_header = (DXBCHeader*)&shader_code[0];
   for (uint32_t i = 0; i < dxbc_header->chunk_count; ++i)
   {
      if (strncmp((const char*)&shader_code[dxbc_header->chunk_offsets[i]], "ISGN", 4) == 0)
      {
         std::byte* isgn = &shader_code[dxbc_header->chunk_offsets[i]];
         uint32_t isgn_entry_count = *(uint32_t*)(isgn + 8);
         DXBCSignatureEntry* isgn_entries = (DXBCSignatureEntry*)(isgn + 16);

         for (uint32_t j = 0; j < isgn_entry_count; ++j)
         {
            if (isgn_entries[j].reg == coord_input_register ||
                isgn_entries[j].reg == prev_coord_input_register)
            {
               isgn_entries[j].read_write_mask = 11;
            }
         }
      }
      else if (strncmp((const char*)&shader_code[dxbc_header->chunk_offsets[i]], "OSGN", 4) == 0)
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
         motion_vector_signature.component_mask = 15;
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

         D3D10_SB_OPCODE_TYPE prev_opcode_type = D3D10_SB_NUM_OPCODES;
         bool need_coord_input_register_input_dcl = true;
         bool need_prev_coord_input_register_input_dcl = true;
         for (;;)
         {
            D3D10_SB_OPCODE_TYPE opcode_type = DECODE_D3D10_SB_OPCODE_TYPE(*(uint32_t*)(shex + pos));
            const uint32_t len = DECODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(*(uint32_t*)(shex + pos));

            if (opcode_type == D3D10_SB_OPCODE_DCL_INPUT_PS)
            {
               uint32_t reg = *(uint32_t*)(shex + pos + 8);
               if (reg == coord_input_register ||
                   reg == prev_coord_input_register)
               {
                  if (reg == coord_input_register)
                  {
                     need_coord_input_register_input_dcl = false;
                  }
                  if (reg == prev_coord_input_register)
                  {
                     need_prev_coord_input_register_input_dcl = false;
                  }
                  *(uint32_t*)(shex + pos + 4) |= ENCODE_D3D10_SB_OPERAND_4_COMPONENT_MASK(0xB0);
               }
            }

            if (prev_opcode_type == D3D10_SB_OPCODE_DCL_INPUT_PS &&
                opcode_type != D3D10_SB_OPCODE_DCL_INPUT_PS)
            {
               uint32_t opcode_token =
                  ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DCL_INPUT_PS) |
                  ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3) |
                  ENCODE_D3D10_SB_INSTRUCTION_SATURATE(false) |
                  ENCODE_D3D10_SB_INPUT_INTERPOLATION_MODE(D3D10_SB_INTERPOLATION_LINEAR);
               uint32_t operand_token =
                  ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_4_COMPONENT) |
                  ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(D3D10_SB_OPERAND_4_COMPONENT_MASK_MODE) |
                  ENCODE_D3D10_SB_OPERAND_4_COMPONENT_MASK(0xB0) |
                  ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_INPUT) |
                  ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_1D) |
                  ENCODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(0, D3D10_SB_OPERAND_INDEX_IMMEDIATE32);

               std::vector<uint32_t> shader_patch;
               if (need_coord_input_register_input_dcl)
               {
                  shader_patch.insert(shader_patch.end(), {opcode_token,
                                                             operand_token, coord_input_register});
               }
               if (need_prev_coord_input_register_input_dcl)
               {
                  shader_patch.insert(shader_patch.end(), {opcode_token,
                                                             operand_token, prev_coord_input_register});
               }
               shader_code.insert(shader_code.begin() + dxbc_header->chunk_offsets[i] + pos, (std::byte*)&shader_patch[0], (std::byte*)(&shader_patch[0] + shader_patch.size()));
               shex = &shader_code[dxbc_header->chunk_offsets[i]];
               shex_header = (SHEXHeader*)shex;
               shex_header->chunk_size += shader_patch.size() * sizeof(uint32_t);
               shex_header->dword_count += shader_patch.size();
               dxbc_header = (DXBCHeader*)&shader_code[0];
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
                  ENCODE_D3D10_SB_OPERAND_4_COMPONENT_MASK(0x30) |
                  ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_OUTPUT) |
                  ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_1D) |
                  ENCODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(0, D3D10_SB_OPERAND_INDEX_IMMEDIATE32);

               std::vector<uint32_t> shader_patch{
                  opcode_token,
                  operand_token, 5};
               shader_code.insert(shader_code.begin() + dxbc_header->chunk_offsets[i] + pos, (std::byte*)&shader_patch[0], (std::byte*)(&shader_patch[0] + shader_patch.size()));
               shex = &shader_code[dxbc_header->chunk_offsets[i]];
               shex_header = (SHEXHeader*)shex;
               shex_header->chunk_size += shader_patch.size() * sizeof(uint32_t);
               shex_header->dword_count += shader_patch.size();
               dxbc_header = (DXBCHeader*)&shader_code[0];
               for (uint32_t j = i + 1; j < dxbc_header->chunk_count; ++j)
               {
                  dxbc_header->chunk_offsets[j] += shader_patch.size() * sizeof(uint32_t);
               }
               prev_opcode_type = D3D10_SB_NUM_OPCODES;
               pos += shader_patch.size() * sizeof(uint32_t);
               continue;
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
                     ENCODE_D3D10_SB_OPERAND_4_COMPONENT_MASK(0x30) |
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
                     ENCODE_D3D10_SB_OPERAND_4_COMPONENT_MASK(0x30) |
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
                     ENCODE_D3D10_SB_OPERAND_4_COMPONENT_MASK(0x30) |
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
}

// Create vertex shader variation for skinned meshes that loads previous vertex position from buffer
// and find the output register for the previous ndc coordinates used by PatchPixelShader
void PatchVertexShader(std::vector<std::byte>& shader_code, uint32_t& prev_coord_output_register)
{
   DXBCHeader* dxbc_header = (DXBCHeader*)&shader_code[0];
   uint32_t vertex_id_register = 0;
   prev_coord_output_register = 0xFFFFFFFF;

   for (uint32_t i = 0; i < dxbc_header->chunk_count; ++i)
   {
      if (strncmp((const char*)&shader_code[dxbc_header->chunk_offsets[i]], "RDEF", 4) == 0)
      {
         // cbuffer GFD_VSCONST_SKIN_CACHE : register(b9)
         //{
         //     uint offset;
         //     uint stride;
         // }
         // ByteAddressBuffer CachedSkinVertices : register(t1);
         std::byte* rdef = &shader_code[dxbc_header->chunk_offsets[i]];

         RDEFHeader rdef_header = *(RDEFHeader*)rdef;
         uint32_t rd11_section[8];
         memcpy(rd11_section, rdef + sizeof(RDEFHeader), 8 * sizeof(uint32_t));

         std::unordered_map<uint32_t, std::string> binding_names;
         binding_names[0] = "CachedSkinVertices";
         binding_names[1] = "GFD_VSCONST_SKIN_CACHE";

         std::vector<ResourceBindingDesc> resource_binding_descs;
         resource_binding_descs.reserve(rdef_header.resource_binding_count + 2);
         for (uint32_t j = 0; j < rdef_header.resource_binding_count; ++j)
         {
            ResourceBindingDesc resource_binding_desc = *(ResourceBindingDesc*)(rdef + 8 + rdef_header.resource_binding_offset + j * sizeof(ResourceBindingDesc));
            binding_names[resource_binding_desc.name_offset] = (const char*)rdef + 8 + resource_binding_desc.name_offset;
            resource_binding_descs.push_back(resource_binding_desc);
         }

         uint32_t variables_offset = 0xFFFFFFFF;
         std::vector<ConstantBufferDesc> constant_buffer_descs;
         constant_buffer_descs.reserve(rdef_header.constant_buffer_count + 1);
         for (uint32_t j = 0; j < rdef_header.constant_buffer_count; ++j)
         {
            ConstantBufferDesc constant_buffer_desc = *(ConstantBufferDesc*)(rdef + 8 + rdef_header.constant_buffer_offset + j * sizeof(ConstantBufferDesc));
            variables_offset = min(variables_offset, constant_buffer_desc.variable_desc_offset + 8);
            constant_buffer_descs.push_back(constant_buffer_desc);
         }

         uint32_t variables_size = rdef_header.creator_string_offset + 8 - variables_offset;
         uint32_t creator_string_length = rdef_header.chunk_size - rdef_header.creator_string_offset;

         {
            ResourceBindingDesc vertex_buffer_binding_desc = {};
            vertex_buffer_binding_desc.name_offset = 0;
            vertex_buffer_binding_desc.input_type = 7;
            vertex_buffer_binding_desc.resource_return_type = 6;
            vertex_buffer_binding_desc.view_dimension = 1;
            vertex_buffer_binding_desc.sample_count = 0;
            vertex_buffer_binding_desc.bind_point = 1;
            vertex_buffer_binding_desc.bind_count = 1;
            vertex_buffer_binding_desc.flags = 1;
            resource_binding_descs.push_back(vertex_buffer_binding_desc);
         }
         {
            ResourceBindingDesc cbuffer_binding_desc = {};
            cbuffer_binding_desc.name_offset = 1;
            cbuffer_binding_desc.input_type = 0;
            cbuffer_binding_desc.resource_return_type = 0;
            cbuffer_binding_desc.view_dimension = 0;
            cbuffer_binding_desc.sample_count = 0;
            cbuffer_binding_desc.bind_point = 9;
            cbuffer_binding_desc.bind_count = 1;
            cbuffer_binding_desc.flags = 1;
            resource_binding_descs.push_back(cbuffer_binding_desc);
         }
         {
            ConstantBufferDesc cbuffer_desc = {};
            cbuffer_desc.name_offset = 1;
            cbuffer_desc.variable_count = 2;
            cbuffer_desc.variable_desc_offset = 0;
            cbuffer_desc.size = 8;
            cbuffer_desc.flags = 0;
            cbuffer_desc.cbuffer_type = 0;
            constant_buffer_descs.push_back(cbuffer_desc);
         }

         uint32_t binding_names_size = 0;
         for (const auto& it : binding_names)
         {
            binding_names_size += it.second.length() + 1;
         }

         const char offset_name[] = "offset";
         const char stride_name[] = "stride";
         const char type_name[] = "dword";

         uint32_t added_variables_size = 2 * sizeof(VariableDesc) + sizeof(VariableTypeDesc) + sizeof(offset_name) + sizeof(stride_name) + sizeof(type_name);

         uint32_t new_bindings_offset = sizeof(rdef_header) + sizeof(rd11_section);
         uint32_t new_binding_names_offset = new_bindings_offset + resource_binding_descs.size() * sizeof(ResourceBindingDesc);
         uint32_t new_constant_buffer_offset = new_binding_names_offset + binding_names_size;
         uint32_t new_variables_offset = new_constant_buffer_offset + constant_buffer_descs.size() * sizeof(ConstantBufferDesc);
         uint32_t added_variables_offset = new_variables_offset + variables_size;
         uint32_t new_creator_string_offset = added_variables_offset + added_variables_size;
         uint32_t patched_rdef_size = new_creator_string_offset + creator_string_length;

         std::vector<std::byte> patched_rdef;
         patched_rdef.resize(patched_rdef_size);

         RDEFHeader updated_header = rdef_header;
         updated_header.chunk_size = patched_rdef_size - 8;
         updated_header.constant_buffer_offset = new_constant_buffer_offset - 8;
         updated_header.constant_buffer_count += 1;
         updated_header.resource_binding_offset = new_bindings_offset - 8;
         updated_header.resource_binding_count += 2;
         updated_header.creator_string_offset = new_creator_string_offset - 8;

         memcpy(&patched_rdef[0], &updated_header, sizeof(updated_header));
         memcpy(&patched_rdef[sizeof(RDEFHeader)], &rd11_section, sizeof(rd11_section));

         std::unordered_map<uint32_t, uint32_t> new_binding_name_offsets;
         uint32_t current_name_offset = new_binding_names_offset;
         for (const auto& it : binding_names)
         {
            new_binding_name_offsets[it.first] = current_name_offset - 8;
            memcpy(&patched_rdef[current_name_offset], it.second.c_str(), it.second.length() + 1);
            current_name_offset += it.second.length() + 1;
         }
         for (uint32_t j = 0; j < resource_binding_descs.size(); ++j)
         {
            ResourceBindingDesc& resource_binding = resource_binding_descs[j];
            resource_binding.name_offset = new_binding_name_offsets[resource_binding.name_offset];
            memcpy(&patched_rdef[new_bindings_offset + j * sizeof(ResourceBindingDesc)], &resource_binding, sizeof(ResourceBindingDesc));
         }

         memcpy(&patched_rdef[new_variables_offset], rdef + variables_offset, variables_size);
         int32_t patch_variables_offset = new_variables_offset - variables_offset;
         std::set<uint32_t> type_offsets;
         for (uint32_t j = 0; j < constant_buffer_descs.size(); ++j)
         {
            ConstantBufferDesc& constant_buffer = constant_buffer_descs[j];
            constant_buffer.name_offset = new_binding_name_offsets[constant_buffer.name_offset];
            if (constant_buffer.variable_desc_offset == 0)
            {
               constant_buffer.variable_desc_offset = added_variables_offset - 8;
            }
            else
            {
               constant_buffer.variable_desc_offset += patch_variables_offset;
               for (uint32_t k = 0; k < constant_buffer.variable_count; ++k)
               {
                  VariableDesc* variable = (VariableDesc*)&patched_rdef[constant_buffer.variable_desc_offset + 8 + k * sizeof(VariableDesc)];
                  variable->name_offset += patch_variables_offset;
                  variable->type_offset += patch_variables_offset;
                  type_offsets.insert(variable->type_offset);
                  if (variable->default_value_offset != 0)
                  {
                     variable->default_value_offset += patch_variables_offset;
                  }
               }
            }
            memcpy(&patched_rdef[new_constant_buffer_offset + j * sizeof(ConstantBufferDesc)], &constant_buffer, sizeof(ConstantBufferDesc));
         }

         for (const auto it : type_offsets)
         {
            VariableTypeDesc* type = (VariableTypeDesc*)&patched_rdef[it + 8];
            type->name_offset += patch_variables_offset;
         }

         {
            VariableDesc offset_desc = {};
            offset_desc.name_offset = added_variables_offset + 2 * sizeof(VariableDesc) + sizeof(VariableTypeDesc) - 8;
            offset_desc.data_offset = 0;
            offset_desc.size = 4;
            offset_desc.flags = 2;
            offset_desc.type_offset = added_variables_offset + 2 * sizeof(VariableDesc) - 8;
            offset_desc.default_value_offset = 0;
            offset_desc.start_texture = 0xFFFFFFFF;
            offset_desc.texture_size = 0;
            offset_desc.start_sampler = 0xFFFFFFFF;
            offset_desc.sampler_size = 0;
            memcpy(&patched_rdef[added_variables_offset], &offset_desc, sizeof(offset_desc));
         }
         {
            VariableDesc stride_desc = {};
            stride_desc.name_offset = added_variables_offset + 2 * sizeof(VariableDesc) + sizeof(VariableTypeDesc) + sizeof(offset_name) - 8;
            stride_desc.data_offset = 4;
            stride_desc.size = 4;
            stride_desc.flags = 2;
            stride_desc.type_offset = added_variables_offset + 2 * sizeof(VariableDesc) - 8;
            stride_desc.default_value_offset = 0;
            stride_desc.start_texture = 0xFFFFFFFF;
            stride_desc.texture_size = 0;
            stride_desc.start_sampler = 0xFFFFFFFF;
            stride_desc.sampler_size = 0;
            memcpy(&patched_rdef[added_variables_offset + sizeof(VariableDesc)], &stride_desc, sizeof(stride_desc));
         }
         {
            VariableTypeDesc dword_desc = {};
            dword_desc.variable_class = 0;
            dword_desc.variable_type = 19;
            dword_desc.row_count = 1;
            dword_desc.column_count = 1;
            dword_desc.element_count = 0;
            dword_desc.member_count = 0;
            dword_desc.member_offset = 0;
            dword_desc.name_offset = added_variables_offset + 2 * sizeof(VariableDesc) + sizeof(VariableTypeDesc) + sizeof(offset_name) + sizeof(stride_name);
            memcpy(&patched_rdef[added_variables_offset + 2 * sizeof(VariableDesc)], &dword_desc, sizeof(dword_desc));
         }

         memcpy(&patched_rdef[added_variables_offset + 2 * sizeof(VariableDesc) + sizeof(VariableTypeDesc)], &offset_name, sizeof(offset_name));
         memcpy(&patched_rdef[added_variables_offset + 2 * sizeof(VariableDesc) + sizeof(VariableTypeDesc) + sizeof(offset_name)], &stride_name, sizeof(stride_name));
         memcpy(&patched_rdef[added_variables_offset + 2 * sizeof(VariableDesc) + sizeof(VariableTypeDesc) + sizeof(offset_name) + sizeof(stride_name)], &type_name, sizeof(type_name));
         memcpy(&patched_rdef[new_creator_string_offset], rdef + rdef_header.creator_string_offset + 8, creator_string_length);

         shader_code.erase(shader_code.begin() + dxbc_header->chunk_offsets[i], shader_code.begin() + dxbc_header->chunk_offsets[i] + rdef_header.chunk_size + 8);
         dxbc_header = (DXBCHeader*)&shader_code[0];
         shader_code.insert(shader_code.begin() + dxbc_header->chunk_offsets[i], patched_rdef.cbegin(), patched_rdef.cend());

         int32_t size_diff = updated_header.chunk_size - rdef_header.chunk_size;
         dxbc_header = (DXBCHeader*)&shader_code[0];
         for (uint32_t j = i + 1; j < dxbc_header->chunk_count; ++j)
         {
            dxbc_header->chunk_offsets[j] += size_diff;
         }
      }
      else if (strncmp((const char*)&shader_code[dxbc_header->chunk_offsets[i]], "ISGN", 4) == 0)
      {
         // add SV_VertexID at first free vertex register
         std::byte* isgn = &shader_code[dxbc_header->chunk_offsets[i]];
         uint32_t isgn_entry_count = *(uint32_t*)(isgn + 8);
         DXBCSignatureEntry* isgn_entries = (DXBCSignatureEntry*)(isgn + 16);

         uint32_t max_name_offset = 0;
         for (uint32_t j = 0; j < isgn_entry_count; ++j)
         {
            max_name_offset = max(max_name_offset, isgn_entries[j].name_offset);
            isgn_entries[j].name_offset += sizeof(DXBCSignatureEntry);
         }

         uint32_t string_section_end = max_name_offset + strlen((const char*)isgn + 8 + max_name_offset) + 1;

         vertex_id_register = isgn_entry_count;

         DXBCSignatureEntry vertex_id_signature;
         vertex_id_signature.name_offset = string_section_end + sizeof(DXBCSignatureEntry);
         vertex_id_signature.semantic_index = 0;
         vertex_id_signature.value_type = 6;
         vertex_id_signature.component_type = 1;
         vertex_id_signature.reg = vertex_id_register;
         vertex_id_signature.component_mask = 1;
         vertex_id_signature.read_write_mask = 1;

         const char vertex_id_name[] = "SV_VertexID";

         *(uint32_t*)(isgn + 4) += sizeof(DXBCSignatureEntry) + sizeof(vertex_id_name);
         *(uint32_t*)(isgn + 8) += 1;

         shader_code.insert(shader_code.begin() + dxbc_header->chunk_offsets[i] + 8 + string_section_end,
            (std::byte*)vertex_id_name,
            (std::byte*)vertex_id_name + sizeof(vertex_id_name));
         shader_code.insert(shader_code.begin() + dxbc_header->chunk_offsets[i] + 16 + isgn_entry_count * sizeof(DXBCSignatureEntry),
            (std::byte*)&vertex_id_signature,
            (std::byte*)&vertex_id_signature + sizeof(DXBCSignatureEntry));
         dxbc_header = (DXBCHeader*)&shader_code[0];
         for (uint32_t j = i + 1; j < dxbc_header->chunk_count; ++j)
         {
            dxbc_header->chunk_offsets[j] += sizeof(DXBCSignatureEntry) + sizeof(vertex_id_name);
         }
      }
      else if (strncmp((const char*)&shader_code[dxbc_header->chunk_offsets[i]], "SHEX", 4) == 0)
      {
         std::byte* shex = &shader_code[dxbc_header->chunk_offsets[i]];
         SHEXHeader* shex_header = (SHEXHeader*)shex;

         uint32_t pos = 16;

         D3D10_SB_OPCODE_TYPE prev_opcode_type = D3D10_SB_NUM_OPCODES;
         for (;;)
         {
            D3D10_SB_OPCODE_TYPE opcode_type = DECODE_D3D10_SB_OPCODE_TYPE(*(uint32_t*)(shex + pos));
            const uint32_t len = DECODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(*(uint32_t*)(shex + pos));

            if (prev_opcode_type == D3D10_SB_OPCODE_DCL_CONSTANT_BUFFER &&
                opcode_type != D3D10_SB_OPCODE_DCL_CONSTANT_BUFFER)
            {
               std::vector<uint32_t> shader_patch;
               {
                  uint32_t opcode_token =
                     ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DCL_CONSTANT_BUFFER) |
                     ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(4) |
                     ENCODE_D3D10_SB_INSTRUCTION_SATURATE(false);
                  uint32_t operand_token =
                     ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_4_COMPONENT) |
                     ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(D3D10_SB_OPERAND_4_COMPONENT_SWIZZLE_MODE) |
                     ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SWIZZLE(D3D10_SB_4_COMPONENT_X, D3D10_SB_4_COMPONENT_Y, D3D10_SB_4_COMPONENT_Z, D3D10_SB_4_COMPONENT_W) |
                     ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER) |
                     ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_2D) |
                     ENCODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(0, D3D10_SB_OPERAND_INDEX_IMMEDIATE32);

                  shader_patch.insert(shader_patch.end(), {opcode_token,
                                                             operand_token, 9, 1});
               }

               {
                  uint32_t opcode_token =
                     ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_DCL_RESOURCE_RAW) |
                     ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3) |
                     ENCODE_D3D10_SB_INSTRUCTION_SATURATE(false);
                  uint32_t operand_token =
                     ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_0_COMPONENT) |
                     ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(D3D10_SB_OPERAND_4_COMPONENT_MASK_MODE) |
                     ENCODE_D3D10_SB_OPERAND_4_COMPONENT_MASK(0x00) |
                     ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_RESOURCE) |
                     ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_1D) |
                     ENCODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(0, D3D10_SB_OPERAND_INDEX_IMMEDIATE32);

                  shader_patch.insert(shader_patch.end(), {opcode_token,
                                                             operand_token, 1});
               }

               shader_code.insert(shader_code.begin() + dxbc_header->chunk_offsets[i] + pos, (std::byte*)&shader_patch[0], (std::byte*)(&shader_patch[0] + shader_patch.size()));
               shex = &shader_code[dxbc_header->chunk_offsets[i]];
               shex_header = (SHEXHeader*)shex;
               shex_header->chunk_size += shader_patch.size() * sizeof(uint32_t);
               shex_header->dword_count += shader_patch.size();
               dxbc_header = (DXBCHeader*)&shader_code[0];
               for (uint32_t j = i + 1; j < dxbc_header->chunk_count; ++j)
               {
                  dxbc_header->chunk_offsets[j] += shader_patch.size() * sizeof(uint32_t);
               }
               prev_opcode_type = D3D10_SB_NUM_OPCODES;
               pos += shader_patch.size() * 4;
            }
            else if (prev_opcode_type == D3D10_SB_OPCODE_DCL_INPUT &&
                     opcode_type != D3D10_SB_OPCODE_DCL_INPUT)
            {
               {
                  uint32_t opcode_token =
                     ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DCL_INPUT_SGV) |
                     ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(4) |
                     ENCODE_D3D10_SB_INSTRUCTION_SATURATE(false);
                  uint32_t operand_token =
                     ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_4_COMPONENT) |
                     ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(D3D10_SB_OPERAND_4_COMPONENT_MASK_MODE) |
                     ENCODE_D3D10_SB_OPERAND_4_COMPONENT_MASK(0x10) |
                     ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_INPUT) |
                     ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_1D) |
                     ENCODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(0, D3D10_SB_OPERAND_INDEX_IMMEDIATE32);

                  std::vector<uint32_t> shader_patch;
                  shader_patch.insert(shader_patch.end(), {opcode_token,
                                                             operand_token, vertex_id_register, 6});

                  shader_code.insert(shader_code.begin() + dxbc_header->chunk_offsets[i] + pos, (std::byte*)&shader_patch[0], (std::byte*)(&shader_patch[0] + shader_patch.size()));
                  shex = &shader_code[dxbc_header->chunk_offsets[i]];
                  shex_header = (SHEXHeader*)shex;
                  shex_header->chunk_size += shader_patch.size() * sizeof(uint32_t);
                  shex_header->dword_count += shader_patch.size();
                  dxbc_header = (DXBCHeader*)&shader_code[0];
                  for (uint32_t j = i + 1; j < dxbc_header->chunk_count; ++j)
                  {
                     dxbc_header->chunk_offsets[j] += shader_patch.size() * sizeof(uint32_t);
                  }
                  prev_opcode_type = D3D10_SB_NUM_OPCODES;
                  pos += shader_patch.size() * 4;
               }
            }
            else if (opcode_type == D3D10_SB_OPCODE_DP4)
            {
               D3D10_SB_OPERAND_TYPE operand_type_out = DECODE_D3D10_SB_OPERAND_TYPE(*(uint32_t*)(shex + pos + 4));
               D3D10_SB_OPERAND_TYPE operand_type = DECODE_D3D10_SB_OPERAND_TYPE(*(uint32_t*)(shex + pos + 20));

               if (operand_type == D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER)
               {
                  uint32_t operand_binding = *(uint32_t*)(shex + pos + 24);
                  uint32_t operand_offset = *(uint32_t*)(shex + pos + 28);
                  // this is always mtxLocalToWorldViewProjPrev._m00_m10_m20_m30
                  if (operand_binding == 1 && operand_offset == 8)
                  {
                     if (operand_type_out == D3D10_SB_OPERAND_TYPE_OUTPUT)
                     {
                        prev_coord_output_register = *(uint32_t*)(shex + pos + 8);

                        std::vector<uint32_t> shader_patch;
                        // r0.x = mad(SV_VertexID, stride, offset);
                        {
                           uint32_t opcode_token =
                              ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IMAD) |
                              ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(11) |
                              ENCODE_D3D10_SB_INSTRUCTION_SATURATE(false);
                           uint32_t dest_0_operand_token =
                              ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_4_COMPONENT) |
                              ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(D3D10_SB_OPERAND_4_COMPONENT_MASK_MODE) |
                              ENCODE_D3D10_SB_OPERAND_4_COMPONENT_MASK(0x10) |
                              ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_TEMP) |
                              ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_1D) |
                              ENCODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(0, D3D10_SB_OPERAND_INDEX_IMMEDIATE32);
                           uint32_t src_0_operand_token =
                              ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_4_COMPONENT) |
                              ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(D3D10_SB_OPERAND_4_COMPONENT_SELECT_1_MODE) |
                              ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECT_1(0) |
                              ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_INPUT) |
                              ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_1D) |
                              ENCODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(0, D3D10_SB_OPERAND_INDEX_IMMEDIATE32);
                           uint32_t src_1_operand_token =
                              ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_4_COMPONENT) |
                              ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(D3D10_SB_OPERAND_4_COMPONENT_SELECT_1_MODE) |
                              ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECT_1(1) |
                              ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER) |
                              ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_2D) |
                              ENCODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(0, D3D10_SB_OPERAND_INDEX_IMMEDIATE32);
                           uint32_t src_2_operand_token =
                              ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_4_COMPONENT) |
                              ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(D3D10_SB_OPERAND_4_COMPONENT_SELECT_1_MODE) |
                              ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECT_1(0) |
                              ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER) |
                              ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_2D) |
                              ENCODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(0, D3D10_SB_OPERAND_INDEX_IMMEDIATE32);

                           shader_patch.insert(shader_patch.end(), {
                                                                      opcode_token,
                                                                      dest_0_operand_token,
                                                                      0,
                                                                      src_0_operand_token,
                                                                      vertex_id_register,
                                                                      src_1_operand_token,
                                                                      9,
                                                                      0,
                                                                      src_2_operand_token,
                                                                      9,
                                                                      0,
                                                                   });
                        }
                        // r0.xyz = asfloat(CachedSkinVertices.Load4(r0.x)).xyz;
                        {
                           uint32_t opcode_token =
                              ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_LD_RAW) |
                              ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9) |
                              ENCODE_D3D10_SB_INSTRUCTION_SATURATE(false) |
                              ENCODE_D3D10_SB_OPCODE_EXTENDED(true);
                           uint32_t extended_opcode_token_0 =
                              ENCODE_D3D10_SB_EXTENDED_OPCODE_TYPE(D3D11_SB_EXTENDED_OPCODE_RESOURCE_DIM) |
                              ENCODE_D3D11_SB_EXTENDED_RESOURCE_DIMENSION(D3D11_SB_RESOURCE_DIMENSION_RAW_BUFFER) |
                              ENCODE_D3D10_SB_OPCODE_EXTENDED(true);
                           uint32_t extended_opcode_token_1 =
                              ENCODE_D3D10_SB_EXTENDED_OPCODE_TYPE(D3D11_SB_EXTENDED_OPCODE_RESOURCE_RETURN_TYPE) |
                              ENCODE_D3D11_SB_EXTENDED_RESOURCE_RETURN_TYPE(D3D10_SB_RETURN_TYPE_MIXED, 0) |
                              ENCODE_D3D11_SB_EXTENDED_RESOURCE_RETURN_TYPE(D3D10_SB_RETURN_TYPE_MIXED, 1) |
                              ENCODE_D3D11_SB_EXTENDED_RESOURCE_RETURN_TYPE(D3D10_SB_RETURN_TYPE_MIXED, 2) |
                              ENCODE_D3D11_SB_EXTENDED_RESOURCE_RETURN_TYPE(D3D10_SB_RETURN_TYPE_MIXED, 3);
                           uint32_t dest_0_operand_token =
                              ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_4_COMPONENT) |
                              ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(D3D10_SB_OPERAND_4_COMPONENT_MASK_MODE) |
                              ENCODE_D3D10_SB_OPERAND_4_COMPONENT_MASK(0x70) |
                              ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_TEMP) |
                              ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_1D) |
                              ENCODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(0, D3D10_SB_OPERAND_INDEX_IMMEDIATE32);
                           uint32_t src_0_operand_token =
                              ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_4_COMPONENT) |
                              ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(D3D10_SB_OPERAND_4_COMPONENT_SELECT_1_MODE) |
                              ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECT_1(0) |
                              ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_TEMP) |
                              ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_1D) |
                              ENCODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(0, D3D10_SB_OPERAND_INDEX_IMMEDIATE32);
                           uint32_t src_1_operand_token =
                              ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_4_COMPONENT) |
                              ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(D3D10_SB_OPERAND_4_COMPONENT_SWIZZLE_MODE) |
                              ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SWIZZLE(D3D10_SB_4_COMPONENT_X, D3D10_SB_4_COMPONENT_Y, D3D10_SB_4_COMPONENT_Z, D3D10_SB_4_COMPONENT_W) |
                              ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_RESOURCE) |
                              ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_1D) |
                              ENCODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(0, D3D10_SB_OPERAND_INDEX_IMMEDIATE32);

                           shader_patch.insert(shader_patch.end(), {opcode_token, extended_opcode_token_0, extended_opcode_token_1,
                                                                      dest_0_operand_token, 0,
                                                                      src_0_operand_token, 0,
                                                                      src_1_operand_token, 1});
                        }
                        // r0.w = 1.0f;
                        {
                           uint32_t opcode_token =
                              ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
                              ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5) |
                              ENCODE_D3D10_SB_INSTRUCTION_SATURATE(false);
                           uint32_t dest_operand_token =
                              ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_4_COMPONENT) |
                              ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(D3D10_SB_OPERAND_4_COMPONENT_MASK_MODE) |
                              ENCODE_D3D10_SB_OPERAND_4_COMPONENT_MASK(0x80) |
                              ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_TEMP) |
                              ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_1D) |
                              ENCODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(0, D3D10_SB_OPERAND_INDEX_IMMEDIATE32);
                           uint32_t src_operand_token =
                              ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_IMMEDIATE32) |
                              ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_1_COMPONENT);

                           shader_patch.insert(shader_patch.end(), {
                                                                      opcode_token,
                                                                      dest_operand_token, 1,
                                                                      src_operand_token, 0x3F800000 // float-value 1.0f
                                                                   });
                        }

                        shader_code.insert(shader_code.begin() + dxbc_header->chunk_offsets[i] + pos, (std::byte*)&shader_patch[0], (std::byte*)(&shader_patch[0] + shader_patch.size()));
                        shex = &shader_code[dxbc_header->chunk_offsets[i]];
                        shex_header = (SHEXHeader*)shex;
                        shex_header->chunk_size += shader_patch.size() * sizeof(uint32_t);
                        shex_header->dword_count += shader_patch.size();
                        dxbc_header = (DXBCHeader*)&shader_code[0];
                        for (uint32_t j = i + 1; j < dxbc_header->chunk_count; ++j)
                        {
                           dxbc_header->chunk_offsets[j] += shader_patch.size() * sizeof(uint32_t);
                        }
                        prev_opcode_type = D3D10_SB_NUM_OPCODES;
                        pos += shader_patch.size() * 4;
                     }
                  }
               }
            }

            if (pos + len * 4 >= shex_header->chunk_size + 8)
            {
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
}