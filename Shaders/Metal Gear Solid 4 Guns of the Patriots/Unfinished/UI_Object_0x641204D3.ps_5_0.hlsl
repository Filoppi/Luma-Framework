Texture2D<uint4> t6 : register(t6);
Texture2D<float4> t4 : register(t4);
Texture2D<float4> t3 : register(t3);
Texture2D<float4> t2 : register(t2);
Texture2D<float4> t1 : register(t1);
Texture2D<float4> t0 : register(t0);

SamplerComparisonState s4_s : register(s4);

SamplerState s3_s : register(s3);
SamplerState s2_s : register(s2);
SamplerState s1_s : register(s1);
SamplerState s0_s : register(s0);

cbuffer cb0 : register(b0)
{
  float4 cb0[27];
}

#define cmp

// TODO
void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : COLOR0,
  float v2 : COLOR2,
  float2 w2 : TEXCOORD0,
  float4 v3 : TEXCOORD4,
  float4 v4 : TEXCOORD5,
  float4 v5 : TEXCOORD6,
  float4 v6 : TEXCOORD7,
  float4 v7 : TEXCOORD8,
  float4 v8 : TEXCOORD9,
  uint v9 : SV_IsFrontFace0,
  out float4 o0 : SV_TARGET0)
{
  float4 r0,r1,r2,r3,r4,r5,r6;
  uint4 bitmask;

  r0.xyzw = t0.Sample(s0_s, w2.xy).xyzw;
  r0.xyzw = saturate(r0.xyzw);
  bitmask.x = ((~(-1 << 1)) << 1) & 0xffffffff;  r1.x = (((uint)v9.x << 1) & bitmask.x) | ((uint)0 & ~bitmask.x);
  r1.x = (int)r1.x + -1;
  r1.x = (int)r1.x;
  r1.yz = float2(0.699999988,0.699999988) * cb0[25].xy;
  r2.xy = v7.xy / v7.ww;
  r2.xy = r2.xy * float2(0.5,-0.5) + float2(0.5,0.5);
  r1.x = v8.y * r1.x + v7.z;
  r2.z = saturate(r1.x / v7.w);
  r1.x = -1 + cb0[26].x;
  r1.w = 360 / r1.x;
  r2.w = t4.SampleCmpLevelZero(s4_s, r2.xy, r2.z).x;
  r3.z = 0;
  r3.w = r2.w;
  r4.x = 0;
  while (true) {
    r4.y = (int)r4.x;
    r4.z = cmp(r4.y >= r1.x);
    if (r4.z != 0) break;
    r4.y = r4.y * r1.w + 30;
    r4.y = 0.0174532924 * r4.y;
    sincos(r4.y, r5.x, r6.x);
    r3.x = r6.x * r1.y;
    r3.y = r5.x * r1.z;
    r4.yzw = r3.xyz + r2.xyz;
    r3.x = t4.SampleCmpLevelZero(s4_s, r4.yz, r4.w).x;
    r3.w = r3.w + r3.x;
    r4.x = (int)r4.x + 1;
  }
  r0.w = v1.w * r0.w;
  r1.x = cmp(0 < cb0[24].x);
  if (r1.x != 0) {
    r1.yz = floor(v0.xy);
    r1.yz = (uint2)r1.yz;
    r2.x = (int)r1.y & 31;
    r2.y = (int)cb0[24].y;
    r2.zw = float2(0,0);
    r1.y = t6.Load(r2.xyz).x;
    r1.z = 1 << (int)r1.z;
    r1.y = (int)r1.z & (int)r1.y;
    r1.y = min(1, (uint)r1.y);
    r1.y = (int)-r1.y + 1;
  }
  r1.x = r1.x ? r1.y : 0;
  if (r1.x != 0) discard;
  r1.x = cmp(cb0[0].x >= 0.501960814);
  r1.y = ~(int)r1.x;
  r1.z = -0.501960814 + cb0[0].x;
  r1.z = cmp(r0.w >= r1.z);
  r1.x = r1.z ? r1.x : 0;
  if (r1.x != 0) discard;
  r1.x = cmp(r0.w < cb0[0].x);
  r1.x = r1.x ? r1.y : 0;
  if (r1.x != 0) discard;
  r1.xyz = -cb0[23].xyz + v6.xyz;
  r1.w = dot(r1.xyz, r1.xyz);
  r1.w = rsqrt(r1.w);
  r2.xyz = t2.Sample(s2_s, w2.xy).xyz;
  r3.xy = t1.Sample(s1_s, w2.xy).yw;
  r3.xy = float2(-0.5,-0.5) + r3.yx;
  r3.xy = r3.xy + r3.xy;
  r2.w = -r3.x * r3.x + 1;
  r2.w = -r3.y * r3.y + r2.w;
  r2.w = max(0, r2.w);
  r3.z = cmp(0 < r2.w);
  r4.x = rsqrt(r2.w);
  r2.w = r4.x * r2.w;
  r2.w = r3.z ? r2.w : 0;
  r4.xyz = v4.xyz * r3.yyy;
  r3.xyz = v3.xyz * r3.xxx + r4.xyz;
  r3.xyz = v5.xyz * r2.www + r3.xyz;
  r2.w = dot(r3.xyz, r3.xyz);
  r2.w = rsqrt(r2.w);
  r3.xyz = r3.xyz * r2.www;
  r2.w = dot(cb0[18].xyz, r3.xyz);
  r4.x = 1 / cb0[26].x;
  r4.y = r4.x * r3.w;
  r3.w = -r3.w * r4.x + 1;
  r4.x = saturate(v8.x);
  r3.w = r3.w * r4.x + r4.y;
  r2.w = cmp(r2.w < 0);
  r2.w = r2.w ? r3.w : 0;
  r3.w = saturate(dot(-r3.xyz, cb0[18].xyz));
  r4.x = saturate(dot(-r3.xyz, cb0[19].xyz));
  r4.y = saturate(dot(-r3.xyz, cb0[20].xyz));
  r5.xyz = cb0[9].xyz * r2.www;
  r4.xzw = cb0[10].xyz * r4.xxx;
  r4.xzw = r5.xyz * r3.www + r4.xzw;
  r4.xyz = cb0[11].xyz * r4.yyy + r4.xzw;
  r3.w = dot(-r3.xyz, cb0[17].xyz);
  r5.xyz = cb0[12].xyz * r3.www + cb0[13].xyz;
  r4.xyz = r5.xyz + r4.xyz;
  r3.w = dot(r3.xyz, -cb0[18].xyz);
  r5.xyz = r1.xyz * r1.www + cb0[18].xyz;
  r4.w = dot(r5.xyz, r5.xyz);
  r4.w = rsqrt(r4.w);
  r5.xyz = r5.xyz * r4.www;
  r4.w = saturate(dot(-r3.xyz, r5.xyz));
  r3.w = saturate(r3.w * 5 + 1);
  r4.w = log2(r4.w);
  r4.w = cb0[1].x * r4.w;
  r4.w = exp2(r4.w);
  r3.w = r4.w * r3.w;
  r4.w = dot(r3.xyz, -cb0[19].xyz);
  r1.xyz = r1.xyz * r1.www + cb0[19].xyz;
  r1.w = dot(r1.xyz, r1.xyz);
  r1.w = rsqrt(r1.w);
  r1.xyz = r1.xyz * r1.www;
  r1.x = saturate(dot(-r3.xyz, r1.xyz));
  r1.y = saturate(r4.w * 5 + 1);
  r1.x = log2(r1.x);
  r1.x = cb0[1].x * r1.x;
  r1.x = exp2(r1.x);
  r1.x = r1.x * r1.y;
  r3.x = r3.w * 0.939999998 + 0.0500000007;
  r3.yw = float2(0.5,0.5);
  r1.yzw = t3.Sample(s3_s, r3.xy).xyz;
  r3.z = r1.x * 0.939999998 + 0.0500000007;
  r3.xyz = t3.Sample(s3_s, r3.zw).xyz;
  r1.x = r2.w * 0.75 + 0.25;
  r5.xyz = cb0[9].xyz * r1.xxx;
  r5.xyz = r5.xyz * r2.xyz;
  r1.xyz = r5.xyz * r1.yzw;
  r1.xyz = max(float3(0,0,0), r1.xyz);
  r0.xyz = r0.xyz * r4.xyz + r1.xyz;
  r1.xyz = cb0[10].xyz * r2.xyz;
  r1.xyz = r3.xyz * r1.xyz;
  r1.xyz = max(float3(0,0,0), r1.xyz);
  r0.xyz = r1.xyz + r0.xyz;
  r1.x = max(cb0[21].z, v2.x);
  r1.x = min(cb0[21].w, r1.x);
  r1.x = cb0[22].w * r1.x;
  r1.yzw = cb0[22].xyz + -r0.xyz;
  o0.xyz = r1.xxx * r1.yzw + r0.xyz;
  o0.w = r0.w;
}