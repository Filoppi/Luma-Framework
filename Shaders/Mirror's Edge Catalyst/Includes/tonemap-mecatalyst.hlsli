#define LUT_3D 1

#include "../../Includes/ColorGradingLUT.hlsl"
#include "../../Includes/Tonemap.hlsl"
#include "./CBuffer_Globals.hlsli"

float Reinhard(float x, float peak = 1.f)
{
   return x / (x / peak + 1.f);
}

float ReinhardExtended(float color, float white_max = 1000.f / 203.f, float peak = 1.f)
{
   return Reinhard(color, peak) * (1.f + (peak * color) / (white_max * white_max));
}

float ComputeReinhardExtendableScale(float w = 100.f, float p = 1.f, float m = 0.f, float x = 0.18f, float y = 0.18f)
{
   // y = (sx / (sx/p + 1) * (1 + (psx)/(sw*sw))
   // solve for s (scale)
   // Min not currently supported
   return p * (w * w * y - (p * x * x)) / (w * w * x * (p - y));
}

float ReinhardPiecewiseExtended(float x, float white_max, float x_max = 1.f, float shoulder = 0.18f)
{
   const float x_min = 0.f;
   float exposure = ComputeReinhardExtendableScale(white_max, x_max, x_min, shoulder, shoulder);
   float extended = ReinhardExtended(x * exposure, white_max * exposure, x_max);
   extended = min(extended, x_max);

   return lerp(x, extended, step(shoulder, x));
}

float ComputeReinhardSmoothClampScale(float3 untonemapped, float rolloff_start = 0.5f, float output_max = 1.f,
                                      float white_clip = 100.f)
{
   float peak = max3(untonemapped.r, untonemapped.g, untonemapped.b);
   float mapped_peak = ReinhardPiecewiseExtended(peak, white_clip, output_max, rolloff_start);
   float scale = safeDivision(mapped_peak, peak, 0);

   return scale;
}

/// Piecewise linear + exponential compression to a target value starting from a specified number.
/// https://www.ea.com/frostbite/news/high-dynamic-range-color-grading-and-display-in-frostbite
#define EXPONENTIALROLLOFF_GENERATOR(T)                                                                                \
   T ExponentialRollOff(T input, float rolloff_start = 0.20f, float output_max = 1.0f)                                 \
   {                                                                                                                   \
      T rolloff_size = output_max - rolloff_start;                                                                     \
      T overage = -max((T)0, input - rolloff_start);                                                                   \
      T rolloff_value = (T)1.0f - exp(overage / rolloff_size);                                                         \
      T new_overage = mad(rolloff_size, rolloff_value, overage);                                                       \
      return input + new_overage;                                                                                      \
   }

EXPONENTIALROLLOFF_GENERATOR(float)
EXPONENTIALROLLOFF_GENERATOR(float3)
#undef EXPONENTIALROLLOFF_GENERATOR

float3 ApplyDisplayMapAndScaleMirrorsEdge(float3 undisplaymapped)
{
   const float shoulder_start = 0.4f;
   undisplaymapped = gamma_to_linear(undisplaymapped, GCT_MIRROR);

   undisplaymapped = BT709_To_BT2020(undisplaymapped);
   undisplaymapped = max(0, undisplaymapped);
   float3 displaymapped = exp2(ExponentialRollOff(log2(undisplaymapped * GamePaperWhiteNits),
                                                  log2(PeakWhiteNits * shoulder_start), log2(PeakWhiteNits))) /
                          GamePaperWhiteNits;
   displaymapped = BT2020_To_BT709(displaymapped);

   // displaymapped = ApplyScale(displaymapped);

   displaymapped = linear_to_gamma(displaymapped, GCT_MIRROR);
   return displaymapped;
}

void PrecomputeTonemapParams(out float4 r1, out float r0_w)
{
   float4 r2;
   r1.xyz = tonemapCoeffA.xzx / tonemapCoeffA.ywy;
   r1.xyz = r1.xyz * float3(-0.2f, 0.23f, 0.18f) + float3(0.57f, 0.01f, 0.02f);
   r0_w = r1.y * r1.x;
   r1.y = tonemapCoeffB.z * 0.2f + r0_w;
   r1.zw = float2(0.02f, 0.3f) * r1.zz;
   r1.y = tonemapCoeffB.z * r1.y + r1.z;
   r2.x = tonemapCoeffB.z * 0.2f + r1.x;
   r2.x = tonemapCoeffB.z * r2.x + r1.w;
   r1.y = r1.y / r2.x;
   r1.y = (-2.f / 30.f) + r1.y;
   r1.y = 1.f / r1.y;
}

#define APPLY_TONEMAP_CURVE_GENERATOR(T)                                                                               \
   T ApplyTonemapCurve(T color, float4 r1, float r0_w)                                                                 \
   {                                                                                                                   \
      T mapped = r1.y * color;                                                                                         \
      T r2 = mapped * 0.2f + r0_w;                                                                                     \
      r2 = mapped * r2 + r1.z;                                                                                         \
      T r3 = mapped * 0.2f + r1.x;                                                                                     \
      mapped = mapped * r3 + r1.w;                                                                                     \
      mapped = r2 / mapped;                                                                                            \
      mapped = (-2.f / 30.f) + mapped;                                                                                 \
      mapped = mapped * r1.y;                                                                                          \
      mapped = mapped / tonemapCoeffB.w;                                                                               \
      return mapped;                                                                                                   \
   }

APPLY_TONEMAP_CURVE_GENERATOR(float)
APPLY_TONEMAP_CURVE_GENERATOR(float3)
APPLY_TONEMAP_CURVE_GENERATOR(float4)
#undef APPLY_TONEMAP_CURVE_GENERATOR

// Inverse of ApplyTonemapCurve
// Solves for original color x given mapped (output) using the quadratic
// relationship produced inside ApplyTonemapCurve. The derivation yields
// a quadratic in m = r1.y * x:  A*m^2 + B*m + C = 0. We solve per-component
// and pick a numerically robust root (with linear fallback when A ~= 0).
#define INVERT_TONEMAP_CURVE_GENERATOR(T)                                                                              \
   T InvertTonemapCurve(T mapped, float4 r1, float r0_w)                                                               \
   {                                                                                                                   \
      const float a = 0.2f;                                                                                            \
      /* reverse final ops: y_final = ((num/den) + (-2/30)) * r1.y / tonemapCoeffB.w */                                \
      T s = mapped * tonemapCoeffB.w / r1.y + (2.f / 30.f);                                                            \
      T A = (T)a * (s - (T)1);                                                                                         \
      T B = s * r1.x - r0_w;                                                                                           \
      T C = s * r1.w - r1.z;                                                                                           \
      T absA = abs(A);                                                                                                 \
      T eps = (T)1e-6;                                                                                                 \
      T disc = B * B - (T)4.0 * A * C;                                                                                 \
      disc = max(disc, (T)0);                                                                                          \
      T sqrtD = sqrt(disc);                                                                                            \
      T denom = (T)2.0 * A;                                                                                            \
      /* quadratic roots */                                                                                            \
      T m1 = (-B + sqrtD) / denom;                                                                                     \
      T m2 = (-B - sqrtD) / denom;                                                                                     \
      /* choose the larger root (tends to pick non-negative / physical solution) */                                    \
      T m_quad = max(m1, m2);                                                                                          \
      /* linear fallback when A ~= 0: B*m + C = 0 => m = -C/B */                                                       \
      T m_linear = -C / (B + (T)1e-12);                                                                                \
      T isLinear = step(absA, eps); /* 1 when absA <= eps */                                                           \
      T m = lerp(m_quad, m_linear, isLinear);                                                                          \
      /* safety clamp to avoid negative m causing issues; pick 0 as fallback */                                        \
      m = max(m, (T)0);                                                                                                \
      T x = m / r1.y;                                                                                                  \
      return x;                                                                                                        \
   }

INVERT_TONEMAP_CURVE_GENERATOR(float)
INVERT_TONEMAP_CURVE_GENERATOR(float3)
INVERT_TONEMAP_CURVE_GENERATOR(float4)
#undef INVERT_TONEMAP_CURVE_GENERATOR

float3 ChrominanceJzazbz(float3 incorrect_color, float3 reference_color, float strength = 1.f,
                         float clamp_chrominance_loss = 0.f)
{
   if (strength == 0.f)
      return incorrect_color;

   float3 incorrect_lab = JzAzBz::rgbToJzazbz(incorrect_color);
   float3 reference_lab = JzAzBz::rgbToJzazbz(reference_color);

   float2 incorrect_ab = incorrect_lab.yz;
   float2 reference_ab = reference_lab.yz;

   // Compute chrominance (magnitude of the a–b vector)
   float incorrect_chrominance = length(incorrect_ab);
   float correct_chrominance = length(reference_ab);

   // Scale original chrominance vector toward target chrominance
   float chrominance_ratio = safeDivision(correct_chrominance, incorrect_chrominance, 1);
   float scale = lerp(1.f, chrominance_ratio, strength);

   float t = 1.f - step(1.f, scale); // t = 1 when scale < 1, 0 when scale >= 1
   scale = lerp(scale, 1.f, t * clamp_chrominance_loss);

   incorrect_lab.yz = incorrect_ab * scale;

   float3 result = JzAzBz::jzazbzToRgb(incorrect_lab);
   return result;
}

float3 ApplyTonemapMirrorsEdge(float3 untonemapped)
{
   float4 r1;
   float r0_w;
   PrecomputeTonemapParams(r1, r0_w);

   // tonemap by luminance
   float lum_in = GetLuminance(untonemapped);
   float lum_out = ApplyTonemapCurve(lum_in, r1, r0_w);
   float3 luminanceTM = untonemapped * ((lum_in > 0) ? (lum_out / lum_in) : 0.f);

   // create adjusted untonemapped
   float midgray_out = InvertTonemapCurve(0.18, r1, r0_w);
   float3 untonemappedMidGrayCorrected = untonemapped * 0.18 / midgray_out;

   // lerp tonemapped and untonemapped
   float3 combinedTM = lerp(luminanceTM, untonemappedMidGrayCorrected, saturate(GetLuminance(luminanceTM)));

   // restore per channel chrominance
   float3 channelTM = ApplyTonemapCurve(untonemapped, r1, r0_w);
   combinedTM = ChrominanceJzazbz(combinedTM, channelTM, 1.f, 0.75f);

   // combinedTM = saturate(channelTM);

#if 0 // test tonemap invert
   combinedTM = ApplyTonemapCurve(combinedTM, r1, r0_w);
   combinedTM = InvertTonemapCurve(combinedTM, r1, r0_w);
#endif

   return combinedTM;
}

float3 SampleLUT32SRGBInSRGBOut(float3 linearColor, Texture3D<float4> colorGradingTexture,
                                SamplerState colorGradingTextureSampler_s)
{

   LUTExtrapolationData lutData = DefaultLUTExtrapolationData();
   LUTExtrapolationSettings lutSettings = DefaultLUTExtrapolationSettings();
   lutSettings.inputLinear = false;
   lutSettings.lutInputLinear = false;
   lutSettings.lutOutputLinear = false;
   lutSettings.outputLinear = false;
   lutSettings.transferFunctionIn = LUT_EXTRAPOLATION_TRANSFER_FUNCTION_SRGB;
   lutSettings.transferFunctionOut = LUT_EXTRAPOLATION_TRANSFER_FUNCTION_SRGB;
   lutSettings.neutralLUTRestorationAmount = 0.0;
   lutSettings.vanillaLUTRestorationAmount = 0.0;
   lutSettings.extrapolationQuality = 1;
   lutSettings.backwardsAmount = 0.25;
   lutSettings.whiteLevelNits = Rec709_WhiteLevelNits;
   lutSettings.inputTonemapToPeakWhiteNits = 0;
   lutSettings.clampedLUTRestorationAmount = 0;
   lutSettings.fixExtrapolationInvalidColors = true;
   lutSettings.lutSize = 32u;
   lutSettings.samplingQuality = 2u;

#if 0
   float3 srgbColor = linear_to_sRGB_gamma(linearColor, GCT_MIRROR);
   lutData.inputColor = srgbColor;
   lutSettings.enableExtrapolation = true;
   float3 lutOutputColor = SampleLUTWithExtrapolation(colorGradingTexture, colorGradingTextureSampler_s, lutData, lutSettings);
#else
   lutSettings.enableExtrapolation = false;

   // apply max channel tonemap
   linearColor = max(0, linearColor);

   float scale = ComputeReinhardSmoothClampScale(linearColor, 0.675f, 1.f, 100.f);
   linearColor *= scale;

   float3 srgbColor = linear_to_sRGB_gamma(linearColor, GCT_NONE);
   
   lutData.inputColor = srgbColor;
   float3 lutOutputColor = SampleLUTWithExtrapolation(
       colorGradingTexture, colorGradingTextureSampler_s, lutData,
       lutSettings); //    float3 lutOutputColor = colorGradingTexture.Sample(colorGradingTextureSampler_s,
                     //    saturate(srgbColor) * 0.96875 + 0.015625).xyz;

   lutOutputColor = gamma_sRGB_to_linear(lutOutputColor, GCT_MIRROR);

   lutOutputColor = lerp(srgbColor, lutOutputColor, 1.f);

   // invert max channel tonemap
   lutOutputColor /= scale;
   // lutOutputColor = RestoreHueAndChrominance(lutOutputColor / scale, lutOutputColor, 1.f, 1.f, 1.f);
   
   lutOutputColor = linear_to_sRGB_gamma(lutOutputColor, GCT_MIRROR);
#endif

   return lutOutputColor;
}

float3 ApplyRunnersVision(float3 srgbColor, Texture2D<float4> runnersVisionAlphaMaskTexture,
                          SamplerState runnersVisionAlphaMaskTextureSampler_s, float2 v2)
{

   float4 r0, r1;
   float3 r2;

   r0.rgb = (srgbColor);

#if 0 // BT.2020 HDR runners vision

#if 0 // apply max channel tonemap to input
   r0.rgb = gamma_sRGB_to_linear(r0.rgb, GCT_MIRROR);
   r0.rgb = max(0, (r0.rgb));
   float scale = ComputeReinhardSmoothClampScale(r0.rgb, 0.75f, 1.f, 100.f);
   r0.rgb = r0.rgb * scale;
   r0.rgb = linear_to_sRGB_gamma(r0.rgb, GCT_MIRROR);
   r0.rgb = saturate(r0.rgb);
#endif

   float3 inputColor = linear_to_sRGB_gamma(BT709_To_BT2020(gamma_sRGB_to_linear(r0.rgb, GCT_MIRROR)), GCT_MIRROR);
   r0.rgb = saturate(inputColor);
   float3 rvColor = linear_to_sRGB_gamma(BT709_To_BT2020(gamma_sRGB_to_linear(runnersVisionColor.rgb, GCT_MIRROR)), GCT_MIRROR);

   r1.xyz = 1 - r0.xyz; //   r1.xyz = float3(1, 1, 1) + -r0.xyz;
   r1.xyz = r1.xyz * 0.4 + r0.xyz;
   r2.xyz = rvColor + -r1.xyz;
   r1.xyz = preBlendAmount * r2.xyz + r1.xyz;
   r1.xyz = 1 - r1.xyz; //    r1.xyz = float3(1, 1, 1) + -r1.xyz;
   r1.xyz = r1.xyz / rvColor;
   r1.xyz = 1 - r1.xyz; //    r1.xyz = float3(1, 1, 1) + -r1.xyz;
   r1.xyz = rvColor * postAddAmount + r1.xyz;
   r1.yzw = rvColor * 0.4 + r1.xyz;
   r0.w = runnersVisionAlphaMaskTexture.Sample(runnersVisionAlphaMaskTextureSampler_s, v2.xy).x;
   r1.x = max(r0.w, r1.y);

   r0.xyz = lerp(inputColor, r1.xzw, r0.w);
   r0.xyz = max(0, r0.xyz);

   r0.rgb = gamma_sRGB_to_linear(r0.rgb, GCT_MIRROR);
   r0.rgb = BT2020_To_BT709(r0.rgb);
   r0.rgb = linear_to_sRGB_gamma(r0.rgb, GCT_MIRROR);

#if 0 // inverse max channel tonemap on output
   r0.rgb = saturate(r0.rgb);
   r0.rgb = gamma_sRGB_to_linear(r0.rgb, GCT_MIRROR);
   r0.rgb = BT709_To_BT2020(r0.rgb);
   r0.rgb = max(0, r0.rgb);
   r0.rgb = BT2020_To_BT709(r0.rgb);
   r0.rgb /= scale;
   r0.rgb = linear_to_sRGB_gamma(r0.rgb, GCT_MIRROR);
#endif

#else // BT.709 HDR runners vision

#if 0 // apply max channel tonemap to input
   r0.rgb = gamma_sRGB_to_linear(r0.rgb, GCT_MIRROR);
   r0.rgb = max(0, (r0.rgb));
   float scale = ComputeReinhardSmoothClampScale(r0.rgb, 0.75f, 1.f, 100.f);
   r0.rgb = r0.rgb * scale;
   r0.rgb = linear_to_sRGB_gamma(r0.rgb, GCT_MIRROR);
   r0.rgb = saturate(r0.rgb);
#endif

   float3 inputColor = r0.rgb;
   r0.rgb *= ComputeReinhardSmoothClampScale(r0.rgb, 0.75f, 1.f, 100.f);

   float3 rvColor = runnersVisionColor.rgb;

   r1.xyz = 1 - r0.xyz; //   r1.xyz = float3(1, 1, 1) + -r0.xyz;
   r1.xyz = r1.xyz * 0.4 + r0.xyz;
   r2.xyz = rvColor + -r1.xyz;
   r1.xyz = preBlendAmount * r2.xyz + r1.xyz;
   r1.xyz = 1 - r1.xyz; //    r1.xyz = float3(1, 1, 1) + -r1.xyz;
   r1.xyz = r1.xyz / rvColor;
   r1.xyz = 1 - r1.xyz; //    r1.xyz = float3(1, 1, 1) + -r1.xyz;
   r1.xyz = rvColor * postAddAmount + r1.xyz;
   r1.yzw = rvColor * 0.4 + r1.xyz;
   r0.w = runnersVisionAlphaMaskTexture.Sample(runnersVisionAlphaMaskTextureSampler_s, v2.xy).x;
   r1.x = max(r0.w, r1.y);

   r0.xyz = lerp(inputColor, r1.xzw, r0.w);
   r0.xyz = max(0, r0.xyz);

   r0.rgb = gamma_sRGB_to_linear(r0.rgb, GCT_MIRROR);
   r0.rgb = BT709_To_BT2020(r0.rgb);
   r0.rgb = max(0, r0.rgb);
   r0.rgb = BT2020_To_BT709(r0.rgb);
   r0.rgb = linear_to_sRGB_gamma(r0.rgb, GCT_MIRROR);

#if 0 // inverse max channel tonemap on output
   r0.rgb = saturate(r0.rgb);
   r0.rgb = gamma_sRGB_to_linear(r0.rgb, GCT_MIRROR);
   r0.rgb = BT709_To_BT2020(r0.rgb);
   r0.rgb = max(0, r0.rgb);
   r0.rgb = BT2020_To_BT709(r0.rgb);
   r0.rgb /= scale;
   r0.rgb = linear_to_sRGB_gamma(r0.rgb, GCT_MIRROR);
#endif

#endif // BT.709 HDR runners vision

   float3 finalColor = r0.rgb;

   return finalColor;
}
