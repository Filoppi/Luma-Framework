// ---- Created with 3Dmigoto v1.3.16 on Wed Jun 24 13:04:31 2026
Texture3D<float4> t4 : register(t4);

Texture2D<float4> t3 : register(t3);

Texture2D<float4> t2 : register(t2);

Texture2D<float4> t1 : register(t1);

Texture2D<float4> t0 : register(t0);

SamplerState s4_s : register(s4);

SamplerState s3_s : register(s3);

SamplerState s2_s : register(s2);

SamplerState s1_s : register(s1);

SamplerState s0_s : register(s0);

cbuffer cb13 : register(b13)
{
  float4 cb13[3];
}

cbuffer cb2 : register(b2)
{
  float4 cb2[11];
}

// 3Dmigoto declarations
#define cmp -
#include "iw7_common.hlsl"

void main(
  float4 v0 : SV_POSITION0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_TARGET0,
  out float o1 : SV_TARGET1)
{
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8,r9,r10,r11;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = cb2[2].xy + v1.xy;
  TM_UV(r0.xy);

  r1.xyzw = cmp(asint(cb2[3].zzww) == int4(1,3,1,3));
  r0.w = (int)r1.y | (int)r1.x;
  if (r1.z != 0) {
    r1.x = -cb2[4].y * 2 + r0.y;
    r1.x = saturate(1 + r1.x);
    r1.x = r1.x * r1.x;
    r2.x = cb2[3].x * cb2[0].x;
    r2.z = -cb2[0].w * cb2[3].y + r0.y;
    r0.z = 1;
    r2.xy = r2.xz + r0.xz;
    r2.zw = t1.Sample(s1_s, r2.xy).xy;
    r2.xy = float2(0.5,0.5) * r2.xy;
    r2.xy = t1.Sample(s1_s, r2.xy).xy;
    r2.xyzw = cb2[0].xxyy * r2.xyzw;
    r2.xy = float2(0.5,0.5) * r2.xy;
    r2.xy = r2.zw * float2(0.5,0.5) + r2.xy;
    r0.z = cb2[4].x * r1.x;
    r1.xz = saturate(r2.xy * r0.zz + r0.xy);
    r2.xy = r0.ww ? r1.xz : r0.xy;
    r2.zw = float2(1,1) + -cb2[5].xz;
    r0.z = cmp(0 < cb2[5].w);
    if (r0.z != 0) {
      r3.xy = cb2[6].xy * float2(0.5,0.5) + float2(0.5,0.5);
      r4.xy = cb2[1].xy * cb2[1].wz;
      r4.z = 1;
      r3.zw = r4.zy * r3.xy;
      r5.xy = r2.xy * r4.zy + -r3.zw;
      r0.z = dot(r5.xy, r5.xy);
      // r0.z = (uint)r0.z >> 1;
      // r0.z = (int)r0.z + 0x1fbd1df5;
      int i0 = asint(r0.w);
      i0 = i0 >> 1;
      i0 = i0 + 0x1fbd1df5;
      r0.z = asfloat(i0);
      
      r3.z = -3 * cb2[5].w;
      r5.xy = r5.xy / r0.zz;
      r0.z = r3.z * r0.z;
      r4.w = r0.z * r0.z;
      r4.w = 0.0662999973 * r4.w;
      r4.w = abs(r0.z) * -0.178399995 + -r4.w;
      r4.w = 1.03009999 + r4.w;
      r0.z = r4.w * r0.z;
      r5.xy = r5.xy * r0.zz;
      r5.xy = r5.xy * r3.ww;
      r0.z = r3.z * r3.w;
      r3.z = r0.z * r0.z;
      r3.z = 0.0662999973 * r3.z;
      r3.z = abs(r0.z) * -0.178399995 + -r3.z;
      r3.z = 1.03009999 + r3.z;
      r0.z = r3.z * r0.z;
      r3.zw = r5.xy / r0.zz;
      r3.xy = r3.xy * r4.zy + r3.zw;
      r2.xy = r3.xy * r4.zx;
    }
    r3.xyzw = r2.xyxy * float4(2,2,2,2) + float4(-1,-1,-1,-1);
    r3.xyzw = -cb2[6].xyxy + r3.xyzw;
    r0.z = dot(r3.zw, r3.zw);
    // r0.z = (uint)r0.z >> 1;
    // r0.z = (int)r0.z + 0x1fbd1df5;
    int i1 = asint(r0.z);
    i1 = i1 >> 1;
    i1 = i1 + 0x1fbd1df5;
    r0.z = asfloat(i1);

    r0.z = max(0, r0.z);
    r4.x = cmp(r2.z < r0.z);
    if (r4.x != 0) {
      r4.xy = float2(1,1) + -r2.zw;
      r2.z = r0.z + -r2.z;
      r4.x = 1 / r4.x;
      r2.z = saturate(r4.x * r2.z);
      r4.x = r2.z * -2 + 3;
      r2.z = r2.z * r2.z;
      r2.z = r4.x * r2.z;
      r2.z = cb2[5].y * r2.z;
      r3.xyzw = r2.zzzz * r3.xyzw;
      r2.z = 1 + -cb2[6].z;
      r4.x = 1 + -r0.z;
      r0.z = r4.x / r0.z;
      r0.z = r0.z * cb2[6].z + r2.z;
      r3.xyzw = r3.xyzw * r0.zzzz;
      r4.xz = r2.yx * float2(1024,1024) + float2(-512,-512);
      r5.xy = float2(0.554549694,0.308517009) * r4.zx;
      r5.xy = frac(r5.xy);
      r4.xz = r4.zx * r5.xy + r4.xz;
      r0.z = r4.x * r4.z;
      r0.z = frac(r0.z);
      r0.z = r0.z * 2 + -1;
      r5.xyzw = r0.zzzz * float4(0.100000001,0.100000001,0.100000001,0.100000001) + float4(-1,-0.600000024,-0.199999988,0.200000048);
      r6.xyzw = -r3.zwzw * r5.xxyy + r2.xyxy;
      r4.xzw = t0.SampleLevel(s0_s, r6.xy, 0).xyz;
      r0.z = min(1, -r5.x);
      r7.x = r0.z * r4.y + r2.w;
      r8.xyz = r5.xzy * cb2[5].zzz + float3(1,1,1);
      r7.y = r8.x;
      r7.z = 1 + -cb2[5].z;
      r6.xyz = t0.SampleLevel(s0_s, r6.zw, 0).xyz;
      r9.xy = -r5.yz * r4.yy + r2.ww;
      r9.z = r8.z;
      r9.w = 1 + -cb2[5].z;
      r6.xyz = r9.xzw * r6.xyz;
      r4.xzw = r7.xyz * r4.xzw + r6.xyz;
      r6.xyz = r9.xzw + r7.xyz;
      r3.xyzw = -r3.xyzw * r5.zzww + r2.xyxy;
      r5.xyz = t0.SampleLevel(s0_s, r3.xy, 0).xyz;
      r8.xz = r9.yw;
      r4.xzw = r8.xyz * r5.xyz + r4.xzw;
      r5.xyz = r8.xyz + r6.xyz;
      r3.xyz = t0.SampleLevel(s0_s, r3.zw, 0).xyz;
      r6.y = r5.w * -cb2[5].z + 1;
      r6.z = r5.w * r4.y + r2.w;
      r6.x = 1 + -cb2[5].z;
      r3.xyz = r6.xyz * r3.xyz + r4.xzw;
      r4.xyz = r6.xyz + r5.xyz;
      r3.xyz = r3.xyz / r4.xyz;
    } else {
      r3.xyz = t0.SampleLevel(s0_s, r2.xy, 0).xyz;
    }
    r0.xy = r1.xz;
    r0.z = 0;
    r1.x = 0;
    r2.x = 0;
  } else {
    r1.z = cmp(asint(cb2[3].w) == 2);
    if (r1.z != 0) {
      r1.z = cb2[3].y * cb2[0].z;
      r1.z = 15 * r1.z;
      r1.z = floor(r1.z);
      r4.xyz = float3(0.0666666701,5.39166689,1.82500005) * r1.zzz;
      r5.xyz = cmp(r4.yzy >= -r4.yzy);
      r4.yzw = frac(abs(r4.yzy));
      r4.yzw = r5.xyz ? r4.yzw : -r4.wzw;
      r4.yzw = float3(8,8,60.2400017) * r4.yzw;
      r4.yz = r0.xy * float2(0.600000024,0.400000006) + r4.yz;
      r1.z = floor(r4.w);
      r5.x = 0.0166002642 * r1.z;
      r5.y = 0;
      r5.xy = r0.xy * float2(0.25,1) + r5.xy;
      r5.xy = t2.Sample(s2_s, r5.xy).zw;
      r1.z = sin(r4.x);
      r1.z = max(0, r1.z);
      r1.z = r5.y * r1.z;
      r2.w = saturate(cb2[3].x + -r5.x);
      r4.xy = t2.Sample(s2_s, r4.yz).xy;
      r4.xy = r4.xy * float2(2,2) + float2(-1,-1);
      r1.z = r2.w * r1.z;
      r4.zw = r2.ww * float2(0.100000001,0.0329999998) + r1.zz;
      r4.xy = r4.xy * r4.zw;
      r1.z = r4.x * 0.5 + cb2[3].x;
      r1.z = cmp(r5.x < r1.z);
      r4.xy = r1.zz ? r4.xy : 0;
      r4.xy = r4.xy + r0.xy;
      r4.zw = r0.ww ? r4.xy : r0.xy;
      r1.z = 1 + -cb2[5].x;
      r2.w = cmp(0 < cb2[5].w);
      if (r2.w != 0) {
        r5.xy = cb2[6].xy * float2(0.5,0.5) + float2(0.5,0.5);
        r6.xy = cb2[1].xy * cb2[1].wz;
        r6.z = 1;
        r5.zw = r6.zy * r5.xy;
        r7.xy = r4.zw * r6.zy + -r5.zw;
        r2.w = dot(r7.xy, r7.xy);
        // r2.w = (uint)r2.w >> 1;
        // r2.w = (int)r2.w + 0x1fbd1df5;
        int i2 = asint(r2.w);
        i2 = i2 >> 1;
        i2 = i2 + 0x1fbd1df5;
        r2.w = asfloat(i2);

        r3.w = -3 * cb2[5].w;
        r7.xy = r7.xy / r2.ww;
        r2.w = r3.w * r2.w;
        r5.z = r2.w * r2.w;
        r5.z = 0.0662999973 * r5.z;
        r5.z = abs(r2.w) * -0.178399995 + -r5.z;
        r5.z = 1.03009999 + r5.z;
        r2.w = r5.z * r2.w;
        r7.xy = r7.xy * r2.ww;
        r7.xy = r7.xy * r5.ww;
        r2.w = r3.w * r5.w;
        r3.w = r2.w * r2.w;
        r3.w = 0.0662999973 * r3.w;
        r3.w = abs(r2.w) * -0.178399995 + -r3.w;
        r3.w = 1.03009999 + r3.w;
        r2.w = r3.w * r2.w;
        r5.zw = r7.xy / r2.ww;
        r5.xy = r5.xy * r6.zy + r5.zw;
        r4.zw = r5.xy * r6.zx;
      }
      r5.xyzw = r4.zwzw * float4(2,2,2,2) + float4(-1,-1,-1,-1);
      r5.xyzw = -cb2[6].xyxy + r5.xyzw;
      r2.w = dot(r5.zw, r5.zw);
      // r2.w = (uint)r2.w >> 1;
      // r2.w = (int)r2.w + 0x1fbd1df5;
      int i3 = asint(r2.w);
      i3 = i3 >> 1;
      i3 = i3 + 0x1fbd1df5;
      r2.w = asfloat(i3);

      r2.w = max(0, r2.w);
      r3.w = cmp(r1.z < r2.w);
      if (r3.w != 0) {
        r6.xw = float2(1,1) + -cb2[5].zz;
        r3.w = 1 + -r1.z;
        r1.z = r2.w + -r1.z;
        r3.w = 1 / r3.w;
        r1.z = saturate(r3.w * r1.z);
        r3.w = r1.z * -2 + 3;
        r1.z = r1.z * r1.z;
        r1.z = r3.w * r1.z;
        r1.z = cb2[5].y * r1.z;
        r5.xyzw = r1.zzzz * r5.xyzw;
        r1.z = 1 + -cb2[6].z;
        r3.w = 1 + -r2.w;
        r2.w = r3.w / r2.w;
        r1.z = r2.w * cb2[6].z + r1.z;
        r5.xyzw = r5.xyzw * r1.zzzz;
        r7.xy = r4.wz * float2(1024,1024) + float2(-512,-512);
        r7.zw = float2(0.554549694,0.308517009) * r7.yx;
        r7.zw = frac(r7.zw);
        r7.xy = r7.yx * r7.zw + r7.xy;
        r1.z = r7.x * r7.y;
        r1.z = frac(r1.z);
        r1.z = r1.z * 2 + -1;
        r7.xyzw = r1.zzzz * float4(0.100000001,0.100000001,0.100000001,0.100000001) + float4(-1,-0.600000024,-0.199999988,0.200000048);
        r8.xyzw = -r5.zwzw * r7.xxyy + r4.zwzw;
        r9.xyz = t0.SampleLevel(s0_s, r8.xy, 0).xyz;
        r1.z = min(1, -r7.x);
        r2.w = 1 + -r6.x;
        r6.y = r1.z * r2.w + r6.x;
        r10.xyz = r7.xzy * cb2[5].zzz + float3(1,1,1);
        r6.z = r10.x;
        r8.xyz = t0.SampleLevel(s0_s, r8.zw, 0).xyz;
        r11.xy = -r7.yz * r2.ww + r6.xx;
        r11.z = r10.z;
        r11.w = 1 + -cb2[5].z;
        r8.xyz = r11.xzw * r8.xyz;
        r8.xyz = r6.yzw * r9.xyz + r8.xyz;
        r6.yzw = r11.xzw + r6.yzw;
        r5.xyzw = -r5.xyzw * r7.zzww + r4.zwzw;
        r7.xyz = t0.SampleLevel(s0_s, r5.xy, 0).xyz;
        r10.xz = r11.yw;
        r7.xyz = r10.xyz * r7.xyz + r8.xyz;
        r6.yzw = r10.xyz + r6.yzw;
        r5.xyz = t0.SampleLevel(s0_s, r5.zw, 0).xyz;
        r8.y = r7.w * -cb2[5].z + 1;
        r8.z = r7.w * r2.w + r6.x;
        r8.x = 1 + -cb2[5].z;
        r5.xyz = r8.xyz * r5.xyz + r7.xyz;
        r6.xyz = r8.xyz + r6.yzw;
        r3.xyz = r5.xyz / r6.xyz;
      } else {
        r3.xyz = t0.SampleLevel(s0_s, r4.zw, 0).xyz;
      }
      r0.xy = r4.xy;
      r0.z = 0;
      r1.x = 0;
      r2.x = 0;
    } else {
      r1.z = cmp(asint(cb2[3].w) == 3);
      if (r1.z != 0) {
        r4.xyz = float3(1234,0.333333343,0.200000003) * cb2[0].www;
        r4.xz = frac(r4.xz);
        r1.z = 12345 * r4.x;
        r1.z = r0.y * 100 + r1.z;
        r1.z = 0.103100002 * r1.z;
        r1.z = frac(r1.z);
        r2.w = 19.1900005 + r1.z;
        r2.w = dot(r1.zzz, r2.www);
        r1.z = r2.w + r1.z;
        r2.w = r1.z + r1.z;
        r1.z = r2.w * r1.z;
        r1.z = frac(r1.z);
        r2.w = 123.449997 * r1.z;
        r2.w = frac(r2.w);
        r2.w = cb2[3].y * r2.w;
        r3.w = r2.w + r2.w;
        r4.x = 0.300000012 + -r0.y;
        r4.x = 3 * r4.x;
        r4.x = frac(r4.x);
        r4.w = 2.66666675 * r4.x;
        r4.w = min(1, r4.w);
        r4.w = 1 + -r4.w;
        r5.xy = float2(5,-14.4269505) * r4.ww;
        r4.w = exp2(r5.y);
        r4.w = r5.x * r4.w;
        r4.w = cb2[3].x * r4.w;
        r2.w = 0.00828157365 * r2.w;
        r2.w = r4.w * r1.z + r2.w;
        r4.x = r4.x * 0.333333343 + -0.119999997;
        r4.x = -abs(r4.x) * 20 + 1;
        r4.x = max(0, r4.x);
        r4.x = r4.x * r4.x;
        r4.x = cb2[3].x * r4.x;
        r0.z = r4.x * 0.400000006 + r3.w;
        r5.x = r2.w + r0.x;
        r2.w = floor(r4.y);
        r2.w = cb2[0].w * 0.333333343 + -r2.w;
        r4.xy = float2(-43.2808495,30) * r2.ww;
        r2.w = exp2(r4.x);
        r2.w = saturate(-0.00999999978 + r2.w);
        r3.w = cos(r4.y);
        r2.w = -r3.w * r2.w;
        r2.w = cb2[4].x * r2.w;
        r3.w = 0.600000024 * r2.w;
        r4.x = cmp(0.949999988 < cb2[4].x);
        r2.w = r2.w * 0.600000024 + r4.z;
        r2.w = -0.5 + r2.w;
        r2.w = r4.x ? r2.w : r3.w;
        r2.w = r2.w * 1.0869565 + r0.y;
        r2.w = 0.920000017 * r2.w;
        r2.w = frac(r2.w);
        r2.xyz = float3(262.5,1.0869565,525) * r2.www;
        r3.w = cmp(0.920000017 < r2.w);
        r2.w = r2.w * 1.0869565 + -1;
        r2.w = 12.5 * r2.w;
        r1.x = r3.w ? r2.w : 0;
        r2.z = floor(r2.z);
        r2.z = r2.z * 0.00207039341 + -r2.y;
        r5.y = cb2[4].z * r2.z + r2.y;
        if (r0.w != 0) {
          r0.w = cmp(cb2[4].y == 0.000000);
          r0.w = r0.w ? 0 : r0.z;
          r4.x = r0.w * 4 + cb2[4].y;
          r4.y = 1 + r0.z;
          r4.xyzw = float4(0.00207039341,-0.000517598353,0.00207039341,0.000517598353) * r4.xyxy;
          r2.y = 1 + r1.z;
          r2.zw = -r4.xy * r2.yy + r5.xy;
          r6.xyz = t0.Sample(s0_s, r2.zw).xyz;
          r2.z = 0.289000005 * r6.y;
          r2.z = r6.x * -0.147 + -r2.z;
          r7.yw = r6.zz * float2(0.43599999,0.43599999) + r2.zz;
          r2.z = 0.514999986 * r6.y;
          r2.z = r6.x * 0.61500001 + -r2.z;
          r7.xz = -r6.zz * float2(0.100000001,0.100000001) + r2.zz;
          r2.zw = -r4.zw * r1.zz + r5.xy;
          r6.xyz = t0.Sample(s0_s, r2.zw).xyz;
          r2.z = 0.289000005 * r6.y;
          r2.z = r6.x * -0.147 + -r2.z;
          r8.yw = r6.zz * float2(0.43599999,0.43599999) + r2.zz;
          r2.z = 0.514999986 * r6.y;
          r2.z = r6.x * 0.61500001 + -r2.z;
          r8.xz = -r6.zz * float2(0.100000001,0.100000001) + r2.zz;
          r6.xyz = t0.Sample(s0_s, r5.xy).xyz;
          r2.z = dot(r6.xyz, float3(0.298999995,0.587000012,0.114));
          r2.w = 0.289000005 * r6.y;
          r2.w = r6.x * -0.147 + -r2.w;
          r9.yw = r6.zz * float2(0.43599999,0.43599999) + r2.ww;
          r2.w = 0.514999986 * r6.y;
          r2.w = r6.x * 0.61500001 + -r2.w;
          r9.xz = -r6.zz * float2(0.100000001,0.100000001) + r2.ww;
          r4.xy = r4.xy * r1.zz + r5.xy;
          r6.xyz = t0.Sample(s0_s, r4.xy).xyz;
          r1.z = 0.289000005 * r6.y;
          r1.z = r6.x * -0.147 + -r1.z;
          r10.yw = r6.zz * float2(0.43599999,0.43599999) + r1.zz;
          r1.z = 0.514999986 * r6.y;
          r1.z = r6.x * 0.61500001 + -r1.z;
          r10.xz = -r6.zz * float2(0.100000001,0.100000001) + r1.zz;
          r2.yw = r4.zw * r2.yy + r5.xy;
          r4.xyz = t0.Sample(s0_s, r2.yw).xyz;
          r1.z = 0.289000005 * r4.y;
          r1.z = r4.x * -0.147 + -r1.z;
          r6.yw = r4.zz * float2(0.43599999,0.43599999) + r1.zz;
          r1.z = 0.514999986 * r4.y;
          r1.z = r4.x * 0.61500001 + -r1.z;
          r6.xz = -r4.zz * float2(0.100000001,0.100000001) + r1.zz;
          r0.w = r0.w * 0.200000003 + 0.200000003;
          r4.xyzw = r8.xyzw + r7.xyzw;
          r4.xyzw = r4.xyzw + r9.xyzw;
          r4.xyzw = r4.xyzw + r10.xyzw;
          r4.xyzw = r4.xyzw + r6.xyzw;
          r4.xyzw = r4.xyzw * r0.wwww;
          r0.w = -r4.y * 0.395000011 + r2.z;
          r3.y = -r4.z * 0.58099997 + r0.w;
          r3.xz = r4.xw * float2(1.13999999,2.03200006) + r2.zz;
        } else {
          r2.yz = float2(1,1) + -cb2[5].xz;
          r0.w = cmp(0 < cb2[5].w);
          if (r0.w != 0) {
            r4.xy = cb2[6].xy * float2(0.5,0.5) + float2(0.5,0.5);
            r6.xy = cb2[1].xy * cb2[1].wz;
            r6.z = 1;
            r4.zw = r6.zy * r4.xy;
            r5.zw = r0.xy * r6.zy + -r4.zw;
            r0.w = dot(r5.zw, r5.zw);
            // r0.w = (uint)r0.w >> 1;
            // r0.w = (int)r0.w + 0x1fbd1df5;
            int i4 = asint(r0.w);
            i4 = i4 >> 1;
            i4 = i4 + 0x1fbd1df5;
            r0.w = asfloat(i4);

            r1.z = -3 * cb2[5].w;
            r5.zw = r5.zw / r0.ww;
            r0.w = r1.z * r0.w;
            r2.w = r0.w * r0.w;
            r2.w = 0.0662999973 * r2.w;
            r2.w = abs(r0.w) * -0.178399995 + -r2.w;
            r2.w = 1.03009999 + r2.w;
            r0.w = r2.w * r0.w;
            r5.zw = r5.zw * r0.ww;
            r5.zw = r5.zw * r4.ww;
            r0.w = r1.z * r4.w;
            r1.z = r0.w * r0.w;
            r1.z = 0.0662999973 * r1.z;
            r1.z = abs(r0.w) * -0.178399995 + -r1.z;
            r1.z = 1.03009999 + r1.z;
            r0.w = r1.z * r0.w;
            r4.zw = r5.zw / r0.ww;
            r4.xy = r4.xy * r6.zy + r4.zw;
            r0.xy = r4.xy * r6.zx;
          }
          r4.xyzw = r0.xyxy * float4(2,2,2,2) + float4(-1,-1,-1,-1);
          r4.xyzw = -cb2[6].xyxy + r4.xyzw;
          r0.w = dot(r4.zw, r4.zw);
          // r0.w = (uint)r0.w >> 1;
          // r0.w = (int)r0.w + 0x1fbd1df5;
          int i5 = asint(r0.w);
          i5 = i5 >> 1;
          i5 = i5 + 0x1fbd1df5;
          r0.w = asfloat(i5);

          r0.w = max(0, r0.w);
          r1.z = cmp(r2.y < r0.w);
          if (r1.z != 0) {
            r5.zw = float2(1,1) + -r2.yz;
            r1.z = r0.w + -r2.y;
            r2.y = 1 / r5.z;
            r1.z = saturate(r2.y * r1.z);
            r2.y = r1.z * -2 + 3;
            r1.z = r1.z * r1.z;
            r1.z = r2.y * r1.z;
            r1.z = cb2[5].y * r1.z;
            r4.xyzw = r1.zzzz * r4.xyzw;
            r1.z = 1 + -cb2[6].z;
            r2.y = 1 + -r0.w;
            r0.w = r2.y / r0.w;
            r0.w = r0.w * cb2[6].z + r1.z;
            r4.xyzw = r4.xyzw * r0.wwww;
            r2.yw = r0.yx * float2(1024,1024) + float2(-512,-512);
            r6.xy = float2(0.554549694,0.308517009) * r2.wy;
            r6.xy = frac(r6.xy);
            r2.yw = r2.wy * r6.xy + r2.yw;
            r0.w = r2.y * r2.w;
            r0.w = frac(r0.w);
            r0.w = r0.w * 2 + -1;
            r6.xyzw = r0.wwww * float4(0.100000001,0.100000001,0.100000001,0.100000001) + float4(-1,-0.600000024,-0.199999988,0.200000048);
            r7.xyzw = -r4.zwzw * r6.xxyy + r0.xyxy;
            r8.xyz = t0.SampleLevel(s0_s, r7.xy, 0).xyz;
            r0.w = min(1, -r6.x);
            r9.x = r0.w * r5.w + r2.z;
            r10.xyz = r6.xzy * cb2[5].zzz + float3(1,1,1);
            r9.y = r10.x;
            r9.z = 1 + -cb2[5].z;
            r7.xyz = t0.SampleLevel(s0_s, r7.zw, 0).xyz;
            r11.xy = -r6.yz * r5.ww + r2.zz;
            r11.z = r10.z;
            r11.w = 1 + -cb2[5].z;
            r7.xyz = r11.xzw * r7.xyz;
            r7.xyz = r9.xyz * r8.xyz + r7.xyz;
            r8.xyz = r11.xzw + r9.xyz;
            r4.xyzw = -r4.xyzw * r6.zzww + r0.xyxy;
            r6.xyz = t0.SampleLevel(s0_s, r4.xy, 0).xyz;
            r10.xz = r11.yw;
            r6.xyz = r10.xyz * r6.xyz + r7.xyz;
            r7.xyz = r10.xyz + r8.xyz;
            r4.xyz = t0.SampleLevel(s0_s, r4.zw, 0).xyz;
            r8.y = r6.w * -cb2[5].z + 1;
            r8.z = r6.w * r5.w + r2.z;
            r8.x = 1 + -cb2[5].z;
            r2.yzw = r8.xyz * r4.xyz + r6.xyz;
            r4.xyz = r8.xyz + r7.xyz;
            r3.xyz = r2.yzw / r4.xyz;
          } else {
            r3.xyz = t0.SampleLevel(s0_s, r0.xy, 0).xyz;
          }
        }
        r0.xy = r5.xy;
      } else {
        r3.xyz = float3(0,0,0);
        r0.z = 0;
        r1.x = 0;
        r2.x = 0;
      }
    }
  }

  r2.yzw = t3.Sample(s3_s, r0.xy).xyz;
  r2.yzw = cb2[10].xxx * GS.Bloom * r2.yzw;
  r2.yzw = cb2[10].yyy * r3.xyz + r2.yzw;
  TM_Color(r2.yzw);

  // r3.xyz = cmp(r2.yzw < cb13[0].xxx);
  // r4.xyzw = r3.xxxx ? cb13[2].xyzw : cb13[1].xyzw;
  // r3.xw = r4.xy * r2.yy + r4.zw;
  // r4.x = saturate(r3.x / r3.w);
  // r5.xyzw = r3.yyyy ? cb13[2].xyzw : cb13[1].xyzw;
  // r2.yz = r5.xy * r2.zz + r5.zw;
  // r4.y = saturate(r2.y / r2.z);
  // r3.xyzw = r3.zzzz ? cb13[2].xyzw : cb13[1].xyzw;
  // r2.yz = r3.xy * r2.ww + r3.zw;
  // r4.z = saturate(r2.y / r2.z);
  TM_Rolloff();

  // r3.x = saturate(dot(r4.xyz, cb2[7].xyz));
  // r3.y = saturate(dot(r4.xyz, cb2[8].xyz));
  // r3.z = saturate(dot(r4.xyz, cb2[9].xyz));
  TM_LumaThingy(7);

  // r2.yzw = log2(r3.xyz);
  // r2.yzw = float3(0.416666657,0.416666657,0.416666657) * r2.yzw;
  // r2.yzw = exp2(r2.yzw);
  // r2.yzw = r2.yzw * float3(1.05499995,1.05499995,1.05499995) + float3(-0.0549999997,-0.0549999997,-0.0549999997);
  // r2.yzw = max(float3(0,0,0), r2.yzw);
  TM_Gamma();

  // r2.yzw = r2.yzw * float3(0.96875,0.96875,0.96875) + float3(0.015625,0.015625,0.015625);
  // r3.xyz = t4.SampleLevel(s4_s, r2.yzw, 0).xyz;
  TM_LUT(t4, s4_s);

  TM_Upgrade();
  r3.xyz = tmi.r0;

  r0.w = cmp(asint(cb2[3].z) == 2);
  r0.w = (int)r1.y | (int)r0.w;
  r0.w = r0.w ? r1.w : 0;
  if (r0.w != 0) {
    r0.w = 8 * r0.x;
    r0.x = r0.y * 483 + r0.x;
    r1.y = 123.779999 * cb2[0].w;
    r1.y = frac(r1.y);
    r1.y = 12345.5996 * r1.y;
    r0.x = r0.x * 234.5 + r1.y;
    r1.y = floor(r0.x);
    r0.x = frac(r0.x);
    r1.z = 0.103100002 * r1.y;
    r1.z = frac(r1.z);
    r1.yw = float2(1,19.1900005) + r1.yz;
    r1.w = dot(r1.zzz, r1.www);
    r1.z = r1.z + r1.w;
    r1.w = r1.z + r1.z;
    r1.z = r1.w * r1.z;
    r1.y = 0.103100002 * r1.y;
    r1.yz = frac(r1.yz);
    r1.w = 19.1900005 + r1.y;
    r1.w = dot(r1.yyy, r1.www);
    r1.y = r1.y + r1.w;
    r1.w = r1.y + r1.y;
    r1.y = r1.w * r1.y;
    r1.y = frac(r1.y);
    r1.w = 1 + -r0.x;
    r1.z = r1.z * r1.w;
    r0.x = r1.y * r0.x + r1.z;
    r1.y = cb2[4].z * 0.300000012 + 1;
    r1.yzw = r3.xyz * r1.yyy;
    r2.y = -0.699999988 + r1.x;
    r2.y = 4 * abs(r2.y);
    r2.y = min(1, r2.y);
    r2.y = 1 + -r2.y;
    r2.z = cmp(r1.x >= 0.00100000005);
    r2.z = r2.z ? 1.000000 : 0;
    r2.y = r2.y * r2.z;
    r2.y = r2.y * r0.x;
    r1.x = cmp(0 < r1.x);
    r1.xyz = r1.xxx ? r2.yyy : r1.yzw;
    r0.x = -0.5 + r0.x;
    r0.x = dot(r0.xx, r0.zz);
    r1.xyz = /* saturate */(r1.xyz + r0.xxx);
    r0.x = r0.y * 2 + r0.w;
    r0.x = cb2[0].w * 0.200000003 + r0.x;
    r0.x = 20 * r0.x;
    r0.x = frac(r0.x);
    r0.x = -r0.x * 2 + 1;
    r0.x = cb2[4].z * abs(r0.x);
    r0.y = 0.25 * r0.z;
    r0.x = saturate(r0.x * 0.5 + r0.y);
    r0.y = dot(r1.xyz, float3(0.298999995,0.587000012,0.114));
    r0.yzw = r0.yyy + -r1.xyz;
    r0.xyz = r0.xxx * r0.yzw + r1.xyz;
    r0.w = frac(r2.x);
    r0.w = -r0.w * 2 + 1;
    r0.w = cb2[4].z * abs(r0.w);
    r0.w = -r0.w * 0.75 + 1;
    r3.xyz = /* saturate */(r0.xyz * r0.www);
  }

  // r3.w = dot(r3.xyz, float3(0.212599993,0.715200007,0.0722000003));
  // o0.xyzw = r3.xyzw;
  // o1.x = r3.w;
  o0.xyz = r3.xyz;
  o0.w = o1.x = TM_LumaForAA(o0.xyz, true, true);
  return;
}