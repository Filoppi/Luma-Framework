// ---- Created with 3Dmigoto v1.3.16 on Sun Aug 31 23:28:36 2025

cbuffer Quad : register(b0)
{
  float4 g_texcoord_modifier : packoffset(c0);
  float4 g_texel_size : packoffset(c1);
  float4 g_color : packoffset(c2);
  float4 g_texture_lod : packoffset(c3);
}

SamplerState g_sampler_s : register(s0);
Texture2D<float4> g_texture : register(t0);

// 3Dmigoto declarations
#define cmp -

//copies tex, controling texCoord & LOD
void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  o0 = g_texture.SampleLevel(g_sampler_s, v1.xy, g_texture_lod.x).xyzw;
  o0 = max(0, o0); o0.w = min(o0.w, 1); // clean

  return;
}