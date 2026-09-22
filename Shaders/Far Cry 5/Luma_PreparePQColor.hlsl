#include "../Includes/Color.hlsl"

Texture2D<float4> Scene : register(t0);
RWTexture2D<float4> Destination : register(u0);

[numthreads(8,8,1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint width, height;
    Destination.GetDimensions(width, height);
    if (id.x >= width || id.y >= height) return;

    // FC5's native HDR10 temporal color is display-referred BT.2020 PQ.
    // NGX expects linear HDR, in the same nits/80 convention as scRGB.
    float3 linear2020 = PQ_to_Linear(saturate(Scene.Load(int3(id.xy, 0)).rgb), GCT_SATURATE)
        * (HDR10_MaxWhiteNits / sRGB_WhiteLevelNits);
    Destination[id.xy] = float4(linear2020, 1.0);
}
