cbuffer PerInstanceCB : register(b2)
{
  float4 cb_positiontoviewtexture : packoffset(c0);
  uint2 cb_postfx_luminance_exposureindex : packoffset(c1);
  float cb_view_white_level : packoffset(c1.z);
}

SamplerState smp_linearclamp_s : register(s0);
Texture2D<float4> ro_viewcolormap : register(t0);


// 3Dmigoto declarations
#define cmp -

#if 1 // Original shader.
void main(
  float4 v0 : SV_POSITION0,
  out float4 o0 : SV_TARGET0)
{
  float4 r0,r1,r2,r3;
  uint4 bitmask, uiDest;
  float4 fDest;

  // Box downsample.
  r0.xy = float2(0.5,0.5) * cb_positiontoviewtexture.zw;
  r0.xy = v0.xy * cb_positiontoviewtexture.zw + -r0.xy;
  r1.xw = cb_positiontoviewtexture.zw;
  r1.yz = float2(0,0);
  r0.zw = r1.xy + r0.xy;
  r2.xyzw = ro_viewcolormap.Sample(smp_linearclamp_s, r0.xy).xyzw;
  r0.xy = r0.zw + r1.zw;
  r3.xyzw = ro_viewcolormap.Sample(smp_linearclamp_s, r0.zw).xyzw;
  r2.xyzw = r3.xyzw + r2.xyzw;
  r0.zw = r0.xy + -r1.xy;
  r1.xyzw = ro_viewcolormap.Sample(smp_linearclamp_s, r0.xy).xyzw;
  r0.xyzw = ro_viewcolormap.Sample(smp_linearclamp_s, r0.zw).xyzw;
  r0.xyzw = r2.xyzw + r0.xyzw;
  r0.xyzw = r0.xyzw + r1.xyzw;
  o0.xyzw = float4(0.25,0.25,0.25,0.25) * r0.xyzw;
  return;
}
#else // Experimental.
void main(
  float4 v0 : SV_POSITION0,
  out float4 o0 : SV_TARGET0)
{
  float4 r0,r1,r2,r3;
  uint4 bitmask, uiDest;
  float4 fDest;
  
  // Custom filter, 13 bilinear texture fetches.
  // https://www.iryoku.com/next-generation-post-processing-in-call-of-duty-advanced-warfare/
  //
  // a - b - c
  // - d - e -
  // f - g - h
  // - i - j -
  // k - l - m
  //

  // Center.
  const float3 g = ro_viewcolormap.SampleLevel(smp_linearclamp_s, v0.xy * cb_positiontoviewtexture.zw, 0.0).xyz;

  // Inner ring.
  const float4 d = ro_viewcolormap.SampleLevel(smp_linearclamp_s, (v0.xy + float2(-0.5, -0.5)) * cb_positiontoviewtexture.zw, 0.0);
  const float4 e = ro_viewcolormap.SampleLevel(smp_linearclamp_s, (v0.xy + float2(0.5, -0.5)) * cb_positiontoviewtexture.zw, 0.0);
  const float4 i = ro_viewcolormap.SampleLevel(smp_linearclamp_s, (v0.xy + float2(-0.5, 0.5)) * cb_positiontoviewtexture.zw, 0.0);
  const float4 j = ro_viewcolormap.SampleLevel(smp_linearclamp_s, (v0.xy + float2(0.5, 0.5)) * cb_positiontoviewtexture.zw, 0.0);

  // Outer ring.
  const float3 a = ro_viewcolormap.SampleLevel(smp_linearclamp_s, (v0.xy + float2(-1.5, -1.5)) * cb_positiontoviewtexture.zw, 0.0).xyz;
  const float3 b = ro_viewcolormap.SampleLevel(smp_linearclamp_s, (v0.xy + float2(0.0, -1.5)) * cb_positiontoviewtexture.zw, 0.0).xyz;
  const float3 c = ro_viewcolormap.SampleLevel(smp_linearclamp_s, (v0.xy + float2(1.5, -1.5)) * cb_positiontoviewtexture.zw, 0.0).xyz;
  const float3 f = ro_viewcolormap.SampleLevel(smp_linearclamp_s, (v0.xy + float2(-1.5, 0.0)) * cb_positiontoviewtexture.zw, 0.0).xyz;
  const float3 h = ro_viewcolormap.SampleLevel(smp_linearclamp_s, (v0.xy + float2(1.5, 0.0)) * cb_positiontoviewtexture.zw, 0.0).xyz;
  const float3 k = ro_viewcolormap.SampleLevel(smp_linearclamp_s, (v0.xy + float2(-1.5, 1.5)) * cb_positiontoviewtexture.zw, 0.0).xyz;
  const float3 l = ro_viewcolormap.SampleLevel(smp_linearclamp_s, (v0.xy + float2(0.0, 1.5)) * cb_positiontoviewtexture.zw, 0.0).xyz;
  const float3 m = ro_viewcolormap.SampleLevel(smp_linearclamp_s, (v0.xy + float2(1.5, 1.5)) * cb_positiontoviewtexture.zw, 0.0).xyz;

  //

  // Apply weighted distribution.
  o0.xyz = g * 0.125;
  o0.w = 0.0;
  o0.xyzw += (d + e + i + j) * float4(0.125, 0.125, 0.125, 0.25); // Alpha is the luma used to build histogram.
  o0.xyz += (b + f + h + l) * 0.0625;
  o0.xyz += (a + c + k + m) * 0.03125;

  return;
}
#endif