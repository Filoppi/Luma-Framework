struct postfx_luminance_autoexposure_t
{
	float EngineLuminanceFactor;   // Offset:    0
	float LuminanceFactor;         // Offset:    4
	float MinLuminanceLDR;         // Offset:    8
	float MaxLuminanceLDR;         // Offset:   12
	float MiddleGreyLuminanceLDR;  // Offset:   16
	float EV;                      // Offset:   20
	float Fstop;                   // Offset:   24
	uint PeakHistogramValue;       // Offset:   28
};

cbuffer PerInstanceCB : register(b2)
{
	float4 cb_positiontoviewtexture : packoffset(c0);
	float4 cb_taatexsize : packoffset(c1);
	float4 cb_taaditherandviewportsize : packoffset(c2);
	float4 cb_postfx_tonemapping_tonemappingparms : packoffset(c3);
	float4 cb_postfx_tonemapping_tonemappingcoeffsinverse1 : packoffset(c4);
	float4 cb_postfx_tonemapping_tonemappingcoeffsinverse0 : packoffset(c5);
	float4 cb_postfx_tonemapping_tonemappingcoeffs1 : packoffset(c6);
	float4 cb_postfx_tonemapping_tonemappingcoeffs0 : packoffset(c7);
	uint2 cb_postfx_luminance_exposureindex : packoffset(c8);
	float2 cb_prevresolutionscale : packoffset(c8.z);
	float cb_env_tonemapping_white_level : packoffset(c9);
	float cb_view_white_level : packoffset(c9.y);
	float cb_taaamount : packoffset(c9.z);
	float cb_postfx_luminance_customevbias : packoffset(c9.w);
}

cbuffer PerViewCB : register(b1)
{
	float4 cb_alwaystweak : packoffset(c0);
	float4 cb_viewrandom : packoffset(c1);
	float4x4 cb_viewprojectionmatrix : packoffset(c2);
	float4x4 cb_viewmatrix : packoffset(c6);
	float4 cb_subpixeloffset : packoffset(c10);
	float4x4 cb_projectionmatrix : packoffset(c11);
	float4x4 cb_previousviewprojectionmatrix : packoffset(c15);
	float4x4 cb_previousviewmatrix : packoffset(c19);
	float4x4 cb_previousprojectionmatrix : packoffset(c23);
	float4 cb_mousecursorposition : packoffset(c27);
	float4 cb_mousebuttonsdown : packoffset(c28);
	float4 cb_jittervectors : packoffset(c29);
	float4x4 cb_inverseviewprojectionmatrix : packoffset(c30);
	float4x4 cb_inverseviewmatrix : packoffset(c34);
	float4x4 cb_inverseprojectionmatrix : packoffset(c38);
	float4 cb_globalviewinfos : packoffset(c42);
	float3 cb_wscamforwarddir : packoffset(c43);
	uint cb_alwaysone : packoffset(c43.w);
	float3 cb_wscamupdir : packoffset(c44);
	uint cb_usecompressedhdrbuffers : packoffset(c44.w);
	float3 cb_wscampos : packoffset(c45);
	float cb_time : packoffset(c45.w);
	float3 cb_wscamleftdir : packoffset(c46);
	float cb_systime : packoffset(c46.w);
	float2 cb_jitterrelativetopreviousframe : packoffset(c47);
	float2 cb_worldtime : packoffset(c47.z);
	float2 cb_shadowmapatlasslicedimensions : packoffset(c48);
	float2 cb_resolutionscale : packoffset(c48.z);
	float2 cb_parallelshadowmapslicedimensions : packoffset(c49);
	float cb_framenumber : packoffset(c49.z);
	uint cb_alwayszero : packoffset(c49.w);
}

StructuredBuffer<postfx_luminance_autoexposure_t> ro_postfx_luminance_buffautoexposure : register(t0);
RWTexture2D<float4> uav : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
	// From the game's native TAA.
	const float r0z = ro_postfx_luminance_buffautoexposure[cb_postfx_luminance_exposureindex.y].EngineLuminanceFactor;
	const float r1w = cb_view_white_level * r0z; // Compression factor?

	float4 color = uav[dtid.xy];

	// As done in the game's native TAA.
	color.rgb = cb_usecompressedhdrbuffers ? color.rgb * rcp(r1w) : color.rgb;

	uav[dtid.xy] = color;
}