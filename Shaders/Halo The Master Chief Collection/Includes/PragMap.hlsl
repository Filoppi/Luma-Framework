#ifndef __PRAGMAP__
#define __PRAGMAP__

// TheGreatHmmmmm's pragamatic display mapping

#include "../Includes/Common.hlsl"
#include "../../Includes/JzAzBz.hlsl"

namespace PragMap
{
  // constants: params ---------------------------------------------------------------------------------------- 
  // TODO: make optional customizable by optional defines... or something
  
  static const float bbRangeStops = 8.1f;
  static const float bbAnchor = 0.18f;

  static const float blowoutRangeStops = 6.f;
  static const float blowoutCubicBlend = 0.5f;

  // static const float jzazbzExponentScale = 1.7f;
  // static const float jzazbzEpsilon = 1.6295499532821566e-11f;

  static const float toneAnchor = 0.18f;
  static const float toneCompression = 3.0f - (2.0f * GS.WhiteClip); //TODO: tune
  static const float overshootShoulder = 0.8f;

  // constants: values ---------------------------------------------------------------------------------------- 
  static const float epsilon = 1e-6f;

  static const float pi = 3.14159265358979324f;
  static const float twoPi = 6.28318530717958648f;
  static const float invTwoPi = 0.15915494309189534f;

  static const float bbPurpleHue = 0.19199f; //  11.0 deg  repeller (494c)
  static const float bbYellowHue = 1.86750f; // 107.0 deg  ATTRACTOR (571nm)
  static const float bbGreenHue = 2.95833f;  // 169.5 deg  repeller (506nm)
  static const float bbBlueHue = 4.45932f; // 255.5 deg  ATTRACTOR (474nm)

  static const float bbArcToYellow = bbYellowHue - bbPurpleHue;
  static const float bbArcToGreen = bbGreenHue - bbPurpleHue;
  static const float bbArcToBlue = bbBlueHue - bbPurpleHue;

  // tonemap ----------------------------------------------------------------------------------------

  // from Musa
  float anchoredCInfinityShoulder(float color, float peak, float anchor, float compressionStrength) {
    float shoulderRange = peak - anchor;
    float distanceFromAnchor = max(color - anchor, 0.f);
    float flatWeight = exp2(-shoulderRange / (compressionStrength * distanceFromAnchor));
    float responseDenominator = mad(distanceFromAnchor, flatWeight, shoulderRange);
    return mad(shoulderRange, distanceFromAnchor / responseDenominator, color - distanceFromAnchor);
  }

  // from Musa
  float3 anchoredCInfinityShoulder(float3 color, float3 peak, float3 anchor, float compressionStrength) {
    float3 shoulderRange = peak - anchor;
    float3 distanceFromAnchor = max(color - anchor, 0.f);
    float3 flatWeight = exp2(-shoulderRange / (compressionStrength * distanceFromAnchor));
    float3 responseDenominator = mad(distanceFromAnchor, flatWeight, shoulderRange);
    return mad(shoulderRange, distanceFromAnchor / responseDenominator, color - distanceFromAnchor);
  }

  // Bezold-Brucke hue shift ----------------------------------------------------------------------------------------
  
  float bezoldBruckeShift(float hue, float amount) {
    float h = hue - bbPurpleHue;
    h -= twoPi * floor(h * invTwoPi);

    float arcStart, arcEnd, arcSign, amountPow;
    if (h < bbArcToYellow) {  // purple -> yellow   (reds, oranges)      attractor at the far end
      arcStart = 0.f;
      arcEnd = bbArcToYellow;
      arcSign = 1.f;
      // amountPow = DVS1;
    } else if (h < bbArcToGreen) {  // yellow -> green    (yellow-greens)     attractor at the near end
      arcStart = bbArcToYellow;
      arcEnd = bbArcToGreen;
      arcSign = -1.f;
      // amountPow = DVS2;
    } else if (h < bbArcToBlue) {  // green -> blue      (greens, cyans)     attractor at the far end
      arcStart = bbArcToGreen;
      arcEnd = bbArcToBlue;
      arcSign = DVS3;
      // amountPow = DVS1;
    } else {  // blue -> purple     (violets, magentas) attractor at the near end
      arcStart = bbArcToBlue;
      arcEnd = twoPi;
      arcSign = -1.f;
      // amountPow = DVS4;
    }

    float arcLength = arcEnd - arcStart;
    float t = (h - arcStart) / arcLength;
    float distanceToAttractor = arcSign > 0.f ? (1.f - t) : t;
    // amount = pow(amount, amountPow);

    return arcSign * arcLength * min(0.5f * sin(pi * t) * amount, distanceToAttractor);
  }

  float3 hueShiftBezoldBrucke(float3 color, float driver, float cap, uint colorspace = CS_BT709) {
    float3 jzazbz = JzAzBz::rgbToJzazbz(color, colorspace);

    float chroma = length(jzazbz.yz);
    if (chroma < epsilon) return color;

    float hue = atan2(jzazbz.z, jzazbz.y);
    float shifted = hue + bezoldBruckeShift(hue, saturate(cap * driver));

    float sinHue, cosHue;
    sincos(shifted, sinHue, cosHue);
    jzazbz.yz = float2(cosHue, sinHue) * chroma;

//     // high chroma colors get their chroma reduces slightly to protect against nans
//     float chromaDriver = safeDivision(chroma, chroma + 0.05f, 0);
//     chroma *= 1.f - (0.0677f * cap) * chromaDriver * chromaDriver;
// 
//     float hue = atan2(jzazbz.z, jzazbz.y);
//     hue += bezoldBruckeShift(hue, saturate(cap * driver));
// 
//     float sinHue, cosHue;
//     sincos(hue, sinHue, cosHue);
//     jzazbz.yz = float2(cosHue, sinHue) * chroma;

    return JzAzBz::jzazbzToRgb(jzazbz, colorspace);
  }

  float distanceRolloff(float value, float anchor, float rangeStops) {
    float stops = log2(max(value, epsilon) / max(anchor, epsilon));
    float position = saturate(stops / max(rangeStops, epsilon));

    float positionSquared = position * position;
    return positionSquared * position * mad(position, mad(6.f, position, -15.f), 10.f);
  }

  float3 hueShiftBezoldBrucke_PerChannelAid(float3 color, float3 colorSDR, float driver, float cap, float sdrHue, float sdrChrom, float2 sdrChromLimit = float2(0, 1000000), uint colorspace = CS_BT709) {
    float3 jzazbz = JzAzBz::rgbToJzazbz(color, colorspace);

    float chroma = length(jzazbz.yz);
    if (chroma < epsilon) return color;

    float hue = atan2(jzazbz.z, jzazbz.y);
    float shifted = hue + bezoldBruckeShift(hue, saturate(cap * driver));

    float sinHue, cosHue;
    sincos(shifted, sinHue, cosHue);
    jzazbz.yz = float2(cosHue, sinHue) * chroma;

    float3 colorSDRJzazbz = JzAzBz::rgbToJzazbz(colorSDR, colorspace);
    float colorSDRJzazbzChrom = length(colorSDRJzazbz.yz);
    jzazbz = RestoreHueAndChrominanceUcs(jzazbz, colorSDRJzazbz, sdrHue * saturate(colorSDRJzazbzChrom * 2), sdrChrom, sdrChromLimit.x, sdrChromLimit.y);

    return JzAzBz::jzazbzToRgb(jzazbz, colorspace);
  }

  // Blowout ----------------------------------------------------------------------------------------
  
  float blowoutRolloff(float value, float ratio, float rangeStops) {
    float stops = log2(max(value, epsilon) / max(ratio, epsilon));
    float position = saturate(stops / max(rangeStops, epsilon));

    float positionSquared = position * position;
    return positionSquared * position * mad(position, mad(6.f, position, -15.f), 10.f);
  }

  float blowoutRolloffDelayed(float rolloff, float cubicBlend) {
    return rolloff * rolloff * mad(cubicBlend, rolloff, 1.f - cubicBlend);
  }

  float blowoutRetention(float rolloff, float strength) {
    return mad(-strength, rolloff, 1.f);
  }

  float3 blowoutJzAzBz(float3 jzazbz, float ratio, float lumStrength, float maxChannelStrength,
                      float rangeStops, float maxChannelDelay, uint colorspace = CS_BT709) {
    // pass 1: by luminance
    float3 rgb = JzAzBz::jzazbzToRgb(jzazbz, colorspace);
    float lumRolloff = blowoutRolloff(GetLuminance(rgb, colorspace), ratio, rangeStops);
    jzazbz.yz *= blowoutRetention(lumRolloff, lumStrength);

    // pass 2: by max channel
    rgb = JzAzBz::jzazbzToRgb(jzazbz, colorspace);
    float channelMax = max(rgb.r, max(rgb.g, rgb.b));
    float maxChannelRolloff = blowoutRolloff(channelMax, ratio, rangeStops);
    maxChannelRolloff = lerp(maxChannelRolloff,
                            blowoutRolloffDelayed(maxChannelRolloff, blowoutCubicBlend),
                            maxChannelDelay);
    jzazbz.yz *= blowoutRetention(maxChannelRolloff, maxChannelStrength);

    return jzazbz;
  }

  float3 blowout(float3 color, float ratio = 1.f, float strength = 0.9f, float maxChannelSplit = 0.45f,
                float rangeStops = blowoutRangeStops, float maxChannelDelay = 0.f, uint colorspace = CS_BT709) {
    strength = saturate(strength);

    float3 jzazbz = JzAzBz::rgbToJzazbz(color, colorspace);

    jzazbz = blowoutJzAzBz(jzazbz, ratio,
                        strength * (1.f - maxChannelSplit),
                        strength * maxChannelSplit,
                        rangeStops,
                        maxChannelDelay, 
                        colorspace);

    return JzAzBz::jzazbzToRgb(jzazbz, colorspace);
  }

  // Overshoot correction ---------------------------------------------------------------------

  float3 overshootCorrection(float3 color, float peak, float shoulder = 0.8f, float compressionStrength = 0.75f) {
    shoulder = clamp(shoulder, 0.01f, 0.99f);
    float shoulderLog2 = log2(shoulder);
    float3 shaped = log2(max(color / peak, 1e-10f));
    float3 rolled = anchoredCInfinityShoulder(shaped, 0.f, shoulderLog2, compressionStrength);
    rolled = min(shaped, min(rolled, 0.f));
    return lerp(peak * exp2(rolled), color, step(color, 0.f));
  }

  // Main ---------------------------------------------------------------------

  float3 pragmap(float3 color, float peak, float hueStrength = 0.6f, float blowoutStrength = 0.9f, uint colorspace = CS_BT709) {

    float y = GetLuminance(color, colorspace);

    float ratio = anchoredCInfinityShoulder(y, peak, toneAnchor, toneCompression) / y;
    float hueDriver = distanceRolloff(y, bbAnchor, bbRangeStops);

    color = hueShiftBezoldBrucke(color, hueDriver, hueStrength, colorspace);
    color = blowout(color, ratio, blowoutStrength, 0.45f, blowoutRangeStops, 0.f, colorspace);

    color *= anchoredCInfinityShoulder(y, peak, toneAnchor, toneCompression) / y;
    color = max(color, 0);

    return overshootCorrection(color, peak, overshootShoulder);
  }

} // namespace PragMap

#endif // __PRAGMAP__
