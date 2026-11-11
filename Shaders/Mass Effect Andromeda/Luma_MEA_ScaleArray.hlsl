// A slice of Luma's output-sized texture (or array) bilinearly scaled into an engine texture's slice, for the passes that draw natively
// while upscaling (see "RestoreEngineInputs"): the pixel shader (after Luma's Scale VS) into a render target slice, the compute shader
// into the UAV slice of a texture that can't be a render target. b0 = (the source slice u32, unused, the target size).

Texture2DArray<float4> sourceTexture : register(t0);
SamplerState linearSampler : register(s0);
RWTexture2DArray<float4> targetTexture : register(u0);

cbuffer ScaleArrayConstants : register(b0)
{
   uint sourceSlice;
   uint padding;
   float2 targetSize;
}

float4 scale_ps(float4 position : SV_Position, float2 uv : TEXCOORD0) : SV_Target
{
   return sourceTexture.SampleLevel(linearSampler, float3(uv, sourceSlice), 0.0);
}

[numthreads(8, 8, 1)] void scale_cs(uint3 id : SV_DispatchThreadID) {
   if (any(float2(id.xy) >= targetSize))
   {
      return;
   }
   targetTexture[uint3(id.xy, 0)] = sourceTexture.SampleLevel(linearSampler, float3((float2(id.xy) + 0.5) / targetSize, sourceSlice), 0.0);
}
