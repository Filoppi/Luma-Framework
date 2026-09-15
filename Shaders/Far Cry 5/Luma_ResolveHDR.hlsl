#include "HDR_Color.hlsl"
Texture2D<float4> Resolved : register(t0);
RWTexture2D<float4> Destination : register(u0);
[numthreads(8,8,1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint width,height;
    Destination.GetDimensions(width,height);
    if(id.x>=width || id.y>=height) return;
    // Restore signed linear scRGB before the original post-processing/UI chain.
    Destination[id.xy] = float4(mul(FC5_2020_TO_709, Resolved.Load(int3(id.xy,0)).rgb),1);
}
