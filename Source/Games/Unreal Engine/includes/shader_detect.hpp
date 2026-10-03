#include <set>
#include <include/reshade.hpp>
#include <d3d11TokenizedProgramFormat.hpp>
#include "..\..\..\Core\utils\system.h"

union word_t
{
   float f;
   int32_t i;
   uint32_t u;
   std::byte b[4];
};

struct GlobalCBInfo
{
   size_t size = 0;                           // size of the global cbuffer
   int32_t jitter_index = -1;                 // index of the jitter vector in the global cbuffer (this is unreliable since not all UE4 versions have it)
   int32_t view_to_clip_start_index = -1;     // start index of the clip to view matrix in the global cbuffer
   int32_t view_size_and_inv_size_index = -1; // index of the view size and inverse size vector in the global cbuffer
   int32_t clip_to_prev_clip_start_index = -1;
};

struct TAAShaderInfo
{
   size_t declared_cbuffer_size;
   size_t max_texture_register = 16;
   int32_t global_buffer_register_index = -1;
   int32_t clip_to_prev_clip_start_index = -1;
   int32_t source_texture_register = -1;
   int32_t depth_texture_register = -1;
   int32_t velocity_texture_register = -1;
   bool found_all = false;
   bool has_multiple_render_targets = false;
};

struct SSAOShaderInfo
{
   int32_t global_buffer_register_index = -1;
   bool found = false;
};

// Dithering shader detection and modification structures
enum class DitheringType
{
   None = 0,
   Texture_ScreenSpace,  // Screen-space texture sampling with noise texture
                         // Pattern 1 (64x64):  SV_Position offset -> MUL 0.015625 (1/64) -> SAMPLE
                         // Pattern 2 (128x128): MAD with 17/89 lattice -> MUL 0.0078125 (1/128) -> SAMPLE_L
};

// Sample instruction info with pre-parsed offsets for modification
struct SampleInstructionInfo
{
   size_t instruction_offset = 0;      // Offset in bytes from start of bytecode
   uint32_t instruction_length = 0;    // Length of instruction in DWORDs
   uint32_t opcode = 0;                // Original opcode (SAMPLE, SAMPLE_B, SAMPLE_L)
   uint32_t dest_operand_token = 0;    // Destination operand token (for building MOV)
   uint32_t dest_register = 0;         // Destination register index
   uint32_t ext_offset = 0;            // Number of extended opcode tokens
};

struct DitheringShaderInfo
{
   DitheringType type = DitheringType::None;
   
   // Texture info
   int32_t noise_texture_register = -1;      // Register index of the noise texture (t17 in BL3, t0 in simplex)
   uint32_t noise_texture_size = 0;          // Detected texture size (64 or 128)
   
   // Sample instructions to modify (there can be multiple samples from the same noise texture)
   std::vector<SampleInstructionInfo> sample_instructions;
   
   // UV scale instruction info (for potential temporal offset injection)
   size_t uv_scale_instruction_offset = 0;   // Offset to MUL with UV scale instruction (bytes)
   uint32_t uv_scale_instruction_length = 0; // Length of the MUL instruction in DWORDs
   
   // Cbuffer info
   int32_t injected_cbuffer_slot = -1;       // Cbuffer slot we injected for frame index (-1 if none available)
   
   // Flags
   bool has_discard = false;                 // True if shader uses discard (needs temporal randomization)
   bool modification_supported = false;      // True if we can modify this shader
};


static uint32_t* FindLargestCBufferDeclaration(const uint32_t* code_u32, const size_t size_u32)
{
   uint32_t offset = 0;
   size_t max_cbuffer_size = 0;
   size_t instruction_count = 0;
   bool found_first_dcl_cbuffer = false;
   uint32_t* max_cbuffer_declaration = nullptr;
   while (offset < size_u32)
   {
      if (instruction_count > 16 && !found_first_dcl_cbuffer)
         break; // bail out if we reached too far without finding any cbuffer declarations
      const uint32_t token = code_u32[offset];
      const uint32_t opcode = DECODE_D3D10_SB_OPCODE_TYPE(token);
      uint32_t len = DECODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(token);
      len = len == 0 ? 1 : len;

      if (opcode == D3D10_SB_OPCODE_DCL_CONSTANT_BUFFER)
      {
         found_first_dcl_cbuffer = true;
         // operand 0 is the cbuffer index
         const uint32_t* operand_start = code_u32 + offset + 1;
         const uint32_t buffer_size = operand_start[2];
         if (buffer_size > max_cbuffer_size)
         {
            max_cbuffer_size = buffer_size;
            max_cbuffer_declaration = const_cast<uint32_t*>(code_u32 + offset);
         }
      }
      else
      {
         if (found_first_dcl_cbuffer)
            break; // we scanned all cbuffer declarations
      }
      instruction_count++;
      // "dcl_immediateConstantBuffer" is opcode 53, which the WDK header this project
      // ships (Source/External/WDK/includes/d3d11TokenizedProgramFormat.hpp) spells
      // D3D10_SB_OPCODE_CUSTOMDATA -- there is no
      // D3D10_SB_OPCODE_DCL_IMMEDIATE_CONSTANT_BUFFER in it. The ICB form is
      // custom-data class 3. Since opcode token bits [31:11] carry the class, the
      // usual instruction length field ([30:24]) decodes to 0 for an ICB token
      // (0x00001835 = 53 | (3 << 11)), and "len == 0 ? 1 : len" would step 1 DWORD
      // into the ICB payload. The declaration's real size in DWORDs -- opcode token
      // included -- is the DWORD right after it, so it can be used as a plain stride.
      if (opcode == D3D10_SB_OPCODE_CUSTOMDATA &&
          DECODE_D3D10_SB_CUSTOMDATA_CLASS(token) == D3D10_SB_CUSTOMDATA_DCL_IMMEDIATE_CONSTANT_BUFFER &&
          offset + 1 < size_u32)
         offset += code_u32[offset + 1];
      else
         offset += len;
   }
   return max_cbuffer_declaration;
}

static float GetTAAShaderConfidence(const std::byte* code, size_t size)
{
   const uint32_t* code_u32 = reinterpret_cast<const uint32_t*>(code);
   const size_t size_u32 = size / sizeof(uint32_t);

   // 1. Scan for Opcodes
   size_t min_max_count = 0;
   bool has_gather4 = false;
   bool has_lds_op = false;

   bool has_1_5 = false;
   bool has_2_5 = false;
   bool has_ycocg = false;

   size_t offset = 0;
   size_t instruction_count = 0;

   while (offset < size_u32)
   {
      const uint32_t token = code_u32[offset];
      const uint32_t opcode = DECODE_D3D10_SB_OPCODE_TYPE(token);
      uint32_t len = DECODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(token);
      len = len == 0 ? 1 : len; // Safety

      if (offset + len > size_u32)
         break;

      switch (opcode)
      {
      case D3D10_SB_OPCODE_MIN:
      case D3D10_SB_OPCODE_MAX:
         min_max_count++;
         break;
      case D3D10_1_SB_OPCODE_GATHER4:
      case D3D11_SB_OPCODE_GATHER4_PO_C:
      case D3D11_SB_OPCODE_GATHER4_PO:
      case D3D11_SB_OPCODE_GATHER4_C:
         has_gather4 = true;
         break;
      case D3D11_SB_OPCODE_STORE_STRUCTURED:
      case D3D11_SB_OPCODE_LD_STRUCTURED:
         has_lds_op = true;
         break;
      default:
         break;
      }

      // Scan for Immediate Constants in this instruction
      // Start after the opcode token(s)
      size_t operand_offset = 1;
      if (DECODE_IS_D3D10_SB_OPCODE_EXTENDED(token))
      {
         operand_offset = 2;
      }

      // Iterate through potential operands
      for (size_t k = operand_offset; k < len; ++k)
      {
         uint32_t operand_token = code_u32[offset + k];

         // Check if it is an immediate operand
         if (DECODE_D3D10_SB_OPERAND_TYPE(operand_token) == D3D10_SB_OPERAND_TYPE_IMMEDIATE32)
         {
            // Handle extended operand tokens
            size_t val_idx = k + 1;
            uint32_t curr_op_tok = operand_token;
            while (DECODE_IS_D3D10_SB_OPERAND_EXTENDED(curr_op_tok) && (val_idx < len))
            {
               curr_op_tok = code_u32[offset + val_idx];
               val_idx++;
            }

            // Determine number of components
            D3D10_SB_OPERAND_NUM_COMPONENTS num_comps_enum = DECODE_D3D10_SB_OPERAND_NUM_COMPONENTS(operand_token);
            int num_comps = 0;
            if (num_comps_enum == D3D10_SB_OPERAND_1_COMPONENT)
               num_comps = 1;
            else if (num_comps_enum == D3D10_SB_OPERAND_4_COMPONENT)
               num_comps = 4;

            if (num_comps > 0 && (val_idx + num_comps <= len))
            {
               const float* values = reinterpret_cast<const float*>(&code_u32[offset + val_idx]);

               for (int i = 0; i < num_comps; ++i)
               {
                  if (values[i] == 1.5f)
                     has_1_5 = true;
                  if (values[i] == 2.5f)
                     has_2_5 = true;
               }

               if (num_comps == 4)
               {
                  // Check for YCoCg: 1.0, 1.0, 2.0
                  if (values[0] == 1.0f && values[1] == 1.0f && values[2] == 2.0f)
                     has_ycocg = true;
                  // Check for YCoCg: -1.0, -1.0, 2.0
                  if (values[0] == -1.0f && values[1] == -1.0f && values[2] == 2.0f)
                     has_ycocg = true;
               }

               // Advance k to skip the values
               k = val_idx + num_comps - 1;
            }
         }
      }

      offset += len;
      instruction_count++;
   }

   float score = 0.0f;

   // Scoring
   if (min_max_count > 15)
   {
      score += 50.0f;
   }

   if (has_gather4)
   {
      score += 20.0f;
   }

   if (has_lds_op)
   {
      score += 20.0f;
   }

   if (has_1_5 && has_2_5)
   {
      score += 40.0f;
   }

   if (has_ycocg)
   {
      score += 40.0f;
   }

   return score;
}

static bool IsUE4SSAOCandidate(const std::byte* code, size_t size, SSAOShaderInfo& ssao_info)
{
   // Heuristic 1: Scan for specific SSAO sampling kernel constants common in UE4 standard SSAO.
   // These values (0.101, 0.325, 0.272, -0.396, etc.) appear in the randomization logic.
   // Code Vein (GTAO/newer algo) does not have these, so this effectively filters it out.
   const float ssao_constants[] = {
      0.101f, 0.325f, 0.272f, -0.396f, -0.385f, -0.488f, 0.274f, 0.06f, -0.711f, 0.9f};

   int found_constants = 0;
   for (float val : ssao_constants)
   {
      word_t w;
      w.f = val;
      std::vector<std::byte> pattern = {std::byte{w.b[0]}, std::byte{w.b[1]}, std::byte{w.b[2]}, std::byte{w.b[3]}};
      if (!System::ScanMemoryForPattern(code, size, pattern).empty())
      {
         found_constants++;
      }
   }

   // If we find at least 3 of these unique constants, it is highly likely to be the standard UE4 SSAO shader.
   if (found_constants < 3)
      return false;

   // Heuristic 2: Identify the Global Constant Buffer (View Uniform Buffer).
   // UE4 SSAO requires the View Uniform Buffer for depth linearization.
   // We assume the largest declared CBuffer is the View Uniform Buffer (standard UE4 behavior).
   const uint32_t* code_u32 = reinterpret_cast<const uint32_t*>(code);
   const size_t size_u32 = size / sizeof(uint32_t);
   uint32_t* largest_cb = FindLargestCBufferDeclaration(code_u32, size_u32);

   if (largest_cb)
   {
      // The register index is usually at index 2 of the declaration token for immediate indexed buffers
      ssao_info.global_buffer_register_index = largest_cb[2];
      ssao_info.found = true;
      return true;
   }

   return false;
}

static bool IsUE4TAACandidate(const std::byte* code, size_t size, uint64_t shader_hash, TAAShaderInfo& taa_shader_info)
{
   // detects if the shader is a UE4 TAA Pixel Shader
   // first we should check resource declarations for textures:
   // taa usually has two color textures (the current frame and the history frame)
   // depth texture
   // velocity texture (unorm RG texture)
   // they should all have return type float on all components
   // iterate over bytecode, texture declarations are usually near the top so we should look until we find the first dcl_resource
   // they are likely consecutive so we iterate until we find a non dcl_resource opcode
   // dcl_resource tN, resourceType, returnType(s) this is the assembly signature
   // after we should look for decode velocity instructions to confirm

   const uint32_t* code_u32 = reinterpret_cast<const uint32_t*>(code);
   const size_t size_u32 = size / sizeof(uint32_t);
   bool found_non_texture_declaration = false;
   size_t offset = 0;
   size_t detected_2d_texture_float_count = 0;
   size_t detected_3d_texture_float_count = 0; // can be dithering texture, should hopefully always be just one or none
   size_t instruction_count = 0;
   int32_t max_texture_register = -1;
   int32_t output_count = 0;
   // while (offset < size_u32)
   // {
   //    if (instruction_count > 16)
   //       return false; // bail out if we reached too far without finding any texture declarations
   //    const uint32_t token = code_u32[offset];
   //    const uint32_t opcode = DECODE_D3D10_SB_OPCODE_TYPE(token);
   //    uint32_t len = DECODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(token);
   //    len = len == 0 ? 1 : len;

   //    if (opcode == D3D10_SB_OPCODE_DCL_RESOURCE)
   //    {
   //       break;
   //    }
   //    else
   //    {
   //       offset += len;
   //       instruction_count++;
   //    }
   // }
   while (offset < size_u32 && !found_non_texture_declaration)
   {
      if (instruction_count > 16 && (detected_2d_texture_float_count == 0 && detected_3d_texture_float_count == 0))
         return false; // bail out if we reached too far without finding any texture declarations
      // code_u32[offset] is the current instruction
      // code_u32[offset + 1] is the first operand
      // code_u32[offset + 2] operand index
      // code_u32[offset + 3] is the resource return type
      const uint32_t token = code_u32[offset];
      const uint32_t opcode = DECODE_D3D10_SB_OPCODE_TYPE(token);
      uint32_t len = DECODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(token);
      const uint32_t resource_type = DECODE_D3D10_SB_RESOURCE_DIMENSION(token);
      len = len == 0 ? 1 : len;

      if(opcode == D3D10_SB_OPCODE_DCL_RESOURCE)
      {
         // check resource type and return type
         const uint32_t resource_return_type_token = code_u32[offset + 3];
         const uint32_t register_index = code_u32[offset + 2]; // asume immediate32
         bool all_float_return =
            DECODE_D3D10_SB_RESOURCE_RETURN_TYPE(resource_return_type_token, D3D10_SB_4_COMPONENT_X) == D3D10_SB_RETURN_TYPE_FLOAT &&
            DECODE_D3D10_SB_RESOURCE_RETURN_TYPE(resource_return_type_token, D3D10_SB_4_COMPONENT_Y) == D3D10_SB_RETURN_TYPE_FLOAT &&
            DECODE_D3D10_SB_RESOURCE_RETURN_TYPE(resource_return_type_token, D3D10_SB_4_COMPONENT_Z) == D3D10_SB_RETURN_TYPE_FLOAT &&
            DECODE_D3D10_SB_RESOURCE_RETURN_TYPE(resource_return_type_token, D3D10_SB_4_COMPONENT_W) == D3D10_SB_RETURN_TYPE_FLOAT;

         max_texture_register = std::max<int32_t>(max_texture_register, static_cast<int32_t>(register_index));
         // velocity texture is usually a 2D texture with unorm RG return type
         if (resource_type == D3D10_SB_RESOURCE_DIMENSION_TEXTURE2D && all_float_return)
         {
            detected_2d_texture_float_count++;
         }
         else if (resource_type == D3D10_SB_RESOURCE_DIMENSION_TEXTURE3D && all_float_return)
         {
            detected_3d_texture_float_count++;
         }
         offset += len;
      } else if (opcode == D3D10_SB_OPCODE_DCL_OUTPUT || opcode == D3D11_SB_OPCODE_DCL_UNORDERED_ACCESS_VIEW_TYPED)
      {
         output_count++;
         offset += len;
      } else if( (opcode >= D3D11_SB_OPCODE_DCL_STREAM && opcode <= D3D11_SB_OPCODE_DCL_RESOURCE_STRUCTURED) ||
                  (opcode >= D3D10_SB_OPCODE_DCL_RESOURCE && opcode <= D3D10_SB_OPCODE_DCL_GLOBAL_FLAGS) || opcode == D3D11_SB_OPCODE_DCL_GS_INSTANCE_COUNT)
      {
         offset += len;
      }
      else if (opcode == D3D10_SB_OPCODE_CUSTOMDATA &&
               DECODE_D3D10_SB_CUSTOMDATA_CLASS(token) == D3D10_SB_CUSTOMDATA_DCL_IMMEDIATE_CONSTANT_BUFFER &&
               offset + 1 < size_u32)
      {
         // opcode 53 is NOT inside the 88..111 declaration range, so without this
         // branch the scan stops at the ICB before a single texture is counted
         // (detected_2d_texture_float_count stays 0 and the shader is rejected).
         // Note the name: opcode 53 is D3D10_SB_OPCODE_CUSTOMDATA in the WDK header
         // this project ships, and the ICB declaration is custom-data class 3.
         // Bits [31:11] of the opcode token hold the class, so the ordinary length
         // field ([30:24]) reads as 0 (0x00001835 = 53 | (3 << 11)). The real size,
         // opcode token included, follows it -- use that as the stride.
         offset += code_u32[offset + 1];
      }
      else
      {
         found_non_texture_declaration = true;
      }
   }

   if (detected_2d_texture_float_count < 4 || detected_3d_texture_float_count > 1)
      return false;

   if (output_count > 1)
      taa_shader_info.has_multiple_render_targets = true;

   taa_shader_info.max_texture_register = static_cast<int32_t>(max_texture_register);
   // now look for velocity decode instructions
   // usually velocity is decoded with a sequence like:
   // float2 DecodeVelocityFromTexture(float2 In)
   // {
   // #if 1
   //     return (In - (32767.0f / 65535.0f)) / (0.499f * 0.5f);
   // #else // MAD layout to help compiler. This is what UE/FF7 used but it's an unnecessary approximation
   //     const float InvDiv = 1.0f / (0.499f * 0.5f);
   //     return In * InvDiv - 32767.0f / 65535.0f * InvDiv;
   // #endif
   // }
   // in hlsl
   // in assembly it looks something like:
   // add r5.yz, r5.yyzy, l(0.000000, -0.499992, -0.499992, 0.000000) this one seems to vary
   // mul r5.yz, r5.yyzy, l(0.000000, 4.008016, 4.008016, 0.000000)
   // in some games this operation is made with a mad instruction instead of add+mul
   // mad r1.yz, r1.zzyz, l(0.000000, 4.008016, 4.008016, 0.000000), l(0.000000, -2.003978, -2.003978, 0.000000)
   // we should look for the immediate values using ScanMemoryForPattern
   // then look backwards for an add/mul? probably not needed

   word_t mul_1;
   mul_1.f = 4.00801611f;
   word_t mul_2;
   mul_2.f = 0.000000f;
   std::vector<std::byte> mul_pattern_bytes = {
      std::byte{mul_1.b[0]}, std::byte{mul_1.b[1]}, std::byte{mul_1.b[2]}, std::byte{mul_1.b[3]},
      std::byte{mul_1.b[0]}, std::byte{mul_1.b[1]}, std::byte{mul_1.b[2]}, std::byte{mul_1.b[3]}};
   std::vector<std::byte*> mul_hits = System::ScanMemoryForPattern(code, size, mul_pattern_bytes);
   bool found_mul_pattern = !mul_hits.empty();

   mul_pattern_bytes = {
      std::byte{mul_1.b[0]}, std::byte{mul_1.b[1]}, std::byte{mul_1.b[2]}, std::byte{mul_1.b[3]},
      std::byte{mul_2.b[0]}, std::byte{mul_2.b[1]}, std::byte{mul_2.b[2]}, std::byte{mul_2.b[3]},
      std::byte{mul_1.b[0]}, std::byte{mul_1.b[1]}, std::byte{mul_1.b[2]}, std::byte{mul_1.b[3]}};
   mul_hits = System::ScanMemoryForPattern(code, size, mul_pattern_bytes);
   found_mul_pattern = found_mul_pattern || !mul_hits.empty();

   mul_pattern_bytes = {
      std::byte{mul_1.b[0]}, std::byte{mul_1.b[1]}, std::byte{mul_1.b[2]}, std::byte{mul_1.b[3]},
      std::byte{mul_2.b[0]}, std::byte{mul_2.b[1]}, std::byte{mul_2.b[2]}, std::byte{mul_2.b[3]},
      std::byte{mul_2.b[0]}, std::byte{mul_2.b[1]}, std::byte{mul_2.b[2]}, std::byte{mul_2.b[3]},
      std::byte{mul_1.b[0]}, std::byte{mul_1.b[1]}, std::byte{mul_1.b[2]}, std::byte{mul_1.b[3]}};
   mul_hits = System::ScanMemoryForPattern(code, size, mul_pattern_bytes);
   found_mul_pattern = found_mul_pattern || !mul_hits.empty();

   // check if it has any type of loops, if so it's not likely to be TAA. Good sanity check to avoid false positives.
   if (!found_mul_pattern)
      return false;

   word_t loop_opcode;
   loop_opcode.u = ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_LOOP) |
                   ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1);
   word_t endloop_opcode;
   endloop_opcode.u = ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ENDLOOP) |
                      ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1);
   std::vector<std::byte> loop_pattern = {
      std::byte{loop_opcode.b[0]},
      std::byte{loop_opcode.b[1]},
      std::byte{loop_opcode.b[2]},
      std::byte{loop_opcode.b[3]},
   };

   std::vector<std::byte*> loop_hits = System::ScanMemoryForPattern(code, size, loop_pattern);
   std::vector<std::byte> endloop_pattern = {
      std::byte{endloop_opcode.b[0]},
      std::byte{endloop_opcode.b[1]},
      std::byte{endloop_opcode.b[2]},
      std::byte{endloop_opcode.b[3]},
   };
   std::vector<std::byte*> endloop_hits = System::ScanMemoryForPattern(code, size, endloop_pattern);
   // A UE4 TAA pixel shader can legitimately contain a loop: the 9-tap neighborhood
   // gather emitted by UE 4.26 (it walks the ICB offset table to accumulate the
   // YCoCg-clamped history neighbourhood) compiles down to a real loop/endloop pair.
   // Only an unbalanced pair indicates the bytecode scan itself went off the rails.
   if (loop_hits.size() != endloop_hits.size())
   {
      return false;
   }

   float confidence = GetTAAShaderConfidence(code, size);
   if (confidence < 60.0f)
   {
      reshade::log::message(reshade::log::level::info, std::format("UE4 TAA Shader detection for shader {:#08X} failed with confidence: {:.2f}%", shader_hash, confidence).c_str());
      return false;
   }
   reshade::log::message(reshade::log::level::info, std::format("UE4 TAA Shader detection for shader {:#08X} successed with confidence: {:.2f}%", shader_hash, confidence).c_str());
   return true;
}

static bool FindShaderInfo(const std::byte* code, size_t size, TAAShaderInfo& taa_shader_info)
{
   // The strategy here is to identify the largest cbuffer (likely global cbuffer) and look for 4 consecutive float4 loads from it
   // with indices that make a range of a 4x4 matrix (the mask xywx can be a hint too)
   // below is an example from an Unreal Engine 4 TAA shader
   // mul r3.xyz, r0.wwww, cb1[119].xywx
   // mad r3.xyz, r0.zzzz, cb1[118].xywx, r3.xyzx
   // mad r3.xyz, r2.xxxx, cb1[120].xywx, r3.xyzx
   // add r3.xyz, r3.xyzx, cb1[121].xywx
   // one way to do it is to scan for all operand tokens that are cbuffer loads, then group them by cbuffer index and look for 4 consecutive indices
   // can use the pointer address to check if the operands are in instructions close to each other, we can use some heuristics with an average instruction size
   // and look for 4 indices that are a max of x instructions apart

   const uint32_t* code_u32 = reinterpret_cast<const uint32_t*>(code);
   const uint32_t size_u32 = size / sizeof(uint32_t);
   uint32_t* max_cbuffer_declaration = FindLargestCBufferDeclaration(code_u32, size_u32);

   if (max_cbuffer_declaration == nullptr)
      return false;
   // cb1[121].xywx
   word_t cbuffer_operand_pattern_tok;
   word_t cbuffer_operand_register;
   cbuffer_operand_register.u = max_cbuffer_declaration[2];

   std::vector<std::byte> cbuffer_operand_pattern(8);
   cbuffer_operand_pattern[4] = std::byte{cbuffer_operand_register.b[0]};
   cbuffer_operand_pattern[5] = std::byte{cbuffer_operand_register.b[1]};
   cbuffer_operand_pattern[6] = std::byte{cbuffer_operand_register.b[2]};
   cbuffer_operand_pattern[7] = std::byte{cbuffer_operand_register.b[3]};

   auto scan_pattern = [&](uint32_t swizzle, uint32_t index_representation) {
      cbuffer_operand_pattern_tok.u =
         ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_4_COMPONENT) |
         ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(D3D10_SB_OPERAND_4_COMPONENT_SWIZZLE_MODE) |
         swizzle |
         ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER) |
         ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_2D) |
         ENCODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(0, D3D10_SB_OPERAND_INDEX_IMMEDIATE32) |
         ENCODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(1, index_representation);

      cbuffer_operand_pattern[0] = std::byte{cbuffer_operand_pattern_tok.b[0]};
      cbuffer_operand_pattern[1] = std::byte{cbuffer_operand_pattern_tok.b[1]};
      cbuffer_operand_pattern[2] = std::byte{cbuffer_operand_pattern_tok.b[2]};
      cbuffer_operand_pattern[3] = std::byte{cbuffer_operand_pattern_tok.b[3]};

      return System::ScanMemoryForPattern(code, size, cbuffer_operand_pattern);
   };

   std::vector<std::byte*> cbuffer_operand_hits;

   // Try xywx (0, 1, 3, 0)
   uint32_t swizzle_xywx = ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SWIZZLE(0, 1, 3, 0);
   auto hits = scan_pattern(swizzle_xywx, D3D10_SB_OPERAND_INDEX_IMMEDIATE32);
   cbuffer_operand_hits.insert(cbuffer_operand_hits.end(), hits.begin(), hits.end());

   hits = scan_pattern(swizzle_xywx, D3D10_SB_OPERAND_INDEX_IMMEDIATE32_PLUS_RELATIVE);
   cbuffer_operand_hits.insert(cbuffer_operand_hits.end(), hits.begin(), hits.end());

   if (cbuffer_operand_hits.size() < 4) // try xxyw instead of xywx
   {
      // NOTE: do NOT clear() here. Some UE4 titles mix swizzles across the four rows
      // of the PrevClipToClip matrix (e.g. one row emited as .xywx, three as .xxyw),
      // so the xywx and xxyw hits must be pooled to reach the four consecutive indices.
      uint32_t swizzle_xxyw = ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SWIZZLE(0, 0, 1, 3);

      hits = scan_pattern(swizzle_xxyw, D3D10_SB_OPERAND_INDEX_IMMEDIATE32);
      cbuffer_operand_hits.insert(cbuffer_operand_hits.end(), hits.begin(), hits.end());

      hits = scan_pattern(swizzle_xxyw, D3D10_SB_OPERAND_INDEX_IMMEDIATE32_PLUS_RELATIVE);
      cbuffer_operand_hits.insert(cbuffer_operand_hits.end(), hits.begin(), hits.end());

      if (cbuffer_operand_hits.size() < 4)
         return false; // not enough hits
   }

   // iterate over hits and store the index (word after the register) and the instruction offset (to group by proximity)

   std::set<uint32_t> indices;
   for (std::byte* hit : cbuffer_operand_hits)
   {
      size_t hit_offset = static_cast<size_t>(hit - code);
      if (hit_offset + 8 > size)
         continue; // out of bounds
      const uint32_t* hit_token = reinterpret_cast<const uint32_t*>(hit);
      uint32_t index = hit_token[2];
      indices.insert(index);
   }

   if (indices.size() < 4)
      return false; // not enough unique indices

   // copy to array and look for 4 consecutive indices
   std::vector<uint32_t> index_array(indices.begin(), indices.end());
   // std::sort(index_array.code_u32(), index_array.end()); // should already be sorted in a set

   // we need to read to find all potential candidates for PrevClipToClip matrix
   // so we look for 4 consecutive indices
   // if there are multiple candidates we pick the one with the highest average index (likely to be near the end of the cbuffer)

   // loop backwards
   int32_t best_start = UINT32_MAX;
   for (size_t i = index_array.size() - 1; i - 3 >= 0; i--)
   {
      if (index_array[i] - index_array[i - 3] == 3)
      {
         best_start = static_cast<int32_t>(index_array[i - 3]);
         break;
      }
   }

   taa_shader_info.clip_to_prev_clip_start_index = best_start;
   taa_shader_info.global_buffer_register_index = cbuffer_operand_register.u;
   taa_shader_info.declared_cbuffer_size = max_cbuffer_declaration[3];

   return true;
}

static void FindJitterFromMVWrite(const std::byte* code, size_t size, GlobalCBInfo& global_cb_info)
{
   // When materials are written to the velocity texture, the jitter is usually removed before being written
   // We can look for the Encode Velocity to Texture operations (the inverse of Decode Velocity from Texture found in TAA shader)
   // usually looks like:
   // float2 EncodeVelocityToTexture(float2 In)
   // {
   //      // 0.499f is a value smaller than 0.5f to avoid using the full range to use the clear color (0,0) as special value
   //      // 0.5f to allow for a range of -2..2 instead of -1..1 for really fast motions for temporal AA.
   //      // Texure is R16G16 UNORM
   //      return In * (0.499f * 0.5f) + (32767.0f / 65535.0f);
   // }
   // the strategy will be the similar as for FindGlobalCBInfo: scan for immediate operands matching the constants used,
   // this will be the first part to determine if it is a shader that writes motion vectors
   // then look backwards for cbuffer load operands and extract the index used (jitter vector)
   // we can use heuristics to find the closest cbuffer load before the encode velocity operation
   // this is the pattern we are looking for:
   // add r0.xyzw, r0.xyzw, -cb0[122].xyzw
   // add r0.xy, -r0.zwzz, r0.xyxx
   // mad o0.xy, r0.xyxx, l(0.249500, 0.249500, 0.000000, 0.000000), l(0.499992, 0.499992, 0.000000, 0.000000)
   // mov o0.zw, l(0,0,0,0)
   // ret
   // we should determine the cbuffer register first, jitter should be in the global cbuffer which we can assume is the largest cbuffer
   // we can find the largest cbuffer by scanning for dcl_constant_buffer instructions

   // first scan for encode velocity immediate operands pattern
   word_t mul_1;
   mul_1.f = 0.249500006f;
   word_t mul_2;
   mul_2.f = 0.f;
   std::vector<std::byte> encode_velocity_pattern_bytes = {
      std::byte{mul_1.b[0]}, std::byte{mul_1.b[1]}, std::byte{mul_1.b[2]}, std::byte{mul_1.b[3]},
      std::byte{mul_1.b[0]}, std::byte{mul_1.b[1]}, std::byte{mul_1.b[2]}, std::byte{mul_1.b[3]}};
   std::vector<std::byte*> encode_velocity_hits = System::ScanMemoryForPattern(code, size, encode_velocity_pattern_bytes);
   if (encode_velocity_hits.empty())
      return; // no hits found
   if (encode_velocity_hits.size() > 1)
      return; // too many hits, likely false positives

   // now find the largest cbuffer declaration to determine the register index to match
   const uint32_t* code_u32 = reinterpret_cast<const uint32_t*>(code);
   const uint32_t size_u32 = size / sizeof(uint32_t);
   uint32_t* max_cbuffer_declaration = FindLargestCBufferDeclaration(code_u32, size_u32);

   if (max_cbuffer_declaration == nullptr)
      return;

   uint32_t cbuffer_register_index = max_cbuffer_declaration[2];

   size_t offset = static_cast<size_t>(encode_velocity_hits[0] - code) / sizeof(uint32_t);
   offset--;
   while (offset > 0)
   {
      uint32_t token = code_u32[offset];
      if (token == cbuffer_register_index)
      {
         if (offset - 1 >= 0)
         {
            uint32_t operand_token = code_u32[offset - 1];
            bool is_cbuffer_load = DECODE_D3D10_SB_OPERAND_TYPE(operand_token) == D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER && DECODE_D3D10_SB_OPERAND_INDEX_DIMENSION(operand_token) == D3D10_SB_OPERAND_INDEX_2D && DECODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(0, operand_token) == D3D10_SB_OPERAND_INDEX_IMMEDIATE32 && DECODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(1, operand_token) == D3D10_SB_OPERAND_INDEX_IMMEDIATE32;

            if (is_cbuffer_load)
            {
               // found cbuffer load before encode velocity
               uint32_t index = code_u32[offset + 1];
               global_cb_info.jitter_index = static_cast<int32_t>(index);
               return;
            }
         }
      }
      offset--;
   }
}

// ============================================================================
// Dithering Shader Detection
// ============================================================================
// Detects UE4/UE5 material shaders that use texture-based screen-space dithering
// 
// Pattern 1: 64x64 Texture Dithering (BL3 style)
//   - SV_Position offset calculation (ADD with cbuffer offset)
//   - MUL by 0.015625 (1/64) to create UV for 64x64 noise texture
//   - SAMPLE/SAMPLE_B from texture2d with those UVs
//   Example (BL3 shader 0xEE32EC75):
//     95: add r8.xy, v5.xyxx, -cb1[138].xyxx
//     96: mul r8.xy, r8.xyxx, l(0.015625, 0.015625)
//     97: sample_b t17.yzwx, r8.xyxx, s3
//
// Pattern 2: 128x128 Simplex Gradient Noise (shader 0xC56DFF0B)
//   - MAD with simplex lattice constants (17, 89)
//   - ADD 0.5 offset
//   - MUL by 0.0078125 (1/128) to create UV for 128x128 noise texture  
//   - SAMPLE_L from texture2d with those UVs at LOD 0
//   Example:
//     r15.zw = r24.zz * float2(17,89) + r24.xy
//     r15.zw = float2(0.5,0.5) + r15.zw
//     r15.zw = float2(0.0078125,0.0078125) * r15.zw
//     r27.xyz = t0.SampleLevel(s1_s, r15.zw, 0).xyz
//
// Modification strategies:
//   - Has DISCARD: Inject cbuffer for frame index, add temporal UV offset
//   - No DISCARD: Replace SAMPLE with MOV to 0.5 (neutral noise value)
// ============================================================================

static bool IsUE4DitheringShader(const std::byte* code, size_t size, uint64_t shader_hash, DitheringShaderInfo& dither_info)
{
   const uint32_t* code_u32 = reinterpret_cast<const uint32_t*>(code);
   const size_t size_u32 = size / sizeof(uint32_t);

   dither_info = {}; // Reset to defaults

   // =========================================================================
   // Pass 1: Scan for declarations and build used cbuffer slot set
   // Also look for MUL with dithering UV scale constants:
   //   - 0.015625 (1/64) for 64x64 noise texture
   //   - 0.0078125 (1/128) for 128x128 noise texture
   // =========================================================================
   
   struct MulScaleInfo {
      size_t offset;           // Offset in uint32_t units
      uint32_t length;         // Instruction length
      uint32_t dest_reg;       // Destination register number
      uint32_t texture_size;   // Detected texture size (64 or 128)
   };
   std::vector<MulScaleInfo> scale_muls;  // MUL instructions with dithering UV scale
   
   struct SampleInfo {
      size_t offset;           // Offset in uint32_t units  
      uint32_t length;         // Instruction length
      uint32_t ext_offset;     // Number of extended opcode tokens
      uint32_t texture_reg;    // Texture register (t#)
      uint32_t uv_reg;         // UV source register
      uint32_t opcode;         // Original opcode (SAMPLE, SAMPLE_B, SAMPLE_L, etc.)
      uint32_t dest_token;     // Destination operand token
      uint32_t dest_reg;       // Destination register index
   };
   std::vector<SampleInfo> samples;       // SAMPLE/SAMPLE_B/SAMPLE_L instructions
   
   // Track lowest available cbuffer slot during iteration
   int32_t next_available_cbuffer = 0;
   
   size_t offset = 0;
   while (offset < size_u32)
   {
      const uint32_t token = code_u32[offset];
      const uint32_t opcode = DECODE_D3D10_SB_OPCODE_TYPE(token);
      uint32_t len = DECODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(token);
      len = len == 0 ? 1 : len;
      
      if (offset + len > size_u32)
         break;

      // Count extended opcode tokens - they chain via bit 31
      // Extended tokens appear between the main opcode token and the operands
      uint32_t ext_offset = 0;
      if (DECODE_IS_D3D10_SB_OPCODE_EXTENDED(token))
      {
         ext_offset = 1;
         // Check if there are more chained extended tokens
         while ((offset + ext_offset < size_u32) && 
                DECODE_IS_D3D10_SB_OPCODE_EXTENDED(code_u32[offset + ext_offset]))
         {
            ext_offset++;
         }
      }

      switch (opcode)
      {
      case D3D10_SB_OPCODE_DCL_CONSTANT_BUFFER:
         // Track used cbuffer slots and find next available
         if (len >= 3)
         {
            uint32_t operand_token = code_u32[offset + 1];
            if (DECODE_D3D10_SB_OPERAND_TYPE(operand_token) == D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER)
            {
               // The register index is in the next token(s) depending on index dimension
               int32_t cb_slot = static_cast<int32_t>(code_u32[offset + 2]);
               // If this slot matches our next available, increment to find the next gap
               if (cb_slot == next_available_cbuffer)
               {
                  next_available_cbuffer = cb_slot + 1;
               }
            }
         }
         break;
         
      case D3D10_SB_OPCODE_DISCARD:
         dither_info.has_discard = true;
         break;

      case D3D10_SB_OPCODE_MUL:
         // Look for MUL with dithering UV scale immediates:
         //   - 0.015625 (1/64) for 64x64 noise texture
         //   - 0.0078125 (1/128) for 128x128 noise texture
         for (size_t i = 1 + ext_offset; i < len; i++)
         {
            uint32_t operand_token = code_u32[offset + i];
            uint32_t op_type = DECODE_D3D10_SB_OPERAND_TYPE(operand_token);
            
            if (op_type == D3D10_SB_OPERAND_TYPE_IMMEDIATE32)
            {
               auto num_comps = DECODE_D3D10_SB_OPERAND_NUM_COMPONENTS(operand_token);
               size_t val_start = i + 1;
               
               if (num_comps == D3D10_SB_OPERAND_4_COMPONENT && val_start + 1 < len)
               {
                  const float* vals = reinterpret_cast<const float*>(&code_u32[offset + val_start]);
                  
                  // Check for dithering UV scale constants and determine texture size
                  uint32_t texture_size = 0;
                  
                  // Pattern 1: 0.015625 (1/64) for 64x64 texture
                  if (std::fabs(vals[0] - 0.015625f) < 0.00001f && 
                      std::fabs(vals[1] - 0.015625f) < 0.00001f)
                  {
                     texture_size = 64;
                  }
                  // Pattern 2: 0.0078125 (1/128) for 128x128 texture
                  else if (std::fabs(vals[0] - 0.0078125f) < 0.00001f && 
                           std::fabs(vals[1] - 0.0078125f) < 0.00001f)
                  {
                     texture_size = 128;
                  }
                  
                  if (texture_size > 0)
                  {
                     // Extract destination register info from first operand after opcode
                     uint32_t dest_token = code_u32[offset + 1 + ext_offset];
                     if (DECODE_D3D10_SB_OPERAND_TYPE(dest_token) == D3D10_SB_OPERAND_TYPE_TEMP)
                     {
                        uint32_t dest_reg = code_u32[offset + 2 + ext_offset];
                        scale_muls.push_back({offset, len, dest_reg, texture_size});
                     }
                  }
               }
               break;
            }
         }
         break;

      case D3D10_SB_OPCODE_SAMPLE:
      case D3D10_SB_OPCODE_SAMPLE_B:
      case D3D10_SB_OPCODE_SAMPLE_L:
         // Track SAMPLE/SAMPLE_B/SAMPLE_L instructions for later matching
         // SAMPLE_L is used in simplex noise pattern with explicit LOD 0
         // Format: sample_l dest, srcAddress, srcResource, srcSampler, srcLOD
         {
            if (len >= 5 + ext_offset)
            {
               SampleInfo info = {offset, len, ext_offset, 0, 0, opcode, 0, 0};
               
               // Parse operands sequentially
               // Each operand = base token + extended tokens (if any) + index tokens based on dimension
               const uint32_t* op_ptr = &code_u32[offset + 1 + ext_offset];
               const uint32_t* op_end = &code_u32[offset + len];
               
               for (int operand_index = 0; operand_index < 3 && op_ptr < op_end; operand_index++)
               {
                  uint32_t op_token = *op_ptr;
                  uint32_t op_type = DECODE_D3D10_SB_OPERAND_TYPE(op_token);
                  auto idx_dim = DECODE_D3D10_SB_OPERAND_INDEX_DIMENSION(op_token);
                  auto num_comps = DECODE_D3D10_SB_OPERAND_NUM_COMPONENTS(op_token);
                  
                  // Count extended operand tokens
                  uint32_t op_ext = 0;
                  while (DECODE_IS_D3D10_SB_OPERAND_EXTENDED(op_ptr[op_ext]) && (op_ptr + op_ext + 1 < op_end))
                  {
                     op_ext++;
                  }
                  
                  // Calculate operand size: base token + extended tokens + index tokens + immediate values
                  size_t op_size = 1 + op_ext;
                  
                  // Add index tokens based on dimension
                  if (idx_dim == D3D10_SB_OPERAND_INDEX_1D)
                     op_size += 1;
                  else if (idx_dim == D3D10_SB_OPERAND_INDEX_2D)
                     op_size += 2;
                  else if (idx_dim == D3D10_SB_OPERAND_INDEX_3D)
                     op_size += 3;
                  
                  // Add immediate values for immediate operands
                  if (op_type == D3D10_SB_OPERAND_TYPE_IMMEDIATE32)
                  {
                     if (num_comps == D3D10_SB_OPERAND_4_COMPONENT)
                        op_size += 4;
                     else if (num_comps == D3D10_SB_OPERAND_1_COMPONENT)
                        op_size += 1;
                  }
                  
                  // Extract register indices
                  // Index position is after base token + extended tokens
                  size_t idx_pos = 1 + op_ext;
                  
                  switch (operand_index)
                  {
                  case 0: // Destination - store for MOV construction
                     if (op_type == D3D10_SB_OPERAND_TYPE_TEMP && idx_dim == D3D10_SB_OPERAND_INDEX_1D)
                     {
                        info.dest_token = op_token;
                        info.dest_reg = op_ptr[idx_pos];
                     }
                     break;
                  case 1: // srcAddress (UV coordinates)
                     if (op_type == D3D10_SB_OPERAND_TYPE_TEMP && idx_dim == D3D10_SB_OPERAND_INDEX_1D)
                     {
                        info.uv_reg = op_ptr[idx_pos];
                     }
                     break;
                  case 2: // srcResource (Texture)
                     if (op_type == D3D10_SB_OPERAND_TYPE_RESOURCE && idx_dim == D3D10_SB_OPERAND_INDEX_1D)
                     {
                        info.texture_reg = op_ptr[idx_pos];
                     }
                     break;
                  }
                  
                  op_ptr += op_size;
               }
               
               samples.push_back(info);
            }
         }
         break;

      default:
         break;
      }

      offset += len;
   }

   // =========================================================================
   // Pass 2: Match MUL UV scale output register with SAMPLE UV input register
   // This confirms the texture-based dithering pattern
   // Supports both 64x64 (0.015625) and 128x128 (0.0078125) texture patterns
   // Collect ALL matching samples (there can be multiple samples from same noise texture)
   // =========================================================================
   
   bool found_match = false;
   
   for (const auto& mul : scale_muls)
   {
      for (const auto& sample : samples)
      {
         // Check if the MUL destination register matches the SAMPLE UV source register
         // The MUL must come before the SAMPLE
         if (mul.offset < sample.offset && mul.dest_reg == sample.uv_reg)
         {
            if (!found_match)
            {
               // First match - set up basic info
               found_match = true;
               dither_info.type = DitheringType::Texture_ScreenSpace;
               dither_info.noise_texture_register = static_cast<int32_t>(sample.texture_reg);
               dither_info.noise_texture_size = mul.texture_size;
               dither_info.uv_scale_instruction_offset = mul.offset * sizeof(uint32_t);
               dither_info.uv_scale_instruction_length = mul.length;
               
               // Use the next available cbuffer slot found during iteration
               // Cap at slot 13 (SM5 allows 0-13 for cbuffers)
               if (next_available_cbuffer <= 13)
               {
                  dither_info.injected_cbuffer_slot = next_available_cbuffer;
               }
            }
            
            // Add this sample to the list (we need to replace all of them)
            SampleInstructionInfo sample_info;
            sample_info.instruction_offset = sample.offset * sizeof(uint32_t);
            sample_info.instruction_length = sample.length;
            sample_info.opcode = sample.opcode;
            sample_info.dest_operand_token = sample.dest_token;
            sample_info.dest_register = sample.dest_reg;
            sample_info.ext_offset = sample.ext_offset;
            dither_info.sample_instructions.push_back(sample_info);
         }
      }
   }
   
   if (found_match)
   {
      // Modification is supported if:
      // - No discard: Always (we just replace sample with MOV 0.5)
      // - Has discard: Only if we found an available cbuffer slot
      dither_info.modification_supported = !dither_info.has_discard || 
                                            (dither_info.injected_cbuffer_slot >= 0);
   }

   return found_match;
}

// ============================================================================
// Dithering Shader Modification
// ============================================================================
// Modifies detected texture-based dithering shaders:
//
// No DISCARD case:
//   - Replace SAMPLE with MOV to 0.5 (neutral noise value)
//   - This makes dithering uniform without visible pattern
//
// Has DISCARD case:
//   - Inject cbuffer declaration for frame index (temporal randomization)
//   - Modify UV calculation to add frame-based offset before sampling
//   - This spreads dithering over time for better visual integration
// ============================================================================

static std::unique_ptr<std::byte[]> ModifyDitheringShader(
   const std::byte* code, 
   size_t& size, 
   const DitheringShaderInfo& dither_info)
{
   if (dither_info.type != DitheringType::Texture_ScreenSpace)
      return nullptr;
   
   if (!dither_info.modification_supported)
      return nullptr;

   const uint32_t* code_u32 = reinterpret_cast<const uint32_t*>(code);
   const size_t size_u32 = size / sizeof(uint32_t);

   // =========================================================================
   // Case 1: No DISCARD - Replace all SAMPLEs with MOV to 0.5
   // =========================================================================
   // This is the simple case - we just need to neutralize the noise value
   // by making the sample result always 0.5 (middle of noise range)
   //
   // Original: sample r4.w, r8.xyxx, t17.yzwx, s3
   // Modified: mov r4.w, l(0.5)
   // =========================================================================
   if (!dither_info.has_discard)
   {
      if (dither_info.sample_instructions.empty())
         return nullptr;
      
      // Create modified bytecode
      auto new_code = std::make_unique<std::byte[]>(size);
      std::memcpy(new_code.get(), code, size);
      uint32_t* new_code_u32 = reinterpret_cast<uint32_t*>(new_code.get());
      
      // 0.5f as uint32_t (compute once)
      union { float f; uint32_t u; } half_val;
      half_val.f = 0.5f;
      
      // Replace each SAMPLE instruction with MOV 0.5
      for (const auto& sample : dither_info.sample_instructions)
      {
         size_t sample_offset = sample.instruction_offset / sizeof(uint32_t);
         uint32_t sample_len = sample.instruction_length;
         
         if (sample_offset + sample_len > size_u32)
            continue;
         
         // Use stored destination operand info directly (no re-parsing needed)
         uint32_t dest_token = sample.dest_operand_token;
         uint32_t dest_reg = sample.dest_register;
         
         // Build MOV instruction: mov dest, l(0.5, 0.5, 0.5, 0.5)
         // MOV format: opcode_token, dest_operand, dest_reg, imm32_operand, val, val, val, val
         std::vector<uint32_t> mov_instr;
         
         // MOV opcode token (length will be filled in after we know size)
         uint32_t mov_opcode = ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV);
         
         // Destination operand (copy from original, preserves masking)
         mov_instr.push_back(dest_token);
         mov_instr.push_back(dest_reg);
         
         // Immediate source operand: l(0.5, 0.5, 0.5, 0.5)
         uint32_t imm_operand = ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_IMMEDIATE32) |
                                ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_4_COMPONENT) |
                                ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(D3D10_SB_OPERAND_4_COMPONENT_SWIZZLE_MODE) |
                                ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_0D);
         mov_instr.push_back(imm_operand);
         mov_instr.push_back(half_val.u); // x
         mov_instr.push_back(half_val.u); // y  
         mov_instr.push_back(half_val.u); // z
         mov_instr.push_back(half_val.u); // w
         
         // Calculate total instruction length (opcode token + operands)
         uint32_t mov_len = 1 + static_cast<uint32_t>(mov_instr.size());
         mov_opcode |= ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(mov_len);
         
         // If the new instruction is longer than the original, skip this one
         if (mov_len > sample_len)
            continue;
         
         // Write MOV instruction
         new_code_u32[sample_offset] = mov_opcode;
         for (size_t i = 0; i < mov_instr.size(); i++)
         {
            new_code_u32[sample_offset + 1 + i] = mov_instr[i];
         }
         
         // Pad remaining space with NOPs if needed
         for (uint32_t i = mov_len; i < sample_len; i++)
         {
            // NOP is just opcode 0 with length 1
            new_code_u32[sample_offset + i] = ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_NOP) |
                                               ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1);
         }
      }
      
      return new_code;
   }
   
   // =========================================================================
   // Case 2: Has DISCARD - Inject cbuffer and add temporal UV offset
   // =========================================================================
   // This is more complex - we need to:
   // 1. Add a new DCL_CONSTANTBUFFER declaration
   // 2. Modify the MUL instruction or add an ADD after it for frame-based UV offset
   //
   // For now, return nullptr - this needs more careful implementation
   // The CPU side should set up the cbuffer with frame index at draw time
   // =========================================================================
   if (dither_info.has_discard && dither_info.injected_cbuffer_slot >= 0)
   {
      // TODO: Implement cbuffer injection and UV offset modification
      // This requires:
      // 1. Finding the declaration section end
      // 2. Inserting DCL_CONSTANTBUFFER instruction
      // 3. Inserting ADD instruction after the MUL for UV offset
      // 4. Shifting all subsequent bytecode
      // 5. Updating shader length in header
      //
      // For now, return nullptr to indicate modification not yet implemented
      // The shader will still be detected and info stored for potential
      // future implementation or alternative fix methods
      return nullptr;
   }
   
   return nullptr;
}