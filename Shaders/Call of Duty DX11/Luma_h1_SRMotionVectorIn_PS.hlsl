#include "./Includes/Common.hlsl"

Texture2D<float4> tex0 : register(t);

/*
  //encoding
  r0.zw = float2(0.25,0.25) * abs(r0.xy);
  r0.zw = min(float2(1,1), r0.zw);
  r1.xy = float2(-4,-4) + abs(r0.xy);
  r0.xy = cmp(r0.xy >= float2(0,0));
  r1.xy = saturate(float2(0.027777778,0.027777778) * r1.xy);
  r1.xy = sqrt(r1.xy);
  r0.zw = -r1.xy + r0.zw;
  r0.zw = r0.zw * float2(0.5,0.5) + r1.xy;
  r0.xy = r0.xy ? r0.zw : -r0.zw;
  r0.xy = clamp(r0.xy, -1, 1); //snorm
*/
float2 DecodeMotionPacked(float2 x)
{
    float2 ae = abs(x);
    float2 t = 2.0 * ae - 1.0;
    float2 l = 8.0 * ae;
    float2 q = 4.0 + 36.0 * t * t;
    return sign(x) * (ae <= 0.5 ? l : q);
}

void main(float4 v1 : SV_Position0, out float2 o0 : SV_Target0)
{
  float2 x = tex0.Load(int3(v1.xy, 0)).xy;
  // float scale = GS.RenderResolutionScale == 1.f ? 1.f : 1.f;
  o0.xy = -DecodeMotionPacked(x) * 1;
}