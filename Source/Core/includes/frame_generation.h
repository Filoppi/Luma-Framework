#pragma once

#include <cstdint>

#include <d3d12.h>
#include <dxgi1_6.h>

// Frame Generation (FG) is only implemented on top of DX12 (DLSS-G through Streamline, and XeSS-FG),
// DX11 games present through a DX12 swapchain proxy (see "D3D12SwapChainProxy.h"), which runs on the DX11->DX12 interop (see "D3D12Interop.h").
namespace FG
{
	// Note that these don't necessarily match 1:1 with classes, some might be different configurations of the same implementation.
	enum class UserType
	{
		None,
		Auto,
		DLSS_G,
		XeSS_FG,
	};

	// Put these in order of preference (most preferred first), for the automatic selection.
	enum class Type
	{
		DLSS_G,
		XeSS_FG,
		None = -1
	};

	__forceinline bool AreTypesEqual(UserType user_type, Type type)
	{
		switch (user_type)
		{
		case FG::UserType::None:
			return type == Type::None;
		case FG::UserType::Auto:
			return true; // The first supported one is picked (they are in order of preference)
		case FG::UserType::DLSS_G:
			return type == Type::DLSS_G;
		case FG::UserType::XeSS_FG:
			return type == Type::XeSS_FG;
		}
		return false;
	}

	constexpr const char* GetTypeName(Type type)
	{
		switch (type)
		{
		case Type::DLSS_G:
			return "DLSS-G";
		case Type::XeSS_FG:
			return "XeSS-FG";
		case Type::None:
			break;
		}
		return "None";
	}

	// The resources that the game can provide to frame generation, for each frame.
	enum class InputType : uint8_t
	{
		// Required. The depth buffer matching the motion vectors (and of the same size). Can be at render resolution.
		Depth,
		// Required. Dense motion vectors (camera and objects motion), pointing from the current to the previous frame position (same as SR). Usually at render resolution.
		MotionVectors,
		// Optional (but highly suggested). The final scene color with all the post processing, without any UI. Same size, format and encoding as the back buffer.
		HUDless,
		// Optional. UI color (premultiplied by alpha by default) and alpha. It should satisfy "FinalColor.rgb = UI.rgb + (1 - UI.a) * HUDless.rgb". Same size and encoding as the back buffer.
		UI,

		Count
	};

	struct SettingsData
	{
		// Back buffer (output) size and format. These are set by the swapchain proxy.
		unsigned int output_width = 1;
		unsigned int output_height = 1;
		DXGI_FORMAT output_format = DXGI_FORMAT_UNKNOWN;
		// Whether the back buffer is in HDR10 (PQ BT.2020). Note that DLSS-G and XeSS-FG don't support scRGB (FP16) swapchains.
		bool hdr = false;

		// Size of the depth and motion vectors (usually the rendering resolution).
		// In case of dynamic resolution, it's the target res (the mid point one).
		unsigned int render_width = 1;
		unsigned int render_height = 1;
		bool dynamic_resolution = false;

		bool inverted_depth = false;
		bool mvs_jittered = false;
		// Whether the UI color is premultiplied by its alpha (that's what most libraries expect)
		bool ui_premultiplied_alpha = true;

		// How many frames to generate in between rendered frames (e.g. 1 is 2x, 3 is 4x). It's clamped by "InstanceData::max_generated_frames".
		unsigned int generated_frames = 1;

		bool operator==(const SettingsData& other) const = default;
	};

	// Interface to be subclassed. Represents a handle.
	struct InstanceData
	{
		bool is_supported = false;

		// The max value "SettingsData::generated_frames" supports (e.g. 1 means 2x only, 3 means up to 4x)
		unsigned int max_generated_frames = 1;
		// Whether the implementation supports scRGB (R16G16B16A16_FLOAT) swapchains. HDR10 (R10G10B10A2_UNORM with the PQ BT.2020 color space) is always supported.
		bool supports_scrgb_hdr = false;
		unsigned int min_resolution = 1; // Min back buffer width or height

		SettingsData settings_data = {};

		virtual ~InstanceData() {}
	};

	// A DX12 resource provided to FG for the current frame.
	// They are only valid "now" (they are shared with DX11, which will write them again in the next frame), so implementations need to copy them on the command list they are provided with.
	struct Resource
	{
		ID3D12Resource* resource = nullptr;
		// The state the resource is in when the command list provided with it executes (implementations must leave it in this state too)
		D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
		// Valid region of the resource (0 width or height means the whole resource)
		uint32_t left = 0;
		uint32_t top = 0;
		uint32_t width = 0;
		uint32_t height = 0;
	};

	// Per frame camera data.
	// All matrices are row major (DirectX style "row vectors", where "clip = world * view * projection") and must not include any jitter.
	struct FrameConstants
	{
		// The "View" matrix
		float world_to_view[16] = { 1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f };
		// The "Projection" matrix
		float view_to_clip[16] = { 1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f };
		// The previous frame matrices (the previous frame values will be used if these are not provided)
		float prev_world_to_view[16] = { 1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f };
		float prev_view_to_clip[16] = { 1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f };
		// Whether "world_to_view" is valid. If not, the camera is assumed static, and its motion must be fully included in the motion vectors (which is always required anyway).
		bool has_view_matrix = false;
		// Whether "view_to_clip" is valid. If not, a projection matrix will be generated from the vertical FOV, aspect ratio and near/far planes.
		bool has_projection_matrix = false;
		// Whether the "prev_*" matrices are valid
		bool has_prev_matrices = false;

		// In pixel space (from -0.5 to 0.5)
		float jitter_x = 0.f;
		float jitter_y = 0.f;
		// Scale to apply to the motion vectors to have them in pixel space (relative to their texture size), with values pointing to the previous frame position (same as SR).
		float mvs_x_scale = 1.f;
		float mvs_y_scale = 1.f;

		// In view space units (usually meters). These should not be inverted for inverted depth (that's handled by "SettingsData::inverted_depth").
		float near_plane = 0.1f;
		float far_plane = 1000.f;
		// Radians. Only needed if the projection matrix isn't provided.
		float vert_fov = 0.f;
		// Width/Height of the view. Calculated from the render resolution if 0.
		float aspect_ratio = 0.f;

		// Optional camera world space info (otherwise it's extracted from the view matrix, if any)
		bool has_camera_vectors = false;
		float camera_pos[3] = { 0.f, 0.f, 0.f };
		float camera_up[3] = { 0.f, 1.f, 0.f };
		float camera_right[3] = { 1.f, 0.f, 0.f };
		float camera_forward[3] = { 0.f, 0.f, 1.f };

		// Whether the previous frame isn't related to the current one (e.g. camera cut), frame generation will be skipped for this frame.
		bool reset = false;
		// Time it took to render the frame (CPU or GPU), in milliseconds. Optional.
		float frame_time_ms = 0.f;
	};

	struct FrameData
	{
		// Unique (increasing) id of the frame that is about to be presented. It matches the one sent to "BeginFrame()".
		uint32_t frame_id = 0;
		// Resources with a null "resource" have not been provided
		Resource inputs[size_t(InputType::Count)] = {};
		FrameConstants constants = {};
	};

	// Latency markers (Reflex/PCL or XeLL ones).
	enum class LatencyMarker : uint8_t
	{
		SimulationStart,
		SimulationEnd,
		RenderSubmitStart,
		RenderSubmitEnd,
		PresentStart,
		PresentEnd,
	};

	// A frame generation implementation (it owns the swapchain the frames are presented on).
	// The functions are expected to be called from the same thread (the one that presents).
	class FrameGenerationImpl
	{
	public:
		virtual ~FrameGenerationImpl() = default;

		virtual Type GetType() const = 0;

		virtual bool HasInit(const InstanceData* data) const { return data != nullptr; }
		// Needs init to be called first
		virtual bool IsSupported(const InstanceData* data) const { return data != nullptr && data->is_supported; }

		// Must be called once before usage (before the swapchain is created). Still expects Deinit() to be called even if it failed.
		// "adapter" is the adapter the device runs on, "device" is the (native) DX12 device frame generation will run on.
		// Returns whether this FG implementation is supported by hardware and driver.
		// Fill in a data "handle", there can be more than one at a time.
		virtual bool Init(InstanceData*& data, IDXGIAdapter* adapter, ID3D12Device* device) { return false; }
		// Should be called before shutdown or on device destruction (after the swapchain has been destroyed, and the GPU is idle).
		virtual void Deinit(InstanceData*& data) {}

		// Creates the queue that presents (and executes the frame generation work), some implementations need to own its creation.
		// Returns the native queue all the DX12 work will be submitted to (the swapchain might be created with a proxy of it, see "CreateSwapchain()").
		virtual HRESULT CreateCommandQueue(InstanceData* data, ID3D12Device* device, const D3D12_COMMAND_QUEUE_DESC& desc, ID3D12CommandQueue** command_queue) { return device->CreateCommandQueue(&desc, IID_PPV_ARGS(command_queue)); }
		// Creates the (proxy) swapchain frame generation will present through (on the target window), for the queue created by "CreateCommandQueue()".
		virtual HRESULT CreateSwapchain(InstanceData* data, IDXGIFactory2* factory, ID3D12CommandQueue* command_queue, HWND hwnd, const DXGI_SWAP_CHAIN_DESC1& desc, IDXGISwapChain4** swapchain) { return E_NOTIMPL; }
		// Called once all the external references to the swapchain have been released (and the GPU is idle)
		virtual void DestroySwapchain(InstanceData* data) {}
		// Called around "IDXGISwapChain::ResizeBuffers()" (the GPU is idle and no back buffer references are held)
		virtual void OnSwapchainResize(InstanceData* data, bool before) {}

		// Returns whether frame generation can run with these settings
		virtual bool UpdateSettings(InstanceData* data, const SettingsData& settings_data) { return false; }
		// Whether the swapchain needs to be re-created for the latest settings to apply (some settings can only be set on swapchain creation)
		virtual bool NeedsSwapchainRecreation(const InstanceData* data) const { return false; }

		// Called at the beginning of every frame (right after the previous one has been presented), before the CPU starts working on it.
		// Low latency libraries (which are required for frame generation) will usually sleep here.
		virtual void BeginFrame(InstanceData* data, uint32_t frame_id) {}

		// Provides the inputs for the frame that is about to be presented (with the next "Present()" call).
		// Any work can be recorded on the command list, it will be executed before presenting.
		// If "generate" is false (or the inputs were not valid), frame generation is suspended for this frame.
		// Returns whether frames will be generated.
		virtual bool PrepareFrame(InstanceData* data, ID3D12GraphicsCommandList* command_list, const FrameData& frame_data, bool generate) { return false; }
		// Presents the frame (and the generated ones)
		virtual HRESULT Present(InstanceData* data, IDXGISwapChain4* swapchain, uint32_t frame_id, UINT sync_interval, UINT flags) { return swapchain->Present(sync_interval, flags); }

		// How many frames were presented by the last "Present()" call (including the generated ones). Optional, for statistics.
		virtual unsigned int GetLastPresentedFramesCount(const InstanceData* data) const { return 1; }
	};
}
