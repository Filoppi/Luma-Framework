#ifndef LUMA_MELE_TONEMAP_EXP_EXTENDED
#define LUMA_MELE_TONEMAP_EXP_EXTENDED

#include "Tonemap_MELE_HDRConfig.hlsli" // MELE_HDR_PIVOT.

// Also needs Includes/Common.hlsl (MELE_NativeToneCurve and its rate, MELE_IsFiniteNonNegative), which every body
// that uses this file already includes.

// Tangent continuation of the native per-channel curve past scene mid-gray, for families 01-03
// (family 03's first tone stage). Below the pivot this IS MELE_NativeToneCurve, bit for bit; above it
// the curve is replaced by its own tangent so the working value keeps rising instead of saturating.
//
//   F(x)  = 1 - exp2(-a*x),  a = kMELE_NativeToneCurveRate   (MELE_NativeToneCurve, unchanged)
//   F'(p) = a * ln2 * exp2(-a*p)
//   E(x)  = F(x)                     for x <= p
//         = F(p) + F'(p) * (x - p)   for x >  p
//
// There is no positive inflection in this exponential, so there is no Hable-style shoulder to find
// here and no foreign coefficient to import. The native curve itself is never modified: the SDR
// reference still runs the bodies' inline transcription of it, and only this branch sees the continuation.
float3 MELE_ExpExtended(float3 x, float pivot)
{
   const float slope = kMELE_NativeToneCurveRate * 0.693147181 * exp2(-kMELE_NativeToneCurveRate * pivot);
   const float3 tangent = MELE_NativeToneCurve(pivot) + slope * (x - pivot);
   return float3(x.x <= pivot ? MELE_NativeToneCurve(x.x) : tangent.x,
                 x.y <= pivot ? MELE_NativeToneCurve(x.y) : tangent.y,
                 x.z <= pivot ? MELE_NativeToneCurve(x.z) : tangent.z);
}

// The working input of families 01-03 - the grade input, or family 03's filmic LUT coordinate: the scene continued
// past mid-gray, plus the bloom where vanilla adds it, so it reduces to the native input exactly wherever the scene
// sits at or below the pivot.
//
// The scene and the bloom are validated here, separately, and nowhere else. Everything downstream sees only their
// SUM, and a positive bloom hides a bad scene channel inside it: C = -0.1 and B = 0.2 give F(-0.1) + 0.2 = 0.074941,
// finite and non-negative, while the source already left this model's domain. The refusal covers the whole triple;
// the scene is never repaired with max(C, 0).
bool MELE_TryExpExtendedInput(float3 sceneLinear, float3 bloomLinear, out float3 workNative)
{
   workNative = MELE_ExpExtended(sceneLinear, MELE_HDR_PIVOT) + bloomLinear;
   return MELE_IsFiniteNonNegative(sceneLinear) && MELE_IsFiniteNonNegative(bloomLinear);
}

#endif // LUMA_MELE_TONEMAP_EXP_EXTENDED
