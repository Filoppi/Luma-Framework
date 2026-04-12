#include "Includes/Common.hlsl"
cbuffer ParamBuffer : register(b1)
{
   float4 materialColor : packoffset(c0);
   float4x4 coordMatrix : packoffset(c1);
   int filterType_ : packoffset(c5);
}

SamplerState g_ImageTextureSampler_s : register(s1);
Texture2D<float4> g_Texture0 : register(t0);
Texture2D<float4> g_ImageTexture : register(t1);

#define cmp -

void main(float4 v0: SV_Position0, float2 v1: TEXCOORD0, out float4 o0: SV_Target0)
{
   float4 r0, r1, r2, r3, r4, r5, r6, r7, r8;

   r0.xy = (int2)v0.xy;
   r0.zw = float2(0, 0);
   int3 pixelCoord = int3((int2)v0.xy, 0);
   r0.xyzw = g_Texture0.Load(pixelCoord).xyzw;
   r1.xyzw = g_ImageTexture.Sample(g_ImageTextureSampler_s, v1.xy).xyzw;
   r2.xyzw = materialColor.yzwx * r1.yzwx;
   r1.w = r2.z;
   switch (filterType_)
   {
   case 0:
      r3.xyz = r1.xyz * materialColor.xyz + -r0.xyz;
      o0.xyz = r1.www * r3.xyz + r0.xyz;
      o0.w = 1;
      return;
   case 1:
      r3.xyz = min(r2.wxy, r0.xyz);
      r3.xyz = r3.xyz + -r0.xyz;
      o0.xyz = r1.www * r3.xyz + r0.xyz;
      o0.w = r0.w;
      return;
   case 2:
      o0.xyz = r2.wxy * r0.xyz;
      o0.w = 1;
      return;
   case 3:
      r3.xyz = cmp(r2.wxy == float3(0, 0, 0));
      r4.xyz = float3(1, 1, 1) + -r0.xyz;
      r4.xyz = r4.xyz / r2.wxy;
      r4.xyz = float3(1, 1, 1) + -r4.xyz;
      r4.xyz = max(float3(0, 0, 0), r4.xyz);
      r3.xyz = r3.xyz ? r2.wxy : r4.xyz;
      r3.xyz = min(r3.xyz, r0.xyz);
      r3.xyz = r3.xyz + -r0.xyz;
      o0.xyz = r1.www * r3.xyz + r0.xyz;
      o0.w = r0.w;
      return;
   case 4:
      r3.xyz = r1.xyz * materialColor.xyz + r0.xyz;
      r3.xyz = float3(-1, -1, -1) + r3.xyz;
      r3.xyz = max(float3(0, 0, 0), r3.xyz);
      r3.xyz = r3.xyz + -r0.xyz;
      o0.xyz = r1.www * r3.xyz + r0.xyz;
      o0.w = r0.w;
      return;
   case 5:
      // r3.x = dot(r0.xyz, float3(0.300000012, 0.589999974, 0.109999999));
      // r3.y = dot(r2.wxy, float3(0.300000012, 0.589999974, 0.109999999));
      r3.x = GetLuminance(r0.xyz);
      r3.y = GetLuminance(r2.wxy);
      r3.x = cmp(r3.x < r3.y);
      r3.xyz = r3.xxx ? r0.xyz : r2.wxy;
      r3.xyz = r3.xyz + -r0.xyz;
      o0.xyz = r1.www * r3.xyz + r0.xyz;
      o0.w = r0.w;
      return;
   case 6:
      r3.xyz = max(r2.wxy, r0.xyz);
      r3.xyz = r3.xyz + -r0.xyz;
      o0.xyz = r1.www * r3.xyz + r0.xyz;
      o0.w = r0.w;
      return;
   case 7:
      r3.xyz = float3(1, 1, 1) + -r0.xyz;
      r4.xyz = -r1.xyz * materialColor.xyz + float3(1, 1, 1);
      r3.xyz = -r3.xyz * r4.xyz + -r0.xyz;
      r3.xyz = float3(1, 1, 1) + r3.xyz;
      o0.xyz = r1.www * r3.xyz + r0.xyz;
      o0.w = r0.w;
      return;
   case 8:
      r3.xyz = cmp(r2.wxy == float3(1, 1, 1));
      r4.xyz = -r1.xyz * materialColor.xyz + float3(1, 1, 1);
      r4.xyz = r0.xyz / r4.xyz;
      r4.xyz = min(float3(1, 1, 1), r4.xyz);
      r3.xyz = r3.xyz ? r2.wxy : r4.xyz;
      r3.xyz = r3.xyz + -r0.xyz;
      o0.xyz = r1.www * r3.xyz + r0.xyz;
      o0.w = r0.w;
      return;
   case 9:
      o0.xyz = r1.www * r2.wxy + r0.xyz;
      o0.w = r0.w;
      return;
   case 10:
      r3.x = GetLuminance(r0.xyz);
      r3.y = GetLuminance(r2.wxy);
      r3.x = cmp(r3.y < r3.x);
      r3.xyz = r3.xxx ? r0.xyz : r2.wxy;
      r3.xyz = r3.xyz + -r0.xyz;
      o0.xyz = r1.www * r3.xyz + r0.xyz;
      o0.w = r0.w;
      return;
   case 11:
      r3.xyz = cmp(r0.xyz < float3(0.5, 0.5, 0.5));
      r3.w = dot(r2.ww, r0.xx);
      r4.xyz = float3(1, 1, 1) + -r0.xyz;
      r4.xyz = r4.xyz + r4.xyz;
      r5.xyz = -r1.xyz * materialColor.xyz + float3(1, 1, 1);
      r4.xyz = -r4.xyz * r5.xyz + float3(1, 1, 1);
      r5.x = r3.x ? r3.w : r4.x;
      r3.x = dot(r2.xx, r0.yy);
      r5.y = r3.y ? r3.x : r4.y;
      r3.x = dot(r2.yy, r0.zz);
      r5.z = r3.z ? r3.x : r4.z;
      r3.xyz = r5.xyz + -r0.xyz;
      o0.xyz = r1.www * r3.xyz + r0.xyz;
      o0.w = r0.w;
      return;
   case 12:
      r3.xyz = cmp(r2.wxy < float3(0.5, 0.5, 0.5));
      r4.xyz = r0.xyz + r0.xyz;
      r5.xyz = r0.xyz * r0.xyz;
      r6.xyz = -r2.wxy * float3(2, 2, 2) + float3(1, 1, 1);
      r5.xyz = r6.xyz * r5.xyz;
      r5.xyz = r4.xyz * r2.wxy + r5.xyz;
      r6.xyz = sqrt(r0.xyz);
      r7.xyz = r2.wxy * float3(2, 2, 2) + float3(-1, -1, -1);
      r8.xyz = -r1.xyz * materialColor.xyz + float3(1, 1, 1);
      r4.xyz = r8.xyz * r4.xyz;
      r4.xyz = r6.xyz * r7.xyz + r4.xyz;
      r3.xyz = r3.xyz ? r5.xyz : r4.xyz;
      r3.xyz = r3.xyz + -r0.xyz;
      o0.xyz = r1.www * r3.xyz + r0.xyz;
      o0.w = r0.w;
      return;
   case 13:
      r3.xyz = cmp(r2.wxy < float3(0.5, 0.5, 0.5));
      r3.w = dot(r0.xx, r2.ww);
      r4.xyz = -r1.xyz * materialColor.xyz + float3(1, 1, 1);
      r4.xyz = r4.xyz + r4.xyz;
      r5.xyz = float3(1, 1, 1) + -r0.xyz;
      r4.xyz = -r4.xyz * r5.xyz + float3(1, 1, 1);
      r5.x = r3.x ? r3.w : r4.x;
      r3.x = dot(r0.yy, r2.xx);
      r5.y = r3.y ? r3.x : r4.y;
      r3.x = dot(r0.zz, r2.yy);
      r5.z = r3.z ? r3.x : r4.z;
      r3.xyz = r5.xyz + -r0.xyz;
      o0.xyz = r1.www * r3.xyz + r0.xyz;
      o0.w = r0.w;
      return;
   case 14:
      r3.xyz = cmp(r2.wxy < float3(0.5, 0.5, 0.5));
      r4.xyz = r2.wxy + r2.wxy;
      r5.xyz = cmp(r2.wxy == float3(0, 0, 0));
      r6.xyz = float3(1, 1, 1) + -r0.xyz;
      r6.xyz = r6.xyz / r4.xyz;
      r6.xyz = float3(1, 1, 1) + -r6.xyz;
      r6.xyz = max(float3(0, 0, 0), r6.xyz);
      r4.xyz = r5.xyz ? r4.xyz : r6.xyz;
      r5.xyz = r1.xyz * materialColor.xyz + float3(-0.5, -0.5, -0.5);
      r6.xyz = r5.xyz + r5.xyz;
      r7.xyz = cmp(r5.xyz == float3(0.5, 0.5, 0.5));
      r5.xyz = -r5.xyz * float3(2, 2, 2) + float3(1, 1, 1);
      r5.xyz = r0.xyz / r5.xyz;
      r5.xyz = min(float3(1, 1, 1), r5.xyz);
      r5.xyz = r7.xyz ? r6.xyz : r5.xyz;
      r3.xyz = r3.xyz ? r4.xyz : r5.xyz;
      r3.xyz = r3.xyz + -r0.xyz;
      o0.xyz = r1.www * r3.xyz + r0.xyz;
      o0.w = r0.w;
      return;
   case 15:
      r3.xyz = cmp(r2.wxy < float3(0.5, 0.5, 0.5));
      r4.xyz = r2.wxy * float3(2, 2, 2) + r0.xyz;
      r4.xyz = float3(-1, -1, -1) + r4.xyz;
      r4.xyz = max(float3(0, 0, 0), r4.xyz);
      r5.xyz = r1.xyz * materialColor.xyz + float3(-0.5, -0.5, -0.5);
      r5.xyz = r5.xyz * float3(2, 2, 2) + r0.xyz;
      r3.xyz = r3.xyz ? r4.xyz : r5.xyz;
      r3.xyz = r3.xyz + -r0.xyz;
      o0.xyz = r1.www * r3.xyz + r0.xyz;
      o0.w = r0.w;
      return;
   case 16:
      r3.xyz = cmp(r2.wxy < float3(0.5, 0.5, 0.5));
      r4.xyz = r2.wxy + r2.wxy;
      r4.xyz = min(r4.xyz, r0.xyz);
      r5.xyz = r1.xyz * materialColor.xyz + float3(-0.5, -0.5, -0.5);
      r5.xyz = r5.xyz + r5.xyz;
      r5.xyz = max(r5.xyz, r0.xyz);
      r3.xyz = r3.xyz ? r4.xyz : r5.xyz;
      r3.xyz = r3.xyz + -r0.xyz;
      o0.xyz = r1.www * r3.xyz + r0.xyz;
      o0.w = r0.w;
      return;
   case 17:
      r3.xyz = cmp(r2.wxy < float3(0.5, 0.5, 0.5));
      r4.xyz = r2.wxy + r2.wxy;
      r5.xyz = cmp(r2.wxy == float3(0, 0, 0));
      r6.xyz = float3(1, 1, 1) + -r0.xyz;
      r6.xyz = r6.xyz / r4.xyz;
      r6.xyz = float3(1, 1, 1) + -r6.xyz;
      r6.xyz = max(float3(0, 0, 0), r6.xyz);
      r4.xyz = r5.xyz ? r4.xyz : r6.xyz;
      r5.xyz = r1.xyz * materialColor.xyz + float3(-0.5, -0.5, -0.5);
      r6.xyz = r5.xyz + r5.xyz;
      r7.xyz = cmp(r5.xyz == float3(0.5, 0.5, 0.5));
      r5.xyz = -r5.xyz * float3(2, 2, 2) + float3(1, 1, 1);
      r5.xyz = r0.xyz / r5.xyz;
      r5.xyz = min(float3(1, 1, 1), r5.xyz);
      r5.xyz = r7.xyz ? r6.xyz : r5.xyz;
      r3.xyz = r3.xyz ? r4.xyz : r5.xyz;
      r3.xyz = cmp(r3.xyz < float3(0.5, 0.5, 0.5));
      r3.xyz = r3.xyz ? float3(0, 0, 0) : float3(1, 1, 1);
      r3.xyz = r3.xyz + -r0.xyz;
      o0.xyz = r1.www * r3.xyz + r0.xyz;
      o0.w = r0.w;
      return;
   case 18:
      r3.xyz = -r1.xyz * materialColor.xyz + r0.xyz;
      r3.xyz = abs(r3.xyz) + -r0.xyz;
      o0.xyz = r1.www * r3.xyz + r0.xyz;
      o0.w = r0.w;
      return;
   case 19:
      r3.xyz = r1.xyz * materialColor.xyz + r0.xyz;
      r4.xyz = r2.wxy * r0.xyz;
      r3.xyz = -r4.xyz * float3(2, 2, 2) + r3.xyz;
      r3.xyz = r3.xyz + -r0.xyz;
      o0.xyz = r1.www * r3.xyz + r0.xyz;
      o0.w = r0.w;
      return;
   case 20:
      r3.xyz = -r1.xyz * materialColor.xyz + r0.xyz;
      r3.xyz = max(float3(0, 0, 0), r3.xyz);
      r3.xyz = r3.xyz + -r0.xyz;
      o0.xyz = r1.www * r3.xyz + r0.xyz;
      o0.w = r0.w;
      return;
   case 21:
      r1.xyz = r1.xyz * materialColor.xyz + float3(9.99999997e-07, 9.99999997e-07, 9.99999997e-07);
      r1.xyz = r0.xyz / r1.xyz;
      r1.xyz = r1.xyz + -r0.xyz;
      o0.xyz = r1.www * r1.xyz + r0.xyz;
      o0.w = r0.w;
      return;
   case 22:
      r1.x = cmp(r0.y < r0.z);
      r1.xy = r1.xx ? r0.zy : r0.yz;
      r3.x = cmp(r0.x < r1.x);
      r1.z = r0.x;
      r1.xyz = r3.xxx ? r1.xyz : r1.zyx;
      r1.y = min(r1.z, r1.y);
      r1.y = r1.x + -r1.y;
      r1.z = 1.00000001e-10 + r1.x;
      r1.y = r1.y / r1.z;
      r1.z = cmp(r2.x < r2.y);
      r3.xy = r2.yx;
      r3.zw = float2(-1, 0.666666985);
      r4.xy = r3.yx;
      r4.zw = float2(0, -0.333332986);
      r3.xyzw = r1.zzzz ? r3.xyzw : r4.xyzw;
      r1.z = cmp(r2.w < r3.x);
      r2.xyz = r3.xyw;
      r3.xyw = r2.wyx;
      r3.xyzw = r1.zzzz ? r2.xyzw : r3.xyzw;
      r1.z = min(r3.w, r3.y);
      r1.z = r3.x + -r1.z;
      r3.x = r3.w + -r3.y;
      r1.z = r1.z * 6 + 1.00000001e-10;
      r1.z = rcp(r1.z);
      r1.z = r3.x * r1.z + r3.z;
      r3.xyz = abs(r1.zzz) * float3(6, 6, 6) + float3(-3, -2, -4);
      r3.xyz = saturate(abs(r3.xyz) * float3(1, -1, -1) + float3(-1, 2, 2));
      r3.xyz = float3(-1, -1, -1) + r3.xyz;
      r3.xyz = r3.xyz * r1.yyy + float3(1, 1, 1);
      r1.xyz = r3.xyz * r1.xxx + -r0.xyz;
      o0.xyz = r1.www * r1.xyz + r0.xyz;
      o0.w = r0.w;
      return;
   case 23:
      r1.x = cmp(r0.y < r0.z);
      r3.xy = r0.zy;
      r3.zw = float2(-1, 0.666666985);
      r4.xy = r3.yx;
      r4.zw = float2(0, -0.333332986);
      r3.xyzw = r1.xxxx ? r3.xyzw : r4.xyzw;
      r1.x = cmp(r0.x < r3.x);
      r4.xyz = r3.xyw;
      r4.w = r0.x;
      r3.xyw = r4.wyx;
      r3.xyzw = r1.xxxx ? r4.xyzw : r3.xyzw;
      r1.x = min(r3.w, r3.y);
      r1.x = r3.x + -r1.x;
      r1.y = r3.w + -r3.y;
      r1.z = r1.x * 6 + 1.00000001e-10;
      r1.z = rcp(r1.z);
      r1.y = r1.y * r1.z + r3.z;
      r1.x = -r1.x * 0.5 + r3.x;
      r1.z = cmp(r2.x < r2.y);
      r2.xy = r1.zz ? r2.yx : r2.xy;
      r1.z = cmp(r2.w < r2.x);
      r3.xyz = r1.zzz ? r2.xyw : r2.wyx;
      r1.z = min(r3.z, r3.y);
      r1.z = r3.x + -r1.z;
      r3.x = -r1.z * 0.5 + r3.x;
      r3.x = r3.x * 2 + -1;
      r3.x = 1 + -abs(r3.x);
      r3.x = rcp(r3.x);
      r1.z = saturate(r3.x * r1.z);
      r3.xyz = abs(r1.yyy) * float3(6, 6, 6) + float3(-3, -2, -4);
      r3.xyz = saturate(abs(r3.xyz) * float3(1, -1, -1) + float3(-1, 2, 2));
      r1.y = r1.x * 2 + -1;
      r1.y = 1 + -abs(r1.y);
      r1.y = r1.y * r1.z;
      r3.xyz = float3(-0.5, -0.5, -0.5) + r3.xyz;
      r1.xyz = r3.xyz * r1.yyy + r1.xxx;
      r1.xyz = r1.xyz + -r0.xyz;
      o0.xyz = r1.www * r1.xyz + r0.xyz;
      o0.w = r0.w;
      return;
   case 24:
      r1.x = cmp(r2.x < r2.y);
      r3.xy = r2.yx;
      r3.zw = float2(-1, 0.666666985);
      r4.xy = r3.yx;
      r4.zw = float2(0, -0.333332986);
      r3.xyzw = r1.xxxx ? r3.xyzw : r4.xyzw;
      r1.x = cmp(r2.w < r3.x);
      r2.xyz = r3.xyw;
      r3.xyw = r2.wyx;
      r3.xyzw = r1.xxxx ? r2.xyzw : r3.xyzw;
      r1.x = min(r3.w, r3.y);
      r1.x = r3.x + -r1.x;
      r1.y = r3.w + -r3.y;
      r1.z = r1.x * 6 + 1.00000001e-10;
      r1.z = rcp(r1.z);
      r1.y = r1.y * r1.z + r3.z;
      r1.z = -r1.x * 0.5 + r3.x;
      r2.z = r1.z * 2 + -1;
      r2.z = 1 + -abs(r2.z);
      r2.z = rcp(r2.z);
      r1.x = saturate(r2.z * r1.x);
      r2.z = cmp(r0.y < r0.z);
      r3.xy = r2.zz ? r0.zy : r0.yz;
      r2.z = cmp(r0.x < r3.x);
      r3.z = r0.x;
      r3.xyz = r2.zzz ? r3.xyz : r3.zyx;
      r2.z = min(r3.z, r3.y);
      r2.z = r3.x + -r2.z;
      r2.z = -r2.z * 0.5 + r3.x;
      r1.z = r2.z * r1.z;
      r3.xyz = abs(r1.yyy) * float3(6, 6, 6) + float3(-3, -2, -4);
      r3.xyz = saturate(abs(r3.xyz) * float3(1, -1, -1) + float3(-1, 2, 2));
      r1.y = r1.z * 2 + -1;
      r1.y = 1 + -abs(r1.y);
      r1.x = r1.y * r1.x;
      r3.xyz = float3(-0.5, -0.5, -0.5) + r3.xyz;
      r1.xyz = r3.xyz * r1.xxx + r1.zzz;
      r1.xyz = r1.xyz + -r0.xyz;
      o0.xyz = r1.www * r1.xyz + r0.xyz;
      o0.w = r0.w;
      return;
   case 25:
      r1.x = cmp(r0.y < r0.z);
      r3.xy = r0.zy;
      r3.zw = float2(-1, 0.666666985);
      r4.xy = r3.yx;
      r4.zw = float2(0, -0.333332986);
      r3.xyzw = r1.xxxx ? r3.xyzw : r4.xyzw;
      r1.x = cmp(r0.x < r3.x);
      r4.xyz = r3.xyw;
      r4.w = r0.x;
      r3.xyw = r4.wyx;
      r3.xyzw = r1.xxxx ? r4.xyzw : r3.xyzw;
      r1.x = min(r3.w, r3.y);
      r1.x = r3.x + -r1.x;
      r1.y = r3.w + -r3.y;
      r1.z = r1.x * 6 + 1.00000001e-10;
      r1.z = rcp(r1.z);
      r1.y = r1.y * r1.z + r3.z;
      r1.z = -r1.x * 0.5 + r3.x;
      r1.z = r1.z * 2 + -1;
      r1.z = 1 + -abs(r1.z);
      r1.z = rcp(r1.z);
      r1.x = saturate(r1.x * r1.z);
      r1.z = cmp(r2.x < r2.y);
      r2.xy = r1.zz ? r2.yx : r2.xy;
      r1.z = cmp(r2.w < r2.x);
      r2.xyz = r1.zzz ? r2.xyw : r2.wyx;
      r1.z = min(r2.z, r2.y);
      r1.z = r2.x + -r1.z;
      r1.z = -r1.z * 0.5 + r2.x;
      r2.xyz = abs(r1.yyy) * float3(6, 6, 6) + float3(-3, -2, -4);
      r2.xyz = saturate(abs(r2.xyz) * float3(1, -1, -1) + float3(-1, 2, 2));
      r1.y = r1.z * 2 + -1;
      r1.y = 1 + -abs(r1.y);
      r1.x = r1.y * r1.x;
      r2.xyz = float3(-0.5, -0.5, -0.5) + r2.xyz;
      r1.xyz = r2.xyz * r1.xxx + r1.zzz;
      r1.xyz = r1.xyz + -r0.xyz;
      o0.xyz = r1.www * r1.xyz + r0.xyz;
      o0.w = r0.w;
      return;
   default:
      break;
   }
   o0.xyzw = float4(1, 0, 0, 1);
   return;
}
