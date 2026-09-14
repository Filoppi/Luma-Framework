#define LUT_3D 1
// #define UCS_MODE 1
#include "./Includes/Common.hlsl"
#include "./Includes/PragMap.hlsl"
#include "../Includes/ColorGradingLUT.hlsl"

#define HALO3_TONEMAP 0
#define BLOOM_MAKEUP 1.36

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

float2 FilmGrainUV(float2 uv) {
#if HALOR_FILMGRAIN_SCALE == 0
  //noop
#elif HALOR_FILMGRAIN_SCALE == 1
  uv *= (720.f / LumaSettings.SwapchainSize.x);
#elif HALOR_FILMGRAIN_SCALE == 2
  uv *= (1080.f / LumaSettings.SwapchainSize.x);
#elif HALOR_FILMGRAIN_SCALE == 3
  uv *= (1440.f / LumaSettings.SwapchainSize.x);
#endif
  return uv;
}

float3 Rolloff(float3 hdr) {
  // SDR early out
  if (!HDR_ENABLED) return saturate(hdr);

  // HDR
  float p = GammaCorrectionPeak(HDR_PEAK);
#if HALO3_TONEMAP == 0
  // Hue Correct
  float y0 = dot(hdr, Rec709_Luminance);
  hdr = NeupowHQ(hdr, p, 5 * GS.WhiteClip); // HDR Rolloff
  hdr = PragMap::hueShiftBezoldBrucke(hdr, 0.78 * y0, 0.0459);
  hdr = max(0, hdr);

  hdr = max(hdr, 0);
#endif

  // Clean
  hdr = max(hdr, 0);
  hdr = min(hdr, p);

  return hdr;
}