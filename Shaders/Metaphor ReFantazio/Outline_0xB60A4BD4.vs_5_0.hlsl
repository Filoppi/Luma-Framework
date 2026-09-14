#include "Includes/Outline.hlsl"

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

cbuffer GFD_VSCONST_COLORS : register(b6)
{
	float4 constantColor : packoffset(c0);
}

cbuffer GFD_VSCONST_TOON_OUTLINE : register(b7)
{
	float outlineThickness : packoffset(c0);
	float outlineThinMax : packoffset(c0.y);
	float2 outlineThinFade : packoffset(c0.z);
}

void main(
	float3 v0 : POSITION0,
	float3 v1 : NORMAL0,
	uint vertexID : SV_VertexID,
	out float4 o0 : SV_POSITION0,
	out float4 o1 : TEXCOORD5,
	out float4 o2 : TEXCOORD6,
	out float4 o3 : TEXCOORD7,
	out float4 o4 : COLOR0,
	out float4 o5 : COLOR1,
	out float4 o6 : TEXCOORD1,
	out float4 o7 : TEXCOORD2,
	out float4 o8 : TEXCOORD3)
{
	float4 r0,r1,r2,r3,r4;
	uint4 bitmask, uiDest;
	float4 fDest;

	r0.x = dot(v1.xyz, mtxLocalToWorld._m00_m10_m20);
	r0.y = dot(v1.xyz, mtxLocalToWorld._m01_m11_m21);
	r0.z = dot(v1.xyz, mtxLocalToWorld._m02_m12_m22);
	r0.w = dot(r0.xyz, r0.xyz);
	r0.w = rsqrt(r0.w);
	r0.xyz = r0.xyz * r0.www;
	r1.xyz = v0.xyz;
	r1.w = 1;
	r2.x = dot(r1.xyzw, mtxLocalToWorld._m00_m10_m20_m30);
	r2.y = dot(r1.xyzw, mtxLocalToWorld._m01_m11_m21_m31);
	r2.z = dot(r1.xyzw, mtxLocalToWorld._m02_m12_m22_m32);
	r3.xyz = -eyePosition.xyz + r2.xyz;
	r0.w = dot(r3.xyz, r3.xyz);
	r0.w = sqrt(r0.w);
	r0.w = fovy * r0.w;
	r0.w = outlineThickness * r0.w;
	r3.xyz = r0.xyz * r0.www + r2.xyz;
	r0.x = 1.00000001e-10 < r0.w;
	r2.xyz = r0.xxx ? r3.xyz : r2.xyz;
	r3.w = 1;
	r4.x = dot(r3.xyzw, mtxViewProj._m00_m10_m20_m30);
	r4.y = dot(r3.xyzw, mtxViewProj._m01_m11_m21_m31);
	r4.z = dot(r3.xyzw, mtxViewProj._m02_m12_m22_m32);
	r4.w = dot(r3.xyzw, mtxViewProj._m03_m13_m23_m33);
	r3.x = dot(r1.xyzw, mtxLocalToWorldViewProj._m00_m10_m20_m30);
	r3.y = dot(r1.xyzw, mtxLocalToWorldViewProj._m01_m11_m21_m31);
	r3.z = dot(r1.xyzw, mtxLocalToWorldViewProj._m02_m12_m22_m32);
	r3.w = dot(r1.xyzw, mtxLocalToWorldViewProj._m03_m13_m23_m33);
	r0.xyzw = r0.xxxx ? r4.xyzw : r3.xyzw;
	o0.xyzw = r0.xyzw;
	o7.xyzw = r0.xyzw;
	r2.w = dot(r1.xyzw, mtxLocalToWorld._m03_m13_m23_m33);
	o1.x = dot(r2.xyzw, mtxLightViewProj[0]._m00_m10_m20_m30);
	o1.y = dot(r2.xyzw, mtxLightViewProj[0]._m01_m11_m21_m31);
	o1.z = dot(r2.xyzw, mtxLightViewProj[0]._m02_m12_m22_m32);
	o1.w = dot(r2.xyzw, mtxLightViewProj[0]._m03_m13_m23_m33);
	o2.x = dot(r2.xyzw, mtxLightViewProj[1]._m00_m10_m20_m30);
	o2.y = dot(r2.xyzw, mtxLightViewProj[1]._m01_m11_m21_m31);
	o2.z = dot(r2.xyzw, mtxLightViewProj[1]._m02_m12_m22_m32);
	o2.w = dot(r2.xyzw, mtxLightViewProj[1]._m03_m13_m23_m33);
	o3.x = dot(r2.xyzw, mtxLightViewProj[2]._m00_m10_m20_m30);
	o3.y = dot(r2.xyzw, mtxLightViewProj[2]._m01_m11_m21_m31);
	o3.z = dot(r2.xyzw, mtxLightViewProj[2]._m02_m12_m22_m32);
	o3.w = dot(r2.xyzw, mtxLightViewProj[2]._m03_m13_m23_m33);
	o6.xyzw = r2.xyzw;
	o4.xyzw = constantColor.xyzw;
	o5.xyzw = float4(0,0,0,1);
	
	float4 prevPosition = r1;
	float3 prevNormal = v1.xyz;
	GetPreviousVertexParameters(vertexID, 12, prevPosition, prevNormal);
	r0.x = dot(prevNormal.xyz, mtxLocalToWorldPrev._m00_m10_m20);
	r0.y = dot(prevNormal.xyz, mtxLocalToWorldPrev._m01_m11_m21);
	r0.z = dot(prevNormal.xyz, mtxLocalToWorldPrev._m02_m12_m22);
	r0.w = dot(r0.xyz, r0.xyz);
	r0.w = rsqrt(r0.w);
	r0.xyz = r0.xyz * r0.www;
	r2.x = dot(prevPosition.xyzw, mtxLocalToWorldPrev._m00_m10_m20_m30);
	r2.y = dot(prevPosition.xyzw, mtxLocalToWorldPrev._m01_m11_m21_m31);
	r2.z = dot(prevPosition.xyzw, mtxLocalToWorldPrev._m02_m12_m22_m32);
	r3.xyz = -eyePositionPrev.xyz + r2.xyz;
	r0.w = dot(r3.xyz, r3.xyz);
	r0.w = sqrt(r0.w);
	r0.w = fovy * r0.w;
	r0.w = outlineThickness * r0.w;
	r3.xyz = r0.xyz * r0.www + r2.xyz;
	r0.x = 1.00000001e-10 < r0.w;
	r2.xyz = r0.xxx ? r3.xyz : r2.xyz;
	r3.w = 1;
	r4.x = dot(r3.xyzw, mtxViewProjPrev._m00_m10_m20_m30);
	r4.y = dot(r3.xyzw, mtxViewProjPrev._m01_m11_m21_m31);
	r4.z = dot(r3.xyzw, mtxViewProjPrev._m02_m12_m22_m32);
	r4.w = dot(r3.xyzw, mtxViewProjPrev._m03_m13_m23_m33);
	r3.x = dot(prevPosition.xyzw, mtxLocalToWorldViewProjPrev._m00_m10_m20_m30);
	r3.y = dot(prevPosition.xyzw, mtxLocalToWorldViewProjPrev._m01_m11_m21_m31);
	r3.z = dot(prevPosition.xyzw, mtxLocalToWorldViewProjPrev._m02_m12_m22_m32);
	r3.w = dot(prevPosition.xyzw, mtxLocalToWorldViewProjPrev._m03_m13_m23_m33);
	r0.xyzw = r0.xxxx ? r4.xyzw : r3.xyzw;
    o8.xyzw = r0.xyzw;
	return;
}