cbuffer cb0_buf : register(b0)
{
    float4 cb0_m : packoffset(c0);
};

cbuffer cb13_buf : register(b13)
{
    float2 cb13_m0 : packoffset(c0);
    float2 cb13_m1 : packoffset(c0.z);
    float2 cb13_m2 : packoffset(c1);
    float2 cb13_m3 : packoffset(c1.z);
    float2 cb13_m4 : packoffset(c2);
    float2 cb13_m5 : packoffset(c2.z);
    float2 cb13_m6 : packoffset(c3);
    float2 cb13_m7 : packoffset(c3.z);
    float2 cb13_m8 : packoffset(c4);
    float2 cb13_m9 : packoffset(c4.z);
    float4 cb13_m10 : packoffset(c5);
};

cbuffer cb1_buf : register(b1)
{
    float3 cb1_m0 : packoffset(c0);
    float cb1_m1 : packoffset(c0.w);
};

cbuffer cb2_buf : register(b2)
{
    uint4 cb2_m0 : packoffset(c0);
    uint4 cb2_m1 : packoffset(c1);
    uint4 cb2_m2 : packoffset(c2);
    uint4 cb2_m3 : packoffset(c3);
    float4 cb2_m4 : packoffset(c4);
};

SamplerState s0 : register(s0);
SamplerState s1 : register(s1);
SamplerState s2 : register(s2);
SamplerState s3 : register(s3);
SamplerState s4 : register(s4);
Texture2D<float4> t0 : register(t0);
Texture2D<float4> t1 : register(t1);
Texture2D<float4> t2 : register(t2);
Texture2D<float4> t3 : register(t3);
Texture2D<float4> t4 : register(t4);

static float2 TEXCOORD;
static float4 TEXCOORD1;
static float3 TEXCOORD2;
static float3 TEXCOORD3;
static float4 SV_Target;
static float4 SV_Target1;

struct SPIRV_Cross_Input
{
    float4 v0 : SV_Position0;
    float v1 : SV_ClipDistance0;
    float4 TEXCOORD : TEXCOORD0;
    float4 TEXCOORD1 : TEXCOORD1;
    float4 TEXCOORD2 : TEXCOORD2;
    float4 TEXCOORD3 : TEXCOORD3;
};

struct SPIRV_Cross_Output
{
    float4 SV_Target : SV_Target0;
    float4 SV_Target1 : SV_Target1;
};

float dp3_f32(float3 a, float3 b)
{
    precise float _87 = a.x * b.x;
    return mad(a.z, b.z, mad(a.y, b.y, _87));
}

#include "./Includes/Common.hlsl"

void frag_main()
{
    float4 _124 = t4.SampleBias(s4, float2(mad(TEXCOORD.x, cb13_m8.x, cb13_m9.x), mad(TEXCOORD.y, cb13_m8.y, cb13_m9.y)), cb0_m.x);
    float _125 = _124.x;
    float _126 = _124.y;
    float3 _133 = float3(_125, _126, sqrt(1.0f - min((_125 * _125) + (_126 * _126), 1.0f)));
    float _135 = rsqrt(dp3_f32(_133, _133));
    float4 _152 = t3.SampleBias(s3, float2(mad(TEXCOORD.x, cb13_m6.x, cb13_m7.x), mad(TEXCOORD.y, cb13_m6.y, cb13_m7.y)), cb0_m.x);
    float _153 = _152.x;
    float _154 = _152.y;
    float _160 = sqrt(1.0f - min((_153 * _153) + (_154 * _154), 1.0f));
    float3 _161 = float3(_153, _154, _160);
    float _163 = rsqrt(dp3_f32(_161, _161));
    float _166 = _160 * _163;
    float _171 = (_153 * _163) + ((_125 * _135) * cb13_m10.x);
    float _172 = ((_126 * _135) * cb13_m10.x) + (_154 * _163);
    float3 _173 = float3(_171, _172, _166);
    float _175 = rsqrt(dp3_f32(_173, _173));
    float _176 = _171 * _175;
    float _177 = _175 * _172;
    float _178 = _175 * _166;
    float _206 = mad(TEXCOORD1.x, _178, (TEXCOORD2.x * _177) + (_176 * TEXCOORD3.x));
    float _207 = mad(TEXCOORD1.y, _178, (_176 * TEXCOORD3.y) + (TEXCOORD2.y * _177));
    float _208 = mad(TEXCOORD1.z, _178, (_176 * TEXCOORD3.z) + (TEXCOORD2.z * _177));
    float3 _209 = float3(_206, _207, _208);
    float _211 = rsqrt(dp3_f32(_209, _209));
    float _212 = _206 * _211;
    float _213 = _211 * _207;
    float _214 = _211 * _208;
    float3 _215 = float3(_212, _213, _214);
    SV_Target1.x = mad(_212, 0.5f, 0.5f);
    SV_Target1.y = mad(_213, 0.5f, 0.5f);
    SV_Target1.z = mad(_214, 0.5f, 0.5f);
    float _235 = mad(clamp(dp3_f32(_215, float3(0.300000011920928955078125f, 0.699999988079071044921875f, 0.60000002384185791015625f)), 0.0f, 1.0f), 0.4000000059604644775390625f, mad(clamp(dp3_f32(_215, float3(-0.300000011920928955078125f, -0.699999988079071044921875f, -0.60000002384185791015625f)), 0.0f, 1.0f), 0.699999988079071044921875f, (clamp(dp3_f32(_215, float3(-0.680000007152557373046875f, -0.4799999892711639404296875f, 0.60000002384185791015625f)), 0.0f, 1.0f) * 0.5f) + (clamp(dp3_f32(_215, float3(0.680000007152557373046875f, 0.4799999892711639404296875f, -0.60000002384185791015625f)), 0.0f, 1.0f) * 1.2000000476837158203125f)));
    float4 _250 = t0.SampleBias(s0, float2(mad(TEXCOORD.x, cb13_m0.x, cb13_m1.x), mad(TEXCOORD.y, cb13_m0.y, cb13_m1.y)), cb0_m.x);
    float4 _269 = t1.SampleBias(s1, float2(mad(TEXCOORD.x, cb13_m2.x, cb13_m3.x), mad(TEXCOORD.y, cb13_m2.y, cb13_m3.y)), cb0_m.x);
    float4 _292 = t2.SampleBias(s2, float2(mad(TEXCOORD.x, cb13_m4.x, cb13_m5.x), mad(TEXCOORD.y, cb13_m4.y, cb13_m5.y)), cb0_m.x);
    float _297 = (_250.x * _269.x) * _292.x;
    float _298 = _292.y * (_250.y * _269.y);
    float _299 = _292.z * (_250.z * _269.z);
    float _300 = _292.w * (_250.w * _269.w);
    float _319 = (_297 * 21.112094879150390625f) + (cb1_m1 * mad(_297, -21.112094879150390625f, cb1_m0.x));
    float _320 = (mad(_298, -21.112094879150390625f, cb1_m0.y) * cb1_m1) + (_298 * 21.112094879150390625f);
    float _321 = (mad(_299, -21.112094879150390625f, cb1_m0.z) * cb1_m1) + (_299 * 21.112094879150390625f);
    bool _328 = cb2_m4.x != 0.0f;
    SV_Target.x = _328 ? (_235 * _319) : _319;
    SV_Target.y = _328 ? (_235 * _320) : _320;
    SV_Target.z = _328 ? (_235 * _321) : _321;
    SV_Target.w = _300;
    SV_Target1.w = _300;
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    TEXCOORD = stage_input.TEXCOORD.xy;
    TEXCOORD1 = stage_input.TEXCOORD1;
    TEXCOORD2 = stage_input.TEXCOORD2.xyz;
    TEXCOORD3 = stage_input.TEXCOORD3.xyz;
    frag_main();
    SPIRV_Cross_Output stage_output;

    SV_Target = saturate(SV_Target);
    SV_Target1 = saturate(SV_Target1);

    stage_output.SV_Target = SV_Target;
    stage_output.SV_Target1 = SV_Target1;

    return stage_output;
}
