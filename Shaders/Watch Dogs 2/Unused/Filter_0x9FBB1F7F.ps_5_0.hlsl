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

cbuffer PostFxGeneric : register(b1)
{
  float4 Color : packoffset(c0);
  float4 QuadParams : packoffset(c1);
  float4 Random : packoffset(c2);
  float4 UVScaleOffset : packoffset(c3);
  float2 Tiling : packoffset(c4);
  float Intensity : packoffset(c4.z);
  float Parameter1 : packoffset(c4.w);
  float Parameter2 : packoffset(c5);
  float Parameter3 : packoffset(c5.y);
  float Parameter4 : packoffset(c5.z);
}

SamplerState PostFxGeneric__PostFxMaskTexturePoint__SampObj___s : register(s0);
SamplerState PostFxGeneric__SrcSamplerLinear__SampObj___s : register(s1);
SamplerState PostFxGeneric__TextureSampler1Point__SampObj___s : register(s2);
Texture2D<float4> PostFxGeneric__PostFxMaskTexturePoint__TexObj__ : register(t0);
Texture2D<float4> PostFxGeneric__SrcSamplerLinear__TexObj__ : register(t1);
Texture2D<float4> PostFxGeneric__TextureSampler1Point__TexObj__ : register(t2);

#define cmp

void main(
  linear centroid float2 v0 : TEXCOORD0,
  float4 v1 : SV_Position0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4;
  r0.xy = v0.xy * float2(5,5) + Random.xy;
  r0.x = PostFxGeneric__TextureSampler1Point__TexObj__.Sample(PostFxGeneric__TextureSampler1Point__SampObj___s, r0.xy).x;
  r0.y = cmp(0 < Parameter3);
  if (r0.y != 0) {
    r1.xyzw = float4(0.0078125,0.013888889,0.00390625,0.0069444445) * Parameter3;
    r0.yz = float2(1,1) / r1.xy;
    r0.yz = v0.xy * r0.yz;
    r0.yz = floor(r0.yz);
    r2.xy = r0.yz * r1.xy;
    r0.yz = r0.yz * r1.xy + r1.zw;
    r1.xyzw = Parameter3 * float4(0.00390625,-0.0069444445,-0.00390625,0.0069444445) + r0.yzyz;
    r2.zw = Parameter3 * float2(0.00390625,0.0069444445) + r0.yz;
    r0.w = PostFxGeneric__PostFxMaskTexturePoint__TexObj__.Sample(PostFxGeneric__PostFxMaskTexturePoint__SampObj___s, r2.xy).x;
    r1.x = PostFxGeneric__PostFxMaskTexturePoint__TexObj__.Sample(PostFxGeneric__PostFxMaskTexturePoint__SampObj___s, r1.xy).x;
    r0.w = r1.x + r0.w;
    r1.x = PostFxGeneric__PostFxMaskTexturePoint__TexObj__.Sample(PostFxGeneric__PostFxMaskTexturePoint__SampObj___s, r1.zw).x;
    r0.w = r1.x + r0.w;
    r1.x = PostFxGeneric__PostFxMaskTexturePoint__TexObj__.Sample(PostFxGeneric__PostFxMaskTexturePoint__SampObj___s, r2.zw).x;
    r0.w = r1.x + r0.w;
    r1.x = PostFxGeneric__PostFxMaskTexturePoint__TexObj__.Sample(PostFxGeneric__PostFxMaskTexturePoint__SampObj___s, r0.yz).x;
    r0.w = r1.x + r0.w;
    r0.w = cmp(r0.w >= 0.100000001);
    r0.w = r0.w ? 1.000000 : 0;
    r0.yz = -v0.xy + r0.yz;
    r0.yz = r0.ww * r0.yz + v0.xy;
  } else {
    r0.yz = v0.xy;
    r0.w = 0;
  }
  r1.xy = r0.yz / ViewportSize.zw;
  r1.xy = float2(0.5,0.25) * r1.xy;
  r1.xy = frac(r1.xy);
  r1.xy = float2(2,0.200000003) * r1.xy;
  r1.x = floor(r1.x);
  r0.yz = r0.yz * float2(2,2) + float2(-1,-1);
  r1.z = saturate(Intensity);
  r1.w = r0.y * r0.y + r1.z;
  r1.w = r0.z * r0.z + r1.w;
  r2.x = cmp(1.99000001 < r1.w);
  r1.w = r2.x ? 1.99000001 : r1.w;
  r2.xy = float2(2,1.95000005) + -r1.ww;
  r2.z = cmp(0.00999999978 < r2.x);
  r2.yw = float2(8.81664467,1.10000002) * r2.xy;
  r2.y = 1 / r2.y;
  r2.y = r2.z ? r2.y : 0;
  r2.y = -1 + r2.y;
  r2.y = r2.y * r1.w;
  r2.z = r1.z * r1.z;
  r2.y = r2.y * r2.z + 1;
  r0.yz = r2.yy * r0.yz;
  r2.yz = r0.yz * float2(0.5,0.5) + float2(0.5,0.5);
  r2.x = cmp(r2.x < 0.00999999978);
  r2.x = r2.x ? -0.0550000034 : r2.w;
  r1.z = 1 + -r1.z;
  r1.z = r1.z * r1.z;
  r1.z = r1.z * r1.z;
  r1.z = r1.z * r1.z;
  r1.z = r1.z * r1.z;
  r2.w = 1 + -r2.x;
  r1.z = r1.z * r2.w + r2.x;
  r0.yz = -abs(r0.yz) * abs(r0.yz) + float2(1,1);
  r0.y = saturate(r0.y * r0.z);
  r0.z = ViewportSize.z * r1.w;
  r3.x = 2 * r0.z;
  r3.yw = float2(0,0);
  r0.w = 1 + -r0.w;
  r2.xw = -r3.xy * r0.ww + r2.yz;
  r4.x = PostFxGeneric__SrcSamplerLinear__TexObj__.Sample(PostFxGeneric__SrcSamplerLinear__SampObj___s, r2.xw).x;
  r4.y = PostFxGeneric__SrcSamplerLinear__TexObj__.Sample(PostFxGeneric__SrcSamplerLinear__SampObj___s, r2.yz).y;
  r0.z = r0.z * r0.w;
  r3.z = 4 * r0.z;
  r0.zw = r3.zw + r2.yz;
  r4.z = PostFxGeneric__SrcSamplerLinear__TexObj__.Sample(PostFxGeneric__SrcSamplerLinear__SampObj___s, r0.zw).z;
  r0.x = r0.x * 0.100000001 + 0.949999988;
  r2.xyz = r4.xyz * r0.xxx;
  r0.z = dot(r2.xyz, float3(0.300000012,0.109999999,0.589999974));
  r3.xyzw = saturate(Color.wxyz);
  r0.xzw = -r4.xyz * r0.xxx + r0.zzz;
  r0.xzw = r3.xxx * r0.xzw + r2.xyz;
  r0.xzw = r0.xzw * r3.yzw;
  r1.x = r1.x * 0.200000003 + 0.800000012;
  r1.x = r1.y * r1.x + 0.800000012;
  r0.xzw = r1.xxx * r0.xzw;
  r0.y = r0.y * 0.720000029 + 0.400000006;
  r0.xyz = r0.xzw * r0.yyy;
  o0.xyz = (r0.xyz * r1.zzz); // Luma: removed unnecessary saturate()
  o0.w = 1;
}