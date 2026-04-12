cbuffer PerInstanceCB : register(b2)
{
  float4 cb_fsdisto_lensstrength : packoffset(c0);
  float4 cb_viewport : packoffset(c1);
  float4 cb_positiontoviewtexture : packoffset(c2);
  float4 cb_fsdisto_wobblestrength : packoffset(c3);
  float4 cb_fsdisto_wobblespeed : packoffset(c4);
  float4 cb_fsdisto_wobblelowbound : packoffset(c5);
  float4 cb_fsdisto_wobblehighbound : packoffset(c6);
  float4 cb_fsdisto_sandstorm_startvalue : packoffset(c7);
  float4 cb_fsdisto_sandstorm_endvalue : packoffset(c8);
  float4 cb_fsdisto_radialdistostrength : packoffset(c9);
  float4 cb_fsdisto_radialblurstrength : packoffset(c10);
  float4 cb_fsdisto_radialblurdeadzone : packoffset(c11);
  float3 cb_fsdisto_sandstorm_worldwind : packoffset(c12);
  float cb_fsdisto_camerablur_enddepth : packoffset(c12.w);
  float2 cb_localtime : packoffset(c13);
  float cb_fsdisto_sandstorm_startdepth : packoffset(c13.z);
  float cb_fsdisto_sandstorm_enddepth : packoffset(c13.w);
  float cb_fsdisto_sandstorm_deadzone : packoffset(c14);
  float cb_fsdisto_camerablur_strength : packoffset(c14.y);
  float cb_fsdisto_camerablur_startdepth : packoffset(c14.z);
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

SamplerState smp_linearclamp_s : register(s0);
Texture2D<float4> ro_fx_depthfull : register(t0);

#ifndef DISABLE_LENS_DISTORTION
#define DISABLE_LENS_DISTORTION 0
#endif

// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_POSITION0,
  out float4 o0 : SV_TARGET0)
{
  float4 r0,r1,r2,r3;
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
  r1.xy = cb_positiontoviewtexture.zw * v0.xy;
  r1.xy = cb_resolutionscale.xy * r1.xy;
  r1.x = ro_fx_depthfull.SampleLevel(smp_linearclamp_s, r1.xy, 0).x;
  r1.y = cb_inverseprojectionmatrix._m32 * r1.x + cb_inverseprojectionmatrix._m33;
  r1.y = -cb_inverseprojectionmatrix._m23 / r1.y;
  r1.zw = -cb_viewport.xy + v0.xy;
  r2.xy = float2(0.5,0.5) * cb_viewport.zw;
  r1.zw = -cb_viewport.zw * float2(0.5,0.5) + r1.zw;
  r1.zw = r1.zw / r2.xy;
  r2.xy = float2(1,-1) * r1.zw;
  r3.xyzw = cb_inverseviewprojectionmatrix._m01_m11_m21_m31 * r2.yyyy;
  r2.xyzw = cb_inverseviewprojectionmatrix._m00_m10_m20_m30 * r2.xxxx + r3.xyzw;
  r2.xyzw = cb_inverseviewprojectionmatrix._m02_m12_m22_m32 * r1.xxxx + r2.xyzw;
  r2.xyzw = cb_inverseviewprojectionmatrix._m03_m13_m23_m33 + r2.xyzw;
  r2.xyzw = r2.xyzw / r2.wwww;
  r3.xyz = cb_previousviewprojectionmatrix._m01_m11_m31 * r2.yyy;
  r3.xyz = cb_previousviewprojectionmatrix._m00_m10_m30 * r2.xxx + r3.xyz;
  r2.xyz = cb_previousviewprojectionmatrix._m02_m12_m32 * r2.zzz + r3.xyz;
  r2.xyz = cb_previousviewprojectionmatrix._m03_m13_m33 * r2.www + r2.xyz;
  r2.xy = r2.xy / abs(r2.zz);
  r1.xz = r1.zw * float2(1,-1) + -r2.xy;
  r1.xz = float2(-0.5,0.5) * r1.xz;
  r1.xz = max(float2(-1,-1), r1.xz);
  r1.xz = min(float2(1,1), r1.xz);
  r1.w = -cb_fsdisto_camerablur_startdepth + cb_fsdisto_camerablur_enddepth;
  r1.y = -cb_fsdisto_camerablur_startdepth + r1.y;
  r1.y = saturate(r1.y / r1.w);
  r2.xy = cb_fsdisto_camerablur_strength * cb_positiontoviewtexture.zw;
  r1.yw = r2.xy * r1.yy;
  r1.yw = cb_resolutionscale.xy * r1.yw;
  r1.xy = r1.xz * r1.yw;
  r1.zw = cb_positiontoviewtexture.zw * r0.yz;
  r1.zw = cb_resolutionscale.xy * r1.zw;
  r2.x = dot(r1.zw, r1.zw);
  r2.x = rsqrt(r2.x);
  r1.zw = r2.xx * r1.zw;
  r1.zw = -r1.zw * r0.xx;
  r0.w = cb_fsdisto_radialblurdeadzone.x / r0.w;
  r0.y = dot(r0.yz, r0.yz);
  r0.y = sqrt(r0.y);
  r0.y = r0.y + -r0.w;
  r0.z = 1 / r0.x;
  r0.y = max(0, r0.y);
  r0.y = min(r0.y, r0.z);
  r0.yz = r1.zw * r0.yy;
  r0.xy = r0.yz * r0.xx;
  r0.xy = cb_resolutionscale.xy * r0.xy;

  #if DISABLE_LENS_DISTORTION
  r0.zw = 0.0;
  #else
  r0.zw = cb_fsdisto_radialblurstrength.xx * r0.xy;
  #endif

  r1.xy = r1.xy * cb_resolutionscale.xy + r0.zw;

  #if DISABLE_LENS_DISTORTION
  r1.zw = 0.0;
  #else
  r1.zw = cb_fsdisto_radialdistostrength.xx * r0.xy;
  #endif

  r0.x = dot(r1.xyzw, float4(1,1,1,1));
  r0.x = cmp(abs(r0.x) == 0.000000);
  if (r0.x != 0) discard;
  o0.xyzw = r1.xyzw;
  return;
}