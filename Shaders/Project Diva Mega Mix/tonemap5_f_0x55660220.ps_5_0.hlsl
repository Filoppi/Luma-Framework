// ---- Created with 3Dmigoto v1.3.16 on Tue Sep 02 12:56:46 2025

cbuffer ToneMap : register(b1)
{
  float4 g_exposure : packoffset(c0);
  float4 g_fade_color : packoffset(c1);
  float4 g_tone_scale : packoffset(c2);
  float4 g_tone_offset : packoffset(c3);
  float4 g_texcoord_transforms[4] : packoffset(c4);
}

SamplerState g_samplers_0__s : register(s0);
SamplerState g_samplers_1__s : register(s1);
Texture2D<float4> g_textures_0_ : register(t0); //color
Texture2D<float4> g_textures_1_ : register(t1); //bloom


// 3Dmigoto declarations
#define cmp -
#include "./common1.hlsl"

//Color + Bloom + Tonemap (Simple) + Fade
//Piano Girl: toon scenes
void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  float4 v3 : TEXCOORD2,
    //z: bloom threshold
    //x: exposure
  float4 v4 : TEXCOORD3,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4;
  uint4 bitmask, uiDest;
  float4 fDest;

  float3 colorUntonemapped, colorTonemapped;

  //sample bloom
  r0.xyz = g_textures_1_.Sample(g_samplers_1__s, v1.zw).xyz * GS.BloomStrength;
  
  //sample color
  r0.w = 0.479999989 * v3.x; //color scaler (not related to sampled bloom)
  r1.xyzw = g_textures_0_.Sample(g_samplers_0__s, v1.xy).xyzw; //sample color
  r1.xyz = r1.xyz * r0.www; //color scale
  o0.w = r1.w; //out w (used 3d color mask)

  //color + bloom
  r0.xyz = r0.xyz * r0.www + r1.xyz; //scale bloom then add color 

  //bloom or not
  // r0.xyz = min(float3(0.959999979,0.959999979,0.959999979), r0.xyz);
  r0.w = cmp(0 < v3.z);
  r0.xyz = r0.www ? r0.xyz : r1.xyz;

  colorUntonemapped = r0.xyz;
  colorUntonemapped = gamma_to_linear(colorUntonemapped, GCT_POSITIVE, 2.2);

  //tonemapper (simple)
  r0.xyz = /* saturate */(r0.xyz * g_tone_scale.xyz + g_tone_offset.xyz); //clamp with color grade
  
  r0.xyz = Tonemap_Do(colorUntonemapped, r0.xyz, v1.xy, g_textures_0_, true);

  //fade
  {
    r1.xyz = g_fade_color.xyz + -r0.xyz;
    r1.xyz = g_fade_color.www * r1.xyz + r0.xyz;
    r2.xyz = g_fade_color.xyz + r0.xyz;
    r3.xyz = g_fade_color.xyz * r0.xyz;
    r4.xy = cmp(g_tone_scale.ww == float2(0,2));
    r2.xyz = r4.yyy ? r2.xyz : r3.xyz;
    r1.xyz = r4.xxx ? r1.xyz : r2.xyz;
    r0.w = cmp(0 < g_fade_color.w);
    o0.xyz = r0.www ? r1.xyz : r0.xyz; //out xyz
  }


  return;
}