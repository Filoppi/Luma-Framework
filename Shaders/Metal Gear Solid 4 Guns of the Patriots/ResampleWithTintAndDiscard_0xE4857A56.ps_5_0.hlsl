#include "../Includes/Common.hlsl"

#ifndef ENABLE_IMPROVED_BLUR
#define ENABLE_IMPROVED_BLUR 1
#endif

Texture2D<float4> SourceTexture : register(t0);
SamplerState SourceSampler : register(s0);

cbuffer cb0 : register(b0)
{
  float4 parameters;
}

void main(
  float4 position    : SV_POSITION,
  float4 vertexColor : COLOR0,
  float2 uv          : TEXCOORD0,
  out float4 output  : SV_TARGET)
{
  float4 sourceColor;
  uint sourceWidth, sourceHeight;
  SourceTexture.GetDimensions(sourceWidth, sourceHeight);
  float2 sourceResolution = float2(sourceWidth, sourceHeight);

  float2 uvDx = ddx(uv);
  float2 uvDy = ddy(uv);
#if ENABLE_IMPROVED_BLUR
  float2 outputPixelSize = abs(uvDx) + abs(uvDy);
  float2 sourceTexelsPerPixel = outputPixelSize * sourceResolution;

  // Approximate check to avoid area resampling when the source is smaller or equal the target resolution
  [branch]
  if (any(sourceTexelsPerPixel > 1.001))
  {
    float2 targetResolution = rcp(outputPixelSize);
    sourceColor = SampleArea(SourceTexture, SourceSampler, uv, sourceResolution, targetResolution, 0.0, 1.0, false);
  }
  else
#endif // ENABLE_IMPROVED_BLUR
  {
    sourceColor = SourceTexture.SampleGrad(SourceSampler, uv, uvDx, uvDy);
  }

  // Zero means "use the texture alpha"; otherwise vertex alpha overrides it.
  float alpha = vertexColor.a == 0.0 ? sourceColor.a : vertexColor.a;

  // Generally does nothing if it's 0 (which is often)
  float encodedThreshold = parameters.x;

  static const float ModeSplit = 128.0 / 255.0;
  if (encodedThreshold >= ModeSplit)
  {
    // High half encodes an inverted alpha test.
    float threshold = encodedThreshold - ModeSplit;
    if (alpha >= threshold)
      discard;
  }
  else
  {
    if (alpha < encodedThreshold)
      discard;
  }

  // The output is using a subtractive blend. In HDR bloom was unclamped and thus has much higher values.
  // Scaling it by half (and then manually clamping to >= 0 values, given the RT is float),
  // roughly brings back the original look.
  if (LumaData.CustomData1)
  {
    sourceColor.rgb *= 0.5;
  }

  output.rgb = sourceColor.rgb * vertexColor.rgb;
  output.a   = alpha;
  
#if 0 // Test the exposure texture that is generated based on this
  float2 centerDistance = abs(uv * 2.0 - 1.0);
#if 0 // Circular falloff
  output = 1.0 - smoothstep(0.0, 1.0, length(centerDistance));
#else // Linear (per axis) falloff
  float2 axisFalloff = 1.0 - smoothstep(0.0, 1.0, centerDistance);
  output = axisFalloff.x * axisFalloff.y;
#endif
  output.a = 0.25; // Neutral RGB encoding
#endif
}
