Texture2D<float4> t1 : register(t1);
Texture2D<float4> t0 : register(t0);

SamplerState s1_s : register(s1);
SamplerState s0_s : register(s0);

void main(
  float4 v0 : SV_POSITION,
  float4 v1 : COLOR0,
  float2 v2 : TEXCOORD0,
  float4 v3 : TEXCOORD1,
  out float4 o0 : SV_TARGET0)
{
  float4 r0,r1;
  r0.xyzw = t1.Sample(s1_s, v2.xy).xyzw;
  r0.xyz = r0.xyz / r0.w;
  r1.xyzw = t0.Sample(s0_s, v2.xy).xyzw;
  r0.xyz = r1.w * r0.xyz;
  o0.xyz = r0.xyz * 0.25 + r1.xyz;
  o0.w = 1;
}