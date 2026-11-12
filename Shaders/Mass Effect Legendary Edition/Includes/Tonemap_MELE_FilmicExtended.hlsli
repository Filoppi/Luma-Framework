#ifndef LUMA_MELE_TONEMAP_FILMIC_EXTENDED
#define LUMA_MELE_TONEMAP_FILMIC_EXTENDED

#include "Tonemap_MELE_HDRConfig.hlsli" // Pivot, probes and the fit slope floors.

// Sampled continuation of the game's own 1D filmic LUT, for the two filmic families. Include after the
// permutation declares "smpFilmicLUT" and its sampler, and after "Includes/Common.hlsl".
//
// Every anchor is a real read of the bound LUT, so a different shipped table gives a different continuation;
// the constants are probe positions, not curve coefficients.
//
// The fit's names stay snake_case (z_lo, pivot_z, probe_hi) to match the derivations below and the offline
// bench model.
//
// Both evaluators hand their result to "MELE_TryBuildGradeProxy", which rejects a non-finite or negative working
// value. A bad channel that stays below the pivot selects the native sample and would pass that check, so the
// inputs are validated before the continuation. The ME3LE evaluator checks its own scene+bloom input; the ME2LE
// one gets z from "MELE_TryExpExtendedInput", which already checked the scene and bloom, so it checks only the
// native samples.

// The raw LUT read in its own input domain z: the strip is addressed as SC * z, where SC is the native input
// scale covering z up to about 16.2.
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
   bool valid;        // False declines the reconstruction.
};

// The part the two families share: three reads and a secant between the outer two. The nodes the caller
// passes are compile-time probe positions with z_lo < pivot_z < z_hi.
//
// The slope denominator is the difference of the nodes. The input scale is already inside the sampled values,
// so dividing by a coordinate delta would count it twice. The table is discrete, so the secant isn't claimed
// to match the local slope of the neighbouring texels.
//
// The validity test is local: a globally non-monotone table that rises across the window is accepted, and
// only a flat, falling or non-finite window is rejected. R16_UNORM quantization can flatten a short run; report
// the rejection there, never substitute a slope of 1 or one borrowed from another game's curve.
//
// min_slope is in the caller's domain, see "MELE_FILMIC_MIN_SLOPE_Z" and "MELE_FILMIC_MIN_SLOPE_X".
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

// ME3LE, family 04. This LUT is addressed with linear scene+bloom directly (z is scene-x here), so the probe
// positions go in unchanged and one scalar fit serves all three channels. Below the pivot the caller's native
// sample is reused rather than re-probed, so that region is exactly the native result.
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

// ME2LE, family 03. The game runs two tone stages in series and adds the bloom between them:
//
//   native:  ell(F(C) + B)      where F(x) = 1 - exp2(-1.7x) and ell(z) = L(SC * z)
//
// so each stage is continued separately and the bloom keeps its place:
//
//   W_tone = E_L(E_F(C) + B)
//
// This is not ell(F(C + B)), which diverges as soon as the bloom is non-zero. z = E_F(C) + B comes from
// "MELE_TryExpExtendedInput": validated inputs keep it non-negative and never NaN, and an overflow to +inf
// lands above the pivot and is rejected downstream.
//
// The fit nodes come from F alone and don't depend on C or B. The table flattens as its coordinate rises, so
// anchors that moved with the bloom would lower the fitted slope as the bloom brightens and make the working
// response fall while the native one rises.
//
// One fit serves all three channels, so a tinted bloom costs nothing extra. The probes can't be cached across
// frames, as the table can be rewritten in place, so a cache keyed on the SRV pointer would be wrong.
//
// Per channel, the continuation runs when the second stage has passed its pivot, which a bright bloom alone can
// cause where the scene never left the native region. Otherwise both stages stayed native and the caller's
// native sample is reused.
// There is no third case. C > pivot with a non-negative bloom puts z above pivot_z algebraically, and where
// float32 rounding defeats that (slope * (C - p) underflowing to zero within about a thousand ULP of the pivot)
// a fresh read at z returns exactly the native sample anyway, measured over the full ULP neighbourhood and a
// 200k random float32 sample on the shipped table.
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
