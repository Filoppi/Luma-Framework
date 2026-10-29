// FSR's exposure. It's the multiplier the tonemap applies to the scene SR reads, g_AdaptedLumBuffer[1]
// (last frame's adapted exposure) times the user's "Exposure", written into the 1x1 texture FSR takes.

#include "Includes/Common.hlsl"

StructuredBuffer<float> exposureBuffer : register(t0); // g_AdaptedLumBuffer
RWTexture2D<float> exposure : register(u0);

[numthreads(1, 1, 1)] void main() {
   // As "Luma_SO_ReactiveMask.hlsl", in case the buffer holds less
   uint elements, stride;
   exposureBuffer.GetDimensions(elements, stride);
   exposure[uint2(0, 0)] = (elements > 1 ? exposureBuffer[1] : 1.0) * LumaSettings.GameSettings.Exposure;
}
