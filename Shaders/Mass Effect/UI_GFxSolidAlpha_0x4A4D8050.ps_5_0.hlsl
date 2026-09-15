// Mass Effect (2007) - GFx vertex-colour fill with a second per-vertex alpha (COLOR1.w). See Includes/GFxUI.hlsl:
// vanilla body plus the 8-bit canvas clamp.
#include "Includes/GFxUI.hlsl"

// 13 interpolators, all declared in order - linkage is by REGISTER (see Luma_ME1_Tonemap.hlsl). This shader reads
// COLOR0 (v2) and COLOR1.w (v3.w).
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
   float4 color = GFxCxform(saturate(v2));
   color.w *= saturate(v3.w);
   o0 = saturate(color);
}
