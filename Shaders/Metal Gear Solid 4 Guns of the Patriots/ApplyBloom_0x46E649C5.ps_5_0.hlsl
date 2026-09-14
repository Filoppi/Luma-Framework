Texture2D<float4> t6 : register(t6);
Texture2D<float4> t5 : register(t5);
Texture2D<float4> t4 : register(t4);
Texture2D<float4> t3 : register(t3);
Texture2D<float4> t2 : register(t2);
Texture2D<float4> t1 : register(t1);
Texture2D<float4> t0 : register(t0);

SamplerState s6_s : register(s6);
SamplerState s5_s : register(s5);
SamplerState s4_s : register(s4);
SamplerState s3_s : register(s3);
SamplerState s2_s : register(s2);
SamplerState s1_s : register(s1);
SamplerState s0_s : register(s0);

void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : COLOR0,
  float2 v2 : TEXCOORD0,
  out float4 o0 : SV_TARGET0)
{
  float4 r0,r1;
  r0.xyzw = t1.Sample(s1_s, v2.xy).xyzw;
  r0.xyz = r0.xyz / r0.www;
  r0.xyz = float3(0.020833334,0.020833334,0.020833334) * r0.xyz;
  r1.xyzw = t0.Sample(s0_s, v2.xy).xyzw;
  r1.xyz = r1.xyz / r1.www;
  r0.xyz = r1.xyz * float3(0.020833334,0.020833334,0.020833334) + r0.xyz;
  r1.xyzw = t2.Sample(s2_s, v2.xy).xyzw;
  r1.xyz = r1.xyz / r1.www;
  r0.xyz = r1.xyz * float3(0.027777778,0.027777778,0.027777778) + r0.xyz;
  r1.xyzw = t3.Sample(s3_s, v2.xy).xyzw;
  r1.xyz = r1.xyz / r1.www;
  r0.xyz = r1.xyz * float3(0.0347222239,0.0347222239,0.0347222239) + r0.xyz;
  r1.xyzw = t4.Sample(s4_s, v2.xy).xyzw;
  r1.xyz = r1.xyz / r1.www;
  r0.xyz = r1.xyz * float3(0.0416666679,0.0416666679,0.0416666679) + r0.xyz;
  r1.xyzw = t5.Sample(s5_s, v2.xy).xyzw;
  r1.xyz = r1.xyz / r1.www;
  r0.xyz = r1.xyz * float3(0.0486111119,0.0486111119,0.0486111119) + r0.xyz;
  r1.xyzw = t6.Sample(s6_s, v2.xy).xyzw;
  r1.xyz = r1.xyz / r1.www;
  r0.xyz = r1.xyz * float3(0.055555556,0.055555556,0.055555556) + r0.xyz;
  o0.xyz = v1.xyz * r0.xyz;
  o0.w = 1;
}