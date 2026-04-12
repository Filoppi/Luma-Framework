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
  float4 UniformPixelVector_0 : packoffset(c19);
  float4 UniformPixelVector_1 : packoffset(c20);
  float4 UniformPixelVector_2 : packoffset(c21);
  float4 UniformPixelVector_3 : packoffset(c22);
  float4 UniformPixelVector_4 : packoffset(c23);
  float4 UniformPixelVector_5 : packoffset(c24);
  float4 UniformPixelVector_6 : packoffset(c25);
  float4 UniformPixelVector_7 : packoffset(c26);
  float4 UniformPixelScalars_0 : packoffset(c27);
  float4 UniformPixelScalars_1 : packoffset(c28);
  float4 UniformPixelScalars_2 : packoffset(c29);
  float4 UniformPixelScalars_3 : packoffset(c30);
  float4 UniformPixelScalars_4 : packoffset(c31);
  float LocalToWorldRotDeterminantFlip : packoffset(c32);
  float4 LightmapCoordinateScaleBias : packoffset(c33);
  float3x3 WorldToLocal : packoffset(c34);
  float3 MeshOrigin : packoffset(c37);
  float3 MeshExtension : packoffset(c38);
  float3 IrradianceUVWScale : packoffset(c39);
  float3 IrradianceUVWBias : packoffset(c40);
  float AverageDynamicSkylightIntensity : packoffset(c40.w);
  float4 LightEnvironmentRedAndGreenUV : packoffset(c41);
  float2 LightEnvironmentBlueUV : packoffset(c42);
}


// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : TEXCOORD0,
  float4 v1 : TEXCOORD4,
  float4 v2 : TEXCOORD5,
  float4 v3 : TEXCOORD6,
  float4 v4 : TEXCOORD7,
  uint v5 : SV_IsFrontFace0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = CameraWorldPos.xz + v4.xz;
  r0.zw = float2(-100,-50) + ObjectWorldPositionAndRadius.xz;
  r0.xy = r0.xy + -r0.zw;
  r0.z = 0.00249999994 * ObjectWorldPositionAndRadius.w;
  r0.xy = r0.xy * float2(0.00300000003,0.00300000003) + r0.zz;
  r0.x = r0.x + -r0.y;
  r0.x = UniformPixelScalars_0.y * r0.x + r0.y;
  r0.y = saturate(r0.x);
  r0.x = 1 + -r0.x;
  r0.z = log2(r0.y);
  r0.y = cmp(r0.y < 9.99999997e-007);
  r0.w = UniformPixelScalars_2.y * -30 + 30;
  r0.z = r0.w * r0.z;
  r0.z = exp2(r0.z);
  r0.y = r0.y ? 0 : r0.z;
  r0.z = log2(abs(r0.x));
  r0.x = cmp(abs(r0.x) < 9.99999997e-007);
  r0.w = 30 * UniformPixelScalars_3.y;
  r0.z = r0.w * r0.z;
  r0.z = exp2(r0.z);
  r0.x = r0.x ? 0 : r0.z;
  r0.x = r0.y * r0.x;
  r0.y = cmp(0.75 < UniformPixelScalars_4.z);
  r0.y = r0.y ? 1.000000 : 0;
  r0.x = r0.x * r0.y;
  r0.xyz = UniformPixelVector_4.xyz * r0.xxx;
  r0.w = dot(v3.xyz, v3.xyz);
  r0.w = rsqrt(r0.w);
  r0.w = v3.z * r0.w;
  r0.w = max(0, r0.w);
  r0.w = 1 + -r0.w;
  r1.x = rsqrt(abs(r0.w));
  r0.w = cmp(abs(r0.w) < 9.99999997e-007);
  r1.x = 1 / r1.x;
  r0.w = r0.w ? 0 : r1.x;
  r1.x = log2(r0.w);
  r0.w = cmp(r0.w < 9.99999997e-007);
  r1.x = 1.35000002 * r1.x;
  r1.x = exp2(r1.x);
  r1.x = 5 * r1.x;
  r0.w = r0.w ? 0 : r1.x;
  r0.xyz = r0.www * r0.xyz + UniformPixelVector_5.xyz;
  o0.xyz = saturate(v1.www * r0.xyz); // Saturate cause it gets boosted by the bloom.
  o0.w = 0;
  return;
}