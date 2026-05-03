#include "../Includes/Common.hlsl"
#include "../Includes/ColorGradingLUT.hlsl"
#include "../Includes/DICE.hlsl"

cbuffer PostFxSimple : register(b0)
{
  float4 Color : packoffset(c0);
  float4 QuadParams : packoffset(c1);
  float4 UVScaleOffset : packoffset(c2);
  float3 GammaBrightnessContrastParams : packoffset(c3);
  float2 Tiling : packoffset(c4);
}

// Names from 0x57A47ED1
SamplerState PostFxSimple__TextureSampler__SampObj___s : register(s0); // "PostFxSimple__FilteredTextureSampler__SampObj___s" with 0x5E003A74
Texture2D<float4> PostFxSimple__TextureSampler__TexObj__ : register(t0); // "PostFxSimple__FilteredTextureSampler__TexObj__" with 0x5E003A74

float LinearToSRGB( float v )
{
    return ( v <= 0.0031308f ) ? 12.92f*v : 1.055f*pow( abs( v ), 1.0f/2.4f ) - 0.055f;
}

float3 LinearToSRGB( float3 v )
{
    return float3( LinearToSRGB( v.x ), LinearToSRGB( v.y ), LinearToSRGB( v.z ) );
}

// This is always called after rendering and post process, while copying the output on the swapchain, before UI starts drawing on it
void main(
  linear centroid float2 v0 : TEXCOORD0,
  float4 v1 : SV_Position0,
  out float4 o0 : SV_Target0)
{
  o0.rgba = PostFxSimple__TextureSampler__TexObj__.Sample(PostFxSimple__TextureSampler__SampObj___s, v0.xy).xyzw;
  o0.rgb = pow(abs(o0.rgb), GammaBrightnessContrastParams.x) * sign(o0.rgb); // Luma: fixed negative values support
  o0.rgb = o0.rgb * GammaBrightnessContrastParams.y + GammaBrightnessContrastParams.z; // These are best left at neutral default (1 and 0)
  
#if POST_PROCESS_SPACE_TYPE == 0
  o0.rgb = LinearToSRGB(o0.rgb);
#endif

  // Luma: display mapping. Do it here because it's the "final" shader before UI, afte AA etc
  if (LumaSettings.DisplayMode == 1)
  {
    const float paperWhite = LumaSettings.GamePaperWhiteNits / sRGB_WhiteLevelNits;
    const float peakWhite = LumaSettings.PeakWhiteNits / sRGB_WhiteLevelNits;
    DICESettings settings = DefaultDICESettings(DICE_TYPE_BY_LUMINANCE_PQ_CORRECT_CHANNELS_BEYOND_PEAK_WHITE);
    o0.rgb = DICETonemap(o0.rgb * paperWhite, peakWhite, settings) / paperWhite;
  }

#if UI_DRAW_TYPE == 2
	ColorGradingLUTTransferFunctionInOutCorrected(o0.rgb, VANILLA_ENCODING_TYPE, GAMMA_CORRECTION_TYPE, true);
  o0.rgb *= LumaSettings.GamePaperWhiteNits / LumaSettings.UIPaperWhiteNits;
	ColorGradingLUTTransferFunctionInOutCorrected(o0.rgb, GAMMA_CORRECTION_TYPE, VANILLA_ENCODING_TYPE, true);
#endif
}