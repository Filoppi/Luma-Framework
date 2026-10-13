#ifndef SRC_C_INFINITY_ROLLOFF_HLSL_
#define SRC_C_INFINITY_ROLLOFF_HLSL_

#include "Math.hlsl"

namespace Tonemap
{
namespace CInfinityRollOff
{

/// Piecewise rolloff: identity below anchor, asymptotic to peak above it.
/// C denotes differentiability class; C-infinity preserves continuity of all derivatives at the join.
/// Monotonic and concave down; compression_strength controls departure from identity.
/// Requires anchor < peak and compression_strength >= 1.
#define C_INFINITY_ROLLOFF_GENERATOR(T)                                                       \
   T Apply(T color, T peak, T anchor = 0.18f, float compression_strength = 1.5f)              \
   {                                                                                          \
      T shoulder_range = peak - anchor;                                                       \
      T distance_from_anchor = max(color - anchor, (T)0.f);                                   \
      T inverse_position = shoulder_range * rcp(compression_strength * distance_from_anchor); \
      T flat_weight = exp2(-inverse_position);                                                \
      T response_denominator = mad(compression_strength, inverse_position, flat_weight);      \
      return mad(shoulder_range, rcp(response_denominator), color - distance_from_anchor);    \
   }

C_INFINITY_ROLLOFF_GENERATOR(float)
C_INFINITY_ROLLOFF_GENERATOR(float3)

#undef C_INFINITY_ROLLOFF_GENERATOR

/// Returns a ratio-preserving RGB scale by rolling off the largest absolute channel.
float ComputeMaxChannelScale(float3 color, float peak, float anchor = 0.18f, float compression_strength = 1.5f)
{
   float max_channel = max3(abs(color));
   float new_max = Apply(max_channel, peak, anchor, compression_strength);
   return max_channel != 0.f ? new_max / max_channel : 1.f;
}

/// Compresses the largest absolute channel toward peak while preserving RGB ratios.
float3 MaxChannel(float3 color, float peak, float anchor = 0.18f, float compression_strength = 1.5f)
{
   return color * ComputeMaxChannelScale(color, peak, anchor, compression_strength);
}

} // namespace CInfinityRollOff
} // namespace Tonemap

#endif // SRC_C_INFINITY_ROLLOFF_HLSL_