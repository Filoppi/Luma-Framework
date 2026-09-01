// Borderlands 2 / The Pre-Sequel — Scaleform emissive HUD sprite (e.g. the autosave icon).
// o0 = min(color, 4) * vertexColor.w + vertexColor.xyz can exceed 1.0; vanilla's 8-bit UNORM backbuffer clamped that
// for free, Luma's fp16 LDR does not, and since it draws POST-tonemap the overshoot bypasses DICE and the paper-white
// scale blows the icon to ~10000 nits. Fix: re-add the saturate. Body verbatim from the dgVoodoo ps_5_0 disasm; the
// cb3 and/or pairs are dgVoodoo's texture-format bit emulation (mask+set), kept exactly via asint.

Texture2D<float4> t0 : register(t0);
Texture2D<float4> t1 : register(t1);
Texture2D<float4> t2 : register(t2);
Texture2D<float4> t3 : register(t3);

SamplerState s0_s : register(s0);
SamplerState s1_s : register(s1);
SamplerState s2_s : register(s2);
SamplerState s3_s : register(s3);

cbuffer cb3 : register(b3)
{
   float4 cb3[77];
}
cbuffer cb4 : register(b4)
{
   float4 cb4[236];
}

#define cmp -

void main(
    float4 v0 : SV_POSITION0,
    float4 v1 : TEXCOORD8,
    float4 v2 : COLOR0,
    float4 v3 : COLOR1,
    float4 v4 : TEXCOORD9,
    float4 v5 : TEXCOORD0,
    float4 v6 : TEXCOORD1,
    float4 v7 : TEXCOORD2,
    float4 v8 : TEXCOORD3,
    float4 v9 : TEXCOORD4,
    float4 v10 : TEXCOORD5,
    float4 v11 : TEXCOORD6,
    float4 v12 : TEXCOORD7,
    out float4 o0 : SV_TARGET0)
{
   float4 r0, r1, r2, r3;

   r0.xy = v5.xy + cb4[18].xy;
   r0.xyzw = t3.Sample(s3_s, r0.xy).xyzw;
   r0.xyzw = asfloat((asuint(r0.xyzw) & asuint(cb3[50].xyzw)) | asuint(cb3[51].xyzw));
   r0.yz = v5.xy + cb4[17].xy;
   r1.xyzw = t3.Sample(s3_s, r0.yz).xyzw;
   r1.xyzw = asfloat((asuint(r1.xyzw) & asuint(cb3[50].xyzw)) | asuint(cb3[51].xyzw));
   r2.xyzw = t2.Sample(s2_s, r0.yz).xyzw;
   r2.xyzw = asfloat((asuint(r2.xyzw) & asuint(cb3[48].xyzw)) | asuint(cb3[49].xyzw));
   r0.yzw = float3(1, 1, 1) + -r2.xyz;
   r0.x = r1.x * r0.x;
   r0.x = 10 * r0.x;
   r3.x = log2(abs(r0.x));
   r3.x = cb4[19].z * r3.x;
   r3.y = cmp(r3.x != r3.x);
   r3.x = r3.y ? 0 : r3.x;
   r1.x = exp2(r3.x);
   r0.x = -9.99999997e-007 + abs(r0.x);
   r2.xyzw = t0.Sample(s0_s, v5.xy).xyzw;
   r2.xyzw = asfloat((asuint(r2.xyzw) & asuint(cb3[44].xyzw)) | asuint(cb3[45].xyzw));
   r1.yzw = float3(1, 1.39999998, 3) * r2.yzx;
   r1.x = r1.x * r1.w;
   r1.x = cb4[19].w * r1.x;
   r3.w = cmp(r0.x >= 0);
   o0.w = r3.w ? r1.x : 0;
   r2.xyzw = t1.Sample(s1_s, v5.xy).xyzw;
   r2.xyzw = asfloat((asuint(r2.xyzw) & asuint(cb3[46].xyzw)) | asuint(cb3[47].xyzw));
   r1.yz = r2.yz * r1.yz;
   r1.x = 0;
   r1.xyz = float3(6, 6, 6) * r1.xyz;
   r2.xyzw = t2.Sample(s2_s, v5.xy).xyzw;
   r2.xyzw = asfloat((asuint(r2.xyzw) & asuint(cb3[48].xyzw)) | asuint(cb3[49].xyzw));
   r0.x = 1 + -r2.y;
   r0.xyz = r0.xxx * r0.yzw;
   r2.xyz = r0.xyz * float3(0, 10, 12) + -r1.xyz;
   r0.xyz = r0.xyz * r2.xyz + r1.xyz;
   r0.xyz = cb4[16].xyz + r0.xyz;
   r1.xyz = min(float3(4, 4, 4), r0.xyz);
   o0.xyz = r1.xyz * v12.www + v12.xyz;

   // Re-add the 8-bit clamp vanilla got from its UNORM backbuffer (see header).
   o0 = saturate(o0);
   return;
}
