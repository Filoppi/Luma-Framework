// Shared stage-1 output tail, included inside each main() after it defines gradedHDR, sdrGamma, v0, v1, o0, o1.
// The bodies already decode sdrGamma for gradedHDR, but this tail decodes it again on purpose: consuming that decode
// reschedules the vanilla curve on 0x2754F750 and 0x69F03340 in Publishing, turning r0*w + (1 - e) into
// (r0*w - e) + 1 at the same instruction count. Float32 addition is not associative and that line is native
// transcription, so the duplicate decode stays (measured). fxc still picks either form per permutation from unrelated
// code (the exposure metering moved some ME1LE/ME2LE ones): at most 1 ULP, and only a "precise" on the curve would pin
// it, which spreads to the whole expression.

// 0 none, 1 radial power, 3 ME3LE smoothstep.
#ifndef TM_VIGNETTE_TYPE
#define TM_VIGNETTE_TYPE 0
#endif

// fxc flags the early return of "MacLeodBoynton::SolvePurity()" as an uninitialized result (X4000). It comes at code
// generation, so it stays off for the whole shader; here, last, because "ColorGradingLUT.hlsl" turns it back on.
#pragma warning(disable : 4000)
#ifndef TM_HAS_GRAIN
#define TM_HAS_GRAIN 0
#endif
// Write the native ME3LE output luma to alpha.
#ifndef TM_ALPHA_LUMA
#define TM_ALPHA_LUMA 0
#endif

float3 sdrLinear = gamma_to_linear(sdrGamma, GCT_MIRROR);

// Native vignette, moved ahead of the tonemap and applied in linear so the display map absorbs its blue-tinted white
// point instead of the frame leaving stage 1 above Scene Peak. linear_to_gamma is a signed pure pow, so this is
// exactly the vanilla multiply in the encoded domain. Grain and dither stay in gamma below, where moving them would
// amplify shadow noise.
float3 vigLinear = 1.0;
float3 vigGamma = 1.0; // The native gamma-domain multiplier, for the exposure metering below.
#if TM_VIGNETTE_TYPE == 1
// The slider scales only radial darkening, so zero intensity does not alter the native white point tint.
{
   float2 vc = float2(-0.5, -0.5) + v0.zw;
   vc = float2(0.832050323, 0.554700196) * vc;
   float vd = max(9.99999975e-05, dot(vc, vc));
   vd = exp2(3.25 * log2(vd));
   vd = 1.0 - vd;
   vd = exp2(TM_VIG_POW * log2(max(vd, 9.99999975e-05)));
   const float3 whitePoint = TM_VIG_FLOOR + 1.0; // Floor plus center value.
   float3 vig = TM_VIG_FLOOR + vd;
   vig = whitePoint * lerp(1.0, vig / whitePoint, LumaSettings.GameSettings.VignetteIntensity);
   vigGamma = vig;
   vigLinear = gamma_to_linear(vig, GCT_MIRROR);
}
#elif TM_VIGNETTE_TYPE == 3
// Native ME3LE smoothstep vignette; scale only radial darkening, not its blue-tinted white point.
{
   float2 vc = v0.zw * ScreenUVScaleBias.xy + ScreenUVScaleBias.zw;
   vc = float2(-0.5, -0.5) + vc;
   vc = float2(0.832050323, 0.554700196) * vc;
   float vd = dot(vc, vc);
   vd = saturate(4.0 * (vd - 0.0500000007));
   float vs = (3.0 - 2.0 * vd) * vd * vd; // smoothstep(0, 1, vd).
   const float3 whitePoint = float3(1.01036298, 1.00000572, 1.16309249);
   float3 vig = whitePoint - vs * LumaSettings.GameSettings.VignetteIntensity;
   vigGamma = vig;
   vigLinear = gamma_to_linear(vig, GCT_MIRROR);
}
#endif

float3 postProcessedColor;
if (LumaSettings.DisplayMode == 1) // HDR
{
   // gradedHDR is Game Paper White relative linear BT.709. Its peak is Scene Peak over Game Paper White, a unit
   // conversion; Game Paper White itself is applied by "Output_0x0765601C" and "Video_0x7B5C59DF". Grain, dither and
   // RCAS can still push the result ~2% past Scene Peak, which we accept.
   postProcessedColor = gradedHDR * vigLinear;
   // Highlights Desaturation: only the whitening (MacLeod-Boynton chrominance) of a per-channel Reinhard (ceiling 5, shoulder
   // 1.5: identity below it), as MGR:R and Mirror's Edge; the hue stays the native grade's. Skipped below the shoulder.
   const float3 color2020 = BT709_To_BT2020(postProcessedColor);
   [branch] if (LumaSettings.GameSettings.HighlightDechroma > 0.0 && max3(color2020) > 1.5)
   {
      postProcessedColor = BT2020_To_BT709(MacLeodBoynton::HueAndPurityEmulationBT2020(color2020, Reinhard::ReinhardPiecewise(color2020, 5.0, 1.5),
                                                                                       0.0, LumaSettings.GameSettings.HighlightDechroma));
   }
   // Contrast (a power of relative luminance around mid gray) and a chrominance scale around relative luminance,
   // applied here as the rolloff doesn't take them.
   const float contrast = LumaSettings.GameSettings.Contrast;
   [branch] if (contrast != 1.0)
   {
      postProcessedColor *= pow(max(GetLuminance(postProcessedColor, CS_BT709), FLT_MIN) / MidGray, contrast - 1.0);
   }
   const float saturationScale = LumaSettings.GameSettings.Saturation;
   [branch] if (saturationScale != 1.0)
   {
      postProcessedColor = Saturation(postProcessedColor, saturationScale, CS_BT709);
   }
   // Max-channel C-infinity rolloff to the peak (MGR:R, Mirror's Edge), which keeps RGB ratios. The peak can't go below
   // paper white (the rolloff needs it above its 0.18 anchor).
   const float paperWhite = max(GamePaperWhiteNits, 1.0);
   postProcessedColor = Tonemap::CInfinityRollOff::MaxChannel(postProcessedColor, max(PeakWhiteNits, paperWhite) / paperWhite, 0.18, 1.0); // Game-Paper-White-relative.
}
else // SDR still uses the scRGB swapchain; sdrLinear is the exact native grade.
{
   // No tonemap sits between here and the encode, so the linear vignette equals the vanilla gamma multiply.
   postProcessedColor = sdrLinear * vigLinear;
}

postProcessedColor = IsNaN_Strict(postProcessedColor) ? 0.0 : postProcessedColor;
// Stage 1 owns the non-negative floor: no negative RGB may reach the gamma HUD or the swapchain, so wide-gamut
// (negative BT.709) components the hue transfer kept end here.
postProcessedColor = max(0.0, postProcessedColor);

// Encode gamma(scene / R), R = UI Paper White / Game Paper White. The native gamma HUD blends before stage 2
// (Output_0x0765601C), which restores R and applies Game Paper White once to the combined frame.
const float uiPaperWhiteRelativeToGame = MELE_GetUIPaperWhiteRelativeToGame();
o0.xyz = linear_to_gamma(postProcessedColor / max(uiPaperWhiteRelativeToGame, 1e-4), GCT_MIRROR);

float grain = 0.0;
#if TM_HAS_GRAIN
// Native film grain stays in gamma after encoding, as vanilla did, scaled by the user control.
{
   float2 nuv = v0.zw * NoiseTextureOffset.xy + NoiseTextureOffset.zw;
   float n = NoiseTexture.Sample(NoiseTextureSampler_s, nuv).x;
   grain = (n - 0.5) * FilmGrain_Scale * LumaSettings.GameSettings.FilmGrainIntensity;
   o0.xyz = o0.xyz + grain;
}
#endif

o0.xyz = max(0.0, o0.xyz); // Grain may make shadows negative; retain HDR headroom above 1.

// linear_to_gamma is a pure pow, so dividing by R in linear equals dividing by gamma(R) in the encoded domain.
// Used by the native SDR clamp at the end of this tail.
const float rEncoded = linear_to_gamma1(uiPaperWhiteRelativeToGame, GCT_MIRROR);

// Native eye adaptation meters the vanilla final gamma scene: the native SDR grade times the gamma vignette, plus
// grain, clamped as vanilla's mov_sat did, HUD not yet present. Built from the native result, not from o0, so the HDR
// reconstruction, the display map and both Paper White controls stay out of exposure in every Display Mode.
{
   float3 metered = saturate(sdrGamma * vigGamma + grain);
   float adaptLuma = dot(metered, float3(0.212670997, 0.715160012, 0.0721689984));
   o1 = 0.25 * log2(adaptLuma * 15 + 1);
}

// Anti-banding dither, one step of the output quantizer: 8-bit in SDR, 10-bit BT.2020 PQ in HDR and SDR on HDR.
if (LumaSettings.GameSettings.Dithering > 0.5)
{
   ApplyOutputDithering(o0.xyz, v1.xy, true, LumaSettings.DisplayMode != 0, LumaSettings.UIPaperWhiteNits);
}

#if TM_ALPHA_LUMA
o0.w = dot(o0.xyz, float3(0.298999995, 0.587000012, 0.114)); // Preserve native ME3LE luma alpha, unclamped as native.
#else
o0.w = 0;
#endif

// Vanilla ended stage 1 with mov_sat into an 8-bit UNORM target, so the gamma HUD always composited against a
// value capped at white; our intermediate is fp16, so the cap must be explicit. It's applied last, after the alpha
// dot product, to keep vanilla's clamp position, and in the transport-normalized domain so neither Paper White
// control moves it. HDR keeps its headroom.
if (LumaSettings.DisplayMode != 1)
{
   o0.xyz = min(o0.xyz, 1.0 / max(rEncoded, 1e-4));
}
