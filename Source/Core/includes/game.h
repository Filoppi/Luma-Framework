#pragma once

#include "shaders.h"

using namespace Shader;

// "Sub" device data per game, subclassable.
// Make sure all variables are default initialized.
struct GameDeviceData
{
   // Empty by default
};

struct GameInfo
{
   std::string title; // Public title (e.g., "Prey (2017)")
   std::string internal_name; // Internal short name (e.g. "White Knuckle"->"WK"), can be used by shaders etc
   uint32_t id = 0; // Internal ID (enum like). 0 is unknown/generic
   std::string shader_define;
   std::vector<std::string> mod_authors; // List of authors, in importance or arbitrary order
};

// Macro to take the "id" variable name and store it as a shader define name (to avoid defining them twice)
#define MAKE_GAME_INFO(title, internal_name, id, mod_authors) { title, internal_name, id, #id, mod_authors }

// Per game implementation
class Game
{
public:
   virtual ~Game() = default; // Avoids warnings. Using the destructor is not suggested if not to clear up persistently allocated memory.

   // The mod dll loaded (before init). Avoid locking mutexes in here.
   virtual void OnLoad(std::filesystem::path& file_path, bool failed = false) {}
   // The mod initialized. Avoid locking mutexes in here.
   virtual void OnInit(bool async) {}
   virtual void OnInitDevice(ID3D11Device* native_device, DeviceData& device_data) {}
   virtual void OnInitSwapchain(reshade::api::swapchain* swapchain) {}
   // You can create and "append" your custom device data here
   virtual void OnCreateDevice(ID3D11Device* native_device, DeviceData& device_data)
   {
      // Example data
      device_data.game = new GameDeviceData;
   }
   // You can destroy your custom device data here
   virtual void OnDestroyDeviceData(DeviceData& device_data)
   {
      delete device_data.game;
      device_data.game = nullptr;
   }

   virtual bool OverrideCopyResource(ID3D11Device* native_device, DeviceData& device_data, uint64_t& dst_resource, uint64_t& src_resource) { return false; }
   virtual bool OverrideCopyTextureRegion(ID3D11Device* native_device, DeviceData& device_data, uint64_t& dst_resource, uint32_t dst_subresource, const D3D11_BOX* dst_box, uint64_t& src_resource, uint32_t src_subresource, const D3D11_BOX* src_box) { return false; }

   // Optionally returns a modified shader.
   // Requires "ENABLE_ORIGINAL_SHADERS_MEMORY_EDITS" to be enabled.
   // "shader_hash" is 32bit, will be set to -1 if it's not specified.
   virtual std::unique_ptr<std::byte[]> ModifyShaderByteCode(const std::byte* code, size_t& size, reshade::api::pipeline_subobject_type type, uint64_t shader_hash = -1, const std::byte* shader_object = nullptr, size_t shader_object_size = 0) { return nullptr; }

   // Called for every game's valid draw call (any type),
   // this is where you can override passes, add new ones, cancel other ones etc.
   // Requires "ENABLE_POST_DRAW_DISPATCH_CALLBACK" to be enabled, given it has a performance impact.
   // Return true to cancel the original call.
   // Use "original_draw_dispatch_func" to call the original draw/dispatch within your function (you have to cancel it manually if you do so, as that's not tracked). This is useful if you want to add any post draw/dispatch behaviour. "ENABLE_POST_DRAW_DISPATCH_CALLBACK" is required for the it to work. Note that some advanced debugging behaviours might conflict if you replace the draw call.
   virtual DrawOrDispatchOverrideType OnDrawOrDispatch(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, reshade::api::shader_stage stages, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, bool is_custom_pass, bool& updated_cbuffers, std::function<void()>* original_draw_dispatch_func = nullptr) { return DrawOrDispatchOverrideType::None; }
   // This is called every frame just before sending out the final image to the display (the swapchain).
   // You can reliable reset any per frame setting here.
   virtual void OnPresent(ID3D11Device* native_device, DeviceData& device_data) {}
   virtual void UpdateLumaInstanceDataCB(CB::LumaInstanceDataPadded& data, CommandListData& cmd_list_data, DeviceData& device_data) {}
   // Retrieves the game's "global" (main, per view, ...) cbuffer data
   virtual bool UpdateGlobalCB(const void* global_buffer_data_ptr, reshade::api::device* device) { return false; }

   // Load ReShade configs on boot
   virtual void LoadConfigs() {}
   // Triggered when the user swaps between SDR and HDR output (in the game settings, not the display state)
   virtual void OnDisplayModeChanged() {}
   // Called when users change the "advanced" settings shader defines (e.g. you can grey out certain settings based on other settings here)
   virtual void OnShaderDefinesChanged() {}
   // Draw and save your settings here
   virtual void DrawImGuiSettings(DeviceData& device_data) {}
#if DEVELOPMENT
   // Same as "DrawImGuiSettings()" but for development settings
   virtual void DrawImGuiDevSettings(DeviceData& device_data) {}
#endif // DEVELOPMENT
#if DEVELOPMENT || TEST
   // You can print game specific information here (e.g. the weapon FOV, once you got access to the projection matrix)
   virtual void PrintImGuiInfo(const DeviceData& device_data) {}
#endif
   // About and Credits section
   virtual void PrintImGuiAbout() {}

   // In case you knew, you can specify whether the game was paused here
   virtual bool IsGamePaused(const DeviceData& device_data) const { return false; }
   // Values between 0 and 0.25 are usually good
   virtual float GetTonemapUIBackgroundAmount(const DeviceData& device_data) const { return 0.f; }
   // In case your SR implementation had any extra resources, you can clean them up here
   virtual void CleanExtraSRResources(DeviceData& device_data) {}
};
