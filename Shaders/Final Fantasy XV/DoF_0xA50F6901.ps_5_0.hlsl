#include "Includes/Common.hlsl"

cbuffer cb0_buf : register(b0)
{
    uint2 cb0_m0 : packoffset(c0);
    float2 cb0_m1 : packoffset(c0.z);
};

SamplerState s0 : register(s0);
Texture2D<float4> t0 : register(t0);

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
    float resolutionScale = 1.0f;
    if (LumaData.GameData.IsUpscaling != 0)
    {
        resolutionScale = LumaData.RenderResolutionScale.x;
    }
    float2 _46 = float2(mad(cb0_m1.x * resolutionScale, -0.5f, TEXCOORD.x), mad(cb0_m1.y * resolutionScale, -0.5f, TEXCOORD.y));
    float4 _49 = t0.GatherRed(s0, _46);
    float4 _55 = t0.GatherGreen(s0, _46);
    SV_TARGET.x = max(_49.w, max(_49.y, max(_49.x, _49.z)));
    SV_TARGET.y = min(_55.w, min(_55.y, min(_55.x, _55.z)));
    SV_TARGET.z = 0.0f;
    SV_TARGET.w = 0.0f;
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    TEXCOORD = stage_input.TEXCOORD;
    frag_main();
    SPIRV_Cross_Output stage_output;
    stage_output.SV_TARGET = SV_TARGET;
    return stage_output;
}
