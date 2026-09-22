#include "HDR_Color.hlsl"
#include "../Includes/Color.hlsl"
#include "../Includes/RCAS.hlsl"

cbuffer ResolveCB : register(b0)
{
    float4 ResolveParams; // width, height, RCAS sharpness [0,1], paper white
}
Texture2D<float4> Resolved : register(t0);
Texture2D<float2> DummyMotion : register(t1);

float4 ResolveRCAS(float4 position)
{
    int2 pixel = int2(position.xy);
    int2 lastPixel = int2((int)ResolveParams.x - 1, (int)ResolveParams.y - 1);
    return RCAS(pixel, int2(0, 0), lastPixel, ResolveParams.z,
        Resolved, DummyMotion, ResolveParams.w, false, (float4)0, false);
}

float4 resolve_sdr_ps(float4 position : SV_Position) : SV_Target
{
    float4 color = ResolveRCAS(position);
    return float4(color.rgb, 1.0);
}

float4 resolve_scrgb_ps(float4 position : SV_Position) : SV_Target
{
    float3 linear2020 = ResolveRCAS(position).rgb;
    return float4(mul(FC5_2020_TO_709, linear2020), 1.0);
}

float4 resolve_pq_ps(float4 position : SV_Position) : SV_Target
{
    float3 linear2020 = ResolveRCAS(position).rgb;
    float3 pq2020 = Linear_to_PQ(linear2020 * (sRGB_WhiteLevelNits / HDR10_MaxWhiteNits), GCT_POSITIVE);
    return float4(pq2020, 1.0);
}
