// ---- Created with 3Dmigoto v1.3.16 on Sun Aug 10 08:31:33 2025

SamplerState biLinearClamp_s : register(s0);
Texture2D<float4> codeTexture0 : register(t0);


// 3Dmigoto declarations
#define cmp -

void S(inout float4 color) {
  color = max(0, color);
}

void main(
  float2 v0 : TEXCOORD0,
  float4 v1 : SV_POSITION0,
  out float3 o0 : SV_TARGET0)
{
  float4 r0,r1,r2,r3;
  uint4 bitmask, uiDest;
  float4 fDest;

  // instr 0: sample_aoffimmi(2,0,0)
  r0.xyz = codeTexture0.Sample(biLinearClamp_s, v0.xy, int2(2, 0)).xyz; S(r0);
  r1.x = dot(r0.xyz, float3(0.212599993,0.715200007,0.0722000003)); //luma weighted
  r1.x = 1 + r1.x;
  r1.x = asfloat(0x7ef311c2 - asint(r1.x)); // iadd: fast rcp bit trick on float bits
  r0.w = 1;
  r0.xyzw = r1.xxxx * r0.xyzw;

  // instr 6: sample (no offset, center)
  r1.xyz = codeTexture0.Sample(biLinearClamp_s, v0.xy).xyz; S(r1);
  r2.x = dot(r1.xyz, float3(0.212599993,0.715200007,0.0722000003));
  r2.x = 1 + r2.x;
  r2.x = asfloat(0x7ef311c2 - asint(r2.x));
  r1.w = 1;
  r0.xyzw = r2.xxxx * r1.xyzw + r0.xyzw;

  // instr 12: sample_aoffimmi(-2,0,0)
  r1.xyz = codeTexture0.Sample(biLinearClamp_s, v0.xy, int2(-2, 0)).xyz; S(r1);
  r2.x = dot(r1.xyz, float3(0.212599993,0.715200007,0.0722000003));
  r2.x = 1 + r2.x;
  r2.x = asfloat(0x7ef311c2 - asint(r2.x));
  r1.w = 1;
  r0.xyzw = r2.xxxx * r1.xyzw + r0.xyzw;

  // instr 18: sample_aoffimmi(0,2,0)
  r1.xyz = codeTexture0.Sample(biLinearClamp_s, v0.xy, int2(0, 2)).xyz; S(r1);
  r2.x = dot(r1.xyz, float3(0.212599993,0.715200007,0.0722000003));
  r2.x = 1 + r2.x;
  r2.x = asfloat(0x7ef311c2 - asint(r2.x));
  r1.w = 1;
  r0.xyzw = r2.xxxx * r1.xyzw + r0.xyzw;

  // instr 24: sample_aoffimmi(0,-2,0)
  r1.xyz = codeTexture0.Sample(biLinearClamp_s, v0.xy, int2(0, -2)).xyz; S(r1);
  r2.x = dot(r1.xyz, float3(0.212599993,0.715200007,0.0722000003));
  r2.x = 1 + r2.x;
  r2.x = asfloat(0x7ef311c2 - asint(r2.x));
  r1.w = 1;
  r0.xyzw = r2.xxxx * r1.xyzw + r0.xyzw;
  r0.xyzw = float4(0.0714285746,0.0714285746,0.0714285746,0.0714285746) * r0.xyzw;

  // instr 31: sample_aoffimmi(-2,-2,0)
  r1.xyz = codeTexture0.Sample(biLinearClamp_s, v0.xy, int2(-2, -2)).xyz; S(r1);
  r2.x = dot(r1.xyz, float3(0.212599993,0.715200007,0.0722000003));
  r2.x = 1 + r2.x;
  r2.x = asfloat(0x7ef311c2 - asint(r2.x));
  r1.w = 1;
  r1.xyzw = r2.xxxx * r1.xyzw;

  // instr 37: sample_aoffimmi(2,-2,0)
  r2.xyz = codeTexture0.Sample(biLinearClamp_s, v0.xy, int2(2, -2)).xyz; S(r2);
  r3.x = dot(r2.xyz, float3(0.212599993,0.715200007,0.0722000003));
  r3.x = 1 + r3.x;
  r3.x = asfloat(0x7ef311c2 - asint(r3.x));
  r2.w = 1;
  r1.xyzw = r3.xxxx * r2.xyzw + r1.xyzw;

  // instr 43: sample_aoffimmi(-2,2,0)
  r2.xyz = codeTexture0.Sample(biLinearClamp_s, v0.xy, int2(-2, 2)).xyz; S(r2);
  r3.x = dot(r2.xyz, float3(0.212599993,0.715200007,0.0722000003));
  r3.x = 1 + r3.x;
  r3.x = asfloat(0x7ef311c2 - asint(r3.x));
  r2.w = 1;
  r1.xyzw = r3.xxxx * r2.xyzw + r1.xyzw;

  // instr 49: sample_aoffimmi(2,2,0)
  r2.xyz = codeTexture0.Sample(biLinearClamp_s, v0.xy, int2(2, 2)).xyz; S(r2);
  r3.x = dot(r2.xyz, float3(0.212599993,0.715200007,0.0722000003));
  r3.x = 1 + r3.x;
  r3.x = asfloat(0x7ef311c2 - asint(r3.x));
  r2.w = 1;
  r1.xyzw = r3.xxxx * r2.xyzw + r1.xyzw;
  r0.xyzw = r1.xyzw * float4(0.0357142873,0.0357142873,0.0357142873,0.0357142873) + r0.xyzw;

  // instr 56: sample_aoffimmi(-1,-1,0)
  r1.xyz = codeTexture0.Sample(biLinearClamp_s, v0.xy, int2(-1, -1)).xyz; S(r1);
  r2.x = dot(r1.xyz, float3(0.212599993,0.715200007,0.0722000003));
  r2.x = 1 + r2.x;
  r2.x = asfloat(0x7ef311c2 - asint(r2.x));
  r1.w = 1;
  r1.xyzw = r2.xxxx * r1.xyzw;

  // instr 62: sample_aoffimmi(1,-1,0)
  r2.xyz = codeTexture0.Sample(biLinearClamp_s, v0.xy, int2(1, -1)).xyz; S(r2);
  r3.x = dot(r2.xyz, float3(0.212599993,0.715200007,0.0722000003));
  r3.x = 1 + r3.x;
  r3.x = asfloat(0x7ef311c2 - asint(r3.x));
  r2.w = 1;
  r1.xyzw = r3.xxxx * r2.xyzw + r1.xyzw;

  // instr 68: sample_aoffimmi(-1,1,0)
  r2.xyz = codeTexture0.Sample(biLinearClamp_s, v0.xy, int2(-1, 1)).xyz; S(r2);
  r3.x = dot(r2.xyz, float3(0.212599993,0.715200007,0.0722000003));
  r3.x = 1 + r3.x;
  r3.x = asfloat(0x7ef311c2 - asint(r3.x));
  r2.w = 1;
  r1.xyzw = r3.xxxx * r2.xyzw + r1.xyzw;

  // instr 74: sample_aoffimmi(1,1,0)
  r2.xyz = codeTexture0.Sample(biLinearClamp_s, v0.xy, int2(1, 1)).xyz; S(r2);
  r3.x = dot(r2.xyz, float3(0.212599993,0.715200007,0.0722000003));
  r3.x = 1 + r3.x;
  r3.x = asfloat(0x7ef311c2 - asint(r3.x));
  r2.w = 1;
  r1.xyzw = r3.xxxx * r2.xyzw + r1.xyzw;
  
  r0.xyzw = r1.xyzw * float4(0.125,0.125,0.125,0.125) + r0.xyzw;
  r0.w = rcp(r0.w); 
  o0.xyz = r0.xyz * r0.www; 
  return;
}

