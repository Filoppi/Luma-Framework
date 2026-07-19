//https://github.com/clshortfuse/renodx/blob/main/src/shaders/tonemap/hermite_spline.hlsl
#include "../Includes/Color.hlsl"

namespace pq {
  static const float M1 = 2610.f / 16384.f;           // 0.1593017578125f;
  static const float M2 = 128.f * (2523.f / 4096.f);  // 78.84375f;
  static const float C1 = 3424.f / 4096.f;            // 0.8359375f;
  static const float C2 = 32.f * (2413.f / 4096.f);   // 18.8515625f;
  static const float C3 = 32.f * (2392.f / 4096.f);   // 18.6875f;

  float Encode(float color, float scaling = 10000.f) {
    color *= (scaling / 10000.f);
    float y_m1 = pow(color, M1);
    return pow((C1 + C2 * y_m1) / (1.f + C3 * y_m1), M2);
  }

  float3 Encode(float3 color, float scaling = 10000.f) {
    color *= (scaling / 10000.f);
    float3 y_m1 = pow(color, M1);
    return pow((C1 + C2 * y_m1) / (1.f + C3 * y_m1), M2);
  }

  float Decode(float color, float scaling = 10000.f) {
    float e_m12 = pow(color, 1.f / M2);
    float out_color = pow(max(0, e_m12 - C1) / (C2 - C3 * e_m12), 1.f / M1);
    return out_color * (10000.f / scaling);
  }

  float3 Decode(float3 color, float scaling = 10000.f) {
    float3 e_m12 = pow(color, 1.f / M2);
    float3 out_color = pow(max(0, e_m12 - C1) / (C2 - C3 * e_m12), 1.f / M1);
    return out_color * (10000.f / scaling);
  }

  float3 EncodeSafe(float3 color, float scaling = 10000.f) {
    return Encode(max(0, color), scaling);
  }

  float3 DecodeSafe(float3 color, float scaling = 10000.f) {
    return Decode(max(0, color), scaling);
  }
}

float Rescale(float x, float x_min, float x_max, float y_min = 0, float y_max = 1, bool clamp = false) {
  float value = lerp(y_min, y_max, (x - x_min) / (x_max - x_min));
  if (clamp) value = saturate(value);
  return value;
}

float HermiteSplineRolloff(float input, float target_white = 1.f, float max_white = 20.f) {
  float l_w = max_white;
  float l_max = target_white;
  float e_1 = Rescale(input, 0, l_w);
  float max_lum = Rescale(l_max, 0, l_w);
  float knee_start = 1.5f * max_lum - 0.5f;
  float t_b = Rescale(e_1, knee_start, 1.f);
  float t_b_squared = t_b * t_b;
  float t_b_cubed = t_b_squared * t_b;
  float two_t_b_cubed = 2.f * t_b_cubed;
  float three_t_b_squared = 3.f * t_b_squared;
  float p_e1_h00 = (two_t_b_cubed - three_t_b_squared + 1.f);
  float p_e1_h10 = (t_b_cubed - 2.f * t_b_squared + t_b);
  float p_e1_h01 = (-two_t_b_cubed + three_t_b_squared);
  float p_e1 = p_e1_h00 * knee_start
               + p_e1_h10 * (1.f - knee_start)
               + p_e1_h01 * max_lum;
  float e_2 = (e_1 < knee_start) ? e_1 : p_e1;
  float e_3 = e_2;
  float e_4 = l_w * e_3;
  return min(e_4, target_white);
}
float HermiteSplineRolloff(
    float input,
    float target_white,
    float max_white,
    float target_black,
    float min_black = 0.f) {
  float l_w = max_white;
  float l_b = min_black;
  float l_min = target_black;
  float l_max = target_white;
  float e_1 = Rescale(input, l_b, l_w);
  float min_lum = Rescale(l_min, l_b, l_w);
  float max_lum = Rescale(l_max, l_b, l_w);
  float knee_start = 1.5f * max_lum - 0.5f;
  float b = min_lum;
  float t_b = Rescale(e_1, knee_start, 1.f);
  float t_b_squared = t_b * t_b;
  float t_b_cubed = t_b_squared * t_b;
  float two_t_b_cubed = 2.f * t_b_cubed;
  float three_t_b_squared = 3.f * t_b_squared;
  float p_e1_h00 = (two_t_b_cubed - three_t_b_squared + 1.f);
  float p_e1_h10 = (t_b_cubed - 2.f * t_b_squared + t_b);
  float p_e1_h01 = (-two_t_b_cubed + three_t_b_squared);
  float p_e1 = p_e1_h00 * knee_start
               + p_e1_h10 * (1.f - knee_start)
               + p_e1_h01 * max_lum;
  float e_2 = (e_1 < knee_start) ? e_1 : p_e1;
  float e_3a1 = (1 - e_2) * (1 - e_2);
  float e_3a2 = e_3a1 * (1 - e_2);
  float e_3 = e_2 + (b * e_3a2);
  e_3 = saturate(e_3);
  float e_4 = lerp(l_b, l_w, e_3);
  return e_4;
}

float HermiteSplineLuminanceRolloff(float luminance, float target_white, float max_white, float target_black, float min_black, float nits = 100.f) {
  float luminance_pq = pq::Encode(luminance, nits);
  float target_white_pq = pq::Encode(target_white, nits);
  float max_white_pq = pq::Encode(max_white, nits);
  float target_black_pq = pq::Encode(target_black, nits);
  float min_black_pq = pq::Encode(min_black, nits);

  float scaled = HermiteSplineRolloff(luminance_pq, target_white_pq, max_white_pq, target_black_pq, min_black_pq);

  float unpq_scaled = pq::Decode(scaled, nits);
  return unpq_scaled;
}

float3 HermiteSplineLuminanceRolloff(float3 color, float target_white, float max_white, float target_black, float min_black = 0.f, float nits = 100.f) {
  float y = GetLuminance(color, CS_BT709);
  float new_y = HermiteSplineLuminanceRolloff(y, target_white, max_white, target_black, min_black, nits);
  float3 new_color = y > 0 ? color * (new_y / y) : 0;
  return new_color;
}

float3 HermiteSplinePerChannelRolloff(float3 input, float target_white, float max_white, float target_black, float min_black = 0.f, float nits = 100.f) {
  float3 input_pq = pq::Encode(input, nits);
  float target_white_pq = pq::Encode(target_white, nits);
  float max_white_pq = pq::Encode(max_white, nits);
  float target_black_pq = pq::Encode(target_black, nits);
  float min_black_pq = pq::Encode(min_black, nits);

  float3 scaled = float3(
      HermiteSplineRolloff(input_pq.r, target_white_pq, max_white_pq, target_black_pq, min_black_pq),
      HermiteSplineRolloff(input_pq.g, target_white_pq, max_white_pq, target_black_pq, min_black_pq),
      HermiteSplineRolloff(input_pq.b, target_white_pq, max_white_pq, target_black_pq, min_black_pq));

  float3 unpq_scaled = pq::Decode(scaled, nits);
  return unpq_scaled;
}

float HermiteSplineLuminanceRolloff(float luminance, float target_white = 1.f, float max_white = 20.f) {
  if (luminance == 0) return 0;
  return exp2(HermiteSplineRolloff(log2(luminance), log2(target_white), log2(max_white)));
}

float3 HermiteSplineLuminanceRolloff(float3 color, float target_white = 1.f, float max_white = 20.f) {
  float y = GetLuminance(color, CS_BT709);
  float new_y = HermiteSplineLuminanceRolloff(y, target_white, max_white);
  float3 new_color = y > 0 ? color * (new_y / y) : 0;
  return new_color;
}

float3 HermiteSplinePerChannelRolloff(float3 input, float target_white = 1.f, float max_white = 20.f) {
  float target_white_log2 = log2(target_white);
  float max_white_log2 = log2(max_white);
  float3 scaled = float3(
      input.r == 0 ? 0 : exp2(HermiteSplineRolloff(log2(input.r), target_white_log2, max_white_log2)),
      input.g == 0 ? 0 : exp2(HermiteSplineRolloff(log2(input.g), target_white_log2, max_white_log2)),
      input.b == 0 ? 0 : exp2(HermiteSplineRolloff(log2(input.b), target_white_log2, max_white_log2)));
  return scaled;
}