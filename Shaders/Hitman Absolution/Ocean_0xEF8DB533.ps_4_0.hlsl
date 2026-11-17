// Luma: HLSL translation of the ocean surface pixel shader 0xEF8DB533 (ps_4_0).
// Vanilla math is kept exactly under TONEMAP_TYPE 0. With TONEMAP_TYPE >= 1 the color output is
// sanitized (NaN/Inf to 0, negatives to 0) and soft capped above 16 (asymptote 32) to stop sun glints from exploding.

#include "Includes/Common.hlsl"

cbuffer Globals_cb4 : register(b4)
{
  float4 cb4[33];
};

cbuffer SharedPerViewData_cb1 : register(b1)
{
  float4 cb1[21];
};

Texture2D<float4> t0 : register(t0); // mapSpecular_01
Texture2D<float4> t1 : register(t1); // mapTextureNormal_04
Texture2D<float4> t2 : register(t2); // mapTextureNormal_02
Texture2D<float4> t3 : register(t3); // mapTexture_04
Texture2D<float4> t4 : register(t4); // mapTextureNormal_01
TextureCube<float4> t5 : register(t5); // mapTextureCube_01

SamplerState s0 : register(s0); // samplerSpecular_01
SamplerState s1 : register(s1); // samplerTextureNormal_04
SamplerState s2 : register(s2); // samplerTextureNormal_02
SamplerState s3 : register(s3); // samplerTexture_04
SamplerState s4 : register(s4); // samplerTextureNormal_01
SamplerState s5 : register(s5); // samplerTextureCube_01

// Vanilla: mul; ge(x, -x); frc(|x|); movc(mask, frc(|x|), -frc(|x|)). Fractional part keeping the sign of x.
float FracSigned(float x)
{
  return (x >= -x) ? frac(abs(x)) : -frac(abs(x));
}

void main(
  float4 v0 : SV_POSITION,
  float3 v1 : TEXCOORD0,
  float3 v2 : TEXCOORD1,
  float3 v3 : TEXCOORD2,
  float3 v4 : TEXCOORD3,
  float2 v5 : TEXCOORD4,
  float4 v6 : TEXCOORD5,
  out float4 o0 : SV_TARGET0)
{
  float4 r0 = 0, r1 = 0, r2 = 0, r3 = 0;

  r0.x = FracSigned(cb4[8].x * cb1[20].x);
  r1.xy = v5.xy;
  r1.z = 1.0;
  r0.y = dot(cb4[6].xyz, r1.xyz);
  r0.x = r0.y + r0.x;
  r0.z = FracSigned(cb4[9].x * cb1[20].x);
  r0.w = dot(cb4[7].xyz, r1.xyz);
  r0.y = r0.w + r0.z;
  r0 = t1.Sample(s1, r0.xy);
  r0.xy = r0.xy - 0.5;
  r0.xy = saturate(r0.xy * cb4[5].y + 0.5);
  r0.xy = r0.xy - 0.501961;
  r0.xy = v6.w * r0.xy + 0.501961;
  r0.z = FracSigned(cb4[3].x * cb1[20].x);
  r0.w = dot(cb4[1].xyz, r1.xyz);
  r2.x = r0.w + r0.z;
  r0.z = FracSigned(cb4[4].x * cb1[20].x);
  r0.w = dot(cb4[2].xyz, r1.xyz);
  r2.y = r0.w + r0.z;
  r2 = t0.Sample(s0, r2.xy);
  r0.z = log(r2.x);
  r0.w = log(r2.y);
  r2.xyz = log(r2.xyz);
  r2.xyz = r2.xyz * cb4[32].x;
  r2.xyz = exp(r2.xyz);
  r0.zw = r0.zw * cb4[5].x;
  r0.zw = exp(r0.zw);
  r0.xy = r0.zw * r0.xy;
  r0.zw = -r0.xy * 2.0 + 1.0;
  r1.w = FracSigned(cb4[11].x * cb1[20].x);
  r2.w = dot(cb4[12].xyz, r1.xyz);
  r3.x = r1.w + r2.w;
  r1.w = FracSigned(cb4[14].x * cb1[20].x);
  r2.w = dot(cb4[13].xyz, r1.xyz);
  r3.y = r1.w + r2.w;
  r3 = t2.Sample(s2, r3.xy);
  r3.xy = r3.xy - 0.5;
  r3.xy = saturate(r3.xy * cb4[10].x + 0.5);
  r3.xy = r3.xy - 0.5;
  r1.w = dot(v1, v1);
  r1.w = sqrt(r1.w);
  r1.w = r1.w - 20.0;
  r1.w = saturate(-r1.w * 0.001 + 1.0);
  r3.zw = r3.xy * r1.w;
  r0.xy = r0.zw * abs(r3.zw) + r0.xy;
  r0.xy = r1.w * r3.xy + r0.xy;
  r0.zw = -r0.xy * 2.0 + 1.0;
  r1.w = FracSigned(cb4[16].x * cb1[20].x);
  r2.w = dot(cb4[17].xyz, r1.xyz);
  r1.x = dot(cb4[18].xyz, r1.xyz);
  r3.x = r1.w + r2.w;
  r1.y = FracSigned(cb4[19].x * cb1[20].x);
  r3.y = r1.x + r1.y;
  r3.z = 1.0;
  r1.x = dot(cb4[21].xyz, r3.xyz);
  r1.y = dot(cb4[22].xyz, r3.xyz);
  r3 = t3.Sample(s3, r3.xy);
  r1.z = log(r3.x);
  r1.z = r1.z * cb4[15].x;
  r1.z = exp(r1.z);
  r3 = t4.Sample(s4, r1.xy);
  r1.xy = r3.xy - 0.5;
  r1.xy = saturate(r1.xy * cb4[20].x + 0.5);
  r1.xy = r1.xy - 0.5;
  r3.xy = r0.zw * abs(r1.xy) + r0.xy;
  r1.xy = r1.xy + r3.xy;
  r1.xy = r1.xy - 0.501961;
  r1.w = r1.z * v6.x;
  r1.z = -r1.z * v6.x + 1.0;
  r1.z = v6.y * r1.z + r1.w;
  r1.xy = r1.z * r1.xy + 0.001961;
  r0.xy = r0.zw * abs(r1.xy) + r0.xy;
  r0.xy = r1.xy + r0.xy;
  r0.xy = r0.xy - 0.5;
  r0.xy = r0.xy + r0.xy;
  r1.xyz = r0.y * v4;
  r1.xyz = v3 * r0.x + r1.xyz;
  r0.x = dot(r0.xy, r0.xy);
  r0.x = min(r0.x, 1.0);
  r0.x = 1.0 - r0.x;
  r0.x = sqrt(r0.x);
  r0.xyz = v2 * r0.x + r1.xyz;
  r0.w = dot(v1, r0.xyz);
  r0.w = r0.w + r0.w;
  r0.xyz = r0.xyz * -r0.w + v1;
  r1.xyz = r0.y * cb1[9].xzy;
  {
    float oldX = r0.x;
    r0.x = oldX * cb1[8].x + r1.x;
    r0.y = oldX * cb1[8].z + r1.y;
    r0.w = oldX * cb1[8].y + r1.z;
  }
  {
    float oldX = r0.x, oldY = r0.y, oldZ = r0.z, oldW = r0.w;
    r0.x = oldZ * cb1[10].x + oldX;
    r0.y = oldZ * cb1[10].z + oldY;
    r0.z = oldZ * cb1[10].y + oldW;
  }
  r0 = t5.Sample(s5, r0.xyz);
  r0.w = r0.w * cb4[31].x;
  r0.xyz = r0.xyz + cb4[30].xyz;
  r0.xyz = r0.xyz * cb4[29].xyz;
  r0.w = r0.w * 16.0;
  r0.w = exp(r0.w);
  r0.xyz = r0.w * r0.xyz;
  r0.xyz = r0.xyz * r2.xyz;
  r0.w = dot(-v1, -v1);
  r0.w = rsqrt(r0.w);
  r1.xyz = r0.w * -v1;
  r0.w = dot(v2, v2);
  r0.w = rsqrt(r0.w);
  r2.xyz = r0.w * v2;
  r0.w = saturate(dot(r1.xyz, r2.xyz));
  r0.w = 1.0 - r0.w;
  r0.w = log(r0.w);
  r0.w = r0.w * cb4[28].x;
  r0.w = exp(r0.w);
  r1.x = cb4[28].z - cb4[28].y;
  r0.w = r0.w * r1.x + cb4[28].y;
  r0.xyz = r0.xyz * r0.w;
  o0.xyz = r0.xyz * cb4[27].xyz;
  o0.w = 1.0;

#if TONEMAP_TYPE >= 1
  // Replace NaN/Inf with 0, then remove negatives.
  o0.rgb = isfinite(o0.rgb) ? o0.rgb : 0.0;
  o0.rgb = max(o0.rgb, 0.0);

  // Soft cap above 16 (asymptote 32). Values below 16 are unchanged.
  float m = max(o0.r, max(o0.g, o0.b));
  const float knee = 4.0; // asymptote 8: with ~2x display headroom (peak/paper), higher glints all flatten into the DICE shoulder and look clipped
  if (m > knee)
  {
    float e = m - knee;
    o0.rgb *= (knee + e / (1.0 + e / knee)) / m;
  }
#endif
}
