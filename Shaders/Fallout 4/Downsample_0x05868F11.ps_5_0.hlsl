// This is always used for 4x downscale?

Texture2D<float4> t0 : register(t0);
SamplerState s0_s : register(s0);

cbuffer cb2 : register(b2)
{
  float4 cb2[13];
}

// 3Dmigoto declarations
#define cmp -

#if 0 // Original shader
void main(
  float4 v0 : SV_POSITION0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xyzw = float4(0,0,0,0);
  while (true) {
    r1.x = cmp((int)r0.w >= 4);
    if (r1.x != 0) break;
    r1.xy = cb2[r0.w+8].xy * cb2[7].xy + v1.xy;
    r1.xy = cb2[4].xy * r1.xy;
    r1.xyz = t0.Sample(s0_s, r1.xy).xyz;

    // Incorrectly decompiled.
    //r2.xyz = (int3)r1.xyz & int3(0x7f800000,0x7f800000,0x7f800000);
    //r2.xyz = cmp((int3)r2.xyz == int3(0x7f800000,0x7f800000,0x7f800000));
    //r1.xyz = r2.xyz ? float3(0,0,0) : r1.xyz;

    // This should match the original shader.
    r1.xyz = isfinite(r1.xyz) ? r1.xyz : 0;

    r0.xyz = r1.xyz * cb2[r0.w+8].zzz + r0.xyz;
    r0.w = (int)r0.w + 1;
  }
  o0.xyz = r0.xyz;
  o0.w = cb2[7].z;
  return;
}
#else // 4x box downsample in 16 bilinear texture fetches.
void main(float4 v0 : SV_POSITION0, float2 v1 : TEXCOORD0, out float4 o0 : SV_Target0)
{
  float x, y;
  t0.GetDimensions(x, y);
  float2 t0_inv_dims = 1.0 / float2(x, y);

  float3 color;
  float3 csum = 0.0;

  color = t0.Sample(s0_s, v1 + float2(-3.0, -3.0) * t0_inv_dims).xyz;
  color = isfinite(color) ? color : 0;
  csum += color;
  color = t0.Sample(s0_s, v1 + float2(-3.0, -1.0) * t0_inv_dims).xyz;
  color = isfinite(color) ? color : 0;
  csum += color;
  color = t0.Sample(s0_s, v1 + float2(-3.0, 1.0) * t0_inv_dims).xyz;
  color = isfinite(color) ? color : 0;
  csum += color;
  color = t0.Sample(s0_s, v1 + float2(-3.0, 3.0) * t0_inv_dims).xyz;
  color = isfinite(color) ? color : 0;
  csum += color;
  color = t0.Sample(s0_s, v1 + float2(-1.0, -3.0) * t0_inv_dims).xyz;
  color = isfinite(color) ? color : 0;
  csum += color;
  color = t0.Sample(s0_s, v1 + float2(-1.0, -1.0) * t0_inv_dims).xyz;
  color = isfinite(color) ? color : 0;
  csum += color;
  color = t0.Sample(s0_s, v1 + float2(-1.0, 1.0) * t0_inv_dims).xyz;
  color = isfinite(color) ? color : 0;
  csum += color;
  color = t0.Sample(s0_s, v1 + float2(-1.0, 3.0) * t0_inv_dims).xyz;
  color = isfinite(color) ? color : 0;
  csum += color;
  color = t0.Sample(s0_s, v1 + float2(1.0, -3.0) * t0_inv_dims).xyz;
  color = isfinite(color) ? color : 0;
  csum += color;
  color = t0.Sample(s0_s, v1 + float2(1.0, -1.0) * t0_inv_dims).xyz;
  color = isfinite(color) ? color : 0;
  csum += color;
  color = t0.Sample(s0_s, v1 + float2(1.0, 1.0) * t0_inv_dims).xyz;
  color = isfinite(color) ? color : 0;
  csum += color;
  color = t0.Sample(s0_s, v1 + float2(1.0, 3.0) * t0_inv_dims).xyz;
  color = isfinite(color) ? color : 0;
  csum += color;
  color = t0.Sample(s0_s, v1 + float2(3.0, -3.0) * t0_inv_dims).xyz;
  color = isfinite(color) ? color : 0;
  csum += color;
  color = t0.Sample(s0_s, v1 + float2(3.0, -1.0) * t0_inv_dims).xyz;
  color = isfinite(color) ? color : 0;
  csum += color;
  color = t0.Sample(s0_s, v1 + float2(3.0, 1.0) * t0_inv_dims).xyz;
  color = isfinite(color) ? color : 0;
  csum += color;
  color = t0.Sample(s0_s, v1 + float2(3.0, 3.0) * t0_inv_dims).xyz;
  color = isfinite(color) ? color : 0;
  csum += color;

  o0.xyz = csum / 16.0;
  o0.w = cb2[7].z;
}
#endif