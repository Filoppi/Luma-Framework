// Metal Gear Solid V: The Phantom Pain — Luma mod (Fox Engine, D3D11).
// Replaces the game's FXAA with native resolution super resolution (FSR 3, FSR 4 through DX12 interop, DLSS).
// The game has no TAA, so Luma injects the jitter (into the game's camera constants as they're uploaded) and builds full precision
// motion vectors (a compute pass next to the game's own 8 bit ones). The game records its post processing on deferred contexts, so SR
// splits the post processing command list right before the game's bloom and runs on the immediate context between its two halves (like
// Persona 5 Royal at FXAA): bloom, lens flare, DoF, motion blur, the LUT and the tonemap run on its output, and FXAA is skipped.
// The game quits at startup under ReShade's D3D11 wrapper unless its device check is answered for it ("InstallDeviceCheckFix()").
// Development builds can also skip FXAA, jitter without SR and flip the SR inputs (MCP "luma_dev_values" or the dev settings).

#define GAME_METAL_GEAR_SOLID_V 1

// No message boxes: the "attach the debugger" prompt halts the game while it loads the mod, and asserts go to the overlay
#define DISABLE_AUTO_DEBUGGER 1
#define AVOID_INPUT_LOSS 1
// The development focus loss handling is unfinished and has broken the input of other engines
#define DISABLE_FOCUS_LOSS_SUPPRESSION 1

#define GEOMETRY_SHADER_SUPPORT 0

// Luma's FSR 4 runs on its own D3D12 device, which ReShade also wraps: ignore the events of non D3D11 devices
#define CHECK_GRAPHICS_API_COMPATIBILITY 1

#include "..\..\Core\core.hpp"
#include "..\..\Core\hooks\Hooks.h"

#include <chrono>
#include <intrin.h>
#include <Psapi.h>

namespace
{
   // FXAA 3.11 Quality, after tonemap and before the UI, at every Post-Processing level: SR replaces it, and runs here instead if the frame
   // couldn't split before bloom
   constexpr uint32_t kFXAAHash = 0x900968FF;
   // Full screen velocity (camera motion from depth plus object velocity), only with Motion Blur on and Post-Processing High+
   constexpr uint32_t kVelocityHash = 0xA13321B6;
   // The first bloom reduction (CopyReductionX4, which reads the scene), early in the post processing list at every Post-Processing level:
   // the SR point. Bloom, lens flare, depth of field, motion blur and the tonemap then run on SR's output, the order the FidelityFX and DLSS
   // guides give.
   constexpr uint32_t kBloomReductionHash = 0x72C1D157;
   // "tpp3dfw_TargetMaker" (sic) draws the marked enemy and night vision thermal outlines after SR, onto the final image, with the scene
   // constants: they need the game's own camera, without our jitter (see "UnjitterOutlines()")
   constexpr uint32_t kOutlineVSHash = 0x3D3E9DE2;

#if DEVELOPMENT
   bool g_skip_fxaa = false;
#endif
#if DEVELOPMENT || TEST
   // The game records its passes on deferred contexts, from several threads
   std::atomic<uint32_t> g_fxaa_draws = 0;
   std::atomic<uint32_t> g_velocity_draws = 0;
   uint32_t g_fxaa_draws_last_frame = 0;
   uint32_t g_velocity_draws_last_frame = 0;

   // Outlier logger: logs every new FXAA target (view format and size) and every change of the per frame pass counts (menus,
   // cutscenes, settings changes...), so the SR code knows each case it has to handle
   std::mutex g_fxaa_targets_mutex;
   std::set<std::tuple<DXGI_FORMAT, UINT, UINT>> g_fxaa_targets;
   uint32_t g_logged_count_changes = 0;
   constexpr uint32_t kMaxLoggedCountChanges = 100; // Loading screens might flip them every frame

   void LogNewFXAATarget(ID3D11DeviceContext* native_device_context)
   {
      com_ptr<ID3D11RenderTargetView> rtv;
      native_device_context->OMGetRenderTargets(1, &rtv, nullptr);
      if (!rtv)
         return;
      com_ptr<ID3D11Resource> resource;
      rtv->GetResource(&resource);
      com_ptr<ID3D11Texture2D> texture;
      if (FAILED(resource->QueryInterface(&texture)))
         return;
      D3D11_TEXTURE2D_DESC texture_desc;
      texture->GetDesc(&texture_desc);
      D3D11_RENDER_TARGET_VIEW_DESC rtv_desc;
      rtv->GetDesc(&rtv_desc);

      const std::lock_guard lock(g_fxaa_targets_mutex);
      if (g_fxaa_targets.emplace(rtv_desc.Format, texture_desc.Width, texture_desc.Height).second)
      {
         reshade::log::message(reshade::log::level::info, std::format("MGSV: new FXAA target {}x{}, view format {}", texture_desc.Width, texture_desc.Height, int(rtv_desc.Format)).c_str());
      }
   }
#endif

#if ENABLE_SAFETYHOOK
   // The game quits (exit code 0) right after creating its D3D11 device if the device's vtable isn't inside the module named "d3d11.dll",
   // and ReShade's device wrapper lives in ReShade's own module. The game gets that module's range through psapi's "GetModuleInformation()"
   // (which jumps to kernelbase's), so the game's own calls for "d3d11.dll" get ReShade's module instead.
   // The cause and the fix are from ghostinthecamera/mgsv-reshade-fix (MIT), which patches ReShade instead.
   safetyhook::InlineHook g_get_module_information_hook;

   BOOL WINAPI HookedGetModuleInformation(HANDLE process, HMODULE module, LPMODULEINFO module_info, DWORD size)
   {
      if (module && Hooks::IsAddressInModule(_ReturnAddress(), GetModuleHandleW(nullptr)))
      {
         const HMODULE reshade_module = reshade::internal::get_reshade_module_handle();
         wchar_t path[MAX_PATH];
         if (reshade_module && module != reshade_module && GetModuleFileNameW(module, path, MAX_PATH))
         {
            const wchar_t* file_name = wcsrchr(path, L'\\');
            if (_wcsicmp(file_name ? file_name + 1 : path, L"d3d11.dll") == 0)
            {
               static std::atomic<bool> logged = false;
               if (!logged.exchange(true))
               {
                  reshade::log::message(reshade::log::level::info, "MGSV: the game's D3D11 device check gets ReShade's module for d3d11.dll");
               }
               module = reshade_module;
            }
         }
      }
      return g_get_module_information_hook.stdcall<BOOL>(process, module, module_info, size);
   }

   // Before the game's device check, which follows its device creation
   void InstallDeviceCheckFix()
   {
      if (g_get_module_information_hook)
         return;
      void* const target = GetProcAddress(GetModuleHandleW(L"kernelbase.dll"), "K32GetModuleInformation");
      if (target)
      {
         if (auto hook = safetyhook::InlineHook::create(target, reinterpret_cast<void*>(&HookedGetModuleInformation)))
         {
            g_get_module_information_hook = std::move(*hook);
            return;
         }
      }
      reshade::log::message(reshade::log::level::error, "MGSV: couldn't hook GetModuleInformation, the game quits at startup unless ReShade has its own Metal Gear Solid V fix");
   }
#endif

#if ENABLE_SR
   // Fox Engine's scene constants ("cVSScene" and "cPSScene", the same 30 float4 rows for both stages) carry the camera, and shaders dot
   // the rows with the position (clip.x = dot(row 0, position)).
   // The engine supports off center cameras: geometry uses the off center terms of the projections, and position reconstruction
   // (lighting, shadows, velocity) subtracts "m_cameraCenterOffset" (NDC) from the screen position. So the jitter shifts both together.
   constexpr UINT kSceneCBSize = 480;
   constexpr int kRowProjectionView = 0;           // "m_projectionView"
   constexpr int kRowProjection = 4;               // "m_projection"
   constexpr int kRowViewOrPrevProjectionView = 8; // "m_view", but the velocity passes get the previous view projection here
   constexpr int kRowInvView = 16;                 // "m_shadowProjection2", but the velocity pass gets its view to world here
   constexpr int kRowEyePos = 20;                  // "m_eyepos"
   constexpr int kRowProjectionParam = 21;         // "m_projectionParam" (view position reconstruction terms)
   constexpr int kRowViewportSize = 22;            // "m_viewportSize" (width, height, near, far)
   constexpr int kRowCameraCenterOffset = 28;      // "m_cameraCenterOffset"
   using SceneRows = float[kSceneCBSize / 16][4];

   // A frame that can't split before bloom (seen on a scene's first frame) runs SR at FXAA's slot instead. That's a soft failure, logged as a
   // warning on its 1st, 10th, 100th... time, so a layout that never fits can't flood the log.
   enum class SRFallback : int
   {
      NoBloomReduction,     // FXAA came first on the frame's thread
      BloomSplitImpossible, // The bloom reduction's input isn't a render size scene SR can write, or a split is still pending
      Count
   };
   std::atomic<uint32_t> g_sr_fallbacks[int(SRFallback::Count)] = {};
   void LogSRFallback(SRFallback kind, const char* reason)
   {
      const uint32_t count = ++g_sr_fallbacks[int(kind)];
      if (count == 1 || count == 10 || count == 100 || count == 1000 || count % 10000 == 0)
      {
         reshade::log::message(reshade::log::level::warning, std::format("MGSV: SR couldn't run before bloom, so it runs at FXAA's slot ({} time{}): {}", count, count == 1 ? "" : "s", reason).c_str());
      }
   }

#if DEVELOPMENT
   bool g_jitter_enable = false;         // Jitter without SR too (to see it with FXAA)
   bool g_decode_motion_vectors = true;  // Decode without SR too (for the MCP readbacks)
   bool g_flip_motion_vectors_x = false; // SR input A/B knobs, in case a sign convention is off
   bool g_flip_motion_vectors_y = false;
   bool g_flip_jitter_x = false;
   bool g_flip_jitter_y = false;
#endif

   bool SrActive(const DeviceData& device_data)
   {
      return device_data.sr_type != SR::Type::None && !device_data.sr_suppressed;
   }

   // Without the velocity pass (Motion Blur off or Post-Processing Low) there are no motion vectors, so SR and its jitter stay off
   std::atomic<uint32_t> g_presents_since_velocity = UINT32_MAX / 2;
   // Safety net: if a split command list ever goes unexecuted (its first half would be lost), or SR keeps failing or not taking FXAA's place,
   // SR and its jitter stop for the session and FXAA comes back
   std::atomic<bool> g_sr_disabled = false;
   std::atomic<uint32_t> g_sr_failures_in_a_row = 0;
   // FXAA drew over a jittered scene (SR didn't take its place), so that frame shakes. Counted at present, like an SR failure.
   std::atomic<bool> g_fxaa_over_jitter = false;
   bool SrUsable(const DeviceData& device_data)
   {
      return SrActive(device_data) && !g_sr_disabled && g_presents_since_velocity < 2;
   }

   void DisableSR(const char* reason)
   {
      if (!g_sr_disabled.exchange(true))
      {
         reshade::log::message(reshade::log::level::error, std::format("MGSV: SR disabled ({}), FXAA is back until another Super Resolution type is picked", reason).c_str());
      }
   }

   // A frame SR doesn't draw has no AA (FXAA was skipped for it): if that keeps happening, the split stops
   void CountSRResult(bool drawn)
   {
      if (drawn)
         g_sr_failures_in_a_row = 0;
      else if (++g_sr_failures_in_a_row >= 3)
         DisableSR("SR failed to draw 3 frames in a row");
   }

   // The velocity pass's camera of the frame being recorded on this thread, without our jitter: its upload comes right before its draw,
   // where the motion vector decode rebuilds camera motion from it. It also carries the frame's jitter for SR: the game records the
   // velocity pass and FXAA of a frame on the same thread (measured in every frame of a long capture).
   struct VelocityCamera
   {
      CB::LumaGameData data = {};
      float jitter_x = 0.f; // Pixels, as SR takes them
      float jitter_y = 0.f;
      float vertical_fov = 0.f; // Radians
      float near_plane = 0.f;
      float far_plane = 0.f;
      bool valid = false;
   };
   thread_local VelocityCamera t_velocity_camera;
   // This thread decoded the motion vectors of the frame it's recording (at its velocity pass), so that frame's FXAA can become SR
   thread_local bool t_decoded_motion_vectors = false;
   // Whether the frame this thread records split before bloom (the same thread records its velocity pass, the post processing list and
   // FXAA's list): set at the bloom reduction, read at FXAA, reset at FXAA and at the velocity pass
   enum class BloomSplit : uint8_t
   {
      None,   // No bloom reduction yet
      Done,   // SR runs before bloom, FXAA goes
      Failed, // Logged there: SR runs at FXAA's slot
   };
   thread_local BloomSplit t_bloom_split = BloomSplit::None;

#if DEVELOPMENT
   // Diagnostics per FXAA interval (FXAA is the game's last pass before the UI)
   std::atomic<uint32_t> g_scene_cb_uploads = 0;
   std::atomic<uint32_t> g_jittered_uploads = 0;
   std::atomic<uint32_t> g_new_frames = 0;
   std::atomic<uint32_t> g_invalid_frame_keys = 0;
   uint32_t g_scene_cb_uploads_last_frame = 0;
   uint32_t g_jittered_uploads_last_frame = 0;
   uint32_t g_new_frames_last_frame = 0;
   uint32_t g_invalid_frame_keys_last_frame = 0;
#endif

   // The aspect ratio of the camera the velocity pass uses (the only upload with the previous view projection), i.e. the main camera,
   // whatever the resolution, aspect ratio or pillarboxing. Until it's seen, the output's aspect ratio stands in.
   std::atomic<float> g_main_aspect_ratio = 0.f;

   // The jitter phase of each frame, keyed by the frame's camera state. The game records its frames on worker threads, and the passes of
   // a frame in no fixed order (some of them after its FXAA, sometimes the next frame's before it), so no record time event (present,
   // FXAA draw) is a reliable frame boundary. All the main view uploads of a frame carry the same camera state instead: the depth row of
   // the view projection (the camera's forward axis and distance term, independent of the projection) and the eye position.
   // A mismatch between the passes of a frame would break depth pre-passes (the terrain's goes black).
   struct FrameKey
   {
      float rows[2][4];

      bool operator==(const FrameKey& other) const
      {
         return std::memcmp(rows, other.rows, sizeof(rows)) == 0;
      }
   };
   struct FramePhase
   {
      FrameKey key;
      uint32_t frame = 0;
      uint32_t phase = 0;
      bool counted = false; // Its velocity pass upload was seen
      bool valid = false;
      uint32_t generation = 0; // See "g_generation"
   };
   std::mutex g_frame_phases_mutex;
   std::array<FramePhase, 8> g_frame_phases; // The latest camera keys (interleaved recording spans two frames)
   uint32_t g_next_frame = 0;
   // Main camera frames so far, counted at their velocity pass upload (exactly one per frame), picks the phase of each new camera key.
   // Other uploads with a camera state of their own (the previous camera's values re-uploaded early in many frames) can't consume phases.
   uint32_t g_main_frames = 0;

   // A perfectly still camera uploads the same key every frame, which would freeze the jitter. Its next frame starts at the first main view
   // upload after a present that follows a pause of at least 1 ms in the uploads (the gap between two frames' recording): that starts a new
   // generation, and a key from an older generation takes the next phase.
   uint32_t g_generation = 0;
   std::atomic<uint32_t> g_presents = 0;
   uint32_t g_last_upload_present = UINT32_MAX; // "g_presents" at the last main view upload
   std::chrono::steady_clock::time_point g_last_upload_time;

#if DEVELOPMENT
   std::atomic<uint32_t> g_latest_frame = 0;

   // Logs the first new camera keys after the jitter is turned on
   uint32_t g_logged_keys = 0;
   constexpr uint32_t kMaxLoggedKeys = 32;

   // Which frame's phase SR gets at the FXAA draw, which uploads no camera: the last main view upload recorded on the FXAA's own thread
   // (the velocity pass's, see "VelocityCamera"). Counts since the jitter was last turned on.
   std::atomic<uint32_t> g_last_velocity_frame = UINT32_MAX;
   std::atomic<uint32_t> g_last_velocity_phase = UINT32_MAX;
   thread_local uint32_t t_last_main_frame = UINT32_MAX;
   thread_local bool t_main_upload_since_fxaa = false;
   uint32_t g_fxaa_records = 0;
   uint32_t g_fxaa_thread_had_main = 0;
   uint32_t g_fxaa_thread_matches_velocity = 0;
   uint32_t g_fxaa_velocity_is_latest = 0;
   // How the velocity pass phase moved since the previous FXAA draw: by one (expected), not at all (a perfectly still camera), or else
   uint32_t g_fxaa_phase_steps = 0;
   uint32_t g_fxaa_phase_repeats = 0;
   uint32_t g_fxaa_phase_skips = 0;
#endif

   // Frames with an identical camera state share an index and phase, until a still camera's next frame (see "g_generation")
   uint32_t GetFrameIndex(const SceneRows& rows, bool velocity_upload, uint32_t& phase)
   {
      FrameKey key;
      std::memcpy(key.rows[0], rows[kRowProjectionView + 3], sizeof(key.rows[0]));
      std::memcpy(key.rows[1], rows[kRowEyePos], sizeof(key.rows[1]));

      const std::lock_guard lock(g_frame_phases_mutex);
      FramePhase* frame_phase = nullptr;
      for (FramePhase& candidate : g_frame_phases)
      {
         if (candidate.valid && candidate.key == key)
         {
            frame_phase = &candidate;
            break;
         }
      }

      // A still camera's next frame (see "g_generation")
      const auto now = std::chrono::steady_clock::now();
      const uint32_t present = g_presents.load(std::memory_order_relaxed);
      if (present != g_last_upload_present && now - g_last_upload_time >= std::chrono::milliseconds(1))
         g_generation++;
      g_last_upload_present = present;
      g_last_upload_time = now;
      if (frame_phase && frame_phase->generation != g_generation)
      {
         // The same camera in a later frame takes that frame's phase
         frame_phase->phase = g_main_frames % SR::GetDefaultJitterPhases();
         frame_phase->counted = false;
         frame_phase->generation = g_generation;
      }

      if (!frame_phase)
      {
         const uint32_t frame = g_next_frame++;
         frame_phase = &g_frame_phases[frame % g_frame_phases.size()];
         *frame_phase = {key, frame, g_main_frames % SR::GetDefaultJitterPhases(), false, true};
         frame_phase->generation = g_generation;
#if DEVELOPMENT
         g_latest_frame = frame;
         g_new_frames++;
         if (g_logged_keys < kMaxLoggedKeys)
         {
            g_logged_keys++;
            const float (&eye)[4] = rows[kRowEyePos];
            const float (&forward)[4] = rows[kRowProjectionView + 3];
            reshade::log::message(reshade::log::level::info, std::format("MGSV: camera key {} (main frame {}, phase {}): velocity upload {}, eye ({:.4f} {:.4f} {:.4f}), forward ({:.5f} {:.5f} {:.5f} {:.4f}), projection scale ({:.4f} {:.4f})", frame, g_main_frames, frame_phase->phase, velocity_upload, eye[0], eye[1], eye[2], forward[0], forward[1], forward[2], forward[3], rows[kRowProjection][0], rows[kRowProjection + 1][1]).c_str());
         }
#endif
      }
      if (velocity_upload && !frame_phase->counted)
      {
         frame_phase->counted = true;
         g_main_frames++;
      }
      phase = frame_phase->phase;
      return frame_phase->frame;
   }

   // The scene cbuffer this thread mapped (each deferred context is recorded by one thread at a time)
   struct PendingSceneCBMap
   {
      uint64_t buffer = 0;
      void* data = nullptr;
   };
   thread_local PendingSceneCBMap t_pending_scene_cb_map;

   // This thread's last jittered scene cbuffer upload, with the game's own rows, for the outlines (see "UnjitterOutlines()")
   struct JitteredUpload
   {
      uint64_t buffer = 0;
      SceneRows game_rows;
      bool restored = false; // The game's rows are back in the buffer
   };
   thread_local JitteredUpload t_last_jittered_upload;
   thread_local bool t_rewriting_scene_cb = false; // Our own write of a scene cbuffer, not the game's upload

#if DEVELOPMENT
   // Outlier logger: every kind of view the game uploads scene constants for (main, shadow, reflection...)
   std::mutex g_view_signatures_mutex;
   std::set<std::tuple<bool, int, bool>> g_view_signatures;
#endif

   // The depth (w) row of a view projection is the camera's unit forward axis (plus its distance term)
   bool IsViewProjectionWRow(const float (&row)[4], const float (&current_w_row)[4])
   {
      const float length_sq = row[0] * row[0] + row[1] * row[1] + row[2] * row[2];
      const float alignment = row[0] * current_w_row[0] + row[1] * current_w_row[1] + row[2] * current_w_row[2];
      return std::abs(length_sq - 1.f) < 0.01f && alignment > 0.5f;
   }

   // Main view only: a perspective projection (w = view z, which leaves out orthographic shadow views) with the main camera's aspect
   // ratio (spot light shadow views are square). Every view reports the output size as its viewport, so that can't tell them apart.
   bool IsMainView(const SceneRows& rows, float2 output_size, bool& prev_projection_view)
   {
      const float (&w_row)[4] = rows[kRowProjection + 3];
      const bool perspective = w_row[0] == 0.f && w_row[1] == 0.f && w_row[2] == 1.f && w_row[3] == 0.f;
      prev_projection_view = IsViewProjectionWRow(rows[kRowViewOrPrevProjectionView + 3], rows[kRowProjectionView + 3]);
      const float aspect_ratio = rows[kRowProjection][0] != 0.f ? std::abs(rows[kRowProjection + 1][1] / rows[kRowProjection][0]) : 0.f;
      if (perspective && prev_projection_view)
      {
         g_main_aspect_ratio = aspect_ratio;
      }
      const float main_aspect_ratio = g_main_aspect_ratio > 0.f ? g_main_aspect_ratio.load() : (output_size.x / output_size.y);
      const bool main_view = perspective && std::abs(aspect_ratio - main_aspect_ratio) < main_aspect_ratio * 0.005f;

#if DEVELOPMENT
      const auto signature = std::make_tuple(perspective, int(std::round(aspect_ratio * 100.f)), prev_projection_view);
      {
         const std::lock_guard lock(g_view_signatures_mutex);
         if (g_view_signatures.size() < 64 && g_view_signatures.insert(signature).second)
         {
            reshade::log::message(reshade::log::level::info, std::format("MGSV: new scene view: perspective {}, aspect ratio {:.3f}, vertical scale {:.3f}, previous view projection in rows 8-11 {}, main view {}", perspective, aspect_ratio, rows[kRowProjection + 1][1], prev_projection_view, main_view).c_str());
         }
      }
#endif

      return main_view;
   }

   // Shifts the view by "offset" (NDC), like an off center projection: clip.xy += offset * clip.w
   void JitterView(SceneRows& rows, bool prev_projection_view, float offset_x, float offset_y)
   {
      const auto shift = [&](int first_row)
      {
         for (int i = 0; i < 4; i++)
         {
            rows[first_row][i] += offset_x * rows[first_row + 3][i];
            rows[first_row + 1][i] += offset_y * rows[first_row + 3][i];
         }
      };
      shift(kRowProjection);
      shift(kRowProjectionView);
      // The velocity pass computes velocity from the rasterized (jittered) pixel position and the previous view projection:
      // shifting that one by the same offset keeps velocity, and thus motion blur, free of jitter
      if (prev_projection_view)
      {
         shift(kRowViewOrPrevProjectionView);
      }
      rows[kRowCameraCenterOffset][0] += offset_x;
      rows[kRowCameraCenterOffset][1] += offset_y;
   }

   void OnMapSceneCB(reshade::api::device* device, reshade::api::resource resource, uint64_t offset, uint64_t size, reshade::api::map_access access, void** data)
   {
      SKIP_UNSUPPORTED_DEVICE_API(device->get_api());
      if (access != reshade::api::map_access::write_discard || t_rewriting_scene_cb)
         return;
      // Only SR (or the development tools) needs the scene constants: skip the per map work otherwise (the game maps thousands of buffers)
      bool needed = SrUsable(*device->get_private_data<DeviceData>());
#if DEVELOPMENT
      needed |= g_jitter_enable || g_decode_motion_vectors;
#endif
      if (!needed)
         return;
      ID3D11Buffer* buffer = reinterpret_cast<ID3D11Buffer*>(resource.handle);
      D3D11_BUFFER_DESC desc;
      buffer->GetDesc(&desc);
      if (desc.ByteWidth == kSceneCBSize && (desc.BindFlags & D3D11_BIND_CONSTANT_BUFFER) != 0)
      {
         t_pending_scene_cb_map = {resource.handle, *data};
      }
   }

   // ReShade calls this before the real unmap, so the mapped memory still takes our changes
   void OnUnmapSceneCB(reshade::api::device* device, reshade::api::resource resource)
   {
      if (t_pending_scene_cb_map.buffer != resource.handle || t_pending_scene_cb_map.data == nullptr)
         return;
      void* const data = t_pending_scene_cb_map.data;
      t_pending_scene_cb_map = {};
#if DEVELOPMENT
      g_scene_cb_uploads++;
#endif
      // A new upload replaces the rows that buffer held (they're kept again below if we jitter them)
      if (t_last_jittered_upload.buffer == resource.handle)
         t_last_jittered_upload.buffer = 0;

      // Work on a copy, the mapped memory is write combined (slow to read)
      SceneRows rows;
      std::memcpy(rows, data, sizeof(rows));
      const DeviceData& device_data = *device->get_private_data<DeviceData>();
      bool prev_projection_view = false;
      if (!IsMainView(rows, device_data.output_resolution, prev_projection_view))
         return;

      if (prev_projection_view)
      {
         VelocityCamera& camera = t_velocity_camera;
         // Previous view projection x view to world, in double: both carry the eye position (hundreds of meters to kilometers from the
         // map origin), which cancels out here instead of in the decode's float math
         float view_to_prev_clip[4][4];
         for (int i = 0; i < 4; i++)
         {
            for (int j = 0; j < 4; j++)
            {
               double sum = 0.0;
               for (int k = 0; k < 4; k++)
               {
                  sum += double(rows[kRowViewOrPrevProjectionView + i][k]) * double(rows[kRowInvView + k][j]);
               }
               view_to_prev_clip[i][j] = float(sum);
            }
         }
         std::memcpy(camera.data.ViewToPrevClip, view_to_prev_clip, sizeof(camera.data.ViewToPrevClip));
         std::memcpy(&camera.data.ProjectionParam, rows[kRowProjectionParam], sizeof(camera.data.ProjectionParam));
         camera.data.CameraCenterOffset = {rows[kRowCameraCenterOffset][0], rows[kRowCameraCenterOffset][1]};
         camera.data.RenderSize = {rows[kRowViewportSize][0], rows[kRowViewportSize][1]};
         camera.jitter_x = 0.f;
         camera.jitter_y = 0.f;
         camera.vertical_fov = rows[kRowProjection + 1][1] != 0.f ? 2.f * std::atan(1.f / std::abs(rows[kRowProjection + 1][1])) : 0.f;
         camera.near_plane = rows[kRowViewportSize][2];
         camera.far_plane = rows[kRowViewportSize][3];
         camera.valid = true;
      }

      bool jitter = SrUsable(device_data);
#if DEVELOPMENT
      jitter |= g_jitter_enable;
#endif
      if (!jitter)
         return;

#if DEVELOPMENT
      // A frame key without a valid camera axis could split a frame's passes into different phases
      if (!IsViewProjectionWRow(rows[kRowProjectionView + 3], rows[kRowProjectionView + 3]))
      {
         g_invalid_frame_keys++;
      }
#endif

      // Halton(2, 3) in pixels of the game's render viewport, as an NDC offset (NDC y goes up, pixels go down)
      uint32_t phase = 0;
      const uint32_t frame = GetFrameIndex(rows, prev_projection_view, phase);
#if DEVELOPMENT
      t_last_main_frame = frame;
      t_main_upload_since_fxaa = true;
      if (prev_projection_view)
      {
         g_last_velocity_frame = frame;
         g_last_velocity_phase = phase;
      }
#endif
      const float jitter_x = SR::HaltonSequence(phase, 2);
      const float jitter_y = SR::HaltonSequence(phase, 3);
      const float render_width = rows[kRowViewportSize][0] > 0.f ? rows[kRowViewportSize][0] : device_data.output_resolution.x;
      const float render_height = rows[kRowViewportSize][1] > 0.f ? rows[kRowViewportSize][1] : device_data.output_resolution.y;
      const float offset_x = 2.f * jitter_x / render_width;
      const float offset_y = -2.f * jitter_y / render_height;
      JitteredUpload& last_upload = t_last_jittered_upload;
      std::memcpy(last_upload.game_rows, rows, sizeof(rows));
      last_upload.buffer = resource.handle;
      last_upload.restored = false;
      JitterView(rows, prev_projection_view, offset_x, offset_y);
      std::memcpy(data, rows, sizeof(rows));
#if DEVELOPMENT
      g_jittered_uploads++;
#endif
      if (prev_projection_view)
      {
         t_velocity_camera.data.CameraCenterOffset.x += offset_x;
         t_velocity_camera.data.CameraCenterOffset.y += offset_y;
         t_velocity_camera.jitter_x = jitter_x;
         t_velocity_camera.jitter_y = jitter_y;
      }
   }

   // The outlines draw after SR (see "kOutlineVSHash") with this thread's last scene cbuffer upload, which carries our jitter: before their
   // first draw, that buffer gets the game's own rows back. They stay for the rest of its draws, which all come after SR too.
   void UnjitterOutlines(ID3D11DeviceContext* native_device_context, const DeviceData& device_data, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes)
   {
      JitteredUpload& last_upload = t_last_jittered_upload;
      if (last_upload.buffer == 0 || last_upload.restored || !original_shader_hashes.Contains(kOutlineVSHash, reshade::api::shader_stage::vertex))
         return;
      if (!SrUsable(device_data))
      {
         last_upload.buffer = 0; // Uploads aren't followed without SR, so this one could be stale
         return;
      }
      com_ptr<ID3D11Buffer> bound;
      native_device_context->VSGetConstantBuffers(2, 1, &bound);
      if (reinterpret_cast<uint64_t>(bound.get()) != last_upload.buffer)
         return;
      // A new version of the buffer for the draws recorded from here on (deferred contexts take write discard maps of dynamic buffers)
      t_rewriting_scene_cb = true;
      D3D11_MAPPED_SUBRESOURCE mapped;
      if (SUCCEEDED(native_device_context->Map(bound.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
      {
         std::memcpy(mapped.pData, last_upload.game_rows, sizeof(SceneRows));
         native_device_context->Unmap(bound.get(), 0);
         last_upload.restored = true;
      }
      t_rewriting_scene_cb = false;
   }

   // A split command list waiting for SR (split at the bloom reduction, or at FXAA), run when the rest of that command list executes
   struct SRJob
   {
      com_ptr<ID3D11CommandList> partial_command_list; // Everything the deferred context recorded before the split
      com_ptr<ID3D11ShaderResourceView> source_srv;    // The scene before bloom, or FXAA's input (a copy of the tonemapped image)
      com_ptr<ID3D11RenderTargetView> target_rtv;      // The same scene, or FXAA's output (where the UI draws next)
      com_ptr<ID3D11Texture2D> motion_vectors;         // The textures this frame's decode wrote (they're recreated on resolution changes)
      com_ptr<ID3D11Texture2D> depth;
      UINT width = 0;
      UINT height = 0;
      float jitter_x = 0.f;
      float jitter_y = 0.f;
      float vertical_fov = 0.f;
      float near_plane = 0.f;
      float far_plane = 0.f;
      uint32_t present_index = 0;
      bool before_bloom = false; // SR writes the scene it read back in place, colour only (depth of field and motion blur read its alpha)
   };
   std::mutex g_sr_jobs_mutex;
   std::unordered_map<ID3D11DeviceContext*, SRJob> g_sr_jobs_by_context;    // Split, until the game finishes the rest of the list
   std::unordered_map<ID3D11CommandList*, SRJob> g_sr_jobs_by_command_list; // Until that command list executes
#endif
} // namespace

struct GameDeviceDataMetalGearSolidV final : public GameDeviceData
{
#if ENABLE_SR
   std::mutex textures_mutex;
   // SR's motion vectors (R16G16_FLOAT) and depth (R32_FLOAT), at render size, written by "Luma_MGSV_DecodeMotionVectors"
   com_ptr<ID3D11Texture2D> motion_vectors;
   com_ptr<ID3D11UnorderedAccessView> motion_vectors_uav;
   com_ptr<ID3D11Texture2D> depth;
   com_ptr<ID3D11UnorderedAccessView> depth_uav;
   // SR's input and output (which must be a UAV, FXAA's target isn't one). They're R8G8B8A8 because the FidelityFX DX11 backend has no
   // B8G8R8A8 formats (FXAA's input and target are): copied from FXAA's input and into its target with Luma's copy shaders
   com_ptr<ID3D11Texture2D> sr_input;
   com_ptr<ID3D11RenderTargetView> sr_input_rtv;
   com_ptr<ID3D11Texture2D> sr_output;
   com_ptr<ID3D11ShaderResourceView> sr_output_srv;
   // 1x1 R32_FLOAT holding 1: the input is already exposed (after the tonemap). FSR 4 rejects frames without an exposure texture when its
   // auto exposure is off, FSR 3 assumes the same 1 without one.
   com_ptr<ID3D11Texture2D> sr_exposure;
   // Before bloom, SR writes the scene back through this view of it (the game only reads the scene there)
   com_ptr<ID3D11Texture2D> scene;
   com_ptr<ID3D11RenderTargetView> scene_rtv;
   // Writes colour only, so SR keeps the scene's alpha before bloom (depth of field and motion blur read it)
   com_ptr<ID3D11BlendState> color_only_blend_state;
   // SR drew since the last present (otherwise its history resets on the next SR frame)
   std::atomic<bool> sr_drawn = false;
   // The SR type at the last present: the safety net's verdict only holds for the type it saw fail
   SR::Type sr_type_at_present = SR::Type::None;
#endif
};

class MetalGearSolidV final : public Game
{
   static GameDeviceDataMetalGearSolidV& GetGameDeviceData(DeviceData& device_data)
   {
      return *static_cast<GameDeviceDataMetalGearSolidV*>(device_data.game);
   }

#if ENABLE_SR
   // (Re)creates a render size texture (shader resource and unordered access, plus render target when its view is asked for), and the views asked for
   static bool EnsureTexture(ID3D11Device* native_device, com_ptr<ID3D11Texture2D>& texture, com_ptr<ID3D11UnorderedAccessView>* uav, UINT width, UINT height, DXGI_FORMAT format,
      com_ptr<ID3D11ShaderResourceView>* srv = nullptr, com_ptr<ID3D11RenderTargetView>* rtv = nullptr)
   {
      D3D11_TEXTURE2D_DESC desc = {};
      if (texture)
      {
         texture->GetDesc(&desc);
         if (desc.Width == width && desc.Height == height && desc.Format == format)
            return true;
      }
      texture.reset();
      if (uav)
         uav->reset();
      if (srv)
         srv->reset();
      if (rtv)
         rtv->reset();
      desc = {};
      desc.Width = width;
      desc.Height = height;
      desc.MipLevels = 1;
      desc.ArraySize = 1;
      desc.Format = format;
      desc.SampleDesc.Count = 1;
      desc.Usage = D3D11_USAGE_DEFAULT;
      desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
      if (rtv)
         desc.BindFlags |= D3D11_BIND_RENDER_TARGET;
      if (FAILED(native_device->CreateTexture2D(&desc, nullptr, &texture)))
         return false;
      if ((uav && FAILED(native_device->CreateUnorderedAccessView(texture.get(), nullptr, &(*uav)))) ||
          (srv && FAILED(native_device->CreateShaderResourceView(texture.get(), nullptr, &(*srv)))) ||
          (rtv && FAILED(native_device->CreateRenderTargetView(texture.get(), nullptr, &(*rtv)))))
      {
         texture.reset();
         return false;
      }
      return true;
   }

   // Recorded in the game's own deferred command list, right before its velocity pass draws: its depth and object velocity are bound there
   static bool DecodeMotionVectors(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data)
   {
      com_ptr<ID3D11ShaderResourceView> srvs[2]; // t2 depth, t3 object velocity (see the pass map)
      native_device_context->PSGetShaderResources(2, 2, &srvs[0]);
      if (!srvs[0] || !srvs[1])
         return false;

      const UINT width = UINT(t_velocity_camera.data.RenderSize.x);
      const UINT height = UINT(t_velocity_camera.data.RenderSize.y);
      auto& game_device_data = GetGameDeviceData(device_data);
      com_ptr<ID3D11UnorderedAccessView> uavs[2];
      {
         const std::lock_guard lock(game_device_data.textures_mutex);
         if (!EnsureTexture(native_device, game_device_data.motion_vectors, std::addressof(game_device_data.motion_vectors_uav), width, height, DXGI_FORMAT_R16G16_FLOAT) ||
             !EnsureTexture(native_device, game_device_data.depth, std::addressof(game_device_data.depth_uav), width, height, DXGI_FORMAT_R32_FLOAT))
            return false;
         uavs[0] = game_device_data.motion_vectors_uav;
         uavs[1] = game_device_data.depth_uav;
      }

      com_ptr<ID3D11ComputeShader> shader;
      {
         const std::shared_lock lock(s_mutex_shader_objects);
         const auto it = device_data.native_compute_shaders.find(CompileTimeStringHash("MGSV Decode Motion Vectors"));
         if (it != device_data.native_compute_shaders.end())
            shader = it->second;
      }
      if (!shader)
         return false;

      DrawStateStack<DrawStateStackType::Compute> compute_state;
      compute_state.Cache(native_device_context, device_data.uav_max_count);
      native_device_context->CSSetShader(shader.get(), nullptr, 0);
      ID3D11ShaderResourceView* const cs_srvs[2] = {srvs[0].get(), srvs[1].get()};
      native_device_context->CSSetShaderResources(0, 2, cs_srvs);
      ID3D11UnorderedAccessView* const cs_uavs[2] = {uavs[0].get(), uavs[1].get()};
      native_device_context->CSSetUnorderedAccessViews(0, 2, cs_uavs, nullptr);
      SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, reshade::api::shader_stage::compute, LumaConstantBufferType::LumaData);
      native_device_context->Dispatch((width + 7) / 8, (height + 7) / 8, 1);
      compute_state.Restore(native_device_context);
      return true;
   }

   // At the FXAA draw, on its deferred context, if the frame couldn't split before bloom: close the command list before FXAA (SR runs
   // between this part and the rest)
   static bool SplitForSR(ID3D11DeviceContext* native_device_context, DeviceData& device_data)
   {
      com_ptr<ID3D11ShaderResourceView> source_srv;
      native_device_context->PSGetShaderResources(0, 1, &source_srv);
      com_ptr<ID3D11RenderTargetView> target_rtv;
      native_device_context->OMGetRenderTargets(1, &target_rtv, nullptr);
      if (!source_srv || !target_rtv)
         return false;

      SRJob job;
      com_ptr<ID3D11Resource> target;
      target_rtv->GetResource(&target);
      com_ptr<ID3D11Texture2D> target_texture;
      if (FAILED(target->QueryInterface(&target_texture)))
         return false;
      D3D11_TEXTURE2D_DESC target_desc;
      target_texture->GetDesc(&target_desc);
      job.width = target_desc.Width;
      job.height = target_desc.Height;
      // SR replaces AA at the render size, into a single sample target (else FXAA stays). The copies in and out of SR take any format.
      if (target_desc.SampleDesc.Count != 1 || job.width != UINT(t_velocity_camera.data.RenderSize.x) || job.height != UINT(t_velocity_camera.data.RenderSize.y))
         return false;
      job.source_srv = std::move(source_srv);
      job.target_rtv = std::move(target_rtv);
      if (!FillSRJobFrame(job, device_data))
         return false;

      // Closed before the job is registered: our own split must never pass for the end of the game's command list
      if (FAILED(native_device_context->FinishCommandList(TRUE, &job.partial_command_list)))
         return false;
      const std::lock_guard lock(g_sr_jobs_mutex);
      g_sr_jobs_by_context[native_device_context] = std::move(job);
      return true;
   }

   // The frame's motion vectors, depth and camera for SR, from this thread's velocity pass
   static bool FillSRJobFrame(SRJob& job, DeviceData& device_data)
   {
      {
         auto& game_device_data = GetGameDeviceData(device_data);
         const std::lock_guard lock(game_device_data.textures_mutex);
         job.motion_vectors = game_device_data.motion_vectors;
         job.depth = game_device_data.depth;
      }
      if (!job.motion_vectors || !job.depth)
         return false;
      job.jitter_x = t_velocity_camera.jitter_x;
      job.jitter_y = t_velocity_camera.jitter_y;
      job.vertical_fov = t_velocity_camera.vertical_fov;
      job.near_plane = t_velocity_camera.near_plane;
      job.far_plane = t_velocity_camera.far_plane;
      job.present_index = g_presents;
      return true;
   }

   // At the first bloom reduction, on the thread that decoded this frame's motion vectors: close the post processing list there, so SR runs
   // before the rest of it, in place on the scene the reduction reads (through the game's own view of it, and a render target view alike)
   static bool SplitBeforeBloom(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, DeviceData& device_data)
   {
      {
         const std::lock_guard lock(g_sr_jobs_mutex);
         if (g_sr_jobs_by_context.contains(native_device_context))
            return false;
      }
      com_ptr<ID3D11ShaderResourceView> scene_srv;
      native_device_context->PSGetShaderResources(0, 1, &scene_srv);
      if (!scene_srv)
         return false;
      com_ptr<ID3D11Resource> resource;
      scene_srv->GetResource(&resource);
      com_ptr<ID3D11Texture2D> scene;
      if (FAILED(resource->QueryInterface(&scene)))
         return false;
      D3D11_TEXTURE2D_DESC desc;
      scene->GetDesc(&desc);
      SRJob job;
      job.before_bloom = true;
      job.width = UINT(t_velocity_camera.data.RenderSize.x);
      job.height = UINT(t_velocity_camera.data.RenderSize.y);
      if (desc.SampleDesc.Count != 1 || desc.Width != job.width || desc.Height != job.height || (desc.BindFlags & D3D11_BIND_RENDER_TARGET) == 0)
         return false;

      com_ptr<ID3D11RenderTargetView> scene_rtv;
      {
         auto& game_device_data = GetGameDeviceData(device_data);
         const std::lock_guard lock(game_device_data.textures_mutex);
         if (game_device_data.scene.get() != scene.get())
         {
            D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc;
            scene_srv->GetDesc(&srv_desc);
            D3D11_RENDER_TARGET_VIEW_DESC rtv_desc = {};
            rtv_desc.Format = srv_desc.Format; // The stored, gamma encoded values (UNORM), like the game reads them
            rtv_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
            game_device_data.scene = scene;
            game_device_data.scene_rtv.reset();
            native_device->CreateRenderTargetView(scene.get(), &rtv_desc, &game_device_data.scene_rtv);
         }
         scene_rtv = game_device_data.scene_rtv;
      }
      if (!scene_rtv || !FillSRJobFrame(job, device_data))
         return false;
      job.source_srv = std::move(scene_srv);
      job.target_rtv = std::move(scene_rtv);

      // Closed before the job is registered, like at FXAA
      if (FAILED(native_device_context->FinishCommandList(TRUE, &job.partial_command_list)))
         return false;
      const std::lock_guard lock(g_sr_jobs_mutex);
      g_sr_jobs_by_context[native_device_context] = std::move(job);
#if DEVELOPMENT || TEST
      static std::atomic<bool> logged = false;
      if (!logged.exchange(true))
      {
         reshade::log::message(reshade::log::level::info, std::format("MGSV: SR runs before bloom (scene {}x{}, texture format {})", desc.Width, desc.Height, int(desc.Format)).c_str());
      }
#endif
      return true;
   }

   // On the immediate context, after the part of the command list before the split executed: the source into SR's input, SR, then SR's
   // output into the target
   static void DrawSR(ID3D11DeviceContext* native_device_context, DeviceData& device_data, const SRJob& job)
   {
      if (!SrActive(device_data))
         return; // Turned off since the split: FXAA's target keeps the image without AA for this frame

      auto& game_device_data = GetGameDeviceData(device_data);
      com_ptr<ID3D11Texture2D> input, output, exposure;
      com_ptr<ID3D11RenderTargetView> input_rtv;
      com_ptr<ID3D11ShaderResourceView> output_srv;
      ID3D11BlendState* const output_blend_state = job.before_bloom ? game_device_data.color_only_blend_state.get() : device_data.default_blend_state.get();
      {
         com_ptr<ID3D11Device> native_device;
         native_device_context->GetDevice(&native_device);
         const std::lock_guard lock(game_device_data.textures_mutex);
         if (!game_device_data.sr_exposure)
         {
            const float one = 1.f;
            const D3D11_TEXTURE2D_DESC exposure_desc = {1, 1, 1, 1, DXGI_FORMAT_R32_FLOAT, {1, 0}, D3D11_USAGE_IMMUTABLE, D3D11_BIND_SHADER_RESOURCE, 0, 0};
            const D3D11_SUBRESOURCE_DATA exposure_data = {&one, sizeof(one), 0};
            native_device->CreateTexture2D(&exposure_desc, &exposure_data, &game_device_data.sr_exposure);
         }
         exposure = game_device_data.sr_exposure;
         if (EnsureTexture(native_device.get(), game_device_data.sr_input, nullptr, job.width, job.height, DXGI_FORMAT_R8G8B8A8_UNORM, nullptr, std::addressof(game_device_data.sr_input_rtv)) &&
             EnsureTexture(native_device.get(), game_device_data.sr_output, nullptr, job.width, job.height, DXGI_FORMAT_R8G8B8A8_UNORM, std::addressof(game_device_data.sr_output_srv)))
         {
            input = game_device_data.sr_input;
            input_rtv = game_device_data.sr_input_rtv;
            output = game_device_data.sr_output;
            output_srv = game_device_data.sr_output_srv;
         }
      }
      com_ptr<ID3D11VertexShader> copy_vs;
      com_ptr<ID3D11PixelShader> copy_ps;
      {
         const std::shared_lock lock(s_mutex_shader_objects);
         const auto vs = device_data.native_vertex_shaders.find(CompileTimeStringHash("Copy VS"));
         if (vs != device_data.native_vertex_shaders.end())
            copy_vs = vs->second;
         const auto ps = device_data.native_pixel_shaders.find(CompileTimeStringHash("Copy PS"));
         if (ps != device_data.native_pixel_shaders.end())
            copy_ps = ps->second;
      }
      auto* sr_instance_data = device_data.GetSRInstanceData();
      if (!input || !output || !exposure || !output_blend_state || !copy_vs || !copy_ps || !sr_instance_data)
      {
         CountSRResult(false);
         return;
      }

      float motion_vectors_scale_x = 1.f, motion_vectors_scale_y = 1.f, jitter_scale_x = 1.f, jitter_scale_y = 1.f;
#if DEVELOPMENT
      motion_vectors_scale_x = g_flip_motion_vectors_x ? -1.f : 1.f;
      motion_vectors_scale_y = g_flip_motion_vectors_y ? -1.f : 1.f;
      jitter_scale_x = g_flip_jitter_x ? -1.f : 1.f;
      jitter_scale_y = g_flip_jitter_y ? -1.f : 1.f;
#endif

      DrawStateStack<DrawStateStackType::FullGraphics> graphics_state;
      graphics_state.Cache(native_device_context, device_data.uav_max_count);
      DrawStateStack<DrawStateStackType::Compute> compute_state;
      compute_state.Cache(native_device_context, device_data.uav_max_count);

      // Like core's redirected copies: no target can still be bound while its texture is read
      native_device_context->OMSetRenderTargetsAndUnorderedAccessViews(0, nullptr, nullptr, 0, 0, nullptr, nullptr);
      DrawCustomPixelShader(native_device_context, device_data.default_depth_stencil_state.get(), device_data.default_blend_state.get(), nullptr, copy_vs.get(), copy_ps.get(), job.source_srv.get(), input_rtv.get(), job.width, job.height, true, "MGSV SR Input");
      native_device_context->OMSetRenderTargetsAndUnorderedAccessViews(0, nullptr, nullptr, 0, 0, nullptr, nullptr);

      SR::SettingsData settings_data;
      settings_data.output_width = job.width;
      settings_data.output_height = job.height;
      settings_data.render_width = job.width;
      settings_data.render_height = job.height;
      settings_data.dynamic_resolution = false;
      settings_data.hdr = false;                          // Gamma space LDR (the scene composite tonemaps it)
      settings_data.inverted_depth = true;                // Reverse Z, infinite far
      settings_data.mvs_jittered = false;                 // Our motion vectors subtract the jitter
      settings_data.mvs_x_scale = motion_vectors_scale_x; // They're in pixels, current to previous
      settings_data.mvs_y_scale = motion_vectors_scale_y;
      sr_implementations[device_data.sr_type]->UpdateSettings(sr_instance_data, native_device_context, settings_data);

      SR::SuperResolutionImpl::DrawData draw_data;
      draw_data.source_color = input.get();
      draw_data.output_color = output.get();
      draw_data.motion_vectors = job.motion_vectors.get();
      draw_data.depth_buffer = job.depth.get();
      draw_data.exposure = exposure.get();
      draw_data.render_width = job.width;
      draw_data.render_height = job.height;
      draw_data.jitter_x = job.jitter_x * jitter_scale_x;
      draw_data.jitter_y = job.jitter_y * jitter_scale_y;
      draw_data.vert_fov = job.vertical_fov;
      draw_data.near_plane = job.near_plane;
      draw_data.far_plane = job.far_plane;
      draw_data.reset = device_data.force_reset_sr;
      const bool drawn = sr_implementations[device_data.sr_type]->Draw(sr_instance_data, native_device_context, draw_data);

      compute_state.Restore(native_device_context); // Also unbinds SR's output, which SR wrote as a UAV, before it's read
      if (drawn)
      {
         DrawCustomPixelShader(native_device_context, device_data.default_depth_stencil_state.get(), output_blend_state, nullptr, copy_vs.get(), copy_ps.get(), output_srv.get(), job.target_rtv.get(), job.width, job.height, true, "MGSV SR Output");
      }
      graphics_state.Restore(native_device_context);
      if (drawn)
      {
         game_device_data.sr_drawn = true;
         device_data.has_drawn_sr = true;
      }
      CountSRResult(drawn);
   }

   // Fires on "FinishCommandList" ("cmd_list" is the new command list, "secondary_cmd_list" the deferred context that recorded it) and on
   // "ExecuteCommandList" ("cmd_list" is the immediate context, "secondary_cmd_list" the command list), before the real call
   static void OnSRCommandList(reshade::api::command_list* cmd_list, reshade::api::command_list* secondary_cmd_list)
   {
      SKIP_UNSUPPORTED_DEVICE_API(cmd_list->get_device()->get_api());

      ID3D11DeviceChild* const device_child = reinterpret_cast<ID3D11DeviceChild*>(cmd_list->get_native());
      com_ptr<ID3D11CommandList> finished_command_list;
      if (SUCCEEDED(device_child->QueryInterface(&finished_command_list)))
      {
         ID3D11DeviceContext* const deferred_context = reinterpret_cast<ID3D11DeviceContext*>(secondary_cmd_list->get_native());
         const std::lock_guard lock(g_sr_jobs_mutex);
         const auto it = g_sr_jobs_by_context.find(deferred_context);
         if (it != g_sr_jobs_by_context.end())
         {
            g_sr_jobs_by_command_list[finished_command_list.get()] = std::move(it->second);
            g_sr_jobs_by_context.erase(it);
         }
         return;
      }

      com_ptr<ID3D11DeviceContext> native_device_context;
      if (FAILED(device_child->QueryInterface(&native_device_context)) || native_device_context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE)
         return;
      ID3D11CommandList* const command_list = reinterpret_cast<ID3D11CommandList*>(secondary_cmd_list->get_native());
      SRJob job;
      {
         const std::lock_guard lock(g_sr_jobs_mutex);
         const auto it = g_sr_jobs_by_command_list.find(command_list);
         if (it == g_sr_jobs_by_command_list.end())
            return;
         job = std::move(it->second);
         g_sr_jobs_by_command_list.erase(it);
      }
      // Restoring the state keeps the game's own immediate context state as it was around its call
      native_device_context->ExecuteCommandList(job.partial_command_list.get(), TRUE);
      DrawSR(native_device_context.get(), *cmd_list->get_device()->get_private_data<DeviceData>(), job);
   }
#endif

public:
#if ENABLE_SR
   static void UnregisterEvents()
   {
      reshade::unregister_event<reshade::addon_event::map_buffer_region>(OnMapSceneCB);
      reshade::unregister_event<reshade::addon_event::unmap_buffer_region>(OnUnmapSceneCB);
      reshade::unregister_event<reshade::addon_event::execute_secondary_command_list>(OnSRCommandList);
   }
#endif

   void OnCreateDevice(ID3D11Device* native_device, DeviceData& device_data) override
   {
#if ENABLE_SAFETYHOOK
      InstallDeviceCheckFix();
#endif
      device_data.game = new GameDeviceDataMetalGearSolidV;
#if ENABLE_SR
      D3D11_BLEND_DESC blend_desc = {};
      D3D11_RENDER_TARGET_BLEND_DESC& target_blend = blend_desc.RenderTarget[0];
      target_blend.SrcBlend = target_blend.SrcBlendAlpha = D3D11_BLEND_ONE;
      target_blend.DestBlend = target_blend.DestBlendAlpha = D3D11_BLEND_ZERO;
      target_blend.BlendOp = target_blend.BlendOpAlpha = D3D11_BLEND_OP_ADD;
      target_blend.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN | D3D11_COLOR_WRITE_ENABLE_BLUE;
      native_device->CreateBlendState(&blend_desc, &GetGameDeviceData(device_data).color_only_blend_state);
#endif
   }

   // "GameDeviceData" has no virtual destructor, so delete it as what it is
   void OnDestroyDeviceData(DeviceData& device_data) override
   {
#if ENABLE_SR
      {
         const std::lock_guard lock(g_sr_jobs_mutex);
         g_sr_jobs_by_context.clear();
         g_sr_jobs_by_command_list.clear();
      }
#endif
      delete static_cast<GameDeviceDataMetalGearSolidV*>(device_data.game);
      device_data.game = nullptr;
   }

   void OnInit(bool async) override
   {
      // We replace no shaders. Luma's per pass data cbuffer carries the camera to our own passes (b12, free in the game).
      luma_settings_cbuffer_index = -1;
      luma_data_cbuffer_index = 12;
      luma_ui_cbuffer_index = -1;

#if ENABLE_SR
      native_shaders_definitions.emplace(CompileTimeStringHash("MGSV Decode Motion Vectors"), ShaderDefinition{"Luma_MGSV_DecodeMotionVectors", reshade::api::pipeline_subobject_type::compute_shader});

      static bool registered_events = false;
      if (!registered_events)
      {
         registered_events = true;
         reshade::register_event<reshade::addon_event::map_buffer_region>(OnMapSceneCB);
         reshade::register_event<reshade::addon_event::unmap_buffer_region>(OnUnmapSceneCB);
         reshade::register_event<reshade::addon_event::execute_secondary_command_list>(OnSRCommandList);
      }
#endif

#if DEVELOPMENT
      // For the MCP "luma_dev_values" tool
      Mcp::RegisterToggles({{"skip_fxaa", &g_skip_fxaa}, {"jitter", &g_jitter_enable}, {"decode_motion_vectors", &g_decode_motion_vectors},
         {"sr_flip_motion_vectors_x", &g_flip_motion_vectors_x}, {"sr_flip_motion_vectors_y", &g_flip_motion_vectors_y},
         {"sr_flip_jitter_x", &g_flip_jitter_x}, {"sr_flip_jitter_y", &g_flip_jitter_y}});
      Mcp::RegisterTextures({MCP_GAME_TEXTURE("mgsv.motion_vectors", motion_vectors), MCP_GAME_TEXTURE("mgsv.depth", depth), MCP_GAME_TEXTURE("mgsv.sr_input", sr_input), MCP_GAME_TEXTURE("mgsv.sr_output", sr_output)});
      Mcp::RegisterCounters({{"fxaa_draws", &g_fxaa_draws_last_frame}, {"velocity_draws", &g_velocity_draws_last_frame},
         {"scene_cb_uploads", &g_scene_cb_uploads_last_frame}, {"jittered_uploads", &g_jittered_uploads_last_frame},
         {"new_frames", &g_new_frames_last_frame}, {"invalid_frame_keys", &g_invalid_frame_keys_last_frame},
         {"fxaa_records", &g_fxaa_records}, {"fxaa_thread_had_main", &g_fxaa_thread_had_main},
         {"fxaa_thread_matches_velocity", &g_fxaa_thread_matches_velocity}, {"fxaa_velocity_is_latest", &g_fxaa_velocity_is_latest},
         {"fxaa_phase_steps", &g_fxaa_phase_steps}, {"fxaa_phase_repeats", &g_fxaa_phase_repeats}, {"fxaa_phase_skips", &g_fxaa_phase_skips}});
#endif
   }

   DrawOrDispatchOverrideType OnDrawOrDispatch(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, reshade::api::shader_stage stages, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, bool is_custom_pass, bool& updated_cbuffers, std::function<void()>* original_draw_dispatch_func) override
   {
#if ENABLE_SR
      UnjitterOutlines(native_device_context, device_data, original_shader_hashes);
#endif
      if (original_shader_hashes.Contains(kFXAAHash, reshade::api::shader_stage::pixel))
      {
#if DEVELOPMENT || TEST
         g_fxaa_draws++;
         LogNewFXAATarget(native_device_context);
#endif
#if DEVELOPMENT
         // Per FXAA interval: "new_frames" should average 1 (one new camera state per frame)
         g_scene_cb_uploads_last_frame = g_scene_cb_uploads.exchange(0);
         g_jittered_uploads_last_frame = g_jittered_uploads.exchange(0);
         g_new_frames_last_frame = g_new_frames.exchange(0);
         g_invalid_frame_keys_last_frame = g_invalid_frame_keys.exchange(0);

         // The jitter diagnostics (see "g_fxaa_records"), restarted each time the jitter turns on
         const bool jitter = g_jitter_enable || SrUsable(device_data);
         static bool jitter_was_enabled = false;
         static uint32_t previous_velocity_phase = UINT32_MAX;
         if (jitter && !jitter_was_enabled)
         {
            g_fxaa_records = g_fxaa_thread_had_main = g_fxaa_thread_matches_velocity = g_fxaa_velocity_is_latest = 0;
            g_fxaa_phase_steps = g_fxaa_phase_repeats = g_fxaa_phase_skips = 0;
            previous_velocity_phase = UINT32_MAX;
            const std::lock_guard lock(g_frame_phases_mutex);
            g_logged_keys = 0;
         }
         jitter_was_enabled = jitter;
         if (jitter)
         {
            const uint32_t velocity_frame = g_last_velocity_frame;
            const uint32_t velocity_phase = g_last_velocity_phase;
            g_fxaa_records++;
            if (t_main_upload_since_fxaa)
            {
               g_fxaa_thread_had_main++;
               if (t_last_main_frame == velocity_frame)
                  g_fxaa_thread_matches_velocity++;
            }
            if (velocity_frame == g_latest_frame)
               g_fxaa_velocity_is_latest++;
            if (previous_velocity_phase != UINT32_MAX && velocity_phase != UINT32_MAX)
            {
               const uint32_t phases = SR::GetDefaultJitterPhases();
               const uint32_t step = (velocity_phase + phases - previous_velocity_phase) % phases;
               if (step == 1)
                  g_fxaa_phase_steps++;
               else if (step == 0)
                  g_fxaa_phase_repeats++;
               else
                  g_fxaa_phase_skips++;
            }
            previous_velocity_phase = velocity_phase;
            t_main_upload_since_fxaa = false;
         }

         // The FXAA target already holds the tonemapped image (FXAA reads a copy of it), so skipping it leaves that unchanged
         if (g_skip_fxaa)
            return DrawOrDispatchOverrideType::Skip;
#endif
#if ENABLE_SR
         // SR replaces FXAA when this thread decoded this frame's motion vectors (no velocity pass, no SR: FXAA stays)
         const bool decoded = t_decoded_motion_vectors;
         t_decoded_motion_vectors = false;
         const BloomSplit bloom_split = t_bloom_split;
         t_bloom_split = BloomSplit::None;
         if (decoded && bloom_split == BloomSplit::Done)
            return DrawOrDispatchOverrideType::Skip; // SR ran before bloom
         if (SrUsable(device_data))
         {
            if (decoded && SplitForSR(native_device_context, device_data))
            {
               if (bloom_split == BloomSplit::None)
                  LogSRFallback(SRFallback::NoBloomReduction, "no bloom reduction on this frame's thread");
               return DrawOrDispatchOverrideType::Skip;
            }
            g_fxaa_over_jitter = true; // The scene uploads were jittered for SR
         }
#endif
      }
      else if (original_shader_hashes.Contains(kBloomReductionHash, reshade::api::shader_stage::pixel))
      {
#if ENABLE_SR
         // The SR point (see "kBloomReductionHash"). The reduction draws twice a frame: the first one splits.
         if (t_decoded_motion_vectors && t_bloom_split == BloomSplit::None && SrUsable(device_data))
         {
            t_bloom_split = SplitBeforeBloom(native_device, native_device_context, device_data) ? BloomSplit::Done : BloomSplit::Failed;
            if (t_bloom_split == BloomSplit::Failed)
               LogSRFallback(SRFallback::BloomSplitImpossible, "couldn't split at the bloom reduction");
         }
#endif
      }
      else if (original_shader_hashes.Contains(kVelocityHash, reshade::api::shader_stage::pixel))
      {
#if DEVELOPMENT || TEST
         g_velocity_draws++;
#endif
#if ENABLE_SR
         g_presents_since_velocity = 0;
         bool decode = SrUsable(device_data);
#if DEVELOPMENT
         decode |= g_decode_motion_vectors;
#endif
         t_bloom_split = BloomSplit::None; // A new frame on this thread
         if (decode && t_velocity_camera.valid)
         {
            t_decoded_motion_vectors = DecodeMotionVectors(native_device, native_device_context, cmd_list_data, device_data);
         }
#endif
      }
      return DrawOrDispatchOverrideType::None;
   }

#if ENABLE_SR
   // Only our motion vector decode uses the game data: the camera of the frame being recorded on this thread
   void UpdateLumaInstanceDataCB(CB::LumaInstanceDataPadded& data, CommandListData& cmd_list_data, DeviceData& device_data) override
   {
      if (t_velocity_camera.valid)
      {
         data.GameData = t_velocity_camera.data;
      }
   }
#endif

   void OnPresent(ID3D11Device* native_device, DeviceData& device_data) override
   {
#if ENABLE_SR
      // A frame without SR (no velocity pass, SR just turned on...) resets SR's history on the next SR frame
      auto& game_device_data = GetGameDeviceData(device_data);
      // Picking another SR type gives it its own chance (e.g. FSR 3 after FSR 4 failed)
      if (device_data.sr_type != game_device_data.sr_type_at_present)
      {
         game_device_data.sr_type_at_present = device_data.sr_type;
         g_sr_failures_in_a_row = 0;
         if (g_sr_disabled.exchange(false))
         {
            reshade::log::message(reshade::log::level::info, "MGSV: SR re-enabled (another Super Resolution type was picked)");
         }
      }
      const bool sr_drawn = game_device_data.sr_drawn.exchange(false);
      device_data.force_reset_sr = !sr_drawn;
      // A frame shown with FXAA over the jitter shakes: if SR keeps not taking FXAA's place (e.g. FXAA's target isn't at the render size),
      // SR and its jitter stop
      if (g_fxaa_over_jitter.exchange(false) && !sr_drawn)
      {
         if (++g_sr_failures_in_a_row >= 3)
            DisableSR("SR couldn't replace FXAA 3 frames in a row");
      }
      if (g_presents_since_velocity < UINT32_MAX / 2)
         g_presents_since_velocity++;
      const uint32_t presents = ++g_presents;
      // Split command lists the game never finished or executed (they hold references to the game's resources)
      {
         size_t dropped = 0;
         {
            const std::lock_guard lock(g_sr_jobs_mutex);
            dropped += std::erase_if(g_sr_jobs_by_context, [presents](const auto& job)
               { return presents - job.second.present_index > 8; });
            dropped += std::erase_if(g_sr_jobs_by_command_list, [presents](const auto& job)
               { return presents - job.second.present_index > 8; });
         }
         if (dropped != 0)
         {
            DisableSR("a split command list was never executed");
         }
      }
#endif
#if DEVELOPMENT || TEST
      const uint32_t fxaa_draws = g_fxaa_draws.exchange(0);
      const uint32_t velocity_draws = g_velocity_draws.exchange(0);
      if ((fxaa_draws != g_fxaa_draws_last_frame || velocity_draws != g_velocity_draws_last_frame) && g_logged_count_changes < kMaxLoggedCountChanges)
      {
         g_logged_count_changes++;
         reshade::log::message(reshade::log::level::info, std::format("MGSV: per frame draws changed: FXAA {} -> {}, velocity {} -> {}", g_fxaa_draws_last_frame, fxaa_draws, g_velocity_draws_last_frame, velocity_draws).c_str());
      }
      g_fxaa_draws_last_frame = fxaa_draws;
      g_velocity_draws_last_frame = velocity_draws;
#endif
   }

#if DEVELOPMENT || TEST
   void PrintImGuiInfo(const DeviceData& device_data) override
   {
      ImGui::Text("FXAA draws (last frame): %u", g_fxaa_draws_last_frame);
      ImGui::Text("Velocity draws (last frame): %u", g_velocity_draws_last_frame);
      if (g_fxaa_draws_last_frame != 0 && g_velocity_draws_last_frame == 0)
      {
         ImGui::TextUnformatted("No velocity: SR needs Motion Blur on and Post-Processing High or higher");
      }
#if DEVELOPMENT
      ImGui::Text("Scene cbuffer uploads (last frame): %u, jittered: %u, new frames: %u, invalid frame keys: %u", g_scene_cb_uploads_last_frame, g_jittered_uploads_last_frame, g_new_frames_last_frame, g_invalid_frame_keys_last_frame);
#endif
   }
#endif

#if DEVELOPMENT
   void DrawImGuiDevSettings(DeviceData& device_data) override
   {
      ImGui::Checkbox("Skip FXAA", &g_skip_fxaa);
      ImGui::Checkbox("Jitter without SR (Halton 2,3, 8 phases)", &g_jitter_enable);
      ImGui::Checkbox("Decode motion vectors without SR", &g_decode_motion_vectors);
      ImGui::Checkbox("SR: flip motion vectors X", &g_flip_motion_vectors_x);
      ImGui::Checkbox("SR: flip motion vectors Y", &g_flip_motion_vectors_y);
      ImGui::Checkbox("SR: flip jitter X", &g_flip_jitter_x);
      ImGui::Checkbox("SR: flip jitter Y", &g_flip_jitter_y);
   }
#endif

   void PrintImGuiAbout() override
   {
      ImGui::PushTextWrapPos(0.f);
      ImGui::Text(
         "Luma for \"Metal Gear Solid V: The Phantom Pain\" is developed by PetarBadzhakov and is open source and free.\n"
         "It replaces the game's FXAA with FSR 4, FSR 3 or DLSS at native resolution.\n"
         "Thanks to the Luma team and contributors.");
      ImGui::PopTextWrapPos();

      ImGui::NewLine();
      static const std::string social_link = std::string("Join our \"HDR Den\" Discord ") + std::string(ICON_FK_SEARCH);
      if (ImGui::Button(social_link.c_str()))
      {
         // Unique link for Luma's HDR Den (tracks the origin of people joining); do not share for other purposes.
         static const std::string discord_link = std::string("https://discord.gg/J9fM") + std::string("3EVuEZ");
         ShellExecuteA(nullptr, "open", discord_link.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
      }
      static const std::string contributing_link = std::string("Contribute on Github ") + std::string(ICON_FK_FILE_CODE);
      if (ImGui::Button(contributing_link.c_str()))
         ShellExecuteA(nullptr, "open", "https://github.com/Filoppi/Luma-Framework", nullptr, nullptr, SW_SHOWNORMAL);

      ImGui::NewLine();
      ImGui::Text("Build Date: %s %s", __DATE__, __TIME__);

      ImGui::NewLine();
      ImGui::Text("Credits:"
                  "\n\nMain:"
                  "\nPetarBadzhakov"
                  "\n\nAcknowledgments:"
                  "\nghostinthecamera (why the game closes under ReShade)"
                  "\n\nThird Party:"
                  "\nReShade"
                  "\nImGui"
                  "\nSafetyHook"
                  "\nAMD FidelityFX Super Resolution"
                  "\nNVIDIA DLSS",
         "");
   }
};

namespace
{
   // Metal Gear Online ("mgsvmgo.exe") shares the game folder, and thus ReShade, with The Phantom Pain: never load in it
   bool IsMetalGearOnline()
   {
      char path[MAX_PATH] = {};
      GetModuleFileNameA(nullptr, path, MAX_PATH);
      const char* file_name = strrchr(path, '\\');
      return _stricmp(file_name ? file_name + 1 : path, "mgsvmgo.exe") == 0;
   }

   bool attached = false;
} // namespace

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
   if (ul_reason_for_call == DLL_PROCESS_ATTACH)
   {
      // A failed attach makes the loader call us with DLL_PROCESS_DETACH right away, and ReShade logs the failed load
      if (IsMetalGearOnline())
         return FALSE;
      attached = true;

      Globals::SetGlobals(PROJECT_NAME, "Metal Gear Solid V: The Phantom Pain Luma mod");
      Globals::DEVELOPMENT_STATE = Globals::ModDevelopmentState::Playable;

      // AA only (like Mass Effect Andromeda): leave HDR / swapchain / texture formats alone, and never run the per-present
      // display composition pass (it changes the gamma of the game's non upgraded SDR swapchain).
      swapchain_format_upgrade_type = TextureFormatUpgradesType::None;
      swapchain_upgrade_type = SwapchainUpgradeType::None;
      texture_format_upgrades_type = TextureFormatUpgradesType::None;
      force_disable_display_composition = true;

      // Keep the game's fullscreen behaviour as is (preventing it also adds a frame latency waitable object to the swapchain)
      prevent_fullscreen_state = false;

      game = new MetalGearSolidV();
   }
   else if (!attached)
   {
      return TRUE;
   }
#if ENABLE_SR
   else if (ul_reason_for_call == DLL_PROCESS_DETACH)
   {
      MetalGearSolidV::UnregisterEvents();
   }
#endif

   CoreMain(hModule, ul_reason_for_call, lpReserved);

   return TRUE;
}
