#ifndef LUMA_QB_FILMGRAIN_HLSLI
#define LUMA_QB_FILMGRAIN_HLSLI

// Port of RenoDX ApplyFilmGrain(), created by shortfuse.
#include "../../Includes/Color.hlsl"

float GenerateRandom(float2 uv)
{
   return frac(sin(dot(uv, float2(12.9898f, 78.233f))) * 43758.5453f);
}

// Bartleson
// https://www.imaging.org/common/uploaded%20files/pdfs/Papers/2003/PICS-0-287/8583.pdf
float ComputeFilmGraininess(float density)
{
   if (density <= 0.f)
   {
      return 0.f;
   }

   float bof_d_over_c = 0.880f - (0.736f * density) - (0.003f * pow(density, 7.6f));
   return pow(10.f, bof_d_over_c);
}

float ComputeGrainedChange(float y, float2 xy, float seed, float strength, float reference_white)
{
   const float random_number = GenerateRandom(xy + seed);

   // Film grain is based on film density
   // Film works in negative, meaning black has no density
   // The greater the film density (lighter), more perceived grain
   // Simplified, grain scales with Y

   // Scaling is not not linear

   const float adjusted_y = y * (1.f / reference_white);

   // Emulate density from a chosen film stock (Removed)
   // float density = computeFilmDensity(adjustedColorY);

   // Ideal film density matches 0-3. Skip emulating film stock
   // https://www.mr-alvandi.com/technique/measuring-film-speed.html

   const float density = adjusted_y * 3.f;
   const float graininess = ComputeFilmGraininess(density);
   const float random_factor = mad(random_number, 2.f, -1.f);
   const float boost = 1.667f; // Boost max to 0.05

   return random_factor * graininess * strength * boost;
}

float3 ApplyFilmGrain(float3 color, float2 xy, float seed, float strength, float reference_white = 1.f, bool debug = false, float3x3 xyz_matrix = BT709_To_XYZ)
{
   float y = max(0.f, dot(color, xyz_matrix[1].rgb));
   float y_change = ComputeGrainedChange(y, xy, seed, strength, reference_white);

   if (debug)
      return abs(y_change).xxx;

   return color * (1.f + y_change);
}

#endif // LUMA_QB_FILMGRAIN_HLSLI
