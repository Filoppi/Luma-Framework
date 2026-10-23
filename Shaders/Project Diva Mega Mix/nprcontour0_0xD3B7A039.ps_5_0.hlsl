#include "./Includes/Common.hlsl"


cbuffer cb0_buf : register(b0)
{
    uint4 cb0_m0 : packoffset(c0);
    float2 cb0_m1 : packoffset(c1);   // g_texel_size xy (inv)
    float2 cb0_m2 : packoffset(c1.z); // g_texel_size zw (pix count)
    uint4 cb0_m3 : packoffset(c2);
    uint cb0_m4 : packoffset(c3);
    float2 cb0_m5 : packoffset(c3.y);
    uint cb0_m6 : packoffset(c3.w);
};

cbuffer cb4_buf : register(b4)
{
    uint4 cb4_m0 : packoffset(c0);
    uint4 cb4_m1 : packoffset(c1);
    uint4 cb4_m2 : packoffset(c2);
    float4 cb4_m3 : packoffset(c3);
};

cbuffer cb2_buf : register(b2)
{
    float4 cb2_m0 : packoffset(c0); // g_contour xyzw
    // 0.0027
    // 0.003
    // 1.20993
    // 0.0008
    float4 cb2_m1 : packoffset(c1); // g_near_far xy
    // -8.3334e-06
    // 0.0500004
    // 0.05
    // 6000
    float4 cb2_m2 : packoffset(c2); // g_ex_param x
    // 0.2
    // 0
    // 0
    // 0
};

SamplerState s0 : register(s0);
SamplerState s1 : register(s1);
Texture2D<float4> t0 : register(t0);
Texture2D<float4> t1 : register(t1);

static float gl_FragDepth;
static float2 TEXCOORD;
static float4 SV_Target;

struct SPIRV_Cross_Input
{
    float4 v0 : SV_POSITION0;
    float2 TEXCOORD : TEXCOORD1;
};

struct SPIRV_Cross_Output
{
    float4 SV_Target : SV_Target0;
    float gl_FragDepth : SV_Depth;
};

float dp4_f32(float4 a, float4 b)
{
    precise float _79 = a.x * b.x;
    return mad(a.w, b.w, mad(a.z, b.z, mad(a.y, b.y, _79)));
}

void frag_main()
{
    float4 g_contour_0 = cb2_m0;
    float4 g_near_far_1 = cb2_m1;
    float4 g_ex_param_2 = cb2_m2;

    float2 _100 = float2(TEXCOORD.x, TEXCOORD.y);
    float4 _103 = t0.Sample(s0, _100);
    float _105 = _103.w;
    if (_105 == 0.0f) // early exit, depth == zero
    {
        SV_Target.x = 0.0f;
        SV_Target.y = 0.0f;
        SV_Target.z = 0.0f;
        SV_Target.w = 0.0f;
        gl_FragDepth = 0.0f;
        return;
    }

    float4 _117 = t1.Sample(s1, _100);
    float _118 = _117.x;
    if (_103.y > 99.0f) // early exit, > far
    {
        SV_Target.x = 0.0f;
        SV_Target.y = 0.0f;
        SV_Target.z = 0.0f;
        SV_Target.w = 0.0f;
        gl_FragDepth = _118;
        return;
    }
    
    // uv stuff
    bool _126 = _105 > 0.5f;
    float _132 = 1.0f / (_118 - g_near_far_1.x);
    float _135 = _132 * g_near_far_1.y;
    float _142 = 1.0f / rsqrt(clamp(mad(1.0f / _135, g_contour_0.z, 0.015625f), 0.0f, 1.0f));

    float _157 = mad(cb0_m2.x, 0.0007812500116415321826934814453125f, max(_135 * cb0_m5.x, -0.89999997615814208984375f));
    float _158 = mad(cb0_m2.y, 0.001388888922519981861114501953125f, max(_135 * cb0_m5.y, -0.89999997615814208984375f));
    float _161 = clamp(mad(_132, g_near_far_1.y, -2.0f) * 0.111111111938953399658203125f, 0.0f, 1.0f);

    float _168 = mad(_161, 1.0f - _157, _157) * (cb0_m1.x * GS.NPRContourRadius);
    float _171 = mad(_161, 1.0f - _158, _158) * (cb0_m1.y * GS.NPRContourRadius);

    // search for edges and contour stuff idk
    float _175 = _168 * cb4_m3.x;
    float _176 = _171 * cb4_m3.x;
    float _177 = mad(_175, -1.0f, TEXCOORD.x);
    float _178 = mad(_176, -0.0f, TEXCOORD.y);
    float _179 = mad(_175, 1.0f, TEXCOORD.x);
    float4 _182 = t0.Sample(s0, float2(_177, _178));
    float _183 = _182.y;
    float4 _187 = t0.Sample(s0, float2(_179, _178));
    float _188 = _187.y;
    float _190 = mad(_175, 0.0f, TEXCOORD.x);
    float _191 = mad(_176, 1.0f, TEXCOORD.y);
    float _192 = mad(_176, -1.0f, TEXCOORD.y);
    float4 _195 = t0.Sample(s0, float2(_190, _191));
    float _196 = _195.z;
    float4 _200 = t0.Sample(s0, float2(_190, _192));
    float _202 = _200.z;
    float2 _206 = float2(mad(-_168, cb4_m3.x, TEXCOORD.x), mad(_171, cb4_m3.x, TEXCOORD.y));
    float4 _208 = t0.Sample(s0, _206);
    float _209 = _208.y;
    float _210 = _208.z;
    float2 _215 = float2(mad(_168, cb4_m3.x, TEXCOORD.x), mad(-_171, cb4_m3.x, TEXCOORD.y));
    float4 _217 = t0.Sample(s0, _215);
    float _219 = _217.y;
    float _220 = _217.z;
    float2 _221 = float2(_177, _192);
    float4 _223 = t0.Sample(s0, _221);
    float _224 = _223.y;
    float _225 = _223.z;
    float2 _227 = float2(_179, _191);
    float4 _229 = t0.Sample(s0, _227);
    float _231 = _229.y;
    float _232 = _229.z;
    float4 _234 = t1.Sample(s1, _206);
    float4 _237 = t1.Sample(s1, _215);
    float4 _240 = t1.Sample(s1, _221);
    float4 _243 = t1.Sample(s1, _227);

    float _261 = dp4_f32(float4(_182.w, _187.w, _195.w, _200.w), 1.0f.xxxx) + dp4_f32(float4(_208.w, _217.w, _223.w, _229.w), 1.0f.xxxx);
    float _266 = float(_126);
    bool _271 = ((_266 * float(_261 < 7.5f)) > 0.0f) || (!(_126 || (!(_261 > 2.2999999523162841796875f))));
    float _276 = ((((1.0f / (_234.x - g_near_far_1.x)) * g_near_far_1.y) + ((1.0f / (_237.x - g_near_far_1.x)) * g_near_far_1.y)) * 0.5f) - _135;
    float _277 = ((((1.0f / (_240.x - g_near_far_1.x)) * g_near_far_1.y) + ((1.0f / (_243.x - g_near_far_1.x)) * g_near_far_1.y)) * 0.5f) - _135;
    float _278 = mad(_142, -3.0f, 4.0f);
    float _283 = mad(_266, g_contour_0.w, g_contour_0.x);
    float _285 = mad(-_278, _283, _276);
    float _287 = mad(-_278, _283, _277);

    if ((_285 > 0.0f) || (_287 > 0.0f))
    {
        SV_Target.x = _142 * clamp(mad(max(_285, _287), 300.0f, 0.25f), 0.0f, 1.0f);
        SV_Target.z = 0.0f;
        SV_Target.y = _271 ? _142 : 0.0f;
        SV_Target.w = _266;
        gl_FragDepth = _118;
        return;
    }
    if (((_276 + g_contour_0.y) < 0.0f) || ((g_contour_0.y + _277) < 0.0f))
    {
        SV_Target.x = 0.0f;
        SV_Target.z = 0.0f;
        SV_Target.y = _271 ? _142 : 0.0f;
        SV_Target.w = _266;
        gl_FragDepth = _118;
        return;
    }
    bool _330 = ((((_224 * (_183 * _209)) < 0.5f) || ((_225 * (_188 * _210)) < 0.5f)) || ((_231 * (_196 * _219)) < 0.5f)) || ((_232 * (_202 * _220)) < 0.5f);
    float _377 = (((_330 ? 0.0f : 1.0f) * (_142 * clamp(mad(max(mad(clamp((2.0f - _202) + (_196 - 2.0f), 0.0f, 1.0f), 0.5f, clamp((_183 - 2.0f) + (2.0f - _188), 0.0f, 1.0f)), clamp(dp4_f32(float4(_209 - 2.0f, _210 - 2.0f, _219 - 2.0f, _220 - 2.0f), float4(0.707099974155426025390625f, 0.707099974155426025390625f, -0.707099974155426025390625f, -0.707099974155426025390625f)), 0.0f, 1.0f) + clamp(dp4_f32(float4(_224 - 2.0f, _232 - 2.0f, _231 - 2.0f, _225 - 2.0f), float4(0.707099974155426025390625f, 0.707099974155426025390625f, -0.707099974155426025390625f, -0.707099974155426025390625f)), 0.0f, 1.0f)), 1.2000000476837158203125f, -0.60000002384185791015625f), 0.0f, 1.0f))) * 0.800000011920928955078125f) * clamp(mad(_266, g_ex_param_2.x - 1.0f, 1.0f), 0.0f, 1.0f);
    if ((t0.Sample(s0, float2(mad(_175, -2.0f, TEXCOORD.x), _178)).w * t0.Sample(s0, float2(mad(_175, 2.0f, TEXCOORD.x), _178)).w) == 0.0f)
    {
        SV_Target.x = _142 * float(_142 > 0.89999997615814208984375f);
        SV_Target.y = _271 ? _142 : 0.0f;
        SV_Target.w = _266;
        SV_Target.z = _330 ? 0.0f : _377;
        gl_FragDepth = _118;
        return;
    }
    SV_Target.x = 0.0f;
    SV_Target.y = _271 ? _142 : 0.0f;
    SV_Target.w = _266;
    SV_Target.z = _330 ? 0.0f : _377;

    gl_FragDepth = _118;
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    TEXCOORD = stage_input.TEXCOORD;
    frag_main();

    SV_Target *= GS.NPRContourStrength;
    SV_Target = saturate(SV_Target);

    SPIRV_Cross_Output stage_output;
    stage_output.gl_FragDepth = gl_FragDepth;
    stage_output.SV_Target = SV_Target;
    return stage_output;
}

// (FAILED DECOMP)
// // ---- Created with 3Dmigoto v1.3.16 on Tue Sep 02 15:46:11 2025
// 
// cbuffer Quad : register(b0)
// {
//   float4 g_texcoord_modifier : packoffset(c0);
//   float4 g_texel_size : packoffset(c1);
//   float4 g_color : packoffset(c2);
//   float4 g_texture_lod : packoffset(c3);
// }
// 
// cbuffer NPRParam : register(b4)
// {
//   uint4 g_npr_flags : packoffset(c0);
//   float4 g_npr_param[8] : packoffset(c1);
//   uint4 g_npr_dbg_flags : packoffset(c9);
//   float4 g_npr_dbg_param[4] : packoffset(c10);
// }
// 
// cbuffer ContourCoef : register(b2)
// {
//   float4 g_contour : packoffset(c0);
//   float4 g_near_far : packoffset(c1);
//   float4 g_ex_param : packoffset(c2);
// }
// 
// SamplerState g_color_sampler_s : register(s0);
// SamplerState g_depth_sampler_s : register(s1);
// Texture2D<float4> g_color_texture : register(t0);
// Texture2D<float4> g_depth_texture : register(t1);
// 
// 
// // 3Dmigoto declarations
// #define cmp -
// 
// 
// void main(
//   float4 v0 : SV_POSITION0,
//   float2 v1 : TEXCOORD0,
//   out float4 o0 : SV_Target0,
//   out float oDepth : SV_Depth)
// {
//   float4 r0,r1,r2,r3,r4,r5,r6,r7,r8,r9,r10,r11,r12;
//   uint4 bitmask, uiDest;
//   float4 fDest;
// 
//   r0.xy = g_color_texture.Sample(g_color_sampler_s, v1.xy).yw;
//   r0.z = cmp(r0.y == 0.000000);
//   if (r0.z != 0) {
//     o0.xyzw = float4(0,0,0,0);
//     oDepth = 0;
//     return;
//   }
//   r0.z = g_depth_texture.Sample(g_depth_sampler_s, v1.xy).x;
//   r0.x = cmp(99 < r0.x);
//   if (r0.x != 0) {
//     o0.xyzw = float4(0,0,0,0);
//     oDepth = r0.z;
//     return;
//   }
//   r0.x = cmp(0.5 < r0.y);
//   r0.w = r0.x ? 1.000000 : 0;
//   r1.x = -g_near_far.x + r0.z;
//   r1.x = rcp(r1.x);
//   r1.y = g_near_far.y * r1.x;
//   r1.z = rcp(r1.y);
//   r1.z = saturate(r1.z * g_contour.z + 0.015625);
//   r1.z = rsqrt(r1.z);
//   r1.z = 1 / r1.z;
//   r2.xy = g_texture_lod.yz * r1.yy;
//   r2.xy = max(float2(-0.899999976,-0.899999976), r2.xy);
//   r2.xy = g_texel_size.zw * float2(0.000781250012,0.00138888892) + r2.xy;
//   r1.x = r1.x * g_near_far.y + -2;
//   r1.x = saturate(0.111111112 * r1.x);
//   r2.zw = float2(1,1) + -r2.xy;
//   r1.xw = r1.xx * r2.zw + r2.xy;
//   r2.xz = g_texel_size.xx * r1.xx;
//   r2.yw = -g_texel_size.yy * r1.ww;
//   r3.xyzw = g_npr_param[2].xxxx * r2.xyzw;
//   r4.xyzw = r3.zwzw * float4(-1,0,1,0) + v1.xyxy;
//   r1.xw = g_color_texture.Sample(g_color_sampler_s, r4.xy).yw;
//   r2.xy = g_color_texture.Sample(g_color_sampler_s, r4.zw).yw;
//   r4.xyzw = r3.zwzw * float4(0,-1,0,1) + v1.xyxy;
//   r4.xy = g_color_texture.Sample(g_color_sampler_s, r4.xy).zw;
//   r5.zw = g_color_texture.Sample(g_color_sampler_s, r4.zw).wz;
//   r4.zw = -r2.zw * g_npr_param[2].xx + v1.xy;
//   r6.xyz = g_color_texture.Sample(g_color_sampler_s, r4.zw).yzw;
//   r2.zw = r2.zw * g_npr_param[2].xx + v1.xy;
//   r7.yzw = g_color_texture.Sample(g_color_sampler_s, r2.zw).wyz;
//   r8.xyzw = r3.zwzw * float4(-1,1,1,-1) + v1.xyxy;
//   r9.xyz = g_color_texture.Sample(g_color_sampler_s, r8.xy).yzw;
//   r10.yzw = g_color_texture.Sample(g_color_sampler_s, r8.zw).wyz;
//   r4.z = g_depth_texture.Sample(g_depth_sampler_s, r4.zw).x;
//   r2.z = g_depth_texture.Sample(g_depth_sampler_s, r2.zw).x;
//   r2.w = g_depth_texture.Sample(g_depth_sampler_s, r8.xy).x;
//   r4.w = g_depth_texture.Sample(g_depth_sampler_s, r8.zw).x;
//   r4.z = -g_near_far.x + r4.z;
//   r4.z = rcp(r4.z);
//   r8.x = g_near_far.y * r4.z;
//   r2.z = -g_near_far.x + r2.z;
//   r2.z = rcp(r2.z);
//   r11.x = g_near_far.y * r2.z;
//   r2.z = -g_near_far.x + r2.w;
//   r2.z = rcp(r2.z);
//   r8.y = g_near_far.y * r2.z;
//   r2.z = -g_near_far.x + r4.w;
//   r2.z = rcp(r2.z);
//   r11.y = g_near_far.y * r2.z;
//   r12.x = r1.w;
//   r12.y = r2.y;
//   r12.z = r4.y;
//   r12.w = r5.z;
//   r1.w = dot(r12.xyzw, float4(1,1,1,1));
//   r12.x = r6.z;
//   r12.y = r7.y;
//   r12.z = r9.z;
//   r12.w = r10.y;
//   r2.y = dot(r12.xyzw, float4(1,1,1,1));
//   r1.w = r2.y + r1.w;
//   r2.y = cmp(2.29999995 < r1.w);
//   r0.x = r0.x ? 0 : r2.y;
//   r1.w = cmp(r1.w < 7.5);
//   r1.w = r1.w ? 1.000000 : 0;
//   r1.w = r1.w * r0.w;
//   r1.w = cmp(0 < r1.w);
//   r0.x = (int)r0.x | (int)r1.w;
//   r0.y = (int)r1.z & (int)r0.x;
//   r2.yz = r11.xy + r8.xy;
//   r1.yw = r2.yz * float2(0.5,0.5) + -r1.yy;
//   r0.x = r1.z * -3 + 4;
//   r2.y = g_contour.w * r0.w + g_contour.x;
//   r2.yz = -r0.xx * r2.yy + r1.yw;
//   r4.yz = cmp(float2(0,0) < r2.yz);
//   r0.x = (int)r4.z | (int)r4.y;
//   if (r0.x != 0) {
//     r0.x = max(r2.y, r2.z);
//     r0.x = saturate(r0.x * 300 + 0.25);
//     o0.x = r0.x * r1.z;
//     o0.z = 0;
//     o0.yw = r0.yw;
//     oDepth = r0.z;
//     return;
//   }
//   r1.yw = g_contour.yy + r1.yw;
//   r1.yw = cmp(r1.yw < float2(0,0));
//   r0.x = (int)r1.w | (int)r1.y;
//   if (r0.x != 0) {
//     o0.xz = float2(0,0);
//     o0.yw = r0.yw;
//     oDepth = r0.z;
//     return;
//   }
//   r5.x = r1.x;
//   r5.y = r2.x;
//   r5.z = r4.x;
//   r7.xy = r6.xy;
//   r2.xyzw = r7.xyzw * r5.xyzw;
//   r10.xy = r9.xy;
//   r2.xyzw = r10.xyzw * r2.xyzw;
//   r2.xyzw = cmp(r2.xyzw < float4(0.5,0.5,0.5,0.5));
//   r0.x = (int)r2.y | (int)r2.x;
//   r0.x = (int)r2.z | (int)r0.x;
//   r0.x = (int)r2.w | (int)r0.x;
//   r1.x = r0.x ? 0 : 1;
//   r2.xyzw = float4(-2,-2,-2,-2) + r5.xyzw;
//   r4.xyzw = float4(-2,-2,-2,-2) + r7.xyzw;
//   r5.xyzw = float4(-2,-2,-2,-2) + r10.xyzw;
//   r3.xyzw = r3.xyzw * float4(-2,0,2,0) + v1.xyxy;
//   r1.y = g_color_texture.Sample(g_color_sampler_s, r3.xy).w;
//   r1.w = g_color_texture.Sample(g_color_sampler_s, r3.zw).w;
//   r0.x = cmp((int)r0.x == 0);
//   r2.xy = saturate(r2.xz + -r2.yw);
//   r2.x = r2.y * 0.5 + r2.x;
//   r2.y = saturate(dot(r4.xyzw, float4(0.707099974,0.707099974,-0.707099974,-0.707099974)));
//   r2.z = saturate(dot(r5.xwzy, float4(0.707099974,0.707099974,-0.707099974,-0.707099974)));
//   r2.y = r2.y + r2.z;
//   r2.x = max(r2.x, r2.y);
//   r2.x = saturate(r2.x * 1.20000005 + -0.600000024);
//   r2.x = r2.x * r1.z;
//   r1.x = r2.x * r1.x;
//   r1.x = 0.800000012 * r1.x;
//   r2.x = 1 + -g_ex_param.x;
//   r2.x = saturate(-r2.x * r0.w + 1);
//   r1.x = r2.x * r1.x;
//   r0.x = r0.x ? r1.x : 0;
//   r1.x = r1.y * r1.w;
//   r1.x = cmp(r1.x == 0.000000);
//   if (r1.x != 0) {
//     r1.x = cmp(0.899999976 < r1.z);
//     r1.x = !r1.x ? 1.000000 : 0;
//     o0.x = r1.z * r1.x;
//     o0.yw = r0.yw;
//     o0.z = r0.x;
//     oDepth = r0.z;
//     return;
//   }
//   o0.x = 0;
//   o0.yw = r0.yw;
//   o0.z = r0.x;
//   oDepth = r0.z;
//   return;
// }