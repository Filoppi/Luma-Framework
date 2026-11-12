#ifndef LUMA_MELE_TONEMAP_HDR_BRIDGE
#define LUMA_MELE_TONEMAP_HDR_BRIDGE

#include "Tonemap_MELE_HDRConfig.hlsli"

// Pure math. Include "Includes/Common.hlsl", "../Includes/Color.hlsl" (which brings "Math.hlsl") and
// "../Includes/Reinhard.hlsl" before this file. Reinhard.hlsl has no include guard, so including it from here
// would redefine its contents in the bodies that already include it.

// Max-channel proxy. One scalar for all three channels, so the limiter cannot move an RGB ratio; the
// per-channel character stays with the working curve and the native reference.
//
// For every family that uses this proxy (01, 03, 04, 05) the tail from grade input to linear gradedHDR is
// "MELE_NativeGammaCurve" then gamma_to_linear(., GCT_MIRROR), which with an identity colour grade composes to
// (scale*c)^r for r = GammaColorScaleAndInverse.w * DefaultGamma, read from the frame's own cbuffer and never
// hardcoded to 2.2. Scaling the grade input by s scales the decoded linear output by s^r, so compressing in
// this adapted domain makes the restore exact: W = A(X), P = q*W, Q = A^-1(P) scales the input by q^(1/r), the
// output by exactly q, and a plain divide undoes it. A raw input multiply with a linear divide would leave a
// q^(r-1) residue whenever r != 1. A negative working value is refused before either conversion, so
// GCT_MIRROR never sees one here.
// That means q only exists in the adapted domain. It is not transferable between gamma, LUT codes, scene
// units and nits, and "MELE_TryRestoreGradeRange" must get the same q this produced.
//
// ReinhardRange with In_Peak <= 0 compresses from infinity and is the shifted rational shoulder this needs:
// identity at and below k, C1 across the seam, asymptotic to 1. ReinhardPiecewise would not do, as it matches
// value but not derivative at its seam.
//
// Every check runs before the pow, division or LUT read it protects. An invalid working value can't be
// recognised afterwards from the output: every grade this feeds passes it through a LUT read or a saturate
// ("MELE_NativeGammaCurve" opens with one), which launders a bad input into a plausible number.
//
// On failure both out parameters keep their entry values and the caller must use neither; it keeps the
// exact native SDR reference for the whole RGB triple. On success q is finite and in (0, 1].
bool MELE_TryBuildGradeProxy(float3 workNative, float r, out float q, out float3 proxyNative)
{
   q = 1.0;
   proxyNative = workNative;

   if (!MELE_IsFiniteNonNegative(workNative) || !MELE_IS_FINITE_POSITIVE(r))
   {
      return false;
   }

   // pow(x, 1) compiles to exp2(log2(x)) in DXBC, not the identity, and r was measured at exactly 1 in every
   // captured frame. This keeps that case bit-exact; it doesn't assume r is 1.
   const float3 adapted = (r == 1.0) ? workNative : gamma_to_linear(workNative, GCT_MIRROR, r);
   if (!MELE_IsFiniteNonNegative(adapted))
   {
      return false;
   }

   const float k = MELE_HDR_BRIDGE_SHOULDER;
   const float m = max3(adapted);
   if (m <= k)
   {
      return true; // No round trip, so no roundoff.
   }

   const float scale = Reinhard::ReinhardRange(m.xxx, k).x / m;
   const float3 compressed = adapted * scale;
   // See MELE_HDR_BRIDGE_PROXY_EPS: a compressed value genuinely above 1 is a shoulder failure, never clamped.
   if (!(scale > 0.0 && scale <= 1.0) || !MELE_IsFiniteNonNegative(compressed) || max3(compressed) > 1.0 + MELE_HDR_BRIDGE_PROXY_EPS)
   {
      return false;
   }

   const float3 candidateProxy = (r == 1.0) ? compressed : gamma_to_linear(compressed, GCT_MIRROR, 1.0 / r);
   if (!MELE_IsFiniteNonNegative(candidateProxy))
   {
      return false;
   }
   q = scale;
   proxyNative = candidateProxy;
   return true;
}

// Undo the compression on the decoded linear grade output. Don't invert the curve by re-reading the LUT: the
// LUT moved the colour, so that read can't recover the original scale. q comes from a successful
// "MELE_TryBuildGradeProxy", so only the quotient needs checking.
bool MELE_TryRestoreGradeRange(float3 gradedLinear, float q, out float3 workHDR)
{
   workHDR = gradedLinear / q;
   return MELE_IsFiniteNonNegative(workHDR);
}

// The native colour contract for families 01-04: RGB ratios from the exact native grade result, relative
// luminance from the new working value. Both sides use the BT.709 relative luminance of linear RGB, so on a
// finite positive reference the channel ratios survive exactly, including the per-channel shift and the
// whitening of the native chain. A channel the grade zeroed is not refilled. The guarantee ends here: the
// vignette, user controls, display map, grain and dither run after it in the shared output tail.
//
// The guards are deliberately not one blanket fallback:
// - The whole reference triple is validated, as a reference with a negative channel can still have a
//   positive luminance.
// - An exactly black reference or exactly zero target gives black. The grade produced that black.
// - A near-black but positive reference is divided normally. The LUT permutations carry clampFloor, so a
//   vanilla black arrives here at 1e-4 in luminance, two decades above the guard. Count the guard's trips,
//   don't widen it.
// - A non-finite gain or product returns the reference for the whole triple, as switching channels
//   independently would change hue.
// A positive target luminance is never clamped to 1, and no path takes colour from the raw scene.
//
// Declining returns the reference unscaled, the native SDR result the caller already holds. A bool +
// out-parameter form was measured: fxc stops folding the fallback into the select it already emits, costing
// +4 instructions on sixteen of the eighteen family 01-04 permutations and +3 on the two analytic ones
// (0x225A8330 never calls this and did not move). Don't re-attempt it without re-measuring.

float3 MELE_NativeColorAtLuminance(float3 nativeReferenceLinear, float targetLuminance)
{
   if (!MELE_IsFiniteNonNegative(nativeReferenceLinear) || !MELE_IsFiniteNonNegative(targetLuminance))
   {
      return nativeReferenceLinear;
   }
   if (targetLuminance == 0.0 || all(nativeReferenceLinear == 0.0))
   {
      return float3(0.0, 0.0, 0.0); // The grade produced that black.
   }
   const float referenceLuminance = GetLuminance(nativeReferenceLinear, CS_BT709);
   if (!MELE_IsFiniteNonNegative(referenceLuminance) || referenceLuminance < MELE_NATIVE_COLOR_MIN_LUMINANCE)
   {
      return nativeReferenceLinear;
   }
   const float gain = targetLuminance / referenceLuminance;
   const float3 result = nativeReferenceLinear * gain;
   if (!MELE_IsFiniteNonNegative(gain) || !MELE_IsFiniteNonNegative(result))
   {
      return nativeReferenceLinear;
   }
   return result;
}

// gradedHDR for families 01-04. The decoded native SDR result is the starting value and the only fallback; a
// valid reconstruction keeps its RGB ratios at the working relative luminance, so whatever hue the grade gave
// the working value is dropped. The output tail decodes sdrGamma again on purpose, see "Tonemap_MELE_Output.hlsli".
float3 MELE_NativeColorGradedHDR(float3 sdrGamma, float3 workHDR, bool workValid)
{
   const float3 sdrLinear = gamma_to_linear(sdrGamma, GCT_MIRROR);
   // One exit: an early return inside the branch trips fxc's X4000 on the inlined result.
   float3 gradedHDR = sdrLinear;
   if (workValid)
   {
      gradedHDR = MELE_NativeColorAtLuminance(sdrLinear, GetLuminance(workHDR, CS_BT709));
   }
   return gradedHDR;
}
#endif // LUMA_MELE_TONEMAP_HDR_BRIDGE
