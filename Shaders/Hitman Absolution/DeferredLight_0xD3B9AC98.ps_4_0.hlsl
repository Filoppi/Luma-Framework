// Luma: faithful HLSL translation of Hitman Absolution deferred light pixel shader 0xD3B9AC98 (3 shadow maps + projector).
// Under TONEMAP_TYPE >= 1 the G-buffer reads (mapDeferred1/2/3) are clamped to [0,1] like the vanilla 8-bit UNORM targets,
// since Luma's fp16 G-buffer lets values > 1 leak in and produce negative light. TONEMAP_TYPE 0 keeps the vanilla math exact.

#include "Includes/Common.hlsl"

#if TONEMAP_TYPE >= 1
#define LUMA_GBUFFER_READ(x) saturate(x)
#else
#define LUMA_GBUFFER_READ(x) (x)
#endif

cbuffer SharedPerViewData_cb1 : register(b1)
{
  float4 cb1[13];
};

cbuffer SharedLightData_cb2 : register(b2)
{
  float4 cb2[20];
};

Texture2D<float4> mapLightModel : register(t0);
Texture2D<float4> mapLightProjectorMap0 : register(t1);
Texture2D<float4> mapShadowColor0BWhite : register(t2);
Texture2D<float4> mapShadowColor1BWhite : register(t3);
Texture2D<float4> mapShadowColor2BWhite : register(t4);
Texture2D<float4> mapDeferred0 : register(t5);
Texture2D<float4> mapDeferred1 : register(t6);
Texture2D<float4> mapDeferred2 : register(t7);
Texture2D<float4> mapDeferred3 : register(t8);

SamplerState samplerLightModel : register(s0);
SamplerState samplerLightProjectorMap0 : register(s1);
SamplerState samplerShadowColor0BWhite : register(s2);
SamplerState samplerShadowColor1BWhite : register(s3);
SamplerState samplerShadowColor2BWhite : register(s4);
SamplerState samplerDeferred0 : register(s5);
SamplerState samplerDeferred1 : register(s6);
SamplerState samplerDeferred2 : register(s7);
SamplerState samplerDeferred3 : register(s8);

void main(
  float4 v0 : SV_POSITION,
  float4 v1 : TEXCOORD0,
  float3 v2 : TEXCOORD1,
  out float4 o0 : SV_TARGET0)
{
  float invW = 1.0 / v1.w;
  float2 uv = v1.xy * invW;
  float3 P = v2.xyz * invW;

  // Depth (mapDeferred0 is R32 depth: not clamped) -> view depth d, view-space position.
  float depthRaw = mapDeferred0.Sample(samplerDeferred0, uv).x;
  float d = 1.0 / (cb1[12].z * depthRaw - cb1[12].y);
  float3 pos = P * d;

  // View -> light-space-ish coordinate q (rows of cb2[11..14]).
  float3 q = pos.y * cb2[12].xyz;
  q = pos.x * cb2[11].xyz + q;
  q = pos.z * cb2[13].xyz + q;
  q += cb2[14].xyz;

  // Shadow map 2 (t4), level 0.
  float3 sh2 = cb2[5].xyz * q + cb2[7].xyz;
  float4 sm2 = mapShadowColor2BWhite.SampleLevel(samplerShadowColor2BWhite, sh2.xy, 0.0);
  float var2 = saturate(-sm2.x * sm2.x + sm2.y);
  float dz2 = sm2.x - sh2.z;
  float3 k2 = saturate(cb2[2].x * abs(sh2 - 0.5) + cb2[2].y);
  float w2 = min(dot(k2, k2), 1.0);
  dz2 = dz2 * cb2[19].y;
  float rs2 = rsqrt(min(var2 + 0.0004, 1.0));
  float f2 = saturate(dz2 * rs2 + 1.0);
  float shadowA = w2 * (1.0 - f2) + f2;

  // Shadow map 1 (t3), level 0.
  float3 sh1 = cb2[4].xyz * q + cb2[6].xyz;
  float4 sm1 = mapShadowColor1BWhite.SampleLevel(samplerShadowColor1BWhite, sh1.xy, 0.0);
  float var1 = saturate(-sm1.x * sm1.x + sm1.y);
  float dz1 = sm1.x - sh1.z;
  float3 k1 = saturate(cb2[1].x * abs(sh1 - 0.5) + cb2[1].y);
  float w1 = min(dot(k1, k1), 1.0);
  dz1 = dz1 * cb2[19].y;
  float rs1 = rsqrt(min(var1 + 0.0004, 1.0));
  float f1 = saturate(dz1 * rs1 + 1.0);
  shadowA = w1 * (shadowA - f1) + f1;

  // Shadow map 0 (t2), sampled at q.xy (not sh).
  float4 sm0 = mapShadowColor0BWhite.SampleLevel(samplerShadowColor0BWhite, q.xy, 0.0);
  float dz0 = sm0.x - q.z;
  float var0 = saturate(-sm0.x * sm0.x + sm0.y);
  float rs0 = rsqrt(min(var0 + 0.0004, 1.0));
  float3 k0 = saturate(cb2[0].x * abs(q - 0.5) + cb2[0].y);
  float w0 = min(dot(k0, k0), 1.0);
  dz0 = dz0 * cb2[19].y;
  float f0 = saturate(dz0 * rs0 + 1.0);
  shadowA = w0 * (shadowA - f0) + f0;
  float shadowFinal = cb2[3].w * (1.0 - shadowA) + shadowA;

  // Light projector (2D, LOD -2). Rows use components x,y,w of cb2[15..18].
  float3 lp = pos.y * float3(cb2[16].x, cb2[16].y, cb2[16].w);
  lp = pos.x * float3(cb2[15].x, cb2[15].y, cb2[15].w) + lp;
  lp = pos.z * float3(cb2[17].x, cb2[17].y, cb2[17].w) + lp;
  lp += float3(cb2[18].x, cb2[18].y, cb2[18].w);
  float2 puv = lp.xy / lp.z;
  puv = puv * cb2[10].xy + cb2[10].zw;
  float4 proj = mapLightProjectorMap0.SampleLevel(samplerLightProjectorMap0, puv, -2.0);
  float3 projCol = proj.rgb * cb2[3].rgb;
  projCol = shadowFinal * projCol;

  // Normal from mapDeferred1 (G-buffer read, clamped under TONEMAP_TYPE >= 1).
  float4 gb1 = LUMA_GBUFFER_READ(mapDeferred1.Sample(samplerDeferred1, uv));
  float3 N = gb1.xyz - 0.5;
  N = N + N;
  N = N * rsqrt(dot(N, N));

  // View direction.
  float3 V = -pos * rsqrt(dot(pos, pos));

  // Reflected light direction R = cb2[9] - 2 (N.cb2[9]) N.
  float dN = dot(N, cb2[9].xyz);
  float3 R = -(2.0 * dN) * N + cb2[9].xyz;
  float spec = saturate(dot(V, -R));

  // mapDeferred2 / mapDeferred3 (G-buffer reads, clamped under TONEMAP_TYPE >= 1).
  float4 gb2 = LUMA_GBUFFER_READ(mapDeferred2.Sample(samplerDeferred2, uv));
  float4 gb3 = LUMA_GBUFFER_READ(mapDeferred3.Sample(samplerDeferred3, uv));

  // Light model LUT: x = saturate(N.L), y = packed spec / gb2.w.
  float lutY = spec * 0.248047 + gb2.w * 0.753906;
  float4 lut = mapLightModel.Sample(samplerLightModel, float2(saturate(dN), lutY));
  float specPow = exp2((lut.w * 1024.0 - 1000.0) * gb3.y);

  // Rim/back term.
  float lum = dot(gb2.xyz, float3(0.447058, 0.882354, 0.170587));
  float3 rim = (gb2.xyz * 2.0 - lum) * (saturate(-dN) * gb3.w * (1.0 - cb2[3].w));

  float3 specTerm = (gb3.x * specPow + rim) * cb2[19].x;
  float3 outColor = lut.xyz * gb2.xyz + specTerm;
  outColor = projCol * outColor;

  o0.xyz = outColor;
  o0.w = 0.0;
}
