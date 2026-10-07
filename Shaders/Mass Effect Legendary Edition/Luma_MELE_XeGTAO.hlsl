// XeGTAO replacement for the trilogy-wide NVIDIA HBAO+ chain, adapted from the repository's existing XeGTAO ports.
// Source: https://github.com/GameTechDev/XeGTAO
//
// MELE-specific contracts shared by all three games:
// - Run at native AO half resolution and write visibility to blur u0, the game's final R8_UNORM AO target.
//   The native apply shader 0x2E826C0F still blends dst*src_color into the fp16 scene.
// - Inherit cb0 HBAO+ $Globals and cb2 CSOffsetConstants; layouts come from live disassembly of
//   0x80212FD6/0x06D92B08 and retain standard GFSDK offsets.
// - Depth input = the game's half-res r24_unorm_x8 depth copy (deinterleave 0x497830D8 t0), read with explicit
//   .Load: GatherRed on an r24_unorm_x8 view returns all-zeros on some drivers and silently kills the AO.
// - ViewNormalTex from horizon shader 0x80212FD6 stores view-space xy in R8G8_UNORM; reconstruct z locally.
// - With no TAA or motion vectors, pass temporalIndex 0 to SpatioTemporalNoise and rely on Very High quality plus two
//   denoisers.
// - Divide UE3 view Z by DepthScale=50 to approximate the meter-scale range expected by XeGTAO.

// Native constant buffers inherited at the hooked dispatches; offsets come from live disassembly.

cbuffer _Globals : register(b0) // NVIDIA GFSDK_SSAO $Globals.
{
   float RadiusToScreen;        // Offset:   0
   float NegInvR2;              // Offset:   4; -1/R^2 in native view units.
   float NDotVBias;             // Offset:   8
   float2 InvFullResolution;    // Offset:  16; inverse AO resolution.
   float2 InvQuarterResolution; // Offset:  24
   int2 FullResOffset;          // Offset:  32
   int2 QuarterResOffset;       // Offset:  40
   float AOMultiplier;          // Offset:  48
   float PowExponent;           // Offset:  52; native blur darkness control.
   float4 ProjInfo;             // Offset:  64; view xy = (uv*xy + zw) * viewZ.
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
   float4 MinZ_MaxZRatioCS;          // Offset:  96; viewZ = 1 / (d * z - w), non-reverse Z.
   float4 DynamicScaleCS;            // Offset: 112
}

// Runtime parameters uploaded by main.cpp in b11.
cbuffer LumaGTAO : register(b11)
{
   float FinalValuePowerRT; // Darkness control calibrated to native AO.
   float DepthScaleRT;      // View-Z divisor from UE3 units to approximate meters.
   float RadiusOverrideRT;  // Positive values override EFFECT_RADIUS after depth scaling.
   float DebugViewRT;       // DEVELOPMENT: 0=off, 1=depth, 2=normals, 3=AO x8, 4=edges.
}

#include "Includes/Common.hlsl"

// Compile-time defaults; runtime b11 overrides the exposed controls.

#ifndef EFFECT_RADIUS
#define EFFECT_RADIUS 0.6 // Native ME1LE radius: 30 UE3 units / DepthScale 50; runtime override wins.
#endif

#ifndef EFFECT_FALLOFF_RANGE
#define EFFECT_FALLOFF_RANGE 0.005 // Hard falloff matches native contact AO; Intel default 0.615 is softer.
#endif

#ifndef SAMPLE_DISTRIBUTION_POWER
#define SAMPLE_DISTRIBUTION_POWER 1.5 // Default 2.0
#endif

// ViewNormalTex z sign; view-space normals face the camera.
#ifndef NORMAL_Z_SIGN
#define NORMAL_Z_SIGN (-1.0)
#endif

#define VIEWPORT_PIXEL_SIZE InvFullResolution

// GFSDK ProjInfo contains the live NDC-to-view multiply/add pair, including dialogue zoom.
#define NDC_TO_VIEW_MUL ProjInfo.xy
#define NDC_TO_VIEW_ADD ProjInfo.zw

// Convert non-reverse D24 through native MinZ_MaxZRatioCS, then scale UE3 units into XeGTAO's expected range.
// Without scaling, its mip and falloff assumptions cause broad over-occlusion.
float XeGTAO_ScreenSpaceToViewSpaceDepth(const float screenDepth)
{
   float viewZ = 1.0 / max(1e-7, screenDepth * MinZ_MaxZRatioCS.z - MinZ_MaxZRatioCS.w);
   return max(0.0, viewZ) / max(1e-3, DepthScaleRT);
}

#define NoiseIndexRT                                      0
#define XE_GTAO_ADJUST_VISIBILITY(visibility, viewspaceZ) visibility = max(0.03, visibility); // A visible surface cannot be fully occluded.
#define XE_GTAO_FINAL_OUTPUT_TYPE                         unorm float                         // Native final R8_UNORM AO.
#define XE_GTAO_ENCODE_FINAL(v)                           (v)

Texture2D tex1 : register(t1);

float3 XeGTAO_LoadViewspaceNormal(uint2 pixCoord)
{
   // Decode packed view-space xy and reconstruct unit z. Camera-facing normals use negative z.
   float2 nxy = tex1.Load(int3(pixCoord, 0)).xy * 2.0 - 1.0;
   float3 viewspaceNormal;
   viewspaceNormal.xy = nxy;
   viewspaceNormal.z = NORMAL_Z_SIGN * sqrt(saturate(1.0 - dot(nxy, nxy)));
   return normalize(viewspaceNormal);
}

// tex0 = native half-resolution R24_UNORM_X8 depth captured at deinterleave.
#include "../Includes/XeGTAO.hlsl"
