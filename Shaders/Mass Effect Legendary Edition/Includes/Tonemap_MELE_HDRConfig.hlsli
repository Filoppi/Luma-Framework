#ifndef LUMA_MELE_TONEMAP_HDR_CONFIG
#define LUMA_MELE_TONEMAP_HDR_CONFIG

// Constants of the stage-1 HDR reconstruction, one model per colour family:
//
//   01  ME1LE/ME2LE exponential curve + colour LUT          8 permutations
//   02  ME1LE/ME2LE exponential curve + analytic grade      2 permutations
//   03  ME2LE pre-curve + 1D filmic LUT + colour LUT        4 permutations
//   04  ME3LE 1D filmic LUT from linear scene+bloom + LUT   4 permutations
//   05  ME3LE analytic hard-clip grade                      0x225A8330 only
//
// There is no selector; an in-game A/B across the three games chose these five.
//
// Each model prepares the grade input (a compressed proxy, or a tone curve continued past mid-gray) and
// divides the scale back out afterwards, so the grade sees range the vanilla clip had flattened. Where a
// model declines, the caller keeps the exact native SDR result. Families 01-04 take their RGB ratios from
// that native result and only their luminance from the reconstruction; family 05 keeps its working HDR
// and moves only its hue.
//
// Families 01-04 were ported to "Shaders/Borderlands 2 and The Pre-Sequel/Luma_BL2TPS_Tonemap.hlsl", which
// re-measured BL2's own vanilla curve first and drops the domain adapter because it compresses and restores
// in the same linear domain. Porting to another game needs its own evidence.

// Bench values, checked against the captured LUTs and cbuffers and kept unchanged through the in-game A/B.
// That A/B validates the set as a combination, not any number as individually tuned, so changing one needs
// its own frames.
#define MELE_HDR_BRIDGE_SHOULDER 0.75 // k, the max-channel proxy shoulder, in the adapted linear domain.
#define MELE_HDR_PIVOT           0.18 // p, scene mid-gray, where the tone-curve continuation starts.
#define MELE_HDR_PROBE_LO        0.16 // Sampled-fit probes for the filmic families, in scene-x.
#define MELE_HDR_PROBE_HI        0.20

// Roundoff tolerance on the bridge's bounded-proxy check. The shoulder is asymptotic to 1, so a compressed
// max channel can only exceed it by arithmetic error. It is not a clamp: widening it to make a failing case
// pass would hide the condition the check reports.
#define MELE_HDR_BRIDGE_PROXY_EPS 1e-4

// The relative luminance below which "MELE_NativeColorAtLuminance" returns its reference unscaled. Deep fades
// put white under it.
#define MELE_NATIVE_COLOR_MIN_LUMINANCE 1e-6

// Minimum accepted slope for each filmic family's fit. The numbers are not interchangeable: family 04 fits
// in scene-x, family 03 in the LUT's own input z, related by F' ~= 0.95 at the pivot and by the probe window
// widths.
// Family 03 only requires strictly positive. For scale, one R16_UNORM step (1/65535) across the z window
// F(0.20) - F(0.16) ~= 0.0381 is a slope of 4.0e-4, so an accepted slope below that is quantization-limited;
// the bench reports that separately instead of rejecting it.
#define MELE_FILMIC_MIN_SLOPE_Z 0.0
#define MELE_FILMIC_MIN_SLOPE_X 1e-5

// Strength of family 05's hue-only transfer toward the native hard-clipped SDR, a calibration value. The
// in-game A/B ran 1.0; 0.75 replaced it on the offline measurement below, without a second in-game A/B.
// At exactly 1.0 the blend is the donor's ab. A hard clip drives the brightest colours to white, whose OKLab
// ab is matrix round-off (about 3.7e-8), and the renormalization scales that up to the target's full OKLab
// chroma, so a blown highlight's hue becomes arbitrary (measured +41.3 to +89.9 deg on one stimulus). At 0.75
// the target keeps 0.25 of its own ab, far above that round-off, so a neutral donor cannot reach that case.
// We don't add a donor-chroma threshold: it would be a new artistic rule, and lowering this number is the
// honest control.
#define MELE_HARDCLIP_HUE_STRENGTH 0.75

#endif // LUMA_MELE_TONEMAP_HDR_CONFIG
