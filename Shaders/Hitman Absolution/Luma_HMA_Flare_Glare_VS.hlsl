// Adapted from Pretty Post Process Glare.usf (GlareVS + GlareGS math; one blade per instance, no geometry shader). See Includes/HMAFlare.hlsl for license.
#include "Includes/HMAFlare.hlsl"

Texture2D<float4> t0 : register(t0); // mip1 (W/4 x H/4), border sampler s1

struct GlareVSOut
{
  float4 pos : SV_Position;
  float2 quadUV : TEXCOORD0;
  float3 color : TEXCOORD1;
};

// Clip-space position of one quad vertex (GlareGS ComputePosition).
float4 GlareComputePosition(float2 tilePos, float2 quadUV, float2 scale, float angle, float2 bufferSize)
{
  float2 bufferRatio = float2(bufferSize.y / bufferSize.x, 1.0);

  float2 bufferPosition = (tilePos - float2(0.25, 0.25)) / bufferSize;
  bufferPosition = 4.0 * bufferPosition - 1.0;

  float2 newPosition = 2.0 * (quadUV - 0.5);
  newPosition *= scale;

  float s = sin(angle);
  float c = cos(angle);
  float2 rotated = float2(newPosition.x * c - newPosition.y * s,
                          newPosition.x * s + newPosition.y * c);

  rotated *= bufferRatio;
  rotated += bufferPosition * float2(1.0, -1.0);

  return float4(rotated, 0.0, 1.0);
}

GlareVSOut main(uint vid : SV_VertexID, uint iid : SV_InstanceID)
{
  GlareVSOut o;
  o.pos = float4(2.0, 2.0, 2.0, 1.0); // outside clip by default (rejected blade)
  o.quadUV = float2(0.0, 0.0);
  o.color = float3(0.0, 0.0, 0.0);

  uint blade = iid & 3u;
  uint tile = iid >> 2;

  uint2 TileCount = uint2(GlareTile.xy);
  float2 BufferSize = GlareTile.zw;
  float2 PixelSize = float2(1.0, 1.0) / BufferSize;

  if (TileCount.x == 0u)
    return o;

  float2 TilePos = float2(tile % TileCount.x, tile / TileCount.x);
  float2 UV = TilePos / BufferSize * 2.0;

  // Five taps around the 2x2 block center (upstream GlareVS).
  static const float2 Coords[5] =
  {
    float2(-1.0, 1.0), float2(1.0, 1.0),
    float2(0.0, 0.0),
    float2(-1.0, -1.0), float2(1.0, -1.0)
  };
  static const float Weights[5] = { 0.175, 0.175, 0.3, 0.175, 0.175 };

  float2 CenterUV = UV + PixelSize * float2(-0.5, -0.5);
  float3 Color = float3(0.0, 0.0, 0.0);
  [unroll]
  for (int i = 0; i < 5; i++)
  {
    float2 CurrentUV = CenterUV + Coords[i] * PixelSize * 1.5;
    Color += Weights[i] * t0.SampleLevel(s1, CurrentUV, 0).rgb;
  }

  float Luminance = dot(Color, float3(1.0, 1.0, 1.0));
  float bladeScale = GlareScales[blade];

  // GlareGS gate: luminance > 0.1 and blade enabled.
  if (!(Luminance > 0.1) || !(bladeScale > 0.0001))
    return o;

  float2 PointUV = UV;
  float3 quadColor = Color * GlareTint.rgb * GlareTint.a * (GlareParams.x / 100.0);

  float LuminanceScale = saturate(Luminance / GlareParams.y);

  // Shrink toward screen borders.
  float Mask = length(PointUV - 0.5);
  Mask = 1.0 - saturate(Mask * 2.0);
  Mask = Mask * 0.6 + 0.4;

  float2 Scale = float2(LuminanceScale * Mask,
                        (1.0 / min(BufferSize.x, BufferSize.y)) * 4.0);

  float AngleOffset = (PointUV.x * 2.0 - 1.0) * 0.196349;
  static const float BladeAngles[4] = { 0.392699, 1.178097, 1.963495, 2.748893 };
  float QuadAngle = AngleOffset + BladeAngles[blade];

  // Quad vertex order: two triangles (matches the upstream strip V0,V1,V3,V2).
  static const float2 QuadUVs[6] =
  {
    float2(0.0, 0.0), float2(1.0, 0.0), float2(0.0, 1.0),
    float2(0.0, 1.0), float2(1.0, 0.0), float2(1.0, 1.0)
  };
  float2 quadUV = QuadUVs[vid % 6u];

  float2 QuadScale = Scale * bladeScale;

  o.pos = GlareComputePosition(TilePos, quadUV, QuadScale, QuadAngle, BufferSize);
  o.quadUV = quadUV;
  o.color = quadColor;
  return o;
}
