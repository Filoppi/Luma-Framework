#include "Includes/Common.hlsl"

struct VSCONST_VIEWPROJ
{
  float4x4 mtxViewProj;
  float4x4 mtxView;
  float3 eyePosition;
  float _reserved_b2;
  float4x4 mtxPrevViewProj;
};

cbuffer GFD_VSCONST_VIEWPROJ : register(b0)
{
	VSCONST_VIEWPROJ g_viewProj;
}

RWStructuredBuffer<VSCONST_VIEWPROJ> g_updatedGFD_VSCONST_VIEWPROJ : register(u0);

float4x4 InverseMatrix(float4x4 m)
{
    float4x4 r;

    float a00 = m[0][0], a01 = m[0][1], a02 = m[0][2], a03 = m[0][3];
    float a10 = m[1][0], a11 = m[1][1], a12 = m[1][2], a13 = m[1][3];
    float a20 = m[2][0], a21 = m[2][1], a22 = m[2][2], a23 = m[2][3];
    float a30 = m[3][0], a31 = m[3][1], a32 = m[3][2], a33 = m[3][3];

    float b00 = a00 * a11 - a01 * a10;
    float b01 = a00 * a12 - a02 * a10;
    float b02 = a00 * a13 - a03 * a10;
    float b03 = a01 * a12 - a02 * a11;
    float b04 = a01 * a13 - a03 * a11;
    float b05 = a02 * a13 - a03 * a12;
    float b06 = a20 * a31 - a21 * a30;
    float b07 = a20 * a32 - a22 * a30;
    float b08 = a20 * a33 - a23 * a30;
    float b09 = a21 * a32 - a22 * a31;
    float b10 = a21 * a33 - a23 * a31;
    float b11 = a22 * a33 - a23 * a32;

    float det = b00 * b11 - b01 * b10 + b02 * b09 + b03 * b08 - b04 * b07 + b05 * b06;
    float invDet = 1.0 / det;

    r[0][0] = ( a11 * b11 - a12 * b10 + a13 * b09) * invDet;
    r[0][1] = (-a01 * b11 + a02 * b10 - a03 * b09) * invDet;
    r[0][2] = ( a31 * b05 - a32 * b04 + a33 * b03) * invDet;
    r[0][3] = (-a21 * b05 + a22 * b04 - a23 * b03) * invDet;
    r[1][0] = (-a10 * b11 + a12 * b08 - a13 * b07) * invDet;
    r[1][1] = ( a00 * b11 - a02 * b08 + a03 * b07) * invDet;
    r[1][2] = (-a30 * b05 + a32 * b02 - a33 * b01) * invDet;
    r[1][3] = ( a20 * b05 - a22 * b02 + a23 * b01) * invDet;
    r[2][0] = ( a10 * b10 - a11 * b08 + a13 * b06) * invDet;
    r[2][1] = (-a00 * b10 + a01 * b08 - a03 * b06) * invDet;
    r[2][2] = ( a30 * b04 - a31 * b02 + a33 * b00) * invDet;
    r[2][3] = (-a20 * b04 + a21 * b02 - a23 * b00) * invDet;
    r[3][0] = (-a10 * b09 + a11 * b07 - a12 * b06) * invDet;
    r[3][1] = ( a00 * b09 - a01 * b07 + a02 * b06) * invDet;
    r[3][2] = (-a30 * b03 + a31 * b01 - a32 * b00) * invDet;
    r[3][3] = ( a20 * b03 - a21 * b01 + a22 * b00) * invDet;

    return r;
}

[numthreads(1, 1, 1)]
void main(uint2 tid : SV_DispatchThreadID, uint3 gid : SV_GroupId, uint gix : SV_GroupIndex)
{
	float4x4 invView = InverseMatrix(g_viewProj.mtxView);
	float4x4 proj = mul(invView, g_viewProj.mtxViewProj);
	float4x4 invProj = InverseMatrix(proj);
	float4x4 prevView = mul(g_viewProj.mtxPrevViewProj, invProj);
	
	proj[2][0] -= LumaSettings.GameSettings.JitterOffset.x;
	proj[2][1] += LumaSettings.GameSettings.JitterOffset.y;

	g_updatedGFD_VSCONST_VIEWPROJ[0].mtxViewProj = mul(g_viewProj.mtxView, proj);
	g_updatedGFD_VSCONST_VIEWPROJ[0].mtxView = g_viewProj.mtxView;
	g_updatedGFD_VSCONST_VIEWPROJ[0].eyePosition = g_viewProj.eyePosition;
	g_updatedGFD_VSCONST_VIEWPROJ[0]._reserved_b2 = g_viewProj._reserved_b2;
	g_updatedGFD_VSCONST_VIEWPROJ[0].mtxPrevViewProj = mul(prevView, proj);
}