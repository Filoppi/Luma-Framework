// Luma: faithful HLSL translation of Hitman Absolution deferred light pixel shaders (ps_4_1, MSAA, sample frequency).
// Shared: G-buffer normal, view ray and depth reconstruction, distance attenuation, diffuse + specular LUT term.
// 0x1D640078 (_1D640078): projected light. Attenuation reads cb2[0], G-buffer t2..t5, adds the light projector map (t1, s1) and its projection (cb2[10..14]); output = diffuse/spec * projected light.
// 0x37DCABB2 (_37DCABB2): plain light, no projector. Attenuation reads cb2[1], G-buffer t1..t4, no t1 projector; output = light color * diffuse/spec.
// Under TONEMAP_TYPE >= 1 the MSAA G-buffer reads are saturated right after Load (mapDeferred1/2/3); mapDeferred0 (depth) is not.
// rsq inputs are floored at 1e-6; w/depth divides use a sign-preserving guard so they cannot hit zero.
// TONEMAP_TYPE 0 keeps the exact vanilla math.
// Uses SV_SampleIndex: each sample is loaded at its own index.

#include "Includes/Common.hlsl"
#include "Includes/DeferredLightSpecular.hlsl"
#include "Includes/DeferredLightCommon.hlsl"


cbuffer SharedPerViewData_cb1 : register(b1)
{
  float4 cb1[13];
};

cbuffer SharedLightData_cb2 : register(b2)
{
  float4 cb2[20];
};

#if _1D640078
Texture2D<float4> mapLightModel : register(t0);
Texture2D<float4> mapLightProjectorMap0 : register(t1);
Texture2DMS<float4, 4> mapDeferred0 : register(t2);
Texture2DMS<float4, 4> mapDeferred1 : register(t3);
Texture2DMS<float4, 4> mapDeferred2 : register(t4);
Texture2DMS<float4, 4> mapDeferred3 : register(t5);

SamplerState samplerLightModel : register(s0);
SamplerState samplerLightProjectorMap0 : register(s1);
#else
Texture2D<float4> mapLightModel : register(t0);
Texture2DMS<float4, 4> mapDeferred0 : register(t1);
Texture2DMS<float4, 4> mapDeferred1 : register(t2);
Texture2DMS<float4, 4> mapDeferred2 : register(t3);
Texture2DMS<float4, 4> mapDeferred3 : register(t4);

SamplerState samplerLightModel : register(s0);
#endif

void main(
  float4 v0 : SV_POSITION,
  float4 v1 : TEXCOORD0,
  float3 v2 : TEXCOORD1,
  uint sampleIndex : SV_SampleIndex,
  out float4 o0 : SV_TARGET0)
{
  int2 pixel = int2(v0.xy);
  int sampleI = int(sampleIndex);

  // Normal from mapDeferred1, remapped from [0,1] to [-1,1] and normalized.
  float4 normalTex = GBUFFER_CLAMP(mapDeferred1.Load(pixel, sampleI));
  float3 n = normalTex.xyz + float3(-0.5, -0.5, -0.5);
  n = n + n;
  n = n * rsqrt(SAFE_MIN(dot(n, n)));

  // View ray: v2 / v1.w.
  float invW = 1.0 / SAFE_W(v1.w);
  float3 rayView = v2 * invW;

  // Linear view depth from mapDeferred0 .x.
  float depth = mapDeferred0.Load(pixel, sampleI).x;
  float viewZ = 1.0 / SAFE_W(cb1[12].z * depth - cb1[12].y);

  // Surface position P = rayView * viewZ. Light vector to the light position.
  float3 P = rayView * viewZ;
  float3 toLight = cb2[8].xyz - P;
  float distSq = dot(toLight, toLight);
  float invLen = rsqrt(SAFE_MIN(distSq));
  float dist = sqrt(distSq);

#if _1D640078
  float2 ra = saturate(cb2[0].yw * dist + cb2[0].xz);
#else
  float2 ra = saturate(cb2[1].yw * dist + cb2[1].xz);
#endif
  float atten = ra.y * ra.x;
  float3 L = toLight * invLen;

  float ndotl = dot(n, L);
  float twoNdotl = ndotl + ndotl;
  float3 R = -twoNdotl * n + L;

  // View direction V = -P normalized.
  float invPLen = rsqrt(SAFE_MIN(dot(-P, -P)));
  float3 V = invPLen * -P;
  float specTerm = saturate(dot(V, -R));

  // mapDeferred2: .xyz is color, .w feeds the LUT coordinate.
  float4 deferred2 = GBUFFER_CLAMP(mapDeferred2.Load(pixel, sampleI));
  float lutV = dot(float2(specTerm, deferred2.w), float2(0.248047, 0.753906));

  float4 lut = mapLightModel.Sample(samplerLightModel, float2(saturate(ndotl), lutV));
  float e = lut.w * 1024.0 - 1000.0;

  // mapDeferred3: .x is specular scale, .y is specular exponent factor.
  float4 deferred3 = GBUFFER_CLAMP(mapDeferred3.Load(pixel, sampleI));
  float specPow = HMADeferredSpecularPower(e * deferred3.y);
  float specScalar = (deferred3.x * specPow) * cb2[19].x;

  float3 diffuseSpec = lut.xyz * deferred2.xyz + specScalar;

#if _1D640078
  // Light projector: position through the light projection rows cb2[11..14].
  float4 projPos = P.y * cb2[12];
  projPos = P.x * cb2[11] + projPos;
  projPos = P.z * cb2[13] + projPos;
  projPos = projPos + cb2[14];

  float inFront = (0.0 < projPos.z) ? 1.0 : 0.0;

  float2 projUV = saturate(projPos.xy / SAFE_W(projPos.w));
  projUV = projUV * cb2[10].xy + cb2[10].zw;
  float3 projCol = mapLightProjectorMap0.SampleLevel(samplerLightProjectorMap0, projUV, -2.0).xyz;

  float3 lightCol = inFront * cb2[3].xyz;
  lightCol = atten * lightCol;
  float3 projected = projCol * lightCol;

  o0.xyz = diffuseSpec * projected;
#else
  float3 lightCol = atten * cb2[3].xyz;
  o0.xyz = lightCol * diffuseSpec;
#endif
  o0.w = 0.0;
}
