// clang-format off
// ORDER MATTERS — do NOT let clang-format sort these. The game-local Common.hlsl MUST come first: it defines
// LumaGameSettings (via GameCBuffers.hlsl) BEFORE the shared Settings.hlsl (reached through DICE.hlsl -> Common.hlsl)
// declares the LumaSettings cbuffer. Sorted after DICE.hlsl, GameSettings becomes the empty fallback struct and
// every LumaSettings.GameSettings.* reference fails to compile (invalid subscript).
#include "Includes/Common.hlsl" // game-local: LumaGameSettings (grade sliders) + shared Common (Color/Math/Settings) — keep FIRST
#include "../Includes/DICE.hlsl"
// clang-format on

// Borderlands 2 + The Pre-Sequel — uber post-process / tonemap SHARED IMPLEMENTATION (UE3, via dgVoodoo D3D9->11).
// Holds the grade body as RunTonemap(); the per-hash wrapper files (Tonemap_0x<HASH>.ps_5_0.hlsl, one per game ×
// dgVoodoo version) declare the full UE3 interpolator set + main() and forward to it. No hash in this filename ->
// not matched/replaced directly; it is #included by the wrappers.
//
// Vanilla body (DOF + screen-blend bloom + vignette + ImageAdjustments + 16-slice ColorGradingLUT) transcribed
// VERBATIM (register-level) from the readable DX9 BL2 tonemap (tonemap_0x54ED86A0.ps_3_0),
// constants remapped DX9 cN -> cb4[N+8].
//
// HDR follows the MELE 01-04 split (Shaders/Mass Effect Legendary Edition/Tonemap_ME_Analytic_Body.hlsl). The
// exact native ImageAdjustments + ColorGradingLUT result is the COMPLETE colour reference, including the game's
// per-channel hue, saturation, tint and its own highlight whitening. A separate unbounded continuation of the
// measured K=0 response (W*c in linear light) passes through a reversible bounded copy of the real LUT to obtain
// HDR luminance, and ONLY that luminance is projected onto the native graded RGB ratios. DICE performs the only
// final display rolloff. SDR executes none of the reconstruction (its scene still gets the user Exposure, Bloom
// and Vignette controls, and Luma's pyramidal bloom is on by default).
//
// This is NOT inverse tonemapping, NOT hue restoration from the raw scene, and NOT MELE family 05's hard-clip
// treatment: no path anywhere takes chroma from the working value.
// The per-channel ImageAdjustments curve below MUST keep the original's swizzles (r0.zzxy / r3.z,w,xy) — a
// "cleaner" rewrite swaps channels and casts the whole image green.

// HDR / vanilla. 1 = the reconstruction + DICE display map above (default). 0 = the vanilla SDR grade on every
// display mode, without the dither.
#ifndef TONEMAP_TYPE
#define TONEMAP_TYPE 1
#endif

// ---- Per-game texture/sampler slot map: Borderlands 2 vs The Pre-Sequel ------------------------------
// dgVoodoo maps DX9 sampler sN 1:1 onto DX11 tN. TPS inserts a LightShaftTexture at slot 1, shifting bloom/vignette/
// LUT/DOF down one (DX9: BL2 tonemap_0x54ED86A0 LUT@s3/DOF@s4; TPS tps_tonemap_0xF8997849 lightshaft@s1/LUT@s4/DOF@s5).
// The grade math is identical, so the body is shared: the TPS wrapper #defines these macros, BL2 leaves them at the
// identity below.
#ifndef TM_T_BLOOM
#define TM_T_BLOOM     t1 // FilterColor1Texture (screen-blend bloom)
#define TM_T_VIGNETTE  t2 // VignetteTexture
#define TM_T_LUT       t3 // ColorGradingLUT (256x16, 16-slice)
#define TM_T_DOF       t4 // LowResPostProcessBuffer (half-res DOF)
#define TM_T_LUMABLOOM t5 // injected Luma HDR bloom (free slot on BL2)
#define TM_S_BLOOM     s1
#define TM_S_VIGNETTE  s2
#define TM_S_LUT       s3
#define TM_S_DOF       s4
#endif

Texture2D<float4> t0 : register(t0); // SceneColorTexture (fp16 HDR scene)
#if TM_HAS_LIGHTSHAFT
Texture2D<float4> t_lightshaft : register(TM_T_LIGHTSHAFT); // LightShaftTexture (TPS god rays) — slot 1
#endif
Texture2D<float4> t1 : register(TM_T_BLOOM);     // FilterColor1Texture (bloom)        BL2 t1 / TPS t2
Texture2D<float4> t2 : register(TM_T_VIGNETTE);  // VignetteTexture                    BL2 t2 / TPS t3
Texture2D<float4> t3 : register(TM_T_LUT);       // ColorGradingLUT (256x16, 16-slice) BL2 t3 / TPS t4
Texture2D<float4> t4 : register(TM_T_DOF);       // LowResPostProcessBuffer (half-res DOF) BL2 t4 / TPS t5
Texture2D<float4> t5 : register(TM_T_LUMABLOOM); // Luma HDR pyramidal bloom, bound by the mod when LumaBloomEnable (BL2 t5 / TPS t8 — TPS t5 is the native DOF)

SamplerState s0_s : register(s0);
#if TM_HAS_LIGHTSHAFT
SamplerState s_lightshaft_s : register(TM_S_LIGHTSHAFT); // TPS s1: the light-shaft texture's own sampler
#endif
SamplerState s1_s : register(TM_S_BLOOM);    // BL2 s1 / TPS s2
SamplerState s2_s : register(TM_S_VIGNETTE); // BL2 s2 / TPS s3
SamplerState s3_s : register(TM_S_LUT);      // BL2 s3 / TPS s4
SamplerState s4_s : register(TM_S_DOF);      // BL2 s4 / TPS s5

cbuffer cb3 : register(b3)
{
   float4 cb3[77];
}
cbuffer cb4 : register(b4)
{
   float4 cb4[236];
}

#define BloomTintAndScreenBlendThreshold cb4[16] // c8
#define ImageAdjustments2                cb4[17] // c9
#define ImageAdjustments3                cb4[18] // c10
#define HalfResMaskRect                  cb4[19] // c11
#define DOFKernelSize                    cb4[20] // c12
#define VignetteSettings                 cb4[21] // c13
#define VignetteColor                    cb4[22] // c14

// ====================== HDR luminance reconstruction ======================
// The native branch in RunTonemap owns the colour; everything here owns the range. See the file
// header for the contract - only Y crosses from the working value to the output.

#define BL2TPS_HDR_BRIDGE_SHOULDER        0.75
#define BL2TPS_HDR_PROXY_EPS              1e-4
#define BL2TPS_NATIVE_COLOR_MIN_LUMINANCE 1e-6

// Every value this reconstruction guards is a light quantity with no meaning below zero, so one
// predicate covers all of them, and for THAT predicate two ordered comparisons are exact:
// !IsNaN_Strict(x) && !IsInfinite_Strict(x) && x >= 0 is the same set as 0 <= x <= FLT_MAX. NaN
// fails both comparisons (DXBC ge/le are ordered), +INF fails the upper one, -INF and negatives fail
// the lower one. This is not the x != x idiom Math.hlsl warns about and works around with bit tests -
// fxc cannot fold a comparison against a constant - and it costs 2 instructions per channel where
// IsNaN_Strict alone costs 6. Measured, not assumed: building 0xD00AA2A7 with the bit-test helpers
// instead costs it 130 more instructions. Do not "tidy" these back to Math.hlsl without re-measuring.
bool BL2TPS_IsFiniteNonNegative(float x)
{
   return x >= 0.0 && x <= FLT_MAX;
}
bool BL2TPS_IsFiniteNonNegative(float3 v)
{
   return all(v >= 0.0) && all(v <= FLT_MAX);
}

// The shoulder the max-channel proxy rides: identity at and below k, C1 across the seam, asymptotic
// to 1. This is Reinhard::ReinhardRange specialized to the only arguments this shader ever passed it
// - In_Peak = -1, Out_Peak = 1, ClampOutput = false - and to its only caller, which has already
// established peak > k. Under those the generic body loses its dead In_Peak > 0 range restore, its
// trailing (Color <= k) select, and ReinhardSimple's abs(), which is the identity on a positive
// argument. What survives is the same float32 operation sequence, so it returns the same bits:
// checked against the generic helper over 2.6M values, including 20k ULP above the seam.
//
// Do NOT fold this to (peak - 0.5625) / (peak - 0.5). It is algebraically equal and reorders the
// rounding, which is exactly what this pass is not allowed to do.
float BL2TPS_CompressWorkingPeak(float peak)
{
   const float k = BL2TPS_HDR_BRIDGE_SHOULDER;
   const float x = peak - k;
   return k + x / (x / (1.0 - k) + 1.0);
}

// The native 16-slice trilinear LUT read, reproduced for the working branch. NOT a generic LUT
// sampler: it is the block in RunTonemap transcribed against its own compiled listing, and two of
// its constants are traps.
//
// Routing, decoded from the ImageAdjustments output (it reaches the native block as (B, B, R, G),
// and as (gB, gG, gR, gB) after fxc's own register allocation): the slice index comes from BLUE, the
// within-slice U from RED, V from GREEN. The table is 256x16, so 0.05859375 = 0.9375/16,
// 0.0625 = 1/16, 0.001953125 = 0.5/256, 0.03125 = 0.5/16, and 0.064453125 is one slice along U.
//
// TRAP 1: the slice index is floored from b * 14.9998999, but the lerp weight is b * 15 minus that
//         floor. Two different constants for what reads as one scale; folding them to 15 moves the
//         weight on every pixel.
// TRAP 2: the weight is taken from b itself, not from the already-scaled b * 14.9998999.
//
// The native block is deliberately NOT routed through this helper - provenance of the verbatim
// register transcription is worth more than removing the duplication. Equivalence is held offline
// instead: the four derived coordinates (the two slice U's, V and the lerp weight) of both forms were
// compared on exact float32 equality over 1.14M inputs. A deterministic sampler handed identical
// coordinates returns identical colours for ANY table, which is a stronger statement than comparing
// sampled values from one.
float3 BL2TPS_SampleColorGradeGamma(float3 gammaRGB)
{
   const float sliceScaled = gammaRGB.b * 14.9998999;
   const float sliceLo = sliceScaled - frac(sliceScaled);
   const float weight = gammaRGB.b * 15.0 - sliceLo;
   const float u = sliceLo * 0.0625 + gammaRGB.r * 0.05859375;
   const float v = gammaRGB.g * 0.9375 + 0.03125;
   const float3 lo = t3.Sample(s3_s, float2(u + 0.001953125, v)).rgb;
   const float3 hi = t3.Sample(s3_s, float2(u + 0.064453125, v)).rgb;
   return weight * (hi - lo) + lo;
}

// The working value: the measured native response continued past its own shoulder, then graded.
//
// Measured in-game over 52 constant sets, the native per-channel curve is an analytic Reinhard with
// its white point at scene 14, driven by the live exposure W: A = 0.22/W, Y = 1 + A/14 (so T(14) = 1),
// Z ~ 0.2512/W, K = 0 everywhere seen. Below Z it is (W*c)^(1/2.2) in gamma, which linearises to
// exactly W*c - a pure gain, not an approximation of one. Continuing THAT branch over the whole
// positive range gives E(c) = W*c: the response the game itself applies to everything below mid-gray,
// with the Reinhard shoulder and the clip at scene >= 14 simply absent. No asymptote, no invented
// curve, no second tonemapper - DICE alone owns the display mapping.
//
// Valid only for K == 0, which is every constant set captured so far. A non-zero K mixes the toe in
// and W*c stops being the native response, so this declines and the caller keeps the exact native
// graded colour rather than inventing a range for a curve it cannot model. DEVELOPMENT logs a warning
// the first time a non-zero K is actually observed - main.cpp, CaptureGradeConstants.
//
// Only the luminance leaves this function. The reconstruction works in RGB throughout - the proxy
// must not move a channel ratio, so it has to see all three - but the caller has no use for that
// colour and must not take it: hue, saturation and whitening all belong to the native grade. Taking
// Y here rather than at the call site puts the contract in the signature.
//
// On false targetLuminance must not be consumed.
bool BL2TPS_TryBuildWorkingLuminance(float3 curveInput, out float targetLuminance)
{
   targetLuminance = 0.0;

   // curveInput is the only unproven input, so it keeps the full predicate. W needs finite AND
   // positive, which is one range test.
   //
   // Every simplified guard below is written as !(lo && hi), never as (x < lo || x > hi). The two
   // read the same but are not: NaN fails BOTH ordered comparisons, so the disjunction is false and
   // that form would ACCEPT it. Keep the negation outside the conjunction.
   const float W = ImageAdjustments2.w;
   if (!BL2TPS_IsFiniteNonNegative(curveInput) || !(W > 0.0 && W <= FLT_MAX) || ImageAdjustments3.x != 0.0)
   {
      return false;
   }

   // A non-negative triple times a positive scalar cannot go negative, so only the upper end is still
   // in question. NaN would fail this too, if the multiply somehow produced one.
   const float3 workLinear = curveInput * W;
   if (!all(workLinear <= FLT_MAX))
   {
      return false;
   }

   // Max-channel proxy: ONE scalar for all three channels, so the limiter cannot move an RGB ratio.
   // The per-channel character stays owned by the working curve and by the native reference.
   //
   // q belongs to the LINEAR working domain and is removed in it. The gamma encode below only
   // converts into the LUT's domain and the decode converts straight back, so the pair is an exact
   // inverse and a plain divide undoes the compression. (MELE's bridge carries a domain adapter
   // because it compresses in the grade-INPUT domain and divides in the linear output one, leaving a
   // q^(r-1) residue without it. Both ends are the same domain here, so an adapter would be dead.)
   const float k = BL2TPS_HDR_BRIDGE_SHOULDER;
   const float m = max3(workLinear);
   float q = 1.0;
   if (m > k)
   {
      q = BL2TPS_CompressWorkingPeak(m) / m;
   }

   // Every check runs BEFORE the pow, the division and the LUT read it protects. An invalid working
   // value cannot be recognised from the output: the encode saturates and the LUT read launders a bad
   // input into a plausible colour. A compressed value genuinely above 1 means the shoulder did not
   // do its job, which is a failure rather than something to clamp quietly.
   // workLinear is non-negative and q is tested positive, so the proxy cannot be negative; only its
   // ceiling is still open. A bad q needs no separate test either: -INF and negatives fail q <= 0,
   // +INF fails q > 1, and a NaN q poisons every proxy channel, which then fails all(x <= limit).
   const float3 proxyLinear = workLinear * q;
   if (q <= 0.0 || q > 1.0 || !all(proxyLinear <= 1.0 + BL2TPS_HDR_PROXY_EPS))
   {
      return false;
   }

   // saturate() because the native block's own LUT input is saturated. The shoulder is asymptotic to
   // 1 and leaves at most ~4.5e-5 of gamma overshoot on the max channel, but any overshoot at all
   // walks the U coordinate into the next slice's data.
   //
   // Going in, proxyLinear is non-negative by the guard right above, so GCT_MIRROR's sign round trip
   // would be dead weight - for x >= 0 it returns the same value.
   //
   // Coming back, the max(0) is not swallowing a fault. The lerp weight reaches 1 + (n + 1) * 6.7e-6
   // just under slice boundary n, because the floor is taken from b * 14.9998999 while the weight is
   // b * 15, so the trilinear read extrapolates a hair past the upper slice and a channel whose upper
   // sample is darker can land about 1e-4 below zero. The native block does exactly the same and its
   // own tail clamps it the same way. Without this the guard below would decline the entire
   // reconstruction on a near-black pixel and flicker back to the native graded colour. GCT_MIRROR keeps the
   // value signed rather than raising a NaN, so the max can see it.
   const float3 proxyGamma = saturate(linear_to_gamma(proxyLinear, GCT_NONE));
   const float3 gradedLinear = max(0.0, gamma_to_linear(BL2TPS_SampleColorGradeGamma(proxyGamma), GCT_MIRROR));
   if (!all(gradedLinear <= FLT_MAX))
   {
      return false;
   }

   // Never invert by re-reading the changed LUT output: the LUT moved the colour, so that read cannot
   // recover the original scale. Dividing a non-negative value by a positive q can only overflow
   // upward, which is the one thing still worth testing.
   const float3 restored = gradedLinear / q;
   if (!all(restored <= FLT_MAX))
   {
      return false;
   }
   targetLuminance = GetLuminance(restored, CS_BT709);
   return true;
}

// RGB ratios from the exact native grade result, luminance from the working value. Y is the same
// linear BT.709 luminance on both sides, so on a finite positive reference the reference's channel
// ratios survive exactly: the per-channel shift, the LUT tint and the whitening the native chain
// produced are kept, not undone. A coloured reference stays coloured, equal channels stay equal, and
// a channel the grade zeroed is not refilled. Of the working value only Y is used; its own hue and
// chroma are deliberately discarded.
//
// Guard contract, deliberately not one blanket fallback:
//   the WHOLE reference triple is validated, not only its luminance: a dot product returns a finite
//     number from non-finite channels, and a positive one from a reference with a negative channel.
//   exactly black reference or exactly zero target -> black. The grade produced that black.
//   a non-finite gain or product -> the reference itself for the WHOLE triple. Switching channels
//     independently would change hue, which is the failure being avoided.
// A positive target luminance is never clamped to 1, and no path takes colour from the raw scene.
//
// Value-returning on purpose. A bool + out-parameter form was written and measured in MELE and cost
// fxc 3 to 4 extra instructions on every permutation, because it stops folding the fallback into the
// select it already emits. Do not re-attempt it without re-measuring.
float3 BL2TPS_NativeColorAtLuminance(float3 nativeReferenceLinear, float targetLuminance)
{
   if (!BL2TPS_IsFiniteNonNegative(nativeReferenceLinear) || !BL2TPS_IsFiniteNonNegative(targetLuminance))
   {
      return nativeReferenceLinear;
   }
   // An exactly black reference needs no test of its own: its luminance is 0, which fails the floor
   // below, and the function then returns that same black.
   if (targetLuminance == 0.0)
   {
      return float3(0.0, 0.0, 0.0);
   }
   // The reference is already proven finite and non-negative, so its BT.709 luminance cannot be
   // negative; the floor and an overflow ceiling are the whole test, in the !(lo && hi) shape.
   const float referenceLuminance = GetLuminance(nativeReferenceLinear, CS_BT709);
   if (!(referenceLuminance >= BL2TPS_NATIVE_COLOR_MIN_LUMINANCE && referenceLuminance <= FLT_MAX))
   {
      return nativeReferenceLinear;
   }
   // gain needs no test of its own. It is non-negative by construction, and a non-finite one cannot
   // hide: the floor above guarantees at least one positive reference channel, so an infinite gain
   // overflows that channel and a NaN gain poisons all three. Either way the product fails below.
   const float gain = targetLuminance / referenceLuminance;
   const float3 result = nativeReferenceLinear * gain;
   if (!all(result <= FLT_MAX))
   {
      return nativeReferenceLinear;
   }
   return result;
}

// The tonemap grade. v5 = TEXCOORD0 (DOF radial/kernel coords in .zw), v6 = TEXCOORD1 (scene UV .xy, half-res DOF
// UV .zw) — the only interpolators the body uses. Returns the final gamma-space color (o0.a is always 0).
float4 RunTonemap(float4 v5, float4 v6)
{
   float4 o;
   float4 r0, r1, r2, r3;

   // --- DOF composite (verbatim) ---
   // t4.a is the in-focus weight (1 = sharp subject, 0.25 = max-blurred background); summed with a radial falloff it
   // picks between the half-res blurred buffer (stored pre-divided by 4) and the sharp scene.
   float3 hdrColor;
   r0.y = DOFKernelSize.w + v5.w;
   r0.x = v5.z;
   r0.xy = r0.xy * 2 + -1;
   r0.xy = r0.xy * DOFKernelSize.z;
   r0.x = saturate(dot(r0.xy, r0.xy) + 0); // dp2: BOTH components (verified in all four dgVoodoo dumps)
   r0.x = -r0.x + 1;
   r0.yz = max(v6.xzww, HalfResMaskRect.xxyw).yz;
   r1.xy = min(HalfResMaskRect.zw, r0.yz);
   r1 = t4.Sample(s4_s, r1.xy);
   r0.x = saturate(r0.x + r1.w);
   r2 = float4(1, 1, 0, 0) * v6.xyxx;
   r2 = t0.SampleLevel(s0_s, r2.xy, 0);
   hdrColor = lerp(r1.xyz * 4, r2.rgb, r0.x);

   // --- bloom ---
   if (LumaSettings.GameSettings.LumaBloomEnable > 0.5)
   {
      // Luma pyramidal bloom (t5, built by the mod from the fp16 scene; the game's is UNORM-clamped). Composited like
      // the vanilla branch below - the artists' per-area tint and the x4 - so BloomIntensity 1 is vanilla strength
      // (it arrives pre-scaled by the pyramid-to-native energy ratio, main.cpp). The vanilla screen-blend gate
      // (saturate(exp2(-3*luma) * .w)) is deliberately skipped: an 8-bit approximation that cancels the glow of the
      // brightest sources, the one thing this bloom exists to fix.
      float3 lumaBloom = t5.SampleLevel(s1_s, v6.xy, 0).rgb;
      hdrColor += lumaBloom * (BloomTintAndScreenBlendThreshold.xyz * (4.0 * LumaSettings.GameSettings.BloomIntensity));
   }
   else
   {
      // Vanilla bloom (screen-blend gated by luminance, t1), never scaled: Bloom Intensity belongs to the Luma pyramid.
      r0.w = dot(hdrColor, float3(0.300000012, 0.589999974, 0.109999999));
      r0.w = r0.w * -3;
      r0.w = exp2(r0.w);
      r0.w = saturate(r0.w * BloomTintAndScreenBlendThreshold.w);
      r1 = t1.Sample(s1_s, v5.zw);
      r1.xyz = r1.xyz * BloomTintAndScreenBlendThreshold.xyz;
      r1.xyz = r1.xyz * 4;
      hdrColor += r1.xyz * r0.w;
   }

#if TM_HAS_LIGHTSHAFT
   // Light shafts / god rays (TPS only), verbatim from tps_tonemap_0xF8997849: an inverse-luminance gate (adds only
   // into darker pixels), additive x4 colour, and a per-pixel attenuation in .a where shafts occlude.
   {
      float lsGate = saturate(exp2(dot(hdrColor, float3(0.300000012, 0.589999974, 0.109999999)) * -3.0));
      float4 ls = t_lightshaft.Sample(s_lightshaft_s, v5.zw);
      hdrColor = hdrColor * ls.w + (ls.xyz * 4.0) * lsGate;
   }
#endif

   // User Exposure (scene-referred, pre-grade; 1 = vanilla). Applies to both SDR and HDR — the grade below tracks it.
   hdrColor *= LumaSettings.GameSettings.Exposure;

   // The grade runs on the NATIVE scene like vanilla; the HDR reconstruction branches off it after the vignette.
   r0.xyz = hdrColor;

   // --- vignette (verbatim) ---
   float3 vignetteColor = r0.rgb;
   r1.xyz = r0.xyz * VignetteColor.xyz;
   r2.xyz = r0.xyz * -VignetteColor.xyz + r0.xyz;
   r1.xyz = v6.y * r2.xyz + r1.xyz;
   r2.xyz = r0.xyz * r1.xyz;
   r1.xyz = r0.xyz * -r1.xyz + r0.xyz;
   r3.xy = v6.xy + v6.xy;
   r3 = t2.Sample(s2_s, r3.xy);
   r0.w = saturate(r3.x + VignetteSettings.y);
   r1.xyz = r0.w * r1.xyz + r2.xyz;
   r2.y = 0.00999999978;
   r0.w = r2.y + -VignetteSettings.x;
   r0.xyz = (r0.w >= 0) ? r0.xyz : r1.xyz;
   // User Vignette Intensity: lerp between the pre-vignette color and the vignetted result (1 = vanilla, 0 = none).
   r0.xyz = lerp(vignetteColor, r0.xyz, LumaSettings.GameSettings.VignetteIntensity);

   // The curve's own input: post-vignette, pre-curve and UNCLIPPED - the physical colour before the native
   // per-channel curve compresses it. The HDR reconstruction continues the curve from here, and the native
   // branch below runs on this same value, unchanged.
   const float3 curveInput = r0.xyz;

   // --- ImageAdjustments per-channel curve (verbatim; keep swizzles exactly) ---
   r1 = r0.zzxy + -ImageAdjustments2.z;
   r1 = saturate(r1 * 10000);
   r2.xyz = r0.xyz + ImageAdjustments2.x;
   // Native guards: a zero divisor becomes 1e37 instead of inf, and log runs on |x| with -inf pinned to -1e37, so a
   // negative or zero channel still yields a finite value rather than NaN.
   r3.z = (abs(r2.x) > 0.0) ? 1 / abs(r2.x) : 1e37;
   r3.w = (abs(r2.y) > 0.0) ? 1 / abs(r2.y) : 1e37;
   r3.xy = (abs(r2.z) > 0.0) ? 1 / abs(r2.z) : 1e37;
   r2 = r0.zzxy * r3;
   r0.xyz = r0.xyz * ImageAdjustments2.w;
   r3.x = log2(abs(r0.x));
   r3.y = log2(abs(r0.y));
   r3.z = log2(abs(r0.z));
   r3.xyz = (asuint(r3.xyz) == 0xff800000u) ? -1e37 : r3.xyz;
   r0.xyz = r3.xyz * 0.454545468;
   r3.z = exp2(r0.x);
   r3.w = exp2(r0.y);
   r3.xy = exp2(r0.z);
   r0 = r2.yyzw * ImageAdjustments2.y + -r3.yyzw;
   r0 = r1 * r0 + r3;
   r1 = r2 * ImageAdjustments2.y + -r0.yyzw;
   r0 = saturate(ImageAdjustments3.x * r1 + r0);

   // --- 16-slice ColorGradingLUT (verbatim trilinear; keep swizzles exactly) ---
   // (Vanilla trilinear: the LUT is near-linear between grid points, so tetrahedral interpolation buys no visible gain.)
   r1.xyw = (r0.xwzz * float4(14.9998999, 0.9375, 0.9375, 0.05859375)).xyw;
   r0.x = frac(r1.x);
   r0.x = -r0.x + r1.x;
   r1.x = r0.x * 0.0625 + r1.w;
   r0.x = r0.y * 15 + -r0.x;
   r1 = r1.xyxy + float4(0.001953125, 0.03125, 0.064453125, 0.03125);
   r2 = t3.Sample(s3_s, r1.zw);
   r1 = t3.Sample(s3_s, r1.xy);
   r0.yzw = (-r1.xxyz + r2.xxyz).yzw;
   o.xyz = r0.x * r0.yzw + r1.xyz;

   // ====================== Luma HDR output ======================
   // o.rgb is the native graded look in gamma space and the COMPLETE colour reference (see the file header): HDR
   // replaces its luminance and nothing else, SDR presents it untouched.
   float3 gradedSdrGamma = o.rgb;
   float3 sdrLinear = gamma_to_linear(gradedSdrGamma, GCT_MIRROR);

   float3 postProcessedColor;

#if TONEMAP_TYPE >= 1
   if (LumaSettings.DisplayMode == 1) // HDR
   {
      const float paperWhite = LumaSettings.GamePaperWhiteNits / sRGB_WhiteLevelNits;
      const float peakWhite = LumaSettings.PeakWhiteNits / sRGB_WhiteLevelNits;

      // Colour from the native grade, range from the working value. Declining leaves the native grade exactly
      // as it is: there is no second HDR model to fall back to, and expanding a curve the reconstruction cannot
      // model is the failure the guards exist to prevent.
      float3 recovered = sdrLinear;

      float workLuminance;
      if (BL2TPS_TryBuildWorkingLuminance(curveInput, workLuminance))
      {
         recovered = BL2TPS_NativeColorAtLuminance(sdrLinear, workLuminance);
      }

      // --- User HDR grade (HDR display path only; defaults are vanilla no-ops) ---
      // Contrast BEFORE the display map so DICE contains whatever it pushes up: after the rolloff the slider would
      // escape the peak it just established, and nothing downstream re-contains it. Multiplicative around mid-gray, the
      // repo's form (RenoDX_Contrast); 0.18 is mid-gray here too, display-referred with 1.0 = paper white (gamma code ~0.46).
      // [branch] on a cbuffer uniform: at the 1.0 default this must be a BIT-EXACT no-op. The pow is spelled out with a
      // floored log2 so Contrast 0 on a black pixel is 0 * log2(1e-30) = 0 rather than pow(0, 0) = NaN.
      [branch] if (LumaSettings.GameSettings.Contrast != 1.0)
      {
         recovered = exp2(LumaSettings.GameSettings.Contrast * log2(max(recovered / MidGray, 1e-30))) * MidGray; // MidGray = 0.18, Color.hlsl
      }

      // Display rolloff to the user's peak/paper-white nits. DICE by-luminance keeps hue; the *_CORRECT_CHANNELS_BEYOND_
      // PEAK_WHITE type also gamut-maps a single channel riding past peak. Feed linear BT.709 directly: DICE converts to
      // BT.2020 itself, and a manual 709<->2020 round-trip no longer cancels once the per-channel gamut map is in.
      DICESettings settings = DefaultDICESettings(DICE_TYPE_BY_LUMINANCE_PQ_CORRECT_CHANNELS_BEYOND_PEAK_WHITE);
      // Highlight dechroma handed to DICE rather than run as our own pass afterwards: it runs INSIDE the containment in
      // the processing primaries, and only above ShoulderStart * PeakWhite (1/3 of peak for this type), so mid-tones
      // cannot be touched. Its ramp follows the max channel, but DICE enters it on its AVERAGE luminance, so a saturated
      // highlight switches on with a visible step (DICE.hlsl notes it; shared code, left as is). 0 = off for the OUTPUT
      // but not the cost: DICE's guard carries no [branch], so fxc flattens it for every pixel above the shoulder.
      settings.HighlightsDesaturation = LumaSettings.GameSettings.HighlightDechroma;
      float3 hdr = DICETonemap(recovered * paperWhite, peakWhite, settings) / paperWhite;

      // User saturation LAST, after the display map: the repo's convention. Color.hlsl's Saturation() is
      // lerp(GetLuminance(c, CS_BT709), c, s) - a BT.709-luminance lerp, not Oklab. 1 = vanilla.
      hdr = Saturation(hdr, LumaSettings.GameSettings.Saturation);

      postProcessedColor = hdr;
   }
   else // SDR (still presented through the scRGB swapchain) — sdrLinear is the vanilla grade, untouched
#endif
   {
      postProcessedColor = sdrLinear;
   }

#if UI_DRAW_TYPE >= 2
   // Pre-scale so the gamma-SDR HUD drawn on top (not pre-scaled) lands at UIPaperWhite after the composition
   // rescales the buffer by it; the scene then lands at GamePaperWhite. Gives the HUD its own paper white.
   postProcessedColor *= LumaSettings.GamePaperWhiteNits / max(LumaSettings.UIPaperWhiteNits, 1.0);
#endif

   // Sanitize (signed LUT excursions, the user grade and DICE can emit NaN/negatives -> garbage on the swapchain).
   postProcessedColor = (postProcessedColor == postProcessedColor) ? postProcessedColor : 0.0; // NaN -> 0
   postProcessedColor = max(0.0, postProcessedColor);

   // GCT_NONE, not GCT_MIRROR: the two lines above already forced this non-negative, so the mirror's
   // sign round trip would be dead weight. The SDR decode further up keeps its mirror - the native
   // trilinear read really can hand it a signed excursion.
   postProcessedColor = linear_to_gamma(postProcessedColor, GCT_NONE);

   // Anti-banding dither, one step of the output quantizer: the 8-bit code in SDR, 10-bit BT.2020 PQ in HDR.
#if TONEMAP_TYPE >= 1
   if (LumaSettings.GameSettings.Dithering > 0.5)
   {
      if (LumaSettings.DisplayMode == 0)
         ApplyDithering(postProcessedColor, v6.xy, true, 1.0, 8u, LumaSettings.FrameIndex, true);
      else
      {
         const float pqScale = max(LumaSettings.UIPaperWhiteNits, 1.0) / HDR10_MaxWhiteNits;
         float3 pq = Linear_to_PQ(BT709_To_BT2020(gamma_to_linear(postProcessedColor, GCT_MIRROR) * pqScale), GCT_MIRROR);
         ApplyDithering(pq, v6.xy, true, 1.0, 10u, LumaSettings.FrameIndex, true);
         postProcessedColor = linear_to_gamma(BT2020_To_BT709(PQ_to_Linear(pq, GCT_MIRROR)) / pqScale, GCT_MIRROR);
      }
   }
#endif

   return float4(postProcessedColor, 0.0); // vanilla wrote o0.w = 0
}
