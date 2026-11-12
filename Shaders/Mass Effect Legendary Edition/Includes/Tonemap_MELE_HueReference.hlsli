#ifndef LUMA_MELE_TONEMAP_HUE_REFERENCE
#define LUMA_MELE_TONEMAP_HUE_REFERENCE

#include "../../Includes/ACES.hlsl"  // BT709 <-> AP1 matrices for the gamut clamp.
#include "../../Includes/Oklab.hlsl" // Same include root as this folder's Common.hlsl, not the body's.

// Hue-only transfer for family 05, adapted from RenoDX's "renodx::color::correct::HueOKLab"
// (renodx/src/shaders/colorcorrect.hlsl), with its AP1-positive clamp (renodx/src/shaders/color/clamp.hlsl)
// rebuilt on Luma's own ACES matrices. Needs "Includes/Common.hlsl" for "MELE_IsFinite".
//
// Only the direction of the OKLab (a, b) vector moves toward the reference. The target keeps its OKLab
// lightness and OKLab chroma, so the reference is a hue donor, not a restoration of the vanilla SDR colour.
// Renormalizing to the target's own chroma also bounds the operation, so it can't amplify anything. When the
// blended vector is at or near zero (a neutral donor at strength 1, or a donor ab that cancels the target's),
// exactly zero comes out achromatic (safeDivision returns 1) and round-off is scaled up to the target's chroma
// in an arbitrary direction. That is deliberate, see "MELE_HARDCLIP_HUE_STRENGTH".
//
// Like RenoDX we clamp to positive AP1, which is wider than BT.709, so a negative BT.709 component out of this
// is ordinary wide-gamut colour. Rejecting the whole triple there would produce a visible switching boundary,
// so the only fallback is for arithmetic that actually broke. The C-infinity rolloff in the shared output tail
// stays the only display mapper.
//
// The OKLab locals keep the source's snake_case so this line-by-line port stays diffable with it.
float3 MELE_HueReferenceOKLab(float3 target, float3 reference, float strength)
{
   float3 target_lab = Oklab::linear_srgb_to_oklab(target);
   const float3 reference_lab = Oklab::linear_srgb_to_oklab(reference);

   const float2 target_ab = target_lab.yz;
   const float chroma_before = length(target_ab);
   float2 blended_ab = lerp(target_ab, reference_lab.yz, strength);
   blended_ab *= safeDivision(chroma_before, length(blended_ab), 1);

   target_lab.yz = blended_ab;
   const float3 corrected = Oklab::oklab_to_linear_srgb(target_lab);

   const float3 clamped = mul(ACES::AP1_TO_BT709_MAT, max(0.0, mul(ACES::BT709_TO_AP1_MAT, corrected)));
   return MELE_IsFinite(clamped) ? clamped : target;
}

#endif // LUMA_MELE_TONEMAP_HUE_REFERENCE
