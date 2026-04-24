#include "../Includes/Color.hlsl"

/*
 * Encodes the linear super-resolution output back to Quantum Break's
 * gamma-space temporal resolve contract before the original resolve shader.
 */
Texture2D<float4> g_PostSRLinearColor : register(t0);

void main(float4 pixel_position : SV_Position0, out float4 output_color : SV_Target0)
{
   const int3 pixel_coord = int3(pixel_position.xy, 0);
   const float4 linear_color = g_PostSRLinearColor.Load(pixel_coord);

   float4 gamma_color = linear_color;
   gamma_color.rgb = linear_to_sRGB_gamma(gamma_color.rgb, GCT_POSITIVE);

   output_color = gamma_color;
}
