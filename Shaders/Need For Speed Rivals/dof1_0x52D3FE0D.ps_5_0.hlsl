// ---- Created with 3Dmigoto v1.3.16 on Wed Jul 22 13:49:00 2026

cbuffer ShaderConstants : register(b0)
{
  float g_OutFocusStart : packoffset(c0);
  float g_OutFocusEnd : packoffset(c0.y);
  float g_MaxFarCoCSize : packoffset(c0.z);
  float g_AddBlur : packoffset(c0.w);
  uint2 g_FullScreenSize : packoffset(c1);
  float2 g_FullPixelSize : packoffset(c1.z);
  float2 g_BokehTextureOffset : packoffset(c2);
  float g_EnergyScale : packoffset(c2.z);
  float g_InfocusMultiplier : packoffset(c2.w);
  float2 g_MinRadiusToLayer4 : packoffset(c3);
  float2 g_MinRadiusToLayer8 : packoffset(c3.z);
  float g_MergeRadiusThreshold : packoffset(c4);
  float g_MergeColorThreshold : packoffset(c4.y);
  float g_DepthDiscontinuityThreshold : packoffset(c4.z);
  float g_DepthFileterEnable : packoffset(c4.w);
  float2 g_VignetteBlurScale : packoffset(c5);
  float g_VignetteBlurExponent : packoffset(c5.z);
  uint g_MaxExtraBlurSize : packoffset(c5.w);
  uint2 g_CurrentScreenSize : packoffset(c6);
  float2 g_CurrentPixelSize : packoffset(c6.z);
  uint2 g_EnergyPixelCoord : packoffset(c7);
  float g_EnergyRadius : packoffset(c7.z);
  float g_energyPad : packoffset(c7.w);
}

SamplerState g_LinearSampler_s : register(s0);
Texture2D<float4> g_RGBTexture : register(t0);
Texture2D<float> g_LinearZTexture : register(t1);
Texture2D<float4> g_LayerTexture0 : register(t2); //dof 0
Texture2D<float4> g_LayerTexture1 : register(t3); //dof 1 (Blank?)
Texture2D<float4> g_LayerTexture2 : register(t4); //dof 2 (Blank?)


// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4;
  uint4 bitmask, uiDest;
  float4 fDest;
  o0.w = 1;

  r0.xy = (int2)v0.xy;
  r0.zw = float2(0,0);
  r1.x = g_LinearZTexture.Load(r0.xyz).x;
  // o0.xyz = r1.x; return; //debug 

  r0.xyz = g_RGBTexture.Load(r0.xyz).xyz;
  r0.xyz = g_InfocusMultiplier * r0.xyz;

  r1.yz = trunc(v0.xy);
  r1.w = cmp(0 < g_VignetteBlurScale.x);
  r1.w = r1.w ? 1.000000 : 0;
  r1.yz = r1.yz * g_FullPixelSize.xy + float2(-0.5,-0.5);
  r1.yz = g_VignetteBlurScale.xy * r1.yz;
  r1.y = dot(r1.yz, r1.yz);
  r1.y = log2(r1.y);
  r1.y = g_VignetteBlurExponent * r1.y;
  r1.y = exp2(r1.y);
  r1.y = saturate(r1.y * r1.w + g_AddBlur);
  r1.x = -g_OutFocusStart + r1.x;
  r1.z = g_OutFocusEnd + -g_OutFocusStart;
  r1.z = saturate(r1.x / r1.z);
  r1.w = g_MaxExtraBlurSize;
  r1.w = dot(r1.yy, r1.ww);
  r1.z = r1.z * g_MaxFarCoCSize + r1.w;
  r1.y = cmp(0 < r1.y);
  r1.xw = cmp(float2(0,2) < r1.xz);
  r1.x = (int)r1.x | (int)r1.y;
  r1.x = r1.x ? r1.w : 0;
  r1.yw = g_FullPixelSize.xy * v0.xy;

  r2.xyzw = g_LayerTexture0.Sample(g_LinearSampler_s, r1.yw).xyzw; //iirc 0.5
  r3.xyzw = g_LayerTexture1.Sample(g_LinearSampler_s, r1.yw).xyzw; //0.25
  r4.xyzw = g_LayerTexture2.Sample(g_LinearSampler_s, r1.yw).xyzw; //0.125
  
  #if TONEMAP_DOF == 1
    bool valid = r2.w > 0.00001 || r3.w > 0.00001 || r4.w > 0.00001;

    if (r1.x != 0 && valid) {
      r2.xyzw = r3.xyzw + r2.xyzw;
      r2.xyzw = r2.xyzw + r4.xyzw;

      r1.x = r1.z * 0.5 + -1;
      r1.x = min(1, r1.x);

      r0.w = 1;

      r2.xyzw = r2.xyzw + -r0.xyzw;
      r0.xyzw = r1.xxxx * r2.xyzw + r0.xyzw;

      r1.x = cmp(9.99999975e-006 < r0.w);
      if (r1.x != 0) {
        r0.xyz = r0.xyz / r0.www;
      }
    }
  #endif

  o0.xyz = r0.xyz;
  return;
}