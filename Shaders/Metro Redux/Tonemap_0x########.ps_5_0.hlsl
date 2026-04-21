#if _F398A1ED || _FA7FE535
#define HAS_GASMASK 1
#endif

#if _A453ADB1 || _FA7FE535
#define HAS_LIGHT_SHAFTS 1
#endif

#if _5C867D7E || _F398A1ED || _A453ADB1 || _FA7FE535
#define HAS_DOF 1
#endif

#define LUT_3D 1

#include "../Includes/Common.hlsl"
#include "../Includes/Tonemap.hlsl"
#include "../Includes/Reinhard.hlsl"

#include "../Includes/ColorGradingLUT.hlsl"

#define USE_VANILLA 0

// CUSTOM FUNCTIONS

float3 ExtendedUncharted2(float3 untonemapped) {

  const float A = 0.150000006f, B = 0.5f, C = 0.100000001f, D = 0.200000003f, E = 0.0199999996f, F = 0.300000012f;
  const float W = 1.0f;

  float3 originalColor = 0.f;
  float3 outputColor = Uncharted2::Tonemap_Uncharted2_Extended(
      untonemapped,
      false,
      originalColor,
      1,
      0.18f,
      0,
      W,
      A, B, C, D, E, F);

  return outputColor;
}

float ComputeReinhardSmoothClampScale(float3 untonemapped, float rolloff_start = 0.375f, float output_max = 1.f, float white_clip = 100.f) {
    float peak = max(untonemapped.r, max(untonemapped.g, untonemapped.b));
    float mapped_peak = Reinhard::ReinhardRange(peak, rolloff_start, white_clip, output_max).x;
    float scale = safeDivision(mapped_peak, peak, 1);

    return scale;
}

// Modernizes old school grading that either clips or raises blacks, not really suitable for modern OLEDs.
// Perceptually raising shadow without raising the black floor.
float3 AddColorOffsetDampened(float3 Color, float3 Offset, float Dampening = 1.f, bool AllowShadowCrush = false)
{
    // Whether the offset was removing color (causing clipping in SDR, and expanding the color range in HDR), or adding to color (raising blacks),
    // for a range matching double of its abs offset, don't fully apply the offset.
    // This means 0 will stay 0, while most of the image will still be affected.
    // This will also prevent offsets from accidentally generating invalid negative values in HDR,
    // that would sometimes expand the gamut, but more often simply generate weird or broken colors.
    float3 OffsetAlpha = saturate(Color / abs(Offset * 2.f));

    // Square to make the dampening more perceptual. Halving in linear space has a much weaker intensity than halving in gamma space.
    // If we don't do this, negative offsets would also generate below 0 colors.
    OffsetAlpha *= float3((Offset.x > 0.f && !AllowShadowCrush) ? OffsetAlpha.x : 1.f, (Offset.y > 0.f && !AllowShadowCrush) ? OffsetAlpha.y : 1.f, (Offset.z > 0.f && !AllowShadowCrush) ? OffsetAlpha.z : 1.f);

    // If the offset go too high, force apply them anyway
    OffsetAlpha = lerp(OffsetAlpha, 1.f, sqrt(saturate((abs(Offset) - MidGray) / (1.f - MidGray))));

    // Intensity scale
    OffsetAlpha = lerp(1.f, OffsetAlpha, sqrt(Dampening));

    Color += Offset * OffsetAlpha;

    return Color;
}

// END CUSTOM FUNCTIONS

// ---- Created with 3Dmigoto v1.3.16 on Thu Aug 29 12:45:16 2024

cbuffer cb_screen : register(b2)
{
  float4 rtdim : packoffset(c0);
  float4 depth_xform : packoffset(c1);
  float4 envmap_color : packoffset(c2);
  float4 sph_r[3] : packoffset(c3);
  float4 sph_g[3] : packoffset(c6);
  float4 sph_b[3] : packoffset(c9);
}

cbuffer cb_misc_0 : register(b3)
{
  float4 fog_color : packoffset(c0);
  float4 fog_height : packoffset(c1);
  float4 sky_color : packoffset(c2);
  float4 clouds_dir : packoffset(c3);
  float4 ambient : packoffset(c4);
  float4 pp_dof : packoffset(c5);
  float4 pp_gasmask : packoffset(c6);
  float4 pp_gasmask_c : packoffset(c7);
  float4 g_color : packoffset(c8);
  row_major float4x4 m_screen : packoffset(c9);
  float4 ddof_params : packoffset(c13);
  float4 gi_bbox[3] : packoffset(c14);
}

cbuffer cb_sun : register(b6)
{
  float4 sun_direction : packoffset(c0);
  float4 sun_position : packoffset(c1);
  float4 sun_color : packoffset(c2);
  row_major float4x4 sun_shadow_near : packoffset(c3);
  row_major float4x4 sun_shadow_mid : packoffset(c7);
  row_major float4x4 sun_shadow_far : packoffset(c11);
  float4 lshafts_color : packoffset(c15);
}

SamplerState s_wrap_tri_s : register(s2);
SamplerState s_clamp_tri_s : register(s5);
SamplerState s_clamp_bi_s : register(s6);
Texture2D<float4> t_dudv : register(t0);
Texture2D<float4> t_position : register(t1);
Texture2D<float4> t_accum : register(t2);
Texture2D<float4> t_bloom : register(t3);
Texture2D<float4> t_bloom_n : register(t4);
Texture2D<float4> t_bloom_b : register(t5);
Texture2D<float4> t_image_nomsaa_srcdof : register(t6);
Texture3D<float4> t_grade : register(t7);
Texture2D<float4> t_lensdirt : register(t8);
Texture2D<float4> t_gm_base : register(t9);
Texture2D<float4> t_gm_dist : register(t10);


// 3Dmigoto declarations
#define cmp -

float3 SampleLightShafts(float2 originUV, float2 dirUV)
{
  float3 sum = 0.0;

  float4 sample0 = t_image_nomsaa_srcdof.Sample(s_clamp_bi_s, originUV).xyzw;
  sum += sample0.xyz * ((sample0.w > 1024.0) ? 1.0 : 0.0);

  [unroll]
  for (uint i = 1; i < 32; i++)
  {
    const float t = i * (1.0 / 32.0);
    const float2 uv = originUV + dirUV * t;
    float4 s = t_image_nomsaa_srcdof.Sample(s_clamp_bi_s, uv).xyzw;
    sum += s.xyz * ((s.w > 1024.0) ? 1.0 : 0.0);
  }

  return sum * (1.0 / 32.0);
}

float3 SampleLightShaftsLevel(float2 originUV, float2 dirUV)
{
  float3 sum = 0.0;

  float4 sample0 = t_image_nomsaa_srcdof.SampleLevel(s_clamp_bi_s, originUV, 0).xyzw;
  sum += sample0.xyz * ((sample0.w > 1024.0) ? 1.0 : 0.0);

  [unroll]
  for (uint i = 1; i < 32; i++)
  {
    const float t = i * (1.0 / 32.0);
    const float2 uv = originUV + dirUV * t;
    float4 s = t_image_nomsaa_srcdof.SampleLevel(s_clamp_bi_s, uv, 0).xyzw;
    sum += s.xyz * ((s.w > 1024.0) ? 1.0 : 0.0);
  }

  return sum * (1.0 / 32.0);
}


void main(
  float4 v0 : SV_Position0,
  float3 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = rtdim.xy * v0.xy;

  float2 coords = r0.xy;

  r1.xy = (int2)v0.xy;
  r1.zw = float2(0,0);
  r0.z = t_position.Load(r1.xyw).z;
  r1.xy = t_dudv.Load(r1.xyz).xy;
#if HAS_GASMASK
  r2.xyz = t_gm_base.Sample(s_clamp_tri_s, r0.xy).xyw;
  r1.zw = float2(5,3) * r0.xy;
  r3.xyz = t_gm_dist.Sample(s_wrap_tri_s, r1.zw).xyw;
  r4.xyz = float3(1.89999998,-0.501960814,-0.501960814) + r2.zxy;
  r0.w = r4.x + -r2.x;
  r0.w = r0.w + -r2.y;
  r0.w = saturate(10 * r0.w);
  r1.zw = float2(0.100000001,0.100000001) * r4.yz;
  r2.w = -3 + pp_gasmask.w;
  r2.w = r2.z * r2.w;
  r3.z = r3.z * 2 + pp_gasmask.w;
  r3.xyz = float3(-0.501960814,-0.501960814,-1) + r3.xyz;
  r2.w = r3.z * r2.w;
  r3.z = r2.z * 2 + -1;
  r2.w = saturate(r3.z * r2.w);
  r3.xy = -r4.yz * float2(0.100000001,0.100000001) + r3.xy;
  r1.zw = r2.ww * r3.xy + r1.zw;
  r1.zw = r1.zw * r0.ww;
  r1.xy = r1.xy * float2(0.0350000001,0.0350000001) + r1.zw;
#else
  r1.xy = r1.xy * float2(0.0350000001,0.0350000001);
#endif
  r3.xyzw = rtdim.xyxy * float4(-0.400000006,0.800000012,0.800000012,0.400000006) + r0.xyxy;
  r4.xyzw = rtdim.xyxy * float4(0.400000006,-0.800000012,-0.800000012,-0.400000006) + r0.xyxy;
  r0.xyw = t_accum.Sample(s_clamp_bi_s, r3.xy).xyz;
  r3.xyz = t_accum.Sample(s_clamp_bi_s, r3.zw).xyz;
  r0.xy = r3.xy + r0.xy;
  r0.w = min(r3.z, r0.w);
  r3.xyz = t_accum.Sample(s_clamp_bi_s, r4.xy).xyz;
  r0.xy = r3.xy + r0.xy;
  r0.w = min(r3.z, r0.w);
  r3.xyz = t_accum.Sample(s_clamp_bi_s, r4.zw).xyz;
  r0.xy = r3.xy + r0.xy;
  r0.w = min(r3.z, r0.w);
  r0.xy = float2(0.25,0.25) * r0.xy;
  r1.z = dot(r0.xy, r0.xy);
  r1.z = sqrt(r1.z);
  r1.z = 9.99999975e-06 + r1.z;
  r1.z = 0.00282842712 / r1.z;
  r1.z = min(1, r1.z);
  r3.yz = r1.zz * r0.xy;
  r0.x = rtdim.x / rtdim.y;
  r3.x = r3.y * r0.x;
  r1.xy = v0.xy * rtdim.xy + r1.xy;
  r4.xyzw = t_bloom.Sample(s_clamp_bi_s, r1.xy).xyzw;
  r4.xyzw = r4.xyzw + r4.xyzw;
  r5.xyzw = t_bloom_n.Sample(s_clamp_bi_s, r1.xy).xyzw;
  r6.xyzw = r5.xyzw + r5.xyzw;
#if HAS_DOF
  r3.yw = saturate(r0.zz * pp_dof.wz + -pp_dof.xy);
#else
  r3.yw = float2(0,0);
#endif
  r0.y = 1 + -r3.y;
  r0.y = -r3.w + r0.y;
  r0.z = cmp(1024 < r0.z);
  r0.y = r0.z ? -r4.w : r0.y;
  r0.z = max(9.99999975e-05, abs(r4.w));
  r4.xyz = r4.xyz / r0.zzz;
  r0.y = saturate(-r0.y);
  r0.z = max(9.99999975e-05, r6.w);
  r5.xyz = r6.xyz / r0.zzz;
  r0.z = saturate(3 * r5.w);
  r1.w = max(r0.y, r0.z);
  r6.xyzw = float4(-4,-4,-3,-3) * r3.xzxz;
  r7.xyzw = rtdim.xyxy * float4(-1.3125,0.1875,-0.9375,-0.9375) + -r6.xyzw;
  r6.xyzw = r1.wwww * r7.xyzw + r6.xyzw;
  r6.xyzw = r6.xyzw + r1.xyxy;
  r7.xyzw = t_image_nomsaa_srcdof.Sample(s_clamp_bi_s, r6.xy).xyzw;
  r2.w = -r7.w + r0.w;
  r2.w = max(0, r2.w);
  r2.w = r2.w * 3 + 1;
  r2.w = 1 / r2.w;
  r6.xyzw = t_image_nomsaa_srcdof.Sample(s_clamp_bi_s, r6.zw).xyzw;
  r3.y = -r6.w + r0.w;
  r3.y = max(0, r3.y);
  r3.y = r3.y * 3 + 1;
  r3.y = 1 / r3.y;
  r6.xyz = r6.xyz * r3.yyy;
  r6.xyz = r7.xyz * r2.www + r6.xyz;
  r2.w = r3.y + r2.w;
  r7.xyzw = float4(-2,-2,3,3) * r3.xzxz;
  r3.yw = rtdim.xy * float2(-0.5625,0.9375) + -r7.xy;
  r3.yw = r1.ww * r3.yw + r7.xy;
  r3.yw = r3.yw + r1.xy;
  r8.xyzw = t_image_nomsaa_srcdof.Sample(s_clamp_bi_s, r3.yw).xyzw;
  r3.y = -r8.w + r0.w;
  r3.y = max(0, r3.y);
  r3.y = r3.y * 3 + 1;
  r3.y = 1 / r3.y;
  r6.xyz = r8.xyz * r3.yyy + r6.xyz;
  r2.w = r3.y + r2.w;
  r3.yw = rtdim.xy * float2(-0.1875,-0.5625) + r3.xz;
  r3.yw = r1.ww * r3.yw + -r3.xz;
  r3.yw = r3.yw + r1.xy;
  r8.xyzw = t_image_nomsaa_srcdof.Sample(s_clamp_bi_s, r3.yw).xyzw;
  r3.y = -r8.w + r0.w;
  r3.y = max(0, r3.y);
  r3.y = r3.y * 3 + 1;
  r3.y = 1 / r3.y;
  r6.xyz = r8.xyz * r3.yyy + r6.xyz;
  r2.w = r3.y + r2.w;
  r3.yw = rtdim.xy * r1.ww;
  r3.yw = r3.yw * float2(0.1875,0.5625) + r1.xy;
  r8.xyzw = t_image_nomsaa_srcdof.Sample(s_clamp_bi_s, r3.yw).xyzw;
  r3.y = -r8.w + r0.w;
  r3.y = max(0, r3.y);
  r3.y = r3.y * 3 + 1;
  r3.y = 1 / r3.y;
  r6.xyz = r8.xyz * r3.yyy + r6.xyz;
  r2.w = r3.y + r2.w;
  r3.yw = rtdim.xy * float2(0.5625,-1.3125) + -r3.xz;
  r3.yw = r1.ww * r3.yw + r3.xz;
  r3.yw = r3.yw + r1.xy;
  r8.xyzw = t_image_nomsaa_srcdof.Sample(s_clamp_bi_s, r3.yw).xyzw;
  r3.y = -r8.w + r0.w;
  r3.y = max(0, r3.y);
  r3.y = r3.y * 3 + 1;
  r3.y = 1 / r3.y;
  r6.xyz = r8.xyz * r3.yyy + r6.xyz;
  r2.w = r3.y + r2.w;
  r3.xy = r3.xz + r3.xz;
  r3.zw = rtdim.xy * float2(0.9375,-0.1875) + -r3.xy;
  r3.xy = r1.ww * r3.zw + r3.xy;
  r3.xy = r3.xy + r1.xy;
  r3.xyzw = t_image_nomsaa_srcdof.Sample(s_clamp_bi_s, r3.xy).xyzw;
  r3.w = -r3.w + r0.w;
  r3.w = max(0, r3.w);
  r3.w = r3.w * 3 + 1;
  r3.w = 1 / r3.w;
  r3.xyz = r3.xyz * r3.www + r6.xyz;
  r2.w = r3.w + r2.w;
  r6.xy = rtdim.xy * float2(1.3125,1.3125) + -r7.zw;
  r6.xy = r1.ww * r6.xy + r7.zw;
  r6.xy = r6.xy + r1.xy;
  r6.xyzw = t_image_nomsaa_srcdof.Sample(s_clamp_bi_s, r6.xy).xyzw;
  r0.w = -r6.w + r0.w;
  r0.w = max(0, r0.w);
  r0.w = r0.w * 3 + 1;
  r0.w = 1 / r0.w;
  r3.xyz = r6.xyz * r0.www + r3.xyz;
  r0.w = r2.w + r0.w;
  r3.xyz = r3.xyz / r0.www;
  r4.xyz = r4.xyz + -r3.xyz;
  r3.xyz = r0.yyy * r4.xyz + r3.xyz;
  r4.xyz = r5.xyz + -r3.xyz;
  r0.yzw = r0.zzz * r4.xyz + r3.xyz;
  r1.w = -0.5 + r1.y;
  r1.z = r1.w * r0.x + 0.5;
  r3.xyzw = t_lensdirt.Sample(s_clamp_tri_s, r1.xz).xyzw;
  r1.xyz = t_bloom_b.Sample(s_clamp_bi_s, r1.xy).xyz;
  r3.xyz = r3.xyz * r3.www;
  r3.xyz = float3(5,5,5) * r3.xyz;
  r0.x = pp_gasmask.w * 5 + 1;
  r3.xyz = r3.xyz * r0.xxx + float3(1,1,1);

  //r3.xyz *= CUSTOM_LENS_DIRT;
  //r1.xyz *= CUSTOM_BLOOM;

  r4.xyz = r3.xyz * r1.xyz;
  r0.xyz = r1.xyz * r3.xyz + r0.yzw;
#if USE_VANILLA
  r0.xyz = max(float3(0,0,0), r0.xyz);
#endif

#if HAS_LIGHT_SHAFTS
  float2 shafts_dir = v0.xy * rtdim.xy - sun_position.xy;
  float shafts_dist = length(shafts_dir);
  const float shafts_max_dist = 0.150000006;
  bool do_near_pass = shafts_dist < shafts_max_dist;

  shafts_dir = shafts_dist > 0.0 ? shafts_dir / shafts_dist : float2(0.0, 0.0);
  shafts_dir *= min(shafts_max_dist, shafts_dist);

  float3 shafts_sum = SampleLightShafts(sun_position.xy, shafts_dir);
  if (do_near_pass)
  {
    float2 near_origin = sun_position.xy + shafts_dir * (1.0 / 64.0);
    float3 shafts_near_sum = SampleLightShaftsLevel(near_origin, shafts_dir);
    shafts_sum = (shafts_sum + shafts_near_sum) * 0.5;
  }

  r0.xyz += shafts_sum * lshafts_color.xyz;

#endif

  float3 untonemapped = r0.xyz;
  float3 tonemapped_bt709;
  float scale = 1.f;
  
#if USE_VANILLA
// UC2 tonemap
  r1.xyz = r0.xyz * float3(0.150000006,0.150000006,0.150000006) + float3(0.0500000007,0.0500000007,0.0500000007);
  r1.xyz = r0.xyz * r1.xyz + float3(0.00400000019,0.00400000019,0.00400000019);
  r3.xyz = r0.xyz * float3(0.150000006,0.150000006,0.150000006) + float3(0.5,0.5,0.5);
  r0.xyz = r0.xyz * r3.xyz + float3(0.0600000024,0.0600000024,0.0600000024);
  r0.xyz = r1.xyz / r0.xyz;
  r0.xyz = float3(-0.0666666627,-0.0666666627,-0.0666666627) + r0.xyz;
  r1.xyz = float3(0.125,0.125,0.125) * r4.xyz;
  r0.xyz = r0.xyz * float3(4.53191471, 4.53191471, 4.53191471) + r1.xyz;
  tonemapped_bt709 = r0.xyz;
#else
  tonemapped_bt709 = ExtendedUncharted2(untonemapped);
  scale = ComputeReinhardSmoothClampScale(tonemapped_bt709);
  float3 scaled_bt709 = tonemapped_bt709 * scale;
  r0.xyz = scaled_bt709;
#endif

#if HAS_GASMASK
  r1.xy = float2(-0.5,-0.5) + r2.xy;
  r1.z = 1;
  r0.w = dot(r1.xyz, r1.xyz);
  r0.w = rsqrt(r0.w);
  r1.xyz = r1.xyz * r0.www;
  r0.w = dot(-pp_gasmask.xyz, r1.xyz);
  r0.w = r0.w * 0.5 + 0.5;
  r0.w = r0.w * r0.w;
  r0.w = max(0.100000001, r0.w);
  r1.xyz = pp_gasmask_c.xyz * r0.www;
  r0.w = r2.z * r2.z;
  r0.xyz = r0.www * r1.xyz + r0.xyz;

#endif

#if USE_VANILLA
  r1.xyz = t_grade.Sample(s_clamp_bi_s, r0.zyx).xyz;
  r0.xyz = saturate(r0.xyz);
  r0.xyz = r1.zyx * float3(2,2,2) + r0.xyz;
  r0.xyz = float3(-1,-1,-1) + r0.xyz;

  r0.w = dot(r0.xyz, float3(0.298999995, 0.587000012, 0.114)); // BT.601 coefficients, bad
  o0.w = sqrt(r0.w);
#else
  #if 1
  r1.xyz = t_grade.Sample(s_clamp_bi_s, r0.zyx).xyz;
  r0.xyz = r1.zyx * float3(2, 2, 2) + r0.xyz;
  r0.xyz = float3(-1, -1, -1) + r0.xyz;
  r0.xyz = max(0, r0.xyz);
  #else
  r1.xyz = SampleLUT(t_grade, s_clamp_bi_s, r0.xyz, 16u, true);
  r0.xyz = r1.zyx * float3(2, 2, 2) + r0.xyz;
  r0.xyz = float3(-1, -1, -1) + r0.xyz;
  //r0.xyz = AddColorOffsetDampened(r0.xyz, 0.05f);
  //r0.xyz = max(0, r0.xyz);
  // r0.xyz = EmulateShadowClip(r0.xyz);
  #endif

  r0.xyz /= scale;

  r0.w = GetLuminance(r0.xyz);
  o0.w = linear_to_gamma(r0.w).x;
#endif
  o0.xyz = r0.xyz;
  return;
}
