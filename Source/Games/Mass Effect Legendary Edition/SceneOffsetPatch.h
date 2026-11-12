#pragma once

#include <array>
#include <bit>
#include <span>
#include <vector>

#include "dxbc.h"

// Render scale for material pixel shaders that move a screen position by an offset in texture UV (refraction reading the scene color,
// the distortion accumulation's depth test): UE3 maps the position into the rendered share of the scene targets with
// PSOffsetConstants.ScreenPositionScaleBias, then adds the material's offset unscaled, so below native the read lands twice as far in
// scene pixels at 50%. Each texture read at "base + offset" (base: the ScreenPositionScaleBias mad) gets
// base + (coordinate - base) * 2 * |ScreenPositionScaleBias.xy|, the scene's size over its target's (1 at native, as vanilla). The
// distortion apply (global 0xC7FDDA3D) has a replacement doing the same.
namespace SceneOffsetPatch
{
   using namespace DXBC;

   // PSOffsetConstants: ScreenPositionScaleBias in c0 (xy scale, wz bias), in every UE3 pixel shader of the three games
   constexpr uint32_t offset_constants_slot = 2;

   struct Stats
   {
      uint32_t patched_reads = 0;
      // Texture reads at a coordinate derived from a screen position some other way (clamped, multiplied, from another branch, from
      // a base computed again since)
      uint32_t refused_reads = 0;
   };

   // The pixel shader's program (version and length tokens first) with its offset screen position reads scaled, or empty if it has
   // none (or can't be read)
   inline std::vector<uint32_t> PatchPixelShaderProgram(const uint8_t* code, size_t size, Stats* stats)
   {
      if (size < sizeof(Shader::DXBCHeader) || std::memcmp(code, "DXBC", 4) != 0)
         return {};
      const auto* header = reinterpret_cast<const Shader::DXBCHeader*>(code);
      if (header->file_size != size || sizeof(Shader::DXBCHeader) + size_t(header->chunk_count) * sizeof(uint32_t) > size)
         return {};
      // In place: nearly every shader is refused by its reflection before its program is read
      const auto find_chunk = [&](uint32_t fourcc, uint32_t alternative) -> std::span<const uint8_t>
      {
         for (uint32_t i = 0; i < header->chunk_count; i++)
         {
            const size_t offset = header->chunk_offsets[i];
            uint32_t chunk_fourcc = 0;
            uint32_t chunk_size = 0;
            if (offset + 8 > size)
               return {};
            std::memcpy(&chunk_fourcc, code + offset, 4);
            std::memcpy(&chunk_size, code + offset + 4, 4);
            if (chunk_fourcc == fourcc || chunk_fourcc == alternative)
               return offset + 8 + chunk_size <= size ? std::span<const uint8_t>(code + offset + 8, chunk_size) : std::span<const uint8_t>();
         }
         return {};
      };

      // The scene textures (render share sized: "Scene" names, as SceneColorTexture and SceneDepthTexture) by their reflection names.
      // Material textures read at a screen position (Texture2D_N noise and masks) stay as they are: their whole coordinate, not the
      // offset, would need the share.
      std::vector<uint32_t> scene_slots;
      if (const std::span<const uint8_t> reflection = find_chunk(FourCC("RDEF"), 0); reflection.size() >= 16)
      {
         uint32_t count = 0;
         uint32_t offset = 0;
         std::memcpy(&count, reflection.data() + 8, 4);
         std::memcpy(&offset, reflection.data() + 12, 4);
         // D3D11_SHADER_INPUT_BIND_DESC as stored: name offset, type, return type, dimension, samples, bind point, bind count, flags
         for (uint32_t i = 0; i < count && offset + (i + 1) * 32 <= reflection.size(); i++)
         {
            uint32_t binding[8];
            std::memcpy(binding, reflection.data() + offset + i * 32, sizeof(binding));
            constexpr char prefix[] = "Scene";
            const bool is_texture2d = binding[1] == D3D_SIT_TEXTURE && binding[3] == D3D_SRV_DIMENSION_TEXTURE2D && binding[6] == 1;
            const bool named_scene = binding[0] + sizeof(prefix) <= reflection.size() && std::memcmp(reflection.data() + binding[0], prefix, sizeof(prefix) - 1) == 0;
            if (is_texture2d && named_scene)
            {
               scene_slots.push_back(binding[5]);
            }
         }
      }
      if (scene_slots.empty())
         return {};

      const std::span<const uint8_t> program = find_chunk(FourCC("SHEX"), FourCC("SHDR"));
      std::vector<uint32_t> tokens;
      std::vector<Instruction> instructions;
      size_t first_body = 0;
      if (program.empty() || !ReadProgram({FourCC("SHEX"), std::vector<uint8_t>(program.begin(), program.end())}, &tokens, &instructions, &first_body))
         return {};
      uint32_t patch_temp = 0; // The added temp. xy: the last base of each axis, zw: the scaled coordinate.
      std::vector<uint32_t> patched = CopyDeclarations(tokens, instructions, first_body, 1, &patch_temp, [](size_t)
         { return std::vector<uint32_t>(); });
      const uint32_t temp_count = patch_temp;

      struct Operand
      {
         size_t position = 0;
         D3D10_SB_OPERAND_TYPE type = D3D10_SB_OPERAND_TYPE_NULL;
         bool extended = false;
         bool mask_mode = false;
         bool modified = false; // neg / abs
         uint32_t mask = 0;     // xyzw bits
         std::array<uint32_t, 4> components = {0, 1, 2, 3};
         uint32_t index[2] = {};
      };
      // The instruction's operands, not their relative index operands; false on an encoding WalkOperand refuses
      const auto read_operands = [&](const Instruction& instruction, std::vector<Operand>* operands)
      {
         operands->clear();
         const size_t end = instruction.begin + instruction.length;
         size_t i = instruction.begin + 1;
         for (uint32_t extended = tokens[instruction.begin]; DECODE_IS_D3D10_SB_OPCODE_EXTENDED(extended); extended = tokens[i++])
         {
            if (i >= end)
               return false;
         }
         while (i < end)
         {
            // Visited last, after its relative index operands (immediates aren't visited); WalkOperand checked its tokens fit
            size_t position = i;
            size_t index_position = no_index;
            i = WalkOperand(tokens, i, end, [&](size_t token_position, size_t first_index_position)
               {
                  position = token_position;
                  index_position = first_index_position;
                  return true; });
            if (i == 0)
               return false;
            const uint32_t token = tokens[position];
            Operand operand = {.position = position, .type = DECODE_D3D10_SB_OPERAND_TYPE(token), .extended = DECODE_IS_D3D10_SB_OPERAND_EXTENDED(token) != 0};
            for (size_t extended = position; DECODE_IS_D3D10_SB_OPERAND_EXTENDED(tokens[extended]); extended++)
               operand.modified |= DECODE_D3D10_SB_EXTENDED_OPERAND_TYPE(tokens[extended + 1]) == D3D10_SB_EXTENDED_OPERAND_MODIFIER &&
                                   DECODE_D3D10_SB_OPERAND_MODIFIER(tokens[extended + 1]) != D3D10_SB_OPERAND_MODIFIER_NONE;
            if (DECODE_D3D10_SB_OPERAND_NUM_COMPONENTS(token) == D3D10_SB_OPERAND_4_COMPONENT && operand.type != D3D10_SB_OPERAND_TYPE_IMMEDIATE32)
            {
               switch (DECODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(token))
               {
               case D3D10_SB_OPERAND_4_COMPONENT_MASK_MODE:
                  operand.mask_mode = true;
                  operand.mask = DECODE_D3D10_SB_OPERAND_4_COMPONENT_MASK(token) >> 4;
                  break;
               case D3D10_SB_OPERAND_4_COMPONENT_SWIZZLE_MODE:
                  for (uint32_t c = 0; c < 4; c++)
                     operand.components[c] = DECODE_D3D10_SB_OPERAND_4_COMPONENT_SWIZZLE_SOURCE(token, c);
                  break;
               case D3D10_SB_OPERAND_4_COMPONENT_SELECT_1_MODE:
                  operand.components.fill(DECODE_D3D10_SB_OPERAND_4_COMPONENT_SELECT_1(token));
                  break;
               }
            }
            // Immediate indices (temps, constant buffers, resources); a relative one makes the operand match nothing below
            for (uint32_t dimension = 0; dimension < (std::min)(uint32_t(DECODE_D3D10_SB_OPERAND_INDEX_DIMENSION(token)), 2u); dimension++)
            {
               if (DECODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(dimension, token) == D3D10_SB_OPERAND_INDEX_IMMEDIATE32)
               {
                  operand.index[dimension] = tokens[index_position + dimension];
               }
               else
               {
                  operand.type = D3D10_SB_OPERAND_TYPE_NULL;
               }
            }
            operands->push_back(operand);
         }
         return true;
      };

      // Per temp component: unrelated to a screen position, "base + offset" (axis 0 = x, 1 = y) with the base still in
      // "patch_temp", or derived from one otherwise
      enum class Kind : uint8_t
      {
         None,
         Screen,
         Unknown,
      };
      struct Value
      {
         Kind kind = Kind::None;
         uint8_t axis = 0;
         bool offset = false;
      };
      std::vector<std::array<Value, 4>> temps(temp_count);
      const auto read = [&](const Operand& operand, uint32_t c)
      {
         if (operand.type != D3D10_SB_OPERAND_TYPE_TEMP || operand.index[0] >= temp_count)
            return Value();
         Value value = temps[operand.index[0]][operand.components[c]];
         if (operand.modified && value.kind == Kind::Screen)
         {
            value.kind = Kind::Unknown;
         }
         return value;
      };
      // Screen positions of these axes no longer match "patch_temp" (a new base replaced it, or a branch merges other states)
      const auto forget = [&](uint32_t axes_mask)
      {
         for (auto& temp : temps)
         {
            for (Value& value : temp)
            {
               if (value.kind == Kind::Screen && (axes_mask & (1u << value.axis)))
               {
                  value.kind = Kind::Unknown;
               }
            }
         }
      };
      const auto is_base_row = [](const Operand& operand)
      {
         return operand.type == D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER && !operand.modified && operand.index[0] == offset_constants_slot && operand.index[1] == 0;
      };

      constexpr uint32_t immediate = ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_4_COMPONENT) | ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_IMMEDIATE32);
      constexpr uint32_t base_row_abs = ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_4_COMPONENT) |
                                        ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(D3D10_SB_OPERAND_4_COMPONENT_SWIZZLE_MODE) |
                                        ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SWIZZLE(0, 1, 0, 1) |
                                        ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER) |
                                        ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_2D) |
                                        ENCODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(0, D3D10_SB_OPERAND_INDEX_IMMEDIATE32) |
                                        ENCODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(1, D3D10_SB_OPERAND_INDEX_IMMEDIATE32) |
                                        ENCODE_D3D10_SB_OPERAND_EXTENDED(true);
      constexpr uint32_t two = std::bit_cast<uint32_t>(2.f);
      constexpr uint32_t mask_zw = D3D10_SB_OPERAND_4_COMPONENT_MASK_Z | D3D10_SB_OPERAND_4_COMPONENT_MASK_W;

      std::vector<Operand> operands;
      Stats found;
      for (size_t i = first_body; i < instructions.size(); i++)
      {
         const Instruction& instruction = instructions[i];
         const auto append_instruction = [&]
         { patched.insert(patched.end(), tokens.begin() + instruction.begin, tokens.begin() + instruction.begin + instruction.length); };
         switch (instruction.opcode)
         {
         case D3D10_SB_OPCODE_LABEL:
         case D3D10_SB_OPCODE_CALL:
         case D3D10_SB_OPCODE_CALLC:
         case D3D11_SB_OPCODE_INTERFACE_CALL:
            return {};
         // A branch's state isn't the previous instruction's (an if's body starts from it)
         case D3D10_SB_OPCODE_ELSE:
         case D3D10_SB_OPCODE_ENDIF:
         case D3D10_SB_OPCODE_LOOP:
         case D3D10_SB_OPCODE_ENDLOOP:
         case D3D10_SB_OPCODE_CASE:
         case D3D10_SB_OPCODE_DEFAULT:
         case D3D10_SB_OPCODE_ENDSWITCH:
            forget(0b11);
            append_instruction();
            continue;
         default:
            break;
         }
         if (!read_operands(instruction, &operands))
            return {};
         if (operands.empty())
         {
            append_instruction();
            continue;
         }

         const Operand& destination = operands[0];
         const bool saturate = DECODE_IS_D3D10_SB_INSTRUCTION_SATURATE_ENABLED(tokens[instruction.begin]) != 0;
         std::array<Value, 4> results;
         std::array<uint32_t, 5> save_base = {};
         bool modeled = destination.type == D3D10_SB_OPERAND_TYPE_TEMP && destination.mask_mode && destination.index[0] < temp_count && !saturate;
         if (modeled && instruction.opcode == D3D10_SB_OPCODE_MOV && operands.size() == 2)
         {
            for (uint32_t c = 0; c < 4; c++)
               results[c] = read(operands[1], c);
         }
         else if (modeled && instruction.opcode == D3D10_SB_OPCODE_MAD && operands.size() == 4 && is_base_row(operands[2]) && is_base_row(operands[3]))
         {
            // The base: position * ScreenPositionScaleBias.xy + ScreenPositionScaleBias.wz, per component, copied to "patch_temp" after it
            uint32_t axes_mask = 0;
            std::array<uint32_t, 4> save_components = {0, 0, 0, 0};
            for (uint32_t c = 0; c < 4; c++)
            {
               const uint32_t scale = operands[2].components[c];
               const uint32_t bias = operands[3].components[c];
               const bool base = scale <= 1 && bias == 3 - scale && read(operands[1], c).kind == Kind::None;
               results[c] = (base ? Value{.kind = Kind::Screen, .axis = uint8_t(scale)} : Value{.kind = Kind::Unknown});
               if (base && (destination.mask & (1u << c)))
               {
                  axes_mask |= 1u << scale;
                  save_components[scale] = c;
               }
            }
            if (axes_mask != 0)
            {
               forget(axes_mask);
               save_base = {
                  ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) | ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5),
                  Destination(D3D10_SB_OPERAND_TYPE_TEMP, axes_mask << 4),
                  patch_temp,
                  Source(D3D10_SB_OPERAND_TYPE_TEMP, save_components[0], save_components[1], save_components[0], save_components[0]),
                  destination.index[0],
               };
            }
         }
         else if (modeled && ((instruction.opcode == D3D10_SB_OPCODE_ADD && operands.size() == 3) ||
                                (instruction.opcode == D3D10_SB_OPCODE_MAD && operands.size() == 4)))
         {
            // "base + offset": one addend (either add source, the mad's last) a screen position, every other source unrelated
            for (uint32_t c = 0; c < 4; c++)
            {
               Value screen;
               bool unknown = false;
               for (size_t k = 1; k < operands.size(); k++)
               {
                  const Value value = read(operands[k], c);
                  const bool addend = instruction.opcode == D3D10_SB_OPCODE_ADD || k + 1 == operands.size();
                  if (addend && value.kind == Kind::Screen && screen.kind == Kind::None)
                  {
                     screen = value;
                  }
                  else
                  {
                     unknown |= value.kind != Kind::None;
                  }
               }
               if (unknown)
               {
                  results[c] = {.kind = Kind::Unknown};
               }
               else if (screen.kind == Kind::Screen)
               {
                  results[c] = {.kind = Kind::Screen, .axis = screen.axis, .offset = true};
               }
               else
               {
                  results[c] = Value();
               }
            }
         }
         else
         {
            modeled = false;
         }

         bool texture_read = false;
         switch (instruction.opcode)
         {
         case D3D10_SB_OPCODE_LD:
         case D3D10_SB_OPCODE_LD_MS:
         case D3D11_SB_OPCODE_LD_RAW:
         case D3D11_SB_OPCODE_LD_STRUCTURED:
         case D3D11_SB_OPCODE_LD_UAV_TYPED:
            texture_read = true;
            break;
         case D3D10_SB_OPCODE_SAMPLE:
         case D3D10_SB_OPCODE_SAMPLE_C:
         case D3D10_SB_OPCODE_SAMPLE_C_LZ:
         case D3D10_SB_OPCODE_SAMPLE_L:
         case D3D10_SB_OPCODE_SAMPLE_D:
         case D3D10_SB_OPCODE_SAMPLE_B:
         case D3D10_1_SB_OPCODE_GATHER4:
         case D3D11_SB_OPCODE_GATHER4_C:
         case D3D11_SB_OPCODE_GATHER4_PO:
         case D3D11_SB_OPCODE_GATHER4_PO_C:
         {
            texture_read = true;
            const size_t resource = ((instruction.opcode == D3D11_SB_OPCODE_GATHER4_PO || instruction.opcode == D3D11_SB_OPCODE_GATHER4_PO_C) ? 3 : 2);
            if (operands.size() <= resource)
               break;
            const Operand& coordinate = operands[1];
            const Value u = read(coordinate, 0);
            const Value v = read(coordinate, 1);
            if (u.kind == Kind::None && v.kind == Kind::None)
               break;
            if (operands[resource].type != D3D10_SB_OPERAND_TYPE_RESOURCE || std::ranges::find(scene_slots, operands[resource].index[0]) == scene_slots.end())
               break;
            if (!coordinate.extended && u.kind == Kind::Screen && v.kind == Kind::Screen && u.axis == 0 && v.axis == 1 && (u.offset || v.offset))
            {
               // patch_temp.zw = patch_temp.xy + (coordinate - patch_temp.xy) * |ScreenPositionScaleBias.xy| * 2, read instead of the coordinate
               patched.insert(patched.end(), {
                                                ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ADD) | ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(8),
                                                Destination(D3D10_SB_OPERAND_TYPE_TEMP, mask_zw),
                                                patch_temp,
                                                Source(D3D10_SB_OPERAND_TYPE_TEMP, coordinate.components[0], coordinate.components[1], coordinate.components[0], coordinate.components[1]),
                                                coordinate.index[0],
                                                Source(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1, 0, 1) | ENCODE_D3D10_SB_OPERAND_EXTENDED(true),
                                                ENCODE_D3D10_SB_EXTENDED_OPERAND_MODIFIER(D3D10_SB_OPERAND_MODIFIER_NEG),
                                                patch_temp,
                                                ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MUL) | ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9),
                                                Destination(D3D10_SB_OPERAND_TYPE_TEMP, mask_zw),
                                                patch_temp,
                                                Source(D3D10_SB_OPERAND_TYPE_TEMP, 2, 3, 2, 3),
                                                patch_temp,
                                                base_row_abs,
                                                ENCODE_D3D10_SB_EXTENDED_OPERAND_MODIFIER(D3D10_SB_OPERAND_MODIFIER_ABS),
                                                offset_constants_slot,
                                                0,
                                                ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MAD) | ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(12),
                                                Destination(D3D10_SB_OPERAND_TYPE_TEMP, mask_zw),
                                                patch_temp,
                                                Source(D3D10_SB_OPERAND_TYPE_TEMP, 2, 3, 2, 3),
                                                patch_temp,
                                                immediate,
                                                two,
                                                two,
                                                two,
                                                two,
                                                Source(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1, 0, 1),
                                                patch_temp,
                                             });
               // Same length: a temp operand without extended tokens
               tokens[coordinate.position] = Source(D3D10_SB_OPERAND_TYPE_TEMP, 2, 3, 2, 3);
               tokens[coordinate.position + 1] = patch_temp;
               found.patched_reads++;
            }
            else if (!(u.kind == Kind::Screen && v.kind == Kind::Screen && !u.offset && !v.offset))
            {
               found.refused_reads++;
            }
            break;
         }
         default:
            break;
         }
         append_instruction();
         if (save_base[0] != 0)
         {
            patched.insert(patched.end(), save_base.begin(), save_base.end());
         }

         // Temp destinations (some instructions have two) not modeled above: derived from a screen position if any source is (a texture
         // read's result never is)
         bool derived = false;
         if (!modeled && !texture_read)
         {
            for (const Operand& operand : operands)
            {
               for (uint32_t c = 0; c < 4 && !operand.mask_mode; c++)
                  derived |= read(operand, c).kind != Kind::None;
            }
         }
         for (const Operand& operand : operands)
         {
            if (operand.type != D3D10_SB_OPERAND_TYPE_TEMP || !operand.mask_mode || operand.index[0] >= temp_count)
               continue;
            for (uint32_t c = 0; c < 4; c++)
            {
               if (operand.mask & (1u << c))
               {
                  const Value unmodeled = {.kind = (derived ? Kind::Unknown : Kind::None)};
                  temps[operand.index[0]][c] = ((modeled && &operand == &destination) ? results[c] : unmodeled);
               }
            }
         }
      }
      if (stats)
      {
         *stats = found;
      }
      if (found.patched_reads == 0)
         return {};
      patched[1] = uint32_t(patched.size());
      return patched;
   }
} // namespace SceneOffsetPatch
