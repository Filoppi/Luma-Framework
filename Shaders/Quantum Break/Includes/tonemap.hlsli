#include "./quantum_break_common.hlsli"

//-----TONEMAPPING-----//
float ReinhardExtended(float x, float clip = 1000.f / 203.f, float peak = 1.f, float minimum = 0.f)
{
   // float increase = (peak - minimum) / (clip * (clip + minimum));
   // float scaler = (1.f + (x * increase));
   // return Reinhard(x, peak, minimum) * scaler;

   // Micro-optimized version below:

   // y = (x + m) / (x/p + 1) * (1 + x(p - m) / c(c + m))
   // y = p(x + m) / (x + p) * (1 + x(p - m) / c(c + m))
   // y = p(x + m) * (1 + x(p - m) / c(c + m)) / (x + p)
   // y = p(x + m) * (c(c + m) + x(p - m)) / ((x + p) * c(c + m))
   // y = p(x + m) * (c(c + m) + x(p - m)) / ((x + p) * c(c + m))

   // float x_plus_m = x + minimum;                 // (x + m)
   // float c_plus_m = clip + minimum;              // (c + m)
   // float cc_plus_cm = clip * c_plus_m;           // c(c + m)
   // float p_minus_m = peak - minimum;             // (p - m)
   // float num_a = mad(x, p_minus_m, cc_plus_cm);  // c(c+m) + x(p-m)
   // float px_plus_pm = peak * x_plus_m;           // p * (x + m)
   // float num = px_plus_pm * num_a;               // p * (x + m) * (c(c+m) + x(p-m))
   // float x_plus_p = x + peak;                    // (x + p)
   // float den = x_plus_p * cc_plus_cm;            // (x + p) * c(c+m)
   // return num / den;

   // Faster if using outside m, though slightly different

   // y = q(x + 0) * (c(c + 0) + x(q - 0)) / ((x + q) * c(c + 0)) + m
   // y = q(x) * (c(c + 0) + x(q - 0)) / ((x + q) * c(c + 0)) + m
   // y = qx * (cc + xq) / ((x + q) * cc) + m
   // y = xq(cc + xq) / cc(x + q) + m

   float q = peak - minimum;             // q = p - m
   float xq = x * q;                     // x * q
   float cc = clip * clip;               // c * c
   float cc_plus_xq = cc + xq;           // cc + xq
   float num0 = xq * cc_plus_xq;         // xq(cc + xq)
   float x_plus_q = x + q;               // x + q
   float den0 = cc * x_plus_q;           // cc(x + q)
   float result = num0 / den0 + minimum; // (xqcc + xqxq) / (ccx + ccq) + m
   return result;
}

float ComputeReinhardScale(float peak = 1.f, float minimum = 0.f, float gray_in = 0.18f, float gray_out = 0.18f)
{
   //  s = (p * y - p * m) / (x * p - x * y)

   float num = peak * (gray_out - minimum); // p * (y - m)
   float den = gray_in * (peak - gray_out); // x * (p - y)

   return num / den;
}

float ReinhardExtendedDerivative(float x, float white_max = 1000.f / 203.f, float peak = 1.f)
{
   float p = peak, w = white_max;
   float w2 = w * w;

   // numerator: p^2 * (x(2p + x) + w^2)
   float num = p * p * (x * (2.f * p + x) + w2);

   // denominator: w^2 * (p + x)^2
   float px = p + x;
   float den = w2 * px * px;

   return num / den;
}

// Cube root that works for negative values too
float Cbrt(float v)
{
   float a = abs(v);
   if (a == 0.0f)
      return 0.0f;
   return sign(v) * pow(a, 1.0f / 3.0f);
}

// Solve f'(x) = x for the ReinhardExtended luminance curve:
// f(x)  = (p * x / (p + x)) * (1 + (p * x) / (w * w))
// f'(x) = p^2 * (w^2 + x * (2p + x)) / (w^2 * (p + x)^2)
// Returns the Cardano analytic pivot x(p, w).
float ReinhardExtendedFindPivot(float peak, float white_max)
{
   float p = peak;
   float w = white_max;
   float p2 = p * p;
   float p3 = p2 * p;
   float w2 = w * w;

   // Cubic: a x^3 + b x^2 + c x + d = 0
   float a = w2;
   float b = 2.0f * p * w2 - p2;
   float c = p2 * w2 - 2.0f * p3;
   float d = -p2 * w2;

   // Normalize: x^3 + A x^2 + B x + C = 0
   float A = b / a;
   float B = c / a;
   float C = d / a;

   // Depressed cubic: t^3 + p_c t + q_c = 0 via x = t - A/3
   float A2 = A * A;
   float A3 = A2 * A;

   float p_c = B - (A2 / 3.0f);
   float q_c = 2.0f * A3 / 27.0f - A * B / 3.0f + C;

   float half_q = 0.5f * q_c;
   float disc = half_q * half_q + (p_c * p_c * p_c) / 27.0f;

   // Clamp tiny negative due to FP error
   disc = max(disc, 0.0f);

   float sqrt_disc = sqrt(disc);

   float u = -half_q + sqrt_disc;
   float v = -half_q - sqrt_disc;

   float u_c = Cbrt(u);
   float v_c = Cbrt(v);

   float t = u_c + v_c;

   // Back-substitute: x = t - A / 3
   float x = t - A / 3.0f;
   return x;
}

#define APPLY_REINHARD_EXTENDED_PLUS_GENERATOR(T)                              \
   T ApplyReinhardExtendedPlus(T x, T base, float white_max, float peak = 1.f) \
   {                                                                           \
      float pivot_x = ReinhardExtendedFindPivot(peak, white_max);              \
                                                                               \
      float pivot_y = ReinhardExtended(pivot_x, white_max, peak);              \
                                                                               \
      float slope = ReinhardExtendedDerivative(pivot_x, white_max, peak);      \
                                                                               \
      /* Line passing through (pivot_x, pivot_y) with matching slope */        \
      T offset = pivot_y - slope * pivot_x;                                    \
      T extended = slope * x + offset;                                         \
                                                                               \
      return (x >= pivot_x) ? extended : base;                                 \
   }

APPLY_REINHARD_EXTENDED_PLUS_GENERATOR(float)
APPLY_REINHARD_EXTENDED_PLUS_GENERATOR(float3)
#undef APPLY_REINHARD_EXTENDED_PLUS_GENERATOR

float ReinhardPiecewise(float x, float x_max = 1.f, float shoulder = 0.18f)
{
   const float x_min = 0.f;
   float exposure = ComputeReinhardScale(x_max, x_min, shoulder, shoulder);
   float tonemapped = mad(x, exposure, x_min) / mad(x, exposure / x_max, 1.f - x_min);

   return lerp(x, tonemapped, step(shoulder, x));
}

float3 ApplyVanillaToneMap(float3 color, float white_clip = 8.f)
{
   float luma = dot(color, float3(0.27, 0.67, 0.06));
   float tonemapped_luma = ReinhardExtended(luma, white_clip, 1.f, 0.f);
   float scale = (luma > 0.0f) ? (tonemapped_luma / luma) : 1.0f;

   return color * scale;
}

float3 ApplyVanillaToneMapExtended(float3 color, float white_clip = 8.f)
{
   float luma = GetLuminance(color);
   float base_luma = ReinhardExtended(luma, white_clip, 1.f, 0.f);
   float sdr_scale = (luma > 0.0f) ? (base_luma / luma) : 1.0f;
   float3 sdr_tonemapped = saturate(color * sdr_scale);

   float extended_lum = ApplyReinhardExtendedPlus(luma, base_luma, white_clip, 1.f);
   float hdr_scale = (luma > 0.0f) ? (extended_lum / luma) : 1.0f;
   float3 hdr_tonemapped = color * hdr_scale;
#if 0
   hdr_tonemapped = RestoreHueAndChrominance(hdr_tonemapped, sdr_tonemapped, 0.f, 0.25f); // needed to blow out blues
#endif
   return hdr_tonemapped;
}

float3 ApplyToneMap(float3 color, float white_clip = 8.f)
{
   if (TONE_MAP_TYPE != 0.f)
   {
      return ApplyVanillaToneMapExtended(color);
   }
   else
   {
      return saturate(ApplyVanillaToneMap(color, white_clip));
   }
}

float ComputeReinhardSmoothClampScale(float3 untonemapped, float rolloff_start = 0.18f, float output_max = 1.f)
{
   float peak = max3(untonemapped.r, untonemapped.g, untonemapped.b);

   float mapped_peak = ReinhardPiecewise(peak, output_max, rolloff_start);

   float scale = safeDivision(mapped_peak, peak, 1);

   return scale;
}

float ComputeMaxChCompressionScale(float3 color)
{
   if (TONE_MAP_TYPE == 0.f)
   {
      return 1.f;
   }
   else
   {
      return ComputeReinhardSmoothClampScale(color, 0.470719f);
   }
}

float3 Sample2DLUT(float3 input_color, Texture2D<float4> lut, SamplerState lut_sampler)
{
   // 32^3 LUT packed into a 2D strip of 32 slices across X.
   const float lut_size = 32.0f;
   const float inv_lut_size = 1.0f / lut_size; // 0.03125

   const float half_texel_x = 1.0f / 2048.0f;    // 0.00048828125
   const float min_v = 1.0f / 64.0f;             // 0.015625
   const float max_v = 63.0f / 64.0f;            // 0.984375
   const float max_u_in_tile = 63.0f / 2048.0f;  // 0.03076171875
   const float max_slice_offset = 31.0f / 32.0f; // 0.96875

   // Map green to LUT V with the same bias/clamp behavior.
   float v = clamp(input_color.y + min_v, min_v, max_v);

   // Map red to U within a single slice.
   float u_in_tile = input_color.x * inv_lut_size + half_texel_x;
   u_in_tile = clamp(u_in_tile, half_texel_x, max_u_in_tile);

   // Blue selects the LUT slice, with linear interpolation between floor/ceil slices.
   float slice_pos = lut_size * input_color.z;
   float slice_t = frac(slice_pos);

   float slice_ceil_offset = ceil(slice_pos) * inv_lut_size;
   slice_ceil_offset = clamp(slice_ceil_offset, 0.0f, max_slice_offset);

   float slice_floor_offset = floor(slice_pos) * inv_lut_size;
   slice_floor_offset = clamp(slice_floor_offset, 0.0f, max_slice_offset);

   float2 uv_ceil = float2(u_in_tile + slice_ceil_offset, v);
   float3 color_ceil = lut.Sample(lut_sampler, uv_ceil).xyz;

   float2 uv_floor = float2(u_in_tile + slice_floor_offset, v);
   float3 color_floor = lut.Sample(lut_sampler, uv_floor).xyz;

   return lerp(color_floor, color_ceil, slice_t);
}

float3 Unclamp(float3 original_gamma, float3 black_gamma, float3 mid_gray_gamma, float3 neutral_gamma)
{
   const float3 added_gamma = black_gamma;

   const float mid_gray_average = (mid_gray_gamma.r + mid_gray_gamma.g + mid_gray_gamma.b) / 3.f;

   // Remove from 0 to mid-gray
   const float shadow_length = mid_gray_average;
   const float shadow_stop = max(neutral_gamma.r, max(neutral_gamma.g, neutral_gamma.b));
   const float3 floor_remove = added_gamma * max(0, shadow_length - shadow_stop) / shadow_length;

   const float3 unclamped_gamma = max(0, original_gamma - floor_remove);
   return unclamped_gamma;
}

float3 ApplyColorGradingLUT(float3 color_input, Texture2D<float4> lut, SamplerState lut_sampler)
{
   float3 color_input_encoded = linear_to_sRGB_gamma(color_input);
   float3 color_output_encoded = Sample2DLUT(color_input_encoded, lut, lut_sampler);

   if (CUSTOM_LUT_SCALING > 0.f)
   {
      float3 lut_black_encoded = Sample2DLUT(0.f, lut, lut_sampler);

      float lut_black_y = GetLuminance(gamma_sRGB_to_linear(lut_black_encoded));
      if (lut_black_y > 0.f)
      {
         float3 lut_mid_encoded = Sample2DLUT(max(lut_black_encoded, linear_to_sRGB_gamma(0.01f)), lut, lut_sampler);

         float3 unclamped_gamma =
             Unclamp(color_output_encoded, lut_black_encoded, lut_mid_encoded, color_input_encoded);

         float3 unclamped_linear = gamma_sRGB_to_linear(unclamped_gamma);

         float3 color_output_linear = gamma_sRGB_to_linear(color_output_encoded);
         color_output_linear *=
             lerp(1.f, safeDivision(GetLuminance(unclamped_linear), GetLuminance(color_output_linear), 1), CUSTOM_LUT_SCALING);

         color_output_encoded = linear_to_sRGB_gamma(color_output_linear);
      }
   }

   return color_output_encoded;
}

float3 SRGBEncodeAndSample2DLUT(float3 input, Texture2D<float4> g_sBaseColorCorrectionMap,
                                SamplerState g_sBaseColorCorrectionMap_s)
{
   float4 r0;
   float3 r1, r2;

   float scale = ComputeMaxChCompressionScale(input);

   float3 input_scaled = input * scale;

   r0.rgb = ApplyColorGradingLUT(input_scaled, g_sBaseColorCorrectionMap, g_sBaseColorCorrectionMap_s);

   r0.rgb = gamma_sRGB_to_linear(r0.rgb);
   r0.rgb /= scale;
   r0.rgb = lerp(input, r0.rgb, CUSTOM_LUT_STRENGTH);
   r0.rgb = linear_to_sRGB_gamma(r0.rgb);

   return r0.rgb;
}
