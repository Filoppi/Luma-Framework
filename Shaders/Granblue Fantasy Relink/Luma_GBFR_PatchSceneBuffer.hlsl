#include "Includes/Common.hlsl"

struct SceneBuffer
{
   column_major float4x4 g_View;                  // Offset:   0  (c0-c3)
   column_major float4x4 g_Proj;                  // Offset:  64  (c4-c7)
   column_major float4x4 g_ViewProjection;        // Offset: 128  (c8-c11)
   column_major float4x4 g_ViewInverseMatrix;     // Offset: 192  (c12-c15)
   column_major float4x4 g_PrevView;              // Offset: 256  (c16-c19)
   column_major float4x4 g_PrevProj;              // Offset: 320  (c20-c23)
   column_major float4x4 g_PrevViewProjection;    // Offset: 384  (c24-c27)
   column_major float4x4 g_PrevViewInverseMatrix; // Offset: 448  (c28-c31)
   float4 g_ProjectionOffset;                     // Offset: 512  (c32) - jitter offsets
   int4 g_FrameCount;                             // Offset: 528  (c33)
};

// The original SceneBuffer bound as a constant buffer (read-only input)
cbuffer OriginalSceneBuffer : register(b0)
{
   SceneBuffer g_SceneBuffer;
}

// Output: patched SceneBuffer written as a structured buffer
RWStructuredBuffer<SceneBuffer> g_PatchedSceneBuffer : register(u0);

[numthreads(1, 1, 1)]
void main(uint3 ThreadID: SV_DispatchThreadID)
{
   SceneBuffer patched = g_SceneBuffer;

   // =========================================================================
   // Patch current-frame projection matrix: replace engine jitter with ours
   // =========================================================================
   // The engine TAA jitter is stored in g_Proj._m20 and g_Proj._m21.
   // These are the asymmetric frustum offsets in NDC space.
   // We strip the engine jitter and apply our own from LumaData.

   float4x4 proj = patched.g_Proj;

   // Strip engine jitter first so we can apply Luma jitter deterministically.
   proj[2][0] = 0.0;
   proj[2][1] = 0.0;

   proj[2][0] = LumaData.GameData.JitterOffset.x;
   proj[2][1] = LumaData.GameData.JitterOffset.y;

   // scale fov for debugging purposes
      // proj[0][0] *= 0.5;
      // proj[1][1] *= 0.5;

   patched.g_Proj = proj;

   patched.g_ViewProjection = mul(patched.g_View, patched.g_Proj);

   float4x4 prevProj = patched.g_PrevProj;

   prevProj[2][0] = 0.0;
   prevProj[2][1] = 0.0;

   prevProj[2][0] = LumaData.GameData.PrevJitterOffset.x;
   prevProj[2][1] = LumaData.GameData.PrevJitterOffset.y;

   // scale fov for debugging purposes
   // prevProj[0][0] *= 0.5;
   // prevProj[1][1] *= 0.5;

   patched.g_PrevProj = prevProj;

   // Recompute PrevViewProjection = PrevView * PrevProj
   patched.g_PrevViewProjection = mul(patched.g_PrevView, prevProj);

   patched.g_ProjectionOffset.x = LumaData.GameData.JitterOffset.x;
   patched.g_ProjectionOffset.y = LumaData.GameData.JitterOffset.y;
   patched.g_ProjectionOffset.z = LumaData.GameData.PrevJitterOffset.x;
   patched.g_ProjectionOffset.w = LumaData.GameData.PrevJitterOffset.y;

   g_PatchedSceneBuffer[0] = patched;
}
