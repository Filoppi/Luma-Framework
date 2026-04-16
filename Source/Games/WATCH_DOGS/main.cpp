#define GAME_WATCH_DOGS 1
#define CHECK_GRAPHICS_API_COMPATIBILITY 1
#define ALLOW_SHADERS_DUMPING 0
#define DISABLE_AUTO_DEBUGGER 1
#define DISABLE_FOCUS_LOSS_SUPPRESSION 1

// To access "last_draw_dispatch_data"
#define ENABLE_DRAW_DISPATCH_DATA_CACHE 1

#define ENABLE_NGX 1
#define ENABLE_FIDELITY_SK 1
//#define AUTO_ENABLE_SR 1
//#define DEBUG_MV 1
#define DEBUG_LOG 1

#include "..\..\Core\core.hpp"
#include "..\..\External\MinHook\include\MinHook.h"

namespace
{

   namespace Nexus
   {
      inline bool IsWine()
      {
         static void* pwine_get_version;
         HMODULE hntdll = GetModuleHandle(TEXT("ntdll.dll"));
         if (!hntdll)
         {
            return false;
         }

         pwine_get_version = (void*)GetProcAddress(hntdll, "wine_get_version");

         if (!pwine_get_version)
         {
            return false;
         }

         return true;
      }
      
      bool m_willRenderThisFrame = false;
      bool m_noPreviousFrame = false;
      bool m_gridShadingEnable = false;
      int32_t m_previousResetRequests = 0;
      float m_lastGameDeltaTime = 0.f;
   } // namespace Nexus
   
   ShaderHashesList shader_hashes_DeferredFXAntialias; // DeferredFX AA
   ShaderHashesList shader_hashes_DeferredFXAntialias_RESOLVE;
   ShaderHashesList shader_hashes_DeferredFXAntialias_NO_PREVIOUS_FRAME; // DeferredFX AA NO_PREVIOUS_FRAME
   ShaderHashesList shader_hashes_WaterHeightMap;
   ShaderHashesList shader_hashes_RainStreak;
   ShaderHashesList shader_hashes_SMAA;

   ShaderHashesList shader_hashes_Downsample;
   ShaderHashesList shader_hashes_LightProbesUpdate;
   
   ShaderHashesList shader_hashes_DriverGenericRoad;

   static std::vector<std::byte*> pattern_1_addresses;
   static std::vector<std::byte*> pattern_2_addresses; // grid shading

   float2 frame_jitters = float2(0.f, 0.f);
   float2 prev_frame_jitters = frame_jitters;
   float aspect_ratio = 1.f;
   
   int32_t i = 0;
   
   bool map_changed = false;

   bool has_forward_pass_finished = false;
   bool will_draw_rain = false;
   bool has_copied_before_rain = false;
   
   bool has_taa_drawn = false;
   bool has_sr_draw_tried = false;
   bool has_changed_all_viewport = false;
   
   uintptr_t manager_ptr;

   // ### MINHOOK ADDITION ###
   // 1. Define the signature of the function to hook (MATCHES sub_6FFFF6C05600)
   typedef int64_t*(__fastcall* fnSub_6FFFF6C05600)(int64_t a1, int64_t* a2);
   // Original function pointer (populated by MinHook)
   fnSub_6FFFF6C05600 oSub_6FFFF6C05600 = nullptr;

   // ### MINHOOK ADDITION ###
   // 2. Hooked function (replaces the original)
   int64_t* __fastcall hkSub_6FFFF6C05600(int64_t a1, int64_t* a2)
   {
      // --------------------------
      // Pre-hook logic (run BEFORE original function)
      // --------------------------

      // Example: Modify parameters before calling the original function
      // a1 = 12345; // Uncomment to change a1's value
      // *a2 = 67890; // Uncomment to change the value pointed to by a2
      manager_ptr = static_cast<uintptr_t>(a1);

      if (a2 && a2[14])
      {
         float* v15 = (float*)a2[14];

         float ax = v15[1000];
         float ay = v15[1001];
         float az = v15[1002];

         float bx = v15[756];
         float by = v15[757];
         float bz = v15[758];

         float dot = ax * bx + ay * by + az * bz;
         
         Nexus::m_noPreviousFrame = Nexus::m_noPreviousFrame || (dot < 0.80000001);
         
         float* m_lastGameDeltaTime = (float*)(a2[14] + 4928LL);
         Nexus::m_lastGameDeltaTime = *m_lastGameDeltaTime;

#if DEVELOPMENT && 0
         
         std::stringstream s;
         s << "CDeferredFxRendererContext: 0x" << std::hex << a2[14];
         s << " | ";
         s << "m_lastGameDeltaTime: 0x" << std::hex << m_lastGameDeltaTime;
         s << " | ";
         s << "m_lastPreviousCamera: 0x" << std::hex << m_lastGameDeltaTime - 976/4; // this addr + 32 = View matrix start
         s << " | ";
         s << "m_lastCurrentCamera: 0x" << std::hex << m_lastGameDeltaTime - 976*2/4;
         reshade::log::message(reshade::log::level::info, s.str().c_str());
         // struct of CControlCamera
         // byte unk[32];
         // Matrix44 m_viewMatrix; //transposed
         // Matrix44 m_viewMatrixInverse; //transposed
         // Matrix44 m_viewMatrixPure; //not transposed
         // Matrix44 m_projectionMatrix; // not transposed
         // Matrix44 m_projectionMatrixInverse; // not transposed
         // Matrix44 m_viewProjectionMatrix; // transposed
         // Matrix44 m_viewProjectionMatrixInverse; //transposed
         // G4::Vector3f m_position;
         // G4::Vector3f m_frontVector;
         // G4::Vector3f m_upVector;
         // G4::Vector3f m_leftVector;
         // float m_nearClipDistance;
         // float m_farClipDistance;
         // float m_FOV;
         
         /*
         s << "PrevCamDirDotCamDir = " << dot;
         reshade::log::message(reshade::log::level::info, s.str().c_str());
         s.clear();
         s.str("");
         s << "m_lastGameDeltaTime = " << *m_lastGameDeltaTime;
         reshade::log::message(reshade::log::level::info, s.str().c_str());
         */
         
#endif
      }

      // --------------------------
      // Call the original function (CRITICAL - preserve game functionality)
      // --------------------------
      int64_t* original_result = oSub_6FFFF6C05600(a1, a2);

      // --------------------------
      // Post-hook logic (run AFTER original function)
      // --------------------------

      // Example: Modify the return value (if needed)
      // int64_t* custom_result = original_result; // Replace with your custom logic

      // Return the result (original or modified)
      return original_result;
   }

   void PatchVMProtect()
   {
      DWORD oldProtect = 0;
      auto ntdll = GetModuleHandleA("ntdll.dll");
      if (Nexus::IsWine())
      {
         auto nt_vp = (BYTE*)GetProcAddress(ntdll, "NtProtectVirtualMemory");
         auto nt_vp_offset = (uintptr_t)nt_vp - (uintptr_t)ntdll + 4;
         char nt_vp_syscall;

         std::ifstream infile("C:\\Windows\\System32\\ntdll.dll", std::ios::binary);
         infile.seekg(nt_vp_offset);
         infile.get(nt_vp_syscall);

         BYTE restore[] = {0x4C, 0x8B, 0xD1, 0xB8, static_cast<BYTE>(nt_vp_syscall)};
         VirtualProtect(nt_vp, sizeof(restore), PAGE_EXECUTE_READWRITE, &oldProtect);
         memcpy(nt_vp, restore, sizeof(restore));
         VirtualProtect(nt_vp, sizeof(restore), oldProtect, &oldProtect);
      }
      else
      {
         BYTE callcode = ((BYTE*)GetProcAddress(ntdll, "NtQuerySection"))[4] - 1;
         BYTE restore[] = {0x4C, 0x8B, 0xD1, 0xB8, callcode};
         auto nt_vp = (BYTE*)GetProcAddress(ntdll, "NtProtectVirtualMemory");
         VirtualProtect(nt_vp, sizeof(restore), PAGE_EXECUTE_READWRITE, &oldProtect);
         memcpy(nt_vp, restore, sizeof(restore));
         VirtualProtect(nt_vp, sizeof(restore), oldProtect, &oldProtect);
      }
   }
   
} // namespace

struct GameDeviceDataWatchDogs final : public GameDeviceData
{
   com_ptr<ID3D11Texture2D> motion_vector;
   com_ptr<ID3D11UnorderedAccessView> motion_vector_uav;
   
   com_ptr<ID3D11Texture2D> unjitter_depth;
   com_ptr<ID3D11ShaderResourceView> unjitter_depth_srv;
   com_ptr<ID3D11UnorderedAccessView> unjitter_depth_uav;
   
   com_ptr<ID3D11Texture2D> game_source_color_before_rain;
   com_ptr<ID3D11UnorderedAccessView> game_source_color_before_rain_uav;
   com_ptr<ID3D11RenderTargetView> game_source_color_before_rain_rtv;
   
   com_ptr<ID3D11UnorderedAccessView> sr_output_uav;
   
   com_ptr<ID3D11ShaderResourceView> game_source_color_srv;
   com_ptr<ID3D11RenderTargetView> game_source_color_rtv;
   com_ptr<ID3D11ShaderResourceView> game_depth_srv;
   com_ptr<ID3D11DepthStencilView> game_depth_dsv;
   com_ptr<ID3D11Texture2D> game_motion_vector;
   com_ptr<ID3D11ShaderResourceView> game_motion_vector_srv;
   com_ptr<ID3D11Buffer> game_viewport_cbv;
   
   com_ptr<ID3D11ShaderResourceView> game_reflection_srv;
   com_ptr<ID3D11SamplerState> game_reflection_sampler;
   
   com_ptr<ID3D11SamplerState> depth_sampler;
   com_ptr<ID3D11DepthStencilState> depth_stencil_state;

   DirectX::XMMATRIX ViewRotProjectionMatrix;
   DirectX::XMMATRIX ViewProjectionMatrix;
   DirectX::XMMATRIX ProjectionMatrix;
   DirectX::XMMATRIX InvProjectionMatrix;
   DirectX::XMMATRIX InvProjectionMatrixDepth;
   DirectX::XMMATRIX PreviousViewProjectionMatrix;
   
   DirectX::XMMATRIX ViewRotProjectionMatrixOriginal;
   DirectX::XMMATRIX ViewProjectionMatrixOriginal;
   DirectX::XMMATRIX ProjectionMatrixOriginal;
   DirectX::XMMATRIX InvProjectionMatrixOriginal;
   DirectX::XMMATRIX InvProjectionMatrixDepthOriginal;
   DirectX::XMMATRIX PreviousViewProjectionMatrixOriginal;
   
   
   float3 camera_position_current = {};
   float3 camera_position_previous = {};
   DirectX::XMMATRIX PreviousInvViewMatrix;
   DirectX::XMMATRIX PreviousViewMatrix;
   DirectX::XMMATRIX PreviousProjectionMatrix;
   
   DirectX::XMMATRIX PreviousViewMatrixCopy;
   DirectX::XMMATRIX PreviousProjectionMatrixCopy;
   DirectX::XMMATRIX CameraSpaceToPreviousProjectedSpace;
   
   float2 CameraDistances = {};

   float2 taa_jitters = {};
   bool found_per_view_globals = false;
   
   bool resolution_changed = false;

   bool set_render_res = false;
   float4 render_res = {0, 0, 0, 0};

   bool set_jitter = false;
   bool is_last_frame_jittered = false;
   
   void CleanMVResources()
   {
      motion_vector = nullptr;
      motion_vector_uav = nullptr;
      unjitter_depth = nullptr;
      unjitter_depth_uav = nullptr;
      unjitter_depth_srv = nullptr;
   }
   
   void CleanRainResources()
   {
      game_source_color_before_rain = nullptr;
      game_source_color_before_rain_uav = nullptr;
      game_source_color_before_rain_rtv = nullptr;
   }
};

class WatchDogs final : public Game
{
public:
   static const GameDeviceDataWatchDogs& GetGameDeviceData(const DeviceData& device_data)
   {
      return *static_cast<const GameDeviceDataWatchDogs*>(device_data.game);
   }
   static GameDeviceDataWatchDogs& GetGameDeviceData(DeviceData& device_data)
   {
      return *static_cast<GameDeviceDataWatchDogs*>(device_data.game);
   }

   void OnInit(bool async) override
   {
      // ### Update these (find the right values) ###
      // ### See the "GameCBuffers.hlsl" in the shader directory to expand settings ###
      native_shaders_definitions.emplace(CompileTimeStringHash("Decode Motion Vector"), ShaderDefinition{"Luma_DecodeMotionVector", reshade::api::pipeline_subobject_type::compute_shader});
      native_shaders_definitions.emplace(CompileTimeStringHash("Composite Rain"), ShaderDefinition{"Luma_CompositeRain", reshade::api::pipeline_subobject_type::compute_shader});
      native_shaders_definitions.emplace(CompileTimeStringHash("Unjitter Depth"), ShaderDefinition{"Luma_CopyDepth", reshade::api::pipeline_subobject_type::pixel_shader});
      native_shaders_definitions.emplace(CompileTimeStringHash("Copy VS"), ShaderDefinition{"Luma_Copy_VS", reshade::api::pipeline_subobject_type::vertex_shader});
   }

   void OnLoad(std::filesystem::path& file_path, bool failed) override
   {
      if (!failed)
      {
         reshade::register_event<reshade::addon_event::map_buffer_region>(WatchDogs::OnMapBufferRegion);
         reshade::register_event<reshade::addon_event::unmap_buffer_region>(WatchDogs::OnUnmapBufferRegion);
         reshade::register_event<reshade::addon_event::clear_render_target_view>(WatchDogs::OnClearRenderTargetView);
         reshade::register_event<reshade::addon_event::clear_depth_stencil_view>(WatchDogs::OnClearDepthStencilView);
         
         //reshade::register_event<reshade::addon_event::create_resource>(WatchDogs::OnCreateResource);
         //reshade::register_event<reshade::addon_event::create_resource_view >(WatchDogs::OnCreateResourceView);

         if (MH_Initialize() != MH_OK)
         {
            reshade::log::message(reshade::log::level::error, "MinHook init failed");
            return;
         }

         HMODULE module_handle = GetModuleHandle(TEXT("Disrupt_b64.dll")); // Handle to the current executable
         if (module_handle == nullptr)                                     // ### ADD ERROR CHECK ###
         {
            reshade::log::message(reshade::log::level::error, "Disrupt_b64.dll not found!");
            MH_Uninitialize();
            return;
         }

         auto dos_header = reinterpret_cast<PIMAGE_DOS_HEADER>(module_handle);
         auto nt_headers = reinterpret_cast<PIMAGE_NT_HEADERS>(reinterpret_cast<std::byte*>(module_handle) + dos_header->e_lfanew);

         std::byte* base = reinterpret_cast<std::byte*>(module_handle);
         std::size_t section_size = nt_headers->OptionalHeader.SizeOfImage;

         using BP = System::BytePattern;

         std::vector<BP> pattern =
            {
               BP(0x48), BP(0x8B), BP(0xC4), BP(0x55), BP(0x56), BP(0x57),
               BP(0x48), BP(0x8D), BP(0x68),
               BP(BP::WildcardType::Wildcard),

               BP(0x48), BP(0x81), BP(0xEC),
               BP(BP::WildcardType::Wildcard),
               BP(BP::WildcardType::Wildcard),
               BP(BP::WildcardType::Wildcard),
               BP(BP::WildcardType::Wildcard),

               BP(0x80), BP(0x79),
               BP(BP::WildcardType::Wildcard),
               BP(0x00)};

         pattern_1_addresses = System::ScanMemoryForPattern(base, section_size, pattern);

         if (!pattern_1_addresses.empty())
         {
            reshade::log::message(reshade::log::level::info, "Found deferred fx aa render addr");

            uintptr_t target = reinterpret_cast<uintptr_t>(pattern_1_addresses[0]);
            PatchVMProtect();

            // ### MINHOOK ADDITION ###
            // 3. Create and enable the hook
            if (MH_CreateHook(reinterpret_cast<void*>(target),
                   &hkSub_6FFFF6C05600,
                   reinterpret_cast<void**>(&oSub_6FFFF6C05600)) != MH_OK)
            {
               reshade::log::message(reshade::log::level::error,
                  "Failed to create hook! MinHook error");
               MH_Uninitialize();
               return;
            }

            // Enable the hook (hooks are disabled by default)
            if (MH_EnableHook(reinterpret_cast<void*>(target)) != MH_OK)
            {
               reshade::log::message(reshade::log::level::error,
                  "Failed to enable hook! MinHook error");
               MH_RemoveHook(reinterpret_cast<void*>(target));
               MH_Uninitialize();
               return;
            }

            reshade::log::message(reshade::log::level::info, "Hook created and enabled successfully!");
         }
         else
         {
            reshade::log::message(reshade::log::level::info, "Found NO deferred fx aa render addr.");
            MH_Uninitialize(); // Cleanup MinHook if pattern scan fails
         }

         std::vector<BP> pattern_grid =
            {
               BP(0x48), BP(0x89), BP(0x2D),
               BP(BP::WildcardType::Wildcard),
               BP(BP::WildcardType::Wildcard),
               BP(BP::WildcardType::Wildcard),
               BP(BP::WildcardType::Wildcard),

               BP(0xE8),
               BP(BP::WildcardType::Wildcard),
               BP(BP::WildcardType::Wildcard),
               BP(BP::WildcardType::Wildcard),
               BP(BP::WildcardType::Wildcard),

               BP(0x48), BP(0x8B), BP(0x15),
               BP(BP::WildcardType::Wildcard),
               BP(BP::WildcardType::Wildcard),
               BP(BP::WildcardType::Wildcard),
               BP(BP::WildcardType::Wildcard),

               BP(0x48), BP(0x8B), BP(0x0D),
               BP(BP::WildcardType::Wildcard),
               BP(BP::WildcardType::Wildcard),
               BP(BP::WildcardType::Wildcard),
               BP(BP::WildcardType::Wildcard),

               BP(0xE8)};
         pattern_2_addresses = System::ScanMemoryForPattern(base, section_size, pattern_grid);

         if (!pattern_2_addresses.empty())
         {
            std::byte* ptr = pattern_2_addresses[0];

            // displacement is at offset 3
            std::int32_t disp = *reinterpret_cast<std::int32_t*>(ptr + 3);

            // compute final address (RIP-relative)
            uintptr_t finalAddress = reinterpret_cast<uintptr_t>(ptr + 3 + sizeof(std::int32_t) + disp);

            std::stringstream s;
            s << "Resolved and stored address: 0x" << std::hex << finalAddress;
            reshade::log::message(reshade::log::level::info, s.str().c_str());

            s.clear();
            s.str("");
            s << "Pattern 2 found at: 0x " << std::hex << pattern_2_addresses[0];
            reshade::log::message(reshade::log::level::info, s.str().c_str());

            // store it back in the vector (cast to std::byte* for consistency)
            pattern_2_addresses[0] = reinterpret_cast<std::byte*>(finalAddress);
         }
#if ENABLE_NGX
         std::stringstream s;
         s << "DLSS Init: " << &NGX::DLSS::HasInit;
         s << " | DLSS Supported: " << &NGX::DLSS::IsSupported;
         reshade::log::message(reshade::log::level::info, s.str().c_str());
#endif
      }
   }

   void OnCreateDevice(ID3D11Device* native_device, DeviceData& device_data) override
   {
      device_data.game = new GameDeviceDataWatchDogs;
   }

   void OnInitSwapchain(reshade::api::swapchain* swapchain) override
   {
      auto& device_data = *swapchain->get_device()->get_private_data<DeviceData>();
      
      aspect_ratio = device_data.output_resolution.x / device_data.output_resolution.y;

      cb_luma_global_settings.GameSettings.InvOutputRes.x = 1.f / device_data.output_resolution.x;
      cb_luma_global_settings.GameSettings.InvOutputRes.y = 1.f / device_data.output_resolution.y;
      device_data.cb_luma_global_settings_dirty = true;
   }

   void PrintImGuiAbout() override
   {
      ImGui::Text("WATCH_DOGS Luma mod - about and credits section", "");
   }
   
   static bool CreateTestSampler(ID3D11Device* native_device, GameDeviceDataWatchDogs& game_device_data)
   {
      // Return early if resources already exist with correct dimensions
      if (game_device_data.depth_sampler.get())
      {
         /*
         std::stringstream s;
         s << "MV: Resource exists and resolution unchanged";
         reshade::log::message(reshade::log::level::info, s.str().c_str());
         */
         return true;
      }
      
      HRESULT hr;
      
      D3D11_SAMPLER_DESC sampler_desc = {};
      // linear for fake depth
      sampler_desc.Filter = D3D11_FILTER::D3D11_FILTER_MIN_MAG_MIP_LINEAR;
      sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_MODE::D3D11_TEXTURE_ADDRESS_BORDER;
      sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_MODE::D3D11_TEXTURE_ADDRESS_BORDER;
      sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_MODE::D3D11_TEXTURE_ADDRESS_BORDER;
      sampler_desc.MipLODBias = 0.0f;
      sampler_desc.MaxAnisotropy = 0;
      sampler_desc.ComparisonFunc = D3D11_COMPARISON_FUNC::D3D11_COMPARISON_NEVER;
      sampler_desc.BorderColor[0] = 1.0f;
      sampler_desc.BorderColor[1] = 1.0f;
      sampler_desc.BorderColor[2] = 1.0f;
      sampler_desc.BorderColor[3] = 1.0f;
      sampler_desc.MinLOD = 0.0f;
      sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;
      
      hr = native_device->CreateSamplerState(&sampler_desc, &game_device_data.depth_sampler);
      if (FAILED(hr))
      {
         std::stringstream s;
         s << "Depth sampler: Creation Failed";
         reshade::log::message(reshade::log::level::info, s.str().c_str());
         return false;
      }
      return true;
   }
   
   static bool CreateMVResources(ID3D11Device* native_device, GameDeviceDataWatchDogs& game_device_data, float width, float height)
   {
      // Return early if resources already exist with correct dimensions
      if (game_device_data.motion_vector.get() && !game_device_data.resolution_changed)
      {
         /*
         std::stringstream s;
         s << "MV: Resource exists and resolution unchanged";
         reshade::log::message(reshade::log::level::info, s.str().c_str());
         */
         return true;
      }
      
      HRESULT hr;
      
      D3D11_TEXTURE2D_DESC depth_desc = {};
      depth_desc.Width = static_cast<UINT>(width);
      depth_desc.Height = static_cast<UINT>(height);
      depth_desc.MipLevels = 1;
      depth_desc.ArraySize = 1;
      depth_desc.Format = DXGI_FORMAT_R16G16_FLOAT;
      depth_desc.SampleDesc.Count = 1;
      depth_desc.Usage = D3D11_USAGE_DEFAULT;
      depth_desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
      
      hr = native_device->CreateTexture2D(&depth_desc, nullptr, &game_device_data.motion_vector);
      if (FAILED(hr))
      {
         std::stringstream s;
         s << "MV: Texture Creation Failed";
         reshade::log::message(reshade::log::level::info, s.str().c_str());
         return false;
      }
      
      D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
      uav_desc.Format = DXGI_FORMAT_R16G16_FLOAT;
      uav_desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
      uav_desc.Texture2D.MipSlice = 0;
      hr = native_device->CreateUnorderedAccessView(game_device_data.motion_vector.get(), &uav_desc, &game_device_data.motion_vector_uav);
      if (FAILED(hr))
      {
         std::stringstream s;
         s << "MV: UAV Creation Failed";
         reshade::log::message(reshade::log::level::info, s.str().c_str());
         return false;
      }
      
      depth_desc.Width = static_cast<UINT>(width);
      depth_desc.Height = static_cast<UINT>(height);
      depth_desc.MipLevels = 1;
      depth_desc.ArraySize = 1;
      depth_desc.Format = DXGI_FORMAT_R32_TYPELESS;
      depth_desc.SampleDesc.Count = 1;
      depth_desc.Usage = D3D11_USAGE_DEFAULT;
      depth_desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
      
      hr = native_device->CreateTexture2D(&depth_desc, nullptr, &game_device_data.unjitter_depth);
      if (FAILED(hr))
      {
         std::stringstream s;
         s << "Depth: Texture Creation Failed";
         reshade::log::message(reshade::log::level::info, s.str().c_str());
         return false;
      }
      
      uav_desc = {};
      uav_desc.Format = DXGI_FORMAT_R32_FLOAT;
      uav_desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
      uav_desc.Texture2D.MipSlice = 0;
      hr = native_device->CreateUnorderedAccessView(game_device_data.unjitter_depth.get(), &uav_desc, &game_device_data.unjitter_depth_uav);
      if (FAILED(hr))
      {
         std::stringstream s;
         s << "Depth: UAV Creation Failed";
         reshade::log::message(reshade::log::level::info, s.str().c_str());
         return false;
      }
      
      D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
      srv_desc.Format = DXGI_FORMAT_R32_FLOAT;
      srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
      srv_desc.Texture2D.MostDetailedMip = 0;
      srv_desc.Texture2D.MipLevels = 1;
      hr = native_device->CreateShaderResourceView(game_device_data.unjitter_depth.get(), &srv_desc, &game_device_data.unjitter_depth_srv);
      if (FAILED(hr))
      {
         std::stringstream s;
         s << "Depth: SRV Creation Failed";
         reshade::log::message(reshade::log::level::info, s.str().c_str());
         return false;
      }
      
      return true;
   }
   
   static bool CreateBeforeRainResources(ID3D11Device* native_device, GameDeviceDataWatchDogs& game_device_data, float width, float height)
   {
      // Return early if resources already exist with correct dimensions
      if (game_device_data.game_source_color_before_rain.get() && !game_device_data.resolution_changed)
      {
         /*
         std::stringstream s;
         s << "MV: Resource exists and resolution unchanged";
         reshade::log::message(reshade::log::level::info, s.str().c_str());
         */
         return true;
      }
      
      HRESULT hr;
      
      /*
      D3D11_TEXTURE2D_DESC depth_desc = {};
      depth_desc.Width = static_cast<UINT>(width);
      depth_desc.Height = static_cast<UINT>(height);
      depth_desc.MipLevels = 0;
      depth_desc.ArraySize = 1;
      depth_desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
      depth_desc.SampleDesc.Count = 1;
      depth_desc.Usage = D3D11_USAGE_DEFAULT;
      depth_desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
      */
      
      com_ptr<ID3D11Resource> output_color_resource;
      game_device_data.game_source_color_srv->GetResource(&output_color_resource);
                     
      com_ptr<ID3D11Texture2D> output_color;
      hr = output_color_resource->QueryInterface(&output_color);
      
      D3D11_TEXTURE2D_DESC source_color_texture_desc;
      output_color->GetDesc(&source_color_texture_desc);
      source_color_texture_desc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;
      
      hr = native_device->CreateTexture2D(&source_color_texture_desc, nullptr, &game_device_data.game_source_color_before_rain);
      if (FAILED(hr))
      {
         std::stringstream s;
         s << "Before Rain Copy: Texture Creation Failed";
         reshade::log::message(reshade::log::level::info, s.str().c_str());
         return false;
      }
      
      D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
      uav_desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
      uav_desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
      uav_desc.Texture2D.MipSlice = 0;
      hr = native_device->CreateUnorderedAccessView(game_device_data.game_source_color_before_rain.get(), &uav_desc, &game_device_data.game_source_color_before_rain_uav);
      if (FAILED(hr))
      {
         std::stringstream s;
         s << "Before Rain Copy: UAV Creation Failed";
         reshade::log::message(reshade::log::level::info, s.str().c_str());
         return false;
      }
      
      D3D11_RENDER_TARGET_VIEW_DESC rtv_desc = {};
      rtv_desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
      rtv_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
      rtv_desc.Texture2D.MipSlice = 0;
      hr = native_device->CreateRenderTargetView(game_device_data.game_source_color_before_rain.get(), &rtv_desc, &game_device_data.game_source_color_before_rain_rtv);
      if (FAILED(hr))
      {
         std::stringstream s;
         s << "Before Rain Copy: RTV Creation Failed";
         reshade::log::message(reshade::log::level::info, s.str().c_str());
         return false;
      }
      
      return true;
   }
   
   static constexpr uint32_t CBPerViewGlobal_buffer_size = 960;

   static void OnMapBufferRegion(reshade::api::device* device, reshade::api::resource resource, uint64_t offset, uint64_t size, reshade::api::map_access access, void** data)
   {
      ID3D11Device* native_device = (ID3D11Device*)(device->get_native());
      ID3D11Buffer* buffer = reinterpret_cast<ID3D11Buffer*>(resource.handle);
      DeviceData& device_data = *device->get_private_data<DeviceData>();
      auto& game_device_data = GetGameDeviceData(device_data);

      if (access == reshade::api::map_access::write_only || access == reshade::api::map_access::write_discard || access == reshade::api::map_access::read_write)
      {
         D3D11_BUFFER_DESC buffer_desc;
         buffer->GetDesc(&buffer_desc);

         if (buffer_desc.ByteWidth == CBPerViewGlobal_buffer_size/* && game_device_data.set_render_res*/)
         {
            device_data.cb_per_view_global_buffer = buffer;
#if DEVELOPMENT
            ASSERT_ONCE(buffer_desc.Usage == D3D11_USAGE_DYNAMIC && buffer_desc.BindFlags == D3D11_BIND_CONSTANT_BUFFER && buffer_desc.CPUAccessFlags == D3D11_CPU_ACCESS_WRITE && buffer_desc.MiscFlags == 0 && buffer_desc.StructureByteStride == 0);
#endif
            ASSERT_ONCE(!device_data.cb_per_view_global_buffer_map_data);
            device_data.cb_per_view_global_buffer_map_data = *data;
            
         }
      }
   }

   static void LogXMMatrix(const char* label, const DirectX::XMMATRIX& matrix, 
                    reshade::log::level level = reshade::log::level::debug)
   {
#if DEBUG_LOG      
      std::stringstream s;
      s << label << ":\n";
    
      for (int row = 0; row < 4; ++row)
      {
         s << "  [";
         for (int col = 0; col < 4; ++col)
         {
            // Format: fixed-point, 4 decimals, width 8 for alignment
            s << std::fixed << std::setprecision(8) << std::setw(12) 
              << matrix.r[row].m128_f32[col];
            if (col < 3) s << ", ";
         }
         s << "]";
         if (row < 3) s << "\n";
      }
    
      reshade::log::message(level, s.str().c_str());
#endif
   }
   
   static void OnUnmapBufferRegion(reshade::api::device* device, reshade::api::resource resource)
   {
      DeviceData& device_data = *device->get_private_data<DeviceData>();
      auto& game_device_data = GetGameDeviceData(device_data);
      ID3D11Device* native_device = (ID3D11Device*)(device->get_native());

      ID3D11Buffer* buffer = reinterpret_cast<ID3D11Buffer*>(resource.handle);

      bool is_global_cbuffer = device_data.cb_per_view_global_buffer != nullptr && device_data.cb_per_view_global_buffer == buffer;

      ASSERT_ONCE(!device_data.cb_per_view_global_buffer_map_data || is_global_cbuffer);

      if (is_global_cbuffer && device_data.cb_per_view_global_buffer_map_data != nullptr && device_data.sr_type != SR::Type::None)
      {
         float4(&float_data)[CBPerViewGlobal_buffer_size / sizeof(float4)] = *((float4(*)[CBPerViewGlobal_buffer_size / sizeof(float4)]) device_data.cb_per_view_global_buffer_map_data);

         bool is_valid_cbuffer = true
                                 //&& float_data[22].x == 0.f && float_data[22].y == 0.f
                                 &&
                                 float_data[35].x == game_device_data.render_res.x && float_data[35].y == game_device_data.render_res.y && float_data[35].z == game_device_data.render_res.z && float_data[35].w == game_device_data.render_res.w;
         
         if (!game_device_data.set_jitter)
         {
            volatile bool m_willRenderThisFrame = *reinterpret_cast<bool*>(manager_ptr + 114);
            volatile int32_t m_previousResetRequests = *reinterpret_cast<int32_t*>(manager_ptr + 116);
         
            void* inst = *reinterpret_cast<void**>(pattern_2_addresses[0]);
            volatile bool m_gridShadingEnable = *reinterpret_cast<bool*>(reinterpret_cast<std::byte*>(inst) + 268);
            Nexus::m_gridShadingEnable = m_gridShadingEnable;
         
            if (m_gridShadingEnable && !Nexus::m_gridShadingEnable)
            {
               Nexus::m_gridShadingEnable = m_gridShadingEnable;
               Nexus::m_noPreviousFrame = true;
            }
         
            if (m_willRenderThisFrame && !Nexus::m_willRenderThisFrame)
            {
               Nexus::m_willRenderThisFrame = m_willRenderThisFrame;
               Nexus::m_noPreviousFrame = true;
            }
         
            if (m_previousResetRequests != Nexus::m_previousResetRequests)
            {
               Nexus::m_previousResetRequests = m_previousResetRequests;
               Nexus::m_noPreviousFrame = true;
            }
#if DEVELOPMENT
            /*
            std::stringstream s;
            s << std::hex;
            s << "Manager: 0x" << manager_ptr;
            s << " | m_willRenderThisFrame: 0x" << m_willRenderThisFrame;
            s << " | m_previousResetRequests: 0x" << m_previousResetRequests;
            reshade::log::message(reshade::log::level::info, s.str().c_str());

               
            s.clear();
            s.str("");
            s << "m_gridShading = " << std::hex << m_gridShadingEnable;
            s << " at " << std::hex << (reinterpret_cast<std::byte*>(inst) + 268);
            reshade::log::message(reshade::log::level::info, s.str().c_str());
            */
#endif
         }
         if (is_valid_cbuffer && !has_changed_all_viewport && !Nexus::m_gridShadingEnable && Nexus::m_willRenderThisFrame)
         {
#if DEVELOPMENT
            /*
            std::stringstream s;
            s.clear();
            s.str("");
            s << "Found cbuffer viewport to change: " << i << " handle: 0x" << std::hex << resource.handle;
            reshade::log::message(reshade::log::level::info, s.str().c_str());
            */
            i++;
            map_changed = true;
#endif
            if (!game_device_data.set_jitter)
            {
               game_device_data.CameraDistances = {float_data[34].x, float_data[34].y};
               game_device_data.camera_position_current = {float_data[36].x, float_data[36].y, float_data[36].z};
               
               using namespace DirectX;
               
               // ------------------------------------------------------------
               // Backup original matrices
               // ------------------------------------------------------------
               game_device_data.ViewRotProjectionMatrixOriginal = XMLoadFloat4x4(reinterpret_cast<XMFLOAT4X4*>(&float_data[0]));
               game_device_data.ViewProjectionMatrixOriginal = XMLoadFloat4x4(reinterpret_cast<XMFLOAT4X4*>(&float_data[4]));
               game_device_data.ProjectionMatrixOriginal = XMLoadFloat4x4(reinterpret_cast<XMFLOAT4X4*>(&float_data[8]));
               game_device_data.InvProjectionMatrixOriginal = XMLoadFloat4x4(reinterpret_cast<XMFLOAT4X4*>(&float_data[12]));
               game_device_data.InvProjectionMatrixDepthOriginal = XMLoadFloat4x4(reinterpret_cast<XMFLOAT4X4*>(&float_data[16]));
               game_device_data.PreviousViewProjectionMatrixOriginal = XMLoadFloat4x4(reinterpret_cast<XMFLOAT4X4*>(&float_data[30]));

               // ------------------------------------------------------------
               // Load original matrices
               // ------------------------------------------------------------
               XMMATRIX view =
                  XMLoadFloat4x4(reinterpret_cast<XMFLOAT4X4*>(&float_data[24]));
               
               LogXMMatrix("View Matrix", view);
               
               view.r[3] = XMVectorSet(0.f, 0.f, 0.f, 1.f); // Clear translation row
               
               LogXMMatrix("View Matrix Fixed", view);

               XMMATRIX baseProjection =
                  XMLoadFloat4x4(reinterpret_cast<XMFLOAT4X4*>(&float_data[8]));

               XMMATRIX originalInvProjectionDepth =
                  XMLoadFloat4x4(reinterpret_cast<XMFLOAT4X4*>(&float_data[16]));

               // ------------------------------------------------------------
               // Use previous VP if last frame is jittered
               // ------------------------------------------------------------
               
               // Don't jitter the previous vp seems to give better MV (dejitter) result
               if (game_device_data.is_last_frame_jittered)
               {
                  XMMATRIX previousVP = game_device_data.ViewRotProjectionMatrix;
                  game_device_data.PreviousViewProjectionMatrix = previousVP;
                  
                  XMMATRIX previousVPOriginal =
                  XMLoadFloat4x4(reinterpret_cast<XMFLOAT4X4*>(&float_data[30]));
                  
                  LogXMMatrix("Jittered PreviousViewProjectionMatrix", game_device_data.PreviousViewProjectionMatrix);
                  LogXMMatrix("Original PreviousViewProjectionMatrix", previousVPOriginal);

                  XMStoreFloat4x4(
                     reinterpret_cast<XMFLOAT4X4*>(&float_data[30]),
                     previousVP);
               }
               
               // Copy prev came pos into "_UncompressDepthWeights_ShadowProjDepthMinValue.xyz"
               memcpy(&float_data[46], &game_device_data.camera_position_previous, sizeof(game_device_data.camera_position_previous));

               // ------------------------------------------------------------
               // Apply jitter to proj
               // ------------------------------------------------------------

               float2 jitter = game_device_data.taa_jitters;

               float jitterX = (jitter.x * 2.0f) / game_device_data.render_res.x;
               float jitterY = (-jitter.y * 2.0f) / game_device_data.render_res.y;

               XMMATRIX jitteredProjection = baseProjection;

               jitteredProjection.r[0].m128_f32[2] += jitterX;
               jitteredProjection.r[1].m128_f32[2] += jitterY;
               
               // ------------------------------------------------------------
               // Recompute matrices
               // ------------------------------------------------------------
               XMMATRIX viewProjection =
                  XMMatrixMultiply(jitteredProjection, view);

               XMMATRIX viewRot = view;
               viewRot.r[0].m128_f32[3] = 0.f; // Clear X translation
               viewRot.r[1].m128_f32[3] = 0.f; // Clear Y translation
               viewRot.r[2].m128_f32[3] = 0.f; // Clear Z translation
               
               // this is exactly why it doesnt work.. matrix is too inaccurate
#if 0
               XMMATRIX newPreviousTranslation = XMMatrixTranslation(-game_device_data.camera_position_previous.x, -game_device_data.camera_position_previous.y, -game_device_data.camera_position_previous.z);
               newPreviousTranslation = XMMatrixTranspose(newPreviousTranslation);
               LogXMMatrix("PreviousTranslation", newPreviousTranslation);
               XMMATRIX newPreviousVP = XMMatrixMultiply(viewRot, newPreviousTranslation);
               newPreviousVP = XMMatrixMultiply(baseProjection, newPreviousVP);
               LogXMMatrix("New PreviousViewProjectionMatrix", newPreviousVP);
#endif
               
               LogXMMatrix("View Rotation Matrix", viewRot);

               XMMATRIX viewRotProjection =
                  XMMatrixMultiply(jitteredProjection, viewRot);

               XMMATRIX invProjection =
                  XMMatrixInverse(nullptr, jitteredProjection);

               XMMATRIX invProjectionDepth = invProjection;
               invProjectionDepth.r[2] = originalInvProjectionDepth.r[2];

               // ------------------------------------------------------------
               // Update matrices in CB Viewport
               // ------------------------------------------------------------

               XMStoreFloat4x4(
                  reinterpret_cast<XMFLOAT4X4*>(&float_data[0]),
                  viewRotProjection);

               XMStoreFloat4x4(
                  reinterpret_cast<XMFLOAT4X4*>(&float_data[4]),
                  viewProjection);

               XMStoreFloat4x4(
                  reinterpret_cast<XMFLOAT4X4*>(&float_data[8]),
                  jitteredProjection);

               XMStoreFloat4x4(
                  reinterpret_cast<XMFLOAT4X4*>(&float_data[12]),
                  invProjection);

               XMStoreFloat4x4(
                  reinterpret_cast<XMFLOAT4X4*>(&float_data[16]),
                  invProjectionDepth);

               // ------------------------------------------------------------
               // Cache current frame matrices
               // ------------------------------------------------------------

               game_device_data.ViewRotProjectionMatrix = viewRotProjection;
               game_device_data.ViewProjectionMatrix = viewProjection;
               game_device_data.ProjectionMatrix = jitteredProjection;
               game_device_data.InvProjectionMatrix = invProjection;
               game_device_data.InvProjectionMatrixDepth = invProjectionDepth;
               
               LogXMMatrix("Jittered ViewRotProjectionMatrix", game_device_data.ViewRotProjectionMatrix);
               LogXMMatrix("Original ViewRotProjectionMatrix", game_device_data.ViewRotProjectionMatrixOriginal);
               
               LogXMMatrix("Jittered ViewProjectionMatrix", game_device_data.ViewProjectionMatrix);
               LogXMMatrix("Original ViewProjectionMatrix", game_device_data.ViewProjectionMatrixOriginal);
               
               LogXMMatrix("Jittered ProjectionMatrix", game_device_data.ProjectionMatrix);
               LogXMMatrix("Original ProjectionMatrix", game_device_data.ProjectionMatrixOriginal);
               
               LogXMMatrix("Jittered InvProjectionMatrix", game_device_data.InvProjectionMatrix);
               LogXMMatrix("Original InvProjectionMatrix", game_device_data.InvProjectionMatrixOriginal);
               
               LogXMMatrix("Jittered InvProjectionMatrixDepth", game_device_data.InvProjectionMatrixDepth);
               LogXMMatrix("Original InvProjectionMatrixDepth", game_device_data.InvProjectionMatrixDepthOriginal);
               
               //if (game_device_data.camera_position_previous.x != 0.0 && game_device_data.camera_position_previous.y != 0.0 && game_device_data.camera_position_previous.z != 0.0)
               {
                  XMMATRIX CurrInvView = XMLoadFloat4x4(reinterpret_cast<XMFLOAT4X4*>(&float_data[27]));
                  
                  CurrInvView.r[3] = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
                  
                  float4 positionDelta = {0.0,0.0,0.0,1.0};
                  positionDelta.x = game_device_data.camera_position_current.x - game_device_data.camera_position_previous.x;
                  positionDelta.y = game_device_data.camera_position_current.y - game_device_data.camera_position_previous.y;
                  positionDelta.z = game_device_data.camera_position_current.z - game_device_data.camera_position_previous.z;
                  
                  XMMATRIX PrevInvViewFull = game_device_data.PreviousInvViewMatrix;

                  // Zero translation in previous (keep only rotation)
                  XMMATRIX PrevRotationOnly = PrevInvViewFull;
                  PrevRotationOnly.r[0].m128_f32[3] = 0.0;
                  PrevRotationOnly.r[1].m128_f32[3] = 0.0;
                  PrevRotationOnly.r[2].m128_f32[3] = 0.0;
                  PrevRotationOnly.r[3] = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
                  
                  // transpose, essentially previous view matrix but rotation only?
                  {
                     XMVECTOR c0 = PrevRotationOnly.r[0];
                     XMVECTOR c1 = PrevRotationOnly.r[1];
                     XMVECTOR c2 = PrevRotationOnly.r[2];

                     PrevRotationOnly.r[0] = XMVectorSet(XMVectorGetX(c0), XMVectorGetX(c1), XMVectorGetX(c2), 0.0f);
                     PrevRotationOnly.r[1] = XMVectorSet(XMVectorGetY(c0), XMVectorGetY(c1), XMVectorGetY(c2), 0.0f);
                     PrevRotationOnly.r[2] = XMVectorSet(XMVectorGetZ(c0), XMVectorGetZ(c1), XMVectorGetZ(c2), 0.0f);
                     PrevRotationOnly.r[3] = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
                  }
                  
                  XMMATRIX CurrWithDelta = CurrInvView;
                  CurrWithDelta.r[0].m128_f32[3] = positionDelta.x;
                  CurrWithDelta.r[1].m128_f32[3] = positionDelta.y;
                  CurrWithDelta.r[2].m128_f32[3] = positionDelta.z;
                  
                  LogXMMatrix("CurrentInvViewMatrix_CurrWithDelta", CurrWithDelta);
                  LogXMMatrix("PreviousInvViewMatrix_PrevRotationOnly", PrevRotationOnly);
                  
                  XMMATRIX temp = XMMatrixMultiply(game_device_data.PreviousProjectionMatrix, PrevRotationOnly);
                  game_device_data.CameraSpaceToPreviousProjectedSpace = XMMatrixMultiply(temp, CurrWithDelta);
                  
                  LogXMMatrix("CameraSpaceToPreviousProjectedSpace", game_device_data.CameraSpaceToPreviousProjectedSpace);
#if DEBUG_LOG 
                  {
                     std::stringstream s;
                     s.clear();
                     s.str("");
                     s << "current campos: " << game_device_data.camera_position_current.x;
                     s << ", " << game_device_data.camera_position_current.y;
                     s << ", " << game_device_data.camera_position_current.z;
                     
                     s << " | prev campos: " << game_device_data.camera_position_previous.x;
                     s << ", " << game_device_data.camera_position_previous.y;
                     s << ", " << game_device_data.camera_position_previous.z;
                     reshade::log::message(reshade::log::level::info, s.str().c_str());
                  }
#endif
                  game_device_data.PreviousProjectionMatrixCopy = game_device_data.PreviousProjectionMatrix;
                  game_device_data.PreviousViewMatrixCopy = game_device_data.PreviousViewMatrix;
                  
                  game_device_data.PreviousViewMatrix = XMLoadFloat4x4(reinterpret_cast<XMFLOAT4X4*>(&float_data[24]));
                  game_device_data.PreviousProjectionMatrix = game_device_data.ProjectionMatrix;
                  game_device_data.PreviousInvViewMatrix = CurrInvView;
               }

               game_device_data.set_jitter = true;
            }
            else
            {
               using namespace DirectX;

               XMStoreFloat4x4(
                  reinterpret_cast<XMFLOAT4X4*>(&float_data[0]),
                  game_device_data.ViewRotProjectionMatrix);

               XMStoreFloat4x4(
                  reinterpret_cast<XMFLOAT4X4*>(&float_data[4]),
                  game_device_data.ViewProjectionMatrix);

               XMStoreFloat4x4(
                  reinterpret_cast<XMFLOAT4X4*>(&float_data[8]),
                  game_device_data.ProjectionMatrix);

               XMStoreFloat4x4(
                  reinterpret_cast<XMFLOAT4X4*>(&float_data[12]),
                  game_device_data.InvProjectionMatrix);

               XMStoreFloat4x4(
                  reinterpret_cast<XMFLOAT4X4*>(&float_data[16]),
                  game_device_data.InvProjectionMatrixDepth);

               // Don't jitter the previous vp seems to give better MV (dejitter) result
               if (game_device_data.is_last_frame_jittered)
               {
                  XMStoreFloat4x4(
                     reinterpret_cast<XMFLOAT4X4*>(&float_data[30]),
                     game_device_data.PreviousViewProjectionMatrix);
               }
               
               // Copy prev came pos into "_UncompressDepthWeights_ShadowProjDepthMinValue.xyz"
               memcpy(&float_data[46], &game_device_data.camera_position_previous, sizeof(game_device_data.camera_position_previous));
            }
         }
      }
      device_data.cb_per_view_global_buffer_map_data = nullptr;
      device_data.cb_per_view_global_buffer = nullptr;
   }
   
   static bool OnClearDepthStencilView(reshade::api::command_list* cmd_list, reshade::api::resource_view dsv, const float *depth, const uint8_t *stencil, uint32_t rect_count, const reshade::api::rect* rects)
   {
      auto* device = cmd_list->get_device();
      DeviceData& device_data = *device->get_private_data<DeviceData>();
      auto& game_device_data = GetGameDeviceData(device_data);
      if (game_device_data.game_depth_dsv.get() == nullptr)
      {
         auto current_rtv_resource = device->get_resource_from_view(dsv);
         auto current_rtv = device->get_resource_desc(current_rtv_resource);

         if (current_rtv.texture.format == reshade::api::format::r24_g8_typeless && (current_rtv.texture.width == game_device_data.render_res.x && current_rtv.texture.height == game_device_data.render_res.y))
         {
            game_device_data.game_depth_dsv = reinterpret_cast<ID3D11DepthStencilView*>(dsv.handle);
         }
      }
      return false;
   }

   static bool OnClearRenderTargetView(reshade::api::command_list* cmd_list, reshade::api::resource_view rtv, const float color[4], uint32_t rect_count, const reshade::api::rect* rects)
   {
      if (will_draw_rain && !has_forward_pass_finished)
      {
         has_forward_pass_finished = true;
      }
      
      auto* device = cmd_list->get_device();
      DeviceData& device_data = *device->get_private_data<DeviceData>();
      auto& game_device_data = GetGameDeviceData(device_data);
      if (!game_device_data.set_render_res)
      {
         if (color[0] == 0.0 && color[1] == -1.0 && color[2] == 0.0 && color[3] == 1.0)
         {
            auto current_rtv_resource = device->get_resource_from_view(rtv);
            auto current_rtv = device->get_resource_desc(current_rtv_resource);

            if (current_rtv.texture.format == reshade::api::format::r16g16_float || current_rtv.texture.format == reshade::api::format::r16g16_typeless)
            {
               float width = static_cast<float>(current_rtv.texture.width);
               float height = static_cast<float>(current_rtv.texture.height);
               if ((width / height) != 1.0)
               {
                  game_device_data.game_motion_vector = reinterpret_cast<ID3D11Texture2D*>(current_rtv_resource.handle);
                  if (width != game_device_data.render_res.x || height != game_device_data.render_res.y)
                  {

                     std::stringstream s;
                     s << "Width = " << current_rtv.texture.width;
                     s << ", Height = " << current_rtv.texture.height;
                     s << ", Format = " << current_rtv.texture.format;
                     reshade::log::message(reshade::log::level::info, s.str().c_str());

                     game_device_data.render_res.x = width;
                     game_device_data.render_res.y = height;
                     game_device_data.render_res.z = 1.0 / width;
                     game_device_data.render_res.w = 1.0 / height;
                     game_device_data.set_render_res = true;
                     game_device_data.resolution_changed = true;
                     
                     device_data.render_resolution.x = width;
                     device_data.render_resolution.y = height;
                     device_data.output_resolution.x = width;
                     device_data.output_resolution.y = height;
                     
                     game_device_data.CleanMVResources();
                  }
                  else
                  {
                     game_device_data.set_render_res = true;
                     game_device_data.resolution_changed = false;
                  }
                  /*
                  game_device_data.game_motion_vector_rtv = reinterpret_cast<ID3D11RenderTargetView*>(rtv.handle);
               
                  ID3D11Device* native_device = nullptr;
                  game_device_data.game_motion_vector_rtv->GetDevice(&native_device);
               
                  ID3D11Resource* game_mv = reinterpret_cast<ID3D11Resource*>(current_rtv_resource.handle);
               
                  HRESULT hr;
                  D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
                  srv_desc.Format = DXGI_FORMAT_R16G16_FLOAT;
                  srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                  srv_desc.Texture2D.MostDetailedMip = 0;
                  srv_desc.Texture2D.MipLevels = 1;
                  hr = native_device->CreateShaderResourceView(game_mv, &srv_desc, &game_device_data.game_motion_vector_srv);
                  if (FAILED(hr))
                  {
                     std::stringstream s;
                     s << "MV: Game SRV Creation Failed";
                     reshade::log::message(reshade::log::level::info, s.str().c_str());
                     return false;
                  }
                  native_device->Release();
                  game_mv->Release();
                  */
               }
            }
         }
      }
      return false;
   }

   static bool OnCreateResource(reshade::api::device* device, reshade::api::resource_desc& desc, reshade::api::subresource_data* initial_data, reshade::api::resource_usage initial_state)
   {
      if (desc.type == reshade::api::resource_type::texture_2d && desc.texture.format == reshade::api::format::r24_g8_typeless && (desc.usage & reshade::api::resource_usage::depth_stencil) == reshade::api::resource_usage::depth_stencil)
      {
         //if (desc.texture.width/desc.texture.height == aspect_ratio)
         {
            desc.texture.format = reshade::api::format::r32_g8_typeless;
            reshade::log::message(reshade::log::level::info, "Resource upgrade: depth r24_g8_typeless >> r32_g8_typeless");
            std::stringstream s;
            s << "Width = " << desc.texture.width;
            s << ", Height = " << desc.texture.height;
            s << ", Support = " << device->check_format_support(reshade::api::format::d32_float_s8_uint, reshade::api::resource_usage::depth_stencil);
            reshade::log::message(reshade::log::level::info, s.str().c_str());
            return true;
         }
      }
      return false;
   }
   
   static bool OnCreateResourceView(reshade::api::device *device, reshade::api::resource resource, reshade::api::resource_usage usage_type, reshade::api::resource_view_desc &desc)
   {
      if (desc.type == reshade::api::resource_view_type::texture_2d && format_to_depth_stencil_typed(desc.format) == reshade::api::format::d24_unorm_s8_uint)
      {
         reshade::api::resource_desc resource_desc = device->get_resource_desc(resource);
         //if (resource_desc.texture.width/resource_desc.texture.height == aspect_ratio)
         {
            //desc.format = reshade::api::format::d32_float_s8_uint;
            switch (desc.format)
            {
            case reshade::api::format::d24_unorm_s8_uint:
               desc.format = reshade::api::format::d32_float_s8_uint;
               reshade::log::message(reshade::log::level::info, "Resource view upgrade: depth d24_unorm_s8_uint >> d32_float_s8_uint");
               return true;

            case reshade::api::format::r24_g8_typeless:
               desc.format = reshade::api::format::r32_g8_typeless;
               reshade::log::message(reshade::log::level::info, "Resource view upgrade: depth r24_g8_typeless >> r32_g8_typeless");
               return true;

            case reshade::api::format::r24_unorm_x8_uint:
               desc.format = reshade::api::format::r32_float_x8_uint;
               reshade::log::message(reshade::log::level::info, "Resource view upgrade: depth r24_unorm_x8_uint >> r32_float_x8_uint");
               return true;
               
            case reshade::api::format::x24_unorm_g8_uint:
               desc.format = reshade::api::format::x32_float_g8_uint;
               reshade::log::message(reshade::log::level::info, "Resource view upgrade: depth x24_unorm_g8_uint >> x32_float_g8_uint");
               return true;
            }
            std::stringstream s;
            s << "Width = " << resource_desc.texture.width;
            s << ", Height = " << resource_desc.texture.height;
            reshade::log::message(reshade::log::level::info, s.str().c_str());
            //return true;
         }
      }
      return false;
   }
   
   WatchDogs() = default;

   DrawOrDispatchOverrideType OnDrawOrDispatch(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, reshade::api::shader_stage stages, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, bool is_custom_pass, bool& updated_cbuffers, std::function<void()>* original_draw_dispatch_func) override
   {
      if (original_shader_hashes.Contains(shader_hashes_Downsample))
      {
         auto& game_device_data = GetGameDeviceData(device_data);
         native_device_context->PSGetSamplers(0, 1, &game_device_data.game_reflection_sampler);
         native_device_context->PSGetShaderResources(0, 1, &game_device_data.game_reflection_srv);
         return DrawOrDispatchOverrideType::None;
      }
      
      if (original_shader_hashes.Contains(shader_hashes_LightProbesUpdate))
      {
         auto& game_device_data = GetGameDeviceData(device_data);
         native_device_context->CSSetSamplers(12, 1, &game_device_data.game_reflection_sampler);
         native_device_context->CSSetShaderResources(12, 1, &game_device_data.game_reflection_srv);
         return DrawOrDispatchOverrideType::None;
         native_device_context->CSSetShaderResources(12, 1, 0);
      }
      
      if (original_shader_hashes.Contains(shader_hashes_RainStreak))
      {
         if (device_data.sr_type == SR::Type::None)
         {
            return DrawOrDispatchOverrideType::None;
         }
         
         will_draw_rain = true;
         auto& game_device_data = GetGameDeviceData(device_data);
         if (CreateBeforeRainResources(native_device, game_device_data, game_device_data.render_res.x, game_device_data.render_res.y))
         {
            if (!has_copied_before_rain)
            {
               com_ptr<ID3D11Resource> output_color_resource;
               game_device_data.game_source_color_srv->GetResource(&output_color_resource);
               native_device_context->CopyResource(game_device_data.game_source_color_before_rain.get(), output_color_resource.get());
               
               //reshade::log::message(reshade::log::level::info, "Before Rain: Copying source color");
               
               has_copied_before_rain = true;
            }
         }
         return DrawOrDispatchOverrideType::None;
      }
      
      // We will draw these twice, not a whole lot after rain so its okay
      if (will_draw_rain && !has_forward_pass_finished && !original_shader_hashes.Contains(shader_hashes_RainStreak))
      {
         if (device_data.sr_type == SR::Type::None)
         {
            return DrawOrDispatchOverrideType::None;
         }
         
         //if (stages == reshade::api::shader_stage::pixel)
         {
            auto& game_device_data = GetGameDeviceData(device_data);
            
            com_ptr<ID3D11RenderTargetView> rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
            com_ptr<ID3D11DepthStencilView> depth_stencil_view;
            native_device_context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, &rtvs[0], &depth_stencil_view);
            
            com_ptr<ID3D11RenderTargetView> original_rtv;
            
            original_rtv = rtvs[0];
            rtvs[0] = game_device_data.game_source_color_before_rain_rtv;
            native_device_context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, &rtvs[0], depth_stencil_view.get());
         
            if (last_draw_dispatch_data.indexed)
            {
               if (last_draw_dispatch_data.instance_count == 0)
               {
                  native_device_context->DrawIndexed(last_draw_dispatch_data.index_count, last_draw_dispatch_data.first_index, last_draw_dispatch_data.vertex_offset);
               }
               else
               {
                  native_device_context->DrawIndexedInstanced(last_draw_dispatch_data.index_count, last_draw_dispatch_data.instance_count, last_draw_dispatch_data.first_index, last_draw_dispatch_data.vertex_offset, last_draw_dispatch_data.first_instance);
               }
            }
            else
            {
               if (last_draw_dispatch_data.instance_count == 0)
               {
                  native_device_context->Draw(last_draw_dispatch_data.vertex_count, last_draw_dispatch_data.first_vertex);
               }
               else
               {
                  native_device_context->DrawInstanced(last_draw_dispatch_data.vertex_count, last_draw_dispatch_data.instance_count, last_draw_dispatch_data.first_vertex, last_draw_dispatch_data.first_instance);
               }
            }
            
            rtvs[0] = original_rtv;
            native_device_context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, &rtvs[0], depth_stencil_view.get());
            return DrawOrDispatchOverrideType::None;
         }
         
      }
      
      if (original_shader_hashes.Contains(shader_hashes_DeferredFXAntialias) || (original_shader_hashes.Contains(shader_hashes_DeferredFXAntialias_NO_PREVIOUS_FRAME)))
      {
         has_taa_drawn = true;
         
         com_ptr<ID3D11ShaderResourceView> ps_shader_resources[4];
         native_device_context->PSGetShaderResources(0, ARRAYSIZE(ps_shader_resources), &ps_shader_resources[0]);
         
         auto& game_device_data = GetGameDeviceData(device_data);
         game_device_data.game_depth_srv = ps_shader_resources[0];
         game_device_data.game_source_color_srv = ps_shader_resources[1];
         game_device_data.game_motion_vector_srv = ps_shader_resources[2];
         
         com_ptr<ID3D11Buffer> constant_buffers[2];
         native_device_context->PSGetConstantBuffers(0, 2, &constant_buffers[0]);
         game_device_data.game_viewport_cbv = constant_buffers[0];
         if (device_data.sr_type != SR::Type::None)
         {
            return DrawOrDispatchOverrideType::Skip; // Don't cancel the original draw call
         }
      }

      if (original_shader_hashes.Contains(shader_hashes_DeferredFXAntialias_RESOLVE))
      {
         auto& game_device_data = GetGameDeviceData(device_data);
         native_device_context->OMGetRenderTargets(1, &game_device_data.game_source_color_rtv, 0);
#if DEBUG_MV
         if (CreateMVResources(native_device, game_device_data, game_device_data.render_res.x, game_device_data.render_res.y))
         {
            // TODO: add exposure texture support (it's possibly calculated just earlier in the auto exposure steps, but they could be after DLSS too, depends on UE), either way auto exposure is ok
            DrawStateStack<DrawStateStackType::FullGraphics> draw_state_stack;
            DrawStateStack<DrawStateStackType::Compute> compute_state_stack;
            // We don't actually replace the shaders with the classic luma shader swapping feature, so we need to set the CBs manually
            draw_state_stack.Cache(native_device_context, device_data.uav_max_count);
            compute_state_stack.Cache(native_device_context, device_data.uav_max_count);
            std::stringstream s;
            s << "TAA: Resolve and bind MV";
            reshade::log::message(reshade::log::level::info, s.str().c_str());
            
            native_device_context->PSSetShaderResources(0, 1, {&game_device_data.game_motion_vector_srv});
            native_device_context->Draw(4, 0);
            
            draw_state_stack.Restore(native_device_context);
            compute_state_stack.Restore(native_device_context);
            
            return DrawOrDispatchOverrideType::Skip; // Don't cancel the original draw call
         }
#endif
         if (device_data.sr_type != SR::Type::None)
         {
            return DrawOrDispatchOverrideType::Skip; // Don't cancel the original draw call
         }
      }
      /*
      else if (original_shader_hashes.Contains(shader_hashes_DeferredFXAntialias_NO_PREVIOUS_FRAME))
      {
         std::stringstream s;
         s << "TAA: No previous frame (Shader)";
         reshade::log::message(reshade::log::level::info, s.str().c_str());
      }
      */
      if (original_shader_hashes.Contains(shader_hashes_SMAA))
      {
         if (device_data.sr_type != SR::Type::None)
         {
            return DrawOrDispatchOverrideType::Skip;
         }
      }
      
      if (original_shader_hashes.Contains((shader_hashes_WaterHeightMap)))
      {
         has_changed_all_viewport = true;
         /*
         std::stringstream s;
         s << "WaterHeightMap: Draw";
         reshade::log::message(reshade::log::level::info, s.str().c_str());
         */
         
         if (has_taa_drawn && !has_sr_draw_tried && !Nexus::m_gridShadingEnable)
         {
            has_sr_draw_tried = true;
            auto& game_device_data = GetGameDeviceData(device_data);
            if (CreateMVResources(native_device, game_device_data, game_device_data.render_res.x, game_device_data.render_res.y))
            {
               // TODO: add exposure texture support (it's possibly calculated just earlier in the auto exposure steps, but they could be after DLSS too, depends on UE), either way auto exposure is ok
               DrawStateStack<DrawStateStackType::FullGraphics> draw_state_stack;
               DrawStateStack<DrawStateStackType::Compute> compute_state_stack;
               // We don't actually replace the shaders with the classic luma shader swapping feature, so we need to set the CBs manually
               draw_state_stack.Cache(native_device_context, device_data.uav_max_count);
               compute_state_stack.Cache(native_device_context, device_data.uav_max_count);
               
               if (CreateTestSampler(native_device, game_device_data))
               {
                  SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, reshade::api::shader_stage::compute, LumaConstantBufferType::LumaData);
                  ID3D11ShaderResourceView* srvs[] = {game_device_data.game_motion_vector_srv.get(), game_device_data.game_depth_srv.get()};
                  ID3D11UnorderedAccessView* uavs[] = {game_device_data.motion_vector_uav.get(), game_device_data.unjitter_depth_uav.get()};
                  ID3D11Buffer* buffers[] = {game_device_data.game_viewport_cbv.get()};
                  native_device_context->CSSetShader(device_data.native_compute_shaders[CompileTimeStringHash("Decode Motion Vector")].get(), nullptr, 0);
                  native_device_context->CSSetShaderResources(0, 2, srvs);
                  native_device_context->CSSetConstantBuffers(0, 1, buffers);
                  native_device_context->CSSetSamplers(0, 1, &game_device_data.depth_sampler);
                  native_device_context->CSSetUnorderedAccessViews(0, 2, uavs, nullptr);
               
                  native_device_context->Dispatch((game_device_data.render_res.x + 7) / 8, (game_device_data.render_res.y + 7) / 8, 1);
                  
                  //unbind the views for bindings
                  uavs[0] = 0;
                  uavs[1] = 0;
                  native_device_context->CSSetUnorderedAccessViews(0, 2, uavs, nullptr);
                  srvs[0] = 0;
                  srvs[1] = 0;
                  native_device_context->CSSetShaderResources(0, 2, srvs);
                  
                  native_device_context->CopyResource(game_device_data.game_motion_vector.get(), game_device_data.motion_vector.get());
               }
               
               if(device_data.sr_type != SR::Type::None && !device_data.sr_suppressed)
               {
                  const bool dlss_inputs_valid = game_device_data.game_source_color_srv.get() && game_device_data.game_depth_srv.get() && game_device_data.motion_vector_uav.get() && game_device_data.game_motion_vector_srv.get();
                  ASSERT_ONCE(dlss_inputs_valid);
                  if (dlss_inputs_valid)
                  {
                     auto* sr_instance_data = device_data.GetSRInstanceData();
                     ASSERT_ONCE(sr_instance_data);
                     
                     com_ptr<ID3D11Resource> output_color_resource;
                     game_device_data.game_source_color_srv->GetResource(&output_color_resource);
                     
                     com_ptr<ID3D11Texture2D> output_color;
                     HRESULT hr = output_color_resource->QueryInterface(&output_color);
                     


                     D3D11_TEXTURE2D_DESC taa_output_texture_desc;
                     output_color->GetDesc(&taa_output_texture_desc);
                     
                     SR::SettingsData settings_data;
                     settings_data.output_width = unsigned int(game_device_data.render_res.x + 0.5);
                     settings_data.output_height = unsigned int(game_device_data.render_res.y + 0.5);
                     settings_data.render_width = unsigned int(game_device_data.render_res.x + 0.5);
                     settings_data.render_height = unsigned int(game_device_data.render_res.y + 0.5);
                     settings_data.hdr = true;
                     settings_data.inverted_depth = false;   //in WD1, 0 = near, 1 = far
                     settings_data.mvs_jittered = false;
                     settings_data.auto_exposure = device_data.sr_type != SR::Type::FSR;
                     // MVs in UV space, so we need to scale by the render resolution to transform to pixel space
                     settings_data.mvs_x_scale = -game_device_data.render_res.x;
                     settings_data.mvs_y_scale = -game_device_data.render_res.y;
                     settings_data.render_preset = dlss_render_preset;
                     sr_implementations[device_data.sr_type]->UpdateSettings(sr_instance_data, native_device_context, settings_data);
                     
                     bool skip_dlss = taa_output_texture_desc.Width < sr_instance_data->min_resolution || taa_output_texture_desc.Height < sr_instance_data->min_resolution;
                     bool dlss_output_changed = false;
                     
                     // We force copy the game source color
                     bool dlss_output_supports_uav = false;
                     if (!dlss_output_supports_uav)
                     {
                        D3D11_TEXTURE2D_DESC dlss_output_texture_desc = taa_output_texture_desc;
                        dlss_output_texture_desc.Width = std::lrintf(game_device_data.render_res.x);
                        dlss_output_texture_desc.Height = std::lrintf(game_device_data.render_res.y);
                        dlss_output_texture_desc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;

                        if (device_data.sr_output_color.get())
                        {
                           D3D11_TEXTURE2D_DESC prev_dlss_output_texture_desc;
                           device_data.sr_output_color->GetDesc(&prev_dlss_output_texture_desc);
                           dlss_output_changed = prev_dlss_output_texture_desc.Width != dlss_output_texture_desc.Width || prev_dlss_output_texture_desc.Height != dlss_output_texture_desc.Height || prev_dlss_output_texture_desc.Format != dlss_output_texture_desc.Format;
                        }
                        if (!device_data.sr_output_color.get() || dlss_output_changed)
                        {
                           device_data.sr_output_color = nullptr; // Make sure we discard the previous one
                           hr = native_device->CreateTexture2D(&dlss_output_texture_desc, nullptr, &device_data.sr_output_color);
                           ASSERT_ONCE(SUCCEEDED(hr));
                           
                           D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
                           uav_desc.Format = dlss_output_texture_desc.Format;
                           uav_desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
                           uav_desc.Texture2D.MipSlice = 0;
                           hr = native_device->CreateUnorderedAccessView(device_data.sr_output_color.get(), &uav_desc, &game_device_data.sr_output_uav);
                           ASSERT_ONCE(SUCCEEDED(hr));
                        }
                        // Texture creation failed, we can't proceed with DLSS
                        if (!device_data.sr_output_color.get())
                        {
                           skip_dlss = true;
                        }
                     }
                     else
                     {
                        ASSERT_ONCE(device_data.sr_output_color == nullptr);
                        device_data.sr_output_color = output_color;
                     }
                     
                     if (!skip_dlss)
                     {
                        com_ptr<ID3D11Resource> sr_source_color;
                        game_device_data.game_source_color_srv->GetResource(&sr_source_color);

                        if (will_draw_rain /*&& test_index == 17*/)
                        {
                           sr_source_color= game_device_data.game_source_color_before_rain.get();
                        }
                        
                        com_ptr<ID3D11Resource> depth;
                        game_device_data.game_depth_srv->GetResource(&depth);

                        ASSERT_ONCE(sr_source_color.get() && depth.get());

                        bool reset_dlss = Nexus::m_noPreviousFrame || dlss_output_changed;
                        device_data.force_reset_sr = reset_dlss;
                        
                        if (Nexus::m_noPreviousFrame)
                        {
                           std::stringstream s;
                           s << "TAA: No previous frame (CPU)";
                           reshade::log::message(reshade::log::level::info, s.str().c_str());
                           Nexus::m_noPreviousFrame = false;
                        }
                        
                        float dlss_pre_exposure = 0.f;

                        SR::SuperResolutionImpl::DrawData draw_data;
                        draw_data.source_color = sr_source_color.get();
                        draw_data.output_color = device_data.sr_output_color.get();
                        draw_data.motion_vectors = game_device_data.motion_vector.get();
                        draw_data.depth_buffer = depth.get();
                        draw_data.pre_exposure = dlss_pre_exposure;
                        draw_data.jitter_x = -game_device_data.taa_jitters.x;
                        draw_data.jitter_y = -game_device_data.taa_jitters.y;
                        draw_data.reset = reset_dlss;
                        draw_data.near_plane = game_device_data.CameraDistances.x; // 10cm
                        draw_data.far_plane = game_device_data.CameraDistances.y; // 40km
                        draw_data.vert_fov = std::atan(1.0f / game_device_data.ProjectionMatrix.r[1].m128_f32[1]) * 2.0;//0.60894538; // Would be "atan(1.f / projection_matrix.m11) * 2.0", however we don't have the proj matrix in any cbuffer in this game, it's only in the CPU. No current SR implementation uses this anyway. Seems like the default is 34.89 degs.
                        draw_data.frame_index = cb_luma_global_settings.FrameIndex;
                        draw_data.time_delta = Nexus::m_lastGameDeltaTime;
                        if (!settings_data.auto_exposure)
                           draw_data.exposure = device_data.sr_exposure.get();

                        bool dlss_succeeded = sr_implementations[device_data.sr_type]->Draw(sr_instance_data, native_device_context, draw_data);
                        if (dlss_succeeded)
                        {
                           device_data.has_drawn_sr = true;
                        }
                        
                        if (game_device_data.game_depth_dsv.get() != nullptr)
                        {
                           //com_ptr<ID3D11DepthStencilState> ds_state;
                           //ds_state.
                           if (!game_device_data.depth_stencil_state.get())
                           {
                              std::stringstream s;
                              s << "DS: Creating DS State";
                              reshade::log::message(reshade::log::level::info, s.str().c_str());
                              
                              CD3D11_DEPTH_STENCIL_DESC ds_desc(D3D11_DEFAULT);
                              ds_desc.DepthEnable = TRUE;
                              ds_desc.DepthFunc = D3D11_COMPARISON_ALWAYS;

                              HRESULT hr = native_device->CreateDepthStencilState(&ds_desc, &game_device_data.depth_stencil_state);
                              if (FAILED(hr))
                              {
                                 std::stringstream s;
                                 s << "DS: DS State Creation Failed";
                                 reshade::log::message(reshade::log::level::info, s.str().c_str());
                                 //return false;
                              }
                           }
                           
                           //DrawCustomPixelShader(native_device_context, nullptr, nullptr, game_device_data.depth_sampler.get(), device_data.native_vertex_shaders[CompileTimeStringHash("Copy VS")].get(), device_data.native_pixel_shaders[CompileTimeStringHash("Unjitter Depth")].get(), game_device_data.unjitter_depth_srv.get(), nullptr, game_device_data.render_res.x, game_device_data.render_res.y, false);
                           
                           // Set the new resources/states:
                           native_device_context->IAGetVertexBuffers(0,1,0,0,0);
                           
                           constexpr FLOAT blend_factor_alpha[4] = { 1.f, 1.f, 1.f, 1.f };
                           constexpr FLOAT blend_factor[4] = { 1.f, 1.f, 1.f, 0.f }; // TODO: this makes no sense as the blend state is unlikely to use it, use write mask instead
                           native_device_context->OMSetBlendState(nullptr, false ? blend_factor_alpha : blend_factor, 0xFFFFFFFF);
                           native_device_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
                           native_device_context->RSSetScissorRects(0, nullptr);
                           D3D11_VIEWPORT viewport;
                           viewport.TopLeftX = 0;
                           viewport.TopLeftY = 0;
                           viewport.Width = game_device_data.render_res.x;
                           viewport.Height = game_device_data.render_res.y;
                           viewport.MinDepth = 0;
                           viewport.MaxDepth = 1;
                           native_device_context->RSSetViewports(1, &viewport); // Viewport is always needed
                           ID3D11ShaderResourceView* srvs[] = {game_device_data.unjitter_depth_srv.get()};
                           native_device_context->PSSetShaderResources(0, 1, srvs);
                           native_device_context->OMSetDepthStencilState(game_device_data.depth_stencil_state.get(), 0);
                           native_device_context->OMSetRenderTargets(0, nullptr, game_device_data.game_depth_dsv.get());

                           auto vs = device_data.native_vertex_shaders[CompileTimeStringHash("Copy VS")].get();
                           auto ps = device_data.native_pixel_shaders[CompileTimeStringHash("Unjitter Depth")].get();
                           native_device_context->VSSetShader(vs, nullptr, 0);
                           native_device_context->PSSetShader(ps, nullptr, 0);
                           native_device_context->IASetInputLayout(nullptr);
                           native_device_context->RSSetState(nullptr);
                           
                           // Finally draw:
                           native_device_context->Draw(4, 0);
                        }

                        draw_state_stack.Restore(native_device_context, device_data.uav_max_count);
                        compute_state_stack.Restore(native_device_context, device_data.uav_max_count);
                        
                        if (device_data.has_drawn_sr)
                        {
      #if 0
                           const std::shared_lock lock_trace(s_mutex_trace);
                           if (trace_running)
                           {
                              const std::unique_lock lock_trace_2(cmd_list_data.mutex_trace);
                              TraceDrawCallData trace_draw_call_data;
                              trace_draw_call_data.type = TraceDrawCallData::TraceDrawCallType::Custom;
                              trace_draw_call_data.command_list = native_device_context;
                              trace_draw_call_data.custom_name = "Super Resolution";
                              // Re-use the RTV data for simplicity
                              GetResourceInfo(device_data.sr_output_color.get(), trace_draw_call_data.rt_size[0], trace_draw_call_data.rt_format[0], &trace_draw_call_data.rt_type_name[0], &trace_draw_call_data.rt_hash[0]);
                              cmd_list_data.trace_draw_calls_data.insert(cmd_list_data.trace_draw_calls_data.end() - 1, trace_draw_call_data);
                           }
      #endif
                           if (!dlss_output_supports_uav)
                           {
                              //native_device_context->CopyResource(output_color.get(), device_data.sr_output_color.get()); // DX11 doesn't need barriers
                              if (will_draw_rain /*&& test_index == 17*/)
                              {
                                 SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, reshade::api::shader_stage::compute, LumaConstantBufferType::LumaData);
                                 ID3D11ShaderResourceView* srvs[] = {game_device_data.game_source_color_srv.get()};
                                 ID3D11UnorderedAccessView* uavs[] = {game_device_data.game_source_color_before_rain_uav.get(), game_device_data.sr_output_uav.get()};
                                 ID3D11Buffer* buffers[] = {game_device_data.game_viewport_cbv.get()};
                                 native_device_context->CSSetShader(device_data.native_compute_shaders[CompileTimeStringHash("Composite Rain")].get(), nullptr, 0);
                                 native_device_context->CSSetShaderResources(0, 1, srvs);
                                 native_device_context->CSSetConstantBuffers(0, 1, buffers);
                                 native_device_context->CSSetUnorderedAccessViews(0, 2, uavs, nullptr);
               
                                 native_device_context->Dispatch((game_device_data.render_res.x + 7) / 8, (game_device_data.render_res.y + 7) / 8, 1);
                                 will_draw_rain = false;
                                 
                                 native_device_context->CopyResource(output_color.get(), game_device_data.game_source_color_before_rain.get()); // DX11 doesn't need barriers
                              }
                              else
                              {
                                 native_device_context->CopyResource(output_color.get(), device_data.sr_output_color.get()); // DX11 doesn't need barriers
                              }
                           }
                           else
                           {
                              device_data.sr_output_color = nullptr;
                           }
                        }
                        else
                        {
                           //ASSERT_ONCE(false);
                           //cb_luma_global_settings.SRType = 0;
                           //device_data.cb_luma_global_settings_dirty = true;
                           //device_data.sr_suppressed = true;
                           device_data.force_reset_sr = true;
                        }
                     }
                  }
               }
            }
         }
      }
      return DrawOrDispatchOverrideType::None; // Don't cancel the original draw call
   }

   void UpdateLumaInstanceDataCB(CB::LumaInstanceDataPadded& data, CommandListData& cmd_list_data, DeviceData& device_data) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);

      float2 jitters = frame_jitters;
      jitters.x /= game_device_data.render_res.x;
      jitters.y /= game_device_data.render_res.y;
      memcpy(&data.GameData.CurrJitters, &jitters, sizeof(jitters));
      jitters = prev_frame_jitters;
      jitters.x /= game_device_data.render_res.x;
      jitters.y /= game_device_data.render_res.y;
      memcpy(&data.GameData.PrevJitters, &jitters, sizeof(jitters));
      
      memcpy(&data.GameData.CameraSpaceToPreviousProjectedSpace, &game_device_data.CameraSpaceToPreviousProjectedSpace, sizeof(game_device_data.CameraSpaceToPreviousProjectedSpace));
      memcpy(&data.GameData.PreviousProjectionMatrix, &game_device_data.PreviousProjectionMatrixCopy, sizeof(game_device_data.PreviousProjectionMatrixCopy));
      memcpy(&data.GameData.PreviousViewMatrix, &game_device_data.PreviousViewMatrixCopy, sizeof(game_device_data.PreviousViewMatrixCopy));
   }

   void OnPresent(ID3D11Device* native_device, DeviceData& device_data) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);

      if (game_device_data.render_res.y != 0.0)
      {
         // Update TAA jitters:
         int phases = 16;           // Decent default for any modern TAA
         const int base_phases = 8; // For DLAA
         // We round to the cloest int, though maybe we should floor? Unclear. Both are probably fine.
         phases = (int)std::lrint(float(base_phases) * powf(float(max(game_device_data.render_res.y, 1)) / float(max(game_device_data.render_res.y, 1)), 2.f));
         int temporal_frame = cb_luma_global_settings.FrameIndex % phases;

         // Note: we add 1 to the temporal frame here to avoid a bias, given that Halton always returns 0 for 0
         game_device_data.taa_jitters.x = SR::HaltonSequence(temporal_frame, 2);
         game_device_data.taa_jitters.y = SR::HaltonSequence(temporal_frame, 3);
      }
#if ENABLE_SR
      if (!custom_texture_mip_lod_bias_offset)
      {
         std::shared_lock shared_lock_samplers(s_mutex_samplers);
         if (device_data.sr_type != SR::Type::None && !device_data.sr_suppressed)
         {
            device_data.texture_mip_lod_bias_offset = SR::GetMipLODBias(game_device_data.render_res.y, game_device_data.render_res.y); // This results in -1 at output res
         }
         else
         {
            device_data.texture_mip_lod_bias_offset = 0.f;
         }
      }
#endif
      
      if (!has_taa_drawn)
      {
         reshade::log::message(reshade::log::level::info, "Present: TAA has not drawn");
#if ENABLE_SR
         device_data.sr_suppressed = false;
         device_data.taa_detected = false;
#endif
         game_device_data.CleanMVResources();
      }
      
      if (!will_draw_rain)
      {
         game_device_data.CleanRainResources();
      }
      
      device_data.has_drawn_sr = false;
      game_device_data.set_render_res = false;
      game_device_data.is_last_frame_jittered = game_device_data.set_jitter;
      game_device_data.set_jitter = false;
      game_device_data.resolution_changed = false;
      
      has_forward_pass_finished = false;
      will_draw_rain = false;
      has_copied_before_rain = false;
      has_taa_drawn = false;
      has_sr_draw_tried = false;
      has_changed_all_viewport = false;
      i = 0;
      map_changed = false;

      if (game_device_data.is_last_frame_jittered)
      {
         prev_frame_jitters = frame_jitters;
      }
      else
      {
         prev_frame_jitters = {0.0, 0.0};
      }
      frame_jitters = game_device_data.taa_jitters;
      
      game_device_data.camera_position_previous = game_device_data.camera_position_current;
      game_device_data.game_depth_dsv = nullptr;
   }

   // ### MINHOOK ADDITION ###
   // 4. Cleanup MinHook on unload (critical to avoid crashes)
   void OnUnload()
   {
      // Disable and remove the hook if it was created
      if (oSub_6FFFF6C05600 != nullptr)
      {
         MH_DisableHook(reinterpret_cast<void*>(pattern_1_addresses[0]));
         MH_RemoveHook(reinterpret_cast<void*>(pattern_1_addresses[0]));
         reshade::log::message(reshade::log::level::info, "Hook disabled and removed");
      }

      // Uninitialize MinHook
      MH_Uninitialize();
      reshade::log::message(reshade::log::level::info, "MinHook uninitialized");
   }
};

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
   if (ul_reason_for_call == DLL_PROCESS_ATTACH)
   {
      Globals::SetGlobals(PROJECT_NAME, "WATCH_DOGS Luma mod");
      Globals::DEVELOPMENT_STATE = Globals::ModDevelopmentState::WorkInProgress;
      Globals::VERSION = 1;

      luma_settings_cbuffer_index = 13;
      luma_data_cbuffer_index = 12;

      force_disable_display_composition = true;
      swapchain_format_upgrade_type = TextureFormatUpgradesType::None;
      swapchain_upgrade_type = SwapchainUpgradeType::None;
      texture_format_upgrades_type = TextureFormatUpgradesType::None;
      enable_indirect_texture_format_upgrades = false;
      texture_upgrade_formats = {};

      shader_hashes_DeferredFXAntialias.pixel_shaders = {
         0x05200DB2,	//engine\shaders\obj\h64\pixel_85cb1ee4.pso
      };
      
      shader_hashes_DeferredFXAntialias_RESOLVE.pixel_shaders = {
         0x525EF528,	//engine\shaders\obj\h02\pixel_f236ae02.pso
      };
      
      shader_hashes_DeferredFXAntialias_NO_PREVIOUS_FRAME.pixel_shaders = {
         0xAEC53111,	//engine\shaders\obj\h3e\pixel_8770b13e.pso
         0xC8EAA873,	//engine\shaders\obj\h1a\pixel_689e4c1a.pso
         0x4222D6F9,	//engine\shaders\obj\h2d\pixel_4cc59a2d.pso
         0x44286B01,	//engine\shaders\obj\h67\pixel_6b8d4067.pso
         0x52CA82EB,	//engine\shaders\obj\h21\pixel_04197c21.pso
      };
      
      shader_hashes_WaterHeightMap.pixel_shaders = {
         0x52EC1506,
      };
      
      shader_hashes_RainStreak.pixel_shaders = {
         // These are rain streak shaders, they don't look nice with SR
         // They are jittered but hardly noticeable
         0x2FEAB314,
         0x6CA4EAA6,
         0xAD998913
      };
      
      shader_hashes_SMAA.pixel_shaders = {
         0xA56DEA84,
         0x51B21BB4
      };
      
      shader_hashes_Downsample.vertex_shaders = {
         0x8BDA327D
      };
      
      shader_hashes_LightProbesUpdate.compute_shaders = {
         0xD7FF651A,	//engine\shaders\obj\h3b\compute_3b56a23b.cso
         0xC73A83B1,	//engine\shaders\obj\h3d\compute_db9d7abd.cso
         0x699EC152,	//engine\shaders\obj\h0c\compute_e38add0c.cso
         0x48416856,	//engine\shaders\obj\h31\compute_38a6bd31.cso
         0x31183D7E,	//engine\shaders\obj\h29\compute_53bc6c29.cso
         0x43B14E6A,	//engine\shaders\obj\h68\compute_aac5d3e8.cso
      };
      
      shader_hashes_DriverGenericRoad.pixel_shaders = {
         0x9967F220,
      };
      
      enable_samplers_upgrade = true;
      //force_upgrade_linear_samplers = true;

      game = new WatchDogs();
   }
   else if (ul_reason_for_call == DLL_PROCESS_DETACH)
   {
      reshade::unregister_event<reshade::addon_event::map_buffer_region>(WatchDogs::OnMapBufferRegion);
      reshade::unregister_event<reshade::addon_event::unmap_buffer_region>(WatchDogs::OnUnmapBufferRegion);
      reshade::unregister_event<reshade::addon_event::clear_render_target_view>(WatchDogs::OnClearRenderTargetView);
      reshade::unregister_event<reshade::addon_event::clear_depth_stencil_view>(WatchDogs::OnClearDepthStencilView);
      
      //reshade::unregister_event<reshade::addon_event::create_resource>(WatchDogs::OnCreateResource);
      //reshade::unregister_event<reshade::addon_event::create_resource_view >(WatchDogs::OnCreateResourceView);

      // ### MINHOOK ADDITION ###
      // Call cleanup when the DLL unloads
      if (game != nullptr)
      {
         static_cast<WatchDogs*>(game)->OnUnload();
      }
   }

   CoreMain(hModule, ul_reason_for_call, lpReserved);

   return TRUE;
}