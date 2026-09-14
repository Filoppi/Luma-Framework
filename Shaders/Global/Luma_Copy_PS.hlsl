#if MS
Texture2DMS<float4> sourceTexture : register(t0);
#else
Texture2D<float4> sourceTexture : register(t0);
#endif

// Custom Luma shader to copy a texture into another one (when e.g. they are of a different format but with matching size)
float4 main(
	float4 pos : SV_Position
#if MS
	, uint sampleIndex : SV_SampleIndex
#endif
	) : SV_Target0
{
#if 0 // TEST
	float4 color = float4(1, pos.y / 1000, 0, 1);
#elif MS
	float4 color = sourceTexture.Load((int2)pos.xy, sampleIndex);
#else
	float4 color = sourceTexture.Load((int3)pos.xyz);
#endif

	// These will also turn NaNs into 0
#if RGB_SAT 
	color.rgb = saturate(color.rgb);
#elif RGB_MAX_0
	color.rgb = max(color.rgb, 0.0);
#endif
#if A_SAT 
	color.a = saturate(color.a);
#elif A_MAX_0
	color.a = max(color.a, 0.0);
#endif

	return color;
}