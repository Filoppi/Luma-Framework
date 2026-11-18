// MELE AO apply (a global shader, the same hash in ME1/ME2/ME3): the AO target, multiplied into the scene by the blend
// (dst * src color). Native output is the AO in rgb and 1 in alpha.
//
// The scene UV (v1) reaches only the rendered share of the AO target below native render scale, which is all the untouched HBAO+
// fills. XeGTAO or the stretched HBAO+ fill the whole target instead (main.cpp, "RunAO"), so the UV is scaled onto it by
// LumaData.CustomData3/4 (output size over render size, 1 for the untouched chain and at native).

#include "Includes/Common.hlsl"

SamplerState AOTextureSampler_s : register(s0);
Texture2D<float4> AOTexture : register(t0);

void main(
    float4 v0 : SV_Position0,
    float2 v1 : TEXCOORD0,
    out float4 o0 : SV_Target0)
{
   o0.xyz = AOTexture.Sample(AOTextureSampler_s, v1 * float2(LumaData.CustomData3, LumaData.CustomData4)).x;
   o0.w = 1.0;
}
