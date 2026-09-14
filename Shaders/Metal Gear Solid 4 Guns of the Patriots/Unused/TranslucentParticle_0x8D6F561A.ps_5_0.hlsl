Texture2D<float4> t1 : register(t1);
Texture2D<float4> t0 : register(t0);

SamplerState s1_s : register(s1);
SamplerState s0_s : register(s0);

cbuffer cb0 : register(b0)
{
  float4 cb0[4];
}

// This also draws in mirrors pre-rendered render targets
void main(
  float4 v0 : SV_POSITION0,
  float v1 : COLOR2,
  float2 w1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  float4 v3 : TEXCOORD2,
  nointerpolation int4 v4 : TEXCOORD4,
  float4 v5 : TEXCOORD5,
  float4 v6 : TEXCOORD7,
  out float4 o0 : SV_TARGET0)
{
  float4 r0,r1;
  r0.x = max(cb0[1].z, v1.x);
  r0.x = min(cb0[1].w, r0.x);
  r0.x = v5.y * r0.x;
  r1.xyzw = t0.Sample(s0_s, w1.xy).xyzw;
  r1.xyzw = v4.xxxx ? float4(1,1,1,1) : r1.xyzw;
  r1.xyzw = v6.xyzw * r1.xyzw;
  r0.yzw = cb0[2].xyz * v5.xxx + -r1.xyz;
  o0.xyz = r0.xxx * r0.yzw + r1.xyz;
  r0.xy = v0.xy / cb0[3].xy;
  r0.x = t1.Sample(s1_s, r0.xy).x;
  r0.x = -v5.w + r0.x;
  r0.x = saturate(v5.z * r0.x);
  r0.x = r1.w * r0.x;
  o0.w = (r0.x >= cb0[0].x) ? r0.x : 0.0;
#if 0 // Luma: UNORM range emulation (not needed)
  o0.w = saturate(o0.w);
#endif
}