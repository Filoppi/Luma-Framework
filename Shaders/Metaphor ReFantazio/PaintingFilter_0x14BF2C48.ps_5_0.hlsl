#include "Includes/Common.hlsl"

cbuffer GFD_PSCONST_SYSTEM : register(b0)
{
	float2 resolution : packoffset(c0);
	float2 resolutionRev : packoffset(c0.z);
	float4x4 mtxView : packoffset(c1);
	float4x4 mtxInvView : packoffset(c5);
	float4x4 mtxProj : packoffset(c9);
	float4x4 mtxInvProj : packoffset(c13);
	float4 invProjParams : packoffset(c17);
}

cbuffer GFD_PSCONST_TEMPERARE : register(b12)
{
	float wobbFocalPlane : packoffset(c0);
	float wobbNearRangeRev : packoffset(c0.y);
	float wobbFarRangeRev : packoffset(c0.z);
	float wobbFarBlurLimit : packoffset(c0.w);
	float wobbpower : packoffset(c1);
	float wobbscale : packoffset(c1.y);
	float edgeFocalPlane : packoffset(c1.z);
	float edgeNearRangeRev : packoffset(c1.w);
	float edgeFarRangeRev : packoffset(c2);
	float edgeFarBlurLimit : packoffset(c2.y);
	float edgesize : packoffset(c2.z);
	float edgepower : packoffset(c2.w);
	float3 edgecolor : packoffset(c3);
	float edgelimit : packoffset(c3.w);
	float edgesize_sky : packoffset(c4);
	float edgepower_sky : packoffset(c4.y);
	float edgelimit_sky : packoffset(c4.z);
}

SamplerState linearSampler_s : register(s0);
SamplerState pointSampler_s : register(s1);
Texture2D<float4> paintlyTexture : register(t0);
Texture2D<float4> gbuffer1Texture : register(t3);

void main(
	float4 v0 : SV_POSITION0,
	float2 v1 : TEXCOORD0,
	out float4 o0 : SV_Target0)
{
	float4 r0,r1,r2,r3,r4;
	uint4 bitmask, uiDest;
	float4 fDest;

	r0.xyzw = paintlyTexture.Sample(pointSampler_s, v1.xy).xyzw;
	r1.x = r0.w == -1.000000;
	if (r1.x != 0) discard;
	r1.x = r0.w == 0.000000;
	if (r1.x != 0) {
		o0.xyz = r0.xyz;
		o0.w = 0;
		return;
	}
	r1.xy = resolution.xy * v1.xy;
	r1.xy = (int2)r1.xy;
	r1.zw = float2(0,0);
	r1.x = gbuffer1Texture.Load(r1.xyz).w;
	r1.x = 255 * r1.x;
	r1.x = (uint)r1.x;
	r1.x = (int)r1.x & 128;
	if (r1.x == 0) {
		// scale by resolution of console version for consistent intensity
		r1.xy = edgesize * pow(resolution.y / 1656.0f, 0.5) * resolutionRev.xy;
		r2.x = -r1.x;
		r2.z = 0;
		r2.xy = v1.xy + r2.xz;
		r2.xyz = paintlyTexture.Sample(linearSampler_s, r2.xy).xyz;
		r1.z = 0;
		r3.xyzw = v1.xyxy + r1.xzzy;
		r4.xyz = paintlyTexture.Sample(linearSampler_s, r3.xy).xyz;
		r1.w = -r1.y;
		r1.xy = v1.xy + r1.zw;
		r1.xyz = paintlyTexture.Sample(linearSampler_s, r1.xy).xyz;
		r3.xyz = paintlyTexture.Sample(linearSampler_s, r3.zw).xyz;
		r2.xyz = r4.xyz + -r2.xyz;
		r1.xyz = -r3.xyz + r1.xyz;
		r1.xyz = abs(r2.xyz) + abs(r1.xyz);
		r1.x = r1.x + r1.y;
		r1.x = r1.x + r1.z;
		r1.x = min(edgelimit, r1.x);
		r1.x = saturate(0.333000004 * r1.x);
		r1.x = edgepower * r1.x + 1;
	} else {
		r2.xy = edgesize_sky * pow(resolution.y / 1656.0f, 0.5) * resolutionRev.xy;
		r3.x = -r2.x;
		r3.z = 0;
		r1.yz = v1.xy + r3.xz;
		r1.yzw = paintlyTexture.Sample(linearSampler_s, r1.yz).xyz;
		r2.z = 0;
		r3.xyzw = v1.xyxy + r2.xzzy;
		r4.xyz = paintlyTexture.Sample(linearSampler_s, r3.xy).xyz;
		r2.w = -r2.y;
		r2.xy = v1.xy + r2.zw;
		r2.xyz = paintlyTexture.Sample(linearSampler_s, r2.xy).xyz;
		r3.xyz = paintlyTexture.Sample(linearSampler_s, r3.zw).xyz;
		r1.yzw = r4.xyz + -r1.yzw;
		r2.xyz = -r3.xyz + r2.xyz;
		r1.yzw = abs(r2.xyz) + abs(r1.yzw);
		r1.y = r1.y + r1.z;
		r1.y = r1.y + r1.w;
		r1.y = log2(r1.y);
		r1.y = 2.5 * r1.y;
		r1.y = exp2(r1.y);
		r1.y = min(edgelimit_sky, r1.y);
		r1.y = saturate(0.333000004 * r1.y);
		r1.x = edgepower_sky * r1.y + 1;
	}
	r1.yzw = -r0.xyz * r0.xyz + r0.xyz;
	r1.x = -1 + r1.x;
	r1.yzw = saturate(-r1.yzw * r1.xxx + r0.xyz);
	r1.yzw = -r1.yzw + r0.xyz;
	r1.x = saturate(r1.x);
	r1.x = r1.x * r0.w;
	r1.yzw = edgecolor.xyz * r1.yzw + -r0.xyz;
	o0.xyz = r1.xxx * r1.yzw + r0.xyz;
	o0.w = r0.w;
	return;
}