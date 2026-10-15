#include "DLSS.h"

#if ENABLE_NGX

#include "nvsdk_ngx_helpers.h"

#include "../includes/debug.h"

#include <cstring>
#include <cassert>
#include <unordered_set>
#include <wrl/client.h>
#include <d3d11.h>

// Should be <= the max (last) of NVSDK_NGX_PerfQuality_Value
#define NUM_PERF_QUALITY_MODES 6

namespace NGX
{
	const char* project_id = "d8238c51-1f2f-438d-a309-38c16e33c716"; // This needs to be a GUID. We generated a unique one. This isn't registered by NV. This was generated for Luma. It's ok to share it for all games.
	const char* engine_version = "1.0";

	// DLSS "instance" per output resolution (and other settings)
	// These never need to be manually destroyed
	struct DLSSInternalInstance
	{
		NVSDK_NGX_Handle* super_sampling_feature = nullptr;
		NVSDK_NGX_Parameter* runtime_params = nullptr;
		Microsoft::WRL::ComPtr<ID3D11DeviceContext>	command_list;
	};

	struct DLSSInstanceData : public SR::InstanceData
	{
		DLSSInstanceData()
		{
		}

		DLSSInternalInstance								instance = {}; // Note that there could be more of these if we ever wished
		std::unordered_set<NVSDK_NGX_Handle*>		unique_handles;
		std::unordered_set<NVSDK_NGX_Parameter*>	unique_parameters;
		// Current global capabilities params (independent from the current settings/res).
		NVSDK_NGX_Parameter*								capabilities_params = nullptr;
		Microsoft::WRL::ComPtr<ID3D11Device>		device;

		virtual ~DLSSInstanceData()
		{
			// Just to be explicit
			device.Reset();
			instance.command_list.Reset();

			// We need to release these at the end, otherwise DLSS crashes as it holds references to them (there's probably a way to release them as they come but it doesn't really matter)
			for (NVSDK_NGX_Handle* handle : unique_handles)
			{
				if (handle != nullptr)
				{
					assert(NVSDK_NGX_SUCCEED(NVSDK_NGX_D3D11_ReleaseFeature(handle)));
				}
			}
			for (NVSDK_NGX_Parameter* parameter : unique_parameters)
			{
				if (parameter != nullptr)
				{
					assert(NVSDK_NGX_SUCCEED(NVSDK_NGX_D3D11_DestroyParameters(parameter)));
				}
			}

			if (capabilities_params != nullptr)
			{
				assert(NVSDK_NGX_SUCCEED(NVSDK_NGX_D3D11_DestroyParameters(capabilities_params)));
			}
		}

		// Based on "settings_data" 
		DLSSInternalInstance CreateSuperSamplingFeature(ID3D11DeviceContext* command_list, int quality_value)
		{
			NVSDK_NGX_Parameter* runtime_params = nullptr;
			// Note: this could fail on outdated drivers
			NVSDK_NGX_Result param_result = NVSDK_NGX_D3D11_AllocateParameters(&runtime_params);
			assert(NVSDK_NGX_SUCCEED(param_result));
			if (NVSDK_NGX_FAILED(param_result))
			{
				return DLSSInternalInstance();
			}

			NVSDK_NGX_Handle* feature = nullptr;

			int create_flags = 
				// Always needed unless MVs are in output (upscaled) resolution
				NVSDK_NGX_DLSS_Feature_Flags_MVLowRes
				// DLSS expects the depth to be the device/HW one (1 being near, not 1 being the camera (linear depth)), CryEngine (Prey) and other games use inverted depth because it's better for quality.
				// Depth is always meant to be jittered with DLSS.
				| (settings_data.inverted_depth ? NVSDK_NGX_DLSS_Feature_Flags_DepthInverted : 0)
				| (settings_data.mvs_jittered ? NVSDK_NGX_DLSS_Feature_Flags_MVJittered : 0)
				// We either force the exposure to 1 if we run after tonemapping, or feed the correct one if we run before
				| (settings_data.auto_exposure ? NVSDK_NGX_DLSS_Feature_Flags_AutoExposure : 0)
				// With this flag on, DLSS process colors "better", and it expects them to be in linear space.
				// If this flag is false, then colors should be in gamma space (values beyond 0-1 are allowed anyway, but are not guaranteed to be preserved, especially not below 0).
				| (settings_data.hdr ? NVSDK_NGX_DLSS_Feature_Flags_IsHDR : 0)
				;

			const NVSDK_NGX_PerfQuality_Value perf_quality_value = static_cast<NVSDK_NGX_PerfQuality_Value>(quality_value);

			// DLAA might have been "NVSDK_NGX_PerfQuality_Value_UltraQuality" or "NVSDK_NGX_PerfQuality_Value_MaxQuality" but it shouldn't matter, it's about whether the in/out res are matching.
			// NOTE: we might also want to check against the closest "DLSSOptimalSettingsInfo" for its "Max_width" and "Max_height"
			// to check if we are actually running DLAA or DLSS? It's probably unnecessary.
			const bool is_dlaa = perf_quality_value == NVSDK_NGX_PerfQuality_Value::NVSDK_NGX_PerfQuality_Value_DLAA || (settings_data.render_width >= settings_data.output_width && settings_data.render_height >= settings_data.output_height);
			NVSDK_NGX_DLSS_Hint_Render_Preset render_preset = static_cast<NVSDK_NGX_DLSS_Hint_Render_Preset>(settings_data.render_preset);

			// Set all of them for simplicity, these params belong to a specific quality mode anyway.
			// If we set "NVSDK_NGX_DLSS_Hint_Render_Preset_Default", it should be equal to not setting anything at all.
			NVSDK_NGX_Parameter_SetUI(runtime_params, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_DLAA, render_preset);
			NVSDK_NGX_Parameter_SetUI(runtime_params, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraQuality, render_preset);
			NVSDK_NGX_Parameter_SetUI(runtime_params, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Quality, render_preset);
			NVSDK_NGX_Parameter_SetUI(runtime_params, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Balanced, render_preset);
			NVSDK_NGX_Parameter_SetUI(runtime_params, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Performance, render_preset);
			NVSDK_NGX_Parameter_SetUI(runtime_params, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraPerformance, render_preset);

#if GAME_FF7_REMAKE // TODO: pass in as param
			NVSDK_NGX_Parameter_SetUI(runtime_params, NVSDK_NGX_EParameter_Hint_UseFireflySwatter, 1);
#endif

			NVSDK_NGX_DLSS_Create_Params create_params;
			std::memset(&create_params, 0, sizeof(create_params));

			create_params.Feature.InTargetWidth = settings_data.output_width;
			create_params.Feature.InTargetHeight = settings_data.output_height;
			create_params.Feature.InWidth = settings_data.render_width;
			create_params.Feature.InHeight = settings_data.render_height;
			// The quality value here is optional and likely irrelevant, as we already specify the input and output resolution (that's why we don't hash it in the map key).
			create_params.Feature.InPerfQualityValue = perf_quality_value;
			create_params.InFeatureCreateFlags = create_flags;

			NVSDK_NGX_Result create_result = NGX_D3D11_CREATE_DLSS_EXT(
				command_list,
				&feature,
				runtime_params,
				&create_params);

			// It's possible that DLSS will reject that the "NVSDK_NGX_PerfQuality_Value" parameter, so try again with a different quality mode (they are often meaningless, as what matters is only the resolution).
			if (NVSDK_NGX_FAILED(create_result))
			{
				render_preset = NVSDK_NGX_DLSS_Hint_Render_Preset_Default; // Let's pick the default just to be sure.
				NVSDK_NGX_Parameter_SetUI(runtime_params, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Balanced, render_preset);

				create_params.Feature.InPerfQualityValue = NVSDK_NGX_PerfQuality_Value_Balanced;

				create_result = NGX_D3D11_CREATE_DLSS_EXT(
					command_list,
					&feature,
					runtime_params,
					&create_params);
			}

			// Continue even if we got an error, we handle them later.
			// If this mode creation failed, it's likely it will always fail anyway.
			assert(NVSDK_NGX_SUCCEED(create_result));

			return DLSSInternalInstance{ feature, runtime_params, command_list };
		}
	};
}

bool NGX::DLSS::Init(SR::InstanceData*& data, ID3D11Device* device, IDXGIAdapter* adapter)
{
	if (data)
	{
		Deinit(data); // This will also null the pointer
	}
	
	auto& custom_data = reinterpret_cast<DLSSInstanceData*&>(data);

	// We expect Deinit() to be called first if the device/adapter changed
	if (!custom_data && device)
	{
		const wchar_t* data_path = L"."; // The DLSS DLL should be distributed with Luma and be in the same folder as the mod
		NVSDK_NGX_Result result = NVSDK_NGX_D3D11_Init_with_ProjectID(project_id, NVSDK_NGX_ENGINE_TYPE_CUSTOM, engine_version, data_path, device);

		if (NVSDK_NGX_SUCCEED(result))
		{
			custom_data = new DLSSInstanceData();
			custom_data->device = device;

			custom_data->min_resolution = 32; // DLSS doesn't support output below 32x32

			result = NVSDK_NGX_D3D11_GetCapabilityParameters(&custom_data->capabilities_params);
			assert(NVSDK_NGX_SUCCEED(result));
		}

		if (custom_data && custom_data->capabilities_params != nullptr)
		{
			int super_sampling_available = 0;
			// The documentation mentions to use the "NVSDK_NGX_Parameter_SuperSampling_Available" parameter,
			// but the public Unreal Engine implementation uses this one. It probably makes no difference.
			custom_data->capabilities_params->Get(NVSDK_NGX_EParameter_SuperSampling_Available, &super_sampling_available);

			custom_data->is_supported = super_sampling_available > 0;

#if 0 // This extra check isn't really needed unless we want to know the reason DLSS SR might not be supported
			if (custom_data->is_supported && adapter != nullptr)
			{
				NVSDK_NGX_FeatureDiscoveryInfo featureDiscoveryInfo;
				std::memset(&featureDiscoveryInfo, 0, sizeof(NVSDK_NGX_FeatureDiscoveryInfo));
				featureDiscoveryInfo.SDKVersion = NVSDK_NGX_Version_API;
				featureDiscoveryInfo.FeatureID = NVSDK_NGX_Feature_SuperSampling;
				featureDiscoveryInfo.Identifier.IdentifierType = NVSDK_NGX_Application_Identifier_Type_Project_Id;
				featureDiscoveryInfo.Identifier.v.ProjectDesc.ProjectId = projectID;
				featureDiscoveryInfo.Identifier.v.ProjectDesc.EngineType = NVSDK_NGX_ENGINE_TYPE_CUSTOM;
				featureDiscoveryInfo.Identifier.v.ProjectDesc.EngineVersion = engineVersion;
				featureDiscoveryInfo.ApplicationDataPath = data_path;
				NVSDK_NGX_FeatureRequirement featureRequirement;
				result = NVSDK_NGX_D3D11_GetFeatureRequirements(adapter, &featureDiscoveryInfo, &featureRequirement);
				assert(NVSDK_NGX_SUCCEED(result)); // NOTE: this might fail on AMD if we somehow got here, so maybe we shouldn't assert
				custom_data->is_supported &= NVSDK_NGX_SUCCEED(result) && featureRequirement.FeatureSupported == NVSDK_NGX_Feature_Support_Result::NVSDK_NGX_FeatureSupportResult_Supported;
			}
#endif
		}
	}

	return custom_data != nullptr && custom_data->is_supported;
}

void NGX::DLSS::Deinit(SR::InstanceData*& data, ID3D11Device* optional_device)
{
	auto& custom_data = reinterpret_cast<DLSSInstanceData*&>(data);

	if (custom_data != nullptr)
	{
		if (optional_device == nullptr)
		{
			optional_device = custom_data->device.Get();
		}
		else
		{
			assert(custom_data->device.Get() == optional_device);
		}

		// Needs to be done before "NVSDK_NGX_D3D11_Shutdown1()"
		delete custom_data;
		custom_data = nullptr;

		assert(NVSDK_NGX_SUCCEED(NVSDK_NGX_D3D11_Shutdown1(optional_device)));
	}
}

bool NGX::DLSS::HasInit(const SR::InstanceData* data) const
{
	return data != nullptr;
}

bool NGX::DLSS::IsSupported(const SR::InstanceData* data) const
{
	return data && data->is_supported;
}

int NGX::DLSS::GetJitterPhases(const SR::InstanceData* data) const
{
	// Nvidia suggested formula.
	constexpr int base_phases = SR::GetDefaultJitterPhases(); // We start from a baseline of 8, for native resolution (DLAA).
	// Use Y resolution as it's generally more reliable.
   return std::lrintf(float(base_phases) * pow(max(float(data->settings_data.output_height) / float(data->settings_data.render_height), 1.f), 2.f));
}

bool NGX::DLSS::UpdateSettings(SR::InstanceData* data, ID3D11DeviceContext* command_list, const SR::SettingsData& settings_data)
{
	auto& custom_data = reinterpret_cast<DLSSInstanceData*&>(data);

	// Early exit if DLSS is not supported by hardware or driver.
	if (!command_list || !custom_data || !custom_data->is_supported)
		return false;

#ifndef NDEBUG 
	Microsoft::WRL::ComPtr<ID3D11Device> device;
	command_list->GetDevice(device.GetAddressOf());
	assert(custom_data->device.Get() == device.Get());

	Microsoft::WRL::ComPtr<ID3D11DeviceContext> immediate_context;
	device->GetImmediateContext(&immediate_context);
	assert(immediate_context.Get() == command_list); // DLSS only supports the immediate context apparently (both here and in the actual draw function)!
#endif

	bool feature_instance_created = custom_data->instance.super_sampling_feature != nullptr && custom_data->instance.runtime_params != nullptr;

	// No need to re-instantiate DLSS "features" if all the params are the same
	if (settings_data == custom_data->settings_data
		&& custom_data->instance.command_list.Get() == command_list && feature_instance_created)
	{
		return true;
	}

	int quality_mode = static_cast<int>(NVSDK_NGX_PerfQuality_Value_Balanced); // Default to balanced if none is found

	unsigned int best_mode_delta = (std::numeric_limits<unsigned int>::max)(); // Wrap it around () because "max" might already be defined as macro

	// Instead of first picking a quality mode and then finding the best render resolution for it,
	// we find the most suitable quality mode for the resolutions we fed in.
	for (int i = 0; i < NUM_PERF_QUALITY_MODES; ++i)
	{
		unsigned int optimal_width = 0;
		unsigned int optimal_height = 0;
		unsigned int min_width = 0, max_width = 0, min_height = 0, max_height = 0;
		float sharpness = 0.f; // Unused

		NVSDK_NGX_Result res = NGX_DLSS_GET_OPTIMAL_SETTINGS(custom_data->capabilities_params, settings_data.output_width, settings_data.output_height, static_cast<NVSDK_NGX_PerfQuality_Value>(i), &optimal_width, &optimal_height, &max_width, &max_height, &min_width, &min_height, &sharpness);

		if (NVSDK_NGX_SUCCEED(res) && optimal_width != 0 && optimal_height != 0)
		{
			const bool is_dlaa = static_cast<NVSDK_NGX_PerfQuality_Value>(i) == NVSDK_NGX_PerfQuality_Value::NVSDK_NGX_PerfQuality_Value_DLAA;

			// Just make sure DLSS is always using the full output resolution (it should be, but we never know, DLAA might allow for res inputs higher than outputs in the future)
			if (is_dlaa)
			{
				assert(optimal_width == settings_data.output_width);
				optimal_width = settings_data.output_width;
				optimal_height = settings_data.output_height;
				max_width = settings_data.output_width;
				max_height = settings_data.output_height;
			}
			// This probably can't happen, but I fear I have seen it before, so protect against it
			else if (max_width == 0 || max_height == 0)
			{
				assert(false);
				max_width = optimal_width;
				max_height = optimal_height;
			}

			const unsigned int delta_from_optimal = std::abs((int)settings_data.render_width - (int)optimal_width) + std::abs((int)settings_data.render_height - (int)optimal_height);
			const bool is_in_range = settings_data.render_width >= min_width && settings_data.render_width <= max_width && settings_data.render_height >= min_height && settings_data.render_height <= max_height;

			// Pick the first one with a matching optimal resolution (unless we are doing dynamic resolution, in that case, simply checking for a raw match isn't enough)
			if (!settings_data.dynamic_resolution && optimal_width == settings_data.render_width && optimal_height == settings_data.render_height)
			{
				quality_mode = i;
				break;
			}
			// or fall back on the one cloest to the optimal resolution range
			else if (is_in_range && delta_from_optimal < best_mode_delta)
			{
				quality_mode = i;
				best_mode_delta = delta_from_optimal;
			}
		}
	}

	if ((!settings_data.dynamic_resolution || best_mode_delta == (std::numeric_limits<unsigned int>::max)()) && settings_data.render_width >= settings_data.output_width && settings_data.render_height >= settings_data.output_height)
	{
		assert(quality_mode == NVSDK_NGX_PerfQuality_Value_DLAA);
		quality_mode = NVSDK_NGX_PerfQuality_Value_DLAA; // Just in case (this isn't expected to happen)
	}

	// Release old DLSS instance before creating a new one to prevent memory leaks
	if (custom_data->instance.super_sampling_feature != nullptr)
	{
		NVSDK_NGX_D3D11_ReleaseFeature(custom_data->instance.super_sampling_feature);
		custom_data->unique_handles.erase(custom_data->instance.super_sampling_feature);
	}
	if (custom_data->instance.runtime_params != nullptr)
	{
		NVSDK_NGX_D3D11_DestroyParameters(custom_data->instance.runtime_params);
		custom_data->unique_parameters.erase(custom_data->instance.runtime_params);
	}

	custom_data->settings_data = settings_data;
	custom_data->instance.command_list.Reset(); // Just to be explicit
	custom_data->instance = custom_data->CreateSuperSamplingFeature(command_list, quality_mode);
	custom_data->unique_handles.insert(custom_data->instance.super_sampling_feature);
	custom_data->unique_parameters.insert(custom_data->instance.runtime_params);

	// If any of these are nullptr, then the initialization failed
	return custom_data->instance.super_sampling_feature != nullptr && custom_data->instance.runtime_params != nullptr;
}

bool NGX::DLSS::Draw(const SR::InstanceData* data, ID3D11DeviceContext* command_list, const DrawData& draw_data)
{
	const auto& custom_data = reinterpret_cast<const DLSSInstanceData*&>(data);

	assert(custom_data->is_supported);
	assert(custom_data->instance.super_sampling_feature != nullptr && custom_data->instance.runtime_params != nullptr);
	assert(custom_data->instance.command_list.Get() == command_list);

	NVSDK_NGX_D3D11_DLSS_Eval_Params eval_params;
	memset(&eval_params, 0, sizeof(eval_params));

	auto render_width = draw_data.render_width;
	auto render_height = draw_data.render_height;

	if (render_width == 0)
	{
		render_width = custom_data->settings_data.render_width;
	}
	if (render_height == 0)
	{
		render_height = custom_data->settings_data.render_height;
	}

	eval_params.pInDepth = draw_data.depth_buffer;
	eval_params.pInMotionVectors = draw_data.motion_vectors;
	eval_params.InRenderSubrectDimensions.Width = render_width;
	eval_params.InRenderSubrectDimensions.Height = render_height;
	eval_params.Feature.pInColor = draw_data.source_color;
	eval_params.Feature.pInOutput = draw_data.output_color; // Needs to be a UAV
	eval_params.pInExposureTexture = draw_data.exposure; // Only used in HDR mode. Needs to be a 2D texture.
	if (draw_data.pre_exposure != 0.f)
	{
		eval_params.InPreExposure = draw_data.pre_exposure;
	}
	eval_params.InReset = draw_data.reset ? 1 : 0;
	// MVs need to have positive values when moving towards the top left of the screen
	eval_params.InMVScaleX = custom_data->settings_data.mvs_x_scale;
	eval_params.InMVScaleY = custom_data->settings_data.mvs_y_scale;
	eval_params.InJitterOffsetX = draw_data.jitter_x;
	eval_params.InJitterOffsetY = draw_data.jitter_y;

	NVSDK_NGX_Result result = NGX_D3D11_EVALUATE_DLSS_EXT(
		command_list,
		custom_data->instance.super_sampling_feature,
		custom_data->instance.runtime_params,
		&eval_params
	);

	//ASSERT_MSGF(NVSDK_NGX_SUCCEED(result), "DLSS Error: %u\n", uint32_t(result));

	return NVSDK_NGX_SUCCEED(result);
}

#endif