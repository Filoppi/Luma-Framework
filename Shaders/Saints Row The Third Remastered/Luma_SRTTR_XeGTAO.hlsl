// XeGTAO adapted for Saints Row: The Third Remastered: replaces the game's MiniEngine SSAO (ssao_miniengine).
// Source: https://github.com/GameTechDev/XeGTAO
//
// SRTTR specifics (ambient PS 0xFD45DCA7 disassembly and input dumps):
// - Runs right before the ambient draw, on its own inputs: t0 full-res D24 hardware depth (standard Z), t1 full-res
//   r16g16_unorm Lambert azimuthal view-space normals, and its cb10 AMBIENT_PARAMS (rebound PS -> CS b10).
// - The final denoise writes the ambient's t2 (the game's full-res r8_unorm SSAO texture) in place of the vanilla
//   chain, which is skipped. The ambient multiplies its lighting by the value, so it is VISIBILITY (1 = open, as the
//   vanilla chain's unoccluded output measures).
// - Depth and NDC->view use the ambient's own formulas: viewZ = P._43 / (d - P._33),
//   view.xy = (ndc - (P._31, P._32)) * viewZ / (P._11, P._22) (P._31/P._32 carry the TAA jitter, so it is removed).
// - Normals: the G-buffer ones (t1), normal maps included, so fine surface detail (gravel, slats, rivets) is occluded too. At the
//   defaults (EFFECT_RADIUS 0.4, FinalValuePowerRT 1.4, calibrated on depth normals to 0.99-1.10x the vanilla darkening) the offline
//   sim of this shader over 5 scenes puts them at ~1.2-1.35x the vanilla (depth-only) SSAO in plain scenes and ~1.65-1.75x in detailed ones.
// - NoiseIndexRT is the frame index while a temporal AA (TAA, DLAA, FSR) accumulates, 0 (frozen pattern) otherwise.

#include "Includes/Common.hlsl"

// --- Game constant buffer: the ambient's AMBIENT_PARAMS (main.cpp binds its range at CS b10) ---

cbuffer AmbientParams : register(b10)
{
   float4 Projection[4]; // Rows: c0.x P._11, c1.y P._22, c2.xyz P._31 P._32 P._33, c3.z P._43
}

// --- Luma runtime knobs (LumaData custom data, set by main.cpp; live-tunable via DEV sliders, no recompile) ---
#define NoiseIndexRT      LumaData.CustomData1 // temporal noise index (0 = frozen)
#define DebugViewRT       LumaData.CustomData2 // DEVELOPMENT debug view (legend in Includes/XeGTAO.hlsl)
#define FinalValuePowerRT LumaData.CustomData3 // primary darkness dial
#define RadiusOverrideRT  LumaData.CustomData4 // > 0 overrides EFFECT_RADIUS (metres)

// Metres at RADIUS_REFERENCE_DEPTH (Intel default 0.5, constant); against the vanilla SSAO see "Normals" above.
// RadiusOverrideRT > 0 wins.
#define EFFECT_RADIUS 0.4

// The vanilla SSAO (MiniEngine) has a screen-space radius (10 px at 1920 wide per hierarchy level), so its world radius grows with
// distance. A constant world radius matched it only at mid range: on the SSAO texture dumps of two scenes it was 1.9-3.2x darker at
// 3-6 m and 0.8-1.3x at 12-25 m. So the radius scales with view depth, pivoting at the scenes' median depth, within limits (Prey's
// heuristic only grows it with distance; here it also shrinks near the camera, as the vanilla one does).
#define RADIUS_REFERENCE_DEPTH 8.0  // Metres
#define RADIUS_DEPTH_SCALE_MIN 0.25 // Below 2 m the radius stays at 1/4
#define RADIUS_DEPTH_SCALE_MAX 8.0  // Beyond 64 m the radius stays at 8x
#define RADIUS_MULTIPLIER      1.457
float XeGTAO_DepthScaledEffectRadius(float viewspaceZ)
{
   const float depthScale = clamp(viewspaceZ / RADIUS_REFERENCE_DEPTH, RADIUS_DEPTH_SCALE_MIN, RADIUS_DEPTH_SCALE_MAX);
   return (RadiusOverrideRT > 0.0 ? RadiusOverrideRT : EFFECT_RADIUS) * RADIUS_MULTIPLIER * depthScale;
}
#define XE_GTAO_EFFECT_RADIUS(viewspaceZ) XeGTAO_DepthScaledEffectRadius(viewspaceZ)

#define XE_GTAO_PIXEL_SIZE_FROM_SOURCE    1 // 1 / AO resolution from tex0 (no constant holds it)

// Transcribed from the ambient: ndc = (uv.x*2-1, 1-2*uv.y), view.xy = (ndc - (P._31, P._32)) * viewZ / (P._11, P._22).
// Expressed as the XeGTAO mul/add pair over raw uv: viewPos.xy = (uv * MUL + ADD) * viewZ.
#define NDC_TO_VIEW_MUL           (float2(2.0, -2.0) / float2(Projection[0].x, Projection[1].y))
#define NDC_TO_VIEW_ADD           ((float2(-1.0, 1.0) - Projection[2].xy) / float2(Projection[0].x, Projection[1].y))

#define XE_GTAO_FINAL_OUTPUT_TYPE unorm float // the game's r8_unorm SSAO texture: visibility
#define XE_GTAO_ENCODE_FINAL(v)   (v)

// Hardware depth -> view z (near 0.15 at d = 0, far 8192 at d = 1; units are metres).
float XeGTAO_ScreenSpaceToViewSpaceDepth(const float screenDepth)
{
   return Projection[3].z / (screenDepth - Projection[2].z);
}

Texture2D<float2> normals : register(t1); // the ambient's t1

// The game's Lambert azimuthal decode (the ambient's): e = t*2-1, f = |e|^2, n = (e * 2*sqrt(1-f), 2f-1), in the same view space as
// the positions.
float3 XeGTAO_LoadViewspaceNormal(uint2 pixCoord)
{
   const float2 e = normals.Load(int3(pixCoord, 0)) * 2.0 - 1.0;
   const float f = min(dot(e, e), 1.0);
   return float3(e * (2.0 * sqrt(1.0 - f)), 2.0 * f - 1.0);
}

#include "../Includes/XeGTAO.hlsl"
