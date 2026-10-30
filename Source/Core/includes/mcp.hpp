#pragma once

#include <cfloat>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <memory>
#include <string>
#include <utility>

#include <d3d11.h>
#include <include/reshade.hpp>

struct CommandListData; // instance_data.h
struct DeviceData;      // instance_data.h
namespace SR
{
   enum class Type;           // super_resolution.h
   class SuperResolutionImpl; // super_resolution.h
} // namespace SR

// Luma MCP backend (DEVELOPMENT only), implemented in "mcp_server.inl" (included by "core.hpp" after the core state it drives).
// A named pipe serves the requests of "Scripts/luma_mcp.py" on the render thread, at present.
namespace Mcp
{
   // Called with the validated value (on the render thread) instead of a plain write, for values whose change has side effects (a mirror
   // into the shader settings, a mode switch). Returns an error, or nothing.
   using DevValueSetter = std::function<std::string(DeviceData& device_data, double value)>;
   // Returns a texture, or a view of it, borrowed (null while it isn't allocated), see "MCP_GAME_TEXTURE"
   using DevTextureGetter = ID3D11DeviceChild* (*)(DeviceData& device_data);

   struct FloatValue
   {
      const char* name;
      float* value;
      float min = -FLT_MAX;
      float max = FLT_MAX;
   };
   struct IntValue
   {
      const char* name;
      int* value;
      int min;
      int max;
      DevValueSetter set = {};
   };

   // Game dev values (toggles, tweakables, counters and textures) exposed to "luma_dev_values", registered by the game code.
   // Values are read and written on the render thread at present, where games usually swap their per frame counters.
   // Some are persisted user settings: writing them here bypasses the config, fine for A/B tests.
   void RegisterToggles(std::initializer_list<std::pair<const char*, bool*>> values);
   // A toggle the shaders read as a 0/1 float from the settings buffer too (e.g. "&cb_luma_global_settings.GameSettings.LumaBloomEnable")
   void RegisterMirroredToggle(const char* name, bool* value, float* mirror);
   void RegisterValues(std::initializer_list<FloatValue> values);
   void RegisterInts(std::initializer_list<IntValue> values);
   // Prefer the per frame copy of a counter (the value from the last finished frame) over the one being incremented
   void RegisterCounter(std::string name, const uint32_t* value, const void* owner = nullptr);
   void RegisterCounters(std::initializer_list<std::pair<const char*, const uint32_t*>> values, const void* owner = nullptr);
   // Luma's own textures: its passes run natively, outside the ReShade events the pass readback hooks. They are read at present, so they
   // hold their last content (usually this frame's).
   void RegisterTextures(std::initializer_list<std::pair<const char*, DevTextureGetter>> textures);
   // Removes the values registered with "owner" (e.g. "&device_data")
   void Unregister(const void* owner);

   // Wraps an SR implementation to record what games feed it ("luma_sr_state", "luma_sr_capture") and trace its draws
   std::unique_ptr<SR::SuperResolutionImpl> TapSuperResolution(SR::Type type, std::unique_ptr<SR::SuperResolutionImpl> implementation);

   // Called from "Init()": starts the pipe server and registers Core's knobs
   void Start();
   // Called from "OnPresent" on the render thread, before display composition
   void OnPresent(DeviceData& device_data, CommandListData& cmd_list_data, ID3D11DeviceContext* native_device_context, reshade::api::swapchain* swapchain);
   // Called on DLL unload
   void Shutdown();
} // namespace Mcp

// A "RegisterTextures" entry for a member of the game's device data (a texture or a view of it), from where "GetGameDeviceData" is visible
#define MCP_GAME_TEXTURE(name, member) {name, [](DeviceData& device_data) -> ID3D11DeviceChild* { return GetGameDeviceData(device_data).member.get(); }}
