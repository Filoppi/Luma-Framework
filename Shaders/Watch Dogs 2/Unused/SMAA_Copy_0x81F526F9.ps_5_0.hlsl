SamplerState ColorPointClamp_s : register(s0);
Texture2D<float4> PostFxSMAA__ColorTexture__TexObj__ : register(t0);

void main(
  linear centroid float2 v0 : TEXCOORD0,
  float4 v1 : SV_Position0,
  out float4 o0 : SV_Target0)
{
  float4 r0;
  r0.xyz = PostFxSMAA__ColorTexture__TexObj__.SampleLevel(ColorPointClamp_s, v0.xy, 0, int2(0, 0)).xyz;
#if 1 // Luma: removed random abs that breaks HDR colors
  o0.xyz = r0.xyz;
#else
  o0.xyz = abs(r0.xyz);
#endif
  o0.w = 1;
}