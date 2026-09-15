#ifndef LUMA_MELE_TONEMAP_FILMIC_EXTENDED
#define LUMA_MELE_TONEMAP_FILMIC_EXTENDED

#include "Tonemap_MELE_HDRConfig.hlsli" // Pivot, probes and the fit slope floors.

// Sampled continuation of the game's own 1D filmic LUT, for the two filmic families. Include AFTER the
// permutation declares smpFilmicLUT and its sampler; MELE_IsFiniteNonNegative and MELE_NativeToneCurve come from
// Includes/Common.hlsl. The include above carries a guard, so a body that already pulled it in pays nothing.
//
// Nothing here invents a curve. Every anchor is a real read of the bound LUT, so a different shipped
// table gives a different continuation; the constants are probe positions, not curve coefficients.
//
// The fit's names stay snake_case (z_lo, pivot_z, probe_hi) where the rest of this code is camelCase: they are the
// subscripts of the derivations below and of the offline bench model, so the three stay comparable.
//
// Both evaluators hand their result straight to MELE_TryBuildGradeProxy, which rejects a non-finite or negative
// working value before any use. What that check cannot see is validated before the continuation: a bad channel that
// stays below the pivot selects the native sample and would otherwise pass. The ME3LE evaluator checks its own
// scene+bloom input; the ME2LE one receives z from MELE_TryExpExtendedInput, which already checked the scene and the
// bloom, so it checks only the native samples.

// The LUT in its own input domain z: the strip is addressed as SC * z, where SC is the native input
// scale covering z up to about 16.2. This is the raw read, with no pre-curve of any kind.
float MELE_FilmicLookupZ(float z)
{
   const float SC = 0.0616082214;
   return smpFilmicLUT.SampleLevel(smpFilmicLUTSampler_s, float2(SC * z, 0.5), 0).x;
}

struct MELE_FilmicFit
{
   float pivot_z;     // Where the continuation starts, in the LUT's own domain.
   float pivot_value; // The last native value before it takes over.
   float slope;       // Secant across the probe window, per unit of that domain.
   bool valid;        // False declines the reconstruction; the caller keeps the native SDR reference.
};

// The one piece of arithmetic the two families genuinely share: three reads and a secant between the
// outer two. Everything that differs between them is in the nodes the caller passes, which are
// compile-time probe positions with z_lo < pivot_z < z_hi.
//
// The slope denominator is the difference of the NODES. The input scale is already inside the sampled
// values, so dividing by a coordinate delta would count it twice.
//
// A secant, not an analytic derivative: the table is discrete, so this cannot be claimed to match the
// local slope of the neighbouring texels.
//
// The validity test is LOCAL by construction. A table that is globally non-monotone but rising across
// the window still yields a usable fit and is accepted; only a window that is flat, falling or
// non-finite is rejected. That matters for R16_UNORM, where quantization can flatten a short run: the
// answer there is to report the rejection, never to substitute a slope of 1 or one borrowed from
// another game's curve. Local validity is not a statement about the whole table.
//
// min_slope belongs to the caller's domain and the two families' thresholds are NOT interchangeable:
// see MELE_FILMIC_MIN_SLOPE_Z and MELE_FILMIC_MIN_SLOPE_X.
MELE_FilmicFit MELE_BuildFilmicFitZ(float z_lo, float pivot_z, float z_hi, float min_slope)
{
   MELE_FilmicFit fit;
   fit.pivot_z = pivot_z;
   const float probe_lo = MELE_FilmicLookupZ(z_lo);
   fit.pivot_value = MELE_FilmicLookupZ(pivot_z);
   const float probe_hi = MELE_FilmicLookupZ(z_hi);
   fit.slope = (probe_hi - probe_lo) / (z_hi - z_lo);
   fit.valid = MELE_IsFiniteNonNegative(float3(probe_lo, fit.pivot_value, probe_hi)) && probe_lo <= fit.pivot_value && fit.pivot_value <= probe_hi && MELE_IsFiniteNonNegative(fit.slope) && fit.slope > min_slope;
   return fit;
}

// ME3LE, family 04. This LUT is addressed with scene-linear scene+bloom directly - the domain z IS
// scene-x here - so the probe positions go in unchanged and one scalar fit serves all three channels.
// Reading the table through MELE_FilmicLookupZ, in its own input domain, also keeps this evaluator
// correct no matter which body includes it.
//
// Below the pivot the caller's already-sampled native value is reused rather than re-probed, so that
// region is the native result and not an approximation of it.
bool MELE_TryEvaluateME3LEFilmicExtended(float3 sceneWithBloom, float3 nativeFilmicRGB, out float3 extendedFilmicRGB)
{
   extendedFilmicRGB = nativeFilmicRGB;
   if (!MELE_IsFiniteNonNegative(sceneWithBloom) || !MELE_IsFiniteNonNegative(nativeFilmicRGB))
   {
      return false;
   }
   const MELE_FilmicFit fit = MELE_BuildFilmicFitZ(MELE_HDR_PROBE_LO, MELE_HDR_PIVOT, MELE_HDR_PROBE_HI, MELE_FILMIC_MIN_SLOPE_X);
   extendedFilmicRGB = sceneWithBloom <= fit.pivot_z ? nativeFilmicRGB : fit.pivot_value + fit.slope * (sceneWithBloom - fit.pivot_z);
   return fit.valid;
}

// ME2LE, family 03. The game runs two tone stages in series and adds the bloom BETWEEN them:
//
//   native:  ell(F(C) + B)      where F(x) = 1 - exp2(-1.7x) and ell(z) = L(SC * z)
//
// so each stage is continued separately and the bloom keeps its place:
//
//   W_tone = E_L(E_F(C) + B)
//
// It is NOT ell(F(C + B)); those two diverge as soon as the bloom is non-zero. z = E_F(C) + B is the caller's
// MELE_TryExpExtendedInput result: validated inputs keep it non-negative and never NaN, and an overflow to +inf
// lands above the pivot and is rejected downstream.
//
// THE FIT NODES COME FROM F ALONE and do not depend on C or B. Anchors that move with the signal they
// extend invert this curve's direction: the table flattens as its coordinate rises, so a brighter
// bloom would lower the fitted slope and make the working response FALL while the native one rises.
// Do not introduce B-dependent anchors.
//
// One fit serves all three channels, so a tinted bloom costs nothing extra. The probes still may not be
// cached across frames - the table's contents can be rewritten in place, so a cache keyed on the SRV
// pointer would be wrong.
//
// Per channel, the continuation runs when the SECOND stage has passed its pivot - which a bright bloom
// alone can cause even where the scene never left the native region. Otherwise the caller's native
// sample is reused, because both stages then stayed native and it IS the native result.
//
// There is no third case. C > pivot with a non-negative bloom puts z above pivot_z algebraically, and
// where float32 rounding defeats that (slope * (C - p) underflowing to zero within about a thousand
// ULP of the pivot) a fresh read at z returns exactly the native sample anyway - measured over the
// full ULP neighbourhood and a 200k random float32 sample on the shipped table.
bool MELE_TryEvaluateME2LEFilmicExtended(float3 z, float3 nativeFilmicRGB, out float3 extendedFilmicRGB)
{
   extendedFilmicRGB = nativeFilmicRGB;
   if (!MELE_IsFiniteNonNegative(nativeFilmicRGB))
   {
      return false;
   }
   const MELE_FilmicFit fit = MELE_BuildFilmicFitZ(MELE_NativeToneCurve(MELE_HDR_PROBE_LO), MELE_NativeToneCurve(MELE_HDR_PIVOT), MELE_NativeToneCurve(MELE_HDR_PROBE_HI), MELE_FILMIC_MIN_SLOPE_Z);
   extendedFilmicRGB = z > fit.pivot_z ? fit.pivot_value + fit.slope * (z - fit.pivot_z) : nativeFilmicRGB;
   return fit.valid;
}

#endif // LUMA_MELE_TONEMAP_FILMIC_EXTENDED
