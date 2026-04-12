cbuffer _Globals : register(b0)
{
  float4x4 LocalToWorld : packoffset(c0);
  float4x4 InvViewProjectionMatrix : packoffset(c4);
  float3 CameraWorldPos : packoffset(c8);
  float4 ObjectWorldPositionAndRadius : packoffset(c9);
  float3 ObjectOrientation : packoffset(c10);
  float3 ObjectPostProjectionPosition : packoffset(c11);
  float3 ObjectNDCPosition : packoffset(c12);
  float4 ObjectMacroUVScales : packoffset(c13);
  float3 FoliageImpulseDirection : packoffset(c14);
  float4 FoliageNormalizedRotationAxisAndAngle : packoffset(c15);
  float4 WindDirectionAndSpeed : packoffset(c16);
  float3 CameraWorldDirection : packoffset(c17) = {1,0,0};
  float4 ObjectOriginAndPrimitiveID : packoffset(c18) = {0,0,0,0};
  float OcclusionPercentage : packoffset(c19);
  float4 UniformPixelVector_0 : packoffset(c20);
  float4 UniformPixelVector_1 : packoffset(c21);
  float4 UniformPixelVector_2 : packoffset(c22);
  float4 UniformPixelVector_3 : packoffset(c23);
  float4 UniformPixelVector_4 : packoffset(c24);
  float4 UniformPixelVector_5 : packoffset(c25);
  float4 UniformPixelScalars_0 : packoffset(c26);
  float4 UniformPixelScalars_1 : packoffset(c27);
  float4 UniformPixelScalars_2 : packoffset(c28);
  float4 CameraRight : packoffset(c29);
  float4 CameraUp : packoffset(c30);
  float3 IrradianceUVWScale : packoffset(c31);
  float3 IrradianceUVWBias : packoffset(c32);
  float AverageDynamicSkylightIntensity : packoffset(c32.w);
  float4 LightEnvironmentRedAndGreenUV : packoffset(c33);
  float2 LightEnvironmentBlueUV : packoffset(c34);
}

cbuffer PSOffsetConstants : register(b2)
{
  float4 ScreenPositionScaleBias : packoffset(c0);
  float4 MinZ_MaxZRatio : packoffset(c1);
  float NvStereoEnabled : packoffset(c2);
}

SamplerState Texture2D_0_s : register(s0);
SamplerState Texture2D_1_s : register(s1);
SamplerState Texture2D_2_s : register(s2);
SamplerState Texture2D_3_s : register(s3);
Texture2D<float4> Texture2D_0 : register(t0); // Starburst.
Texture2D<float4> Texture2D_1 : register(t1); // Lens dirt.
Texture2D<float4> Texture2D_2 : register(t2); // Lens dirt.
Texture2D<float4> Texture2D_3 : register(t3); // Lens dirt.

#ifndef DISABLE_LENS_DIRT
#define DISABLE_LENS_DIRT 0
#endif

// 3Dmigoto declarations
#define cmp -

void main(
  float4 v0 : TEXCOORD0,
  float4 v1 : TEXCOORD1,
  float4 v2 : TEXCOORD2,
  float4 v3 : TEXCOORD4,
  float4 v4 : TEXCOORD5,
  float4 v5 : TEXCOORD6,
  float4 v6 : TEXCOORD7,
  uint v7 : SV_IsFrontFace0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xyzw = v4.xyxy / v4.wwww;
  r0.xyzw = r0.xyzw * ScreenPositionScaleBias.xyxy + ScreenPositionScaleBias.wzwz;
  r0.xy = r0.xy * float2(1,0.0199999996) + UniformPixelVector_2.xy;

  #if DISABLE_LENS_DIRT
  r0.x = 0.0;
  r0.y = 0.0;
  #else
  r0.x = Texture2D_2.Sample(Texture2D_2_s, r0.xy).y;
  r0.y = Texture2D_1.Sample(Texture2D_1_s, r0.zw).y;
  #endif

  r0.x = r0.x * r0.y;
  r1.xyzw = float4(-0.300000012,-0.5,0.300000012,0.300000012) + r0.zwzw;
  r0.yz = float2(1.20000005,1.29999995) * r0.zw;

  #if DISABLE_LENS_DIRT
  r0.y = 0.0;
  #else
  r0.y = Texture2D_3.Sample(Texture2D_3_s, r0.yz).y;
  #endif

  r0.y = UniformPixelScalars_2.y * r0.y;
  r0.zw = float2(0.800000012,0.899999976) * r1.zw;

  #if DISABLE_LENS_DIRT
  r0.z = 0.0;
  #else
  r0.z = Texture2D_1.Sample(Texture2D_1_s, r0.zw).y;
  #endif

  r1.zw = r1.zw * float2(0.800000012,0.0359999985) + UniformPixelVector_2.xy;

  #if DISABLE_LENS_DIRT
  r0.w = 0.0;
  #else
  r0.w = Texture2D_2.Sample(Texture2D_2_s, r1.zw).y;
  #endif

  r0.x = r0.w * r0.z + r0.x;
  r0.zw = float2(1.29999995,1.39999998) * r1.xy;
  r1.xy = r1.xy * float2(1.29999995,0.027999999) + UniformPixelVector_1.xy;

  #if DISABLE_LENS_DIRT
  r1.x = 0.0;
  r0.z = 0.0;
  #else
  r1.x = Texture2D_2.Sample(Texture2D_2_s, r1.xy).y;
  r0.z = Texture2D_1.Sample(Texture2D_1_s, r0.zw).y;
  #endif

  r0.x = r1.x * r0.z + r0.x;
  r0.x = r0.x * UniformPixelScalars_2.x + r0.y;
  r0.yzw = Texture2D_0.Sample(Texture2D_0_s, v0.xy).xyz;
  r0.yzw = v1.xyz * r0.yzw;
  r0.yzw = OcclusionPercentage * r0.yzw;
  r0.yzw = UniformPixelScalars_0.xxx * r0.yzw;
  r0.xyz = r0.xxx * r0.yzw + r0.yzw;
  r0.xyz = v2.www * r0.xyz;
  r0.xyz = max(float3(0,0,0), r0.xyz);
  r0.xyz = min(float3(500,500,500), r0.xyz);
  r0.xyz = UniformPixelVector_3.xyz + r0.xyz;
  o0.xyz = v3.www * r0.xyz;
  o0.w = 0;
  return;
}