#ifndef __CURVE360__
#define __CURVE360__

// TheGreatHmmmmm's piecewise & smoothed Xenia Xbox 360 gamma curve

//==============================================================================
// Xbox 360 gamma emulation
//
// The Xbox 360 encoded with a 4-segment piecewise-linear (PWL) curve rather
// than sRGB or a pure power function. MCC drops that curve, so content authored
// against it reads flatter than intended.
//
// Three related pieces live here:
//   encode360Gamma  - the exact 360 PWL encode (reference; matches Xenia's
//                     PreSaturatedLinearToPWLGamma)
//   smooth360       - a quintic fit of the same curve, without the PWL's slope
//                     discontinuities at the segment joins
//   curve/correct360- S curve plus chrominance correction, reproducing the
//                     360 look while avoiding the banding the raw PWL causes
//==============================================================================

#include "../../Includes/Common.hlsl"

namespace Curve360 // Piecewise
{
  namespace Piecewise {
    // Segment breakpoints (input domain).
    static const float gamma360T1 = 64.0f / 1023.0f;   // ~0.062561
    static const float gamma360T2 = 128.0f / 1023.0f;  // ~0.125122
    static const float gamma360T3 = 512.0f / 1023.0f;  // ~0.500489

    // Slope / intercept per segment.
    static const float gamma360S1 = 1023.0f / 255.0f;   // ~4.011765
    static const float gamma360S2 = 1023.0f / 510.0f;   // ~2.005882
    static const float gamma360S3 = 1023.0f / 1020.0f;  // ~1.002941
    static const float gamma360I2 = 32.0f / 255.0f;     // ~0.125490
    static const float gamma360I3 = 64.0f / 255.0f;     // ~0.250980

    // Fourth segment passes through (T3, 192/255) and (1, 1).
    static const float gamma360Y3 = 192.0f / 255.0f;  // ~0.752941
    static const float gamma360S4 = (1.0f - gamma360Y3) / (1.0f - gamma360T3);  // ~0.4946
    static const float gamma360I4 = 1.0f - gamma360S4;                          // ~0.5054

    float Curve(float x) {
      float y = x * gamma360S1;
      y = lerp(y, x * gamma360S2 + gamma360I2, step(gamma360T1, x));
      y = lerp(y, x * gamma360S3 + gamma360I3, step(gamma360T2, x));
      y = lerp(y, x * gamma360S4 + gamma360I4, step(gamma360T3, x));
      y = lerp(y, sqrt(max(x, 0.0f)), step(1.0f, x));
      return y;
    }

    float3 Curve(float3 color) {
      color.r = Curve(color.r);
      color.g = Curve(color.g);
      color.b = Curve(color.b);
      return color;
    }
  }

  namespace Smooth // Smooth (quintic) Approximation
  {
    static const float smooth360C0 = 0.0064137f;
    static const float smooth360C1 = 3.9601974f;
    static const float smooth360C2 = -11.8895335f;
    static const float smooth360C3 = 21.3926067f;
    static const float smooth360C4 = -18.7037998f;
    static const float smooth360C5 = 6.2341155f;

    float Curve(float x) {
      float poly = smooth360C5;
      poly = poly * x + smooth360C4;
      poly = poly * x + smooth360C3;
      poly = poly * x + smooth360C2;
      poly = poly * x + smooth360C1;
      poly = poly * x + smooth360C0;

      return lerp(poly, pow(max(x, 0.0f), 1.0f / 2.2f), step(1.0f, x));
    }

    float3 Curve(float3 color) {
      color.r = Curve(color.r);
      color.g = Curve(color.g);
      color.b = Curve(color.b);
      return color;
    }
  } // namespace Smooth

  namespace LumaShape { // Luma Shaping
    static const float curveC1 = 0.622927f;
    static const float curveC2 = 1.470944f;
    static const float curveC3 = -1.587363f;
    static const float curveC4 = 0.493492f;

    float Curve(float x) {
      float poly = curveC4;
      poly = poly * x + curveC3;
      poly = poly * x + curveC2;
      poly = poly * x + curveC1;
      poly = poly * x;

      return lerp(poly, x, step(1.0f, x));
    }

    float3 Curve(float3 color) {
      color.r = Curve(color.r);
      color.g = Curve(color.g);
      color.b = Curve(color.b);
      return color;
    }
  } // namespace LumaShape

  float3 FullCorrect(float3 color) { // clamped BT709
    const float minLuminance = 1e-6f;
    float y = GetLuminance(color, CS_BT709);
    float ratio = (abs(y) > minLuminance) ? (LumaShape::Curve(y) / y) : LumaShape::curveC1;
    float3 luminanceCorrected = color * ratio;
    float3 chrominanceSource = Smooth::Curve(luminanceCorrected);
    chrominanceSource = pow(chrominanceSource, 2.2/* DefaultGamma */); // TODO: must be 2.2 or no?
    luminanceCorrected = RestoreChrominance(chrominanceSource, luminanceCorrected, CS_BT709); // TODO: orig uses UCS
    luminanceCorrected = max(0, luminanceCorrected);
    return luminanceCorrected;
  }
} // namespace Curve360

#endif // __CURVE360__
