Texture2D<float4> t5 : register(t5);

Texture2D<float4> t4 : register(t4);

Texture2D<float4> t3 : register(t3);

Texture2D<float4> t2 : register(t2);

Texture2D<float4> t1 : register(t1);

Texture3D<float4> t0 : register(t0);
Texture2DArray<float4> FastNoise : register(t6);

SamplerState s0_s : register(s0);

cbuffer cb1 : register(b1)
{
  float4 cb1[140];
}

cbuffer cb0 : register(b0)
{
  float4 cb0[21];
}

#include "includes/SSAO.hlsli"

#define cmp -

void main(
  float4 v0 : TEXCOORD0,
  float4 v1 : SV_POSITION0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8,r9,r10,r11,r12;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = (int2)v1.xy;
  r1.xy = trunc(v1.xy);
  r1.xy = float2(0.5,0.5) + r1.xy;
  r1.xy = -cb1[121].xy + r1.xy;
  r1.xy = cb1[122].zw * r1.xy;
  r1.xy = r1.xy * float2(2,2) + float2(-1,-1);
  r1.zw = float2(1,-1) * r1.xy;
  r0.z = asuint(cb1[139].w) << 1;
  r2.xyz = (int3)r0.xyz & int3(63,63,63);
  r2.w = 0;
  r2.xy = t0.Load(r2.xyzw).yz;
  r0.w = 0;
  r0.z = t2.Load(r0.xyw).x;
  r2.z = r0.z * cb1[57].x + cb1[57].y;
  r2.w = r0.z * cb1[57].z + -cb1[57].w;
  r2.w = rcp(r2.w);
  r3.z = r2.z + r2.w;
  r4.xyz = t1.Load(r0.xyw).xyz;
  r4.xyz = r4.xyz * float3(2,2,2) + float3(-1,-1,-1);
  r2.z = dot(r4.xyz, r4.xyz);
  r2.z = rsqrt(r2.z);
  r4.xyz = r4.xyz * r2.zzz;
  r5.xyz = cb1[9].xyz * r4.yyy;
  r4.xyw = r4.xxx * cb1[8].xyz + r5.xyz;
  r4.xyz = r4.zzz * cb1[10].xyz + r4.xyw;
float2 pixelCenter = trunc(v1.xy) + 0.5f;
  float2 localPixel = pixelCenter - cb1[121].xy;
  uint frameIndex = asuint(cb1[139].y);
  float ao = 1.0f;
#if SSAO_ALGORITHM == 0
  ao = CalculateOriginalGameSSAO(r2.xy, r3.z, r1.zw, r1.xy, r4.xyz);
#else
  uint2 noisePixel = (uint2)localPixel & 127u;
  uint noiseSlice = frameIndex & 63u;
  float4 random = FastNoise.Load(int4(int2(noisePixel), (int)noiseSlice, 0));
#if SSAO_ALGORITHM == 1
  ao = r0.z > 0.0f ? CalculateVBAO(localPixel, r0.z, r3.z, r4.xyz, random) : 1.0f;
#else
  ao = 1.0f;
#endif
#endif
  r2.w = ao;
  r3.xyw = cb1[115].xyw * r1.www;
  r3.xyw = r1.zzz * cb1[114].xyw + r3.xyw;
  r3.xyw = r0.zzz * cb1[116].xyw + r3.xyw;
  r3.xyw = cb1[117].xyw + r3.xyw;
  r3.xy = r3.xy / r3.ww;
  r3.xy = r1.xy * float2(1,-1) + -r3.xy;
  r0.xy = t4.Load(r0.xyw).xy;
  r0.z = dot(r0.xy, r0.xy);
  r0.z = cmp(0 < r0.z);
  r0.xy = float2(-0.499992371,-0.499992371) + r0.xy;
  r0.xy = float2(4.00801611,4.00801611) * r0.xy;
  r0.xy = r0.zz ? r0.xy : r3.xy;
  r0.xy = r1.xy * float2(1,-1) + -r0.xy;
  r1.xy = cb1[125].xy * r0.xy;
  r1.xy = r1.zw * cb1[122].xy + -r1.xy;
  r0.w = dot(r1.xy, r1.xy);
  r0.w = sqrt(r0.w);
  r0.w = 0.0125000002 * r0.w;
  r2.y = min(1, r0.w);
  r0.w = max(abs(r0.x), abs(r0.y));
  r0.w = cmp(r0.w < 1);
  if (r0.w != 0) {
    r0.xy = r0.xy * cb1[123].xy + cb1[123].wz;
    r0.xy = cb1[126].xy * r0.xy;
    r1.xy = float2(0.5,0.5) + cb1[124].xy;
    r1.zw = cb1[125].xy + cb1[124].xy;
    r1.zw = float2(-0.5,-0.5) + r1.zw;
    r0.xy = max(r1.xy, r0.xy);
    r0.xy = min(r0.xy, r1.zw);
    r0.xy = cb0[1].zw * r0.xy;
    r0.xyw = t3.SampleLevel(s0_s, r0.xy, 0).xyz;
    r1.x = cmp(r0.y != 0.000000);
    r0.y = r0.w * 0.800000012 + r2.y;
    r0.w = ~(int)r0.z;
    r0.w = r1.x ? r0.w : 0;
    r2.x = 1;
    r2.xy = r0.ww ? r2.wx : r0.xy;
  } else {
    r2.xy = r2.wy;
  }
  r2.z = r0.z ? -r3.z : r3.z;
  o0.xyzw = r2.wxyz;
  return;
}
