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

cbuffer FireUiPrimitive : register(b1)
{
  float4 ColorAdd : packoffset(c0);
  float4 ColorMultiplier : packoffset(c1);
  float4 DiffuseSampler0Size : packoffset(c2);
  float4 DistanceFieldFloatArray[3] : packoffset(c3);
  float4x4 Transform : packoffset(c6);
  float4 UVOffsets[9] : packoffset(c10);
  float4x4 UVTransform : packoffset(c19);
  float4 VideoTextureUnpack[8] : packoffset(c23);
  float3 GammaBrightnessContrastParams : packoffset(c31);
  float DesaturationFactor : packoffset(c31.w);
  float2 SystemTime_GlitchFactor : packoffset(c32);
}

SamplerState Video_s : register(s0);
Texture2D<float4> FireUiPrimitive__DiffuseSampler0__TexObj__ : register(t0);

void main(
  linear centroid float4 v0 : TEXCOORD0,
  linear centroid float2 v1 : TEXCOORD1,
  float4 v2 : SV_Position0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1;
  r0.xy = frac(v1.xy);
  r1.xy = r0.xy * VideoTextureUnpack[2].xy + VideoTextureUnpack[2].zw;
  r1.zw = r0.xy * VideoTextureUnpack[3].xy + VideoTextureUnpack[3].zw;
  r0.xy = r0.xy * VideoTextureUnpack[0].xy + VideoTextureUnpack[0].zw;
  r0.xy = max(VideoTextureUnpack[4].xy, r0.xy);
  r0.xy = min(VideoTextureUnpack[5].xy, r0.xy);
  r0.x = FireUiPrimitive__DiffuseSampler0__TexObj__.Sample(Video_s, r0.xy).w;
  r1.xyzw = max(VideoTextureUnpack[6].xyzw, r1.xyzw);
  r1.xyzw = min(VideoTextureUnpack[7].xyzw, r1.xyzw);
  r0.y = FireUiPrimitive__DiffuseSampler0__TexObj__.Sample(Video_s, r1.zw).w;
  r0.z = FireUiPrimitive__DiffuseSampler0__TexObj__.Sample(Video_s, r1.xy).w;
  r1.xyz = float3(0,-0.391448975,2.01782227) * r0.yyy; // TODO: fix wrong video color space and wrong luminance formula
  r0.yzw = r0.z * float3(1.59579468,-0.813476563,0) + r1.xyz;
  r0.xyz = r0.x * float3(1.16412354,1.16412354,1.16412354) + r0.yzw;
  r0.xyz = float3(-0.87065506,0.529705048,-1.08166885) + r0.xyz;
  //r0.xyz = max(float3(0,0,0), r0.xyz); // Luma: disabled
  r1.xyz = v0.xyz * r0.xyz;
  r0.w = dot(r1.xyz, float3(0.298999995,0.587000012,0.114));
  r0.xyz = v0.xyz * r0.xyz + -r0.w;
  r0.xyz = DesaturationFactor * r0.xyz + r0.w;
  r0.xyz = ExposureScale * r0.xyz;
  //r0.xyz = clamp(r0.xyz, 0.0, 64512.0); // Luma: disabled
  r0.xyz = pow(abs(r0.xyz), GammaBrightnessContrastParams.x) * sign(r0.xyz); // Luma: fixed negative values support
  o0.xyz = r0.xyz * GammaBrightnessContrastParams.y + GammaBrightnessContrastParams.z;
  o0.w = v0.w;
}