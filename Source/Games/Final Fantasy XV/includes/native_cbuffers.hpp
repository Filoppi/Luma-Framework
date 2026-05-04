#pragma once

struct cbTemporalAA
{

  float4 g_screenSize;               // Offset:    0 Size:    16
  float4 g_frameBits;                // Offset:   16 Size:    16
  float4 g_uvJitterOffset;           // Offset:   32 Size:    16
  Math::Matrix44 g_motionMatrix;           // Offset:   48 Size:    64
  Math::Matrix44 g_reconstructMatrix;      // Offset:  112 Size:    64
  float4 g_unprojectParams;          // Offset:  176 Size:    16 [unused]
  float g_maxLuminanceInv;           // Offset:  192 Size:     4
  uint32_t g_gamePaused;             // Offset:  196 Size:     4
  uint32_t g_hairUseAlphaTest;       // Offset:  200 Size:     4
  uint32_t g_waterResponsiveAA;      // Offset:  204 Size:     4

};

struct IView_Combined_cbView
{

  Math::Matrix44 Projection;            // Offset:    0 Size:    64
  Math::Matrix44 View;                  // Offset:   64 Size:    64
  Math::Matrix44 ViewProj;              // Offset:  128 Size:    64
  Math::Matrix44 InvView;               // Offset:  192 Size:    64
  Math::Matrix44 InvProjection;         // Offset:  256 Size:    64

  struct Viewport
  {

    int2 TopLeft;                       // Offset:  320 Size:     8
    int2 Size;                          // Offset:  328 Size:     8

  } ViewPort;                           // Offset:  320 Size:    16
  float3 ViewPoint;                     // Offset:  336 Size:    12
  Math::Matrix44 PreviousView;         // Offset:  352 Size:    64
  Math::Matrix44 PreviousProj;         // Offset:  416 Size:    64
  Math::Matrix44 PreviousViewProj;     // Offset:  480 Size:    64
  Math::Matrix44 InvViewProj;          // Offset:  544 Size:    64
  int RenderTargetViewIndex;           // Offset:  608 Size:     4
  int dummy_0;                         // Offset:  612 Size:     4
  int dummy_1;                         // Offset:  616 Size:     4
  int dummy_2;                         // Offset:  620 Size:     4
  Math::Matrix44 ViewProjLS;           // Offset:  624 Size:    64
  Math::Matrix44 PreviousViewProjLS;   // Offset:  688 Size:    64
  float3 PreviousViewPoint;            // Offset:  752 Size:    12

};
