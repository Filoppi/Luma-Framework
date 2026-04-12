#include "Includes/Common.hlsl"
#include "../Includes/Oklab.hlsl"

cbuffer ParamBuffer : register(b1)
{
   float4 g_Param : packoffset(c0);
}

SamplerState g_Texture0Sampler_s : register(s0);
Texture2D<float4> g_Texture0 : register(t0);

#define cmp -

void main(float4 v0: SV_Position0, float2 v1: TEXCOORD0, out float4 o0: SV_Target0)
{
   float4 r0, r1, r2, r3;
   float4 texSample = g_Texture0.Sample(g_Texture0Sampler_s, v1.xy);

#if TONEMAP_AFTER_TAA
   // The late native cutscene gamma + color grade path handles this work after TAA/DLSS.
   o0 = texSample;
   return;
#endif

   float3 color = texSample.rgb;
   [branch]
   if (LumaSettings.DisplayMode > 0)
   {
      color = gamma_sRGB_to_linear(color, GCT_MIRROR); // convert to linear for grading math
      color = Oklab::linear_srgb_to_oklch(color.rgb);

      color.z += g_Param.x * 0.00370370364f;
      color.y += g_Param.y;
      color.x += g_Param.z;

      color = Oklab::oklch_to_linear_srgb(color);
      color = linear_to_sRGB_gamma(color, GCT_MIRROR); // convert back to sRGB gamma for output
   }
   else
   {
      r0.zw = float2(-1, 0.666666985);
      r1.zw = float2(0, -0.333332986);
      r2.xyzw = texSample.xyzw;
      r0.xy = r2.zy;
      r1.xy = r0.yx;
      r2.y = cmp(r0.y < r2.z);
      r0.xyzw = r2.yyyy ? r0.xyzw : r1.xyzw;
      r1.x = cmp(r2.x < r0.x);
      r3.xyz = r0.xyw;
      r3.w = r2.x;
      o0.w = r2.w;
      r0.xyw = r3.wyx;
      r0.xyzw = r1.xxxx ? r3.xyzw : r0.xyzw;
      r1.x = min(r0.w, r0.y);
      r1.x = -r1.x + r0.x;
      r1.y = r1.x * 6 + 1.00000001e-10;
      r1.y = rcp(r1.y);
      r0.y = r0.w + -r0.y;
      r0.y = r0.y * r1.y + r0.z;
      r0.x = -r1.x * 0.5 + r0.x;
      r0.y = g_Param.x * 0.00370370364 + abs(r0.y);
      r0.yzw = r0.yyy * float3(6, 6, 6) + float3(-3, -2, -4);
      r0.yzw = saturate(abs(r0.yzw) * float3(1, -1, -1) + float3(-1, 2, 2));
      r0.yzw = float3(-0.5, -0.5, -0.5) + r0.yzw;
      r1.y = r0.x * 2 + -1;
      r0.x = g_Param.z + r0.x;
      r1.y = 1 + -abs(r1.y);
      r1.y = rcp(r1.y);
      r1.x = saturate(r1.x * r1.y);
      r1.x = g_Param.y + r1.x;
      r1.y = r0.x * 2 + -1;
      r1.y = 1 + -abs(r1.y);
      r1.x = r1.y * r1.x;
      color.xyz = r0.yzw * r1.xxx + r0.xxx;
   }
   o0 = float4(color.rgb, texSample.a);

   return;
}
