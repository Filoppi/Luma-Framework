SamplerState PostFxSimple__TextureSampler__SampObj___s : register(s0);
Texture2D<float4> PostFxSimple__TextureSampler__TexObj__ : register(t0);

// Used by the "Temporal Filtering" setting only?
void main(
  linear centroid float2 v0 : TEXCOORD0,
  float4 v1 : SV_Position0,
  out float4 o0 : SV_Target0)
{
  o0.xyz = PostFxSimple__TextureSampler__TexObj__.Sample(PostFxSimple__TextureSampler__SampObj___s, v0.xy).xyz;
  o0.w = 1;
}