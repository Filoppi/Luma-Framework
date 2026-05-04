#define GAME_BLUE_REFLECTION_SECOND_LIGHT 1

#define ENABLE_NGX 0
// Hooking a debugger is forbidden
#define DISABLE_AUTO_DEBUGGER 1
#define DEBUG_LOG 0

//#define ENABLE_ORIGINAL_SHADERS_MEMORY_EDITS 1

#include "..\..\Core\core.hpp"
#include "..\..\Core\includes\shader_patching.h"
#include "includes\hooks.hpp"
#include "includes\safetyhook.hpp"
#include "includes\hooks.cpp"
#include "includes\stretchy_buffer.hpp"

struct PreviousSkinCache
{
   uint offset;
   uint stride;
};

struct SkinCacheEntry
{
   uint32_t offset;
   uint32_t stride;
};

float2 projection_jitters = {0, 0};
float2 output_resolution = {0, 0};

namespace
{
   union word_t
   {
      float f;
      int32_t i;
      uint32_t u;
      std::byte b[4];
   };
   
   std::shared_mutex materials_mutex;
   
   std::unordered_set<ID3D11DeviceContext*> motion_vector_contexts;
}

struct GameDeviceDataBlueReflectionSecondLight final : public GameDeviceData
{
   // resources used to identify the deferred context used for scene drawing
   std::atomic<ID3D11CommandList*> remainder_command_list;
   std::atomic<ID3D11DeviceContext*> draw_device_context = nullptr;

   // textures we got from the game
   ComPtr<ID3D11Texture2D> source_color;
   ComPtr<ID3D11ShaderResourceView> source_color_srv;
   ComPtr<ID3D11Buffer> viewport_cbv;
   ComPtr<ID3D11ShaderResourceView> sr_output_color_srv;
   
   ComPtr<ID3D11Texture2D> decoded_motion_vectors;
   ComPtr<ID3D11UnorderedAccessView> decoded_motion_vectors_uav;

   // the command list we split to interject dlss
   ComPtr<ID3D11CommandList> partial_command_list;

   ComPtr<ID3D11Buffer> modifiable_index_vertex_buffer;
   
   ComPtr<ID3D11Buffer> cbuffer_skin_cache;
   std::unique_ptr<StretchyBuffer> prev_skin_buffer;
   std::unique_ptr<StretchyBuffer> skin_buffer;
   std::unordered_map<ID3D11Buffer*, SkinCacheEntry> prev_skin_lookup;
   std::unordered_map<ID3D11Buffer*, SkinCacheEntry> skin_lookup;
   
   std::mutex game_device_data_mutex;

   DirectX::XMMATRIX CameraSpaceToPreviousProjectedSpace;
   DirectX::XMMATRIX PreviousViewRotProjectionMatrix;
   
   float2 render_resolution;
   
   void CleanMVResources()
   {
      decoded_motion_vectors.reset();
      decoded_motion_vectors_uav.reset();
   }
};

class BlueReflectionSecondLight final : public Game
{
   static GameDeviceDataBlueReflectionSecondLight& GetGameDeviceData(DeviceData& device_data)
   {
      return *static_cast<GameDeviceDataBlueReflectionSecondLight*>(device_data.game);
   }

   static const GameDeviceDataBlueReflectionSecondLight& GetGameDeviceData(const DeviceData& device_data)
   {
      return *static_cast<const GameDeviceDataBlueReflectionSecondLight*>(device_data.game);
   }

public:
   void OnInit(bool async) override
   {
      HMODULE engine_module = nullptr;
      while (!engine_module)
      {
         engine_module = GetModuleHandleA("BLUE REFLECTION Second Light.exe");
         Sleep(100);
      }
      auto base_addr = (uintptr_t)engine_module;
      auto dos_header = reinterpret_cast<PIMAGE_DOS_HEADER>(engine_module);
      auto nt_headers = reinterpret_cast<PIMAGE_NT_HEADERS>(reinterpret_cast<std::byte*>(engine_module) + dos_header->e_lfanew);
      std::size_t section_size = nt_headers->OptionalHeader.SizeOfImage;
      
      uintptr_t camera_addr = base_addr + 0x19C18E0;
      
      CameraData = *reinterpret_cast<uintptr_t**>(base_addr + 0x19C18E0);
      
      RenderResolution = base_addr + 0x18C28E0;

      auto WILDCARD = System::BytePattern(System::BytePattern::WildcardType::Wildcard);

      std::vector<System::BytePattern> pattern = {
         0x40, 0x53, 0x48, 0x83, 0xEC,
         WILDCARD,
         0xF3, 0x0F, 0x59, 0x0D,
         WILDCARD, WILDCARD, WILDCARD, WILDCARD,
         0x48, 0x8B, 0xD9, 0x0F, 0x29, 0x74, 0x24
      };

      auto results = System::ScanMemoryForPattern(
         reinterpret_cast<std::byte*>(engine_module),
         section_size,
         pattern
         );

      if (!results.empty() && !g_compute_projection_matrix_hook)
      {
         void* fn = reinterpret_cast<void*>(results[0]);

         g_compute_projection_matrix_hook = safetyhook::create_inline(
            fn,
            Hooked_ComputeProjectionMatrix
            );
      }
      
      pattern = {
         0x40, 0x53, 0x48, 0x83, 0xEC,
         WILDCARD,
         0xF6, 0x81,
         WILDCARD, WILDCARD, WILDCARD, WILDCARD,
         WILDCARD,
         0x48, 0x8B, 0xD9, 0x0F, 0x84
      };
      
      results = System::ScanMemoryForPattern(
         reinterpret_cast<std::byte*>(engine_module),
         section_size,
         pattern
         );

      if (!results.empty() && !g_camera_compute_projection_matrix_hook)
      {
         void* fn = reinterpret_cast<void*>(results[0]);

         g_camera_compute_projection_matrix_hook = safetyhook::create_inline(
            fn,
            Hooked_CameraComputeProjectionMatrix
            );
      }
      
      GetShaderDefineData(POST_PROCESS_SPACE_TYPE_HASH).SetDefaultValue('1'); // Game was all linear, rendering is R16G16B16A16_FLOAT and post processing + UI is R8G8B8A8_UNORM_SRGB or B8G8R8A8_UNORM_SRGB.
      GetShaderDefineData(GAMMA_CORRECTION_TYPE_HASH).SetDefaultValue('0');   // Game seemengly looks better (less crush, less unnatural shadow) in sRGB than 2.2
      GetShaderDefineData(UI_DRAW_TYPE_HASH).SetDefaultValue('2');
   }

   void OnLoad(std::filesystem::path& file_path, bool failed) override
   {
      if (!failed)
      {
      }
   }

   void OnCreateDevice(ID3D11Device* native_device, DeviceData& device_data) override
   {
      device_data.game = new GameDeviceDataBlueReflectionSecondLight;
   }
   
   void OnInitDevice(ID3D11Device* native_device, DeviceData& device_data) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);
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
   
   DrawOrDispatchOverrideType OnDrawOrDispatch(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, reshade::api::shader_stage stages, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, bool is_custom_pass, bool& updated_cbuffers, std::function<void()>* original_draw_dispatch_func) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);

      return DrawOrDispatchOverrideType::None;
   }
   
   void UpdateLumaInstanceDataCB(CB::LumaInstanceDataPadded& data, CommandListData& cmd_list_data, DeviceData& device_data) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);
   }
   
   void OnPresent(ID3D11Device* native_device, DeviceData& device_data) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);
      
      auto index = cb_luma_global_settings.FrameIndex % 8;
      projection_jitters.x = SR::HaltonSequence(index+1, 2);
      projection_jitters.y = SR::HaltonSequence(index+1, 3);
      
      int width  = *(int*)(RenderResolution + 0x00);
      int height = *(int*)(RenderResolution + 0x04);
      device_data.render_resolution.x = static_cast<float>(width);
      device_data.render_resolution.y = static_cast<float>(height);
   }
};

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
   if (ul_reason_for_call == DLL_PROCESS_ATTACH)
   {
      Globals::SetGlobals(PROJECT_NAME, "Blue Reflection Second Light Luma mod");
      Globals::DEVELOPMENT_STATE = Globals::ModDevelopmentState::WorkInProgress;
      Globals::VERSION = 1;

      luma_settings_cbuffer_index = 13;
      luma_data_cbuffer_index = 12;
      
      enable_samplers_upgrade = false;
      force_disable_display_composition = true;

      swapchain_format_upgrade_type = TextureFormatUpgradesType::None;
      swapchain_upgrade_type = SwapchainUpgradeType::None;
      texture_format_upgrades_type = TextureFormatUpgradesType::None;
      texture_upgrade_formats = {
      };
#if DEVELOPMENT
      forced_shader_names.emplace(Shader::Hash_StrToNum("E48D0C69"), "SSR Raytrace");
#endif

      game = new BlueReflectionSecondLight();
   }
   else if (ul_reason_for_call == DLL_PROCESS_DETACH)
   {
      g_compute_projection_matrix_hook.reset();
      g_camera_compute_projection_matrix_hook.reset();
   }

   CoreMain(hModule, ul_reason_for_call, lpReserved);

   return TRUE;
}