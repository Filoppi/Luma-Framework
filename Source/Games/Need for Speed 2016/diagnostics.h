#pragma once

#include "log.h"
#include "super_resolution_pass.h"

// Development and Test builds: the Info tab, the first SR draw in the log, and a counted warning for every run of resolves where the
// game's TAA ran instead of the selected SR. Development builds also have the TAA bypass. In Publishing builds it's an empty class whose
// calls compile to nothing (at the end of the file).
#if DEVELOPMENT || TEST

class Diagnostics
{
public:
   // From any thread
   void OnDeferredResolve();
   // After each resolve where an SR type was selected
   void OnSRResult(SRResult result, SR::Type type, const Camera* camera, RenderSize render_size, const ResolveResources& resources);
   void OnFrameEnd(bool resolve_ran, SRResult result, const Camera* camera, RenderSize render_size);

   // The Development TAA bypass, which also needs the depth of field resolve to run first
   bool IsBypassOn() const
   {
      return bypass_taa;
   }
   // Replaces the resolve with its raw jittered color input, to check the jitter. False if it's off, or the textures differ.
   bool TryBypassTAA(ID3D11DeviceContext* context, const ResolveResources& resources) const;

   void DrawInfo(float texture_mip_lod_bias, const std::string& engine_jitter) const;
   void DrawDevSettings();

private:
   static const char* GetName(SRResult result);
   // True on the 1st, 10th, 100th... time, to log a repeated warning with its count without flooding the log
   static bool IsLogCount(uint64_t count);
   static std::string DescribeTexture(ID3D11Resource* texture);
   static void LogFirstDraw(SRResult result, SR::Type type, const Camera& camera, RenderSize render_size);
   static void LogUnfitTextures(const ResolveResources& resources, RenderSize render_size);
   void TrackFallbacks(SRResult result, RenderSize render_size);
   void LogStreak(const char* state) const;
   void DrawCameraInfo() const;

   std::atomic<uint64_t> deferred_resolves = 0;
   bool logged_first_draw = false;
   uint64_t sr_resolves = 0;
   uint64_t fallback_resolves = 0;
   // The current run of resolves with the same result
   SRResult streak_result = SRResult::Drawn;
   uint64_t streak_length = 0; // 0 while SR draws
   RenderSize streak_render_size;
   bool bypass_taa = false; // Only drawn in Development builds' settings

   // Last frame, for the Info tab
   bool last_resolve_ran = false;
   SRResult last_result = SRResult::Off;
   bool last_has_camera = false;
   Camera last_camera = {};
   RenderSize last_render_size;
};

// Counted: the camera is only captured on the immediate context, so SR can't run there
inline void Diagnostics::OnDeferredResolve()
{
   const uint64_t count = ++deferred_resolves;
   if (IsLogCount(count))
      LogDiagnostic(reshade::log::level::warning, "the TAA resolve ran on a deferred context, so the game's TAA ran ({} times)", count);
}

// Counts the result, logs the first draw and the first unfit textures of a run, and tracks runs of fallbacks
inline void Diagnostics::OnSRResult(SRResult result, SR::Type type, const Camera* camera, RenderSize render_size, const ResolveResources& resources)
{
   if (result == SRResult::Drawn)
      sr_resolves++;
   else
      fallback_resolves++;

   const bool drew = result == SRResult::Drawn || result == SRResult::Failed;
   if (drew && camera && !logged_first_draw)
   {
      logged_first_draw = true;
      LogFirstDraw(result, type, *camera, render_size);
   }
   if (result == SRResult::Unfit && streak_result != SRResult::Unfit)
      LogUnfitTextures(resources, render_size);
   TrackFallbacks(result, render_size);
}

// Keeps the frame's state for the Info tab, which ImGui draws after it ends
inline void Diagnostics::OnFrameEnd(bool resolve_ran, SRResult result, const Camera* camera, RenderSize render_size)
{
   last_resolve_ran = resolve_ran;
   last_result = result;
   last_has_camera = camera != nullptr;
   if (camera)
      last_camera = *camera;
   last_render_size = render_size;
}

// The resolve's output and history get the jittered color as is
inline bool Diagnostics::TryBypassTAA(ID3D11DeviceContext* context, const ResolveResources& resources) const
{
   ID3D11Resource* color = resources.color.get();
   if (!bypass_taa || !color || !resources.output || !resources.history)
      return false;
   if (!AreResourcesEqual(color, resources.output.get()) || !AreResourcesEqual(color, resources.history.get()))
      return false;
   context->CopyResource(resources.output.get(), color);
   context->CopyResource(resources.history.get(), color);
   return true;
}

// Luma's Info tab: the last frame's resolve, SR and camera, the engine's jitter and the mip bias
inline void Diagnostics::DrawInfo(float texture_mip_lod_bias, const std::string& engine_jitter) const
{
   ImGui::NewLine();
   const char* resolve_state = "not running (FXAA, a menu or loading)";
   if (last_resolve_ran)
      resolve_state = "running";
   ImGui::Text("TAA resolve: %s", resolve_state);
   ImGui::Text("Super resolution: %s", GetName(last_result));
   ImGui::Text("Resolves with SR: %llu, with the game's TAA instead: %llu", sr_resolves, fallback_resolves);
   DrawCameraInfo();
   ImGui::Text("Engine jitter: %s", engine_jitter.c_str());
   ImGui::Text("Texture mip LOD bias: %.2f", texture_mip_lod_bias);
}

// In Luma's dev settings (Development builds)
inline void Diagnostics::DrawDevSettings()
{
   ImGui::Checkbox("Bypass TAA (show the raw jittered frame)", &bypass_taa);
   if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      ImGui::SetTooltip("Shows the frame before the TAA or SR, to check the jitter. Not saved.");
}

// For the Info tab and the log
inline const char* Diagnostics::GetName(SRResult result)
{
   switch (result)
   {
   case SRResult::Off:
      return "off (None selected, or no TAA)";
   case SRResult::Drawn:
      return "active";
   case SRResult::NoCamera:
      return "the game's TAA (no camera bound)";
   case SRResult::Unfit:
      return "the game's TAA (the resolve's textures don't fit)";
   case SRResult::Failed:
      return "the game's TAA (SR failed, see the log)";
   }
   return "unknown";
}

// Strips the trailing zeros: only a power of ten leaves 1
inline bool Diagnostics::IsLogCount(uint64_t count)
{
   while (count >= 10 && count % 10 == 0)
      count /= 10;
   return count == 1;
}

// Size, format, slices and mips, for the log
inline std::string Diagnostics::DescribeTexture(ID3D11Resource* texture)
{
   if (!texture)
      return "none";
   uint4 size;
   DXGI_FORMAT format;
   GetResourceInfo(texture, size, format);
   return std::format("{}x{} {} ({} slices, {} mips)", size.x, size.y, GetFormatNameSafe(format), size.z, size.w);
}

// Once per device, so the log shows what SR was given
inline void Diagnostics::LogFirstDraw(SRResult result, SR::Type type, const Camera& camera, RenderSize render_size)
{
   const Jitter jitter = camera.GetJitter(render_size);
   LogDiagnostic(reshade::log::level::info, "first SR draw ({}): ok {}, {}x{}, jitter ({:+.3f}, {:+.3f}) px, near plane {:.3f}, vertical FOV {:.3f} rad", SR::GetTypeName(type), result == SRResult::Drawn, render_size.width, render_size.height, jitter.x, jitter.y, camera.GetNearPlane(), camera.GetVerticalFov());
}

// SR needs the color output (u2) at the render size, the history (u3) like it, and the output apart from the inputs
inline void Diagnostics::LogUnfitTextures(const ResolveResources& resources, RenderSize render_size)
{
   LogDiagnostic(reshade::log::level::warning, "the resolve's textures don't fit SR at {}x{}: output (u2) {}, history (u3) {}, color {}, depth {}, MVs {}", render_size.width, render_size.height, DescribeTexture(resources.output.get()), DescribeTexture(resources.history.get()), DescribeTexture(resources.color.get()), DescribeTexture(resources.depth.get()), DescribeTexture(resources.mvs.get()));
}

// A run of resolves where the game's TAA ran instead of SR is logged when it ends (a short one is a visible flicker), and while it lasts
// at 1000, 10000... resolves (one that never ends is SR not engaging)
inline void Diagnostics::TrackFallbacks(SRResult result, RenderSize render_size)
{
   if (result == streak_result)
   {
      if (result == SRResult::Drawn)
         return;
      streak_length++;
      if (streak_length >= 1000 && IsLogCount(streak_length))
         LogStreak("so far");
      return;
   }
   if (streak_length > 0)
      LogStreak("in a row");
   streak_result = result;
   // 0 while SR draws
   streak_length = 0;
   if (result != SRResult::Drawn)
      streak_length = 1;
   streak_render_size = render_size;
}

// "state" is "in a row" for a run that ended, "so far" for one still going
inline void Diagnostics::LogStreak(const char* state) const
{
   LogDiagnostic(reshade::log::level::warning, "{} for {} resolves {} at {}x{}", GetName(streak_result), streak_length, state, streak_render_size.width, streak_render_size.height);
}

// The last frame's main camera, as SR got it
inline void Diagnostics::DrawCameraInfo() const
{
   if (!last_has_camera)
   {
      ImGui::Text("Camera: none bound");
      return;
   }
   const Jitter jitter = last_camera.GetJitter(last_render_size);
   const float fov_degrees = last_camera.GetVerticalFov() * 180.f / 3.14159265f;
   ImGui::Text("Camera: %ux%u, jitter (%+.3f, %+.3f) px, vertical FOV %.1f degrees", last_render_size.width, last_render_size.height, jitter.x, jitter.y, fov_degrees);
}

#else

// Publishing builds: the same calls, all empty
class Diagnostics
{
public:
   void OnDeferredResolve()
   {
   }
   void OnSRResult(SRResult, SR::Type, const Camera*, RenderSize, const ResolveResources&)
   {
   }
   void OnFrameEnd(bool, SRResult, const Camera*, RenderSize)
   {
   }
   bool IsBypassOn() const
   {
      return false;
   }
   bool TryBypassTAA(ID3D11DeviceContext*, const ResolveResources&) const
   {
      return false;
   }
};

#endif // DEVELOPMENT || TEST
