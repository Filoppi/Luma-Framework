#pragma once

#include <icm.h> // Color profiles (see "HasUserHDRColorProfile()")

#if ENABLE_NVAPI
#include "nvapi.h"

// wstring conversion
#include <locale>
#include <codecvt>

#ifdef _DEBUG
#include <iostream>
#include <iomanip>
#define NVIDIA_API_ERROR_MSG(expression, x) if(expression) std::cerr << "[ERROR] " << __FILE__ << " " << __FUNCTION__ << " " << std::hex << std::uppercase << x << std::nouppercase << std::dec << std::endl
#define NVIDIA_API_INFO_MSG(x) std::cout << "[INFO] " << __FILE__ << " " << __FUNCTION__ << " " << std::hex << std::uppercase << x << std::dec << std::endl
#else
#define NVIDIA_API_ERROR_MSG(expression, x) (void)(expression); (void)x
#define NVIDIA_API_INFO_MSG(x) (void)x
#endif
#endif // ENABLE_NVAPI

// Allows falling back on the legacy Windows "Advanced Color" display APIs to check for and engage HDR, on Windows versions older than 11 24H2.
// These can't distinguish between HDR and "Advanced Color" in SDR (from Windows 11 22H2), so they can report HDR as enabled when it isn't, or engage the wrong mode.
// Without them, older Windows versions can only detect HDR through the swapchain.
#ifndef ENABLE_LEGACY_ADVANCED_COLOR
#define ENABLE_LEGACY_ADVANCED_COLOR 0
#endif // ENABLE_LEGACY_ADVANCED_COLOR

namespace Display
{
#if ENABLE_NVAPI
	namespace NVAPI
	{
		bool hasInit = false;

		bool Init()
		{
			// No need to init locally more than once for now
			if (hasInit)
			{
				return true;
			}

			// This will return "NVAPI_LIBRARYNOTFOUND" if Nvidia drivers aren't installed
			NvAPI_Status status = NvAPI_Initialize();
			if (status != NVAPI_OK)
			{
				NvAPI_ShortString error;
				NvAPI_GetErrorMessage(status, error);
				printf("NVAPI init failed: 0x%x - %s\n", status, error);
				return false;
			}
		
			assert(!hasInit);
			hasInit = true;
			return true;
		}

		bool DeInit()
		{
			if (!hasInit)
			{
				return true;
			}

			NvAPI_Status status = NvAPI_Unload();
			NVIDIA_API_ERROR_MSG(status != NVAPI_OK, status);
			if (status != NVAPI_OK)
			{
				assert(false);
				return false;
			}

			assert(hasInit);
			hasInit = false;
			return true;
		}

		NvU32 GetNvapiDisplayIdFromHwnd(HWND hWnd, HMONITOR hMonitor = 0, bool fallbackOnPrimary = false)
		{
			if (hMonitor == 0)
			{
				hMonitor = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
				// Might happen if "hWnd" is 0/invalid
				if (fallbackOnPrimary && hMonitor == 0)
				{
					hMonitor = MonitorFromWindow(hWnd, MONITOR_DEFAULTTOPRIMARY);
				}
				else if (hMonitor == 0)
				{
					return 0;
				}
			}

			MONITORINFOEXW monitorInfo = {};
			monitorInfo.cbSize = sizeof(monitorInfo);
			if (!GetMonitorInfoW(hMonitor, &monitorInfo))
				return 0;

			// szDevice is a wide GDI name like L"\\.\DISPLAY1"; NVAPI wants narrow.
			const int wlen = static_cast<int>(wcslen(monitorInfo.szDevice));
			const int len  = WideCharToMultiByte(CP_UTF8, 0, monitorInfo.szDevice, wlen, nullptr, 0, nullptr, nullptr);
			std::string displayName(len, '\0');
			WideCharToMultiByte(CP_UTF8, 0, monitorInfo.szDevice, wlen, displayName.data(), len, nullptr, nullptr);

			NvU32 displayId = 0;
			if (NvAPI_DISP_GetDisplayIdByDisplayName(displayName.c_str(), &displayId) == NVAPI_OK)
				return displayId;
		
			// 0 is never used by valid NV displays ID
			return 0;
		}

		NvAPI_Status GetDisplayCapabilities( NvU32 displayId, NV_HDR_CAPABILITIES& hdrCapabilities)
		{
			memset(&hdrCapabilities, 0, sizeof(hdrCapabilities));
			hdrCapabilities.version = NV_HDR_CAPABILITIES_VER; // Latest (e.g. NV_HDR_CAPABILITIES_VER3)
			hdrCapabilities.driverExpandDefaultHdrParameters = 1; // In case the display EDID didn't contain some metadata, the driver fills it up with its best guessed values
			NvAPI_Status result = NvAPI_Disp_GetHdrCapabilities(displayId, &hdrCapabilities);
			NVIDIA_API_ERROR_MSG(NVAPI_OK != result, result);
		
			// Don't exclusively check "NVAPI_INCOMPATIBLE_STRUCT_VERSION" as we aren't sure it'd always returned
			if (result != NVAPI_OK)
			{
				memset(&hdrCapabilities, 0, sizeof(hdrCapabilities));
				hdrCapabilities.version = NV_HDR_CAPABILITIES_VER2;
				hdrCapabilities.driverExpandDefaultHdrParameters = 1;
				result = NvAPI_Disp_GetHdrCapabilities(displayId, &hdrCapabilities);
				NVIDIA_API_ERROR_MSG(NVAPI_OK != result, result);
			
				if (result != NVAPI_OK)
				{
					memset(&hdrCapabilities, 0, sizeof(hdrCapabilities));
					hdrCapabilities.version = NV_HDR_CAPABILITIES_VER1;
					hdrCapabilities.driverExpandDefaultHdrParameters = 1;
					result = NvAPI_Disp_GetHdrCapabilities(displayId, &hdrCapabilities);
					NVIDIA_API_ERROR_MSG(NVAPI_OK != result, result);
				}
			}

			return result;
		}

		NvAPI_Status TurnOffHDROnDisplay(NvU32 displayId)
		{
			NV_HDR_COLOR_DATA hdrColorData = {};
			hdrColorData.version = NV_HDR_COLOR_DATA_VER; // Latest (e.g. NV_HDR_COLOR_DATA_VE2)
			hdrColorData.cmd = NV_HDR_CMD_SET;
			hdrColorData.static_metadata_descriptor_id = NV_STATIC_METADATA_TYPE_1;
			hdrColorData.hdrMode = NV_HDR_MODE_OFF;
			NvAPI_Status result = NvAPI_Disp_HdrColorControl(displayId, &hdrColorData);
			NVIDIA_API_ERROR_MSG(NVAPI_OK != result, result);

			// Try again with version 1 (probably useless)
			if (result != NVAPI_OK)
			{
				hdrColorData.version = NV_HDR_COLOR_DATA_VER1;
				result = NvAPI_Disp_HdrColorControl(displayId, &hdrColorData);
				NVIDIA_API_ERROR_MSG(NVAPI_OK != result, result);
			}

			return result;
		}

		// Enables HDR on the display, and automatically disables it when the application closes (unless it was already on)
		NvAPI_Status TurnOnHDROnDisplay(NvU32 displayId)
		{
			NV_HDR_COLOR_DATA hdrColorData = {};
			hdrColorData.version = NV_HDR_COLOR_DATA_VER;
			hdrColorData.cmd = NV_HDR_CMD_SET;
			hdrColorData.static_metadata_descriptor_id = NV_STATIC_METADATA_TYPE_1;
			// Described as scRGB HDR by internal comments, but it's referring to the old implementation or the Windows window composition space.
			// This allows both scRGB HDR and HDR10 swapchains, and outputs HDR10 like the DXGI HDR mode (it likely just engages that nowadays).
			hdrColorData.hdrMode = NV_HDR_MODE_UHDA;
		
#if 0 // These are seemingly ignored in HDR10/HDR10+ modes, and are only for DV. Comments about them are confusing though.
			NV_COLOR_DATA colorData = {};
			colorData.version = NV_COLOR_DATA_VER;
			colorData.size = sizeof(colorData);
			colorData.cmd = NV_COLOR_CMD_GET;
			NvAPI_Disp_ColorControl(displayId, &colorData);
			// Preserve the current user values to avoid overriding them for lower quality defaults.
			hdrColorData.hdrColorFormat = (NV_COLOR_FORMAT)colorData.data.colorFormat;
			hdrColorData.hdrDynamicRange = (NV_DYNAMIC_RANGE)colorData.data.dynamicRange;
			hdrColorData.hdrBpc = colorData.data.bpc;
#elif 0
			// Ideally we'd set the highest quality possible for all but there's no easy way to query for it.
			hdrColorData.hdrColorFormat = NV_COLOR_FORMAT_AUTO;
			hdrColorData.hdrDynamicRange = NV_DYNAMIC_RANGE_AUTO;
			hdrColorData.hdrBpc = NV_BPC_DEFAULT;
#endif

			NvAPI_Status result = NvAPI_Disp_HdrColorControl(displayId, &hdrColorData);
			NVIDIA_API_ERROR_MSG(NVAPI_OK != result, result);

			// Try again with version 1 (probably useless)
			// We don't exclusively check "NVAPI_INCOMPATIBLE_STRUCT_VERSION" as we aren't sure it'd always returned
			if (result != NVAPI_OK)
			{
				hdrColorData.version = NV_HDR_COLOR_DATA_VER1;
				result = NvAPI_Disp_HdrColorControl(displayId, &hdrColorData);
				NVIDIA_API_ERROR_MSG(NVAPI_OK != result, result);
			}

			return result;
		}

		bool IsHDR10PlusSupportedOnDisplay(HWND hWnd)
		{
			NvU32 displayId = GetNvapiDisplayIdFromHwnd(hWnd);
			NV_HDR_CAPABILITIES hdrCapabilities = {};
			NvAPI_Status result = GetDisplayCapabilities(displayId, hdrCapabilities);
			NVIDIA_API_ERROR_MSG(result != NVAPI_OK, result);
			return hdrCapabilities.isHdr10PlusGamingSupported;
		}

		bool IsHDR10SupportedOnDisplay(HWND hWnd)
		{
			NvU32 displayId = GetNvapiDisplayIdFromHwnd(hWnd);
			NV_HDR_CAPABILITIES hdrCapabilities = {};
			NvAPI_Status result = GetDisplayCapabilities(displayId, hdrCapabilities);
			NVIDIA_API_ERROR_MSG(result != NVAPI_OK, result);
			return hdrCapabilities.isST2084EotfSupported;
		}

		bool IsHDR10PlusEnabledOnDisplay(HWND hWnd)
		{
			NvU32 displayId = GetNvapiDisplayIdFromHwnd(hWnd);
			NV_DISPLAY_OUTPUT_MODE outputMode = NV_DISPLAY_OUTPUT_MODE_SDR;
			NvAPI_Status result = NvAPI_Disp_GetOutputMode(displayId, &outputMode);
			NVIDIA_API_ERROR_MSG(result != NVAPI_OK, result);
			return outputMode == NV_DISPLAY_OUTPUT_MODE_HDR10PLUS_GAMING;
		}

		bool IsHDR10EnabledOnDisplay(HWND hWnd)
		{
			NvU32 displayId = GetNvapiDisplayIdFromHwnd(hWnd);
			NV_DISPLAY_OUTPUT_MODE outputMode = NV_DISPLAY_OUTPUT_MODE_SDR;
			NvAPI_Status result = NvAPI_Disp_GetOutputMode(displayId, &outputMode);
			NVIDIA_API_ERROR_MSG(result != NVAPI_OK, result);
			// HDR10+ is also HDR10
			return outputMode == NV_DISPLAY_OUTPUT_MODE_HDR10 || outputMode == NV_DISPLAY_OUTPUT_MODE_HDR10PLUS_GAMING;
		}
	
		// Returns the absolute peak brightness of the display the window is contained in.
		// Note that the accuracy of these values is limited, relying on the user calibration from DXGI might be better.
		// Returns <= 0 if HDR10+ is not supported by the display.
		float GetHDR10PlusDisplayPeakBrightness(HWND hWnd, HMONITOR hMonitor = 0)
		{
			// HDR10+ GAMING index to nits mapping, from Samsung docs
			static constexpr std::array<float, 16> nitValues = {
				100.f,  // 0
				200.f,  // 1
				300.f,  // 2
				400.f,  // 3
				500.f,  // 4
				600.f,  // 5
				800.f,  // 6
				1000.f, // 7
				1200.f, // 8
				1500.f, // 9
				2000.f, // 10
				2500.f, // 11
				3000.f, // 12
				4000.f, // 13
				6000.f, // 14
				8000.f  // 15
			};

			NvU32 displayId = GetNvapiDisplayIdFromHwnd(hWnd, hMonitor);
			if (displayId == 0)
				return 0.f;

			NV_HDR_CAPABILITIES hdrCapabilities = {};
			if (GetDisplayCapabilities(displayId, hdrCapabilities) != NVAPI_OK || !hdrCapabilities.isHdr10PlusGamingSupported)
				return 0.f;

			// Always safe
			return nitValues[hdrCapabilities.hdr10plus_vsvdb.peak_luminance_index];
		}

		bool SetDisplayOutputMode(HWND hWnd, NV_DISPLAY_OUTPUT_MODE displayOutputMode, bool allowEnableHDROnDisplay = true, bool allowDisableHDROnDisplay = false)
		{
			if (!hasInit)
				return false;

			NvU32 displayId = GetNvapiDisplayIdFromHwnd(hWnd); // Note: we could force fallback on the primary display here, which would be more likely to be right than not (but not guaranteed)
			if (displayId == 0)
			{
				printf("SetDisplayOutputMode failed to retrieve an NV display ID, likely because the Window isn't on an NV GPU");
				return false;
			}

			bool isHDR = displayOutputMode == NV_DISPLAY_OUTPUT_MODE_HDR10 || displayOutputMode == NV_DISPLAY_OUTPUT_MODE_HDR10PLUS_GAMING;
			bool isHDR10PlusGaming = displayOutputMode == NV_DISPLAY_OUTPUT_MODE_HDR10PLUS_GAMING;

			NvAPI_Status result = NVAPI_OK;

			NV_HDR_CAPABILITIES hdrCapabilities = {};
			result = GetDisplayCapabilities(displayId, hdrCapabilities);

			// Fall back on HDR10 if HDR10+ is not supported on the display.
			// If HDR10 isn't supported, we print an error but we shouldn't have gotten here.
			if (result == NVAPI_OK && isHDR10PlusGaming && !hdrCapabilities.isHdr10PlusGamingSupported)
			{
				isHDR10PlusGaming = false;
				displayOutputMode = NV_DISPLAY_OUTPUT_MODE_HDR10;
			}

			if (isHDR && allowEnableHDROnDisplay)
			{
				result = TurnOnHDROnDisplay(displayId);
			}
			else if (!isHDR && allowDisableHDROnDisplay)
			{
				result = TurnOffHDROnDisplay(displayId);
			}
			// We continue even if we couldn't toggle HDR properly on the display

			// Early out if we were already in the target mode, to avoid the display changing mode more than necessary
			NV_DISPLAY_OUTPUT_MODE currentDisplayOutputMode = NV_DISPLAY_OUTPUT_MODE_SDR;
			result = NvAPI_Disp_GetOutputMode(displayId, &currentDisplayOutputMode);
			if (result == NVAPI_OK && currentDisplayOutputMode == displayOutputMode)
			{
				return true;
			}

			// The output mode seems to be partially be linked with the display HDR state,
			// however if we change the display HDR state from within the application, it's good to also enforce the new output mode.
			// Note: this succeeds over DisplayPort but then doesn't work.
			NV_DISPLAY_OUTPUT_MODE tempDisplayOutputMode = displayOutputMode;
			result = NvAPI_Disp_SetOutputMode(displayId, &tempDisplayOutputMode);
			NVIDIA_API_ERROR_MSG(result != NVAPI_OK, result);
			bool outputModeSetSuccessful = result == NVAPI_OK;

			// Fallback on standard HDR10 if engaging HDR10+ failed for some reason
			if (isHDR10PlusGaming && !outputModeSetSuccessful && result != NVAPI_RESOURCE_IN_USE)
			{
				isHDR10PlusGaming = false;
				displayOutputMode = NV_DISPLAY_OUTPUT_MODE_HDR10;
				tempDisplayOutputMode = displayOutputMode;
				result = NvAPI_Disp_SetOutputMode(displayId, &tempDisplayOutputMode);
				outputModeSetSuccessful = result == NVAPI_OK;
			}

#if 0 // This works but is theoretically only used by GPU side tonemapping. It might be useful to inform the TV we are already tonemapping the game to its capabilities, however this might also have downsides.
			// Cannot continue if we didn't engage HDR10/HDR10+ properly, and nothing more to do in SDR
			if (!outputModeSetSuccessful || !isHDR)
				return true;

			// TODO: is it even useful to set the HDR10 metadata to the exact same HGiG display capabilities (and what the game would tonemap to)?
			if (isHDR10PlusGaming)
			{
				// HDR10+ GAMING index to nits mapping, from Samsung docs
				constexpr std::array<uint16_t, 16> nit_values = {
					100,  // 0
					200,  // 1
					300,  // 2
					400,  // 3
					500,  // 4
					600,  // 5
					800,  // 6
					1000, // 7
					1200, // 8
					1500, // 9
					2000, // 10
					2500, // 11
					3000, // 12
					4000, // 13
					6000, // 14
					8000  // 15
				};
				// TODO: auto calibration (semi accurate)
				hdrCapabilities.hdr10plus_vsvdb.peak_luminance_index;
				hdrCapabilities.hdr10plus_vsvdb.full_frame_peak_luminance_index;
				return nit_values[hdrCapabilities.hdr10plus_vsvdb.peak_luminance_index];

				NV_HDR_METADATA Hdr10MetadataNv = {};
				Hdr10MetadataNv.displayPrimary_x0 = hdrCapabilities.display_data.displayPrimary_x0;
				Hdr10MetadataNv.displayPrimary_y0 = hdrCapabilities.display_data.displayPrimary_y0;
				Hdr10MetadataNv.displayPrimary_x1 = hdrCapabilities.display_data.displayPrimary_x1;
				Hdr10MetadataNv.displayPrimary_y1 = hdrCapabilities.display_data.displayPrimary_y1;
				Hdr10MetadataNv.displayPrimary_x2 = hdrCapabilities.display_data.displayPrimary_x2;
				Hdr10MetadataNv.displayPrimary_y2 = hdrCapabilities.display_data.displayPrimary_y2;
				Hdr10MetadataNv.displayWhitePoint_x = hdrCapabilities.display_data.displayWhitePoint_x;
				Hdr10MetadataNv.displayWhitePoint_y = hdrCapabilities.display_data.displayWhitePoint_y;
				Hdr10MetadataNv.max_display_mastering_luminance = hdrCapabilities.display_data.desired_content_max_luminance;
				Hdr10MetadataNv.min_display_mastering_luminance = hdrCapabilities.display_data.desired_content_min_luminance;
				Hdr10MetadataNv.max_content_light_level = static_cast<NvU16>(hdrCapabilities.display_data.desired_content_max_luminance);
				Hdr10MetadataNv.max_frame_average_light_level = hdrCapabilities.display_data.desired_content_max_frame_average_luminance;
				Hdr10MetadataNv.version = NV_HDR_METADATA_VER;
				result = NvAPI_Disp_SetSourceHdrMetadata(displayId, &Hdr10MetadataNv);
				NVIDIA_API_ERROR_MSG(result != NVAPI_OK, result);
			}
#endif

			return outputModeSetSuccessful;
		}
	}
#endif // ENABLE_NVAPI

	// The new HDR checks are only available from Windows 11 SDK 10.0.26100.0. The code is declared if NTDDI_VERSION is >= NTDDI_WIN11_GA, however the functions likely fail until NTDDI_WIN11_GE (10.0.26100.0).
	// If c++ had "static warning" these would have been one. Disable them locally to fall back on older features that might not work as well.
	#ifndef NTDDI_WIN11_GE
	static_assert(false, "Your Windows SDK is too old and lacks some features to check/engage for HDR on the display. Please upgrade to \"Windows 11 SDK 10.0.26100.0\".");
	#elif NTDDI_VERSION < NTDDI_WIN11_GA
	static_assert(false, "NTDDI_VERSION must be at least NTDDI_WIN11_GA to expose the newer HDR display configuration structures.");
	#endif

	bool GetDisplayConfigPathInfo(HWND hwnd, HMONITOR fallbackMonitor, DISPLAYCONFIG_PATH_INFO& outPathInfo)
	{
		uint32_t pathCount, modeCount;
		if (ERROR_SUCCESS != GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount))
		{
			return false;
		}

		std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
		std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
		// Note: the "/Zc:enumTypes" compiler flag breaks these enums (their padding changes and they end up offsetted)
		if (ERROR_SUCCESS != QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount, modes.data(), nullptr))
		{
			return false;
		}

		// We prefer simply failing than using the closest/primary monitor if the window doesn't overlap any, nor we specify a fallback
		const auto monitorFallbackMode = (hwnd == 0 && fallbackMonitor == 0) ? MONITOR_DEFAULTTOPRIMARY : MONITOR_DEFAULTTONULL;
		HMONITOR targetMonitor = MonitorFromWindow(hwnd, monitorFallbackMode);
		if (targetMonitor == 0)
		{
			targetMonitor = fallbackMonitor;
		}
		for (uint32_t i = 0; i < pathCount; i++)
		{
			auto& pathInfo = paths[i];
			if (pathInfo.flags & DISPLAYCONFIG_PATH_ACTIVE && pathInfo.sourceInfo.statusFlags & DISPLAYCONFIG_SOURCE_IN_USE)
			{
				const bool bVirtual = pathInfo.flags & DISPLAYCONFIG_PATH_SUPPORT_VIRTUAL_MODE;
				const uint32_t modeIndex = bVirtual ? pathInfo.sourceInfo.sourceModeInfoIdx : pathInfo.sourceInfo.modeInfoIdx;
				if (modeIndex == DISPLAYCONFIG_PATH_MODE_IDX_INVALID || modeIndex >= modeCount) continue;
				assert(modes[modeIndex].infoType == DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE);
				const DISPLAYCONFIG_SOURCE_MODE& sourceMode = modes[modeIndex].sourceMode;

				RECT rect{ sourceMode.position.x, sourceMode.position.y, sourceMode.position.x + (LONG)sourceMode.width, sourceMode.position.y + (LONG)sourceMode.height };
				if (!IsRectEmpty(&rect))
				{
					const HMONITOR currentMonitor = MonitorFromRect(&rect, MONITOR_DEFAULTTONULL); // No need to default this to the primary or closest, it should never be a problem
					if (currentMonitor != nullptr && currentMonitor == targetMonitor)
					{
						outPathInfo = pathInfo;
						return true;
					}
				}
			}
		}

		// Note: for now, if we couldn't find the right monitor from the window, we simply return false.
		// If ever necessary, we could force taking the first active path (monitor), increasing the overlap threshold.

		return false;
	}

	bool GetColorInfo(HWND hwnd, DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO& outColorInfo, HMONITOR fallbackMonitor = 0)
	{
		DISPLAYCONFIG_PATH_INFO pathInfo{};
		if (GetDisplayConfigPathInfo(hwnd, fallbackMonitor, pathInfo))
		{
			DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO colorInfo{};
			colorInfo.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO;
			colorInfo.header.size = sizeof(colorInfo);
			colorInfo.header.adapterId = pathInfo.targetInfo.adapterId;
			colorInfo.header.id = pathInfo.targetInfo.id;
			auto result = DisplayConfigGetDeviceInfo(&colorInfo.header);
			if (result == ERROR_SUCCESS)
			{
				outColorInfo = colorInfo;
				return true;
			}
		}
		return false;
	}

	#if defined(NTDDI_WIN11_GE) && NTDDI_VERSION >= NTDDI_WIN11_GA
	bool GetColorInfo2(HWND hwnd, DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO_2& outColorInfo2, HMONITOR fallbackMonitor = 0)
	{
		DISPLAYCONFIG_PATH_INFO pathInfo{};
		if (GetDisplayConfigPathInfo(hwnd, fallbackMonitor, pathInfo))
		{
			DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO_2 colorInfo2{};
			colorInfo2.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO_2;
			colorInfo2.header.size = sizeof(colorInfo2);
			colorInfo2.header.adapterId = pathInfo.targetInfo.adapterId;
			colorInfo2.header.id = pathInfo.targetInfo.id;
			auto result = DisplayConfigGetDeviceInfo(&colorInfo2.header);
			if (result == ERROR_SUCCESS)
			{
				outColorInfo2 = colorInfo2;
				return true;
			}
		}
		return false;
	}
	#endif

	// Pass in the game window (e.g. retrieve it from the swapchain) and an optional fallback monitor. 0 on both to use the primary display.
	// Optionally pass in the swapchain pointer to fall back to checking on the swapchain.
	// If HDR is enabled, it's automatically also supported.
   bool IsHDRSupportedAndEnabled(HWND hwnd /*= 0*/, bool& supported, bool& enabled, IDXGISwapChain3* swapChain = nullptr, HMONITOR fallbackMonitor = 0)
	{
		// Default to not supported for the unknown/failed states
		supported = false;
		enabled = false;

	#if defined(NTDDI_WIN11_GE) && NTDDI_VERSION >= NTDDI_WIN11_GA
		// This will only succeed from Windows 11 24H2
		DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO_2 colorInfo2{};
		if (GetColorInfo2(hwnd, colorInfo2, fallbackMonitor))
		{
			// Note: we don't currently consider "DISPLAYCONFIG_ADVANCED_COLOR_MODE_WCG" as an HDR mode.
			// WCG seemingly allows for a wider color range and bit depth, without a higher brightness peak,
			// it's seemingly true when enabling "Automatically Manage Colors" ("Advanced Color"), in Win 11, and while HDR is disabled.
			// Their documentation also mentions it's display referred, which might be, at least for display transfer.
			// WCG mode could still benefit from running games in HDR mode, but it's probably not worth bothering.
			// Note that this variable can have a small amount of lag compared to the other ones ("DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO_2::highDynamicRangeUserEnabled" in particular).
			enabled = colorInfo2.activeColorMode == DISPLAYCONFIG_ADVANCED_COLOR_MODE_HDR;
			// Verify all other related states are set consistently.
			assert(!enabled || (colorInfo2.advancedColorSupported && !colorInfo2.advancedColorLimitedByPolicy && colorInfo2.highDynamicRangeSupported));
			// "HDR" falls under the umbrella of "Advanced Color" in Windows, thus if advanced color is "blocked" so is HDR (and WCG).
			// This implies we don't need to check for "DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO_2::advancedColorSupported" as checking for HDR support is enough.
			// The "DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO_2::highDynamicRangeUserEnabled" flag, while theoretically should only be true if a user manually enabled HDR on the display,
			// is actually set to true even when HDR is enabled by an app through these functions, so theoretically we could check that too, but it wouldn't be reliable enough and it might change in the future.
			supported = enabled || (colorInfo2.highDynamicRangeSupported && !colorInfo2.advancedColorLimitedByPolicy);
			return true;
		}
	#endif

	#if ENABLE_LEGACY_ADVANCED_COLOR
		// Older Windows versions need to fall back to a simpler implementation.
		// Note: from Windows 11 22H2 (build 22621), "Advanced Color" can also be enabled in SDR, so this returns false positives there (until 24H2, where the check above succeeds).
		DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO colorInfo{};
		if (GetColorInfo(hwnd, colorInfo, fallbackMonitor))
		{
			enabled = colorInfo.advancedColorEnabled;
			assert(!enabled || (colorInfo.advancedColorSupported && !colorInfo.advancedColorForceDisabled));
			supported = enabled || (colorInfo.advancedColorSupported && !colorInfo.advancedColorForceDisabled);
			return true;
		}
	#endif

		if (swapChain)
		{
			com_ptr<IDXGIOutput> output;
			if (SUCCEEDED(swapChain->GetContainingOutput(&output)))
			{
				com_ptr<IDXGIOutput6> output6;
				if (SUCCEEDED(output->QueryInterface(&output6)))
				{
					DXGI_OUTPUT_DESC1 desc1;
					if (SUCCEEDED(output6->GetDesc1(&desc1)))
					{
						// Note: we check for "DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709" (scRGB) even if it's not specified by the documentation.
						// Hopefully this is future proof, and won't cause any damage.
						enabled = desc1.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 || desc1.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;
						supported |= enabled;
					}
				}
			}

			UINT colorSpaceSupported = 0;
         // Note: this function is weird and it will return true in case the swapchain was set to HDR, even if the display actually doesn't support it.
			// It might also not support true sometimes even if the display is in HDR mode.
			if (SUCCEEDED(swapChain->CheckColorSpaceSupport(DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020, &colorSpaceSupported)))
			{
				supported |= colorSpaceSupported & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT;
				colorSpaceSupported = 0;
			}
			// Note that "DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709" doesn't seem to ever be supported on swapchains unless it's currently enabled.
			// Hopefully checking it anyway is future proof, and won't cause any damage.
			if (SUCCEEDED(swapChain->CheckColorSpaceSupport(DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709, &colorSpaceSupported)))
			{
				supported |= colorSpaceSupported & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT;
			}
		}

		return true;
	}

	// Returns true if the display has been successfully set to the target SDR/HDR mode, or if it already was.
	// Returns false in case of an unknown error.
	bool SetHDREnabled(HWND hwnd, bool enabled = true, HMONITOR fallbackMonitor = 0)
	{
	#if defined(NTDDI_WIN11_GE) && NTDDI_VERSION >= NTDDI_WIN11_GA
		// This will only succeed from Windows 11 24H2
		DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO_2 colorInfo2{};
		if (GetColorInfo2(hwnd, colorInfo2, fallbackMonitor))
		{
			if (colorInfo2.highDynamicRangeSupported && (!enabled || !colorInfo2.advancedColorLimitedByPolicy) && (colorInfo2.activeColorMode == DISPLAYCONFIG_ADVANCED_COLOR_MODE_HDR) != enabled)
			{
				DISPLAYCONFIG_SET_HDR_STATE setHDRState{};
				setHDRState.header.type = DISPLAYCONFIG_DEVICE_INFO_SET_HDR_STATE;
				setHDRState.header.size = sizeof(setHDRState);
				setHDRState.header.adapterId = colorInfo2.header.adapterId;
				setHDRState.header.id = colorInfo2.header.id;
				setHDRState.enableHdr = enabled;
				const bool succeeded = (ERROR_SUCCESS == DisplayConfigSetDeviceInfo(&setHDRState.header));
	#ifndef NDEBUG
				// Verify that Windows now reports HDR as enabled (sometimes it might get delayed?).
				// The function above seemingly turns on "DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO_2::highDynamicRangeUserEnabled" too.
				assert(!succeeded || !enabled || !GetColorInfo2(hwnd, colorInfo2, fallbackMonitor) || colorInfo2.activeColorMode == DISPLAYCONFIG_ADVANCED_COLOR_MODE_HDR);
	#endif
				return succeeded;
			}
			return (colorInfo2.activeColorMode == DISPLAYCONFIG_ADVANCED_COLOR_MODE_HDR) == enabled;
		}
	#endif

	#if ENABLE_LEGACY_ADVANCED_COLOR
		// Note: older Windows versions didn't allow to distinguish between HDR and "Advanced Color",
		// so it seems like this possibly has a small chance of breaking your display state until you manually toggle HDR again or change resolution etc.
		// It's not clear if that was a separate issue or if it was caused by a mismatch between HDR and WCG modes.
		DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO colorInfo{};
		if (GetColorInfo(hwnd, colorInfo, fallbackMonitor))
		{
			if (colorInfo.advancedColorSupported && (!enabled || !colorInfo.advancedColorForceDisabled) && static_cast<bool>(colorInfo.advancedColorEnabled) != enabled)
			{
				DISPLAYCONFIG_SET_ADVANCED_COLOR_STATE setAdvancedColorState{};
				setAdvancedColorState.header.type = DISPLAYCONFIG_DEVICE_INFO_SET_ADVANCED_COLOR_STATE;
				setAdvancedColorState.header.size = sizeof(setAdvancedColorState);
				setAdvancedColorState.header.adapterId = colorInfo.header.adapterId;
				setAdvancedColorState.header.id = colorInfo.header.id;
				setAdvancedColorState.enableAdvancedColor = enabled;
				bool succeeded = (ERROR_SUCCESS == DisplayConfigSetDeviceInfo(&setAdvancedColorState.header));
				return succeeded;
			}
			return static_cast<bool>( colorInfo.advancedColorEnabled ) == enabled;
		}
	#endif

		return false;
	}

	// TODO: update every frame
	bool GetSDRWhiteLevel(HWND hwnd, float& nits, HMONITOR fallbackMonitor = 0)
	{
		DISPLAYCONFIG_PATH_INFO pathInfo{};
		if (GetDisplayConfigPathInfo(hwnd, fallbackMonitor, pathInfo))
		{
			DISPLAYCONFIG_SDR_WHITE_LEVEL sdrWhiteLevel = {};
			sdrWhiteLevel.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL;
			sdrWhiteLevel.header.size = sizeof(sdrWhiteLevel);
			sdrWhiteLevel.header.adapterId = pathInfo.targetInfo.adapterId;
			sdrWhiteLevel.header.id = pathInfo.targetInfo.id;
			auto result = DisplayConfigGetDeviceInfo(&sdrWhiteLevel.header);
			if (result == ERROR_SUCCESS)
			{
				nits = (float)sdrWhiteLevel.SDRWhiteLevel / 1000.0f * 80.0f;
				return true;
			}
		}
		return false;
	}

	#define DISPLAYCONFIG_DEVICE_INFO_SET_SDR_WHITE_LEVEL (DISPLAYCONFIG_DEVICE_INFO_TYPE)0xFFFFFFEE
	typedef struct __declspec(align(4)) _DISPLAYCONFIG_SET_SDR_WHITE_LEVEL
	{
		DISPLAYCONFIG_DEVICE_INFO_HEADER header;
		ULONG                            SDRWhiteLevel;
		BYTE                             finalValue;
	} DISPLAYCONFIG_SET_SDR_WHITE_LEVEL;

	// NOTE: Undocumented Windows feature. USE AT YOUR OWN RISK.
	bool SetSDRWhiteLevel(HWND hwnd, float nits = srgb_white_level, HMONITOR fallbackMonitor = 0)
	{
		DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO colorInfo{};
		if (GetColorInfo(hwnd, colorInfo, fallbackMonitor))
		{
			DISPLAYCONFIG_SET_SDR_WHITE_LEVEL setSdrWhiteLevel{};
			setSdrWhiteLevel.header.type = DISPLAYCONFIG_DEVICE_INFO_SET_SDR_WHITE_LEVEL;
			setSdrWhiteLevel.header.size = sizeof(DISPLAYCONFIG_SET_SDR_WHITE_LEVEL);
			setSdrWhiteLevel.header.adapterId = colorInfo.header.adapterId;
			setSdrWhiteLevel.header.id = colorInfo.header.id;
			setSdrWhiteLevel.SDRWhiteLevel = static_cast <ULONG>((1000.0f * nits) / 80.0f);
			setSdrWhiteLevel.finalValue = TRUE;
			// This will return error for any range beyond 80-480 nits
			bool succeeded = (ERROR_SUCCESS == DisplayConfigSetDeviceInfo((DISPLAYCONFIG_DEVICE_INFO_HEADER*)&setSdrWhiteLevel));
			return succeeded;
		}
		return false;
	}

	// Note: these are dynamically loaded as they are only available on recent Windows versions
	using ColorProfileGetDisplayDefaultPtr = HRESULT(WINAPI*)(WCS_PROFILE_MANAGEMENT_SCOPE, LUID, UINT32, COLORPROFILETYPE, COLORPROFILESUBTYPE, LPWSTR*);
	using ColorProfileGetDisplayUserScopePtr = HRESULT(WINAPI*)(LUID, UINT32, WCS_PROFILE_MANAGEMENT_SCOPE*);
	static HMODULE GetMscmsModule()
	{
		// Do it all dynamically so we don't need to import "Mscms.lib"
		static HMODULE MscmsModule = LoadLibraryExW(L"mscms.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
		return MscmsModule;
	}
	static ColorProfileGetDisplayDefaultPtr GetColorProfileGetDisplayDefault()
	{
		#pragma warning(suppress: 4191)
		static ColorProfileGetDisplayDefaultPtr Function = GetMscmsModule() ? reinterpret_cast<ColorProfileGetDisplayDefaultPtr>(GetProcAddress(GetMscmsModule(), "ColorProfileGetDisplayDefault")) : nullptr;
		return Function;
	}
	static ColorProfileGetDisplayUserScopePtr GetColorProfileGetDisplayUserScope()
	{
		#pragma warning(suppress: 4191)
		static ColorProfileGetDisplayUserScopePtr Function = GetMscmsModule() ? reinterpret_cast<ColorProfileGetDisplayUserScopePtr>(GetProcAddress(GetMscmsModule(), "ColorProfileGetDisplayUserScope")) : nullptr;
		return Function;
	}

	bool HasUserHDRColorProfile(HWND hwnd, HMONITOR fallbackMonitor = 0)
	{
		const ColorProfileGetDisplayDefaultPtr GetDisplayDefault = GetColorProfileGetDisplayDefault();
		const ColorProfileGetDisplayUserScopePtr GetDisplayUserScope = GetColorProfileGetDisplayUserScope();
		if (!GetDisplayDefault || !GetDisplayUserScope)
			return false;

		DISPLAYCONFIG_PATH_INFO pathInfo{};
		if (!GetDisplayConfigPathInfo(hwnd, fallbackMonitor, pathInfo))
			return false;

		WCS_PROFILE_MANAGEMENT_SCOPE scope = WCS_PROFILE_MANAGEMENT_SCOPE_SYSTEM_WIDE;
		if (FAILED(GetDisplayUserScope(pathInfo.targetInfo.adapterId, pathInfo.sourceInfo.id, &scope)) || scope != WCS_PROFILE_MANAGEMENT_SCOPE_CURRENT_USER)
			return false;

		PWSTR profileName = nullptr;
		const HRESULT result = GetDisplayDefault(WCS_PROFILE_MANAGEMENT_SCOPE_CURRENT_USER, pathInfo.targetInfo.adapterId, pathInfo.sourceInfo.id, CPT_ICC, CPST_EXTENDED_DISPLAY_COLOR_MODE, &profileName);
		const bool hasProfile = SUCCEEDED(result) && profileName && profileName[0] != L'\0';

		if (profileName)
			LocalFree(profileName);

		return hasProfile;
	}

	// Returns false if failed or if HDR is not engaged (but the white luminance can still be used).
	bool GetHDRMaxLuminance(IDXGISwapChain* swapChain, float& maxLuminance, float defaultMaxLuminance = 80.f /*Windows sRGB standard luminance*/)
	{
		maxLuminance = defaultMaxLuminance;

		com_ptr<IDXGIOutput> output;
		if (FAILED(swapChain->GetContainingOutput(&output)))
		{
			return false;
		}

		com_ptr<IDXGIOutput6> output6;
		if (FAILED(output->QueryInterface(&output6)))
		{
			return false;
		}

		DXGI_OUTPUT_DESC1 outputDesc1;
		if (FAILED(output6->GetDesc1(&outputDesc1)))
		{
			return false;
		}

		// Note: this might end up being outdated if a new display is added/removed,
		// or if HDR is toggled on them after swapchain creation (though it seems to be consistent between SDR and HDR).
		maxLuminance = outputDesc1.MaxLuminance;

#if ENABLE_NVAPI
		// Override the peak brightness from the Windows calibration with the HDR10+ GAMING one, if supported (we automatically engage it when enabling HDR).
		// The DXGI display metadata isn't HDR10+ aware, as HDR10+ is vendor specific.
		// The HDR10+ peak brightness is limited to one of 16 values, however the HDR10+ GAMING mode might assume a signal with a peak matching it, so using it might be safer.
		// If the user has an HDR color profile (e.g. from the Windows HDR Calibration app), we follow that instead, as they might prefer a dimmer presentation.
		// Note: we check "NVAPI::hasInit" instead of calling "NVAPI::Init()" again, to avoid spamming init failures on non Nvidia GPUs.
		if (NVAPI::hasInit && !HasUserHDRColorProfile(0, outputDesc1.Monitor))
		{
			const float hdr10PlusPeakBrightness = NVAPI::GetHDR10PlusDisplayPeakBrightness(0, outputDesc1.Monitor);
			if (hdr10PlusPeakBrightness > 0.f)
			{
				maxLuminance = hdr10PlusPeakBrightness;
			}
		}
#endif

		// HDR is not supported (this only works if HDR is enaged on the monitor that currently contains the swapchain)
		if (outputDesc1.ColorSpace != DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020
			&& outputDesc1.ColorSpace != DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709)
		{
			return false;
		}

		return true;
	}
}