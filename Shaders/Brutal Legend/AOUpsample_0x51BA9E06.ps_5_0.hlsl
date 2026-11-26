#include "Includes/Common.hlsl"

// Translation of Dump/0x51BA9E06.ps_5_0.cso (bilateral AO upsample to full res).
// o0.x = ao, o0.y = ao*ao, o0.zw = packed linear depth (vanilla, always kept).
// With LumaSettings.GameSettings.XeGTAOActive > 0.5 the AO comes from the XeGTAO texture (t5) instead.

Texture2D<float4> t2 : register(t2); // Full res device depth (x), masked by cb3[48..49]
Texture2D<float4> t3 : register(t3); // Full res packed AO input (masked by cb3[50..51])
Texture2D<float4> t4 : register(t4); // Half res AO + packed depth (masked by cb3[52..53])
Texture2D<float> lumaGTAO : register(t5); // Luma XeGTAO full res visibility (R8_UNORM), used when XeGTAOActive

SamplerState s2_s : register(s2);
SamplerState s3_s : register(s3);
SamplerState s4_s : register(s4);

cbuffer cb4 : register(b4)
{
  float4 cb4[236];
}

cbuffer cb3 : register(b3)
{
  float4 cb3[77];
}

float4 ApplyMask(float4 v, float4 andMask, float4 orMask)
{
  return asfloat((asuint(v) & asuint(andMask)) | asuint(orMask));
}

// rcp with the DXBC "lt 0 |x| ? rcp(x) : huge" guard
float GuardedRcp(float x)
{
  return (0.0 < abs(x)) ? rcp(x) : 9999999933815812510711506376257961984.0;
}

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
  // Four half res taps around the pixel (bilinear neighbours in depth space)
  float4 a = ApplyMask(t4.Sample(s4_s, float2(v5.y, v5.z)), cb3[52], cb3[53]);
  float4 b = ApplyMask(t4.Sample(s4_s, float2(v5.x, v5.z)), cb3[52], cb3[53]);
  float dB = dot(b.zw, float2(512.0, 2.0));
  float dA = dot(a.zw, float2(512.0, 2.0));
  float rAB = GuardedRcp(dA - dB);

  // Centre full res depth (t2) and packed input (t3)
  float2 uvS = float2(v6.z, v5.z);
  float depthRaw = ApplyMask(t2.Sample(s2_s, uvS), cb3[48], cb3[49]).x;
  float Zc = GuardedRcp(depthRaw + cb4[16].z) * cb4[16].w; // linear view depth
  float4 S = ApplyMask(t3.Sample(s3_s, uvS), cb3[50], cb3[51]);

  float4 c = ApplyMask(t4.Sample(s4_s, float2(v6.z, v6.x)), cb3[52], cb3[53]);
  float dC = dot(c.zw, float2(512.0, 2.0));
  float4 d = ApplyMask(t4.Sample(s4_s, float2(v6.z, v6.y)), cb3[52], cb3[53]);
  float dD = dot(d.zw, float2(512.0, 2.0));
  float rCD = GuardedRcp(dD - dC);

  // Depth weighted interpolation along A-B and along C-D
  float wAB = saturate((Zc - dB) * rAB);
  float interpAB = wAB * (a.x - b.x) + b.x;
  float wCD = saturate((Zc - dC) * rCD);
  float interpCD = wCD * (d.x - c.x) + c.x;

  float aoBlend = 0.5 * (interpAB + interpCD) - S.x;
  float dS = dot(S.zw, float2(512.0, 2.0));
  float wS = saturate(max(1.0, 6.0 - 0.2 * Zc) * abs(Zc - dS));
  float aoVanilla = wS * aoBlend + S.x;

  // Vanilla packed linear depth (o0.zw), always kept
  float2 zp = float2(0.5 * Zc, 128.0 * Zc);
  float2 zpFloor = zp - frac(zp);
  float2 packedDepth = frac(zpFloor * (1.0 / 256.0));

  float ao = aoVanilla;
  if (LumaSettings.GameSettings.XeGTAOActive > 0.5)
  {
    ao = lumaGTAO.Load(int3(v0.xy, 0));
  }

  o0.x = ao;
  o0.y = ao * ao;
  o0.zw = packedDepth;
}
