// Film grain ported from RenoDX (MIT) https://github.com/clshortfuse/renodx/blob/main/src/shaders/effects.hlsl
// Only the non-colored ApplyFilmGrain path is included, renamed with a BL_ prefix.

#ifndef BL_FILM_GRAIN_HLSL
#define BL_FILM_GRAIN_HLSL

// Bartleson graininess curve (density <= 0 returns 0)
float BL_ComputeFilmGraininess(float density)
{
  if (density <= 0)
  {
    return 0; // Luminance can be negative, pow can be unsafe
  }
  float bof_d_over_c = 0.880f - (0.736f * density) - (0.003f * pow(density, 7.6f));
  return pow(10.f, bof_d_over_c);
}

// Hash based uniform random in [0, 1) (from renodx/random.hlsl)
float BL_FilmGrainRandom(float2 uv)
{
  return frac(sin(dot(uv, float2(12.9898, 78.233))) * 43758.5453);
}

float3 BL_ApplyFilmGrain(float3 color, float2 xy, float seed, float strength, float reference_white = 1.f)
{
  const float random_number = BL_FilmGrainRandom(xy + seed);

  // Film grain is based on film density; grain scales with luminance (Y)
  const float3 signs = sign(color);
  color = abs(color);
  const float color_y = dot(color, float3(0.2126f, 0.7152f, 0.0722f));

  const float adjusted_color_y = color_y * (1.f / reference_white);

  // Ideal film density matches 0-3
  const float density = adjusted_color_y * 3.f;

  const float graininess = BL_ComputeFilmGraininess(density);
  const float random_factor = (random_number * 2.f) - 1.f;
  const float boost = 1.667f; // Grain amplitude multiplier

  const float y_change = random_factor * graininess * strength * boost;
  return signs * color * (1.f + y_change);
}

#endif // BL_FILM_GRAIN_HLSL
