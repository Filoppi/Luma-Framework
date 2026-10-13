// Batman: Arkham Knight - FCameraVelocityComputeShader (CRC32 0x47811EB4), replaced.
//
// Writes camera-only motion for every pixel into the R16G16_FLOAT velocity
// target before the base pass; moving objects later overwrite their pixels
// with (curNDC - prevNDC) * (-0.5, 0.5), computed by the material shaders
// with the jittered current and previous translated view projections.
// The original shader reprojected through an approximated, projection divided matrix,
// which gave velocities in different units than the objects, with a different jitter
// and a wrong camera translation.
// Here the jittered pixel NDC and depth go through a single matrix built on the CPU in double precision
// (inverse current translated view projection, camera translation since last frame, previous translated view projection),
// in homogeneous coordinates so that infinitely far pixels (sky, depth 1) work too.
// The pixel NDC comes from the view rect we provide: the pass's own constants have the full buffer size,
// which is wrong when upscaling (the view rect is smaller).
// Bindings and thread layout are identical to the original.

#include "Includes/Common.hlsl"

cbuffer CameraVelocityCB : register(b0)
{
    float4 c0[15];
}

Texture2D<float4> SceneDepth : register(t0);
RWTexture2D<float4> Velocity : register(u0);

[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    float device_depth = min(SceneDepth.Load(int3(id.xy, 0)).x, 1.0);

    // Jittered NDC of the pixel center (the jitter is part of the matrices, like for the objects)
    float4 view_rect = LumaData.GameData.CameraVelocityViewRect; // Size (xy), origin (zw)
    float2 ndc = (float2(id.xy) - view_rect.zw + 0.5) * (float2(2.0, -2.0) / view_rect.xy) + float2(-1.0, 1.0);

    float4x4 reprojection = float4x4(LumaData.GameData.ReprojectionMatrix[0], LumaData.GameData.ReprojectionMatrix[1], LumaData.GameData.ReprojectionMatrix[2], LumaData.GameData.ReprojectionMatrix[3]);
    float4 prev_clip = mul(float4(ndc, device_depth, 1.0), reprojection);
    float2 prev_ndc = prev_clip.xy / prev_clip.w;

    float2 velocity = (ndc - prev_ndc) * float2(-0.5, 0.5);
    Velocity[id.xy] = float4(velocity, velocity.x, velocity.x);
}
