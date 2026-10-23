// Copies any (typeless) depth format into a R32_FLOAT texture (e.g. for FSR 4, which can't share depth stencil textures between DX11 and DX12)
Texture2D<float> sourceTexture : register(t0);
RWTexture2D<float> targetTexture : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 vDispatchThreadId : SV_DispatchThreadID)
{
  targetTexture[vDispatchThreadId.xy] = sourceTexture.Load(int3(vDispatchThreadId.xy, 0)); // Out of bounds writes are discarded
}
