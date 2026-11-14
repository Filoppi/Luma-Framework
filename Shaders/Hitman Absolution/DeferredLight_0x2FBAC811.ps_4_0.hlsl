// Luma: faithful HLSL translation of Hitman Absolution deferred light pixel shader 0x2FBAC811 (ps_4_0, MSAA).
// Under TONEMAP_TYPE >= 1 the MSAA G-buffer reads mapDeferred1/2/3 (t2, t3, t4) are saturated to [0,1] right after Load,
// and the rsq/div inputs are floored at 1e-6 so zero vectors (e.g. a clamped gray normal) cannot produce inf/NaN.
// TONEMAP_TYPE 0 keeps the exact vanilla math.
// ps_4_0 has no SV_SampleIndex: vanilla always loads sample 0, so this does too.

#include "Includes/Common.hlsl"
#include "Includes/DeferredLightSpecular.hlsl"

#if TONEMAP_TYPE >= 1
#define GBUFFER_CLAMP(v) saturate(v)
#define SAFE_MIN(x) max((x), 1e-6)
#define SAFE_W(x) (((x) < 0.0) ? min((x), -1e-6) : max((x), 1e-6))
#else
#define GBUFFER_CLAMP(v) (v)
#define SAFE_MIN(x) (x)
#define SAFE_W(x) (x)
#endif

cbuffer Globals_cb4 : register(b4)
{
  float4 cb4[16];
};

cbuffer SharedPerViewData_cb1 : register(b1)
{
  float4 cb1[18];
};

cbuffer SharedLightData_cb2 : register(b2)
{
  float4 cb2[8];
};

Texture2D<float4> mapLightModel : register(t0);
Texture2DMS<float4, 4> mapDeferred0 : register(t1);
Texture2DMS<float4, 4> mapDeferred1 : register(t2);
Texture2DMS<float4, 4> mapDeferred2 : register(t3);
Texture2DMS<float4, 4> mapDeferred3 : register(t4);
Texture2D<float4> mapOcclusion : register(t5);

SamplerState samplerLightModel : register(s0);

void main(
  float4 v0 : SV_POSITION,
  float2 v1 : TEXCOORD0,
  float3 v2 : TEXCOORD1,
  out float4 o0 : SV_TARGET0)
{
  int2 pixel = int2(v0.xy);
  int sampleI = 0;

  // Ray direction (normalized). r0.xyz
  float3 viewDir = v2 * rsqrt(SAFE_MIN(dot(v2, v2)));
  bool negRay = (cb4[15].x != 0.0);

  // Reconstruct view position from depth (mapDeferred0 .x). r1
  float4 recon = v1.y * cb4[8];
  recon = v1.x * cb4[7] + recon;
  float4 depthTex = mapDeferred0.Load(pixel, sampleI);
  recon = depthTex.x * cb4[9] + recon;
  recon = recon + cb4[10];
  float3 pos = recon.xyz / SAFE_W(recon.w); // Sign-preserving floor, like the sibling variants: a negative w must not flip the position
  pos = pos * rsqrt(SAFE_MIN(dot(pos, pos)));
  viewDir = negRay ? -pos : viewDir;

  // Normal from mapDeferred1, remapped from [0,1] to [-1,1] and normalized.
  float4 normalTex = GBUFFER_CLAMP(mapDeferred1.Load(pixel, sampleI));
  float3 n = normalTex.xyz + float3(-0.5, -0.5, -0.5);
  n = n + n;
  n = n * rsqrt(SAFE_MIN(dot(n, n)));

  float ndotl = dot(n, cb2[7].xyz);
  float3 r3xyz = ndotl * n - cb2[6].xyz;
  float lightDot = dot(n, cb1[17].xyz);
  float3 r1xyz = cb2[2].xyz * lightDot + cb2[0].xyz;

  float4 r4 = 0.0;
  r4.x = saturate(ndotl * 0.5);
  r4.y = saturate(dot(viewDir, r3xyz));

  // mapDeferred2 (r0 in the dump): .xyz and .w are used later.
  float4 deferred2 = GBUFFER_CLAMP(mapDeferred2.Load(pixel, sampleI));
  r4.z = saturate(deferred2.w);
  r4.w = dot(r4.yz, float2(0.248047, 0.753906));

  float4 lut = mapLightModel.Sample(samplerLightModel, float2(r4.x, r4.w));
  float r1w = lut.w * 1024.0 - 1000.0;
  r1xyz = lut.xyz * cb2[4].xyz + r1xyz;

  // mapDeferred3: .x and .y used.
  float4 deferred3 = GBUFFER_CLAMP(mapDeferred3.Load(pixel, sampleI));

  // Half-resolution occlusion/extra buffer, squared. Coordinates are pixel * 0.5 (truncated), as in the dump.
  float4 occ = mapOcclusion.Load(int3(uint2(float2(pixel) * 0.5), 0));
  occ.xyz = occ.xyz * occ.xyz;
#if TONEMAP_TYPE >= 1
  occ.xyz = lerp(1.0, occ.xyz, LumaSettings.GameSettings.AODarkness);
#endif

  r1w = r1w * deferred3.y;
  r1w = HMADeferredSpecularPower(r1w);
  r1w = deferred3.x * r1w;
  float3 r3 = r1w * cb2[4].xyz;

  float3 lit = r1xyz * deferred2.xyz + r3;
  o0.w = saturate(deferred2.w - 0.66);
  o0.xyz = lit * occ.xyz;
}
