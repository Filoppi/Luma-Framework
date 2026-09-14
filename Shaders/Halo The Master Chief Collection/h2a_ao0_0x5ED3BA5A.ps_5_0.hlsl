cbuffer CB_PS_COMMON : register(b0)
{
  float4 COMMON_LBUF_PARAMS : packoffset(c0);
  float4 COMMON_VIEW_POSITION : packoffset(c1);
  float4 COMMON_VIEWPROJ_MATRIX[4] : packoffset(c2);
  float4 COMMON_VIEW_POSITION_COLORCAM : packoffset(c6);
  float4 COMMON_VIEWPROJ_MATRIX_COLORCAM[4] : packoffset(c7);
  float4 COMMON_VIEWPROJ_REFLECTION[4] : packoffset(c11);
  float4 COMMON_OBLIQUE_MATR[4] : packoffset(c15);
  float4 VS_REG_COMMON_FOG_PARAMS[2] : packoffset(c19);
  float4 REG_COMMON_CLIPPING_PLANE : packoffset(c21);
  float4 COMMON_VP_PARAMS[2] : packoffset(c22);
  float4 PS_REG_COMMON_HDR_PARAMS : packoffset(c24);
  float4 PS_REG_COMMON_FOG_SUN_DIR : packoffset(c25);
  float4 PS_REG_COMMON_FOG_RAYLEIGH_FACTOR : packoffset(c26);
  float4 PS_REG_COMMON_FOG_COLOR : packoffset(c27);
  float4 PS_REG_COMMON_FOG_PLANE_MIRROR : packoffset(c28);
  float4 PS_REG_COMMON_FOG_ATMOSPHERE_0[6] : packoffset(c29);
  float4 PS_REG_COMMON_FOG_ATMOSPHERE_EXTRA : packoffset(c35);
  float4 PS_REG_COMMON_ELAPSED_TIME : packoffset(c36);
  float4 PS_REG_COMMON_AMBIENT : packoffset(c37);
  float4 PS_REG_COMMON_DEBUG_SHOW_LIGHTING : packoffset(c38);
  float4 VS_REG_COMMON_FOG_COLOR : packoffset(c39);
  float4 VS_REG_COMMON_FOG_SUN_DIR : packoffset(c40);
  float4 VS_REG_COMMON_FOG_RAYLEIGH_FACTOR : packoffset(c41);
  float4 VS_REG_COMMON_FOG_VOLUME_COUNT : packoffset(c42);
  float4 VS_REG_COMMON_FOG_VOL_START[32] : packoffset(c43);
}

cbuffer CB_DYNAMIC_PASS_SSAO : register(b7)
{
  float4 PS_REG_SSAO_SCREEN : packoffset(c0);
  float4 PS_REG_SSAO_PARAMS : packoffset(c1);
  float4 PS_REG_SSAO_MV_1 : packoffset(c2);
  float4 PS_REG_SSAO_MV_2 : packoffset(c3);
  float4 PS_REG_SSAO_MV_3 : packoffset(c4);
  float4 SSAO_FRUSTUM_SCALE : packoffset(c5);
  float4 SSAO_TEX_COORD_SCALE : packoffset(c6);
  float4 PS_REG_SSAO_FRUSTUM_SCALE : packoffset(c7); //copy of SSAO_FRUSTUM_SCALE
  /*
    PS_REG_SSAO_FRUSTUM_SCALE example @ 78* FOV
    1.623990
    0.913496
    0.615766 (rcp() of 1.623990)
    1.094700 (rcp() of 0.913496)

    PS_REG_SSAO_FRUSTUM_SCALE example @ 90* FOV
    1.966250
    1.106010
    0.508584 (rcp() of 1.966250)
    0.904149 (rcp() of 1.106010)

    PS_REG_SSAO_FRUSTUM_SCALE example @ 120* FOV
    3.10636
    1.74733
    0.32192
    0.572302
  */
}

SamplerState PS_SAMPLERS_0__s : register(s0);
SamplerState PS_SAMPLERS_3__s : register(s3);
Texture2D<float4> PS_TEXTURES_2D_0_ : register(t0); //depth
Texture2D<float4> PS_TEXTURES_2D_2_ : register(t2); //per-pixel rotation/dither (2x 2D vectors, [-1,1], used to jitter the AO kernel)
Texture2D<float4> PS_TEXTURES_2D_3_ : register(t3); //normal

// 3Dmigoto declarations
#define cmp -
#include "./Includes/Common.hlsl"
#include "./Includes/Math.hlsl"

// Fixed 8-tap kernel (bit-exact to the original shader). Each tap is rotated per-pixel by the dither
// vectors (ditherA/ditherB) via a dot product, i.e. tapDir = (dot(kernelDir, ditherA), dot(kernelDir, ditherB)).
static const float2 AO_KERNEL_DIR[8] =
{
  float2(0.0666666701, -0.0666666701),
  float2(-0.0599999987, -0.0599999987),
  float2(0.0533333346, 0.0533333346),
  float2(-0.0466666669, 0.0466666669),

  float2(-0.0424264073, 0),
  float2(0.0353553407, 0),
  float2(0, -0.0318198055),
  float2(0, 0.0282842703),
};
static const float AO_KERNEL_DEPTH_BIAS[8] =
{
  0.0333333351, 0.0299999993, 0.0266666673, 0.0233333334,
  0.0424264073, 0.0353553407, 0.0318198055, 0.0282842703,
};

// AI ahh:
// One horizon-based occlusion tap: offsets the view-space position along a dithered kernel direction,
// reflects it around the normal-bent view direction, reprojects it to screen space and diffs the depths.
// Also recovers the *actual* view-space occluder position (GTAO-style) from the real sampled depth
// (instead of trusting the assumed tap depth) to derive a horizon cosine and sample distance, used below
// to weight taps the way GTAO's cosine-weighted horizon integration does, without needing a full
// multi-step horizon march (this depth/view-space isn't reliable enough for that here).
float SampleHorizonTapDelta(float2 kernelDir, float depthBias, float2 ditherA, float2 ditherB, float3 bendDir, float3 viewNormal, float3 centerViewPos, float aoRange, float2 frustumScale, out float horizonCos, out float sampleDist)
{
  float2 tapDir = float2(dot(kernelDir, ditherA), dot(kernelDir, ditherB));
  float3 tapOffset = float3(tapDir, depthBias);
  tapOffset = reflect(tapOffset, bendDir);
  float3 samplePos = tapOffset * aoRange + centerViewPos;
  float2 sampleScreenDir = samplePos.xy / samplePos.z;
  float2 sampleUV = sampleScreenDir * frustumScale + 0.5;

  float sampleDepth = PS_TEXTURES_2D_0_.Sample(PS_SAMPLERS_3__s, sampleUV).x;
  float sampleLinearDepth = 0.100000016 / (1.33333344e-007 + sampleDepth);

  // Real occluder view-space position, reprojected using its own depth rather than the assumed tap depth.
  float3 actualSamplePos = float3(sampleScreenDir * sampleLinearDepth, sampleLinearDepth);
  float3 toSample = actualSamplePos - centerViewPos;
  sampleDist = length(toSample);
  horizonCos = dot(toSample, viewNormal) / max(sampleDist, 1e-5); // cosine between occluder direction and surface normal

  return sampleLinearDepth - samplePos.z; // positive => occluder closer to camera than the sample point
}

// AI ahh:
// Converts a raw depth delta into a smoothed occlusion factor and its blend weight (matches the original curve),
// further shaped by a GTAO-style cosine-weighted horizon term (grazing/back-facing occluders contribute less,
// instead of every depth delta occluding equally).
float ShapeOcclusionTap(float strength, float delta, float normFactor1, float normFactor2, float horizonCos, float sampleDist, out float weight)
{
  float scaledDelta = delta * normFactor1;
  float curved = 1.0 - 0.5 * (saturate(-scaledDelta) + min(1.0, abs(scaledDelta)));
  weight = max(0.5, curved);
  float edge = saturate(delta * normFactor2) - 1.0;
  float occlusion = min(1.0, curved + curved) * edge * strength + 1.0;
  return lerp(1.0, occlusion, saturate(horizonCos));
}

// One horizon-based occlusion tap: offsets the view-space position along a dithered kernel direction,
// reflects it around the normal-bent view direction, reprojects it to screen space and diffs the depths.
float SampleHorizonTapDeltaSimpler(float2 kernelDir, float depthBias, float2 ditherA, float2 ditherB, float3 bendDir, float3 centerViewPos, float aoRange, float2 frustumScale)
{
  float2 tapDir = float2(dot(kernelDir, ditherA), dot(kernelDir, ditherB));
  float3 tapOffset = float3(tapDir, depthBias);
  tapOffset = reflect(tapOffset, bendDir);
  float3 samplePos = tapOffset * aoRange + centerViewPos;
  float2 sampleUV = (samplePos.xy / samplePos.z) * frustumScale + 0.5;

  float sampleDepth = PS_TEXTURES_2D_0_.Sample(PS_SAMPLERS_3__s, sampleUV).x;
  float sampleLinearDepth = 0.100000016 / (1.33333344e-007 + sampleDepth);
  return sampleLinearDepth - samplePos.z; // positive => occluder closer to camera than the sample point
}

// Converts a raw depth delta into a smoothed occlusion factor and its blend weight (matches the original curve).
float ShapeOcclusionTapSimpler(float strength, float delta, float normFactor1, float normFactor2, out float weight)
{
  float scaledDelta = delta * normFactor1;
  float curved = 1.0 - 0.5 * (saturate(-scaledDelta) + min(1.0, abs(scaledDelta)));
  weight = max(0.5, curved);
  float edge = saturate(delta * normFactor2) - 1.0;
  return min(1.0, curved + curved) * edge * strength + 1.0;
}

float CompletePass(int sampleCount, float strength, int ringOffset, float ringScale0, float rightScale1, float2 ditherA, float2 ditherB, float3 bendDir, float3 viewNormal, float3 centerViewPos, float aoRange, float2 frustumScale, float normFactor1, float normFactor2, float4 v1) 
{
  float numerator = 0.0;
  float denominator = 0.0;

  [unroll]
  for (int i = 0; i < sampleCount; i++)
  {
    int slot = i % 8;
    int ring = i / 8 + ringOffset;
    float ringScale = pow(ringScale0, ring);

    // ringScale by distance
    float expansion = max((0.1 / PS_TEXTURES_2D_0_.Sample(PS_SAMPLERS_3__s, v1.xy).x), 1);
    // if (expansion > DVS9) {
      expansion = pow(expansion, 0.327) * 0.28;
      ringScale *= expansion;
    // }

    float horizonCos, sampleDist;
    float delta = SampleHorizonTapDelta(AO_KERNEL_DIR[slot] * ringScale * rightScale1, AO_KERNEL_DEPTH_BIAS[slot] * ringScale / rightScale1, ditherA, ditherB, bendDir, viewNormal, centerViewPos, aoRange, PS_REG_SSAO_FRUSTUM_SCALE.zw, horizonCos, sampleDist);

    float weight;
    float occlusion = ShapeOcclusionTap(strength, delta, normFactor1, normFactor2, horizonCos, sampleDist, weight);

    numerator += occlusion * weight;
    denominator += weight;
  }

  return min(1.0, numerator / denominator);
}

float CompletePassSimpler(int sampleCount, float strength, int ringOffset, float ringScale0, float rightScale1, float2 ditherA, float2 ditherB, float3 bendDir, float3 viewNormal, float3 centerViewPos, float aoRange, float2 frustumScale, float normFactor1, float normFactor2, float4 v1) 
{
  float numerator = 0.0;
  float denominator = 0.0;

  [unroll]
  for (int i = 0; i < sampleCount; i++)
  {
    int slot = i % 8;
    int ring = i / 8 + ringOffset;
    float ringScale = pow(ringScale0, ring);
  
    float delta = SampleHorizonTapDeltaSimpler(AO_KERNEL_DIR[slot] * ringScale * rightScale1, AO_KERNEL_DEPTH_BIAS[slot] * ringScale / rightScale1, ditherA, ditherB, bendDir, centerViewPos, aoRange, PS_REG_SSAO_FRUSTUM_SCALE.zw);
  
    float weight;
    float occlusion = ShapeOcclusionTapSimpler(strength, delta, normFactor1, normFactor2, weight);

    numerator += occlusion * weight;
    denominator += weight;
  }

  return min(1.0, numerator / denominator);
}

void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD2, //unused
  float3 v3 : TEXCOORD1,
  out float2 o0 : SV_Target0)
{
  // World space normal
  const float3 normal = normalize(PS_TEXTURES_2D_3_.Sample(PS_SAMPLERS_3__s, v1.xy).xyz * 2.0 - 1.0);
 
  // Transform into the space used for the AO kernel, then bend it towards the view direction; this is
  // used below to bias the sample kernel towards the surface hemisphere without a full tangent basis.
  const float3 viewNormal = float3(dot(normal, PS_REG_SSAO_MV_1.xyz), dot(normal, PS_REG_SSAO_MV_2.xyz), dot(normal, PS_REG_SSAO_MV_3.xyz));
  const float3 bendDir = normalize(float3(0, 0, -1) + viewNormal);
  // o0.xy = viewNormal.y * 0.5 + 0.5; return; //debug viewNormal

  const float4 dither = PS_TEXTURES_2D_2_.Sample(PS_SAMPLERS_0__s, v1.zw) * 2.0 - 1.0;
  const float2 ditherA = dither.xy;
  const float2 ditherB = dither.zw;
  // o0.xy = ditherA.xy * 0.5 + 0.5; return; //debug ditherA

  const float centerDepth = PS_TEXTURES_2D_0_.Sample(PS_SAMPLERS_3__s, v1.xy).x;
  const float centerLinearDepth = 0.100000016 / (1.33333344e-007 + centerDepth);

  const float aoRange = saturate(0.5 * centerLinearDepth) * (PS_REG_SSAO_PARAMS.x * (centerLinearDepth * 0.100000001 + 1.39999998));
  const float3 centerViewPos = v3.xyz * centerLinearDepth / v3.z;
  // o0.xy = centerViewPos.xy * 0.5 + 0.5; return; //debug centerViewPos

  const float normFactor1 = PS_REG_SSAO_PARAMS.z / aoRange;
  const float normFactor2 = 64.0 / aoRange;

  #if HALO2_AO == 0
    float ao0 = CompletePassSimpler(8, 1, 0, 0.75, 1, ditherA, ditherB, bendDir, viewNormal, centerViewPos, aoRange, PS_REG_SSAO_FRUSTUM_SCALE.zw, normFactor1, normFactor2, v1);
    o0.x = pow(ao0, 1.5);
    o0.x = ao0;
  #elif HALO2_AO == 1
    float ao0 = CompletePass(8*2, 1, 0, 1.3, 1, ditherA, ditherB, bendDir, viewNormal, centerViewPos, aoRange, PS_REG_SSAO_FRUSTUM_SCALE.zw, normFactor1, normFactor2, v1);
    float ao1 = CompletePassSimpler(8, 0.36, 0, 1, 0.6, ditherA, ditherB, bendDir, viewNormal, centerViewPos, aoRange, PS_REG_SSAO_FRUSTUM_SCALE.zw, normFactor1, normFactor2, v1); //support
    o0.x = ao0;
    o0.x = min(ao0, ao1);
    o0.x = pow(o0.x, 1.46);
  #elif HALO2_AO == 2
    float ao0 = CompletePass(8*3, 1, 0, 1.3, 1, ditherA, ditherB, bendDir, viewNormal, centerViewPos, aoRange, PS_REG_SSAO_FRUSTUM_SCALE.zw, normFactor1, normFactor2, v1);
    float ao1 = CompletePassSimpler(8, 0.36, 0, 1, 0.6, ditherA, ditherB, bendDir, viewNormal, centerViewPos, aoRange, PS_REG_SSAO_FRUSTUM_SCALE.zw, normFactor1, normFactor2, v1); //support
    o0.x = ao0;
    o0.x = min(ao0, ao1);
    o0.x = pow(o0.x, 1.46);
  #elif HALO2_AO == 3
    float ao0 = CompletePass(8*4, 1, 0, 1.3, 1, ditherA, ditherB, bendDir, viewNormal, centerViewPos, aoRange, PS_REG_SSAO_FRUSTUM_SCALE.zw, normFactor1, normFactor2, v1);
    float ao1 = CompletePassSimpler(8, 0.36, 0, 1, 0.6, ditherA, ditherB, bendDir, viewNormal, centerViewPos, aoRange, PS_REG_SSAO_FRUSTUM_SCALE.zw, normFactor1, normFactor2, v1); //support
    o0.x = ao0;
    o0.x = min(ao0, ao1);
    o0.x = pow(o0.x, 1.46);
  #elif HALO2_AO == 4
    float ao0 = CompletePass(8*5, 1, 0, 1.3, 1, ditherA, ditherB, bendDir, viewNormal, centerViewPos, aoRange, PS_REG_SSAO_FRUSTUM_SCALE.zw, normFactor1, normFactor2, v1);
    float ao1 = CompletePassSimpler(8, 0.36, 0, 1, 0.6, ditherA, ditherB, bendDir, viewNormal, centerViewPos, aoRange, PS_REG_SSAO_FRUSTUM_SCALE.zw, normFactor1, normFactor2, v1); //support
    o0.x = ao0;
    o0.x = min(ao0, ao1);
    o0.x = pow(o0.x, 1.46);
  #endif

  /////////////////////////////
  #if HALO2_AO == 0
    o0.y = 0; //NOT USED!!!
  #else
    // Depth curvature edge-stopping term, used by the blur pass to avoid bleeding AO across depth discontinuities.
    float depthA0 = PS_TEXTURES_2D_0_.SampleLevel(PS_SAMPLERS_3__s, v1.xy - COMMON_VP_PARAMS[0].xy, 0).x;
    float depthA1 = PS_TEXTURES_2D_0_.SampleLevel(PS_SAMPLERS_3__s, v1.xy + COMMON_VP_PARAMS[0].xy, 0).x;
    float depthB0 = PS_TEXTURES_2D_0_.SampleLevel(PS_SAMPLERS_3__s, v1.xy - COMMON_VP_PARAMS[0].xy * float2(1, -1), 0).x;
    float depthB1 = PS_TEXTURES_2D_0_.SampleLevel(PS_SAMPLERS_3__s, v1.xy + COMMON_VP_PARAMS[0].xy * float2(1, -1), 0).x;

    float centerRemap = 0.100000016 / centerLinearDepth - 1.33333344e-007;
    float curvatureA = (depthA0 + depthA1) - 2.0 * centerRemap;
    float curvatureB = (depthB0 + depthB1) - 2.0 * centerRemap;

    float curvature = max(abs(curvatureA), abs(curvatureB)) * centerLinearDepth;
    o0.y = saturate(1.0 - curvature * 1024.0);
  #endif

  return;
}