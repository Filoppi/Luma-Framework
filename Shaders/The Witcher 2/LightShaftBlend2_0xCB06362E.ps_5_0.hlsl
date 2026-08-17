// The Witcher 2 light-shaft screen blend, permutation 2 (dgVoodoo -> ps_5_0, hash 0xCB06362E).
// Vanilla: o0 = 1 - (1 - canvas) * (1 - saturate(shafts * tint * saturate(exp2(-3 * lum(canvas)) * fade)))
// — a screen blend whose shaft contribution is attenuated where the canvas is already bright. Unlike the
// sibling glow blend (0x12931281) the canvas itself is NOT saturated here, but the screen formula still damps
// any canvas value above 1 by (1 - shaftPart): HDR highlights sink up to ~30% wherever god rays overlap them.
// Replacement: vanilla math verbatim (bit-exact at any range), then, in HDR only, undo that damping on the
// above-1 part of the canvas (identity <= 1), matching how the glow blend is treated.
//
// A third permutation of this same source exists and is deliberately NOT replaced: 0x262CAE95, identical
// instruction for instruction except that it combines ADDITIVELY (o0.xyz = canvas + shaftPart) instead of
// screen-blending. Additive never damps the canvas, so the above-1 restore below would be a no-op there and
// vanilla is already correct in HDR. Found by a full fingerprint sweep of the 592-shader dump; that sweep
// also confirmed there is no fourth grade permutation and no SSAO-generator permutation.

#include "Includes/Common.hlsl" // game-local: LumaSettings (DisplayMode gates the HDR-only excess restore)

Texture2D<float4> t0 : register(t0); // light-shaft / glow source
Texture2D<float4> t1 : register(t1); // scene canvas (fp16, gamma-space; carries Luma HDR range > 1)

SamplerState s0_s : register(s0);
SamplerState s1_s : register(s1);

cbuffer cb3 : register(b3)
{
   float4 cb3[77];
}
cbuffer cb4 : register(b4)
{
   float4 cb4[236];
}

// dgVoodoo texture-format fixup masks (see Luma_TW2_Tonemap.hlsl) — transcribed verbatim.
float4 DgVoodooTexFixup(float4 color, float4 mask_and, float4 mask_or)
{
   return asfloat((asuint(color) & asuint(mask_and)) | asuint(mask_or));
}

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
   float4 canvas = DgVoodooTexFixup(t1.Sample(s1_s, v6.xy), cb3[46], cb3[47]);
   float lum = dot(canvas.rgb, float3(0.30, 0.59, 0.11));
   // DXBC "exp" is base-2 (the vanilla disasm multiplies by exactly -3, with no folded log2(e) factor), so
   // this must be exp2: HLSL exp() would attenuate the shafts up to 60% too hard on a bright canvas.
   float atten = saturate(exp2(lum * -3.0) * cb4[62].w);

   float4 shafts = DgVoodooTexFixup(t0.Sample(s0_s, v5.xy), cb3[44], cb3[45]);
   float3 shaftPart = saturate(atten * (shafts.rgb * cb4[62].xyz));

   // Vanilla screen blend on the RAW canvas (this permutation does not saturate it, unlike 0x12931281), so
   // this line alone is bit-exact vanilla for any input, in range or above it.
   float3 blended = 1.0 - (1.0 - canvas.rgb) * (1.0 - shaftPart);

#if TONEMAP_TYPE == 1
   // Undo the (1 - shaftPart) damping on the part of the canvas that sits above 1: vanilla sinks HDR
   // highlights by up to ~30% wherever god rays overlap them. Identity for any canvas <= 1.
   // HDR display path only. The SDR one has to see what vanilla produced, and not because the composition
   // clips later anyway: this pass runs BEFORE the final grade, whose saturated-luma tint weights and
   // per-channel gamma pow react non-linearly to an above-1 input, so the excess would move graded values
   // that end up BELOW 1 as well. "== 1" also keeps the dev "SDR on HDR" mode honest — composition only
   // clamps for DisplayMode 0, so there the excess would never be clipped downstream either.
   [branch] if (LumaSettings.DisplayMode == 1)
       blended += shaftPart * max(canvas.rgb - 1.0, 0.0);
#endif

   o0 = float4(blended, 1.0); // vanilla alpha: 1
}
