// Shared helpers for the Hitman Absolution deferred light pixel shaders.
#ifndef DEFERRED_LIGHT_COMMON_HLSL
#define DEFERRED_LIGHT_COMMON_HLSL

#if TONEMAP_TYPE >= 1
// Clamps a G-buffer read to [0,1] like the vanilla 8-bit UNORM targets; Luma's fp16 targets can hold values above 1 that produce negative light.
#define GBUFFER_CLAMP(v) saturate(v)
// Floors a value used under rsqrt so zero vectors cannot produce inf/NaN.
#define SAFE_MIN(x) max((x), 1e-6)
// Sign-preserving floor for a value used as a divisor (w divide, depth reconstruction denominator).
#define SAFE_W(x) (((x) < 0.0) ? min((x), -1e-6) : max((x), 1e-6))
#else
#define GBUFFER_CLAMP(v) (v)
#define SAFE_MIN(x) (x)
#define SAFE_W(x) (x)
#endif

// Fog-density AO: the AO term is hidden as much as the fog hides the surface (TONEMAP_TYPE >= 1 only).
float3 ApplyFogDensityToAO(float3 ao, float aoDarkness, Texture2D<float4> fogDensity, SamplerState fogSampler, float2 uv)
{
  ao = lerp(1.0, ao, aoDarkness);
  uint fogW, fogH;
  fogDensity.GetDimensions(fogW, fogH);
  if (fogW > 0)
  {
    float fogAlpha = saturate(fogDensity.Sample(fogSampler, uv).w);
    fogAlpha *= fogAlpha; // the fog pass blends with density squared
    ao = lerp(ao, 1.0, fogAlpha); // AO sits under the fog: hide it as much as the fog hides the surface
  }
  return ao;
}

#endif
