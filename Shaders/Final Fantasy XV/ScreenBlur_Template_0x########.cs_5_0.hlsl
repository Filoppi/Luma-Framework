// Replacement of the game's screen blur (UI backdrop), two compute passes writing a rectangle of a half resolution texture:
// the first one downsamples the scene by 2 and blurs it horizontally, the second does the same vertically on its result.
// Define SCREEN_BLUR_VERTICAL for the second pass.
//
// These follow the upscaled post processing chain (see "DispatchScreenBlur"): when upscaling the destination is larger than the game's,
// the rectangle and gaussian are scaled with it to cover the same screen area, and the destination size gives the UVs.
// With a scale of 1 this matches the game shader, but it always needs our dispatch (64 threads along the blur axis, no border).

#include "Includes/Common.hlsl"

cbuffer ScreenBlurParams : register(b0)
{
  int4 g_TargetAreaXY : packoffset(c0); // Rectangle to write (x0, y0, x1, y1), in the game's destination texels
  float2 g_TextureInvRes : packoffset(c1); // Unused, we get it from the destination
  float2 g_GaussianParam : packoffset(c1.z); // The falloff of each pass (exp(-distance^2 * falloff)): x for the first, y for the second
}

SamplerState g_SampleLinear : register(s0);
Texture2D<float4> g_Input : register(t0);
RWTexture2D<float4> g_Output : register(u0);

#ifdef SCREEN_BLUR_VERTICAL
static const int GaussianRadius = 8;
#define GAUSSIAN_FALLOFF g_GaussianParam.y
#else
static const int GaussianRadius = 16;
#define GAUSSIAN_FALLOFF g_GaussianParam.x
#endif

[numthreads(64, 1, 1)]
void main(uint3 group_id : SV_GroupID, uint3 thread_id : SV_GroupThreadID)
{
  float2 scale = 1.0;
  if (LumaData.GameData.IsUpscaling != 0)
  {
    scale = 1.0 / LumaData.RenderResolutionScale.xy;
  }

  uint2 destination_size;
  g_Output.GetDimensions(destination_size.x, destination_size.y);

  const int2 rectangle_min = int2(floor(float2(g_TargetAreaXY.xy) * scale));
  const int2 rectangle_max = int2(ceil(float2(g_TargetAreaXY.zw) * scale));

#ifdef SCREEN_BLUR_VERTICAL
  const int2 pixel = rectangle_min + int2(group_id.x, group_id.y * 64 + thread_id.x);
  const float axis_scale = scale.y;
#else
  const int2 pixel = rectangle_min + int2(group_id.x * 64 + thread_id.x, group_id.y);
  const float axis_scale = scale.x;
#endif
  if (any(pixel >= rectangle_max))
  {
    return;
  }

  // Same shape of the gaussian in screen space, it spans more texels
  const int radius = (int)ceil(float(GaussianRadius) * axis_scale - 1e-4);
  const float falloff = GAUSSIAN_FALLOFF / (axis_scale * axis_scale);

  float4 sum = 0.0;
  float weight_sum = 0.0;
  for (int i = -radius; i <= radius; i++)
  {
    const float weight = exp(-float(i * i) * falloff);
#ifdef SCREEN_BLUR_VERTICAL
    const int2 tap = pixel + int2(0, i);
#else
    const int2 tap = pixel + int2(i, 0);
#endif
    const float2 uv = (float2(tap) + 0.5) / float2(destination_size);
    sum += g_Input.SampleLevel(g_SampleLinear, uv, 0.0) * weight;
    weight_sum += weight;
  }

  g_Output[pixel] = sum / weight_sum;
}
