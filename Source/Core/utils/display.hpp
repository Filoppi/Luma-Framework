#pragma once

// Display (HDR/SDR) state queries and control. The implementation is in "display.inl".

#include <Windows.h>

#include "../includes/globals.h" // "srgb_white_level"

#if ENABLE_NVAPI
#include "nvapi.h"
#endif

struct IDXGISwapChain;
struct IDXGISwapChain3;

namespace Display
{
	// Display queries made by Luma itself need to see the real display state, so they declare themselves.
	// While at least one of these is alive on a thread, hooks that spoof the display capabilities to the game (e.g. "DXGIHooks") let that thread's calls through untouched.
	// We can't tell Luma's calls from the game's ones through the caller module, as the ReShade swapchain wrapper forwards the calls.
	struct ScopedRealDisplayQuery
	{
		ScopedRealDisplayQuery();
		~ScopedRealDisplayQuery();
		ScopedRealDisplayQuery(const ScopedRealDisplayQuery&) = delete;
		ScopedRealDisplayQuery& operator=(const ScopedRealDisplayQuery&) = delete;
	};
	bool IsRealDisplayQueryActive();

#if ENABLE_NVAPI
	namespace NVAPI
	{
		bool Init();
		bool DeInit();
		bool SetDisplayOutputMode(HWND hWnd, NV_DISPLAY_OUTPUT_MODE displayOutputMode, bool allowEnableHDROnDisplay = true, bool allowDisableHDROnDisplay = false);
	}
#endif

	// Pass in the game window (e.g. retrieve it from the swapchain) and an optional fallback monitor. 0 on both to use the primary display.
	// Optionally pass in the swapchain pointer to fall back to checking on the swapchain.
	// If HDR is enabled, it's automatically also supported.
	bool IsHDRSupportedAndEnabled(HWND hwnd /*= 0*/, bool& supported, bool& enabled, IDXGISwapChain3* swapChain = nullptr, HMONITOR fallbackMonitor = 0);

	// Returns true if the display has been successfully set to the target SDR/HDR mode, or if it already was.
	// Returns false in case of an unknown error.
	bool SetHDREnabled(HWND hwnd, bool enabled = true, HMONITOR fallbackMonitor = 0);

	bool GetSDRWhiteLevel(HWND hwnd, float& nits, HMONITOR fallbackMonitor = 0);
	bool SetSDRWhiteLevel(HWND hwnd, float nits = srgb_white_level, HMONITOR fallbackMonitor = 0);

	// Returns false if failed or if HDR is not engaged (but the white luminance can still be used).
	bool GetHDRMaxLuminance(IDXGISwapChain* swapChain, float& maxLuminance, float defaultMaxLuminance = 80.f /*Windows sRGB standard luminance*/);
}
