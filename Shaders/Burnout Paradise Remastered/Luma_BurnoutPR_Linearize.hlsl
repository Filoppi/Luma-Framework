#include "../Includes/Color.hlsl"

Texture2D tex : register(t0);
RWTexture2D<float4> uav : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    uav[dtid.xy] = float4(gamma_sRGB_to_linear(tex.Load(int3(dtid.xy, 0)).rgb), 1.0);    
}