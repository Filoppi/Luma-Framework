Texture2D<float4> t1 : register(t1);
Texture2D<float4> t0 : register(t0);

SamplerState s1_s : register(s1);
SamplerState s0_s : register(s0);

cbuffer cb0 : register(b0)
{
  float4 cb0[9];
}

// Draws a sprite on screen. It might stretch in ultrawide but we can't do much about that.
// Used for to draw blinks of "damage overlay" when being hurt.
void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : COLOR0,
  float2 v2 : TEXCOORD0,
  out float4 o0 : SV_TARGET0)
{
  float4 r0,r1,r2,r3;
  int4 r3i;
  r0.xy = v2.xy * float2(2,2) + float2(-1,-1);
  r0.zw = -cb0[1].xy + r0.xy;
  r1.x = dot(r0.zw, r0.zw);
  r1.y = sqrt(r1.x);
  r1.x = rsqrt(r1.x);
  r0.zw = r1.xx * r0.zw;
  r1.x = min(abs(r0.w), abs(r0.z));
  r1.z = max(abs(r0.w), abs(r0.z));
  r1.z = 1 / r1.z;
  r1.x = r1.x * r1.z;
  r1.z = r1.x * r1.x;
  r1.w = r1.z * 0.0208350997 + -0.0851330012;
  r1.w = r1.z * r1.w + 0.180141002;
  r1.w = r1.z * r1.w + -0.330299497;
  r1.z = r1.z * r1.w + 0.999866009;
  r1.w = r1.x * r1.z;
  r2.x = (abs(r0.z) < abs(r0.w));
  r1.w = r1.w * -2 + 1.57079637;
  r1.w = r2.x ? r1.w : 0;
  r1.x = r1.x * r1.z + r1.w;
  r1.z = (r0.z < -r0.z);
  r1.z = r1.z ? -3.141593 : 0;
  r1.x = r1.x + r1.z;
  r1.z = min(r0.w, r0.z);
  r1.w = max(r0.w, r0.z);
  r1.z = (r1.z < -r1.z);
  r1.w = (r1.w >= -r1.w);
  r1.z = r1.w ? r1.z : 0;
  r1.x = r1.z ? -r1.x : r1.x;
  r1.x = 3.14159203 + r1.x;
  r1.x = cb0[8].x * r1.x;
  r2.x = 0.159153998 * r1.x;
  r2.y = 0;
  r1.x = t1.Sample(s1_s, r2.xy).x;
  r1.z = min(1, r1.y);
  r1.x = r1.x + r1.z;
  r1.x = min(1, r1.x);
  r0.zw = r1.xx * r0.zw;
  r0.zw = cb0[0].xy * r0.zw;
  r0.zw = float2(-16,-16) * r0.zw;
  r1.xzw = float3(0,0,0);
  r2.xyzw = r0.xyzw;
  r3i.x = 0;
  while (true) {
    if (r3i.x >= 4) break;
    r2.xy = r2.xy + r2.zw;
    r3.yz = float2(1,1) + r2.xy;
    r3.yz = float2(0.5,0.5) * r3.yz;
    r3.yzw = t0.Sample(s0_s, r3.yz).xyz;
    r1.xzw = r3.yzw + r1.xzw;
    r2.zw = float2(0.800000012,0.800000012) * r2.zw;
    r3i.x++;
  }
  r0.x = 3.33333325 * r1.y;
  r0.x = min(1, r0.x);
  r0.y = r0.x * -2 + 3;
  r0.x = r0.x * r0.x;
  r0.w = r0.y * r0.x;
  r0.xyz = v1.xyz * r1.xzw;
  r1.x = 0.25;
  r1.w = v1.w;
  o0.xyzw = r1.xxxw * r0.xyzw;
}