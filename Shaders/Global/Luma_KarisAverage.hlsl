#include "../Includes/Color.hlsl"

Texture2D tex : register(t0);
RWTexture2D<float4> uav : register(u0);

float get_karis_weight(float3 color)
{
    const float luma = GetLuminance(color);
    return rcp(1.0 + luma);
}

[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    // Cross pattern. TODO: Test other patterns.
    //   a
    // b c d
    //   e
    const float3 a = tex.Load(int3(dtid.xy, 0), int2(0, -1)).rgb;
    const float3 b = tex.Load(int3(dtid.xy, 0), int2(-1, 0)).rgb;
    const float3 c = tex.Load(int3(dtid.xy, 0)).rgb;
    const float3 d = tex.Load(int3(dtid.xy, 0), int2(1, 0)).rgb;
    const float3 e = tex.Load(int3(dtid.xy, 0), int2(0, 1)).rgb;

    // Do Karis average.
    float4 sum = float4(a, 1.0) * get_karis_weight(a);
    sum += float4(b, 1.0) * get_karis_weight(b);
    sum += float4(c, 1.0) * get_karis_weight(c);
    sum += float4(d, 1.0) * get_karis_weight(d);
    sum += float4(e, 1.0) * get_karis_weight(e);
    sum.rgb *= rcp(sum.a);

    uav[dtid.xy] = float4(sum.rgb, 1.0);
}