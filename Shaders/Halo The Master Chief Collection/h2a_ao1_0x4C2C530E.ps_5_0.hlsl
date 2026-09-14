// ---- Created with 3Dmigoto v1.3.16 on Tue Aug 04 12:24:13 2026

cbuffer CB_DYNAMIC_PASS_SSAO : register(b7)
{
  float4 PS_REG_SSAO_SCREEN : packoffset(c0);
  float4 PS_REG_SSAO_PARAMS : packoffset(c1);
  float4 PS_REG_SSAO_MV_1 : packoffset(c2);
  float4 PS_REG_SSAO_MV_2 : packoffset(c3);
  float4 PS_REG_SSAO_MV_3 : packoffset(c4);
  float4 SSAO_FRUSTUM_SCALE : packoffset(c5);
  float4 SSAO_TEX_COORD_SCALE : packoffset(c6);
  float4 PS_REG_SSAO_FRUSTUM_SCALE : packoffset(c7);
}

SamplerState PS_SAMPLERS_3__s : register(s3);
SamplerState PS_SAMPLERS_4__s : register(s4);
Texture2D<float4> PS_TEXTURES_2D_0_ : register(t0); //depth
Texture2D<float4> PS_TEXTURES_2D_1_ : register(t1); //depth 1/2 res
Texture2D<float4> PS_TEXTURES_2D_6_ : register(t6); //visiblity & edges


// 3Dmigoto declarations
#define cmp -
#include "./Includes/Common.hlsl"

void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  out float2 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4;
  uint4 bitmask, uiDest;
  float4 fDest;

#if HALO2_GTAO == 1
  // use as copy & upscale
  r0.x = PS_TEXTURES_2D_6_.GatherRed(PS_SAMPLERS_3__s, v1.xy, int2(-1, -1)).x;
#else
  r0.xy = SSAO_TEX_COORD_SCALE.zw + -SSAO_TEX_COORD_SCALE.xy;
  r0.zw = v1.xy + -r0.xy;
  r1.xy = SSAO_TEX_COORD_SCALE.yx + SSAO_TEX_COORD_SCALE.wz;
  r0.zw = r0.zw / r1.yx;
  r0.zw = floor(r0.zw);
  r0.zw = r0.zw * r1.yx + r0.xy;
  r1.z = PS_TEXTURES_2D_1_.SampleLevel(PS_SAMPLERS_3__s, r0.zw, 0).x;
  r1.w = PS_TEXTURES_2D_0_.SampleLevel(PS_SAMPLERS_3__s, v1.xy, 0).x;
  r2.x = r1.w + -r1.z;
  r2.z = abs(r2.x);
  r0.xy = r0.zw + r1.yx;
  r2.y = PS_TEXTURES_2D_1_.SampleLevel(PS_SAMPLERS_3__s, r0.xw, 0).x;
  r2.w = -r2.y + r1.w;
  r3.z = abs(r2.w);
  r2.w = cmp(r3.z < r2.z);
  r2.x = r0.z;
  r3.xy = r0.xw;
  r3.xz = r2.ww ? r3.xz : r2.xz;
  r2.x = PS_TEXTURES_2D_1_.SampleLevel(PS_SAMPLERS_3__s, r0.zy, 0).x;
  r2.z = -r2.x + r1.w;
  r4.z = abs(r2.z);
  r2.z = cmp(r4.z < r3.z);
  r4.xy = r0.zy;
  r0.zw = v1.yx + -r0.wz;
  r0.zw = r0.zw / r1.xy;
  r3.xyz = r2.zzz ? r4.xyz : r3.xyz;
  r1.x = PS_TEXTURES_2D_1_.SampleLevel(PS_SAMPLERS_3__s, r0.xy, 0).x;
  r1.y = r1.w + -r1.x;
  r1.x = r1.x * r0.z;
  r1.y = cmp(abs(r1.y) < r3.z);
  r0.xy = r1.yy ? r0.xy : r3.xy;
  r1.y = r2.x * r0.z;
  r2.xz = float2(1,1) + -r0.zw;
  r0.z = r1.z * r2.x + r1.y;
  r1.x = r2.y * r2.x + r1.x;
  r0.w = r1.x * r0.w;
  r0.z = r0.z * r2.z + r0.w;
  r0.z = r1.w + -r0.z;
  r0.w = 1.33333344e-007 + r1.w;
  r0.w = 0.100000016 / r0.w;
  r0.z = abs(r0.z) * r0.w;
  r0.z = cmp(0.00048828125 < r0.z);
  r0.xy = r0.zz ? r0.xy : v1.xy;
  float2 aoUv = r0.xy;
  #if HALO2_AO == 0
    r0.x = PS_TEXTURES_2D_6_.SampleLevel(PS_SAMPLERS_4__s, r0.xy, 0).x; // BRUH!!! Edges not even used...
#else
    #if HALO2_AO == 0
      const float edgeThres = 0.6;
    #else 
      const float edgeThres = 0.935;
    #endif

    // sample AO with blur
    r0.xy = PS_TEXTURES_2D_6_.SampleLevel(PS_SAMPLERS_4__s, aoUv, 0).xy;
    if (r0.y > edgeThres) { //edges
      // r0.x += PS_TEXTURES_2D_6_.GatherRed(PS_SAMPLERS_4__s, aoUv, int2(-1, 0));
      // r0.x += PS_TEXTURES_2D_6_.GatherRed(PS_SAMPLERS_4__s, aoUv, int2(1, 0));
      // r0.x += PS_TEXTURES_2D_6_.GatherRed(PS_SAMPLERS_4__s, aoUv, int2(0, -1));
      // r0.x += PS_TEXTURES_2D_6_.GatherRed(PS_SAMPLERS_4__s, aoUv, int2(0, 1));
      // r0.x += PS_TEXTURES_2D_6_.GatherRed(PS_SAMPLERS_4__s, aoUv, int2(-1, -1));
      // r0.x += PS_TEXTURES_2D_6_.GatherRed(PS_SAMPLERS_4__s, aoUv, int2(1, -1));
      // r0.x += PS_TEXTURES_2D_6_.GatherRed(PS_SAMPLERS_4__s, aoUv, int2(-1, 1));
      // r0.x += PS_TEXTURES_2D_6_.GatherRed(PS_SAMPLERS_4__s, aoUv, int2(1, 1));
      // r0.x /= 9.0; // average

      // 4 additional
      r0.x += PS_TEXTURES_2D_6_.GatherRed(PS_SAMPLERS_4__s, aoUv, int2(-1, 0));
      r0.x += PS_TEXTURES_2D_6_.GatherRed(PS_SAMPLERS_4__s, aoUv, int2(1, 0));
      r0.x += PS_TEXTURES_2D_6_.GatherRed(PS_SAMPLERS_4__s, aoUv, int2(0, -1));
      r0.x += PS_TEXTURES_2D_6_.GatherRed(PS_SAMPLERS_4__s, aoUv, int2(0, 1));
      r0.x /= 5.0; // average
    }
  #endif
  // r0.x = saturate(r0.x);
#endif

  r0.x = pow(r0.x, PS_REG_SSAO_PARAMS.x * GS.AmbientOcclusion); // strength
  r0.x = max(PS_REG_SSAO_PARAMS.y, r0.x); // minimum value
  o0.xy = r0.xx;

  return;
}