// XeGTAO adapted for The Witcher 2 EE (REDengine DX9 -> dgVoodoo2 -> DX11) — replaces the game's HBAO.
// Source: https://github.com/GameTechDev/XeGTAO
//
// TW2 specifics:
// - Only the native generator draw (PS 0x3FEEC0F7 / VS 0x5D9D0449, half-res HBAO) is replaced, by the 4
//   XeGTAO dispatches; the result is CopyResource'd into the game's AO target, which is created without a
//   UAV bind, hence the copy through our own texture. Downstream stays vanilla and reads only .x:
//   pack 0x953119B5 -> ping-pong 0xC131C40D x2 -> blur 0xD01CBD13 x2 -> apply 0x5C63E1C2.
// - Depth input = the generator's own t0: r32_float half-res, ALREADY LINEAR view-space depth, so no
//   unpack, only the DepthScaleRT unit rescale.
// - Normals are generated from depth in-place (XE_GTAO_GENERATE_NORMALS) using the same NDC->view
//   convention as the native HBAO, so the space stays self-consistent.
// - NDC->view from the game's cb4 (copied PS->CS by main.cpp):
//   viewRay.xy = (uv.x*2-1, 1-2*uv.y) * cb4[9].zw; viewPos = viewRay * depth, viewPos.z = +depth
// - No TAA and no motion vectors here: NoiseIndex is FROZEN at 0 (a frame index would make the pattern
//   boil) and denoise runs twice; all stability is spatial.
// - Viewport size comes through the Luma knobs CB at b9, not from game constants.

// --- Game constant buffer (bound by the game at the hooked draw; main.cpp copies PS b4 -> CS b4) ---

cbuffer GameCB4 : register(b4)
{
   float4 cb4[236]; // [9].zw = tan-half-FOV NDC->view scale (see header); layout as captured, do not trim
}

// --- Luma runtime knobs (set from main.cpp; live-tunable via DEV sliders, no recompile) ---
// b9, NOT b11 (which the sibling MELE/BL GOTY ports use): core's DrawBloom owns b11 for its own constants,
// so keeping the AO knobs off that slot costs nothing and avoids a clash. Mirrored by kGTAOKnobsCBSlot.
cbuffer LumaGTAO : register(b9)
{
   float FinalValuePowerRT;    // primary darkness dial, calibrated to the vanilla AO histogram
   float DepthScaleRT;         // viewZ divisor (game units -> ~meters); THE dial against broad over-occlusion
   float RadiusOverrideRT;     // > 0 overrides EFFECT_RADIUS (view units after DepthScale)
   float DebugViewRT;          // DEVELOPMENT: 0=off 1=depth gradient 2=normals 3=AO x8 4=edges
   float2 ViewportPixelSizeRT; // 1 / AO target resolution (half render res), set by main.cpp
   float2 PaddingRT;
}

// User configurable
//

#ifndef XE_GTAO_GENERATE_NORMALS
#define XE_GTAO_GENERATE_NORMALS 1 // TW2: no normal buffer captured at this point of the frame; derive from depth (a real normal source exists later in the frame — see 0x53AB3429 — but not here)
#endif

#ifndef EFFECT_RADIUS
#define EFFECT_RADIUS 0.81 // anchored to the game's own HBAO radius: probed cb4[11] = (R, R^2) = (1.1835, 1.4008) view units (meters — depth p50 ~7.3); 0.81 * RADIUS_MULTIPLIER 1.457 = 1.18 matches native R. RadiusOverrideRT > 0 wins.
#endif

#ifndef EFFECT_FALLOFF_RANGE
#define EFFECT_FALLOFF_RANGE 0.005 // hard-edge falloff (full sample weight up to the radius edge) = crisp contact AO, repo-standard for HBAO-native games; Intel default 0.615 is softer
#endif

#ifndef SAMPLE_DISTRIBUTION_POWER
#define SAMPLE_DISTRIBUTION_POWER 1.5 // Default 2.0
#endif

//

#define VIEWPORT_PIXEL_SIZE ViewportPixelSizeRT

// Transcribed from the native HBAO PS (0x3FEEC0F7): ndc = (uv.x*2-1, 1-2*uv.y), viewRay = ndc * cb4[9].zw.
// Expressed as the XeGTAO mul/add pair over raw uv: viewPos.xy = (uv * MUL + ADD) * viewZ.
#define NDC_TO_VIEW_MUL (float2(2.0, -2.0) * cb4[9].zw)
#define NDC_TO_VIEW_ADD (float2(-1.0, 1.0) * cb4[9].zw)

// TW2's depth texture is ALREADY linear view-space depth (the native HBAO scales its NDC ray by the raw
// sample) — no projection unpack. DepthScaleRT only rescales the game units into the ~meter range XeGTAO's
// Intel-tuned constants (radius/falloff/mip offsets) expect.
float XeGTAO_ScreenSpaceToViewSpaceDepth(const float screenDepth)
{
   return max(0.0, screenDepth) / max(1e-3, DepthScaleRT);
}

#define NoiseIndexRT 0

// Capped at the native HBAO kernel, cb4[16].y pixels.
#define XE_GTAO_MAIN_PASS_EFFECT_RADIUS(viewspaceZ) min(XeGTAO_EffectRadius(), cb4[16].y * viewspaceZ * NDC_TO_VIEW_MUL_X_PIXEL_SIZE.x)

// Plain float4 (not unorm): the copy-source texture matches the game AO RT's ACTUAL format, which is
// rgba16_float whenever Luma's swapchain-aspect r8g8b8a8 upgrade catches it (and unorm8 otherwise) —
// float4 UAV writes are valid against both.
#define XE_GTAO_FINAL_OUTPUT_TYPE float4
// Vanilla generator writes (AO, viewZ, 0, 0); the downstream pack pass 0x953119B5 reads only .x from
// this target (it re-reads depth from the depth texture), so .yzw can stay 0.
#define XE_GTAO_ENCODE_FINAL(v) float4(v, 0.0, 0.0, 0.0)

// tex0 = the game's half-res r32_float linear view-space depth (the hooked HBAO draw's t0)
#include "../Includes/XeGTAO.hlsl"
