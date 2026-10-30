#pragma once

#include <include/reshade.hpp>

struct DeviceData; // instance_data.h

// Luma's dev UI (DEVELOPMENT only), the parts moved out of "core.hpp" so far. Implemented in the ".inl" files of this folder (included by "core.hpp"
// after the modules they draw).
namespace UI
{
   // "trace_ui.inl": the Captured Commands tab (of the frame capture, see "trace.hpp"), and the debug draw settings (in the Settings tab)
   void DrawCaptureTab(reshade::api::effect_runtime* runtime, DeviceData& device_data, bool& open_settings_tab);
   void DrawDebugDrawSettings(DeviceData& device_data);
} // namespace UI
