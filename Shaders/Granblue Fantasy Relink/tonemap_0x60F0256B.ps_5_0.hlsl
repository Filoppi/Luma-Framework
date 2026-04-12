#include "Includes/Tonemap.hlsli"

SamplerState g_TextureAdaptLumminanceSampler_s : register(s2);
SamplerState g_TextureBloomSampler_s : register(s3);
SamplerState g_TextureSceneColorHDRSampler_s : register(s4);
Texture2D<float4> g_TextureAdaptLumminance : register(t2);
Texture2D<float4> g_TextureBloom : register(t3);
Texture2D<float4> g_TextureSceneColorHDR : register(t4);

#define cmp -

// float ApplyVanillaToneMap(float untonemapped) {
//   float r0, r1, r2;
//   r0 = untonemapped;

//   r1 = r0 * 0.15 + 0.05;
//   r1 = r0 * r1 + 0.004;
//   r2 = r0 * 0.15 + 0.5;
//   r0 = r0 * r2 + 0.06;
//   r0 = r1 / r0;
//   r0 = -0.0666666701 + r0;
//   r0 *= 1.37906432;

//   return r0;
// }

// float3 ApplyVanillaToneMap(float3 untonemapped) {
//   float3 r0, r1, r2;
//   r0.rgb = untonemapped;

//   r1.xyz = r0.xyz * 0.15 + 0.05;
//   r1.xyz = r0.xyz * r1.xyz + 0.004;
//   r2.xyz = r0.xyz * 0.15 + 0.5;
//   r0.xyz = r0.xyz * r2.xyz + 0.06;
//   r0.xyz = r1.xyz / r0.xyz;
//   r0.xyz = -0.0666666701 + r0.xyz;
//   r0.rgb *= 1.37906432;

//   return r0;
// }

void main(float4 v0: SV_Position0, float2 v1: TEXCOORD0, out float4 o0: SV_Target0)
{
   float4 r0, r1, r2;

   r0.xyz = g_TextureSceneColorHDR.Sample(g_TextureSceneColorHDRSampler_s, v1).xyz;
#if TONEMAP_AFTER_TAA
   o0.xyzw = float4(linear_to_sRGB_gamma(r0.xyz, GCT_MIRROR), 1);
   return;
#endif
   r1.xyz = (int3)r0.xyz & int3(0x7f800000, 0x7f800000, 0x7f800000);
   r1.xyz = cmp((int3)r1.xyz == int3(0x7f800000, 0x7f800000, 0x7f800000));
   r0.w = (int)r1.y | (int)r1.x;
   r0.w = (int)r1.z | (int)r0.w;

   r0.xyz = r0.www ? float3(1000000, 1000000, 1000000) : r0.xyz;

   r0.w = g_TextureAdaptLumminance.Sample(g_TextureAdaptLumminanceSampler_s, float2(0.5, 0.5)).x;
   r0.xyz = r0.www * r0.xyz;

   [branch]
   if (LumaSettings.DisplayMode == 0)
   {
      r1.xyz =
          r0.xyz * float3(0.150000006, 0.150000006, 0.150000006) + float3(0.0500000007, 0.0500000007, 0.0500000007);
      r1.xyz = r0.xyz * r1.xyz + float3(0.00400000019, 0.00400000019, 0.00400000019);
      r2.xyz = r0.xyz * float3(0.150000006, 0.150000006, 0.150000006) + float3(0.5, 0.5, 0.5);
      r0.xyz = r0.xyz * r2.xyz + float3(0.0599999987, 0.0599999987, 0.0599999987);
      r0.xyz = r1.xyz / r0.xyz;
      r0.xyz = float3(-0.0666666701, -0.0666666701, -0.0666666701) + r0.xyz;
   }

   r1.xyz = g_TextureBloom.Sample(g_TextureBloomSampler_s, v1.xy).xyz;
   r0.xyz += r1.xyz; // additive bloom, matching vanilla post-exposure application

   [branch]
   if (LumaSettings.DisplayMode == 1)
   {
      float3 color = r0.rgb;

      color.rgb = ApplyUserGradingAndToneMap(color.rgb, float2(0, 0));
      o0 = float4(color.rgb, 1.0);
      return;
   }

   r0.xyz = saturate(r0.xyz * float3(1.37906432, 1.37906432, 1.37906432) + r1.xyz);

   // o0.rgb = renodx::draw::RenderIntermediatePass(r0.rgb);
   o0.rgb = r0.rgb;

   o0.rgb = linear_to_sRGB_gamma(r0.rgb, GCT_SATURATE);

   o0.w = 1;

   // return;

   // r1.xyz = log2(r0.xyz);
   // r1.xyz = float3(0.416666657,0.416666657,0.416666657) * r1.xyz;
   // r1.xyz = exp2(r1.xyz);
   // r1.xyz = r1.xyz * float3(1.05499995,1.05499995,1.05499995) + float3(-0.0549999997,-0.0549999997,-0.0549999997);
   // r2.xyz = cmp(float3(0.00313080009,0.00313080009,0.00313080009) >= r0.xyz);
   // r0.xyz = float3(12.9200001,12.9200001,12.9200001) * r0.xyz;
   // o0.xyz = r2.xyz ? r0.xyz : r1.xyz;
   // o0.w = 1;
   // return;
}
