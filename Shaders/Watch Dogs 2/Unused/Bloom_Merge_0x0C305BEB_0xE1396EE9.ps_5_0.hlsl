cbuffer MergerTarget : register(b0)
{
  float2 TargetSizes : packoffset(c0);
}

cbuffer MergerSources : register(b1)
{
  float4 MergeValues : packoffset(c0);
  float4 Sizes[18] : packoffset(c1);
  float Weights[18] : packoffset(c19);
}

SamplerState ColorClamp_s : register(s0);
Texture2D<float4> MergerSources__Source00__TexObj__ : register(t0);
Texture2D<float4> MergerSources__Source01__TexObj__ : register(t1);
Texture2D<float4> MergerSources__Source02__TexObj__ : register(t2);
Texture2D<float4> MergerSources__Source03__TexObj__ : register(t3);

// Luma: unchanged
void main(
  float2 v0 : TEXCOORD0,
  float4 v1 : SV_Position0,
  out float4 o0 : SV_Target0)
{
  float2 r0 = v1.xy / TargetSizes.xy;
  float4 baseColor = MergerSources__Source00__TexObj__.SampleLevel(ColorClamp_s, r0.xy, 0).rgba;
  float3 color = baseColor.rgb * Weights[0];
  color += MergerSources__Source01__TexObj__.SampleLevel(ColorClamp_s, r0.xy, 0).rgb * Weights[1];
  color += MergerSources__Source02__TexObj__.SampleLevel(ColorClamp_s, r0.xy, 0).rgb * Weights[2];
#if _0C305BEB
  color += MergerSources__Source03__TexObj__.SampleLevel(ColorClamp_s, r0.xy, 0).rgb * Weights[3];
  color /= 5.0;
#else // _E1396EE9
  color /= 4.0;
#endif
#if 1 // Luma: Remove unnecessary clamp? If these go negative, you probably wouldn't want to subtract colors (well, unless they nicely expanded saturation and kept a >= 0 luminance!)
  color = max(color, 0.0);
#endif
  o0.xyzw = float4(color, baseColor.a);
}