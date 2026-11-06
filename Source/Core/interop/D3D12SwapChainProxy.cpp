#include "D3D12SwapChainProxy.h"

#if ENABLE_D3D12_SWAPCHAIN_PROXY

#include "../includes/debug.h"

#include <include/reshade.hpp>

#include <d3d11_4.h>
#include <dxgi1_6.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <format>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace D3D12SwapChainProxy
{
   using Microsoft::WRL::ComPtr;

   class DeviceImpl;
   class SwapChainImpl;

   namespace
   {
      // See "com_utils.hpp" in ReShade, querying it from a ReShade proxy returns the original object
      constexpr GUID IID_UnwrappedObject = {0x7f2c9a11, 0x3b4e, 0x4d6a, {0x81, 0x2f, 0x5e, 0x9c, 0xd3, 0x7a, 0x1b, 0x42}};
      // SpecialK, ReShade and Luma store the swapchain color space in this private data (there's no DXGI API to retrieve it)
      constexpr GUID SKID_SwapChainColorSpace = {0x18b57e4, 0x1493, 0x4953, {0xad, 0xf2, 0xde, 0x6d, 0x99, 0xcc, 0x5, 0xe5}};

      constexpr DWORD wait_timeout_ms = 2000; // Same as the interop
      // After how many frames without "PresentBackBuffer()" calls we start presenting the back buffer ourselves
      constexpr uint32_t max_missed_external_presents = 3;

      thread_local uint32_t internal_objects_creation_depth = 0;

      struct ScopedInternalObjectsCreation
      {
         ScopedInternalObjectsCreation() { internal_objects_creation_depth++; }
         ~ScopedInternalObjectsCreation() { internal_objects_creation_depth--; }
      };

      void Log(reshade::log::level level, const std::string& message)
      {
         reshade::log::message(level, ("D3D12 SwapChain Proxy: " + message).c_str());
      }

      // Returns the original object in case it was a ReShade proxy, otherwise returns the object itself (as the requested interface)
      template<typename T>
      ComPtr<T> GetUnwrapped(IUnknown* object, bool* was_wrapped = nullptr)
      {
         ComPtr<T> result;
         if (was_wrapped)
            *was_wrapped = false;
         if (!object)
            return result;
         ComPtr<IUnknown> unwrapped;
         if (SUCCEEDED(object->QueryInterface(IID_UnwrappedObject, reinterpret_cast<void**>(unwrapped.GetAddressOf()))) && unwrapped && SUCCEEDED(unwrapped.As(&result)))
         {
            if (was_wrapped)
               *was_wrapped = true;
            return result;
         }
         object->QueryInterface(IID_PPV_ARGS(&result));
         return result;
      }

      HMODULE GetCurrentModule()
      {
         HMODULE module = nullptr;
         GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(&GetCurrentModule), &module);
         return module;
      }

      using PFN_CreateDXGIFactory2 = HRESULT(WINAPI*)(UINT flags, REFIID riid, void** factory);

      // Flip model swapchains don't support sRGB formats (views can still be sRGB). Returns "DXGI_FORMAT_UNKNOWN" for unsupported formats.
      DXGI_FORMAT GetFlipModelSwapChainFormat(DXGI_FORMAT format)
      {
         switch (format)
         {
         case DXGI_FORMAT_R16G16B16A16_FLOAT:
         case DXGI_FORMAT_R10G10B10A2_UNORM:
         case DXGI_FORMAT_R8G8B8A8_UNORM:
         case DXGI_FORMAT_B8G8R8A8_UNORM:
            return format;
         case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
         case DXGI_FORMAT_R8G8B8A8_TYPELESS:
            return DXGI_FORMAT_R8G8B8A8_UNORM;
         case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
         case DXGI_FORMAT_B8G8R8A8_TYPELESS:
            return DXGI_FORMAT_B8G8R8A8_UNORM;
         case DXGI_FORMAT_R10G10B10A2_TYPELESS:
            return DXGI_FORMAT_R10G10B10A2_UNORM;
         case DXGI_FORMAT_R16G16B16A16_TYPELESS:
            return DXGI_FORMAT_R16G16B16A16_FLOAT;
         default:
            return DXGI_FORMAT_UNKNOWN;
         }
      }

      void TransitionResource(ID3D12GraphicsCommandList* command_list, ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
      {
         D3D12_RESOURCE_BARRIER barrier = {};
         barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
         barrier.Transition.pResource = resource;
         barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
         barrier.Transition.StateBefore = before;
         barrier.Transition.StateAfter = after;
         command_list->ResourceBarrier(1, &barrier);
      }

      //
      // Hidden window
      //

      constexpr wchar_t hidden_window_class_name[] = L"LumaD3D12SwapChainProxyWindow";

      LRESULT CALLBACK HiddenWindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
      {
         switch (msg)
         {
         // The window is never visible, but make sure it never interferes with the game window input
         case WM_NCHITTEST:
            return HTTRANSPARENT;
         case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
         case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
         }
         return DefWindowProcW(hwnd, msg, wparam, lparam);
      }

      std::mutex hidden_windows_mutex;
      std::unordered_map<HWND, HWND> hidden_to_game_windows;

      // A hidden child window of the game window, that hosts the original (DX11) swapchain.
      // It's a child (and not a top level window) so that ReShade input still works (ReShade looks for the child windows of the window that received the input),
      // and it's placed at the origin of the game window client area, with the same size, so coordinates match.
      // It's created on its own thread (with its own message loop), so it always processes its messages, even if the game threads are blocked.
      class HiddenWindow
      {
         // Shared with the window thread, in case it outlives the object (e.g. if the creation timed out)
         struct State
         {
            HANDLE created_event = nullptr;
            std::atomic<HWND> hwnd = nullptr;
            std::atomic<bool> abandoned = false;
            ~State()
            {
               if (created_event)
                  CloseHandle(created_event);
            }
         };

      public:
         static std::unique_ptr<HiddenWindow> Create(HWND game_window, UINT width, UINT height)
         {
            static const ATOM window_class = []()
               {
                  const HINSTANCE instance = GetCurrentModule();
                  // Window classes registered by dlls aren't unregistered when they unload, so a previous instance of this module might have left it registered
                  UnregisterClassW(hidden_window_class_name, instance);
                  WNDCLASSEXW window_class = {};
                  window_class.cbSize = sizeof(window_class);
                  window_class.lpfnWndProc = HiddenWindowProc;
                  window_class.hInstance = instance;
                  window_class.lpszClassName = hidden_window_class_name;
                  return RegisterClassExW(&window_class);
               }();
            if (window_class == 0)
            {
               Log(reshade::log::level::error, "Failed to register the hidden window class");
               return nullptr;
            }

            auto state = std::make_shared<State>();
            state->created_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            if (!state->created_event)
               return nullptr;

            auto window = std::unique_ptr<HiddenWindow>(new HiddenWindow());
            window->state = state;
            window->thread = std::thread([state, game_window, width, height]()
               {
                  // "WS_EX_NOPARENTNOTIFY" avoids the parent having to process "WM_PARENTNOTIFY" (synchronously) on creation and destruction
                  const HWND hwnd = CreateWindowExW(WS_EX_NOPARENTNOTIFY | WS_EX_NOACTIVATE, hidden_window_class_name, L"Luma DX12 SwapChain Proxy", WS_CHILD | WS_DISABLED | WS_CLIPSIBLINGS, 0, 0, (std::max)(width, 1u), (std::max)(height, 1u), game_window, nullptr, GetCurrentModule(), nullptr);
                  state->hwnd = hwnd;
                  SetEvent(state->created_event);
                  if (!hwnd)
                     return;
                  if (state->abandoned)
                  {
                     DestroyWindow(hwnd);
                     return;
                  }
                  MSG msg;
                  while (GetMessageW(&msg, nullptr, 0, 0) > 0)
                  {
                     TranslateMessage(&msg);
                     DispatchMessageW(&msg);
                  }
               });

            const bool created = WaitForSingleObject(state->created_event, wait_timeout_ms) == WAIT_OBJECT_0;
            window->hwnd = state->hwnd;
            if (!created || !window->hwnd)
            {
               state->abandoned = true;
               Log(reshade::log::level::error, "Failed to create the hidden window");
               return nullptr;
            }

            const std::lock_guard lock(hidden_windows_mutex);
            hidden_to_game_windows[window->hwnd] = game_window;
            return window;
         }

         ~HiddenWindow()
         {
            if (hwnd)
            {
               const std::lock_guard lock(hidden_windows_mutex);
               hidden_to_game_windows.erase(hwnd);
            }
            // The window belongs to another thread, ask it to destroy itself (the default "WM_CLOSE" behaviour), which also ends its message loop.
            // If it was created after we gave up waiting for it, the thread will destroy it itself.
            if (const HWND current_hwnd = state->hwnd)
            {
               PostMessageW(current_hwnd, WM_CLOSE, 0, 0);
            }
            if (thread.joinable())
            {
               if (WaitForSingleObject(static_cast<HANDLE>(thread.native_handle()), wait_timeout_ms) == WAIT_OBJECT_0)
                  thread.join();
               else
                  thread.detach(); // Better leak the window than hanging
            }
         }

         void Resize(UINT width, UINT height) const
         {
            if (hwnd)
               SetWindowPos(hwnd, nullptr, 0, 0, (std::max)(width, 1u), (std::max)(height, 1u), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS); // Async, as the window belongs to another thread
         }

         HWND hwnd = nullptr;

      private:
         HiddenWindow() = default;
         std::shared_ptr<State> state;
         std::thread thread;
      };

      // All the proxy devices with proxying enabled (weak references, they are owned by the caller)
      std::mutex devices_mutex;
      std::vector<std::weak_ptr<DeviceImpl>> devices;

      std::shared_ptr<DeviceImpl> FindDeviceForSwapChainCreation(IUnknown* device, bool& device_was_wrapped);
   }

   // All the functions are expected to be called from the game render thread (the one that owns the immediate context and presents), or with "mutex" locked.
   class DeviceImpl final : public Device, public std::enable_shared_from_this<DeviceImpl>
   {
   public:
      ~DeviceImpl() override
      {
         SetProxyEnabled(false);
         if (!D3D12Interop::WaitForIdle(interop) || !WaitForPresentQueueIdle())
         {
            // Same as the other interop users, leak everything instead of risking a crash if the GPU might still be using the resources
            Log(reshade::log::level::warning, "Leaking the DX12 resources, as the GPU might still be using them");
            for (auto& input : inputs)
               new D3D12Interop::SharedTexture(std::move(input.texture));
            new D3D12Interop::SharedTexture(std::move(back_buffer));
            new D3D12Interop::Context(std::move(interop));
            return;
         }
         for (auto& input : inputs)
            input.texture = {};
         back_buffer = {};
         dxgi_factory.Reset();
         idle_fence.Reset();
         if (idle_fence_event)
            CloseHandle(idle_fence_event);
         D3D12Interop::Deinit(interop);
      }

      bool Initialize(ID3D11Device* native_device, std::function<ID3D11ComputeShader*()> get_depth_copy_shader)
      {
         HRESULT hr = native_device->QueryInterface(IID_PPV_ARGS(&d3d11_device));
         if (FAILED(hr))
            return false;
         ComPtr<ID3D11DeviceContext> immediate_context;
         native_device->GetImmediateContext(&immediate_context);
         immediate_context.As(&d3d11_context);
         if (!d3d11_context)
         {
            Log(reshade::log::level::error, "ID3D11DeviceContext4 is not supported");
            return false;
         }

         {
            ScopedInternalObjectsCreation internal_objects_creation;
            if (!D3D12Interop::Init(interop, native_device))
               return false;
         }
         interop.get_depth_copy_shader = std::move(get_depth_copy_shader);

         const auto create_factory = reinterpret_cast<PFN_CreateDXGIFactory2>(GetProcAddress(LoadLibraryW(L"dxgi.dll"), "CreateDXGIFactory2")); // Never freed, it's a system dll
         ComPtr<IDXGIFactory2> created_factory;
         hr = create_factory ? create_factory(0, IID_PPV_ARGS(&created_factory)) : E_FAIL;
         if (FAILED(hr))
         {
            Log(reshade::log::level::error, std::format("Failed to create the DXGI factory, hr = {:#010x}", unsigned(hr)));
            return false;
         }
         dxgi_factory = GetUnwrapped<IDXGIFactory2>(created_factory.Get());
         if (!DXGIHooks::InitFactory(dxgi_factory.Get()))
         {
            Log(reshade::log::level::error, "Failed to hook the DXGI factory");
            return false;
         }
         ComPtr<IDXGIFactory5> dxgi_factory_5;
         BOOL allow_tearing = FALSE;
         if (SUCCEEDED(dxgi_factory.As(&dxgi_factory_5)) && SUCCEEDED(dxgi_factory_5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allow_tearing, sizeof(allow_tearing))))
            tearing_supported = allow_tearing != FALSE;

         // To know when the queue finished presenting (the interop fence only tracks our command lists, and it's shared with DX11)
         hr = interop.device_12->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&idle_fence));
         idle_fence_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
         return SUCCEEDED(hr) && idle_fence_event;
      }

      bool WaitForPresentQueueIdle()
      {
         if (!idle_fence || !interop.command_queue)
            return true;
         const UINT64 value = ++idle_fence_value;
         if (FAILED(interop.command_queue->Signal(idle_fence.Get(), value)))
            return false;
         if (idle_fence->GetCompletedValue() >= value)
            return true;
         if (FAILED(idle_fence->SetEventOnCompletion(value, idle_fence_event)))
            return false;
         return WaitForSingleObject(idle_fence_event, wait_timeout_ms) == WAIT_OBJECT_0 || FAILED(interop.device_12->GetDeviceRemovedReason());
      }

      // Everything we submitted to the queue (our copies and the presents) is done
      bool WaitForIdle()
      {
         return D3D12Interop::WaitForIdle(interop) && WaitForPresentQueueIdle();
      }

      // Builds the frame generation data for the current frame
      void BuildFrameData(FG::FrameData& frame_data, FG::SettingsData& settings_data, bool& valid)
      {
         frame_data.frame_id = frame_id;
         for (size_t i = 0; i < size_t(FG::InputType::Count); i++)
         {
            const InputData& input = inputs[i];
            if (input.ready && input.texture.resource_12)
            {
               FG::Resource& resource = frame_data.inputs[i];
               resource.resource = input.texture.resource_12.Get();
               resource.state = D3D12_RESOURCE_STATE_COMMON; // Shared textures are in the common state at the beginning and end of every DX12 command list
               resource.left = input.left;
               resource.top = input.top;
               resource.width = input.width != 0 ? input.width : input.texture.width;
               resource.height = input.height != 0 ? input.height : input.texture.height;
            }
         }

         const FG::Resource& depth = frame_data.inputs[size_t(FG::InputType::Depth)];
         const FG::Resource& motion_vectors = frame_data.inputs[size_t(FG::InputType::MotionVectors)];
         valid = depth.resource != nullptr && motion_vectors.resource != nullptr && frame_constants_set;

         settings_data = fg_settings;
         if (settings_data.render_width <= 1 || settings_data.render_height <= 1)
         {
            settings_data.render_width = (std::max)(depth.width, 1u);
            settings_data.render_height = (std::max)(depth.height, 1u);
         }

         frame_data.constants = frame_constants;
         // Fill in the previous frame matrices if the game didn't
         if (!frame_data.constants.has_prev_matrices)
         {
            const FG::FrameConstants& source = prev_frame_constants_valid ? prev_frame_constants : frame_constants;
            std::memcpy(frame_data.constants.prev_world_to_view, source.world_to_view, sizeof(frame_data.constants.prev_world_to_view));
            std::memcpy(frame_data.constants.prev_view_to_clip, source.view_to_clip, sizeof(frame_data.constants.prev_view_to_clip));
            frame_data.constants.has_prev_matrices = true;
         }
         if (frame_data.constants.aspect_ratio <= 0.f)
            frame_data.constants.aspect_ratio = float(settings_data.render_width) / float(settings_data.render_height);
         // Generating frames after not having generated them for a while, the history can't be trusted
         if (!generated_last_frame)
            frame_data.constants.reset = true;
      }

      // Called after the frame has been presented (by the game facing swapchain)
      void EndFrame(bool generated)
      {
         generated_last_frame = generated;
         prev_frame_constants = frame_constants;
         prev_frame_constants_valid = frame_constants_set;
         frame_constants_set = false;
         for (auto& input : inputs)
            input.ready = false;

         frame_id++;
         frame_begun = false;
         BeginFrame();
      }

      // Notifies the beginning of a new frame to frame generation (low latency sleep etc)
      void BeginFrame()
      {
         if (frame_begun)
            return;
         frame_begun = true;
         if (fg_implementation && fg_instance_data && fg_instance_data->is_supported && !swapchains.empty())
            fg_implementation->BeginFrame(fg_instance_data, frame_id);
      }

      HRESULT CreateProxySwapChain(HWND game_window, UINT width, UINT height, bool original_is_reshade_proxy, const std::function<HRESULT(HWND window, UINT width, UINT height, IDXGISwapChain** swapchain)>& create_original, IDXGISwapChain** swapchain);

      void RegisterSwapChain(SwapChainImpl* swapchain)
      {
         const std::lock_guard lock(mutex);
         swapchains.push_back(swapchain);
      }
      void UnregisterSwapChain(SwapChainImpl* swapchain)
      {
         const std::lock_guard lock(mutex);
         std::erase(swapchains, swapchain);
      }

      //
      // Device interface
      //

      ID3D12Device* GetD3D12Device() const override { return interop.device_12.Get(); }
      IDXGIAdapter* GetAdapter() const override { return interop.adapter.Get(); }

      bool SetFrameGeneration(FG::FrameGenerationImpl* implementation, FG::InstanceData* instance_data) override
      {
         const std::lock_guard lock(mutex);
         if (!swapchains.empty() || fg_implementation || !implementation || !instance_data)
         {
            ASSERT_ONCE(false); // Only supported once, before creating swapchains
            return false;
         }
         // The queue created by the interop hasn't been used yet, replace it with the one frame generation wants to present with
         D3D12_COMMAND_QUEUE_DESC queue_desc = {};
         queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
         ComPtr<ID3D12CommandQueue> command_queue;
         HRESULT hr;
         {
            ScopedInternalObjectsCreation internal_objects_creation;
            hr = implementation->CreateCommandQueue(instance_data, interop.device_12.Get(), queue_desc, &command_queue);
         }
         if (FAILED(hr) || !command_queue)
         {
            Log(reshade::log::level::error, std::format("Failed to create the {} command queue, hr = {:#010x}", FG::GetTypeName(implementation->GetType()), unsigned(hr)));
            return false;
         }
         interop.command_queue = command_queue;
         fg_implementation = implementation;
         fg_instance_data = instance_data;
         return true;
      }

      void SetProxyEnabled(bool enabled) override;

      SwapChain* FindSwapChain(IDXGISwapChain* swapchain) const override;
      SwapChain* GetSwapChain() const override;

      void SetFrameGenerationSettings(const FG::SettingsData& settings_data, bool enable) override
      {
         const std::lock_guard lock(mutex);
         fg_settings = settings_data;
         fg_enabled = enable;
      }

      bool SetFrameInput(ID3D11DeviceContext* immediate_context, FG::InputType type, ID3D11Resource* resource, uint32_t left, uint32_t top, uint32_t width, uint32_t height) override
      {
         const std::lock_guard lock(mutex);
         if (type >= FG::InputType::Count || !resource || !immediate_context || immediate_context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE || interop.poisoned)
         {
            ASSERT_ONCE(false);
            return false;
         }
         InputData& input = inputs[size_t(type)];
         input.ready = false;
         if (type == FG::InputType::Depth)
         {
            if (!D3D12Interop::UpdateSharedTextureFrom(interop, input.texture, resource, true, true) || !D3D12Interop::CopyDepth(interop, input.texture, immediate_context, resource))
               return false;
         }
         else
         {
            // "D3D12Interop::UpdateSharedTextureFrom()" doesn't support sRGB, we just copy the data into the non sRGB format, as that's how the DX12 swapchain sees it
            ComPtr<ID3D11Texture2D> texture;
            if (FAILED(resource->QueryInterface(IID_PPV_ARGS(&texture))))
               return false;
            D3D11_TEXTURE2D_DESC desc;
            texture->GetDesc(&desc);
            if (desc.MipLevels != 1 || desc.ArraySize != 1 || desc.SampleDesc.Count != 1)
            {
               ASSERT_ONCE_MSG(false, "Unsupported Frame Generation input texture");
               return false;
            }
            const DXGI_FORMAT format = DXGI_FORMAT(reshade::api::format_to_default_typed(reshade::api::format(desc.Format), 0));
            if (!D3D12Interop::UpdateSharedTexture(interop, input.texture, desc.Width, desc.Height, format, false))
               return false;
            immediate_context->CopyResource(input.texture.resource_11.Get(), resource);
         }
         input.ready = true;
         input.left = left;
         input.top = top;
         input.width = width;
         input.height = height;
         return true;
      }

      void SetFrameConstants(const FG::FrameConstants& constants) override
      {
         const std::lock_guard lock(mutex);
         frame_constants = constants;
         frame_constants_set = true;
      }

      uint32_t GetFrameId() const override { return frame_id; }

      struct InputData
      {
         D3D12Interop::SharedTexture texture;
         bool ready = false;
         uint32_t left = 0;
         uint32_t top = 0;
         uint32_t width = 0;
         uint32_t height = 0;
      };

      ComPtr<ID3D11Device> d3d11_device;
      ComPtr<ID3D11DeviceContext4> d3d11_context; // Immediate context
      D3D12Interop::Context interop; // Its queue is the one we present with
      ComPtr<IDXGIFactory2> dxgi_factory; // Native
      bool tearing_supported = false;
      ComPtr<ID3D12Fence> idle_fence;
      UINT64 idle_fence_value = 0;
      HANDLE idle_fence_event = nullptr;

      D3D12Interop::SharedTexture back_buffer;
      InputData inputs[size_t(FG::InputType::Count)];

      mutable std::recursive_mutex mutex;
      std::vector<SwapChainImpl*> swapchains; // The alive ones (they own a reference to this device)

      FG::FrameGenerationImpl* fg_implementation = nullptr;
      FG::InstanceData* fg_instance_data = nullptr;
      FG::SettingsData fg_settings = {};
      bool fg_enabled = false;

      uint32_t frame_id = 1; // Some libraries don't like 0
      bool frame_begun = false;
      bool generated_last_frame = false;
      FG::FrameConstants frame_constants = {};
      bool frame_constants_set = false;
      FG::FrameConstants prev_frame_constants = {};
      bool prev_frame_constants_valid = false;
   };

   // The swapchain the game receives (and uses), it wraps the original (DX11) one, that presents on a hidden window, and presents through a DX12 swapchain on the game window.
   // "original_is_reshade_proxy" means that ReShade is beneath us (it wraps the original swapchain), otherwise ReShade will wrap this one (it depends on which hook is called first), or isn't there.
   class DECLSPEC_UUID("4f5d1e3a-7b2c-4f61-9a8e-2d6c0b1e9f74") SwapChainImpl final : public SwapChain, public IDXGISwapChain4
   {
   public:
      SwapChainImpl(std::shared_ptr<DeviceImpl> _device, ComPtr<IDXGISwapChain> _original, std::unique_ptr<HiddenWindow> _hidden_window, HWND _game_window, bool _original_is_reshade_proxy)
         : device(std::move(_device)), original(std::move(_original)), hidden_window(std::move(_hidden_window)), game_window(_game_window), original_is_reshade_proxy(_original_is_reshade_proxy)
      {
         original.As(&original_1);
         original.As(&original_2);
         original.As(&original_3);
         original.As(&original_4);
         // The native swapchain is what ReShade exposes to addons (if ReShade is beneath us)
         original_native = GetUnwrapped<IDXGISwapChain>(original.Get());

         fg_implementation = device->fg_implementation;
         fg_instance_data = device->fg_instance_data;

         device->RegisterSwapChain(this);
      }

      ~SwapChainImpl() override
      {
         const std::lock_guard lock(device->mutex);
         DestroyD3D12SwapChain();
         device->UnregisterSwapChain(this);
         original_4.Reset();
         original_3.Reset();
         original_2.Reset();
         original_1.Reset();
         original_native.Reset();
         original.Reset(); // This triggers the ReShade swapchain destruction events, if ReShade is beneath us
         hidden_window.reset();
      }

      HRESULT CreateD3D12SwapChain()
      {
         DXGI_SWAP_CHAIN_DESC original_desc = {};
         HRESULT hr = original->GetDesc(&original_desc);
         if (FAILED(hr))
            return hr;

         const DXGI_FORMAT format = GetFlipModelSwapChainFormat(original_desc.BufferDesc.Format);
         if (format == DXGI_FORMAT_UNKNOWN)
         {
            Log(reshade::log::level::error, std::format("Unsupported swapchain format {}", int(original_desc.BufferDesc.Format)));
            return E_INVALIDARG;
         }

         DXGI_SWAP_CHAIN_DESC1 desc = {};
         desc.Width = original_desc.BufferDesc.Width;
         desc.Height = original_desc.BufferDesc.Height;
         desc.Format = format;
         desc.SampleDesc.Count = 1;
         desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
         desc.BufferCount = std::clamp(original_desc.BufferCount, 2u, 8u);
         desc.Scaling = DXGI_SCALING_STRETCH;
         desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
         desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
         // We don't use "DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT", frame generation libraries pace frames themselves (the game's waitable object is the one of the original swapchain)
         desc.Flags = device->tearing_supported ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

         ComPtr<IDXGISwapChain4> new_swapchain;
         {
            ScopedInternalObjectsCreation internal_objects_creation;
            if (fg_implementation && fg_instance_data && fg_instance_data->is_supported)
            {
               hr = fg_implementation->CreateSwapchain(fg_instance_data, device->dxgi_factory.Get(), device->interop.command_queue.Get(), game_window, desc, &new_swapchain);
               if (FAILED(hr) || !new_swapchain)
               {
                  Log(reshade::log::level::error, std::format("Failed to create the {} swapchain, hr = {:#010x}, falling back to a plain DX12 swapchain", FG::GetTypeName(fg_implementation->GetType()), unsigned(hr)));
                  new_swapchain.Reset();
                  fg_implementation = nullptr;
                  fg_instance_data = nullptr;
               }
            }
            if (!new_swapchain)
            {
               ComPtr<IDXGISwapChain1> swapchain_1;
               hr = device->dxgi_factory->CreateSwapChainForHwnd(device->interop.command_queue.Get(), game_window, &desc, nullptr, nullptr, &swapchain_1);
               if (SUCCEEDED(hr))
                  hr = swapchain_1.As(&new_swapchain);
            }
         }
         if (FAILED(hr) || !new_swapchain)
         {
            Log(reshade::log::level::error, std::format("Failed to create the DX12 swapchain, hr = {:#010x}", unsigned(hr)));
            return FAILED(hr) ? hr : E_FAIL;
         }

         // Prevent DXGI from changing the window (e.g. alt+enter for exclusive fullscreen), the game swapchain is the one that controls these
         ComPtr<IDXGIFactory1> parent_factory;
         if (SUCCEEDED(new_swapchain->GetParent(IID_PPV_ARGS(&parent_factory))))
            parent_factory->MakeWindowAssociation(game_window, DXGI_MWA_NO_WINDOW_CHANGES | DXGI_MWA_NO_ALT_ENTER);

         d3d12_swapchain = new_swapchain;
         d3d12_desc = desc;
         color_space_set = false;
         SyncColorSpace();
         return S_OK;
      }

      void DestroyD3D12SwapChain()
      {
         if (!d3d12_swapchain)
            return;
         device->WaitForIdle();
         d3d12_swapchain.Reset();
         if (fg_implementation && fg_instance_data)
            fg_implementation->DestroySwapchain(fg_instance_data);
      }

      bool RecreateD3D12SwapChain()
      {
         DestroyD3D12SwapChain();
         return SUCCEEDED(CreateD3D12SwapChain());
      }

      // Copies the color space set by the game (or Luma) on the original swapchain, given that's the one they see
      void SyncColorSpace()
      {
         if (!d3d12_swapchain)
            return;
         DXGI_COLOR_SPACE_TYPE original_color_space = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
         UINT size = sizeof(original_color_space);
         IDXGISwapChain* const source = original_native ? original_native.Get() : original.Get();
         if (FAILED(source->GetPrivateData(SKID_SwapChainColorSpace, &size, &original_color_space)) || size != sizeof(original_color_space))
         {
            // Fall back on the default color space for the format
            original_color_space = d3d12_desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT ? DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709 : DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
         }
         ApplyColorSpace(original_color_space);
      }

      void ApplyColorSpace(DXGI_COLOR_SPACE_TYPE new_color_space)
      {
         if (!d3d12_swapchain || (color_space_set && new_color_space == color_space))
            return;
         // Set it as done even if it failed, to avoid trying again every frame
         color_space = new_color_space;
         color_space_set = true;
         UINT support = 0;
         if (SUCCEEDED(d3d12_swapchain->CheckColorSpaceSupport(new_color_space, &support)) && (support & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT) != 0)
            d3d12_swapchain->SetColorSpace1(new_color_space);
      }

      // Resizes the DX12 swapchain to match the original one (whether it was resized by the game or not)
      bool ResizeD3D12SwapChain()
      {
         if (!d3d12_swapchain)
            return false;
         DXGI_SWAP_CHAIN_DESC original_desc = {};
         if (FAILED(original->GetDesc(&original_desc)))
            return false;
         const DXGI_FORMAT format = GetFlipModelSwapChainFormat(original_desc.BufferDesc.Format);
         if (format == DXGI_FORMAT_UNKNOWN)
            return false;
         if (hidden_window)
            hidden_window->Resize(original_desc.BufferDesc.Width, original_desc.BufferDesc.Height);
         if (original_desc.BufferDesc.Width == d3d12_desc.Width && original_desc.BufferDesc.Height == d3d12_desc.Height && format == d3d12_desc.Format)
            return true;

         device->WaitForIdle();
         if (fg_implementation && fg_instance_data)
            fg_implementation->OnSwapchainResize(fg_instance_data, true);
         HRESULT hr;
         {
            ScopedInternalObjectsCreation internal_objects_creation;
            hr = d3d12_swapchain->ResizeBuffers(d3d12_desc.BufferCount, original_desc.BufferDesc.Width, original_desc.BufferDesc.Height, format, d3d12_desc.Flags);
         }
         if (fg_implementation && fg_instance_data)
            fg_implementation->OnSwapchainResize(fg_instance_data, false);
         if (FAILED(hr))
         {
            Log(reshade::log::level::error, std::format("Failed to resize the DX12 swapchain, hr = {:#010x}", unsigned(hr)));
            return false;
         }
         d3d12_desc.Width = original_desc.BufferDesc.Width;
         d3d12_desc.Height = original_desc.BufferDesc.Height;
         d3d12_desc.Format = format;
         // Changing format might reset the color space
         color_space_set = false;
         SyncColorSpace();
         return true;
      }

      // Copies the back buffer to the DX12 swapchain and presents it (once per frame)
      void PresentInternal(ID3D11Resource* back_buffer)
      {
         if (d3d12_presented || !d3d12_swapchain || !back_buffer || device->interop.poisoned)
            return;

         device->BeginFrame(); // In case this is the first frame

         ComPtr<ID3D11Texture2D> back_buffer_texture;
         if (FAILED(back_buffer->QueryInterface(IID_PPV_ARGS(&back_buffer_texture))))
            return;
         D3D11_TEXTURE2D_DESC back_buffer_desc;
         back_buffer_texture->GetDesc(&back_buffer_desc);
         const DXGI_FORMAT format = GetFlipModelSwapChainFormat(back_buffer_desc.Format);
         if (format == DXGI_FORMAT_UNKNOWN || back_buffer_desc.SampleDesc.Count != 1)
         {
            ASSERT_ONCE(false);
            return;
         }
         // The game swapchain might have been resized without us knowing about it (if ReShade wraps this swapchain, or the format was changed by Luma)
         if (back_buffer_desc.Width != d3d12_desc.Width || back_buffer_desc.Height != d3d12_desc.Height || format != d3d12_desc.Format)
         {
            if (!ResizeD3D12SwapChain() || back_buffer_desc.Width != d3d12_desc.Width || back_buffer_desc.Height != d3d12_desc.Height || format != d3d12_desc.Format)
               return;
         }

         SyncColorSpace();

         // Frame Generation settings, before touching the swapchain buffers, as the swapchain might need to be re-created
         FG::FrameData frame_data;
         bool generate = false;
         format_unsupported = false;
         if (fg_implementation && fg_instance_data && fg_instance_data->is_supported)
         {
            FG::SettingsData settings_data;
            bool inputs_valid = false;
            device->BuildFrameData(frame_data, settings_data, inputs_valid);
            settings_data.output_width = d3d12_desc.Width;
            settings_data.output_height = d3d12_desc.Height;
            settings_data.output_format = d3d12_desc.Format;
            settings_data.hdr = color_space == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
            settings_data.generated_frames = std::clamp(settings_data.generated_frames, 1u, (std::max)(fg_instance_data->max_generated_frames, 1u));

            format_unsupported = !fg_instance_data->supports_scrgb_hdr && d3d12_desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT;
            if (format_unsupported && !logged_unsupported_format)
            {
               logged_unsupported_format = true;
               Log(reshade::log::level::warning, std::format("{} doesn't support scRGB (FP16) swapchains, frames won't be generated", FG::GetTypeName(fg_implementation->GetType())));
            }

            generate = device->fg_enabled && inputs_valid && !format_unsupported;
            generate &= d3d12_desc.Width >= fg_instance_data->min_resolution && d3d12_desc.Height >= fg_instance_data->min_resolution;
            generate &= fg_implementation->UpdateSettings(fg_instance_data, settings_data);

            if (fg_implementation->NeedsSwapchainRecreation(fg_instance_data) && !RecreateD3D12SwapChain())
               return;
         }

         D3D12Interop::Context& interop = device->interop;
         if (!D3D12Interop::UpdateSharedTexture(interop, device->back_buffer, back_buffer_desc.Width, back_buffer_desc.Height, format, false))
            return;
         // Same format family, so it's allowed even if the back buffer was sRGB (or typeless)
         device->d3d11_context->CopyResource(device->back_buffer.resource_11.Get(), back_buffer);

         ID3D12GraphicsCommandList* const command_list = D3D12Interop::BeginCommandList(interop, device->d3d11_context.Get());
         if (!command_list)
            return;

         // Note: frame generation implementations need this to be called every frame
         const UINT back_buffer_index = d3d12_swapchain->GetCurrentBackBufferIndex();
         ComPtr<ID3D12Resource> d3d12_back_buffer;
         HRESULT hr = d3d12_swapchain->GetBuffer(back_buffer_index, IID_PPV_ARGS(&d3d12_back_buffer));
         if (SUCCEEDED(hr))
         {
            ID3D12Resource* const shared_back_buffer = device->back_buffer.resource_12.Get();
            TransitionResource(command_list, d3d12_back_buffer.Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST);
            TransitionResource(command_list, shared_back_buffer, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE);
            command_list->CopyResource(d3d12_back_buffer.Get(), shared_back_buffer);
            TransitionResource(command_list, shared_back_buffer, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON);
            TransitionResource(command_list, d3d12_back_buffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT);
         }
         d3d12_back_buffer.Reset(); // Don't keep references to the swapchain buffers
         ASSERT_ONCE(SUCCEEDED(hr));

         generating_frames = false;
         if (fg_implementation && fg_instance_data && fg_instance_data->is_supported)
            generating_frames = fg_implementation->PrepareFrame(fg_instance_data, command_list, frame_data, generate);

         // Submitted even if something failed, to keep the DX11/DX12 sync consistent.
         // DX11 waits for it, so it can write the shared textures again in the next frame (frame generation copied the inputs on our command list).
         if (!D3D12Interop::ExecuteCommandList(interop, device->d3d11_context.Get()))
         {
            generating_frames = false;
            return;
         }

         // We don't pass in sync intervals beyond 1, they would make little sense with frame generation (which paces frames itself)
         const UINT sync_interval = (std::min)(game_sync_interval, 1u);
         UINT flags = 0;
         if (sync_interval == 0 && (d3d12_desc.Flags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING) != 0)
            flags |= DXGI_PRESENT_ALLOW_TEARING;

         HRESULT present_result;
         {
            ScopedInternalObjectsCreation internal_objects_creation; // Some libraries might create objects lazily on the first presents
            if (fg_implementation && fg_instance_data && fg_instance_data->is_supported)
            {
               present_result = fg_implementation->Present(fg_instance_data, d3d12_swapchain.Get(), device->frame_id, sync_interval, flags);
               last_presented_frames = fg_implementation->GetLastPresentedFramesCount(fg_instance_data);
            }
            else
            {
               present_result = d3d12_swapchain->Present(sync_interval, flags);
               last_presented_frames = 1;
            }
         }
         ASSERT_ONCE(SUCCEEDED(present_result));
         d3d12_present_result = present_result;
         d3d12_presented = true;
      }

      void PresentCurrentBackBuffer()
      {
         // In DX11, the buffer 0 is always the current back buffer (for any swap effect)
         ComPtr<ID3D11Resource> back_buffer;
         IDXGISwapChain* const source = original_native ? original_native.Get() : original.Get();
         if (SUCCEEDED(source->GetBuffer(0, IID_PPV_ARGS(&back_buffer))))
            PresentInternal(back_buffer.Get());
      }

      //
      // SwapChain interface
      //

      void PresentBackBuffer(ID3D11Resource* back_buffer) override
      {
         const std::lock_guard lock(device->mutex);
         external_present_this_frame = true;
         PresentInternal(back_buffer);
      }

      HWND GetGameWindow() const override { return game_window; }
      HWND GetHiddenWindow() const override { return hidden_window ? hidden_window->hwnd : nullptr; }
      bool IsGeneratingFrames() const override { return generating_frames; }
      unsigned int GetLastPresentedFramesCount() const override { return last_presented_frames; }
      bool IsUsingFallbackPresent() const override { return use_internal_present; }
      bool IsFormatUnsupported() const override { return format_unsupported; }

      bool Matches(IDXGISwapChain* swapchain) const
      {
         if (!swapchain)
            return false;
         if (swapchain == static_cast<const IDXGISwapChain*>(this) || swapchain == original.Get() || swapchain == original_native.Get())
            return true;
         // Fallback (e.g. in case the ReShade proxy couldn't be unwrapped)
         DXGI_SWAP_CHAIN_DESC desc;
         const HWND hidden_hwnd = GetHiddenWindow();
         return hidden_hwnd && SUCCEEDED(swapchain->GetDesc(&desc)) && desc.OutputWindow == hidden_hwnd;
      }

      //
      // IUnknown
      //

      HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override
      {
         if (object == nullptr)
            return E_POINTER;
         *object = nullptr;
         if (riid == __uuidof(IUnknown) || riid == __uuidof(IDXGIObject) || riid == __uuidof(IDXGIDeviceSubObject) || riid == __uuidof(IDXGISwapChain) || riid == __uuidof(SwapChainImpl)
            || (riid == __uuidof(IDXGISwapChain1) && original_1)
            || (riid == __uuidof(IDXGISwapChain2) && original_2)
            || (riid == __uuidof(IDXGISwapChain3) && original_3)
            || (riid == __uuidof(IDXGISwapChain4) && original_4))
         {
            AddRef();
            *object = static_cast<IDXGISwapChain4*>(this);
            return S_OK;
         }
         return E_NOINTERFACE;
      }

      ULONG STDMETHODCALLTYPE AddRef() override
      {
         return InterlockedIncrement(&ref_count);
      }

      ULONG STDMETHODCALLTYPE Release() override
      {
         const ULONG ref = InterlockedDecrement(&ref_count);
         if (ref == 0)
            delete this;
         return ref;
      }

      //
      // IDXGIObject
      //

      HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID name, UINT data_size, const void* data) override { return original->SetPrivateData(name, data_size, data); }
      HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID name, const IUnknown* unknown) override { return original->SetPrivateDataInterface(name, unknown); }
      HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID name, UINT* data_size, void* data) override { return original->GetPrivateData(name, data_size, data); }
      HRESULT STDMETHODCALLTYPE GetParent(REFIID riid, void** parent) override { return original->GetParent(riid, parent); }

      //
      // IDXGIDeviceSubObject
      //

      HRESULT STDMETHODCALLTYPE GetDevice(REFIID riid, void** device_object) override { return original->GetDevice(riid, device_object); }

      //
      // IDXGISwapChain
      //

      HRESULT STDMETHODCALLTYPE Present(UINT sync_interval, UINT flags) override
      {
         // Status checks (e.g. occlusion) are about the window we present on
         if ((flags & DXGI_PRESENT_TEST) != 0)
         {
            const std::lock_guard lock(device->mutex);
            return d3d12_swapchain ? d3d12_swapchain->Present(0, DXGI_PRESENT_TEST) : original->Present(sync_interval, flags);
         }

         {
            const std::lock_guard lock(device->mutex);
            game_sync_interval = sync_interval;
            // If ReShade isn't beneath this swapchain, it either already called "PresentBackBuffer()" (if it wraps this swapchain), or it's not there.
            // If it's beneath, but it stopped sending its present event, we present before it, which skips its effects and overlay, but it's better than a frozen screen.
            if (!d3d12_presented && (!original_is_reshade_proxy || use_internal_present))
               PresentCurrentBackBuffer();
         }

         // If ReShade is beneath us, "PresentBackBuffer()" will be called within this.
         // The original swapchain never waits for v-sync and never tears (it's not visible anyway).
         const HRESULT original_result = original->Present(0, flags & ~(DXGI_PRESENT_ALLOW_TEARING | DXGI_PRESENT_RESTART));

         const std::lock_guard lock(device->mutex);
         if (original_is_reshade_proxy)
         {
            if (external_present_this_frame)
            {
               missed_external_presents = 0;
               use_internal_present = false;
            }
            else if (++missed_external_presents >= max_missed_external_presents && !use_internal_present)
            {
               use_internal_present = true;
               Log(reshade::log::level::warning, "ReShade's present event isn't being called, presenting the frames before ReShade draws");
            }
         }
         HRESULT result = original_result;
         if (SUCCEEDED(original_result))
         {
            if (d3d12_presented)
               result = d3d12_present_result;
            else if (result == DXGI_STATUS_OCCLUDED)
               result = S_OK; // The original swapchain window is hidden, so it would always be occluded
         }
         const bool generated = d3d12_presented && generating_frames;
         d3d12_presented = false;
         d3d12_present_result = S_OK;
         external_present_this_frame = false;
         device->EndFrame(generated);
         return result;
      }

      HRESULT STDMETHODCALLTYPE GetBuffer(UINT buffer, REFIID riid, void** surface) override { return original->GetBuffer(buffer, riid, surface); }

      HRESULT STDMETHODCALLTYPE SetFullscreenState(BOOL fullscreen, IDXGIOutput* target) override
      {
         // Exclusive fullscreen isn't supported (it would be on the hidden window, and frame generation doesn't support it anyway).
         // If ReShade is beneath us, let the request go through its addon events (Luma blocks it for proxied swapchains, and emulates borderless fullscreen).
         if (original_is_reshade_proxy)
            return original->SetFullscreenState(fullscreen, target);
         return fullscreen ? S_OK : original->SetFullscreenState(FALSE, nullptr);
      }

      HRESULT STDMETHODCALLTYPE GetFullscreenState(BOOL* fullscreen, IDXGIOutput** target) override { return original->GetFullscreenState(fullscreen, target); }

      HRESULT STDMETHODCALLTYPE GetDesc(DXGI_SWAP_CHAIN_DESC* desc) override
      {
         const HRESULT hr = original->GetDesc(desc);
         if (SUCCEEDED(hr) && desc)
         {
            desc->OutputWindow = game_window;
            desc->Windowed = TRUE;
         }
         return hr;
      }

      HRESULT STDMETHODCALLTYPE ResizeBuffers(UINT buffer_count, UINT width, UINT height, DXGI_FORMAT new_format, UINT swapchain_flags) override
      {
         return ResizeBuffersInternal(buffer_count, width, height, new_format, swapchain_flags, nullptr, nullptr, false);
      }

      // These would resize/query the game window, forward them to the swapchain on it
      HRESULT STDMETHODCALLTYPE ResizeTarget(const DXGI_MODE_DESC* new_target_parameters) override { return d3d12_swapchain ? d3d12_swapchain->ResizeTarget(new_target_parameters) : original->ResizeTarget(new_target_parameters); }
      HRESULT STDMETHODCALLTYPE GetContainingOutput(IDXGIOutput** output) override { return d3d12_swapchain ? d3d12_swapchain->GetContainingOutput(output) : original->GetContainingOutput(output); }
      HRESULT STDMETHODCALLTYPE GetFrameStatistics(DXGI_FRAME_STATISTICS* stats) override { return d3d12_swapchain ? d3d12_swapchain->GetFrameStatistics(stats) : original->GetFrameStatistics(stats); }
      HRESULT STDMETHODCALLTYPE GetLastPresentCount(UINT* last_present_count) override { return original->GetLastPresentCount(last_present_count); }

      //
      // IDXGISwapChain1
      //

      HRESULT STDMETHODCALLTYPE GetDesc1(DXGI_SWAP_CHAIN_DESC1* desc) override { return original_1 ? original_1->GetDesc1(desc) : E_NOINTERFACE; }
      HRESULT STDMETHODCALLTYPE GetFullscreenDesc(DXGI_SWAP_CHAIN_FULLSCREEN_DESC* desc) override { return original_1 ? original_1->GetFullscreenDesc(desc) : E_NOINTERFACE; }
      HRESULT STDMETHODCALLTYPE GetHwnd(HWND* hwnd) override
      {
         if (hwnd == nullptr)
            return DXGI_ERROR_INVALID_CALL;
         *hwnd = game_window;
         return S_OK;
      }
      HRESULT STDMETHODCALLTYPE GetCoreWindow(REFIID riid, void** unknown) override { return original_1 ? original_1->GetCoreWindow(riid, unknown) : E_NOINTERFACE; }
      // Dirty rects and scrolling are ignored (the whole back buffer is copied anyway)
      HRESULT STDMETHODCALLTYPE Present1(UINT sync_interval, UINT flags, const DXGI_PRESENT_PARAMETERS* present_parameters) override { return Present(sync_interval, flags); }
      BOOL STDMETHODCALLTYPE IsTemporaryMonoSupported() override { return original_1 ? original_1->IsTemporaryMonoSupported() : FALSE; }
      HRESULT STDMETHODCALLTYPE GetRestrictToOutput(IDXGIOutput** restrict_to_output) override { return original_1 ? original_1->GetRestrictToOutput(restrict_to_output) : E_NOINTERFACE; }
      HRESULT STDMETHODCALLTYPE SetBackgroundColor(const DXGI_RGBA* color) override { return original_1 ? original_1->SetBackgroundColor(color) : E_NOINTERFACE; }
      HRESULT STDMETHODCALLTYPE GetBackgroundColor(DXGI_RGBA* color) override { return original_1 ? original_1->GetBackgroundColor(color) : E_NOINTERFACE; }
      HRESULT STDMETHODCALLTYPE SetRotation(DXGI_MODE_ROTATION rotation) override { return original_1 ? original_1->SetRotation(rotation) : E_NOINTERFACE; }
      HRESULT STDMETHODCALLTYPE GetRotation(DXGI_MODE_ROTATION* rotation) override { return original_1 ? original_1->GetRotation(rotation) : E_NOINTERFACE; }

      //
      // IDXGISwapChain2
      //

      HRESULT STDMETHODCALLTYPE SetSourceSize(UINT width, UINT height) override { return original_2 ? original_2->SetSourceSize(width, height) : E_NOINTERFACE; }
      HRESULT STDMETHODCALLTYPE GetSourceSize(UINT* width, UINT* height) override { return original_2 ? original_2->GetSourceSize(width, height) : E_NOINTERFACE; }
      // The game paces itself on the original swapchain (which presents every frame), frame generation paces the DX12 one
      HRESULT STDMETHODCALLTYPE SetMaximumFrameLatency(UINT max_latency) override { return original_2 ? original_2->SetMaximumFrameLatency(max_latency) : E_NOINTERFACE; }
      HRESULT STDMETHODCALLTYPE GetMaximumFrameLatency(UINT* max_latency) override { return original_2 ? original_2->GetMaximumFrameLatency(max_latency) : E_NOINTERFACE; }
      HANDLE STDMETHODCALLTYPE GetFrameLatencyWaitableObject() override { return original_2 ? original_2->GetFrameLatencyWaitableObject() : nullptr; }
      HRESULT STDMETHODCALLTYPE SetMatrixTransform(const DXGI_MATRIX_3X2_F* matrix) override { return original_2 ? original_2->SetMatrixTransform(matrix) : E_NOINTERFACE; }
      HRESULT STDMETHODCALLTYPE GetMatrixTransform(DXGI_MATRIX_3X2_F* matrix) override { return original_2 ? original_2->GetMatrixTransform(matrix) : E_NOINTERFACE; }

      //
      // IDXGISwapChain3
      //

      UINT STDMETHODCALLTYPE GetCurrentBackBufferIndex() override { return original_3 ? original_3->GetCurrentBackBufferIndex() : 0; }
      HRESULT STDMETHODCALLTYPE CheckColorSpaceSupport(DXGI_COLOR_SPACE_TYPE color_space_type, UINT* color_space_support) override
      {
         const std::lock_guard lock(device->mutex);
         if (d3d12_swapchain)
            return d3d12_swapchain->CheckColorSpaceSupport(color_space_type, color_space_support);
         return original_3 ? original_3->CheckColorSpaceSupport(color_space_type, color_space_support) : E_NOINTERFACE;
      }
      HRESULT STDMETHODCALLTYPE SetColorSpace1(DXGI_COLOR_SPACE_TYPE color_space_type) override
      {
         const HRESULT hr = original_3 ? original_3->SetColorSpace1(color_space_type) : E_NOINTERFACE;
         const std::lock_guard lock(device->mutex);
         if (d3d12_swapchain)
         {
            color_space_set = false;
            ApplyColorSpace(color_space_type);
         }
         return hr;
      }
      HRESULT STDMETHODCALLTYPE ResizeBuffers1(UINT buffer_count, UINT width, UINT height, DXGI_FORMAT format, UINT swapchain_flags, const UINT* creation_node_mask, IUnknown* const* present_queue) override
      {
         return ResizeBuffersInternal(buffer_count, width, height, format, swapchain_flags, creation_node_mask, present_queue, true);
      }

      //
      // IDXGISwapChain4
      //

      HRESULT STDMETHODCALLTYPE SetHDRMetaData(DXGI_HDR_METADATA_TYPE type, UINT size, void* metadata) override
      {
         const HRESULT hr = original_4 ? original_4->SetHDRMetaData(type, size, metadata) : E_NOINTERFACE;
         const std::lock_guard lock(device->mutex);
         if (d3d12_swapchain)
            d3d12_swapchain->SetHDRMetaData(type, size, metadata);
         return hr;
      }

   private:
      HRESULT ResizeBuffersInternal(UINT buffer_count, UINT width, UINT height, DXGI_FORMAT format, UINT swapchain_flags, const UINT* creation_node_mask, IUnknown* const* present_queue, bool version_1)
      {
         // Zero sizes would make DXGI use the size of the (hidden) window, use the game window size instead
         if (width == 0 || height == 0)
         {
            RECT client_rect = {};
            GetClientRect(game_window, &client_rect);
            if (width == 0)
               width = (std::max)(UINT(client_rect.right - client_rect.left), 1u);
            if (height == 0)
               height = (std::max)(UINT(client_rect.bottom - client_rect.top), 1u);
         }

         const std::lock_guard lock(device->mutex);
         HRESULT hr;
         if (version_1 && original_3)
            hr = original_3->ResizeBuffers1(buffer_count, width, height, format, swapchain_flags, creation_node_mask, present_queue);
         else
            hr = original->ResizeBuffers(buffer_count, width, height, format, swapchain_flags);
         if (SUCCEEDED(hr))
            ResizeD3D12SwapChain();
         return hr;
      }

      LONG ref_count = 1;
      std::shared_ptr<DeviceImpl> device;

      ComPtr<IDXGISwapChain> original; // A ReShade proxy (if it's beneath us) or the native swapchain
      ComPtr<IDXGISwapChain1> original_1;
      ComPtr<IDXGISwapChain2> original_2;
      ComPtr<IDXGISwapChain3> original_3;
      ComPtr<IDXGISwapChain4> original_4;
      ComPtr<IDXGISwapChain> original_native;
      std::unique_ptr<HiddenWindow> hidden_window;
      HWND game_window = nullptr;
      bool original_is_reshade_proxy = false;

      ComPtr<IDXGISwapChain4> d3d12_swapchain;
      DXGI_SWAP_CHAIN_DESC1 d3d12_desc = {};
      FG::FrameGenerationImpl* fg_implementation = nullptr; // The one used to create the DX12 swapchain (null if it failed)
      FG::InstanceData* fg_instance_data = nullptr;
      DXGI_COLOR_SPACE_TYPE color_space = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
      bool color_space_set = false;

      UINT game_sync_interval = 0; // The last one the game requested
      bool d3d12_presented = false; // Whether we presented since the last game present
      HRESULT d3d12_present_result = S_OK;
      bool external_present_this_frame = false; // Whether "PresentBackBuffer()" was called since the last game present
      uint32_t missed_external_presents = 0;
      bool use_internal_present = false;
      bool generating_frames = false;
      unsigned int last_presented_frames = 1;
      bool format_unsupported = false;
      bool logged_unsupported_format = false;
   };

   HRESULT DeviceImpl::CreateProxySwapChain(HWND game_window, UINT width, UINT height, bool original_is_reshade_proxy, const std::function<HRESULT(HWND window, UINT width, UINT height, IDXGISwapChain** swapchain)>& create_original, IDXGISwapChain** swapchain)
   {
      if (!game_window || !IsWindow(game_window) || interop.poisoned)
         return E_INVALIDARG;

      // Zero sizes mean the window client size
      if (width == 0 || height == 0)
      {
         RECT client_rect = {};
         GetClientRect(game_window, &client_rect);
         if (width == 0)
            width = (std::max)(UINT(client_rect.right - client_rect.left), 1u);
         if (height == 0)
            height = (std::max)(UINT(client_rect.bottom - client_rect.top), 1u);
      }

      auto hidden_window = HiddenWindow::Create(game_window, width, height);
      if (!hidden_window)
         return E_FAIL;

      // Create the original swapchain on the hidden window (if ReShade is beneath us, this goes through it, and triggers all its events)
      ComPtr<IDXGISwapChain> original;
      HRESULT hr = create_original(hidden_window->hwnd, width, height, &original);
      if (FAILED(hr) || !original)
      {
         Log(reshade::log::level::error, std::format("Failed to create the original swapchain on the hidden window, hr = {:#010x}", unsigned(hr)));
         return FAILED(hr) ? hr : E_FAIL;
      }

      const std::lock_guard lock(mutex);
      auto* proxy_swapchain = new SwapChainImpl(shared_from_this(), std::move(original), std::move(hidden_window), game_window, original_is_reshade_proxy);
      hr = proxy_swapchain->CreateD3D12SwapChain();
      if (FAILED(hr))
      {
         proxy_swapchain->Release(); // This also destroys the original swapchain and the hidden window
         return hr;
      }

      Log(reshade::log::level::info, std::format("Created a proxy swapchain ({}x{}){}", width, height, original_is_reshade_proxy ? "" : " (ReShade isn't beneath it)"));
      *swapchain = static_cast<IDXGISwapChain4*>(proxy_swapchain);
      return S_OK;
   }

   SwapChain* DeviceImpl::FindSwapChain(IDXGISwapChain* swapchain) const
   {
      const std::lock_guard lock(mutex);
      for (SwapChainImpl* proxy_swapchain : swapchains)
      {
         if (proxy_swapchain->Matches(swapchain))
            return proxy_swapchain;
      }
      return nullptr;
   }

   SwapChain* DeviceImpl::GetSwapChain() const
   {
      const std::lock_guard lock(mutex);
      return swapchains.empty() ? nullptr : swapchains.back();
   }

   namespace
   {
      std::shared_ptr<DeviceImpl> FindDeviceForSwapChainCreation(IUnknown* device, bool& device_was_wrapped)
      {
         device_was_wrapped = false;
         ComPtr<ID3D11Device> d3d11_device;
         if (!device || FAILED(device->QueryInterface(IID_PPV_ARGS(&d3d11_device))))
            return nullptr; // Not a DX11 device (e.g. a DX12 queue)
         const ComPtr<ID3D11Device> native_device = GetUnwrapped<ID3D11Device>(device, &device_was_wrapped);

         const std::lock_guard lock(devices_mutex);
         std::erase_if(devices, [](const std::weak_ptr<DeviceImpl>& device) { return device.expired(); });
         for (const auto& weak_device : devices)
         {
            auto proxy_device = weak_device.lock();
            if (proxy_device && proxy_device->d3d11_device.Get() == native_device.Get())
               return proxy_device;
         }
         return nullptr;
      }

      // Creates the swapchains of the devices with proxying enabled
      class ProxySwapChainCreationOverride final : public DXGIHooks::SwapChainCreationOverride
      {
      public:
         bool CreateSwapChain(IDXGIFactory* factory, IUnknown* device, const DXGI_SWAP_CHAIN_DESC& desc, IDXGISwapChain** swapchain, HRESULT& result) override
         {
            if (!desc.OutputWindow || desc.SampleDesc.Count > 1)
               return false;
            bool device_was_wrapped = false;
            auto proxy_device = FindDeviceForSwapChainCreation(device, device_was_wrapped);
            if (!proxy_device)
               return false;
            const auto create_original = [&](HWND window, UINT width, UINT height, IDXGISwapChain** original_swapchain) -> HRESULT
               {
                  DXGI_SWAP_CHAIN_DESC hidden_desc = desc;
                  hidden_desc.OutputWindow = window;
                  hidden_desc.Windowed = TRUE;
                  hidden_desc.BufferDesc.Width = width;
                  hidden_desc.BufferDesc.Height = height;
                  return DXGIHooks::CreateSwapChainOriginal(factory, device, &hidden_desc, original_swapchain);
               };
            ComPtr<IDXGISwapChain> proxy_swapchain;
            result = proxy_device->CreateProxySwapChain(desc.OutputWindow, desc.BufferDesc.Width, desc.BufferDesc.Height, device_was_wrapped, create_original, &proxy_swapchain);
            if (FAILED(result))
            {
               Log(reshade::log::level::warning, std::format("Failed to proxy the swapchain, hr = {:#010x}, creating it normally", unsigned(result)));
               return false;
            }
            *swapchain = proxy_swapchain.Detach();
            return true;
         }

         bool CreateSwapChainForHwnd(IDXGIFactory2* factory, IUnknown* device, HWND hwnd, const DXGI_SWAP_CHAIN_DESC1& desc, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fullscreen_desc, IDXGIOutput* restrict_to_output, IDXGISwapChain1** swapchain, HRESULT& result) override
         {
            if (!hwnd || desc.SampleDesc.Count > 1)
               return false;
            bool device_was_wrapped = false;
            auto proxy_device = FindDeviceForSwapChainCreation(device, device_was_wrapped);
            if (!proxy_device)
               return false;
            const auto create_original = [&](HWND window, UINT width, UINT height, IDXGISwapChain** original_swapchain) -> HRESULT
               {
                  DXGI_SWAP_CHAIN_DESC1 hidden_desc = desc;
                  hidden_desc.Width = width;
                  hidden_desc.Height = height;
                  ComPtr<IDXGISwapChain1> original_swapchain_1;
                  // Always windowed (no fullscreen desc)
                  const HRESULT hr = DXGIHooks::CreateSwapChainForHwndOriginal(factory, device, window, &hidden_desc, nullptr, restrict_to_output, &original_swapchain_1);
                  if (SUCCEEDED(hr))
                     *original_swapchain = original_swapchain_1.Detach();
                  return hr;
               };
            ComPtr<IDXGISwapChain> proxy_swapchain;
            ComPtr<IDXGISwapChain1> proxy_swapchain_1;
            result = proxy_device->CreateProxySwapChain(hwnd, desc.Width, desc.Height, device_was_wrapped, create_original, &proxy_swapchain);
            if (SUCCEEDED(result))
               result = proxy_swapchain.As(&proxy_swapchain_1);
            if (FAILED(result))
            {
               Log(reshade::log::level::warning, std::format("Failed to proxy the swapchain, hr = {:#010x}, creating it normally", unsigned(result)));
               return false;
            }
            *swapchain = proxy_swapchain_1.Detach();
            return true;
         }
      };

      ProxySwapChainCreationOverride swapchain_creation_override;
   }

   void DeviceImpl::SetProxyEnabled(bool enabled)
   {
      const std::lock_guard lock(devices_mutex);
      std::erase_if(devices, [this](const std::weak_ptr<DeviceImpl>& device) { auto locked = device.lock(); return !locked || locked.get() == this; });
      if (enabled)
         devices.push_back(weak_from_this());
      DXGIHooks::SetSwapChainCreationOverride(devices.empty() ? nullptr : &swapchain_creation_override);
   }

   bool IsCreatingInternalObjects()
   {
      return internal_objects_creation_depth > 0;
   }

   HWND GetGameWindow(HWND window)
   {
      const std::lock_guard lock(hidden_windows_mutex);
      const auto it = hidden_to_game_windows.find(window);
      return it != hidden_to_game_windows.end() ? it->second : window;
   }

   std::shared_ptr<Device> Device::Create(ID3D11Device* native_device, std::function<ID3D11ComputeShader*()> get_depth_copy_shader)
   {
      if (!native_device)
         return nullptr;
      auto device = std::make_shared<DeviceImpl>();
      if (!device->Initialize(native_device, std::move(get_depth_copy_shader)))
         return nullptr;
      return device;
   }
}

#endif // ENABLE_D3D12_SWAPCHAIN_PROXY
