// ---- Created with 3Dmigoto v1.3.16 on Wed Aug 05 17:40:18 2026

cbuffer ExposurePS : register(b0)
{
  float4 g_exposure : packoffset(c0);
  float4 g_alt_exposure : packoffset(c1);
}

cbuffer FinalCompositePS : register(b1)
{
  float4 tone_curve_constants : packoffset(c0);
  float4 player_window_constants : packoffset(c1);
  float4 depth_constants2 : packoffset(c2);
  float4x3 color_matrix : packoffset(c3);
  float4 gamma : packoffset(c6);
  float4 noise_params : packoffset(c7);
}

SamplerState GlobalSampler_surface_sampler_s : register(s0);
SamplerState GlobalSampler_bloom_sampler_s : register(s2);
SamplerState GlobalSampler_noise_sampler_s : register(s7);
Texture2D<float4> GlobalTexture_surface_sampler : register(t0);
Texture2D<float4> GlobalTexture_bloom_sampler : register(t2);
Texture2D<float4> GlobalTexture_noise_sampler : register(t7);


// 3Dmigoto declarations
#define cmp -
#include "./hr_t.hlsl"
//TODO: common for variants

//https://github.com/halohlsl/HaloReach-Shader-Source/blob/pc/source/omaha/rasterizer/hlsl/postprocess/final_composite_base.fx
void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2;
  uint4 bitmask, uiDest;
  float4 fDest;

  // Noise calculate
  r0.x = GlobalTexture_noise_sampler.Sample(GlobalSampler_noise_sampler_s, FilmGrainUV(v2.zw)).z;
  r0.xy = r0.xx * noise_params.xy + noise_params.zw;
  r0.y *= 0.5 * GS.FilmGrain;

  // r1.xyz = GlobalTexture_bloom_sampler.Sample(GlobalSampler_bloom_sampler_s, v2.xy).xyz * GS.Bloom;
  {
    float3 C = GlobalTexture_bloom_sampler.Sample(GlobalSampler_bloom_sampler_s, v2.xy).xyz;
    float3 N = GlobalTexture_bloom_sampler.Sample(GlobalSampler_bloom_sampler_s, v2.xy, int2(0,-1)).xyz;
    float3 S = GlobalTexture_bloom_sampler.Sample(GlobalSampler_bloom_sampler_s, v2.xy, int2(0,1)).xyz;
    float3 W = GlobalTexture_bloom_sampler.Sample(GlobalSampler_bloom_sampler_s, v2.xy, int2(-1,0)).xyz;
    float3 E = GlobalTexture_bloom_sampler.Sample(GlobalSampler_bloom_sampler_s, v2.xy, int2(1, 0)).xyz;
    float3 x = C.xyz;
    x += N.xyz;
    x += S.xyz;
    x += W.xyz;
    x += E.xyz;
    x /= 5;
    r1.xyz = x * (GS.Bloom * BLOOM_MAKEUP);
    r1.xyz = max(0, r1.xyz);
  }
  r1.xyz = max(r1.xyz, 0);
  r1.xyz = float3(8,8,8) * r1.xyz;

  r2.xyz = GlobalTexture_surface_sampler.Sample(GlobalSampler_surface_sampler_s, v1.xy).xyz;
  r1.xyz = r2.xyz * g_exposure.yyy + r1.xyz;
  
  // Gamma Encode
  r1.xyz = max(r1.xyz, 0);
  r1.xyz = pow(r1.xyz, gamma.z); //1/2.0

  // Color Matrix
  r1.w = 1;
  r2.x = /* saturate */(dot(r1.xyzw, color_matrix._m00_m10_m20_m30));
  r2.y = /* saturate */(dot(r1.xyzw, color_matrix._m01_m11_m21_m31));
  r2.z = /* saturate */(dot(r1.xyzw, color_matrix._m02_m12_m22_m32));
  r2.xyz = max(r2.xyz, 0);

  // HDR Tonemap
  r2.xyz = sRGB_Decode(r2.xyz);
  r2.xyz = Rolloff(r2.xyz);
  r2.xyz = sRGB_Encode(r2.xyz);

  // Noise apply
  r0.xyz = r2.xyz * r0.xxx + r0.yyy;
  r0.xyz = max(r0.xyz, 0);

  // Luma for AA
  // o0.w = dot(r0.xyz, float3(0.298999995,0.587000012,0.114)); //BT601
  o0.w = GetLuminance(r0.xyz, CS_BT709);

  o0.xyz = r0.xyz;
  return;
}

// cbuffer cb0_buf : register(b0)
// {
//     float4 cb0_m : packoffset(c0);
// };
// 
// cbuffer cb1_buf : register(b1)
// {
//     uint4 cb1_m0 : packoffset(c0);
//     uint4 cb1_m1 : packoffset(c1);
//     uint4 cb1_m2 : packoffset(c2);
//     float4 cb1_m3 : packoffset(c3);
//     float4 cb1_m4 : packoffset(c4);
//     float4 cb1_m5 : packoffset(c5);
//     float4 cb1_m6 : packoffset(c6);
//     float2 cb1_m7 : packoffset(c7);
//     float2 cb1_m8 : packoffset(c7.z);
// };
// 
// SamplerState s0 : register(s0);
// SamplerState s2 : register(s2);
// SamplerState s7 : register(s7);
// Texture2D<float4> t0 : register(t0);
// Texture2D<float4> t2 : register(t2);
// Texture2D<float4> t7 : register(t7);
// 
// static float2 TEXCOORD;
// static float4 TEXCOORD1;
// static float4 SV_Target;
// 
// struct SPIRV_Cross_Input
// {
//     float4 SV_Position : SV_Position;
//     float2 TEXCOORD : TEXCOORD0;
//     float4 TEXCOORD1 : TEXCOORD1;
// };
// 
// struct SPIRV_Cross_Output
// {
//     float4 SV_Target : SV_Target0;
// };
// 
// float dp4_f32(float4 a, float4 b)
// {
//     precise float _71 = a.x * b.x;
//     return mad(a.w, b.w, mad(a.z, b.z, mad(a.y, b.y, _71)));
// }
// 
// float dp3_f32(float3 a, float3 b)
// {
//     precise float _56 = a.x * b.x;
//     return mad(a.z, b.z, mad(a.y, b.y, _56));
// }
// 
// void frag_main()
// {
//     float4 _94 = t7.Sample(s7, float2(TEXCOORD1.z, TEXCOORD1.w));
//     float _95 = _94.z;
//     float _106 = mad(_95, cb1_m7.x, cb1_m8.x);
//     float _107 = mad(_95, cb1_m7.y, cb1_m8.y);
//     float4 _116 = t2.Sample(s2, float2(TEXCOORD1.x, TEXCOORD1.y));
//     float4 _131 = t0.Sample(s0, float2(TEXCOORD.x, TEXCOORD.y));
//     float4 _156 = float4(exp2(log2((_116.x * 8.0f) + (_131.x * cb0_m.y)) * cb1_m6.z), exp2(cb1_m6.z * log2((_131.y * cb0_m.y) + (_116.y * 8.0f))), exp2(cb1_m6.z * log2((_116.z * 8.0f) + (_131.z * cb0_m.y))), 1.0f);
//     float _170 = mad(_106, clamp(dp4_f32(_156, cb1_m3), 0.0f, 1.0f), _107);
//     float _171 = mad(_106, clamp(dp4_f32(_156, cb1_m4), 0.0f, 1.0f), _107);
//     float _172 = mad(_106, clamp(dp4_f32(_156, cb1_m5), 0.0f, 1.0f), _107);
//     SV_Target.w = dp3_f32(float3(_170, _171, _172), float3(0.2989999949932098388671875f, 0.58700001239776611328125f, 0.114000000059604644775390625f));
//     SV_Target.x = _170;
//     SV_Target.y = _171;
//     SV_Target.z = _172;
// }
// 
// SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
// {
//     TEXCOORD = stage_input.TEXCOORD;
//     TEXCOORD1 = stage_input.TEXCOORD1;
//     frag_main();
//     SPIRV_Cross_Output stage_output;
//     stage_output.SV_Target = SV_Target;
//     return stage_output;
// }

/*
//
// Generated by Microsoft (R) HLSL Shader Compiler 10.1
//
//
// Buffer Definitions: 
//
// cbuffer ExposurePS
// {
//
//   float4 g_exposure;                 // Offset:    0 Size:    16
//   float4 g_alt_exposure;             // Offset:   16 Size:    16 [unused]
//
// }
//
// cbuffer FinalCompositePS
// {
//
//   float4 tone_curve_constants;       // Offset:    0 Size:    16 [unused]
//   float4 player_window_constants;    // Offset:   16 Size:    16 [unused]
//   float4 depth_constants2;           // Offset:   32 Size:    16 [unused]
//   float4x3 color_matrix;             // Offset:   48 Size:    48
//   float4 gamma;                      // Offset:   96 Size:    16
//   float4 noise_params;               // Offset:  112 Size:    16
//
// }
//
//
// Resource Bindings:
//
// Name                                 Type  Format         Dim      HLSL Bind  Count
// ------------------------------ ---------- ------- ----------- -------------- ------
// GlobalSampler_surface_sampler     sampler      NA          NA             s0      1 
// GlobalSampler_bloom_sampler       sampler      NA          NA             s2      1 
// GlobalSampler_noise_sampler       sampler      NA          NA             s7      1 
// GlobalTexture_surface_sampler     texture  float4          2d             t0      1 
// GlobalTexture_bloom_sampler       texture  float4          2d             t2      1 
// GlobalTexture_noise_sampler       texture  float4          2d             t7      1 
// ExposurePS                        cbuffer      NA          NA            cb0      1 
// FinalCompositePS                  cbuffer      NA          NA            cb1      1 
//
//
//
// Input signature:
//
// Name                 Index   Mask Register SysValue  Format   Used
// -------------------- ----- ------ -------- -------- ------- ------
// SV_Position              0   xyzw        0      POS   float       
// TEXCOORD                 0   xy          1     NONE   float   xy  
// TEXCOORD                 1   xyzw        2     NONE   float   xyzw
//
//
// Output signature:
//
// Name                 Index   Mask Register SysValue  Format   Used
// -------------------- ----- ------ -------- -------- ------- ------
// SV_Target                0   xyzw        0   TARGET   float   xyzw
//
      0x00000000: ps_5_0
      0x00000008: dcl_globalFlags refactoringAllowed
      0x0000000C: dcl_constantbuffer CB0[1], immediateIndexed
      0x0000001C: dcl_constantbuffer CB1[8], immediateIndexed
      0x0000002C: dcl_sampler s0, mode_default
      0x00000038: dcl_sampler s2, mode_default
      0x00000044: dcl_sampler s7, mode_default
      0x00000050: dcl_resource_texture2d (float,float,float,float) t0
      0x00000060: dcl_resource_texture2d (float,float,float,float) t2
      0x00000070: dcl_resource_texture2d (float,float,float,float) t7
      0x00000080: dcl_input_ps linear v1.xy
      0x0000008C: dcl_input_ps linear v2.xyzw
      0x00000098: dcl_output o0.xyzw
      0x000000A4: dcl_temps 3
   0  0x000000AC: sample_indexable(texture2d)(float,float,float,float) r0.x, v2.zwzz, t7.zxyw, s7
   1  0x000000D8: mad r0.xy, r0.xxxx, cb1[7].xyxx, cb1[7].zwzz
   2  0x00000104: sample_indexable(texture2d)(float,float,float,float) r1.xyz, v2.xyxx, t2.xyzw, s2
   3  0x00000130: mul r1.xyz, r1.xyzx, l(8.000000, 8.000000, 8.000000, 0.000000)
   4  0x00000158: sample_indexable(texture2d)(float,float,float,float) r2.xyz, v1.xyxx, t0.xyzw, s0
   5  0x00000184: mad r1.xyz, r2.xyzx, cb0[0].yyyy, r1.xyzx
   6  0x000001AC: log r1.xyz, r1.xyzx
   7  0x000001C0: mul r1.xyz, r1.xyzx, cb1[6].zzzz
   8  0x000001E0: exp r1.xyz, r1.xyzx
   9  0x000001F4: mov r1.w, l(1.000000)
  10  0x00000208: dp4_sat r2.x, r1.xyzw, cb1[3].xyzw
  11  0x00000228: dp4_sat r2.y, r1.xyzw, cb1[4].xyzw
  12  0x00000248: dp4_sat r2.z, r1.xyzw, cb1[5].xyzw
  13  0x00000268: mad r0.xyz, r2.xyzx, r0.xxxx, r0.yyyy
  14  0x0000028C: dp3 o0.w, r0.xyzx, l(0.299000, 0.587000, 0.114000, 0.000000)
  15  0x000002B4: mov o0.xyz, r0.xyzx
  16  0x000002C8: ret 
// Approximately 17 instruction slots used

*/