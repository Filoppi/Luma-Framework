#include "../shared.h"
#include "./drawbinary.hlsl"

#define SDR_NORMALIZATION_MAX      32768.0  // 1 / 3.05175781e-005
#define RENODX_TONE_MAP_TYPE_IS_ON RENODX_TONE_MAP_TYPE > 0
#define TONEMAP_START_FLAT         5

// void Tonemap_Compressor(inout color) {
//   //y
//   float y = renodx::color::
// }

// void F(inout float4 color) {
//   saturate(color);
// }

// void F(inout float3 color) {
//   saturate(color);
// }

// void F(inout float2 color) {
//   saturate(color);
// }

// void F(inout float color) {
//   saturate(color);
// }

float Sum3(in float3 v) {
  return (v.x + v.y + v.z);
}

bool CheckBlack(in float v) {
  return v == 0;
}
bool CheckBlack(in float2 v) {
  return v == (float2)0;
}
bool CheckBlack(in float3 v) {
  return v == (float3)0;
}
bool CheckBlack(in float4 v) {
  return v == (float4)0;
}

bool CheckWhite(in float v) {
  return v == 1;
}
bool CheckWhite(in float2 v) {
  return v == (float2)1;
}
bool CheckWhite(in float3 v) {
  return v == (float3)1;
}
bool CheckWhite(in float4 v) {
  return v == (float4)1;
}

float4 Tonemap_BloomMultiplier(in float4 color) {
  return color * CUSTOM_BLOOM;
}

float3 Tonemap_BloomMultiplier(in float3 color) {
  return color * CUSTOM_BLOOM;
}

float3 Tonemap_ExposureComplex(in float3 colorTonemapped, in float v3) {
  const float exp = v3 /* * 7.f */ /* CUSTOM_PREEXPOSURE_COMPLEXFUDGE */;  // TODO this is probably a curve.
  return colorTonemapped * exp;
}

void Tonemap_ExposureMultiplierApply(inout float3 colorU, in float midGray, in float3 midGrayT) {
  const float exp = renodx::color::y::from::BT709(midGrayT) / midGray;
  colorU *= exp;
}

#ifdef TONEMAP_COMPLEX
float3 Tonemap_Complex(float3 colorT, float4 v3) {
  float4 r0, r1;
  r0 = float4(colorT, 1);

  r0.y = dot(r0.xyz, float3(0.300000012,0.589999974,0.109999999));
  r0.xz = r0.xz + -r0.yy;
  r1.x = v3.y * r0.y; //exposure 
  r1.y = 0; //because 512x1
  r1.xy = g_textures_2_.SampleLevel(g_samplers_2__s, r1.xy, 0).yx;
  r0.y = v3.x * r1.x; //sat
  r1.xz = r0.yy * r0.xz;
  r0.xz = r0.yy * r0.xz + r1.yy;
  r0.y = dot(r1.xyz, float3(-0.508475006,1,-0.186441004));
  r0.xyz = r0.xyz * g_tone_scale.xyz + g_tone_offset.xyz;
  r0.xyz = /* saturate */(r0.xyz);

  return r0.xyz;
}
float3 Tonemap_ResolveComplexWithExposure(in float3 colorT, inout float3 colorU, float4 v3) {
  const float midGray = 0.458656446864f;  // TODO slider
  Tonemap_ExposureMultiplierApply(colorU, Tonemap_Complex(midGray, v3), midGray);
  
  colorT = Tonemap_Complex(colorT, v3);
  return colorT;
}
#endif

#ifdef TONEMAP_FADE
float3 Tonemap_DoFade(float3 x) {
  float4 r0, r1, r2, r3, r4;
  r0 = float4(x, 1);

  r1.x = cmp(0 < g_fade_color.w);
  r1.yzw = g_fade_color.xyz + -r0.xyz;
  r1.yzw = g_fade_color.www * r1.yzw + r0.xyz;
  r2.xy = cmp(g_tone_scale.ww == float2(0,2));
  r3.xyz = g_fade_color.xyz + r0.xyz;
  r4.xyz = g_fade_color.xyz * r0.xyz;
  r2.yzw = r2.yyy ? r3.xyz : r4.xyz;
  r1.yzw = r2.xxx ? r1.yzw : r2.yzw;
  r0.xyz = r1.xxx ? r1.yzw : r0.xyz;

  return r0.xyz;
}
#endif

// float3 Tonemap_ExposureComplexSimple(in float3 colorTonemapped, in float v3) {
//   const float exp = v3;
//   return colorTonemapped * exp;
// }

void Tonemap_SaveSprites(inout float3 colorUntonemapped, in float colorUntonemappedMask, in float3 colorVanilla, in float2 uv) {
  // gatekeep: was 3d rendered
  if (RENODX_TONE_MAP_TYPE == 0 || colorUntonemappedMask > 0 || (Sum3(colorUntonemapped) > 0.0005f && Sum3(colorVanilla) <= 0)) {
    // if (CUSTOM_BGSPRITES_DEBUG == 1) colorUntonemapped = (float3)0; //debug
    return;
  }

  // //debug
  // if (CUSTOM_BGSPRITES_DEBUG == 1) {
  //   const float size = 0.01f;
  //   const float sizeHalf = size/2.f;
  //   colorUntonemapped = ((uv.x % size < sizeHalf) && (uv.y % size > sizeHalf)) ? (float3)0.35f : (float3)0.1f;
  //   return;
  // }

  // //y correct sprites (should only effect mov)
  // {
  //   float3 colorSpritesDecoded = /* renodx::color::gamma::DecodeSafe */(colorSprites);
  //   float colorSpritesDecodedY = renodx::color::y::from::BT709(colorSprites);

  //   //only if HDR range
  //   if (colorSpritesDecodedY > 1) {
  //     float3 r0Decoded = /* renodx::color::gamma::DecodeSafe */(r0.xyz);
  //     float r0DecodedY = renodx::color::y::from::BT709(r0Decoded);

  //     r0.xyz = renodx::color::correct::Luminance(r0Decoded, r0DecodedY, colorSpritesDecodedY, 1);
  //   }

  //   // r0.xyz = renodx::color::gamma::EncodeSafe(r0.xyz);
  // }

  // colorUntonemap save blacks
  colorUntonemapped = colorVanilla;
  // colorUntonemapped *= 1.2f;

  // inv
  if (CUSTOM_BGSPRITES_ISINVTONEMAP) {
    const float maxIn = 2.5f;
    colorUntonemapped = min((float3)maxIn, colorUntonemapped);
    colorUntonemapped = renodx::tonemap::inverse::ReinhardScalable(colorUntonemapped, maxIn, 0, CUSTOM_BGSPRITES_EXPOSURE, 0.18f);
  }
}

#define MINIMUM_Y 0.000001f
float3 Tonemap_Do(in float3 colorUntonemapped, in float3 colorTonemapped, in float2 uv, in Texture2D<float4> texColor) {
  if (RENODX_TONE_MAP_TYPE_IS_ON) {
    // sum of colorTonemapped (save blacks)
    // const float sumTonemapped = Sum3(colorTonemapped);

    // decode (100% gamma 2.2)
    colorUntonemapped = renodx::color::gamma::DecodeSafe(colorUntonemapped);
    colorTonemapped = renodx::color::gamma::DecodeSafe(colorTonemapped);

    // CUSTOM_GRADE_SATURATE
    if (CUSTOM_GRADE_SATURATE > 0) colorTonemapped = lerp(colorTonemapped, min(1, colorTonemapped), CUSTOM_GRADE_SATURATE);

    // inverse tonemap (for toon)
    if (
        CUSTOM_TONEMAP2ND_MODE != 1 &&  // not disabled
        (
            CUSTOM_TONEMAP2ND_MODE == 2 ||                  // forced
            CALLBACK_TONEMAP_ISDRAWN >= TONEMAP_START_FLAT  // flat
            )) {
      // colorTonemapped = saturate(colorTonemapped); //just in case
      float y = renodx::color::y::from::BT709(colorTonemapped);
      // y = max(0, y);

      float yDes = y * CUSTOM_TONEMAP2ND_INV_PREEXP;
      yDes = renodx::math::PowSafe(yDes, CUSTOM_TONEMAP2ND_INV_POW);
      // yDes = renodx::tonemap::inverse::Reinhard(yDes);
      yDes *= CUSTOM_TONEMAP2ND_INV_EXP;

      colorUntonemapped = renodx::color::correct::Luminance(colorUntonemapped, y, yDes, 1);
    }

    // colorSDRNeutral prepare (CUSTOM_GRADE_LUMA)
    float3 colorSDRNeutral = renodx::tonemap::Reinhard(colorUntonemapped);

    // ToneMapPass()
    {
      renodx::draw::Config config = renodx::draw::BuildConfig();
      config.tone_map_type = renodx::draw::TONE_MAP_TYPE_UNTONEMAPPED;  // we will do it later, in final
      colorTonemapped = renodx::draw::ToneMapPass(colorUntonemapped, colorTonemapped, colorSDRNeutral, config);
    }

    // intermediate encode for rest of shaders until RenderIntermediatePass
    colorTonemapped = renodx::color::gamma::EncodeSafe(colorTonemapped);
  }

  return colorTonemapped;
}

// static renodx::debug::graph::Config graph_config; //no warning of unused var if out here
float3 Final_Do(in float3 color, in float2 uv, in Texture2D<float4> texColor) {
  // decode
  // color = max(0, color);
  color = renodx::color::gamma::DecodeSafe(color); 

  if (RENODX_TONE_MAP_TYPE_IS_ON) {
    // graph start
    //  if (CUSTOM_TONEMAP_DEBUG == 1.f) graph_config = renodx::debug::graph::DrawStart(uv, color, texColor, RENODX_PEAK_WHITE_NITS, RENODX_DIFFUSE_WHITE_NITS);

    renodx::draw::Config draw_config = renodx::draw::BuildConfig();
    renodx::tonemap::Config tone_map_config = renodx::tonemap::config::Create();
    {
      tone_map_config.peak_nits = draw_config.peak_white_nits;
      tone_map_config.game_nits = draw_config.diffuse_white_nits;
      tone_map_config.type = min(draw_config.tone_map_type, 3);
      tone_map_config.gamma_correction = draw_config.gamma_correction;
      tone_map_config.exposure = draw_config.tone_map_exposure;
      tone_map_config.highlights = draw_config.tone_map_highlights;
      tone_map_config.shadows = draw_config.tone_map_shadows;
      tone_map_config.contrast = draw_config.tone_map_contrast;
      tone_map_config.saturation = draw_config.tone_map_saturation;

      tone_map_config.reno_drt_highlights = 1.0f;
      tone_map_config.reno_drt_shadows = 1.0f;
      tone_map_config.reno_drt_contrast = 1.0f;
      tone_map_config.reno_drt_saturation = 1.0f;
      tone_map_config.reno_drt_blowout = -1.f * (draw_config.tone_map_highlight_saturation - 1.f);
      tone_map_config.reno_drt_dechroma = draw_config.tone_map_blowout;
      tone_map_config.reno_drt_flare = 0.10f * pow(draw_config.tone_map_flare, 10.f);
      tone_map_config.reno_drt_working_color_space = draw_config.tone_map_working_color_space;
      tone_map_config.reno_drt_per_channel = draw_config.tone_map_per_channel == 1.f;
      tone_map_config.reno_drt_hue_correction_method = draw_config.tone_map_hue_processor;
      tone_map_config.reno_drt_clamp_color_space = draw_config.tone_map_clamp_color_space;
      tone_map_config.reno_drt_clamp_peak = draw_config.tone_map_clamp_peak;
      tone_map_config.reno_drt_tone_map_method = draw_config.reno_drt_tone_map_method;
      tone_map_config.reno_drt_white_clip = draw_config.reno_drt_white_clip;
    }
    color = renodx::tonemap::config::Apply(color, tone_map_config);

    // graph end
    //  if (CUSTOM_TONEMAP_DEBUG == 1.f) color = renodx::debug::graph::DrawEnd(color, graph_config);
  } else {
    color = min(1, color);
  }

  color = renodx::draw::RenderIntermediatePass(color);

  return color;
}

float3 Mov_Do(in float3 color) {
  if (CALLBACK_TONEMAP_ISDRAWN == 0.f) {  // if tonemap0
    color.xyz *= CUSTOM_MOV_MULTIPLIER;
    color.xyz = saturate(color.xyz);  // else, there is weird black
    color.xyz = CUSTOM_MOV_ISUPSCALE ? renodx::draw::UpscaleVideoPass(color.xyz) : color.xyz;
  }

  return color;
}