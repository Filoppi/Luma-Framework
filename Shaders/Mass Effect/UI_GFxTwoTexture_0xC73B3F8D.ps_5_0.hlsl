// Mass Effect (2007) - GFx two-texture fill (gradient/bitmap morph, COLOR1.z is the weight of the first texture). See
// Includes/GFxUI.hlsl: vanilla body plus the 8-bit canvas clamp.
#include "Includes/GFxUI.hlsl"

Texture2D<float4> t0 : register(t0);
Texture2D<float4> t1 : register(t1);
SamplerState s0_s : register(s0);
SamplerState s1_s : register(s1);

// 13 interpolators, all declared in order - linkage is by REGISTER (see Luma_ME1_Tonemap.hlsl). This shader reads
// COLOR1.z (v3.z), TEXCOORD0 (v5.xy) and TEXCOORD1 (v6.xy).
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
   float4 first = ApplyDgvMask(t0.Sample(s0_s, v5.xy), DgvMaskT0, DgvFillT0);
   float4 second = ApplyDgvMask(t1.Sample(s1_s, v6.xy), DgvMaskT1, DgvFillT1);
   o0 = saturate(GFxCxform(lerp(second, first, saturate(v3).z)));
}
