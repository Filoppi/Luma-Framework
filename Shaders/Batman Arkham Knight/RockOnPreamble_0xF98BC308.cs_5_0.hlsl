// Batman: Arkham Knight - TRockOnPreambleComputeShader (CRC32 0xF98BC308), replaced.
//
// Motion blur/DOF setup: writes the clamped velocity and linear depth of each pixel (u0), and the largest velocity of each 16x16 tile (u1).
// Same as the original, except that the depth and velocity are loaded at the render resolution: after super resolution upscaling
// the post processes run at the output resolution, while these buffers are still at the render resolution (top left of the textures).

#include "Includes/Common.hlsl"

cbuffer PreambleCB : register(b0)
{
    float4 c0[8];
    // c0[6].xy: view size (output resolution after upscaling)
    // c0[7].zw: view origin
}

cbuffer ViewCB : register(b1)
{
    float4 c1[21];
    // c1[20].zw: depth linearisation (1 / (d * z - w))
}

Texture2D<float4> SceneDepth : register(t0);
Texture2D<float4> Velocity : register(t1);
RWTexture2D<float4> VelocityAndDepth : register(u0);
RWTexture2D<float4> TileMaxVelocity : register(u1);

groupshared uint MaxVelocityLengthSquared;

[numthreads(16, 16, 1)]
void main(uint3 group_id : SV_GroupID, uint3 group_thread_id : SV_GroupThreadID, uint3 thread_id : SV_DispatchThreadID)
{
    if (group_thread_id.y * 16 + group_thread_id.x == 0)
    {
        MaxVelocityLengthSquared = 0;
    }
    GroupMemoryBarrierWithGroupSync();

    float2 pixel = float2(thread_id.xy) + c0[7].zw;
    uint2 coords = uint2(pixel);
    // Identical to "coords" when not upscaling
    uint2 render_coords = uint2((pixel + 0.5) * LumaData.GameData.PostRenderScale.xy);

    float2 velocity = Velocity.Load(int3(render_coords, 0)).xy * -0.25;
    float2 max_velocity = 16.0 / c0[6].xy;
    velocity = min(max_velocity * 2.0, max(velocity, max_velocity * -2.0));

    // Velocity lengths are positive floats, so they compare like their bits
    float velocity_length_squared = dot(velocity, velocity);
    InterlockedMax(MaxVelocityLengthSquared, asuint(velocity_length_squared));
    GroupMemoryBarrierWithGroupSync();
    if (MaxVelocityLengthSquared == asuint(velocity_length_squared))
    {
        TileMaxVelocity[group_id.xy] = float4(velocity, 0.0, 0.0);
    }

    float device_depth = min(SceneDepth.Load(int3(render_coords, 0)).x, 1.0);
    float view_depth = 1.0 / (device_depth * c1[20].z - c1[20].w);
    VelocityAndDepth[coords] = float4(velocity, view_depth, 0.0);
}
