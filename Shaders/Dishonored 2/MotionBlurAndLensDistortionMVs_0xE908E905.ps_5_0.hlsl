cbuffer PerInstanceCB : register(b2)
{
  float4 cb_fsdisto_lensstrength : packoffset(c0);
  float4 cb_positiontoviewtexture : packoffset(c1);
  float4 cb_fsdisto_wobblestrength : packoffset(c2);
  float4 cb_fsdisto_wobblespeed : packoffset(c3);
  float4 cb_fsdisto_wobblelowbound : packoffset(c4);
  float4 cb_fsdisto_wobblehighbound : packoffset(c5);
  float4 cb_fsdisto_sandstorm_startvalue : packoffset(c6);
  float4 cb_fsdisto_sandstorm_endvalue : packoffset(c7);
  float4 cb_fsdisto_radialdistostrength : packoffset(c8);
  float4 cb_fsdisto_radialblurstrength : packoffset(c9);
  float4 cb_fsdisto_radialblurdeadzone : packoffset(c10);
  float3 cb_fsdisto_sandstorm_worldwind : packoffset(c11);
  float cb_fsdisto_sandstorm_deadzone : packoffset(c11.w);
  float2 cb_localtime : packoffset(c12);
  float cb_fsdisto_sandstorm_startdepth : packoffset(c12.z);
  float cb_fsdisto_sandstorm_enddepth : packoffset(c12.w);
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

#ifndef DISABLE_LENS_DISTORTION
#define DISABLE_LENS_DISTORTION 0
#endif

// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_POSITION0,
  out float4 o0 : SV_TARGET0)
{
  float4 r0,r1;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xyz = float3(1,1,1) / cb_positiontoviewtexture.zzw;
  r0.x = cb_positiontoviewtexture.w * r0.x;
  r0.w = cb_resolutionscale.x / cb_resolutionscale.y;
  r0.x = r0.x / r0.w;
  r0.x = 1.77777779 / r0.x;
  r1.xy = cb_resolutionscale.xy * cb_positiontoviewtexture.zw;
  r0.xw = r1.xy * r0.xx;
  r0.x = max(r0.x, r0.w);
  r0.w = 450 * r0.x;
  r0.yz = -r0.yz * float2(0.5,0.5) + v0.xy;
  r0.yz = r0.yz / cb_resolutionscale.xy;
  r1.xy = cb_positiontoviewtexture.zw * r0.yz;
  r1.xy = cb_resolutionscale.xy * r1.xy;
  r1.z = dot(r1.xy, r1.xy);
  r1.z = rsqrt(r1.z);
  r1.xy = r1.xy * r1.zz;
  r1.xy = -r1.xy * r0.xx;
  r0.w = cb_fsdisto_radialblurdeadzone.x / r0.w;
  r0.y = dot(r0.yz, r0.yz);
  r0.y = sqrt(r0.y);
  r0.y = r0.y + -r0.w;
  r0.z = 1 / r0.x;
  r0.y = max(0, r0.y);
  r0.y = min(r0.y, r0.z);
  r0.yz = r1.xy * r0.yy;
  r0.xy = r0.yz * r0.xx;
  r0.xy = cb_resolutionscale.xy * r0.xy;

  #if DISABLE_LENS_DISTORTION
  r1.xy = 0.0;
  r1.zw = 0.0;
  #else
  r1.xy = cb_fsdisto_radialblurstrength.xx * r0.xy;
  r1.zw = cb_fsdisto_radialdistostrength.xx * r0.xy;
  #endif

  r0.x = dot(r1.xyzw, float4(1,1,1,1));
  r0.x = cmp(abs(r0.x) == 0.000000);
  if (r0.x != 0) discard;
  o0.xyzw = r1.xyzw;
  return;
}