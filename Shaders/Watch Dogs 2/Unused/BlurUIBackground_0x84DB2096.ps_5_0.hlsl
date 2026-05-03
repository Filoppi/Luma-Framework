#include "../Includes/Common.hlsl"
#include "../Includes/ColorGradingLUT.hlsl"
#include "../Includes/Reinhard.hlsl"

cbuffer BlurProcess : register(b0)
{
  float4 ColorizeColor : packoffset(c0);
  float4 QuadParams : packoffset(c1);
  float4 UVOffsets[9] : packoffset(c2);
  float BilateralDepthTreshold : packoffset(c11);
  float TextureAlpha : packoffset(c11.y);
}

SamplerState BlurProcess__DiffuseSampler__SampObj___s : register(s0);
Texture2D<float4> BlurProcess__DiffuseSampler__TexObj__ : register(t0);

// TODO: find blood overlay shader (unrelated from this!!!)
void main(
  linear centroid float2 v0 : TEXCOORD0,
  float4 v1 : SV_Position0,
  out float4 o0 : SV_Target0)
{
  // The average of these is ~1/8
  const float4 coeffs[] = { { 0.068493, 0, 0, 0},
                              { 0.089041, 0, 0, 0},
                              { 0.123288, 0, 0, 0},
                              { 0.143836, 0, 0, 0},
                              { 0.150685, 0, 0, 0},
                              { 0.143836, 0, 0, 0},
                              { 0.123288, 0, 0, 0},
                              { 0.089041, 0, 0, 0},
                              { 0.068493, 0, 0, 0} };

  float4 blurredColor = 0.0;
  int i = 0;
  // TODO: make sure this blurs the same in UW and at 4k
  while (true) {
    if (i.x >= 9) break; // 8 samples
    float2 uv = v0.xy * UVOffsets[i].zw + UVOffsets[i].xy;
    float4 sceneColor = BlurProcess__DiffuseSampler__TexObj__.Sample(BlurProcess__DiffuseSampler__SampObj___s, uv).xyzw;
    // Tonemap to SDR before blurring (unless we already had), in every iteration, otherwise post blurring could end up below below 1, but still overly bright for the vanilla UI visibility design
    // We do it by luminance otherwise hues behind the UI would shift too much
    if (LumaSettings.DisplayMode == 1)
    {
      sceneColor.rgb = RestoreLuminance(sceneColor.rgb, Reinhard::ReinhardRange(GetLuminance(sceneColor.rgb), MidGray, -1.0, 1.0, false).x, true);
    }
    blurredColor += sceneColor.rgba * coeffs[i].x;
    i++;
  }
  o0.rgba = blurredColor;
  
#if UI_DRAW_TYPE == 2
	ColorGradingLUTTransferFunctionInOutCorrected(o0.rgb, VANILLA_ENCODING_TYPE, GAMMA_CORRECTION_TYPE, true); // TODO: make sure this scales properly on UI
  o0.rgb *= LumaSettings.GamePaperWhiteNits / LumaSettings.UIPaperWhiteNits;
	ColorGradingLUTTransferFunctionInOutCorrected(o0.rgb, GAMMA_CORRECTION_TYPE, VANILLA_ENCODING_TYPE, true);
#endif

  // Note: originally this was sampled in linear from a B8G8R8A8_UNORM_SRGB texture, but written on a B8G8R8A8_UNORM, and also sampled from
  // the same view in the UI, which meant it heavily quantized, due to being written as linear space in a texture format meant for gamma space (the UI was in linear space!).
  // With Luma upgrades, everything becomes R16G16B16A16_FLOAT, so this mismatch doesn't exist.
}