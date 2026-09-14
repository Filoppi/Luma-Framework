#define LUT_3D 1
#include "./Includes/Common.hlsl"
#include "./Includes/PragMap.hlsl"
// #include "./Includes/PragMap2.hlsl"
#include "../Includes/ColorGradingLUT.hlsl"

#define HALO3_TONEMAP 0

struct ToneMapInfo {
  float3 x;   // HDR output
  float3 sdr; // SDR Hable
  float  p;   // HDR peak, adjusted by delta
};
static ToneMapInfo tmi = {float3(0,0,0), float3(0,0,0), 1.0};

struct TexTuple {
  Texture3D<float4> t;
  SamplerState s;
};

float3 ColorInBlowout(float3 hdr) {
  if (!HDR_ENABLED) return saturate(hdr);

#if HALO3_TONEMAP == 0
  float3 sdr = anchoredCInfinityShoulder(hdr, 1.1525, 0.8175, 1);
  float sdrY = GetLuminance(sdr);
  if (sdrY <= 0) return 0;

  sdr *= GetLuminance(hdr) / sdrY;
  sdr = UCS_Encode(sdr);
  hdr = UCS_Encode(hdr);
  hdr = RestoreHueAndChrominanceUcs(hdr, sdr, 0, 0.18, 0, 100000);
  hdr = UCS_Decode(hdr);
#elif HALO3_TONEMAP == 1

#endif

  hdr = max(0, hdr);
  return hdr;
}

void SetColor(float3 x) {
  tmi.x = x;
  tmi.x = max(tmi.x, 0);
}

float ContrastPower(float3 x, float y, float c) {
  if (!HDR_ENABLED) return pow(y, c);
  return lerp(pow(y, c), y, smoothstep(0, 0.87, GetLuminance(x) * min(1, c))) * max(1, InverseLerp(0.1, 0.09, c)); //when c < 1, flashbang fx
}

float Rolloff_Root(float4 c) {
  //-\frac{c_{z}}{3c_{w}}
  return -c.z / (3.0 * c.w);
}
float Rolloff_Accel(float x, float4 c) {
  //6c_{w}x+2c_{z}
  return 6.0 * c.w * x + 2.0 * c.z;
}
float Rolloff_Vel(float x, float4 c) {
  //3c_{w}x^{2}+2c_{z}x+c_{y}
  return 3.0 * c.w * x * x + 2.0 * c.z * x + c.y;
}
float Rolloff_Vel_1Abs(float4 c) { //Breaks when c.w >= 0 || c.y <= 0. Also mirrored, where positive is useful.
  //\frac{-c_{z}+\sqrt{\left(c_{z}\right)^{2}-3c_{w}\left(c_{y}-1\right)}}{3c_{w}}
  if (c.w >= 0 || c.y <= 0) return 0.0;
  return abs((c.z + sqrt(c.z * c.z - 3.0 * c.w * (c.y - 1.0))) / (3.0 * c.w));
}
float Rolloff_Pos(float x, float4 c) {
  //c_{w}x^{3}+c_{z}x^{2}+c_{y}x
  return ((c.w * x + c.z) * x + c.y) * x;
}
float3 Rolloff_Pos(float3 x, float4 c) {
  //c_{w}x^{3}+c_{z}x^{2}+c_{y}x
  return ((c.w * x + c.z) * x + c.y) * x;
}

// x: linear HDR color
// c: tone_curve_constants
// pDelta: scaler on peak
// https://www.desmos.com/calculator/1kdzfkobf7
void Rolloff(TexTuple lut0, TexTuple lut1, float4 cg_blend_factor, float4 c) {
  // SDR
  tmi.sdr = Rolloff_Pos(min(c.x, tmi.x), c);
  tmi.sdr = max(tmi.sdr, 0);

  // SDR early out
  if (!HDR_ENABLED) {
    tmi.x = tmi.sdr;
    return;
  }

  // HDR Setup
  float slope_at_piecewise;
  float thres_at_piecewise;
  float output_at_piecewise;

  // HDR Threshold
  float root = Rolloff_Root(c);
  thres_at_piecewise = clamp(root, 0, c.x);
  if (thres_at_piecewise <= 0) thres_at_piecewise = Rolloff_Vel_1Abs(c); //find when compression (slope < 1) starts

  // HDR Slope & Output
  if (thres_at_piecewise > 0) {
    slope_at_piecewise = Rolloff_Vel(root, c);
    output_at_piecewise = Rolloff_Pos(thres_at_piecewise, c);
  } else {
    slope_at_piecewise = 1;
    output_at_piecewise = 0;
  }

  // HDR Extended 
  tmi.x = LinearPiecewiseExtension(tmi.sdr, tmi.x, thres_at_piecewise, slope_at_piecewise, output_at_piecewise);

  // HDR pDelta
  float mLut0 = max3(lut0.t.Sample(lut0.s, 1).xyz);
  float mLut1 = max3(lut1.t.Sample(lut0.s, 1).xyz);
  float mLut = lerp(mLut0, mLut1, cg_blend_factor.x);
  float pDelta = rcp(mLut);

  // HDR
  tmi.p = GammaCorrectionPeak(HDR_PEAK * pDelta);
#if HALO3_TONEMAP == 0
  float3 hdr = tmi.x;

  // Hue Correct
  float y0 = dot(hdr, Rec709_Luminance);
  tmi.x = NeupowHQ(tmi.x, tmi.p, 5 * GS.WhiteClip); // HDR Rolloff
  // tmi.x = PragMap::hueShiftBezoldBrucke_PerChannelAid(tmi.x, tmi.sdr, 0.7614 * y0, 0.0459, 0.236 * y0, 0.124 * y0, float2(0, 1));
  tmi.x = PragMap::hueShiftBezoldBrucke(tmi.x, 0.78 * y0, 0.0459);
  tmi.x = max(0, tmi.x);

  tmi.x = max(tmi.x, 0);
#elif HALO3_TONEMAP == 1
  // tmi.x = PragMap::pragmap(tmi.x, tmi.p, 0.5, 0.09);
  // tmi.x = PragMap2::pragmap2_BT709(tmi.x, tmi.p, GamePaperWhiteNits, true, true, 0.777, 0.5115, 0);
#endif

  // Clean
  tmi.x = min(tmi.x, tmi.p);
}

void LUT(TexTuple lut0, TexTuple lut1, float4 cg_blend_factor) {
  #if ALLOW_COLORGRADE == 0
    return;
  #endif

  // HDR Compress
  float3 colorU = tmi.x;
  float3 colorN = colorU;
  if (HDR_ENABLED)
  {
    // luma compress
    float y = GetLuminance(colorN);
    float y1 = y;
    // y1 = Neupow(y1, 0.96, p, 1.);
    y1 = ReinhardClip(y1, 0.96, tmi.p); // max of input is HDR peak
    colorN *= safeDivision(y1, y, 0);

    // cram
    float m = max3(colorN);
    if (m > 1.0) colorN /= m;

    tmi.x = colorN;
  }

  // Sample
  float3 cLut0 = 0;
  if (cg_blend_factor.x < 1) cLut0 = SampleLUT(lut0.t, lut0.s, tmi.x, 16, true);

  float3 cLut1 = SampleLUT(lut1.t, lut1.s, tmi.x, 16, true);
  // float3 cLut1 = lut1.t.Sample(lut1.s, tmi.x * 0.9375 + 0.03125).xyz;
    
  tmi.x = lerp(cLut0, cLut1, cg_blend_factor.x);
  
  // HDR Decompress
  if (HDR_ENABLED) {
    tmi.x = RestorePostProcess(colorU, colorN, tmi.x, 0, true);
    tmi.x = max(0, tmi.x);
    // tmi.x = min(tmi.x, HDR_PEAK); // very minor
  }
}

void GammaOut() {
  // Indirect Upgrade SRV Gamma Mismatch
  // tmi.x = sRGB_Encode(tmi.x);
}