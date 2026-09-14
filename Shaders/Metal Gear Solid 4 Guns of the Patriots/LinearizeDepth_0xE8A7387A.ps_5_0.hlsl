Texture2D<float4> t0 : register(t0);

SamplerState s0_s : register(s0);

void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : COLOR0,
  float2 v2 : TEXCOORD0,
  float4 v3 : TEXCOORD1,
  out float1 o0 : SV_TARGET0)
{
  float1 r0;
  r0.x = t0.Sample(s0_s, v2.xy).x;
  r0.x = -v3.x + r0.x;
  o0.x = v3.y / r0.x;
}