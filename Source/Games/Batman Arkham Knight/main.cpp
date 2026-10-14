
#define GAME_BATMAN_ARKHAM_KNIGHT 1

#include "../../../Shaders/Batman Arkham Knight/Includes/GameCBuffers.hlsl"
#include "..\..\Core\core.hpp"

// TODO: Fix this globaly? Define NOMINMAX before including windows.h.
#undef min
#undef max

namespace
{
   const ShaderHashesList shader_hashes_SMAAEdgeDetection = {.pixel_shaders = {0x067FF80F}};
   const ShaderHashesList shader_hashes_SMAABlendingWeightCalculation = {.pixel_shaders = {0x2F55F98E}};
   const ShaderHashesList shader_hashes_SMAANeighborhoodBlending = {.compute_shaders = {0x42C0137E}};
   // Writes camera motion for every pixel of the velocity buffer, before the base pass draws object motion on top.
   // Replaced by "CameraVelocity_0x47811EB4.cs_5_0.hlsl", which reprojects with the matrix we provide.
   const ShaderHashesList shader_hashes_CameraVelocity = {.compute_shaders = {0x47811EB4}};
   // Eye adaptation, writes the 1x1 exposure divisor (scene / value is the tonemapper input) the tonemapper reads in the same frame
   const ShaderHashesList shader_hashes_EyeAdaptation = {.compute_shaders = {0x44283FF8}};
   // Post processes that read the render resolution depth/velocity after upscaling, replaced by versions that scale their load coordinates:
   // "Gather_0xB2A276FD.cs_5_0.hlsl" (DOF/bloom downsample, depth) and "RockOnPreamble_0xF98BC308.cs_5_0.hlsl" (motion blur/DOF setup, depth and velocity).

   // Super resolution quality modes (render resolution scale), the engine ScreenPercentage is set from these.
   // Slightly above the nominal scales, as the engine truncates "float(size) * (percentage * 0.01f)".
   constexpr const char* sr_quality_names[] = {"Native (DLAA)", "Quality", "Balanced", "Performance", "Ultra Performance"};
   constexpr float sr_quality_screen_percentages[] = {100.f, 66.667f, 58.001f, 50.001f, 33.334f};
   constexpr int sr_quality_count = int(std::size(sr_quality_names));
   std::atomic<int> g_sr_quality = 0;
   // Added to the recommended super resolution texture mip bias (log2(render / output) - 1), negative is sharper but shimmers more
   std::atomic<float> g_mip_bias_offset = 0.f;

   // FViewInfo layout (Steam build, verified in the view copy constructor, the view init function and against the view cbuffer).
   constexpr size_t view_size_x_offset = 0x6C;                      // float
   constexpr size_t view_size_y_offset = 0x70;                      // float
   constexpr size_t view_projection_offset = 0x110;                 // FMatrix, row vectors (clip = pos * M)
   constexpr size_t view_secondary_projection_offset = 0x150;       // FMatrix, second perspective projection built together with the main one
   constexpr size_t view_translated_view_projection_offset = 0x380; // FMatrix, translated world to clip (the view cbuffer one)
   constexpr size_t view_pre_view_translation_offset = 0x440;       // float3, translated world = world + this (minus the view origin)
   constexpr size_t view_jitter_offset = 0x520;                     // float2, NDC offset ("UnicornJitter"), copied along with the view to the render thread
   constexpr size_t view_rect_min_x_offset = 0x5C;                  // float
   constexpr size_t view_rect_min_y_offset = 0x60;                  // float
   // History invalidation flags (render thread view), all copied from the game thread view or set by the scene renderer before rendering
   constexpr size_t view_flags_offset = 0x800;                  // uint32 bitfield
   constexpr uint32_t view_flag_instant_switch = 0x200;         // LocalPlayer.bInstantlySwitchPostProcessVolume (teleport, player switch, cutscene camera jump)
   constexpr size_t view_camera_cut_offset = 0x80C;             // uint32, PlayerController.bCameraCut (cutscene camera switch)
   constexpr size_t view_prev_transforms_reset_offset = 0x3AE4; // uint32, any of the above, first frame, or a large camera movement

   // Engine globals and functions found by signature (null if not found, upscaling is then unavailable)
   // Our ScreenPercentages, read by the engine instead of the game setting (patched): [0] view scaling, [1] unscaling (HUD canvas),
   // chosen so that unscaling the truncated view size gives back exactly the output size.
   float* g_screen_percentages = nullptr;
   const uint32_t* g_smaa_enabled = nullptr; // The game's anti aliasing setting, super resolution replaces SMAA so it needs to be on
   using InitRenderTargetRectFunc = void(__fastcall*)(uint8_t* view, float min_x, float min_y);
   InitRenderTargetRectFunc g_init_render_target_rect_func = nullptr;
   using PostGatherFunc = void(__fastcall*)(void* post_proxy, void* context, uint8_t* view, int levels);
   PostGatherFunc g_original_post_gather_func = nullptr;
   bool g_upscaling_supported = false;

   // Written on the render thread, read on the game thread
   std::atomic<bool> g_jitter_enabled = false;
   std::atomic<uint32_t> g_render_width = 0;
   std::atomic<uint32_t> g_render_height = 0;
   std::atomic<uint32_t> g_output_width = 0;
   std::atomic<uint32_t> g_output_height = 0;
   std::atomic<uint32_t> g_jitter_phases = SR::GetDefaultJitterPhases();
   // Game thread only
   uint32_t g_jitter_index = 0;
   // Render thread only
   uint32_t g_frame_index = 0;
   float2 g_frame_jitter_pixels = {};
   float2 g_previous_frame_jitter_pixels = {}; // The jitter of the previous super resolution frame
   bool g_camera_velocity_rendered = false;
   bool g_camera_velocity_has_history = false;
   std::atomic<bool> g_use_engine_exposure = true;
   float4 g_reprojection_matrix[4] = {}; // Current jittered NDC + device depth to previous jittered clip space, for the camera velocity pass
   double g_last_translated_view_projection[4][4] = {};
   double g_last_pre_view_translation[3] = {};
   uint32_t g_last_camera_velocity_frame_index = 0;
   bool g_has_last_camera_velocity = false;
   bool g_sr_upscaled_this_frame = false; // Super resolution upscaled the scene this frame, so the post processes need to switch to the output resolution
   float2 g_post_render_scale = {1.f, 1.f};
   float4 g_camera_velocity_view_rect = {}; // Size (xy) and origin (zw) of the main view rect, for the camera velocity pass

   using ViewJitterFunc = void(__fastcall*)(uint8_t* view);
   using CameraVelocityFunc = void(__fastcall*)(void* renderer, uint8_t* view);
   ViewJitterFunc g_original_view_jitter_func = nullptr;
   CameraVelocityFunc g_original_camera_velocity_func = nullptr;

   bool IsRenderResolutionView(const uint8_t* view)
   {
      const float size_x = *reinterpret_cast<const float*>(view + view_size_x_offset);
      const float size_y = *reinterpret_cast<const float*>(view + view_size_y_offset);
      return std::abs(size_x - float(g_render_width.load(std::memory_order_relaxed))) <= 0.5f && std::abs(size_y - float(g_render_height.load(std::memory_order_relaxed))) <= 0.5f;
   }

   // Row vectors: out = a * b
   void Multiply(const double a[4][4], const double b[4][4], double out[4][4])
   {
      for (int r = 0; r < 4; r++)
      {
         for (int c = 0; c < 4; c++)
         {
            out[r][c] = a[r][0] * b[0][c] + a[r][1] * b[1][c] + a[r][2] * b[2][c] + a[r][3] * b[3][c];
         }
      }
   }

   // Gauss-Jordan elimination with partial pivoting
   bool Invert(const double in[4][4], double out[4][4])
   {
      double m[4][8];
      for (int r = 0; r < 4; r++)
      {
         for (int c = 0; c < 4; c++)
         {
            m[r][c] = in[r][c];
            m[r][c + 4] = r == c ? 1.0 : 0.0;
         }
      }
      for (int c = 0; c < 4; c++)
      {
         int pivot = c;
         for (int r = c + 1; r < 4; r++)
         {
            if (std::abs(m[r][c]) > std::abs(m[pivot][c]))
            {
               pivot = r;
            }
         }
         if (m[pivot][c] == 0.0)
         {
            return false;
         }
         if (pivot != c)
         {
            for (int k = 0; k < 8; k++)
            {
               std::swap(m[c][k], m[pivot][k]);
            }
         }
         const double inv_pivot = 1.0 / m[c][c];
         for (int k = 0; k < 8; k++)
         {
            m[c][k] *= inv_pivot;
         }
         for (int r = 0; r < 4; r++)
         {
            if (r != c && m[r][c] != 0.0)
            {
               const double factor = m[r][c];
               for (int k = 0; k < 8; k++)
               {
                  m[r][k] -= factor * m[c][k];
               }
            }
         }
      }
      for (int r = 0; r < 4; r++)
      {
         for (int c = 0; c < 4; c++)
         {
            out[r][c] = m[r][c + 4];
         }
      }
      return true;
   }

   std::vector<System::BytePattern> ParsePattern(std::string_view text)
   {
      std::vector<System::BytePattern> pattern;
      for (size_t i = 0; i < text.size(); i += 3)
      {
         pattern.push_back(text[i] == '?' ? System::ANY : System::BytePattern(unsigned(std::stoul(std::string(text.substr(i, 2)), nullptr, 16))));
      }
      return pattern;
   }

   // Returns the start of the only match of the pattern in the executable, or null if there are none or several
   std::byte* FindUniquePattern(const char* name, std::string_view pattern)
   {
      const std::vector<std::byte*> matches = System::ScanModuleForPattern(ParsePattern(pattern));
      if (matches.size() != 1)
      {
         reshade::log::message(reshade::log::level::warning, (std::string("Batman Arkham Knight Luma: pattern \"") + name + "\" matched " + std::to_string(matches.size()) + " times, patch skipped").c_str());
         return nullptr;
      }
      return matches[0];
   }

   void NopBytes(std::byte* address, size_t size)
   {
      const std::vector<uint8_t> nops(size, 0x90);
      System::PatchMemory(address, nops.data(), size);
   }

   // Redirects the "call rel32" at "call_site" to "target" through an absolute jump stub allocated within reach,
   // returning the original callee (or null on failure, in which case nothing was changed).
   void* RedirectCall(std::byte* call_site, void* target)
   {
      if (call_site[0] != std::byte{0xE8})
      {
         return nullptr;
      }
      int32_t original_rel;
      std::memcpy(&original_rel, call_site + 1, sizeof(original_rel));
      void* original_callee = call_site + 5 + original_rel;

      uint8_t* stub = static_cast<uint8_t*>(System::VirtualAllocNear(call_site, 14));
      if (!stub)
      {
         return nullptr;
      }
      const int64_t stub_rel = reinterpret_cast<int64_t>(stub) - reinterpret_cast<int64_t>(call_site + 5);
      if (stub_rel < INT32_MIN || stub_rel > INT32_MAX)
      {
         VirtualFree(stub, 0, MEM_RELEASE);
         return nullptr;
      }
      // jmp qword ptr [rip+0] followed by the absolute target
      const uint8_t jmp_abs[6] = {0xFF, 0x25, 0x00, 0x00, 0x00, 0x00};
      std::memcpy(stub, jmp_abs, sizeof(jmp_abs));
      std::memcpy(stub + sizeof(jmp_abs), &target, sizeof(target));
      FlushInstructionCache(GetCurrentProcess(), stub, 14);

      const int32_t new_rel = static_cast<int32_t>(stub_rel);
      System::PatchMemory(call_site + 1, &new_rel, sizeof(new_rel));
      return original_callee;
   }

   // Game thread: the engine resets the view jitter (it's only used by its disabled checkerboard upscaler) right before
   // deriving all the view matrices from the projection, so we offset the projection here (only for perspective views that render at our resolution, so not shadows or scene captures).
   // The NDC offset is also stored in the view's own jitter field, which is copied along with the view to the render thread.
   // This runs twice per frame for the main view: once for the game thread view, and once for the render thread view constructed from it (with the already jittered projection).
   void __fastcall HookedViewJitter(uint8_t* view)
   {
      g_original_view_jitter_func(view);

      if (!g_jitter_enabled.load(std::memory_order_relaxed) || !IsRenderResolutionView(view))
      {
         return;
      }
      // M[2][3] is 1 for perspective projections (w = view z), so M[2][0..1] offsets x/y by a constant amount after the perspective divide
      float* projection = reinterpret_cast<float*>(view + view_projection_offset);
      if (projection[11] != 1.0f)
      {
         return;
      }

      float* view_jitter = reinterpret_cast<float*>(view + view_jitter_offset);
      // The game's perspective projections have exactly 0 there, and Halton never gives 0 on both axes, so a non zero offset is our own jitter (from the view this one was constructed from)
      if (projection[8] != 0.0f || projection[9] != 0.0f)
      {
         view_jitter[0] = projection[8];
         view_jitter[1] = projection[9];
         return;
      }

      const float size_x = *reinterpret_cast<const float*>(view + view_size_x_offset);
      const float size_y = *reinterpret_cast<const float*>(view + view_size_y_offset);
      const unsigned int index = g_jitter_index++ % g_jitter_phases.load(std::memory_order_relaxed);
      const float jitter_ndc_x = SR::HaltonSequence(index, 2) * 2.0f / size_x;
      const float jitter_ndc_y = -SR::HaltonSequence(index, 3) * 2.0f / size_y;

      projection[8] = jitter_ndc_x;
      projection[9] = jitter_ndc_y;
      float* secondary_projection = reinterpret_cast<float*>(view + view_secondary_projection_offset);
      if (secondary_projection[11] == 1.0f)
      {
         secondary_projection[8] += jitter_ndc_x;
         secondary_projection[9] += jitter_ndc_y;
      }
      view_jitter[0] = jitter_ndc_x;
      view_jitter[1] = jitter_ndc_y;
   }

   // Render thread, main view: builds the camera velocity reprojection matrix (current jittered NDC + depth -> previous jittered clip space).
   // The engine's previous view projection is exactly last frame's translated view projection, relative to last frame's view origin,
   // so this moves current translated world positions by how much the origin moved since. Built in double precision, as positions are relative to the camera.
   void UpdateReprojectionMatrix(const uint8_t* view, bool camera_cut)
   {
      const float* translated_view_projection_f = reinterpret_cast<const float*>(view + view_translated_view_projection_offset);
      const float* pre_view_translation_f = reinterpret_cast<const float*>(view + view_pre_view_translation_offset);
      double translated_view_projection[4][4];
      for (int i = 0; i < 16; i++)
      {
         translated_view_projection[i / 4][i % 4] = translated_view_projection_f[i];
      }
      const double pre_view_translation[3] = {pre_view_translation_f[0], pre_view_translation_f[1], pre_view_translation_f[2]};

      // On camera cuts there's no previous frame to reproject to, but this frame is still stored as the next frame's previous one
      double inverse_translated_view_projection[4][4];
      g_camera_velocity_has_history = !camera_cut && g_has_last_camera_velocity && g_last_camera_velocity_frame_index + 1 == g_frame_index && Invert(translated_view_projection, inverse_translated_view_projection);
      if (g_camera_velocity_has_history)
      {
         double translation[4][4] = {{1.0, 0.0, 0.0, 0.0}, {0.0, 1.0, 0.0, 0.0}, {0.0, 0.0, 1.0, 0.0}, {0.0, 0.0, 0.0, 1.0}};
         for (int i = 0; i < 3; i++)
         {
            translation[3][i] = g_last_pre_view_translation[i] - pre_view_translation[i];
         }
         double to_previous_translated_world[4][4];
         double reprojection[4][4];
         Multiply(inverse_translated_view_projection, translation, to_previous_translated_world);
         Multiply(to_previous_translated_world, g_last_translated_view_projection, reprojection);
         for (int r = 0; r < 4; r++)
         {
            g_reprojection_matrix[r] = float4{float(reprojection[r][0]), float(reprojection[r][1]), float(reprojection[r][2]), float(reprojection[r][3])};
         }
      }
      else
      {
         // No motion (DLSS history is reset this frame)
         g_reprojection_matrix[0] = float4{1.0f, 0.0f, 0.0f, 0.0f};
         g_reprojection_matrix[1] = float4{0.0f, 1.0f, 0.0f, 0.0f};
         g_reprojection_matrix[2] = float4{0.0f, 0.0f, 1.0f, 0.0f};
         g_reprojection_matrix[3] = float4{0.0f, 0.0f, 0.0f, 1.0f};
      }

      std::memcpy(g_last_translated_view_projection, translated_view_projection, sizeof(g_last_translated_view_projection));
      std::memcpy(g_last_pre_view_translation, pre_view_translation, sizeof(g_last_pre_view_translation));
      g_last_camera_velocity_frame_index = g_frame_index;
      g_has_last_camera_velocity = true;
   }

   // Render thread: runs once per frame for the main view (we force it), before the base pass and SMAA.
   // The matrix needs to be ready before the pass (our replaced shader reads it from the Luma instance data).
   void __fastcall HookedCameraVelocity(void* renderer, uint8_t* view)
   {
      const bool is_main_view = IsRenderResolutionView(view);
      if (is_main_view)
      {
         // The engine resets its own history (previous transforms, eye adaptation, ...) on these
         const bool camera_cut = *reinterpret_cast<const uint32_t*>(view + view_prev_transforms_reset_offset) != 0 || *reinterpret_cast<const uint32_t*>(view + view_camera_cut_offset) != 0 || (*reinterpret_cast<const uint32_t*>(view + view_flags_offset) & view_flag_instant_switch) != 0;
         UpdateReprojectionMatrix(view, camera_cut);
         g_camera_velocity_view_rect = float4{*reinterpret_cast<const float*>(view + view_size_x_offset), *reinterpret_cast<const float*>(view + view_size_y_offset),
            *reinterpret_cast<const float*>(view + view_rect_min_x_offset), *reinterpret_cast<const float*>(view + view_rect_min_y_offset)};
      }

      g_original_camera_velocity_func(renderer, view);

      if (is_main_view)
      {
         const float size_x = *reinterpret_cast<const float*>(view + view_size_x_offset);
         const float size_y = *reinterpret_cast<const float*>(view + view_size_y_offset);
         const float* view_jitter = reinterpret_cast<const float*>(view + view_jitter_offset);
         g_frame_jitter_pixels.x = view_jitter[0] * size_x * 0.5f;
         g_frame_jitter_pixels.y = view_jitter[1] * size_y * -0.5f;
         g_camera_velocity_rendered = true;
      }
   }

   // The engine only computes velocities (camera and objects) when motion blur is enabled, and skips the camera velocity pass when the camera barely moved.
   // These patches make it always compute them, while the motion blur effect itself still follows the user setting.
   void PatchVelocityAndJitter()
   {
      // Motion blur post process proxy creation: don't require the motion blur setting for "requires velocities" (je -> nop)
      if (std::byte* match = FindUniquePattern("MotionBlurRequiresVelocities", "39 3D ?? ?? ?? ?? 74 1E 39 3D ?? ?? ?? ?? 75 16 39 3D ?? ?? ?? ?? 75 0E B8 01 00 00 00"))
      {
         NopBytes(match + 6, 2);
      }
      // Velocity stage (camera velocity pass) motion blur setting check (je rel32 -> nop)
      if (std::byte* match = FindUniquePattern("VelocityStageGate", "83 3D ?? ?? ?? ?? 00 49 8B D9 48 8B F2 0F 84 ?? ?? ?? ?? F7 41 28 00 04 00 00"))
      {
         NopBytes(match + 13, 6);
      }
      // Velocity stage end motion blur setting check (je -> nop)
      if (std::byte* match = FindUniquePattern("VelocityStageEndGate", "83 3D ?? ?? ?? ?? 00 49 8B F1 48 8B EA 48 8B F9 74 ?? F7 41 28 00 04 00 00"))
      {
         NopBytes(match + 16, 2);
      }
      // Base pass object velocity motion blur setting check (je -> nop)
      if (std::byte* match = FindUniquePattern("ObjectVelocityGate", "83 3D ?? ?? ?? ?? 00 74 ?? F7 43 28 00 04 00 00"))
      {
         NopBytes(match + 7, 2);
      }
      // Camera velocity pass: also run it on camera cut frames (where it writes no motion), so the camera of those frames is known by the next one
      // ("bPrevTransformsReset", "bInstantlySwitchPostProcessVolume" and "bCameraCut" checks, jne -> nop)
      if (std::byte* match = FindUniquePattern("CameraVelocityCutGates", "83 BB E4 3A 00 00 00 75 59 F7 83 00 08 00 00 00 02 00 00 75 4D 83 BB 0C 08 00 00 00 75 44 85 C0 75 40"))
      {
         NopBytes(match + 7, 2);
         NopBytes(match + 19, 2);
         NopBytes(match + 28, 2);
      }
      // Camera velocity pass: skip the "camera didn't move enough" early out (jne -> nop) and hook the pass itself
      if (std::byte* match = FindUniquePattern("CameraVelocityPass", "E8 ?? ?? ?? ?? 85 C0 75 ?? 48 8B D3 48 8B CE E8 ?? ?? ?? ?? E9"))
      {
         NopBytes(match + 7, 2);
         g_original_camera_velocity_func = reinterpret_cast<CameraVelocityFunc>(RedirectCall(match + 15, reinterpret_cast<void*>(&HookedCameraVelocity)));
      }
      // View init: hook the view jitter setup, to jitter the projection before all the other view matrices are derived from it
      if (std::byte* match = FindUniquePattern("ViewJitter", "E8 ?? ?? ?? ?? 8B 05 ?? ?? ?? ?? 45 33 E4 89 87 38 05 00 00"))
      {
         g_original_view_jitter_func = reinterpret_cast<ViewJitterFunc>(RedirectCall(match, reinterpret_cast<void*>(&HookedViewJitter)));
      }
   }

   // Render thread: the first post process after the anti aliasing (SMAA or our super resolution), in the Rocksteady post process proxy.
   // When super resolution upscaled the scene, switch the view to the output resolution rect, so that all the later post processes
   // (bloom, tonemapping, DOF, motion blur, ...) derive their constants and dispatch sizes from it.
   // The view is rebuilt from the game thread view every frame, so it doesn't need restoring.
   void __fastcall HookedPostGather(void* post_proxy, void* context, uint8_t* view, int levels)
   {
      if (g_sr_upscaled_this_frame)
      {
         g_sr_upscaled_this_frame = false;
         const float output_width = float(g_output_width.load(std::memory_order_relaxed));
         const float output_height = float(g_output_height.load(std::memory_order_relaxed));
         float* view_size = reinterpret_cast<float*>(view + view_size_x_offset);
         g_post_render_scale = float2{view_size[0] / output_width, view_size[1] / output_height};
         *reinterpret_cast<float*>(view + view_rect_min_x_offset) = 0.f;
         *reinterpret_cast<float*>(view + view_rect_min_y_offset) = 0.f;
         view_size[0] = output_width;
         view_size[1] = output_height;
         // Recomputes the render target rects and screen position scale biases from the view rect
         g_init_render_target_rect_func(view, 0.f, 0.f);
      }
      g_original_post_gather_func(post_proxy, context, view, levels);
   }

   // The engine ScreenPercentage path (render the scene in a smaller view rect of the full size buffers, then upscale) is broken on PC:
   // the scaled view rect is centered, but the raster passes draw it at the top left (while compute passes use the centered one),
   // and the final upscale samples a texture that's still bound as UAV (so it reads black).
   // These patches keep the scaled view rect at the top left and disable the engine upscale (the post processes run at the output resolution after our super resolution instead).
   // The view scaling reads our own screen percentage instead of the game setting, so the game never saves it to its config.
   // Nothing changes at 100%. All the patterns are found first, so either all of them are applied or none.
   void PatchUpscaling()
   {
      std::byte* scale_screen_coords = FindUniquePattern("ScaleScreenCoords", "48 89 5C 24 10 57 F3 0F 10 05 ?? ?? ?? ?? 4D 8B D9 49 8B D8 0F 2E 05 ?? ?? ?? ?? 48 8B FA 0F 84 ?? ?? ?? ?? 83 B9 B8 06 00 00 00");
      std::byte* scale_screen_coords_center = FindUniquePattern("ScaleScreenCoordsCenter", "41 2B 0B D1 E9 03 CA 89 0F 45 2B 08 41 D1 E9 45 03 CA 44 89 0B");
      std::byte* unscale_screen_coords = FindUniquePattern("UnScaleScreenCoords", "83 B9 AC 05 00 00 00 0F 84 ?? ?? ?? ?? 83 B9 B8 06 00 00 00 0F 85 ?? ?? ?? ?? F3 0F 10 05 ?? ?? ?? ?? 0F 2F 81 A8 05 00 00 0F 86 ?? ?? ?? ?? F3 0F 10 05 ?? ?? ?? ??");
      std::byte* unscale_screen_coords_center = FindUniquePattern("UnScaleScreenCoordsCenter", "F3 48 0F 2A C0 F3 0F 5C 44 24 40 F3 0F 59 05 ?? ?? ?? ?? F3 0F 5C C8 0F 57 C0 F3 0F 10 C1 F3 0F 10 4C 24 38 F3 0F 2C C0 66 0F EF C0 89 02 8B 01 F3 48 0F 2A C0 F3 0F 5C 44 24 48 F3 0F 59 05 ?? ?? ?? ?? F3 0F 5C C8");
      std::byte* needs_upscale = FindUniquePattern("NeedsUpscale", "83 B9 AC 05 00 00 00 74 ?? 83 B9 B8 06 00 00 00 75 ?? F3 0F 10 05 ?? ?? ?? ?? 0F 2F 81 A8 05 00 00 76 ?? B8 01 00 00 00 C3 33 C0 C3");
      std::byte* smaa_check = FindUniquePattern("PostProxySMAA", "39 15 ?? ?? ?? ?? 44 8D 62 01 74 ?? 4C 8B C3 48 8B D7 48 8B CD E8 ?? ?? ?? ??");
      std::byte* post_gather_call = FindUniquePattern("PostProxyGather", "41 B9 09 00 00 00 4C 8B C3 48 8B D7 48 8B CD E8 ?? ?? ?? ??");
      std::byte* init_render_target_rect = FindUniquePattern("InitRenderTargetRect", "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 48 83 EC 50 83 3D ?? ?? ?? ?? 00 0F 29 74 24 40 0F 29 7C 24 30 48 8B F9 44 0F 29 4C 24 20 0F 28 FA 0F 28 F1 74 ?? 83 3D ?? ?? ?? ?? 00 74 ?? FF 15 ?? ?? ?? ??");
      if (!scale_screen_coords || !scale_screen_coords_center || !unscale_screen_coords || !unscale_screen_coords_center || !needs_upscale || !smaa_check || !post_gather_call || !init_render_target_rect)
      {
         return;
      }

      // Our screen percentages, within rip relative reach of the two functions that read them
      float* screen_percentages = static_cast<float*>(System::VirtualAllocNear(scale_screen_coords, sizeof(float) * 2, PAGE_READWRITE));
      if (!screen_percentages)
      {
         return;
      }
      screen_percentages[0] = 100.f;
      screen_percentages[1] = 100.f;
      // Returns the rip relative displacement from "next_instruction" to "target", if reachable
      auto GetDisplacement = [](std::byte* next_instruction, const float* target, int32_t& displacement)
      {
         const int64_t offset = reinterpret_cast<int64_t>(target) - reinterpret_cast<int64_t>(next_instruction);
         displacement = static_cast<int32_t>(offset);
         return offset >= INT32_MIN && offset <= INT32_MAX;
      };
      int32_t scale_displacement, unscale_compare_displacement, unscale_load_displacement;
      if (!GetDisplacement(scale_screen_coords + 0x0E, &screen_percentages[0], scale_displacement) || !GetDisplacement(unscale_screen_coords + 0x29, &screen_percentages[1], unscale_compare_displacement) || !GetDisplacement(unscale_screen_coords + 0x37, &screen_percentages[1], unscale_load_displacement))
      {
         VirtualFree(screen_percentages, 0, MEM_RELEASE);
         return;
      }

      g_original_post_gather_func = reinterpret_cast<PostGatherFunc>(RedirectCall(post_gather_call + 0x0F, reinterpret_cast<void*>(&HookedPostGather)));
      if (!g_original_post_gather_func)
      {
         VirtualFree(screen_percentages, 0, MEM_RELEASE);
         return;
      }
      g_screen_percentages = screen_percentages;
      // "cmp [rip+disp32], edx" at +0x00 (disp32 at +0x02, next instruction at +0x06)
      int32_t smaa_displacement;
      std::memcpy(&smaa_displacement, smaa_check + 0x02, sizeof(smaa_displacement));
      g_smaa_enabled = reinterpret_cast<const uint32_t*>(smaa_check + 0x06 + smaa_displacement);
      g_init_render_target_rect_func = reinterpret_cast<InitRenderTargetRectFunc>(init_render_target_rect);

      // ScaleScreenCoords (view rect): "movss xmm0, [rip+disp32]" at +0x06 reads our screen percentage
      System::PatchMemory(scale_screen_coords + 0x0A, &scale_displacement, sizeof(scale_displacement));
      // ScaleScreenCoords: don't center the scaled view rect ("mov [rdi], ecx" and "mov [rbx], r9d" -> nop)
      NopBytes(scale_screen_coords_center + 0x07, 2);
      NopBytes(scale_screen_coords_center + 0x12, 3);
      // UnScaleScreenCoords (HUD canvas rect): ignore the UpscaleScreenPercentage setting (je -> nop), compare and load our screen percentage
      // ("comiss xmm0, [rcx+0x5A8]" -> "comiss xmm0, [rip+disp32]", same length, and "movss xmm0, [rip+disp32]" at +0x2F)
      NopBytes(unscale_screen_coords + 0x07, 6);
      uint8_t compare_screen_percentage[7] = {0x0F, 0x2F, 0x05};
      std::memcpy(compare_screen_percentage + 3, &unscale_compare_displacement, sizeof(unscale_compare_displacement));
      System::PatchMemory(unscale_screen_coords + 0x22, compare_screen_percentage, sizeof(compare_screen_percentage));
      System::PatchMemory(unscale_screen_coords + 0x33, &unscale_load_displacement, sizeof(unscale_load_displacement));
      // UnScaleScreenCoords: don't un-center it ("subss xmm1, xmm0" -> nop)
      NopBytes(unscale_screen_coords_center + 0x13, 4);
      NopBytes(unscale_screen_coords_center + 0x43, 4);
      // NeedsUpscale (game setting based): always false ("xor eax, eax; ret"), so the post processes write the final image like at 100%
      const uint8_t return_false[] = {0x33, 0xC0, 0xC3};
      System::PatchMemory(needs_upscale, return_false, sizeof(return_false));

      g_upscaling_supported = true;
   }
} // namespace

struct GameDeviceDataBatmanArkhamKnight final : GameDeviceData
{
   ComPtr<ID3D11Resource> resource_depth;
   ComPtr<ID3D11Resource> resource_mvs;
   // The engine eye adaptation output (1x1 exposure divisor), and our exposure multiplier for super resolution (1 / divisor)
   ComPtr<ID3D11Resource> resource_engine_exposure;
   ID3D11Resource* engine_exposure_srv_resource = nullptr; // The resource "srv_engine_exposure" was created for
   ComPtr<ID3D11ShaderResourceView> srv_engine_exposure;
   ComPtr<ID3D11Texture2D> texture_sr_exposure;
   ComPtr<ID3D11UnorderedAccessView> uav_sr_exposure;
   // Unjittered copy of the game motion vectors for super resolution (same size as the game ones)
   ID3D11Resource* game_mvs_srv_resource = nullptr; // The resource "srv_game_mvs" was created for
   ComPtr<ID3D11ShaderResourceView> srv_game_mvs;
   ComPtr<ID3D11Texture2D> texture_sr_mvs;
   ComPtr<ID3D11UnorderedAccessView> uav_sr_mvs;
   ComPtr<ID3D11Buffer> cb_unjitter_mvs;
};

class BatmanArkhamKnight final : public Game
{
public:
   static GameDeviceDataBatmanArkhamKnight& GetGameDeviceData(DeviceData& device_data)
   {
      return *(GameDeviceDataBatmanArkhamKnight*)device_data.game;
   }

   void OnLoad(std::filesystem::path& file_path, bool failed) override
   {
      if (!failed)
      {
         reshade::register_event<reshade::addon_event::clear_render_target_view>(BatmanArkhamKnight::OnClearRenderTargetView);
      }
   }

   void OnInit(bool async) override
   {
      PatchVelocityAndJitter();
      PatchUpscaling();

      std::vector<ShaderDefineData> game_shader_defines_data = {
         {"DISABLE_LENS_FLARE_AND_LENS_DIRT", '0', true, false, "Disables lens flare and lens dirt effects.", 1}};

      shader_defines_data.append_range(game_shader_defines_data);

      luma_settings_cbuffer_index = 13;
      luma_data_cbuffer_index = 12;

      native_shaders_definitions.emplace(CompileTimeStringHash("BAK SR Exposure"), ShaderDefinition("Luma_BAK_SRExposure", reshade::api::pipeline_subobject_type::compute_shader));
      native_shaders_definitions.emplace(CompileTimeStringHash("BAK Unjitter MVs"), ShaderDefinition("Luma_BAK_UnjitterMVs", reshade::api::pipeline_subobject_type::compute_shader));
   }

   void OnCreateDevice(ID3D11Device* native_device, DeviceData& device_data) override
   {
      device_data.game = new GameDeviceDataBatmanArkhamKnight;
   }

   void OnDestroyDeviceData(DeviceData& device_data) override
   {
      delete static_cast<GameDeviceDataBatmanArkhamKnight*>(device_data.game);
      device_data.game = nullptr;
   }

   static bool OnClearRenderTargetView(reshade::api::command_list* cmd_list, reshade::api::resource_view rtv, const float color[4], uint32_t rect_count, const reshade::api::rect* rects)
   {
      // Get RTV resource and try to query it to texture2d.
      auto native_rtv = (ID3D11RenderTargetView*)rtv.handle;
      ComPtr<ID3D11Resource> resource;
      native_rtv->GetResource(resource.put());
      ComPtr<ID3D11Texture2D> tex;
      auto hr = resource->QueryInterface(tex.put());

      if (SUCCEEDED(hr))
      {
         D3D11_TEXTURE2D_DESC tex_desc;
         tex->GetDesc(&tex_desc);

         // Motion vectors should be only ones with DXGI_FORMAT_R16G16_FLOAT format,
         // at least here.
         if (tex_desc.Format == DXGI_FORMAT_R16G16_FLOAT)
         {
            auto& device_data = *cmd_list->get_device()->get_private_data<DeviceData>();
            auto& game_device_data = GetGameDeviceData(device_data);

            game_device_data.resource_mvs = resource;
         }
      }

      return false;
   }

   DrawOrDispatchOverrideType OnDrawOrDispatch(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, reshade::api::shader_stage stages, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, bool is_custom_pass, bool& updated_cbuffers, std::function<void()>* original_draw_dispatch_func) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);

      if (original_shader_hashes.Contains(shader_hashes_CameraVelocity))
      {
         // UAV0 is the velocity buffer (the base pass then draws moving objects on top of it).
         // It's not necessarily cleared anymore when this pass runs, so grab it here too.
         ComPtr<ID3D11UnorderedAccessView> uav_mvs;
         native_device_context->CSGetUnorderedAccessViews(0, 1, uav_mvs.put());
         if (uav_mvs.get())
         {
            uav_mvs->GetResource(game_device_data.resource_mvs.put());
         }
         return DrawOrDispatchOverrideType::None;
      }

      if (original_shader_hashes.Contains(shader_hashes_EyeAdaptation))
      {
         // UAV0 is the new exposure, the next frame's super resolution uses it (it ping-pongs between two textures)
         ComPtr<ID3D11UnorderedAccessView> uav_exposure;
         native_device_context->CSGetUnorderedAccessViews(0, 1, uav_exposure.put());
         if (uav_exposure.get())
         {
            game_device_data.resource_engine_exposure.reset();
            uav_exposure->GetResource(game_device_data.resource_engine_exposure.put());
         }
         return DrawOrDispatchOverrideType::None;
      }

      if (original_shader_hashes.Contains(shader_hashes_SMAAEdgeDetection))
      {
         if (device_data.sr_type != SR::Type::None)
         {
            // SRV1 should be the depth.
            ComPtr<ID3D11ShaderResourceView> srv_depth;
            native_device_context->PSGetShaderResources(1, 1, srv_depth.put());
            srv_depth->GetResource(game_device_data.resource_depth.put());

            return DrawOrDispatchOverrideType::Skip;
         }

         return DrawOrDispatchOverrideType::None;
      }

      if (original_shader_hashes.Contains(shader_hashes_SMAABlendingWeightCalculation))
      {
         if (device_data.sr_type != SR::Type::None)
         {
            return DrawOrDispatchOverrideType::Skip;
         }

         return DrawOrDispatchOverrideType::None;
      }

      if (original_shader_hashes.Contains(shader_hashes_SMAANeighborhoodBlending))
      {
         if (device_data.sr_type != SR::Type::None)
         {
            // DLSS requires an immediate context for execution!
            ASSERT_ONCE(native_device_context->GetType() == D3D11_DEVICE_CONTEXT_IMMEDIATE);

            auto* sr_instance_data = device_data.GetSRInstanceData();
            ASSERT_ONCE(sr_instance_data);

            // UAV0 should be the anti aliased out.
            ComPtr<ID3D11UnorderedAccessView> uav_out;
            native_device_context->CSGetUnorderedAccessViews(0, 1, uav_out.put());
            ComPtr<ID3D11Resource> resource_out;
            uav_out->GetResource(resource_out.put());

            // SRV0 should be the scene in linear color space.
            ComPtr<ID3D11ShaderResourceView> srv_scene;
            native_device_context->CSGetShaderResources(0, 1, srv_scene.put());
            ComPtr<ID3D11Resource> resource_scene;
            srv_scene->GetResource(resource_scene.put());

            // We don't need to set/call this every frame?
            SR::SettingsData settings_data;
            settings_data.output_width = device_data.output_resolution.x;
            settings_data.output_height = device_data.output_resolution.y;
            settings_data.render_width = device_data.render_resolution.x;
            settings_data.render_height = device_data.render_resolution.y;
            settings_data.dynamic_resolution = false;
            settings_data.hdr = true;
            // Super resolution gets unjittered motion vectors (the game ones are jittered, see "UnjitterMVs()"), if that fails it gets the game ones, flagged as jittered.
            ID3D11Resource* sr_mvs = UnjitterMVs(native_device, native_device_context, device_data);
            settings_data.mvs_jittered = sr_mvs == nullptr;

            // MVs are "previous UV - current UV" (camera velocity matches thanks to the replaced shader), DLSS wants pixels.
            settings_data.mvs_x_scale = device_data.render_resolution.x;
            settings_data.mvs_y_scale = device_data.render_resolution.y;

            settings_data.render_preset = dlss_render_preset;
            // The engine exposure is honored by DLSS presets J and K (others always auto expose)
            ID3D11Resource* sr_exposure = g_use_engine_exposure.load(std::memory_order_relaxed) ? UpdateSRExposure(native_device, native_device_context, device_data) : nullptr;
            settings_data.auto_exposure = sr_exposure == nullptr;

            sr_implementations[device_data.sr_type]->UpdateSettings(sr_instance_data, native_device_context, settings_data);

            SR::SuperResolutionImpl::DrawData draw_data;
            draw_data.source_color = resource_scene.get();
            draw_data.output_color = resource_out.get();
            draw_data.motion_vectors = sr_mvs ? sr_mvs : game_device_data.resource_mvs.get();
            draw_data.depth_buffer = game_device_data.resource_depth.get();
            draw_data.exposure = sr_exposure;

            // Jitter the frame was rendered with (pixels), read back from its view on the render thread.
            draw_data.jitter_x = g_frame_jitter_pixels.x;
            draw_data.jitter_y = g_frame_jitter_pixels.y;
            // The camera velocity pass hook isn't called when there's no main view this frame (loading, videos),
            // and the velocities need last frame's camera to be valid (not on camera cuts).
            draw_data.reset = !g_camera_velocity_rendered || !g_camera_velocity_has_history;

            draw_data.render_width = device_data.render_resolution.x;
            draw_data.render_height = device_data.render_resolution.y;

            sr_implementations[device_data.sr_type]->Draw(sr_instance_data, native_device_context, draw_data);
            g_previous_frame_jitter_pixels = g_frame_jitter_pixels;
            // The post processes need to switch to the output resolution (done in the hook of the first one)
            g_sr_upscaled_this_frame = device_data.render_resolution.x < device_data.output_resolution.x || device_data.render_resolution.y < device_data.output_resolution.y;

            return DrawOrDispatchOverrideType::Replaced;
         }

         return DrawOrDispatchOverrideType::None;
      }

      return DrawOrDispatchOverrideType::None;
   }

   // Writes the game motion vectors without jitter into our texture and returns it, or null if unavailable.
   // All the game velocities are computed from the jittered current and previous projections, so they include "previous jitter - current jitter" (measured in game:
   // static geometry has exactly that), which is the same for every pixel. Removing it ourselves doesn't depend on how DLSS/FSR interpret jittered motion vectors.
   static ID3D11Resource* UnjitterMVs(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, DeviceData& device_data)
   {
      auto& game_device_data = GetGameDeviceData(device_data);
      ID3D11Resource* game_mvs = game_device_data.resource_mvs.get();
      const auto unjitter_shader = device_data.native_compute_shaders.find(CompileTimeStringHash("BAK Unjitter MVs"));
      if (!game_mvs || unjitter_shader == device_data.native_compute_shaders.end() || !unjitter_shader->second.get())
      {
         return nullptr;
      }
      ComPtr<ID3D11Texture2D> game_mvs_texture;
      if (FAILED(game_mvs->QueryInterface(game_mvs_texture.put())))
      {
         return nullptr;
      }
      D3D11_TEXTURE2D_DESC game_mvs_desc;
      game_mvs_texture->GetDesc(&game_mvs_desc);

      if (game_mvs != game_device_data.game_mvs_srv_resource)
      {
         game_device_data.srv_game_mvs.reset();
         D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
         srv_desc.Format = DXGI_FORMAT_R16G16_FLOAT;
         srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
         srv_desc.Texture2D.MipLevels = 1;
         native_device->CreateShaderResourceView(game_mvs, &srv_desc, game_device_data.srv_game_mvs.put());
         game_device_data.game_mvs_srv_resource = game_mvs;
      }
      D3D11_TEXTURE2D_DESC sr_mvs_desc = {};
      if (game_device_data.texture_sr_mvs.get())
      {
         game_device_data.texture_sr_mvs->GetDesc(&sr_mvs_desc);
      }
      if (sr_mvs_desc.Width != game_mvs_desc.Width || sr_mvs_desc.Height != game_mvs_desc.Height)
      {
         game_device_data.texture_sr_mvs.reset();
         game_device_data.uav_sr_mvs.reset();
         D3D11_TEXTURE2D_DESC desc = {};
         desc.Width = game_mvs_desc.Width;
         desc.Height = game_mvs_desc.Height;
         desc.MipLevels = 1;
         desc.ArraySize = 1;
         desc.Format = DXGI_FORMAT_R16G16_FLOAT;
         desc.SampleDesc.Count = 1;
         desc.Usage = D3D11_USAGE_DEFAULT;
         desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
         if (FAILED(native_device->CreateTexture2D(&desc, nullptr, game_device_data.texture_sr_mvs.put())) || FAILED(native_device->CreateUnorderedAccessView(game_device_data.texture_sr_mvs.get(), nullptr, game_device_data.uav_sr_mvs.put())))
         {
            game_device_data.texture_sr_mvs.reset();
            game_device_data.uav_sr_mvs.reset();
         }
      }
      if (!game_device_data.cb_unjitter_mvs.get())
      {
         D3D11_BUFFER_DESC desc = {};
         desc.ByteWidth = sizeof(float4);
         desc.Usage = D3D11_USAGE_DYNAMIC;
         desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
         desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
         native_device->CreateBuffer(&desc, nullptr, game_device_data.cb_unjitter_mvs.put());
      }
      if (!game_device_data.srv_game_mvs.get() || !game_device_data.uav_sr_mvs.get() || !game_device_data.cb_unjitter_mvs.get())
      {
         return nullptr;
      }

      // Velocities are in UV, and the jitters are image offsets in render pixels (x right, y down), like the velocities
      const float render_width = device_data.render_resolution.x;
      const float render_height = device_data.render_resolution.y;
      D3D11_MAPPED_SUBRESOURCE mapped;
      if (FAILED(native_device_context->Map(game_device_data.cb_unjitter_mvs.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
      {
         return nullptr;
      }
      const float4 params = {(g_previous_frame_jitter_pixels.x - g_frame_jitter_pixels.x) / render_width, (g_previous_frame_jitter_pixels.y - g_frame_jitter_pixels.y) / render_height, render_width, render_height};
      std::memcpy(mapped.pData, &params, sizeof(params));
      native_device_context->Unmap(game_device_data.cb_unjitter_mvs.get(), 0);

      DrawStateStack<DrawStateStackType::Compute> compute_state_stack;
      compute_state_stack.Cache(native_device_context, device_data.uav_max_count);
      ID3D11Buffer* cb = game_device_data.cb_unjitter_mvs.get();
      ID3D11ShaderResourceView* srv = game_device_data.srv_game_mvs.get();
      ID3D11UnorderedAccessView* uav = game_device_data.uav_sr_mvs.get();
      native_device_context->CSSetShader(unjitter_shader->second.get(), nullptr, 0);
      native_device_context->CSSetConstantBuffers(0, 1, &cb);
      native_device_context->CSSetShaderResources(0, 1, &srv);
      native_device_context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
      native_device_context->Dispatch((UINT(render_width) + 7) / 8, (UINT(render_height) + 7) / 8, 1);
      compute_state_stack.Restore(native_device_context);
      return game_device_data.texture_sr_mvs.get();
   }

   // Writes the super resolution exposure multiplier (1 / the engine exposure divisor) into our 1x1 texture and returns it, or null if unavailable.
   // At the super resolution point, the latest engine exposure is the one from the previous frame (it adapts slowly, so that's fine).
   // Before the engine wrote any, ours stays at 1 (what the engine writes on resets too).
   static ID3D11Resource* UpdateSRExposure(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, DeviceData& device_data)
   {
      auto& game_device_data = GetGameDeviceData(device_data);
      if (!game_device_data.texture_sr_exposure.get())
      {
         D3D11_TEXTURE2D_DESC desc = {};
         desc.Width = 1;
         desc.Height = 1;
         desc.MipLevels = 1;
         desc.ArraySize = 1;
         desc.Format = DXGI_FORMAT_R32_FLOAT;
         desc.SampleDesc.Count = 1;
         desc.Usage = D3D11_USAGE_DEFAULT;
         desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
         const float one = 1.f;
         const D3D11_SUBRESOURCE_DATA initial_data = {&one, sizeof(one), sizeof(one)};
         if (FAILED(native_device->CreateTexture2D(&desc, &initial_data, game_device_data.texture_sr_exposure.put())) || FAILED(native_device->CreateUnorderedAccessView(game_device_data.texture_sr_exposure.get(), nullptr, game_device_data.uav_sr_exposure.put())))
         {
            game_device_data.texture_sr_exposure.reset();
            game_device_data.uav_sr_exposure.reset();
            return nullptr;
         }
      }

      ID3D11Resource* engine_exposure = game_device_data.resource_engine_exposure.get();
      if (engine_exposure && engine_exposure != game_device_data.engine_exposure_srv_resource)
      {
         game_device_data.srv_engine_exposure.reset();
         D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
         srv_desc.Format = DXGI_FORMAT_R16_FLOAT;
         srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
         srv_desc.Texture2D.MipLevels = 1;
         native_device->CreateShaderResourceView(engine_exposure, &srv_desc, game_device_data.srv_engine_exposure.put());
         game_device_data.engine_exposure_srv_resource = engine_exposure;
      }
      const auto exposure_shader = device_data.native_compute_shaders.find(CompileTimeStringHash("BAK SR Exposure"));
      if (engine_exposure && game_device_data.srv_engine_exposure.get() && exposure_shader != device_data.native_compute_shaders.end() && exposure_shader->second.get())
      {
         DrawStateStack<DrawStateStackType::Compute> compute_state_stack;
         compute_state_stack.Cache(native_device_context, device_data.uav_max_count);
         ID3D11ShaderResourceView* srv = game_device_data.srv_engine_exposure.get();
         ID3D11UnorderedAccessView* uav = game_device_data.uav_sr_exposure.get();
         native_device_context->CSSetShader(exposure_shader->second.get(), nullptr, 0);
         native_device_context->CSSetShaderResources(0, 1, &srv);
         native_device_context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
         native_device_context->Dispatch(1, 1, 1);
         compute_state_stack.Restore(native_device_context);
      }
      return game_device_data.texture_sr_exposure.get();
   }

   // Same math as the engine view scaling (ScaleScreenCoords) and unscaling (UnScaleScreenCoords)
   static uint32_t ScaleSize(uint32_t size, float screen_percentage)
   {
      const float scale = std::clamp(screen_percentage * 0.01f, 0.f, 1.f);
      return uint32_t(std::max(1, int(float(size) * scale)));
   }
   static uint32_t UnscaleSize(uint32_t size, float screen_percentage)
   {
      const float scale = std::clamp(screen_percentage * 0.01f, 0.f, 1.f);
      return uint32_t(int(float(size) * (1.f / scale)));
   }

   // Finds the view scaling percentage closest above "nominal_screen_percentage", and the matching unscaling percentage (HUD canvas),
   // so that unscaling the truncated render size gives back exactly the output size on both axes (the engine truncates both ways)
   static void FindScreenPercentages(uint32_t output_width, uint32_t output_height, float nominal_screen_percentage, float& screen_percentage, float& unscale_screen_percentage)
   {
      screen_percentage = nominal_screen_percentage;
      unscale_screen_percentage = nominal_screen_percentage;
      if (nominal_screen_percentage >= 100.f)
      {
         return;
      }
      for (int step = 0; step < 200; step++)
      {
         const float candidate = nominal_screen_percentage + float(step) * 0.005f;
         const uint32_t render_width = ScaleSize(output_width, candidate);
         const uint32_t render_height = ScaleSize(output_height, candidate);
         // Valid unscaling scales are in (render / (output + 1), render / output] on each axis
         const double min_scale = std::max(double(render_width) / double(output_width + 1), double(render_height) / double(output_height + 1));
         const double max_scale = std::min(double(render_width) / double(output_width), double(render_height) / double(output_height));
         if (min_scale >= max_scale)
         {
            continue;
         }
         const float unscale_candidate = float((min_scale + max_scale) * 50.0);
         if (UnscaleSize(render_width, unscale_candidate) == output_width && UnscaleSize(render_height, unscale_candidate) == output_height)
         {
            screen_percentage = candidate;
            unscale_screen_percentage = unscale_candidate;
            return;
         }
      }
   }

   void OnPresent(ID3D11Device* native_device, DeviceData& device_data) override
   {
      const bool sr_active = device_data.sr_type != SR::Type::None && !device_data.sr_suppressed;
      g_jitter_enabled.store(sr_active, std::memory_order_relaxed);

      const uint32_t output_width = uint32_t(device_data.output_resolution.x + 0.5f);
      const uint32_t output_height = uint32_t(device_data.output_resolution.y + 0.5f);
      // Super resolution replaces SMAA, so it only runs (and can upscale) when the game anti aliasing is on
      float screen_percentage = 100.f;
      float unscale_screen_percentage = 100.f;
      if (g_upscaling_supported && sr_active && *g_smaa_enabled != 0)
      {
         const int sr_quality = std::clamp(g_sr_quality.load(std::memory_order_relaxed), 0, sr_quality_count - 1);
         static uint32_t cached_output_width = 0, cached_output_height = 0;
         static int cached_sr_quality = -1;
         static float cached_screen_percentage = 100.f, cached_unscale_screen_percentage = 100.f;
         if (cached_output_width != output_width || cached_output_height != output_height || cached_sr_quality != sr_quality)
         {
            FindScreenPercentages(output_width, output_height, sr_quality_screen_percentages[sr_quality], cached_screen_percentage, cached_unscale_screen_percentage);
            cached_output_width = output_width;
            cached_output_height = output_height;
            cached_sr_quality = sr_quality;
         }
         screen_percentage = cached_screen_percentage;
         unscale_screen_percentage = cached_unscale_screen_percentage;
      }
      const uint32_t render_width = ScaleSize(output_width, screen_percentage);
      const uint32_t render_height = ScaleSize(output_height, screen_percentage);
      if (g_upscaling_supported)
      {
         g_screen_percentages[0] = screen_percentage;
         g_screen_percentages[1] = unscale_screen_percentage;
      }
      // The engine applies the new percentage from the next view it builds, so this can be off for a frame or two after a change
      device_data.render_resolution = float2{float(render_width), float(render_height)};
      g_render_width.store(render_width, std::memory_order_relaxed);
      g_render_height.store(render_height, std::memory_order_relaxed);
      g_output_width.store(output_width, std::memory_order_relaxed);
      g_output_height.store(output_height, std::memory_order_relaxed);
      // More jitter phases the more we upscale, to cover every output pixel (as suggested by DLSS and FSR)
      const float upscale_ratio = float(output_width) / float(render_width);
      g_jitter_phases.store(std::max(uint32_t(SR::GetDefaultJitterPhases()), uint32_t(std::ceil(8.f * upscale_ratio * upscale_ratio))), std::memory_order_relaxed);

      g_camera_velocity_rendered = false;
      g_sr_upscaled_this_frame = false;
      g_post_render_scale = float2{1.f, 1.f};
      g_frame_index++;

      if (!custom_texture_mip_lod_bias_offset)
      {
         std::shared_lock shared_lock_samplers(s_mutex_samplers);
         if (sr_active)
         {
            device_data.texture_mip_lod_bias_offset = SR::GetMipLODBias(device_data.render_resolution.y, device_data.output_resolution.y) + g_mip_bias_offset.load(std::memory_order_relaxed); // This results in -1 at output res (without the user offset)
         }
         else
         {
            device_data.texture_mip_lod_bias_offset = 0.0f;
         }
      }
   }

   void UpdateLumaInstanceDataCB(CB::LumaInstanceDataPadded& data, CommandListData& cmd_list_data, DeviceData& device_data) override
   {
      std::memcpy(data.GameData.ReprojectionMatrix, g_reprojection_matrix, sizeof(g_reprojection_matrix));
      data.GameData.PostRenderScale = float4{g_post_render_scale.x, g_post_render_scale.y, 0.f, 0.f};
      data.GameData.CameraVelocityViewRect = g_camera_velocity_view_rect;
   }

   void LoadConfigs() override
   {
      reshade::api::effect_runtime* runtime = nullptr;
      int sr_quality = 0;
      reshade::get_config_value(runtime, NAME, "SRQuality", sr_quality);
      g_sr_quality.store(std::clamp(sr_quality, 0, sr_quality_count - 1), std::memory_order_relaxed);
      float mip_bias_offset = 0.f;
      reshade::get_config_value(runtime, NAME, "MipBiasOffset", mip_bias_offset);
      g_mip_bias_offset.store(std::clamp(mip_bias_offset, -1.f, 1.f), std::memory_order_relaxed);
      bool use_engine_exposure = true;
      reshade::get_config_value(runtime, NAME, "UseEngineExposure", use_engine_exposure);
      g_use_engine_exposure.store(use_engine_exposure, std::memory_order_relaxed);
   }

   void DrawImGuiSettings(DeviceData& device_data) override
   {
      reshade::api::effect_runtime* runtime = nullptr;
      ImGui::BeginDisabled(!g_upscaling_supported);
      int sr_quality = g_sr_quality.load(std::memory_order_relaxed);
      if (ImGui::Combo("Super Resolution Quality", &sr_quality, sr_quality_names, sr_quality_count))
      {
         g_sr_quality.store(sr_quality, std::memory_order_relaxed);
         reshade::set_config_value(runtime, NAME, "SRQuality", sr_quality);
      }
      ImGui::EndDisabled();
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      {
         ImGui::SetTooltip(g_upscaling_supported ? "Render resolution of DLSS/FSR (it replaces the game anti aliasing, so that needs to be on)." : "Not supported by this version of the game.");
      }
      float mip_bias_offset = g_mip_bias_offset.load(std::memory_order_relaxed);
      if (ImGui::SliderFloat("Texture Mip Bias Offset", &mip_bias_offset, -1.f, 1.f, "%.2f"))
      {
         // Quantized, as every value creates new samplers
         mip_bias_offset = std::round(mip_bias_offset * 20.f) / 20.f;
         g_mip_bias_offset.store(mip_bias_offset, std::memory_order_relaxed);
         reshade::set_config_value(runtime, NAME, "MipBiasOffset", mip_bias_offset);
      }
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      {
         ImGui::SetTooltip("Added to the texture mip bias recommended for DLSS/FSR (log2(render / output resolution) - 1).\nHigher reduces texture shimmering in motion, lower is sharper. 0 is the recommended value.");
      }
      bool use_engine_exposure = g_use_engine_exposure.load(std::memory_order_relaxed);
      if (ImGui::Checkbox("Use Game Exposure for Super Resolution", &use_engine_exposure))
      {
         g_use_engine_exposure.store(use_engine_exposure, std::memory_order_relaxed);
         reshade::set_config_value(runtime, NAME, "UseEngineExposure", use_engine_exposure);
      }
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      {
         ImGui::SetTooltip("Gives DLSS/FSR the game's eye adaptation exposure, instead of letting them estimate one.\nOnly DLSS presets J and K (and FSR) use it, other DLSS presets always estimate it.");
      }
   }

   void PrintImGuiAbout() override
   {
      ImGui::Text("Batman Arkham Knight Luma mod - about and credits section", "");
   }
};

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
   if (ul_reason_for_call == DLL_PROCESS_ATTACH)
   {
      Globals::SetGlobals(PROJECT_NAME, "Batman Arkham Knight Luma mod");
      Globals::VERSION = 1;

      swapchain_format_upgrade_type = TextureFormatUpgradesType::AllowedEnabled;
      swapchain_upgrade_type = SwapchainUpgradeType::scRGB;
      texture_format_upgrades_type = TextureFormatUpgradesType::AllowedEnabled;
      // ### Check which of these are needed and remove the rest ###
      texture_upgrade_formats = {
         reshade::api::format::r8g8b8a8_unorm,
         reshade::api::format::r8g8b8a8_unorm_srgb,
         reshade::api::format::r8g8b8a8_typeless,
         reshade::api::format::r8g8b8x8_unorm,
         reshade::api::format::r8g8b8x8_unorm_srgb,
         reshade::api::format::b8g8r8a8_unorm,
         reshade::api::format::b8g8r8a8_unorm_srgb,
         reshade::api::format::b8g8r8a8_typeless,
         reshade::api::format::b8g8r8x8_unorm,
         reshade::api::format::b8g8r8x8_unorm_srgb,
         reshade::api::format::b8g8r8x8_typeless,

         reshade::api::format::r11g11b10_float,
      };
      // ### Check these if textures are not upgraded ###
      texture_format_upgrades_2d_size_filters = 0 | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainResolution | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainAspectRatio;

      enable_samplers_upgrade = true;

#if DEVELOPMENT
      forced_shader_names.emplace(0x978BFB09, "Tonemap");
      forced_shader_names.emplace(0xFE8D7315, "Lightning");
      forced_shader_names.emplace(0x816CB5D7, "Lightning");
      forced_shader_names.emplace(0x067FF80F, "SMAAEdgeDetection");
      forced_shader_names.emplace(0x2F55F98E, "SMAABlendingWeightCalculation");
      forced_shader_names.emplace(0x42C0137E, "SMAANeighborhoodBlending");
#endif

      // TODO: Remove this later!
      Globals::DEVELOPMENT_STATE = Globals::ModDevelopmentState::WorkInProgress;

      game = new BatmanArkhamKnight();
   }

   CoreMain(hModule, ul_reason_for_call, lpReserved);

   return TRUE;
}