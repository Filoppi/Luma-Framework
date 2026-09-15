// Mass Effect (2007) - GFx textured fill blended against the vertex colour (COLOR1.z is the texture weight, COLOR1.w
// a second alpha). The most common HUD shader. See Includes/GFxUI.hlsl: vanilla body plus the 8-bit canvas clamp.
#include "Includes/GFxUI.hlsl"

Texture2D<float4> t0 : register(t0);
SamplerState s0_s : register(s0);

// 13 interpolators, all declared in order - linkage is by REGISTER (see Luma_ME1_Tonemap.hlsl). This shader reads
// COLOR0 (v2), COLOR1 (v3) and TEXCOORD0 (v5.xy).
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
   float4 texel = ApplyDgvMask(t0.Sample(s0_s, v5.xy), DgvMaskT0, DgvFillT0);
   float4 factors = saturate(v3);
   float4 color = GFxCxform(lerp(saturate(v2), texel, factors.z));
   color.w *= factors.w;
   o0 = saturate(color);
}
