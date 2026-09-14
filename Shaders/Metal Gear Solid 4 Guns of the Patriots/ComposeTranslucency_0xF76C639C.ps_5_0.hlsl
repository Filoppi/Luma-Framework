Texture2D<float4> sceneTexture : register(t0);
Texture2D<float4> translucencyTexture : register(t1);

SamplerState translucencySampler : register(s1);
SamplerState sceneSampler : register(s0);

void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : COLOR0,
  float2 v2 : TEXCOORD0,
  out float4 o0 : SV_TARGET0)
{
  float4 r0,r1;

  r0.xyzw = sceneTexture.Sample(sceneSampler, v2.xy).xyzw;
  r0.xyz = r0.xyz / r0.w;

  r1.xyzw = translucencyTexture.Sample(translucencySampler, v2.xy).xyzw;
  r1.xyzw = max(r1.xyzw, 0.0); // Luma: protect against negative values
  r0.xyz = r1.w * r0.xyz;
  r0.xyz = r0.xyz * float3(0.25,0.25,0.25) + r1.xyz;
  r0.w = max(r0.x, r0.y);
  r1.x = max(0.25, r0.z);
  r0.w = max(r1.x, r0.w);
  r0.w = 1 / r0.w;
  // Encode them like all the other direct rendering
  o0.xyz = r0.xyz * r0.w;
  o0.w = 0.25 * r0.w;
#if 0 // Disabled saturate as it's not helping
  o0.xyzw = saturate(o0.xyzw);
#endif
}