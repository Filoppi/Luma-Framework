#include "Includes/Common.hlsl"

cbuffer cbDof : register(b0)
{
    float4 unprojectParam : packoffset(c0);
    float4 CoCParam0 : packoffset(c1);
    float4 CoCParam1 : packoffset(c2);
    float4 CoCParam2 : packoffset(c3);
    float4 CoCParam3 : packoffset(c4);
    float4 fullscreenDims : packoffset(c5);
    float4 vignetteBlurParam0 : packoffset(c6);
    float4 vignetteBlurParam1 : packoffset(c7);
    float4 uvJitterOffset : packoffset(c8);
    float4x4 motionMatrix : packoffset(c9);
    float blendBokeh_param0 : packoffset(c13);
    float blendBokeh_param1 : packoffset(c13.y);
    float gamePaused : packoffset(c13.z);
    float spare : packoffset(c14);
}

#define cmp -

void main(
    float4 v0: POSITION0,
    float2 v1: TEXCOORD0,
    out float4 o0: SV_POSITION0,
    out float2 o1: TEXCOORD0,
    out float2 p1: TEXCOORD1,
    out float2 o2: TEXCOORD2)
{
    float4 r0;
    uint4 bitmask, uiDest;
    float4 fDest;

    float resolutionScale = 1.f;
    if (LumaData.GameData.IsUpscaling != 0)
    {
        resolutionScale = LumaData.RenderResolutionScale.x;
    }

    o0.xyzw = v0.xyzw;
    o1.xy = v1.xy;
    p1.xy = fullscreenDims.zw * resolutionScale * float2(-0.5, -0.5) + v1.xy;
    r0.xy = v1.xy * float2(2, 2) + float2(-1, -1);
    o2.xy = vignetteBlurParam0.xy * r0.xy;
    return;
}