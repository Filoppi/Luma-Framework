Texture2D<float4> t0 : register(t0);

SamplerState s0_s : register(s0);

cbuffer cb0 : register(b0)
{
  float4 cb0[2];
}

// Draws a sprite on screen. It might stretch in ultrawide but we can't do much about that.
// Used for screen space flashbang effects.
void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD7,
  out float4 o0 : SV_TARGET0)
{
  float4 r0,r1;
  r0.xyzw = t0.Sample(s0_s, v1.xy).xyzw;
  r0.xyzw = v2.xyzw * r0.xyzw;
  r1.x = cb0[1].x;
  r1.w = 1;
  o0.xyzw = r1.xxxw * r0.xyzw;
}