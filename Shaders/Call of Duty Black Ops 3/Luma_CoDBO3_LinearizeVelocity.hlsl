#include "common1.hlsl"

Texture2D<float4> codeTexture0 : register(t0);
// Texture2D<float> OITTexture : register(t1);

// r0.zw = float2(0.100000001,0.100000001) * abs(r0.xy);
// r0.zw = min(float2(1,1), r0.zw);
// r1.xy = float2(-10,-10) + abs(r0.xy);
// r0.xy = cmp(r0.xy >= float2(0,0));
// r1.xy = saturate(float2(0.0333333351,0.0333333351) * r1.xy);
// r0.zw = -r1.xy + r0.zw;
// r0.zw = r0.zw * float2(0.5,0.5) + r1.xy;
// o0.xy = r0.xy ? r0.zw : -r0.zw;

// Decodes a motion vector from the 16SNorm piecewise-nonlinear encoding used by this shader.
// The encoding splits abs(mv) into two linear segments:
//   abs(mv) in [0,  10] -> encoded in [0.0, 0.5]  via e = abs(mv) * 0.05
//   abs(mv) in [10, 40] -> encoded in [0.5, 1.0]  via e = 0.5 + (abs(mv) - 10) / 60
// Sign is preserved from the encoded value.
float2 DecodeMotionVector16SNorm(float2 enc)
{
    float2 s = sign(enc);
    float2 e = abs(enc);
    float2 lo = e * 20.0;                        // inverse of segment 1: e <= 0.5
    float2 hi = 10.0 + (e - 0.5) * 60.0;        // inverse of segment 2: e >  0.5
    return s * (e <= 0.5 ? lo : hi);
}

void main(float4 v1 : SV_Position0, out float2 o0 : SV_Target0)
{
  float2 x = codeTexture0.Load(int3(v1.xy, 0)).xy;

  o0.xy = -DecodeMotionVector16SNorm(x.xy);
}