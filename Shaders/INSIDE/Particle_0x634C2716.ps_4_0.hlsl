Texture2D<float4> t1 : register(t1);
Texture2D<float4> t0 : register(t0);

SamplerState s1_s : register(s1);
SamplerState s0_s : register(s0);

cbuffer cb1 : register(b1)
{
  float4 cb1[8];
}

cbuffer cb0 : register(b0)
{
  float4 cb0[9];
}

void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : COLOR0,
  float2 v2 : TEXCOORD0,
  float4 v3 : TEXCOORD1,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1;
  r0.xy = v3.xy / v3.ww;
  r0.xyzw = t0.Sample(s1_s, r0.xy).xyzw;
  r0.x = cb1[7].z * r0.x + cb1[7].w;
  r0.x = 1 / r0.x;
  r0.x = -v3.z + r0.x;
  r0.x = saturate(cb0[8].x * r0.x);
  r0.w = v1.w * r0.x;
  r0.xyz = v1.xyz;
  r0.xyzw = r0.xyzw + r0.xyzw;
  r0.xyzw = cb0[7].xyzw * r0.xyzw;
  r1.xyzw = t1.Sample(s0_s, v2.xy).xyzw;
  r0.xyzw = r1.xyzw * r0.xyzw;
  o0.xyz = r0.xyz * r0.www;
  o0.w = r0.w;
  
  // Luma: typical UNORM like clamping
  o0.rgb = max(o0.rgb, 0.0);
  o0.a = saturate(o0.a);
}