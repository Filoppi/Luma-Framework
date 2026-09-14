Texture2D<float4> t0 : register(t0);

SamplerState s0_s : register(s0);

cbuffer cb0 : register(b0)
{
  float4 cb0[1];
}

void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : COLOR0,
  float2 v2 : TEXCOORD0,
  out float4 o0 : SV_TARGET0)
{
  float4 r0,r1;
  r0.xyz = t0.Sample(s0_s, v2.xy).xzw;
  r0.w = (0 >= r0.z);
  if (r0.w != 0) discard;
  r0.w = (r0.y < cb0[0].x);
  if (r0.w != 0) discard;
  r0.w = (cb0[0].z < 1);
  r0.y = -cb0[0].x + r0.y;
  r0.y = saturate(cb0[0].z * abs(r0.y));
  r0.y = r0.z * r0.y;
  r1.w = saturate(r0.w ? r0.y : r0.z);
  r1.xyz = saturate(r0.x);
  o0.xyzw = v1.xyzw * r1.xyzw;
}