#include "./Includes/Common.hlsl"
// #include "./Includes/HermiteSpline_portable.hlsl"
#include "./Includes/ictcp_portable.hlsl"

#define FIRE_PEAK 1.26
#define FIRE_BOOST 2.6

/////////////////////////////////////////////////////////////////////////////////////////
// rect is top left (x,y), bottom right (x,y)
float3 DrawRect(float2 uv, float4 rect, float3 color, float3 rectColor) {
	float r = step(rect.x, uv.x) * step(uv.x, rect.z) * step(rect.y, uv.y) * step(uv.y, rect.w);
	if (r == 0) return color;
	return rectColor;
}
////////////////////////////////////////////////////////////////////////////////////////
float3 RestoreHueAndChrominanceUcsInternal(float3 targetUcs, float3 sourceUcs, float currentChrominance, float hueStrength, float chrominanceStrength, float minChromaRatio = 0.f)
{
  if (targetUcs.x == 0) return targetUcs;

  if (hueStrength != 0.0)
  {
    const float chrominancePre = currentChrominance;
    targetUcs.yz = lerp(targetUcs.yz, sourceUcs.yz, hueStrength);
    const float chrominancePost = length(targetUcs.yz);
    float chrominanceRatio = safeDivision(chrominancePre, chrominancePost, 1);
    targetUcs.yz *= chrominanceRatio;
  }

  if (chrominanceStrength != 0.0)
  {
    const float sourceChrominance = length(sourceUcs.yz);
    float targetChrominanceRatio = safeDivision(sourceChrominance, currentChrominance, 1);
    targetChrominanceRatio = clamp(targetChrominanceRatio, minChromaRatio, FLT_MAX);
    targetUcs.yz *= lerp(1.0, targetChrominanceRatio, chrominanceStrength);
  }

  return targetUcs;
}

float3 RestoreHueAndChrominanceUcs(float3 targetUcs, float3 sourceUcs, float hueStrength, float chrominanceStrength, float minChromaRatio = 0.f)
{
  return RestoreHueAndChrominanceUcsInternal(targetUcs, sourceUcs, length(targetUcs.yz), hueStrength, chrominanceStrength, minChromaRatio);
}
////////////////////////////////////////////////////////////////////////////////////////
float3 UCS_Encode(float3 x) {
  return renodx::color::ictcp::To(x, CS_BT709);
}
float3 UCS_Decode(float3 x) {
  return renodx::color::ictcp::From(x, CS_BT709);
}
/////////////////////////////////////////////////////////////////////////////////////////
// float GetLuminance(float3 x) {
//   return dot(x, float3(0.2126390059f, 0.7151686788f, 0.0721923154f));
// }
float3 ClampByMaxChannel(float3 x, float p) {
  float m = max(max(x.x, x.y), x.z);
  if (m > p) x *= p / m;
  return x;
}
float DivideSafe(float numerator, float denominator, float defaultValue) {
  return denominator != 0.0f ? numerator / denominator : defaultValue;
}
float3 DivideSafe(float3 numerator, float3 denominator, float3 defaultValue) {
  return denominator != 0.0f ? numerator / denominator : defaultValue;
}
bool FloatEquals(float a, float b, float epsilon) {
  return abs(a - b) <= epsilon;
}
/////////////////////////////////////////////////////////////////////////////////////////
float Neutwo(float x) {
  float numerator = x;
  float denominator_squared = mad(x, x, 1.0);
  return numerator * rsqrt(denominator_squared);
}
float Neutwo(float x, float peak) {
  float p = peak;

  float numerator = p * x;
  float denominator_squared = mad(x, x, p * p);
  return numerator * rsqrt(denominator_squared);
}
float3 Neutwo(float3 x, float peak) {
  float p = peak;

  float3 numerator = p * x;
  float3 denominator_squared = mad(x, x, p * p);
  return numerator * rsqrt(denominator_squared);
}
float3 Neupow(float3 x, float peak, float power) {
  float p = peak;
  float3 numerator = p * x;
  float3 denominator_pow = pow(x, power) + pow(p, power);
  return numerator / pow(denominator_pow, rcp(power));
}
float Neupow(float x, float peak, float power) {
  float p = peak;
  float numerator = p * x;
  float denominator_pow = pow(x, power) + pow(p, power);
  return numerator / pow(denominator_pow, rcp(power));
}

float Neutwo(float x, float peak, float clip) {
  float p = peak;
  float c = clip;
  float cc = c * c;
  float pp = p * p;
  float xx = x * x;

  float numerator = c * p * x;
  float denominator_squared = mad(xx, (cc - pp), cc * pp);

  return numerator * rsqrt(denominator_squared);
}
float3 Neutwo(float3 x, float peak, float clip) {
  float p = peak;
  float c = clip;
  float cc = c * c;
  float pp = p * p;
  float3 xx = x * x;

  float3 numerator = c * p * x;
  float3 denominator_squared = mad(xx, (cc - pp), cc * pp);

  return numerator * rsqrt(denominator_squared);
}
float2 Neutwo(float2 x, float peak, float clip) {
  float p = peak;
  float c = clip;
  float cc = c * c;
  float pp = p * p;
  float2 xx = x * x;

  float2 numerator = c * p * x;
  float2 denominator_squared = mad(xx, (cc - pp), cc * pp);

  return numerator * rsqrt(denominator_squared);
}
float NeutwoI(float x, float peak, float clip) {
  float p = peak;
  float c = clip;
  float cc = c * c;
  float pp = p * p;
  float xx = x * x;
  float numerator = c * p * x;
  float denominator_squared = mad(-xx, (cc - pp), cc * pp);
  return numerator * rsqrt(denominator_squared);
}
float3 NeutwoI(float3 x, float peak, float clip) {
  float p = peak;
  float c = clip;
  float cc = c * c;
  float pp = p * p;
  float3 xx = x * x;
  float3 numerator = c * p * x;
  float3 denominator_squared = mad(-xx, (cc - pp), cc * pp);
  return numerator * rsqrt(denominator_squared);
}
float3 Neupow(float3 x, float peak, float clip, float power) {
  float p = peak;
  float c = clip;
  float cc = pow(c, power);
  float pp = pow(p, power);
  float3 xx = pow(x, power);

  float3 numerator = c * p * x;
  float3 denominator_pow = xx * ((cc - pp)) + (cc * pp);
  return numerator / pow(denominator_pow, rcp(power));
}
float Neupow(float x, float peak, float clip, float power) {
  float p = peak;
  float c = clip;
  float cc = pow(c, power);
  float pp = pow(p, power);
  float xx = pow(x, power);

  float numerator = c * p * x;
  float denominator_pow = xx * ((cc - pp)) + (cc * pp);
  return numerator / pow(denominator_pow, rcp(power));
}
/////////////////////////////////////////////////////////////////////////////////////////
float ReinhardSimple(float x, float peak = 1.0)
{
  return x / ((abs(x) / peak) + 1.0);
}
float3 ReinhardSimple(float3 x, float peak = 1.0)
{
  return x / ((abs(x) / peak) + 1.0);
}
float ReinhardExtended(float color, float white_max = 1000.f / 203.f, float peak = 1.f) {
  return ReinhardSimple(color, peak) * (1.f + (peak * color) / (white_max * white_max));
}
float3 ReinhardExtended(float3 color, float white_max = 1000.f / 203.f, float peak = 1.f) {
  return ReinhardSimple(color, peak) * (1.f + (peak * color) / (white_max * white_max));
}
float ComputeReinhardScale(float channel_max = 1.f, float channel_min = 0.f, float gray_in = MidGray, float gray_out = MidGray) {
  return (channel_max * (channel_min * gray_out + channel_min - gray_out))
        / (gray_in * (gray_out - channel_max));
}
float3 ReinhardPiecewise(float3 x, float x_max = 1.f, float shoulder = 0.18f) {
    const float x_min = 0.f;
    float exposure = ComputeReinhardScale(x_max, x_min, shoulder, shoulder);
    float3 tonemapped = mad(x, exposure, x_min) / mad(x, exposure / x_max, 1.f - x_min);
    return lerp(x, tonemapped, step(shoulder, x));
}
float ReinhardPiecewise(float x, float x_max = 1.f, float shoulder = 0.18f) {
    const float x_min = 0.f;
    float exposure = ComputeReinhardScale(x_max, x_min, shoulder, shoulder);
    float tonemapped = mad(x, exposure, x_min) / mad(x, exposure / x_max, 1.f - x_min);
    return lerp(x, tonemapped, step(shoulder, x));
}
////////////////////////////////////////////////////////////////////////////////////////
/// Piecewise linear + exponential compression to a target value starting from a specified number.
/// https://www.ea.com/frostbite/news/high-dynamic-range-color-grading-and-display-in-frostbite
#define EXPONENTIALROLLOFF_GENERATOR(T)                                                 \
  T ExponentialRollOff(T input, float rolloff_start = 0.20f, float output_max = 1.0f) { \
    T rolloff_size = output_max - rolloff_start;                                        \
    T overage = -max((T)0, input - rolloff_start);                                      \
    T rolloff_value = (T)1.0f - exp(overage / rolloff_size);                            \
    T new_overage = mad(rolloff_size, rolloff_value, overage);                          \
    return input + new_overage;                                                         \
  }
#define EXPONENTIALROLLOFF_CLIP_GENERATOR(T)                                         \
  T ExponentialRollOff(T input, float rolloff_start, float output_max, float clip) { \
    T rolloff_size = output_max - rolloff_start;                                     \
    T overage = -max((T)0, input - rolloff_start);                                   \
    T clip_size = rolloff_start - clip;                                              \
    T rolloff_value = (T)1.0f - exp(overage / rolloff_size);                         \
    T clip_value = (T)1.0f - exp(clip_size / rolloff_size);                          \
    T new_overage = mad(rolloff_size, rolloff_value / clip_value, overage);          \
    return input + new_overage;                                                      \
  }
EXPONENTIALROLLOFF_GENERATOR(float)
EXPONENTIALROLLOFF_GENERATOR(float3)
EXPONENTIALROLLOFF_CLIP_GENERATOR(float)
EXPONENTIALROLLOFF_CLIP_GENERATOR(float3)
#undef EXPONENTIALROLLOFF_GENERATOR
#undef EXPONENTIALROLLOFF_CLIP_GENERATOR
//////////////////////////////////////////////////////////////////////////////////////
// Uchimura 2018, "Practical HDR and Wide Color Techniques in Gran Turismo SPORT"
// https://www.desmos.com/calculator/gslcdxvipg
// http://cdn2.gran-turismo.com/data/www/pdi_publications/PracticalHDRandWCGinGTS.pdf
#define GTTONEMAP_GENERATOR(T)                \
  T GTTonemap(T x,                            \
              float P = 1.f,                  \
              float a = 1.f,                  \
              float m = 0.22f,                \
              float l = 0.4f,                 \
              float c = 1.33f,                \
              float b = 0.f) {                \
    float l0 = ((P - m) * l) / a;             \
    float L0 = m - (m / a);                   \
    float L1 = m + (1.0f - m) / a;            \
                                              \
    T S0 = m + l0;                            \
    T S1 = m + a * l0;                        \
    T C2 = (a * P) / (P - S1);                \
    T CP = -C2 / P;                           \
                                              \
    T w0 = 1.0f - smoothstep(0.0f, m, x);     \
    T w2 = step(m + l0, x);                   \
    T w1 = 1.0f - w0 - w2;                    \
                                              \
    T T_ = m * pow(x / m, c) + b;             \
    T S_ = P - (P - S1) * exp(CP * (x - S0)); \
    T L_ = m + a * (x - m);                   \
                                              \
    return T_ * w0 + L_ * w1 + S_ * w2;       \
  }
GTTONEMAP_GENERATOR(float)
GTTONEMAP_GENERATOR(float3)
#undef GTTONEMAP_GENERATOR
float3 GTTonemapToe(float3 x, float m = 100/203, float c = 1.027) {
  m = max(m, 0.0001f);
  float3 w0 = 1.0f - smoothstep(0.0f, m, x);
  float3 T_ = m * pow(x / m, c);
  return T_ * w0 + x * (1.0f - w0);
}
////////////////////////////////////////////////////////////////////////////////////////
float3 HDRTonemap(float3 x, float peak, float start = 0.18) {
  x = Neupow(x, peak, 1 * GS.WhiteClip);
  // x = ExponentialRollOff(x, start, peak);
  // x = ReinhardExtended(x, 10, peak);
  // x = ReinhardPiecewise(x, peak, start);
  return x;
}
////////////////////////////////////////////////////////////////////////////////////////
float FireTonemap(float x, float clip = FIRE_PEAK, float output_max = 1.0f) {
  x = min(x, clip);
  x = Neutwo(x, output_max, clip);
  return x;
}
float2 FireTonemap(float2 x, float clip = FIRE_PEAK, float output_max = 1.0f) {
  x = min(x, clip);
  x = Neutwo(x, output_max, clip);
  return x;
}
float3 FireTonemap(float3 x, float clip = FIRE_PEAK, float output_max = 1.0f) {
  x = min(x, clip);
  x = Neutwo(x, output_max, clip);
  return x;
}