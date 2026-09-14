// ---- Created with 3Dmigoto v1.3.16 on Wed Jun 24 15:00:48 2026
Buffer<float4> t14 : register(t14);

Texture2D<uint4> t5 : register(t5);

Texture2D<float4> t4 : register(t4);

Texture3D<float4> t3 : register(t3);

Texture2D<float4> t2 : register(t2);

Texture2D<float4> t1 : register(t1);

Texture2D<float4> t0 : register(t0);

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
  float4 cb2[17];
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
  float4 r0,r1,r2,r3,r4,r5;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = cb2[4].xy + v1.xy;
  TM_UV(r0.xy);

  r1.xyz = t0.Sample(s0_s, r0.xy).xyz;

  r0.zw = cb2[4].wz + r0.yx;
  r2.xy = cb2[2].xy * r0.wz;
  r2.yz = (uint2)r2.xy;
  r2.x = (uint)r2.y >> 1;
  r2.w = 0;
  r1.w = t5.Load(r2.xzw).x;
  r2.x = (int)r2.y & 1;
  r3.x = (uint)r1.w >> 4;
  r1.w = r2.x ? r3.x : r1.w;
  r2.x = (int)r1.w & 2;


  r3.xyz = t2.Sample(s2_s, r0.xy).xyz;
  r3.xyz = cb2[8].xxx * GS.Bloom * r3.xyz;
  r1.xyz = cb2[8].yyy * r1.xyz + r3.xyz;
  TM_Color(r1.xyz);

  // r3.xyz = cmp(r1.xyz < cb13[0].xxx);
  // r4.xyzw = r3.xxxx ? cb13[2].xyzw : cb13[1].xyzw;
  // r0.xy = r4.xy * r1.xx + r4.zw;
  // r4.x = saturate(r0.x / r0.y);
  // r5.xyzw = r3.yyyy ? cb13[2].xyzw : cb13[1].xyzw;
  // r0.xy = r5.xy * r1.yy + r5.zw;
  // r4.y = saturate(r0.x / r0.y);
  // r3.xyzw = r3.zzzz ? cb13[2].xyzw : cb13[1].xyzw;
  // r0.xy = r3.xy * r1.zz + r3.zw;
  // r4.z = saturate(r0.x / r0.y);
  TM_Rolloff();

  // r1.x = saturate(dot(r4.xyz, cb2[5].xyz));
  // r1.y = saturate(dot(r4.xyz, cb2[6].xyz));
  // r1.z = saturate(dot(r4.xyz, cb2[7].xyz));
  TM_LumaThingy(5);

  // r1.xyz = log2(r1.xyz);
  // r1.xyz = float3(0.416666657,0.416666657,0.416666657) * r1.xyz;
  // r1.xyz = exp2(r1.xyz);
  // r1.xyz = r1.xyz * float3(1.05499995,1.05499995,1.05499995) + float3(-0.0549999997,-0.0549999997,-0.0549999997);
  // r1.xyz = max(float3(0,0,0), r1.xyz);
  TM_Gamma();

  // r1.xyz = r1.xyz * float3(0.96875,0.96875,0.96875) + float3(0.015625,0.015625,0.015625);
  // r3.xyz = t3.SampleLevel(s3_s, r1.xyz, 0).xyz;
  TM_LUT(t3, s3_s);

  TM_Upgrade();
  r3.xyz = tmi.r0;

  if (r2.x != 0) { //TODO: Broken thermal scope
    r0.xy = asint((int2)r1.ww & int2(4,8));
    r0.x = cmp((int)r0.x != 0);
    r1.xy = t4.Load(r2.yzw).xy;
    r1.xy = float2(255.5,255.100006) * r1.yx;
    r1.xy = (uint2)r1.xy;
    r1.x = (uint)r1.x << 11;
    bitmask.x = ((~(-1 << 8)) << 3) & 0xffffffff;  r1.x = (((uint)r1.y << 3) & bitmask.x) | ((uint)r1.x & ~bitmask.x);
    r1.y = (int)r1.x + 2;
    r1.y = t14.Load(r1.y).x;
    r1.y = cmp(0 < r1.y);
    r0.y = cmp((int)r0.y == 0);
    r0.x = r0.y ? r0.x : 0;
    r0.y = t14.Load(r1.x).w;
    r0.y = cmp(0 < r0.y);
    r0.x = r0.y ? r0.x : 0;

    if (r0.x != 0) {
      if (r1.y != 0) {
        r1.xyz = cb2[14].xyz;
        r2.xyz = cb2[13].xyz;
      } else {
        r1.xyz = cb2[12].xyz;
        r2.xyz = cb2[11].xyz;
      }
      r0.x = cb2[9].x;
      r0.y = cb2[10].y;
      r4.xyz = t1.Gather(s1_s, r0.wz).xzw;
      r5.xy = abs(r4.zz) + -abs(r4.yx);
      r5.z = abs(r4.z) * 0.000250000012 + 2.32830644e-010;
      r1.w = dot(r5.xyz, r5.xyz);
      // r1.w = (uint)r1.w >> 1;
      // r1.w = (int)-r1.w + 0x5f3759df;
        r1.w = rsqrt(r1.w);
      r1.w = r5.z * r1.w;
      r1.w = min(1, r1.w);
    } else {
      r1.xyz = cb2[16].xyz;
      r2.xyz = cb2[15].xyz;
      r0.x = cb2[9].y;
      r0.y = cb2[10].z;
      r1.w = dot(r3.xyz, float3(0.212599993,0.715200007,0.0722000003));
    }
    float what = r1.w;

    r4.xy = cb2[0].yx + r0.zw;
    r4.xy = r4.xy * float2(1024,1024) + float2(-512,-512);
    r4.zw = float2(0.554549694,0.308517009) * r4.yx;
    r4.zw = frac(r4.zw);
    r4.xy = r4.yx * r4.zw + r4.xy;
    r2.w = r4.x * r4.y;
    r2.w = frac(r2.w);
    r2.w = r2.w * 2 + -1;
    r2.w = r2.w * r0.y + 1;
    r1.w = saturate(r2.w * r1.w);
    r1.w = (int)r1.w;
    // { int _itof = asint(r1.w); r1.w = (float)_itof; }
    r1.w = -1.06529242e+009 + r1.w;
    r0.x = r0.x * r1.w + 1.06529242e+009;
    r0.x = (int)r0.x;
    // { int _itof2 = asint(r0.x); r0.x = (float)_itof2; }
    r0.x = saturate(r0.x);
    r1.xyz = r1.xyz + -r2.xyz;
    r1.xyz = r0.xxx * r1.xyz + r2.xyz;
    r0.xz = cb2[0].ww * r0.zw;
    r0.xz = frac(r0.xz);
    r0.xz = r0.xz * float2(1024,1024) + float2(-512,-512);
    r2.xy = float2(0.554549694,0.308517009) * r0.zx;
    r2.xy = frac(r2.xy);
    r0.xz = r0.zx * r2.xy + r0.xz;
    r0.x = r0.x * r0.z;
    r0.x = frac(r0.x);
    r0.x = r0.x * 2 + -1;
    r0.x = r0.x * r0.y + 1;

    // r3.xyz = saturate(r1.xyz * r0.xxx);
      r3.xyz = saturate(r1.xyz * r0.xxx * sqrt(what));
  }
  // r3.w = dot(r3.xyz, float3(0.212599993,0.715200007,0.0722000003));
  // o0.xyzw = r3.xyzw;
  // o1.x = r3.w;
  o0.xyz = r3.xyz;
  o0.w = o1.x = TM_LumaForAA(o0.xyz, true, true);
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
      0x0000000C: dcl_constantbuffer CB2[17], immediateIndexed
      0x0000001C: dcl_constantbuffer CB13[3], immediateIndexed
      0x0000002C: dcl_sampler s0, mode_default
      0x00000038: dcl_sampler s1, mode_default
      0x00000044: dcl_sampler s2, mode_default
      0x00000050: dcl_sampler s3, mode_default
      0x0000005C: dcl_resource_texture2d (float,float,float,float) t0
      0x0000006C: dcl_resource_texture2d (float,float,float,float) t1
      0x0000007C: dcl_resource_texture2d (float,float,float,float) t2
      0x0000008C: dcl_resource_texture3d (float,float,float,float) t3
      0x0000009C: dcl_resource_texture2d (float,float,float,float) t4
      0x000000AC: dcl_resource_texture2d (uint,uint,uint,uint) t5
      0x000000BC: dcl_resource_buffer (float,float,float,float) t14
      0x000000CC: dcl_input_ps linear v1.xy
      0x000000D8: dcl_output o0.xyzw
      0x000000E4: dcl_output o1.x
      0x000000F0: dcl_temps 6
   0  0x000000F8: add r0.xy, v1.xyxx, cb2[4].xyxx
   1  0x00000118: sample_indexable(texture2d)(float,float,float,float) r1.xyz, r0.xyxx, t0.xyzw, s0
   2  0x00000144: add r0.zw, r0.yyyx, cb2[4].wwwz
   3  0x00000164: mul r2.xy, r0.wzww, cb2[2].xyxx
   4  0x00000184: ftou r2.yz, r2.xxyx
   5  0x00000198: ushr r2.x, r2.y, l(1)
   6  0x000001B4: mov r2.w, l(0)
   7  0x000001C8: ld_indexable(texture2d)(uint,uint,uint,uint) r1.w, r2.xzww, t5.yzwx
   8  0x000001EC: and r2.x, r2.y, l(1)
   9  0x00000208: ushr r3.x, r1.w, l(4)
  10  0x00000224: movc r1.w, r2.x, r3.x, r1.w
  11  0x00000248: and r2.x, r1.w, l(2)
  12  0x00000264: sample_indexable(texture2d)(float,float,float,float) r3.xyz, r0.xyxx, t2.xyzw, s2
  13  0x00000290: mul r3.xyz, r3.xyzx, cb2[8].xxxx
  14  0x000002B0: mad r1.xyz, cb2[8].yyyy, r1.xyzx, r3.xyzx
  15  0x000002D8: lt r3.xyz, r1.xyzx, cb13[0].xxxx
  16  0x000002F8: movc r4.xyzw, r3.xxxx, cb13[2].xyzw, cb13[1].xyzw
  17  0x00000324: mad r0.xy, r4.xyxx, r1.xxxx, r4.zwzz
  18  0x00000348: div_sat r4.x, r0.x, r0.y
  19  0x00000364: movc r5.xyzw, r3.yyyy, cb13[2].xyzw, cb13[1].xyzw
  20  0x00000390: mad r0.xy, r5.xyxx, r1.yyyy, r5.zwzz
  21  0x000003B4: div_sat r4.y, r0.x, r0.y
  22  0x000003D0: movc r3.xyzw, r3.zzzz, cb13[2].xyzw, cb13[1].xyzw
  23  0x000003FC: mad r0.xy, r3.xyxx, r1.zzzz, r3.zwzz
  24  0x00000420: div_sat r4.z, r0.x, r0.y
  25  0x0000043C: dp3_sat r1.x, r4.xyzx, cb2[5].xyzx
  26  0x0000045C: dp3_sat r1.y, r4.xyzx, cb2[6].xyzx
  27  0x0000047C: dp3_sat r1.z, r4.xyzx, cb2[7].xyzx
  28  0x0000049C: log r1.xyz, r1.xyzx
  29  0x000004B0: mul r1.xyz, r1.xyzx, l(0.416667, 0.416667, 0.416667, 0.000000)
  30  0x000004D8: exp r1.xyz, r1.xyzx
  31  0x000004EC: mad r1.xyz, r1.xyzx, l(1.055000, 1.055000, 1.055000, 0.000000), l(-0.055000, -0.055000, -0.055000, 0.000000)
  32  0x00000528: max r1.xyz, r1.xyzx, l(0.000000, 0.000000, 0.000000, 0.000000)
  33  0x00000550: mad r1.xyz, r1.xyzx, l(0.968750, 0.968750, 0.968750, 0.000000), l(0.015625, 0.015625, 0.015625, 0.000000)
  34  0x0000058C: sample_l_indexable(texture3d)(float,float,float,float) r3.xyz, r1.xyzx, t3.xyzw, s3, l(0.000000)
  35  0x000005C0: if_nz r2.x
  36  0x000005CC:   and r0.xy, r1.wwww, l(4, 8, 0, 0)
  37  0x000005F4:   ine r0.x, r0.x, l(0)
  38  0x00000610:   ld_indexable(texture2d)(float,float,float,float) r1.xy, r2.yzww, t4.xyzw
  39  0x00000634:   mul r1.xy, r1.yxyy, l(255.500000, 255.100006, 0.000000, 0.000000)
  40  0x0000065C:   ftou r1.xy, r1.xyxx
  41  0x00000670:   ishl r1.x, r1.x, l(11)
  42  0x0000068C:   bfi r1.x, l(8), l(3), r1.y, r1.x
  43  0x000006B8:   iadd r1.y, r1.x, l(2)
  44  0x000006D4:   ld_indexable(buffer)(float,float,float,float) r1.y, r1.yyyy, t14.yxzw
  45  0x000006F8:   lt r1.y, l(0.000000), r1.y
  46  0x00000714:   ieq r0.y, r0.y, l(0)
  47  0x00000730:   and r0.x, r0.y, r0.x
  48  0x0000074C:   ld_indexable(buffer)(float,float,float,float) r0.y, r1.xxxx, t14.xwyz
  49  0x00000770:   lt r0.y, l(0.000000), r0.y
  50  0x0000078C:   and r0.x, r0.y, r0.x
  51  0x000007A8:   if_nz r0.x
  52  0x000007B4:     if_nz r1.y
  53  0x000007C0:       mov r1.xyz, cb2[14].xyzx
  54  0x000007D8:       mov r2.xyz, cb2[13].xyzx
  55  0x000007F0:     else 
  56  0x000007F4:       mov r1.xyz, cb2[12].xyzx
  57  0x0000080C:       mov r2.xyz, cb2[11].xyzx
  58  0x00000824:     endif 
  59  0x00000828:     mov r0.x, cb2[9].x
  60  0x00000840:     mov r0.y, cb2[10].y
  61  0x00000858:     gather4_indexable(texture2d)(float,float,float,float) r4.xyz, r0.wzww, t1.xzwy, s1.x
  62  0x00000884:     add r5.xy, -|r4.yxyy|, |r4.zzzz|
  63  0x000008A8:     mad r5.z, |r4.z|, l(0.000250), l(0.000000)
  64  0x000008D0:     dp3 r1.w, r5.xyzx, r5.xyzx
  65  0x000008EC:     ishr r1.w, r1.w, l(1)
  66  0x00000908:     iadd r1.w, -r1.w, l(0x5f3759df)
  67  0x00000928:     mul r1.w, r1.w, r5.z
  68  0x00000944:     min r1.w, r1.w, l(1.000000)
  69  0x00000960:   else 
  70  0x00000964:     mov r1.xyz, cb2[16].xyzx
  71  0x0000097C:     mov r2.xyz, cb2[15].xyzx
  72  0x00000994:     mov r0.x, cb2[9].y
  73  0x000009AC:     mov r0.y, cb2[10].z
  74  0x000009C4:     dp3 r1.w, r3.xyzx, l(0.212600, 0.715200, 0.072200, 0.000000)
  75  0x000009EC:   endif 
  76  0x000009F0:   add r4.xy, r0.zwzz, cb2[0].yxyy
  77  0x00000A10:   mad r4.xy, r4.xyxx, l(1024.000000, 1024.000000, 0.000000, 0.000000), l(-512.000000, -512.000000, 0.000000, 0.000000)
  78  0x00000A4C:   mul r4.zw, r4.yyyx, l(0.000000, 0.000000, 0.554550, 0.308517)
  79  0x00000A74:   frc r4.zw, r4.zzzw
  80  0x00000A88:   mad r4.xy, r4.yxyy, r4.zwzz, r4.xyxx
  81  0x00000AAC:   mul r2.w, r4.y, r4.x
  82  0x00000AC8:   frc r2.w, r2.w
  83  0x00000ADC:   mad r2.w, r2.w, l(2.000000), l(-1.000000)
  84  0x00000B00:   mad r2.w, r2.w, r0.y, l(1.000000)
  85  0x00000B24:   mul_sat r1.w, r1.w, r2.w
  86  0x00000B40:   itof r1.w, r1.w
  87  0x00000B54:   add r1.w, r1.w, l(-1065292416.000000)
  88  0x00000B70:   mad r0.x, r0.x, r1.w, l(1065292416.000000)
  89  0x00000B94:   ftoi r0.x, r0.x
  90  0x00000BA8:   mov_sat r0.x, r0.x
  91  0x00000BBC:   add r1.xyz, -r2.xyzx, r1.xyzx
  92  0x00000BDC:   mad r1.xyz, r0.xxxx, r1.xyzx, r2.xyzx
  93  0x00000C00:   mul r0.xz, r0.zzwz, cb2[0].wwww
  94  0x00000C20:   frc r0.xz, r0.xxzx
  95  0x00000C34:   mad r0.xz, r0.xxzx, l(1024.000000, 0.000000, 1024.000000, 0.000000), l(-512.000000, 0.000000, -512.000000, 0.000000)
  96  0x00000C70:   mul r2.xy, r0.zxzz, l(0.554550, 0.308517, 0.000000, 0.000000)
  97  0x00000C98:   frc r2.xy, r2.xyxx
  98  0x00000CAC:   mad r0.xz, r0.zzxz, r2.xxyx, r0.xxzx
  99  0x00000CD0:   mul r0.x, r0.z, r0.x
 100  0x00000CEC:   frc r0.x, r0.x
 101  0x00000D00:   mad r0.x, r0.x, l(2.000000), l(-1.000000)
 102  0x00000D24:   mad r0.x, r0.x, r0.y, l(1.000000)
 103  0x00000D48:   mul_sat r3.xyz, r0.xxxx, r1.xyzx
 104  0x00000D64: endif 
 105  0x00000D68: dp3 r3.w, r3.xyzx, l(0.212600, 0.715200, 0.072200, 0.000000)
 106  0x00000D90: mov o0.xyzw, r3.xyzw
 107  0x00000DA4: mov o1.x, r3.w
 108  0x00000DB8: ret 
// Approximately 0 instruction slots used

*/