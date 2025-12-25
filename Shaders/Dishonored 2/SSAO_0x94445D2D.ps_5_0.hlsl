// ---- Created with 3Dmigoto v1.3.16 on Wed Dec 10 11:59:55 2025

cbuffer PerInstanceCB : register(b2)
{
  float4 cb_dwao_params0 : packoffset(c0);
  float3 cb_dwao_params1 : packoffset(c1);
}

cbuffer PerViewCB : register(b1)
{
  float4 cb_alwaystweak : packoffset(c0);
  float4 cb_viewrandom : packoffset(c1);
  float4x4 cb_viewprojectionmatrix : packoffset(c2);
  float4x4 cb_viewmatrix : packoffset(c6);
  float4 cb_subpixeloffset : packoffset(c10);
  float4x4 cb_projectionmatrix : packoffset(c11);
  float4x4 cb_previousviewprojectionmatrix : packoffset(c15);
  float4x4 cb_previousviewmatrix : packoffset(c19);
  float4x4 cb_previousprojectionmatrix : packoffset(c23);
  float4 cb_mousecursorposition : packoffset(c27);
  float4 cb_mousebuttonsdown : packoffset(c28);
  float4 cb_jittervectors : packoffset(c29);
  float4x4 cb_inverseviewprojectionmatrix : packoffset(c30);
  float4x4 cb_inverseviewmatrix : packoffset(c34);
  float4x4 cb_inverseprojectionmatrix : packoffset(c38);
  float4 cb_globalviewinfos : packoffset(c42);
  float3 cb_wscamforwarddir : packoffset(c43);
  uint cb_alwaysone : packoffset(c43.w);
  float3 cb_wscamupdir : packoffset(c44);
  uint cb_usecompressedhdrbuffers : packoffset(c44.w);
  float3 cb_wscampos : packoffset(c45);
  float cb_time : packoffset(c45.w);
  float3 cb_wscamleftdir : packoffset(c46);
  float cb_systime : packoffset(c46.w);
  float2 cb_jitterrelativetopreviousframe : packoffset(c47);
  float2 cb_worldtime : packoffset(c47.z);
  float2 cb_shadowmapatlasslicedimensions : packoffset(c48);
  float2 cb_resolutionscale : packoffset(c48.z);
  float2 cb_parallelshadowmapslicedimensions : packoffset(c49);
  float cb_framenumber : packoffset(c49.z);
  uint cb_alwayszero : packoffset(c49.w);
}

// Usefull when developing.
#ifndef ENABLE_SSAO
#define ENABLE_SSAO 1
#endif

Texture2D<float> ro_ssao_zbuffer : register(t0);
Texture2D<float4> ro_downsampling_downsampleddepthbuffer : register(t1);


// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_POSITION0,
  out float4 o0 : SV_TARGET0)
{
  const float4 icb[] = { { -0.388235, 0.921569, 0, 0},
                              { -0.929412, 0.380392, 0, 0},
                              { 0.921569, 0.380392, 0, 0},
                              { 0.380392, -0.929412, 0, 0},
                              { -0.003922, -1.000000, 0, 0},
                              { -0.709804, -0.709804, 0, 0},
                              { 0.701961, -0.709804, 0, 0},
                              { 0.380392, 0.921569, 0, 0},
                              { -0.929412, -0.388235, 0, 0},
                              { -0.709804, 0.701961, 0, 0},
                              { 1.000000, -0.003922, 0, 0},
                              { -0.003922, 1.000000, 0, 0},
                              { 0.921569, -0.388235, 0, 0},
                              { -0.388235, -0.929412, 0, 0},
                              { -1.000000, -0.003922, 0, 0},
                              { 0.701961, 0.701961, 0, 0} };
  float4 r0,r1,r2;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = (int2)v0.yx;
  r0.xy = (int2)r0.xy & int2(3,3);
  r0.xy = (uint2)r0.xy;
  r0.x = r0.x * 4 + r0.y;
  r0.x = (uint)r0.x;
  r0.y = icb[r0.x+0].y * 0.349999994;
  r1.x = icb[r0.x+0].x * -0.150000006 + -r0.y;
  r1.y = dot(icb[r0.x+0].yx, float2(-0.150000006,0.349999994));
  r0.xy = cb_dwao_params0.xx * r1.xy;
  r1.xy = (uint2)v0.xy;
  r0.zw = (uint2)r1.xy;
  r2.xy = r0.xy * cb_resolutionscale.xy + r0.zw;
  r0.xy = -r0.xy * cb_resolutionscale.xy + r0.zw;
  r0.xy = float2(0.5,0.5) * r0.xy;
  r0.xy = (int2)r0.xy;
  r2.xy = float2(0.5,0.5) * r2.xy;
  r2.xy = (int2)r2.xy;
  r2.zw = float2(0,0);
  r2.x = ro_downsampling_downsampleddepthbuffer.Load(r2.xyz).z;
  r1.zw = float2(0,0);
  r1.x = ro_ssao_zbuffer.Load(r1.xyz).x;
  r1.x = cb_inverseprojectionmatrix._m32 * r1.x + cb_inverseprojectionmatrix._m33;
  r1.x = -cb_inverseprojectionmatrix._m23 / r1.x;
  r2.x = r1.x + -r2.x;
  r0.zw = float2(0,0);
  r0.x = ro_downsampling_downsampleddepthbuffer.Load(r0.xyz).z;
  r2.y = r1.x + -r0.x;
  r0.xy = cb_dwao_params0.zz * r1.xx + -r2.xy;
  r0.zw = cb_dwao_params0.zy * r1.xx;
  r0.xy = saturate(r0.xy / r0.zz);
  r0.zw = r2.xy / r0.ww;
  r0.zw = saturate(r0.zw * float2(0.5,0.5) + float2(0.5,0.5));
  r0.xy = r0.zw * r0.xy;
  r0.x = r0.x + r0.y;
  r0.x = cb_dwao_params1.x + r0.x;
  r0.x = -1 + r0.x;
  r0.x = saturate(cb_dwao_params0.w * r0.x);
  
  #if ENABLE_SSAO
  o0.x = 1 + -r0.x;
  #else
  o0.x = 1.0;
  #endif

  o0.y = 1;
  r0.x = 1.00390625 * r1.x;
  r0.x = trunc(r0.x);
  r0.xy = float2(256,0.00392156886) * r0.xx;
  r0.x = r1.x * 257 + -r0.x;
  o0.z = r0.y;
  o0.w = 0.00392156886 * r0.x;
  return;
}