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
  float4 cb_positiontoviewtexture : packoffset(c0);
  float4 cb_taatexsize : packoffset(c1);
  float4 cb_taaditherandviewportsize : packoffset(c2);
  float4 cb_postfx_tonemapping_tonemappingparms : packoffset(c3);
  float4 cb_postfx_tonemapping_tonemappingcoeffsinverse1 : packoffset(c4);
  float4 cb_postfx_tonemapping_tonemappingcoeffsinverse0 : packoffset(c5);
  float4 cb_postfx_tonemapping_tonemappingcoeffs1 : packoffset(c6);
  float4 cb_postfx_tonemapping_tonemappingcoeffs0 : packoffset(c7);
  uint2 cb_postfx_luminance_exposureindex : packoffset(c8);
  float2 cb_prevresolutionscale : packoffset(c8.z);
  float cb_env_tonemapping_white_level : packoffset(c9);
  float cb_taaamount : packoffset(c9.y);
  float cb_postfx_luminance_customevbias : packoffset(c9.z);
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

#define DISPATCH_BLOCK 16

groupshared struct { float val[36]; } g1[18];
groupshared struct { float val[72]; } g0[18];

SamplerState smp_linearclamp_s : register(s0);
Texture2D<float3> ro_taahistory_read : register(t0);
Texture2D<float4> ro_motionvectors : register(t1);
Texture2D<float4> ro_viewcolormap : register(t2);
StructuredBuffer<postfx_luminance_autoexposure_t> ro_postfx_luminance_buffautoexposure : register(t3);
RWTexture2D<float3> rw_taahistory_write : register(u0);
RWTexture2D<float3> rw_taaresult : register(u1);

#define cmp -

// Runs before tonemapping
// This is a lower quality TAA version, with less luminance stuff, no "cb_usecompressedhdrbuffers" and less samples from the previous frame
[numthreads(DISPATCH_BLOCK, DISPATCH_BLOCK, 1)]
void main(uint3 vThreadID : SV_DispatchThreadID, uint3 vGroupID : SV_GroupID, uint3 vThreadIDInGroup : SV_GroupThreadID)
{
#if 0 // Disable TAA
    const uint2 vPixelPosUInt = vGroupID.xy * uint2(DISPATCH_BLOCK, DISPATCH_BLOCK) + vThreadIDInGroup.xy; // Equal to "vThreadID.xy"
    GroupMemoryBarrierWithGroupSync();
    //rw_taaresult[vThreadID.xy] = ro_viewcolormap[vThreadID.xy].rgb;
    rw_taaresult[vThreadID.xy] = ro_viewcolormap.SampleLevel(smp_linearclamp_s, ((vThreadID.xy + 0.5) * cb_taatexsize.zw) - (cb_jittervectors.xy * float2(1, -1)), 0).rgb; // "fast" dejitter
#else
// Needs manual fix for instruction:
// unknown dcl_: dcl_uav_typed_texture2d (float,float,float,float) u0
// Needs manual fix for instruction:
// unknown dcl_: dcl_uav_typed_texture2d (float,float,float,float) u1
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8,r9,r10,r11,r12;

// Needs manual fix for instruction:
// unknown dcl_: dcl_thread_group 16, 16, 1
  r0.x = mad((int)vThreadIDInGroup.y, 16, (int)vThreadIDInGroup.x);
  r0.x = (int)r0.x;
  r0.x = 0.5 + r0.x;
  r0.x = 0.055555556 * r0.x;
  r0.y = floor(r0.x);
  r0.x = frac(r0.x);
  r0.x = 18 * r0.x;
  r0.x = floor(r0.x);
  r1.xy = (int2)r0.xy;
  r0.xy = (int2)-vThreadIDInGroup.xy + (int2)vThreadID.xy;
  r0.xy = (int2)r0.xy + int2(-1,-1);
  r0.z = ro_postfx_luminance_buffautoexposure[cb_postfx_luminance_exposureindex.y].EngineLuminanceFactor;
  r0.w = exp2(-cb_postfx_luminance_customevbias);
  r0.z = r0.z * r0.w;
  r0.w = cmp((int)r1.y < 14);
  if (r0.w != 0) {
    r2.xy = (int2)r1.xy + (int2)r0.xy;
    r2.zw = float2(0,0);
    r3.xyz = ro_viewcolormap.Load(r2.xyw).xyz;
    r3.xyz = max(float3(0,0,0), r3.xyz);
    r3.xyz = min(cb_env_tonemapping_white_level, r3.xyz);
    r3.xyz = r3.xyz * r0.zzz;
    r4.xyz = cmp(r3.xyz < cb_postfx_tonemapping_tonemappingparms.xxx);
    r5.xyzw = r4.xxxx ? cb_postfx_tonemapping_tonemappingcoeffs0.xyzw : cb_postfx_tonemapping_tonemappingcoeffs1.xyzw;
    r3.xw = r5.xy * r3.xx + r5.zw;
    r0.w = r3.x / r3.w;
    r5.xyzw = r4.yyyy ? cb_postfx_tonemapping_tonemappingcoeffs0.xyzw : cb_postfx_tonemapping_tonemappingcoeffs1.xyzw;
    r3.xy = r5.xy * r3.yy + r5.zw;
    r1.w = r3.x / r3.y;
    r4.xyzw = r4.zzzz ? cb_postfx_tonemapping_tonemappingcoeffs0.xyzw : cb_postfx_tonemapping_tonemappingcoeffs1.xyzw;
    r3.xy = r4.xy * r3.zz + r4.zw;
    r3.x = r3.x / r3.y;
    r4.y = -r3.x + r0.w;
    r0.w = r4.y * 0.5 + r3.x;
    r4.z = r1.w + -r0.w;
    r4.x = r4.z * 0.5 + r0.w;
    r2.xy = ro_motionvectors.Load(r2.xyz).xy;
    r4.w = dot(r2.xy, r2.xy);
    r2.zw = (uint2)r1.xx << int2(4,3);
    g0[r1.y].val[r2.z/4] = r4.x;
    g0[r1.y].val[r2.z/4+1] = r4.y;
    g0[r1.y].val[r2.z/4+2] = r4.z;
    g0[r1.y].val[r2.z/4+3] = r4.w;
    g1[r1.y].val[r2.w/4] = r2.x;
    g1[r1.y].val[r2.w/4+1] = r2.y;
  }
  r1.z = (int)r1.y + 14;
  r0.w = cmp((int)r1.z < 18);
  if (r0.w != 0) {
    r2.xy = (int2)r0.xy + (int2)r1.xz;
    r2.zw = float2(0,0);
    r0.xyw = ro_viewcolormap.Load(r2.xyw).xyz;
    r0.xyw = max(float3(0,0,0), r0.xyw);
    r0.xyw = min(cb_env_tonemapping_white_level, r0.xyw);
    r0.xyw = r0.xyw * r0.zzz;
    r3.xyz = cmp(r0.xyw < cb_postfx_tonemapping_tonemappingparms.xxx);
    r4.xyzw = r3.xxxx ? cb_postfx_tonemapping_tonemappingcoeffs0.xyzw : cb_postfx_tonemapping_tonemappingcoeffs1.xyzw;
    r1.yw = r4.xy * r0.xx + r4.zw;
    r0.x = r1.y / r1.w;
    r4.xyzw = r3.yyyy ? cb_postfx_tonemapping_tonemappingcoeffs0.xyzw : cb_postfx_tonemapping_tonemappingcoeffs1.xyzw;
    r1.yw = r4.xy * r0.yy + r4.zw;
    r0.y = r1.y / r1.w;
    r3.xyzw = r3.zzzz ? cb_postfx_tonemapping_tonemappingcoeffs0.xyzw : cb_postfx_tonemapping_tonemappingcoeffs1.xyzw;
    r1.yw = r3.xy * r0.ww + r3.zw;
    r0.w = r1.y / r1.w;
    r3.y = r0.x + -r0.w;
    r0.x = r3.y * 0.5 + r0.w;
    r3.z = r0.y + -r0.x;
    r3.x = r3.z * 0.5 + r0.x;
    r0.xy = ro_motionvectors.Load(r2.xyz).xy;
    r3.w = dot(r0.xy, r0.xy);
    r1.xy = (uint2)r1.xx << int2(4,3);
    g0[r1.z].val[r1.x/4] = r3.x;
    g0[r1.z].val[r1.x/4+1] = r3.y;
    g0[r1.z].val[r1.x/4+2] = r3.z;
    g0[r1.z].val[r1.x/4+3] = r3.w;
    g1[r1.z].val[r1.y/4] = r0.x;
    g1[r1.z].val[r1.y/4+1] = r0.y;
  }
  GroupMemoryBarrierWithGroupSync();
  r0.xy = (int2)vThreadID.yx;
  r0.xy = float2(0.5,0.5) + r0.xy;
  r1.xy = cb_taaditherandviewportsize.yx + r0.xy;
  r0.w = dot(r1.xy, float2(214013.156,2531011.75));
  r1.x = (int)r0.w * (int)r0.w;
  r1.x = mad((int)r1.x, 0x00003d73, 0x000c0ae5);
  r0.w = (int)r0.w * (int)r1.x;
  r0.w = (uint)r0.w >> 9;
  r0.w = (int)r0.w + 0x3f800000;
  r0.w = 2 + -r0.w;
  r0.w = r0.w * 0.600000024 + -0.300000012;
  r1.x = (uint)vThreadIDInGroup.x << 4;
  r2.x = g0[vThreadIDInGroup.y].val[r1.x/4];
  r2.y = g0[vThreadIDInGroup.y].val[r1.x/4+1];
  r2.z = g0[vThreadIDInGroup.y].val[r1.x/4+2];
  r2.w = g0[vThreadIDInGroup.y].val[r1.x/4+3];
  r1.y = cmp(-1 < r2.w);
  r3.x = r2.w;
  r3.yz = vThreadIDInGroup.xy;
  r1.yzw = r1.yyy ? r3.xyz : float3(-1,0,0);
  r3.xyzw = (int4)vThreadIDInGroup.xxyy + int4(1,2,0,0);
  r4.xy = (int2)r1.xx + int2(16,32);
  r5.x = g0[r3.w].val[r4.x/4+3];
  r5.y = g0[r3.w].val[r4.x/4];
  r5.z = g0[r3.w].val[r4.x/4+1];
  r5.w = g0[r3.w].val[r4.x/4+2];
  r6.xyzw = r5.yzwy + r2.xyzx;
  r7.xyzw = r5.yzwy * r5.yzwy;
  r2.xyzw = r2.xyzx * r2.xyzx + r7.xyzw;
  r4.z = cmp(r1.y < r5.x);
  r5.yz = r3.xw;
  r1.yzw = r4.zzz ? r5.xyz : r1.yzw;
  r5.x = g0[r3.z].val[r4.y/4];
  r5.y = g0[r3.z].val[r4.y/4+1];
  r5.z = g0[r3.z].val[r4.y/4+2];
  r5.w = g0[r3.z].val[r4.y/4+3];
  r6.xyzw = r6.xyzw + r5.xyzx;
  r2.xyzw = r5.xyzx * r5.xyzx + r2.xyzw;
  r3.w = cmp(r1.y < r5.w);
  r3.x = r5.w;
  r1.yzw = r3.www ? r3.xyz : r1.yzw;
  r3.xyzw = (int4)vThreadIDInGroup.xxyy + int4(0,1,1,1);
  r5.x = g0[r3.w].val[r1.x/4+3];
  r5.y = g0[r3.w].val[r1.x/4];
  r5.z = g0[r3.w].val[r1.x/4+1];
  r5.w = g0[r3.w].val[r1.x/4+2];
  r6.xyzw = r6.xyzw + r5.yzwy;
  r2.xyzw = r5.yzwy * r5.yzwy + r2.xyzw;
  r4.z = cmp(r1.y < r5.x);
  r5.yz = r3.xw;
  r1.yzw = r4.zzz ? r5.xyz : r1.yzw;
  r5.x = g0[r3.z].val[r4.x/4];
  r5.y = g0[r3.z].val[r4.x/4+1];
  r5.z = g0[r3.z].val[r4.x/4+2];
  r5.w = g0[r3.z].val[r4.x/4+3];
  r3.w = 1 + -r5.x;
  r0.w = r0.w * r3.w + 1;
  r7.xw = r5.xx * r0.ww;
  r6.xyzw = r6.xyzw + r5.xyzx;
  r2.xyzw = r5.xyzx * r5.xyzx + r2.xyzw;
  r0.w = cmp(r1.y < r5.w);
  r3.x = r5.w;
  r1.yzw = r0.www ? r3.xyz : r1.yzw;
  r3.xyzw = (int4)vThreadIDInGroup.xxyy + int4(2,0,2,1);
  r8.x = g0[r3.w].val[r4.y/4+3];
  r8.y = g0[r3.w].val[r4.y/4];
  r8.z = g0[r3.w].val[r4.y/4+1];
  r8.w = g0[r3.w].val[r4.y/4+2];
  r6.xyzw = r8.yzwy + r6.xyzw;
  r2.xyzw = r8.yzwy * r8.yzwy + r2.xyzw;
  r0.w = cmp(r1.y < r8.x);
  r8.yz = r3.xw;
  r1.yzw = r0.www ? r8.xyz : r1.yzw;
  r8.x = g0[r3.z].val[r1.x/4];
  r8.y = g0[r3.z].val[r1.x/4+1];
  r8.z = g0[r3.z].val[r1.x/4+2];
  r8.w = g0[r3.z].val[r1.x/4+3];
  r6.xyzw = r8.xyzx + r6.xyzw;
  r2.xyzw = r8.xyzx * r8.xyzx + r2.xyzw;
  r0.w = cmp(r1.y < r8.w);
  r3.x = r8.w;
  r1.xyz = r0.www ? r3.xyz : r1.yzw;
  r3.xyzw = (int4)vThreadIDInGroup.xyxy + int4(1,2,2,2);
  r8.x = g0[r3.y].val[r4.x/4+3];
  r8.y = g0[r3.y].val[r4.x/4];
  r8.z = g0[r3.y].val[r4.x/4+1];
  r8.w = g0[r3.y].val[r4.x/4+2];
  r6.xyzw = r8.yzwy + r6.xyzw;
  r2.xyzw = r8.yzwy * r8.yzwy + r2.xyzw;
  r0.w = cmp(r1.x < r8.x);
  r8.yz = r3.xy;
  r1.xyz = r0.www ? r8.xyz : r1.xyz;
  r4.x = g0[r3.w].val[r4.y/4];
  r4.y = g0[r3.w].val[r4.y/4+1];
  r4.z = g0[r3.w].val[r4.y/4+2];
  r4.w = g0[r3.w].val[r4.y/4+3];
  r6.xyzw = r6.xyzw + r4.xyzx;
  r2.xyzw = r4.xyzx * r4.xyzx + r2.xyzw;
  r0.w = cmp(r1.x < r4.w);
  r1.xy = r0.ww ? r3.zw : r1.yz;
  r3.xyz = float3(0.111111112,0.111111112,0.111111112) * r6.yzw;
  r4.xyzw = r3.zxyz * r3.zxyz;
  r2.xyzw = r2.xyzw * float4(0.111111112,0.111111112,0.111111112,0.111111112) + -r4.xyzw;
  r2.xyzw = max(float4(0,0,0,0), r2.xyzw);
  r2.xyzw = sqrt(r2.xyzw);
  r4.xyzw = r6.wyzw * float4(0.111111112,0.111111112,0.111111112,0.111111112) + -r2.wyzw;
  r2.xyzw = r6.xyzw * float4(0.111111112,0.111111112,0.111111112,0.111111112) + r2.xyzw;
  r7.yz = r5.yz;
  r4.xyzw = min(r7.wyzw, r4.xyzw);
  r2.xyzw = max(r7.wyzw, r2.xyzw);
  r0.w = (uint)r1.x << 3;
  r1.x = g1[r1.y].val[r0.w/4];
  r1.y = g1[r1.y].val[r0.w/4+1];
  r0.xy = -r1.yx * cb_taaditherandviewportsize.wz + r0.xy;
  r1.xy = cb_prevresolutionscale.yx / cb_resolutionscale.yx;
  r1.zw = r0.yx * r1.yx + float2(-0.5,-0.5);
  r1.zw = floor(r1.zw);
  r5.xyzw = float4(0.5,0.5,-0.5,-0.5) + r1.zwzw;
  r0.xy = r0.xy * r1.xy + -r5.yx;
  r1.xy = r0.yx * r0.yx;
  r3.xy = r1.xy * r0.yx;
  r6.xy = r1.yx * r0.xy + r0.xy;
  r6.xy = -r6.xy * float2(0.5,0.5) + r1.yx;
  r6.zw = float2(2.5,2.5) * r1.yx;
  r3.xy = r3.yx * float2(1.5,1.5) + -r6.zw;
  r3.xy = float2(1,1) + r3.xy;
  r0.xy = r1.xy * r0.yx + -r1.xy;
  r1.xy = float2(0.5,0.5) * r0.xy;
  r6.zw = float2(1,1) + -r6.yx;
  r6.zw = r6.zw + -r3.yx;
  r0.xy = -r0.xy * float2(0.5,0.5) + r6.zw;
  r3.xy = r3.xy + r0.yx;
  r0.xy = r0.xy / r3.yx;
  r0.xy = r5.xy + r0.xy;
  r1.zw = float2(2.5,2.5) + r1.zw;
  r5.xw = cb_taatexsize.zw * r5.zw;
  r5.yz = cb_taatexsize.wz * r0.yx;
  r8.xy = cb_taatexsize.zw * r1.zw;
  r0.xy = -cb_positiontoviewtexture.zw * float2(0.5,0.5) + cb_prevresolutionscale.xy;
  r9.xyzw = cb_prevresolutionscale.xyxy * r5.xwzw;
  r9.xyzw = max(float4(0,0,0,0), r9.xyzw);
  r9.xyzw = min(r9.xyzw, r0.xyxy);
  r10.xyz = ro_taahistory_read.SampleLevel(smp_linearclamp_s, r9.xy, 0).xyz;
  r0.w = r6.y * r6.x;
  r1.z = min(1.00000003e+032, r10.x);
  r1.w = max(-1.00000003e+032, r10.x);
  r9.xyz = ro_taahistory_read.SampleLevel(smp_linearclamp_s, r9.zw, 0).xyz;
  r6.zw = r3.yx * r6.xy;
  r11.xyzw = r9.xyzx * r6.zzzz;
  r11.xyzw = r10.xyzx * r0.wwww + r11.xyzw;
  r0.w = r10.x + r9.x;
  r1.z = min(r9.x, r1.z);
  r1.w = max(r9.x, r1.w);
  r8.zw = r5.wy;
  r9.xyzw = cb_prevresolutionscale.xyxy * r8.xzxw;
  r9.xyzw = max(float4(0,0,0,0), r9.xyzw);
  r9.xyzw = min(r9.xyzw, r0.xyxy);
  r10.xyz = ro_taahistory_read.SampleLevel(smp_linearclamp_s, r9.xy, 0).xyz;
  r6.xy = r1.xy * r6.xy;
  r11.xyzw = r10.xyzx * r6.xxxx + r11.xyzw;
  r0.w = r10.x + r0.w;
  r1.z = min(r10.x, r1.z);
  r1.w = max(r10.x, r1.w);
  r10.xyzw = cb_prevresolutionscale.xyxy * r5.xyzy;
  r10.xyzw = max(float4(0,0,0,0), r10.xyzw);
  r10.xyzw = min(r10.xyzw, r0.xyxy);
  r12.xyz = ro_taahistory_read.SampleLevel(smp_linearclamp_s, r10.xy, 0).xyz;
  r11.xyzw = r12.xyzx * r6.wwww + r11.xyzw;
  r0.w = r12.x + r0.w;
  r1.z = min(r12.x, r1.z);
  r1.w = max(r12.x, r1.w);
  r6.xzw = ro_taahistory_read.SampleLevel(smp_linearclamp_s, r10.zw, 0).xyz;
  r3.w = r3.y * r3.x;
  r10.xyzw = r6.xzwx * r3.wwww + r11.xyzw;
  r0.w = r6.x + r0.w;
  r1.z = min(r6.x, r1.z);
  r1.w = max(r6.x, r1.w);
  r6.xzw = ro_taahistory_read.SampleLevel(smp_linearclamp_s, r9.zw, 0).xyz;
  r3.xy = r3.xy * r1.xy;
  r9.xyzw = r6.xzwx * r3.xxxx + r10.xyzw;
  r0.w = r6.x + r0.w;
  r1.z = min(r6.x, r1.z);
  r1.w = max(r6.x, r1.w);
  r5.y = r8.y;
  r5.xyzw = cb_prevresolutionscale.xyxy * r5.xyzy;
  r5.xyzw = max(float4(0,0,0,0), r5.xyzw);
  r5.xyzw = min(r5.xyzw, r0.xyxy);
  r6.xzw = ro_taahistory_read.SampleLevel(smp_linearclamp_s, r5.xy, 0).xyz;
  r9.xyzw = r6.xzwx * r6.yyyy + r9.xyzw;
  r0.w = r6.x + r0.w;
  r1.z = min(r6.x, r1.z);
  r1.w = max(r6.x, r1.w);
  r5.xyz = ro_taahistory_read.SampleLevel(smp_linearclamp_s, r5.zw, 0).xyz;
  r6.xyzw = r5.xyzx * r3.yyyy + r9.xyzw;
  r0.w = r5.x + r0.w;
  r1.z = min(r5.x, r1.z);
  r1.w = max(r5.x, r1.w);
  r3.xy = cb_prevresolutionscale.xy * r8.xy;
  r3.xy = max(float2(0,0), r3.xy);
  r0.xy = min(r3.xy, r0.xy);
  r3.xyw = ro_taahistory_read.SampleLevel(smp_linearclamp_s, r0.xy, 0).xyz;
  r0.x = r1.x * r1.y;
  r5.xyzw = r3.xywx * r0.xxxx + r6.xyzw;
  r0.x = r3.x + r0.w;
  r0.y = min(r3.x, r1.z);
  r0.w = max(r3.x, r1.w);
  r0.x = 0.111111112 * r0.x;
  r1.xyzw = max(r5.xyzw, r4.xyzw);
  r1.xyzw = min(r2.xyzw, r1.xyzw);
  r2.x = r2.w + -r4.w;
  r2.x = max(0.100000001, r2.x);
  r0.x = max(0.100000001, r0.x);
  r0.x = r2.x * r0.x;
  r0.y = r0.w + -r0.y;
  r0.y = max(0.100000001, r0.y);
  r0.w = max(0.100000001, r3.z);
  r0.y = r0.y * r0.w;
  r0.x = r0.x / r0.y;
  r0.x = saturate(-1 + r0.x);
  r0.y = r0.x * -2 + 3;
  r0.x = r0.x * r0.x;
  r0.x = -r0.y * r0.x + 1;
  r0.x = cb_taaamount * r0.x + 0.00999999978;
  r2.xyzw = r7.xyzw + -r1.wyzw;
  r1.xyzw = r0.xxxx * r2.xyzw + r1.xyzw;
// No code for instruction (needs manual fix):
  rw_taahistory_write[vThreadID.xy] = r1.xyz;
  r0.x = -r1.z * 0.5 + r1.w;
  r0.y = r1.z + r0.x;
  r0.x = -r1.y * 0.5 + r0.x;
  r0.w = r1.y + r0.x;
  r1.x = cmp(r0.w < cb_postfx_tonemapping_tonemappingparms.y);
  r1.xyzw = r1.xxxx ? cb_postfx_tonemapping_tonemappingcoeffsinverse0.xyzw : cb_postfx_tonemapping_tonemappingcoeffsinverse1.xyzw;
  r1.xy = r1.xy * r0.ww + r1.zw;
  r1.xw = r1.xx / r1.yy;
  r0.w = cmp(r0.y < cb_postfx_tonemapping_tonemappingparms.y);
  r2.xyzw = r0.wwww ? cb_postfx_tonemapping_tonemappingcoeffsinverse0.xyzw : cb_postfx_tonemapping_tonemappingcoeffsinverse1.xyzw;
  r0.yw = r2.xy * r0.yy + r2.zw;
  r1.y = r0.y / r0.w;
  r0.y = cmp(r0.x < cb_postfx_tonemapping_tonemappingparms.y);
  r2.xyzw = r0.yyyy ? cb_postfx_tonemapping_tonemappingcoeffsinverse0.xyzw : cb_postfx_tonemapping_tonemappingcoeffsinverse1.xyzw;
  r0.xy = r2.xy * r0.xx + r2.zw;
  r1.z = r0.x / r0.y;
  r0.xyzw = r1.xyzw / r0.zzzz;
// No code for instruction (needs manual fix):
  rw_taaresult[vThreadID.xy] = r0.xyz;
#endif
}