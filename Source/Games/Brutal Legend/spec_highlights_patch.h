#pragma once

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

#include "dxbc.h"

// Specular highlight energy normalization: rewrites the Phong specular pow() that dgVoodoo translates from D3D9 (log, movc of the -16.609640 literal,
// mul by the shininess cb4 scalar K, exp) so its result is scaled by a gain derived from K:
// gain = (K + 1) / (ref_exponent + 1), clamped to [min_gain, max_gain], gamma-adjusted (gain^(1/gamma), skipped when gamma == 1),
// lerped toward 1 by strength, and set to 1 unless 0 < K <= 2000. Narrower highlights (higher K) get proportionally more energy.
// Two idioms are patched: A (the dgVoodoo pow with the -16.609640 movc literal) and B (log / mul K / ne / movc 0 / exp, a NaN-guarded
// pow). B sites are only patched when their K matches the K of an A site in the same shader (guards against non-specular pows).
// Sites that don't match the pattern exactly are left untouched.
// Returns false (with "error") if the container isn't DXBC or no site matched. Otherwise "out_code" is the patched container (valid
// checksum) and "out_sites" the number of sites patched. Only the new temp and the inserted instructions change; everything else is copied.
namespace SpecularHighlights
{
   struct Params
   {
      float strength = 1.f;
      float ref_exponent = 16.f;
      float min_gain = 0.25f;
      float max_gain = 8.f;
      float gamma = 2.2f; // 1 = no gamma adjustment
   };

   namespace Detail
   {
      using Tokens = std::vector<uint32_t>;

      constexpr uint32_t kMaskX = D3D10_SB_OPERAND_4_COMPONENT_MASK_X;
      constexpr uint32_t kMaskY = D3D10_SB_OPERAND_4_COMPONENT_MASK_Y;
      constexpr float kLowLiteral = -16.609640f; // The movc literal of the dgVoodoo pow translation
      constexpr float kImmediateTolerance = 1e-4f; // Absolute tolerance of IsImmediateNear
      constexpr float kMaxShininess = 2000.f;      // The gain applies only for 0 < K <= this
      constexpr uint32_t kShininessSlot = 4;     // cb4 holds the shader constants of the specular term
      constexpr size_t kMaxSiteLength = 12;      // log to exp, at most this many instructions
      constexpr size_t kLiteralWindow = 6;       // the literal movc comes at most this far after the log

      struct Operand
      {
         size_t begin;
         size_t end;
      };

      struct Site
      {
         size_t log;       // Index of the log of the specular dot
         size_t exp;       // Index of the exp, the gain goes after it
         Tokens shininess; // The K operand (cb4 scalar), copied as is
         Tokens dest;      // The exp's destination, copied as is
         uint32_t dest_reg;
         uint32_t k_row;       // The K operand's cb4 row
         uint32_t k_component; // The K operand's scalar component
      };

      inline bool IsControlFlow(D3D10_SB_OPCODE_TYPE opcode)
      {
         switch (opcode)
         {
         case D3D10_SB_OPCODE_IF:
         case D3D10_SB_OPCODE_ELSE:
         case D3D10_SB_OPCODE_ENDIF:
         case D3D10_SB_OPCODE_LOOP:
         case D3D10_SB_OPCODE_ENDLOOP:
         case D3D10_SB_OPCODE_BREAK:
         case D3D10_SB_OPCODE_BREAKC:
         case D3D10_SB_OPCODE_CONTINUE:
         case D3D10_SB_OPCODE_CONTINUEC:
         case D3D10_SB_OPCODE_SWITCH:
         case D3D10_SB_OPCODE_CASE:
         case D3D10_SB_OPCODE_DEFAULT:
         case D3D10_SB_OPCODE_ENDSWITCH:
         case D3D10_SB_OPCODE_RET:
         case D3D10_SB_OPCODE_RETC:
         case D3D10_SB_OPCODE_CALL:
         case D3D10_SB_OPCODE_CALLC:
         case D3D10_SB_OPCODE_LABEL:
            return true;
         default:
            return false;
         }
      }

      // The operands of a body instruction (no extended opcode tokens). False if they can't be split.
      inline bool ReadOperands(const Tokens& tokens, const DXBC::Instruction& instruction, std::vector<Operand>* operands)
      {
         if (DECODE_IS_D3D10_SB_OPCODE_EXTENDED(tokens[instruction.begin]))
            return false;
         const size_t end = instruction.begin + instruction.length;
         size_t i = instruction.begin + 1;
         while (i < end)
         {
            const size_t next = DXBC::WalkOperand(tokens, i, end, [](size_t, size_t)
               { return true; });
            if (next == 0)
               return false;
            operands->push_back({i, next});
            i = next;
         }
         return true;
      }

      // Index tokens start after the operand token and its extended (modifier) tokens
      inline size_t IndexStart(const Tokens& tokens, const Operand& operand)
      {
         size_t i = operand.begin + 1;
         uint32_t token = tokens[operand.begin];
         while (DECODE_IS_D3D10_SB_OPERAND_EXTENDED(token))
            token = tokens[i++];
         return i;
      }

      // A temp operand with an immediate index: its register number
      inline bool IsTempRegister(const Tokens& tokens, const Operand& operand, uint32_t* reg)
      {
         const uint32_t token = tokens[operand.begin];
         if (DECODE_D3D10_SB_OPERAND_TYPE(token) != D3D10_SB_OPERAND_TYPE_TEMP || DECODE_D3D10_SB_OPERAND_INDEX_DIMENSION(token) != D3D10_SB_OPERAND_INDEX_1D ||
             DECODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(0, token) != D3D10_SB_OPERAND_INDEX_IMMEDIATE32)
            return false;
         const size_t index = IndexStart(tokens, operand);
         if (operand.end != index + 1)
            return false;
         *reg = tokens[index];
         return true;
      }

      // A constant buffer operand cb[slot][row] with immediate indices
      inline bool IsConstantBuffer(const Tokens& tokens, const Operand& operand, uint32_t slot)
      {
         const uint32_t token = tokens[operand.begin];
         if (DECODE_D3D10_SB_OPERAND_TYPE(token) != D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER || DECODE_D3D10_SB_OPERAND_INDEX_DIMENSION(token) != D3D10_SB_OPERAND_INDEX_2D ||
             DECODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(0, token) != D3D10_SB_OPERAND_INDEX_IMMEDIATE32 ||
             DECODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(1, token) != D3D10_SB_OPERAND_INDEX_IMMEDIATE32)
            return false;
         const size_t index = IndexStart(tokens, operand);
         return operand.end == index + 2 && tokens[index] == slot;
      }

      // Whether a destination writes component "component" (0-3)
      inline bool WritesComponent(uint32_t token, uint32_t component)
      {
         return DECODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(token) == D3D10_SB_OPERAND_4_COMPONENT_MASK_MODE &&
                (DECODE_D3D10_SB_OPERAND_4_COMPONENT_MASK(token) & (0x10u << component)) != 0;
      }

      // The single component a source reads for every component (a replicated swizzle or a select), false otherwise
      inline bool ReplicatedComponent(uint32_t token, uint32_t* component)
      {
         const auto mode = DECODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(token);
         if (mode == D3D10_SB_OPERAND_4_COMPONENT_SELECT_1_MODE)
         {
            *component = DECODE_D3D10_SB_OPERAND_4_COMPONENT_SELECT_1(token);
            return true;
         }
         if (mode != D3D10_SB_OPERAND_4_COMPONENT_SWIZZLE_MODE)
            return false;
         const uint32_t first = DECODE_D3D10_SB_OPERAND_4_COMPONENT_SWIZZLE_SOURCE(token, 0);
         for (uint32_t c = 1; c < 4; c++)
         {
            if (DECODE_D3D10_SB_OPERAND_4_COMPONENT_SWIZZLE_SOURCE(token, c) != first)
               return false;
         }
         *component = first;
         return true;
      }

      // An immediate32 operand whose components all are "value" (within a small tolerance: the dump prints six decimals)
      inline bool IsImmediateNear(const Tokens& tokens, const Operand& operand, float value)
      {
         const uint32_t token = tokens[operand.begin];
         if (DECODE_D3D10_SB_OPERAND_TYPE(token) != D3D10_SB_OPERAND_TYPE_IMMEDIATE32)
            return false;
         const uint32_t count = (DECODE_D3D10_SB_OPERAND_NUM_COMPONENTS(token) == D3D10_SB_OPERAND_4_COMPONENT) ? 4 : 1;
         if (operand.end != operand.begin + 1 + count)
            return false;
         for (uint32_t i = 0; i < count; i++)
         {
            if (!(std::fabs(std::bit_cast<float>(tokens[operand.begin + 1 + i]) - value) <= kImmediateTolerance))
               return false;
         }
         return true;
      }

      inline bool Saturates(const Tokens& tokens, const DXBC::Instruction& instruction)
      {
         return DECODE_IS_D3D10_SB_INSTRUCTION_SATURATE_ENABLED(tokens[instruction.begin]) != 0;
      }

      // Whether the last write of component "component" of temp "reg" before "index" (straight line of the body) is a saturated dp3
      // (any operands). False if it's anything else or there's control flow on the way
      inline bool IsSaturatedDotBefore(const Tokens& tokens, const std::vector<DXBC::Instruction>& instructions, size_t first_body, size_t index, uint32_t reg, uint32_t component)
      {
         for (size_t k = index; k-- > first_body;)
         {
            const DXBC::Instruction& instruction = instructions[k];
            if (IsControlFlow(instruction.opcode))
               return false;
            if (instruction.opcode == D3D10_SB_OPCODE_DISCARD)
               continue;
            std::vector<Operand> operands;
            if (!ReadOperands(tokens, instruction, &operands) || operands.empty())
               return false;
            uint32_t written = 0;
            if (!IsTempRegister(tokens, operands[0], &written) || written != reg || !WritesComponent(tokens[operands[0].begin], component))
               continue;
            return instruction.opcode == D3D10_SB_OPCODE_DP3 && Saturates(tokens, instruction);
         }
         return false;
      }

      // The lowest component a destination token writes (mask mode), false if none
      inline bool FirstWrittenComponent(uint32_t token, uint32_t* component)
      {
         for (uint32_t c = 0; c < 4; c++)
         {
            if (WritesComponent(token, c))
            {
               *component = c;
               return true;
            }
         }
         return false;
      }

      // A temp operand of register "reg" that reads a single component (replicated) into "component"
      inline bool IsRegisterReplicated(const Tokens& tokens, const Operand& operand, uint32_t reg, uint32_t* component)
      {
         uint32_t temp = 0;
         return IsTempRegister(tokens, operand, &temp) && temp == reg && ReplicatedComponent(tokens[operand.begin], component);
      }

      // Whether the last write of component "component" of temp "reg" before "index", within the straight line of the body, is a dp3
      // with a cb4 operand (the specular dot). False if it's anything else or there's control flow on the way
      inline bool IsSpecularDotBefore(const Tokens& tokens, const std::vector<DXBC::Instruction>& instructions, size_t first_body, size_t index, uint32_t reg, uint32_t component)
      {
         for (size_t k = index; k-- > first_body;)
         {
            const DXBC::Instruction& instruction = instructions[k];
            if (IsControlFlow(instruction.opcode))
               return false;
            if (instruction.opcode == D3D10_SB_OPCODE_DISCARD)
               continue;
            std::vector<Operand> operands;
            if (!ReadOperands(tokens, instruction, &operands) || operands.empty())
               return false;
            uint32_t written = 0;
            if (!IsTempRegister(tokens, operands[0], &written) || written != reg || !WritesComponent(tokens[operands[0].begin], component))
               continue;
            if (instruction.opcode != D3D10_SB_OPCODE_DP3)
               return false;
            return std::any_of(operands.begin() + 1, operands.end(), [&](const Operand& operand)
               { return IsConstantBuffer(tokens, operand, kShininessSlot); });
         }
         return false;
      }

      // Finds the sites: log of the specular dot, then (within a few instructions) a movc of the literal, and mul by K immediately before exp.
      // Sites don't overlap.
      inline std::vector<Site> FindSites(const Tokens& tokens, const std::vector<DXBC::Instruction>& instructions, size_t first_body)
      {
         std::vector<Site> sites;
         size_t last_exp = 0;
         for (size_t i = first_body; i < instructions.size(); i++)
         {
            const DXBC::Instruction& log = instructions[i];
            if (log.opcode != D3D10_SB_OPCODE_LOG || i < last_exp)
               continue;
            std::vector<Operand> log_operands;
            if (!ReadOperands(tokens, log, &log_operands) || log_operands.size() != 2)
               continue;
            uint32_t log_reg = 0, log_component = 0;
            if (!IsTempRegister(tokens, log_operands[1], &log_reg) || !ReplicatedComponent(tokens[log_operands[1].begin], &log_component))
               continue;
            if (!IsSpecularDotBefore(tokens, instructions, first_body, i, log_reg, log_component))
               continue;

            // The literal movc, then mul and exp, straight line
            size_t literal = 0, exp = 0;
            for (size_t j = i + 1; j < instructions.size() && j <= i + kMaxSiteLength; j++)
            {
               const DXBC::Instruction& instruction = instructions[j];
               if (IsControlFlow(instruction.opcode))
                  break;
               if (instruction.opcode == D3D10_SB_OPCODE_EXP)
               {
                  exp = j;
                  break;
               }
               if (instruction.opcode == D3D10_SB_OPCODE_MOVC && j <= i + kLiteralWindow && literal == 0)
               {
                  std::vector<Operand> operands;
                  if (ReadOperands(tokens, instruction, &operands) && std::any_of(operands.begin(), operands.end(), [&](const Operand& operand)
                                                                          { return IsImmediateNear(tokens, operand, kLowLiteral); }))
                     literal = j;
               }
            }
            if (literal == 0 || exp == 0 || exp - 1 <= literal)
               continue;

            const DXBC::Instruction& mul = instructions[exp - 1];
            const DXBC::Instruction& exp_instruction = instructions[exp];
            std::vector<Operand> mul_operands, exp_operands;
            if (mul.opcode != D3D10_SB_OPCODE_MUL || Saturates(tokens, mul) || Saturates(tokens, exp_instruction) || !ReadOperands(tokens, mul, &mul_operands) ||
                mul_operands.size() != 3 || !ReadOperands(tokens, exp_instruction, &exp_operands) || exp_operands.size() != 2)
               continue;

            // exp rD, rM.c with the mul before it writing rM.c = rM.c * K
            uint32_t dest_reg = 0, exp_source = 0, mul_dest = 0, exp_component = 0;
            if (!IsTempRegister(tokens, exp_operands[0], &dest_reg) || !IsTempRegister(tokens, exp_operands[1], &exp_source) ||
                !ReplicatedComponent(tokens[exp_operands[1].begin], &exp_component) || !IsTempRegister(tokens, mul_operands[0], &mul_dest) || mul_dest != exp_source ||
                !WritesComponent(tokens[mul_operands[0].begin], exp_component))
               continue;
            const Operand* shininess = nullptr;
            for (const auto [k, t] : {std::pair<size_t, size_t>{1, 2}, std::pair<size_t, size_t>{2, 1}})
            {
               uint32_t temp_reg = 0, temp_component = 0, scalar_component = 0;
               if (!IsConstantBuffer(tokens, mul_operands[k], kShininessSlot) || !ReplicatedComponent(tokens[mul_operands[k].begin], &scalar_component))
                  continue;
               if (IsTempRegister(tokens, mul_operands[t], &temp_reg) && temp_reg == exp_source && ReplicatedComponent(tokens[mul_operands[t].begin], &temp_component) &&
                   temp_component == exp_component)
               {
                  shininess = &mul_operands[k];
                  break;
               }
            }
            if (!shininess)
               continue;

            Site site;
            site.log = i;
            site.exp = exp;
            site.shininess.assign(tokens.begin() + shininess->begin, tokens.begin() + shininess->end);
            site.dest.assign(tokens.begin() + exp_operands[0].begin, tokens.begin() + exp_operands[0].end);
            site.dest_reg = dest_reg;
            site.k_row = tokens[IndexStart(tokens, *shininess) + 1];
            ReplicatedComponent(tokens[shininess->begin], &site.k_component);
            sites.push_back(std::move(site));
            last_exp = exp + 1;
         }
         return sites;
      }

      // Idiom B, 5 consecutive instructions from the log: log r, |dot| / mul r, r, K / ne r2, r, r / movc r, r2, 0, r / exp d, r.
      // Sites must not overlap the idiom A sites (their log..exp range) or each other. Only sites whose K (same cb4 row and component) is
      // the K of an idiom A site of this shader are returned.
      inline std::vector<Site> FindSitesB(const Tokens& tokens, const std::vector<DXBC::Instruction>& instructions, size_t first_body, const std::vector<Site>& a_sites)
      {
         std::vector<Site> sites;
         size_t last_exp = 0;
         for (size_t i = first_body; i + 4 < instructions.size(); i++)
         {
            const DXBC::Instruction& log = instructions[i];
            if (log.opcode != D3D10_SB_OPCODE_LOG || i < last_exp)
               continue;
            const bool overlaps_a = std::any_of(a_sites.begin(), a_sites.end(), [&](const Site& a)
               { return i <= a.exp && a.log <= i + 4; });
            if (overlaps_a)
               continue;

            // log rL, |rS.c| with rS.c the last write of a specular dp3 with a cb4 operand
            std::vector<Operand> log_operands;
            if (!ReadOperands(tokens, log, &log_operands) || log_operands.size() != 2)
               continue;
            uint32_t log_reg = 0, log_component = 0, dot_reg = 0, dot_component = 0;
            if (!IsTempRegister(tokens, log_operands[0], &log_reg) || !FirstWrittenComponent(tokens[log_operands[0].begin], &log_component) ||
                !IsTempRegister(tokens, log_operands[1], &dot_reg) || !ReplicatedComponent(tokens[log_operands[1].begin], &dot_component) ||
                !IsSaturatedDotBefore(tokens, instructions, first_body, i, dot_reg, dot_component))
               continue;

            // mul rL.c, rL.cccc, K (either source order)
            const DXBC::Instruction& mul = instructions[i + 1];
            std::vector<Operand> mul_operands;
            if (mul.opcode != D3D10_SB_OPCODE_MUL || Saturates(tokens, mul) || !ReadOperands(tokens, mul, &mul_operands) || mul_operands.size() != 3)
               continue;
            uint32_t mul_reg = 0, k_component = 0;
            const Operand* shininess = nullptr;
            for (const auto [k, t] : {std::pair<size_t, size_t>{1, 2}, std::pair<size_t, size_t>{2, 1}})
            {
               uint32_t temp_component = 0;
               if (!IsConstantBuffer(tokens, mul_operands[k], kShininessSlot) || !ReplicatedComponent(tokens[mul_operands[k].begin], &k_component))
                  continue;
               if (IsRegisterReplicated(tokens, mul_operands[t], log_reg, &temp_component) && temp_component == log_component)
               {
                  shininess = &mul_operands[k];
                  break;
               }
            }
            if (!shininess || !IsTempRegister(tokens, mul_operands[0], &mul_reg) || mul_reg != log_reg || !WritesComponent(tokens[mul_operands[0].begin], log_component))
               continue;

            // ne rN.y, rL.c, rL.c (NaN check)
            const DXBC::Instruction& ne = instructions[i + 2];
            std::vector<Operand> ne_operands;
            uint32_t ne_reg = 0, ne_a = 0, ne_b = 0;
            if (ne.opcode != D3D10_SB_OPCODE_NE || Saturates(tokens, ne) || !ReadOperands(tokens, ne, &ne_operands) || ne_operands.size() != 3 ||
                !IsTempRegister(tokens, ne_operands[0], &ne_reg) || !WritesComponent(tokens[ne_operands[0].begin], 1) ||
                !IsRegisterReplicated(tokens, ne_operands[1], log_reg, &ne_a) || !IsRegisterReplicated(tokens, ne_operands[2], log_reg, &ne_b) || ne_a != log_component ||
                ne_b != log_component)
               continue;

            // movc rL.c, rN.y, l(0), rL.c
            const DXBC::Instruction& movc = instructions[i + 3];
            std::vector<Operand> movc_operands;
            uint32_t movc_dest = 0, movc_cond_component = 0, movc_src = 0;
            if (movc.opcode != D3D10_SB_OPCODE_MOVC || Saturates(tokens, movc) || !ReadOperands(tokens, movc, &movc_operands) || movc_operands.size() != 4 ||
                !IsTempRegister(tokens, movc_operands[0], &movc_dest) || movc_dest != log_reg || !WritesComponent(tokens[movc_operands[0].begin], log_component) ||
                !IsRegisterReplicated(tokens, movc_operands[1], ne_reg, &movc_cond_component) || movc_cond_component != 1 ||
                !IsImmediateNear(tokens, movc_operands[2], 0.f) || !IsRegisterReplicated(tokens, movc_operands[3], log_reg, &movc_src) || movc_src != log_component)
               continue;

            // exp rD, rL.c
            const DXBC::Instruction& exp_instruction = instructions[i + 4];
            std::vector<Operand> exp_operands;
            uint32_t dest_reg = 0, exp_source = 0;
            if (exp_instruction.opcode != D3D10_SB_OPCODE_EXP || Saturates(tokens, exp_instruction) || !ReadOperands(tokens, exp_instruction, &exp_operands) ||
                exp_operands.size() != 2 || !IsTempRegister(tokens, exp_operands[0], &dest_reg) || !IsRegisterReplicated(tokens, exp_operands[1], log_reg, &exp_source) ||
                exp_source != log_component)
               continue;

            const bool k_matches = std::any_of(a_sites.begin(), a_sites.end(), [&](const Site& a)
               { return a.k_row == tokens[IndexStart(tokens, *shininess) + 1] && a.k_component == k_component; });
            if (!k_matches)
               continue;

            Site site;
            site.log = i;
            site.exp = i + 4;
            site.shininess.assign(tokens.begin() + shininess->begin, tokens.begin() + shininess->end);
            site.dest.assign(tokens.begin() + exp_operands[0].begin, tokens.begin() + exp_operands[0].end);
            site.dest_reg = dest_reg;
            site.k_row = tokens[IndexStart(tokens, *shininess) + 1];
            site.k_component = k_component;
            sites.push_back(std::move(site));
            last_exp = i + 5;
         }
         return sites;
      }

      inline Tokens Dst(uint32_t reg, uint32_t mask)
      {
         return {DXBC::Destination(D3D10_SB_OPERAND_TYPE_TEMP, mask), reg};
      }
      inline Tokens Src(uint32_t reg, uint32_t x, uint32_t y, uint32_t z, uint32_t w)
      {
         return {DXBC::Source(D3D10_SB_OPERAND_TYPE_TEMP, x, y, z, w), reg};
      }
      inline Tokens Imm(float value)
      {
         const uint32_t bits = std::bit_cast<uint32_t>(value);
         return {uint32_t(ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_4_COMPONENT) | ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_IMMEDIATE32)), bits, bits, bits, bits};
      }

      // One instruction: opcode token (with its length) and operands
      inline Tokens Op(D3D10_SB_OPCODE_TYPE opcode, std::initializer_list<Tokens> operands)
      {
         Tokens out = {uint32_t(ENCODE_D3D10_SB_OPCODE_TYPE(opcode))};
         for (const Tokens& operand : operands)
            out.insert(out.end(), operand.begin(), operand.end());
         out[0] |= uint32_t(ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(out.size()));
         return out;
      }

      // The gain after an exp, using rG.x (gain) and rG.y (valid mask): rG.y = (0 < K) & (K <= 2000) (NaN is invalid), rG.x = (K + 1) / (ref + 1)
      // clamped to [min, max], gamma adjusted (skipped when gamma == 1), lerped by strength, then rG.x = valid ? rG.x : 1, then D *= rG.x
      inline Tokens GainInstructions(const Site& site, uint32_t gain, const Params& p)
      {
         const float scale = 1.f / (p.ref_exponent + 1.f);
         const Tokens gx = Src(gain, 0, 0, 0, 0);
         const Tokens gy = Src(gain, 1, 1, 1, 1);
         Tokens out;
         const auto add = [&](const Tokens& instruction)
         { out.insert(out.end(), instruction.begin(), instruction.end()); };
         add(Op(D3D10_SB_OPCODE_LT, {Dst(gain, kMaskY), Imm(0.f), site.shininess}));
         add(Op(D3D10_SB_OPCODE_GE, {Dst(gain, kMaskX), Imm(kMaxShininess), site.shininess}));
         add(Op(D3D10_SB_OPCODE_AND, {Dst(gain, kMaskY), gx, gy}));
         add(Op(D3D10_SB_OPCODE_MAD, {Dst(gain, kMaskX), site.shininess, Imm(scale), Imm(scale)}));
         add(Op(D3D10_SB_OPCODE_MAX, {Dst(gain, kMaskX), gx, Imm(p.min_gain)}));
         add(Op(D3D10_SB_OPCODE_MIN, {Dst(gain, kMaskX), gx, Imm(p.max_gain)}));
         if (p.gamma != 1.f)
         {
            add(Op(D3D10_SB_OPCODE_LOG, {Dst(gain, kMaskX), gx}));
            add(Op(D3D10_SB_OPCODE_MUL, {Dst(gain, kMaskX), gx, Imm(1.f / p.gamma)}));
            add(Op(D3D10_SB_OPCODE_EXP, {Dst(gain, kMaskX), gx}));
         }
         add(Op(D3D10_SB_OPCODE_MAD, {Dst(gain, kMaskX), gx, Imm(p.strength), Imm(1.f - p.strength)}));
         add(Op(D3D10_SB_OPCODE_MOVC, {Dst(gain, kMaskX), gy, gx, Imm(1.f)}));
         add(Op(D3D10_SB_OPCODE_MUL, {site.dest, Src(site.dest_reg, 0, 1, 2, 3), gx}));
         return out;
      }
   } // namespace Detail

   // Patches the specular pow sites of a pixel shader (see the top of this file). False and "error" if there are none or it isn't DXBC.
   inline bool PatchSpecularPow(const void* code, size_t size, const Params& p, std::vector<uint8_t>* out_code, uint32_t* out_sites, std::string* error)
   {
      if (out_sites)
         *out_sites = 0;
      const auto fail = [&](const char* message)
      {
         if (error)
            *error = message;
         return false;
      };
      if (!std::isfinite(p.strength) || !(p.ref_exponent + 1.f > 0.f) || !(p.min_gain <= p.max_gain) || !std::isfinite(p.gamma) || !(p.gamma > 0.f))
         return fail("invalid params");

      std::vector<DXBC::Chunk> chunks;
      if (!DXBC::ReadChunks(static_cast<const uint8_t*>(code), size, &chunks))
         return fail("not a DXBC container");
      DXBC::Chunk* const program = DXBC::FindChunk(&chunks, DXBC::FourCC("SHEX"), DXBC::FourCC("SHDR"));
      if (!program)
         return fail("no program");

      Detail::Tokens tokens;
      std::vector<DXBC::Instruction> instructions;
      size_t first_body = 0;
      if (!DXBC::ReadProgram(*program, &tokens, &instructions, &first_body))
         return fail("unreadable program");

      std::vector<Detail::Site> sites = Detail::FindSites(tokens, instructions, first_body);
      const std::vector<Detail::Site> b_sites = Detail::FindSitesB(tokens, instructions, first_body, sites);
      sites.insert(sites.end(), b_sites.begin(), b_sites.end());
      std::sort(sites.begin(), sites.end(), [](const Detail::Site& a, const Detail::Site& b)
         { return a.exp < b.exp; });
      if (sites.empty())
         return fail("no specular pow site");

      // The gain temp is the first new one (after the shader's own temps)
      uint32_t gain = 0;
      Detail::Tokens patched = DXBC::CopyDeclarations(tokens, instructions, first_body, 1, &gain, [](size_t)
         { return Detail::Tokens(); });
      size_t next_site = 0;
      for (size_t i = first_body; i < instructions.size(); i++)
      {
         const DXBC::Instruction& instruction = instructions[i];
         patched.insert(patched.end(), tokens.begin() + instruction.begin, tokens.begin() + instruction.begin + instruction.length);
         if (next_site < sites.size() && sites[next_site].exp == i)
         {
            const Detail::Tokens gain_instructions = Detail::GainInstructions(sites[next_site], gain, p);
            patched.insert(patched.end(), gain_instructions.begin(), gain_instructions.end());
            next_site++;
         }
      }

      DXBC::WriteProgram(&patched, program);
      if (out_code)
         *out_code = DXBC::WriteChunks(chunks);
      if (out_sites)
         *out_sites = uint32_t(sites.size());
      if (error)
         error->clear();
      return true;
   }
} // namespace SpecularHighlights
