#include "../Includes/Common.hlsl"

cbuffer ShaderParams : register(b1)
{
  float3 gFogColor : packoffset(c0);
  uint gFogEnabled : packoffset(c0.w);
  float4 gTextureFactor : packoffset(c1);
  uint gAlphaTestEnabled : packoffset(c2);
  float gAlphaRef : packoffset(c2.y);
  uint gAlphaFunc : packoffset(c2.z);
  uint gAlphaOP : packoffset(c2.w);
  uint gAlphaARG0 : packoffset(c3);
  uint gAlphaARG1 : packoffset(c3.y);
  uint gAlphaARG2 : packoffset(c3.z);
  uint gSpecularEnable : packoffset(c3.w);
  uint gRenderMode : packoffset(c4);
  float gDepthBias : packoffset(c4.y);
  uint gUseEnvMap : packoffset(c4.z);
  uint gDisableReflex : packoffset(c4.w);
  float4 Constant0_0 : packoffset(c5);
  float4 Constant0_1 : packoffset(c6);
  float4 Constant0_2 : packoffset(c7);
  float4 Constant0_3 : packoffset(c8);
  float4 Constant0_4 : packoffset(c9);
  float4 Constant0_5 : packoffset(c10);
  float4 Constant0_6 : packoffset(c11);
  float4 Constant0_7 : packoffset(c12);
  float4 Constant1_0 : packoffset(c13);
  float4 Constant1_1 : packoffset(c14);
  float4 Constant1_2 : packoffset(c15);
  float4 Constant1_3 : packoffset(c16);
  float4 Constant1_4 : packoffset(c17);
  float4 Constant1_5 : packoffset(c18);
  float4 Constant1_6 : packoffset(c19);
  float4 Constant1_7 : packoffset(c20);
  float4 c[117] : packoffset(c21);
}

SamplerState DiffuseSampler_s : register(s0);
Texture2D<float4> diffuseTexture : register(t0);

#define cmp

void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  float w1 : FOG0,
  float4 v2 : COLOR0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2;
  r0.xy = cmp(gRenderMode == uint2(0,3));
  r0.x = asfloat(asint(r0.y) | asint(r0.x));
  if (r0.x != 0) {
    r0.xyzw = diffuseTexture.Sample(DiffuseSampler_s, v1.xy).xyzw;
    r1.xyz = v2.xyz * r0.xyz;
    switch (gAlphaARG1) {
      case 0 :      r1.w = v2.w;
      break;
      case 1 :      r1.w = v2.w;
      break;
      case 2 :      r1.w = r0.w;
      break;
      case 3 :      r1.w = gTextureFactor.w;
      break;
      default :
      r1.w = 0;
      break;
    }
    switch (gAlphaARG2) {
      case 0 :      r0.w = v2.w;
      break;
      case 1 :      r0.w = v2.w;
      break;
      case 2 :      break;
      case 3 :      r0.w = gTextureFactor.w;
      break;
      default :
      r0.w = 0;
      break;
    }
    switch (gAlphaOP) {
      case 2 :      r0.w = r1.w;
      break;
      case 3 :      break;
      default :
      r0.w = r1.w * r0.w;
      break;
    }
  } else {
    r1.xyz = v2.xyz;
    r0.w = v2.w;
  }
  if (gAlphaTestEnabled != 0) {
    switch (gAlphaFunc) {
      case 0 :      if (-1 != 0) discard;
      break;
      case 1 :      r1.w = cmp(r0.w < gAlphaRef);
      if (r1.w == 0) discard;
      break;
      case 2 :      r1.w = -gAlphaRef + r0.w;
      r1.w = cmp(abs(r1.w) < 0.025);
      if (r1.w == 0) discard;
      break;
      case 3 :      r1.w = cmp(gAlphaRef >= r0.w);
      if (r1.w == 0) discard;
      break;
      case 4 :      r1.w = cmp(gAlphaRef < r0.w);
      if (r1.w == 0) discard;
      break;
      case 5 :      r1.w = -gAlphaRef + r0.w;
      r1.w = cmp(0.00001 < abs(r1.w));
      if (r1.w == 0) discard;
      break;
      case 6 :      r1.w = cmp(r0.w >= gAlphaRef);
      if (r1.w == 0) discard;
      break;
      case 7 :      break;
      default :
      break;
    }
  }
  r1.w = float(-int(gFogEnabled) + 1); // utof
  r1.w = max(w1.x, r1.w);
  r1.w = min(1, r1.w);
  r2.x = 1 - r1.w;
  r2.xyz = gFogColor.xyz * r2.xxx;
  r2.xyz = r1.w * r1.xyz + r2.xyz;
  r0.xyz = gRenderMode ? r1.xyz : r2.xyz;
  o0.xyzw = r0.xyzw;
  
#if ENABLE_HDR_BOOST
  if (gAlphaTestEnabled) // gRenderMode is almost always 0
  {
    o0.rgb = gamma_to_linear(o0.rgb);
    o0.rgb = PumboAutoHDR(o0.rgb, 250.0, LumaSettings.GamePaperWhiteNits) * 5;
    o0.rgb = linear_to_gamma(o0.rgb);
  }
#endif

  o0.w = saturate(o0.w); // Luma: UNORM emulation
}