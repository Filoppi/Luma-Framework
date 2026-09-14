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
  float4 sourceColor = SourceTexture.Sample(SourceSampler, uv);

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

  output.rgb = sourceColor.rgb * vertexColor.rgb;
  output.a   = alpha;
}