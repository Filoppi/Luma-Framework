Texture2D<float4> t1 : register(t1);
Texture2D<float4> t0 : register(t0);

SamplerState s1_s : register(s1);
SamplerState s0_s : register(s0);

// TODO: actually not DoF exclusive
void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : COLOR0,
  float2 v2 : TEXCOORD0,
  out float4 o0 : SV_TARGET0)
{
  float4 r0,r1;
  r0.xyzw = t1.Sample(s1_s, v2.xy).xyzw;
  r0.xyz = r0.xyz / r0.www;
  r1.xyzw = t0.Sample(s0_s, v2.xy).xyzw;
  r0.xyz = r1.www * r0.xyz;
  r0.xyz = r0.xyz * float3(0.25,0.25,0.25) + r1.xyz;
  r0.w = max(r0.x, r0.y);
  r1.x = max(0.25, r0.z);
  r0.w = max(r1.x, r0.w);
  r0.w = 1 / r0.w;

  // Encode them like all the other direct rendering
  o0.xyz = r0.xyz * r0.www;
  o0.w = 0.25 * r0.w;
#if 0 // Disabled saturate as it's not helping
  o0.xyzw = saturate(o0.xyzw);
#endif
}