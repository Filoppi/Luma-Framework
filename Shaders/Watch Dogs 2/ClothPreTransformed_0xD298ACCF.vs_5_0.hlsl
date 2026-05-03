#include "Includes/Common.hlsl"

cbuffer Viewport : register(b0)
{
  float4 CameraNearPlaneSize : packoffset(c0);
  float4x4 DepthTextureTransform : packoffset(c1);
  float4 FSMClipPlanes : packoffset(c5);
  float4 FacettedShadowCastParams : packoffset(c6);
  float4 FogValues0 : packoffset(c7);
  float4 FogValues1 : packoffset(c8);
  float4x4 InvProjectionMatrix : packoffset(c9);
  float4x4 InvProjectionMatrixDepth : packoffset(c13);
  float4x3 InvViewMatrix : packoffset(c17);
  float4x4 PreviousViewProjectionMatrix : packoffset(c20);
  float4x4 ProjectionMatrix : packoffset(c24);
  float4 RainOcclusionFadeParams : packoffset(c28);
  float4x4 RainOcclusionProjectionMatrix : packoffset(c29);
  float4 RainOcclusionShadowMapSize : packoffset(c33);
  float4 ReflectionVolumeDebugColors[15] : packoffset(c34);
  float4 VPosOffset : packoffset(c49);
  float4 VPosScale : packoffset(c50);
  float4x3 ViewMatrix : packoffset(c51);
  float4x4 ViewProjectionMatrix : packoffset(c54);
  float4x4 ViewRotProjectionMatrix : packoffset(c58);
  float4x4 ViewRotProjectionMatrixPure : packoffset(c62);
  float4 ViewportSize : packoffset(c66);

  struct
  {
    float near;
    float far;
    float view;
    float oneOverView;
  } CameraDistances : packoffset(c67);


  struct
  {
    float4x4 inverseTransform;
    float3 rcpFadeRangePositive;
    float textureArrayIndexAsFloat;
    float3 rcpFadeRangeNegative;
    float fadeFactor;
    float2 multipliers;
    uint parallaxCorrection;
    float padding0;
  } ReflectionVolumes[15] : packoffset(c68);

  float3 CameraDirection : packoffset(c173);
  float DefaultReflectionTextureArrayIndexAsFloat : packoffset(c173.w);
  float3 CameraPosition : packoffset(c174);
  float DynamicCubeMapReflectionTextureMaxMipIndex : packoffset(c174.w);
  float3 CullingCameraPosition : packoffset(c175);
  float ExposedWhitePointOverExposureScale : packoffset(c175.w);
  float3 FogColorVector : packoffset(c176);
  float ExposureScale : packoffset(c176.w);
  float3 OppositeFogColorDelta : packoffset(c177);
  float MaxParaboloidReflectionMipIndex : packoffset(c177.w);
  float3 SideFogColor : packoffset(c178);
  float MaxStaticReflectionMipIndex : packoffset(c178.w);
  float3 SunFogColorDelta : packoffset(c179);
  float MeasuredExposureScale : packoffset(c179.w);
  float3 TemporalFilteringParams : packoffset(c180);
  float RaindropRippleScale : packoffset(c180.w);
  float3 UncompressDepthWeights : packoffset(c181);
  float ReflectionScaleDistanceMul : packoffset(c181.w);
  float3 UncompressDepthWeightsWS : packoffset(c182);
  float ReflectionScaleStrength : packoffset(c182.w);
  float3 ViewPoint : packoffset(c183);
  float SkyParaboloidTextureMaxMipIndex : packoffset(c183.w);
  float2 DefaultReflectionMultipliers : packoffset(c184);
  bool UseOnlySkyReflection : packoffset(c184.z);
  float2 ReflectionGIControl : packoffset(c185);
  uint2 SelectedPixel : packoffset(c185.z);
}

cbuffer SceneGraphicObjectInstancePart : register(b1)
{
  float4x3 WorldMatrix : packoffset(c0);
}

cbuffer SceneGeometry : register(b2)
{
  float4 GeometryPickingID : packoffset(c0);
  float4 GeometryUserData : packoffset(c1);
  float4 UVDecompression : packoffset(c2);

  struct
  {
    float positionMin;
    float positionRange;
    float meshLocalHeight;
    float isBuildingFacadeInterior;
  } MeshDecompression : packoffset(c3);

  float3 GeometryBBoxMax : packoffset(c4);
  float3 GeometryBBoxMin : packoffset(c5);
}

cbuffer MaterialWD2Cloth : register(b3)
{
  float4 MaterialPickingID : packoffset(c0);
  float4 DetailUVTiling1 : packoffset(c1);
  float4 DetailUVTiling2 : packoffset(c2);
  float4 DetailUVTiling3 : packoffset(c3);
  float4 DiffuseUVTiling1 : packoffset(c4);
  float4 MaskTextureUVTiling1 : packoffset(c5);
  float4 NormalUVTiling1 : packoffset(c6);
  float3 DiffuseColor1 : packoffset(c7);
  float DetailIntensity1 : packoffset(c7.w);
  float3 DiffuseColor2 : packoffset(c8);
  float DetailIntensity2 : packoffset(c8.w);
  float3 DiffuseColor3 : packoffset(c9);
  float DetailIntensity3 : packoffset(c9.w);
  float3 DiffuseColor4 : packoffset(c10);
  float GlobalReflectance : packoffset(c10.w);
  float3 ModulationColor : packoffset(c11);
  float Metalness : packoffset(c11.w);
  float3 PatternColor : packoffset(c12);
  float ModulationRoughness : packoffset(c12.w);
  float3 VelvetColor1 : packoffset(c13);
  float NormalIntensity : packoffset(c13.w);
  float3 VelvetColor2 : packoffset(c14);
  float Reflectance : packoffset(c14.w);
  float3 VelvetColor3 : packoffset(c15);
  float Roughness : packoffset(c15.w);
  float2 DiffuseTexture1Offset : packoffset(c16);
  int DetailColorChannel : packoffset(c16.z);
  int DetailColorType : packoffset(c16.w);
  int WetSurfaceTypeIndex : packoffset(c17);
  bool CinematicMode : packoffset(c17.y);
  bool ModulationColorInvert : packoffset(c17.z);
  bool ModulationRoughInvert : packoffset(c17.w);
  bool isVelvet1 : packoffset(c18);
  bool isVelvet2 : packoffset(c18.y);
  bool isVelvet3 : packoffset(c18.z);
}

cbuffer PreviousWorldTransform : register(b4)
{
  float4x3 PreviousWorldMatrix : packoffset(c0);
}

cbuffer PreviousSkinCache : register(b9)
{
	uint offset : packoffset(c0);
	uint stride : packoffset(c0.y);
}

ByteAddressBuffer CachedSkinVertices : register(t1);



// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : position0,
  int4 v1 : texcoord2,
  float4 v2 : blendweight0,
  float4 v3 : normal0,
  float4 v4 : texcoord1,
  float4 v5 : texcoord0,
  uint vertexID : SV_VertexID,
  out float4 o0 : TEXCOORD0,
  out float4 o1 : TEXCOORD1,
  out float4 o2 : TEXCOORD2,
  out float4 o3 : TEXCOORD3,
  out float4 o4 : TEXCOORD4,
  out float4 o5 : TEXCOORD5,
  out float4 o6 : TEXCOORD6,
  out float4 o7 : SV_Position0)
{
  float4 r0,r1,r2,r3;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xyz = v3.zyx * float3(2,2,2) + float3(-1,-1,-1);
  o0.x = dot(r0.xyz, WorldMatrix._m00_m10_m20);
  o0.y = dot(r0.xyz, WorldMatrix._m01_m11_m21);
  o0.z = dot(r0.xyz, WorldMatrix._m02_m12_m22);
  r0.xyz = v5.zyx * float3(2,2,2) + float3(-1,-1,-1);
  o1.x = dot(r0.xyz, WorldMatrix._m00_m10_m20);
  o1.y = dot(r0.xyz, WorldMatrix._m01_m11_m21);
  o1.z = dot(r0.xyz, WorldMatrix._m02_m12_m22);
  r0.xyz = v4.zyx * float3(2,2,2) + float3(-1,-1,-1);
  o2.x = dot(r0.xyz, WorldMatrix._m00_m10_m20);
  o2.y = dot(r0.xyz, WorldMatrix._m01_m11_m21);
  o2.z = dot(r0.xyz, WorldMatrix._m02_m12_m22);
  r0.xyz = v0.xyz;
  r0.w = 1;
  r1.x = dot(r0.xyzw, WorldMatrix._m00_m10_m20_m30);
  r1.y = dot(r0.xyzw, WorldMatrix._m01_m11_m21_m31);
  r1.z = dot(r0.xyzw, WorldMatrix._m02_m12_m22_m32);
  r1.xyz = -CameraPosition.xyz + r1.xyz;
  r2.x = ViewRotProjectionMatrix._m10 * r1.y;
  r2.y = ViewRotProjectionMatrix._m11 * r1.y;
  r2.z = ViewRotProjectionMatrix._m12 * r1.y;
  r2.w = ViewRotProjectionMatrix._m13 * r1.y;
  r3.x = r1.z * ViewRotProjectionMatrix._m20 + ViewRotProjectionMatrix._m30;
  r3.y = r1.z * ViewRotProjectionMatrix._m21 + ViewRotProjectionMatrix._m31;
  r3.z = r1.z * ViewRotProjectionMatrix._m22 + ViewRotProjectionMatrix._m32;
  r3.w = r1.z * ViewRotProjectionMatrix._m23 + ViewRotProjectionMatrix._m33;
  r2.xyzw = r3.xyzw + r2.xyzw;
  r3.x = ViewRotProjectionMatrix._m00 * r1.x;
  r3.y = ViewRotProjectionMatrix._m01 * r1.x;
  r3.z = ViewRotProjectionMatrix._m02 * r1.x;
  r3.w = ViewRotProjectionMatrix._m03 * r1.x;
  r1.xyzw = r3.xyzw + r2.xyzw;
  o3.xyz = float3(0.5,-0.5,1) * r1.xyw;
  o7.xyzw = r1.xyzw;
  
  float4 previousLocalPosition;
  previousLocalPosition.xyz = asfloat(CachedSkinVertices.Load3(offset + vertexID * stride)).xyz;
  previousLocalPosition.w = 1.0f;
  float3 previousWorldPosition = mul(previousLocalPosition, PreviousWorldMatrix);
  previousWorldPosition -= LumaData.GameData.PreviousCameraPosition.xyz;
  
  float4 previousProjectedPosition = mul(float4(previousWorldPosition, 1.0), LumaData.GameData.PreviousViewRotProjectionMatrix);
  
  o4.xyz = previousProjectedPosition.xyw * float3(0.5f, -0.5f, 1.0f);
  
  /*
  r1.x = dot(r0.xyzw, PreviousWorldMatrix._m00_m10_m20_m30);
  r1.y = dot(r0.xyzw, PreviousWorldMatrix._m01_m11_m21_m31);
  r1.z = dot(r0.xyzw, PreviousWorldMatrix._m02_m12_m22_m32);
  r1.w = 1;
  r0.x = dot(r1.xyzw, PreviousViewProjectionMatrix._m00_m10_m20_m30);
  r0.y = dot(r1.xyzw, PreviousViewProjectionMatrix._m01_m11_m21_m31);
  o4.z = dot(r1.xyzw, PreviousViewProjectionMatrix._m03_m13_m23_m33);
  o4.xy = float2(0.5,-0.5) * r0.xy;
  */
  r0.xyzw = (int4)v1.xyzw;
  r0.xyzw = r0.xyzw * UVDecompression.zwzw + UVDecompression.xyxy;
  r1.xyzw = MaskTextureUVTiling1.xyzw * r0.xyzw;
  o5.xy = r1.xy + r1.zw;
  r1.xyzw = DiffuseUVTiling1.xyzw * r0.xyzw;
  o5.zw = r1.xy + r1.zw;
  r1.xyzw = NormalUVTiling1.xyzw * r0.xyzw;
  r0.xyzw = DetailUVTiling1.xyzw * r0.xyzw;
  o6.zw = r0.xy + r0.zw;
  o6.xy = r1.xy + r1.zw;
  return;
}