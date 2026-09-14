// ---- Created with 3Dmigoto v1.3.16 on Tue Aug 04 16:25:24 2026

cbuffer _Globals : register(b0)
{
  float4 const_color : packoffset(c0);
  bool4 bool_const[3] : packoffset(c19);
  float4 func_configs[18] : packoffset(c1);
}

SamplerState TexS0_s : register(s0);
SamplerState TexS1_s : register(s1);
SamplerState TexS2_s : register(s2);
SamplerState TexS3_s : register(s3);
Texture2D<float4> Texture0 : register(t0);
Texture2D<float4> Texture1 : register(t1);
Texture2D<float4> Texture2 : register(t2);
Texture2D<float4> Texture3 : register(t3);


// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : COLOR0,
  float4 v2 : COLOR1,
  float4 v3 : TEXCOORD0,
  float4 v4 : TEXCOORD1,
  float4 v5 : TEXCOORD2,
  float4 v6 : TEXCOORD3,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4,r5,r6,r7;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = v3.xy / v3.ww;
  r0.xyzw = Texture0.Sample(TexS0_s, r0.xy).xyzw;
  r1.xyzw = Texture1.Sample(TexS1_s, v4.xy).xyzw;
  r2.xyzw = Texture2.Sample(TexS2_s, v5.xy).xyzw;
  r3.xyzw = Texture3.Sample(TexS3_s, v6.xy).xyzw;
  r1.xyzw = r1.xyzw + -r0.xyzw;
  r4.xyzw = func_configs[0].xxxx * r1.xyzw + r0.xyzw;
  r4.xyz = bool_const[0].xxx ? r4.www : r4.xyz;
  r5.xyzw = func_configs[0].yyyy * r1.xyzw + r0.xyzw;
  r6.xyz = r5.www + -r5.xyz;
  r5.xyz = func_configs[0].zzz * r6.xyz + r5.xyz;
  r4.w = -func_configs[0].w + 1;
  r5.xyz = r5.xyz * r4.www + func_configs[0].www;
  r6.xyz = func_configs[1].xxx * r1.xyz + r0.xyz;
  r4.w = -func_configs[1].y + 1;
  r6.xyz = r6.xyz * r4.www + func_configs[1].yyy;
  r7.xyzw = func_configs[1].zzzz * r1.xyzw + r0.xyzw;
  r0.xyz = r7.www + -r7.xyz;
  r0.xyz = func_configs[1].www * r0.xyz + r7.xyz;
  r0.xyz = -func_configs[2].xxx * r0.xyz + float3(1,1,1);
  r1.x = -func_configs[2].y + 1;
  r0.xyz = r0.xyz * r1.xxx + func_configs[2].yyy;
  r1.xyz = func_configs[2].zzz * r4.xyz;
  r4.xyz = func_configs[2].www * r6.xyz;
  r0.xyz = r4.xyz * r0.xyz;
  r4.xyz = r1.xyz * r5.xyz + r0.xyz;
  r0.xyz = func_configs[3].xyw * r1.www + r0.www;
  r1.x = -func_configs[3].z + 1;
  r0.y = r0.y * r1.x + func_configs[3].z;
  r1.xy = -func_configs[4].xw + float2(1,1);
  r0.z = r0.z * r1.x + func_configs[4].x;
  r0.w = func_configs[4].y * r1.w + r0.w;
  r0.w = -func_configs[4].z * r0.w + 1;
  r0.w = r0.w * r1.y + func_configs[4].w;
  r0.xz = func_configs[5].xy * r0.xz;
  r0.z = r0.z * r0.w;
  r4.w = r0.x * r0.y + r0.z;
  r0.xyzw = -r4.xyzw + r2.xyzw;
  r1.xyzw = func_configs[6].xxxx * r0.xyzw + r4.xyzw;
  r1.xyz = bool_const[0].yyy ? r1.www : r1.xyz;
  r2.xyzw = func_configs[6].yyyy * r0.xyzw + r4.xyzw;
  r5.xyz = r2.www + -r2.xyz;
  r2.xyz = func_configs[6].zzz * r5.xyz + r2.xyz;
  r1.w = -func_configs[6].w + 1;
  r2.xyz = r2.xyz * r1.www + func_configs[6].www;
  r5.xyz = func_configs[7].xxx * r0.xyz + r4.xyz;
  r1.w = -func_configs[7].y + 1;
  r5.xyz = r5.xyz * r1.www + func_configs[7].yyy;
  r6.xyzw = func_configs[7].zzzz * r0.xyzw + r4.xyzw;
  r0.xyz = r6.www + -r6.xyz;
  r0.xyz = func_configs[7].www * r0.xyz + r6.xyz;
  r0.xyz = -func_configs[8].xxx * r0.xyz + float3(1,1,1);
  r1.w = -func_configs[8].y + 1;
  r0.xyz = r0.xyz * r1.www + func_configs[8].yyy;
  r1.xyz = func_configs[8].zzz * r1.xyz;
  r4.xyz = func_configs[8].www * r5.xyz;
  r0.xyz = r4.xyz * r0.xyz;
  r1.xyz = r1.xyz * r2.xyz + r0.xyz;
  r0.xyz = func_configs[9].xyw * r0.www + r4.www;
  r2.x = -func_configs[9].z + 1;
  r0.y = r0.y * r2.x + func_configs[9].z;
  r2.xy = -func_configs[10].xw + float2(1,1);
  r0.z = r0.z * r2.x + func_configs[10].x;
  r0.w = func_configs[10].y * r0.w + r4.w;
  r0.w = -func_configs[10].z * r0.w + 1;
  r0.w = r0.w * r2.y + func_configs[10].w;
  r0.xz = func_configs[11].xy * r0.xz;
  r0.z = r0.z * r0.w;
  r1.w = r0.x * r0.y + r0.z;
  r0.xyzw = r3.xyzw + -r1.xyzw;
  r2.xyzw = func_configs[12].xxxx * r0.xyzw + r1.xyzw;
  r2.xyz = bool_const[0].zzz ? r2.www : r2.xyz;
  r3.xyzw = func_configs[12].yyyy * r0.xyzw + r1.xyzw;
  r4.xyz = r3.www + -r3.xyz;
  r3.xyz = func_configs[12].zzz * r4.xyz + r3.xyz;
  r2.w = -func_configs[12].w + 1;
  r3.xyz = r3.xyz * r2.www + func_configs[12].www;
  r4.xyz = func_configs[13].xxx * r0.xyz + r1.xyz;
  r2.w = -func_configs[13].y + 1;
  r4.xyz = r4.xyz * r2.www + func_configs[13].yyy;
  r5.xyzw = func_configs[13].zzzz * r0.xyzw + r1.xyzw;
  r0.xyz = r5.www + -r5.xyz;
  r0.xyz = func_configs[13].www * r0.xyz + r5.xyz;
  r0.xyz = -func_configs[14].xxx * r0.xyz + float3(1,1,1);
  r1.x = -func_configs[14].y + 1;
  r0.xyz = r0.xyz * r1.xxx + func_configs[14].yyy;
  r1.xyz = func_configs[14].zzz * r2.xyz;
  r2.xyz = func_configs[14].www * r4.xyz;
  r0.xyz = r2.xyz * r0.xyz;
  r2.xyz = r1.xyz * r3.xyz + r0.xyz;
  r0.xyz = func_configs[15].xyw * r0.www + r1.www;
  r1.x = -func_configs[15].z + 1;
  r0.y = r0.y * r1.x + func_configs[15].z;
  r1.xy = -func_configs[16].xw + float2(1,1);
  r0.z = r0.z * r1.x + func_configs[16].x;
  r0.w = func_configs[16].y * r0.w + r1.w;
  r0.w = -func_configs[16].z * r0.w + 1;
  r0.w = r0.w * r1.y + func_configs[16].w;
  r0.xz = func_configs[17].xy * r0.xz;
  r0.z = r0.z * r0.w;
  r2.w = r0.x * r0.y + r0.z;
  if (bool_const[1].x != 0) {
    r0.xyzw = v1.wwww;
  } else {
    if (bool_const[1].y != 0) {
      r0.xyz = v2.www;
    } else {
      if (bool_const[1].z == 0) {
        r1.xyz = const_color.xyz + -r2.xyz;
        o0.xyz = const_color.www * r1.xyz + r2.xyz;
        o0.w = r2.w;
        return;
      }
      r0.xyz = v2.xyz;
    }
    r0.w = v2.w;
  }
  r1.xyz = float3(1,1,1) + -r0.xyz;
  r3.xyz = r2.xyz * r1.xyz + r0.xyz;
  r4.xyz = r2.xyz * r0.xyz;
  r5.xyz = r2.xyz * r0.xyz + r1.xyz;
  r3.xyz = bool_const[0].www ? r3.xyz : r5.xyz;
  r5.xyz = r0.xyz + r2.xyz;
  r5.xyz = float3(0.5,0.5,0.5) * r5.xyz;
  r1.xyz = r1.xyz * float3(0.5,0.5,0.5) + r4.xyz;
  r1.xyz = bool_const[0].www ? r5.xyz : r1.xyz;
  r0.xyzw = r2.xyzw * r0.xyzw;
  r4.w = r2.w;
  r5.xyzw = bool_const[2].wwww ? r4.xyzw : r0.xyzw;
  r1.w = r4.w;
  r4.xyzw = bool_const[2].zzzz ? r1.xyzw : r5.xyzw;
  r3.w = r1.w;
  r1.xyzw = bool_const[2].yyyy ? r3.xyzw : r4.xyzw;
  r0.xyz = r2.xyz;
  o0.xyzw = bool_const[2].xxxx ? r0.xyzw : r1.xyzw;

  o0 = max(o0, 0); o0.w = min(o0.w, 1);
  return;
}