// The bloom SRV exposes mip 0; use filtered taps and fade samples at the screen edge.
float3 FlareTap(float2 uv)
{
  float edge = saturate(min(min(uv.x, uv.y), min(1.0 - uv.x, 1.0 - uv.y)) * 25.0);
  return max(t4.SampleLevel(s2, saturate(uv), 0).rgb, 0.0) * edge;
}

float3 ModernLensFlare(float2 uv, float exposure, float fade)
{
  uint width, height;
  t4.GetDimensions(width, height);
  float aspect = max((float)width, 1.0) / max((float)height, 1.0);
  float2 centered = (uv - 0.5) * float2(aspect, 1.0);
  float2 inverseAspect = float2(1.0 / aspect, 1.0);
  float3 ghosts = FlareTap(0.5 - centered * 0.65 * inverseAspect) * 0.40;
  ghosts += FlareTap(0.5 - centered * 1.25 * inverseAspect) * 0.25;
  float2 dx = float2(0.008 / aspect, 0.0);
  float3 streak = FlareTap(uv) * 0.04;
  [unroll] for (int i = 1; i <= 4; ++i)
    streak += (FlareTap(uv + dx * i) + FlareTap(uv - dx * i)) * (0.04 * (5.0 - i) / 5.0);
  float3 flare = (ghosts + streak) * exposure * saturate(1.0 - fade);
  float luminance = GetLuminance(flare);
  float peak = max(LumaSettings.PeakWhiteNits / max(LumaSettings.GamePaperWhiteNits, 1.0), 1.0);
  return flare / (1.0 + luminance / (0.25 * peak));
}
