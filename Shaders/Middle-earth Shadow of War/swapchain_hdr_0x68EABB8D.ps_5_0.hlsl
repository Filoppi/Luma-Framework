// ---- Created with 3Dmigoto v1.3.16 on Tue Jul 14 19:50:52 2026

cbuffer CBuffer_Data : register(b0)
{
  float2 TexCoordScale0 : packoffset(c0);
  float2 MaxTexCoord0 : packoffset(c0.z);
  float2 Gamma : packoffset(c1);
}

SamplerState TextureSampler_s : register(s0);
Texture2D<float4> SourceBuffer0 : register(t0);
Texture2D<float4> SourceBuffer1 : register(t1);


// 3Dmigoto declarations
#define cmp -
#include "./common.hlsl"
#include "./Includes/RCAS.hlsl"

void main(
  float4 v0 : SV_POSITION0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_TARGET0)
{
  float4 r0,r1;
  uint4 bitmask, uiDest;
  float4 fDest;

// //   r0.xy = TexCoordScale0.xy * v1.xy;
// //   r0.xy = min(MaxTexCoord0.xy, r0.xy);
// //   r0.xyz = SourceBuffer0.SampleLevel(TextureSampler_s, r0.xy, 0).xyz; //idk black, hud mask?
// //   r1.xyzw = SourceBuffer1.SampleLevel(TextureSampler_s, v1.xy, 0).xyzw; //main color
// //   r1.w = 1 + -r1.w;
// // 
// //   r1.xyz = log2(r1.xyz);
// //   r1.xyz = Gamma.xxx * r1.xyz;
// //   r1.xyz = exp2(r1.xyz);
// //   r1.xyz = Gamma.yyy * r1.xyz;
// // 
// //   o0.xyz = r0.xyz * r1.www + r1.xyz;
// //   o0.w = 1;
// 
//   // texcoords
//   r0.xy = TexCoordScale0.xy * v1.xy;
//   r0.xy = min(MaxTexCoord0.xy, r0.xy);
// 
//   //// GAME ////
//   float3 game = SourceBuffer0.SampleLevel(TextureSampler_s, r0.xy, 0).xyz;
//   // game = Neupow(game, HDR_PEAK, 2); //rolloff
//   #if RCAS_ENABLED == 1
//     game = RCAS(game, SourceBuffer0, TextureSampler_s, r0.xy, GS.RCAS, GamePaperWhiteNits / ITU_WhiteLevelNits);
//   #endif
//   game = max(game, 0);
// 
//   #if GAMMA_CORRECT_CUSTOM == 1
//     game.xyz = sRGB_Encode(game.xyz);
//     game.xyz = game.xyz < 1 ? pow(game.xyz, 2.2) : sRGB_Decode(game.xyz);
//   #endif
// 
//   game *= HDR_INTSCALING;
// 
//   //// UI //// 
//   float4 ui = SourceBuffer1.SampleLevel(TextureSampler_s, v1.xy, 0).xyzw;
//   ui.w = saturate(ui.w);
//   ui.w = pow(ui.w, 2.2);
//   #if GAMMA_CORRECT_CUSTOM == 0
//     ui.xyz = sRGB_Decode(ui.xyz);
//   #else
//     ui.xyz = ui.xyz < 1 ? pow(ui.xyz, 2.2) : sRGB_Decode(ui.xyz);
//   #endif
//   ui = max(ui, 0);
// 
//   //// COMBINE ////
//   float3 x = game * (1 - ui.w) + ui.xyz;
//   x = min(HDR_PEAK * HDR_INTSCALING, x);
// 
//   //// TEST_USER_PEAK ////
//   #if TEST_USER_PEAK == 1
//     x = 0;
// 	  x = DrawRect(v1.xy, float4(0.35, 0.47, 0.65, 0.53),     x, 10000.f/UIPaperWhiteNits);
// 	  x = DrawRect(v1.xy, float4(0.365, 0.483, 0.448, 0.517), x, PeakWhiteNits*2/UIPaperWhiteNits);
// 	  x = DrawRect(v1.xy, float4(0.458, 0.483, 0.542, 0.517), x, PeakWhiteNits/UIPaperWhiteNits);
// 	  x = DrawRect(v1.xy, float4(0.552, 0.483, 0.635, 0.517), x, PeakWhiteNits/2/UIPaperWhiteNits);
//   #endif

  float3 x;

  //do a pink checkboard pattern
  if (fmod(floor(v1.x * 128) + floor(v1.y * 128), 2) == 0)
    x = float3(0.1, 0, 0.1);
  else
    x = float3(0, 0, 0);
  
  o0.xyz = x;
  o0.w = 1;
  return;
}