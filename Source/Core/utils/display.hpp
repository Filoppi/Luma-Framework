#pragma once

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
#endif

namespace Display
{
#if ENABLE_NVAPI
	bool InitNVApi()
	{
		NvAPI_Status status = NvAPI_Initialize();
		if (status != NVAPI_OK) {
			NvAPI_ShortString error;
			NvAPI_GetErrorMessage(status, error);
			printf("NVAPI init failed: 0x%x - %s\n", status, error); // Likely a non NV GPU
			return false;
		}

		return true;
	}

	void DeInitNVApi()
	{
		NvAPI_Status status = NvAPI_Unload();
		NVIDIA_API_ERROR_MSG(status != NVAPI_OK, status);
	}

	NvU32 GetNvapiDisplayIdFromHwnd(HWND hWnd, ID3D11Device* device = nullptr)
	{
		HMONITOR hMonitor = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);

		MONITORINFOEX monitorInfo = {};
		monitorInfo.cbSize = sizeof(MONITORINFOEX);
		if (GetMonitorInfo(hMonitor, &monitorInfo))
		{
			std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
			std::string displayName = converter.to_bytes(monitorInfo.szDevice);

			NvU32 displayId = 0;
			if (NvAPI_DISP_GetDisplayIdByDisplayName(displayName.c_str(), &displayId) == NVAPI_OK)
			{
				return displayId;
			}
		}

		return 0;
	}

#if 0 // TODO: delete?
	NvDisplayHandle GetNvapiDisplayFromHwnd(ID3D11Device* device, HWND hWnd)
	{
#if 1
		UINT gpu_index = 0;
		com_ptr<IDXGIDevice> dxgi_device;
		HRESULT hr = device->QueryInterface(&dxgi_device);
		if (SUCCEEDED(hr))
		{
			com_ptr<IDXGIAdapter> adapter;
			hr = dxgi_device->GetAdapter(&adapter);
			if (SUCCEEDED(hr))
			{
#if 0
				DXGI_ADAPTER_DESC adapter_desc;
				hr = adapter->GetDesc(&adapter_desc);
				if (SUCCEEDED(hr)) { }
#endif

				com_ptr<IDXGIFactory7> factory;
				CreateDXGIFactory1(IID_PPV_ARGS(&factory));

				com_ptr<IDXGIAdapter1> enum_adapter;
				while (factory->EnumAdapters1(gpu_index, &enum_adapter) != DXGI_ERROR_NOT_FOUND)
				{
					if (enum_adapter.get() == adapter.get()) break;
					++gpu_index;
				}
			}
		}
		assert(SUCCEEDED(hr));
#endif

		//HMONITOR hMonitor = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
		HMONITOR hMonitor = MonitorFromWindow(0, MONITOR_DEFAULTTOPRIMARY);

#if 0
		MONITORINFOEX monitorInfo = {};
		monitorInfo.cbSize = sizeof(MONITORINFOEX);
		if (GetMonitorInfo(hMonitor, &monitorInfo))
		{
			DISPLAY_DEVICE displayDevice = {};
			displayDevice.cb = sizeof(DISPLAY_DEVICE);
			if (EnumDisplayDevices(monitorInfo.szDevice, 0, &displayDevice, 0))
			{
				NvAPI_SYS_GetDisplayIdFromGpuAndOutputId(gpu_index, )
				return displayDevice.DeviceName; // or DeviceString for a human-readable name
			}
		}
#endif

#if 0
		// Get connected displays to NVidia GPUs
		NvU32 displayIdCount = 0;
		NV_GPU_DISPLAYIDS displayIdArray[NVAPI_MAX_HEADS_PER_GPU] = {};
		displayIdArray[0].version = NV_GPU_DISPLAYIDS_VER;
		NvU32 flags = 0;
		result = NvAPI_GPU_GetConnectedDisplayIds(gpuArray[gpuIndex], nullptr, &displayIdCount, flags);
		NVIDIA_API_ERROR_MSG(result != NVAPI_OK, result);
		result = NvAPI_GPU_GetConnectedDisplayIds(gpuArray[gpuIndex], displayIdArray, &displayIdCount, flags);
		NVIDIA_API_ERROR_MSG(result != NVAPI_OK, result);
		NvAPI_ShortString name = "";

		if (result != NVAPI_OK)
		{
			continue;
		}
		for (NvU32 displayIndex = 0; displayIndex < displayIdCount; ++displayIndex)
		{
			NvDisplayHandle handle = NULL;
			result = NvAPI_EnumNvidiaDisplayHandle(displayIndex, &handle);
			NVIDIA_API_ERROR_MSG(result != NVAPI_OK, result);
			result = NvAPI_GetAssociatedNvidiaDisplayName(handle, name);
			NVIDIA_API_ERROR_MSG(result != NVAPI_OK, result);
			displayInfo.push_back({ displayIdArray[displayIndex].displayId, name });
		}
#endif

		NvDisplayHandle nv_display_handle = 0;
		for (NvU32 i = 0; NvAPI_EnumNvidiaDisplayHandle(i, &nv_display_handle) == NVAPI_OK; ++i)
		{
			// Get Windows display name from NVAPI
			NvAPI_ShortString display_name;
			NvU32 output_id = 0;
			if (NvAPI_GetAssociatedNvidiaDisplayName(nv_display_handle, display_name) == NVAPI_OK)
			{
				// Convert NVAPI display name to an HMONITOR
				DISPLAY_DEVICEA dd = {};
				dd.cb = sizeof(dd);
				if (EnumDisplayDevicesA(display_name, 0, &dd, 0))
				{
					MONITORINFOEXA mi = {};
					mi.cbSize = sizeof(mi);
					if (GetMonitorInfoA(hMonitor, &mi))
					{
						if (_stricmp(mi.szDevice, dd.DeviceName) == 0)
						{
							NvAPI_GetAssociatedDisplayOutputId(nv_display_handle, &output_id);
							break;
						}
					}
				}
			}
		}

		return nv_display_handle;
	}
#endif

	struct DisplayID
	{
		DisplayID(NvU32 _id, const NvAPI_ShortString& _name)
			: id(_id), name()
		{
			strncpy_s(name, _name, NVAPI_SHORT_STRING_MAX);
			for (int i = 0; i < NVAPI_SHORT_STRING_MAX; i++)
			{
				str_name += name[i];
			}
		};

		NvU32 id;
		NvAPI_ShortString name;
		std::string str_name;
	};

	NvAPI_Status CheckIfNvidiaGpu()
	{
		NvAPI_Status result = NVAPI_OK;
		// Get connected NVidia GPUs to Computer
		NvU32 gpuCount = 0;
		NvPhysicalGpuHandle gpuArray[NVAPI_MAX_PHYSICAL_GPUS] = {};
		result = NvAPI_EnumPhysicalGPUs(gpuArray, &gpuCount);
		NVIDIA_API_ERROR_MSG(result != NVAPI_OK, result);
		if (result != NVAPI_OK)
		{
			return result;
		}
		return NVAPI_OK;
	}

	NvAPI_Status GetGpuName(std::string& gpu_name)
	{
		// Get connected NVidia GPUs to Computer
		NvU32 gpuCount = 0;
		NvPhysicalGpuHandle gpuArray[NVAPI_MAX_PHYSICAL_GPUS] = {};
		NvAPI_Status result = NvAPI_EnumPhysicalGPUs(gpuArray, &gpuCount);
		NVIDIA_API_ERROR_MSG(result != NVAPI_OK, result);
		if (result != NVAPI_OK)
		{
			return result;
		}

		NvAPI_ShortString name = "";
		for (NvU32 gpuIndex = 0; gpuIndex < gpuCount; ++gpuIndex)
		{
			NvAPI_GPU_GetFullName(gpuArray[gpuIndex], name);
			int i;
			std::string s = "";
			for (i = 0; i < NVAPI_SHORT_STRING_MAX; i++)
			{
				s = s + name[i];
			}
			gpu_name = s; // TODO: display index...?
		}

		return NVAPI_OK;
	}

	NvAPI_Status GetMonitorIdAndName(OUT std::vector<DisplayID>& displayInfo)
	{
		displayInfo.clear();
		NvAPI_Status result = NVAPI_OK;

		// Get connected NVidia GPUs to Computer
		NvU32 gpuCount = 0;
		NvPhysicalGpuHandle gpuArray[NVAPI_MAX_PHYSICAL_GPUS] = {};
		result = NvAPI_EnumPhysicalGPUs(gpuArray, &gpuCount);
		NVIDIA_API_ERROR_MSG(result != NVAPI_OK, result);
		if (result != NVAPI_OK)
		{
			return result;
		}

		for (NvU32 gpuIndex = 0; gpuIndex < gpuCount; ++gpuIndex)
		{
			// Get connected displays to NVidia GPUs
			NvU32 displayIdCount = 0;
			NV_GPU_DISPLAYIDS displayIdArray[NVAPI_MAX_HEADS_PER_GPU] = {};
			displayIdArray[0].version = NV_GPU_DISPLAYIDS_VER;
			NvU32 flags = 0;
			result = NvAPI_GPU_GetConnectedDisplayIds(gpuArray[gpuIndex], nullptr, &displayIdCount, flags);
			NVIDIA_API_ERROR_MSG(result != NVAPI_OK, result);
			result = NvAPI_GPU_GetConnectedDisplayIds(gpuArray[gpuIndex], displayIdArray, &displayIdCount, flags);
			NVIDIA_API_ERROR_MSG(result != NVAPI_OK, result);
			NvAPI_ShortString name = "";

			if (result != NVAPI_OK)
			{
				continue;
			}
			for (NvU32 displayIndex = 0; displayIndex < displayIdCount; ++displayIndex)
			{
				NvDisplayHandle handle = NULL;
				result = NvAPI_EnumNvidiaDisplayHandle(displayIndex, &handle);
				NVIDIA_API_ERROR_MSG(result != NVAPI_OK, result);
				result = NvAPI_GetAssociatedNvidiaDisplayName(handle, name);
				NVIDIA_API_ERROR_MSG(result != NVAPI_OK, result);
				displayInfo.push_back({ displayIdArray[displayIndex].displayId, name });
			}
		}

		if (displayInfo.empty())
		{
			NVIDIA_API_INFO_MSG("NvApi: No displays obtained from NVidia GPU.");
		}
		else
		{
			NVIDIA_API_INFO_MSG("NvApi: Displays obtained from NVidia GPU.");
		}

		return NVAPI_OK;
	}

	NvAPI_Status GetMonitorId(NvU32& displayId, const std::string& name)
	{
		displayId = 0;
		NvAPI_Status result = NvAPI_DISP_GetDisplayIdByDisplayName(name.c_str(), &displayId);
		NVIDIA_API_ERROR_MSG(NVAPI_OK != result, result);
		return result;
	}

	NvAPI_Status GetMonitorCapabilities( NvU32 displayId, NV_HDR_CAPABILITIES& hdrCapabilities)
	{
		memset(&hdrCapabilities, 0, sizeof(hdrCapabilities));

		hdrCapabilities.version = NV_HDR_CAPABILITIES_VER;
		hdrCapabilities.driverExpandDefaultHdrParameters = 1;
		NvAPI_Status result = NvAPI_Disp_GetHdrCapabilities(displayId, &hdrCapabilities);
		NVIDIA_API_ERROR_MSG(NVAPI_OK != result, result);

		if (result != NVAPI_OK)
		{
			hdrCapabilities.version = NV_HDR_CAPABILITIES_VER2;
			hdrCapabilities.driverExpandDefaultHdrParameters = 1;
			result = NvAPI_Disp_GetHdrCapabilities(displayId, &hdrCapabilities);
			NVIDIA_API_ERROR_MSG(NVAPI_OK != result, result);
		}
		if (result != NVAPI_OK)
		{
			hdrCapabilities.version = NV_HDR_CAPABILITIES_VER1;
			hdrCapabilities.driverExpandDefaultHdrParameters = 1;
			result = NvAPI_Disp_GetHdrCapabilities(displayId, &hdrCapabilities);
			NVIDIA_API_ERROR_MSG(NVAPI_OK != result, result);
		}

		return result;
	}

	NvAPI_Status SetHdr10Metadata(NvU32 displayId, NV_HDR_METADATA& hdr10Metadata)
	{
		hdr10Metadata.version = NV_HDR_METADATA_VER;
		NvAPI_Status result = NvAPI_Disp_SetSourceHdrMetadata(displayId, &hdr10Metadata);
		NVIDIA_API_ERROR_MSG(NVAPI_OK != result, result);
		return result;
	}

	NvAPI_Status TurnOffHdr(NvU32 displayId)
	{
		NV_HDR_COLOR_DATA hdrColorData = {};
		memset(&hdrColorData, 0, sizeof(hdrColorData));

		hdrColorData.version = NV_HDR_COLOR_DATA_VER;
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

	NvAPI_Status TurnOnHdr(NvU32 displayId)
	{
		NV_HDR_COLOR_DATA hdrColorData = {};
		memset(&hdrColorData, 0, sizeof(hdrColorData));

		hdrColorData.version = NV_HDR_COLOR_DATA_VER;
		hdrColorData.cmd = NV_HDR_CMD_SET;
		hdrColorData.static_metadata_descriptor_id = NV_STATIC_METADATA_TYPE_1;
		hdrColorData.hdrMode = NV_HDR_MODE_UHDA; // scRGB HDR in, HDR10 out
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

	NvAPI_Status SetOutputMode(NvU32 displayId, NV_DISPLAY_OUTPUT_MODE mode)
	{
		NvAPI_Status result = NVAPI_OK;
		result = NvAPI_Disp_SetOutputMode(displayId, &mode);
		NVIDIA_API_ERROR_MSG(NVAPI_OK != result, result);
		return result;
	}

	NvAPI_Status GetOutputMode(NvU32 displayId, NV_DISPLAY_OUTPUT_MODE& mode)
	{
		NvAPI_Status result = NVAPI_OK;
		result = NvAPI_Disp_GetOutputMode(displayId, &mode);
		NVIDIA_API_ERROR_MSG(NVAPI_OK != result, result);
		return result;
	}

	NvAPI_Status SetToneMappingMode(NvU32 displayId, NV_HDR_TONEMAPPING_METHOD mode)
	{
		NvAPI_Status result = NVAPI_OK;
		result = NvAPI_Disp_SetHdrToneMapping(displayId, mode);
		NVIDIA_API_ERROR_MSG(NVAPI_OK != result, result);
		return result;
	}

	NvAPI_Status GetToneMappingMode(NvU32 displayId, NV_HDR_TONEMAPPING_METHOD& mode)
	{
		NvAPI_Status result = NVAPI_OK;
		result = NvAPI_Disp_GetHdrToneMapping(displayId, &mode);
		NVIDIA_API_ERROR_MSG(NVAPI_OK != result, result);
		return result;
	}

	NvAPI_Status SetColorSpace(NvU32 displayId, NV_COLORSPACE_TYPE color_space)
	{
		NvAPI_Status result = NVAPI_OK;
		result = NvAPI_Disp_SetSourceColorSpace(displayId, color_space);
		NVIDIA_API_ERROR_MSG(NVAPI_OK != result, result);
		return result;
	}

	NvAPI_Status GetColorSpace(NvU32 displayId, NV_COLORSPACE_TYPE& color_space)
	{
		NvAPI_Status result = NVAPI_OK;
		result = NvAPI_Disp_GetSourceColorSpace(displayId, &color_space, NvU64(GetCurrentProcessId()));
		NVIDIA_API_ERROR_MSG(NVAPI_OK != result, result);
		return result;
	}

	struct DisplayChromaticities
	{
		DisplayChromaticities(float rx,
			float ry,
			float gx,
			float gy,
			float bx,
			float by,
			float wx,
			float wy)
			: redX(rx),
			redY(ry),
			greenX(gx),
			greenY(gy),
			blueX(bx),
			blueY(by),
			whiteX(wx),
			whiteY(wy)
		{
		}

		float redX;
		float redY;
		float greenX;
		float greenY;
		float blueX;
		float blueY;
		float whiteX;
		float whiteY;
	};

	bool IsHdr10PlusDisplayOutput(HWND hWnd)
	{
		NvU32 DisplayId = GetNvapiDisplayIdFromHwnd(hWnd);
		NV_HDR_CAPABILITIES HdrCapabilities = {};
		NvAPI_Status result = GetMonitorCapabilities(DisplayId, HdrCapabilities);
		NVIDIA_API_ERROR_MSG(result != NVAPI_OK, result);
		return HdrCapabilities.isHdr10PlusGamingSupported;
	}

	bool IsHdr10DisplayOutput(HWND hWnd)
	{
		NvU32 DisplayId = GetNvapiDisplayIdFromHwnd(hWnd);
		NV_HDR_CAPABILITIES HdrCapabilities = {};
		NvAPI_Status result = GetMonitorCapabilities(DisplayId, HdrCapabilities);
		NVIDIA_API_ERROR_MSG(result != NVAPI_OK, result);
		return HdrCapabilities.isST2084EotfSupported;
	}

	bool EnableHdr10PlusDisplayOutput(HWND hWnd)
	{
		//TODO: branch out in case of failures
		NvU32 DisplayId = GetNvapiDisplayIdFromHwnd(hWnd);

		NvAPI_Status result = TurnOnHdr(DisplayId);
		NVIDIA_API_ERROR_MSG(result != NVAPI_OK, result);

		result = SetOutputMode(DisplayId, NV_DISPLAY_OUTPUT_MODE_HDR10PLUS_GAMING);
		NVIDIA_API_ERROR_MSG(result != NVAPI_OK, result);

		result = SetToneMappingMode(DisplayId, NV_HDR_TONEMAPPING_GPU);
		//result = SetToneMappingMode(DisplayId, NV_HDR_TONEMAPPING_APP); // Need for GPU?
		NVIDIA_API_ERROR_MSG(result != NVAPI_OK, result);

		result = SetColorSpace(DisplayId, NV_COLORSPACE_REC2100);
		//result = SetColorSpace(DisplayId, NV_COLORSPACE_xRGB); // Try linear scRGB HDR. Seemengly doesn't work
		NVIDIA_API_ERROR_MSG(result != NVAPI_OK, result);

		NV_HDR_CAPABILITIES HdrCapabilities = {};
		result = GetMonitorCapabilities(DisplayId, HdrCapabilities);
		NVIDIA_API_ERROR_MSG(result != NVAPI_OK, result);

		NV_HDR_METADATA Hdr10MetadataNv = {};
		Hdr10MetadataNv.displayPrimary_x0 = HdrCapabilities.display_data.displayPrimary_x0;
		Hdr10MetadataNv.displayPrimary_y0 = HdrCapabilities.display_data.displayPrimary_y0;
		Hdr10MetadataNv.displayPrimary_x1 = HdrCapabilities.display_data.displayPrimary_x1;
		Hdr10MetadataNv.displayPrimary_y1 = HdrCapabilities.display_data.displayPrimary_y1;
		Hdr10MetadataNv.displayPrimary_x2 = HdrCapabilities.display_data.displayPrimary_x2;
		Hdr10MetadataNv.displayPrimary_y2 = HdrCapabilities.display_data.displayPrimary_y2;
		Hdr10MetadataNv.displayWhitePoint_x = HdrCapabilities.display_data.displayWhitePoint_x;
		Hdr10MetadataNv.displayWhitePoint_y = HdrCapabilities.display_data.displayWhitePoint_y;
		Hdr10MetadataNv.max_display_mastering_luminance = HdrCapabilities.display_data.desired_content_max_luminance;
		Hdr10MetadataNv.max_frame_average_light_level = HdrCapabilities.display_data.desired_content_max_frame_average_luminance;
		Hdr10MetadataNv.min_display_mastering_luminance = HdrCapabilities.display_data.desired_content_min_luminance;
		Hdr10MetadataNv.max_content_light_level = static_cast<NvU16>(HdrCapabilities.display_data.desired_content_max_luminance);
		Hdr10MetadataNv.max_display_mastering_luminance = 10000;
		Hdr10MetadataNv.max_frame_average_light_level = 10000;
		Hdr10MetadataNv.min_display_mastering_luminance = 0;
		Hdr10MetadataNv.max_content_light_level = static_cast<NvU16>(10000);
		result = SetHdr10Metadata(DisplayId, Hdr10MetadataNv);
		NVIDIA_API_ERROR_MSG(result != NVAPI_OK, result);

		NV_DISPLAY_OUTPUT_MODE outputMode = NV_DISPLAY_OUTPUT_MODE_SDR;
		result = GetOutputMode(DisplayId, outputMode);
		NVIDIA_API_ERROR_MSG(result != NVAPI_OK, result);
		assert(outputMode == NV_DISPLAY_OUTPUT_MODE_HDR10PLUS_GAMING);

		return (result == NVAPI_OK);
	}

	bool EnableHdr10DisplayOutput(HWND hWnd)
	{
		NvU32 DisplayId = GetNvapiDisplayIdFromHwnd(hWnd);
		NvAPI_Status result = TurnOnHdr(DisplayId);
		result = SetOutputMode(DisplayId, NV_DISPLAY_OUTPUT_MODE_HDR10);
		NVIDIA_API_ERROR_MSG(result != NVAPI_OK, result);
		SetColorSpace(DisplayId, NV_COLORSPACE_REC2100);
		return (result == NVAPI_OK);
	}

	bool DisableHdr10PlusDisplayOutput(HWND hWnd)
	{
		NvU32 DisplayId = GetNvapiDisplayIdFromHwnd(hWnd);
		NvAPI_Status result = SetOutputMode(DisplayId, NV_DISPLAY_OUTPUT_MODE_HDR10);
		NVIDIA_API_ERROR_MSG(result != NVAPI_OK, result);
		return (result == NVAPI_OK);
	}

	bool DisableHdr10DisplayOutput(HWND hWnd)
	{
		NvU32 DisplayId = GetNvapiDisplayIdFromHwnd(hWnd);
		NvAPI_Status result = SetOutputMode(DisplayId, NV_DISPLAY_OUTPUT_MODE_SDR);
		result = TurnOffHdr(DisplayId);
		NVIDIA_API_ERROR_MSG(result != NVAPI_OK, result);
		return (result == NVAPI_OK);
	}

	bool IsHdr10PlusEnabled(HWND hWnd)
	{
		NvU32 DisplayId = GetNvapiDisplayIdFromHwnd(hWnd);
		NV_DISPLAY_OUTPUT_MODE outputMode = NV_DISPLAY_OUTPUT_MODE_SDR;
		NvAPI_Status result = GetOutputMode(DisplayId, outputMode);
		NVIDIA_API_ERROR_MSG(result != NVAPI_OK, result);
		return (outputMode == NV_DISPLAY_OUTPUT_MODE_HDR10PLUS_GAMING);
	}

	bool IsHdr10Enabled(HWND hWnd)
	{
		NvU32 DisplayId = GetNvapiDisplayIdFromHwnd(hWnd);
		NV_DISPLAY_OUTPUT_MODE outputMode = NV_DISPLAY_OUTPUT_MODE_SDR;
		NvAPI_Status result = GetOutputMode(DisplayId, outputMode);
		NVIDIA_API_ERROR_MSG(result != NVAPI_OK, result);
		return (outputMode == NV_DISPLAY_OUTPUT_MODE_HDR10);
	}

	bool SetMetadataHdr(NvU32 DisplayId, NV_HDR_METADATA& HdrMetadata)
	{
		NvAPI_Status result = SetHdr10Metadata(DisplayId, HdrMetadata);
		NVIDIA_API_ERROR_MSG(result != NVAPI_OK, result);
		return (result == NVAPI_OK);
	}

	bool GetHdrCapabilities(NvU32 DisplayId, NV_HDR_CAPABILITIES& HdrCapabilities)
	{
		NvAPI_Status result = GetMonitorCapabilities(DisplayId, HdrCapabilities);
		NVIDIA_API_ERROR_MSG(result != NVAPI_OK, result);
		return (result == NVAPI_OK);
	}

	void SetSDR(NvU32 DisplayId)
	{
		TurnOffHdr(DisplayId);
		SetColorSpace(DisplayId, NV_COLORSPACE_sRGB);
	}
#endif

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

		DXGI_OUTPUT_DESC1 desc1;
		if (FAILED(output6->GetDesc1(&desc1)))
		{
			return false;
		}

		// Note: this might end up being outdated if a new display is added/removed,
		// or if HDR is toggled on them after swapchain creation (though it seems to be consistent between SDR and HDR).
		maxLuminance = desc1.MaxLuminance;

		// HDR is not supported (this only works if HDR is enaged on the monitor that currently contains the swapchain)
		if (desc1.ColorSpace != DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020
			&& desc1.ColorSpace != DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709)
		{
			return false;
		}

		return true;
	}

	#ifndef NTDDI_WIN11_GE
	#define NTDDI_WIN11_GE 0x0A000010
	#endif

	// Only available from Windows 11 SDK 10.0.26100.0
	#if NTDDI_VERSION >= NTDDI_WIN11_GE
	#else
	// If c++ had "static warning" this would have been one.
	static_assert(false, "Your Windows SDK is too old and lacks some features to check/engage for HDR on the display. Either upgrade to \"Windows 11 SDK 10.0.26100.0\" or disable this assert locally (the code will fall back on older features that might not work as well).");
	#endif

	bool GetDisplayConfigPathInfo(HWND hwnd, DISPLAYCONFIG_PATH_INFO& outPathInfo)
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

		const auto monitorFallback = hwnd == 0 ? MONITOR_DEFAULTTOPRIMARY : MONITOR_DEFAULTTONULL; // We prefer simply failing than using the closest/primary monitor if the window doesn't overlap any
		const HMONITOR monitorFromWindow = MonitorFromWindow(hwnd, monitorFallback);
		for (auto& pathInfo : paths)
		{
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
					const HMONITOR monitorFromMode = MonitorFromRect(&rect, MONITOR_DEFAULTTONULL); // No need to default this to the primary or closest, it should never be a problem
					if (monitorFromMode != nullptr && monitorFromMode == monitorFromWindow)
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

	bool GetColorInfo(HWND hwnd, DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO& outColorInfo)
	{
		DISPLAYCONFIG_PATH_INFO pathInfo{};
		if (GetDisplayConfigPathInfo(hwnd, pathInfo))
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

	#if NTDDI_VERSION >= NTDDI_WIN11_GE
	bool GetColorInfo2(HWND hwnd, DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO_2& outColorInfo2)
	{
		DISPLAYCONFIG_PATH_INFO pathInfo{};
		if (GetDisplayConfigPathInfo(hwnd, pathInfo))
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

	// Pass in the game window (e.g. retrieve it from the swapchain), or 0 to fall back on the primary display.
	// Optionally pass in the swapchain pointer to fall back to checking on the swapchain.
	// If HDR is enabled, it's automatically also supported.
   bool IsHDRSupportedAndEnabled(HWND hwnd /*= 0*/, bool& supported, bool& enabled, IDXGISwapChain3* swapChain = nullptr)
	{
		// Default to not supported for the unknown/failed states
		supported = false;
		enabled = false;

	#if NTDDI_VERSION >= NTDDI_WIN11_GE
		// This will only succeed from Windows 11 24H2
		DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO_2 colorInfo2{};
		if (GetColorInfo2(hwnd, colorInfo2))
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

		// Older Windows versions need to fall back to a simpler implementation.
		DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO colorInfo{};
		if (GetColorInfo(hwnd, colorInfo))
		{
			enabled = colorInfo.advancedColorEnabled;
			assert(!enabled || (colorInfo.advancedColorSupported && !colorInfo.advancedColorForceDisabled));
			supported = enabled || (colorInfo.advancedColorSupported && !colorInfo.advancedColorForceDisabled);
			return true;
		}

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

			UINT color_space_supported = 0;
         // Note: this function is weird and it will return true in case the swapchain was set to HDR, even if the display actually doesn't support it
			if (SUCCEEDED(swapChain->CheckColorSpaceSupport(DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020, &color_space_supported)))
			{
				supported |= color_space_supported & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT;
				color_space_supported = 0;
			}
			// Note that "DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709" doesn't seem to ever be supported on swapchains unless it's currently enabled.
			// Hopefully checking it anyway is future proof, and won't cause any damage.
			if (SUCCEEDED(swapChain->CheckColorSpaceSupport(DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709, &color_space_supported)))
			{
				supported |= color_space_supported & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT;
			}
		}

		return false;
	}

	// Returns true if the display has been successfully set to the target SDR/HDR mode, or if it already was.
	// Returns false in case of an unknown error.
	bool SetHDREnabled(HWND hwnd, bool enabled = true)
	{
	#if NTDDI_VERSION >= NTDDI_WIN11_GE
		// This will only succeed from Windows 11 24H2
		DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO_2 colorInfo2{};
		if (GetColorInfo2(hwnd, colorInfo2))
		{
			if (colorInfo2.highDynamicRangeSupported && (!enabled || !colorInfo2.advancedColorLimitedByPolicy) && (colorInfo2.activeColorMode == DISPLAYCONFIG_ADVANCED_COLOR_MODE_HDR) != enabled)
			{
				DISPLAYCONFIG_SET_HDR_STATE setHDRState{};
				setHDRState.header.type = DISPLAYCONFIG_DEVICE_INFO_SET_HDR_STATE;
				setHDRState.header.size = sizeof(setHDRState);
				setHDRState.header.adapterId = colorInfo2.header.adapterId;
				setHDRState.header.id = colorInfo2.header.id;
				setHDRState.enableHdr = enabled;
				enabled = (ERROR_SUCCESS == DisplayConfigSetDeviceInfo(&setHDRState.header));
	#ifndef NDEBUG
				// Verify that Windows reports HDR as enabled by the user, even if it was an application to enable it.
				// The function above seemingly turns on "DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO_2::highDynamicRangeUserEnabled" too.
				assert(!enabled || !GetColorInfo2(hwnd, colorInfo2) || colorInfo2.activeColorMode == DISPLAYCONFIG_ADVANCED_COLOR_MODE_HDR);
	#endif
				return enabled == (bool)setHDRState.enableHdr;
			}
			return (colorInfo2.activeColorMode == DISPLAYCONFIG_ADVANCED_COLOR_MODE_HDR) == enabled;
		}
	#endif

		// Note: older Windows versions didn't allow to distinguish between HDR and "Advanced Color",
		// so it seems like this possibly has a small chance of breaking your display state until you manually toggle HDR again or change resolution etc.
		// It's not clear if that was a separate issue or if it was caused by a mismatch between HDR and WCG modes.
		DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO colorInfo{};
		if (GetColorInfo(hwnd, colorInfo))
		{
			if (colorInfo.advancedColorSupported && (!enabled || !colorInfo.advancedColorForceDisabled) && static_cast<bool>(colorInfo.advancedColorEnabled) != enabled)
			{
				DISPLAYCONFIG_SET_ADVANCED_COLOR_STATE setAdvancedColorState{};
				setAdvancedColorState.header.type = DISPLAYCONFIG_DEVICE_INFO_SET_ADVANCED_COLOR_STATE;
				setAdvancedColorState.header.size = sizeof(setAdvancedColorState);
				setAdvancedColorState.header.adapterId = colorInfo.header.adapterId;
				setAdvancedColorState.header.id = colorInfo.header.id;
				setAdvancedColorState.enableAdvancedColor = enabled;
				enabled = (ERROR_SUCCESS == DisplayConfigSetDeviceInfo(&setAdvancedColorState.header));
				return enabled == (bool)setAdvancedColorState.enableAdvancedColor;
			}
			return static_cast<bool>( colorInfo.advancedColorEnabled ) == enabled;
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
	bool SetSDRWhiteLevel(HWND hwnd, float nits = 80.f /*Windows sRGB standard luminance*/)
	{
		DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO colorInfo{};
		if (GetColorInfo(hwnd, colorInfo))
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
}