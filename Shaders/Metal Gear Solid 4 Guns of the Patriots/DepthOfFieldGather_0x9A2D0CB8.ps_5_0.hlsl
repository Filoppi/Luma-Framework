Texture2D<float4> t0 : register(t0);

SamplerState s0_s : register(s0);

cbuffer cb0 : register(b0)
{
  float4 cb0[10];
}

#define cmp

void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : COLOR0,
  float2 v2 : TEXCOORD0,
  float4 v3 : TEXCOORD1,
  out float4 o0 : SV_TARGET0)
{
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8;
  r0.xy = cb0[8].zw * cb0[5].xy;
  r0.zw = max(float2(0,0), v2.xy);
  r0.zw = min(r0.zw, r0.xy);
  r1.xyzw = t0.Sample(s0_s, r0.zw).xyzw;
#if 0 // Luma: disabled clamping to SDR
  r1.xyz = saturate(r1.xyz);
#endif
  r2.x = cmp(abs(r1.w) < cb0[2].w);
  if (r2.x != 0) {
    o0.xyz = r1.xyz;
    o0.w = 0;
  } else {
    r2.x = 1 + -cb0[3].z;
    r2.y = cb0[3].z + cb0[3].z;
    r2.z = cmp(1 < cb0[3].y);
    r2.w = 0.159154952 * cb0[3].y;
    r3.xy = float2(0.000781250012,0.00138888892) * cb0[9].xx;
    r3.z = cb0[2].z * abs(r1.w);
    r3.w = cmp(cb0[3].y < 0);
    r4.xyz = r1.xyz;
    r4.w = 0;
    r5.xy = float2(1,0);
    r5.z = cb0[3].x;
    while (true) {
      r5.w = cmp(r5.z >= cb0[2].x);
      if (r5.w != 0) break;
      r5.w = r4.w * r2.w + cb0[3].w;
      r5.w = frac(r5.w);
      r5.w = -0.5 + r5.w;
      r5.w = abs(r5.w) * r2.y + r2.x;
      r5.w = r2.z ? r5.w : 1;
      sincos(r4.w, r6.x, r7.x);
      r7.y = r6.x;
      r6.xy = r7.xy * r3.xy;
      r5.w = r5.z * r5.w;
      r6.xy = r6.xy * r5.ww + r0.zw;
      r6.xy = max(float2(0,0), r6.xy);
      r6.xy = min(r6.xy, r0.xy);
      r6.xyzw = t0.SampleLevel(s0_s, r6.xy, 0).xyzw;
#if 0 // Luma: disabled clamping to SDR
      r6.xyz = saturate(r6.xyz);
#endif
      r5.w = cmp(r1.w < r6.w);
      r7.x = min(abs(r6.w), r3.z);
      r5.w = r5.w ? r7.x : abs(r6.w);
      r7.xyz = r6.xyz + r4.xyz;
      r6.w = -cb0[3].y + r5.z;
      r7.w = cb0[3].y + r5.z;
      r7.w = r7.w + -r6.w;
      r6.w = -r6.w + r5.w;
      r7.w = 1 / r7.w;
      r6.w = saturate(r7.w * r6.w);
      r7.w = r6.w * -2 + 3;
      r6.w = r6.w * r6.w;
      r6.w = r7.w * r6.w;
      r8.xyz = r4.xyz / r5.xxx;
      r6.xyz = -r8.xyz + r6.xyz;
      r6.xyz = r6.www * r6.xyz + r8.xyz;
      r6.xyz = r6.xyz + r4.xyz;
      r4.xyz = r3.www ? r7.xyz : r6.xyz;
      r5.y = r5.y + r5.w;
      r5.x = 1 + r5.x;
      r4.w = 2.39996314 + r4.w;
      r5.w = cb0[3].x / r5.z;
      r5.z = r5.z + r5.w;
    }
    r0.x = 1 / r5.x;
    o0.xyz = r4.xyz * r0.xxx;
    r0.x = -1 + r5.x;
    o0.w = saturate(r5.y / r0.x);
  }
}