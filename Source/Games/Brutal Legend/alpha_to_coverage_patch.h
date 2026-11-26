#pragma once

#include <algorithm>
#include <bit>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

#include "dxbc.h"

// Foliage Alpha to Coverage: rewrites a pixel shader with dgVoodoo's D3D9 alpha test (ftou(a * 255) against cb3[8].z, then discard_nz)
// so that the discard is replaced by a coverage alpha, written to o0.w. The alpha is compared with the threshold after the screen
// space derivatives of the tested alpha sharpen it (an alpha test sampled every pixel becomes a one pixel wide edge), and that
// value goes to o0.w before every ret, so it's the final output alpha whatever the game wrote to o0 before.
// Empty output and false (with "error") if the pattern isn't found exactly once, or o0 isn't a float4 target.
namespace AlphaToCoverage
{
   using Tokens = std::vector<uint32_t>;

   constexpr uint32_t kMaskX = D3D10_SB_OPERAND_4_COMPONENT_MASK_X;
   constexpr uint32_t kMaskY = D3D10_SB_OPERAND_4_COMPONENT_MASK_Y;
   constexpr uint32_t kMaskZ = D3D10_SB_OPERAND_4_COMPONENT_MASK_Z;
   constexpr uint32_t kMaskW = D3D10_SB_OPERAND_4_COMPONENT_MASK_W;
   constexpr float kAlphaTestScale = 255.0f;            // dgVoodoo scales the alpha to 0-255 before the ftou
   constexpr float kAlphaTestScaleInverse = -0.00392156863f; // -1 / 255, the literal the coverage math uses

   // One operand of an instruction: its first token and the position after it
   struct Operand
   {
      size_t begin;
      size_t end;
   };

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
         operands->push_back({.begin = i, .end = next});
         i = next;
      }
      return true;
   }

   // A 1D register operand of "type" with an immediate index (no extended tokens): its register number
   inline bool IsRegister(const Tokens& tokens, const Operand& operand, D3D10_SB_OPERAND_TYPE type, uint32_t* reg)
   {
      const uint32_t token = tokens[operand.begin];
      if (DECODE_IS_D3D10_SB_OPERAND_EXTENDED(token) || DECODE_D3D10_SB_OPERAND_TYPE(token) != type || DECODE_D3D10_SB_OPERAND_INDEX_DIMENSION(token) != D3D10_SB_OPERAND_INDEX_1D || operand.end != operand.begin + 2)
         return false;
      *reg = tokens[operand.begin + 1];
      return true;
   }

   // Whether a temp operand is register "reg"
   inline bool IsTemp(const Tokens& tokens, const Operand& operand, uint32_t reg)
   {
      uint32_t found = 0;
      return IsRegister(tokens, operand, D3D10_SB_OPERAND_TYPE_TEMP, &found) && found == reg;
   }

   // A destination operand with the write mask "mask" (token position, like D3D10_SB_OPERAND_4_COMPONENT_MASK_W)
   inline bool HasMask(uint32_t token, uint32_t mask)
   {
      return DECODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(token) == D3D10_SB_OPERAND_4_COMPONENT_MASK_MODE && DECODE_D3D10_SB_OPERAND_4_COMPONENT_MASK(token) == mask;
   }

   // The component (0-3) of a source operand that component "component" (0-3) reads, or -1 for other selection modes
   inline int SourceComponent(uint32_t token, uint32_t component)
   {
      const auto mode = DECODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(token);
      if (mode == D3D10_SB_OPERAND_4_COMPONENT_SWIZZLE_MODE)
         return static_cast<int>(DECODE_D3D10_SB_OPERAND_4_COMPONENT_SWIZZLE_SOURCE(token, component));
      if (mode == D3D10_SB_OPERAND_4_COMPONENT_SELECT_1_MODE)
         return static_cast<int>(DECODE_D3D10_SB_OPERAND_4_COMPONENT_SELECT_1(token));
      return -1;
   }

   // An immediate operand whose components (all of them, for a vector) are "value"
   inline bool IsImmediate(const Tokens& tokens, const Operand& operand, float value)
   {
      const uint32_t token = tokens[operand.begin];
      if (DECODE_IS_D3D10_SB_OPERAND_EXTENDED(token) || DECODE_D3D10_SB_OPERAND_TYPE(token) != D3D10_SB_OPERAND_TYPE_IMMEDIATE32)
         return false;
      const uint32_t count = (DECODE_D3D10_SB_OPERAND_NUM_COMPONENTS(token) == D3D10_SB_OPERAND_4_COMPONENT) ? 4 : 1;
      if (operand.end != operand.begin + 1 + count)
         return false;
      for (uint32_t i = 0; i < count; i++)
      {
         if (tokens[operand.begin + 1 + i] != std::bit_cast<uint32_t>(value))
            return false;
      }
      return true;
   }

   // A constant buffer operand cb[slot][row] whose component "component" (0-3) is read with an immediate index
   inline bool IsConstantBuffer(const Tokens& tokens, const Operand& operand, uint32_t slot, uint32_t row, uint32_t component)
   {
      const uint32_t token = tokens[operand.begin];
      return !DECODE_IS_D3D10_SB_OPERAND_EXTENDED(token) && DECODE_D3D10_SB_OPERAND_TYPE(token) == D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER &&
             DECODE_D3D10_SB_OPERAND_INDEX_DIMENSION(token) == D3D10_SB_OPERAND_INDEX_2D &&
             DECODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(0, token) == D3D10_SB_OPERAND_INDEX_IMMEDIATE32 &&
             DECODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(1, token) == D3D10_SB_OPERAND_INDEX_IMMEDIATE32 &&
             operand.end == operand.begin + 3 && tokens[operand.begin + 1] == slot && tokens[operand.begin + 2] == row &&
             SourceComponent(token, 0) == static_cast<int>(component);
   }

   // The four instructions at "i": mad rT.w, rA.w, l(255.0), l(0.0001) / ftou rT.w, rT.w / uge rT.w, cb3[8].z, rT.w / discard_nz rT.w
   // Returns rT (test) and rA (alpha) if they are there.
   inline bool MatchAlphaTest(const Tokens& tokens, const std::vector<DXBC::Instruction>& instructions, size_t i, uint32_t* test, uint32_t* alpha)
   {
      if (i + 4 > instructions.size())
         return false;
      if (instructions[i].opcode != D3D10_SB_OPCODE_MAD || instructions[i + 1].opcode != D3D10_SB_OPCODE_FTOU || instructions[i + 2].opcode != D3D10_SB_OPCODE_UGE ||
          instructions[i + 3].opcode != D3D10_SB_OPCODE_DISCARD)
         return false;
      if (DECODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(tokens[instructions[i + 3].begin]) != D3D10_SB_INSTRUCTION_TEST_NONZERO)
         return false;
      std::vector<Operand> mad, ftou, uge, discard;
      if (!ReadOperands(tokens, instructions[i], &mad) || !ReadOperands(tokens, instructions[i + 1], &ftou) || !ReadOperands(tokens, instructions[i + 2], &uge) ||
          !ReadOperands(tokens, instructions[i + 3], &discard))
         return false;
      if (mad.size() != 4 || ftou.size() != 2 || uge.size() != 3 || discard.size() != 1)
         return false;

      // mad rT.w, rA.w, l(255.0), l(0.0001)
      uint32_t reg_test = 0, reg_alpha = 0;
      if (!IsRegister(tokens, mad[0], D3D10_SB_OPERAND_TYPE_TEMP, &reg_test) || !HasMask(tokens[mad[0].begin], kMaskW) ||
          !IsRegister(tokens, mad[1], D3D10_SB_OPERAND_TYPE_TEMP, &reg_alpha) || SourceComponent(tokens[mad[1].begin], 3) != 3 ||
          !IsImmediate(tokens, mad[2], kAlphaTestScale) || !IsImmediate(tokens, mad[3], 0.0001f))
         return false;
      // ftou rT.w, rT.w
      if (!IsTemp(tokens, ftou[0], reg_test) || !HasMask(tokens[ftou[0].begin], kMaskW) || !IsTemp(tokens, ftou[1], reg_test) || SourceComponent(tokens[ftou[1].begin], 3) != 3)
         return false;
      // uge rT.w, cb3[8].z, rT.w
      if (!IsTemp(tokens, uge[0], reg_test) || !HasMask(tokens[uge[0].begin], kMaskW) || !IsConstantBuffer(tokens, uge[1], 3, 8, 2) || !IsTemp(tokens, uge[2], reg_test) ||
          SourceComponent(tokens[uge[2].begin], 3) != 3)
         return false;
      // discard_nz rT.w
      if (!IsTemp(tokens, discard[0], reg_test) || SourceComponent(tokens[discard[0].begin], 3) != 3)
         return false;

      *test = reg_test;
      *alpha = reg_alpha;
      return true;
   }

   // Operand tokens
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
   inline Tokens ConstantBuffer(uint32_t slot, uint32_t row, uint32_t x, uint32_t y, uint32_t z, uint32_t w)
   {
      const uint32_t token = uint32_t(ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_4_COMPONENT) |
                                      ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(D3D10_SB_OPERAND_4_COMPONENT_SWIZZLE_MODE) | ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SWIZZLE(x, y, z, w) |
                                      ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER) | ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_2D) |
                                      ENCODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(0, D3D10_SB_OPERAND_INDEX_IMMEDIATE32) |
                                      ENCODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(1, D3D10_SB_OPERAND_INDEX_IMMEDIATE32));
      return {token, slot, row};
   }
   // The output o0.w destination
   inline Tokens OutputW()
   {
      return {DXBC::Destination(D3D10_SB_OPERAND_TYPE_OUTPUT, kMaskW), 0u};
   }

   // One instruction: opcode token (with its length) and operands
   inline Tokens Op(D3D10_SB_OPCODE_TYPE opcode, std::initializer_list<Tokens> operands, bool saturate = false)
   {
      Tokens out = {uint32_t(ENCODE_D3D10_SB_OPCODE_TYPE(opcode) | ENCODE_D3D10_SB_INSTRUCTION_SATURATE(saturate))};
      for (const Tokens& operand : operands)
      {
         out.insert(out.end(), operand.begin(), operand.end());
      }
      out[0] |= uint32_t(ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(out.size()));
      return out;
   }

   // The replacement for the alpha test: the coverage alpha of "alpha" (tested alpha) goes to temp "u".z. Uses "u".x and "u".y.
   // The derivatives sharpen the alpha: its threshold is pushed by a fraction of the threshold's distance to the alpha's, scaled
   // by the alpha's screen space rate of change, so an edge covers about one pixel of samples.
   inline Tokens CoverageInstructions(uint32_t u, uint32_t alpha)
   {
      Tokens out;
      const auto add = [&](const Tokens& instruction)
      { out.insert(out.end(), instruction.begin(), instruction.end()); };
      add(Op(D3D10_SB_OPCODE_DERIV_RTX, {Dst(u, kMaskX), Src(alpha, 3, 3, 3, 3)}));
      add(Op(D3D10_SB_OPCODE_DERIV_RTY, {Dst(u, kMaskY), Src(alpha, 3, 3, 3, 3)}));
      add(Op(D3D10_SB_OPCODE_DP2, {Dst(u, kMaskX), Src(u, 0, 1, 0, 0), Src(u, 0, 1, 0, 0)}));
      add(Op(D3D10_SB_OPCODE_ADD, {Dst(u, kMaskX), Src(u, 0, 0, 0, 0), Imm(1e-8f)}));
      add(Op(D3D10_SB_OPCODE_RSQ, {Dst(u, kMaskX), Src(u, 0, 0, 0, 0)}));
      add(Op(D3D10_SB_OPCODE_UTOF, {Dst(u, kMaskY), ConstantBuffer(3, 8, 2, 2, 2, 2)}));
      add(Op(D3D10_SB_OPCODE_ADD, {Dst(u, kMaskY), Src(u, 1, 1, 1, 1), Imm(0.5f)}));
      add(Op(D3D10_SB_OPCODE_MAD, {Dst(u, kMaskY), Src(u, 1, 1, 1, 1), Imm(kAlphaTestScaleInverse), Src(alpha, 3, 3, 3, 3)}));
      add(Op(D3D10_SB_OPCODE_MAD, {Dst(u, kMaskZ), Src(u, 1, 1, 1, 1), Src(u, 0, 0, 0, 0), Imm(0.5f)}, true));
      return out;
   }

   // The pixel shader with the alpha test replaced by the coverage alpha in o0.w (see the top of this file). False if it can't be.
   inline bool PatchAlphaTestToCoverage(const uint8_t* code, size_t size, std::vector<uint8_t>* out, std::string* error)
   {
      if (out)
         out->clear();
      const auto fail = [&](const char* message)
      {
         if (error)
            *error = message;
         return false;
      };

      std::vector<DXBC::Chunk> chunks;
      if (!DXBC::ReadChunks(code, size, &chunks))
         return fail("not a DXBC container");
      DXBC::Chunk* const program = DXBC::FindChunk(&chunks, DXBC::FourCC("SHEX"), DXBC::FourCC("SHDR"));
      const DXBC::Chunk* const output_chunk = DXBC::FindChunk(&chunks, DXBC::FourCC("OSGN"));
      if (!program || !output_chunk)
         return fail("no program or output signature");

      Tokens tokens;
      std::vector<DXBC::Instruction> instructions;
      size_t first_body = 0;
      if (!DXBC::ReadProgram(*program, &tokens, &instructions, &first_body))
         return fail("unreadable program");
      std::vector<DXBC::SignatureElement> outputs;
      if (!DXBC::ReadSignature(output_chunk->data, &outputs))
         return fail("unreadable output signature");

      // o0 must be a float4 target: declared with all four components, and float32 in the signature
      bool o0_declared = false;
      bool has_ret = false;
      for (size_t i = 0; i < instructions.size(); i++)
      {
         const DXBC::Instruction& instruction = instructions[i];
         if (instruction.opcode == D3D10_SB_OPCODE_RETC)
            return fail("conditional return");
         if (instruction.opcode == D3D10_SB_OPCODE_RET)
            has_ret = true;
         if (i < first_body && instruction.opcode == D3D10_SB_OPCODE_DCL_OUTPUT && instruction.length == 3 &&
             DECODE_D3D10_SB_OPERAND_TYPE(tokens[instruction.begin + 1]) == D3D10_SB_OPERAND_TYPE_OUTPUT && DECODE_D3D10_SB_OPERAND_INDEX_DIMENSION(tokens[instruction.begin + 1]) == D3D10_SB_OPERAND_INDEX_1D &&
             tokens[instruction.begin + 2] == 0)
         {
            o0_declared = HasMask(tokens[instruction.begin + 1], D3D10_SB_OPERAND_4_COMPONENT_MASK_ALL);
         }
      }
      const bool float_target = std::ranges::any_of(outputs, [](const DXBC::SignatureElement& element)
         { return element.reg == 0 && element.component_type == static_cast<uint32_t>(D3D_REGISTER_COMPONENT_FLOAT32) && element.mask == 0xF; });
      if (!o0_declared || !float_target)
         return fail("no float4 o0 output");
      if (!has_ret)
         return fail("no ret");

      // The alpha test, exactly once
      size_t match = SIZE_MAX;
      uint32_t test = 0, alpha = 0;
      int matches = 0;
      for (size_t i = first_body; i < instructions.size(); i++)
      {
         uint32_t found_test = 0, found_alpha = 0;
         if (MatchAlphaTest(tokens, instructions, i, &found_test, &found_alpha))
         {
            matches++;
            match = i;
            test = found_test;
            alpha = found_alpha;
         }
      }
      if (matches != 1)
         return fail(matches == 0 ? "alpha test not found" : "alpha test found more than once");
      (void)test; // rT is simply no longer written by the replaced instructions

      // A new temp, the last one (fxc declares them all first, a temps declaration is added if none)
      uint32_t coverage_temp = 0;
      Tokens patched = DXBC::CopyDeclarations(tokens, instructions, first_body, 1, &coverage_temp, [](size_t)
         { return Tokens(); });

      const Tokens coverage = CoverageInstructions(coverage_temp, alpha);
      const Tokens mov_o0_w = Op(D3D10_SB_OPCODE_MOV, {OutputW(), Src(coverage_temp, 2, 2, 2, 2)});
      for (size_t i = first_body; i < instructions.size(); i++)
      {
         const DXBC::Instruction& instruction = instructions[i];
         if (i == match)
         {
            patched.insert(patched.end(), coverage.begin(), coverage.end());
            i += 3; // The four replaced instructions
            continue;
         }
         if (instruction.opcode == D3D10_SB_OPCODE_RET)
         {
            patched.insert(patched.end(), mov_o0_w.begin(), mov_o0_w.end());
         }
         patched.insert(patched.end(), tokens.begin() + instruction.begin, tokens.begin() + instruction.begin + instruction.length);
      }

      DXBC::WriteProgram(&patched, program);
      *out = DXBC::WriteChunks(chunks);
      if (error)
         error->clear();
      return true;
   }
} // namespace AlphaToCoverage
