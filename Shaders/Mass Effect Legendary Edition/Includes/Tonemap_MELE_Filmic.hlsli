#ifndef LUMA_MELE_TONEMAP_FILMIC
#define LUMA_MELE_TONEMAP_FILMIC

// Max-channel highlight extrapolation for the native 4096x1 R16_UNORM filmic LUT, shared by the ME2 filmic and
// all ME3 LUT permutations. RenoDX-style wrap, following Unreal Engine/Luma_UpgradeTonemapLUT.hlsl: match the
// filmic value and slope at scene mid-gray, keep the native curve below it, ease highlights toward that tangent,
// then shoulder-compress reversibly. Only the expansion scalar leaves this function, so the native per-channel
// value still reaches the grade untouched and the vanilla white blowout survives into HDR.
//
// Include after the permutation declares smpFilmicLUT and its sampler. MELE_FILMIC_PRECURVE supplies the domain
// the LUT is addressed in: ME3 samples scene-linear directly, ME2 pre-applies the native 1-exp2(-1.7x) curve.
#ifndef MELE_FILMIC_PRECURVE
#define MELE_FILMIC_PRECURVE(x) (x)
#endif

float MELE_FilmicMaxChannelExpand(float3 untonemapped)
{
   // Input scale covers scene-linear to about 16.2.
   const float SC = 0.0616082214;
   float mele_mch = max(max3(untonemapped), 1e-6);
   // Data-driven central difference around scene mid-gray.
   float y_mid = smpFilmicLUT.SampleLevel(smpFilmicLUTSampler_s, float2(SC * MELE_FILMIC_PRECURVE(0.18), 0.5), 0).x;
   float g_lo = smpFilmicLUT.SampleLevel(smpFilmicLUTSampler_s, float2(SC * MELE_FILMIC_PRECURVE(0.16), 0.5), 0).x;
   float g_hi = smpFilmicLUT.SampleLevel(smpFilmicLUTSampler_s, float2(SC * MELE_FILMIC_PRECURVE(0.20), 0.5), 0).x;
   float slope = (g_hi - g_lo) / 0.04;
   float g_mch = smpFilmicLUT.SampleLevel(smpFilmicLUTSampler_s, float2(SC * MELE_FILMIC_PRECURVE(mele_mch), 0.5), 0).x;
   // Curve ceiling read through the includer's domain, never a fixed 1.0: ME2's pre-curve saturates at 1, so its
   // coordinate stays inside the first 6.16% of the LUT and a 1.0 denominator would starve its expansion by 1.7x
   // to 6.5x. g_mch converges to exactly this ceiling, so progress still reaches 1 with no overshoot. Sample the
   // last texel center rather than u = 1.0 so the game's sampler addressing mode cannot affect the read.
   const float LUT_LAST_TEXEL = 4095.5 / 4096.0;
   float uv_top = min(LUT_LAST_TEXEL, SC * MELE_FILMIC_PRECURVE(1e4));
   float g_top = smpFilmicLUT.SampleLevel(smpFilmicLUTSampler_s, float2(uv_top, 0.5), 0).x;
   float tm_tan = y_mid + slope * (mele_mch - 0.18); // C1 tangent at mid-gray.
   // Zero at and below mid-gray for any curve, one at the ceiling. The guard only covers a degenerate flat LUT.
   float progress = saturate((g_mch - y_mid) / max(g_top - y_mid, 1e-4));
   float tm = lerp(g_mch, tm_tan, progress * progress);   // Delay highlight expansion through upper mids.
   float fit = Reinhard::ReinhardPiecewise(tm, 1.0, 0.9); // Identity below 0.9, shoulder to [0,1].
   // Reinhard's shoulder exposure is 10 here, so this is exactly tm + 0.1 and reaches 1.0 at the 0.9 seam.
   return (tm > 0.9) ? (tm / fit) : 1.0;
}

#endif // LUMA_MELE_TONEMAP_FILMIC