Texture2D<float4> t0 : register(t0);

SamplerState s0_s : register(s0);

cbuffer cb0 : register(b0)
{
  float4 cb0[3];
}

void main(
  float4 v0 : SV_POSITION0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_TARGET0)
{
  float4 r0;
  r0.xy = float2(1,1) / cb0[0].xy;
  r0.xy = cb0[2].xx + -r0.xy;
  r0.zw = -cb0[1].xy + v1.xy;
  r0.xy = r0.zw * r0.xy + cb0[1].xy;
  o0.xyzw = t0.Sample(s0_s, r0.xy).xyzw;
}