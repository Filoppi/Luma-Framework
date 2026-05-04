#pragma once

#include <cstddef>
#include <cstdio>
#include <include/reshade.hpp>
#include <d3d11TokenizedProgramFormat.hpp>
#include "debug.h"
#include "..\..\..\Core\utils\system.h"

#define TO_BYTES(v)                                            \
   static_cast<std::byte>(((uint32_t)(v)) & 0xFFu),            \
      static_cast<std::byte>((((uint32_t)(v)) >> 8) & 0xFFu),  \
      static_cast<std::byte>((((uint32_t)(v)) >> 16) & 0xFFu), \
      static_cast<std::byte>((((uint32_t)(v)) >> 24) & 0xFFu)

struct DepthDitherInfo
{
   size_t mad_offset = 0;
   size_t sample_offset = 0;
   size_t discard_offset = 0;
   uint32_t mad_size = 0;
   uint32_t sample_size = 0;
   uint32_t discard_size = 0;
   uint32_t sample_dest_operand = 0;
   uint32_t sample_dest_reg = 0;
   uint32_t sample_uv_operand = 0;
   uint32_t sample_uv_reg = 0;
   size_t match_offset = 0;
   uint32_t match_size = 0;
};

struct Depth2DitherInfo
{
   size_t full_match_offset = 0;
   uint32_t full_match_size = 0;
   size_t full_sample_offset = 0;
   uint32_t full_sample_size = 0;
   size_t full_tail_offset = 0;
   uint32_t full_tail_size = 0;
   uint32_t full_sample_dest_operand = 0;
   uint32_t full_sample_dest_reg = 0;
   uint32_t full_sample_uv_operand = 0;
   uint32_t full_sample_uv_reg = 0;
   uint32_t full_mul_alpha_src_operand = 0;
   uint32_t full_mul_alpha_src_reg = 0;
   uint32_t full_ult_dest_operand = 0;
   uint32_t full_ult_dest_reg = 0;
   uint32_t full_ult_src_operand = 0;
   uint32_t full_ult_src_reg = 0;
   uint32_t full_discard_src_operand = 0;
   uint32_t full_discard_src_reg = 0;
   size_t tail_match_offset = 0;
   uint32_t tail_match_size = 0;
   uint32_t tail_round_dest_operand = 0;
   uint32_t tail_round_dest_reg = 0;
   uint32_t tail_ult_dest_operand = 0;
   uint32_t tail_ult_dest_reg = 0;
   uint32_t tail_ult_src_operand = 0;
   uint32_t tail_ult_src_reg = 0;
   uint32_t tail_discard_src_operand = 0;
   uint32_t tail_discard_src_reg = 0;
};

struct ColorDitherInfo
{
   size_t mul_offset = 0;
   size_t sample_offset = 0;
   size_t decode_offset = 0;
   uint32_t mul_size = 0;
   uint32_t sample_size = 0;
   uint32_t decode_size = 0;
   uint32_t pattern_size = 0;
   uint32_t uv_operand = 0;
   uint32_t uv_reg = 0;
   uint32_t sample_dest_operand = 0;
   uint32_t sample_dest_reg = 0;
   uint32_t mul_src_operand = 0;
   uint32_t mul_src_reg = 0;
   size_t match_offset = 0;
   uint32_t match_size = 0;
};

// Store information about shaders that match known patterns in FFXV dithering shaders, to be used for patching and IGN code injection.
struct FFXVDitheringShaderInfo
{
   bool has_depth_dither = false;
   bool has_color_dither = false;
   bool has_depth_dither2 = false;
   bool should_patch = false;

   int32_t temp_reg_end = -1;
   int32_t frame_cbuffer_slot = -1;
   int32_t frame_reg_index = -1;
   uint32_t original_temp_count = 0;
   uint32_t color_patch_temp_base = 0;
   uint32_t base_reg_index = 0;

   DepthDitherInfo depth;
   Depth2DitherInfo depth2;
   ColorDitherInfo color;
};

// Store which patterns to check for in a given shader, to avoid unnecessary pattern searches.
struct DitherPrefilter
{
   bool should_check_depth_pattern = false;
   bool should_check_depth_pattern_2 = false;
   bool should_check_color_pattern = false;
};

union word_t
{
   float f;
   int32_t i;
   uint32_t u;
   uint8_t b[4];
};

constexpr word_t IGN_X = {0.06711056f};
constexpr word_t IGN_Y = {0.00583715f};
constexpr word_t IGN_SCALER = {52.9829189f};
constexpr word_t ZERO = {0.0f};
constexpr word_t PHI = {0.618034f};

constexpr word_t DECODE_POS = {2.0f};
constexpr word_t DECODE_NEG = {-1.0f};
constexpr word_t COLOR_UV_SCALE = {0.35f};

// Color hash constants pre-scaled by 0.35 (replaces MUL + SAMPLE_B + MAD chain)
constexpr word_t COLOR_HASH_X = {0.06711056f * 0.35f};
constexpr word_t COLOR_HASH_Y = {0.00583715f * 0.35f};

static constexpr size_t kDwordSize = sizeof(uint32_t);

static constexpr uint32_t kDepthMadDwords = 15;
static constexpr uint32_t kDepthSampleDwords = 12;
static constexpr uint32_t kDepthMatchDwords = 27;
static constexpr uint32_t kDepthSampleDestOperandDword = 2;
static constexpr uint32_t kDepthSampleDestIndexDword = 3;
static constexpr uint32_t kDepthSampleUvOperandDword = 4;
static constexpr uint32_t kDepthSampleUvIndexDword = 5;

static constexpr uint32_t kColorMulDwords = 10;
static constexpr uint32_t kColorSampleDwords = 14;
static constexpr uint32_t kColorMadDwords = 9;
static constexpr uint32_t kColorMatchDwords = 33;
static constexpr uint32_t kColorMulSrcOperandDword = 3;
static constexpr uint32_t kColorMulSrcIndexDword = 4;
// SAMPLE_B has a 4-dword instruction header before dest/uv/resource/sampler/lod-bias operands.
// Keep these offsets aligned to the actual operand slots so UV register capture is correct.
static constexpr uint32_t kColorSampleDestOperandDword = 14;
static constexpr uint32_t kColorSampleDestIndexDword = 15;
static constexpr uint32_t kColorSampleUvOperandDword = 16;
static constexpr uint32_t kColorSampleUvIndexDword = 17;
static constexpr uint32_t kColorMadDestOperandDword = 25;
static constexpr uint32_t kColorMadDestIndexDword = 26;

static constexpr uint32_t kDepth2TailMatchDwords = 34;
static constexpr uint32_t kDepth2TailMulDwords = 7;
static constexpr uint32_t kDepth2TailMulSrcOperandDword = 3;
static constexpr uint32_t kDepth2TailMulSrcIndexDword = 4;
static constexpr uint32_t kDepth2TailRoundDestOperandDword = 15;
static constexpr uint32_t kDepth2TailRoundDestIndexDword = 16;
static constexpr uint32_t kDepth2TailUltDestOperandDword = 25;
static constexpr uint32_t kDepth2TailUltDestIndexDword = 26;
static constexpr uint32_t kDepth2TailUltSrcOperandDword = 29;
static constexpr uint32_t kDepth2TailUltSrcIndexDword = 30;
static constexpr uint32_t kDepth2TailDiscardSrcOperandDword = 32;
static constexpr uint32_t kDepth2TailDiscardSrcIndexDword = 33;

// Match for bayer matrix sample for depth dithering pattern in FFXV shaders
// Should replace the bayer matrix for IGN instead.
// Sample: (from hair_0x44E818B0.asm)
// mad r?.xy, r?.xy, l(0.125,0.125,0,0), l(0.0625,0.0625,0,0)
// sample_aoffimmi... r?.x, r?.xyxx, t?.xyzw, s?
static const std::vector<System::BytePattern> kDepthNoisePattern = {
   // MAD len=15
   0x32u,
   0x00u,
   0x00u,
   0x0Fu,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,

   // l(0.125, 0.125, 0, 0)
   0x02u,
   0x40u,
   0x00u,
   0x00u,
   0x00u,
   0x00u,
   0x00u,
   0x3Eu,
   0x00u,
   0x00u,
   0x00u,
   0x3Eu,
   0x00u,
   0x00u,
   0x00u,
   0x00u,
   0x00u,
   0x00u,
   0x00u,
   0x00u,

   // l(0.0625, 0.0625, 0, 0)
   0x02u,
   0x40u,
   0x00u,
   0x00u,
   0x00u,
   0x00u,
   0x80u,
   0x3Du,
   0x00u,
   0x00u,
   0x80u,
   0x3Du,
   0x00u,
   0x00u,
   0x00u,
   0x00u,
   0x00u,
   0x00u,
   0x00u,
   0x00u,

   // SAMPLE_AOFFIMMI_INDEXABLE len=12
   // Keep opcode class fixed (0x45u) but wildcard control byte for close variants.
   0x45u,
   0x00u,
   0x00u,
   System::ANY,
   0x01u,
   0x00u,
   0x00u,
   0x80u,
   0xC2u,
   0x00u,
   0x00u,
   0x80u,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,

   // dest, uv, resource, sampler, offset-imm payload
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
};

// Match for hair strand dithering pattern in FFXV shaders, uses a sample from a noise texture
// should be replaced jittered UVs into the same noise texture for better quality noise without changing the overall pattern
// Sample: (from hair_0x44E818B0.asm)
// mul r?.xy, v?.zwzz, l(0.35,0.35,0,0)
// sample_b... r?.w, r?.xyxx, t?, s?, l(1.0)
// mad r?.w, r?.w, l(2.0), l(-1.0)
static const std::vector<System::BytePattern> kColorNoisePattern = {
   // MUL len=10
   0x38u,
   0x00u,
   0x00u,
   0x0Au,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,

   // l(0.35, 0.35, 0, 0)
   0x02u,
   0x40u,
   0x00u,
   0x00u,
   0x33u,
   0x33u,
   0xB3u,
   0x3Eu,
   0x33u,
   0x33u,
   0xB3u,
   0x3Eu,
   0x00u,
   0x00u,
   0x00u,
   0x00u,
   0x00u,
   0x00u,
   0x00u,
   0x00u,

   // SAMPLE_B len=14
   0x4Au,
   0x00u,
   0x00u,
   System::ANY,
   0x01u,
   0x00u,
   0x00u,
   0x80u,
   0xC2u,
   0x00u,
   0x00u,
   0x80u,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,

   // dest, uv, resource, sampler, lod-bias
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   0x01u,
   0x40u,
   0x00u,
   0x00u,
   0x00u,
   0x00u,
   0x80u,
   0x3Fu,

   // MAD len=9: mad r?.w, r?.w, l(2.0), l(-1.0)
   0x32u,
   0x00u,
   0x00u,
   0x09u,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   0x01u,
   0x40u,
   0x00u,
   0x00u,
   0x00u,
   0x00u,
   0x00u,
   0x40u,
   0x01u,
   0x40u,
   0x00u,
   0x00u,
   0x00u,
   0x00u,
   0x80u,
   0xBFu,
};

// Match for depth dithering using alpha channel with low quality noise in FFXV shaders used for LOD and dirt roads decals.
// Sample: (from dirt_0x850830F0.asm)
// mul r0.y, r0.y, v2.x
// mul r0.y, r0.y, l(16.000000)
// round_ne r0.y, r0.y
// ftou r0.y, r0.y
// ult r0.x, r0.x, r0.y
// discard_z r0.x
static const std::vector<System::BytePattern> kDepthNoisePattern2 = {
   // mul
   0x38u,
   0x00u,
   0x00u,
   0x07u,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY, // dest operand
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY, // dest index
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY, // src0 operand
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY, // src0 index
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY, // src1 operand
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY, // src1 index

   // mul by 16
   0x38u,
   0x00u,
   0x00u,
   0x07u,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY, // dest operand
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY, // dest index
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY, // src0 operand
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY, // src0 index
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY, // src1 operand (immediate)
   0x00u,
   0x00u,
   0x80u,
   0x41u, // 16.0f

   // round_ne
   0x40u,
   0x00u,
   0x00u,
   0x05u,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,

   // ftou
   0x1Cu,
   0x00u,
   0x00u,
   0x05u,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,

   // ult
   0x4Fu,
   0x00u,
   0x00u,
   0x07u,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,

   // discard_z
   0x0Du,
   0x00u,
   0x00u,
   0x03u,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
   System::ANY,
};

// TODO: refactor to be a macro or consider inlining this
static bool ReadU32At(const uint8_t* code, size_t size, size_t offset, uint32_t& value)
{
   if (code == nullptr || offset + sizeof(uint32_t) > size)
   {
      return false;
   }

   std::memcpy(&value, code + offset, sizeof(uint32_t));
   return true;
}

static void AppendFrameCounterOperand(const FFXVDitheringShaderInfo& info, std::vector<std::byte>& out_code)
{
   out_code.insert(out_code.end(), {
                                      TO_BYTES(ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_1_COMPONENT) |
                                               ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(D3D10_SB_OPERAND_4_COMPONENT_SELECT_1_MODE) |
                                               ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECT_1(D3D10_SB_4_COMPONENT_Y) |
                                               ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER) |
                                               ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_2D)),
                                      TO_BYTES(luma_settings_cbuffer_index),
                                      TO_BYTES(2u),
                                   });
}

static uint32_t ReadDclTemps(const uint8_t* code, size_t size)
{
   size_t cursor = 0;
   while (cursor + 2 * kDwordSize <= size)
   {
      uint32_t token = 0;
      std::memcpy(&token, code + cursor, sizeof(uint32_t));
      if (DECODE_D3D10_SB_OPCODE_TYPE(token) == D3D10_SB_OPCODE_DCL_TEMPS)
      {
         uint32_t count = 0;
         std::memcpy(&count, code + cursor + kDwordSize, sizeof(uint32_t));
         return count;
      }
      uint32_t length = DECODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(token);
      if (length == 0)
      {
         length = 1;
      }
      cursor += length * kDwordSize;
   }
   return 0;
}

// Prefilters shader code for presence of specific instruction patterns and constants used in FFXV dithering shaders.
// Avoids more expensive pattern scans when the shader doesn't contain relevant code sections.
static DitherPrefilter PreFilterShader(const uint8_t* code, size_t size)
{
   DitherPrefilter flags = {};
   if (code == nullptr || size < kDwordSize || (size % kDwordSize) != 0)
   {
      return flags;
   }

   bool has_depth_mad_scale = false;
   bool has_depth_mad_bias = false;
   bool has_depth2_mul16 = false;
   bool has_depth2_round = false;
   bool has_depth2_ftou = false;
   bool has_depth2_ult = false;
   bool has_depth2_discard = false;
   bool has_color_uv_scale = false;
   bool has_decode_pos = false;
   bool has_decode_neg = false;

   size_t cursor = 0;
   const size_t max_instructions = size / kDwordSize;
   size_t instruction_count = 0;

   while (cursor + kDwordSize <= size)
   {
      if (instruction_count++ > max_instructions)
      {
         break;
      }

      const size_t previous_cursor = cursor;
      uint32_t token = 0;
      if (!ReadU32At(code, size, cursor, token))
      {
         break;
      }

      const uint32_t opcode = DECODE_D3D10_SB_OPCODE_TYPE(token);
      uint32_t length = DECODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(token);
      if (length == 0)
      {
         length = 1;
      }

      const size_t inst_size = static_cast<size_t>(length) * kDwordSize;
      if (inst_size == 0 || inst_size > (size - cursor))
      {
         break;
      }

      const size_t inst_end = cursor + inst_size;

      // Raw opcode ids are used here so we can cheaply prefilter without depending on
      // optional opcode aliases in different tokenized format headers.
      has_depth2_round = has_depth2_round || opcode == 0x40u;     // round_ne
      has_depth2_ftou = has_depth2_ftou || opcode == 0x1Cu;       // ftou
      has_depth2_ult = has_depth2_ult || opcode == 0x4Fu;         // ult
      has_depth2_discard = has_depth2_discard || opcode == 0x0Du; // discard_z

      size_t imm_cursor = cursor;
      while (imm_cursor + kDwordSize <= inst_end)
      {
         uint32_t value = 0;
         std::memcpy(&value, code + imm_cursor, sizeof(uint32_t));

         has_depth_mad_scale = has_depth_mad_scale || value == 0x3E000000u; // 0.125
         has_depth_mad_bias = has_depth_mad_bias || value == 0x3D800000u;   // 0.0625
         has_depth2_mul16 = has_depth2_mul16 || value == 0x41800000u;       // 16.0
         has_color_uv_scale = has_color_uv_scale || value == COLOR_UV_SCALE.u;
         has_decode_pos = has_decode_pos || value == DECODE_POS.u;
         has_decode_neg = has_decode_neg || value == DECODE_NEG.u;

         imm_cursor += kDwordSize;
      }

      cursor += inst_size;
      if (cursor <= previous_cursor)
      {
         break;
      }
   }

   const bool has_depth2_tail_signature = has_depth2_round && has_depth2_ftou && has_depth2_ult && has_depth2_discard;

   flags.should_check_depth_pattern = has_depth_mad_scale && has_depth_mad_bias;
   flags.should_check_depth_pattern_2 = has_depth2_tail_signature || has_depth2_mul16;
   flags.should_check_color_pattern = has_color_uv_scale && has_decode_pos && has_decode_neg;
   return flags;
}

static constexpr size_t kDepthDitherPatchSize = 376;
static constexpr size_t kDepth2DitherPatchSize = 276;
static constexpr size_t kColorDitherPatchSize = 200;

static std::vector<std::byte> GenerateColorDitherPatch(const FFXVDitheringShaderInfo& info)
{
   std::vector<std::byte> uv_code;

   const uint32_t phase_reg = info.color_patch_temp_base;
   const uint32_t phase_reg2 = phase_reg + 1;
   const uint32_t uv_reg = info.color.uv_reg;

   // 4_COMPONENT + MASK_MODE for dest, 4_COMPONENT + SELECT_1_MODE for src.
   // 1_COMPONENT operands ignore selection bits per D3D10 spec.
   constexpr uint32_t phase_w_dest = ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_4_COMPONENT) |
                                     ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(D3D10_SB_OPERAND_4_COMPONENT_MASK_MODE) |
                                     ENCODE_D3D10_SB_OPERAND_4_COMPONENT_MASK(D3D10_SB_OPERAND_4_COMPONENT_MASK_W) |
                                     ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_TEMP) |
                                     ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_1D);

   constexpr uint32_t phase_w_src = ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_4_COMPONENT) |
                                    ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(D3D10_SB_OPERAND_4_COMPONENT_SELECT_1_MODE) |
                                    ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECT_1(D3D10_SB_4_COMPONENT_W) |
                                    ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_TEMP) |
                                    ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_1D);

   constexpr uint32_t uv_x_dest = ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_4_COMPONENT) |
                                  ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(D3D10_SB_OPERAND_4_COMPONENT_MASK_MODE) |
                                  ENCODE_D3D10_SB_OPERAND_4_COMPONENT_MASK(D3D10_SB_OPERAND_4_COMPONENT_MASK_X) |
                                  ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_TEMP) |
                                  ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_1D);

   constexpr uint32_t uv_y_dest = ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_4_COMPONENT) |
                                  ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(D3D10_SB_OPERAND_4_COMPONENT_MASK_MODE) |
                                  ENCODE_D3D10_SB_OPERAND_4_COMPONENT_MASK(D3D10_SB_OPERAND_4_COMPONENT_MASK_Y) |
                                  ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_TEMP) |
                                  ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_1D);

   constexpr uint32_t uv_xy_src = ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_4_COMPONENT) |
                                  ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(D3D10_SB_OPERAND_4_COMPONENT_SWIZZLE_MODE) |
                                  ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SWIZZLE(D3D10_SB_4_COMPONENT_X, D3D10_SB_4_COMPONENT_Y, D3D10_SB_4_COMPONENT_X, D3D10_SB_4_COMPONENT_X) |
                                  ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_TEMP) |
                                  ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_1D);

   constexpr uint32_t uv_xy_dest = ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_4_COMPONENT) |
                                   ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(D3D10_SB_OPERAND_4_COMPONENT_MASK_MODE) |
                                   ENCODE_D3D10_SB_OPERAND_4_COMPONENT_MASK(D3D10_SB_OPERAND_4_COMPONENT_MASK_X | D3D10_SB_OPERAND_4_COMPONENT_MASK_Y) |
                                   ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_TEMP) |
                                   ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_1D);

   constexpr uint32_t imm_4comp_xyxx = ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_4_COMPONENT) |
                                       ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(D3D10_SB_OPERAND_4_COMPONENT_SWIZZLE_MODE) |
                                       ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SWIZZLE(D3D10_SB_4_COMPONENT_X, D3D10_SB_4_COMPONENT_Y, D3D10_SB_4_COMPONENT_X, D3D10_SB_4_COMPONENT_X) |
                                       ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_IMMEDIATE32) |
                                       ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_0D);

   constexpr uint32_t imm_1comp_x = ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_1_COMPONENT) |
                                    ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_IMMEDIATE32) |
                                    ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_0D);

   constexpr uint32_t cb_frame_y_src = ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_4_COMPONENT) |
                                       ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(D3D10_SB_OPERAND_4_COMPONENT_SELECT_1_MODE) |
                                       ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECT_1(D3D10_SB_4_COMPONENT_Y) |
                                       ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER) |
                                       ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_2D);

   constexpr uint32_t imm7_val = 7u;

   auto emit = [&](std::initializer_list<std::byte> bytes)
   {
      uv_code.insert(uv_code.end(), bytes);
   };

   // 1. and rPhase.w, l(7), cb0[4].y             len=8  (frameCount & 7)
   emit({TO_BYTES(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) | ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(8))});
   emit({TO_BYTES(phase_w_dest), TO_BYTES(phase_reg)});
   emit({TO_BYTES(imm_1comp_x), TO_BYTES(imm7_val)});
   emit({TO_BYTES(cb_frame_y_src), TO_BYTES(static_cast<uint32_t>(info.frame_cbuffer_slot)), TO_BYTES(static_cast<uint32_t>(info.frame_reg_index))});

   // 2. ubfe rPhase2.w, l(3), l(3), cb0[4].y      len=10  ((frameCount >> 3) & 7)
   emit({TO_BYTES(ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_UBFE) | ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10))});
   emit({TO_BYTES(phase_w_dest), TO_BYTES(phase_reg2)});
   emit({TO_BYTES(imm_1comp_x), TO_BYTES(3u)});
   emit({TO_BYTES(imm_1comp_x), TO_BYTES(3u)});
   emit({TO_BYTES(cb_frame_y_src), TO_BYTES(static_cast<uint32_t>(info.frame_cbuffer_slot)), TO_BYTES(static_cast<uint32_t>(info.frame_reg_index))});

   // 3. utof rUV.x, rPhase.w                      len=5
   emit({TO_BYTES(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_UTOF) | ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5))});
   emit({TO_BYTES(uv_x_dest), TO_BYTES(uv_reg)});
   emit({TO_BYTES(phase_w_src), TO_BYTES(phase_reg)});

   // 4. utof rUV.y, rPhase2.w                     len=5
   emit({TO_BYTES(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_UTOF) | ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5))});
   emit({TO_BYTES(uv_y_dest), TO_BYTES(uv_reg)});
   emit({TO_BYTES(phase_w_src), TO_BYTES(phase_reg2)});

   // 5. mad rUV.xy, rUV.xyxx, l(PHI,PHI,0,0), v1.zwzz   len=12
   emit({TO_BYTES(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MAD) | ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(12)),
      TO_BYTES(uv_xy_dest), TO_BYTES(uv_reg),
      TO_BYTES(uv_xy_src), TO_BYTES(uv_reg),
      TO_BYTES(imm_4comp_xyxx),
      TO_BYTES(PHI.u), TO_BYTES(PHI.u), TO_BYTES(ZERO.u), TO_BYTES(ZERO.u),
      TO_BYTES(info.color.mul_src_operand), TO_BYTES(info.color.mul_src_reg)});

   // 6. mul rUV.xy, rUV.xyxx, l(0.35,0.35,0,0)           len=10
   emit({TO_BYTES(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MUL) | ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10)),
      TO_BYTES(uv_xy_dest), TO_BYTES(uv_reg),
      TO_BYTES(uv_xy_src), TO_BYTES(uv_reg),
      TO_BYTES(imm_4comp_xyxx),
      TO_BYTES(COLOR_UV_SCALE.u), TO_BYTES(COLOR_UV_SCALE.u), TO_BYTES(ZERO.u), TO_BYTES(ZERO.u)});

   return uv_code;
}

static std::vector<std::byte> GenerateDepthDitherPatch(const FFXVDitheringShaderInfo& info)
{
   std::vector<std::byte> ign_code;

   const uint32_t new_reg = info.original_temp_count;

   // D3D10 1_COMPONENT operands IGNORE component selection bits [11:02] per spec.
   // Use 4_COMPONENT + MASK_MODE for dest (write to a single component) and
   // 4_COMPONENT + SELECT_1_MODE for src (read a single component).
   constexpr uint32_t new_x_dest = ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_4_COMPONENT) |
                                   ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(D3D10_SB_OPERAND_4_COMPONENT_MASK_MODE) |
                                   ENCODE_D3D10_SB_OPERAND_4_COMPONENT_MASK(D3D10_SB_OPERAND_4_COMPONENT_MASK_X) |
                                   ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_TEMP) |
                                   ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_1D);

   constexpr uint32_t new_x_src = ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_4_COMPONENT) |
                                  ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(D3D10_SB_OPERAND_4_COMPONENT_SELECT_1_MODE) |
                                  ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECT_1(D3D10_SB_4_COMPONENT_X) |
                                  ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_TEMP) |
                                  ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_1D);

   constexpr uint32_t orig_dest_x_dest = ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_4_COMPONENT) |
                                         ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(D3D10_SB_OPERAND_4_COMPONENT_MASK_MODE) |
                                         ENCODE_D3D10_SB_OPERAND_4_COMPONENT_MASK(D3D10_SB_OPERAND_4_COMPONENT_MASK_X) |
                                         ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_TEMP) |
                                         ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_1D);

   constexpr uint32_t orig_uv_xy_src = ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_4_COMPONENT) |
                                       ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(D3D10_SB_OPERAND_4_COMPONENT_SWIZZLE_MODE) |
                                       ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SWIZZLE(D3D10_SB_4_COMPONENT_X, D3D10_SB_4_COMPONENT_Y, D3D10_SB_4_COMPONENT_X, D3D10_SB_4_COMPONENT_X) |
                                       ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_TEMP) |
                                       ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_1D);

   constexpr uint32_t imm_4comp_xyxx = ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_4_COMPONENT) |
                                       ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(D3D10_SB_OPERAND_4_COMPONENT_SWIZZLE_MODE) |
                                       ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SWIZZLE(D3D10_SB_4_COMPONENT_X, D3D10_SB_4_COMPONENT_Y, D3D10_SB_4_COMPONENT_X, D3D10_SB_4_COMPONENT_X) |
                                       ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_IMMEDIATE32) |
                                       ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_0D);

   constexpr uint32_t imm_1comp_x = ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_1_COMPONENT) |
                                    ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_IMMEDIATE32) |
                                    ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_0D);

   constexpr uint32_t new_y_dest = ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_4_COMPONENT) |
                                   ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(D3D10_SB_OPERAND_4_COMPONENT_MASK_MODE) |
                                   ENCODE_D3D10_SB_OPERAND_4_COMPONENT_MASK(D3D10_SB_OPERAND_4_COMPONENT_MASK_Y) |
                                   ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_TEMP) |
                                   ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_1D);

   constexpr uint32_t new_y_src = ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_4_COMPONENT) |
                                  ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(D3D10_SB_OPERAND_4_COMPONENT_SELECT_1_MODE) |
                                  ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECT_1(D3D10_SB_4_COMPONENT_Y) |
                                  ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_TEMP) |
                                  ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_1D);

   constexpr uint32_t new_z_dest = ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_4_COMPONENT) |
                                   ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(D3D10_SB_OPERAND_4_COMPONENT_MASK_MODE) |
                                   ENCODE_D3D10_SB_OPERAND_4_COMPONENT_MASK(D3D10_SB_OPERAND_4_COMPONENT_MASK_Z) |
                                   ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_TEMP) |
                                   ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_1D);

   constexpr uint32_t new_z_src = ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_4_COMPONENT) |
                                  ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(D3D10_SB_OPERAND_4_COMPONENT_SELECT_1_MODE) |
                                  ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECT_1(D3D10_SB_4_COMPONENT_Z) |
                                  ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_TEMP) |
                                  ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_1D);

   constexpr uint32_t new_xy_mask = ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_4_COMPONENT) |
                                    ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(D3D10_SB_OPERAND_4_COMPONENT_MASK_MODE) |
                                    ENCODE_D3D10_SB_OPERAND_4_COMPONENT_MASK(D3D10_SB_OPERAND_4_COMPONENT_MASK_X | D3D10_SB_OPERAND_4_COMPONENT_MASK_Y) |
                                    ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_TEMP) |
                                    ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_1D);

   constexpr uint32_t v0_xyxx = ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_4_COMPONENT) |
                                ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(D3D10_SB_OPERAND_4_COMPONENT_SWIZZLE_MODE) |
                                ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SWIZZLE(D3D10_SB_4_COMPONENT_X, D3D10_SB_4_COMPONENT_Y, D3D10_SB_4_COMPONENT_X, D3D10_SB_4_COMPONENT_X) |
                                ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_INPUT) |
                                ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_1D);

   constexpr uint32_t cb_frame_y_src = ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_4_COMPONENT) |
                                       ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(D3D10_SB_OPERAND_4_COMPONENT_SELECT_1_MODE) |
                                       ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECT_1(D3D10_SB_4_COMPONENT_Y) |
                                       ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER) |
                                       ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_2D);

   constexpr uint32_t neg_mod_token = ENCODE_D3D10_SB_EXTENDED_OPERAND_MODIFIER(D3D10_SB_OPERAND_MODIFIER_NEG);

   constexpr uint32_t v0_idx = 0u;
   constexpr uint32_t sign_mask_val = 0x80000000u;
   constexpr uint32_t imm7_val = 7u;

   auto emit = [&](std::initializer_list<std::byte> bytes)
   {
      ign_code.insert(ign_code.end(), bytes);
   };

   // 1. and rN.x, l(0x80000000), cb0[4].y — len=8  (sign bit extract)
   emit({TO_BYTES(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) | ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(8)),
      TO_BYTES(new_x_dest), TO_BYTES(new_reg),
      TO_BYTES(imm_1comp_x), TO_BYTES(sign_mask_val),
      TO_BYTES(cb_frame_y_src), TO_BYTES(static_cast<uint32_t>(info.frame_cbuffer_slot)), TO_BYTES(static_cast<uint32_t>(info.frame_reg_index))});

   // 2. imax rN.y, cb0[4].y, -cb0[4].y — len=10 (abs via negated src1)
   emit({TO_BYTES(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IMAX) | ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10)),
      TO_BYTES(new_y_dest), TO_BYTES(new_reg),
      TO_BYTES(cb_frame_y_src), TO_BYTES(static_cast<uint32_t>(info.frame_cbuffer_slot)), TO_BYTES(static_cast<uint32_t>(info.frame_reg_index)),
      TO_BYTES(cb_frame_y_src | ENCODE_D3D10_SB_OPERAND_EXTENDED(1)), TO_BYTES(neg_mod_token), TO_BYTES(static_cast<uint32_t>(info.frame_cbuffer_slot)), TO_BYTES(static_cast<uint32_t>(info.frame_reg_index))});

   // 3. and rN.y, rN.y, l(7) — len=7
   emit({TO_BYTES(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) | ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7))});
   emit({TO_BYTES(new_y_dest), TO_BYTES(new_reg)});
   emit({TO_BYTES(new_y_src), TO_BYTES(new_reg)});
   emit({TO_BYTES(imm_1comp_x), TO_BYTES(imm7_val)});

   // 4. ineg rN.z, rN.y — len=5
   emit({TO_BYTES(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_INEG) | ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5))});
   emit({TO_BYTES(new_z_dest), TO_BYTES(new_reg)});
   emit({TO_BYTES(new_y_src), TO_BYTES(new_reg)});

   // 5. movc rN.x, rN.x, rN.z, rN.y — len=9 (sign-preserved mod)
   emit({TO_BYTES(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) | ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9)),
      TO_BYTES(new_x_dest), TO_BYTES(new_reg),
      TO_BYTES(new_x_src), TO_BYTES(new_reg),
      TO_BYTES(new_z_src), TO_BYTES(new_reg),
      TO_BYTES(new_y_src), TO_BYTES(new_reg)});

   // 6. itof rN.x, rN.x — len=5
   emit({TO_BYTES(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ITOF) | ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5))});
   emit({TO_BYTES(new_x_dest), TO_BYTES(new_reg)});
   emit({TO_BYTES(new_x_src), TO_BYTES(new_reg)});

   // 7. itof rN.y, cb0[4].y — len=6
   emit({TO_BYTES(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ITOF) | ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(6)),
      TO_BYTES(new_y_dest), TO_BYTES(new_reg),
      TO_BYTES(cb_frame_y_src), TO_BYTES(static_cast<uint32_t>(info.frame_cbuffer_slot)), TO_BYTES(static_cast<uint32_t>(info.frame_reg_index))});

   // 8. mad rN.xy, rN.xyxx, l(PHI,PHI,0,0), v0.xyxx — len=12
   emit({TO_BYTES(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MAD) | ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(12)),
      TO_BYTES(new_xy_mask), TO_BYTES(new_reg),
      TO_BYTES(orig_uv_xy_src), TO_BYTES(new_reg),
      TO_BYTES(imm_4comp_xyxx),
      TO_BYTES(PHI.u), TO_BYTES(PHI.u), TO_BYTES(ZERO.u), TO_BYTES(ZERO.u),
      TO_BYTES(v0_xyxx), TO_BYTES(v0_idx)});

   // 9. dp2 rN.x, rN.xyxx, l(IGN_X,IGN_Y,0,0) — len=10
   emit({TO_BYTES(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DP2) | ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10)),
      TO_BYTES(new_x_dest), TO_BYTES(new_reg),
      TO_BYTES(orig_uv_xy_src), TO_BYTES(new_reg),
      TO_BYTES(imm_4comp_xyxx),
      TO_BYTES(IGN_X.u), TO_BYTES(IGN_Y.u), TO_BYTES(ZERO.u), TO_BYTES(ZERO.u)});

   // 10. frc rN.x, rN.x — len=5
   emit({TO_BYTES(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_FRC) | ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5))});
   emit({TO_BYTES(new_x_dest), TO_BYTES(new_reg)});
   emit({TO_BYTES(new_x_src), TO_BYTES(new_reg)});

   // 11. mul rN.x, rN.x, l(IGN_SCALER) — len=7
   emit({TO_BYTES(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MUL) | ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7))});
   emit({TO_BYTES(new_x_dest), TO_BYTES(new_reg)});
   emit({TO_BYTES(new_x_src), TO_BYTES(new_reg)});
   emit({TO_BYTES(imm_1comp_x), TO_BYTES(IGN_SCALER.u)});

   // 12. frc rN.x, rN.x — len=5
   emit({TO_BYTES(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_FRC) | ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5))});
   emit({TO_BYTES(new_x_dest), TO_BYTES(new_reg)});
   emit({TO_BYTES(new_x_src), TO_BYTES(new_reg)});

   // 13. mov rDEST.x, rN.x — len=5
   emit({TO_BYTES(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) | ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5)),
      TO_BYTES(orig_dest_x_dest), TO_BYTES(info.base_reg_index),
      TO_BYTES(new_x_src), TO_BYTES(new_reg)});

   return ign_code;
}

static bool ComputeNewShaderSize(const FFXVDitheringShaderInfo& info, size_t original_size, size_t& new_size)
{
   ptrdiff_t delta = 0;

   if (info.has_depth_dither)
   {
      delta += static_cast<ptrdiff_t>(kDepthDitherPatchSize) - static_cast<ptrdiff_t>(info.depth.match_size);
   }

   if (info.has_depth_dither2)
   {
      delta += static_cast<ptrdiff_t>(kDepth2DitherPatchSize) - static_cast<ptrdiff_t>(info.depth2.tail_match_size - kDepth2TailMulDwords * kDwordSize);
      delta += 16;
   }

   if (info.has_color_dither)
   {
      delta += static_cast<ptrdiff_t>(kColorDitherPatchSize) - static_cast<ptrdiff_t>(kColorMulDwords * kDwordSize);
   }

   new_size = original_size + delta;
   return info.has_depth_dither || info.has_depth_dither2 || info.has_color_dither;
}

static std::vector<std::byte> GenerateDepthDither2Patch(const FFXVDitheringShaderInfo& info)
{
   std::vector<std::byte> patch;

   const uint32_t new_reg = info.original_temp_count;

   const uint32_t new_1comp_z_token = ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_1_COMPONENT) |
                                      ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(D3D10_SB_OPERAND_4_COMPONENT_SELECT_1_MODE) |
                                      ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECT_1(D3D10_SB_4_COMPONENT_Z) |
                                      ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_TEMP) |
                                      ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_1D);

   const uint32_t new_1comp_w_token = ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_1_COMPONENT) |
                                      ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(D3D10_SB_OPERAND_4_COMPONENT_SELECT_1_MODE) |
                                      ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECT_1(D3D10_SB_4_COMPONENT_W) |
                                      ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_TEMP) |
                                      ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_1D);

   constexpr uint32_t cb13_2_y_token = ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_1_COMPONENT) |
                                       ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(D3D10_SB_OPERAND_4_COMPONENT_SELECT_1_MODE) |
                                       ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECT_1(D3D10_SB_4_COMPONENT_Y) |
                                       ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER) |
                                       ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_2D);

   constexpr uint32_t v0_4comp_xy_token = ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_4_COMPONENT) |
                                          ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(D3D10_SB_OPERAND_4_COMPONENT_SWIZZLE_MODE) |
                                          ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SWIZZLE(D3D10_SB_4_COMPONENT_X, D3D10_SB_4_COMPONENT_Y, D3D10_SB_4_COMPONENT_X, D3D10_SB_4_COMPONENT_X) |
                                          ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_INPUT) |
                                          ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_1D);

   constexpr uint32_t imm_1comp_x_token = ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_1_COMPONENT) |
                                          ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(D3D10_SB_OPERAND_4_COMPONENT_SELECT_1_MODE) |
                                          ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECT_1(D3D10_SB_4_COMPONENT_X) |
                                          ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_IMMEDIATE32) |
                                          ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_0D);

   constexpr uint32_t imm_4comp_xyxx_token = ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_4_COMPONENT) |
                                             ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(D3D10_SB_OPERAND_4_COMPONENT_SWIZZLE_MODE) |
                                             ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SWIZZLE(D3D10_SB_4_COMPONENT_X, D3D10_SB_4_COMPONENT_Y, D3D10_SB_4_COMPONENT_X, D3D10_SB_4_COMPONENT_X) |
                                             ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_IMMEDIATE32) |
                                             ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_0D);

   constexpr uint32_t r0_idx = 0u;
   constexpr uint32_t v0_idx = 0u;
   constexpr uint32_t cb13_slot = 13u;
   constexpr uint32_t cb_frame_idx = 2u;
   constexpr uint32_t l7 = 7u;

   patch.insert(patch.end(), {
                                TO_BYTES(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) | ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(8)),
                                TO_BYTES(new_1comp_z_token),
                                TO_BYTES(new_reg),
                                TO_BYTES(cb13_2_y_token),
                                TO_BYTES(cb13_slot),
                                TO_BYTES(cb_frame_idx),
                                TO_BYTES(imm_1comp_x_token),
                                TO_BYTES(l7),

                                TO_BYTES(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_UTOF) | ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5)),
                                TO_BYTES(new_1comp_z_token),
                                TO_BYTES(new_reg),
                                TO_BYTES(new_1comp_z_token),
                                TO_BYTES(new_reg),

                                TO_BYTES(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MUL) | ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7)),
                                TO_BYTES(new_1comp_z_token),
                                TO_BYTES(new_reg),
                                TO_BYTES(new_1comp_z_token),
                                TO_BYTES(new_reg),
                                TO_BYTES(imm_1comp_x_token),
                                TO_BYTES(PHI.u),

                                TO_BYTES(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_FRC) | ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5)),
                                TO_BYTES(new_1comp_z_token),
                                TO_BYTES(new_reg),
                                TO_BYTES(new_1comp_z_token),
                                TO_BYTES(new_reg),

                                TO_BYTES(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DP2) | ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10)),
                                TO_BYTES(new_1comp_w_token),
                                TO_BYTES(new_reg),
                                TO_BYTES(v0_4comp_xy_token),
                                TO_BYTES(v0_idx),
                                TO_BYTES(imm_4comp_xyxx_token),
                                TO_BYTES(IGN_X.u),
                                TO_BYTES(IGN_Y.u),
                                TO_BYTES(ZERO.u),
                                TO_BYTES(ZERO.u),

                                TO_BYTES(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ADD) | ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7)),
                                TO_BYTES(new_1comp_w_token),
                                TO_BYTES(new_reg),
                                TO_BYTES(new_1comp_w_token),
                                TO_BYTES(new_reg),
                                TO_BYTES(new_1comp_z_token),
                                TO_BYTES(new_reg),

                                TO_BYTES(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_FRC) | ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5)),
                                TO_BYTES(new_1comp_w_token),
                                TO_BYTES(new_reg),
                                TO_BYTES(new_1comp_w_token),
                                TO_BYTES(new_reg),

                                TO_BYTES(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MUL) | ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7)),
                                TO_BYTES(new_1comp_w_token),
                                TO_BYTES(new_reg),
                                TO_BYTES(new_1comp_w_token),
                                TO_BYTES(new_reg),
                                TO_BYTES(imm_1comp_x_token),
                                TO_BYTES(IGN_SCALER.u),

                                TO_BYTES(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_FRC) | ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5)),
                                TO_BYTES(new_1comp_w_token),
                                TO_BYTES(new_reg),
                                TO_BYTES(new_1comp_w_token),
                                TO_BYTES(new_reg),

                                TO_BYTES(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_LT) | ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7)),
                                TO_BYTES(info.depth2.tail_ult_dest_operand),
                                TO_BYTES(info.depth2.tail_ult_dest_reg),
                                TO_BYTES(info.depth2.tail_ult_src_operand),
                                TO_BYTES(info.depth2.tail_ult_src_reg),
                                TO_BYTES(new_1comp_w_token),
                                TO_BYTES(new_reg),

                                TO_BYTES(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DISCARD) | ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3) | ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(D3D10_SB_INSTRUCTION_TEST_NONZERO)),
                                TO_BYTES(info.depth2.tail_discard_src_operand),
                                TO_BYTES(info.depth2.tail_discard_src_reg),
                             });

   return patch;
}

static bool HasDepthDitherPattern(const uint8_t* code, size_t size, FFXVDitheringShaderInfo& info, uint64_t shader_hash = 0)
{
   if (code == nullptr || size < (kDepthMatchDwords * kDwordSize))
   {
      return false;
   }

   const std::vector<std::byte*> matches =
      System::ScanMemoryForPattern(reinterpret_cast<const std::byte*>(code), size, kDepthNoisePattern, false);

   for (std::byte* match : matches)
   {
      const size_t match_offset = static_cast<size_t>(match - reinterpret_cast<const std::byte*>(code));
      const size_t match_size = kDepthNoisePattern.size();

      if (match_offset + match_size > size)
      {
         Log_Debug(
            reshade::log::level::warning,
            std::format("Skipping depth candidate: out of bounds (offset=%zu size=%zu total=%zu)", match_offset, match_size, size).c_str());
         continue;
      }

      // From the matched MAD+SAMPLE block:
      // Read UV register index from the SAMPLE instruction (dword 6 from SAMPLE start = MATCH+15+6 = MATCH+21)
      uint32_t uv_reg_index = 0;
      if (!ReadU32At(code, size, match_offset + (kDepthMadDwords + 5) * kDwordSize, uv_reg_index))
      {
         Log_Debug(reshade::log::level::warning,
            std::format("Skipping depth candidate: uv register read failed (offset=%zu)", match_offset).c_str());
         continue;
      }

      info.has_depth_dither = true;
      info.depth.match_offset = match_offset;
      info.depth.match_size = static_cast<uint32_t>(match_size);
      info.base_reg_index = uv_reg_index;
      info.original_temp_count = (std::max)(info.original_temp_count, ReadDclTemps(code, size));

      // Verified from target dump.
      info.frame_cbuffer_slot = 0;
      info.frame_reg_index = 4;

      Log_Debug(
         reshade::log::level::info,
         std::format("Depth pattern accepted (match_offset=%zu match_size=%zu reg=%u)", match_offset, match_size, uv_reg_index).c_str());

      return true;
   }

   return false;
}

static bool HasColorDitherPattern(const uint8_t* code, const size_t size, FFXVDitheringShaderInfo& info, uint64_t shader_hash = 0)
{
   if (code == nullptr || size < (kColorMatchDwords * kDwordSize))
   {
      return false;
   }

   const std::vector<std::byte*> matches =
      System::ScanMemoryForPattern(reinterpret_cast<const std::byte*>(code), size, kColorNoisePattern, false);

   for (std::byte* match : matches)
   {
      const size_t match_offset = static_cast<size_t>(match - reinterpret_cast<const std::byte*>(code));
      const size_t match_size = kColorNoisePattern.size();

      if (match_offset + match_size > size)
      {
         Log_Debug(
            reshade::log::level::warning, std::format("Skipping color candidate: out of bounds (offset=%zu size=%zu total=%zu)", match_offset, match_size, size).c_str());
         continue;
      }

      uint32_t mul_src_reg_index = 0;
      uint32_t mul_src_operand_token = 0;
      uint32_t uv_operand_token = 0;
      uint32_t uv_reg_index = 0;

      if (!ReadU32At(code, size, match_offset + kColorMulSrcOperandDword * kDwordSize, mul_src_operand_token) ||
          !ReadU32At(code, size, match_offset + kColorMulSrcIndexDword * kDwordSize, mul_src_reg_index) ||
          !ReadU32At(code, size, match_offset + kColorSampleUvOperandDword * kDwordSize, uv_operand_token) ||
          !ReadU32At(code, size, match_offset + kColorSampleUvIndexDword * kDwordSize, uv_reg_index))
      {
         Log_Debug(reshade::log::level::warning, std::format("Skipping color candidate: token read failed (offset=%zu)", match_offset).c_str());
         continue;
      }

      info.has_color_dither = true;
      info.original_temp_count = (std::max)(info.original_temp_count, ReadDclTemps(code, size));
      info.color.match_offset = match_offset;
      info.color.match_size = static_cast<uint32_t>(match_size);
      info.color.mul_offset = match_offset;
      info.color.mul_src_operand = mul_src_operand_token;
      info.color.mul_src_reg = mul_src_reg_index;
      info.color.uv_operand = uv_operand_token;
      info.color.uv_reg = uv_reg_index;

      if (info.frame_cbuffer_slot == -1)
      {
         info.frame_cbuffer_slot = 0;
      }
      if (info.frame_reg_index == -1)
      {
         info.frame_reg_index = 4;
      }

      Log_Debug(
         reshade::log::level::info,
         std::format("Color pattern accepted (match_offset=%zu match_size=%zu reg=%u)",
            match_offset, match_size, mul_src_reg_index)
            .c_str());

      return true;
   }

   return false;
}

static bool HasDepthDitherPattern2(const uint8_t* code, size_t size, FFXVDitheringShaderInfo& info, uint64_t shader_hash = 0)
{
   if (code == nullptr || size < (kDepth2TailMatchDwords * kDwordSize))
   {
      return false;
   }

   const std::vector<std::byte*> matches =
      System::ScanMemoryForPattern(reinterpret_cast<const std::byte*>(code), size, kDepthNoisePattern2, false);

   for (std::byte* match : matches)
   {
      const size_t match_offset = static_cast<size_t>(match - reinterpret_cast<const std::byte*>(code));
      const size_t match_size = kDepthNoisePattern2.size();

      if (match_offset + match_size > size)
      {
         Log_Debug(
            reshade::log::level::warning,
            std::format("Skipping depth2 candidate: out of bounds (offset=%zu size=%zu total=%zu)", match_offset, match_size, size).c_str());
         continue;
      }

      uint32_t round_dest_operand_token = 0;
      uint32_t round_dest_reg_index = 0;
      uint32_t ult_dest_operand_token = 0;
      uint32_t ult_dest_reg_index = 0;
      uint32_t ult_src_operand_token = 0;
      uint32_t ult_src_reg_index = 0;
      uint32_t discard_src_operand_token = 0;
      uint32_t discard_src_reg_index = 0;

      if (!ReadU32At(code, size, match_offset + (kDepth2TailRoundDestOperandDword * kDwordSize), round_dest_operand_token) ||
          !ReadU32At(code, size, match_offset + (kDepth2TailRoundDestIndexDword * kDwordSize), round_dest_reg_index) ||
          !ReadU32At(code, size, match_offset + (kDepth2TailUltDestOperandDword * kDwordSize), ult_dest_operand_token) ||
          !ReadU32At(code, size, match_offset + (kDepth2TailUltDestIndexDword * kDwordSize), ult_dest_reg_index) ||
          !ReadU32At(code, size, match_offset + (kDepth2TailUltSrcOperandDword * kDwordSize), ult_src_operand_token) ||
          !ReadU32At(code, size, match_offset + (kDepth2TailUltSrcIndexDword * kDwordSize), ult_src_reg_index) ||
          !ReadU32At(code, size, match_offset + (kDepth2TailDiscardSrcOperandDword * kDwordSize), discard_src_operand_token) ||
          !ReadU32At(code, size, match_offset + (kDepth2TailDiscardSrcIndexDword * kDwordSize), discard_src_reg_index))
      {
         Log_Debug(
            reshade::log::level::warning,
            std::format("Skipping depth2 candidate: token read failed (offset=%zu)", match_offset).c_str());
         continue;
      }

      info.has_depth_dither2 = true;
      info.depth2.tail_match_offset = match_offset;
      info.depth2.tail_match_size = static_cast<uint32_t>(match_size);
      info.original_temp_count = (std::max)(info.original_temp_count, ReadDclTemps(code, size));
      info.depth2.tail_round_dest_operand = round_dest_operand_token;
      info.depth2.tail_round_dest_reg = round_dest_reg_index;
      info.depth2.tail_ult_dest_operand = ult_dest_operand_token;
      info.depth2.tail_ult_dest_reg = ult_dest_reg_index;
      info.depth2.tail_ult_src_operand = ult_src_operand_token;
      info.depth2.tail_ult_src_reg = ult_src_reg_index;
      info.depth2.tail_discard_src_operand = discard_src_operand_token;
      info.depth2.tail_discard_src_reg = discard_src_reg_index;

      if (info.frame_cbuffer_slot == -1)
      {
         info.frame_cbuffer_slot = 0;
      }
      if (info.frame_reg_index == -1)
      {
         info.frame_reg_index = 4;
      }

      Log_Debug(
         reshade::log::level::info,
         std::format("Depth2 pattern accepted (match_offset=%zu match_size=%zu round_dest_op=0x%08X round_dest_reg=%u ult_dest_op=0x%08X ult_dest_reg=%u)",
            match_offset,
            match_size,
            round_dest_operand_token,
            round_dest_reg_index,
            ult_dest_operand_token,
            ult_dest_reg_index)
            .c_str());

      return true;
   }

   return false;
}

static std::unique_ptr<std::byte[]> PatchShaderCode(const uint8_t* code, size_t original_size, const FFXVDitheringShaderInfo& info, size_t& new_size)
{
   struct Region
   {
      size_t start;
      size_t skip_bytes;
      std::vector<std::byte> replacement;
   };
   std::vector<Region> regions;

   if (info.has_depth_dither)
      regions.push_back({info.depth.match_offset, info.depth.match_size, GenerateDepthDitherPatch(info)});

   if (info.has_color_dither)
      regions.push_back({info.color.mul_offset, kColorMulDwords * kDwordSize, GenerateColorDitherPatch(info)});

   if (info.has_depth_dither2)
      regions.push_back({info.depth2.tail_match_offset + kDepth2TailMulDwords * kDwordSize,
         info.depth2.tail_match_size - kDepth2TailMulDwords * kDwordSize,
         GenerateDepthDither2Patch(info)});

   std::sort(regions.begin(), regions.end(), [](const Region& a, const Region& b)
      { return a.start < b.start; });

   auto output = std::make_unique<std::byte[]>(new_size);
   size_t src_cursor = 0;
   size_t dst_cursor = 0;

   if (info.has_depth_dither2)
   {
      const uint8_t decl[] = {
         0x59,
         0x00,
         0x00,
         0x04,
         0x46,
         0x8E,
         0x20,
         0x00,
         0x0D,
         0x00,
         0x00,
         0x00,
         0x01,
         0x00,
         0x00,
         0x00,
      };
      std::memcpy(output.get() + dst_cursor, decl, sizeof(decl));
      dst_cursor += sizeof(decl);
   }

   for (const auto& region : regions)
   {
      if (region.start < src_cursor)
         continue;

      size_t gap = region.start - src_cursor;
      std::memcpy(output.get() + dst_cursor, code + src_cursor, gap);
      src_cursor += gap;
      dst_cursor += gap;

      std::memcpy(output.get() + dst_cursor, region.replacement.data(), region.replacement.size());
      dst_cursor += region.replacement.size();
      src_cursor += region.skip_bytes;
   }

   size_t tail = original_size - src_cursor;
   std::memcpy(output.get() + dst_cursor, code + src_cursor, tail);

   if (info.original_temp_count > 0)
   {
      for (size_t i = 0; i + 2 * kDwordSize <= new_size; i += kDwordSize)
      {
         uint32_t token = 0;
         std::memcpy(&token, output.get() + i, sizeof(uint32_t));
         if (DECODE_D3D10_SB_OPCODE_TYPE(token) == D3D10_SB_OPCODE_DCL_TEMPS)
         {
            uint32_t new_count = info.original_temp_count + (info.has_depth_dither ? 1u : 0u) + (info.has_color_dither ? 2u : 0u);
            std::memcpy(output.get() + i + kDwordSize, &new_count, sizeof(uint32_t));
            break;
         }
      }
   }

   return output;
}

static bool IsFFXVDitheringShader(const uint8_t* code, size_t size, FFXVDitheringShaderInfo& dither_info, uint64_t shader_hash = 0)
{
   if (code == nullptr || size < kDwordSize)
   {
      return false;
   }

   // Prefilter the shader for quick rejection of irrelevant shaders without doing expensive pattern scans.
   DitherPrefilter prefilter = PreFilterShader(code, size);

   if (!prefilter.should_check_depth_pattern && !prefilter.should_check_depth_pattern_2 && !prefilter.should_check_color_pattern)
   {
      return false;
   }

   dither_info = {};

   // Temporarily run detectors unconditionally while validating depth2 matching.
   if (prefilter.should_check_depth_pattern_2)
   {
      dither_info.has_depth_dither2 = HasDepthDitherPattern2(code, size, dither_info, shader_hash);
      if (dither_info.has_depth_dither2)
      {
         dither_info.should_patch = true;

         Log_Debug(
            reshade::log::level::info, std::format("[{:x}] Depth pattern 2 detected, skipping legacy pattern checks", shader_hash).c_str());

         Log_Debug(
            reshade::log::level::info,
            std::format("[{:x}] Detection summary: depth2={} depth={} color={} supported=yes", shader_hash, (dither_info.has_depth_dither2 ? "yes" : "no"), (dither_info.has_depth_dither ? "yes" : "no"), (dither_info.has_color_dither ? "yes" : "no")).c_str());

         return true;
      }
   }

   if (prefilter.should_check_depth_pattern)
   {
      dither_info.has_depth_dither = HasDepthDitherPattern(code, size, dither_info, shader_hash);
   }

   if (prefilter.should_check_color_pattern)
   {
      dither_info.has_color_dither = HasColorDitherPattern(code, size, dither_info, shader_hash);
   }
   dither_info.should_patch = dither_info.has_depth_dither || dither_info.has_color_dither;

   // Color patch needs 2 scratch regs; depth patch needs 1. When both are active they must
   // not share the same register index, so offset the color base past depth's register.
   dither_info.color_patch_temp_base = dither_info.original_temp_count + (dither_info.has_depth_dither ? 1u : 0u);

   Log_Debug(
      reshade::log::level::info,
      std::format("[{:x}] Pattern match found, starting patch process (size={} depth2={} depth={} color={})", shader_hash, size, (dither_info.has_depth_dither2 ? "yes" : "no"), (dither_info.has_depth_dither ? "yes" : "no"), (dither_info.has_color_dither ? "yes" : "no")).c_str());

   Log_Debug(
      reshade::log::level::info,
      std::format("[{:x}] Detection summary: depth2={} depth={} color={} supported={}", shader_hash, (dither_info.has_depth_dither2 ? "yes" : "no"), (dither_info.has_depth_dither ? "yes" : "no"), (dither_info.has_color_dither ? "yes" : "no"), (dither_info.should_patch ? "yes" : "no")).c_str());

   return dither_info.should_patch;
}

static void DumpShaderHex(const std::byte* code, size_t size, uint64_t shader_hash, const char* label)
{
   char path[512];
   snprintf(path, sizeof(path), "luma_patched_%016llx_%s.hex", static_cast<unsigned long long>(shader_hash), label);
   FILE* f = fopen(path, "wb");
   if (!f)
      return;

   fprintf(f, "// %s shader_hash=%016llx size=%zu dwords=%zu\n", label, static_cast<unsigned long long>(shader_hash), size, size / 4);

   for (size_t i = 0; i < size; i += 16)
   {
      fprintf(f, "%08zX: ", i);
      for (size_t j = 0; j < 16 && i + j < size; ++j)
         fprintf(f, "%02X ", static_cast<unsigned char>(code[i + j]));
      for (size_t j = i + 16 > size ? size - i : 16; j < 16; ++j)
         fprintf(f, "   ");
      fprintf(f, " ");
      for (size_t j = 0; j < 16 && i + j < size; ++j)
      {
         unsigned char c = static_cast<unsigned char>(code[i + j]);
         fprintf(f, "%c", (c >= 0x20 && c < 0x7F) ? c : '.');
      }
      fprintf(f, "\n");
   }
   fclose(f);
}

static std::unique_ptr<std::byte[]> ModifyFFXVDitheringShader(const uint8_t* code, size_t& size, const FFXVDitheringShaderInfo& info, uint64_t shader_hash = 0)
{
   Log_Debug(
      reshade::log::level::info,
      std::format("[{:x}] Starting patching (size={} depth2={} depth={} color={})", shader_hash, size, (info.has_depth_dither2 ? "yes" : "no"), (info.has_depth_dither ? "yes" : "no"), (info.has_color_dither ? "yes" : "no")).c_str());

   if (code == nullptr)
   {
      Log_Debug(reshade::log::level::warning, std::format("[{:x}] Aborted patching: code is null", shader_hash).c_str());
      return nullptr;
   }

   if (!info.has_depth_dither && !info.has_color_dither && !info.has_depth_dither2)
   {
      Log_Debug(reshade::log::level::info, std::format("[{:x}] Aborted patching: no depth/color dither flags set", shader_hash).c_str());
      return nullptr;
   }

   if (info.has_depth_dither && info.depth.match_offset + info.depth.match_size > size)
   {
      Log_Debug(
         reshade::log::level::warning,
         std::format("[{:x}] Aborted patching: depth region out of bounds (offset={} size={} original={})", shader_hash, info.depth.match_offset, info.depth.match_size, size).c_str());
      return nullptr;
   }

   if (info.has_color_dither && info.color.match_offset + info.color.match_size > size)
   {
      Log_Debug(
         reshade::log::level::warning, std::format("[{:x}] Aborted patching: color region out of bounds (offset={} size={} original={})", shader_hash, info.color.match_offset, info.color.match_size, size).c_str());
      return nullptr;
   }

   if (info.has_depth_dither2 && info.depth2.tail_match_offset + info.depth2.tail_match_size > size)
   {
      Log_Debug(
         reshade::log::level::warning, std::format("[{:x}] Aborted patching: depth2 region out of bounds (offset={} size={} original={})", shader_hash, info.depth2.tail_match_offset, info.depth2.tail_match_size, size).c_str());
      return nullptr;
   }

   size_t new_size = size;
   if (!ComputeNewShaderSize(info, size, new_size))
   {
      Log_Debug(reshade::log::level::warning, std::format("[{:x}] ComputeNewShaderSize returned no delta", shader_hash).c_str());
      return nullptr;
   }

   Log_Debug(
      reshade::log::level::info,
      std::format("[{:x}] New shader size calculated: old={} new={} delta={}", shader_hash, size, new_size, static_cast<long long>(new_size) - static_cast<long long>(size)).c_str());

   // DumpShaderHex(reinterpret_cast<const std::byte*>(code), size, shader_hash, "original");

   auto new_code = PatchShaderCode(code, size, info, new_size);
   size = new_size;

   // DumpShaderHex(new_code.get(), new_size, shader_hash, "patched");

   return new_code;
}
