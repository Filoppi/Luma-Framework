// rl_prim_2d_bink_s_01: Bink video, BT.601 limited-range Y'CbCr -> R'G'B'.
// Fullscreen movies draw it straight into the swapchain as the frame's only draw (no compose runs), where 1 = UI paper white
// like the GUI layer; menu videos go into the RGBA8 GUI layer and stay vanilla. main.cpp sets LumaData.CustomData1 to 1 only for the
// swapchain (FP16) target, the only one that keeps AutoHDR highlights above 1.
// The AutoHDR stays conservative because the videos are low bitrate and compression artifacts blow up when pushed hard.

#include "Includes/Common.hlsl"

cbuffer CB_PIXEL : register(b3)
{
   float Alpha_test_ref : packoffset(c0.x);
   float4 Tint_color : packoffset(c10);
}

SamplerState Sampler_Aniso_WW : register(s1);
Texture2D<float4> Y_texTexture : register(t0);
Texture2D<float4> Cr_texTexture : register(t1);
Texture2D<float4> Cb_texTexture : register(t2);

void main(float4 pos : SV_Position, float2 uv : TEXCOORD0, float4 vertexColor : COLOR0, out float4 o0 : SV_Target0)
{
   const float y = Y_texTexture.Sample(Sampler_Aniso_WW, uv).x;
   const float cr = Cr_texTexture.Sample(Sampler_Aniso_WW, uv).x;
   const float cb = Cb_texTexture.Sample(Sampler_Aniso_WW, uv).x;
   float4 color = float4(YUVtoRGB(y, cr, cb, 3), 1.0) * vertexColor; // BT.601 limited range, the vanilla coefficients
   if (color.a * Tint_color.a - Alpha_test_ref < 0.0)
      discard;
   // The vanilla 8-bit targets clamped the conversion (negatives and whites up to 1.25)
   color = saturate(color * Tint_color);

   [branch] if (LumaData.CustomData1 != 0 && LumaSettings.DisplayMode == 1 && LumaSettings.GameSettings.VideoAutoHDREnable > 0.5)
   {
      // "Boost" 0 puts the AutoHDR peak at sRGB white (a no-op), 1 at 250 nits
      const float peakNits = lerp(sRGB_WhiteLevelNits, 250.0, saturate(LumaSettings.GameSettings.VideoAutoHDRBoost));
      color.rgb = linear_to_gamma(PumboAutoHDR(gamma_to_linear(color.rgb), peakNits, LumaSettings.UIPaperWhiteNits), GCT_NONE);
   }
   o0 = color;
}
