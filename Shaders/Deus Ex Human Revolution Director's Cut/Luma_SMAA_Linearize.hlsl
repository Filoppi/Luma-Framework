#include "../Includes/Common.hlsl"

Texture2D tex : register(t0);
RWTexture2D<float4> uav : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    float4 color = tex.Load(int3(dtid.xy, 0));
    color.rgb = gamma_sRGB_to_linear(color.rgb);
    uav[dtid.xy] = color;
}