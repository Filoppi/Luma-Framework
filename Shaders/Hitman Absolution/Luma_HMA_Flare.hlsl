// Luma HMA lens flare passes. One entry point per pass; compiled from this file by Source/Games/Hitman Absolution/HMAFlare.hpp.

#include "Includes/Common.hlsl"
#include "Includes/AutoExposure.hlsl"
#include "Includes/HMAFlare.hlsl"

cbuffer cb4 : register(b4)
{
  float4 cb4[102];
}

// Shared texture slots. Each entry point only uses the subset it needs.
Texture2D<float4> t0 : register(t0); // prepare: full-res linear HDR scene; downsample/halo/ghosts/kawase/glare VS: flare mip chain; mix: blurred feature
Texture2D<float4> t1 : register(t1); // ghosts: halo; mix: glare
Texture2D<float4> t3 : register(t3); // mix: game fade

// Upstream: Downsample.usf (13-tap, first mip from full-res scene) + soft threshold from PrettyPostProcess bloom input.
float4 prepare_ps(float4 pos : SV_Position) : SV_Target0
{
  float2 uv = pos.xy * OutSize.zw;
  float exposure = GetHMAExposure(cb4[41].z) * LumaSettings.GameSettings.Exposure;
  float2 px = 0.5 * OutSize.zw;

  // 13-tap downsample (same taps and weights as Downsample13) with per-sample max(scene, 0) * exposure.
  static const float2 Coords[13] =
  {
    float2(-1.0f, 1.0f), float2(1.0f, 1.0f),
    float2(-1.0f, -1.0f), float2(1.0f, -1.0f),

    float2(-2.0f, 2.0f), float2(0.0f, 2.0f), float2(2.0f, 2.0f),
    float2(-2.0f, 0.0f), float2(0.0f, 0.0f), float2(2.0f, 0.0f),
    float2(-2.0f, -2.0f), float2(0.0f, -2.0f), float2(2.0f, -2.0f)
  };

  static const float Weights[13] =
  {
    0.125f, 0.125f,
    0.125f, 0.125f,

    0.0555555f, 0.0555555f, 0.0555555f,
    0.0555555f, 0.0555555f, 0.0555555f,
    0.0555555f, 0.0555555f, 0.0555555f
  };

  float3 c = float3(0.0f, 0.0f, 0.0f);
  [unroll]
  for (int i = 0; i < 13; i++)
  {
    float2 tapUV = uv + Coords[i] * px;
    c += Weights[i] * (max(t0.Sample(s0, tapUV).rgb, 0.0f) * exposure);
  }

  // Soft threshold on max channel (fading knee).
  float T = ThresholdParams.x;
  float K = T * ThresholdParams.y;
  float m = max(c.r, max(c.g, c.b));
  float soft = clamp(m - T + K, 0.0f, 2.0f * K);
  soft = soft * soft / (4.0f * K + 1e-5f);
  float w = max(soft, m - T) / max(m, 1e-5f);
  float3 o = c * w;

  if (!IsFinite3(o))
    o = float3(0.0f, 0.0f, 0.0f);

  return float4(o, 1.0f);
}

// Upstream: Downsample.usf (13-tap CoD-style downsample, RenderDownsample).
float4 downsample_ps(float4 pos : SV_Position) : SV_Target0
{
  float2 uv = pos.xy * OutSize.zw;
  float3 c = Downsample13(t0, s1, uv, 0.5 * OutSize.zw);
  return float4(c, 1.0f);
}

// Adapted from Pretty Post Process Halo.usf (starburst term dropped, sb = 0). See Includes/HMAFlare.hlsl for license.
float4 halo_ps(float4 pos : SV_Position) : SV_Target0
{
  float2 uv = pos.xy * OutSize.zw;
  float2 sp = uv * 2.0 - 1.0;
  float aspect = InSize.x / InSize.y;

  float2 UV = SquareUV(uv, aspect);
  float2 FishUV = FisheyeUV(UV, HaloParams.z, 1.0);

  // Distortion vector toward the center (zero at the exact center to avoid NaN).
  float2 toCenter = float2(0.5, 0.5) - UV;
  float toCenterLen = length(toCenter);
  float2 HaloVector = (toCenterLen > 1e-6) ? (toCenter / toCenterLen) * HaloParams.x : float2(0.0, 0.0);

  // Halo mask
  float hm = saturate(distance(UV, float2(0.5, 0.5)) * 2.0);
  hm = smoothstep(HaloParams.y, 1.0, hm);

  // Screen border mask
  float border = DiscMask(sp) * DiscMask(sp * 0.8);
  border = border * 0.95 + 0.05;

  float cs = HaloParams2.x;
  float2 UVr = (FishUV - 0.5) * (1.0 + cs) + 0.5 + HaloVector;
  float2 UVg = FishUV + HaloVector;
  float2 UVb = (FishUV - 0.5) * (1.0 - cs) + 0.5 + HaloVector;

  float3 col;
  col.r = t0.SampleLevel(s1, UVr, 0).r;
  col.g = t0.SampleLevel(s1, UVg, 0).g;
  col.b = t0.SampleLevel(s1, UVb, 0).b;

  col *= border * hm * HaloParams.w;
  return float4(col, 0.0);
}

// Adapted from Pretty Post Process Ghosts.usf (starburst term omitted). See Includes/HMAFlare.hlsl for license.
float4 ghosts_ps(float4 pos : SV_Position) : SV_Target0
{
  float2 uv = pos.xy * OutSize.zw;
  float2 sp = uv * 2.0 - 1.0;
  float aspect = InSize.x / InSize.y;

  float intensity = GhostParams.x;
  float chromaShift = GhostParams.y;
  float compression = GhostParams.z;

  float2 SquareUVv = SquareUV(uv, aspect);
  float ScaleAlpha = length(SquareUVv - 0.5);
  float2 ScaleUV = (uv - 0.5) * (1.0 - compression * ScaleAlpha) + 0.5;

  float3 Color = float3(0.0, 0.0, 0.0);
  [unroll]
  for (int i = 0; i < 8; i++)
  {
    if (abs(GhostColors[i].a * GhostScales[i].x) > 0.0001)
    {
      float2 NewUV = (ScaleUV - 0.5) * GhostScales[i].x;

      // Local mask
      float DistanceMask = 1.0 - length(NewUV);
      float Mask = smoothstep(0.5, 0.9, DistanceMask);
      float Mask2 = smoothstep(0.75, 1.0, DistanceMask) * 0.95 + 0.05;

      Color.r += t0.SampleLevel(s1, NewUV * (1.0 + chromaShift) + 0.5, 0).r * GhostColors[i].r * Mask * Mask2;
      Color.g += t0.SampleLevel(s1, NewUV + 0.5, 0).g * GhostColors[i].g * Mask * Mask2;
      Color.b += t0.SampleLevel(s1, NewUV * (1.0 - chromaShift) + 0.5, 0).b * GhostColors[i].b * Mask * Mask2;
    }
  }

  float ScreenborderMask = DiscMask(sp * 0.9);
  Color = Color * ScreenborderMask * (intensity / 100.0) / 10.0;

  // Halo is added on top of the ghosts (upstream halo sampled at the output uv).
  Color += t1.SampleLevel(s0, uv, 0).rgb;

  return float4(Color, 0.0);
}

// Upstream: DualKawaseBlur.usf KawaseBlurDownsamplePS (dual Kawase down pass).
float4 kawase_down_ps(float4 pos : SV_Position) : SV_Target0
{
  float2 uv = pos.xy * OutSize.zw;
  float2 hp = 0.5 * OutSize.zw;

  float3 c = t0.Sample(s0, uv).rgb * 4.0f;
  c += t0.Sample(s0, uv + float2(-hp.x, hp.y)).rgb;
  c += t0.Sample(s0, uv + float2(hp.x, hp.y)).rgb;
  c += t0.Sample(s0, uv + float2(hp.x, -hp.y)).rgb;
  c += t0.Sample(s0, uv + float2(-hp.x, -hp.y)).rgb;
  c /= 8.0f;

  return float4(c, 1.0f);
}

// Upstream: DualKawaseBlur.usf KawaseBlurUpsamplePS (dual Kawase up pass).
float4 kawase_up_ps(float4 pos : SV_Position) : SV_Target0
{
  float2 uv = pos.xy * OutSize.zw;
  float2 hp = 0.5 * OutSize.zw;

  float3 c = t0.Sample(s0, uv + float2(-hp.x, hp.y)).rgb;
  c += t0.Sample(s0, uv + float2(hp.x, hp.y)).rgb;
  c += t0.Sample(s0, uv + float2(hp.x, -hp.y)).rgb;
  c += t0.Sample(s0, uv + float2(-hp.x, -hp.y)).rgb;

  c += t0.Sample(s0, uv + float2(-hp.x, 0.0f)).rgb * 2.0f;
  c += t0.Sample(s0, uv + float2(hp.x, 0.0f)).rgb * 2.0f;
  c += t0.Sample(s0, uv + float2(0.0f, hp.y)).rgb * 2.0f;
  c += t0.Sample(s0, uv + float2(0.0f, -hp.y)).rgb * 2.0f;
  c /= 12.0f;

  return float4(c, 1.0f);
}

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

// Adapted from Pretty Post Process Glare.usf (GlareVS + GlareGS math; one blade per instance, no geometry shader). See Includes/HMAFlare.hlsl for license.
GlareVSOut glare_vs(uint vid : SV_VertexID, uint iid : SV_InstanceID)
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

// Adapted from Pretty Post Process Glare.usf (GlarePS; procedural line mask replaces T_GlareLine). See Includes/HMAFlare.hlsl for license.
float4 glare_ps(float4 pos : SV_Position, float2 quadUV : TEXCOORD0, float3 color : TEXCOORD1) : SV_Target0
{
  // u runs along the blade, v across it.
  float along = saturate(1.0 - abs(2.0 * quadUV.x - 1.0));
  along = along * along;
  float across = saturate(1.0 - abs(2.0 * quadUV.y - 1.0));
  across = smoothstep(0.0, 1.0, across);

  float mask = along * across;
  return float4(mask * color, 1.0);
}

// Upstream: Mix.usf (blurred feature + glare composite, FlareTint, FlareIntensity). Gradient texture omitted (white).
float4 mix_ps(float4 pos : SV_Position) : SV_Target0
{
  float2 uv = pos.xy * OutSize.zw;

  float3 flare = t0.Sample(s0, uv).rgb;

  if (MixParams.y > 0.5f)
  {
    // Glare is W/2 x H/2 (half of the output size), so the UV offset is 2 * OutSize.zw.
    float2 gt = 2.0f * OutSize.zw;
    float3 g = t1.Sample(s0, uv + float2(-gt.x, gt.y)).rgb;
    g += t1.Sample(s0, uv + float2(gt.x, gt.y)).rgb;
    g += t1.Sample(s0, uv + float2(-gt.x, -gt.y)).rgb;
    g += t1.Sample(s0, uv + float2(gt.x, -gt.y)).rgb;
    flare += g * 0.25f;
  }

  float3 o = flare * FlareTint.rgb * MixParams.x;

  if (MixParams.z > 0.5f)
  {
    float4 fade = t3.Sample(s0, uv);
    o *= saturate(1.0f - fade.a);
  }

  if (!IsFinite3(o))
    o = float3(0.0f, 0.0f, 0.0f);

  return float4(o, 1.0f);
}
