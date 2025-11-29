#define LUT_3D 1

#include "../Includes/ColorGradingLUT.hlsl"
#include "../Includes/Tonemap.hlsl"

struct postfx_luminance_autoexposure_t
{
    float EngineLuminanceFactor;   // Offset:    0
    float LuminanceFactor;         // Offset:    4
    float MinLuminanceLDR;         // Offset:    8
    float MaxLuminanceLDR;         // Offset:   12
    float MiddleGreyLuminanceLDR;  // Offset:   16
    float EV;                      // Offset:   20
    float Fstop;                   // Offset:   24
    uint PeakHistogramValue;       // Offset:   28
};

cbuffer PerInstanceCB : register(b2)
{
  float4 cb_positiontoviewtexture : packoffset(c0);
  float4 cb_postfx_tonemapping_tonemappingparms : packoffset(c1);
  // TM params for colors above a threshold
  float4 cb_postfx_tonemapping_tonemappingcoeffs1 : packoffset(c2);
  // TM params for colors below a threshold
  float4 cb_postfx_tonemapping_tonemappingcoeffs0 : packoffset(c3);
  float4 cb_postfx_lensdirt_usedefault : packoffset(c4);
  float2 cb_env_tonemapping_gamma_brightness : packoffset(c5);
  uint2 cb_postfx_luminance_exposureindex : packoffset(c5.z);
#if _A6F33860
  float cb_env_bloom_veil_strength : packoffset(c6);
  float cb_view_white_level : packoffset(c6.y);
  float cb_postfx_luminance_customevbias : packoffset(c6.z);
  float cb_postfx_lensflares_streakwidth : packoffset(c6.w);
  float cb_postfx_lensflares_streakradius : packoffset(c7);
  float cb_postfx_lensflares_streakopacity : packoffset(c7.y);
  float cb_postfx_lensflares_streakoffset : packoffset(c7.z);
  float cb_postfx_bloom_lensdirt_strength : packoffset(c7.w);
  float cb_postfx_bloom_lensdirt_blendweight : packoffset(c8);
  uint cb_postfx_bloom_enabled : packoffset(c8.y);
#elif _D2F50617
  float2 cb_localtime : packoffset(c6);
  float cb_env_bloom_veil_strength : packoffset(c6.z);
  float cb_view_white_level : packoffset(c6.w);
  float cb_postfx_luminance_customevbias : packoffset(c7);
  float cb_postfx_lensflares_streakwidth : packoffset(c7.y);
  float cb_postfx_lensflares_streakradius : packoffset(c7.z);
  float cb_postfx_lensflares_streakopacity : packoffset(c7.w);
  float cb_postfx_lensflares_streakoffset : packoffset(c8);
  float cb_postfx_bloom_lensdirt_strength : packoffset(c8.y);
  float cb_postfx_bloom_lensdirt_blendweight : packoffset(c8.z);
  uint cb_postfx_bloom_enabled : packoffset(c8.w);
#endif
}

cbuffer PerViewCB : register(b1)
{
  float4 cb_alwaystweak : packoffset(c0);
  float4 cb_viewrandom : packoffset(c1);
  float4x4 cb_viewprojectionmatrix : packoffset(c2);
  float4x4 cb_viewmatrix : packoffset(c6);
  float4 cb_subpixeloffset : packoffset(c10);
  float4x4 cb_projectionmatrix : packoffset(c11);
  float4x4 cb_previousviewprojectionmatrix : packoffset(c15);
  float4x4 cb_previousviewmatrix : packoffset(c19);
  float4x4 cb_previousprojectionmatrix : packoffset(c23);
  float4 cb_mousecursorposition : packoffset(c27);
  float4 cb_mousebuttonsdown : packoffset(c28);
  float4 cb_jittervectors : packoffset(c29);
  float4x4 cb_inverseviewprojectionmatrix : packoffset(c30);
  float4x4 cb_inverseviewmatrix : packoffset(c34);
  float4x4 cb_inverseprojectionmatrix : packoffset(c38);
  float4 cb_globalviewinfos : packoffset(c42);
  float3 cb_wscamforwarddir : packoffset(c43);
  uint cb_alwaysone : packoffset(c43.w);
  float3 cb_wscamupdir : packoffset(c44);
  uint cb_usecompressedhdrbuffers : packoffset(c44.w);
  float3 cb_wscampos : packoffset(c45);
  float cb_time : packoffset(c45.w);
  float3 cb_wscamleftdir : packoffset(c46);
  float cb_systime : packoffset(c46.w);
  float2 cb_jitterrelativetopreviousframe : packoffset(c47);
  float2 cb_worldtime : packoffset(c47.z);
  float2 cb_shadowmapatlasslicedimensions : packoffset(c48);
  float2 cb_resolutionscale : packoffset(c48.z);
  float2 cb_parallelshadowmapslicedimensions : packoffset(c49);
  float cb_framenumber : packoffset(c49.z);
  uint cb_alwayszero : packoffset(c49.w);
}

SamplerState smp_linearclamp_s : register(s0);
Texture2D<float4> ro_postfx_bloom_lensdirt_to : register(t0);
Texture2D<float4> ro_postfx_bloom_lensdirt_from : register(t1);
Texture3D<float4> ro_tonemapping_finalcolorcube : register(t2); // LUT
Texture2D<float4> ro_postfx_lensflares_texlensflares : register(t3);
Texture2D<float4> ro_postfx_bloom_texbloom : register(t4); // R16G16B16A16F by defualt
Texture2D<float4> ro_viewcolormap : register(t5); // The actual input color (HDR linear) (R11G11B10F by default)
StructuredBuffer<postfx_luminance_autoexposure_t> ro_postfx_luminance_buffautoexposure : register(t6);

// 3Dmigoto declarations
#define cmp -

#define FIX_RAISED_BLACKS 1

static const float bloomStrength = 1.0;
static const float lensDirtStrength = 1.0;
static const float colorGradeLUTStrength = 1.0;

float3 applyUserTonemap(float3 inputColor)
{
  if (LumaSettings.DisplayMode == 1)
  {
    //float3 gammaCorrectedColor = renodx::color::correct::GammaSafe(inputColor);
    const float paperWhite = LumaSettings.GamePaperWhiteNits / sRGB_WhiteLevelNits;
    const float peakWhite = LumaSettings.PeakWhiteNits / sRGB_WhiteLevelNits;
		return Tonemap_DICE(inputColor * paperWhite, peakWhite) / paperWhite;
    //tonemapped = renodx::color::correct::GammaSafe(tonemapped, true);
  }
  else
  {
    //const float peakWhite = renodx::color::correct::GammaSafe(injectedData.toneMapPeakNits / injectedData.toneMapGameNits, true);
    //return renodx::tonemap::ReinhardScalable(inputColor, peakWhite);
    return inputColor;
  }
}

float3 vanillaTonemap_Inverse(float3 inputColor, float4 tonemappingCoeffs0 /*= cb_postfx_tonemapping_tonemappingcoeffs0*/, float4 tonemappingCoeffs1 /*= cb_postfx_tonemapping_tonemappingcoeffs1*/)
{
#if 0
    return inputColor;
#endif
    
    float3 outputColor0 = (tonemappingCoeffs0.z - (tonemappingCoeffs0.w * inputColor.rgb)) / ((tonemappingCoeffs0.y * inputColor.rgb) - tonemappingCoeffs0.x);
    float3 outputColor1 = (tonemappingCoeffs1.z - (tonemappingCoeffs1.w * inputColor.rgb)) / ((tonemappingCoeffs1.y * inputColor.rgb) - tonemappingCoeffs1.x);

    //TODO: add threshold? Or actually, pick based on the distance from validity...
    bool3 valid0 = outputColor0 < cb_postfx_tonemapping_tonemappingparms.x;
    bool3 valid1 = outputColor1 >= cb_postfx_tonemapping_tonemappingparms.x;

    float3 outputColor = 0.0;
    if (valid0.r && valid1.r)
        outputColor.r = max(outputColor0.r, outputColor1.r);
    else if (valid0.r)
        outputColor.r = outputColor0.r;
    else if (valid1.r)
        outputColor.r = outputColor1.r;
    if (valid0.g && valid1.g)
        outputColor.g = max(outputColor0.g, outputColor1.g);
    else if (valid0.g)
        outputColor.g = outputColor0.g;
    else if (valid1.g)
        outputColor.g = outputColor1.g;
    if (valid0.b && valid1.b)
        outputColor.b = max(outputColor0.b, outputColor1.b);
    else if (valid0.b)
        outputColor.b = outputColor0.b;
    else if (valid1.b)
        outputColor.b = outputColor1.b;
    return outputColor;
}

// This doesn't seem to make much sense given that TAA was running before tonemapping and storing its history on a linear (R11G11B10F) texture.
// My guess is that they first wrote TAA on a R8G8B8A8 UNORM texture, and hence applied gamma and tonemap to it, and then converted to storing it in linear space and forgot about it.
float3 vanillaTonemap(float3 inputColor, float4 tonemappingCoeffs0 /*= cb_postfx_tonemapping_tonemappingcoeffs0*/, float4 tonemappingCoeffs1 /*= cb_postfx_tonemapping_tonemappingcoeffs1*/)
{
#if 0 // TAA doesn't need tonemapping to work properly, in fact, it's probably worse (and more expensive) to run it (actually this breaks the output)
    return inputColor;
#endif
#if 0
    return inputColor * (0.18 / vanillaTonemap_Inverse(0.18, inverse, tonemappingCoeffs0, tonemappingCoeffs1));
#endif
#if 0
    if (inverse)
    {
        return inputColor * (0.18 / vanillaTonemap_Inverse(0.18, inverse, tonemappingCoeffs0, tonemappingCoeffs1));
    }
#endif
    // Some threshold to skip tonemapping, or treat highlights differently.
    // Unless this threshold is 0, the tonemapper output won't be contiguous,
    // unless both "cb_postfx_tonemapping_tonemappingcoeffs0" and "cb_postfx_tonemapping_tonemappingcoeffs1" were equal,
    // or were specifically calculated to match at the threshold point (which is probably what's happening).
    bool3 tonemapThreshold = inputColor < cb_postfx_tonemapping_tonemappingparms.x;
        
    // This isn't the actual inverse tonemap formula, it's just called inverse anyway for some reason
    
    float3 outputColor;
    float4 tonemappingCoeffs;
    // This is an "advanced" version of Reinhard with levels and other kind of curves/scaling (it seemengly supports negative input values properly).
    // Unless the coefficients have very specific values, this will not compress to exactly 0-1, and could end up clipping.
    // The tonemap coefficients are probably something like this:
    // x: exposure/brightness scaling (dividend). Likely close or identical to "y". Neutral value at 1.
    // y: exposure/brightness scaling (divisor). Likely close or identical to "x". Neutral value at 1.
    // z: additive brightness levelling. This can be used to raise or crush (clip) blacks. Neutral value at 0. This should generally be lower than "w".
    // w: neutral Reinhard value at 1. It's likely that it often revolves around that value.
    tonemappingCoeffs = tonemapThreshold.r ? tonemappingCoeffs0.xyzw : tonemappingCoeffs1.xyzw;
    outputColor.r = ((tonemappingCoeffs.x * inputColor.r) + tonemappingCoeffs.z) / ((tonemappingCoeffs.y * inputColor.r) + tonemappingCoeffs.w);

    tonemappingCoeffs = tonemapThreshold.g ? tonemappingCoeffs0.xyzw : tonemappingCoeffs1.xyzw;
    outputColor.g = ((tonemappingCoeffs.x * inputColor.g) + tonemappingCoeffs.z) / ((tonemappingCoeffs.y * inputColor.g) + tonemappingCoeffs.w);

    tonemappingCoeffs = tonemapThreshold.b ? tonemappingCoeffs0.xyzw : tonemappingCoeffs1.xyzw;
    outputColor.b = ((tonemappingCoeffs.x * inputColor.b) + tonemappingCoeffs.z) / ((tonemappingCoeffs.y * inputColor.b) + tonemappingCoeffs.w);

    return outputColor;
}

void main(
  float4 v0 : INTERP0,
  float4 v1 : SV_POSITION0,
  out float4 o0 : SV_TARGET0)
{
  float4 r0,r1,r2,r3,r4;

  r0.xy = (uint2)v1.xy;
  r0.zw = float2(0,0);
  r0.xyz = ro_viewcolormap.Load(r0.xyz).xyz;
  r0.w = ro_postfx_luminance_buffautoexposure[cb_postfx_luminance_exposureindex.y].EngineLuminanceFactor;
  r0.w = cb_view_white_level * r0.w;
  r1.xyz = r0.xyz * r0.www;
  r0.xyz = cb_usecompressedhdrbuffers ? r1.xyz : r0.xyz;
  if (cb_postfx_bloom_enabled != 0)
  {
    r1.xy = cb_resolutionscale.xy * v0.xy;
    r1.z = cmp(0 < cb_postfx_lensdirt_usedefault.x);
    
    if (lensDirtStrength == 0)
    {
      r1.z = false; // skip dirty lens effect
    }
    
    if (r1.z != 0)
    {
      r1.zw = cb_subpixeloffset.xy + v0.xy;
      r2.xyz = ro_postfx_bloom_lensdirt_from.SampleLevel(smp_linearclamp_s, r1.zw, 0).xyz;
    } else
    {
      r1.zw = cb_subpixeloffset.xy + v0.xy;
      r3.xyz = ro_postfx_bloom_lensdirt_from.SampleLevel(smp_linearclamp_s, r1.zw, 0).xyz;
      r4.xyz = ro_postfx_bloom_lensdirt_to.SampleLevel(smp_linearclamp_s, r1.zw, 0).xyz;
      r4.xyz = r4.xyz + -r3.xyz;
      r2.xyz = cb_postfx_bloom_lensdirt_blendweight * r4.xyz * lensDirtStrength + r3.xyz;
    }
    r1.z = cmp(0 < cb_env_bloom_veil_strength);
    if (r1.z != 0)
    {
      r3.xyz = ro_postfx_bloom_texbloom.SampleLevel(smp_linearclamp_s, r1.xy, 0).xyz;
      r4.xyz = r3.xyz * r0.www;
      r3.xyz = cb_usecompressedhdrbuffers ? r4.xyz : r3.xyz;

      float3 vanillaBloom = cb_env_bloom_veil_strength * r3.xyz;
      r3.xyz = bloomStrength * cb_env_bloom_veil_strength * r3.xyz;  // bloom strength

      if (bloomStrength != 1)
      {
        float vanillaBloomLum = GetLuminance(vanillaBloom);
        r3.xyz = lerp(vanillaBloom.rgb, r3.xyz, saturate(vanillaBloomLum/0.18f));
      }
      
      r4.xyz = cb_postfx_bloom_lensdirt_strength * r2.xyz * lensDirtStrength; // lens dirt
      r3.xyz = r4.xyz * r3.xyz + r3.xyz;
      r0.xyz = r3.xyz + r0.xyz;
    }
    r1.zw = float2(-0.5,-0.5) + v0.xy;
    r2.w = cb_viewmatrix._m02 + cb_viewmatrix._m21;
    sincos(r2.w, r3.x, r4.x);
    r3.xy = float2(-0.5,0.5) * r3.xx;
    r3.z = r4.x;
    r4.x = dot(r3.zx, r1.zw);
    r4.y = dot(r3.yz, r1.zw);
    r1.z = dot(r4.xy, r4.xy);
    r1.w = min(abs(r4.x), abs(r4.y));
    r2.w = max(abs(r4.x), abs(r4.y));
    r2.w = 1 / r2.w;
    r1.w = r2.w * r1.w;
    r2.w = r1.w * r1.w;
    r3.x = r2.w * 0.0208350997 + -0.0851330012;
    r3.x = r2.w * r3.x + 0.180141002;
    r3.x = r2.w * r3.x + -0.330299497;
    r2.w = r2.w * r3.x + 0.999866009;
    r3.x = r2.w * r1.w;
    r3.y = cmp(abs(r4.y) < abs(r4.x));
    r3.x = r3.x * -2 + 1.57079637;
    r3.x = r3.y ? r3.x : 0;
    r1.w = r1.w * r2.w + r3.x;
    r2.w = cmp(r4.y < -r4.y);
    r2.w = r2.w ? -3.141593 : 0;
    r1.w = r2.w + r1.w;
    r2.w = min(r4.x, r4.y);
    r3.x = max(r4.x, r4.y);
    r2.w = cmp(r2.w < -r2.w);
    r3.x = cmp(r3.x >= -r3.x);
    r2.w = r2.w ? r3.x : 0;
    r1.w = r2.w ? -r1.w : r1.w;
    r1.w = 3.14159274 + r1.w;
    r2.w = 651.898621 * r1.w;
    r2.w = floor(r2.w);
    r3.x = sin(r2.w);
    r3.x = 43758.5469 * r3.x;
    r3.y = 1 + r2.w;
    r3.y = sin(r3.y);
    r3.y = 43758.5469 * r3.y;
    r3.xy = frac(r3.xy);
    r1.w = r1.w * 651.898621 + -r2.w;
    r2.w = r3.y + -r3.x;
    r1.w = r1.w * r2.w + r3.x;
    r1.w = r1.w * cb_postfx_lensflares_streakoffset + cb_postfx_lensflares_streakradius;
    r1.z = -r1.w * r1.w + r1.z;
    r1.z = -cb_postfx_lensflares_streakwidth + abs(r1.z);
    r1.w = 1 / -cb_postfx_lensflares_streakwidth;
    r1.z = saturate(r1.z * r1.w);
    r1.w = r1.z * -2 + 3;
    r1.z = r1.z * r1.z;
    r1.z = r1.w * r1.z;
    r2.xyz = r1.zzz * cb_postfx_lensflares_streakopacity + r2.xyz;
    r1.zw = float2(1,1) / cb_resolutionscale.xy;
    r1.xy = saturate(-cb_positiontoviewtexture.zw * r1.zw + r1.xy);
    r1.xyz = ro_postfx_lensflares_texlensflares.SampleLevel(smp_linearclamp_s, r1.xy, 0).xyz;
    r3.xyz = r1.xyz * r0.www;
    r1.xyz = cb_usecompressedhdrbuffers ? r3.xyz : r1.xyz;
    r2.xyz = r2.xyz * float3(0.800000012,0.800000012,0.800000012) + float3(0.200000003,0.200000003,0.200000003);
    r0.xyz = r1.xyz * r2.xyz + r0.xyz;
  }
  r0.xyz = v0.zzz * r0.xyz; // auto exposure
  float3 untonemapped = r0.xyz;
  
  float4 tonemappingCoeffs0 = cb_postfx_tonemapping_tonemappingcoeffs0;
  float4 tonemappingCoeffs1 = cb_postfx_tonemapping_tonemappingcoeffs1;

#if FIX_RAISED_BLACKS
    tonemappingCoeffs0.z *= pow(saturate(GetLuminance(untonemapped) / cb_postfx_tonemapping_tonemappingparms.x), 0.333);
    //tonemappingCoeffs0.z = 0;
    //tonemappingCoeffs0.z = 0;
#endif

  float3 outputColor;
  float3 vanillaTonemap_ = vanillaTonemap(untonemapped, tonemappingCoeffs0, tonemappingCoeffs1); // TODO: remove _
  
#if 0 // apply color grading to vanillaTonemap so blending doesn't break sliders
  //case: if (LumaSettings.DisplayMode >= 1)
  float3 adjustedVanillaTonemap = renodx::color::grade::UserColorGrading(
        vanillaTonemap_,
        injectedData.colorGradeExposure,
        1.f,                                // highlight only applies to HDR color
        injectedData.colorGradeShadows,
        injectedData.colorGradeContrast,
        injectedData.colorGradeSaturation,
        injectedData.colorGradeBlowout);
#else
  float3 adjustedVanillaTonemap = vanillaTonemap_;
#endif
  
  if (LumaSettings.DisplayMode == 1) // HDR / DICE
  {
#if 0 // make HDR tonemap resemble SDR tonemap to facilitate better blending
    untonemapped = renodx::color::grade::UserColorGrading(
        untonemapped,
        injectedData.colorGradeExposure,
        injectedData.colorGradeHighlights * 1.04f,
        injectedData.colorGradeShadows * 1.18f,
        injectedData.colorGradeContrast * 1.2f,
        injectedData.colorGradeSaturation * 1.1f,
        injectedData.colorGradeBlowout,
        injectedData.toneMapHueCorrection,
        vanillaTonemap_);
#endif

    const float SDRTMMidGrayOut = MidGray; 
    const float SDRTMMidGrayIn = GetLuminance(vanillaTonemap_Inverse(SDRTMMidGrayOut, tonemappingCoeffs0, tonemappingCoeffs1));
    float SDRTMMidGrayRatio = SDRTMMidGrayOut / SDRTMMidGrayIn;
    untonemapped *= SDRTMMidGrayRatio;
    
    // blend HDR with SDR
    float3 negHDR = min(0, untonemapped); // save WCG
    untonemapped = lerp(vanillaTonemap_, max(0, untonemapped), saturate(vanillaTonemap_ / MidGray));
    untonemapped += negHDR; // add back WCG
    
    LUTExtrapolationData extrapolationData = DefaultLUTExtrapolationData();
    extrapolationData.inputColor = untonemapped.rgb;
    extrapolationData.vanillaInputColor = vanillaTonemap_;

    LUTExtrapolationSettings extrapolationSettings = DefaultLUTExtrapolationSettings();
    extrapolationSettings.enableExtrapolation = bool(ENABLE_LUT_EXTRAPOLATION);
    extrapolationSettings.extrapolationQuality = LUT_EXTRAPOLATION_QUALITY;
    extrapolationSettings.lutSize = 32;
    
    // DH2 is all linear
    extrapolationSettings.inputLinear = true;
    extrapolationSettings.lutInputLinear = true;
    extrapolationSettings.lutOutputLinear = true;
    extrapolationSettings.outputLinear = true;

    // Intermediary gamma correction through LUT
    extrapolationSettings.transferFunctionIn = LUT_EXTRAPOLATION_TRANSFER_FUNCTION_GAMMA_2_2;
    extrapolationSettings.transferFunctionOut = LUT_EXTRAPOLATION_TRANSFER_FUNCTION_GAMMA_2_2;

    // apply extrapolated LUT to HDR
    float3 hdrLUTOutput = SampleLUTWithExtrapolation(
        ro_tonemapping_finalcolorcube,        // LUT
        smp_linearclamp_s,                    // samplerState
        extrapolationData,
        extrapolationSettings
    );
    hdrLUTOutput = lerp(untonemapped, hdrLUTOutput, colorGradeLUTStrength);

    // tonemap HDR Color
    float3 hdrColor = applyUserTonemap(hdrLUTOutput);

#if FIX_RAISED_BLACKS && 0
    if (injectedData.blackFloor)
    {  // blend back lower black floor from hdrColor
      float3 hdrLab = renodx::color::oklab::from::BT709(hdrColor);
      float3 sdrLab = renodx::color::oklab::from::BT709(sdrColor);

      sdrLab[0] = hdrLab[0];  // apply lightness from hdrColor to sdrColor

      float3 newBlackFloor = renodx::color::bt709::from::OkLab(sdrLab);
      float newBlackFloorLum = renodx::color::y::from::BT709(newBlackFloor);

      negHDR = min(0, blendedColor); // save WCG
      blendedColor = lerp(saturate(newBlackFloor), max(0, blendedColor), saturate(newBlackFloorLum / MidGray));
      blendedColor += negHDR; // add back WCG
      hdrColor = blendedColor;
    }
#endif

    outputColor = hdrColor;
  }
  else
  {
    float3 sdrColor = SampleLUT(ro_tonemapping_finalcolorcube, smp_linearclamp_s, adjustedVanillaTonemap, 32u);
    sdrColor = lerp(adjustedVanillaTonemap, sdrColor, colorGradeLUTStrength);
    outputColor = sdrColor;
  }

  r0.xyz = cb_env_tonemapping_gamma_brightness.y * outputColor;
  o0.xyz = safePow(r0.xyz, cb_env_tonemapping_gamma_brightness.x); //TODO: apply this b4 TM?
  o0.w = 1;

  if (cb_env_tonemapping_gamma_brightness.y != 1)
  {
    o0.xyz = float3(0, 1, 0);
  }
  if (cb_env_tonemapping_gamma_brightness.x != 1)
  {
    o0.xyz = float3(1, 0, 0);
  }
#if !FIX_RAISED_BLACKS && 0 // Test raised blacks. Happens always
  if (cb_postfx_tonemapping_tonemappingcoeffs0.z != 0)
  {
    o0.xyz = float3(1, 0, 1);
  }
#endif
}
