// Distance haze fit for the Hitman Absolution 256x7 color grading LUT.
// The tonemap must declare t2 (the LUT) and s2 before including this file.

float3 SampleCurve(float v, float x)
{
  float pos = clamp(min(x, 1.0) * 256.0 - 0.5, 0.0, 255.0);
  float i = floor(pos);
  float t = pos - i;
  float3 pm1 = t2.SampleLevel(s2, float2((clamp(i - 1.0, 0.0, 255.0) + 0.5) / 256.0, v), 0).rgb;
  float3 p0 = t2.SampleLevel(s2, float2((clamp(i, 0.0, 255.0) + 0.5) / 256.0, v), 0).rgb;
  float3 p1 = t2.SampleLevel(s2, float2((clamp(i + 1.0, 0.0, 255.0) + 0.5) / 256.0, v), 0).rgb;
  float3 p2 = t2.SampleLevel(s2, float2((clamp(i + 2.0, 0.0, 255.0) + 0.5) / 256.0, v), 0).rgb;
  float3 r = p0 + 0.5 * t * (p1 - pm1 + t * (2.0 * pm1 - 5.0 * p0 + 4.0 * p1 - p2 + t * (3.0 * (p0 - p1) + p2 - pm1)));
  return clamp(r, min(p0, p1), max(p0, p1));
}

static const float HMA_ROW_NEAR_R = 0.5 / 7.0;
static const float HMA_ROW_FAR_R  = 1.5 / 7.0;
static const float HMA_ROW_NEAR_G = 2.5 / 7.0;
static const float HMA_ROW_FAR_G  = 3.5 / 7.0;
static const float HMA_ROW_NEAR_B = 4.5 / 7.0;
static const float HMA_ROW_FAR_B  = 5.5 / 7.0;
static const float HMA_ROW_DEPTH  = 6.5 / 7.0;

float3 ApplyDistanceHaze(float3 hdrGraded, float3 neutral, float d, float strength)
{
  float4 depthRow = t2.SampleLevel(s2, float2(saturate(d), HMA_ROW_DEPTH), 0);
  float3 s = saturate(float3(depthRow.x, depthRow.z, depthRow.y));
  if (strength <= 0.0 || max(s.r, max(s.g, s.b)) <= 0.0)
    return hdrGraded;

  // Each pair contributes its own channel: R rows 0/1, G rows 2/3, B rows 4/5.
  float3 n05 = float3(SampleCurve(HMA_ROW_NEAR_R, 0.5).r,
                      SampleCurve(HMA_ROW_NEAR_G, 0.5).g,
                      SampleCurve(HMA_ROW_NEAR_B, 0.5).b);
  float3 f05 = float3(SampleCurve(HMA_ROW_FAR_R, 0.5).r,
                      SampleCurve(HMA_ROW_FAR_G, 0.5).g,
                      SampleCurve(HMA_ROW_FAR_B, 0.5).b);
  float3 n1 = float3(SampleCurve(HMA_ROW_NEAR_R, 1.0).r,
                     SampleCurve(HMA_ROW_NEAR_G, 1.0).g,
                     SampleCurve(HMA_ROW_NEAR_B, 1.0).b);
  float3 f1 = float3(SampleCurve(HMA_ROW_FAR_R, 1.0).r,
                     SampleCurve(HMA_ROW_FAR_G, 1.0).g,
                     SampleCurve(HMA_ROW_FAR_B, 1.0).b);

  float3 nearA = n05 * n05;
  float3 farA = f05 * f05;
  float3 nearB = n1 * n1;
  float3 farB = f1 * f1;
  float3 denom = nearB - nearA;
  float3 fittedT = (farB - farA) / max(abs(denom), 1e-6) * sign(denom);
  float3 T = lerp(1.0, clamp(fittedT, 1e-4, 1.0), step(1e-6, abs(denom)));
  // Bilinear taps retain each curve's measured deviation from the affine fit.
  float3 q = sqrt(saturate(neutral));
  float3 nearTapR = t2.SampleLevel(s2, float2(q.r, HMA_ROW_NEAR_R), 0).rgb;
  float3 nearTapG = t2.SampleLevel(s2, float2(q.g, HMA_ROW_NEAR_G), 0).rgb;
  float3 nearTapB = t2.SampleLevel(s2, float2(q.b, HMA_ROW_NEAR_B), 0).rgb;
  float3 farTapR = t2.SampleLevel(s2, float2(q.r, HMA_ROW_FAR_R), 0).rgb;
  float3 farTapG = t2.SampleLevel(s2, float2(q.g, HMA_ROW_FAR_G), 0).rgb;
  float3 farTapB = t2.SampleLevel(s2, float2(q.b, HMA_ROW_FAR_B), 0).rgb;
  float3 nearSum = (nearTapR + nearTapG) + nearTapB;
  float3 farSum = (farTapR + farTapG) + farTapB;
  float3 nearLin = nearSum * nearSum;
  float3 farLin = farSum * farSum;

  float3 e = s * max(strength, 0.0);
  float3 Teff = pow(T, e);
  float3 weight = (1.0 - Teff) / max(1.0 - T, 1e-4);
  weight = lerp(e, weight, step(1e-4, 1.0 - T));
  float3 residual = farLin - T * nearLin;
  return max(hdrGraded * Teff + weight * residual, 0.0);
}
