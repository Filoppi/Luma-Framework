#ifndef IMPROVE_SHADOW_MAPS
#define IMPROVE_SHADOW_MAPS 1
#endif

#if IMPROVE_SHADOW_MAPS
// 1.0 matches the original projected tap spread; 2.0 makes softness visible nearby.
#ifndef MGS4_SHADOW_SOFTNESS
#define MGS4_SHADOW_SOFTNESS 3.0
#endif
// Multiplier relative to the near radius (default total: 2x near, 4x far).
#ifndef MGS4_SHADOW_FAR_SOFTNESS
#define MGS4_SHADOW_FAR_SOFTNESS 2.0
#endif
#ifndef MGS4_SHADOW_DEBUG
#define MGS4_SHADOW_DEBUG 0 // Set to 1 to mark surfaces using the improved branch magenta.
#endif
// Raw v11.x = clipW / 6000 - 9 in the captured VS: ramp from 12k to 48k.
#ifndef MGS4_SHADOW_SOFTNESS_START
#define MGS4_SHADOW_SOFTNESS_START -7.0
#endif
#ifndef MGS4_SHADOW_SOFTNESS_END
#define MGS4_SHADOW_SOFTNESS_END -1.0
#endif
// Luma currently resizes the resource without changing cb0[13].xy.
// This is the ORIGINAL filter scale, not the actual texture's texel size.
// If the CB is later divided by a resolution scale, override this expression
// with cb0[13].xy * that scale (per axis), or an independent original value.
#ifndef MGS4_SHADOW_REFERENCE_INV_SIZE
#define MGS4_SHADOW_REFERENCE_INV_SIZE (cb0[13].xy)
#endif

// Center + 6 inner + 12 rotated outer taps; no per-pixel noise or sincos.
static const float2 MGS4_SHADOW_OFFSETS[18] = {
  float2( 0.433012702,  0.250000000), float2( 0.000000000,  0.500000000),
  float2(-0.433012702,  0.250000000), float2(-0.433012702, -0.250000000),
  float2( 0.000000000, -0.500000000), float2( 0.433012702, -0.250000000),
  float2( 0.965925826,  0.258819045), float2( 0.707106781,  0.707106781),
  float2( 0.258819045,  0.965925826), float2(-0.258819045,  0.965925826),
  float2(-0.707106781,  0.707106781), float2(-0.965925826,  0.258819045),
  float2(-0.965925826, -0.258819045), float2(-0.707106781, -0.707106781),
  float2(-0.258819045, -0.965925826), float2( 0.258819045, -0.965925826),
  float2( 0.707106781, -0.707106781), float2( 0.965925826, -0.258819045)
};
#endif

Texture2D<uint4> t5 : register(t5);
Texture2D<float4> t3 : register(t3);
Texture2D<float4> t2 : register(t2);
Texture2D<float4> t1 : register(t1);
Texture2D<float4> t0 : register(t0);

SamplerComparisonState s3_s : register(s3);

SamplerState s2_s : register(s2);
SamplerState s1_s : register(s1);
SamplerState s0_s : register(s0);

cbuffer cb0 : register(b0)
{
  float4 cb0[15];
}

// TODO1
void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : COLOR0,
  float3 v2 : COLOR1,
  float w2 : COLOR2,
  float4 v3 : TEXCOORD0,
  float4 v4 : TEXCOORD1,
  float3 v5 : TEXCOORD2,
  float3 v6 : TEXCOORD3,
  float3 v7 : TEXCOORD4,
  float3 v8 : TEXCOORD5,
  float3 v9 : TEXCOORD6,
  float4 v10 : TEXCOORD8,
  float4 v11 : TEXCOORD9,
  uint v12 : SV_IsFrontFace0,
  out float4 o0 : SV_TARGET0)
{
  float4 r0,r1,r2,r3,r4,r5;

  // DXBC bfi/iadd/itof: preserve the signed, face-dependent shadow bias.
  r0.x = (float)((int)((v12 & 1u) << 1u) - 1);
  r1.xy = v10.xy / v10.ww;
  r1.xy = r1.xy * float2(0.5,-0.5) + float2(0.5,0.5);
  r0.x = v11.y * r0.x + v10.z;
  r1.z = saturate(r0.x / v10.w);
#if IMPROVE_SHADOW_MAPS
  float2 shadowSize;
  t3.GetDimensions(shadowSize.x, shadowSize.y); // Bound SRV, mip 0, after Luma's upgrade.
  float2 shadowTexelSize = rcp(shadowSize);
  float2 referenceInvSize = MGS4_SHADOW_REFERENCE_INV_SIZE;
  float2 shadowLinearScale = shadowSize * referenceInvSize;

  float softnessRamp = saturate((v11.x - MGS4_SHADOW_SOFTNESS_START) /
    max(MGS4_SHADOW_SOFTNESS_END - MGS4_SHADOW_SOFTNESS_START, 0.0001));
  softnessRamp = softnessRamp * softnessRamp * (3.0 - 2.0 * softnessRamp);
  float softness = MGS4_SHADOW_SOFTNESS * lerp(1.0, MGS4_SHADOW_FAR_SOFTNESS, softnessRamp);
  // Match the original 7-tap offsets' RMS radius despite adding inner taps.
  const float kernelRadiusScale = 1.098339295; // sqrt((6/7) / ((6*0.5^2+12)/19))
  // Texel size shrinks and radius in texels grows with resolution, preserving
  // the original projected footprint. Hardware bilinear PCF still shrinks per tap.
  float2 shadowRadius = shadowTexelSize * shadowLinearScale * (0.7 * kernelRadiusScale * softness);
  r2.w = t3.SampleCmpLevelZero(s3_s, r1.xy, r1.z);
  [unroll]
  for (int tap = 0; tap < 18; ++tap)
    r2.w += t3.SampleCmpLevelZero(s3_s, r1.xy + MGS4_SHADOW_OFFSETS[tap] * shadowRadius, r1.z);
#else
  // Original center + ring PCF, including the CB-controlled sample count.
  r0.yz = float2(0.699999988,0.699999988) * cb0[13].xy;
  r0.x = -1 + cb0[14].x;
  r0.w = 360 / r0.x;
  r1.w = t3.SampleCmpLevelZero(s3_s, r1.xy, r1.z).x;
  r2.z = 0;
  r2.w = r1.w;
  int shadowTap = 0; // DXBC uses an integer loop counter.
  while (true) {
    r3.y = (float)shadowTap;
    r3.z = (r3.y >= r0.x);
    if (r3.z != 0) break;
    r3.y = r3.y * r0.w + 30;
    r3.y = 0.0174532924 * r3.y;
    sincos(r3.y, r4.x, r5.x);
    r2.x = r5.x * r0.y;
    r2.y = r4.x * r0.z;
    r3.yzw = r2.xyz + r1.xyz;
    r2.x = t3.SampleCmpLevelZero(s3_s, r3.yz, r3.w).x;
    r2.w = r2.w + r2.x;
    shadowTap += 1;
  }
#endif
  if (0 < cb0[12].x) {
    // Keep the stipple mask in uints: float conversion loses bits (DXBC 30-42).
    uint2 stipplePixel = (uint2)floor(v0.xy);
    uint stippleMask = t5.Load(int3(stipplePixel.x & 31u, (int)cb0[12].y, 0)).x;
    uint stippleBit = 1u << (stipplePixel.y & 31u); // DXBC ishl uses the low 5 bits.
    if ((stippleMask & stippleBit) == 0u) discard;
  }
  r0.xyz = t0.Sample(s0_s, v3.xy).xyz;
  r1.xyzw = t2.Sample(s2_s, v4.zw).xyzw;
  r0.w = v1.w * r1.w;
  r1.xyz = float3(-1,-1,-1) + r1.xyz;
  r1.xyz = r0.www * r1.xyz + float3(1,1,1);
  r0.xyz = r1.xyz * r0.xyz;
  r0.xyz = cb0[0].xyz * r0.xyz;
  r1.xyz = float3(4,4,4) * v2.xyz;
  r2.xy = t1.Sample(s1_s, v3.zw).yw;
  r2.xy = float2(-0.5,-0.5) + r2.yx;
  r2.xy = r2.xy + r2.xy;
  r0.w = -r2.x * r2.x + 1;
  r0.w = -r2.y * r2.y + r0.w;
  r0.w = max(0, r0.w);
  r1.w = (0 < r0.w);
  r3.x = rsqrt(r0.w);
  r0.w = r3.x * r0.w;
  r2.z = r1.w ? r0.w : 0;
  r3.xyz = v8.xyz * r2.yyy;
  r3.xyz = v7.xyz * r2.xxx + r3.xyz;
  r3.xyz = v9.xyz * r2.zzz + r3.xyz;
  r0.w = dot(r3.xyz, r3.xyz);
  r0.w = rsqrt(r0.w);
  r3.xyz = r3.xyz * r0.www;
  r4.x = saturate(dot(r2.xyz, float3(0.707099974,-0.408199996,0.577400029)));
  r4.y = saturate(dot(r2.xyz, float3(-0.707099974,-0.408199996,0.577400029)));
  r4.z = saturate(dot(r2.yz, float2(0.816500008,0.577400029)));
  r2.xyz = r4.xyz * r4.xyz;
  r4.xyz = v6.xyz * r2.yyy;
  r4.xyz = v5.xyz * r2.xxx + r4.xyz;
  r1.xyz = r1.xyz * r2.zzz + r4.xyz;
  r1.xyz = v1.xyz * float3(2,2,2) + r1.xyz;
  r0.w = dot(cb0[9].xyz, r3.xyz);
#if IMPROVE_SHADOW_MAPS
  r1.w = 1.0 / 19.0;
#else
  r1.w = 1 / cb0[14].x;
#endif
  r2.x = r2.w * r1.w;
  r1.w = -r2.w * r1.w + 1;
  // Original fade to unshadowed is independent of the earlier softness ramp.
  r2.y = saturate(v11.x);
  r1.w = r1.w * r2.y + r2.x;
  r0.w = (r0.w < 0);
  r0.w = r0.w ? r1.w : 0;
  r1.w = saturate(dot(-cb0[9].xyz, r3.xyz));
  r2.xyz = cb0[8].xyz * r0.www;
  r1.xyz = r2.xyz * r1.www + r1.xyz;
  r2.xyz = r1.xyz * r0.xyz;
  r0.w = max(cb0[10].z, w2.x);
  r0.w = min(cb0[10].w, r0.w);
  r0.w = cb0[11].w * r0.w;
  r0.xyz = -r0.xyz * r1.xyz + cb0[11].xyz;
  r0.xyz = r0.www * r0.xyz + r2.xyz;
  r0.w = max(r0.x, r0.y);
  r1.x = max(0.25, r0.z);
  r0.w = max(r1.x, r0.w);
  r0.w = 1 / r0.w;
  o0.xyz = saturate(r0.xyz * r0.www);
  o0.w = saturate(0.25 * r0.w);
#if IMPROVE_SHADOW_MAPS && MGS4_SHADOW_DEBUG
  // Diagnostic only: use the original color encoding, then toggle IMPROVE_SHADOW_MAPS.
  o0 = float4(1.0, 0.0, 1.0, 0.25);
#endif
  return;
}
