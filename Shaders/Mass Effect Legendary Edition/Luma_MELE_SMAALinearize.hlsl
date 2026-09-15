// Linear-light copy of the gamma post buffer for SMAA's neighborhood blend: it averages through the hardware bilinear,
// so it needs linear input up front. Luma_SMAA_impl.hlsl re-encodes the result.
#include "../Includes/Color.hlsl"

Texture2D<float4> encoded : register(t0);
RWTexture2D<float4> linear_out : register(u0);

[numthreads(8, 8, 1)] void main(uint3 id : SV_DispatchThreadID) {
   const float4 c = encoded.Load(int3(id.xy, 0));
   linear_out[id.xy] = float4(gamma_to_linear(c.rgb, GCT_MIRROR), c.a);
}
