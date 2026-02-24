#define GAME_WATCH_DOGS 1
#define CHECK_GRAPHICS_API_COMPATIBILITY 1
#define ALLOW_SHADERS_DUMPING 0

#include "..\..\Core\core.hpp"

namespace
{
   ShaderHashesList shader_hashes_TAA; // DeferredFX AA
}

struct GameDeviceDataWatchDogs final : public GameDeviceData
{
   com_ptr<ID3D11Resource> motion_vectors;
   com_ptr<ID3D11Resource> depth;

   com_ptr<ID3D11Resource> last_motion_vectors;
   com_ptr<ID3D11RenderTargetView> last_motion_vectors_rtv;

   DirectX::XMMATRIX ViewRotProjectionMatrix;
   DirectX::XMMATRIX ViewProjectionMatrix;
   DirectX::XMMATRIX ProjectionMatrix;
   DirectX::XMMATRIX InvProjectionMatrix;
   DirectX::XMMATRIX InvProjectionMatrixDepth;
   DirectX::XMMATRIX PreviousViewProjectionMatrix;

   float2 taa_jitters = {};
   bool found_per_view_globals = false;

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

   static constexpr uint32_t CBPerViewGlobal_buffer_size = 960;

   static void OnMapBufferRegion(reshade::api::device* device, reshade::api::resource resource, uint64_t offset, uint64_t size, reshade::api::map_access access, void** data)
   {
      ID3D11Device* native_device = (ID3D11Device*)(device->get_native());
      ID3D11Buffer* buffer = reinterpret_cast<ID3D11Buffer*>(resource.handle);
      DeviceData& device_data = *device->get_private_data<DeviceData>();
      // auto& game_device_data = GetGameDeviceData(device_data);

      if (access == reshade::api::map_access::write_only || access == reshade::api::map_access::write_discard || access == reshade::api::map_access::read_write)
      {
         D3D11_BUFFER_DESC buffer_desc;
         buffer->GetDesc(&buffer_desc);

         if (buffer_desc.ByteWidth == CBPerViewGlobal_buffer_size)
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

         if (is_valid_cbuffer)
         {
            // reshade::log::message(reshade::log::level::info, "Found cbuffer viewport.");
            if (!game_device_data.set_jitter)
            {
               using namespace DirectX;

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
               // Use previous VP if last frame is jittere
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
               }
            }
         }
      }
      return false;
   }

   void OnPresent(ID3D11Device* native_device, DeviceData& device_data) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);

      // Update TAA jitters:
      int phases = 16;           // Decent default for any modern TAA
      const int base_phases = 8; // For DLAA
      // We round to the cloest int, though maybe we should floor? Unclear. Both are probably fine.
      phases = (int)std::lrint(float(base_phases) * powf(float(device_data.output_resolution.y) / float(device_data.render_resolution.y), 2.f));
      int temporal_frame = cb_luma_global_settings.FrameIndex % phases;

      // Note: we add 1 to the temporal frame here to avoid a bias, given that Halton always returns 0 for 0
      game_device_data.taa_jitters.x = SR::HaltonSequence(temporal_frame, 2);
      game_device_data.taa_jitters.y = SR::HaltonSequence(temporal_frame, 3);

      game_device_data.set_render_res = false;
      game_device_data.is_last_frame_jittered = game_device_data.set_jitter;
      game_device_data.set_jitter = false;
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

      swapchain_format_upgrade_type = TextureFormatUpgradesType::AllowedEnabled;
      swapchain_upgrade_type = SwapchainUpgradeType::scRGB;
      texture_format_upgrades_type = TextureFormatUpgradesType::None;
      enable_indirect_texture_format_upgrades = false;
      texture_upgrade_formats = {};

      game = new WatchDogs();
   }
   else if (ul_reason_for_call == DLL_PROCESS_DETACH)
   {
      reshade::unregister_event<reshade::addon_event::map_buffer_region>(WatchDogs::OnMapBufferRegion);
      reshade::unregister_event<reshade::addon_event::unmap_buffer_region>(WatchDogs::OnUnmapBufferRegion);
      reshade::unregister_event<reshade::addon_event::clear_render_target_view>(WatchDogs::OnClearRenderTargetView);
   }

   CoreMain(hModule, ul_reason_for_call, lpReserved);

   return TRUE;
}