// The scene's depth and stencil (D24S8) point stretched to the output size, for the passes after the upscaler that depth or stencil
// test (the scanner overlay). The depth goes through SV_Depth; the stencil can't be written per pixel (SV_StencilRef isn't on every
// GPU), so it takes a pass per bit: the pixels whose source stencil has "stencilBit" set, the depth-stencil state writing that bit
// alone. b0 = (render size / output size, the stencil bit: 0 for the depth pass).

Texture2D<float> depthTexture : register(t0);
Texture2D<uint2> stencilTexture : register(t1);

cbuffer ResampleConstants : register(b0)
{
   float2 sourceToTarget;
   uint stencilBit;
}

float main(float4 position : SV_Position) : SV_Depth
{
   const int3 texel = int3(int2(position.xy * sourceToTarget), 0);
   if (stencilBit != 0 && (stencilTexture.Load(texel).g & stencilBit) == 0)
   {
      discard;
   }
   return depthTexture.Load(texel);
}
