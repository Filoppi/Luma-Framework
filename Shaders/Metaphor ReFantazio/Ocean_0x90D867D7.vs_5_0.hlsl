cbuffer GFD_VSCONST_TRANSFORM : register(b1)
{
	float4x4 mtxLocalToWorld : packoffset(c0);
	float4x4 mtxLocalToWorldViewProj : packoffset(c4);
	float4x4 mtxLocalToWorldViewProjPrev : packoffset(c8);
	float4x4 mtxModelToLocal : packoffset(c12);
}

cbuffer GFD_VSCONST_VIEWPROJ : register(b2)
{
	float4x4 mtxViewProj : packoffset(c0);
	float4x4 mtxView : packoffset(c4);
	float4x4 mtxInvView : packoffset(c8);
	float3 eyePosition : packoffset(c12);
	float fovy : packoffset(c12.w);
}

cbuffer GFD_VSCONST_SHADOW : register(b3)
{
	float4x4 mtxLightViewProj[3] : packoffset(c0);
}

cbuffer GFD_VSCONST_OCEAN_PREV_DATA : register(b5)
{
    float4x4 mtxLocalToWorldPrev;
    float4x4 mtxViewProjPrev;
	float4 TexShiftPrev;
}

cbuffer GFD_VSCONST_OCEAN : register(b7)
{
	float4x4 mtxInvLocalToWorld : packoffset(c0);
	float2 TexShift : packoffset(c4);
	float TCScale : packoffset(c4.z);
	float WaterWidthScale : packoffset(c4.w);
	float WaterHeightScale : packoffset(c5);
	float VertexPitch : packoffset(c5.y);
}

SamplerState linearWrapSampler_s : register(s1);
Texture2D<float4> waterTexture : register(t0);

void main(
	float3 v0 : POSITION0,
	out float4 o0 : SV_POSITION0,
	out float3 o1 : NORMAL0,
	out float4 o2 : TEXCOORD0,
	out float4 o3 : TEXCOORD1,
	out float4 o4 : TEXCOORD2,
	out float4 o5 : TEXCOORD3,
	out float4 o6 : TEXCOORD4,
	out float4 o7 : TEXCOORD5,
	out float4 o8 : TEXCOORD6,
	out float4 o9 : TEXCOORD7,
	out float4 o10 : TEXCOORD8)
{
	float4 r0,r1,r2,r3,r4,r5;
	uint4 bitmask, uiDest;
	float4 fDest;

	r0.xy = float2(-1,1) * TexShift.xy;
	r0.zw = eyePosition.xz / VertexPitch;
	r1.xy = r0.zw >= -r0.zw;
	r0.zw = frac(abs(r0.zw));
	r0.zw = r1.xy ? r0.zw : -r0.zw;
	r1.xyz = v0.xyz;
	r1.w = 1;
	r2.x = dot(r1.xyzw, mtxLocalToWorld._m00_m10_m20_m30);
	r2.z = dot(r1.xyzw, mtxLocalToWorld._m02_m12_m22_m32);
	r0.zw = -r0.zw * VertexPitch + r2.xz;
	r2.xy = TCScale * r0.zw;
	r2.zw = r2.xy * float2(0.00273437495,0.00273437495) + r0.xy;
	r0.xy = r2.xy * float2(0.00535937492,0.00535937492) + r0.xy;
	r3.xyz = waterTexture.SampleLevel(linearWrapSampler_s, r0.xy, 0).xyw;
	r4.xyz = waterTexture.SampleLevel(linearWrapSampler_s, r2.zw, 0).xyw;
	r0.x = -0.5 + r4.z;
	r2.zw = r4.xy * float2(2,2) + float2(-1,-1);
	r4.xy = r2.xy * float2(0.001953125,0.001953125) + TexShift.xy;
	r4.xyz = waterTexture.SampleLevel(linearWrapSampler_s, r4.xy, 0).xyw;
	r0.y = -0.5 + r4.z;
	r4.xy = r4.xy * float2(2,2) + float2(-1,-1);
	r2.zw = r2.zw * float2(0.649999976,0.649999976) + r4.xy;
	r0.x = r0.x * 0.649999976 + r0.y;
	r4.xy = r2.xy * float2(0.00382812484,0.00382812484) + TexShift.xy;
	r2.xy = r2.xy * float2(0.00750312489,0.00750312489) + TexShift.xy;
	r5.xyz = waterTexture.SampleLevel(linearWrapSampler_s, r2.xy, 0).xyw;
	r4.xyz = waterTexture.SampleLevel(linearWrapSampler_s, r4.xy, 0).xyw;
	r0.y = -0.5 + r4.z;
	r2.xy = r4.xy * float2(2,2) + float2(-1,-1);
	r2.xy = r2.xy * float2(0.422499955,0.422499955) + r2.zw;
	r0.x = r0.y * 0.422499955 + r0.x;
	r0.y = -0.5 + r3.z;
	r2.zw = r3.xy * float2(2,2) + float2(-1,-1);
	r2.xy = r2.zw * float2(0.274624974,0.274624974) + r2.xy;
	r0.x = r0.y * 0.274624974 + r0.x;
	r0.y = -0.5 + r5.z;
	r2.zw = r5.xy * float2(2,2) + float2(-1,-1);
	r2.xz = r2.zw * float2(0.178506225,0.178506225) + r2.xy;
	r0.x = r0.y * 0.178506225 + r0.x;
	r0.y = dot(r1.xyzw, mtxLocalToWorld._m01_m11_m21_m31);
	r3.y = r0.x * WaterHeightScale + r0.y;
	r2.y = 4;
	r0.x = dot(r2.xyz, r2.xyz);
	r4.y = rsqrt(r0.x);
	r4.xz = r4.yy * r2.xz;
	o1.xyz = float3(1,4,1) * r4.xyz;
	r3.xz = -r4.xz * WaterWidthScale + r0.zw;
	r3.w = dot(r1.xyzw, mtxLocalToWorld._m03_m13_m23_m33);
	r0.x = dot(r3.xyzw, mtxViewProj._m00_m10_m20_m30);
	r0.y = dot(r3.xyzw, mtxViewProj._m01_m11_m21_m31);
	r0.z = dot(r3.xyzw, mtxViewProj._m02_m12_m22_m32);
	r0.w = dot(r3.xyzw, mtxViewProj._m03_m13_m23_m33);
	o0.xyzw = r0.xyzw;
	o6.xyzw = r0.xyzw;
	o2.xyzw = r3.xyzw;
	o3.x = dot(r1.xyzw, mtxModelToLocal._m00_m10_m20_m30);
	o3.y = dot(r1.xyzw, mtxModelToLocal._m01_m11_m21_m31);
	o3.z = dot(r1.xyzw, mtxModelToLocal._m02_m12_m22_m32);
	o3.w = dot(r1.xyzw, mtxModelToLocal._m03_m13_m23_m33);
	o4.xy = float2(0.013888889,0.013888889) * r3.xz;
	o5.xyz = eyePosition.xyz + -r3.xyz;
	o7.x = dot(r3.xyzw, mtxLightViewProj[0]._m00_m10_m20_m30);
	o7.y = dot(r3.xyzw, mtxLightViewProj[0]._m01_m11_m21_m31);
	o7.z = dot(r3.xyzw, mtxLightViewProj[0]._m02_m12_m22_m32);
	o7.w = dot(r3.xyzw, mtxLightViewProj[0]._m03_m13_m23_m33);
	o8.x = dot(r3.xyzw, mtxLightViewProj[1]._m00_m10_m20_m30);
	o8.y = dot(r3.xyzw, mtxLightViewProj[1]._m01_m11_m21_m31);
	o8.z = dot(r3.xyzw, mtxLightViewProj[1]._m02_m12_m22_m32);
	o8.w = dot(r3.xyzw, mtxLightViewProj[1]._m03_m13_m23_m33);
	o9.x = dot(r3.xyzw, mtxLightViewProj[2]._m00_m10_m20_m30);
	o9.y = dot(r3.xyzw, mtxLightViewProj[2]._m01_m11_m21_m31);
	o9.z = dot(r3.xyzw, mtxLightViewProj[2]._m02_m12_m22_m32);
	o9.w = dot(r3.xyzw, mtxLightViewProj[2]._m03_m13_m23_m33);
	
	r0.xy = float2(-1,1) * TexShiftPrev.xy;
	r0.zw = eyePosition.xz / VertexPitch;
	r1.xy = r0.zw >= -r0.zw;
	r0.zw = frac(abs(r0.zw));
	r0.zw = r1.xy ? r0.zw : -r0.zw;
	r1.xyz = v0.xyz;
	r1.w = 1;
	r2.x = dot(r1.xyzw, mtxLocalToWorldPrev._m00_m10_m20_m30);
	r2.z = dot(r1.xyzw, mtxLocalToWorldPrev._m02_m12_m22_m32);
	r0.zw = -r0.zw * VertexPitch + r2.xz;
	r2.xy = TCScale * r0.zw;
	r2.zw = r2.xy * float2(0.00273437495,0.00273437495) + r0.xy;
	r0.xy = r2.xy * float2(0.00535937492,0.00535937492) + r0.xy;
	r3.xyz = waterTexture.SampleLevel(linearWrapSampler_s, r0.xy, 0).xyw;
	r4.xyz = waterTexture.SampleLevel(linearWrapSampler_s, r2.zw, 0).xyw;
	r0.x = -0.5 + r4.z;
	r2.zw = r4.xy * float2(2,2) + float2(-1,-1);
	r4.xy = r2.xy * float2(0.001953125,0.001953125) + TexShift.xy;
	r4.xyz = waterTexture.SampleLevel(linearWrapSampler_s, r4.xy, 0).xyw;
	r0.y = -0.5 + r4.z;
	r4.xy = r4.xy * float2(2,2) + float2(-1,-1);
	r2.zw = r2.zw * float2(0.649999976,0.649999976) + r4.xy;
	r0.x = r0.x * 0.649999976 + r0.y;
	r4.xy = r2.xy * float2(0.00382812484,0.00382812484) + TexShift.xy;
	r2.xy = r2.xy * float2(0.00750312489,0.00750312489) + TexShift.xy;
	r5.xyz = waterTexture.SampleLevel(linearWrapSampler_s, r2.xy, 0).xyw;
	r4.xyz = waterTexture.SampleLevel(linearWrapSampler_s, r4.xy, 0).xyw;
	r0.y = -0.5 + r4.z;
	r2.xy = r4.xy * float2(2,2) + float2(-1,-1);
	r2.xy = r2.xy * float2(0.422499955,0.422499955) + r2.zw;
	r0.x = r0.y * 0.422499955 + r0.x;
	r0.y = -0.5 + r3.z;
	r2.zw = r3.xy * float2(2,2) + float2(-1,-1);
	r2.xy = r2.zw * float2(0.274624974,0.274624974) + r2.xy;
	r0.x = r0.y * 0.274624974 + r0.x;
	r0.y = -0.5 + r5.z;
	r2.zw = r5.xy * float2(2,2) + float2(-1,-1);
	r2.xz = r2.zw * float2(0.178506225,0.178506225) + r2.xy;
	r0.x = r0.y * 0.178506225 + r0.x;
	r0.y = dot(r1.xyzw, mtxLocalToWorldPrev._m01_m11_m21_m31);
	r3.y = r0.x * WaterHeightScale + r0.y;
	r2.y = 4;
	r0.x = dot(r2.xyz, r2.xyz);
	r4.y = rsqrt(r0.x);
	r4.xz = r4.yy * r2.xz;
	r3.xz = -r4.xz * WaterWidthScale + r0.zw;
	r3.w = dot(r1.xyzw, mtxLocalToWorldPrev._m03_m13_m23_m33);
	r0.x = dot(r3.xyzw, mtxViewProjPrev._m00_m10_m20_m30);
	r0.y = dot(r3.xyzw, mtxViewProjPrev._m01_m11_m21_m31);
	r0.z = dot(r3.xyzw, mtxViewProjPrev._m02_m12_m22_m32);
	r0.w = dot(r3.xyzw, mtxViewProjPrev._m03_m13_m23_m33);
	o10.xyzw = r0.xyzw;
	return;
}