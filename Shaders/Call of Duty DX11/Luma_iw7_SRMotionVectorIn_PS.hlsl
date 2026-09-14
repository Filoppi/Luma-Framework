#include "./Includes/Common.hlsl"

Texture2D<float4> tex0 : register(t);

float2 DecodeMotionVectorCompact(float4 u2Sample)
{
    float encodedMag    = u2Sample.z * 255.0 + u2Sample.w;
    int   bits          = ((int)encodedMag - 0x1fbd1df5) << 1;
    float magnitude01   = saturate(asfloat(bits));
    float rawMag        = magnitude01 / 3.05175781e-005;

    float2 dir          = (u2Sample.xy - 0.498039216) / 0.0125000002;
    // dir.x = motionX (magnitude-scaled), dir.y = ±1 (sign only)
    float2 motionVec    = float2(dir.x, dir.y * rawMag);

    return motionVec;
}

void main(float4 v1 : SV_Position0, out float2 o0 : SV_Target0)
{
  float2 x = tex0.Load(int3(v1.xy, 0)).xy;
  // float scale = GS.RenderResolutionScale == 1.f ? 1.f : 1.f;
  o0.xy = /* DecodeMotionVectorCompact */(x) * -1;
}