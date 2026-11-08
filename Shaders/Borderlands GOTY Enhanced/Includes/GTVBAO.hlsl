// GT-VBAO by Mirko Salm (TinyTexel), https://www.shadertoy.com/view/XXGSDd, dual licensed CC0 1.0 / MIT.
// Visibility bitmask AO (https://arxiv.org/abs/2301.11376) with perspective correct slices, slice relative CDF
// weighting and (approximate) importance sampling of the cosine weighted hemisphere.
//
// A main pass alternative to XeGTAO's on the same contract: "Luma_BL_XeGTAO.hlsl" includes this, which brings in the shared
// "XeGTAO.hlsl", and dispatches "gtvbao_main_pass_cs" instead of "main_pass_cs". The depth prefilter, denoise, normals,
// debug views, radius and visibility hooks stay XeGTAO's. The slice projection uses "NDC_TO_VIEW_MUL", so it assumes
// the linear uv -> view mapping of "XeGTAO_ComputeViewspacePosition()" even with "XE_GTAO_CUSTOM_VIEWSPACE_POSITION".

#ifndef LUMA_GTVBAO_HLSL
#define LUMA_GTVBAO_HLSL

#include "../../Includes/XeGTAO.hlsl"

#ifndef GTVBAO_SLICE_COUNT
#define GTVBAO_SLICE_COUNT 2.0
#endif

#ifndef GTVBAO_STEP_COUNT
#define GTVBAO_STEP_COUNT 16.0 // Per side, growing exponentially from 1 pixel to the screen space radius
#endif

// View space thickness assumed behind every depth sample
#ifndef GTVBAO_THICKNESS
#define GTVBAO_THICKNESS(effectRadius) (0.5 * (effectRadius))
#endif

// 0 is uniform hemisphere weighting (brighter, the original VBAO's look)
#ifndef GTVBAO_COSINE_WEIGHTED
#define GTVBAO_COSINE_WEIGHTED 1
#endif

// Center depth, packed edges, normal and view vector for the main pass.
// Returns true when a debug view already wrote the output.
bool XeGTAO_MainPassPrologue(uint2 pixCoord, Texture2D sourceViewspaceDepth, SamplerState depthSampler, RWTexture2D<XE_GTAO_MAIN_PASS_OUTPUT_TYPE> outWorkingAOTermAndEdges, inout float3 viewspaceNormal, out float viewspaceZ, out float edges, out float3 pixCenterPos, out float3 viewVec)
{
   float2 normalizedScreenPos = (pixCoord + 0.5) * VIEWPORT_PIXEL_SIZE;

   // Center + cross depths from the prefiltered (already linearized + scaled) mip0. This texture is our
   // own R32F, Gather is safe here (the r24 quirk is only on the game's depth view).
   float4 valuesUL = sourceViewspaceDepth.GatherRed(depthSampler, float2(pixCoord * VIEWPORT_PIXEL_SIZE));
   float4 valuesBR = sourceViewspaceDepth.GatherRed(depthSampler, float2(pixCoord * VIEWPORT_PIXEL_SIZE), int2(1, 1));

   // viewspace Z at the center
   viewspaceZ = valuesUL.y;

   // viewspace Zs left top right bottom
   const float pixLZ = valuesUL.x;
   const float pixTZ = valuesUL.z;
   const float pixRZ = valuesBR.z;
   const float pixBZ = valuesBR.x;

   float4 edgesLRTB = XeGTAO_CalculateEdges(viewspaceZ, pixLZ, pixRZ, pixTZ, pixBZ);
   edges = XeGTAO_PackEdges(edgesLRTB);

#if XE_GTAO_GENERATE_NORMALS
   {
      float3 CENTER = XeGTAO_ComputeViewspacePosition(normalizedScreenPos, viewspaceZ);
      float3 LEFT = XeGTAO_ComputeViewspacePosition(normalizedScreenPos + float2(-1.0, 0.0) * VIEWPORT_PIXEL_SIZE, pixLZ);
      float3 RIGHT = XeGTAO_ComputeViewspacePosition(normalizedScreenPos + float2(1.0, 0.0) * VIEWPORT_PIXEL_SIZE, pixRZ);
      float3 TOP = XeGTAO_ComputeViewspacePosition(normalizedScreenPos + float2(0.0, -1.0) * VIEWPORT_PIXEL_SIZE, pixTZ);
      float3 BOTTOM = XeGTAO_ComputeViewspacePosition(normalizedScreenPos + float2(0.0, 1.0) * VIEWPORT_PIXEL_SIZE, pixBZ);
      viewspaceNormal = XeGTAO_CalculateNormal(edgesLRTB, CENTER, LEFT, RIGHT, TOP, BOTTOM);
   }
#endif

   // Move center pixel slightly towards camera to avoid imprecision artifacts due to depth buffer imprecision; offset depends on depth texture format used
   viewspaceZ *= 0.99999; // this is good for FP32 depth buffer

   pixCenterPos = XeGTAO_ComputeViewspacePosition(normalizedScreenPos, viewspaceZ);
   viewVec = normalize(-pixCenterPos);

   // prevents normals that are facing away from the view vector - xeGTAO struggles with extreme cases. Intel's
   // XeGTAO ships this line commented out as rare in vanilla, we run it.
   viewspaceNormal = normalize(viewspaceNormal + max(0, -dot(viewspaceNormal, viewVec)) * viewVec);

#if DEVELOPMENT
   // Debug views (visible on screen through the game's own AO apply blit; the final denoise passes raw
   // values through when DebugViewRT > 0). One return at the end: fxc flags the out parameters on early returns (X4000).
   bool wroteDebugView = true;
   if (DebugViewRT > 0.5 && DebugViewRT < 1.5) // 1 = depth gradient (proves live depth + linearization/scale)
   {
      outWorkingAOTermAndEdges[pixCoord] = XE_GTAO_MAIN_PASS_DEBUG_VALUE(saturate(frac(log2(max(viewspaceZ, 1e-6)))));
   }
   else if (DebugViewRT >= 1.5 && DebugViewRT < 2.5) // 2 = normals view-facing term (smooth per-surface shading = correct decode)
   {
      outWorkingAOTermAndEdges[pixCoord] = XE_GTAO_MAIN_PASS_DEBUG_VALUE(saturate(abs(dot(viewspaceNormal, viewVec))));
   }
   else if (DebugViewRT >= 3.5 && DebugViewRT < 4.5) // 4 = edges
   {
      outWorkingAOTermAndEdges[pixCoord] = XE_GTAO_MAIN_PASS_DEBUG_VALUE(dot(edgesLRTB, 0.25));
   }
   else
   {
      wroteDebugView = false;
   }
   return wroteDebugView;
#else
   return false;
#endif
}

// GTAOFastAcos with one more mad, minimizes max abs(ACos(cos(x)) - x)
float GTVBAO_ACos(float x)
{
   float absX = abs(x);
   float u = (1.5707963267948966 + (-0.20491203466059038 + 0.04832927023878897 * absX) * absX) * sqrt(saturate(1.0 - absX));
   return x >= 0.0 ? u : (XE_GTAO_PI - u);
}

// Rotation from (0, 0, -1) to "to" (xyz, w)
float4 GTVBAO_GetQuaternion(float3 to)
{
   float u = rsqrt(max(0.0, -to.z * 0.5 + 0.5)); // rcp(cosine half-angle formula)
   return float4(float3(to.y, -to.x, 0.0) * (u * 0.5), rcp(u));
}

// Transforms v by unit quaternion q.xy0w
float3 GTVBAO_Transform_Qz0(float3 v, float4 q)
{
   float k = v.y * q.x - v.x * q.y;
   float g = 2.0 * (v.z * q.w + k);
   return float3(v.xy + q.yx * float2(g, -g), v.z + 2.0 * (q.w * k - v.z * dot(q.xy, q.xy)));
}

// Transforms v.xy0 by unit quaternion q.xy0w
float3 GTVBAO_Transform_Vz0Qz0(float2 v, float4 q)
{
   float o = q.x * v.y;
   float c = q.y * v.x;
   float3 b = float3(o - c, -o + c, o - c);
   return float3(v, 0.0) + 2.0 * (b * q.yxw);
}

#if GTVBAO_COSINE_WEIGHTED
// Approximate importance sampling of the slice direction for the cosine weighted hemisphere, from one random number.
// "normalVVS" is the normal in view vector space (view vector along -z).
float2 GTVBAO_SampleSliceDir(float3 normalVVS, float rnd01)
{
   float2 dir0;
   sincos(rnd01 * XE_GTAO_PI, dir0.y, dir0.x);

   float l = length(normalVVS.xy);
   if (l == 0.0)
      return dir0;

   // Flip into the hemi-circle of the normal
   dir0 *= dot(dir0, normalVVS.xy) < 0.0 ? -1.0 : 1.0;

   float2 n = normalVVS.xy / l;
   float x = dir0.x * n.y - dir0.y * n.x;

   // acos stretch, chosen to approximately match the reference pdf
   float s = l + (l - l * l) * 0.15;

   // Inverse asin approximation
   const float k = 0.21545;
   const float a = 0.5 + 0.5 / k;
   const float b = 0.5 - 0.5 / k;
   float xs = 0.5 - 0.5 * s;
   xs = a - sqrt(b * b + (4.0 / k) * (xs * xs));
   x *= xs;

   // acos approximation
   float u = (abs(x) * k - k - 1.0) * abs(x) + 1.0;
   float y = abs((x > 0.0 ? 2.0 : 0.0) - sqrt(saturate(u)));

   float ys = rcp(s);
   float2 dir;
   dir.y = ys - ys * y;
   dir.x = sqrt(saturate(1.0 - dir.y * dir.y));

   // Align the x axis with the normal
   return float2(dir.x * n.x - dir.y * n.y, dir.y * n.x + dir.x * n.y);
}

// Maps a [0, 1] horizon angle (0.5 is the projected normal) to the slice relative cosine weighted CDF
float GTVBAO_SliceRelCDF(float x, float angN, float cosN, bool isPhiLargerThanAngN)
{
   if (x <= 0.0 || x >= 1.0)
      return x;

   float phi = x * XE_GTAO_PI - XE_GTAO_PI_HALF;
   float n0 = isPhiLargerThanAngN ? 3.0 : 1.0;
   float n1 = isPhiLargerThanAngN ? -1.0 : 1.0;
   float n2 = isPhiLargerThanAngN ? 4.0 : 0.0;
   float sinN = sin(angN);
   return (n0 * cosN + n1 * cos(angN - 2.0 * phi) + (n2 * angN + 2.0 * n1 * phi + XE_GTAO_PI) * sinN) / (4.0 * (cosN + angN * sinN));
}
#else
// Same as above for uniform weighting
float GTVBAO_SliceRelCDF(float x, float angN, float cosN, bool isPhiLargerThanAngN)
{
   if (x <= 0.0 || x >= 1.0)
      return x;

   float phi = x * XE_GTAO_PI - XE_GTAO_PI_HALF;
   float m0 = isPhiLargerThanAngN ? 2.0 : 0.0;
   float m1 = isPhiLargerThanAngN ? -1.0 : 1.0;
   return 0.5 * (m0 + m1 * cos(angN - phi) + sin(angN));
}
#endif

// "localNoise": x picks the slice direction, y jitters the steps along it, z jitters the horizons
void GTVBAO_MainPass(uint2 pixCoord, float3 localNoise, float3 viewspaceNormal, Texture2D sourceViewspaceDepth, SamplerState depthSampler, RWTexture2D<XE_GTAO_MAIN_PASS_OUTPUT_TYPE> outWorkingAOTermAndEdges)
{
   float viewspaceZ;
   float edges;
   float3 pixCenterPos;
   float3 viewVec;
   if (XeGTAO_MainPassPrologue(pixCoord, sourceViewspaceDepth, depthSampler, outWorkingAOTermAndEdges, viewspaceNormal, viewspaceZ, edges, pixCenterPos, viewVec))
      return;

   const float effectRadius = XE_GTAO_MAIN_PASS_EFFECT_RADIUS(viewspaceZ);
   const float thickness = GTVBAO_THICKNESS(effectRadius);
   float screenspaceRadius = effectRadius * rcp(abs(viewspaceZ * NDC_TO_VIEW_MUL_X_PIXEL_SIZE.x));
   XE_GTAO_ADJUST_SCREENSPACE_RADIUS(screenspaceRadius)
   const float stepGrowth = pow(max(screenspaceRadius, 1.0), 1.0 / GTVBAO_STEP_COUNT);

   const float2 pixelCenter = pixCoord + 0.5;
   const float4 toViewVec = GTVBAO_GetQuaternion(viewVec);
#if GTVBAO_COSINE_WEIGHTED
   const float3 normalVVS = GTVBAO_Transform_Qz0(viewspaceNormal, toViewVec * float4(-1.0, -1.0, -1.0, 1.0));
#endif

   float visibility = 0.0;
   [unroll] for (float slice = 0.0; slice < GTVBAO_SLICE_COUNT; slice++)
   {
      const float sliceNoise = (slice + localNoise.x) / GTVBAO_SLICE_COUNT;
#if GTVBAO_COSINE_WEIGHTED
      const float2 dirVVS = GTVBAO_SampleSliceDir(normalVVS, sliceNoise);
#else
      float2 dirVVS;
      sincos(sliceNoise * XE_GTAO_PI, dirVVS.y, dirVVS.x);
#endif
      // The slice is uniform around the view vector, not around the screen space ray, so it holds off center too
      const float3 sampleDirVS = GTVBAO_Transform_Vz0Qz0(dirVVS, toViewVec);
      // Screen space derivative of the view space ray's projection (the 1 / z^2 factor drops out with normalize)
      const float2 screenDir = normalize((sampleDirVS.xy * pixCenterPos.z - pixCenterPos.xy * sampleDirVS.z) / NDC_TO_VIEW_MUL_X_PIXEL_SIZE);

      const float3 sliceN = cross(viewVec, sampleDirVS);
      const float3 projN = viewspaceNormal - sliceN * dot(viewspaceNormal, sliceN);
      const float projNSqrLen = dot(projN, projN);
      if (projNSqrLen <= 0.0)
      {
         visibility += 1.0;
         continue;
      }
      const float cosN = dot(projN, viewVec) * rsqrt(projNSqrLen);
      const float angN = (dot(viewVec, cross(sliceN, projN)) < 0.0 ? -1.0 : 1.0) * GTVBAO_ACos(cosN);
      const float angOff = angN / XE_GTAO_PI + 0.5;

      uint occludedBits = 0;
      float stepNoise = localNoise.y;
      [unroll] for (float side = -1.0; side <= 1.0; side += 2.0)
      {
         float t = pow(stepGrowth, stepNoise);
         stepNoise = 1.0 - stepNoise;

         for (float step = 0.0; step < GTVBAO_STEP_COUNT; step++)
         {
            // Snapped to a texel center, so the reconstructed position matches the depth it was read from
            const float2 sampleScreenPos = (pixelCenter + round(screenDir * (side * t))) * VIEWPORT_PIXEL_SIZE;
            if (any(abs(sampleScreenPos - 0.5) > 0.5))
               break;

            // Point sampler, as in XeGTAO: a linear one would blend unrelated depths of the same mip
            const float mipLevel = clamp(log2(t) - DEPTH_MIP_SAMPLING_OFFSET, 0.0, XE_GTAO_DEPTH_MIP_LEVELS);
            t *= stepGrowth;
            const float sampleZ = sourceViewspaceDepth.SampleLevel(depthSampler, sampleScreenPos, mipLevel).x;
            const float3 samplePos = XeGTAO_ComputeViewspacePosition(sampleScreenPos, sampleZ);

            // The back face is "thickness" further along the sample's own view ray
            const float3 frontDelta = samplePos - pixCenterPos;
            const float3 backDelta = frontDelta + samplePos * (thickness / max(sampleZ, 1e-6));
            float2 hor01 = saturate(float2(GTVBAO_ACos(dot(normalize(frontDelta), viewVec)), GTVBAO_ACos(dot(normalize(backDelta), viewVec))) * (side / XE_GTAO_PI) + angOff);
            hor01 = side >= 0.0 ? hor01.xy : hor01.yx;
            hor01.x = GTVBAO_SliceRelCDF(hor01.x, angN, cosN, side > 0.0);
            hor01.y = GTVBAO_SliceRelCDF(hor01.y, angN, cosN, side > 0.0);
            hor01 = saturate(hor01 + localNoise.z / 32.0);

            const uint2 horInt = uint2(floor(hor01 * 32.0));
            const uint startMask = horInt.x < 32u ? (0xFFFFFFFFu << horInt.x) : 0u;
            const uint endMask = horInt.y != 0u ? (0xFFFFFFFFu >> (32u - horInt.y)) : 0u;
            occludedBits |= startMask & endMask;
         }
      }
      visibility += 1.0 - countbits(occludedBits) / 32.0;
   }
   visibility /= GTVBAO_SLICE_COUNT;
   visibility = pow(max(0.0, visibility), max(0.05, FinalValuePowerRT)); // XeGTAO's runtime dial
   XE_GTAO_ADJUST_VISIBILITY(visibility, viewspaceZ)

   outWorkingAOTermAndEdges[pixCoord] = XE_GTAO_MAIN_PASS_VALUE(visibility, edges);
}

// XeGTAO's Hilbert curve noise, extended to the R3 sequence for the third value
float3 GTVBAO_SpatioTemporalNoise(uint2 pixCoord, uint temporalIndex)
{
   uint index = HilbertIndex(pixCoord.x, pixCoord.y) + 288 * (temporalIndex % 64);
   return frac(0.5 + index * float3(0.8191725133961645, 0.6710436067037893, 0.5497004779019703));
}

// A game's own per pixel noise (slice, step and sector jitter in [0, 1))
#ifndef GTVBAO_NOISE
#define GTVBAO_NOISE(pixCoord) GTVBAO_SpatioTemporalNoise(pixCoord, uint(NoiseIndexRT))
#endif

[numthreads(XE_GTAO_NUMTHREADS_X, XE_GTAO_NUMTHREADS_Y, 1)] void gtvbao_main_pass_cs(uint2 dtid : SV_DispatchThreadID) {
   // tex0 = prefiltered viewspace depth MIP pyramid (our R32F)
   // smp = point-clamp
   XE_GTAO_CS_PROLOGUE
#if XE_GTAO_GENERATE_NORMALS
   const float3 viewspaceNormal = float3(0.0, 0.0, -1.0);
#else
   const float3 viewspaceNormal = XeGTAO_LoadViewspaceNormal(dtid);
#endif
   GTVBAO_MainPass(dtid, GTVBAO_NOISE(dtid), viewspaceNormal, tex0, smp, ao_term_and_edges);
}

#endif // LUMA_GTVBAO_HLSL
