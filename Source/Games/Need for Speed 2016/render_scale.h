#pragma once

#include "frostbite_settings.h"
#include "log.h"

// The game's resolution scale (what the "-Render.ResolutionScale" launch option sets), in Frostbite's "Render" settings container: the
// scene renders at this multiple of the screen resolution, then the game scales it down to the screen after the TAA or SR
// (supersampling). A Luma setting: saved, and applied from the first frame in game. Until it's changed, the game's own value stays.
class RenderScale
{
public:
   void LoadConfig();
   // Looks up the game's setting (once, at the first frame in game) and applies the saved scale
   void Find(const Frostbite::SettingsLookup& lookup);
   void DrawSetting();

private:
   static constexpr const char* kConfigName = "ResolutionScale";
   static constexpr float kDefault = 1.f;
   static constexpr float kMin = 1.f;
   static constexpr float kMax = 2.f;

   // A value the field can hold, to tell it from another class's data at the same offset
   static bool IsPlausible(float value)
   {
      return value >= 0.25f && value <= 4.f;
   }

   void Apply();

   bool searched = false;
   float* game_scale = nullptr; // In the game's live settings
   float scale = kDefault;
   bool saved = false; // Whether a scale was chosen in the settings (else the game's own stays)
};

// The saved scale, if one was chosen. It's applied once the game's setting is found.
inline void RenderScale::LoadConfig()
{
   saved = reshade::get_config_value(nullptr, NAME, kConfigName, scale);
   scale = std::clamp(scale, kMin, kMax);
}

// The field's offset from the type info, the live container through the settings lookup, and a sanity check of its value
inline void RenderScale::Find(const Frostbite::SettingsLookup& lookup)
{
   if (searched)
      return;
   searched = true;
   Frostbite::FieldOffset fields[] = {{"ResolutionScale"}};
   const uint16_t container_size = Frostbite::FindClassFields("GameRenderSettings", fields);
   if (container_size == 0)
   {
      LogDiagnostic(reshade::log::level::warning, "the game's resolution scale wasn't found in its type info");
      return;
   }
   uint8_t* const container = lookup.GetContainer("Render");
   if (!container || !MemoryScan::IsReadable(container, container_size))
   {
      LogDiagnostic(reshade::log::level::warning, "the engine's \"Render\" settings weren't found");
      return;
   }
   float* const field = reinterpret_cast<float*>(container + fields[0].offset);
   if (!IsPlausible(*field))
   {
      LogDiagnostic(reshade::log::level::warning, "the game's resolution scale reads {}, so it's not the field", *field);
      return;
   }
   game_scale = field;
   LogDiagnostic(reshade::log::level::info, "the game's resolution scale was found: {}", *game_scale);
   if (saved)
      Apply();
   else
      scale = std::clamp(*game_scale, kMin, kMax);
}

// A slider with a reset button, saved and applied when changed
inline void RenderScale::DrawSetting()
{
   ImGui::BeginDisabled(!game_scale);
   // Clamped when typed too, and applied when the edit ends, not on every drag step (a change can resize the game's render textures)
   ImGui::SliderFloat("Resolution Scale", &scale, kMin, kMax, "%.2f", ImGuiSliderFlags_AlwaysClamp);
   bool changed = ImGui::IsItemDeactivatedAfterEdit();
   if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      ImGui::SetTooltip("Renders the game at this multiple of the screen resolution, then scales it down (supersampling).\nSharper, but much heavier: 2 renders 4 times the pixels.\nGreyed out while the game's own setting isn't available (it's looked up at the first frame in game).");
   changed |= DrawResetButton<float, false>(scale, kDefault, kConfigName);
   ImGui::EndDisabled();
   if (!changed)
      return;
   saved = true;
   reshade::set_config_value(nullptr, NAME, kConfigName, scale);
   Apply();
}

// The engine reads it every frame and resizes its render textures itself
inline void RenderScale::Apply()
{
   *game_scale = scale;
   LogDiagnostic(reshade::log::level::info, "set the game's resolution scale to {}", scale);
}
