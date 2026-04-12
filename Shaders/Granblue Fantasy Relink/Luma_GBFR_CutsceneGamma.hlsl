#include "Includes/Common.hlsl"

cbuffer ParamBuffer : register(b1)
{
   float4 g_Param : packoffset(c0);
}

SamplerState g_Texture0Sampler_s : register(s0);
Texture2D<float4> g_Texture0 : register(t0);

void main(
   float4 v0 : SV_Position0,
   out float4 o0 : SV_Target0)
{
   float2 uv = v0.xy / LumaSettings.SwapchainSize;
   float4 color = g_Texture0.Sample(g_Texture0Sampler_s, uv);

   float contrast = 1.01171875 * ((g_Param.y + 1.0) / (-g_Param.y + 1.01171875));

   color.rgb = g_Param.xxx + color.rgb;
   color.rgb = contrast.xxx * (color.rgb - 0.5.xxx) + 0.5.xxx;

   o0 = color;
}
