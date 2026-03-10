#define GAME_WATCH_DOGS 1
#define CHECK_GRAPHICS_API_COMPATIBILITY 0
#define ALLOW_SHADERS_DUMPING 0

#define ENABLE_NGX 1
//#define AUTO_ENABLE_SR 1

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

   static std::vector<std::byte*> pattern_1_addresses;
   static std::vector<std::byte*> pattern_2_addresses; // grid shading

   float2 frame_jitters = float2(0.f, 0.f);
   float2 prev_frame_jitters = frame_jitters;
   
   int32_t i = 0;

   bool has_taa_drawn = false;
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

#if DEVELOPMENT
         std::stringstream s;
         s << "PrevCamDirDotCamDir = " << dot;
         reshade::log::message(reshade::log::level::info, s.str().c_str());

         float* m_lastGameDeltaTime = (float*)(a2[14] + 4928LL);
         Nexus::m_lastGameDeltaTime = *m_lastGameDeltaTime;
         s.clear();
         s.str("");
         s << "m_lastGameDeltaTime = " << *m_lastGameDeltaTime;
         reshade::log::message(reshade::log::level::info, s.str().c_str());
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
   com_ptr<ID3D11ShaderResourceView> motion_vector_srv;
   com_ptr<ID3D11RenderTargetView> motion_vector_rtv;

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

   float2 taa_jitters = {};
   bool found_per_view_globals = false;
   
   bool resolution_changed = false;

   bool set_render_res = false;
   float4 render_res = {0, 0, 0, 0};

   bool set_jitter = false;
   bool is_last_frame_jittered = false;
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
   }

   void OnLoad(std::filesystem::path& file_path, bool failed) override
   {
      if (!failed)
      {
         reshade::register_event<reshade::addon_event::map_buffer_region>(WatchDogs::OnMapBufferRegion);
         reshade::register_event<reshade::addon_event::unmap_buffer_region>(WatchDogs::OnUnmapBufferRegion);
         reshade::register_event<reshade::addon_event::clear_render_target_view>(WatchDogs::OnClearRenderTargetView);

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
      }
   }

   void OnCreateDevice(ID3D11Device* native_device, DeviceData& device_data) override
   {
      device_data.game = new GameDeviceDataWatchDogs;
   }

   void OnInitSwapchain(reshade::api::swapchain* swapchain) override
   {
      auto& device_data = *swapchain->get_device()->get_private_data<DeviceData>();

      cb_luma_global_settings.GameSettings.InvOutputRes.x = 1.f / device_data.output_resolution.x;
      cb_luma_global_settings.GameSettings.InvOutputRes.y = 1.f / device_data.output_resolution.y;
      device_data.cb_luma_global_settings_dirty = true;
   }

   void PrintImGuiAbout() override
   {
      ImGui::Text("WATCH_DOGS Luma mod - about and credits section", "");
   }
   
   static bool CreateMVResources(ID3D11Device* native_device, GameDeviceDataWatchDogs& game_device_data, float width, float height)
   {
      // Return early if resources already exist with correct dimensions
      if (game_device_data.motion_vector.get() && !game_device_data.resolution_changed)
      {
         std::stringstream s;
         s << "MV: Resource exists and resolution unchanged";
         reshade::log::message(reshade::log::level::info, s.str().c_str());
         return true;
      }
      
      HRESULT hr;
      
      D3D11_TEXTURE2D_DESC depth_desc = {};
      depth_desc.Width = static_cast<UINT>(width);
      depth_desc.Height = static_cast<UINT>(height);
      depth_desc.MipLevels = 0;
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
      
      D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
      srv_desc.Format = DXGI_FORMAT_R16G16_FLOAT;
      srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
      srv_desc.Texture2D.MostDetailedMip = 0;
      srv_desc.Texture2D.MipLevels = 1;
      hr = native_device->CreateShaderResourceView(game_device_data.motion_vector.get(), &srv_desc, &game_device_data.motion_vector_srv);
      if (FAILED(hr))
      {
         std::stringstream s;
         s << "MV: SRV Creation Failed";
         reshade::log::message(reshade::log::level::info, s.str().c_str());
         return false;
      }
      
      D3D11_RENDER_TARGET_VIEW_DESC rtv_desc = {};
      rtv_desc.Format = DXGI_FORMAT_R16G16_FLOAT;
      rtv_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
      hr = native_device->CreateRenderTargetView(game_device_data.motion_vector.get(), &rtv_desc, &game_device_data.motion_vector_rtv);
      if (FAILED(hr))
      {
         std::stringstream s;
         s << "MV: RTV Creation Failed";
         reshade::log::message(reshade::log::level::info, s.str().c_str());
         return false;
      }
      
      if (FAILED(hr))
      {
         std::stringstream s;
         s << "MV: Resource Creation succeed";
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

         if (buffer_desc.ByteWidth == CBPerViewGlobal_buffer_size && game_device_data.set_render_res)
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

   static void OnUnmapBufferRegion(reshade::api::device* device, reshade::api::resource resource)
   {
      DeviceData& device_data = *device->get_private_data<DeviceData>();
      auto& game_device_data = GetGameDeviceData(device_data);
      ID3D11Device* native_device = (ID3D11Device*)(device->get_native());

      ID3D11Buffer* buffer = reinterpret_cast<ID3D11Buffer*>(resource.handle);

      bool is_global_cbuffer = device_data.cb_per_view_global_buffer != nullptr && device_data.cb_per_view_global_buffer == buffer;

      ASSERT_ONCE(!device_data.cb_per_view_global_buffer_map_data || is_global_cbuffer);

      if (is_global_cbuffer && device_data.cb_per_view_global_buffer_map_data != nullptr)
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
#endif
         }
         if (is_valid_cbuffer && !has_changed_all_viewport && !Nexus::m_gridShadingEnable && Nexus::m_willRenderThisFrame)
         {
#if DEVELOPMENT
            std::stringstream s;
            s.clear();
            s.str("");
            s << "Found cbuffer viewport to change: " << i;
            reshade::log::message(reshade::log::level::info, s.str().c_str());
            i++;
#endif
            if (!game_device_data.set_jitter)
            {
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
               view.r[3] = XMVectorSet(0.f, 0.f, 0.f, 1.f); // Clear translation row

               XMMATRIX baseProjection =
                  XMLoadFloat4x4(reinterpret_cast<XMFLOAT4X4*>(&float_data[8]));

               XMMATRIX originalInvProjectionDepth =
                  XMLoadFloat4x4(reinterpret_cast<XMFLOAT4X4*>(&float_data[16]));

               // ------------------------------------------------------------
               // Use previous VP if last frame is jittered
               // ------------------------------------------------------------

               if (game_device_data.is_last_frame_jittered)
               {
                  XMMATRIX previousVP = game_device_data.ViewProjectionMatrix;
                  game_device_data.PreviousViewProjectionMatrix = previousVP;

                  XMStoreFloat4x4(
                     reinterpret_cast<XMFLOAT4X4*>(&float_data[30]),
                     previousVP);
               }

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

               XMMATRIX viewRotProjection =
                  XMMatrixMultiply(jitteredProjection, viewRot);

               XMMATRIX invProjection =
                  XMMatrixInverse(nullptr, jitteredProjection);

               XMMATRIX invProjectionDepth = invProjection;
               invProjectionDepth.r[3] = originalInvProjectionDepth.r[3];

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

               if (game_device_data.is_last_frame_jittered)
               {
                  XMStoreFloat4x4(
                     reinterpret_cast<XMFLOAT4X4*>(&float_data[30]),
                     game_device_data.PreviousViewProjectionMatrix);
               }
            }
         }
      }
      device_data.cb_per_view_global_buffer_map_data = nullptr;
      device_data.cb_per_view_global_buffer = nullptr;
   }

   static bool OnClearRenderTargetView(reshade::api::command_list* cmd_list, reshade::api::resource_view rtv, const float color[4], uint32_t rect_count, const reshade::api::rect* rects)
   {
      auto* device = cmd_list->get_device();
      DeviceData& device_data = *device->get_private_data<DeviceData>();
      auto& game_device_data = GetGameDeviceData(device_data);
      if (!game_device_data.set_render_res)
      {
         if (color[0] == 0.0 && color[1] == -1.0 && color[2] == 0.0 && color[3] == 1.0)
         {
            auto current_rtv = device->get_resource_desc(device->get_resource_from_view(rtv));

            if (current_rtv.texture.format == reshade::api::format::r16g16_float)
            {
               float width = static_cast<float>(current_rtv.texture.width);
               float height = static_cast<float>(current_rtv.texture.height);
               if ((width / height) == (device_data.output_resolution.x / device_data.output_resolution.y))
               {
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
                  }
                  else
                  {
                     game_device_data.set_render_res = true;
                     game_device_data.resolution_changed = false;
                  }
               }
            }
         }
      }
      return false;
   }

   DrawOrDispatchOverrideType OnDrawOrDispatch(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, reshade::api::shader_stage stages, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, bool is_custom_pass, bool& updated_cbuffers, std::function<void()>* original_draw_dispatch_func) override
   {
      if (original_shader_hashes.Contains(shader_hashes_DeferredFXAntialias))
      {
         has_taa_drawn = true;
         // TODO: add exposure texture support (it's possibly calculated just earlier in the auto exposure steps, but they could be after DLSS too, depends on UE), either way auto exposure is ok
         DrawStateStack<DrawStateStackType::FullGraphics> draw_state_stack;
         DrawStateStack<DrawStateStackType::Compute> compute_state_stack;
         // We don't actually replace the shaders with the classic luma shader swapping feature, so we need to set the CBs manually
         draw_state_stack.Cache(native_device_context, device_data.uav_max_count);
         compute_state_stack.Cache(native_device_context, device_data.uav_max_count);
         
         auto& game_device_data = GetGameDeviceData(device_data);
         
         if (CreateMVResources(native_device, game_device_data, game_device_data.render_res.x, game_device_data.render_res.y))
         {
            com_ptr<ID3D11RenderTargetView> render_target_views[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
            com_ptr<ID3D11DepthStencilView> depth_stencil_view;
            native_device_context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, &render_target_views[0], &depth_stencil_view);
            render_target_views[1] = game_device_data.motion_vector_rtv;
            
            ID3D11RenderTargetView* rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
            for (UINT i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
            {
               rtvs[i] = render_target_views[i].get();
            }
            native_device_context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtvs, depth_stencil_view.get());
            
            if (!updated_cbuffers)
            {
               SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, reshade::api::shader_stage::pixel, LumaConstantBufferType::LumaSettings);
               SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, reshade::api::shader_stage::pixel, LumaConstantBufferType::LumaData);
               updated_cbuffers = true;
            }
            
            native_device_context->Draw(4, 0);

#if ENABLE_SR
            if(device_data.sr_type != SR::Type::None && !device_data.sr_suppressed)
            {
               // 0 Device Depth
               // 1 Source Color
               // 2 Encoded MV
               // 3 Previous Color
               com_ptr<ID3D11ShaderResourceView> ps_shader_resources[4];
               native_device_context->PSGetShaderResources(0, ARRAYSIZE(ps_shader_resources), &ps_shader_resources[0]);
               /*
               com_ptr<ID3D11RenderTargetView> render_target_views[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]; // There should only be 1
               com_ptr<ID3D11DepthStencilView> depth_stencil_view;
               native_device_context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, &render_target_views[0], &depth_stencil_view);
               */
               const bool dlss_inputs_valid = ps_shader_resources[0].get() && ps_shader_resources[1].get() && ps_shader_resources[3].get() && render_target_views[0].get() && game_device_data.motion_vector_srv.get();
               ASSERT_ONCE(dlss_inputs_valid);

               if (dlss_inputs_valid)
               {
               
                  std::stringstream s;
                  s << "DLSS: try drawing";
                  reshade::log::message(reshade::log::level::info, s.str().c_str());
               
                  auto* sr_instance_data = device_data.GetSRInstanceData();
                  ASSERT_ONCE(sr_instance_data);
               
                  s.clear();
                  s.str("");
                  s << "DLSS: got instance data";
                  reshade::log::message(reshade::log::level::info, s.str().c_str());

                  com_ptr<ID3D11Resource> output_color_resource;
                  render_target_views[0]->GetResource(&output_color_resource);
               
                  s.clear();
                  s.str("");
                  s << "DLSS: got output_color_resource";
                  reshade::log::message(reshade::log::level::info, s.str().c_str());
               
                  com_ptr<ID3D11Texture2D> output_color;
                  HRESULT hr = output_color_resource->QueryInterface(&output_color);
               
                  s.clear();
                  s.str("");
                  s << "DLSS: got output_color";
                  reshade::log::message(reshade::log::level::info, s.str().c_str());
               
                  ASSERT_ONCE(SUCCEEDED(hr));

                  D3D11_TEXTURE2D_DESC taa_output_texture_desc;
                  output_color->GetDesc(&taa_output_texture_desc);
               
                  s.clear();
                  s.str("");
                  s << "DLSS: got taa_output_texture_desc";
                  reshade::log::message(reshade::log::level::info, s.str().c_str());

                  SR::SettingsData settings_data;
               
                  s.clear();
                  s.str("");
                  s << "DLSS: allocating settings_data";
                  reshade::log::message(reshade::log::level::info, s.str().c_str());
               
                  settings_data.output_width = unsigned int(game_device_data.render_res.x + 0.5);
                  settings_data.output_height = unsigned int(game_device_data.render_res.y + 0.5);
                  settings_data.render_width = unsigned int(game_device_data.render_res.x + 0.5);
                  settings_data.render_height = unsigned int(game_device_data.render_res.y + 0.5);
                  settings_data.hdr = true; // At this point we are linear and "HDR" though the image is partially tonemapped if we are after SMAA
                  settings_data.inverted_depth = true;
                  settings_data.mvs_jittered = false; // See shader 0xA1037803, they were partially jittered (with the current frame jitter but not the previous one, so we fixed that up and completely removed jitters, so it also makes motion blur independent from them)
                  settings_data.auto_exposure = device_data.sr_type != SR::Type::FSR; // Exp is ~1 given it's all after post processing (and the game's auto exposure). FSR breaks with auto exposure in this game (it heavily clips highlights). DLSS looks fine with it.
                  // MVs in UV space, so we need to scale by the render resolution to transform to pixel space
                  settings_data.mvs_x_scale = -game_device_data.render_res.x;
                  settings_data.mvs_y_scale = -game_device_data.render_res.y;
                  settings_data.render_preset = dlss_render_preset;
                  sr_implementations[device_data.sr_type]->UpdateSettings(sr_instance_data, native_device_context, settings_data);
               
                  s.clear();
                  s.str("");
                  s << "DLSS: update settings";
                  reshade::log::message(reshade::log::level::info, s.str().c_str());

                  bool skip_dlss = taa_output_texture_desc.Width < sr_instance_data->min_resolution || taa_output_texture_desc.Height < sr_instance_data->min_resolution;
                  bool dlss_output_changed = false;

                  constexpr bool dlss_use_native_uav = true;
                  bool dlss_output_supports_uav = dlss_use_native_uav && (taa_output_texture_desc.BindFlags & D3D11_BIND_UNORDERED_ACCESS) != 0;
                  // Create a copy that supports Unordered Access if it wasn't already supported
                  if (!dlss_output_supports_uav)
                  {
                     D3D11_TEXTURE2D_DESC dlss_output_texture_desc = taa_output_texture_desc;
                     dlss_output_texture_desc.Width = std::lrintf(device_data.output_resolution.x);
                     dlss_output_texture_desc.Height = std::lrintf(device_data.output_resolution.y);
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
                     ps_shader_resources[1]->GetResource(&sr_source_color);
                     com_ptr<ID3D11Resource> motion_vectors;
                     game_device_data.motion_vector_srv->GetResource(&motion_vectors);
                     com_ptr<ID3D11Resource> depth;
                     ps_shader_resources[0]->GetResource(&depth);

                     ASSERT_ONCE(motion_vectors.get() && sr_source_color.get() && depth.get());

                     bool reset_dlss = Nexus::m_noPreviousFrame || dlss_output_changed;
                     device_data.force_reset_sr = false;

                     float dlss_pre_exposure = 0.f;

                     SR::SuperResolutionImpl::DrawData draw_data;
                     draw_data.source_color = sr_source_color.get();
                     draw_data.output_color = device_data.sr_output_color.get();
                     draw_data.motion_vectors = motion_vectors.get();
                     draw_data.depth_buffer = depth.get();
                     draw_data.pre_exposure = dlss_pre_exposure;
                     draw_data.jitter_x = frame_jitters.x; // Not 100% sure these shouldn't be scaled by 0.5, but probably not! (I tried, couldn't tell the difference, but logic points towards not doing it)
                     draw_data.jitter_y = frame_jitters.y;
                     draw_data.reset = reset_dlss; // TODO: implement camera cuts too... I don't think the game has them exposed though. Possibly reset DLSS when we pause the game or go into a loading screen.

#if 1 // Extracted from proj matrix on the CPU (not 100% if this or the ones below are right)
                     draw_data.near_plane = 0.25; // 10cm
                     draw_data.far_plane = 6500.0; // 40km
#else // Extracted from "A1037803" PS cbuffers. Supposedly they are fixed throughout the game. "7BE70E91" might also have them.
                     draw_data.near_plane = 0.025; // 2.5cm
                     draw_data.far_plane = 10000.0; // 10km
#endif
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

                     draw_state_stack.Restore(native_device_context, device_data.uav_max_count);
                     compute_state_stack.Restore(native_device_context, device_data.uav_max_count);

                     if (device_data.has_drawn_sr)
                     {
#if DEVELOPMENT
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
                           native_device_context->CopyResource(output_color.get(), device_data.sr_output_color.get()); // DX11 doesn't need barriers
                        }
                        else
                        {
                           device_data.sr_output_color = nullptr;
                        }

                        return DrawOrDispatchOverrideType::Skip;
                     }
                     else
                     {
                        // ASSERT_ONCE(false);
                        // cb_luma_global_settings.SRType = 0;
                        // device_data.cb_luma_global_settings_dirty = true;
                        // device_data.sr_suppressed = true;
                        device_data.force_reset_sr = true;
                     }
                  }
                  if (dlss_output_supports_uav)
                  {
                     device_data.sr_output_color = nullptr;
                  }
               }
            }
            
            if (Nexus::m_noPreviousFrame)
            {
               std::stringstream s;
               s << "TAA: No previous frame (CPU)";
               reshade::log::message(reshade::log::level::info, s.str().c_str());
               Nexus::m_noPreviousFrame = false;
            }
            
            return DrawOrDispatchOverrideType::Skip;
#endif // ENABLE_SR
         
            draw_state_stack.Restore(native_device_context);
            compute_state_stack.Restore(native_device_context);
            
            return DrawOrDispatchOverrideType::Skip; // Don't cancel the original draw call
         }
      }
#if DEBUG_MV
      else if (original_shader_hashes.Contains(shader_hashes_DeferredFXAntialias_RESOLVE))
      {
         auto& game_device_data = GetGameDeviceData(device_data);
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
            
            native_device_context->PSSetShaderResources(0, 1, {&game_device_data.motion_vector_srv});
            native_device_context->Draw(4, 0);
            
            draw_state_stack.Restore(native_device_context);
            compute_state_stack.Restore(native_device_context);
            
            return DrawOrDispatchOverrideType::Skip; // Don't cancel the original draw call
         }
      }
#endif
      else if (original_shader_hashes.Contains(shader_hashes_DeferredFXAntialias_NO_PREVIOUS_FRAME))
      {
         std::stringstream s;
         s << "TAA: No previous frame (Shader)";
         reshade::log::message(reshade::log::level::info, s.str().c_str());
      }
      else if (original_shader_hashes.Contains((shader_hashes_WaterHeightMap)))
      {
         has_changed_all_viewport = true;
         std::stringstream s;
         s << "WaterHeightMap: Draw";
         reshade::log::message(reshade::log::level::info, s.str().c_str());
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
         phases = (int)std::lrint(float(base_phases) * powf(float(game_device_data.render_res.y) / float(game_device_data.render_res.y), 2.f));
         int temporal_frame = cb_luma_global_settings.FrameIndex % phases;

         // Note: we add 1 to the temporal frame here to avoid a bias, given that Halton always returns 0 for 0
         game_device_data.taa_jitters.x = SR::HaltonSequence(temporal_frame, 2);
         game_device_data.taa_jitters.y = SR::HaltonSequence(temporal_frame, 3);
      }
      
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
      
      if (!has_taa_drawn)
      {
         device_data.sr_suppressed = false;
         device_data.taa_detected = false;
      }
      
      device_data.has_drawn_sr = false;
      game_device_data.set_render_res = false;
      game_device_data.is_last_frame_jittered = game_device_data.set_jitter;
      game_device_data.set_jitter = false;
      game_device_data.resolution_changed = false;
      has_taa_drawn = false;
      has_changed_all_viewport = false;
      i = 0;

      if (game_device_data.is_last_frame_jittered)
      {
         prev_frame_jitters = frame_jitters;
      }
      else
      {
         prev_frame_jitters = {0.0, 0.0};
      }
      frame_jitters = game_device_data.taa_jitters;
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
      swapchain_format_upgrade_type = TextureFormatUpgradesType::AllowedEnabled;
      swapchain_upgrade_type = SwapchainUpgradeType::scRGB;
      texture_format_upgrades_type = TextureFormatUpgradesType::None;
      enable_indirect_texture_format_upgrades = false;
      texture_upgrade_formats = {};

      shader_hashes_DeferredFXAntialias.pixel_shaders = {
         0x0A2DA44D,
      };
      
      shader_hashes_DeferredFXAntialias_RESOLVE.pixel_shaders = {
         0x32E76101,
      };
      
      shader_hashes_DeferredFXAntialias_NO_PREVIOUS_FRAME.pixel_shaders = {
         0x3CD1C00D,
         0x4F6ABBC7,
      };
      
      shader_hashes_WaterHeightMap.pixel_shaders = {
         0x52EC1506,
      };

      game = new WatchDogs();
   }
   else if (ul_reason_for_call == DLL_PROCESS_DETACH)
   {
      reshade::unregister_event<reshade::addon_event::map_buffer_region>(WatchDogs::OnMapBufferRegion);
      reshade::unregister_event<reshade::addon_event::unmap_buffer_region>(WatchDogs::OnUnmapBufferRegion);
      reshade::unregister_event<reshade::addon_event::clear_render_target_view>(WatchDogs::OnClearRenderTargetView);

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