// ---- Created with 3Dmigoto v1.3.16 on Tue Aug 04 16:25:31 2026

cbuffer _Globals : register(b0)
{
  float4 stage_color0[8] : packoffset(c0);
  float4 stage_color1[8] : packoffset(c8);
  float2 fog_config : packoffset(c16);
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
  float4 r0,r1,r2,r3;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = v3.xy / v3.ww;
  r0.xyzw = Texture0.Sample(TexS0_s, r0.xy).xyzw;
  r1.xyzw = Texture1.Sample(TexS1_s, v4.xy).xyzw;
  r2.xyzw = Texture2.Sample(TexS2_s, v5.xy).xyzw;
  r3.xyz = Texture3.Sample(TexS3_s, v6.xy).xyz;
  r0.xyzw = max(float4(0,0,0,0), r0.xyzw);
  r1.xyzw = max(float4(0,0,0,0), r1.xyzw);
  r2.xyzw = max(float4(0,0,0,0), r2.xyzw);
  r1.xyzw = r1.xyzw * r0.xyzw;
  // r1.xyzw = min(float4(1,1,1,1), r1.xyzw);
  r0.xyzw = r2.xyzw * r0.xyzw;
  // r0.xyzw = min(float4(1,1,1,1), r0.xyzw);
  r0.xyzw = r1.xyzw + r0.xyzw;
  // r0.xyzw = min(float4(1,1,1,1), r0.xyzw);
  r1.xyz = max(float3(0,0,0), r3.xyz);
  r1.xyz = r1.xyz * r0.xyz;
  r0.xyz = min(float3(1,1,1), r1.xyz);
  r1.x = cmp(0.5 < fog_config.x);
  r1.y = cmp(fog_config.y < 0.5);
  r1.y = r1.y ? r1.x : 0;
  if (r1.y != 0) {
    r1.y = max(0, v1.w);
    r1.y = r1.y * r0.w;
    o0.w = min(1, r1.y);
    o0.xyz = r0.xyz;
  } else {
    r1.y = cmp(0.5 < fog_config.y);
    r1.x = r1.y ? r1.x : 0;
    if (r1.x != 0) {
      r1.xyzw = max(stage_color0[2].wxyz, float4(0,0,0,0));
      r2.x = min(1, r1.x);
      r2.x = 1 + -r2.x;
      r1.xyz = r1.xxx * r1.yzw;
      r1.xyz = r2.xxx * r0.xyz + r1.xyz;
      r0.xyz = min(float3(1,1,1), r1.xyz);
    }
    o0.xyzw = r0.xyzw;
  }
  o0.xyz = max(0, o0.xyz);
  o0.w = saturate(o0.w);
  return;
}