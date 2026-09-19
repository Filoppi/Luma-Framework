// Mass Effect (2007) - UE3 DOFAndBloomBlend, the standalone DoF/bloom composite of chains without an uber
// (Space_PostProcess_DOF2, the default UI/thumbnail chains). Vanilla mix via ME1SceneMix plus the Luma glow, which only
// the uber composited otherwise: the replaced gather drops the native glow. Leaves LINEAR light and depth in alpha for
// the gamma pass, which runs the HDR block itself on uber-less frames.
#include "Luma_ME1_Tonemap.hlsl"

// 13 interpolators, all declared in order - linkage is by REGISTER (see Luma_ME1_Tonemap.hlsl).
void main(
    float4 v0 : SV_POSITION0,
    float4 v1 : TEXCOORD8,
    float4 v2 : COLOR0,
    float4 v3 : COLOR1,
    float4 v4 : TEXCOORD9,
    float4 v5 : TEXCOORD0,
    float4 v6 : TEXCOORD1,
    float4 v7 : TEXCOORD2,
    float4 v8 : TEXCOORD3,
    float4 v9 : TEXCOORD4,
    float4 v10 : TEXCOORD5,
    float4 v11 : TEXCOORD6,
    float4 v12 : TEXCOORD7,
    out float4 o0 : SV_TARGET0)
{
   float sceneDepth;
   const float4 mix = ME1SceneMix(v5.xy, v6.xy, sceneDepth);
   o0 = float4(mix.xyz * rcp(max(mix.w, 0.001)), sceneDepth);
}
