#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <vector>

// Makes the game's shaders that rebuild a position from the depth buffer aware of the projection jitter.
//
// They all turn NDC into a view space ray with the projection scale only ("ndc.xy * tan(fov / 2)", or "ndc.xy / projection[i][i]"),
// which is only right for a centered projection: with jitter the position ends up shifted by a fraction of a pixel,
// by a different amount every frame, so deferred decals, lighting, shadows and AO shimmer (the more the lower the rendering resolution).
// The right math is "(ndc.xy - jitter) * tan(fov / 2)", so that's what we patch in, in around 2000 shaders:
// - Pixel shaders (deferred decals and materials that read the scene depth):
//      div rS.xy, vN.xy, vN.w            // NDC from the interpolated clip position
//      mul rD.xy, rS.xy, cb0[K].xy   =>  mul rD.xy, (rS.xy - jitter), cb0[K].xy
// - Compute shaders (tiled lighting, reflections, AO):
//      mad rS.ab, ..., l(-1, 1)          // NDC from the pixel coordinates
//      div rD.x, rS.a, cb0[K].x      =>  div rD.x, (rS.a - jitter.x), cb0[K].x
//      div rD.y, rS.b, cb0[K + 1].y  =>  div rD.y, (rS.b - jitter.y), cb0[K + 1].y
//   and the opposite where they project a view space position back to the screen (AO):
//      div rS.ab, rV.xy, rV.z
//      mul rD.x, rS.a, cb0[K].x      =>  ... + jitter.x
//      mul rD.y, rS.b, cb0[K + 1].y  =>  ... + jitter.y
// The jitter (in NDC) comes from the view constant buffer the shaders already have (b1): it's the clip space xy of the camera forward direction,
// "view_projection[0..2].xy" (cb1[0..2]) times "view_to_world[2].xyz" (cb1[17]), so it's exactly the one the frame was rendered with,
// and zero when there's no jitter (the patched shaders then output the same as the original ones).
namespace BatmanArkhamKnightJitterPatch
{
   namespace Detail
   {
      // DXBC (SM4/5) tokens
      constexpr uint32_t opcode_add = 0x00;
      constexpr uint32_t opcode_div = 0x0E;
      constexpr uint32_t opcode_mad = 0x32;
      constexpr uint32_t opcode_mul = 0x38;
      constexpr uint32_t opcode_custom_data = 0x35;
      constexpr uint32_t opcode_dcl_constant_buffer = 0x59;
      constexpr uint32_t opcode_dcl_temps = 0x68;

      constexpr uint32_t view_cbuffer_slot = 1;
      constexpr uint32_t view_cbuffer_view_projection_row = 0;
      constexpr uint32_t view_cbuffer_view_forward_row = 17;

      constexpr uint32_t swizzle_xxxx = 0x00;
      constexpr uint32_t swizzle_yyyy = 0x55;
      constexpr uint32_t swizzle_zzzz = 0xAA;
      constexpr uint32_t swizzle_wwww = 0xFF;
      constexpr uint32_t swizzle_xyxx = 0x04;
      constexpr uint32_t swizzle_xyzw = 0xE4;

      constexpr uint32_t operand_temp_mask = 0x00100002;       // "rN.mask" (followed by N)
      constexpr uint32_t operand_temp_swizzle = 0x00100006;    // "rN.swizzle" (followed by N)
      constexpr uint32_t operand_temp_select = 0x0010000A;     // "rN.component" (followed by N)
      constexpr uint32_t operand_input_swizzle = 0x00101006;   // "vN.swizzle" (followed by N)
      constexpr uint32_t operand_cbuffer_swizzle = 0x00208006; // "cbN[M].swizzle" (followed by N and M)
      constexpr uint32_t operand_cbuffer_select = 0x0020800A;  // "cbN[M].component" (followed by N and M)
      constexpr uint32_t operand_literal_4 = 0x00004002;       // "l(x, y, z, w)" (followed by the 4 values)
      constexpr uint32_t operand_extended = 0x80000000;
      constexpr uint32_t operand_modifier_negate = 0x00000041; // Extended operand token

      constexpr uint32_t literal_one = 0x3F800000;
      constexpr uint32_t literal_minus_one = 0xBF800000;

      constexpr uint32_t Instruction(uint32_t opcode, uint32_t length)
      {
         return opcode | (length << 24);
      }
      constexpr uint32_t Components(uint32_t operand)
      {
         return (operand >> 4) & 0xFF;
      } // Mask (4 bits), swizzle (4x 2 bits) or selected component (2 bits)
      constexpr uint32_t WithComponents(uint32_t operand, uint32_t components)
      {
         return operand | (components << 4);
      }
      constexpr bool IsOperand(uint32_t token, uint32_t operand)
      {
         return (token & 0xFFFFF00F) == operand;
      }
      constexpr bool IsSelectOperand(uint32_t token, uint32_t operand)
      {
         return (token & 0xFFFFFFCF) == operand;
      }
      constexpr uint32_t Swizzle(uint32_t operand, uint32_t component)
      {
         return (operand >> (4 + 2 * component)) & 3;
      }
      constexpr bool IsDeclaration(uint32_t opcode)
      {
         return (opcode >= 0x58 && opcode <= 0x6B) || (opcode >= 0x8F && opcode <= 0xA2) || opcode == 0xCE || opcode == opcode_custom_data;
      }

      struct InstructionView
      {
         const uint32_t* tokens;
         uint32_t length;
         uint32_t opcode;

         uint32_t operator[](size_t i) const
         {
            return tokens[i];
         }
         // Exact opcode and length (so without the "saturate" flag or other extensions)
         bool Is(uint32_t opcode_and_length) const
         {
            return tokens[0] == opcode_and_length;
         }
      };

      inline void Append(std::vector<uint32_t>& out, std::initializer_list<uint32_t> tokens)
      {
         out.insert(out.end(), tokens);
      }
      inline void Append(std::vector<uint32_t>& out, const InstructionView& instruction)
      {
         out.insert(out.end(), instruction.tokens, instruction.tokens + instruction.length);
      }

      // "rT.xy = jitter" (in NDC)
      inline void AppendJitter(std::vector<uint32_t>& out, uint32_t temp)
      {
         const uint32_t dest = WithComponents(operand_temp_mask, 0x3);
         const uint32_t row = WithComponents(operand_cbuffer_swizzle, swizzle_xyxx);
         const uint32_t self = WithComponents(operand_temp_swizzle, swizzle_xyxx);
         Append(out, {Instruction(opcode_mul, 9), dest, temp, row, view_cbuffer_slot, view_cbuffer_view_projection_row + 0, WithComponents(operand_cbuffer_swizzle, swizzle_xxxx), view_cbuffer_slot, view_cbuffer_view_forward_row});
         Append(out, {Instruction(opcode_mad, 11), dest, temp, row, view_cbuffer_slot, view_cbuffer_view_projection_row + 1, WithComponents(operand_cbuffer_swizzle, swizzle_yyyy), view_cbuffer_slot, view_cbuffer_view_forward_row, self, temp});
         Append(out, {Instruction(opcode_mad, 11), dest, temp, row, view_cbuffer_slot, view_cbuffer_view_projection_row + 2, WithComponents(operand_cbuffer_swizzle, swizzle_zzzz), view_cbuffer_slot, view_cbuffer_view_forward_row, self, temp});
      }

      // "op rD.x, rS.a, cb0[K].x" followed by "op rD.y, rS.b, cb0[K + 1].y" (the diagonal of a projection matrix)
      inline bool IsProjectionScalePair(const std::vector<InstructionView>& instructions, size_t i, uint32_t opcode)
      {
         if (i + 1 >= instructions.size())
            return false;
         const InstructionView& x = instructions[i];
         const InstructionView& y = instructions[i + 1];
         return x.Is(Instruction(opcode, 8)) && y.Is(Instruction(opcode, 8)) && x[1] == WithComponents(operand_temp_mask, 0x1) && y[1] == WithComponents(operand_temp_mask, 0x2) && x[2] == y[2] && IsSelectOperand(x[3], operand_temp_select) && IsSelectOperand(y[3], operand_temp_select) && x[4] == y[4] && x[5] == WithComponents(operand_cbuffer_select, 0) && y[5] == WithComponents(operand_cbuffer_select, 1) && x[6] == 0 && y[6] == 0 && y[7] == x[7] + 1;
      }

      // Pixel shaders. Returns the number of patched places.
      inline uint32_t PatchPixelShader(const std::vector<InstructionView>& instructions, uint32_t temp, std::vector<uint32_t>& out)
      {
         uint32_t patches = 0;
         for (size_t i = 0; i < instructions.size(); i++)
         {
            const InstructionView& mul = instructions[i];
            bool patch = false;
            // "mul rD.mask, rS.swizzle, cb0[K].swizzle", writing two components, one scaled by "cb0[K].x" (horizontal) and one by "cb0[K].y" (vertical)
            if (mul.Is(Instruction(opcode_mul, 8)) && IsOperand(mul[1], operand_temp_mask) && IsOperand(mul[3], operand_temp_swizzle) && IsOperand(mul[5], operand_cbuffer_swizzle) && mul[6] == 0)
            {
               // For each component of the source register, the NDC axis it needs to hold (-1 if not read)
               int source_axis[4] = {-1, -1, -1, -1};
               uint32_t axes = 0;
               bool valid = true;
               for (uint32_t c = 0; c < 4; c++)
               {
                  if ((Components(mul[1]) & (1 << c)) == 0)
                     continue;
                  const uint32_t axis = Swizzle(mul[5], c);
                  const uint32_t source = Swizzle(mul[3], c);
                  valid &= axis <= 1 && (axes & (1 << axis)) == 0 && source_axis[source] == -1;
                  axes |= 1 << axis;
                  source_axis[source] = int(axis);
               }
               if (valid && axes == 0x3)
               {
                  // The source needs to come from a perspective divide of an interpolated clip space position, "div rS.mask, vN.swizzle, vN.wwww",
                  // either right before or with one unrelated instruction in between.
                  for (size_t back = 1; back <= 2 && back <= i; back++)
                  {
                     const InstructionView& previous = instructions[i - back];
                     if (IsDeclaration(previous.opcode) || previous.length < 3 || !IsOperand(previous[1], operand_temp_mask))
                        break;
                     bool writes_source = false;
                     if (previous[2] == mul[4])
                     {
                        for (uint32_t c = 0; c < 4; c++)
                        {
                           writes_source |= source_axis[c] != -1 && (Components(previous[1]) & (1 << c)) != 0;
                        }
                     }
                     if (!writes_source)
                        continue;
                     patch = previous.Is(Instruction(opcode_div, 7)) && IsOperand(previous[3], operand_input_swizzle) && previous[5] == WithComponents(operand_input_swizzle, swizzle_wwww) && previous[4] == previous[6];
                     for (uint32_t c = 0; c < 4 && patch; c++)
                     {
                        if (source_axis[c] != -1)
                           patch = (Components(previous[1]) & (1 << c)) != 0 && Swizzle(previous[3], c) == uint32_t(source_axis[c]);
                     }
                     break;
                  }
               }
            }

            if (!patch)
            {
               Append(out, mul);
               continue;
            }

            // rT.xy = jitter
            // rT1.mask = rS.swizzle - rT.(swizzle of cb0[K], so each component gets the jitter of its axis)
            // rD.mask = rT1 * cb0[K].swizzle
            AppendJitter(out, temp);
            Append(out, {Instruction(opcode_add, 8), mul[1], temp + 1, mul[3], mul[4], WithComponents(operand_temp_swizzle, Components(mul[5])) | operand_extended, operand_modifier_negate, temp});
            Append(out, {Instruction(opcode_mul, 8), mul[1], mul[2], WithComponents(operand_temp_swizzle, swizzle_xyzw), temp + 1, mul[5], mul[6], mul[7]});
            patches++;
         }
         return patches;
      }

      // Compute shaders. Returns the number of patched places.
      inline uint32_t PatchComputeShader(const std::vector<InstructionView>& instructions, uint32_t temp, std::vector<uint32_t>& out)
      {
         // Find the places that turn NDC into a view space ray: a pair of divisions by the projection scale, right after NDC was built from the pixel coordinates,
         // "mad rS.ab, ..., l(-1 at a, 1 at b)" (other shaders that divide by the projection scale work on different coordinates and are left alone).
         std::vector<bool> unprojections(instructions.size(), false);
         std::vector<uint32_t> projection_rows;
         for (size_t i = 1; i < instructions.size(); i++)
         {
            if (!IsProjectionScalePair(instructions, i, opcode_div))
               continue;
            const InstructionView& previous = instructions[i - 1];
            const uint32_t source_x = Components(instructions[i][3]) & 3;
            const uint32_t source_y = Components(instructions[i + 1][3]) & 3;
            if (previous.opcode != opcode_mad || previous.length < 8 || !IsOperand(previous[1], operand_temp_mask) || previous[2] != instructions[i][4])
               continue;
            if ((Components(previous[1]) & (1 << source_x)) == 0 || (Components(previous[1]) & (1 << source_y)) == 0)
               continue;
            const uint32_t* literal = previous.tokens + previous.length - 5;
            if (literal[0] != operand_literal_4 || literal[1 + source_x] != literal_minus_one || literal[1 + source_y] != literal_one)
               continue;
            unprojections[i] = true;
            projection_rows.push_back(instructions[i][7]);
         }
         if (projection_rows.empty())
            return 0;

         uint32_t patches = 0;
         bool jitter_added = false;
         for (size_t i = 0; i < instructions.size(); i++)
         {
            // The jitter is calculated once, before the first instruction
            if (!jitter_added && !IsDeclaration(instructions[i].opcode))
            {
               AppendJitter(out, temp);
               jitter_added = true;
            }

            if (unprojections[i])
            {
               // rD.c = rS.a - jitter.c
               // rD.c = rD.c / cb0[K + c].c
               for (uint32_t c = 0; c < 2; c++)
               {
                  const InstructionView& div = instructions[i + c];
                  const uint32_t swizzle = c == 0 ? swizzle_xxxx : swizzle_yyyy;
                  Append(out, {Instruction(opcode_add, 8), div[1], div[2], div[3], div[4], WithComponents(operand_temp_swizzle, swizzle) | operand_extended, operand_modifier_negate, temp});
                  Append(out, {Instruction(opcode_div, 8), div[1], div[2], WithComponents(operand_temp_swizzle, swizzle), div[2], div[5], div[6], div[7]});
               }
               patches++;
               i++;
               continue;
            }

            // The opposite: a view space position divided by its depth and scaled by the same projection rows, to go back to NDC
            if (i > 0 && IsProjectionScalePair(instructions, i, opcode_mul) && std::find(projection_rows.begin(), projection_rows.end(), instructions[i][7]) != projection_rows.end())
            {
               const InstructionView& previous = instructions[i - 1];
               if (previous.Is(Instruction(opcode_div, 7)) && IsOperand(previous[1], operand_temp_mask) && previous[2] == instructions[i][4] && IsOperand(previous[3], operand_temp_swizzle) && previous[5] == WithComponents(operand_temp_swizzle, swizzle_zzzz) && previous[4] == previous[6])
               {
                  // rD.c = rS.a * cb0[K + c].c
                  // rD.c = rD.c + jitter.c
                  for (uint32_t c = 0; c < 2; c++)
                  {
                     const InstructionView& mul = instructions[i + c];
                     const uint32_t self = WithComponents(operand_temp_swizzle, c == 0 ? swizzle_xxxx : swizzle_yyyy);
                     Append(out, mul);
                     Append(out, {Instruction(opcode_add, 7), mul[1], mul[2], self, mul[2], self, temp});
                  }
                  patches++;
                  i++;
                  continue;
               }
            }

            Append(out, instructions[i]);
         }
         return patches;
      }
   } // namespace Detail

   // "code" is the instructions of a pixel or compute shader (the SHEX chunk without its two header tokens).
   // Returns the patched instructions and updates "size", or null if the shader doesn't need (or can't take) the patch.
   inline std::unique_ptr<std::byte[]> Patch(const std::byte* code, size_t& size, bool compute_shader)
   {
      using namespace Detail;

      if (!code || size == 0 || size % sizeof(uint32_t) != 0)
         return nullptr;

      const uint32_t* tokens = reinterpret_cast<const uint32_t*>(code);
      const size_t tokens_count = size / sizeof(uint32_t);

      std::vector<InstructionView> instructions;
      instructions.reserve(tokens_count / 4);
      size_t temps_instruction = SIZE_MAX;
      bool has_view_cbuffer = false;
      for (size_t i = 0; i < tokens_count;)
      {
         const uint32_t opcode = tokens[i] & 0x7FF;
         if (opcode == opcode_custom_data && i + 1 >= tokens_count)
            return nullptr;
         const uint32_t length = opcode == opcode_custom_data ? tokens[i + 1] : ((tokens[i] >> 24) & 0x7F);
         if (length == 0 || length > tokens_count - i)
            return nullptr;
         if (opcode == opcode_dcl_temps && length == 2)
            temps_instruction = instructions.size();
         // The shader needs to have the view constant buffer, large enough to hold all we read from it
         else if (opcode == opcode_dcl_constant_buffer && length == 4 && tokens[i + 2] == view_cbuffer_slot && tokens[i + 3] > view_cbuffer_view_forward_row)
            has_view_cbuffer = true;
         instructions.push_back({tokens + i, length, opcode});
         i += length;
      }
      if (temps_instruction == SIZE_MAX || !has_view_cbuffer)
         return nullptr;

      // We use new temporary registers, so we don't need to know which of the existing ones are free
      const uint32_t temps_count = instructions[temps_instruction][1];
      const uint32_t new_temps_count = temps_count + (compute_shader ? 1 : 2);
      // Leave some room below the limit of 4096
      if (temps_count >= 4000)
         return nullptr;

      std::vector<uint32_t> out;
      out.reserve(tokens_count + 64);
      const uint32_t patches = compute_shader ? PatchComputeShader(instructions, temps_count, out) : PatchPixelShader(instructions, temps_count, out);
      if (patches == 0)
         return nullptr;

      // The declarations are all before the code, so the offset of this one didn't change
      out[(instructions[temps_instruction].tokens - tokens) + 1] = new_temps_count;

      size = out.size() * sizeof(uint32_t);
      std::unique_ptr<std::byte[]> new_code = std::make_unique<std::byte[]>(size);
      std::memcpy(new_code.get(), out.data(), size);
      return new_code;
   }
} // namespace BatmanArkhamKnightJitterPatch
