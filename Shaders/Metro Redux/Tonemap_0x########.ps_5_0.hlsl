// Metro Redux tonemapper.
//
// The current dump permutations establish the feature axes:
//   0x0F9D2B4D: no gas mask, no light shafts
//   0x28677B5F: light shafts
//   0x3076B7D6: gas mask
//   0xFD8F63E3: gas mask + light shafts
// The refreshed vanilla output uses Uncharted 2/Hable plus the 3D grading LUT.
// The default custom path keeps Luma's intentional pre-tonemap gas-mask lighting
// and pre-tonemap additive bloom behavior. Define USE_VANILLA=1 for comparison.

#if _F398A1ED || _FA7FE535 || _3076B7D6 || _FD8F63E3
#define HAS_GASMASK 1
#else
#define HAS_GASMASK 0
#endif

#if _A453ADB1 || _FA7FE535 || _28677B5F || _FD8F63E3
#define HAS_LIGHT_SHAFTS 1
#else
#define HAS_LIGHT_SHAFTS 0
#endif

#if _5C867D7E || _F398A1ED || _A453ADB1 || _FA7FE535 || _0F9D2B4D || _28677B5F || _3076B7D6 || _FD8F63E3
#define HAS_DOF 1
#else
#define HAS_DOF 0
#endif

#define LUT_3D 1

#include "./Common.hlsl"
#include "./renodx/random.hlsl"
#include "../Includes/Tonemap.hlsl"
#include "../Includes/Reinhard.hlsl"
#include "../Includes/ColorGradingLUT.hlsl"

#ifndef USE_VANILLA
#define USE_VANILLA 0
#endif

#ifndef METRO_REDUX_DEBUG_RAW_SCENE
#define METRO_REDUX_DEBUG_RAW_SCENE 0
#endif

// Luma's extended Hable curve for the HDR path.
float3 ExtendedUncharted2(float3 untonemapped)
{
   const float A = 0.150000006f, B = 0.5f, C = 0.100000001f, D = 0.200000003f, E = 0.0199999996f, F = 0.300000012f;
   const float W = 1.0f;
   float3 originalColor = 0.f;
   return Uncharted2::Tonemap_Uncharted2_Extended(
       untonemapped, false, originalColor, 1, 0.18f, 0, W, A, B, C, D, E, F);
}

// Exact Hable arithmetic recovered in the updated vanilla permutations.
float3 VanillaUncharted2(float3 color)
{
   float3 numerator = color * (color * 0.150000006f + 0.0500000007f) + 0.00400000019f;
   float3 denominator = color * (color * 0.150000006f + 0.5f) + 0.0599999987f;
   return (numerator / denominator - 0.0666666701f) * 4.53191471f;
}

// Vanilla applies bloom after its Hable curve; the custom HDR path intentionally
// adds bloom before its extended curve instead.
float3 VanillaUncharted2WithBloom(float3 color, float3 bloom)
{
   return VanillaUncharted2(color) + bloom * 0.125f;
}

float ComputeReinhardSmoothClampScale(float3 untonemapped, float rolloff_start = 0.375f, float output_max = 1.f, float white_clip = 100.f)
{
   float peak = max(untonemapped.r, max(untonemapped.g, untonemapped.b));
   float mapped_peak = Reinhard::ReinhardRange(peak, rolloff_start, white_clip, output_max, true).x;
   return safeDivision(mapped_peak, peak, 1);
}

// Modernizes old-school grading without lifting the black floor.
float3 AddColorOffsetDampened(float3 Color, float3 Offset, float Dampening = 1.f, bool AllowShadowCrush = false)
{
   float3 OffsetAlpha = saturate(Color / abs(Offset * 2.f));
   OffsetAlpha *= float3((Offset.x > 0.f && !AllowShadowCrush) ? OffsetAlpha.x : 1.f,
                         (Offset.y > 0.f && !AllowShadowCrush) ? OffsetAlpha.y : 1.f,
                         (Offset.z > 0.f && !AllowShadowCrush) ? OffsetAlpha.z : 1.f);
   OffsetAlpha = lerp(OffsetAlpha, 1.f, sqrt(saturate((abs(Offset) - MidGray) / (1.f - MidGray))));
   OffsetAlpha = lerp(1.f, OffsetAlpha, sqrt(Dampening));
   return Color + Offset * OffsetAlpha;
}

float3 ApplyMetroFilmGrain(float3 color, float2 uv, float seed, float strength)
{
   const float random_number = renodx::random::Generate(uv + seed);
   const float3 signs = sign(color);
   color = abs(color);
   const float adjusted_luminance = GetLuminance(color);
   const float density = adjusted_luminance * 3.f;
   const float graininess = density <= 0.f
                                ? 0.f
                                : pow(10.f, 0.880f - (0.736f * density) - (0.003f * pow(density, 7.6f)));
   const float random_factor = (random_number * 2.f) - 1.f;
   const float y_change = random_factor * graininess * strength * 1.667f;
   return signs * color * (1.f + y_change);
}

float reinhardPiecewiseSmooth(float x, float peak, float anchor)
{
   if (x <= anchor)
      return x;

   float range = peak - anchor;
   float t = (x - anchor) / range;
   float s = exp(-0.01 / (t * t));
   return anchor + range * t / (1.0 + t * s);
}

float3 reinhardPiecewiseSmooth(float3 color, float peak, float anchor)
{
   return float3(
       reinhardPiecewiseSmooth(color.x, peak, anchor),
       reinhardPiecewiseSmooth(color.y, peak, anchor),
       reinhardPiecewiseSmooth(color.z, peak, anchor));
}

static const float3x3 prismInset1 = float3x3(
    0.726346493f, 0.166654825f, 0.106998682f,
    0.134236977f, 0.700138867f, 0.165624142f,
    0.072639547f, 0.060120508f, 0.867239952f);

static const float3x3 prismOutset1 = float3x3(
    1.450212359f, -0.335329264f, -0.114883065f,
    -0.253468722f, 1.510707617f, -0.257238865f,
    -0.103896290f, -0.076639965f, 1.180536270f);

static const float3x3 prismInset2 = float3x3(
    0.737337414f, 0.150990499f, 0.111672185f,
    0.310830779f, 0.413497919f, 0.275671278f,
    0.129649707f, 0.116560894f, 0.753789354f);

static const float3x3 prismOutset2 = float3x3(
    1.604745378f, -0.578613002f, -0.026132549f,
    -1.139794955f, 3.107331527f, -0.967536430f,
    -0.099761623f, -0.380976904f, 1.480738594f);

float3 BT709_To_Custom1(float3 color)
{
   return mul(prismInset1, color);
}

float3 Custom1_To_BT709(float3 color)
{
   return mul(prismOutset1, color);
}

float3 BT709_To_Custom2(float3 color)
{
   return mul(prismInset2, color);
}

float3 Custom2_To_BT709(float3 color)
{
   return mul(prismOutset2, color);
}

float3 ApplyHdrReinhardAndFilmGrain(float3 color, float2 uv)
{
   // The Metro LUT returns sRGB-encoded BT.709. Decode in that basis first,
   // then convert the linear values to Custom1 for Reinhard.
   float3 color_bt709_linear = gamma_sRGB_to_linear(color, GCT_MIRROR);
   // Custom1 is active; Custom2 remains available for comparison, but unused.
   float3 color_custom1 = BT709_To_Custom1(color_bt709_linear);

   float peak = LumaSettings.PeakWhiteNits / sRGB_WhiteLevelNits;
   float diffuse_white = LumaSettings.GamePaperWhiteNits / sRGB_WhiteLevelNits;
   peak = peak / diffuse_white;

   float3 tonemapped_custom1 = reinhardPiecewiseSmooth(color_custom1, peak, 0.22f);
   float3 tonemapped_bt709_linear = Custom1_To_BT709(tonemapped_custom1);
   tonemapped_bt709_linear = ApplyMetroFilmGrain(
       tonemapped_bt709_linear,
       uv,
       LumaSettings.GameSettings.custom_random,
       LumaSettings.GameSettings.custom_film_grain_strength * 0.03f);
   return linear_to_sRGB_gamma(tonemapped_bt709_linear, GCT_MIRROR);
}

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

#define cmp -

float3 SampleLightShafts(float2 originUV, float2 dirUV)
{
   float3 sum = 0.0;
   float4 sample0 = t_image_nomsaa_srcdof.Sample(s_clamp_bi_s, originUV);
   sum += sample0.xyz * (sample0.w > 1024.0 ? 1.0 : 0.0);
   [unroll]
   for (uint i = 1; i < 32; i++)
   {
      const float t = i * (1.0 / 32.0);
      float4 sampleValue = t_image_nomsaa_srcdof.Sample(s_clamp_bi_s, originUV + dirUV * t);
      sum += sampleValue.xyz * (sampleValue.w > 1024.0 ? 1.0 : 0.0);
   }
   return sum * (1.0 / 32.0);
}

float3 SampleLightShaftsLevel(float2 originUV, float2 dirUV)
{
   float3 sum = 0.0;
   float4 sample0 = t_image_nomsaa_srcdof.SampleLevel(s_clamp_bi_s, originUV, 0);
   sum += sample0.xyz * (sample0.w > 1024.0 ? 1.0 : 0.0);
   [unroll]
   for (uint i = 1; i < 32; i++)
   {
      const float t = i * (1.0 / 32.0);
      float4 sampleValue = t_image_nomsaa_srcdof.SampleLevel(s_clamp_bi_s, originUV + dirUV * t, 0);
      sum += sampleValue.xyz * (sampleValue.w > 1024.0 ? 1.0 : 0.0);
   }
   return sum * (1.0 / 32.0);
}

void main(float4 v0: SV_Position0, float3 v1: TEXCOORD0, out float4 o0: SV_Target0)
{
   float4 r0, r1, r2, r3, r4, r5, r6, r7, r8;
   uint4 bitmask, uiDest;
   float4 fDest;
   r0.xy = rtdim.xy * v0.xy;
   float2 coords = r0.xy;
#if METRO_REDUX_DEBUG_RAW_SCENE
   // t_image_nomsaa_srcdof is the composed game scene before this shader's
   // DOF, bloom/lens dirt, gas mask, tonemap, and grading passes.
   const float3 raw_scene_color = t_image_nomsaa_srcdof.Sample(s_clamp_bi_s, coords).rgb;
   o0 = float4(raw_scene_color, 1.0);
   return;
#endif
   r1.xy = (int2)v0.xy;
   r1.zw = float2(0, 0);
   r0.z = t_position.Load(r1.xyw).z;
   r1.xy = t_dudv.Load(r1.xyz).xy;
#if HAS_GASMASK
   r2.xyz = t_gm_base.Sample(s_clamp_tri_s, r0.xy).xyw;
   r1.zw = float2(5, 3) * r0.xy;
   r3.xyz = t_gm_dist.Sample(s_wrap_tri_s, r1.zw).xyw;
   r4.xyz = float3(1.89999998, -0.501960814, -0.501960814) + r2.zxy;
   r0.w = saturate(10 * (r4.x - r2.x - r2.y));
   r1.zw = 0.1 * r4.yz;
   r2.w = (-3 + pp_gasmask.w) * r2.z;
   r3.z = r3.z * 2 + pp_gasmask.w;
   r3.xyz = float3(-0.501960814, -0.501960814, -1) + r3.xyz;
   r2.w = r3.z * r2.w;
   r3.z = r2.z * 2 - 1;
   r2.w = saturate(r3.z * r2.w);
   r3.xy = r3.xy - r4.yz * 0.1;
   r1.zw = (r2.w * r3.xy + r1.zw) * r0.ww;
   r1.xy = r1.xy * 0.0350000001 + r1.zw;
#else
   r1.xy = r1.xy * 0.0350000001;
#endif
   r3.xyzw = rtdim.xyxy * float4(-0.400000006, 0.800000012, 0.800000012, 0.400000006) + r0.xyxy;
   r4.xyzw = rtdim.xyxy * float4(0.400000006, -0.800000012, -0.800000012, -0.400000006) + r0.xyxy;
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
   r0.xy = 0.25 * r0.xy;
   r1.z = sqrt(dot(r0.xy, r0.xy)) + 9.99999975e-06;
   r1.z = min(1, 0.00282842712 / r1.z);
   r3.yz = r1.zz * r0.xy;
   r0.x = rtdim.x / rtdim.y;
   r3.x = r3.y * r0.x;
   r1.xy = v0.xy * rtdim.xy + r1.xy;
   r4.xyzw = 2 * t_bloom.Sample(s_clamp_bi_s, r1.xy);
   r5.xyzw = t_bloom_n.Sample(s_clamp_bi_s, r1.xy);
   r6.xyzw = 2 * r5.xyzw;
#if HAS_DOF
   r3.yw = saturate(r0.zz * pp_dof.wz - pp_dof.xy);
#else
   r3.yw = float2(0, 0);
#endif
   r0.y = 1 - r3.y - r3.w;
   r0.z = r0.z > 1024 ? 1 : 0;
   r0.y = r0.z ? -r4.w : r0.y;
   r0.z = max(9.99999975e-05, abs(r4.w));
   r4.xyz /= r0.zzz;
   r0.y = saturate(-r0.y);
   r0.z = max(9.99999975e-05, r6.w);
   r5.xyz /= r0.zzz;
   r0.z = saturate(3 * r5.w);
   r1.w = max(r0.y, r0.z);
   r6.xyzw = float4(-4, -4, -3, -3) * r3.xzxz;
   r7.xyzw = rtdim.xyxy * float4(-1.3125, 0.1875, -0.9375, -0.9375) - r6.xyzw;
   r6.xyzw = r1.wwww * r7.xyzw + r6.xyzw + r1.xyxy;
   r7.xyzw = t_image_nomsaa_srcdof.Sample(s_clamp_bi_s, r6.xy);
   r2.w = 1 / (max(0, r0.w - r7.w) * 3 + 1);
   r6.xyzw = t_image_nomsaa_srcdof.Sample(s_clamp_bi_s, r6.zw);
   r3.y = 1 / (max(0, r0.w - r6.w) * 3 + 1);
   r6.xyz = r6.xyz * r3.yyy + r7.xyz * r2.www;
   r2.w = r3.y + r2.w;
   r7.xyzw = float4(-2, -2, 3, 3) * r3.xzxz;
   r3.yw = rtdim.xy * float2(-0.5625, 0.9375) - r7.xy;
   r3.yw = r1.ww * r3.yw + r7.xy + r1.xy;
   r8.xyzw = t_image_nomsaa_srcdof.Sample(s_clamp_bi_s, r3.yw);
   r3.y = 1 / (max(0, r0.w - r8.w) * 3 + 1);
   r6.xyz = r8.xyz * r3.yyy + r6.xyz;
   r2.w += r3.y;
   r3.yw = rtdim.xy * float2(-0.1875, -0.5625) + r3.xz;
   r3.yw = r1.ww * r3.yw - r3.xz + r1.xy;
   r8.xyzw = t_image_nomsaa_srcdof.Sample(s_clamp_bi_s, r3.yw);
   r3.y = 1 / (max(0, r0.w - r8.w) * 3 + 1);
   r6.xyz = r8.xyz * r3.yyy + r6.xyz;
   r2.w += r3.y;
   r3.yw = rtdim.xy * r1.ww * float2(0.1875, 0.5625) + r1.xy;
   r8.xyzw = t_image_nomsaa_srcdof.Sample(s_clamp_bi_s, r3.yw);
   r3.y = 1 / (max(0, r0.w - r8.w) * 3 + 1);
   r6.xyz = r8.xyz * r3.yyy + r6.xyz;
   r2.w += r3.y;
   r3.yw = rtdim.xy * float2(0.5625, -1.3125) + r3.xz;
   r3.yw = r1.ww * r3.yw + r3.xz + r1.xy;
   r8.xyzw = t_image_nomsaa_srcdof.Sample(s_clamp_bi_s, r3.yw);
   r3.y = 1 / (max(0, r0.w - r8.w) * 3 + 1);
   r6.xyz = r8.xyz * r3.yyy + r6.xyz;
   r2.w += r3.y;
   r3.xy = 2 * r3.xz;
   r3.zw = rtdim.xy * float2(0.9375, -0.1875) - r3.xy;
   r3.xy = r1.ww * r3.zw + r3.xy + r1.xy;
   r3.xyzw = t_image_nomsaa_srcdof.Sample(s_clamp_bi_s, r3.xy);
   r3.w = 1 / (max(0, r0.w - r3.w) * 3 + 1);
   r3.xyz = r3.xyz * r3.www + r6.xyz;
   r2.w += r3.w;
   r6.xy = rtdim.xy * 1.3125 + -r7.zw;
   r6.xy = r1.ww * r6.xy + r7.zw + r1.xy;
   r6.xyzw = t_image_nomsaa_srcdof.Sample(s_clamp_bi_s, r6.xy);
   r0.w = 1 / (max(0, r0.w - r6.w) * 3 + 1);
   r3.xyz = (r6.xyz * r0.www + r3.xyz) / (r2.w + r0.w);
   r4.xyz = r4.xyz - r3.xyz;
   r3.xyz = r0.yyy * r4.xyz + r3.xyz;
   r4.xyz = r5.xyz - r3.xyz;
   r0.yzw = r0.zzz * r4.xyz + r3.xyz;
   r1.w = -0.5 + r1.y;
   r1.z = r1.w * r0.x + 0.5;
   r3.xyzw = t_lensdirt.Sample(s_clamp_tri_s, r1.xz);
   r1.xyz = t_bloom_b.Sample(s_clamp_bi_s, r1.xy).xyz;
#if !USE_VANILLA
   if (!ShouldForceSDR(coords))
   {
      r1.xyz = FakeHDR(r1.xyz, 0.04f, 0.22f);
      r3.xyz *= LumaSettings.GameSettings.custom_lens_dirt;
      r1.xyz *= LumaSettings.GameSettings.custom_bloom;
   }
#endif
   r3.xyz = r3.xyz * r3.www * 5;
   r0.x = pp_gasmask.w * 5 + 1;
   r3.xyz = r3.xyz * r0.xxx + 1;
   r4.xyz = r3.xyz * r1.xyz;
   r0.xyz = r1.xyz * r3.xyz + r0.yzw;

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
      shafts_sum = (shafts_sum + SampleLightShaftsLevel(near_origin, shafts_dir)) * 0.5;
   }
   r0.xyz += shafts_sum * lshafts_color.xyz;
#endif

#if HAS_GASMASK && !USE_VANILLA
   // The Luma custom path intentionally applies gas-mask illumination in the
   // scene-linear composite before the bloom change and tone mapping.
   r1.xy = r2.xy - 0.5;
   r1.z = 1;
   r0.w = rsqrt(dot(r1.xyz, r1.xyz));
   r1.xyz *= r0.www;
   r0.w = dot(-pp_gasmask.xyz, r1.xyz) * 0.5 + 0.5;
   r0.w = max(0.1, r0.w * r0.w);
   r0.xyz += r2.z * r2.z * pp_gasmask_c.xyz * r0.www;
#endif

   // The refreshed vanilla permutations clamp the composite before their Hable
   // curve. Apply the same input constraint to both output modes.
   r0.xyz = max(0.0, r0.xyz);
   float3 untonemapped = r0.xyz;
   float3 tonemapped_bt709;
   float scale = 1;

#if USE_VANILLA
   // Dump-oriented vanilla ordering: Hable, post-curve bloom, gas-mask lighting,
   // then the vanilla 3D LUT and alpha/luminance output.
   tonemapped_bt709 = VanillaUncharted2WithBloom(untonemapped, r4.xyz);
#if HAS_GASMASK
   r1.xy = r2.xy - 0.5;
   r1.z = 1;
   r0.w = rsqrt(dot(r1.xyz, r1.xyz));
   r1.xyz *= r0.www;
   r0.w = dot(-pp_gasmask.xyz, r1.xyz) * 0.5 + 0.5;
   r0.w = max(0.1, r0.w * r0.w);
   tonemapped_bt709 += r2.z * r2.z * pp_gasmask_c.xyz * r0.www;
#endif
   r0.xyz = tonemapped_bt709;
   r1.xyz = t_grade.Sample(s_clamp_bi_s, r0.zyx).xyz;
   r0.xyz = saturate(r0.xyz);
   r0.xyz = r1.zyx * 2 + r0.xyz - 1;
   r0.w = dot(r0.xyz, float3(0.298999995, 0.587000012, 0.114));
   o0 = float4(r0.xyz, sqrt(r0.w));
   return;
#else
   if (ShouldForceSDR(coords))
   {
      tonemapped_bt709 = VanillaUncharted2WithBloom(untonemapped, r4.xyz);
   }
   else
   {
      float3 additive_bloom = r4.xyz * 0.125f;
      untonemapped += additive_bloom;
      untonemapped *= LumaSettings.GameSettings.exposure;
      float3 untonemapped_custom1 = BT709_To_Custom1(untonemapped);
      float3 tonemapped_custom1 = ExtendedUncharted2(untonemapped_custom1);
      tonemapped_custom1 = lerp(untonemapped_custom1, tonemapped_custom1, 1.f);

      // Gamma conversion belongs in BT.709. C-infinity itself remains in
      // Custom1, using the equivalent decoded mapped-midgray anchor.
      tonemapped_bt709 = Custom1_To_BT709(tonemapped_custom1);
      tonemapped_bt709 = gamma_sRGB_to_linear(tonemapped_bt709, GCT_MIRROR);
      float3 grading_custom1 = BT709_To_Custom1(tonemapped_bt709);
      const float3 matched_midgray_bt709 = gamma_sRGB_to_linear(
          Custom1_To_BT709(0.22f.xxx), GCT_MIRROR);
      const float3 matched_midgray_custom1 = BT709_To_Custom1(matched_midgray_bt709);
      grading_custom1 = ApplyCInfinityGrading(
          grading_custom1,
          matched_midgray_custom1,
          matched_midgray_custom1,
          LumaSettings.GameSettings.contrast,
          LumaSettings.GameSettings.flare,
          LumaSettings.GameSettings.highlight_contrast,
          LumaSettings.GameSettings.shadow_contrast,
          LumaSettings.GameSettings.highlights,
          LumaSettings.GameSettings.shadows);
      tonemapped_bt709 = linear_to_sRGB_gamma(
          Custom1_To_BT709(grading_custom1), GCT_MIRROR);
      tonemapped_bt709 = ApplyMetroChromaControls(
          tonemapped_bt709,
          LumaSettings.GameSettings.saturation,
          LumaSettings.GameSettings.blowout,
          -(LumaSettings.GameSettings.highlight_saturation - 1.f));
   }

   // Gas-mask lighting was intentionally moved into the scene-linear input above
   // for the custom path. Do not add it again after tone mapping.
   r0.xyz = tonemapped_bt709;

   if (ShouldForceSDR(coords))
   {
      r1.xyz = t_grade.Sample(s_clamp_bi_s, r0.zyx).xyz;
      r0.xyz = saturate(r0.xyz);
      r0.xyz = r1.zyx * 2 + r0.xyz - 1;
      r0.w = dot(r0.xyz, float3(0.298999995, 0.587000012, 0.114));
      o0.w = sqrt(r0.w);
   }
   else
   {
      scale = ComputeReinhardSmoothClampScale(r0.xyz);
      r0.xyz = ApplyMetroLUTWithFadeToWhite(t_grade, s_clamp_bi_s, r0.xyz, scale);
      r0.w = GetLuminance(r0.xyz);
      o0.w = linear_to_gamma(r0.w).x;
      r0.xyz = ApplyHdrReinhardAndFilmGrain(r0.xyz, coords);
      o0.w = saturate(o0.w);
   }
   o0.xyz = r0.xyz;
   return;
#endif
}
