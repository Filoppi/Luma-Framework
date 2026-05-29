// ---- Created with 3Dmigoto v1.3.16 on Sun Aug 31 23:17:54 2025

SamplerState g_sampler_s : register(s0);
Texture2D<float4> g_texture : register(t0);


// 3Dmigoto declarations
#define cmp -
#include "./common1.hlsl"

void Check(float2 uv, inout float4 o0) {
   // ignore bg sprites
   if (!TonemapInfo::GetDrawnFinal(GS.TonemapInfo)) return;

  //size
  float w;
  float h;
  g_texture.GetDimensions(w, h);

  //health bar (both versions, main and flashing after HPDelta)
  if (
    (w == 2048.f && h == 512.f)
    && CheckCustom(g_texture.Load(int3(937, 242, 1)).xy, float2(0.34902f, 0.32157f), 0.00001f)
    && CheckCustom(g_texture.Load(int3(938, 242, 1)).xy, float2(0.94902f, 0.45142f), 0.00001f)
  ) { o0.xyz *= GS.HUDBrightnessHealthBar * (!TonemapInfo::GetDrawnHPBarDelta(GS.TonemapInfo) ? 1 : 0.2); return; }

  //held notes combo bg
  if (
    true
    && TonemapInfo::GetDrawnFinal(GS.TonemapInfo) 
    && !TonemapInfo::GetDrawnHPBarDelta(GS.TonemapInfo)
    && (w == 1024.f && h == 2048.f) 
    && (uv.x >= 0 && uv.x <= 515.01f / 1024.f)
    && (uv.y >= 1984.f / 2048.f && uv.x <= 1)
    #if CUSTOM_HUDBRIGHTNESS == 1
      && CheckCustom(g_texture.Load(int3(0, 37, 1)).xy, float2(0.06667f, 0.63137f), 0.00001f)
      && CheckCustom(g_texture.Load(int3(0, 38, 1)).xy, float2(0.50140f, 0.50196f), 0.00001f)
    #elif CUSTOM_HUDBRIGHTNESS == 2
      && CheckCustom(g_texture.Load(int3(4, 992, 1)).xy, float2(0.51244f, 0.47059f), 0.00001f)
      && CheckCustom(g_texture.Load(int3(5, 992, 1)).xy, float2(0.56078f, 0.31678f), 0.00001f)
    #endif
  ) { o0.xyz *= GS.HUDBrightnessHoldComboBg; return; }

  //Common
  if (
    true
    && TonemapInfo::GetDrawnFinal(GS.TonemapInfo)
    && (w == 2048.f && h == 2048.f)
    #if CUSTOM_HUDBRIGHTNESS == 1
      && CheckCustom(g_texture.Load(int3(0, 0, 1)).xy, float2(0.49412f, 0.50196f), 0.00001f)
      && CheckCustom(g_texture.Load(int3(0, 1, 1)).xy, float2(0.39583f, 0.68214f), 0.00001f)
    #elif CUSTOM_HUDBRIGHTNESS == 2
      && CheckCustom(g_texture.Load(int3(805, 22, 1)).xy, float2(0.49804f, 0.49804f), 0.00001f)
      && CheckCustom(g_texture.Load(int3(805, 23, 1)).xy, float2(0.72157f, 0.45570f), 0.00001f)
    #endif
    && (
      //Song name icon
      (uv.x >= 1906 / 2048.f && uv.x <= 1944 / 2048.f
      && uv.y >= 44 / 2048.f && uv.y <= 86 / 2048.f)
    || 
      //Progress BG
      (uv.x >= 0 / 2048.f && uv.x <= 1907 / 2048.f
      && uv.y >= 1425 / 2048.f && uv.y <= 1472 / 2048.f)
    || 
      //Life <3
      (uv.x >= 1925 / 2048.f && uv.x <= 2020 / 2048.f
      && uv.y >= 1747 / 2048.f && uv.y <= 1769 / 2048.f)
    || 
      //Lyrics BG
      (uv.x >= 0 / 2048.f && uv.x <= 1922 / 2048.f
      && uv.y >= 1475 / 2048.f && uv.y <= 2048 / 2048.f)
    || 
      //Safety
      (uv.x >= 1927 / 2048.f && uv.x <= 2016 / 2048.f
      && uv.y >= 1474 / 2048.f && uv.y <= 1492 / 2048.f)
    )
  ) { o0.xyz *= GS.HUDBrightnessCommonIcons; return; }

  //PJDLogo
  if (
    GS.HUDBrightnessPJDLogo != 1.f
    && !TonemapInfo::GetDrawnHPBarDelta(GS.TonemapInfo)
    && (w == 512.f && h == 1024.f)
    && (uv.x > 411 / 512.f && uv.y > 768 / 1024.f)
    && (uv.x < 507 / 512.f && uv.y < 884 / 1024.f)
    && CheckCustom(g_texture.Load(int3(129, 14, 1)).xy, float2(0.55560f, 0.08235f), 0.00001f)
    && CheckCustom(g_texture.Load(int3(130, 14, 1)).xy, float2(0.57647f, 0.38441f), 0.00001f)
  ) { o0.xyzw *= GS.HUDBrightnessPJDLogo; return; }

  //Stage Clear
  if (
    true
    && TonemapInfo::GetDrawnHPBarDelta(GS.TonemapInfo)
    && (w == 1024.f && h == 2048.f)
    && (uv.x >= 0 / 1024.f && uv.x <= 238 / 1024.f)
    && (uv.y >= 189 / 2048.f && uv.y <= 220 / 2048.f)
    #if CUSTOM_HUDBRIGHTNESS == 1
      && CheckCustom(g_texture.Load(int3(0, 37, 1)).xy, float2(0.06667f, 0.63137f), 0.00001f)
      && CheckCustom(g_texture.Load(int3(0, 38, 1)).xy, float2(0.50140f, 0.50196f), 0.00001f)
    #elif CUSTOM_HUDBRIGHTNESS == 2
      && CheckCustom(g_texture.Load(int3(4, 992, 1)).xy, float2(0.51244f, 0.47059f), 0.00001f)
      && CheckCustom(g_texture.Load(int3(5, 992, 1)).xy, float2(0.56078f, 0.31678f), 0.00001f)
    #endif
  ) { o0.xyzw *= GS.HUDBrightnessCommonIcons; return; }
    
  // progress bar 
  if (
    (
      true
      && TonemapInfo::GetDrawnHPBarDelta(GS.TonemapInfo)
      && w == 16.f && h == 32.f 
      // && CheckCustom(g_texture.Load(int3(0, 3, 1)).xy, float2(0.50196f, 0.50196f), 0.00001f)
      && CheckCustom(g_texture.Load(int3(0, 4, 1)).xy, float2(0.44706f, 0.18431f), 0.00001f)
    )
  ) {o0.xyz *= GS.HUDBrightnessCommonIcons; return; }

  // progress bar underlay
  if (
    true
    && TonemapInfo::GetDrawnHPBarDelta(GS.TonemapInfo)
    && w == 2048.f && h == 512.f 
    && CheckCustom(g_texture.Load(int3(1204, 499, 0)).xy, float2(0.f, 0.1f), 0.00001f)
    && CheckCustom(g_texture.Load(int3(1204, 500, 0)).xy, float2(1.0f, 1.f), 0.00001f)
  ) { o0.xyz *= GS.HUDBrightnessCommonIcons; return; }

}

void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : COLOR0,
  float2 v2 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = g_texture.SampleLevel(g_sampler_s, v2.xy, 0).yx;  
  r1.w = r0.x;
  r2.xy = g_texture.SampleLevel(g_sampler_s, v2.xy, 1).xy;
  r0.xz = r2.yx * float2(1.00392163,1.00392163) + float2(-0.503929257,-0.503929257);

  r1.x = dot(float2(1.57480001,1), r0.xy);
  r1.y = dot(float3(-0.468100011,1,-0.187299997), r0.xyz);
  r1.z = dot(float2(1,1.8556), r0.yz);

  o0.xyzw = v1.xyzw * r1.xyzw;

#if CUSTOM_HUDBRIGHTNESS > 0
  Check(v2.xy, o0.xyzw);
#endif
  return;
}