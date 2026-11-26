// Generic sprite PS (video frames and UI sprites). Faithful translation of 0xD5C6F8E3.ps_5_0.cso.
// Luma: with TONEMAP_TYPE >= 1 the video draw (VideoDrawActive, set by c++) gets HDR highlight expansion (AutoHDR), scaled by VideoAutoHDR.

#include "Includes/Common.hlsl"
#include "../Includes/ColorGradingLUT.hlsl"

Texture2D<float4> t0 : register(t0); // Video frame (or sprite texture)

SamplerState s0_s : register(s0);

cbuffer cb4 : register(b4)
{
  float4 cb4[236];
}

void main(
  // Full original input signature: D3D11 links PS inputs by register, so unused ones must stay declared
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
  float4 r0 = t0.Sample(s0_s, v5.xy);
  float4 r2 = saturate(v3);
  float4 r3 = saturate(v2);

  // Vanilla: lerp between the vertex colors and the texture
  float4 r1;
  r1.xyz = (r0.xyz - r3.xyz) * r2.xyz + r3.xyz;
  r1.w = (r0.w - r3.w) * r2.z + r3.w;
  r1 = r1 * cb4[2] + cb4[3];

  float alpha = r1.w * r2.w;
  float3 color = r1.xyz;

#if TONEMAP_TYPE >= 1
  if (LumaSettings.DisplayMode == 1 && LumaSettings.GameSettings.VideoDrawActive > 0 && LumaSettings.GameSettings.VideoAutoHDREnable > 0 && LumaSettings.GameSettings.VideoAutoHDR > 0)
  {
    // PumboAutoHDR caps at MaxPeakWhiteNits / sRGB_WhiteLevelNits: pass the target as a paper white multiple so 1 = display peak.
    float3 rgb = max(color, 0.0);
    float3 lin = pow(rgb, 2.2);
    float targetNits = lerp(LumaSettings.GamePaperWhiteNits, LumaSettings.PeakWhiteNits, LumaSettings.GameSettings.VideoAutoHDR);
    lin = PumboAutoHDR(lin, targetNits / LumaSettings.GamePaperWhiteNits * sRGB_WhiteLevelNits, LumaSettings.GamePaperWhiteNits);
#if UI_DRAW_TYPE == 2 // The UI draws on top in gamma space; display composition scales everything by UI/game paper white, so pre-divide the video
    lin *= LumaSettings.GamePaperWhiteNits / LumaSettings.UIPaperWhiteNits;
#endif
    // Inverse of Display Composition's decode (gamma 2.2 in 0-1, sRGB above 1), as in the tonemap, so the peak lands exactly
    color = ColorGradingLUTTransferFunctionInCorrected(lin, GAMMA_CORRECTION_TYPE, VANILLA_ENCODING_TYPE);
    // Premultiply by alpha (no saturate on color, so HDR values survive)
    o0 = float4(color * alpha, saturate(alpha));
  }
  else
#endif
  {
    o0 = saturate(float4(color * alpha, alpha));
  }
}
