#include "../Includes/Color.hlsl"

/*
 * Decodes Quantum Break's gamma-space temporal resolve color into linear space
 * before super resolution so DLSS can run with its HDR/linear input flag.
 */
Texture2D<float4> g_PreSRGammaColor : register(t0);

void main(float4 pixel_position : SV_Position0, out float4 output_color : SV_Target0)
{
   const int3 pixel_coord = int3(pixel_position.xy, 0);
   const float4 gamma_color = g_PreSRGammaColor.Load(pixel_coord);

   float4 linear_color = gamma_color;
   linear_color.rgb = gamma_sRGB_to_linear(linear_color.rgb, GCT_POSITIVE);

   output_color = linear_color;
}
