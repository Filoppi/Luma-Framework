#include "GameCBuffers.hlsl"
#include "../../Includes/Common.hlsl"
#include "Settings.hlsl"

#define GS LumaSettings.GameSettings
#define HDR_ENABLED LumaSettings.DisplayMode == 1
#define HDR_PEAK PeakWhiteNits / GamePaperWhiteNits
#define HDR_INTSCALING GamePaperWhiteNits / UIPaperWhiteNits
#define HDR_SHOULDERSTART GS.TonemapperRolloffStart / GamePaperWhiteNits
#define HDR_MAXEXPECTED GS.TonemapperMaxExpected / GamePaperWhiteNits

/////////////////////////////////////////////////////////////////////////////////////

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

/////////////////////////////////////////////////////////////////////////////////////

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
  float3 p_over_x_pow_a = exp2(power * (log2(peak) - log2(x)));
  return peak * rcp(exp2(log2(1.0f + p_over_x_pow_a) * rcp(power)));
}
float Neupow(float x, float peak, float power) {
  float p_over_x_pow_a = exp2(power * (log2(peak) - log2(x)));
  return peak * rcp(exp2(log2(1.0f + p_over_x_pow_a) * rcp(power)));
}
float3 NeupowHQ(float3 x, float peak, float power) {
  float3 m = max(x, peak); //normalization to avoid float errors
  float3 xn = x / m;
  float3 pn = peak / m;
  return m * (xn * pn) / pow(pow(xn, power) + pow(pn, power), rcp(power));
}
float NeupowHQ(float x, float peak, float power) {
  float m = max(x, peak); //normalization to avoid float errors
  float xn = x / m;
  float pn = peak / m;
  return m * (xn * pn) / pow(pow(xn, power) + pow(pn, power), rcp(power));
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

float Neupow(float x, float peak, float clip, float power) {
  // return (clip * peak * x) / pow(pow(x, power) * (pow(clip, power) - pow(peak, power)) + (pow(clip, power) * pow(peak, power)), rcp(power));

  float log2_x = log2(x);
  float x_pow_a = exp2(power * log2_x);
  float k = exp2(-power * log2(peak)) - exp2(-power * log2(clip));
  return exp2(log2_x - log2(k * x_pow_a + 1.0f) * rcp(power));
}
float3 Neupow(float3 x, float peak, float clip, float power) {
  // return (clip * peak * x) / pow(pow(x, power) * (pow(clip, power) - pow(peak, power)) + (pow(clip, power) * pow(peak, power)), rcp(power));

  float3 log2_x = log2(x);
  float3 x_pow_a = exp2(power * log2_x);
  float3 k = exp2(-power * log2(peak)) - exp2(-power * log2(clip));
  return exp2(log2_x - log2(k * x_pow_a + 1.0f) * rcp(power));
}

float Reinhard(float x, float p) {return x / ((x / p) + 1);}
float3 Reinhard(float3 x, float p) {return x / ((x / p) + 1);}

float ReinhardClip(float x, float peak, float clip) { //when power = 1
  return (clip * peak * x) / (x * (clip - peak) + (clip * peak));
}

float3 ReinhardClip(float3 x, float peak, float clip) { //when power = 1
  return (clip * peak * x) / (x * (clip - peak) + (clip * peak));
}

/////////////////////////////////////////////////////////////////////////////////////////

// from PragMap (from Musa)
float anchoredCInfinityShoulder(float color, float peak, float anchor, float compressionStrength) {
  float shoulderRange = peak - anchor;
  float distanceFromAnchor = max(color - anchor, 0.f);
  float flatWeight = exp2(-shoulderRange / (compressionStrength * distanceFromAnchor));
  float responseDenominator = mad(distanceFromAnchor, flatWeight, shoulderRange);
  return mad(shoulderRange, distanceFromAnchor / responseDenominator, color - distanceFromAnchor);
}
float3 anchoredCInfinityShoulder(float3 color, float3 peak, float3 anchor, float compressionStrength) {
  float3 shoulderRange = peak - anchor;
  float3 distanceFromAnchor = max(color - anchor, 0.f);
  float3 flatWeight = exp2(-shoulderRange / (compressionStrength * distanceFromAnchor));
  float3 responseDenominator = mad(distanceFromAnchor, flatWeight, shoulderRange);
  return mad(shoulderRange, distanceFromAnchor / responseDenominator, color - distanceFromAnchor);
}

/////////////////////////////////////////////////////////////////////////////////////

// Extension: slope_at_piecewise * (x - thres_at_piecewise) + output_at_piecewise
float3 LinearPiecewiseExtension(float3 sdr, float3 hdr, float thres, float slope, float output)
{
  float3 lower = sdr;
  float3 upper = slope * (hdr - thres) + output;
  return hdr < thres ? lower : upper;
}

// no contrast
float ReinhardSekiroHDRExtThres(float4 g_ReinhardParam) {
  // 2nd derivate = 0: \frac{1}{r_{y}}\left(\frac{r_{x}-1}{r_{x}+1}\right)^{\frac{1}{r_{x}}}
  float rx = g_ReinhardParam.x;
  float ry = g_ReinhardParam.y;
  return (1.0 / ry) * pow((rx - 1) / (rx + 1), 1.0 / rx); // no need for safe since it'd div0 position too.
}

// no contrast
float ReinhardSekiroVelocity(float x, float4 g_ReinhardParam)
{
  // 1st derivative: \frac{r_{x} \left(r_{y} x\right)^{r_{x}}}{x \left(\left(r_{y} x\right)^{r_{x}} + 1\right)^{2}}
  float rx = g_ReinhardParam.x;
  float ry = g_ReinhardParam.y;
  float a = pow(ry * x, rx);
  float a1 = a + 1;
  return (rx * a) / (x * a1 * a1);
}

// https://www.desmos.com/calculator/vyegkra6zo
float ReinhardSekiro(float x, float4 g_ReinhardParam, float4 g_ToneMapParam, bool doContrast)
{
  float2 r0;
  r0.x = x;

  // num
  r0.x = g_ReinhardParam.y * r0.x;
  r0.x = pow(r0.x, g_ReinhardParam.x);

  // denom
  r0.y = 1 + r0.x; 

  // div
  r0.x = r0.x / r0.y;

  // contrast pow
  if (doContrast) r0.x = pow(r0.x, rcp(g_ToneMapParam.y)); 
  return r0.x;
}
float3 ReinhardSekiro(float3 x, float4 g_ReinhardParam, float4 g_ToneMapParam, bool doContrast)
{
  return float3(
    ReinhardSekiro(x.x, g_ReinhardParam, g_ToneMapParam, doContrast),
    ReinhardSekiro(x.y, g_ReinhardParam, g_ToneMapParam, doContrast),
    ReinhardSekiro(x.z, g_ReinhardParam, g_ToneMapParam, doContrast)
  );
}

/////////////////////////////////////////////////////////////////////////////////////

// cbuffer cbSceneParam : register(b8)
// {
//   float4 FC_FarClipInfo : packoffset(c0);
//   float4 FC_ScreenSize : packoffset(c1);
//   float4 FC_DepthComputeParam : packoffset(c2);
//   float4 SC_CameraPos : packoffset(c3); // isnt affected by jitter
//   float4 VC_ClipPlane : packoffset(c4);
//   float4x3 FC_MatrixView : packoffset(c5);
//   float4x4 FC_MatrixInvViewProj : packoffset(c8);
//   float4x4 FC_MatrixInvProj : packoffset(c12);
//   float4x4 FC_MatrixProj : packoffset(c16);
//   float4x4 FC_MatrixInvView : packoffset(c20);
//   float4x4 VC_MatrixViewProj : packoffset(c24);
//   float4 FC_ShadowMapParam : packoffset(c28);
//   float4 FC_ShadowColor : packoffset(c29);
//   float4 FC_ShadowDir : packoffset(c30);
//   float4x4 VC_ShadowMapMatrix : packoffset(c31);
//   float FC_ShadowDepthRate : packoffset(c35);
//   float FC_ShadowDepthScale : packoffset(c35.y);
//   float FC_ShadowKernelScale : packoffset(c35.z);
//   float FC_ShadowRandTexInvScale : packoffset(c35.w);
//   float FC_ShadowTexInvScaleX : packoffset(c36);
//   float FC_ShadowTexInvScaleY : packoffset(c36.y);
//   float FC_ShadowDepthOffsetScale : packoffset(c36.z);
//   float4 FC_ShadowViewKernelScale : packoffset(c37);
//   float4x4 FC_ProjSpaceToShadowMatrix[4] : packoffset(c38);
//   float4 FC_CascadeSelectDist : packoffset(c54);
//   float FC_FrustumNear : packoffset(c55);
//   float FC_FrustumSliceScale : packoffset(c55.y);
//   float FC_FrustumSliceBias : packoffset(c55.z);
//   uint FC_LightTile_Width : packoffset(c56);
//   uint FC_LightTile_NumCellX : packoffset(c56.y);
//   float4 FC_CloudShadowParam0 : packoffset(c57);
//   float4 FC_CloudShadowParam1 : packoffset(c58);
//   float FC_ShadowScale0 : packoffset(c59);
//   float FC_ShadowScale1 : packoffset(c59.y);
//   bool FC_InsideWaterMode : packoffset(c59.z);
//   bool FC_CameraFadeEnable : packoffset(c59.w);
//   float4 FC_VelocityEncodeParam : packoffset(c60);
//   float4 SC_RenderCameraPos : packoffset(c61); // isnt affected by jitter
//   float SC_ScreenResolutionScale : packoffset(c62);
//   float SC_ViewFrustumEpsilon : packoffset(c62.y);
//   float SC_PreExposure : packoffset(c62.z);
//   float SC_InvPreExposure : packoffset(c62.w);
//   float4 SC_ViewFrustumPlanes[4] : packoffset(c63);
//   float4 FC_VolumeFogParam[4] : packoffset(c67);
// }
