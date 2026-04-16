// ---- Created with 3Dmigoto v1.3.16 on Thu Apr 16 12:01:34 2026

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

SamplerState AmbientOcclusionVolume__AmbientOcclusionTexure__SampObj___s : register(s0);
Texture2DMS<float> Viewport__DepthVPSamplerMS : register(t0);
Texture2DMS<float4> Viewport__GBufferNormalTextureMS : register(t1);
Texture2D<float4> AmbientOcclusionVolume__AmbientOcclusionTexure__TexObj__ : register(t2);


// 3Dmigoto declarations
#define cmp -


void main(
  linear centroid float4 v0 : TEXCOORD0,
  linear centroid float4 v1 : TEXCOORD1,
  linear centroid float4 v2 : TEXCOORD2,
  linear centroid float4 v3 : TEXCOORD3,
  linear centroid float4 v4 : TEXCOORD4,
  linear centroid float2 v5 : TEXCOORD5,
  float4 v6 : SV_Position0,
  uint v7 : SV_SampleIndex0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = (int2)v6.xy;
  r0.zw = float2(0,0);
  r1.xyz = Viewport__GBufferNormalTextureMS.Load(r0.xy, v7.x).xyz;
  r0.x = Viewport__DepthVPSamplerMS.Load(r0.xy, v7.x).x;
  r2.x = dot(r1.xyz, ViewMatrix._m00_m10_m20);
  r2.y = dot(r1.xyz, ViewMatrix._m01_m11_m21);
  r2.z = dot(r1.xyz, ViewMatrix._m02_m12_m22);
  r0.z = dot(r2.xyz, r2.xyz);
  r0.z = rsqrt(r0.z);
  r1.xyz = r2.xyz * r0.zzz;
  r0.y = 1;
  r0.z = dot(r0.xy, InvProjectionMatrix._m22_m32);
  r0.x = dot(r0.xy, InvProjectionMatrix._m23_m33);
  r0.x = -r0.z / r0.x;
  r0.yzw = v4.xyz / v4.zzz;
  r2.x = v0.w;
  r2.y = v1.w;
  r2.z = v2.w;
  r0.xyz = r0.yzw * -r0.xxx + -r2.xyz;
  r0.w = dot(r0.xyz, r0.xyz);
  r0.w = rsqrt(r0.w);
  r2.xyz = r0.xyz * r0.www;
  r0.w = dot(r1.xyz, r2.xyz);
  r0.w = saturate(0.5 + r0.w);
  r0.w = -r0.w * v5.y + 1;
  r1.x = dot(r0.xyz, v0.xyz);
  r1.y = dot(r0.xyz, v1.xyz);
  r0.z = dot(r0.xyz, v2.xyz);
  r0.z = abs(r0.z);
  r1.zw = abs(r1.xy) * abs(r1.xy);
  r2.xy = r1.xy * float2(0.5,0.5) + float2(0.5,0.5);
  r0.xy = r1.zw * r1.zw;
  r0.xyz = min(float3(1,1,1), r0.xyz);
  r0.xyz = float3(1,1,1) + -r0.xyz;
  r0.x = r0.x * r0.y;
  r0.x = r0.x * r0.z;
  r2.z = 1 + -r2.y;
  r1.xyz = AmbientOcclusionVolume__AmbientOcclusionTexure__TexObj__.Sample(AmbientOcclusionVolume__AmbientOcclusionTexure__SampObj___s, r2.xz).xyz;
  r0.y = dot(r1.xyz, v3.xyz);
  r0.x = r0.y * r0.x;
  r0.x = r0.x * r0.w;
  r0.x = v4.w * r0.x;
  r0.x = saturate(min(v3.w, r0.x));
  r0.x = v5.x + -r0.x;
  o0.xyzw = saturate(float4(1,1,1,1) + r0.xxxx);
  return;
}