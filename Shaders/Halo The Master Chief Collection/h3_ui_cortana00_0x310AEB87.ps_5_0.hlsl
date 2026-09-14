cbuffer cb0_buf : register(b0)
{
    uint4 cb0_m0 : packoffset(c0);
    uint4 cb0_m1 : packoffset(c1);
    uint4 cb0_m2 : packoffset(c2);
    uint4 cb0_m3 : packoffset(c3);
    uint4 cb0_m4 : packoffset(c4);
    uint4 cb0_m5 : packoffset(c5);
    uint4 cb0_m6 : packoffset(c6);
    uint4 cb0_m7 : packoffset(c7);
    uint4 cb0_m8 : packoffset(c8);
    uint4 cb0_m9 : packoffset(c9);
    uint4 cb0_m10 : packoffset(c10);
    uint4 cb0_m11 : packoffset(c11);
    uint4 cb0_m12 : packoffset(c12);
    uint4 cb0_m13 : packoffset(c13);
};

cbuffer cb1_buf : register(b1)
{
    float3 cb1_m0 : packoffset(c0);
    uint cb1_m1 : packoffset(c0.w);
    uint4 cb1_m2 : packoffset(c1);
    uint4 cb1_m3 : packoffset(c2);
    float4 cb1_m4 : packoffset(c3);
    float4 cb1_m5 : packoffset(c4);
    float4 cb1_m6 : packoffset(c5);
    float4 cb1_m7 : packoffset(c6);
    float4 cb1_m8 : packoffset(c7);
    float4 cb1_m9 : packoffset(c8);
    float4 cb1_m10 : packoffset(c9);
    float4 cb1_m11 : packoffset(c10);
    float4 cb1_m12 : packoffset(c11);
    uint4 cb1_m13 : packoffset(c12);
    uint4 cb1_m14 : packoffset(c13);
    uint4 cb1_m15 : packoffset(c14);
    float4 cb1_m16 : packoffset(c15);
};

cbuffer cb2_buf : register(b2)
{
    uint4 cb2_m0 : packoffset(c0);
    uint4 cb2_m1 : packoffset(c1);
    float4 cb2_m2 : packoffset(c2);
    float4 cb2_m3 : packoffset(c3);
    float4 cb2_m4 : packoffset(c4);
};

SamplerState s0 : register(s0);
SamplerState s1 : register(s1);
SamplerState s2 : register(s2);
Texture2D<float4> t0 : register(t0);
Texture2D<float4> t1 : register(t1);
Texture2D<float4> t2 : register(t2);

static float2 TEXCOORD;
static float2 TEXCOORD2;
static float4 TEXCOORD1;
static float TEXCOORD3;
static float4 SV_Target;

struct SPIRV_Cross_Input
{
    float4 SV_POSITION : SV_Position0;
    float2 TEXCOORD : TEXCOORD0;
    float2 TEXCOORD2 : TEXCOORD1;
    float4 TEXCOORD1 : TEXCOORD2;
    float TEXCOORD3 : TEXCOORD3;
};

struct SPIRV_Cross_Output
{
    float4 SV_Target : SV_Target0;
};

float dp4_f32(float4 a, float4 b)
{
    precise float _93 = a.x * b.x;
    return mad(a.w, b.w, mad(a.z, b.z, mad(a.y, b.y, _93)));
}

float dp2_f32(float2 a, float2 b)
{
    precise float _81 = a.x * b.x;
    return mad(a.y, b.y, _81);
}

#include "./Includes/Common.hlsl"

void frag_main()
{
    float _110 = cb1_m9.z * 0.800000011920928955078125f;
    float _111 = cb1_m9.z * 0.5f;
    float _117 = TEXCOORD.x * TEXCOORD3;
    float _118 = TEXCOORD.y * TEXCOORD3;
    float _119 = mad(_117, 2.0f, -1.0f);
    float _120 = mad(_118, 2.0f, -1.0f);
    float4 _134 = t1.Sample(s1, float2(mad(_119 * _110, 0.5f, 0.5f), mad(_110 * _120, 0.5f, 0.5f)));
    float4 _141 = t1.Sample(s1, float2(mad(_119 * _111, 0.5f, 0.5f), mad(_111 * _120, 0.5f, 0.5f)));
    float _154 = dp4_f32(float4(_134.x + _141.x, _134.y + _141.y, _134.z + _141.z, _134.w + _141.w), cb1_m7);
    float2 _167 = float2(_117, _118);
    float4 _169 = t1.Sample(s1, _167);
    float _170 = _169.x;
    float4 _177 = t0.Sample(s0, _167);
      _177.xyz = sRGB_Encode(_177.xyz);
    float _190 = mad(cb1_m8.x * cb1_m9.x, _154, _170);
    float _191 = mad(_154, cb1_m9.x * cb1_m8.y, _169.y);
    float _192 = mad(_154, cb1_m8.z * cb1_m9.x, _169.z);
    float _193 = mad(_154, cb1_m8.w * cb1_m9.x, _169.w);
    float _198 = min(dp4_f32(float4(_190, _191, _192, _193), cb1_m4), 1.0f);
    float _214 = exp2(log2(abs((_198 - cb1_m6.x) / max(1.0f - cb1_m6.x, 9.9999999747524270787835121154785e-07f))) * cb1_m6.y) * float(_198 >= cb1_m6.x);
    float _221 = mad(_214, cb1_m5.x, _190);
    float _222 = mad(_214, cb1_m5.y, _191);
    float _223 = mad(_214, cb1_m5.z, _192);
    float _224 = mad(_214, cb1_m5.w, _193);
    float _229 = min(dp4_f32(float4(_221, _222, _223, _224), cb1_m10), 1.0f);
    bool _254 = cb0_m13.y != 0u;
    float2 _265 = float2(TEXCOORD1.x - 0.5f, TEXCOORD1.y - 0.5f);
    float _275 = clamp((sqrt(dp2_f32(_265, _265)) - cb1_m16.x) / (cb1_m16.y - cb1_m16.x), 0.0f, 1.0f);
    float _278 = (_275 * _275) + (_170 * 0.5f);
    float _279 = _278 * 0.4000000059604644775390625f;
    float _280 = _278 * 0.60000002384185791015625f;
    float4 _289 = t2.Sample(s2, float2(TEXCOORD2.x, TEXCOORD2.y));
    float4 _306 = float4(_177.x * cb1_m0.x, _177.y * cb1_m0.y, _177.z * cb1_m0.z, 1.0f);
    float _317 = mad(1.0f - ((_289.x * _279) + _280), dp4_f32(_306, cb2_m2), _254 ? ((_229 * cb1_m12.x) * cb1_m11.x) : _221);
    float _318 = mad(1.0f - ((_289.y * _279) + _280), dp4_f32(_306, cb2_m3), _254 ? (cb1_m11.y * (_229 * cb1_m12.y)) : _222);
    float _319 = mad(1.0f - ((_289.z * _279) + _280), dp4_f32(_306, cb2_m4), _254 ? (cb1_m11.z * (_229 * cb1_m12.z)) : _223);
    SV_Target.w = mad(1.0f - ((_289.w * _279) + _280), 1.0f, _254 ? (cb1_m11.w * (_229 * cb1_m12.w)) : _224);
      SV_Target.w = saturate(SV_Target.w);
    SV_Target.x = (_317 <= 0.040449999272823333740234375f) ? (_317 * 0.077399380505084991455078125f) : exp2(log2((_317 + 0.054999999701976776123046875f) * 0.947867333889007568359375f) * 2.400000095367431640625f);
    SV_Target.y = (_318 <= 0.040449999272823333740234375f) ? (_318 * 0.077399380505084991455078125f) : exp2(log2((_318 + 0.054999999701976776123046875f) * 0.947867333889007568359375f) * 2.400000095367431640625f);
    SV_Target.z = (_319 <= 0.040449999272823333740234375f) ? (_319 * 0.077399380505084991455078125f) : exp2(log2((_319 + 0.054999999701976776123046875f) * 0.947867333889007568359375f) * 2.400000095367431640625f);
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    TEXCOORD = stage_input.TEXCOORD;
    TEXCOORD2 = stage_input.TEXCOORD2;
    TEXCOORD1 = stage_input.TEXCOORD1;
    TEXCOORD3 = stage_input.TEXCOORD3;
    frag_main();
    SPIRV_Cross_Output stage_output;
    stage_output.SV_Target = SV_Target;
    return stage_output;
}




// // ---- Created with 3Dmigoto v1.3.16 on Wed Aug 05 15:06:09 2026
// 
// cbuffer CHUDPS : register(b0)
// {
//   float4 chud_color_output_A : packoffset(c0);
//   float4 chud_color_output_B : packoffset(c1);
//   float4 chud_color_output_C : packoffset(c2);
//   float4 chud_color_output_D : packoffset(c3);
//   float4 chud_color_output_E : packoffset(c4);
//   float4 chud_color_output_F : packoffset(c5);
//   float4 chud_scalar_output_ABCD : packoffset(c6);
//   float4 chud_scalar_output_EF : packoffset(c7);
//   float4 chud_texture_bounds : packoffset(c8);
//   float4 chud_savedfilm_chap1 : packoffset(c9);
//   float4 chud_savedfilm_chap2 : packoffset(c10);
//   float4 chud_savedfilm_chap3 : packoffset(c11);
//   float4 chud_savedfilm_data : packoffset(c12);
//   bool chud_cortana_pixel : packoffset(c13);
//   bool chud_comp_colorize_enabled : packoffset(c13.y);
// }
// 
// cbuffer CHUDCortanaPS : register(b1)
// {
//   float4 cortana_back_colormix_result : packoffset(c0);
//   float4 cortana_back_hsv_result : packoffset(c1);
//   float4 cortana_texcam_colormix_result : packoffset(c2);
//   float4 cortana_comp_solarize_inmix : packoffset(c3);
//   float4 cortana_comp_solarize_outmix : packoffset(c4);
//   float4 cortana_comp_solarize_result : packoffset(c5);
//   float4 cortana_comp_doubling_inmix : packoffset(c6);
//   float4 cortana_comp_doubling_outmix : packoffset(c7);
//   float4 cortana_comp_doubling_result : packoffset(c8);
//   float4 cortana_comp_colorize_inmix : packoffset(c9);
//   float4 cortana_comp_colorize_outmix : packoffset(c10);
//   float4 cortana_comp_colorize_result : packoffset(c11);
//   float4 cortana_texcam_bloom_inmix : packoffset(c12);
//   float4 cortana_texcam_bloom_outmix : packoffset(c13);
//   float4 cortana_texcam_bloom_result : packoffset(c14);
//   float4 cortana_vignette_data : packoffset(c15);
// }
// 
// cbuffer PostProcessPS : register(b2)
// {
//   float4 ps_postprocess_pixel_size : packoffset(c0);
//   float4 ps_postprocess_scale : packoffset(c1);
//   float4x3 ps_postprocess_hue_saturation_matrix : packoffset(c2);
//   float4 ps_postprocess_contrast : packoffset(c5);
// }
// 
// SamplerState LocalSampler_basemap_sampler_s : register(s0);
// SamplerState LocalSampler_cortana_sampler_s : register(s1);
// SamplerState LocalSampler_goo_sampler_s : register(s2);
// Texture2D<float4> LocalTexture_basemap_sampler : register(t0);
// Texture2D<float4> LocalTexture_cortana_sampler : register(t1);
// Texture2D<float4> LocalTexture_goo_sampler : register(t2);
// 
// 
// // 3Dmigoto declarations
// #define cmp -
// #include "./Includes/Common.hlsl"
// 
// void main(
//   float4 v0 : SV_Position0,
//   float2 v1 : TEXCOORD0,
//   float2 w1 : TEXCOORD2,
//   float4 v2 : TEXCOORD1,
//   float v3 : TEXCOORD3,
//   out float4 o0 : SV_Target0)
// {
//   float4 r0,r1,r2,r3;
//   uint4 bitmask, uiDest;
//   float4 fDest;
// 
//   r0.xyzw = float4(0.800000012,0.800000012,0.5,0.5) * cortana_comp_doubling_result.zzzz;
//   r1.xyzw = v3.xxxx * v1.xyxy;
//   r2.xyzw = r1.xyzw * float4(2,2,2,2) + float4(-1,-1,-1,-1);
//   r0.xyzw = r2.xyzw * r0.xyzw;
//   r0.xyzw = r0.xyzw * float4(0.5,0.5,0.5,0.5) + float4(0.5,0.5,0.5,0.5);
//   r2.xyzw = LocalTexture_cortana_sampler.Sample(LocalSampler_cortana_sampler_s, r0.xy).xyzw;
//     r2.w = saturate(r2.w);
//   r0.xyzw = LocalTexture_cortana_sampler.Sample(LocalSampler_cortana_sampler_s, r0.zw).xyzw;
//     r0.w = saturate(r0.w);
//   r0.xyzw = r2.xyzw + r0.xyzw;
//   r0.x = dot(r0.xyzw, cortana_comp_doubling_inmix.xyzw);
//   r2.xyzw = cortana_comp_doubling_result.xxxx * cortana_comp_doubling_outmix.xyzw;
//   r3.xyzw = LocalTexture_cortana_sampler.Sample(LocalSampler_cortana_sampler_s, r1.zw).xyzw;
//     r3.w = saturate(r3.w);
//   r0.yzw = LocalTexture_basemap_sampler.Sample(LocalSampler_basemap_sampler_s, r1.zw).xyz;
//   r0.yzw = max(0, r0.yzw);
//   r0.yzw = sRGB_Encode(r0.yzw);
//   
//   // TODO: something is wrong here, creating black screen...
//   r1.xyz = cortana_back_colormix_result.xyz * r0.yzw;
//   r0.xyzw = r2.xyzw * r0.xxxx + r3.xyzw;
//   r2.x = dot(r0.xyzw, cortana_comp_solarize_inmix.xyzw);
//   r2.x = min(1, r2.x);
//   r2.y = -cortana_comp_solarize_result.x + r2.x;
//   r2.x = cmp(r2.x >= cortana_comp_solarize_result.x);
//   r2.x = r2.x ? 1.000000 : 0;
//   r2.z = 1 + -cortana_comp_solarize_result.x;
//   r2.z = max(9.99999997e-007, r2.z);
//   r2.y = r2.y / r2.z;
//   r2.y = log2(abs(r2.y));
//   r2.y = cortana_comp_solarize_result.y * r2.y;
//   r2.y = exp2(r2.y);
//   r2.x = r2.x * r2.y;
//   r0.xyzw = r2.xxxx * cortana_comp_solarize_outmix.xyzw + r0.xyzw;
//   r2.x = dot(r0.xyzw, cortana_comp_colorize_inmix.xyzw);
//   r2.x = min(1, r2.x);
//   r2.xyzw = cortana_comp_colorize_result.xyzw * r2.xxxx;
//   r2.xyzw = cortana_comp_colorize_outmix.xyzw * r2.xyzw;
//   r0.xyzw = chud_cortana_pixel ? r2.xyzw : r0.xyzw;
//   r2.xy = float2(-0.5,-0.5) + v2.xy;
//   r2.x = dot(r2.xy, r2.xy);
//   r2.x = sqrt(r2.x);
//   r2.x = -cortana_vignette_data.x + r2.x;
//   r2.y = cortana_vignette_data.y + -cortana_vignette_data.x;
//   r2.x = saturate(r2.x / r2.y);
//   r2.x = r2.x * r2.x;
//   r2.x = r3.x * 0.5 + r2.x;
//   r2.xy = float2(0.400000006,0.600000024) * r2.xx;
//   r3.xyzw = LocalTexture_goo_sampler.Sample(LocalSampler_goo_sampler_s, w1.xy).xyzw;
//   r2.xyzw = r2.xxxx * r3.xyzw + r2.yyyy;
//   r2.xyzw = float4(1,1,1,1) + -r2.xyzw;
//   r3.w = 1;
//   r1.w = 1;
//   r3.x = dot(r1.xyzw, ps_postprocess_hue_saturation_matrix._m00_m10_m20_m30);
//   r3.y = dot(r1.xyzw, ps_postprocess_hue_saturation_matrix._m01_m11_m21_m31);
//   r3.z = dot(r1.xyzw, ps_postprocess_hue_saturation_matrix._m02_m12_m22_m32);
//   r0.xyzw = r3.xyzw * r2.xyzw + r0.xyzw;
// 
//   // r1.xyz = float3(0.0549999997,0.0549999997,0.0549999997) + r0.xyz;
//   // r1.xyz = float3(0.947867334,0.947867334,0.947867334) * r1.xyz;
//   // r1.xyz = log2(r1.xyz);
//   // r1.xyz = float3(2.4000001,2.4000001,2.4000001) * r1.xyz;
//   // r1.xyz = exp2(r1.xyz);
//   // r2.xyz = cmp(float3(0.0404499993,0.0404499993,0.0404499993) >= r0.xyz);
//   // r0.xyz = float3(0.0773993805,0.0773993805,0.0773993805) * r0.xyz;
//   // o0.xyz = r2.xyz ? r0.xyz : r1.xyz;
//   r0.xyz = max(0, r0.xyz);
//   r0.xyz = sRGB_Decode(r0.xyz);
//   o0.xyz = r0.xyz;
// 
//   o0.w = r0.w;
//   o0.w = saturate(o0.w);
//   return;
// }