Texture2D<float4> t0 : register(t0);

SamplerState s0_s : register(s0);

void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : COLOR0,
  float2 v2 : TEXCOORD0,
  float4 v3 : TEXCOORD1,
  out float4 o0 : SV_TARGET0)
{
  float4 r0,r1;
  r0.xyzw = t0.Sample(s0_s, v2.xy).xyzw;
  r0.w = min(1.0, r0.w);
#if 0 // Luma: disable SDR clamping
  r0.xyz = min(1.0, r0.xyz);
#endif
  r0.xyzw = max(v3.xyzw, r0.xyzw);
  r0.xyzw = v1.xyzw * r0.xyzw;
  // Discard if the alpha is too low
  if (r0.w <= (1.0 / 255.0)) discard;
  o0.xyzw = r0.xyzw;
}