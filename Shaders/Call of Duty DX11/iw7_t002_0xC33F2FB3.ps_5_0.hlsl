// ---- Created with 3Dmigoto v1.3.16 on Tue Jun 23 21:14:18 2026
Buffer<float4> t14 : register(t14);

Texture2D<uint4> t4 : register(t4);

Texture2D<float4> t3 : register(t3);

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
  float4 cb2[14];
}




// 3Dmigoto declarations
#define cmp -
#include "iw7_common.hlsl"

// main, xray
// 0
void main(
  float4 v0 : SV_POSITION0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_TARGET0,
  out float o1 : SV_TARGET1)
{
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8,r9;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = cb2[3].xy + v1.xy;
  TM_UV(r0.xy);
  r1.xyz = t0.Sample(s0_s, r0.xy).xyz;
 
  // xray mask and uv stuff
  r0.zw = cb2[3].wz + r0.yx;
  r2.xy = cb2[2].xy * r0.wz;
  r2.yz = (uint2)r2.xy;
  r2.x = (uint)r2.y >> 1;
  r2.w = 0;
  r1.w = t4.Load(r2.xzw).x;
  r3.x = (int)r2.y & 1;
  r3.y = (uint)r1.w >> 4;
  r1.w = r3.x ? r3.y : r1.w;
  r3.y = (int)r1.w & 4;

  r4.xyz = t1.Sample(s1_s, r0.xy).xyz;
  r4.xyz = cb2[7].xxx * GS.Bloom * r4.xyz;
  r1.xyz = cb2[7].yyy * r1.xyz + r4.xyz;
  TM_Color(r1.xyz);

  // r4.xyz = cmp(r1.xyz < cb13[0].xxx);
  // r5.xyzw = r4.xxxx ? cb13[2].xyzw : cb13[1].xyzw;
  // r0.xy = r5.xy * r1.xx + r5.zw;
  // r5.x = saturate(r0.x / r0.y);
  // r6.xyzw = r4.yyyy ? cb13[2].xyzw : cb13[1].xyzw;
  // r0.xy = r6.xy * r1.yy + r6.zw;
  // r5.y = saturate(r0.x / r0.y);
  // r4.xyzw = r4.zzzz ? cb13[2].xyzw : cb13[1].xyzw;
  // r0.xy = r4.xy * r1.zz + r4.zw;
  // r5.z = saturate(r0.x / r0.y);
  TM_Rolloff();

  // r1.x = saturate(dot(r5.xyz, cb2[4].xyz));
  // r1.y = saturate(dot(r5.xyz, cb2[5].xyz));
  // r1.z = saturate(dot(r5.xyz, cb2[6].xyz));
  TM_LumaThingy(4);

  // r1.xyz = log2(r1.xyz);
  // r1.xyz = float3(0.416666657,0.416666657,0.416666657) * r1.xyz;
  // r1.xyz = exp2(r1.xyz);
  // r1.xyz = r1.xyz * float3(1.05499995,1.05499995,1.05499995) + float3(-0.0549999997,-0.0549999997,-0.0549999997);
  // r1.xyz = max(float3(0,0,0), r1.xyz);
  TM_Gamma();

  // r1.xyz = r1.xyz * float3(0.96875,0.96875,0.96875) + float3(0.015625,0.015625,0.015625);
  // r4.xyz = t2.SampleLevel(s2_s, r1.xyz, 0).xyz;
  TM_LUT(t2, s2_s);

  TM_Upgrade();
  r4.xyz = tmi.r0;

  // // xray mask and uv stuff
  // r0.zw = cb2[3].wz + tmi.uvRaw.yx;
  // r2.xy = cb2[2].xy * r0.wz;
  // // r2.xy = (uint2)r2.xy;
  // r2.xy = asuint(r2.xy);
  // // r2.x = (uint)r2.y >> 1;
  // r2.x = (asuint(r2.y) >> 1);
  // r2.zw = 0;
  // r1.w = t4.Load(asuint(r2.xzw)).x;
  // // r3.x = (int)r2.y & 1;
  // r3.x = (asuint(r2.y) & 1);
  // // r3.y = (uint)r1.w >> 4;
  // r3.y = (asuint(r1.w) >> 4);
  // r1.w = r3.x ? r3.y : r1.w;
  // // r3.y = (int)r1.w & 4;
  // r3.y = (asuint(r1.w) & 4);

  // xray
  if (r3.y != 0) {
    r0.x = (int)r1.w & 8;
    r1.xy = t3.Load(r2.yzw).xy;
    r1.xy = float2(255.5,255.100006) * r1.yx;
    r1.xy = (uint2)r1.xy;
    r0.y = (uint)r1.x << 11;
    bitmask.y = ((~(-1 << 8)) << 3) & 0xffffffff;  r0.y = (((uint)r1.y << 3) & bitmask.y) | ((uint)r0.y & ~bitmask.y);
    r1.xy = (int2)r0.yy + int2(1,2);
    r1.xz = t14.Load(r1.x).yw;
    r1.yw = t14.Load(r1.y).yz;
    r5.xyzw = t14.Load(r0.y).xyzw;
    r1.yw = cmp(float2(0,0) < r1.yw);
    r0.y = r0.x ? 1 : 0;
    r0.y = r1.y ? r0.y : r1.x;
    r1.x = (uint)cb2[8].w;
    if (r0.x != 0) {
      r0.y = r0.y * r1.z;
      r6.xyzw = float4(-1,-1,-1,-1) + r5.xyzw;
      r6.xyzw = cb2[8].xxxx * r6.xyzw + float4(1,1,1,1);
      r6.xyzw = cb2[11].xyzw * r6.xyzw;
      r5.xyzw = r6.xyzw * r1.zzzz;
      r6.xyzw = cb2[13].xyzw;
      r7.xyzw = cb2[12].xyzw;
    } else {
      r6.xyzw = cb2[9].xyzw;
      r7.xyzw = cb2[10].xyzw;
    }
    r1.y = cb2[0].x + cb2[0].x;
    r1.y = r1.w ? r1.y : 1;
    r5.xyzw = r5.xyzw * r1.yyyy;
    if (r1.x == 0) {
      r1.y = 0;
    } else {
      r8.xy = (int2)-r1.xx + (int2)r2.zy;
      r8.zw = r2.xw;
      r1.z = t4.Load(r8.zxw).x;
      r1.w = (uint)r1.z >> 4;
      r1.z = r3.x ? r1.w : r1.z;
      r1.z = (int)r1.z & 4;
      r1.z = cmp((int)r1.z != 0);
      r9.xy = (int2)r1.xx + (int2)r2.zy;
      r9.zw = r8.zw;
      r1.x = t4.Load(r9.zxw).x;
      r1.w = (uint)r1.x >> 4;
      r1.x = r3.x ? r1.w : r1.x;
      r1.x = (int)r1.x & 4;
      r1.x = cmp((int)r1.x != 0);
      r2.y = (uint)r8.y >> 1;
      r1.w = t4.Load(r2.yzw).x;
      r2.y = (int)r8.y & 1;
      r3.x = (uint)r1.w >> 4;
      r1.w = r2.y ? r3.x : r1.w;
      r1.w = (int)r1.w & 4;
      r1.w = cmp((int)r1.w != 0);
      r2.x = (uint)r9.y >> 1;
      r2.x = t4.Load(r2.xzw).x;
      r2.y = (int)r9.y & 1;
      r2.z = (uint)r2.x >> 4;
      r2.x = r2.y ? r2.z : r2.x;
      r2.x = (int)r2.x & 4;
      r2.x = cmp((int)r2.x != 0);
      r1.x = r1.x ? r1.z : 0;
      r1.x = r1.w ? r1.x : 0;
      r1.x = r2.x ? r1.x : 0;
      r1.y = ~(int)r1.x;
    }
    if (r1.y == 0) {
      r0.zw = cb2[1].yx * r0.zw;
      r1.xyzw = float4(12,12,12,12) * r0.wzwz;
      r1.xyzw = cmp(r1.xyzw >= -r1.zwzw);
      r1.xyzw = r1.xyzw ? float4(12,12,0.0833333358,0.0833333358) : float4(-12,-12,-0.0833333358,-0.0833333358);
      r1.zw = r1.wz * r0.zw;
      r1.zw = frac(r1.zw);
      r1.xy = r1.yx * r1.zw;
      r1.xy = (uint2)r1.xy;
      r1.zw = cmp((int2)r1.yx == int2(0,0));
      r1.xy = (int2)r1.xy & int2(14,14);
      r0.z = (uint)r0.z;
      r1.xy = cmp((int2)r1.xy != int2(6,6));
      r1.xy = r1.xy ? r1.zw : 0;
      r0.w = (int)r1.y | (int)r1.x;
      r0.z = (int)r0.z & 2;
      r0.z = cmp((int)r0.z != 0);
      r0.x = r0.x ? r0.w : r0.z;
      r1.xyzw = r0.xxxx ? r7.xyzw : r6.xyzw;
      r0.xyzw = r1.xyzw * r0.yyyy;
      r5.xyzw = r0.xyzw * r5.xyzw;
    }
    r0.xyz = r5.xyz + -r4.xyz;
    r4.xyz = r5.www * r0.xyz + r4.xyz;
  }

  // r4.w = dot(r4.xyz, float3(0.212599993,0.715200007,0.0722000003));
  // o0.xyzw = r4.xyzw;
  // o1.x = r4.w;
  o0.xyz = r4.xyz;
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
      0x0000000C: dcl_constantbuffer CB2[14], immediateIndexed
      0x0000001C: dcl_constantbuffer CB13[3], immediateIndexed
      0x0000002C: dcl_sampler s0, mode_default
      0x00000038: dcl_sampler s1, mode_default
      0x00000044: dcl_sampler s2, mode_default
      0x00000050: dcl_resource_texture2d (float,float,float,float) t0
      0x00000060: dcl_resource_texture2d (float,float,float,float) t1
      0x00000070: dcl_resource_texture3d (float,float,float,float) t2
      0x00000080: dcl_resource_texture2d (float,float,float,float) t3
      0x00000090: dcl_resource_texture2d (uint,uint,uint,uint) t4
      0x000000A0: dcl_resource_buffer (float,float,float,float) t14
      0x000000B0: dcl_input_ps linear v1.xy
      0x000000BC: dcl_output o0.xyzw
      0x000000C8: dcl_output o1.x
      0x000000D4: dcl_temps 10

   0  0x000000DC: add r0.xy, v1.xyxx, cb2[3].xyxx
   1  0x000000FC: sample_indexable(texture2d)(float,float,float,float) r1.xyz, r0.xyxx, t0.xyzw, s0
   2  0x00000128: add r0.zw, r0.yyyx, cb2[3].wwwz
   3  0x00000148: mul r2.xy, r0.wzww, cb2[2].xyxx
   4  0x00000168: ftou r2.yz, r2.xxyx
   5  0x0000017C: ushr r2.x, r2.y, l(1)
   6  0x00000198: mov r2.w, l(0)
   7  0x000001AC: ld_indexable(texture2d)(uint,uint,uint,uint) r1.w, r2.xzww, t4.yzwx
   8  0x000001D0: and r3.x, r2.y, l(1)
   9  0x000001EC: ushr r3.y, r1.w, l(4)
  10  0x00000208: movc r1.w, r3.x, r3.y, r1.w
  11  0x0000022C: and r3.y, r1.w, l(4)
  12  0x00000248: sample_indexable(texture2d)(float,float,float,float) r4.xyz, r0.xyxx, t1.xyzw, s1
  13  0x00000274: mul r4.xyz, r4.xyzx, cb2[7].xxxx
  14  0x00000294: mad r1.xyz, cb2[7].yyyy, r1.xyzx, r4.xyzx
  15  0x000002BC: lt r4.xyz, r1.xyzx, cb13[0].xxxx
  16  0x000002DC: movc r5.xyzw, r4.xxxx, cb13[2].xyzw, cb13[1].xyzw
  17  0x00000308: mad r0.xy, r5.xyxx, r1.xxxx, r5.zwzz
  18  0x0000032C: div_sat r5.x, r0.x, r0.y
  19  0x00000348: movc r6.xyzw, r4.yyyy, cb13[2].xyzw, cb13[1].xyzw
  20  0x00000374: mad r0.xy, r6.xyxx, r1.yyyy, r6.zwzz
  21  0x00000398: div_sat r5.y, r0.x, r0.y
  22  0x000003B4: movc r4.xyzw, r4.zzzz, cb13[2].xyzw, cb13[1].xyzw
  23  0x000003E0: mad r0.xy, r4.xyxx, r1.zzzz, r4.zwzz
  24  0x00000404: div_sat r5.z, r0.x, r0.y
  25  0x00000420: dp3_sat r1.x, r5.xyzx, cb2[4].xyzx
  26  0x00000440: dp3_sat r1.y, r5.xyzx, cb2[5].xyzx
  27  0x00000460: dp3_sat r1.z, r5.xyzx, cb2[6].xyzx
  28  0x00000480: log r1.xyz, r1.xyzx
  29  0x00000494: mul r1.xyz, r1.xyzx, l(0.416667, 0.416667, 0.416667, 0.000000)
  30  0x000004BC: exp r1.xyz, r1.xyzx
  31  0x000004D0: mad r1.xyz, r1.xyzx, l(1.055000, 1.055000, 1.055000, 0.000000), l(-0.055000, -0.055000, -0.055000, 0.000000)
  32  0x0000050C: max r1.xyz, r1.xyzx, l(0.000000, 0.000000, 0.000000, 0.000000)
  33  0x00000534: mad r1.xyz, r1.xyzx, l(0.968750, 0.968750, 0.968750, 0.000000), l(0.015625, 0.015625, 0.015625, 0.000000)
  34  0x00000570: sample_l_indexable(texture3d)(float,float,float,float) r4.xyz, r1.xyzx, t2.xyzw, s2, l(0.000000)
  35  0x000005A4: if_nz r3.y
  36  0x000005B0:   and r0.x, r1.w, l(8)
  37  0x000005CC:   ld_indexable(texture2d)(float,float,float,float) r1.xy, r2.yzww, t3.xyzw
  38  0x000005F0:   mul r1.xy, r1.yxyy, l(255.500000, 255.100006, 0.000000, 0.000000)
  39  0x00000618:   ftou r1.xy, r1.xyxx
  40  0x0000062C:   ishl r0.y, r1.x, l(11)
  41  0x00000648:   bfi r0.y, l(8), l(3), r1.y, r0.y
  42  0x00000674:   iadd r1.xy, r0.yyyy, l(1, 2, 0, 0)
  43  0x0000069C:   ld_indexable(buffer)(float,float,float,float) r1.xz, r1.xxxx, t14.yxwz
  44  0x000006C0:   ld_indexable(buffer)(float,float,float,float) r1.yw, r1.yyyy, t14.xywz
  45  0x000006E4:   ld_indexable(buffer)(float,float,float,float) r5.xyzw, r0.yyyy, t14.xyzw
  46  0x00000708:   lt r1.yw, l(0.000000, 0.000000, 0.000000, 0.000000), r1.yyyw
  47  0x00000730:   movc r0.y, r0.x, l(1.000000), l(0)
  48  0x00000754:   movc r0.y, r1.y, r0.y, r1.x
  49  0x00000778:   ftou r1.x, cb2[8].w
  50  0x00000790:   if_nz r0.x
  51  0x0000079C:     mul r0.y, r1.z, r0.y
  52  0x000007B8:     add r6.xyzw, r5.xyzw, l(-1.000000, -1.000000, -1.000000, -1.000000)
  53  0x000007E0:     mad r6.xyzw, cb2[8].xxxx, r6.xyzw, l(1.000000, 1.000000, 1.000000, 1.000000)
  54  0x00000814:     mul r6.xyzw, r6.xyzw, cb2[11].xyzw
  55  0x00000834:     mul r5.xyzw, r1.zzzz, r6.xyzw
  56  0x00000850:     mov r6.xyzw, cb2[13].xyzw
  57  0x00000868:     mov r7.xyzw, cb2[12].xyzw
  58  0x00000880:   else 
  59  0x00000884:     mov r6.xyzw, cb2[9].xyzw
  60  0x0000089C:     mov r7.xyzw, cb2[10].xyzw
  61  0x000008B4:   endif 
  62  0x000008B8:   add r1.y, cb2[0].x, cb2[0].x
  63  0x000008DC:   movc r1.y, r1.w, r1.y, l(1.000000)
  64  0x00000900:   mul r5.xyzw, r1.yyyy, r5.xyzw
  65  0x0000091C:   if_z r1.x
  66  0x00000928:     mov r1.y, l(0)
  67  0x0000093C:   else 
  68  0x00000940:     iadd r8.xy, -r1.xxxx, r2.zyzz
  69  0x00000960:     mov r8.zw, r2.xxxw
  70  0x00000974:     ld_indexable(texture2d)(uint,uint,uint,uint) r1.z, r8.zxww, t4.yzxw
  71  0x00000998:     ushr r1.w, r1.z, l(4)
  72  0x000009B4:     movc r1.z, r3.x, r1.w, r1.z
  73  0x000009D8:     and r1.z, r1.z, l(4)
  74  0x000009F4:     ine r1.z, r1.z, l(0)
  75  0x00000A10:     iadd r9.xy, r1.xxxx, r2.zyzz
  76  0x00000A2C:     mov r9.zw, r8.zzzw
  77  0x00000A40:     ld_indexable(texture2d)(uint,uint,uint,uint) r1.x, r9.zxww, t4.xyzw
  78  0x00000A64:     ushr r1.w, r1.x, l(4)
  79  0x00000A80:     movc r1.x, r3.x, r1.w, r1.x
  80  0x00000AA4:     and r1.x, r1.x, l(4)
  81  0x00000AC0:     ine r1.x, r1.x, l(0)
  82  0x00000ADC:     ushr r2.y, r8.y, l(1)
  83  0x00000AF8:     ld_indexable(texture2d)(uint,uint,uint,uint) r1.w, r2.yzww, t4.yzwx
  84  0x00000B1C:     and r2.y, r8.y, l(1)
  85  0x00000B38:     ushr r3.x, r1.w, l(4)
  86  0x00000B54:     movc r1.w, r2.y, r3.x, r1.w
  87  0x00000B78:     and r1.w, r1.w, l(4)
  88  0x00000B94:     ine r1.w, r1.w, l(0)
  89  0x00000BB0:     ushr r2.x, r9.y, l(1)
  90  0x00000BCC:     ld_indexable(texture2d)(uint,uint,uint,uint) r2.x, r2.xzww, t4.xyzw
  91  0x00000BF0:     and r2.y, r9.y, l(1)
  92  0x00000C0C:     ushr r2.z, r2.x, l(4)
  93  0x00000C28:     movc r2.x, r2.y, r2.z, r2.x
  94  0x00000C4C:     and r2.x, r2.x, l(4)
  95  0x00000C68:     ine r2.x, r2.x, l(0)
  96  0x00000C84:     and r1.x, r1.x, r1.z
  97  0x00000CA0:     and r1.x, r1.w, r1.x
  98  0x00000CBC:     and r1.x, r2.x, r1.x
  99  0x00000CD8:     not r1.y, r1.x
 100  0x00000CEC:   endif 
 101  0x00000CF0:   if_z r1.y
 102  0x00000CFC:     mul r0.zw, r0.zzzw, cb2[1].yyyx
 103  0x00000D1C:     mul r1.xyzw, r0.wzwz, l(12.000000, 12.000000, 12.000000, 12.000000)
 104  0x00000D44:     ge r1.xyzw, r1.xyzw, -r1.zwzw
 105  0x00000D64:     movc r1.xyzw, r1.xyzw, l(12.000000,12.000000,0.083333,0.083333), l(-12.000000,-12.000000,-0.083333,-0.083333)
 106  0x00000DA0:     mul r1.zw, r0.zzzw, r1.wwwz
 107  0x00000DBC:     frc r1.zw, r1.zzzw
 108  0x00000DD0:     mul r1.xy, r1.zwzz, r1.yxyy
 109  0x00000DEC:     ftou r1.xy, r1.xyxx
 110  0x00000E00:     ieq r1.zw, r1.yyyx, l(0, 0, 0, 0)
 111  0x00000E28:     and r1.xy, r1.xyxx, l(14, 14, 0, 0)
 112  0x00000E50:     ftou r0.z, r0.z
 113  0x00000E64:     ine r1.xy, r1.xyxx, l(6, 6, 0, 0)
 114  0x00000E8C:     and r1.xy, r1.xyxx, r1.zwzz
 115  0x00000EA8:     or r0.w, r1.y, r1.x
 116  0x00000EC4:     and r0.z, r0.z, l(2)
 117  0x00000EE0:     ine r0.z, r0.z, l(0)
 118  0x00000EFC:     movc r0.x, r0.x, r0.w, r0.z
 119  0x00000F20:     movc r1.xyzw, r0.xxxx, r7.xyzw, r6.xyzw
 120  0x00000F44:     mul r0.xyzw, r0.yyyy, r1.xyzw
 121  0x00000F60:     mul r5.xyzw, r5.xyzw, r0.xyzw
 122  0x00000F7C:   endif 
 123  0x00000F80:   add r0.xyz, -r4.xyzx, r5.xyzx
 124  0x00000FA0:   mad r4.xyz, r5.wwww, r0.xyzx, r4.xyzx
 125  0x00000FC4: endif 
 126  0x00000FC8: dp3 r4.w, r4.xyzx, l(0.212600, 0.715200, 0.072200, 0.000000)
 127  0x00000FF0: mov o0.xyzw, r4.xyzw
 128  0x00001004: mov o1.x, r4.w
 129  0x00001018: ret 
// Approximately 0 instruction slots used

*/