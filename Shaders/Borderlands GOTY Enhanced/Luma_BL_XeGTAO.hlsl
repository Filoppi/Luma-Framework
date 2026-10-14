// XeGTAO adapted for Borderlands GOTY Enhanced (UE3.5) — replaces the game's NVIDIA HBAO+ (GFSDK_SSAO).
// Source: https://github.com/GameTechDev/XeGTAO
//
// BL GOTY specifics:
// - The native AO pipeline is FULL-RES (3840x2160 @ 4K): deinterleave 0xFFE232A6 -> normals 0xB2B47225 ->
//   coarse horizon AO 0xF534EB09 -> bilateral blur 0x4E1BEE34 -> apply-multiply PS 0x44764BF6. We run at
//   full res and write the final visibility term into the game's own FINAL AO target (the blur CS u0, a
//   r16g16_float; apply blit reads .x). The apply blit (blend dst*src_color) is untouched by us.
// - We read the game's OWN constant buffers, left bound by its AO passes (main.cpp does not rebind them):
//   cb0 = HBAO+ $Globals (ProjInfo = NDC->view, live FOV), cb2 = CSOffsetConstants (MinZ_MaxZRatioCS = depth
//   linearization).
// - Depth input = the game's full-res r24_g8 scene depth (deinterleave 0xFFE232A6 t0), viewed r24_unorm_x8 and
//   read with explicit Loads (see XeGTAO_PrefilterDepths16x16).
// - Normals input = the game's ViewNormalTex (coarse-AO pass 0xF534EB09 t0), r11g11b10_float: xyz packed
//   v*0.5+0.5 (full 3-channel view-space normal, no z reconstruct). View-space already.
// - BL GOTY has NO TAA and no motion vectors: NoiseIndex is FROZEN at 0 (never feed a frame index — the
//   pattern must be static or it boils), quality default is Very High and denoise runs twice; all
//   stability is spatial.
// - viewZ is in UE3 units (near plane ~10 units) — a huge range; DepthScale (default 50 -> ~meters)
//   rescales it into the range XeGTAO's Intel-tuned constants expect (R32F pyramid, so no fp16 precision
//   loss at huge Z). EFFECT_RADIUS below is anchored at that scale.

// --- Game constant buffers (bound by the game at the hooked dispatches) ---

cbuffer _Globals : register(b0) // NVIDIA GFSDK_SSAO $Globals
{
   float RadiusToScreen;        // Offset:   0
   float NegInvR2;              // Offset:   4  (GFSDK name; this build stores +1/R^2 (measured positive) -> R=sqrt(1/NegInvR2), view units = vanilla AO radius)
   float NDotVBias;             // Offset:   8
   float2 InvFullResolution;    // Offset:  16  (1 / the AO input resolution; native HBAO+ runs full-res here — we size scratch + dispatches from that same depth, so the UV math matches by construction)
   float2 InvQuarterResolution; // Offset:  24
   int2 FullResOffset;          // Offset:  32
   int2 QuarterResOffset;       // Offset:  40
   float AOMultiplier;          // Offset:  48
   float PowExponent;           // Offset:  52  (vanilla darkness dial, applied in the blur CS)
   float4 ProjInfo;             // Offset:  64  (viewPos.xy = (uv*ProjInfo.xy + ProjInfo.zw) * viewZ)
   float2 Float2Offset;         // Offset:  80
   float4 Jitter;               // Offset:  96
   int ArrayOffset;             // Offset: 112
   float4 JitterCS[8];          // Offset: 128
}

cbuffer CSOffsetConstants : register(b2)
{
   float4x4 ViewProjectionMatrixCS;  // Offset:   0
   float4 CameraPositionCS;          // Offset:  64
   float4 ScreenPositionScaleBiasCS; // Offset:  80
   float4 MinZ_MaxZRatioCS;          // Offset:  96  (viewZ = 1 / (d * .z - .w), non-reverse-Z)
   float4 DynamicScaleCS;            // Offset: 112
}

// --- Luma runtime knobs (set from main.cpp; live-tunable via DEV sliders, no recompile) ---
cbuffer LumaGTAO : register(b11)
{
   float FinalValuePowerRT; // primary darkness dial, calibrated to the vanilla AO histogram
   float DepthScaleRT;      // viewZ divisor (UE3 units -> ~meters); THE dial against broad over-occlusion
   float RadiusOverrideRT;  // > 0 overrides EFFECT_RADIUS (view units after DepthScale)
   float DebugViewRT;       // DEVELOPMENT: 0=off 1=depth gradient 2=normals 3=AO x8 4=edges
}

// clang-format off
#include "Includes/Common.hlsl" // game-local: order load-bearing (pulls GameCBuffers before shared includes) — do not let SortIncludes move it
// clang-format on

// User configurable
//

#ifndef EFFECT_RADIUS
#define EFFECT_RADIUS 0.64 // anchored to the game's own HBAO+ radius: probed NegInvR2=0.000463 -> R=sqrt(1/NegInvR2)=46.5 UE3-units; chosen so the effective search (EFFECT_RADIUS * RADIUS_MULTIPLIER * DepthScale) = 0.64*1.457*50 = 46.6uu matches native R. RadiusOverrideRT > 0 wins.
#endif

#ifndef EFFECT_FALLOFF_RANGE
#define EFFECT_FALLOFF_RANGE 0.005 // punchy hard-edge falloff: full sample weight up to the radius edge = crisp contact AO matching BL's punchy native HBAO+ (vs the soft 0.615/0.95 gradual fade). Pairs with power ~1.0
#endif

#ifndef SAMPLE_DISTRIBUTION_POWER
#define SAMPLE_DISTRIBUTION_POWER 1.5 // Default 2.0
#endif

// BL packs the full xyz view-space normal (v*0.5+0.5) — decoded directly, no reconstruction. NORMAL_Z_SIGN
// flips only the decoded z if the view-space handedness needs it (verify via DebugViewRT=2: smooth
// per-surface shading = correct; flip to -1.0 if the shading looks inverted).
#ifndef NORMAL_Z_SIGN
#define NORMAL_Z_SIGN (1.0)
#endif

#define VIEWPORT_PIXEL_SIZE InvFullResolution

// GFSDK ProjInfo is exactly the NDC->view mul/add pair (live FOV, dialogue zoom included).
#define NDC_TO_VIEW_MUL ProjInfo.xy
#define NDC_TO_VIEW_ADD ProjInfo.zw

// Hardware d24 (non-reverse-Z) -> view Z via the game's own MinZ_MaxZRatioCS, then rescaled by
// DepthScaleRT so the tuned XeGTAO constants (radius/falloff, ~meter scale) apply. UE3 near plane ~10
// units and far -> infinity; without the rescale the huge Z range causes broad over-occlusion
// (mips/falloff assumptions break).
float XeGTAO_ScreenSpaceToViewSpaceDepth(const float screenDepth)
{
   float viewZ = 1.0 / max(1e-7, screenDepth * MinZ_MaxZRatioCS.z - MinZ_MaxZRatioCS.w);
   return max(0.0, viewZ) / max(1e-3, DepthScaleRT);
}

#define NoiseIndexRT                                      0
#define XE_GTAO_ADJUST_VISIBILITY(visibility, viewspaceZ) visibility = max(0.03, visibility); // disallow total occlusion (which wouldn't make any sense anyhow since pixel is visible but also helps with packing bent normals)
#define XE_GTAO_FINAL_OUTPUT_TYPE                         float2                              // the game's r16g16_float final AO (apply blit reads .x)
#define XE_GTAO_ENCODE_FINAL(v)                           float2(v, 0.0)

Texture2D tex1 : register(t1);

float3 XeGTAO_LoadViewspaceNormal(uint2 pixCoord)
{
   // tex1 = the game's ViewNormalTex (r11g11b10_float, captured at the coarse-AO dispatch)

   // Decode the game's packed view-space normals: xyz in [0,1] -> [-1,1], all three channels (see NORMAL_Z_SIGN).
   float3 n = tex1.Load(int3(pixCoord, 0)).xyz * 2.0 - 1.0;
   n.z *= NORMAL_Z_SIGN;
   return normalize(n);
}

// tex0 = the game's full-res scene depth (r24_g8, viewed r24_unorm_x8), captured at the deinterleave dispatch
#include "../Includes/XeGTAO.hlsl"
