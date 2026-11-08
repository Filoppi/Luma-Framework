#pragma once

#include <cstdint>

// The protocol between a 32-bit game's SR bridge ("SRBridge.h") and the x64 "Luma-Upscaler.exe" next to the game's exe
// (Source/Tools/SR Bridge Helper), which runs DLSS, FSR 3, FSR 4 or XeSS (all x64 only) on its own D3D11 device of the game's adapter,
// on NT-handle shared textures between two shared fences.
// Text lines over the helper's stdin, its stdout and stderr go to "Luma-Upscaler.log" next to the exe.
//
// Settings: "<render width> <render height> <output width> <output height> <hdr> <inverted depth> <jittered motion vectors>
//   <auto exposure> <dynamic resolution> <motion vector scale x> <motion vector scale y> <DLSS render preset>"
// Start line: "<version> <SR::Type> <luid low> <luid high> <settings> <a handle per "Resource" (0: none)> <fence in> <fence out>"
// The helper signals "out" at READY_FENCE_VALUE once the upscaler exists (it exits if it can't create it).
// Then a line per message:
// "settings <settings>": the helper recreates the upscaler on the same textures (it exits if it can't).
// "frame <n> <jitter x> <jitter y> <reset> <render width> <render height> <pre exposure> <sharpness> <near> <far> <vertical fov>
//   <time delta> <FSR velocity factor>":
//   the helper waits for "in" n, upscales and signals "out" n (n past READY_FENCE_VALUE); it exits if the upscaler failed.
//   The floats can be "inf" or "nan", which streams can't read, so the helper parses the line with "sscanf_s".
// Any exit after READY_FENCE_VALUE first signals "out" UINT64_MAX (no game wait is left hanging; the bridge reads it as an exit).
// EOF ends it.
namespace SRBridgeProtocol
{
   constexpr int PROTOCOL_VERSION = 1;

   constexpr const wchar_t* HELPER_NAME = L"Luma-Upscaler.exe";
   constexpr const wchar_t* LOG_NAME = L"Luma-Upscaler.log";

   // The shared textures, in the order their handles travel. A missing optional one (0) isn't passed to the upscaler.
   enum Resource
   {
      RESOURCE_COLOR, // The render resolution color
      RESOURCE_MOTION,
      RESOURCE_DEPTH,        // Device depth
      RESOURCE_REACTIVE,     // Reactive / bias mask (optional)
      RESOURCE_TRANSPARENCY, // FSR's transparency & composition mask (optional)
      RESOURCE_EXPOSURE,     // 1x1 exposure (optional)
      RESOURCE_OUTPUT,       // The output resolution color (the helper writes it through a UAV)
      RESOURCE_COUNT
   };

   constexpr uint64_t READY_FENCE_VALUE = 1;
} // namespace SRBridgeProtocol
