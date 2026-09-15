#ifndef LUMA_MELE_TONEMAP_HDR_BRIDGE
#define LUMA_MELE_TONEMAP_HDR_BRIDGE

#include "Tonemap_MELE_HDRConfig.hlsli"

// Pure math. Prerequisites, which this file deliberately does NOT include:
//   Includes/Common.hlsl      MELE_IsFiniteNonNegative
//   ../Includes/Color.hlsl    gamma_to_linear, GetLuminance, GCT_MIRROR, CS_BT709
//   ../Includes/Math.hlsl     max3, FLT_MAX  (arrive through Color.hlsl)
//   ../Includes/Reinhard.hlsl ReinhardRange
// Reinhard.hlsl has no include guard, so including it from here would redefine its contents in the
// bodies that already include it. Include it in the body, before this file.

// Max-channel proxy. One scalar for all three channels, so the limiter cannot move an RGB ratio; the
// per-channel character stays owned by the working curve and by the native reference.
//
// Adapted domain. For every family that uses this proxy (01, 03, 04, 05) the tail from grade input to linear gradedHDR is
// MELE_NativeGammaCurve followed by gamma_to_linear(., GCT_MIRROR), which with an identity colour grade
// composes to (scale*c)^r for r = GammaColorScaleAndInverse.w * DefaultGamma, read from the frame's own
// cbuffer and never hardcoded to 2.2. Scaling the grade INPUT by s scales the decoded linear OUTPUT by s^r,
// so compressing in the adapted domain makes the restore exact - W = A(X), P = q*W, Q = A^-1(P) scales the
// input by q^(1/r), the output by exactly q, and a plain divide undoes it. A raw input multiply with a
// linear divide would leave a q^(r-1) residue whenever r != 1. A negative working value is refused before
// either conversion, so GCT_MIRROR never sees one here.
//
// ReinhardRange with In_Peak <= 0 compresses from infinity and is the shifted rational shoulder this
// needs: identity at and below k, C1 across the seam, asymptotic to 1. It is NOT ReinhardPiecewise,
// which matches value but not derivative at its seam.
//
// q BELONGS TO THE ADAPTED DOMAIN. It is not transferable between gamma, LUT codes, scene units and
// nits, and MELE_TryRestoreGradeRange must be handed the same q this produced.
//
// Every check runs BEFORE the pow, the division and the LUT read it protects. An invalid working
// value cannot be recognised afterwards from the finiteness of the output: every grade this feeds
// passes it through a LUT read or a saturate (MELE_NativeGammaCurve opens with one), which launders
// a bad input into a plausible number.
//
// On failure both out parameters keep the neutral values written at entry and the caller must use
// neither: false means the HDR reconstruction declined and the caller retains the exact
// native SDR reference for the whole RGB triple. On success q is finite and in (0, 1].
bool MELE_TryBuildGradeProxy(float3 workNative, float r, out float q, out float3 proxyNative)
{
   q = 1.0;
   proxyNative = workNative;

   if (!MELE_IsFiniteNonNegative(workNative) || !(r > 0.0 && r <= FLT_MAX))
   {
      return false;
   }

   // pow(x, 1) compiles to exp2(log2(x)) in DXBC, not the identity, and r was measured at exactly 1
   // in every captured frame. This keeps that common case bit-exact; it is a shortcut for one value
   // of r, never an assumption that r is 1.
   const float3 adapted = (r == 1.0) ? workNative : gamma_to_linear(workNative, GCT_MIRROR, r);
   if (!MELE_IsFiniteNonNegative(adapted))
   {
      return false;
   }

   const float k = MELE_HDR_BRIDGE_SHOULDER;
   const float m = max3(adapted);
   if (m <= k)
   {
      return true; // q stays 1 and the proxy stays the working value: no round trip, no roundoff.
   }

   const float scale = Reinhard::ReinhardRange(m.xxx, k).x / m;
   const float3 compressed = adapted * scale;
   // The shoulder is asymptotic to 1, so the tolerance covers roundoff in it and nothing else. A
   // compressed value genuinely above 1 means the shoulder did not do its job, which is a failure
   // rather than something to clamp quietly.
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

// Undo the compression on the decoded linear grade output. Never invert the curve by re-reading the
// changed LUT output: the LUT moved the colour, so that read cannot recover the original scale.
// q must come from a successful MELE_TryBuildGradeProxy, so only the quotient needs checking: a bad
// gradedLinear stays bad after dividing by a finite positive q.
bool MELE_TryRestoreGradeRange(float3 gradedLinear, float q, out float3 workHDR)
{
   workHDR = gradedLinear / q;
   return MELE_IsFiniteNonNegative(workHDR);
}

// The NATIVE colour contract for families 01-04: RGB ratios from the exact native grade result,
// luminance from the new working value. Y is the same linear BT.709 luminance on both sides, so on a
// finite positive reference the reference's channel ratios survive exactly - the per-channel shift
// and the whitening the native chain produced are kept, not undone. A coloured reference stays
// coloured, equal channels stay equal, and a channel the grade zeroed is not refilled.
//
// Of the working value only Y is used; its own hue and chroma are deliberately discarded.
//
// The guarantee ends here, before the shared output tail. The vignette, DICE, the user controls, grain
// and dither all run after it.
//
// Guard contract, deliberately not one blanket fallback:
//   the WHOLE reference triple is validated, not only its luminance: a reference with a negative
//     channel can still have a positive luminance.
//   exactly black reference or exactly zero target -> black. The grade produced that black.
//   near-black but positive reference -> divided normally. The LUT permutations carry clampFloor, so
//     a vanilla black arrives here at 1e-4 in luminance, two decades above the guard. Collapsing that
//     to black would erase a real part of the native output; count the guard's trips, do not widen it.
//   a non-finite gain or product -> the reference itself for the WHOLE triple. Switching channels
//     independently would change hue, which is the failure being avoided.
// A positive target luminance is never clamped to 1, and no path takes colour from the raw scene.
//
// Declining returns the reference unscaled, which is the exact native SDR result the caller already
// holds - there is no second HDR model to fall back to and no fallback for a caller to choose. A
// bool + out-parameter form would move that decision to the call site, and it was written and
// measured: fxc costs 3 to 4 extra instructions on every permutation that uses this helper, because
// it stops folding the fallback into the select it already emits. That is the eighteen of families
// 01-04, +4 on sixteen of them and +3 on the two analytic ones; 0x225A8330 takes its colour from the
// hue donor instead, never calls this, and did not move. Measured, not assumed. Do not re-attempt it
// without re-measuring.
#define MELE_NATIVE_COLOR_MIN_LUMINANCE 1e-6

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

// gradedHDR for families 01-04. The exact native SDR result, decoded once, is the starting value and the only
// fallback for the whole triple; a valid reconstruction keeps its RGB ratios at the working luminance, so whatever
// hue the grade gave the working value is dropped. The output tail decodes sdrGamma again on purpose; see
// Tonemap_MELE_Output.hlsli.
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
