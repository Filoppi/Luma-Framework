#include "Includes/Common.hlsl"

Texture2D<float4> g_Texture0 : register(t0);

void main(float4 v0: SV_Position0, out float4 o0: SV_Target0)
{
   const int3 uv = int3(v0.xy, 0);
   float4 color = g_Texture0.Load(uv);
   color.rgb = gamma_sRGB_to_linear(color.rgb, GCT_MIRROR);
   o0 = color;
}
