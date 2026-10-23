#pragma once
#include <atomic>
// Included by main.cpp: use Luma's existing ReShade registration, menu and MinHook.
extern "C"
{
#include "MouseFix_Settings.h"
   int QBMF_StartIntegrated(HMODULE, QBMF_GetConfigFn, QBMF_SetConfigFn);
   void QBMF_StopIntegrated(void);
   void QBMF_SetOverlayCapture(int);
   const char* QBMF_GetStatus(void);
   int QBMF_GetRawMouseEnabled(void);
   int QBMF_SetRawMouseEnabled(int);
   int QBMF_GetMotionBlurEnabled(void);
   int QBMF_SetMotionBlurDisabled(int);
   int QBMF_GetBloomEnabled(void);
   int QBMF_SetBloomDisabled(int);
   int QBMF_GetFlaresEnabled(void);
   int QBMF_SetFlaresDisabled(int);
   int QBMF_GetDepthOfFieldEnabled(void);
   int QBMF_SetDepthOfFieldDisabled(int);
   int QBMF_GetCutsceneEnabled(void);
   int QBMF_SetCutsceneEnabled(int);
   int QBMF_GetHighResolutionParticles(void);
   int QBMF_SetHighResolutionParticles(int);
}
namespace QuantumBreakFixes
{
   inline std::atomic<bool> attempted{false};
   inline std::atomic<bool> available{false};
   inline bool GetConfig(void*, void*, const char* section, const char* key, char* value, size_t* size)
   {
      return reshade::get_config_value(nullptr, section, key, value, size);
   }
   inline void SetConfig(void*, void*, const char* section, const char* key, const char* value)
   {
      reshade::set_config_value(nullptr, section, key, value);
   }
   inline bool Overlay(reshade::api::effect_runtime*, bool open, reshade::api::input_source)
   {
      QBMF_SetOverlayCapture(open ? 1 : 0);
      return false;
   }
   inline void Initialize()
   {
      if (attempted.exchange(true)) return;
      available = QBMF_StartIntegrated(reshade::internal::get_current_module_handle(), GetConfig, SetConfig) != 0;
      if (available) reshade::register_event<reshade::addon_event::reshade_open_overlay>(Overlay);
   }
   inline bool Draw()
   {
      if (!available)
      {
         ImGui::TextDisabled("Native fixes: %s", QBMF_GetStatus());
         return false;
      }
      bool reset_history = false;
      struct Control
      {
         const char* label;
         int (*get)();
         int (*set)(int);
         bool inverted;
      };
      const Control controls[] = {
         {"Fix Mouse Input", QBMF_GetRawMouseEnabled, QBMF_SetRawMouseEnabled, false},
         {"Disable Motion Blur", QBMF_GetMotionBlurEnabled, QBMF_SetMotionBlurDisabled, true},
         {"Disable Bloom", QBMF_GetBloomEnabled, QBMF_SetBloomDisabled, true},
         {"Disable Flares", QBMF_GetFlaresEnabled, QBMF_SetFlaresDisabled, true},
         {"Disable Depth of Field", QBMF_GetDepthOfFieldEnabled, QBMF_SetDepthOfFieldDisabled, true},
         {"Unlock Cutscene FPS", QBMF_GetCutsceneEnabled, QBMF_SetCutsceneEnabled, false},
         {"High Resolution Particles", QBMF_GetHighResolutionParticles, QBMF_SetHighResolutionParticles, false},
      };
      for (const auto& control : controls)
      {
         const int state = control.get();
         bool value = control.inverted ? state == 0 : state != 0;
         ImGui::BeginDisabled(state < 0);
         if (ImGui::Checkbox(control.label, &value) && control.set(value ? 1 : 0))
            reset_history |= control.set != QBMF_SetRawMouseEnabled;
         ImGui::EndDisabled();
      }
      return reset_history;
   }
} // namespace QuantumBreakFixes
