RWTexture2D<float4> uav : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    float4 color = uav[dtid.xy];

    // As done in the game's native TAA.
    color = clamp(color, 0.0, 10000.0);
    color *= color;

    uav[dtid.xy] = color;
}