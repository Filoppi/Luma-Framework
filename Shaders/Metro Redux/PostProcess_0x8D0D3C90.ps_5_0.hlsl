// Debug-retained Metro Redux post-process shader (dump hash 0x8D0D3C90).
// This shader performs a four-tap, edge-aware blend of t0 and a weighted blend
// toward v2. It has no explicit transfer-function conversion; use it to inspect
// the pass after the raw-scene debug output.
Texture2D<float4> t0 : register(t0);
SamplerState s5_s : register(s5);

#define cmp -

void main(
    float4 v0: SV_Position0,
    float4 v1: TEXCOORD0,
    float4 v2: TEXCOORD1,
    out float4 o0: SV_Target0)
{
   float4 r0, r1, r2;
   uint4 bitmask, uiDest;
   float4 fDest;

   r0.xyzw = float4(-0.0009765625, -0.0009765625, 0.0009765625, -0.0009765625) + v1.xyxy;
   r0.xy = t0.SampleBias(s5_s, r0.xy, -0.5).xy;
   r0.zw = t0.SampleBias(s5_s, r0.zw, -0.5).xy;
   r0.xyzw = r0.xxxy + r0.zzzw;
   r1.xyzw = float4(-0.0009765625, 0.0009765625, 0.0009765625, 0.0009765625) + v1.xyxy;
   r1.xy = t0.SampleBias(s5_s, r1.xy, -0.5).xy;
   r1.zw = t0.SampleBias(s5_s, r1.zw, -0.5).xy;
   r0.xyzw = r1.xxxy + r0.xyzw;
   r0.xyzw = r0.xyzw + r1.zzzw;
   r1.xyzw = r0.zzzw * float4(0.25, 0.25, 0.25, 0.25) + float4(-0.100000001, -0.100000001, -0.100000001, -0.100000001);
   r0.xyzw = float4(0.25, 0.25, 0.25, 0.25) * r0.xyzw;
   r1.xyzw = saturate(float4(1.25, 1.25, 1.25, 1.25) * r1.xyzw);
   r2.xyzw = r1.zzzw * float4(-2, -2, -2, -2) + float4(3, 3, 3, 3);
   r1.xyzw = r1.xyzw * r1.xyzw;
   r1.xyzw = r2.xyzw * r1.xyzw + -r0.zzzw;
   r2.xy = ddx_coarse(v1.xy);
   r2.zw = ddy_coarse(v1.xy);
   r2.xy = max(abs(r2.xy), abs(r2.zw));
   r2.x = max(r2.x, r2.y);
   r2.x = 1 / r2.x;
   r2.x = saturate(r2.x * 0.001953125 + -1);
   r0.xyzw = r2.xxxx * r1.xyzw + r0.xyzw;
   r1.xyzw = v2.zyxw * r0.zzzz;
   r0.xyzw = r0.xyzw * v2.zyxw + -r1.xyzw;
   r2.x = dot(v2.zyx, float3(0.300000012, 0.479999989, 0.219999999));
   r0.xyzw = r2.xxxx * r0.xyzw + r1.xyzw;
   o0.xyz = r0.xyz * r0.www;
   o0.w = r0.w;
   return;
}
