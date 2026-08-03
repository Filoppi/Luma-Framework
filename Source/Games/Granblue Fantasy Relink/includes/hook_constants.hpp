#pragma once

#include <cstdint>

// ============================================================
// Granblue Fantasy Relink - Version-Specific Address Constants
// ============================================================
// Define the appropriate version macro before including this file:
//   - V1_3_2  : Game version 1.3.2 (Pre-DLC)
//   - V2_0_2  : Game version 2.0.2 (DLC)
//   - V2_0_3  : Game version 2.0.3 (Current)
// ============================================================

#ifndef V2_0_3
#define V2_0_3
#endif

// ============================================================
// v2.0.3 - Current
// ============================================================
#ifdef V2_0_3

// Code Addresses (RVA from module base)
constexpr uintptr_t kInitializeDX11RenderingPipeline_RVA = 0x007F3480;
// JitterWrite: Entry point of TemporalAntiAliasingComponent::trans (RVA 0x215F9C0)
//   v2.0.2: 0x216582D (mid-function) | v2.0.3: 0x215F9C0 (entry point)
//   Non-trivial: function moved from mid-site to entry point
constexpr uintptr_t kJitterWrite_RVA = 0x00215F9C0;
// TAA Component Init: Entry point (push rsi — prologue start)
//   v2.0.2: 0x2165260 | v2.0.3: 0x215F810
constexpr uintptr_t kTemporalAntiAliasingComponent_Init_RVA = 0x0215F810;

// Data Addresses (RVA from module base)
// Render dimensions: TAA component reads these for upscale decision
//   v2.0.2: 0x6B84088 / 0x6B8408C | v2.0.3: 0x6B81058 / 0x6B8105C
constexpr uintptr_t kRenderWidth_RVA = 0x06B81058;
constexpr uintptr_t kRenderHeight_RVA = 0x06B8105C;
// CameraIndex: int32 — read to index into g_cameraTable
//   v2.0.2: 0x7021320 | v2.0.3: 0x701E2E0
constexpr uintptr_t kCameraIndex_RVA = 0x0701E2E0;
// CameraTable: Array of camera pointers
//   v2.0.2: 0x54BF400 | v2.0.3: 0x54BB3A0
constexpr uintptr_t kCameraTable_RVA = 0x054BB3A0;
// TAASettingsGlobal: 16-byte xmmword buffer in v2.0.3 (NOT a pointer-to-struct)
//   v2.0.2: 0x7032DE0 (pointer) | v2.0.3: 0x703CA90 (xmmword buffer)
//   Non-trivial: changed from pointer to inline buffer
constexpr uintptr_t kTAASettingsGlobal_RVA = 0x0703CA90;
// TAARunningFlag: Pointer (qword) to the TAA running flag byte.
//   Double-dereference: load pointer from 0x7371338, then read byte at target.
//   v2.0.2: 0x7032E45 (offset 0x65 from pointer) | v2.0.3: 0x7371338 (pointer to byte)
//   Non-trivial: changed from pointer+offset to pointer-to-byte (requires double-deref)
constexpr uintptr_t kTAARunningFlag_RVA = 0x07371338;
// TAARenderScaleFlagPointer: Pointer to struct with render scale flag at +0x65
//   v2.0.2: 0x7032DE0 | v2.0.3: 0x702FDA0
constexpr uintptr_t kTAARenderScaleFlagPointer_RVA = 0x0702FDA0;
// JitterPhaseCounter: Global phase counter (v2.0.3 moved from TAA component)
//   v2.0.2: 0x703F470 | v2.0.3: 0x703C430
//   Non-trivial: moved from [self+0x24] in TAA component to global
constexpr uintptr_t kJitterPhaseCounter_RVA = 0x0703C430;
// TAAResetFlag: uint8_t
constexpr uintptr_t kTAAResetFlag_RVA = 0x07371010;

#endif // V2_0_3

// ============================================================
// v2.0.2
// ============================================================
#ifdef V2_0_2

constexpr uintptr_t kInitializeDX11RenderingPipeline_RVA = 0x007F9E10;
constexpr uintptr_t kJitterWrite_RVA = 0x0216582D;
constexpr uintptr_t kTemporalAntiAliasingComponent_Init_RVA = 0x02165260;

constexpr uintptr_t kOutputWidth_RVA = 0x06B84090;
constexpr uintptr_t kOutputHeight_RVA = 0x06B84094;
constexpr uintptr_t kRenderWidth_RVA = 0x06B84088;
constexpr uintptr_t kRenderHeight_RVA = 0x06B8408C;
constexpr uintptr_t kCameraIndex_RVA = 0x07021320;
constexpr uintptr_t kCameraTable_RVA = 0x054BF400;
constexpr uintptr_t kTAASettingsGlobal_RVA = 0x07032DE0;
constexpr uintptr_t kJitterPhaseCounter_RVA = 0x0703F470;
constexpr uintptr_t kJitterPhaseMask_CL_RVA = 0x02165876;
constexpr uintptr_t kJitterPhaseMask_EAX_RVA = 0x0216587C;
constexpr uintptr_t kTAAResetFlag_RVA = 0x07371010;

#endif // V2_0_2

// ============================================================
// v1.3.2
// ============================================================
#ifdef V1_3_2

constexpr uintptr_t kInitializeDX11RenderingPipeline_RVA = 0x007455C2;
constexpr uintptr_t kJitterWrite_RVA = 0x01A9EB6B;
constexpr uintptr_t kTemporalAntiAliasingComponent_Init_RVA = 0x01A9E5D0;

constexpr uintptr_t kOutputWidth_RVA = 0x068B4090;
constexpr uintptr_t kOutputHeight_RVA = 0x068B4094;
constexpr uintptr_t kRenderWidth_RVA = 0x068B4088;
constexpr uintptr_t kRenderHeight_RVA = 0x068B408C;
constexpr uintptr_t kCameraGlobal_RVA = 0x068B4F90;
constexpr uintptr_t kTAASettingsGlobal_RVA = 0x06D32DE0;
constexpr uintptr_t kJitterPhaseCounter_RVA = 0x06D3F470;
constexpr uintptr_t kJitterPhaseMask_CL_RVA = 0x01A9EB76;
constexpr uintptr_t kJitterPhaseMask_EAX_RVA = 0x01A9EB7C;

#endif // V1_3_2

// ============================================================
// Common offsets (same across all versions)
// ============================================================
constexpr size_t kVSSetConstantBuffers1_VTableIndex = 119;
constexpr uintptr_t kCameraProjectionDataOffset = 0x60;
constexpr uintptr_t kProjectionJitterXOffset = 0x940;
constexpr uintptr_t kProjectionJitterYOffset = 0x944;
// Jitter table: 64 entries × 8 bytes (float2), offset 0x28 from TAA component*
//   Unchanged across all versions (1.3.2/2.0.2/2.0.3)
constexpr uintptr_t kTAAJitterTableOffset = 0x28;
constexpr uintptr_t kTAAJitterPhaseIndexOffset = 0x24;
constexpr size_t kTAAJitterTableCount = 64;
