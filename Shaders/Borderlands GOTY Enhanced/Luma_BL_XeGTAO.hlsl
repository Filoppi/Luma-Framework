// XeGTAO (https://github.com/GameTechDev/XeGTAO) for Borderlands GOTY Enhanced (UE3.5), replacing the game's NVIDIA HBAO+
// (GFSDK_SSAO).
// - The native AO runs at full resolution: deinterleave 0xFFE232A6 -> normals 0xB2B47225 -> coarse horizon AO 0xF534EB09 ->
//   bilateral blur 0x4E1BEE34 -> apply-multiply PS 0x44764BF6. XeGTAO runs at full resolution too and writes the final visibility
//   into the blur CS's u0, the game's final AO target (r16g16_float, the apply blit reads .x); the apply blit (dst * src_color) is
//   untouched.
// - It reads the game's own constant buffers, left bound by its AO passes (main.cpp does not rebind them): b0 = HBAO+ $Globals
//   (ProjInfo = NDC to view, live FOV), b2 = CSOffsetConstants (MinZ_MaxZRatioCS, the depth linearization).
// - Depth: the game's full-res r24_g8 scene depth (deinterleave 0xFFE232A6 t0), viewed r24_unorm_x8 and read with explicit Loads
//   (see XeGTAO_PrefilterDepths16x16).
// - Normals: the game's ViewNormalTex (coarse AO 0xF534EB09 t0), r11g11b10_float, the full view-space xyz packed v * 0.5 + 0.5 (no
//   z reconstruction).
// - Noise: frozen at 0 without an upscaler (the game has no TAA: a frame index would make the pattern boil), and denoise runs twice.
//   With DLSS/FSR (they accumulate the lit scene the AO multiplies into) it cycles frame % 64, an AO history runs between the main
//   pass and the denoise ("temporal_pass_cs", main.cpp) and denoise runs once, as Intel's XeGTAO.h advises with TAA (NoiseIndexRT,
//   set by main.cpp). The quality default is Very High.
// - GT-VBAO ("gtvbao_main_pass_cs", "GT-VBAO Enable") replaces the main pass only. It keeps both denoise passes.
// - View z is in UE3 units (near plane ~10), a huge range: DepthScaleRT (default 50, to ~metres) brings it into the range Intel's
//   constants expect (the R32F pyramid loses no precision at large z). EFFECT_RADIUS is anchored at that scale.

// --- Game constant buffers (bound by the game at the hooked dispatches) ---

cbuffer _Globals : register(b0) // NVIDIA GFSDK_SSAO $Globals
{
   float RadiusToScreen;        // Offset:   0
   float NegInvR2;              // Offset:   4  (GFSDK name, but stored as +1/R^2, measured: R = sqrt(1/NegInvR2), the vanilla AO radius)
   float NDotVBias;             // Offset:   8
   float2 InvFullResolution;    // Offset:  16  (1 / the AO input size: HBAO+ runs full-res; scratch and dispatches use that depth's size)
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

// --- Luma runtime knobs (main.cpp; DEVELOPMENT/TEST sliders tune them live) ---
cbuffer LumaGTAO : register(b11)
{
   float FinalValuePowerRT; // primary darkness dial, calibrated to the vanilla AO histogram
   float DepthScaleRT;      // view z divisor (UE3 units to ~metres), the dial against broad over-occlusion
   float RadiusOverrideRT;  // > 0 overrides EFFECT_RADIUS (view units after DepthScale)
   float DebugViewRT;       // DEVELOPMENT debug view (legend in the shared "XeGTAO.hlsl")
   float NoiseIndexRT;      // frame % 64 with DLSS/FSR, 0 otherwise (see the header)
}

// clang-format off
#include "Includes/Common.hlsl" // Game-local, before any shared include (see Includes/Common.hlsl)
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

#define XE_GTAO_ADJUST_VISIBILITY(visibility, viewspaceZ) visibility = max(0.03, visibility); // disallow total occlusion (which wouldn't make any sense anyhow since pixel is visible but also helps with packing bent normals)
#define XE_GTAO_FINAL_OUTPUT_TYPE                         float2                              // the game's r16g16_float final AO (apply blit reads .x)
#define XE_GTAO_ENCODE_FINAL(v)                           float2(v, 0.0)

Texture2D tex1 : register(t1);

float3 XeGTAO_LoadViewspaceNormal(uint2 pixCoord)
{
   // tex1 = the game's ViewNormalTex (r11g11b10_float, captured at the coarse-AO dispatch)

   // Decode the game's packed view-space normals: xyz in [0,1] -> [-1,1], all three channels, no reconstruction.
   return normalize(tex1.Load(int3(pixCoord, 0)).xyz * 2.0 - 1.0);
}

// GT-VBAO settings for UE3 with one AO pixel per scene pixel, not measured against this game's HBAO+: 2 slices, 1 under
// the history ("temporal_pass_cs", main.cpp defines it) with Core's FAST noise, 8 steps, a 0.2 radius back face.
#ifndef GTVBAO_SLICE_COUNT
#define GTVBAO_SLICE_COUNT 2.0
#endif
#ifndef GTVBAO_STEP_COUNT
#define GTVBAO_STEP_COUNT 8.0
#endif
#define GTVBAO_THICKNESS(effectRadius) (0.2 * (effectRadius))
// Core's "FAST Noise" (Shaders/Global/Textures/FAST: 64 layers of 128x128 RGBA, filter adapted spatio-temporal noise), a layer per frame
#if GTVBAO_FAST_NOISE
Texture2DArray<float4> fast_noise : register(t4);
#define GTVBAO_NOISE(pixCoord) fast_noise.Load(int4((pixCoord) & 127u, uint(NoiseIndexRT) & 63u, 0)).xyz
#endif
#include "Includes/GTVBAO.hlsl"
#include "Includes/XeGTAOTemporal.hlsl"
