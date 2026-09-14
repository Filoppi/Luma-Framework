#define LUT_3D 1
#include "./Includes/Common.hlsl"
#include "../Includes/ColorGradingLUT.hlsl"
#include "./Includes/PragMap.hlsl"
// #include "./Includes/PragMap2.hlsl"

struct ToneMapInfo {
  float3 x;      // HDR output
  float  w;      // w channel of output
  float3 sdr;    // SDR Hable
  float  p;      // HDR peak, adjusted by delta
  float  pDelta; // HDR peak, adjusted by delta
};
static ToneMapInfo tmi = {float3(0,0,0), 1, float3(0,0,0), 1.0, 1.0};

struct TexTuple {
  Texture3D<float4> t;
  SamplerState s;
};

void SetColor(float3 x) {
  tmi.x = x;
}

void CalcPeakDeltaFromLut(TexTuple lut, float4 ps_color_grading_scale_offset[2], float4 ps_color_grading_half_texel_offset[2]) {
  float d = max3(lut.t.Sample(lut.s, 1).xyz);
  d *= d; // Gamma 2.0
  tmi.pDelta /= d;
}

// https://www.desmos.com/calculator/vhfuy1nawj
void Rolloff(float4 c[5]) {
  tmi.p = 1;

  if (HDR_ENABLED) {
    tmi.p = HDR_PEAK;
    tmi.p *= tmi.p;
    tmi.p *= tmi.pDelta;
    tmi.p = sRGB_Encode(tmi.p);
  }

  float3 x = tmi.x; // TODO: extension needs 100% playthrough test
  float y0 = GetLuminance(x);
  float3 num = (x * (x * c[0].xyz + c[1].xyz));
  float3 den = ((((x * x * c[2].xyz) / tmi.p) + x * c[3].xyz) + c[4].xyz);
  x = safeDivision(num, den, 0);
  x = max(x, 0);

  if (HDR_ENABLED) x = PragMap::hueShiftBezoldBrucke(x, y0 * 0.717, 0.018);

  x = max(x, 0);
  x = min(x, tmi.p);
  tmi.x = x;
}

void LUTGamma(TexTuple lut, float4 ps_color_grading_scale_offset[2], float4 ps_color_grading_half_texel_offset[2]) {
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

  // Gamma 2.0
  tmi.x = sqrt(tmi.x);

  // Sample
  float4 lutResult = lut.t.Sample(lut.s, tmi.x * ps_color_grading_scale_offset[0].xyz + ps_color_grading_half_texel_offset[0].xyz);
  tmi.w = lutResult.w;
  tmi.x = lutResult.xyz;
  // const int lutSize = rcp(1 - ps_color_grading_scale_offset[0].x);
  // tmi.w = lut.t.Sample(lut.s, tmi.x * ps_color_grading_scale_offset[0].xyz + ps_color_grading_half_texel_offset[0].xyz).w;
  // tmi.x = SampleLUT(lut.t, lut.s, tmi.x, 16, true); // TODO: assumming LUT 16x
  
  // HDR Decompress
  if (HDR_ENABLED) {
    tmi.x *= tmi.x;

    tmi.x = RestorePostProcess(colorU, colorN, tmi.x, 0, true);
    tmi.x = max(0, tmi.x);
    // tmi.x = min(tmi.x, HDR_PEAK); // very minor

    tmi.x = sqrt(tmi.x);
  }
}