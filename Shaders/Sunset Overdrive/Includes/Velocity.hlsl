// Shared by the two velocity writers (LinearDepthCameraVelocity_0x524BC7C4, MotionBlurObjectVelocity_0x928DFB65), which see the same
// PSMotionBlurVelocityCB (b2, filled by 0x18B6F70 for both passes).

cbuffer PSMotionBlurVelocityCB : register(b2)
{
   row_major float4x4 g_MBV_ViewSpaceDelta; // Current view -> previous view (row vector)
   float4 g_MBV_ViewVec;                    // UV -> view x/z, y/z (current projection, with the current jitter)
   float4 g_MBV_ViewToScreen;               // View x/z, y/z -> UV (previous projection)
   float4 g_MBV_VelocityScale;              // xy = render size in pixels, w = max velocity length in pixels
}

// Writes both velocities from the current UV and the previous view position (x/z, y/z).
// "motionVectors" are SR's: render pixels with current + mv = previous, unclamped, with the jitter removed from both ends. The frustum
// is symmetric, so the projection centers are at UV 0.5 and the jitter is whatever ViewVec.zw and ViewToScreen.zw add to that. The
// previous UV takes the center 0.5 and the current one has the current jitter removed.
// "vanilla" is the game's velocity for its readers (motion blur, SSR reprojection, materials): pixels, current minus previous, clamped
// to g_MBV_VelocityScale.w (72 px) and stored as v / 127.5 + 127 / 255 in the R8G8_UNORM target. We write it from the same jitter-free
// motion. Vanilla carries the current jitter, so the larger offsets of SR and our SMAA T2x would reach
// every reader. Ours differs from vanilla's by at most the current jitter (0.125 px with the game's own T2x, under the 0.5 px encoding
// step).
void VelocityOutputs(float2 uv, float2 prevViewXYOverZ, out float2 vanilla, out float2 motionVectors)
{
   const float2 prevUV = prevViewXYOverZ * g_MBV_ViewToScreen.xy + 0.5;
   const float2 currentUV = uv + (g_MBV_ViewVec.zw + g_MBV_ViewVec.xy * 0.5) / g_MBV_ViewVec.xy;
   motionVectors = (prevUV - currentUV) * g_MBV_VelocityScale.xy;

   float2 velocity = -motionVectors;
   velocity *= g_MBV_VelocityScale.w / max(length(velocity), g_MBV_VelocityScale.w);
   vanilla = velocity * (2.0 / 255.0) + (127.0 / 255.0);
}
