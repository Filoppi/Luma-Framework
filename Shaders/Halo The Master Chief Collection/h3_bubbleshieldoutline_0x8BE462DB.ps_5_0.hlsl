// // ---- Created with 3Dmigoto v1.3.16 on Wed Aug 05 15:06:26 2026
// 
// cbuffer ParametersPS : register(b13)
// {
//   float4 ___albedo_color : packoffset(c0);
//   float4 ___base_map_xform : packoffset(c1);
//   float4 ___detail_map_xform : packoffset(c2);
//   float ___self_illum_intensity : packoffset(c3);
//   float4 ___self_illum_map_xform : packoffset(c4);
//   float4 ___self_illum_color : packoffset(c5);
//   float3 ___edge_fade_center_tint : packoffset(c6);
//   float3 ___edge_fade_edge_tint : packoffset(c7);
//   float ___edge_fade_power : packoffset(c8);
// }
// 
// cbuffer ExposurePS : register(b0)
// {
//   float4 g_exposure : packoffset(c0);
//   float4 g_alt_exposure : packoffset(c1);
// }
// 
// cbuffer MiscPS : register(b1)
// {
//   float2 texture_size : packoffset(c0);
//   float2 texture_size_pad : packoffset(c0.z);
//   float4 dynamic_environment_blend : packoffset(c1);
//   float4 p_render_debug_mode : packoffset(c2);
//   float p_shader_pc_specular_enabled : packoffset(c3);
//   float3 p_shader_pc_specular_enabled_pad : packoffset(c3.y);
//   float p_shader_pc_albedo_lighting : packoffset(c4);
//   float3 p_shader_pc_albedo_lighting_pad : packoffset(c4.y);
//   bool LDR_gamma2 : packoffset(c5);
//   bool HDR_gamma2 : packoffset(c5.y);
//   bool actually_calc_albedo : packoffset(c5.z);
//   bool p_lightmap_compress_constant_using_dxt : packoffset(c5.w);
//   float ps_total_time : packoffset(c6);
//   float3 ps_total_time_pad : packoffset(c6.y);
// }
// 
// SamplerState UserParameterSampler_self_illum_map_s : register(s2);
// Texture2D<float4> UserParameterTexture_self_illum_map : register(t2);
// Texture2D<float4> normal_texture : register(t17);
// 
// 
// // 3Dmigoto declarations
// #define cmp -
#include "./Includes/Common.hlsl"
// 
// void main(
//   float4 v0 : SV_Position0,
//   float v1 : SV_ClipDistance0,
//   float4 v2 : TEXCOORD0,
//   float4 v3 : TEXCOORD3,
//   float4 v4 : TEXCOORD4,
//   float4 v5 : TEXCOORD5,
//   float4 v6 : TEXCOORD6,
//   float4 v7 : TEXCOORD7,
//   float3 v8 : COLOR0,
//   float3 v9 : COLOR1,
//   out float4 o0 : SV_Target0,
//   out float4 o1 : SV_Target1)
// {
//   float4 r0,r1,r2,r3;
//   uint4 bitmask, uiDest;
//   float4 fDest;
// 
//   r0.x = dot(v6.xyz, v6.xyz);
//   r0.x = rsqrt(r0.x);
//   r0.xyz = v6.xyz * r0.xxx;
//   if (LDR_gamma2 == 0) {
//     r1.xyz = v3.xyz;
//   } else {
//     r2.xy = (int2)v0.xy;
//     r2.zw = float2(0,0);
//     r2.xyz = normal_texture.Load(r2.xyz).xyz;
//     r1.xyz = r2.xyz * float3(2,2,2) + float3(-1,-1,-1);
//   }
//   r0.w = dot(r1.xyz, r1.xyz);
//   r0.w = sqrt(r0.w);
//   r1.xyz = r1.xyz / r0.www;
//   r0.x = dot(r0.xyz, r1.xyz);
//   r0.yz = v2.xy * ___self_illum_map_xform.xy + ___self_illum_map_xform.zw;
//   r0.yzw = UserParameterTexture_self_illum_map.Sample(UserParameterSampler_self_illum_map_s, r0.yz).xyz;
//   r0.yzw = ___self_illum_color.xyz * r0.yzw;
//   r0.yzw = ___self_illum_intensity * r0.yzw;
//   r0.yzw = g_alt_exposure.xxx * r0.yzw;
//   r0.x = log2(abs(r0.x));
//   r0.x = ___edge_fade_power * r0.x;
//   r0.x = exp2(r0.x);
//   r1.xyz = -___edge_fade_edge_tint.xyz + ___edge_fade_center_tint.xyz;
//   r1.xyz = r0.xxx * r1.xyz + ___edge_fade_edge_tint.xyz;
//   r0.xyz = r1.xyz * r0.yzw;
//   r0.xyz = v8.xyz * r0.xyz;
//   r0.xyz = g_exposure.xxx * r0.xyz;
//   r0.xyz = max(float3(0,0,0), r0.xyz);
//   r1.xyz = r0.xyz / g_exposure.yyy;
//   r2.xyz = cmp(float3(0,0,0) >= r0.xyz);
//   r3.xyz = sqrt(r0.xyz);
//   r2.xyz = r2.xyz ? float3(0,0,0) : r3.xyz;
//   o0.xyz = LDR_gamma2 ? r2.xyz : r0.xyz;
//   r0.xyz = cmp(float3(0,0,0) >= r1.xyz);
//   r2.xyz = sqrt(r1.xyz);
//   r0.xyz = r0.xyz ? float3(0,0,0) : r2.xyz;
//   o1.xyz = LDR_gamma2 ? r0.xyz : r1.xyz;
//   // o1.xyz = Neutwo(o1.xyz, 1.26);
//   o0.w = 0;
//   o1.w = 0;
//   return;
// }

cbuffer cb13_buf : register(b13)
{
    uint4 cb13_m0 : packoffset(c0);
    uint4 cb13_m1 : packoffset(c1);
    uint4 cb13_m2 : packoffset(c2);
    float4 cb13_m3 : packoffset(c3);
    float2 cb13_m4 : packoffset(c4);
    float2 cb13_m5 : packoffset(c4.z);
    float3 cb13_m6 : packoffset(c5);
    uint cb13_m7 : packoffset(c5.w);
    float3 cb13_m8 : packoffset(c6);
    uint cb13_m9 : packoffset(c6.w);
    float3 cb13_m10 : packoffset(c7);
    uint cb13_m11 : packoffset(c7.w);
    float4 cb13_m12 : packoffset(c8);
};

cbuffer cb0_buf : register(b0)
{
    float4 cb0_m0 : packoffset(c0);
    float4 cb0_m1 : packoffset(c1);
};

cbuffer cb1_buf : register(b1)
{
    uint4 cb1_m0 : packoffset(c0);
    uint4 cb1_m1 : packoffset(c1);
    uint4 cb1_m2 : packoffset(c2);
    uint4 cb1_m3 : packoffset(c3);
    uint4 cb1_m4 : packoffset(c4);
    uint4 cb1_m5 : packoffset(c5);
};

SamplerState s2 : register(s2);
Texture2D<float4> t2 : register(t2);
Texture2D<float4> t17 : register(t17);

static float4 gl_FragCoord;
static float3 TEXCOORD;
static float3 TEXCOORD3;
static float3 TEXCOORD6;
static float3 COLOR;
static float4 SV_Target;
static float4 SV_Target1;

struct SPIRV_Cross_Input
{
    float4 gl_FragCoord : SV_Position0;
    float v1 : SV_ClipDistance0;
    float4 TEXCOORD : TEXCOORD0;
    float4 TEXCOORD3 : TEXCOORD3;
    float4 TEXCOORD4 : TEXCOORD4;
    float4 TEXCOORD5 : TEXCOORD5;
    float4 TEXCOORD6 : TEXCOORD6;
    float4 TEXCOORD7 : TEXCOORD7;
    float3 COLOR : COLOR0;
    float3 v9 : COLOR1;

//   float4 v0 : SV_Position0,
//   float v1 : SV_ClipDistance0,
//   float4 v2 : TEXCOORD0,
//   float4 v3 : TEXCOORD3,
//   float4 v4 : TEXCOORD4,
//   float4 v5 : TEXCOORD5,
//   float4 v6 : TEXCOORD6,
//   float4 v7 : TEXCOORD7,
//   float3 v8 : COLOR0,
//   float3 v9 : COLOR1,
//   out float4 o0 : SV_Target0,
//   out float4 o1 : SV_Target1)
};

struct SPIRV_Cross_Output
{
    float4 SV_Target : SV_Target0;
    float4 SV_Target1 : SV_Target1;
};

float dp3_f32(float3 a, float3 b)
{
    precise float _82 = a.x * b.x;
    return mad(a.z, b.z, mad(a.y, b.y, _82));
}

int cvt_f32_i32(float v)
{
    return isnan(v) ? 0 : ((v < (-2147483648.0f)) ? int(0x80000000) : ((v > 2147483520.0f) ? 2147483647 : int(v)));
}

void frag_main()
{
    float3 _100 = float3(TEXCOORD6.x, TEXCOORD6.y, TEXCOORD6.z);
    float _102 = rsqrt(dp3_f32(_100, _100));
    float _137;
    float _138;
    float _139;
    if (cb1_m5.z != 0u)
    {
        _137 = TEXCOORD3.z;
        _138 = TEXCOORD3.y;
        _139 = TEXCOORD3.x;
    }
    else
    {
        float4 _130 = t17.Load(int3(uint2(uint(cvt_f32_i32(gl_FragCoord.x)), uint(cvt_f32_i32(gl_FragCoord.y))), 0u));
        _137 = mad(_130.z, 2.0f, -1.0f);
        _138 = mad(_130.y, 2.0f, -1.0f);
        _139 = mad(_130.x, 2.0f, -1.0f);
    }
    float3 _140 = float3(_139, _138, _137);
    float _142 = sqrt(dp3_f32(_140, _140));
    float4 _170 = t2.Sample(s2, float2(mad(TEXCOORD.x, cb13_m4.x, cb13_m5.x), mad(TEXCOORD.y, cb13_m4.y, cb13_m5.y)));
    float _200 = exp2(log2(abs(dp3_f32(float3(TEXCOORD6.x * _102, TEXCOORD6.y * _102, TEXCOORD6.z * _102), float3(_139 / _142, _138 / _142, _137 / _142)))) * cb13_m12.x);
    float _234 = max((((((_170.x * cb13_m6.x) * cb13_m3.x) * cb0_m1.x) * mad(_200, cb13_m8.x - cb13_m10.x, cb13_m10.x)) * COLOR.x) * cb0_m0.x, 0.0f);
    float _235 = max(cb0_m0.x * (COLOR.y * (mad(_200, cb13_m8.y - cb13_m10.y, cb13_m10.y) * (cb0_m1.x * (cb13_m3.x * (_170.y * cb13_m6.y))))), 0.0f);
    float _236 = max(cb0_m0.x * (COLOR.z * (mad(_200, cb13_m8.z - cb13_m10.z, cb13_m10.z) * (cb0_m1.x * (cb13_m3.x * (_170.z * cb13_m6.z))))), 0.0f);
    float _239 = _234 / cb0_m0.y;
    float _240 = _235 / cb0_m0.y;
    float _241 = _236 / cb0_m0.y;
    bool _253 = cb1_m5.x != 0u;
    SV_Target.x = _253 ? ((_234 <= 0.0f) ? 0.0f : sqrt(_234)) : _234;
    SV_Target.y = _253 ? ((_235 <= 0.0f) ? 0.0f : sqrt(_235)) : _235;
    SV_Target.z = _253 ? ((_236 <= 0.0f) ? 0.0f : sqrt(_236)) : _236;
    bool _272 = cb1_m5.y != 0u;
    SV_Target1.x = _272 ? ((_239 <= 0.0f) ? 0.0f : sqrt(_239)) : _239;
    SV_Target1.y = _272 ? ((_240 <= 0.0f) ? 0.0f : sqrt(_240)) : _240;
    SV_Target1.z = _272 ? ((_241 <= 0.0f) ? 0.0f : sqrt(_241)) : _241;
    SV_Target.w = 0.0f;
    SV_Target1.w = 0.0f;
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    gl_FragCoord = stage_input.gl_FragCoord;
    gl_FragCoord.w = 1.0 / gl_FragCoord.w;
    TEXCOORD = stage_input.TEXCOORD.xyz;
    TEXCOORD3 = stage_input.TEXCOORD3.xyz;
    TEXCOORD6 = stage_input.TEXCOORD6.xyz;
    COLOR = stage_input.COLOR;
    frag_main();
    SPIRV_Cross_Output stage_output;
      // SV_Target.xyz = Neutwo(SV_Target.xyz, 1.26);
      SV_Target1.xyz = Neutwo(SV_Target1.xyz, 1.26);

    stage_output.SV_Target = SV_Target;
    stage_output.SV_Target1 = SV_Target1;


    return stage_output;
}
