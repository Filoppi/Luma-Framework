#include "./Includes/Common.hlsl"

cbuffer cb0_buf : register(b0)
{
    uint4 cb0_m0 : packoffset(c0);
    float2 cb0_m1 : packoffset(c1);
    float2 cb0_m2 : packoffset(c1.z);
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
    float4 cb2_m0 : packoffset(c0);
    float4 cb2_m1 : packoffset(c1);
    float4 cb2_m2 : packoffset(c2);
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
    precise float _78 = a.x * b.x;
    return mad(a.w, b.w, mad(a.z, b.z, mad(a.y, b.y, _78)));
}

// this one is when NPR uses SSS!?!?! only used by Megpoid mod pack, else normal MegaMix toggle uses 0th variant
void frag_main()
{
    float2 _99 = float2(TEXCOORD.x, TEXCOORD.y);
    float4 _102 = t0.Sample(s0, _99);
    float _104 = _102.w;
    if (_104 == 0.0f)
    {
        SV_Target.x = 0.0f;
        SV_Target.y = 0.0f;
        SV_Target.z = 0.0f;
        SV_Target.w = 0.0f;
        gl_FragDepth = 0.0f;
        return;
    }

    float4 _116 = t1.Sample(s1, _99);
    float _117 = _116.x;
    if (_102.y > 99.0f)
    {
        SV_Target.x = 0.0f;
        SV_Target.y = 0.0f;
        SV_Target.z = 0.0f;
        SV_Target.w = 0.0f;
        gl_FragDepth = _117;
        return;
    }

    bool _125 = _104 > 0.5f;
    float _131 = 1.0f / (_117 - cb2_m1.x);
    float _134 = _131 * cb2_m1.y;
    float _141 = 1.0f / rsqrt(clamp(mad(1.0f / _134, cb2_m0.z, 0.015625f), 0.0f, 1.0f));
    
    float _156 = mad(cb0_m2.x, 0.0007812500116415321826934814453125f, max(_134 * cb0_m5.x, -0.89999997615814208984375f));
    float _157 = mad(cb0_m2.y, 0.001388888922519981861114501953125f, max(_134 * cb0_m5.y, -0.89999997615814208984375f));
    float _160 = clamp(mad(_131, cb2_m1.y, -2.0f) * 0.111111111938953399658203125f, 0.0f, 1.0f);

    float _167 = mad(_160, 1.0f - _156, _156) * (cb0_m1.x * GS.NPRContourRadius);
    float _170 = mad(_160, 1.0f - _157, _157) * (cb0_m1.y * GS.NPRContourRadius);

    float _174 = _167 * cb4_m3.x;
    float _175 = _170 * cb4_m3.x;
    float _176 = mad(_174, -1.0f, TEXCOORD.x);
    float _177 = mad(_175, -0.0f, TEXCOORD.y);
    float _178 = mad(_174, 1.0f, TEXCOORD.x);
    float4 _181 = t0.Sample(s0, float2(_176, _177));
    float _182 = _181.y;
    float4 _186 = t0.Sample(s0, float2(_178, _177));
    float _187 = _186.y;
    float _189 = mad(_174, 0.0f, TEXCOORD.x);
    float _190 = mad(_175, 1.0f, TEXCOORD.y);
    float _191 = mad(_175, -1.0f, TEXCOORD.y);
    float4 _194 = t0.Sample(s0, float2(_189, _190));
    float _195 = _194.z;
    float4 _199 = t0.Sample(s0, float2(_189, _191));
    float _201 = _199.z;
    float2 _205 = float2(mad(-_167, cb4_m3.x, TEXCOORD.x), mad(_170, cb4_m3.x, TEXCOORD.y));
    float4 _207 = t0.Sample(s0, _205);
    float _208 = _207.y;
    float _209 = _207.z;
    float2 _214 = float2(mad(_167, cb4_m3.x, TEXCOORD.x), mad(-_170, cb4_m3.x, TEXCOORD.y));
    float4 _216 = t0.Sample(s0, _214);
    float _218 = _216.y;
    float _219 = _216.z;
    float2 _220 = float2(_176, _191);
    float4 _222 = t0.Sample(s0, _220);
    float _223 = _222.y;
    float _224 = _222.z;
    float2 _226 = float2(_178, _190);
    float4 _228 = t0.Sample(s0, _226);
    float _230 = _228.y;
    float _231 = _228.z;
    float4 _233 = t1.Sample(s1, _205);
    float4 _236 = t1.Sample(s1, _214);
    float4 _239 = t1.Sample(s1, _220);
    float4 _242 = t1.Sample(s1, _226);

    float _260 = dp4_f32(float4(_181.w, _186.w, _194.w, _199.w), 1.0f.xxxx) + dp4_f32(float4(_207.w, _216.w, _222.w, _228.w), 1.0f.xxxx);
    float _265 = float(_125);
    bool _270 = ((_265 * float(_260 < 7.5f)) > 0.0f) || (!(_125 || (!(_260 > 0.5f))));
    float _275 = ((((1.0f / (_233.x - cb2_m1.x)) * cb2_m1.y) + ((1.0f / (_236.x - cb2_m1.x)) * cb2_m1.y)) * 0.5f) - _134;
    float _276 = ((((1.0f / (_239.x - cb2_m1.x)) * cb2_m1.y) + ((1.0f / (_242.x - cb2_m1.x)) * cb2_m1.y)) * 0.5f) - _134;
    float _277 = mad(_141, -3.0f, 4.0f);
    float _282 = mad(_265, cb2_m0.w, cb2_m0.x);
    float _284 = mad(-_277, _282, _275);
    float _286 = mad(-_277, _282, _276);
    if ((_284 > 0.0f) || (_286 > 0.0f))
    {
        SV_Target.x = _141 * clamp(mad(max(_284, _286), 300.0f, 0.25f), 0.0f, 1.0f);
        SV_Target.z = 0.0f;
        SV_Target.y = _270 ? _141 : 0.0f;
        SV_Target.w = _265;
        gl_FragDepth = _117;
        return;
    }
    if (((_275 + cb2_m0.y) < 0.0f) || ((cb2_m0.y + _276) < 0.0f))
    {
        SV_Target.x = 0.0f;
        SV_Target.z = 0.0f;
        SV_Target.y = _270 ? _141 : 0.0f;
        SV_Target.w = _265;
        gl_FragDepth = _117;
        return;
    }
    bool _329 = ((((_223 * (_182 * _208)) < 0.5f) || ((_224 * (_187 * _209)) < 0.5f)) || ((_230 * (_195 * _218)) < 0.5f)) || ((_231 * (_201 * _219)) < 0.5f);
    float _376 = (((_329 ? 0.0f : 1.0f) * (_141 * clamp(mad(max(mad(clamp((2.0f - _201) + (_195 - 2.0f), 0.0f, 1.0f), 0.5f, clamp((_182 - 2.0f) + (2.0f - _187), 0.0f, 1.0f)), clamp(dp4_f32(float4(_208 - 2.0f, _209 - 2.0f, _218 - 2.0f, _219 - 2.0f), float4(0.707099974155426025390625f, 0.707099974155426025390625f, -0.707099974155426025390625f, -0.707099974155426025390625f)), 0.0f, 1.0f) + clamp(dp4_f32(float4(_223 - 2.0f, _231 - 2.0f, _230 - 2.0f, _224 - 2.0f), float4(0.707099974155426025390625f, 0.707099974155426025390625f, -0.707099974155426025390625f, -0.707099974155426025390625f)), 0.0f, 1.0f)), 1.2000000476837158203125f, -0.60000002384185791015625f), 0.0f, 1.0f))) * 0.800000011920928955078125f) * clamp(mad(_265, cb2_m2.x - 1.0f, 1.0f), 0.0f, 1.0f);
    if ((t0.Sample(s0, float2(mad(_174, -2.0f, TEXCOORD.x), _177)).w * t0.Sample(s0, float2(mad(_174, 2.0f, TEXCOORD.x), _177)).w) == 0.0f)
    {
        SV_Target.x = _141 * float(_141 > 0.89999997615814208984375f);
        SV_Target.y = _270 ? _141 : 0.0f;
        SV_Target.w = _265;
        SV_Target.z = _329 ? 0.0f : _376;
        gl_FragDepth = _117;
        return;
    }
    SV_Target.x = 0.0f;
    SV_Target.y = _270 ? _141 : 0.0f;
    SV_Target.w = _265;
    SV_Target.z = _329 ? 0.0f : _376;
    gl_FragDepth = _117;
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
