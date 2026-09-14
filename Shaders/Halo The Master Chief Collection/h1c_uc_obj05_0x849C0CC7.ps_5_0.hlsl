// ---- Created with 3Dmigoto v1.3.16 on Tue Aug 04 16:25:32 2026

cbuffer _Globals : register(b0)
{
  float4 stage_color0[8] : packoffset(c0);
  float4 stage_color1[8] : packoffset(c8);
  float2 fog_config : packoffset(c16);
}

SamplerState TexS0_s : register(s0);
Texture2D<float4> Texture0 : register(t0);


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
  float4 r0,r1;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = v3.xy / v3.ww;
  r0.xyzw = Texture0.Sample(TexS0_s, r0.xy).xyzw;
  r0.xyz = saturate(r0.xyz);
  o0.w = saturate(r0.w);

  r0.w = cmp(0.5 < fog_config.x);
  r1.x = cmp(fog_config.y < 0.5);
  r1.x = r0.w ? r1.x : 0;
  if (r1.x != 0) {
    r1.x = max(0, v1.w);
    r1.xyz = r1.xxx * r0.xyz;
    o0.xyz = /* min(float3(1,1,1), */ r1.xyz/* ) */;
  } else {
    r1.x = cmp(0.5 < fog_config.y);
    r0.w = r0.w ? r1.x : 0;
    if (r0.w != 0) {
      r1.xyzw = max(stage_color0[1].wxyz, float4(0,0,0,0));
      r0.w = min(1, r1.x);
      r0.w = 1 + -r0.w;
      r1.xyz = r1.xxx * r1.yzw;
      r1.xyz = r0.www * r0.xyz + r1.xyz;
      o0.xyz = /* min(float3(1,1,1),  */r1.xyz/* ) */;
    } else {
      o0.xyz = r0.xyz;
    }
  }
  o0 = max(0, o0); o0.w = min(1, o0.w);
  return;
}