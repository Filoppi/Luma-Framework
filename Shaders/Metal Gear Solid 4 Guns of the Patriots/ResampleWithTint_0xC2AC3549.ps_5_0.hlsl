Texture2D<float4> SourceTexture : register(t0);
SamplerState SourceSampler : register(s0);

cbuffer cb0 : register(b0)
{
  float4 parameters;
}

void main(
  float4 position    : SV_POSITION,
  float2 uv          : TEXCOORD0,
  out float4 output  : SV_TARGET)
{
  float2 uvScale = parameters.zw;
  float2 uvOffset = parameters.xy;
  float2 remappedUV = uv.xy * uvScale + uvOffset;
  output.xyzw = SourceTexture.Sample(SourceSampler, remappedUV).xyzw;
}