#include "HDR_Color.hlsl"
Texture2D<float4> Scene : register(t0);
RWTexture2D<float4> Destination : register(u0);
[numthreads(8,8,1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint width,height;
    Destination.GetDimensions(width,height);
    if(id.x>=width || id.y>=height) return;
    // RenoDX maps into BT2020 gamut. Avoid feeding signed scRGB to DLSS,
    // which clamped negative BT709 components in the captured native HDR test.
    Destination[id.xy] = float4(mul(FC5_709_TO_2020, Scene.Load(int3(id.xy,0)).rgb),1);
}
