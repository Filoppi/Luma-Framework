#include "../../Includes/Common.hlsl"

// Number of authored grading LUTs; the neutral LUT is generated below.
#if _56B46C0A
#define LUT_COUNT 0
#elif _90F01F91
#define LUT_COUNT 1
#elif _06F9EC9F
#define LUT_COUNT 2
#elif _51A8FE04
#define LUT_COUNT 3
#elif _9D987720
#define LUT_COUNT 4
#else
#error Unknown LUT builder shader hash
#endif

Texture2D<float4> t3 : register(t3);
Texture2D<float4> t2 : register(t2);
Texture2D<float4> t1 : register(t1);
Texture2D<float4> t0 : register(t0);

SamplerState s3_s : register(s3);
SamplerState s2_s : register(s2);
SamplerState s1_s : register(s1);
SamplerState s0_s : register(s0);

cbuffer cb0 : register(b0)
{
  float4 cb0[11 + LUT_COUNT];
}

void main(
  float2 v0 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4;
  r0.xz = float2(-0.001953125,-0.03125) + v0.xy;
  r1.x = 16 * r0.x;
  r0.w = frac(r1.x);
  r0.y = -r0.w * 0.0625 + r0.x;
  r0.xyz = cb0[10].x * r0.yzw;

#if LUT_COUNT == 0 // No luts, use neutral

  r1.xyz = 1.06666672 * r0.xyz;
  r0.w = GetLuminance(r1.xyz); // Luma: fixed from BT.601 coeffs
  r0.xyz = -r0.xyz * 1.06666672 + r0.w;
  r0.w = linear_to_gamma1(r0.w, GCT_POSITIVE); // Luma: fixed using sqrt as approximation of gamma 2.2
  r2.x = cb0[7].x * r0.w;
  r2.y = cb0[7].x * r0.w - 0.5;
  r2.xy = saturate(r2.xy + r2.xy);

#else // LUT_COUNT != 0

  r0.xyz = 1.06666672 * r0.xyz;
  r0.w = 0;
#if LUT_COUNT >= 1
  r1.xyzw = t0.Sample(s0_s, v0.xy).xyzw;
  r0.xyzw += cb0[11].x * r1.xyzw;
#endif
#if LUT_COUNT >= 2
  r1.xyzw = t1.Sample(s1_s, v0.xy).xyzw;
  r0.xyzw += cb0[12].x * r1.xyzw;
#endif
#if LUT_COUNT >= 3
  r1.xyzw = t2.Sample(s2_s, v0.xy).xyzw;
  r0.xyzw += cb0[13].x * r1.xyzw;
#endif
#if LUT_COUNT >= 4
  r1.xyzw = t3.Sample(s3_s, v0.xy).xyzw;
  r0.xyzw += cb0[14].x * r1.xyzw;

#endif // LUT_COUNT == 0

  r1.x = GetLuminance(r0.xyz); // Luma: fixed from BT.601 coeffs
  r1.yzw = r1.x - r0.xyz;
  r1.x = linear_to_gamma1(r1.x, GCT_POSITIVE); // Luma: fixed using sqrt as approximation of gamma 2.2
  r2.x = cb0[7].x * r1.x;
  r2.y = cb0[7].x * r1.x - 0.5;
  r2.xy = saturate(r2.xy + r2.xy);
#endif
  r3.xyzw = cb0[3].xyzw - cb0[1].xyzw;
  r3.xyzw = r2.x * r3.xyzw + cb0[1].xyzw;
  r4.xyzw = cb0[5].xyzw - r3.xyzw;
  r2.xyzw = r2.y * r4.xyzw + r3.xyzw;
#if LUT_COUNT == 0
  r0.xyz = r2.w * r0.xyz + r1.xyz;
#else
  r0.xyz = r2.w * r1.yzw + r0.xyz;
  o0.w = r0.w;
#endif
  r1.xyz = r2.xyz * r0.xyz;
  r0.xyz = -r2.xyz * r0.xyz + cb0[9].xyz;
  o0.xyz = cb0[9].w * r0.xyz + r1.xyz;
#if LUT_COUNT == 0
  o0.w = 0;
#endif
}