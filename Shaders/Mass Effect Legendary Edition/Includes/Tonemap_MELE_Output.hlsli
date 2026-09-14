// Shared stage-1 output tail, included inside each main() after it defines graded_hdr, sdr_gamma, v0, v1, o0, o1.
// Includer macros, all defaulting to off: TM_VIGNETTE_TYPE (none / radial-power / ME3 smoothstep), TM_HAS_GRAIN,
// TM_ALPHA_LUMA (native ME3 output luma to alpha).
#ifndef TM_VIGNETTE_TYPE
#define TM_VIGNETTE_TYPE 0
#endif
#ifndef TM_HAS_GRAIN
#define TM_HAS_GRAIN 0
#endif
#ifndef TM_ALPHA_LUMA
#define TM_ALPHA_LUMA 0
#endif

const float paperWhite = LumaSettings.GamePaperWhiteNits / sRGB_WhiteLevelNits;
const float peakWhite = LumaSettings.PeakWhiteNits / sRGB_WhiteLevelNits;

float3 sdr_lin = gamma_to_linear(sdr_gamma, GCT_MIRROR);

// Native vignette, hoisted ahead of the tonemap and expressed in linear so DICE absorbs its blue-tinted white
// point instead of the frame leaving stage 1 above Scene Peak. linear_to_gamma is a signed pure pow, so this is
// exactly the vanilla multiply in the encoded domain and SDR stays bit-identical. Grain and dither stay in gamma
// below, where hoisting would amplify shadow noise.
float3 vigLinear = 1.0;
#if TM_VIGNETTE_TYPE == 1
// The slider scales only radial darkening, so zero intensity does not alter the native white point tint.
{
   float2 vc = float2(-0.5, -0.5) + v0.zw;
   vc = float2(0.832050323, 0.554700196) * vc;
   float vd = max(9.99999975e-05, dot(vc, vc));
   vd = exp2(3.25 * log2(vd));
   vd = 1.0 - vd;
   vd = exp2(TM_VIG_POW * log2(max(vd, 9.99999975e-05)));
   const float3 white_point = TM_VIG_FLOOR + 1.0; // Floor plus center value.
   float3 vig = TM_VIG_FLOOR + vd;
   vig = white_point * lerp(1.0, vig / white_point, LumaSettings.GameSettings.VignetteIntensity);
   vigLinear = gamma_to_linear(vig, GCT_MIRROR);
}
#elif TM_VIGNETTE_TYPE == 3
// Native ME3 smoothstep vignette; scale only radial darkening, not its blue-tinted white point.
{
   float2 vc = v0.zw * ScreenUVScaleBias.xy + ScreenUVScaleBias.zw;
   vc = float2(-0.5, -0.5) + vc;
   vc = float2(0.832050323, 0.554700196) * vc;
   float vd = dot(vc, vc);
   vd = saturate(4.0 * (vd - 0.0500000007));
   float vs = (3.0 - 2.0 * vd) * vd * vd; // smoothstep(0, 1, vd).
   const float3 white_point = float3(1.01036298, 1.00000572, 1.16309249);
   float3 vig = white_point - vs * LumaSettings.GameSettings.VignetteIntensity;
   vigLinear = gamma_to_linear(vig, GCT_MIRROR);
}
#endif

float3 postProcessedColor;
if (LumaSettings.DisplayMode == 1) // HDR
{
   // graded_hdr arrives as paper-white-relative linear HDR; DICE owns peak rolloff and gamut from here.
   float3 recovered = graded_hdr * vigLinear;

   // DICE works in absolute-nit ratios, so its cap lands at the display peak. Type 2 also runs
   // CorrectOutOfRangeColor. Grain, dither, and RCAS can still push the result ~2% past Scene Peak; accepted.
   DICESettings settings = DefaultDICESettings(DICE_TYPE_BY_LUMINANCE_PQ_CORRECT_CHANNELS_BEYOND_PEAK_WHITE);
   float3 hdr = DICETonemap(recovered * paperWhite, peakWhite, settings) / paperWhite; // Game-Paper-White-relative.

   // User HDR grade in Game-Paper-White-relative linear RGB; defaults are no-ops.
   const float highlightDechroma = LumaSettings.GameSettings.HighlightDechroma;
   if (highlightDechroma > 0.0)
   {
      float dcExp = lerp(1.0, 0.05, highlightDechroma);
      // hdr is Game-Paper-White-relative while peakWhite is 80-nit-relative, so convert the peak before dividing.
      const float relativePeak = peakWhite / max(paperWhite, 1e-6);
      float dcWeight = saturate(pow(saturate(GetLuminance(hdr) / relativePeak), dcExp));
      hdr = Saturation(hdr, 1.0 - dcWeight);
   }
   hdr = Saturation(hdr, LumaSettings.GameSettings.Saturation);
   const float midGray = 0.18; // Game-Paper-White-relative mid-gray.
   hdr = (hdr - midGray) * LumaSettings.GameSettings.Contrast + midGray;

   postProcessedColor = hdr;
}
else // SDR still uses the scRGB swapchain; sdr_lin is the exact native grade.
{
   // No tonemap sits between here and the encode, so the linear vignette equals the vanilla gamma multiply.
   postProcessedColor = sdr_lin * vigLinear;
}

postProcessedColor = IsNaN_Strict(postProcessedColor) ? 0.0 : postProcessedColor; // Replace NaN with zero.
postProcessedColor = max(0.0, postProcessedColor);

// Encode gamma(scene / R), R = UI Paper White / Game Paper White. The native gamma HUD blends before stage 2,
// which restores R; Core then applies Game Paper White once to the combined frame.
const float uiPaperWhiteRelativeToGame = MELE_GetUIPaperWhiteRelativeToGame();
o0.xyz = linear_to_gamma(postProcessedColor / max(uiPaperWhiteRelativeToGame, 1e-4), GCT_MIRROR);

#if TM_HAS_GRAIN
// Native film grain stays in gamma after encoding, as vanilla did, scaled by the user control.
{
   float2 nuv = v0.zw * NoiseTextureOffset.xy + NoiseTextureOffset.zw;
   float n = NoiseTexture.Sample(NoiseTextureSampler_s, nuv).x;
   n = (n - 0.5) * FilmGrain_Scale * LumaSettings.GameSettings.FilmGrainIntensity;
   o0.xyz = o0.xyz + n;
}
#endif

o0.xyz = max(0.0, o0.xyz); // Grain may make shadows negative; retain HDR headroom above 1.

// linear_to_gamma is a pure pow, so dividing by R in linear equals dividing by gamma(R) in the encoded domain.
// Used by the metering below and by the native SDR clamp at the end of this tail.
const float pw_norm = linear_to_gamma1(uiPaperWhiteRelativeToGame, GCT_MIRROR);

// Native eye adaptation meters the final gamma scene after vignette, grain, and SDR clamp, HUD not yet present.
// Cancelling transport with gamma(x / R) * gamma(R) = gamma(x) keeps both Paper White controls out of exposure.
{
   float3 metered = saturate(o0.xyz * pw_norm);
   float adaptLuma = dot(metered, float3(0.212670997, 0.715160012, 0.0721689984));
   o1 = 0.25 * log2(adaptLuma * 15 + 1);
}

// Apply animated triangular dither in gamma for HDR and SDR.
if (LumaSettings.GameSettings.Dithering > 0.5)
   ApplyDithering(o0.xyz, v1.xy, true, 1.0, DITHERING_BIT_DEPTH, LumaSettings.FrameIndex, true);

#if TM_ALPHA_LUMA
o0.w = dot(o0.xyz, float3(0.298999995, 0.587000012, 0.114)); // Preserve native ME3 luma alpha, unclamped as native.
#else
o0.w = 0;
#endif

// Vanilla ended stage 1 with mov_sat into an 8-bit UNORM target, so the gamma HUD always composited against a
// value capped at white; the Luma intermediate is fp16, so the cap must be explicit. Applied last, after the
// alpha dot product, to keep vanilla's clamp position, and in the transport-normalized domain so neither Paper
// White control moves it. HDR keeps its headroom.
if (LumaSettings.DisplayMode != 1)
{
   o0.xyz = min(o0.xyz, 1.0 / max(pw_norm, 1e-4));
}
