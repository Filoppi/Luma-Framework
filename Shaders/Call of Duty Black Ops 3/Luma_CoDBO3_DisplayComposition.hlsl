#include "./Includes/Common.hlsl"

Texture2D<float4> sourceTexture : register(t0);
Texture2D<float4> debugTexture : register(t2); // DEVELOPMENT
Texture2D<float4> sourceTextureHDR10 : register(t10);

// rect is top left (x,y), bottom right (x,y)
float3 DrawRect(float2 uv, float4 rect, float3 x, float3 rectColor) {
	float r = step(rect.x, uv.x) * step(uv.x, rect.z) * step(rect.y, uv.y) * step(uv.y, rect.w);
	if (r == 0) return x;
	return rectColor;
}

float4 main(float4 pos : SV_Position) : SV_Target0
{
#if OUTPUT_MODE == 0
	float4 x = sourceTexture.Load((int3)pos.xyz);
#elif OUTPUT_MODE == 1
  float4 x = sourceTextureHDR10.Load((int3)pos.xyz);
#elif OUTPUT_MODE == 2
	float4 x = sourceTexture.Load((int3)pos.xyz);
#endif

	float p = HDR_PEAK;
	float intScaling = HDR_INTSCALING;
	float2 uv = pos.xy * LumaSettings.SwapchainInvSize;
	
#if CUSTOM_HDTVREC709 > 0
	float3 rec709Sign = Sign_Fast(x.rgb);
	x.rgb = abs(x.rgb);
	x.rgb = gamma_sRGB_to_linear(x.rgb, GCT_NONE);
	x.rgb = rec709Sign * EncodeRec709(x.rgb);
#endif

#if GAMMA_CORRECTION_TYPE == 0
  x.rgb = gamma_sRGB_to_linear(x.rgb, GCT_MIRROR);
#else
  x.rgb = gamma_to_linear(x.rgb, GCT_MIRROR);
#endif

  x.rgb /= intScaling;

#if SWAPCHAIN_CLAMP_PEAK == 1
	x = min(x, p);
#elif SWAPCHAIN_CLAMP_PEAK == 2
  {
    float m = max(x.x, max(x.y, x.z));
		if (m > p) x *= p / m;
	}
#endif

#if SWAPCHAIN_TEST_USER_PEAK > 0
	//black
  x.rgb = 0;
  x.rgb = DrawRect(uv, float4(0.35, 0.47, 0.65, 0.53), x.rgb, 10000.f / GamePaperWhiteNits);
  x.rgb = DrawRect(uv, float4(0.365, 0.483, 0.448, 0.517), x.rgb, PeakWhiteNits * 2 / GamePaperWhiteNits);
  x.rgb = DrawRect(uv, float4(0.458, 0.483, 0.542, 0.517), x.rgb, PeakWhiteNits / GamePaperWhiteNits);
  x.rgb = DrawRect(uv, float4(0.552, 0.483, 0.635, 0.517), x.rgb, PeakWhiteNits / 2 / GamePaperWhiteNits);
#endif

#if OUTPUT_MODE == 0
	// scRGB encode
	x.rgb *= intScaling * (UIPaperWhiteNits / 80.f);
#elif OUTPUT_MODE == 1
  // HDR10 encode
  x.xyz = BT709_To_BT2020(x.xyz);
  x.xyz = max(0, x.xyz);
  x.xyz *= intScaling * (UIPaperWhiteNits / HDR10_MaxWhiteNits);
	x.xyz = Linear_to_PQ(x.xyz);
#elif OUTPUT_MODE == 2
  x.xyz = linear_to_sRGB_gamma(x.xyz, GCT_POSITIVE);
#endif

	return float4(x.rgb, x.a);
}

// float4 rec709override(float2 v0 : TEXCOORD0, float4 v1 : SV_POSITION0) : SV_Target0
// {
//   return sourceTexture.Load((int3)v1.xyz);
// }