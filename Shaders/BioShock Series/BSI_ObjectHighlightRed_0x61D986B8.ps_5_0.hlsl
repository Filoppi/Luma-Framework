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
  float4 UniformPixelVector_8 : packoffset(c27);
  float4 UniformPixelVector_9 : packoffset(c28);
  float4 UniformPixelVector_10 : packoffset(c29);
  float4 UniformPixelScalars_0 : packoffset(c30);
  float4 UniformPixelScalars_1 : packoffset(c31);
  float4 UniformPixelScalars_2 : packoffset(c32);
  float4 UniformPixelScalars_3 : packoffset(c33);
  float4 UniformPixelScalars_4 : packoffset(c34);
  float4 UniformPixelScalars_5 : packoffset(c35);
  float4 UniformVertexScalars_0 : packoffset(c36);
  float LocalToWorldRotDeterminantFlip : packoffset(c37);
  float4 LightmapCoordinateScaleBias : packoffset(c38);
  float3x3 WorldToLocal : packoffset(c39);
  float3 MeshOrigin : packoffset(c42);
  float3 MeshExtension : packoffset(c43);
  float3 IrradianceUVWScale : packoffset(c44);
  float3 IrradianceUVWBias : packoffset(c45);
  float4 LightVolumeScaleAndAverageDynamicSkylightIntensity : packoffset(c46);
  float3 CubeMapHDRScale : packoffset(c47);
  float3 LightMapCofficientScale : packoffset(c48);
  float4 GlobalLightMapScaleAndAverageDynamicSkylightIntensity : packoffset(c49);
  float4 BaseVolumeRawBand0 : packoffset(c50);
  float4 BaseVolumeRawLinearSH : packoffset(c51);
}

cbuffer PSOffsetConstants : register(b2)
{
  float4 ScreenPositionScaleBias : packoffset(c0);
  float4 MinZ_MaxZRatio : packoffset(c1);
  float NvStereoEnabled : packoffset(c2);
}

SamplerState Texture2D_0_s : register(s0);
SamplerState Texture2D_1_s : register(s1);
SamplerState MaterialBufferTexture_s : register(s2);
SamplerState CubeMapProbeTexture_s : register(s3);
SamplerState DiffuseLightingTexture_s : register(s4);
SamplerState SpecularLightingTexture_s : register(s5);
SamplerState IrradianceVolumeTextureBand0SH_s : register(s6);
SamplerState IrradianceVolumeTextureLinearSH_s : register(s7);
Texture3D<float4> IrradianceVolumeTextureBand0SH : register(t0);
Texture3D<float4> IrradianceVolumeTextureLinearSH : register(t1);
Texture2D<float4> MaterialBufferTexture : register(t2);
Texture2D<float4> Texture2D_1 : register(t3);
Texture2D<float4> Texture2D_0 : register(t4);
Texture2D<float4> DiffuseLightingTexture : register(t5);
Texture2D<float4> SpecularLightingTexture : register(t6);
TextureCube<float4> CubeMapProbeTexture : register(t7);


// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : COLOR0,
  float4 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD5,
  float4 v3 : TEXCOORD6,
  float4 v4 : TEXCOORD7,
  float4 v5 : TEXCOORD8,
  uint v6 : SV_IsFrontFace0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4,r5;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xyzw = IrradianceVolumeTextureBand0SH.Sample(IrradianceVolumeTextureBand0SH_s, v5.xyz).xyzw;
  r1.xyzw = BaseVolumeRawBand0.xyzw + -r0.xyzw;
  r2.x = saturate(v5.w);
  r0.xyzw = r2.xxxx * r1.xyzw + r0.xyzw;
  r1.x = r0.w;
  r3.xyzw = IrradianceVolumeTextureLinearSH.Sample(IrradianceVolumeTextureLinearSH_s, v5.xyz).xyzw;
  r4.xyzw = BaseVolumeRawLinearSH.xyzw + -r3.xyzw;
  r2.xyzw = r2.xxxx * r4.xyzw + r3.xyzw;
  r1.y = r2.w;
  r2.xyz = r2.xyz * float3(2,2,2) + float3(-1,-1,-1);
  r1.xy = r1.xy * r1.xy;
  r0.w = 10 * r1.y;
  r1.x = r1.x * 10 + 0.100000001;
  r0.xyz = r1.xxx * r0.xyz;
  r1.yzw = r2.xyz * r0.www;
  r0.w = dot(r0.xyz, r0.xyz);
  r0.w = sqrt(r0.w);
  r1.x = 9.99999975e-005 + r0.w;
  r2.xyzw = float4(-1,-1,1,0.295409203) * r1.wyzx;
  r0.w = dot(r2.xyz, r2.xyz);
  r0.w = sqrt(r0.w);
  r2.xyz = r2.xyz / r0.www;
  r0.w = cmp(r0.w == 0.000000);
  r2.xyz = r0.www ? float3(0,0,0) : r2.xyz;
  r3.yzw = float3(-0.488602996,0.488602996,-0.488602996) * r2.yzx;
  r3.x = 0.282094985;
  r0.w = dot(r3.xyzw, r1.xyzw);
  r0.w = max(0, r0.w);
  r3.xyzw = -r3.xyzw * r0.wwww + r1.xyzw;
  r0.xyz = r0.xyz / r1.xxx;
  r0.xyz = LightVolumeScaleAndAverageDynamicSkylightIntensity.xyz * r0.xyz;
  r1.x = 3.14159274;
  r4.xy = v2.xy / v2.ww;
  r4.xy = r4.xy * ScreenPositionScaleBias.xy + ScreenPositionScaleBias.wz;
  r5.xyz = MaterialBufferTexture.Sample(MaterialBufferTexture_s, r4.xy).xyz;
  r5.xyz = r5.yzx * float3(2,2,2) + float3(-1,-1,-1);
  r4.z = dot(r5.xyz, r5.xyz);
  r4.z = rsqrt(r4.z);
  r1.yzw = r5.xyz * r4.zzz;
  r5.xyzw = float4(0.282094985,-1.02332771,1.02332771,-1.02332771) * r1.xyzw;
  r1.x = dot(r5.xyzw, r3.xyzw);
  r1.x = max(0, r1.x);
  r3.xyzw = r1.xxxx * r0.xyzz;
  r5.xyzw = r0.xyzz * r0.wwww;
  r0.x = dot(r2.www, r0.xyz);
  r0.y = saturate(dot(r2.yzx, r1.yzw));
  r2.xyzw = r0.yyyy * r5.xyzw + r3.xyzw;
  r0.yzw = DiffuseLightingTexture.Sample(DiffuseLightingTexture_s, r4.xy).xyz;
  r3.xyz = SpecularLightingTexture.Sample(SpecularLightingTexture_s, r4.xy).xyz;
  r2.xyzw = r0.yzww + r2.xyzw;
  r0.yzw = Texture2D_1.Sample(Texture2D_1_s, v1.xy).xyz;
  r4.xyzw = UniformPixelVector_8.xyzz * r0.yzww;
  r0.yz = Texture2D_0.Sample(Texture2D_0_s, v1.xy).yz;
  r0.w = r0.y * r0.y;
  r0.w = r0.w * 512 + 5;
  r0.w = 0.159154937 * r0.w;
  r5.xyzw = UniformPixelVector_10.xyzz * r0.wwww;
  r3.xyzw = r5.xyzw * r3.xyzz;
  r2.xyzw = r4.xyzw * r2.xyzw + r3.xyzw;
  r0.w = LightVolumeScaleAndAverageDynamicSkylightIntensity.w + r0.x;
  r0.x = 0.100000001 * r0.x;
  r0.x = max(r0.w, r0.x);
  r0.w = dot(-v4.xyz, -v4.xyz);
  r0.w = rsqrt(r0.w);
  r3.xyz = -v4.xyz * r0.www;
  r0.w = dot(r1.wyz, r3.xyz);
  r1.xyz = r1.wyz * r0.www;
  r1.xyz = r1.xyz * float3(2,2,2) + -r3.xyz;
  r0.y = 1 + -r0.y;
  r3.xyzw = UniformPixelVector_2.xyzz * r0.zzzz;
  r0.y = 5 * r0.y;
  r0.yzw = CubeMapProbeTexture.SampleLevel(CubeMapProbeTexture_s, r1.xyz, r0.y).xyz;
  r1.xyzw = CubeMapHDRScale.xyzz * r0.yzww;
  r1.xyzw = UniformPixelVector_10.xyzz * r1.xyzw;
  r0.xyzw = r0.xxxx * r1.xyzw + r2.xyzw;
  r1.xy = CameraWorldPos.xz + v4.xz;
  r1.zw = float2(-100,-50) + ObjectWorldPositionAndRadius.xz;
  r1.xy = r1.xy + -r1.zw;
  r1.z = 0.00249999994 * ObjectWorldPositionAndRadius.w;
  r1.xy = r1.xy * float2(0.00300000003,0.00300000003) + r1.zz;
  r1.x = r1.x + -r1.y;
  r1.x = UniformPixelScalars_0.z * r1.x + r1.y;
  r1.y = saturate(r1.x);
  r1.x = 1 + -r1.x;
  r1.z = log2(r1.y);
  r1.y = cmp(r1.y < 9.99999997e-007);
  r1.w = UniformPixelScalars_2.z * -30 + 30;
  r1.z = r1.w * r1.z;
  r1.z = exp2(r1.z);
  r1.y = r1.y ? 0 : r1.z;
  r1.z = log2(abs(r1.x));
  r1.x = cmp(abs(r1.x) < 9.99999997e-007);
  r1.w = 30 * UniformPixelScalars_3.z;
  r1.z = r1.w * r1.z;
  r1.z = exp2(r1.z);
  r1.x = r1.x ? 0 : r1.z;
  r1.x = r1.y * r1.x;
  r1.y = cmp(0.75 < UniformPixelScalars_4.w);
  r1.y = r1.y ? 1.000000 : 0;
  r1.x = r1.x * r1.y;
  r1.xyzw = UniformPixelVector_6.xyzz * r1.xxxx;
  r2.x = dot(v3.xyz, v3.xyz);
  r2.x = rsqrt(r2.x);
  r2.x = v3.z * r2.x;
  r2.x = max(0, r2.x);
  r2.x = 1 + -r2.x;
  r2.y = rsqrt(abs(r2.x));
  r2.x = cmp(abs(r2.x) < 9.99999997e-007);
  r2.y = 1 / r2.y;
  r2.x = r2.x ? 0 : r2.y;
  r2.y = log2(r2.x);
  r2.x = cmp(r2.x < 9.99999997e-007);
  r2.y = 1.35000002 * r2.y;
  r2.y = exp2(r2.y);
  r2.y = 5 * r2.y;
  r2.x = r2.x ? 0 : r2.y;
  r1.xyzw = r2.xxxx * r1.xyzw;
  r1.xyzw = v0.xxxx * r1.xyzw;
  r1.xyzw = UniformPixelScalars_5.xxxx * r1.xyzw;
  r1.xyzw = UniformPixelScalars_0.xxxx * r3.xyzw + r1.xyzw;
  r1.xyzw = UniformPixelVector_7.xyzz + r1.xyzw;
  o0.xyzw = saturate(r1.xyzw + r0.xyzw); // Saturate cause it gets boosted by the bloom.
  return;
}