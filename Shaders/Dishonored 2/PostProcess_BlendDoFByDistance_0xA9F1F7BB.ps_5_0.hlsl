struct postfx_luminance_autoexposure_t
{
    float EngineLuminanceFactor;   // Offset:    0
    float LuminanceFactor;         // Offset:    4
    float MinLuminanceLDR;         // Offset:    8
    float MaxLuminanceLDR;         // Offset:   12
    float MiddleGreyLuminanceLDR;  // Offset:   16
    float EV;                      // Offset:   20
    float Fstop;                   // Offset:   24
    uint PeakHistogramValue;       // Offset:   28
};

cbuffer PerInstanceCB : register(b2)
{
  float4 cb_postfx_dof_vignparams : packoffset(c0);
  float3 cb_atm_worldfog_scatteringcolor : packoffset(c1);
  float cb_postfx_dof_commonparams : packoffset(c1.w);
  float3 cb_postfx_dof_farparams : packoffset(c2);
  float cb_view_white_level : packoffset(c2.w);
  float3 cb_postfx_dof_distortionparams : packoffset(c3);
  float2 cb_postfx_dof_center : packoffset(c4);
  uint2 cb_postfx_luminance_exposureindex : packoffset(c4.z);
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

SamplerState smp_bilinearclamp_s : register(s0);
SamplerState smp_pointclamp_s : register(s1);
Texture2D<float4> ro_postfx_dof_framebuffermap : register(t0);
Texture2D<float> ro_postfx_dof_depthmap : register(t1);
Texture2D<float4> ro_postfx_dof_dofmap : register(t2);
Texture2D<float> ro_postfx_dof_dofnearalphamap : register(t3);
Texture2D<float3> ro_postfx_dof_dofnearmap : register(t4);
Texture2D<float4> ro_postfx_dof_diffusemap : register(t5);
StructuredBuffer<postfx_luminance_autoexposure_t> ro_postfx_luminance_buffautoexposure : register(t6);

// 3Dmigoto declarations
#define cmp -

void main(
  float4 v0 : INTERP0,
  out float4 o0 : SV_TARGET0)
{
  float4 r0,r1,r2,r3;
  r0.x = ro_postfx_dof_depthmap.SampleLevel(smp_pointclamp_s, v0.xy, 0).x;
  r0.x = cb_inverseprojectionmatrix._m32 * r0.x + cb_inverseprojectionmatrix._m33;
  r0.x = -cb_inverseprojectionmatrix._m23 / r0.x;
  r0.x = saturate(r0.x / cb_postfx_dof_commonparams);
  r0.y = -cb_postfx_dof_farparams.y + r0.x;
  r0.x = (cb_postfx_dof_farparams.y < r0.x) ? 1.0 : 0.0;
  r0.z = cb_postfx_dof_farparams.x + -cb_postfx_dof_farparams.y;
  r0.y = r0.y / r0.z;
  r0.x = saturate(r0.x * r0.y + cb_postfx_dof_farparams.z);
  r0.x = r0.x + r0.x;
  r0.x = min(1, r0.x);
  r0.y = ro_postfx_luminance_buffautoexposure[cb_postfx_luminance_exposureindex.y].EngineLuminanceFactor;
  r0.y = cb_view_white_level * r0.y;
  r1.xyz = ro_postfx_dof_framebuffermap.SampleLevel(smp_pointclamp_s, v0.xy, 0).xyz;
  r0.yzw = r1.xyz * r0.yyy;
  r0.yzw = cb_usecompressedhdrbuffers ? r0.yzw : r1.xyz;
  r0.yzw = cmp(r0.yzw >= cb_atm_worldfog_scatteringcolor.xyz);
  r0.yzw = r0.yzw ? float3(-0.75,-0.75,-0.75) : float3(-0,-0,-0);
  r2.xyz = ro_postfx_dof_diffusemap.SampleLevel(smp_bilinearclamp_s, v0.xy, 0).xyz;
  r3.xyz = cmp(r1.xyz >= r2.xyz);
  r1.xyz = -r2.xyz + r1.xyz;
  r3.xyz = r3.xyz ? float3(1,1,1) : 0;
  r0.yzw = r3.xyz + r0.yzw;
  r0.yzw = max(float3(0,0,0), r0.yzw);
  r0.y = dot(float3(0.298900008,0.587000012,0.114), r0.yzw);
  r0.y = cb_postfx_dof_distortionparams.z * r0.y + 1;
  r0.yzw = r0.yyy * r1.xyz + r2.xyz;
  r1.xyz = ro_postfx_dof_dofnearmap.SampleLevel(smp_bilinearclamp_s, v0.xy, 0).xyz;
  r1.xyz = r1.xyz + -r0.yzw;
  r1.w = ro_postfx_dof_dofnearalphamap.SampleLevel(smp_bilinearclamp_s, v0.xy, 0).x;
  r1.w = r1.w * r1.w;
  r1.w = r1.w * r1.w;
  r0.yzw = r1.www * r1.xyz + r0.yzw;
  r1.xyz = ro_postfx_dof_dofmap.SampleLevel(smp_bilinearclamp_s, v0.xy, 0).xyz;
  r1.xyz = r1.xyz + -r0.yzw;
  r0.xyz = r0.xxx * r1.xyz + r0.yzw;
  r1.xy = v0.xy / cb_resolutionscale.xy;
  r1.xy = -cb_postfx_dof_center.xy + r1.xy;
  r0.w = dot(r1.xy, r1.xy);
  r0.w = sqrt(r0.w);
  r1.x = cb_postfx_dof_vignparams.w / cb_postfx_dof_vignparams.z;
  r1.xy = cb_postfx_dof_vignparams.xy + r1.xx;
  r0.w = -r1.x + r0.w;
  r1.x = r1.y + -r1.x;
  r0.w = saturate(r0.w / r1.x);
  o0.xyz = r0.xyz * r0.www;
  o0.w = 1;
}