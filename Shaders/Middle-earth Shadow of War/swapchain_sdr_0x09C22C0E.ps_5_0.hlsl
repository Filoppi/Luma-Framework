// ---- Created with 3Dmigoto v1.3.16 on Thu Jul 16 19:31:12 2026

cbuffer CBuffer_Data : register(b0)
{
  float2 TexCoordScale0 : packoffset(c0);
  float2 MaxTexCoord0 : packoffset(c0.z);
  float2 Gamma : packoffset(c1);
  float2 RenderResolution : packoffset(c1.z);
  float2 SwapchainResolution : packoffset(c2);
  float2 idk1 : packoffset(c2.z);

  /*
  1
  1
  0.999857
  0.999745

  1
  6.25
  960
  540

  3840
  2160
  0
  0
  */
}

SamplerState TextureSampler_s : register(s0);
Texture2D<float4> SourceBuffer0 : register(t0);
Texture2D<float4> SourceBuffer1 : register(t1);

Texture2D<float2> Dummy; //TODO: take in MVs for dynamic RCAS?


// 3Dmigoto declarations
#define cmp -
#include "./common.hlsl"
#include "../Includes/RCAS.hlsl"

float3 GameGamma(float3 x) {
  float3 r0, r1, r2;
  r0.xyz = x;
  r0.xyz = log2(r0.xyz);
  r0.xyz = Gamma.xxx * r0.xyz;
  r1.xyz = float3(0.416666657,0.416666657,0.416666657) * r0.xyz;
  r0.xyz = exp2(r0.xyz);
  r1.xyz = exp2(r1.xyz);
  r1.xyz = r1.xyz * float3(1.05499995,1.05499995,1.05499995) + float3(-0.0549999997,-0.0549999997,-0.0549999997);
  r2.xyz = cmp(r0.xyz < float3(0.00313080009,0.00313080009,0.00313080009));
  r0.xyz = float3(12.9200001,12.9200001,12.9200001) * r0.xyz;
  return r2.xyz ? r0.xyz : r1.xyz;
}

void main(
  float4 v0 : SV_POSITION0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_TARGET0)
{
  float4 r0,r1,r2;
  uint4 bitmask, uiDest;
  float4 fDest;

  // texcoords (in subnative res, color smaller top left)
  r0.xy = TexCoordScale0.xy * v1.xy;
  r0.xy = min(MaxTexCoord0.xy, r0.xy);

  //// GAME ////

  #if RCAS_ENABLED == 0
    float3 game = SourceBuffer0.SampleLevel(TextureSampler_s, r0.xy, 0).xyz;
  #else
    float3 game = RCAS(
      r0.xy * RenderResolution,
      int2(0,0),
      int2(RenderResolution),
      GS.RCAS,
      SourceBuffer0,
      Dummy,
      GamePaperWhiteNits / ITU_WhiteLevelNits
    ).xyz;
    game = max(game, 0);
  #endif

  if (!HDR_ENABLED)
  {
    game = sRGB_Decode(game); //decode gamma slider
  } else {
    #if GAMMA_CORRECT_CUSTOM == 1
      game.xyz = sRGB_Encode(game.xyz);
      game.xyz = game.xyz < 1 ? pow(game.xyz, 2.2) : sRGB_Decode(game.xyz);
    #endif

    game *= HDR_INTSCALING;
  }

  //// UI //// 
  float4 ui = SourceBuffer1.SampleLevel(TextureSampler_s, v1.xy, 0).xyzw;
  ui.xyzw = max(0, ui.xyzw);
  ui.w = min(ui.w, 1);

  float3 uiBack = ui.xyz;
  ui.xyz = sRGB_Decode(ui.xyz);

  if (HDR_ENABLED) {
    #if GAMMA_CORRECT_CUSTOM == 1
      ui.xyz = ui.xyz < 1 ? pow(uiBack, 2.2) : ui.xyz;
    #endif
  } else {
    ui.xyz = GameGamma(ui.xyz);
    ui.xyz = sRGB_Decode(ui.xyz);
  }

  //// COMBINE ////
  ui.xyz = sRGB_Encode(ui.xyz); // TODO: or 2.2? prob doesnt matter
  game = sRGB_Encode(game);
  float3 x = game * (1 - ui.w) + ui.xyz; //in gamma space
  x = sRGB_Decode(x);
  if (HDR_ENABLED) x = min(HDR_PEAK * HDR_INTSCALING, x); //peak clamp

  //// TEST_USER_PEAK ////
  #if TEST_USER_PEAK == 1
    x = 0;
	  x = DrawRect(v1.xy, float4(0.35, 0.47, 0.65, 0.53),     x, 10000.f/UIPaperWhiteNits);
	  x = DrawRect(v1.xy, float4(0.365, 0.483, 0.448, 0.517), x, PeakWhiteNits*2/UIPaperWhiteNits);
	  x = DrawRect(v1.xy, float4(0.458, 0.483, 0.542, 0.517), x, PeakWhiteNits/UIPaperWhiteNits);
	  x = DrawRect(v1.xy, float4(0.552, 0.483, 0.635, 0.517), x, PeakWhiteNits/2/UIPaperWhiteNits);
  #endif
  
  o0.xyz = x;
  o0.w = 1;
  return;
}

// void main(
//   float4 v0 : SV_POSITION0,
//   float2 v1 : TEXCOORD0,
//   out float4 o0 : SV_TARGET0)
// {
//   float4 r0,r1,r2;
//   uint4 bitmask, uiDest;
//   float4 fDest;
// 
//   r0.xyzw = SourceBuffer1.SampleLevel(TextureSampler_s, v1.xy, 0).xyzw;
//   r0.w = 1 + -r0.w;
//   r0.xyz = log2(r0.xyz);
//   r0.xyz = Gamma.xxx * r0.xyz;
//   r1.xyz = float3(0.416666657,0.416666657,0.416666657) * r0.xyz;
//   r0.xyz = exp2(r0.xyz);
//   r1.xyz = exp2(r1.xyz);
//   r1.xyz = r1.xyz * float3(1.05499995,1.05499995,1.05499995) + float3(-0.0549999997,-0.0549999997,-0.0549999997);
//   r2.xyz = cmp(r0.xyz < float3(0.00313080009,0.00313080009,0.00313080009));
//   r0.xyz = float3(12.9200001,12.9200001,12.9200001) * r0.xyz;
//   r0.xyz = r2.xyz ? r0.xyz : r1.xyz;
// 
//   r1.xy = TexCoordScale0.xy * v1.xy;
//   r1.xy = min(MaxTexCoord0.xy, r1.xy);
//   r1.xyz = SourceBuffer0.SampleLevel(TextureSampler_s, r1.xy, 0).xyz;
//   #if RCAS_ENABLED == 1
//     r1.xyz = RCAS(r1.xyz, SourceBuffer0, TextureSampler_s, r0.xy, GS.RCAS, 1);
//     r1.xyz = max(r1.xyz, 0);
//   #endif
// 
//   o0.xyz = r1.xyz * r0.www + r0.xyz;
//   o0.w = 1;
//   return;
// }
