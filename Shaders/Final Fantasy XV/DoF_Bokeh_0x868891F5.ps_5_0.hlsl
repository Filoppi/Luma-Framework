#include "Includes/Common.hlsl"

cbuffer cb0_buf : register(b0)
{
    uint2 cb0_m0 : packoffset(c0);
    float2 cb0_m1 : packoffset(c0.z);
};

cbuffer cb1_buf : register(b1)
{
    uint4 cb1_m0 : packoffset(c0);
    float4 cb1_m1 : packoffset(c1);
    uint4 cb1_m2 : packoffset(c2);
    uint4 cb1_m3 : packoffset(c3);
    float4 cb1_m4 : packoffset(c4);
};

SamplerState s0 : register(s0);
Texture2D<float4> t0 : register(t0);
Texture2D<float4> t1 : register(t1);

static float2 TEXCOORD;
static float4 SV_TARGET;

struct SPIRV_Cross_Input
{
    float4 Position : SV_Position;

    float2 TEXCOORD : TEXCOORD1;
};

struct SPIRV_Cross_Output
{
    float4 SV_TARGET : SV_Target0;
};

void frag_main()
{
    float2 _48 = float2(TEXCOORD.x, TEXCOORD.y);
    float4 _51 = t0.SampleLevel(s0, _48, 0.0f);
    float _52 = _51.x;
    float _53 = _51.y;
    float _54 = _51.z;
    float4 _58 = t1.SampleLevel(s0, _48, 0.0f);
    float _61 = max(_58.x, _58.y);
    float _286;
    float _287;
    float _288;
    float _289;
    float resolutionScale = 1.0f;
    if (LumaData.GameData.IsUpscaling != 0)
    {
        resolutionScale = LumaData.RenderResolutionScale.x;
    }
    if (_61 > cb1_m4.w)
    {
        float _78 = mad(cb0_m1.x * resolutionScale, -0.5f, TEXCOORD.x);
        float _79 = mad(cb0_m1.y * resolutionScale, -0.5f, TEXCOORD.y);
        float2 _84 = float2(_78, _79);
        float4 _86 = t0.GatherRed(s0, _84);
        float4 _92 = t0.GatherGreen(s0, _84);
        float4 _98 = t0.GatherBlue(s0, _84);
        float4 _104 = t1.GatherRed(s0, _84);
        float4 _110 = t1.GatherGreen(s0, _84);
        float _125 = clamp(mad(max(_104.x, _110.x), cb1_m1.w, -1.0f), 0.0f, 1.0f);
        float _126 = clamp(mad(max(_104.y, _110.y), cb1_m1.w, -1.0f), 0.0f, 1.0f);
        float _127 = clamp(mad(max(_104.z, _110.z), cb1_m1.w, -1.0f), 0.0f, 1.0f);
        float _128 = clamp(mad(max(_104.w, _110.w), cb1_m1.w, -1.0f), 0.0f, 1.0f);
        float2 _169 = float2(mad(cb0_m1.x * resolutionScale, 0.5f, TEXCOORD.x), _79);
        float4 _171 = t0.GatherRed(s0, _169);
        float4 _175 = t0.GatherGreen(s0, _169);
        float4 _179 = t0.GatherBlue(s0, _169);
        float4 _183 = t1.GatherRed(s0, _169);
        float4 _187 = t1.GatherGreen(s0, _169);
        float _194 = clamp(mad(max(_183.y, _187.y), cb1_m1.w, -1.0f), 0.0f, 1.0f);
        float _195 = clamp(mad(max(_183.z, _187.z), cb1_m1.w, -1.0f), 0.0f, 1.0f);
        float2 _216 = float2(_78, mad(cb0_m1.y * resolutionScale, 0.5f, TEXCOORD.y));
        float4 _218 = t0.GatherRed(s0, _216);
        float4 _222 = t0.GatherGreen(s0, _216);
        float4 _226 = t0.GatherBlue(s0, _216);
        float4 _230 = t1.GatherRed(s0, _216);
        float4 _234 = t1.GatherGreen(s0, _216);
        float _241 = clamp(mad(max(_230.x, _234.x), cb1_m1.w, -1.0f), 0.0f, 1.0f);
        float _242 = clamp(mad(max(_230.y, _234.y), cb1_m1.w, -1.0f), 0.0f, 1.0f);
        float2 _263 = float2(TEXCOORD.x + cb0_m1.x * resolutionScale, TEXCOORD.y + cb0_m1.y * resolutionScale);
        float4 _265 = t0.SampleLevel(s0, _263, 0.0f);
        float4 _270 = t1.SampleLevel(s0, _263, 0.0f);
        float _275 = clamp(mad(max(_270.x, _270.y), cb1_m1.w, -1.0f), 0.0f, 1.0f);
        _286 = ((_241 + ((_194 + (_128 + (_127 + (_126 + (_125 + 1.0f))))) + _195)) + _242) + _275;
        _287 = mad(mad(_61, _265.z - _54, _54), _275, mad(mad(_61, _226.y - _54, _54), _242, mad(_241, mad(_61, _226.x - _54, _54), mad(mad(_61, _179.z - _54, _54), _195, mad(_194, mad(_61, _179.y - _54, _54), mad(_128, mad(_61, _98.w - _54, _54), mad(_127, mad(_61, _98.z - _54, _54), mad(_126, mad(_61, _98.y - _54, _54), mad(_125, mad(_98.x - _54, _61, _54), _54)))))))));
        _288 = mad(mad(_61, _265.y - _53, _53), _275, mad(mad(_61, _222.y - _53, _53), _242, mad(_241, mad(_61, _222.x - _53, _53), mad(mad(_61, _175.z - _53, _53), _195, mad(_194, mad(_61, _175.y - _53, _53), mad(_128, mad(_61, _92.w - _53, _53), mad(_127, mad(_61, _92.z - _53, _53), mad(_126, mad(_61, _92.y - _53, _53), mad(_125, mad(_92.x - _53, _61, _53), _53)))))))));
        _289 = mad(_275, mad(_61, _265.x - _52, _52), mad(_242, mad(_61, _218.y - _52, _52), mad(_241, mad(_61, _218.x - _52, _52), mad(mad(_61, _171.z - _52, _52), _195, mad(_194, mad(_61, _171.y - _52, _52), mad(_128, mad(_61, _86.w - _52, _52), mad(_127, mad(_61, _86.z - _52, _52), mad(_126, mad(_61, _86.y - _52, _52), mad(_125, mad(_61, _86.x - _52, _52), _52)))))))));
    }
    else
    {
        _286 = 1.0f;
        _287 = _54;
        _288 = _53;
        _289 = _52;
    }
    SV_TARGET.x = _289 / _286;
    SV_TARGET.y = _288 / _286;
    SV_TARGET.z = _287 / _286;
    SV_TARGET.w = _51.w;
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    TEXCOORD = stage_input.TEXCOORD;
    frag_main();
    SPIRV_Cross_Output stage_output;
    stage_output.SV_TARGET = SV_TARGET;
    return stage_output;
}
