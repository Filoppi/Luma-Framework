#pragma once

// Granblue Fantasy Relink cbuffer structures

// SceneBuffer (cb0) - 544 bytes (34 float4 registers)
struct cbSceneBuffer
{
   Math::Matrix44 g_View;                   // Offset:   0  (c0-c3)
   Math::Matrix44 g_Proj;                   // Offset:  64  (c4-c7)
   Math::Matrix44 g_ViewProjection;         // Offset: 128  (c8-c11)
   Math::Matrix44 g_ViewInverseMatrix;      // Offset: 192  (c12-c15)
   Math::Matrix44 g_PrevView;               // Offset: 256  (c16-c19)
   Math::Matrix44 g_PrevProj;               // Offset: 320  (c20-c23)
   Math::Matrix44 g_PrevViewProjection;     // Offset: 384  (c24-c27)
   Math::Matrix44 g_PrevViewInverseMatrix;  // Offset: 448  (c28-c31)
   float4 g_ProjectionOffset;               // Offset: 512  (c32) - contains jitter offsets
   int g_FrameCount[4];                     // Offset: 528  (c33)
};
