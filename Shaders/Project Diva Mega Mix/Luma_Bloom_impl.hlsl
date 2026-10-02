// Bloom
//

#include "./common1.hlsl"

cbuffer LumaBloom : register(b11)
{
  float2 src_size;
  float2 inv_src_size;
  float2 axis;
  float sigma;
  float tex_noise_index;
}
#define SUGMA sigma

cbuffer Quad : register(b0)
{
  float4 g_texcoord_modifier : packoffset(c0);
  float4 g_texel_size : packoffset(c1);
  float4 g_color : packoffset(c2);
  float4 g_texture_lod : packoffset(c3);
}

SamplerState smp : register(s0);
Texture2D tex : register(t0);

// Fullscreen triangle VS.
void bloom_main_vs(uint vid: SV_VertexID, out float4 pos: SV_Position, out float2 texcoord: TEXCOORD)
{
  texcoord = float2((vid << 1) & 2, vid & 2);
  pos = float4(texcoord * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

float get_gaussian_weight(float x, float s)
{
  return exp(-x * x * rcp(2.0 * s * s));
}

float almost_max_lerp(float a, float b, float r, bool isDisardLessB = false)
{
  if (isDisardLessB && a > b) return a;
  return lerp(a, b, a > b ? 1 - r : r);
}

float3 almost_max_lerp(float3 a, float3 b, float r, bool isDisardLessB = false)
{
  float aH = /* max3(a) */ GetLuminance(a);
  float bH = /* max3(b) */ GetLuminance(b);
  if (isDisardLessB && aH > bH) return a;
  return lerp(a, b, aH > bH ? 1-r : r);
  // return PerceptualLerp(float4(a, 1), float4(b, 1), aH > bH ? 1-r : r).xyz;
  
  // return lerp(a, b, a > b ? 1 - r : r);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

float3 bloom_downsample_almost_max(float4 pos, float2 texcoord, float sigmaBoost = 0)
{
  // texcoord
  const float f = dot(frac(texcoord * src_size - 0.5), axis);
  const float2 tc = texcoord - f * inv_src_size * axis;
  
  // accumlators
  float3 csum = 0.0;
  float wsum = 0.0;
  // float3 cmax = 0.0;
  float ymax = 0.0;

  // loop
  float sigma1 = /* SUGMA */0.1 + sigmaBoost;
  const float radius = ceil(sigma1 * 3.0);
  [unroll] for (float i = 1.0 - radius; i <= radius; i++)
  {
    const float weight = get_gaussian_weight(i - f, sigma1);
    float3 b = tex.SampleLevel(smp, tc + i * inv_src_size * axis, 0.0).rgb;
    
    csum += b * weight;
    wsum += weight;

    // cmax = almost_max_lerp(cmax, b, 0.777);

    float ynew = GetLuminance(b);
    ymax = almost_max_lerp(ymax, ynew, 0.850, true);
  }

  // normalize
  csum *= rcp(wsum);
  
  #define CMAX_MODE 4
  #if CMAX_MODE == 0
    // // average (SUCKS AS MAX CHANNEL BIAS HUES)
    // csum += cmax;
    // csum /= 2.0;
  #elif CMAX_MODE == 1
    // // max per channel lerp (SUCKS AS MAX CHANNEL BIAS HUES)
    // csum = almost_max_lerp(csum, cmax, 0.777);
  #elif CMAX_MODE == 2
    // // max per channel full (SUCKS AS MAX CHANNEL BIAS HUES)
    // csum = cmax;
  #elif CMAX_MODE == 3
    // // luminance match using cmax luminance (SUCKS AS MAX CHANNEL BIAS HUES)
    // float csumY = GetLuminance(csum);
    // float cmaxY = GetLuminance(cmax);
    // csum *= safeDivision(cmaxY, csumY, 1);
  #elif CMAX_MODE == 4
    // luminance match with y accumulator
    // TODO: there is disconnect between guassian per channel & y accumulator, able to cause hue artifacts by boosting low luminance colors, 
    //       but it's much more faithful to original weights & unnoticable after blur.
    float csumY = GetLuminance(csum);
    csum *= safeDivision(ymax, csumY, 1);
  #elif CMAX_MODE == 5
    // UCS blending between both
    // TODO: too costly?
  #endif
  
  return csum;
}

float3 bloom_downsample_all_within(float4 pos, float2 texcoord)
{
  // texcoord
  const float f = dot(frac(texcoord * src_size - 0.5), axis);
  const float2 tc = texcoord - f * inv_src_size * axis;

  float3 b0 = tex.SampleLevel(smp, tc, 0.0).rgb;
  float3 b1 = tex.SampleLevel(smp, tc + inv_src_size * axis, 0.0).rgb;
  // float3 csum = (b0 + b1) * 0.5; // box avg
  // float3 csum = max(b0, b1); // per channel max

  float3 b2 = tex.SampleLevel(smp, tc + 2.0 * inv_src_size * axis, 0.0).rgb;
  float3 b3 = tex.SampleLevel(smp, tc - 1.0 * inv_src_size * axis, 0.0).rgb;

  float3 csum = (b0 + b1 + b2 + b3) * 0.25; // box avg 4

  return csum;
}

float3 bloom_downsample_guassian(float4 pos, float2 texcoord, float sigmaBoost = 0)
{
  // texcoord
  const float f = dot(frac(texcoord * src_size - 0.5), axis);
  const float2 tc = texcoord - f * inv_src_size * axis;
  
  // accumulators
  float3 csum = 0.0;
  float wsum = 0.0;
  float sigma1 = SUGMA + sigmaBoost;

  // loop
  const float radius = ceil(sigma1 * 3.0);
  for (float i = 1.0 - radius; i <= radius; i++)
  {
    const float weight = get_gaussian_weight(i - f, sigma1);
    csum += tex.SampleLevel(smp, tc + i * inv_src_size * axis, 0.0).rgb * weight;
    wsum += weight;
  }
  
  // normalize
  csum *= rcp(wsum);
  
  return csum;
}

float4 bloom_downsample_and_prefilter(float4 pos, float2 texcoord, bool isPrefilter)
{
  // float3 csum = bloom_downsample_all_within(pos, texcoord);
  float3 csum = bloom_downsample_almost_max(pos, texcoord, 0.5);
  // float3 csum = bloom_downsample_guassian(pos, texcoord, 4);

  if (isPrefilter)
  {
    const float luma = GetLuminance(csum);
    if (luma > 0) {
      // tint (to correct too much yellow compared to vanilla)
      csum *= float3(0.955, 0.958, 1.00);
      csum *= luma * rcp(max(1e-6, GetLuminance(csum)));

      // fudge (to correct weight closer to vanilla)
      float y = luma;
      if (y > 0) {
        float y1 = y;
        y1 = RenoDX_Contrast(y1, 0.102 + 1, 0.667);
        csum *= y1 / y;
      }
    }

    // threshold https://www.desmos.com/calculator/tai2ea2f2t
    float3 csumBack = csum;
    csum = BloomThreshold(csum, g_color.xyz);

    // test g_color equal
#if DEVELOPMENT
    if (g_color.x != g_color.y && g_color.y != g_color.z) csum = float3(1, 0, 1);
    if (g_color.x != 1.1 && g_color.y != 1.1 && g_color.z != 1.1) csum = float3(1, 1, 0);
    // so far, components are ALWAYS equal!
    // so far, usually 1.1
#endif
  }

  return float4(csum, 1.0);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// full to mip0, right before prefilter
float4 bloom_downsample1_ps(float4 pos: SV_Position, float2 texcoord: TEXCOORD) : SV_Target
{
  return bloom_downsample_and_prefilter(pos, texcoord, false);
}

// full to mip0, w/ prefilter
float4 bloom_prefilter_ps(float4 pos: SV_Position, float2 texcoord: TEXCOORD) : SV_Target
{
  return bloom_downsample_and_prefilter(pos, texcoord, true);
}

// rest of downsampling
float4 bloom_downsample_ps(float4 pos: SV_Position, float2 texcoord: TEXCOORD) : SV_Target
{
  float3 csum = bloom_downsample_guassian(pos, texcoord);
  return float4(csum, 1.0);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Bicubic upsampling in 4 texture fetches.
//
// f(x) = (4 + 3 * |x|^3 – 6 * |x|^2) / 6 for 0 <= |x| <= 1
// f(x) = (2 – |x|)^3 / 6 for 1 < |x| <= 2
// f(x) = 0 otherwise
//
// Source: https://www.researchgate.net/publication/220494113_Efficient_GPU-Based_Texture_Interpolation_using_Uniform_B-Splines
float4 bloom_upsample_ps(float4 pos: SV_Position, float2 texcoord: TEXCOORD) : SV_Target
{
  // transform the coordinate from [0,extent] to [-0.5, extent-0.5]
  float2 coord_grid = texcoord * src_size - 0.5;
  float2 index = floor(coord_grid);
  float2 fraction = coord_grid - index;
  float2 one_frac = 1.0 - fraction;
  float2 one_frac2 = one_frac * one_frac;
  float2 fraction2 = fraction * fraction;
  float2 w0 = 1.0 / 6.0 * one_frac2 * one_frac;
  float2 w1 = 2.0 / 3.0 - 0.5 * fraction2 * (2.0 - fraction);
  float2 w2 = 2.0 / 3.0 - 0.5 * one_frac2 * (2.0 - one_frac);
  float2 w3 = 1.0 / 6.0 * fraction2 * fraction;
  float2 g0 = w0 + w1;
  float2 g1 = w2 + w3;
  
  // h0 = w1/g0 - 1, move from [-0.5, extent-0.5] to [0, extent]
  float2 h0 = (w1 / g0) - 0.5 + index;
  float2 h1 = (w3 / g1) + 1.5 + index;
  
  // fetch the four linear interpolations
  float3 tex00 = tex.SampleLevel(smp, float2(h0.x, h0.y) * inv_src_size, 0.0).rgb;
  float3 tex10 = tex.SampleLevel(smp, float2(h1.x, h0.y) * inv_src_size, 0.0).rgb;
  float3 tex01 = tex.SampleLevel(smp, float2(h0.x, h1.y) * inv_src_size, 0.0).rgb;
  float3 tex11 = tex.SampleLevel(smp, float2(h1.x, h1.y) * inv_src_size, 0.0).rgb;
  
  // weigh along the y-direction
  tex00 = lerp(tex01, tex00, g0.y);
  tex10 = lerp(tex11, tex10, g0.y);
  
  // weigh along the x-direction
  return float4(lerp(tex10, tex00, g0.x), 1.0);
}

// bloom_combine_ps
SamplerState g_sampler_s : register(s0);
Texture2D<float4> g_textures_0_ : register(t0); // 255x144 (1/1)
Texture2D<float4> g_textures_1_ : register(t1); // 128x72  (1/2)
Texture2D<float4> g_textures_2_ : register(t2); // 64x36   (1/4)
Texture2D<float4> g_textures_3_ : register(t3); // 32x18   (1/8)
// out: 255x144

float4 BloomUpsample2(Texture2D tex, SamplerState smp, float2 texcoord, float2 texSize, float2 pixSize)
{
  float2 coord_grid = texcoord * texSize - 0.5;
  float2 index = floor(coord_grid);
  float2 fraction = coord_grid - index;
  float2 one_frac = 1.0 - fraction;
  float2 one_frac2 = one_frac * one_frac;
  float2 fraction2 = fraction * fraction;
  float2 w0 = 1.0 / 6.0 * one_frac2 * one_frac;
  float2 w1 = 2.0 / 3.0 - 0.5 * fraction2 * (2.0 - fraction);
  float2 w2 = 2.0 / 3.0 - 0.5 * one_frac2 * (2.0 - one_frac);
  float2 w3 = 1.0 / 6.0 * fraction2 * fraction;
  float2 g0 = w0 + w1;
  float2 g1 = w2 + w3;
  
  // h0 = w1/g0 - 1, move from [-0.5, extent-0.5] to [0, extent]
  float2 h0 = (w1 / g0) - 0.5 + index;
  float2 h1 = (w3 / g1) + 1.5 + index;
  
  // fetch the four linear interpolations
  float4 tex00 = tex.SampleLevel(smp, float2(h0.x, h0.y) * pixSize, 0.0).xyzw;
  float4 tex10 = tex.SampleLevel(smp, float2(h1.x, h0.y) * pixSize, 0.0).xyzw;
  float4 tex01 = tex.SampleLevel(smp, float2(h0.x, h1.y) * pixSize, 0.0).xyzw;
  float4 tex11 = tex.SampleLevel(smp, float2(h1.x, h1.y) * pixSize, 0.0).xyzw;
  
  // weigh along the y-direction
  tex00 = lerp(tex01, tex00, g0.y);
  tex10 = lerp(tex11, tex10, g0.y);
  
  // weigh along the x-direction
  return lerp(tex10, tex00, g0.x);
}

void bloom_blur0_vs(
  uint v0: SV_VertexID0,
  out float4 o0: SV_POSITION0,
  out float4 o1: TEXCOORD0,
  out float4 o2: TEXCOORD1,
  out float2 o3: TEXCOORD2
) {
  float4 r0;
  uint4 bitmask, uiDest;
  float4 fDest;
  
  r0.x = (uint)v0.x >> 1;
  r0.x = (uint)r0.x;
  r0.x = r0.x * 2 + -1;
  r0.z = (int)v0.x & 1;
  r0.z = (uint)r0.z;
  r0.y = r0.z * 2 + -1;
  o0.xy = r0.xy;
  r0.xyzw = r0.xyxy * g_texcoord_modifier.xyxy + g_texcoord_modifier.zwzw;
  o0.zw = float2(0, 1);
  
  o1.xyzw = g_texel_size.xyxy * float4(-0.5, -0.5, 0.5, -0.5) + r0.zwzw;
  o2.xyzw = g_texel_size.xyxy * float4(-0.5, 0.5, 0.5, 0.5) + r0.xyzw;
  o3.xy = r0.xy; // center
  
  return;
}

void bloom_blur0_ps(
  float4 v0: SV_POSITION0,
  float4 v1: TEXCOORD0,
  float4 v2: TEXCOORD1,
  float2 v3: TEXCOORD2,
  out float4 o0: SV_Target0
) {
  float4 r0, r1;
  uint4 bitmask, uiDest;
  float4 fDest;

  // // blurred avg
  // r0.xyzw = g_textures_0_.Sample(g_sampler_s, v1.xy).xyzw;
  // r1.xyzw = g_textures_0_.Sample(g_sampler_s, v1.zw).xyzw;
  // r0.xyzw = r1.xyzw + r0.xyzw;
  // r1.xyzw = g_textures_0_.Sample(g_sampler_s, v2.xy).xyzw;
  // r0.xyzw = r1.xyzw + r0.xyzw;
  // r1.xyzw = g_textures_0_.Sample(g_sampler_s, v2.zw).xyzw;
  // r0.xyzw = r1.xyzw + r0.xyzw;
  // o0.xyzw = g_color.xyzw * 0.25 * r0.xyzw;

  // float4 bloom = BloomUpsample2(g_textures_0_, g_sampler_s, v3.xy, g_texel_size.zw, g_texel_size.xy);
  // o0 = g_color * bloom;

  float4 bloom = g_textures_0_.Sample(g_sampler_s, v3.xy).xyzw;
  o0 = g_color * bloom;
}
    
void bloom_combine_ps(
  float4 v0: SV_POSITION0,
  float4 v1: TEXCOORD0,
  float4 v2: TEXCOORD1,
  float2 v3: TEXCOORD2,
  out float4 o0: SV_Target0
) {
  float4 r0, r1;
  uint4 bitmask, uiDest;
  float4 fDest;
  o0.w = 1;
  
  //   uint2 texSize1;
  //   g_textures_1_.GetDimensions(texSize1.x, texSize1.y);
  //   float2 texSize2 = texSize1 * 0.5f;
  //   float2 texSize3 = texSize2 * 0.5f;
  //
  //   float2 pixSize1 = rcp(texSize1);
  //   float2 pixSize2 = rcp(texSize2);
  //   float2 pixSize3 = rcp(texSize3);
  //
  //   float4 b0 = g_textures_0_.Sample(g_sampler_s, v3.xy).xyzw;
  //   float3 b1 = BloomUpsample2(g_textures_1_, g_sampler_s, v3.xy, texSize1, pixSize1);
  //   float3 b2 = BloomUpsample2(g_textures_2_, g_sampler_s, v3.xy, texSize2, pixSize2);
  //   float3 b3 = BloomUpsample2(g_textures_3_, g_sampler_s, v3.xy, texSize3, pixSize3);
  
  // float4 b0 = g_textures_0_.Sample(g_sampler_s, v3.xy).xyzw;
  // float3 b1 = g_textures_1_.Sample(g_sampler_s, v3.xy).xyz;
  // float3 b2 = g_textures_2_.Sample(g_sampler_s, v3.xy).xyz;
  // float3 b3 = g_textures_3_.Sample(g_sampler_s, v3.xy).xyz;
  
  uint2 texSize0;
  g_textures_0_.GetDimensions(texSize0.x, texSize0.y);
  float2 texSize1 = texSize0 * 0.5f;
  float2 texSize2 = texSize1 * 0.5f;
  float2 texSize3 = texSize2 * 0.5f;
  
  float2 pixSize0 = rcp(texSize0);
  float2 pixSize1 = rcp(texSize1);
  float2 pixSize2 = rcp(texSize2);
  float2 pixSize3 = rcp(texSize3);

  float4 b0 = g_textures_0_.SampleLevel(g_sampler_s, v3.xy, 0).xyzw /* BloomUpsample2(g_textures_0_, g_sampler_s, v3.xy, texSize0, pixSize0).xyzw */;
  float3 b1 = BloomUpsample2(g_textures_1_, g_sampler_s, v3.xy, texSize1, pixSize1).xyz;
  float3 b2 = BloomUpsample2(g_textures_2_, g_sampler_s, v3.xy, texSize2, pixSize2).xyz;
  float3 b3 = BloomUpsample2(g_textures_3_, g_sampler_s, v3.xy, texSize3, pixSize3).xyz;
  
  o0.w = b0.w;
  o0.xyz = b0.xyz  * (g_color.x * GS.BloomStrengths.x * 1.466 /* * DVS1 */);
  o0.xyz += b1.xyz * (g_color.y * GS.BloomStrengths.y * 0.976 /* * DVS2 */);
  o0.xyz += b2.xyz * (g_color.z * GS.BloomStrengths.z * 0.970 /* * DVS3 */);
  o0.xyz += b3.xyz * (g_color.w * GS.BloomStrengths.w * 0.836 /* * DVS4 */);
  
  //   o0 = b0; //debug
  
  o0.xyz *= GS.BloomStrength /* * 1.26 */;
  
  return;
}
      