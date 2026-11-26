// Decodes the game's gamma 2.2 scene (sign mirrored, HDR) for SMAA (see DrawSMAAForFXAA in main.cpp):
// uav0 = linear copy (HDR and negatives kept, alpha untouched) for the blending,
// uav1 = saturated gamma copy (alpha untouched) for the SDR-range edge detection.

#include "Includes/Common.hlsl"

Texture2D<float4> scene_tex : register(t0);
RWTexture2D<float4> linear_out : register(u0);
RWTexture2D<float4> gamma_sdr_out : register(u1);

// Soft shoulder above 0.8 (per channel) so edges above 1 stay distinguishable instead of saturating.
float SMAASoftShoulder(float c)
{
   c = max(c, 0.0);
   return c <= 0.8 ? c : 0.8 + 0.2 * (1.0 - exp(-(c - 0.8) / 0.2));
}

[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
   uint width, height;
   scene_tex.GetDimensions(width, height);
   if (dtid.x >= width || dtid.y >= height)
      return;

   float4 color = scene_tex.Load(int3(dtid.xy, 0));
   linear_out[dtid.xy] = float4(gamma_to_linear(color.rgb, GCT_MIRROR), color.a);
   gamma_sdr_out[dtid.xy] = float4(SMAASoftShoulder(color.r), SMAASoftShoulder(color.g), SMAASoftShoulder(color.b), color.a);
}
