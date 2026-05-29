// ---- Created with 3Dmigoto v1.3.16 on Fri Aug 01 21:12:23 2025


Texture2D<float4> t9 : register(t9);

Texture2D<float4> t7 : register(t7);

Texture2D<float4> t6 : register(t6);

Texture2D<float4> t0 : register(t0);

SamplerState s0_s : register(s0);

cbuffer cb1 : register(b1)
{
  float4 cb1[70];
}

cbuffer cb8 : register(b8)
{
  float4 cb8[2];
}




// 3Dmigoto declarations
#define cmp -
#include "./common1.hlsl"

void main(
  float4 v0 : SV_POSITION0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_TARGET0)
{
  float4 r0,r1,r2,r3,r4,r5,r6,r7;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = (int2)v0.xy;
  r0.zw = float2(0,0);
  r1.x = t0.Load(r0.xyw).x;
  r0.xyz = t6.Load(r0.xyz).xyz;

  if (GS_XrayOutline == 0) {
    o0.xyz = r0.xyz;
    o0.w = 1;
    return;
  }

  r0.w = cmp(0.899999976 < r1.x);
  r2.xy = cb1[44].zw * cb8[0].ww;
  r3.x = -r2.y;
  r3.z = 0;
  r1.yz = v1.xy + r3.zx;
  r3.xyz = t7.Sample(s0_s, r1.yz).xyz;
  r2.z = 0;
  r4.xyzw = v1.xyxy + r2.zyxz;
  r5.xyz = t7.Sample(s0_s, r4.xy).xyz;
  r2.w = -r2.x;
  r2.xy = v1.xy + r2.wz;
  r6.xyz = t7.Sample(s0_s, r2.xy).xyz;
  r7.xyz = t7.Sample(s0_s, r4.zw).xyz;
  r1.y = t9.Sample(s0_s, r1.yz).x;
  r1.z = t9.Sample(s0_s, r4.xy).x;
  r1.w = t9.Sample(s0_s, r2.xy).x;
  r2.x = t9.Sample(s0_s, r4.zw).x;
  if (r0.w != 0) {
    o0.xyz = r0.xyz;
    o0.w = 1;
    return;
  }
  r0.w = t9.Sample(s0_s, v1.xy).x;
  r2.y = cmp(0 < r0.w);
  if (r2.y != 0) {
    o0.xyz = r0.xyz;
    o0.w = 1;
    return;
  }
  r2.yzw = r5.xyz + r3.xyz;
  r2.yzw = r2.yzw + r6.xyz;
  r2.yzw = r2.yzw + r7.xyz;
  r2.yzw = float3(0.25,0.25,0.25) * r2.yzw;
  r3.x = max(r2.z, r2.w);
  r3.x = max(r3.x, r2.y);
  r3.x = cmp(r3.x == 0.000000);
  if (r3.x != 0) {
    o0.xyz = r0.xyz;
    o0.w = 1;
    return;
  }
  r1.y = max(r1.y, r1.z);
  r0.w = max(r1.y, r0.w);
  r1.y = max(r2.x, r1.w);
  r0.w = max(r1.y, r0.w);
  r1.y = cmp(0 < r0.w);
  r1.z = -4.99999987e-005 + r1.x;
  r0.w = cmp(r1.z < r0.w);
  r0.w = r0.w ? r1.y : 0;
  if (r0.w != 0) {
    o0.xyz = r0.xyz;
    o0.w = 1;
    return;
  }
  r0.w = cmp(r1.x >= 0.984375);
  r1.y = 1.01587307 * r1.x;
  r1.x = r1.x * 64 + -63;
  r0.w = r0.w ? r1.x : r1.y;
  r0.w = max(9.99999994e-009, r0.w);
  r0.w = rcp(r0.w);
  r0.w = cb8[1].x + -r0.w;
  r0.w = saturate(cb8[1].y * r0.w);
  r1.x = sin(cb1[69].w);
  r1.x = r1.x * 0.5 + 0.75;
  r1.xyz = r2.yzw * r1.xxx;
  r1.xyz = r1.xyz * r0.www;
  o0.xyz = r1.xyz /* * 32768 + r0.xyz */;
  o0.xyz *= GS_XrayOutline;
  
  #if CUSTOM_SDR == 0
    r0.xyz = Trade_Out(r0.xyz); //decode tradeoff, linearize color
    r0.xyz = max(r0.xyz, 0); //clamp bt709
    o0.xyz += r0.xyz; //add to linear xray
    o0.xyz = Trade_In(o0.xyz); //encode tradeoff
  #else
    o0.xyz = o0.xyz * 32768 + r0.xyz;
  #endif

  o0.w = 1;
  return;
}

/*
      0x00000000: ps_5_0
      0x00000008: dcl_globalFlags refactoringAllowed
      0x0000000C: dcl_constantbuffer CB8[2], immediateIndexed
      0x0000001C: dcl_constantbuffer CB1[70], immediateIndexed
      0x0000002C: dcl_sampler s0, mode_default
      0x00000038: dcl_resource_texture2d (float,float,float,float) t0
      0x00000048: dcl_resource_texture2d (float,float,float,float) t6
      0x00000058: dcl_resource_texture2d (float,float,float,float) t7
      0x00000068: dcl_resource_texture2d (float,float,float,float) t9
      0x00000078: dcl_input_ps_siv linear noperspective v0.xy, position
      0x00000088: dcl_input_ps linear v1.xy
      0x00000094: dcl_output o0.xyzw
      0x000000A0: dcl_temps 8
   0  0x000000A8: ftoi r0.xy, v0.xyxx
   1  0x000000BC: mov r0.zw, l(0,0,0,0)
   2  0x000000DC: ld_indexable(texture2d)(float,float,float,float) r1.x, r0.xyww, t0.xyzw
   3  0x00000100: ld_indexable(texture2d)(float,float,float,float) r0.xyz, r0.xyzw, t6.xyzw
   4  0x00000124: lt r0.w, l(0.900000), r1.x
   5  0x00000140: mul r2.xy, cb8[0].wwww, cb1[44].zwzz
   6  0x00000164: mov r3.x, -r2.y
   7  0x0000017C: mov r3.z, l(0)
   8  0x00000190: add r1.yz, r3.zzxz, v1.xxyx
   9  0x000001AC: sample_indexable(texture2d)(float,float,float,float) r3.xyz, r1.yzyy, t7.xyzw, s0
  10  0x000001D8: mov r2.z, l(0)
  11  0x000001EC: add r4.xyzw, r2.zyxz, v1.xyxy
  12  0x00000208: sample_indexable(texture2d)(float,float,float,float) r5.xyz, r4.xyxx, t7.xyzw, s0
  13  0x00000234: mov r2.w, -r2.x
  14  0x0000024C: add r2.xy, r2.wzww, v1.xyxx
  15  0x00000268: sample_indexable(texture2d)(float,float,float,float) r6.xyz, r2.xyxx, t7.xyzw, s0
  16  0x00000294: sample_indexable(texture2d)(float,float,float,float) r7.xyz, r4.zwzz, t7.xyzw, s0
  17  0x000002C0: sample_indexable(texture2d)(float,float,float,float) r1.y, r1.yzyy, t9.yxzw, s0
  18  0x000002EC: sample_indexable(texture2d)(float,float,float,float) r1.z, r4.xyxx, t9.yzxw, s0
  19  0x00000318: sample_indexable(texture2d)(float,float,float,float) r1.w, r2.xyxx, t9.yzwx, s0
  20  0x00000344: sample_indexable(texture2d)(float,float,float,float) r2.x, r4.zwzz, t9.xyzw, s0
  21  0x00000370: if_nz r0.w
  22  0x0000037C:   mov o0.xyz, r0.xyzx
  23  0x00000390:   mov o0.w, l(1.000000)
  24  0x000003A4:   ret 
  25  0x000003A8: endif 
  26  0x000003AC: sample_indexable(texture2d)(float,float,float,float) r0.w, v1.xyxx, t9.yzwx, s0
  27  0x000003D8: lt r2.y, l(0.000000), r0.w
  28  0x000003F4: if_nz r2.y
  29  0x00000400:   mov o0.xyz, r0.xyzx
  30  0x00000414:   mov o0.w, l(1.000000)
  31  0x00000428:   ret 
  32  0x0000042C: endif 
  33  0x00000430: add r2.yzw, r3.xxyz, r5.xxyz
  34  0x0000044C: add r2.yzw, r6.xxyz, r2.yyzw
  35  0x00000468: add r2.yzw, r7.xxyz, r2.yyzw
  36  0x00000484: mul r2.yzw, r2.yyzw, l(0.000000, 0.250000, 0.250000, 0.250000)
  37  0x000004AC: max r3.x, r2.w, r2.z
  38  0x000004C8: max r3.x, r2.y, r3.x
  39  0x000004E4: eq r3.x, r3.x, l(0.000000)
  40  0x00000500: if_nz r3.x
  41  0x0000050C:   mov o0.xyz, r0.xyzx
  42  0x00000520:   mov o0.w, l(1.000000)
  43  0x00000534:   ret 
  44  0x00000538: endif 
  45  0x0000053C: max r1.y, r1.z, r1.y
  46  0x00000558: max r0.w, r0.w, r1.y
  47  0x00000574: max r1.y, r1.w, r2.x
  48  0x00000590: max r0.w, r0.w, r1.y
  49  0x000005AC: lt r1.y, l(0.000000), r0.w
  50  0x000005C8: add r1.z, r1.x, l(-0.000050)
  51  0x000005E4: lt r0.w, r1.z, r0.w
  52  0x00000600: and r0.w, r0.w, r1.y
  53  0x0000061C: if_nz r0.w
  54  0x00000628:   mov o0.xyz, r0.xyzx
  55  0x0000063C:   mov o0.w, l(1.000000)
  56  0x00000650:   ret 
  57  0x00000654: endif 
  58  0x00000658: ge r0.w, r1.x, l(0.984375)
  59  0x00000674: mul r1.y, r1.x, l(1.015873)
  60  0x00000690: mad r1.x, r1.x, l(64.000000), l(-63.000000)
  61  0x000006B4: movc r0.w, r0.w, r1.x, r1.y
  62  0x000006D8: max r0.w, r0.w, l(0.000000)
  63  0x000006F4: rcp r0.w, r0.w
  64  0x00000708: add r0.w, -r0.w, cb8[1].x
  65  0x0000072C: mul_sat r0.w, r0.w, cb8[1].y
  66  0x0000074C: sincos r1.x, null, cb1[69].w
  67  0x00000768: mad r1.x, r1.x, l(0.500000), l(0.750000)
  68  0x0000078C: mul r1.xyz, r1.xxxx, r2.yzwy
  69  0x000007A8: mul r1.xyz, r0.wwww, r1.xyzx
  70  0x000007C4: mad o0.xyz, r1.xyzx, l(32768.000000, 32768.000000, 32768.000000, 0.000000), r0.xyzx
  71  0x000007F4: mov o0.w, l(1.000000)
  72  0x00000808: ret 
// Approximately 73 instruction slots used

*/