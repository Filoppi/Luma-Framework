Texture2D<float4> t0 : register(t0);

SamplerState s0_s : register(s0);

cbuffer cb0 : register(b0)
{
  float4 cb0[2];
}

void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : COLOR0,
  float4 v2 : TEXCOORD0,
  float4 v3 : TEXCOORD1,
  float4 v4 : TEXCOORD2,
  float4 v5 : TEXCOORD3,
  float4 v6 : TEXCOORD4,
  float4 v7 : TEXCOORD5,
  float4 v8 : TEXCOORD6,
  float4 v9 : TEXCOORD7,
  out float4 o0 : SV_TARGET0)
{
  float4 r0,r1;
  r0.xyzw = t0.Sample(s0_s, v3.xy).xyzw;
  r0.xyzw = cb0[0].yyyy * r0.xyzw;
  r1.xyzw = t0.Sample(s0_s, v2.xy).xyzw;
  r0.xyzw = r1.xyzw * cb0[0].xxxx + r0.xyzw;
  r1.xyzw = t0.Sample(s0_s, v3.zw).xyzw;
  r0.xyzw = r1.xyzw * cb0[0].yyyy + r0.xyzw;
  r1.xyzw = t0.Sample(s0_s, v4.xy).xyzw;
  r0.xyzw = r1.xyzw * cb0[0].zzzz + r0.xyzw;
  r1.xyzw = t0.Sample(s0_s, v4.zw).xyzw;
  r0.xyzw = r1.xyzw * cb0[0].zzzz + r0.xyzw;
  r1.xyzw = t0.Sample(s0_s, v5.xy).xyzw;
  r0.xyzw = r1.xyzw * cb0[0].wwww + r0.xyzw;
  r1.xyzw = t0.Sample(s0_s, v5.zw).xyzw;
  r0.xyzw = r1.xyzw * cb0[0].wwww + r0.xyzw;
  r1.xyzw = t0.Sample(s0_s, v6.xy).xyzw;
  r0.xyzw = r1.xyzw * cb0[1].xxxx + r0.xyzw;
  r1.xyzw = t0.Sample(s0_s, v6.zw).xyzw;
  r0.xyzw = r1.xyzw * cb0[1].xxxx + r0.xyzw;
  r1.xyzw = t0.Sample(s0_s, v7.xy).xyzw;
  r0.xyzw = r1.xyzw * cb0[1].yyyy + r0.xyzw;
  r1.xyzw = t0.Sample(s0_s, v7.zw).xyzw;
  r0.xyzw = r1.xyzw * cb0[1].yyyy + r0.xyzw;
  r1.xyzw = t0.Sample(s0_s, v8.xy).xyzw;
  r0.xyzw = r1.xyzw * cb0[1].zzzz + r0.xyzw;
  r1.xyzw = t0.Sample(s0_s, v8.zw).xyzw;
  r0.xyzw = r1.xyzw * cb0[1].zzzz + r0.xyzw;
  r1.xyzw = t0.Sample(s0_s, v9.xy).xyzw;
  r0.xyzw = r1.xyzw * cb0[1].wwww + r0.xyzw;
  r1.xyzw = t0.Sample(s0_s, v9.zw).xyzw;
  r0.xyzw = r1.xyzw * cb0[1].wwww + r0.xyzw;
  r0.xyzw = v1.xyzw * r0.xyzw;
  o0.xyzw = v2.zzzz * r0.xyzw;
}