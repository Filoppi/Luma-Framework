#ifndef ENABLE_VIGNETTE
#define ENABLE_VIGNETTE 1
#endif

Texture2D<float4> t3 : register(t3);
Texture2D<float4> t2 : register(t2);
Texture2D<float4> t1 : register(t1);
Texture2D<float4> t0 : register(t0);

SamplerState s3_s : register(s3);
SamplerState s2_s : register(s2);
SamplerState s1_s : register(s1);
SamplerState s0_s : register(s0);

cbuffer cb3 : register(b3)
{
  float4 cb3[8];
}
cbuffer cb2 : register(b2)
{
  float4 cb2[1];
}
cbuffer cb1 : register(b1)
{
  float4 cb1[133];
}
cbuffer cb0 : register(b0)
{
  float4 cb0[18];
}

#define cmp

// Blends in vignette/UI and dirt effects
void main(
  float4 v0 : SV_Position0,
  out float4 o0 : SV_Target0)
{
  float2 screenUV = cb1[123].zw * v0.xy;

#if !ENABLE_VIGNETTE // Test: quick bypass
  o0 = t3.Sample(s3_s, screenUV);
  return;
#endif

  float4 r0,r1,r2;
  r0.xy = (float2)asuint(cb0[17].xy);
  r0.xy = v0.xy + -r0.xy;
  r0.zw = r0.xy * cb0[16].zw + float2(-0.5,-0.5);
  r0.z = dot(r0.zw, r0.zw);
  r0.z = sqrt(r0.z);
  r0.w = cb2[0].y * 1.5 + -0.5;
  r1.xy = r0.ww * float2(-2.5,-14) + float2(2,15);
  r0.z = r0.z / r1.y;
  r1.y = cb3[7].y * r0.z;
  r1.y = r1.y * r1.y;
  r1.y = 1.44269514 * r1.y;
  r1.y = exp2(r1.y);
  r1.y = 1 / r1.y;
  r1.z = cmp(r0.z >= 0);
  r0.z = cmp(9.99999975e-006 < abs(r0.z));
  r1.y = r1.z ? -r1.y : -1;
  r0.z = r0.z ? r1.y : -1;
  r0.z = r0.w + r0.z;
  r0.w = r0.w * -2.5 + r1.x;
  r0.w = 3 + r0.w;
  r0.z = saturate(1 + r0.z);
  r1.yz = cb0[16].zw * r0.xy;
  r1.w = t1.Sample(s2_s, r1.yz).x;
  r2.xyz = float3(0.15,0.1,0.05) * r1.www;
  r0.xy = r0.xy * cb0[16].zw + r2.xx;
  r2.xyzw = r1.yzyz * float4(1.3,1.3,0.5,0.5) + r2.yyzz;
  r0.x = cb1[132].z * 0.2 + r0.x;
  r0.x = t1.Sample(s2_s, r0.xy).x;
  r0.y = cb3[6].z + -cb3[2].x;
  r0.x = saturate(r0.x * r0.y + cb3[2].x);
  r2.xz = cb1[132].zz * float2(0.2,0.2) + r2.xz;
  r1.w = t1.Sample(s2_s, r2.xy).x;
  r2.x = t1.Sample(s2_s, r2.zw).x;
  r0.w = saturate(r2.x * r0.w + -r1.x);
  r0.y = saturate(r1.w * r0.y + cb3[2].x);
  r1.x = t2.Sample(s1_s, r1.yz).x;
  r1.yzw = t0.Sample(s0_s, r1.yz).xyz;
  r0.y = r1.x * r0.y;
  r0.x = r0.x * r1.x + r0.y;
  r0.y = r1.x * r0.w;
  r0.x = 0.5 * r0.x;
  r0.z = -r0.x * r0.y + r0.z;
  r0.x = r0.x * r0.y + -1;
  r0.y = 1 + -r0.z;
  r0.z = cb3[7].z + -cb3[4].x;
  r0.y = saturate(r0.y * r0.z + cb3[4].x);
  r0.x = saturate(r0.y * 2 + r0.x);
  r0.yzw = t3.Sample(s3_s, screenUV).xyz;
  r0.yzw = r0.yzw + -r1.yzw;
  r0.xyz = r0.xxx * r0.yzw + r1.yzw;
  r0.xyz = cb3[5].xyz + r0.xyz;
#if 1 // Luma
  o0.xyz = r0.xyz;
  o0.w = 0; // Make sure alpha has 0 for the UI
#else
  o0.xyz = max(0.0, r0.xyz);
  o0.w = 1;
#endif
}