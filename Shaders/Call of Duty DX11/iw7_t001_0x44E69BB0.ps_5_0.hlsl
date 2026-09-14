// ---- Created with 3Dmigoto v1.3.16 on Tue Jun 23 20:53:10 2026
Texture3D<float4> t2 : register(t2);

Texture2D<float4> t1 : register(t1);

Texture2D<float4> t0 : register(t0);

SamplerState s2_s : register(s2);

SamplerState s1_s : register(s1);

SamplerState s0_s : register(s0);

cbuffer cb13 : register(b13)
{
  float4 cb13[3];
}

cbuffer cb2 : register(b2)
{
  float4 cb2[8];
}




// 3Dmigoto declarations
#define cmp -
#include "iw7_common.hlsl"


// main, Refract CA
// 0
void main(
  float4 v0 : SV_POSITION0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_TARGET0,
  out float o1 : SV_TARGET1)
{
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = cb2[1].xy + v1.xy;
  TM_UV(r0.xy);
  
  r0.z = 1 + -cb2[2].x;
  r0.w = cmp(0 < cb2[2].w);
  if (r0.w != 0) {
    r1.xy = cb2[3].xy * float2(0.5,0.5) + float2(0.5,0.5);
    r2.xy = cb2[0].xy * cb2[0].wz;
    r2.z = 1;
    r1.zw = r2.zy * r1.xy;
    r3.xy = r0.xy * r2.zy + -r1.zw;
    r0.w = dot(r3.xy, r3.xy);

    // r0.w = (uint)r0.w >> 1;
    // r0.w = (int)r0.w + 0x1fbd1df5;
    int i0 = asint(r0.w);
    i0 = i0 >> 1;
    i0 = i0 + 0x1fbd1df5;
    r0.w = asfloat(i0);

    r1.z = -3 * cb2[2].w;
    r3.xy = r3.xy / r0.ww;
    r0.w = r1.z * r0.w;
    r2.w = r0.w * r0.w;
    r2.w = 0.0662999973 * r2.w;
    r2.w = abs(r0.w) * -0.178399995 + -r2.w;
    r2.w = 1.03009999 + r2.w;
    r0.w = r2.w * r0.w;
    r3.xy = r3.xy * r0.ww;
    r3.xy = r3.xy * r1.ww;
    r0.w = r1.z * r1.w;
    r1.z = r0.w * r0.w;
    r1.z = 0.0662999973 * r1.z;
    r1.z = abs(r0.w) * -0.178399995 + -r1.z;
    r1.z = 1.03009999 + r1.z;
    r0.w = r1.z * r0.w;
    r1.zw = r3.xy / r0.ww;
    r1.xy = r1.xy * r2.zy + r1.zw;
    r1.xy = r1.xy * r2.zx;
  } else {
    r1.xy = r0.xy;
  }

  r2.xyzw = r1.xyxy * float4(2,2,2,2) + float4(-1,-1,-1,-1);
  r2.xyzw = -cb2[3].xyxy + r2.xyzw;
  r0.w = dot(r2.zw, r2.zw);

  // r0.w = (uint)r0.w >> 1;
  // r0.w = (int)r0.w + 0x1fbd1df5;
  int i1 = asint(r0.w);
  i1 = i1 >> 1;
  i1 = i1 + 0x1fbd1df5;
  r0.w = asfloat(i1);

  r0.w = max(0, r0.w);
  r1.z = cmp(r0.z < r0.w);
  if (r1.z != 0) {
    r3.xw = float2(1,1) + -cb2[2].zz;
    r1.z = 1 + -r0.z;
    r0.z = r0.w + -r0.z;
    r1.z = 1 / r1.z;
    r0.z = saturate(r1.z * r0.z);
    r1.z = r0.z * -2 + 3;
    r0.z = r0.z * r0.z;
    r0.z = r1.z * r0.z;
    r0.z = cb2[2].y * r0.z;
    r2.xyzw = r0.zzzz * r2.xyzw;
    r0.z = 1 + -cb2[3].z;
    r1.z = 1 + -r0.w;
    r0.w = r1.z / r0.w;
    r0.z = r0.w * cb2[3].z + r0.z;
    r2.xyzw = r2.xyzw * r0.zzzz;
    r0.zw = r1.yx * float2(1024,1024) + float2(-512,-512);
    r1.zw = float2(0.554549694,0.308517009) * r0.wz;
    r1.zw = frac(r1.zw);
    r0.zw = r0.wz * r1.zw + r0.zw;
    r0.z = r0.z * r0.w;
    r0.z = frac(r0.z);
    r0.z = r0.z * 2 + -1;
    r4.xyzw = r0.zzzz * float4(0.100000001,0.100000001,0.100000001,0.100000001) + float4(-1,-0.600000024,-0.199999988,0.200000048);
    r5.xyzw = -r2.zwzw * r4.xxyy + r1.xyxy;
    r6.xyz = t0.SampleLevel(s0_s, r5.xy, 0).xyz;
    r0.z = min(1, -r4.x);
    r0.w = 1 + -r3.x;
    r3.y = r0.z * r0.w + r3.x;
    r7.xyz = r4.xzy * cb2[2].zzz + float3(1,1,1);
    r3.z = r7.x;
    r5.xyz = t0.SampleLevel(s0_s, r5.zw, 0).xyz;
    r8.xy = -r4.yz * r0.ww + r3.xx;
    r8.z = r7.z;
    r8.w = 1 + -cb2[2].z;
    r5.xyz = r8.xzw * r5.xyz;
    r5.xyz = r3.yzw * r6.xyz + r5.xyz;
    r3.yzw = r8.xzw + r3.yzw;
    r2.xyzw = -r2.xyzw * r4.zzww + r1.xyxy;
    r4.xyz = t0.SampleLevel(s0_s, r2.xy, 0).xyz;
    r7.xz = r8.yw;
    r4.xyz = r7.xyz * r4.xyz + r5.xyz;
    r3.yzw = r7.xyz + r3.yzw;
    r2.xyz = t0.SampleLevel(s0_s, r2.zw, 0).xyz;
    r5.y = r4.w * -cb2[2].z + 1;
    r5.z = r4.w * r0.w + r3.x;
    r5.x = 1 + -cb2[2].z;
    r2.xyz = r5.xyz * r2.xyz + r4.xyz;
    r3.xyz = r5.xyz + r3.yzw;
    r2.xyz = r2.xyz / r3.xyz;
  } else {
    r2.xyz = t0.SampleLevel(s0_s, r1.xy, 0).xyz;
  }

  r0.xyz = t1.Sample(s1_s, r0.xy).xyz;
  r0.xyz = cb2[7].xxx * GS.Bloom * r0.xyz;
  r0.xyz = cb2[7].yyy * r2.xyz + r0.xyz;
  TM_Color(r0.xyz);

  // r1.xyz = cmp(r0.xyz < cb13[0].xxx);
  // r2.xyzw = r1.xxxx ? cb13[2].xyzw : cb13[1].xyzw;
  // r0.xw = r2.xy * r0.xx + r2.zw;
  // r2.x = saturate(r0.x / r0.w);
  // r3.xyzw = r1.yyyy ? cb13[2].xyzw : cb13[1].xyzw;
  // r0.xy = r3.xy * r0.yy + r3.zw;
  // r2.y = saturate(r0.x / r0.y);
  // r1.xyzw = r1.zzzz ? cb13[2].xyzw : cb13[1].xyzw;
  // r0.xy = r1.xy * r0.zz + r1.zw;
  // r2.z = saturate(r0.x / r0.y);
  TM_Rolloff();

  // r0.x = saturate(dot(r2.xyz, cb2[4].xyz));
  // r0.y = saturate(dot(r2.xyz, cb2[5].xyz));
  // r0.z = saturate(dot(r2.xyz, cb2[6].xyz));
  TM_LumaThingy(4);

  // r0.xyz = log2(r0.xyz);
  // r0.xyz = float3(0.416666657,0.416666657,0.416666657) * r0.xyz;
  // r0.xyz = exp2(r0.xyz);
  // r0.xyz = r0.xyz * float3(1.05499995,1.05499995,1.05499995) + float3(-0.0549999997,-0.0549999997,-0.0549999997);
  // r0.xyz = max(float3(0,0,0), r0.xyz);
  TM_Gamma();

  // r0.xyz = r0.xyz * float3(0.96875,0.96875,0.96875) + float3(0.015625,0.015625,0.015625);
  // r0.xyz = t2.SampleLevel(s2_s, r0.xyz, 0).xyz;
  TM_LUT(t2, s2_s);

  TM_Upgrade();
  o0.xyz = tmi.r0;

  // r0.w = dot(r0.xyz, float3(0.212599993,0.715200007,0.0722000003));
  // o0.xyzw = r0.xyzw;
  // o1.x = r0.w;
  o0.w = o1.x = tmi.aay;
  return;
}

/*
//
// Generated by Microsoft (R) D3D Shader Disassembler
//
//
// Input signature:
//
// Name                 Index   Mask Register SysValue  Format   Used
// -------------------- ----- ------ -------- -------- ------- ------
// SV_POSITION              0   xyzw        0      POS   float       
// TEXCOORD                 0   xy          1     NONE   float   xy  
//
//
// Output signature:
//
// Name                 Index   Mask Register SysValue  Format   Used
// -------------------- ----- ------ -------- -------- ------- ------
// SV_TARGET                0   xyzw        0   TARGET   float   xyzw
// SV_TARGET                1   x           1   TARGET   float   x   
//
      0x00000000: ps_5_0
      0x00000008: dcl_globalFlags refactoringAllowed
      0x0000000C: dcl_constantbuffer CB2[8], immediateIndexed
      0x0000001C: dcl_constantbuffer CB13[3], immediateIndexed
      0x0000002C: dcl_sampler s0, mode_default
      0x00000038: dcl_sampler s1, mode_default
      0x00000044: dcl_sampler s2, mode_default
      0x00000050: dcl_resource_texture2d (float,float,float,float) t0
      0x00000060: dcl_resource_texture2d (float,float,float,float) t1
      0x00000070: dcl_resource_texture3d (float,float,float,float) t2
      0x00000080: dcl_input_ps linear v1.xy
      0x0000008C: dcl_output o0.xyzw
      0x00000098: dcl_output o1.x
      0x000000A4: dcl_temps 9
   0  0x000000AC: add r0.xy, v1.xyxx, cb2[1].xyxx
   1  0x000000CC: add r0.z, -cb2[2].x, l(1.000000)
   2  0x000000F0: lt r0.w, l(0.000000), cb2[2].w
   3  0x00000110: if_nz r0.w
   4  0x0000011C:   mad r1.xy, cb2[3].xyxx, l(0.500000, 0.500000, 0.000000, 0.000000), l(0.500000, 0.500000, 0.000000, 0.000000)
   5  0x0000015C:   mul r2.xy, cb2[0].wzww, cb2[0].xyxx
   6  0x00000180:   mov r2.z, l(1.000000)
   7  0x00000194:   mul r1.zw, r1.xxxy, r2.zzzy
   8  0x000001B0:   mad r3.xy, r0.xyxx, r2.zyzz, -r1.zwzz
   9  0x000001D8:   dp2 r0.w, r3.xyxx, r3.xyxx
  10  0x000001F4:   ishr r0.w, r0.w, l(1)
  11  0x00000210:   iadd r0.w, r0.w, l(0x1fbd1df5)
  12  0x0000022C:   mul r1.z, cb2[2].w, l(-3.000000)
  13  0x0000024C:   div r3.xy, r3.xyxx, r0.wwww
  14  0x00000268:   mul r0.w, r0.w, r1.z
  15  0x00000284:   mul r2.w, r0.w, r0.w
  16  0x000002A0:   mul r2.w, r2.w, l(0.066300)
  17  0x000002BC:   mad r2.w, |r0.w|, l(-0.178400), -r2.w
  18  0x000002E8:   add r2.w, r2.w, l(1.030100)
  19  0x00000304:   mul r0.w, r0.w, r2.w
  20  0x00000320:   mul r3.xy, r0.wwww, r3.xyxx
  21  0x0000033C:   mul r3.xy, r1.wwww, r3.xyxx
  22  0x00000358:   mul r0.w, r1.w, r1.z
  23  0x00000374:   mul r1.z, r0.w, r0.w
  24  0x00000390:   mul r1.z, r1.z, l(0.066300)
  25  0x000003AC:   mad r1.z, |r0.w|, l(-0.178400), -r1.z
  26  0x000003D8:   add r1.z, r1.z, l(1.030100)
  27  0x000003F4:   mul r0.w, r0.w, r1.z
  28  0x00000410:   div r1.zw, r3.xxxy, r0.wwww
  29  0x0000042C:   mad r1.xy, r1.xyxx, r2.zyzz, r1.zwzz
  30  0x00000450:   mul r1.xy, r2.zxzz, r1.xyxx
  31  0x0000046C: else 
  32  0x00000470:   mov r1.xy, r0.xyxx
  33  0x00000484: endif 
  34  0x00000488: mad r2.xyzw, r1.xyxy, l(2.000000, 2.000000, 2.000000, 2.000000), l(-1.000000, -1.000000, -1.000000, -1.000000)
  35  0x000004C4: add r2.xyzw, r2.xyzw, -cb2[3].xyxy
  36  0x000004E8: dp2 r0.w, r2.zwzz, r2.zwzz
  37  0x00000504: ishr r0.w, r0.w, l(1)
  38  0x00000520: iadd r0.w, r0.w, l(0x1fbd1df5)
  39  0x0000053C: max r0.w, r0.w, l(0.000000)
  40  0x00000558: lt r1.z, r0.z, r0.w
  41  0x00000574: if_nz r1.z
  42  0x00000580:   add r3.xw, -cb2[2].zzzz, l(1.000000, 0.000000, 0.000000, 1.000000)
  43  0x000005B0:   add r1.z, -r0.z, l(1.000000)
  44  0x000005D0:   add r0.z, -r0.z, r0.w
  45  0x000005F0:   div r1.z, l(1.000000, 1.000000, 1.000000, 1.000000), r1.z
  46  0x00000618:   mul_sat r0.z, r0.z, r1.z
  47  0x00000634:   mad r1.z, r0.z, l(-2.000000), l(3.000000)
  48  0x00000658:   mul r0.z, r0.z, r0.z
  49  0x00000674:   mul r0.z, r0.z, r1.z
  50  0x00000690:   mul r0.z, r0.z, cb2[2].y
  51  0x000006B0:   mul r2.xyzw, r2.xyzw, r0.zzzz
  52  0x000006CC:   add r0.z, -cb2[3].z, l(1.000000)
  53  0x000006F0:   add r1.z, -r0.w, l(1.000000)
  54  0x00000710:   div r0.w, r1.z, r0.w
  55  0x0000072C:   mad r0.z, r0.w, cb2[3].z, r0.z
  56  0x00000754:   mul r2.xyzw, r0.zzzz, r2.xyzw
  57  0x00000770:   mad r0.zw, r1.yyyx, l(0.000000, 0.000000, 1024.000000, 1024.000000), l(0.000000, 0.000000, -512.000000, -512.000000)
  58  0x000007AC:   mul r1.zw, r0.wwwz, l(0.000000, 0.000000, 0.554550, 0.308517)
  59  0x000007D4:   frc r1.zw, r1.zzzw
  60  0x000007E8:   mad r0.zw, r0.wwwz, r1.zzzw, r0.zzzw
  61  0x0000080C:   mul r0.z, r0.w, r0.z
  62  0x00000828:   frc r0.z, r0.z
  63  0x0000083C:   mad r0.z, r0.z, l(2.000000), l(-1.000000)
  64  0x00000860:   mad r4.xyzw, r0.zzzz, l(0.100000, 0.100000, 0.100000, 0.100000), l(-1.000000, -0.600000, -0.200000, 0.200000)
  65  0x0000089C:   mad r5.xyzw, -r2.zwzw, r4.xxyy, r1.xyxy
  66  0x000008C4:   sample_l_indexable(texture2d)(float,float,float,float) r6.xyz, r5.xyxx, t0.xyzw, s0, l(0.000000)
  67  0x000008F8:   min r0.z, -r4.x, l(1.000000)
  68  0x00000918:   add r0.w, -r3.x, l(1.000000)
  69  0x00000938:   mad r3.y, r0.z, r0.w, r3.x
  70  0x0000095C:   mad r7.xyz, r4.xzyx, cb2[2].zzzz, l(1.000000, 1.000000, 1.000000, 0.000000)
  71  0x00000990:   mov r3.z, r7.x
  72  0x000009A4:   sample_l_indexable(texture2d)(float,float,float,float) r5.xyz, r5.zwzz, t0.xyzw, s0, l(0.000000)
  73  0x000009D8:   mad r8.xy, -r4.yzyy, r0.wwww, r3.xxxx
  74  0x00000A00:   mov r8.z, r7.z
  75  0x00000A14:   add r8.w, -cb2[2].z, l(1.000000)
  76  0x00000A38:   mul r5.xyz, r5.xyzx, r8.xzwx
  77  0x00000A54:   mad r5.xyz, r3.yzwy, r6.xyzx, r5.xyzx
  78  0x00000A78:   add r3.yzw, r3.yyzw, r8.xxzw
  79  0x00000A94:   mad r2.xyzw, -r2.xyzw, r4.zzww, r1.xyxy
  80  0x00000ABC:   sample_l_indexable(texture2d)(float,float,float,float) r4.xyz, r2.xyxx, t0.xyzw, s0, l(0.000000)
  81  0x00000AF0:   mov r7.xz, r8.yywy
  82  0x00000B04:   mad r4.xyz, r7.xyzx, r4.xyzx, r5.xyzx
  83  0x00000B28:   add r3.yzw, r3.yyzw, r7.xxyz
  84  0x00000B44:   sample_l_indexable(texture2d)(float,float,float,float) r2.xyz, r2.zwzz, t0.xyzw, s0, l(0.000000)
  85  0x00000B78:   mad r5.y, r4.w, -cb2[2].z, l(1.000000)
  86  0x00000BA4:   mad r5.z, r4.w, r0.w, r3.x
  87  0x00000BC8:   add r5.x, -cb2[2].z, l(1.000000)
  88  0x00000BEC:   mad r2.xyz, r5.xyzx, r2.xyzx, r4.xyzx
  89  0x00000C10:   add r3.xyz, r3.yzwy, r5.xyzx
  90  0x00000C2C:   div r2.xyz, r2.xyzx, r3.xyzx
  91  0x00000C48: else 
  92  0x00000C4C:   sample_l_indexable(texture2d)(float,float,float,float) r2.xyz, r1.xyxx, t0.xyzw, s0, l(0.000000)
  93  0x00000C80: endif 
  94  0x00000C84: sample_indexable(texture2d)(float,float,float,float) r0.xyz, r0.xyxx, t1.xyzw, s1
  95  0x00000CB0: mul r0.xyz, r0.xyzx, cb2[7].xxxx
  96  0x00000CD0: mad r0.xyz, cb2[7].yyyy, r2.xyzx, r0.xyzx

  97  0x00000CF8: lt r1.xyz, r0.xyzx, cb13[0].xxxx
  98  0x00000D18: movc r2.xyzw, r1.xxxx, cb13[2].xyzw, cb13[1].xyzw
  99  0x00000D44: mad r0.xw, r2.xxxy, r0.xxxx, r2.zzzw
 100  0x00000D68: div_sat r2.x, r0.x, r0.w
 101  0x00000D84: movc r3.xyzw, r1.yyyy, cb13[2].xyzw, cb13[1].xyzw
 102  0x00000DB0: mad r0.xy, r3.xyxx, r0.yyyy, r3.zwzz
 103  0x00000DD4: div_sat r2.y, r0.x, r0.y
 104  0x00000DF0: movc r1.xyzw, r1.zzzz, cb13[2].xyzw, cb13[1].xyzw
 105  0x00000E1C: mad r0.xy, r1.xyxx, r0.zzzz, r1.zwzz
 106  0x00000E40: div_sat r2.z, r0.x, r0.y
 107  0x00000E5C: dp3_sat r0.x, r2.xyzx, cb2[4].xyzx
 108  0x00000E7C: dp3_sat r0.y, r2.xyzx, cb2[5].xyzx
 109  0x00000E9C: dp3_sat r0.z, r2.xyzx, cb2[6].xyzx
 110  0x00000EBC: log r0.xyz, r0.xyzx
 111  0x00000ED0: mul r0.xyz, r0.xyzx, l(0.416667, 0.416667, 0.416667, 0.000000)
 112  0x00000EF8: exp r0.xyz, r0.xyzx
 113  0x00000F0C: mad r0.xyz, r0.xyzx, l(1.055000, 1.055000, 1.055000, 0.000000), l(-0.055000, -0.055000, -0.055000, 0.000000)
 114  0x00000F48: max r0.xyz, r0.xyzx, l(0.000000, 0.000000, 0.000000, 0.000000)
 115  0x00000F70: mad r0.xyz, r0.xyzx, l(0.968750, 0.968750, 0.968750, 0.000000), l(0.015625, 0.015625, 0.015625, 0.000000)
 116  0x00000FAC: sample_l_indexable(texture3d)(float,float,float,float) r0.xyz, r0.xyzx, t2.xyzw, s2, l(0.000000)
 117  0x00000FE0: dp3 r0.w, r0.xyzx, l(0.212600, 0.715200, 0.072200, 0.000000)
 118  0x00001008: mov o0.xyzw, r0.xyzw
 119  0x0000101C: mov o1.x, r0.w
 120  0x00001030: ret 
// Approximately 0 instruction slots used
*/