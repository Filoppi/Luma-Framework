//FFXV_Directional_Light_CSM_AO.hlsl 0x4B8E0FF8

//|||||||||||||||||||||||||| CONFIGURATION ||||||||||||||||||||||||||
//|||||||||||||||||||||||||| CONFIGURATION ||||||||||||||||||||||||||
//|||||||||||||||||||||||||| CONFIGURATION ||||||||||||||||||||||||||

//NOTE: by default, the game applies the combined ambient occlusion (SSAO * VXAO) to the main directional light CSM shadow buffer
//while this generally helps and could be an artistic call by the developers to add in more shading for areas in direct sunlight, this is technically inaccurate.
//for better accuracy, occlusion (Material AO/SSAO/VXAO/etc.) needs to be applied to ambient lighting ONLY (hence the name, ambient occlusion)
//applying AO to surfaces that are being lit by direct light can lead to over-occlusion and extra shadowing that shouldn't be there especially when it's being directly lit by a light source
//the only occlusion that should happen from direct lighting sources is from actual shadows (shadowmaps, contact shadows, micro shadows), not from AO terms
//#define APPLY_AO_TO_DIRECTIONAL_LIGHT

//|||||||||||||||||||||||||| STRUCTS ||||||||||||||||||||||||||
//|||||||||||||||||||||||||| STRUCTS ||||||||||||||||||||||||||
//|||||||||||||||||||||||||| STRUCTS ||||||||||||||||||||||||||

struct InputStruct 
{
	float4 Position : SV_Position;
	float2 param1 : TEXCOORD;
};

//|||||||||||||||||||||||||| SHADER RESOURCES ||||||||||||||||||||||||||
//|||||||||||||||||||||||||| SHADER RESOURCES ||||||||||||||||||||||||||
//|||||||||||||||||||||||||| SHADER RESOURCES ||||||||||||||||||||||||||

//SamplerComparisonState linearClampSampler : register(s0); // can't disambiguate
//SamplerComparisonState pointClampSampler : register(s1); // can't disambiguate

SamplerState linearClampSampler : register(s0); // can't disambiguate
SamplerState pointClampSampler : register(s1); // can't disambiguate

Texture2D<float> g_depthTex : register(t0);
Texture2D<float> g_sunAOMask : register(t1);

//|||||||||||||||||||||||||| SHADER CBUFFERS ||||||||||||||||||||||||||
//|||||||||||||||||||||||||| SHADER CBUFFERS ||||||||||||||||||||||||||
//|||||||||||||||||||||||||| SHADER CBUFFERS ||||||||||||||||||||||||||

cbuffer cbFoliageAOUpsample : register(b0) 
{
	float4 g_fullscreenDims : packoffset(c0.x);
	float4 g_unprojectParams : packoffset(c1.x);
};

//|||||||||||||||||||||||||| BRDF ||||||||||||||||||||||||||
//|||||||||||||||||||||||||| BRDF ||||||||||||||||||||||||||
//|||||||||||||||||||||||||| BRDF ||||||||||||||||||||||||||

float main(in InputStruct IN) : SV_Target0
{
	#if defined(APPLY_AO_TO_DIRECTIONAL_LIGHT)
		return g_sunAOMask.SampleLevel(linearClampSampler, IN.param1, 0);
	#else
		return 1.0f; //return no occlusion value
	#endif
}
