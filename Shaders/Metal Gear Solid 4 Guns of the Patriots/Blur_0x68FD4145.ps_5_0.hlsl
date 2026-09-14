Texture2D<float4> t0 : register(t0);

SamplerState s0_s : register(s0);

cbuffer cb0 : register(b0)
{
  float4 cb0[2];
}

float2 AdjustUV(float2 shiftedUV, float2 originalUV, float horScale)
{
  return ((shiftedUV - originalUV) * float2(horScale, 1.0)) + originalUV;
}

void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : COLOR0,
  // TODO1: fix these in the VS
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
  float horScale = 1.0;

  // TODO1: feed in "horScale" from c++! Bloom horizontal scale factor.
  float2 outputPixelSize = abs(ddx(v2.xy)) + abs(ddy(v2.xy));
  float2 outputResolution = round(rcp(outputPixelSize));
  if ((outputResolution.x * 1.0) == outputResolution.y)
  {
    horScale = 0.5;
  }

  float4 r0,r1;
  r0.xyzw = t0.Sample(s0_s, AdjustUV(v3.xy, v2.xy, horScale)).xyzw;
  r0.xyzw = cb0[0].yyyy * r0.xyzw;
  r1.xyzw = t0.Sample(s0_s, v2.xy).xyzw;
  r0.xyzw = r1.xyzw * cb0[0].xxxx + r0.xyzw;
  r1.xyzw = t0.Sample(s0_s, AdjustUV(v3.zw, v2.xy, horScale)).xyzw;
  r0.xyzw = r1.xyzw * cb0[0].yyyy + r0.xyzw;
  r1.xyzw = t0.Sample(s0_s, AdjustUV(v4.xy, v2.xy, horScale)).xyzw;
  r0.xyzw = r1.xyzw * cb0[0].zzzz + r0.xyzw;
  r1.xyzw = t0.Sample(s0_s, AdjustUV(v4.zw, v2.xy, horScale)).xyzw;
  r0.xyzw = r1.xyzw * cb0[0].zzzz + r0.xyzw;
  r1.xyzw = t0.Sample(s0_s, AdjustUV(v5.xy, v2.xy, horScale)).xyzw;
  r0.xyzw = r1.xyzw * cb0[0].wwww + r0.xyzw;
  r1.xyzw = t0.Sample(s0_s, AdjustUV(v5.zw, v2.xy, horScale)).xyzw;
  r0.xyzw = r1.xyzw * cb0[0].wwww + r0.xyzw;
  r1.xyzw = t0.Sample(s0_s, AdjustUV(v6.xy, v2.xy, horScale)).xyzw;
  r0.xyzw = r1.xyzw * cb0[1].xxxx + r0.xyzw;
  r1.xyzw = t0.Sample(s0_s, AdjustUV(v6.zw, v2.xy, horScale)).xyzw;
  r0.xyzw = r1.xyzw * cb0[1].xxxx + r0.xyzw;
  r1.xyzw = t0.Sample(s0_s, AdjustUV(v7.xy, v2.xy, horScale)).xyzw;
  r0.xyzw = r1.xyzw * cb0[1].yyyy + r0.xyzw;
  r1.xyzw = t0.Sample(s0_s, AdjustUV(v7.zw, v2.xy, horScale)).xyzw;
  r0.xyzw = r1.xyzw * cb0[1].yyyy + r0.xyzw;
  r1.xyzw = t0.Sample(s0_s, AdjustUV(v8.xy, v2.xy, horScale)).xyzw;
  r0.xyzw = r1.xyzw * cb0[1].zzzz + r0.xyzw;
  r1.xyzw = t0.Sample(s0_s, AdjustUV(v8.zw, v2.xy, horScale)).xyzw;
  r0.xyzw = r1.xyzw * cb0[1].zzzz + r0.xyzw;
  r1.xyzw = t0.Sample(s0_s, AdjustUV(v9.xy, v2.xy, horScale)).xyzw;
  r0.xyzw = r1.xyzw * cb0[1].wwww + r0.xyzw;
  r1.xyzw = t0.Sample(s0_s, AdjustUV(v9.zw, v2.xy, horScale)).xyzw;
  r0.xyzw = r1.xyzw * cb0[1].wwww + r0.xyzw;
  r0.xyzw = v1.xyzw * r0.xyzw;
  o0.xyzw = v2.zzzz * r0.xyzw;
}