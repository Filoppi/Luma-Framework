#pragma once

#include "camera.h"
#include "taa_resolve.h"

// What SR did at a resolve
enum class SRResult : uint8_t
{
   Off, // No SR selected, or no resolve
   Drawn,
   // The game's TAA ran instead:
   NoCamera, // No main camera was bound (no jitter to compensate, SR would shake)
   Unfit,    // The resolve's textures don't fit SR
   Failed,   // SR failed to draw
};

// An SR type is picked, core hasn't suppressed it, and its instance exists
inline bool IsSRSelected(const DeviceData& device_data)
{
   return device_data.sr_type != SR::Type::None && !device_data.sr_suppressed && device_data.GetSRInstanceData();
}

// The selected SR's jitter phase count, within a sane range for the engine's sequence
inline uint32_t GetSRJitterPhases(const DeviceData& device_data)
{
   const int phases = sr_implementations[device_data.sr_type]->GetJitterPhases(device_data.GetSRInstanceData());
   return uint32_t(std::clamp(phases, 1, 64));
}

// Runs SR in place of the game's TAA resolve, at its render resolution (no upscaling). SR writes the resolve's color output (u2), which is
// then copied to the history (u3): the game's resolve writes the same values to both.
class SuperResolutionPass
{
public:
   SuperResolutionPass();

   SRResult Draw(ID3D11DeviceContext* context, const DeviceData& device_data, const ResolveResources& resources, const Camera* camera, RenderSize render_size, float frame_seconds);

private:
   using DrawData = SR::SuperResolutionImpl::DrawData;

   // The projection has an infinite far plane: SR only uses this to linearize depth
   static constexpr float kFarPlane = 100000.f;

   static bool CanWriteOutput(const ResolveResources& resources, RenderSize render_size);
   bool UpdateSettings(SR::SuperResolutionImpl& sr, SR::InstanceData* instance, ID3D11DeviceContext* context, RenderSize render_size);
   static DrawData MakeDrawData(const ResolveResources& resources, const Camera& camera, RenderSize render_size, bool reset, float frame_seconds);
   static bool DrawPreservingState(SR::SuperResolutionImpl& sr, SR::InstanceData* instance, ID3D11DeviceContext* context, UINT uav_count, const DrawData& draw_data, ID3D11Resource* history);

   SR::SettingsData settings;
   RenderSize last_size; // SR's last draw size, a change resets its history
};

// FSR 3 compares the settings with memcmp, so padding with garbage would recreate its context (and lose its history) any frame. Every
// field is set before each use.
inline SuperResolutionPass::SuperResolutionPass()
{
   std::memset(&settings, 0, sizeof(settings));
}

// Only with the main camera and fitting textures, else the game's TAA runs. The history restarts when core asks or the size changes.
inline SRResult SuperResolutionPass::Draw(ID3D11DeviceContext* context, const DeviceData& device_data, const ResolveResources& resources, const Camera* camera, RenderSize render_size, float frame_seconds)
{
   if (!camera || !camera->IsMain(render_size))
      return SRResult::NoCamera;
   if (!CanWriteOutput(resources, render_size))
      return SRResult::Unfit;

   SR::SuperResolutionImpl& sr = *sr_implementations[device_data.sr_type];
   SR::InstanceData* instance = device_data.GetSRInstanceData();
   if (!UpdateSettings(sr, instance, context, render_size))
      return SRResult::Failed;

   const bool reset = device_data.force_reset_sr || render_size != last_size;
   const DrawData draw_data = MakeDrawData(resources, *camera, render_size, reset, frame_seconds);
   if (!DrawPreservingState(sr, instance, context, device_data.uav_max_count, draw_data, resources.history.get()))
      return SRResult::Failed;
   last_size = render_size;
   return SRResult::Drawn;
}

// SR writes the output at the render size, the history is a copy of it, and the output can't be one of SR's inputs
inline bool SuperResolutionPass::CanWriteOutput(const ResolveResources& resources, RenderSize render_size)
{
   ID3D11Resource* output = resources.output.get();
   if (!output || !resources.depth || !resources.mvs || !resources.color || !resources.history)
      return false;
   if (output == resources.history.get() || output == resources.color.get() || output == resources.depth.get() || output == resources.mvs.get())
      return false;
   return GetTextureSize(output) == render_size && AreResourcesEqual(output, resources.history.get());
}

// False if SR couldn't take the settings (e.g. its context couldn't be created)
inline bool SuperResolutionPass::UpdateSettings(SR::SuperResolutionImpl& sr, SR::InstanceData* instance, ID3D11DeviceContext* context, RenderSize render_size)
{
   settings.output_width = render_size.width;
   settings.output_height = render_size.height;
   settings.render_width = render_size.width;
   settings.render_height = render_size.height;
   settings.dynamic_resolution = false;
   settings.hdr = true;            // Linear HDR scene color
   settings.inverted_depth = true; // Reverse-Z
   settings.mvs_jittered = false;
   // The motion vectors are the NDC offset from the previous position: SR wants it in pixels, y down
   settings.mvs_x_scale = -0.5f * float(render_size.width);
   settings.mvs_y_scale = 0.5f * float(render_size.height);
   settings.auto_exposure = true;
   settings.render_preset = 0;
   return sr.UpdateSettings(instance, context, settings);
}

// The resolve's textures, and the camera's jitter, near plane and FOV
inline SuperResolutionPass::DrawData SuperResolutionPass::MakeDrawData(const ResolveResources& resources, const Camera& camera, RenderSize render_size, bool reset, float frame_seconds)
{
   DrawData draw_data;
   draw_data.reset = reset;
   draw_data.source_color = resources.color.get();
   draw_data.output_color = resources.output.get();
   draw_data.motion_vectors = resources.mvs.get();
   draw_data.depth_buffer = resources.depth.get();
   draw_data.render_width = render_size.width;
   draw_data.render_height = render_size.height;

   // Float rounding can push a sample on the pixel's edge (the engine's own Halton starts at its corner) just outside [-0.5, 0.5], and
   // XeSS then rejects the frame
   const Jitter jitter = camera.GetJitter(render_size);
   draw_data.jitter_x = std::clamp(jitter.x, -0.5f, 0.5f);
   draw_data.jitter_y = std::clamp(jitter.y, -0.5f, 0.5f);
   draw_data.near_plane = camera.GetNearPlane(); // Each camera view has its own
   draw_data.far_plane = kFarPlane;
   draw_data.vert_fov = camera.GetVerticalFov();
   draw_data.time_delta = frame_seconds;
   draw_data.frame_index = cb_luma_global_settings.FrameIndex;
   return draw_data;
}

// SR doesn't restore the pipeline state, the graphics state included (FSR 4 and XeSS unbind the render targets for their depth copy).
// The resolve's UAVs are unbound meanwhile: SR writes the output through its own view.
inline bool SuperResolutionPass::DrawPreservingState(SR::SuperResolutionImpl& sr, SR::InstanceData* instance, ID3D11DeviceContext* context, UINT uav_count, const DrawData& draw_data, ID3D11Resource* history)
{
   DrawStateStack<DrawStateStackType::SimpleGraphics> graphics_state;
   graphics_state.Cache(context, uav_count);
   DrawStateStack<DrawStateStackType::Compute> compute_state;
   compute_state.Cache(context, uav_count);

   ID3D11UnorderedAccessView* const null_uavs[D3D11_1_UAV_SLOT_COUNT] = {};
   context->CSSetUnorderedAccessViews(0, uav_count, null_uavs, nullptr);
   const bool drawn = sr.Draw(instance, context, draw_data);
   if (drawn)
      context->CopyResource(history, draw_data.output_color);

   compute_state.Restore(context);
   graphics_state.Restore(context);
   return drawn;
}
