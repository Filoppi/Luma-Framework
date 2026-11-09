///////////////////////////////////////////////////////////////////////
// Mafia: Definitive Edition — Luma mod
//
// Goal: replace the game's forced AA with DLAA (DLSS), the way Luma does for
// Mafia II: Definitive Edition and Mafia III.
//
// ============================================================================
// STATUS: release 1.0 - the temporal resolve is replaced by DLSS (DLAA at native res). An optional
// RCAS sharpening pass exists on top of it (off by default: the base image is sharp enough now that
// the motion vector flag is fixed, and sharpening it only brings the noise back).
//
// Passes, identified by "disassembly fingerprint x live binding" (see REPORT.md):
//   TAA resolve  0x37B05605 / 0x7B9B914F   <- the passes we replace
//                3 textures: t0 = jittered HDR colour (R11G11B10_FLOAT), t1 = TAA history,
//                t2 = UV space motion vectors (R16G16B16A16_FLOAT); 3x3 Karis variance clamp.
//                Runs at every quality level.
//   FXAA on R32  0x2B974E48   NOT the colour AA: all four of its FXAA taps read t0, which is a
//                single channel R32_FLOAT auxiliary buffer. Deliberately not hooked - feeding it
//                to DLSS as the colour source is what produced the "heavy noise" report.
//   Position fill 0x121A4A96  geometry pass, depth test AND write -> the main depth is captured
//                from here, because the resolve itself runs with depth testing disabled.
//   Tonemap      0x916B1D65  (same hash as RenoDX's, so independently confirmed)
//   DOF          0xA6CCD59B
//   Bloom bright 0x5C8BCE6A  5-tap mean + max(., 0). Earlier revisions cancelled it as a
//                "post-TAA composite" that supposedly overwrote the SR output - that was wrong
//                and the pass is left alone now.
//   Post-tonemap sharpen 0x747C6210   Do NOT cancel: "mul r0, r0, v1" sits outside its
//                "if (strength > 0)" block, so cancelling the draw removes a per pixel multiply
//                and turns the screen black. To tame it, zero cb0[0].z instead.
//
// Facts that took the longest to establish (do not re-litigate them):
//   - Motion vectors: this game has NO standalone raw MV buffer. The TAA's own t2 is the MV
//     (confirmed by sniffing ~5.1M draws for any full res two channel float texture). It is
//     stored in UV space, so it must be scaled by the render resolution.
//   - Jitter: the TAA pass' PS constant buffer slot 0, floats 32/33, already in pixel space
//     (-0.5..0.5, quantised to an 8 phase Halton sequence). No NDC conversion, no sign flip.
//   - The motion vectors *contain* the per frame jitter delta: the engine bakes the jitter into
//     the projection matrices and the resolve shader does "prevUV = curUV + MV" with no jitter term
//     at all (see sr_mvs_jittered for the disassembly). So the correct "MVJittered" declaration is
//     ON - but the shipping default is OFF, because with DLSS 310.9.x ON comes out softer and noisier
//     while OFF matches the game's own TAA in sharpness at the cost of some edge aliasing. Both
//     sides of that trade are measured and written up at "sr_mvs_jittered"; do not flip it back
//     from the disassembly alone, and do not assume OFF is a bug.
//   - The resolve runs TWICE per frame, for different parts of the image (the same shader hash
//     both times; Mafia III's mod documents the same thing with "this happens 3 times"). The
//     second call covers the character / skin / transparency layer. If it is left to run, the
//     game's own TAA re-resolves that layer on top of our output, which is why faces and hands
//     showed no DLAA and no NR effect while the rest of the frame did. cancel_second_taa (on by
//     default) cancels it: DLSS writes the whole frame, so those pixels keep the SR result.
//   - Depth is reversed-Z - **verified**, not assumed: the main geometry pass uses
//     DepthFunc = D3D11_COMPARISON_GREATER_EQUAL (see the one-shot probe: "DepthFunc=7 (GREATER_EQUAL)
//     ... reversed-Z = yes"), so sr_inverted_depth must stay 1. The main depth lives in
//     a 3840x2160 single slice R24G8_TYPELESS target, and the capture from the position fill pass
//     MUST be validated: that pass is a bare "mov o0.xyzw, v0.xyzw" which the engine reuses for
//     depth-only shadow draws, whose target is a 2048x2048 R16_TYPELESS atlas of two slices.
//   - DLAA keeps visibly more noise than the game's TAA. That is inherent, not a bug: the TAA
//     resolves the scene's stochastic lighting noise with real temporal samples, and DLAA cannot
//     beat that without giving the sharpness back. RCAS on top is the accepted trade-off.
//
// DLAA only: keep the game's output format and colour pipeline. Luma does not add HDR output.
//
// Safety: if DLSS refuses the input set, or the AA pass' inputs do not look like what we
// expect, the hook returns without replacing anything and the game runs its own AA that frame.
// ============================================================================
//
// Verified facts about this game:
//   - Fusion Engine (a tweaked Mafia III engine), DX11, x64 only
//   - TAA/AA is always on (Low/Medium/High, no in-game way to turn it off)
//   - Shaders are fully stripped: no RDEF reflection, no names, so every pass has to be
//     identified by its binary hash
//   - Nothing is shared with Mafia III at the binary level (0/24 of Luma's Mafia III shader
//     hashes appear here)
///////////////////////////////////////////////////////////////////////

// Specify a define with the name of the game here (GAME_*).
#define GAME_MAFIA_DEFINITIVE_EDITION 1

// REQUIRED for coexistence with OptiScaler.
//
// OptiScaler injected into a DX11 game can run its upscaling/frame generation through a D3D12
// device (Upscalers.Dx11Upscaler = dlss_12, or the "Dx11withDx12" mode). Luma only understands
// D3D11, so it used to assert and then crash on those foreign device objects:
//   core.hpp:2656  "api == reshade::api::device_api::d3d11"
//   core.hpp:5470  "ASSERT_ONCE(false)"   (the command list was a D3D12 one)
// Both come from the same root cause and both are fixed by this one define, which makes the
// core's SKIP_UNSUPPORTED_DEVICE_API() guards (50 call sites) return early for non D3D11 calls
// instead of asserting. This is the fix the assert message itself recommends.
// Note: it only guards against foreign APIs, it does not change D3D11 behaviour.
#define CHECK_GRAPHICS_API_COMPATIBILITY 1

//// Define all the global "core" defines before including its files: ////

// Enable support for Geometry Shaders (GS), rather rare.
#define GEOMETRY_SHADER_SUPPORT 0

// Enable to stop the "You can now attach the debugger" popup.
//
// Must stay 1. In a DEVELOPMENT build this otherwise pops a modal MessageBoxA with
// MB_YESNOCANCEL from inside DllMain, and "Cancel" (which is what pressing Esc sends) calls
// exit(0). That turns into ERROR_DLL_INIT_FAILED (1114) with the game still running normally,
// which is extremely hard to trace back to what looks like an unrelated dialog.
#define DISABLE_AUTO_DEBUGGER 1

// Keep the game's swap effect; this mod does not need an HDR swapchain upgrade.
#define DISABLE_SWAPCHAIN_FLIP_MODEL 1

// UseLumaNGX is enabled in the .vcxproj, so the DLSS/DLAA code paths are compiled in.

#include "..\..\Core\core.hpp"
#include "taa_inputs.h"

#include <fstream>
#include <mutex>
#include <sstream>
#include <string>

///////////////////////////////////////////////////////////////////////
// Pass identification (filled in OnInit)
///////////////////////////////////////////////////////////////////////
// The temporal resolves. Both variants are hooked so the mod keeps working whatever quality
// level the user picks. 0x2B974E48 is deliberately NOT in this list (FXAA over R32, see header).
ShaderHashesList shader_hashes_AA;
// Geometry pass that fills the per-pixel position buffer. Used to grab the main depth buffer,
// because the AA pass itself runs with depth testing disabled and has no DSV bound.
ShaderHashesList shader_hashes_PositionBufferFill;
// The game's own post-tonemap sharpening pass. We never replace it and never cancel it: its
// "cb0[0].z" is the strength the game authored to compensate for TAA softness, which is exactly the
// knob worth scaling once the resolve is replaced by DLSS. See "ScaleGameSharpenStrength".
ShaderHashesList shader_hashes_GameSharpen;
// Reference points, not replaced.
ShaderHashesList shader_hashes_Tonemap;
ShaderHashesList shader_hashes_DepthOfField;

// This module's own HMODULE, kept so OnLoad can report it. ReShade's "invalid module handle" error
// means the handle that reached ReShadeRegisterAddon was null or already taken, and the only way to
// tell which from this mod's own log is to have the number here.
static HMODULE g_own_module = nullptr;

// The persisted value of the MVJittered declaration. It needs a global because LoadConfigs() runs
// on the Game object, before any device data exists, while the setting itself lives in the per
// device data ("sr_mvs_jittered", which carries the long explanation). OnCreateDevice() copies it
// across. The panel writes both so the two never drift.
static int g_mvs_jittered_ini = 0; // 0 = OFF, which is also the shipping default when the key is absent

// Fill the pass hash lists during static initialisation rather than in Game::OnInit.
// Game::OnInit() *is* called (core.hpp:16137), but it runs on a provider thread and its ordering
// relative to the first draws is not something to depend on: an empty list silently turns every
// hook below into a no-op, which is exactly the kind of failure that is invisible in the log.
static struct PassHashesInit
{
   PassHashesInit()
   {
      // Only the real temporal resolves are hooked. 0x2B974E48 used to be in this list and turned
      // out to be a red herring: all four of its FXAA taps read t0, and t0 is a *single channel*
      // R32_FLOAT buffer, so it is FXAA over an auxiliary buffer, not the colour AA. Replacing it
      // with DLSS produced heavy noise, which is exactly what was reported at the time.
      shader_hashes_AA.pixel_shaders = {
         static_cast<uint32_t>(std::stoul("37B05605", nullptr, 16)), // TAA resolve, 3 textures (colour/history/MV)
         static_cast<uint32_t>(std::stoul("7B9B914F", nullptr, 16)), // TAA resolve, second variant
      };
      shader_hashes_PositionBufferFill.pixel_shaders = {
         static_cast<uint32_t>(std::stoul("121A4A96", nullptr, 16)),
      };
      shader_hashes_Tonemap.pixel_shaders = {
         static_cast<uint32_t>(std::stoul("916B1D65", nullptr, 16)),
      };
      shader_hashes_GameSharpen.pixel_shaders = {
         static_cast<uint32_t>(std::stoul("747C6210", nullptr, 16)),
      };
      shader_hashes_DepthOfField.pixel_shaders = {
         static_cast<uint32_t>(std::stoul("A6CCD59B", nullptr, 16)),
      };
   }
} s_pass_hashes_init;

///////////////////////////////////////////////////////////////////////
// Logging
///////////////////////////////////////////////////////////////////////
namespace Probe
{
   static std::mutex s_mutex;
   static std::ofstream s_log;
   static int s_lines = 0;
   // ONE file, overwritten at the start of every run, with a cap that announces itself when it is
   // reached. Nothing is ever renamed or deleted.
   //
   // A rollover scheme was tried here first (segments kept as ".1"/".2"/".3", the oldest removed)
   // and it is deliberately gone: of the ~63 mods in this framework only two write a log file of
   // their own, both with a plain append, and a mod that renames and deletes files inside the
   // user's game folder is not a habit worth starting. What the rollover was there for is kept in
   // a cheaper form - the cap is far above what a release build can reach, and if it is ever hit,
   // the file says so instead of going quiet.
   //
   // A release build emits about one line every five seconds (the "[frame]" heartbeat is already
   // rate limited to 300 frames) plus a handful of one-off events, so this is many hours of play
   // and roughly seven megabytes in the pathological case.
   static constexpr int kLineCap = 50000;
   static bool s_cap_reported = false;

   static int s_frame = 0;
   static long long s_draw_calls = 0; // every OnDrawOrDispatch call, to tell "not called" apart from "no match"

   // Resolve our own module handle without needing DllMain to have run yet.
   static HMODULE SelfModule()
   {
      static HMODULE module = nullptr;
      if (module == nullptr)
      {
         ::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&SelfModule), &module);
      }
      return module;
   }

   // Log next to the add-on itself rather than relying on the process working directory.
   static std::filesystem::path LogPath()
   {
      wchar_t buffer[MAX_PATH] = {};
      const DWORD length = ::GetModuleFileNameW(SelfModule(), buffer, MAX_PATH);
      if (length == 0 || length >= MAX_PATH)
      {
         return std::filesystem::path(L"LumaMafiaDE-probe.log");
      }
      std::filesystem::path path(buffer);
      path.replace_filename(L"LumaMafiaDE-probe.log");
      return path;
   }

   static const char* FormatName(DXGI_FORMAT format)
   {
      switch (format)
      {
      case DXGI_FORMAT_UNKNOWN:
         return "UNKNOWN";
      case DXGI_FORMAT_R32G32B32A32_TYPELESS:
         return "R32G32B32A32_TYPELESS";
      case DXGI_FORMAT_R32G32B32A32_FLOAT:
         return "R32G32B32A32_FLOAT";
      case DXGI_FORMAT_R16G16B16A16_TYPELESS:
         return "R16G16B16A16_TYPELESS";
      case DXGI_FORMAT_R16G16B16A16_FLOAT:
         return "R16G16B16A16_FLOAT";
      case DXGI_FORMAT_R10G10B10A2_UNORM:
         return "R10G10B10A2_UNORM";
      case DXGI_FORMAT_R11G11B10_FLOAT:
         return "R11G11B10_FLOAT";
      case DXGI_FORMAT_R8G8B8A8_TYPELESS:
         return "R8G8B8A8_TYPELESS";
      case DXGI_FORMAT_R8G8B8A8_UNORM:
         return "R8G8B8A8_UNORM";
      case DXGI_FORMAT_B8G8R8A8_UNORM:
         return "B8G8R8A8_UNORM";
      case DXGI_FORMAT_R32G32_TYPELESS:
         return "R32G32_TYPELESS";
      case DXGI_FORMAT_R32_FLOAT:
         return "R32_FLOAT";
      case DXGI_FORMAT_R32G32_FLOAT:
         return "R32G32_FLOAT";
      case DXGI_FORMAT_R16G16_FLOAT:
         return "R16G16_FLOAT";
      case DXGI_FORMAT_R16_FLOAT:
         return "R16_FLOAT";
      case DXGI_FORMAT_R16_TYPELESS:
         return "R16_TYPELESS";
      case DXGI_FORMAT_D32_FLOAT:
         return "D32_FLOAT";
      case DXGI_FORMAT_R24G8_TYPELESS:
         return "R24G8_TYPELESS";
      case DXGI_FORMAT_D24_UNORM_S8_UINT:
         return "D24_UNORM_S8_UINT";
      case DXGI_FORMAT_R8_UNORM:
         return "R8_UNORM";
      default:
         return nullptr;
      }
   }

   static std::string Describe(ID3D11Resource* resource)
   {
      if (resource == nullptr)
      {
         return "(null)";
      }

      com_ptr<ID3D11Texture2D> texture_2d;
      if (SUCCEEDED(resource->QueryInterface(&texture_2d)) && texture_2d.get())
      {
         D3D11_TEXTURE2D_DESC desc = {};
         texture_2d->GetDesc(&desc);
         const char* name = FormatName(desc.Format);
         return std::format("{}x{} {}({}) mips={} bind=0x{:X}", desc.Width, desc.Height,
            name ? name : "?", static_cast<int>(desc.Format), desc.MipLevels, desc.BindFlags);
      }
      return "(other)";
   }

   static void Line(const std::string& text)
   {
      std::lock_guard<std::mutex> lock(s_mutex);
      if (!s_log.is_open())
      {
         s_log.open(LogPath(), std::ios::out | std::ios::trunc);
         if (s_log.is_open())
         {
            s_log << "Mafia: Definitive Edition Luma mod - probe log\n\n";
         }
      }
      if (!s_log.is_open())
      {
         return;
      }
      if (s_lines >= kLineCap)
      {
         // Said once, in the file itself. Silently returning here is what used to make a session's
         // tail disappear without anyone noticing.
         if (!s_cap_reported)
         {
            s_cap_reported = true;
            s_log << "[line cap " << kLineCap << " reached, further output suppressed]\n";
            s_log.flush();
         }
         return;
      }
      s_log << text << '\n';
      s_log.flush();
      ++s_lines;
   }

#if DEVELOPMENT
   // --- per frame pixel shader set ---------------------------------------------------------------
   // This answers the two questions the rest of the log cannot. One: the game's AA has Low, Medium
   // and High, DLSS only takes effect at Low, so something else must be resolving the image at the
   // other two - and if that something else is a shader this mod has never hooked, it shows up here
   // as a hash that was not in the set before. Two: a cutscene shows nothing on the DLSS indicator,
   // which suggests its resolves are simply never drawn; the set for a cutscene frame tells us
   // whether that is true and what does run instead.
   //
   // The set is rebuilt every frame, dumped whenever it changes (rate limited, so a scene whose LOD
   // churns cannot flood the log), and a hash that has never been seen in this session is reported
   // on its own line the instant it appears. That last one is the event that matters and it must
   // not be buried inside a dump of a hundred hashes.
   static constexpr int kFrameHashMax = 256;
   static uint32_t s_frame_hashes[kFrameHashMax];
   static int s_frame_hash_count = 0;
   static uint32_t s_seen_hashes[4096];
   static int s_seen_hash_count = 0;
   static char s_new_hashes[512] = {};
   static int s_new_hash_count = 0;
   static uint32_t s_dumped_hashes[kFrameHashMax];
   static int s_dumped_count = -1; // -1 = nothing dumped yet
   static int s_dumped_frame = -1000000;
   static constexpr int kSetDumpIntervalFrames = 90; // at most one set dump per ~1.5 s

   // Called for every draw. Lock free on purpose: it is one linear scan over the ~50 distinct
   // shaders a frame uses, and the render thread is the only writer.
   static void NotePixelShader(uint32_t hash)
   {
      if (hash == 0)
      {
         return;
      }
      for (int i = 0; i < s_frame_hash_count; ++i)
      {
         if (s_frame_hashes[i] == hash)
         {
            return;
         }
      }
      if (s_frame_hash_count < kFrameHashMax)
      {
         s_frame_hashes[s_frame_hash_count++] = hash;
      }
   }

   static bool ContainsHash(const uint32_t* list, int count, uint32_t hash)
   {
      for (int i = 0; i < count; ++i)
      {
         if (list[i] == hash)
         {
            return true;
         }
      }
      return false;
   }

   static void EndFrameShaderSet()
   {
      if (s_frame_hash_count == 0)
      {
         return;
      }

      // First sight of a shader in this session, reported immediately: if changing a setting makes
      // the game pick a different resolve, this line is the answer.
      for (int i = 0; i < s_frame_hash_count; ++i)
      {
         const uint32_t hash = s_frame_hashes[i];
         if (ContainsHash(s_seen_hashes, s_seen_hash_count, hash))
         {
            continue;
         }
         if (s_seen_hash_count < (int)(sizeof(s_seen_hashes) / sizeof(s_seen_hashes[0])))
         {
            s_seen_hashes[s_seen_hash_count++] = hash;
         }
         if (s_new_hash_count < 32)
         {
            snprintf(s_new_hashes + strlen(s_new_hashes), sizeof(s_new_hashes) - strlen(s_new_hashes), " %08X", hash);
            ++s_new_hash_count;
         }
      }
      if (s_new_hash_count > 0)
      {
         Line(std::format("[shader] {} first seen this session @frame {}:{}", s_new_hash_count, s_frame, s_new_hashes));
         s_new_hashes[0] = 0;
         s_new_hash_count = 0;
      }

      // Insertion sort instead of std::sort so this file needs no extra include; the frame order of
      // the set is not stable and the dump has to be comparable line to line.
      uint32_t sorted[kFrameHashMax];
      memcpy(sorted, s_frame_hashes, sizeof(uint32_t) * s_frame_hash_count);
      for (int i = 1; i < s_frame_hash_count; ++i)
      {
         const uint32_t value = sorted[i];
         int j = i - 1;
         while (j >= 0 && sorted[j] > value)
         {
            sorted[j + 1] = sorted[j];
            --j;
         }
         sorted[j + 1] = value;
      }

      bool changed = (s_dumped_count != s_frame_hash_count);
      for (int i = 0; !changed && i < s_frame_hash_count; ++i)
      {
         changed = (sorted[i] != s_dumped_hashes[i]);
      }
      if (changed && s_frame - s_dumped_frame >= kSetDumpIntervalFrames)
      {
         s_dumped_frame = s_frame;
         s_dumped_count = s_frame_hash_count;
         memcpy(s_dumped_hashes, sorted, sizeof(uint32_t) * s_frame_hash_count);
         std::string list;
         for (int i = 0; i < s_frame_hash_count; ++i)
         {
            list += std::format(" {:08X}", sorted[i]);
         }
         Line(std::format("[set] frame {}: {} pixel shaders:{}", s_frame, s_frame_hash_count, list));
      }

      s_frame_hash_count = 0;
   }
#endif

} // namespace Probe

///////////////////////////////////////////////////////////////////////

#if DEVELOPMENT
// Number of draws recorded before the temporal resolve, so we can see what leads up to it.
static constexpr int kPreTaaRing = 24;
// Number of draws recorded after the tonemapper (the frame tail), where the final image is
// assembled. See the "s_tail_ring" declaration.
static constexpr int kTailRing = 24;
// The frame tail constant buffer snapshot. See "s_post" below.
static constexpr int kPostMaxHashes = 64;
static constexpr int kPostSlots = 2;
static constexpr int kPostFloats = 32;
// The window is counted in frames. An earlier version armed a budget of 400 draws, which at the
// ~330 draws per frame this game issues is barely one frame, so the window always closed before the
// tonemapper was reached and the log could only ever contain first sight baselines.
static constexpr int kPostArmFrames = 900;
// Staging a constant buffer and mapping it stalls the pipeline, so only look at every fourth
// sampled draw. These are per frame constants, nothing is missed by the sampling.
static constexpr int kPostSampleStride = 4;
#endif

// Is this depth resource really the main scene depth? DLSS and DLSS-NR reproject against it, so a
// wrong one is worse than none: the model pairs the wrong pixels and its temporal part degenerates.
//
// The trap this guards against: the position buffer fill pass (0x121A4A96) is a bare
// "mov o0.xyzw, v0.xyzw" with zero resources, so the engine also reuses it for depth-only draws,
// whose DSV is a *shadow cascade atlas*: 2048x2048 R16_TYPELESS with TWO array slices. The old
// code took whatever the last matching draw had bound, so it flip-flopped between the real depth
// (3840x2160 R24G8_TYPELESS, one slice) and that atlas within a single session - visible one
// second apart as the "game depth is R16_TYPELESS (53)" / "R24G8_TYPELESS (44)" lines in
// dlss5-bridge.log. The probe log had only ever printed the depth once per variant, and that
// sample happened to be a good one, which is why it went unnoticed for so long.
//
// The discriminator is size and slice count rather than format: the main depth must be at least
// (almost) the render resolution and a single slice. The format is deliberately not whitelisted,
// so a game whose main depth uses a different depth format would still be accepted.
static bool IsUsableMainDepth(ID3D11Resource* resource, float min_width, float min_height)
{
   if (resource == nullptr)
   {
      return false;
   }
   com_ptr<ID3D11Texture2D> texture;
   if (FAILED(resource->QueryInterface(&texture)) || !texture)
   {
      return false;
   }
   D3D11_TEXTURE2D_DESC desc = {};
   texture->GetDesc(&desc);
   if (desc.ArraySize != 1 || desc.SampleDesc.Count != 1)
   {
      return false;
   }
   return static_cast<float>(desc.Width) >= min_width && static_cast<float>(desc.Height) >= min_height;
}

#if DEVELOPMENT
// Only used by the one-shot depth convention probe below.
static const char* DepthFuncName(D3D11_COMPARISON_FUNC func)
{
   switch (func)
   {
   case D3D11_COMPARISON_NEVER:
      return "NEVER";
   case D3D11_COMPARISON_LESS:
      return "LESS";
   case D3D11_COMPARISON_EQUAL:
      return "EQUAL";
   case D3D11_COMPARISON_LESS_EQUAL:
      return "LESS_EQUAL";
   case D3D11_COMPARISON_GREATER:
      return "GREATER";
   case D3D11_COMPARISON_NOT_EQUAL:
      return "NOT_EQUAL";
   case D3D11_COMPARISON_GREATER_EQUAL:
      return "GREATER_EQUAL";
   case D3D11_COMPARISON_ALWAYS:
      return "ALWAYS";
   default:
      return "?";
   }
}
#endif

struct MafiaDefinitiveEditionGameDeviceData final : public GameDeviceData
{
   // The scene depth, captured from the position fill pass. Only a target that passes
   // IsUsableMainDepth is ever stored, and a rejected candidate never overwrites a good one, so
   // this always holds the last *valid* main depth (the game reuses the same texture every frame).
   com_ptr<ID3D11Resource> main_depth;
   int main_depth_rejected = 0; // how often a depth target was seen and rejected (shadow atlas)
   int main_depth_logged = 0;
   com_ptr<ID3D11Texture2D> dlss_output_color; // UAV enabled copy of the AA target if needed
   // The live resolve bindings, retained for the input diagnostics below.
   com_ptr<ID3D11ShaderResourceView> aa_srv[8];
   // The resolves bind t0 = current HDR colour, t1 = history, t2 = UV space motion vectors.
   // RenoDX can hot-swap both colour and history to FP16; format alone cannot identify a role.
   // R32 inputs are still rejected, including the unsupported menu binding seen in early probes.
   int aa_auto_slots = 1;  // use the verified t0/t2 layout, with descriptor validation
   int aa_source_slot = 0; // manual override, only used when aa_auto_slots is off
   int aa_mv_slot = 2;     // manual override, only used when aa_auto_slots is off
   // The TAA shader computes prevUV = curUV + t2.xy, but DLSS expects the opposite sign
   // convention. A negative mvs scale flips it without needing a pass of our own.
   int mv_scale_sign = 1; // +1 = as the game writes it, -1 = flipped
#if DEVELOPMENT
   // Where the SR draw is triggered. 0 = at the temporal resolve itself (the shipping mode),
   // 1 = at 0xA75736C5, the copy pass that runs right before the TAA (Mafia III style, because
   // that game skips its TAA shaders when TAA is off). Mode 1 turned the screen black:
   // 0xA75736C5 runs several times per frame and the first one is long before the scene is done.
   int dlss_hook_mode = 0;
   // 0x747C6210 is the game's own sharpening pass, running AFTER the tonemap. It is a
   // neighbourhood-variance sharpener (sqrt of the centre, compare with an 8x8 neighbourhood
   // offset table, square the difference and add it back) tuned for the blurry TAA output, so on
   // an already sharp DLSS frame it overshoots. Do NOT cancel the draw to turn it off:
   // "mul r0, r0, v1.xyzw" sits OUTSIDE the "if (sharpen strength > 0)" block, so cancelling
   // also removes a per pixel multiply (exposure/vignette/alpha) and the screen goes black.
   // To tame the sharpening, zero cb0[0].z (the strength) in its constant buffer instead.
   bool skip_game_sharpen = false;
#endif
   int invalid_output_reports = 0;
   // Whether to cancel the resolves after the first one in a frame. This engine resolves the image
   // in *layers*: the same TAA shader is called more than once per frame "for different parts of
   // the image" (Mafia III's own mod says 3 times; this game does it twice), and the second call
   // covers the character / skin / transparency layer.
   //
   // Measured, and it is the important one: with this OFF (the old default), the
   // game's own TAA ran over our DLSS/DLAA output on that second layer, which is why *faces and
   // hands specifically* showed no DLAA and no NR effect at all while the rest of the frame did.
   // Turning it ON made the faces/hands show the DLAA/NR character immediately. DLSS writes the
   // whole frame, so cancelling a later layer resolve leaves those pixels with the SR result
   // rather than losing them. Kept switchable to A/B it (and to watch transparencies for
   // ghosting, as the layer's own TAA history then stops advancing).
   bool cancel_second_taa = true;
#if DEVELOPMENT
   // --- diagnostics: dump the draws that lead up to the resolve (one shot) -----------------
   struct PreTaaEntry
   {
      uint32_t hash = 0;
      char rtv[160] = {};
   };
   PreTaaEntry s_pre_taa_ring[kPreTaaRing] = {};
   int s_pre_taa_ring_pos = 0;
   int s_pre_taa_dumped = 0;
   // Log what the resolves after the first one write, so we know exactly what we are cancelling.
   int extra_resolve_logged = 0;
   // One-shot probe of the main pass' depth comparison function (settles the "Inverted depth" flag).
   int depth_func_logged = 0;
   // --- One shot dump of the frame tail ------------------------------------------------------
   // The game's post-tonemap sharpening was identified as 0x747C6210, but that hash never turns up
   // during gameplay (the "[sharpen]" lines stay empty and the strength scale does nothing), while
   // RenoDX labels the very same hash "loading screen". So either the pass only runs in menus, or
   // the sharpening we are looking for is folded into another pass. This records the draws that
   // happen *after* the tonemapper until Present, which is where the final image is assembled, so
   // the real candidate can be read straight off the log instead of guessed at.
   struct TailEntry
   {
      uint32_t hash = 0;
      char rtv[160] = {};
   };
   TailEntry s_tail_ring[kTailRing];
   int s_tail_ring_pos = 0;
   int s_tail_tonemap_seen = 0;
   int s_tail_dumped = 0;
   // --- One shot, on demand log of the frame tail's constant buffers ------------------------
   // Neither RCAS nor the game's own post-tonemap pass (0x747C6210) recovers the sharpness: scaling
   // that pass' "cb0[0].z" by 20 changed nothing, so the value is a gate rather than an amount, and
   // the tonemapper plus the five other passes that draw on the backbuffer after it are the remaining
   // suspects. This logs the first floats of their pixel shader constant buffer *only when a value
   // changes*, so toggling a setting in the game produces a short readable diff instead of a flood.
   // ("kPostMaxHashes" and friends live at file scope with the other dev constants.)
   //
   // The hardcoded list of suspects this replaced was a dead end twice over: it only covered six
   // hashes picked by hand, and one run of it produced nothing at all, because there is no way to
   // tell "the button was never pressed" apart from "the options menu does not draw these passes".
   // So the logger no longer has a list. It fingerprints *every* pixel shader drawn from the
   // tonemapper onwards, takes a baseline at the start of the window, and at the end prints the
   // passes it saw plus, for each one, exactly which floats moved. One button, one answer.
   struct PostPassLog
   {
      uint32_t hash = 0;
      int draws = 0;             // draws of this shader seen since the arm
      bool from_tonemap = false; // seen on the tonemapper draw itself, not further down the tail
      bool base_set[kPostSlots] = {};
      float base[kPostSlots][kPostFloats] = {};
      float last[kPostSlots][kPostFloats] = {};
   };
   PostPassLog s_post[kPostMaxHashes];
   int s_post_count = 0;
   com_ptr<ID3D11Buffer> cb_post_staging[kPostSlots];
   uint32_t cb_post_staging_size[kPostSlots] = {};
   int s_post_cb_arm = 0;       // nonzero while the panel button's window is open
   int s_post_cb_arm_frame = 0; // the frame it was granted on, the window is counted in frames
   int s_post_tonemap_seen = 0; // the tonemapper has run this frame, so the tail has started
   // --- One shot, automatic: the tonemapper's own constants and inputs -----------------------
   // Reading absolute values never needed a button, and needing one was a reliable way to end up
   // with an empty log: two runs in a row produced nothing because the panel button was not pressed.
   // So this fires on its own as soon as the game is really rendering (the resolve has been hooked
   // a few dozen times, which the loading screen never does), and never again.
   //
   // What it buys, from the disassembly of 0x916B1D65: cb0[3].x is the tonemap operator index,
   // cb0[2].x its gamma exponent, cb1[4]/cb1[5] the Uncharted2 parameters, and t4 is a 1x1 texture
   // that holds the exposure and the white point, i.e. exactly what an HDR replacement needs.
   int s_tm_dump_state = 0; // 0 idle, 1 dumped
   com_ptr<ID3D11Buffer> cb_tm_staging[2];
   uint32_t cb_tm_staging_size[2] = {};
   com_ptr<ID3D11Texture2D> tex_tm_staging;
   com_ptr<ID3D11Texture2D> tex_tm_1x1;
   // --- Dev probe: rewrite the tonemapper's own constants on the way to the draw ---------------
   // Why this is enough to test the whole HDR premise, with no new shader at all. The tonemapper
   // takes exactly two switches that matter (see the disassembly in docs/disasm-916B1D65.txt):
   //   cb0[8].z > 0  -> the dithered sqrt (gamma 2.0) encode runs. Set it to 0 and the shader
   //                    outputs *linear* values instead, which is exactly what Luma's display
   //                    composition wants to be handed.
   //   cb0[3].x      -> the tonemap operator. 0 and 1 clamp to 1 (SDR, what the game uses);
   //                    -1/-2/-3/-4 do not clamp and can carry values above 1.
   // So "SDR, but linear out" is one float, and "HDR" is two. The game rewrites the constant
   // buffer every frame, so it has to be rewritten every frame too; that is why this happens in
   // the draw hook rather than once. UpdateSubresource on a constant buffer is legal from here and
   // returns E_FAIL instead of crashing if the buffer turns out to be immutable.
   int tm_probe_mode = 0; // 0 off, 1 linear out, 2 linear + operator -2, 3 linear + operator -3
   int tm_probe_logged = 0;
   com_ptr<ID3D11Buffer> cb_tm_patch; // our own writable copy, bound in place of the game's
   uint32_t cb_tm_patch_size = 0;
   // What the tonemapper was actually asked to do last frame, for the panel readout. Without this
   // the panel cannot answer "is the HDR path on", which is the one question that matters here.
   int tm_operator_seen = 0;
   int tm_sqrt_gate_seen = 0;
   float tm_exposure_seen = 0.0f;
   float tm_white_point_seen = 0.0f;
#endif
#if DEVELOPMENT
   // MV sniffer. Mafia III's mod does not feed DLSS the TAA's own t2, it hooks a separate
   // "Encode Motion Vectors" pass and takes its R16G16F SRV, so this looks for any full
   // resolution two channel float texture the game binds, for the diagnostic log only.
   // Its mission is complete: ~5.1M draws were sniffed and this game has no such buffer, the
   // resolve's t2 IS the motion vector. Wrapped in DEVELOPMENT so it cannot get re-enabled by
   // accident in a release build.
   long long mv_sniff_next = 0;
   com_ptr<ID3D11Resource> mv_sniffed[4];
   int mv_sniffed_count = 0;
   int mv_sniff_logged = 0;
#endif
   // NOTE: a CPU readback of the motion vector texture (CopySubresourceRegion + Map) reliably
   // crashed this game, so no code path does one any more. The MV encoding was instead confirmed
   // from the disassembly: the resolve does "add r1.xyzw, r0.xyxy, v1.xyxy" on t2.
   // ---- RCAS sharpening on the SR output ----
   com_ptr<ID3D11Buffer> cb_sharpen;         // (w, h, sharpness, 0)
   com_ptr<ID3D11Texture2D> tex_sharpen_src; // copy of the SR result (RCAS cannot read/write one target)
   com_ptr<ID3D11ShaderResourceView> tex_sharpen_srv;
   com_ptr<ID3D11RenderTargetView> tex_sharpen_rtv;
   uint32_t sharpen_w = 0, sharpen_h = 0;
   float sharpen_amount_cached = -1.f;
   // RCAS on top of the SR output, like Mafia III's mod does. It used to be a load bearing part of
   // this mod (default 0.35) because DLAA looked noticeably softer than the game's TAA - but that
   // softness turned out to be mostly the wrong motion vector declaration (see sr_mvs_jittered):
   // with the MVJittered flag off the base image got visibly sharp, and sharpening it further only
   // re-adds the noise that DLAA is already criticised for. So the default is now 0, which skips
   // the pass entirely (DrawSharpen returns before creating anything).
   // Negative values make RCAS blur instead of sharpen. That is deliberately allowed: it is the
   // cheapest way to preview what a spatial denoiser would do (trade noise for blur), without
   // writing one. A spatial filter can only smear the scene's stochastic lighting noise, whereas
   // the game's TAA resolved it with real temporal samples.
   float sharpen_amount = 0.f;
   int sharpen_logged = 0;
   // ---- Scaling the game's own post-tonemap sharpening (0x747C6210) ----
   // Staging copy of that pass' constant buffer, read once per draw of the pass so the authored
   // strength can be multiplied without ever compounding our own previous output. Same pattern (and
   // same reason) as the resolve's jitter readback further down.
   com_ptr<ID3D11Buffer> cb_game_sharpen_staging;
   uint32_t cb_game_sharpen_staging_size = 0;
   int game_sharpen_logged = 0;
   // Last value the game authored, and whether writing ours back ever failed. Both are only there so
   // the panel can show the real number instead of a multiplier that means nothing on its own (the
   // authored strength turns out to be a very small 0.0196, so "x2" is barely a difference).
   float game_sharpen_authored = -1.f;
   int game_sharpen_write_failed = 0;
   com_ptr<ID3D11RenderTargetView> cached_output_rtv;
   void* cached_output_ptr = nullptr;
   // Staging copy of the resolve's PS constant buffer, used to read the per frame jitter.
   com_ptr<ID3D11Buffer> cb_staging;
   uint32_t cb_staging_size = 0;
   // Per frame TAA jitter, read from the resolve's pixel shader constant buffer slot 0, floats
   // 32 and 33. Located by diffing dumps across frames: they are the only values changing every
   // frame that sit in Luma's expected -0.5..0.5 pixel space, and they are quantised
   // (1/8, 1/6, 5/18 ...) i.e. an 8 phase Halton sequence. They are already in pixel space and
   // are handed to DLSS as-is: no NDC conversion and no sign flip (flipping it made the temporal
   // accumulation pair the wrong pixels, which looked worse even though DLSS did run).
   float2 jitter_px = {0.f, 0.f};
#if DEVELOPMENT
   // Dev experiments: jitter_flip negates the game's jitter, jitter_one_frame_late feeds DLSS the
   // previous frame's value in case the one readable at the resolve is the next frame's (that
   // mismatch shows up as speckle on high frequency content such as sunlit floors).
   int jitter_flip = 0;
   int jitter_one_frame_late = 0;
   float2 jitter_prev = {0.f, 0.f};
#endif
   // DLSS parameters that the game does not expose and that had to be established by A/B tests.
   // These are the values that ship; a DEVELOPMENT build can still toggle them in the overlay.
   int sr_auto_exposure = 1;
   // Verified, not assumed: the main geometry pass runs with DepthFunc = D3D11_COMPARISON_GREATER_EQUAL
   // (the one-shot probe below logs "DepthFunc=7 (GREATER_EQUAL) ... reversed-Z = yes"), i.e. the engine
   // renders reversed-Z with the far plane at 0, so this flag must stay ON. An in game A/B of the flag
   // shows no visible difference at all, which is exactly why it needs a hard judge like that probe:
   // getting it wrong only shows up as wrong disocclusion at occluded edges.
   int sr_inverted_depth = 1;
   int sr_hdr = 1;
   // The motion vectors are scaled by the render resolution because they are stored in UV space.
   // Reading UV space vectors as pixel space shrinks the perceived motion by a factor of 3840,
   // which makes DLSS see essentially random motion: that was the source of the heavy noise.
   int sr_mv_scale_by_resolution = 1; // 1 = multiply by render resolution (Mafia III behaviour)
   // Default OFF. READ THIS BEFORE CHANGING IT BACK, BECAUSE THE CODE AND THE
   // FACTS DISAGREE ON PURPOSE HERE.
   //
   // What the data says (and it is not in doubt): the engine bakes the jitter into the projection
   // matrices, so the motion vectors it produces already contain the per frame jitter delta, which
   // means the *correct* declaration is ON. Proof, from the disassembly of the resolve itself
   // (0x37B05605.ps_5_0.cso.asm, dumped with D3DDisassemble):
   //   sample_indexable(texture2d) r0.xyzw, v1.xyxx, t2.xyzw, s0   // r0.xy = motion vector
   //   add r1.xyzw, r0.xyxy, v1.xyxy                                // prevUV = curUV + MV, nothing else
   // and the shader never reads cb0[8], i.e. the jitter floats we read and hand to DLSS - there is
   // no jitter correction term anywhere in it. For the game's own TAA to be exact (it is: with SR
   // off the image has almost no noise) the delta has to be inside the MVs.
   //
   // Why the default is OFF anyway: with DLSS 310.9.x the flag no longer behaves the way the DLSS 3
   // era reasoning above assumes. Measured in game, repeatedly, on the same scenes:
   //   OFF -> visibly sharper, and *less* noise than ON (not more, as an earlier A/B against an
   //          older DLSS recorded), reflective/transparent content unchanged, more aliasing on
   //          high contrast edges (taxi roof, reflective trim against the sky).
   //   ON  -> softer overall, the bright sky and sunlit ground speckle and flicker more.
   // Measured against "none" mode (the game's own TAA, i.e. no DLSS at all) as the reference for
   // correctness, OFF is the closer match in sharpness; the aliasing it trades for is the price.
   //
   // Rule of thumb for why OFF is not "broken": the offset it introduces is bounded by the jitter
   // amplitude, at most 0.5 px per axis, so DLSS's history clamp absorbs it as *reduced temporal
   // accumulation* (sharper, noisier structure, more aliasing) rather than as a reprojection to the
   // wrong content. It does not smear: ghosting needs the filter to trust its history MORE, and OFF
   // makes it trust it less. If ghosting ever does appear, look at the MV scale and sign first
   // (sr_mv_scale_by_resolution below), not at this flag - toggling that one produces ghosting an
   // order of magnitude worse and is the fastest way to see what real ghosting looks like here.
   //
   // It stays a user facing toggle, and the panel shows the current state: anyone who prefers the
   // correct declaration and accepts the softness can flip it back in one click.
   //
   // NOTE: this only changes the *declaration*; the jitter we feed (jitter_px, from cb0 32/33) is
   // unchanged and still has to be the offset the game really applied.
   int sr_mvs_jittered = 0;

   int aa_seen = 0;       // how often the resolve was hooked (lifetime)
   int aa_this_frame = 0; // how often it was hooked in the current frame
   int dlss_drawn = 0;    // how often the SR draw actually succeeded
   // Which resolve variant is currently firing: 0 = 0x37B05605, 1 = 0x7B9B914F, 2 = the PreTAA
   // copy, -1 = none has been seen yet. The game picks a different one depending on the anti
   // aliasing quality level, so the panel has to be able to say which. "DLSS: active" on its own
   // only reports the type selection, and it keeps saying that even when every draw fails, which
   // makes a silent fallback look exactly like success.
   int resolve_last_index = -1;
   uint32_t resolve_seen_mask = 0; // bit per resolve variant, so each is announced exactly once
   // The video / letterbox pass (0x00D96EAE). Cutscenes are the one place this mod cannot do
   // anything: if the fullscreen pass that plays them is this one, then what is on screen is a
   // pre-rendered video with no geometry in it, so there is nothing to anti-alias and DLSS cannot
   // be given motion vectors or depth that mean anything. Distinguishing that from "the game
   // renders a cutscene in engine but switches its own TAA off for it" is the whole question, and
   // the draw count alone cannot tell them apart (both are a handful of draws per frame). This can:
   // if the video pass draws while the resolve does not, it is a video.
   int video_pass_draws = 0;
   // "The world was drawn this frame" and "a resolve ran this frame", tracked per frame so the panel
   // can explain why DLAA is not engaging. A frame that draws the tonemapper but runs no resolve
   // means the game's Anti-Aliasing setting is not Low: at Medium and High this game runs no
   // temporal resolve at all (measured - see REPORT.md §6.5), so there is nothing for DLSS to
   // replace. From the user's side that is indistinguishable from a broken mod, which is why it is
   // worth surfacing in the panel rather than leaving to the log.
   bool tonemap_this_frame = false;
   // The world is being drawn and no resolve runs: the Anti-Aliasing setting is wrong. Drives the
   // warning, and only that.
   int no_resolve_frames = 0;
   // No resolve, whatever is on screen. A cutscene has none either, and the "DLSS:" line must not
   // claim to be running while a video plays - but a cutscene is not a fault, which is why it must
   // not feed the warning above. Two counters because those are two different questions.
   int no_resolve_any_frames = 0;
   // Last reported DLSS/SR state, so the "[frame]" heartbeat only prints when that state changes (or
   // every 300 frames). Comparing running totals instead, as it used to, made it print every frame.
   int last_sr_state = -1;
   // Log each colour format once: native and cloned colour can alternate every frame.
   int logged_each[3] = {0, 0, 0}; // [0] = 0x37B05605, [1] = 0x7B9B914F, [2] = dev PreTAA copy
   uint32_t logged_colour_formats[3] = {};
   bool logged_draw_ok[3] = {};
   int last_sr_log_frame[3] = {-300, -300, -300};
   int input_rejections = 0;
   int settings_failures = 0;
   int draw_failures = 0;
   int sr_resets = 0;
};

class MafiaDefinitiveEditionGame final : public Game
{
   static MafiaDefinitiveEditionGameDeviceData& GetGameDeviceData(DeviceData& device_data)
   {
      return *static_cast<MafiaDefinitiveEditionGameDeviceData*>(device_data.game);
   }

   // Persisted in ReShade.ini: DllMain re-applies its default on every launch, so an
   // in-memory-only toggle silently reverts on restart.
   int texture_upgrades_enabled = 0;

   void ApplyTextureUpgradeSetting()
   {
      texture_format_upgrades_type = texture_upgrades_enabled
                                        ? TextureFormatUpgradesType::AllowedEnabled
                                        : TextureFormatUpgradesType::None;
   }

public:
   // Called from CoreMain right after reshade::register_addon() and before the load is
   // rejected. "failed" is true when register_addon() returned false, which is the ONLY way a
   // Luma add-on makes DllMain return FALSE (=> LoadLibrary error 1114). We log every
   // condition that can make register_addon() fail so a load failure leaves a readable trail.
   void OnLoad(std::filesystem::path& file_path, bool failed = false) override
   {
      std::string s = std::format("OnLoad: module = {}\n  register_addon failed = {}", file_path.string(), failed);
      // Also log our own HMODULE. "invalid module handle" in the ReShade log means the handle that
      // reached ReShadeRegisterAddon was null or already registered, and the only way to tell which
      // is to have the number in this log. See the comment in LoadConfigs() about how a config API
      // call placed before register_addon() poisons that handle.
      s += std::format("\n  our module handle = {}", static_cast<const void*>(g_own_module));
      HMODULE reshade_module = reshade::internal::get_reshade_module_handle(nullptr);
      s += std::format("\n  reshade module handle = {}", static_cast<const void*>(reshade_module));
      if (reshade_module != nullptr)
      {
         s += std::format("\n  ReShadeRegisterAddon found = {}",
            reinterpret_cast<const void*>(GetProcAddress(reshade_module, "ReShadeRegisterAddon")) != nullptr);
      }
      s += std::format("\n  RESHADE_API_VERSION = {}", static_cast<int>(RESHADE_API_VERSION));
      Probe::Line(s);
   }

   void OnInit(bool async) override
   {
      // The pass hash lists are filled during static initialisation (see PassHashesInit above):
      // Game::OnInit runs on a provider thread and its ordering relative to the first draws is not
      // something to depend on, while an empty list silently turns every hook into a no-op.

      // Our own RCAS sharpening pass, drawn right after the SR output.
      native_shaders_definitions.emplace(CompileTimeStringHash("MafiaDE Sharpen PS"),
         ShaderDefinition{"Luma_MafiaDE_Sharpen", reshade::api::pipeline_subobject_type::pixel_shader, nullptr, "sharpen_ps"});

      GetShaderDefineData(POST_PROCESS_SPACE_TYPE_HASH).SetDefaultValue('0');
      GetShaderDefineData(EARLY_DISPLAY_ENCODING_HASH).SetDefaultValue('0');
      GetShaderDefineData(VANILLA_ENCODING_TYPE_HASH).SetDefaultValue('0');
      GetShaderDefineData(GAMMA_CORRECTION_TYPE_HASH).SetDefaultValue('1');
      GetShaderDefineData(UI_DRAW_TYPE_HASH).SetDefaultValue('0');
      luma_settings_cbuffer_index = 13;
      luma_data_cbuffer_index = 12;
      luma_ui_cbuffer_index = -1;

      default_luma_global_game_settings.GameSetting01 = cb_luma_global_settings.GameSettings.GameSetting01 = 0.5f;
      default_luma_global_game_settings.GameSetting02 = cb_luma_global_settings.GameSettings.GameSetting02 = 33;
   }

   void OnCreateDevice(ID3D11Device* native_device, DeviceData& device_data) override
   {
      auto* gd = new MafiaDefinitiveEditionGameDeviceData;
      gd->sr_mvs_jittered = g_mvs_jittered_ini; // the ini value LoadConfigs() already read
      device_data.game = gd;
   }

   // ---- The game's own post-tonemap sharpening (0x747C6210) ---------------------------------
   //
   // Vanilla keeps this pass and it is authored against the game's own TAA: a neighbourhood
   // variance sharpener whose strength lives in "cb0[0].z" (the "mul r0, r0, v1" that sits outside
   // the pass' own "if (strength > 0)" block - which is also why the pass must never be cancelled,
   // see the file header). DLAA comes out slightly softer than a good TAA (it trades sharpness
   // for not ghosting), so the authored strength reads as "not sharp enough" once the resolve is
   // replaced. Rather than stacking a generic filter on top (our RCAS pass, off by default because
   // it also brings DLAA's noise back), scale what the game already does:
   //   1.00 = vanilla, 0 = strength zeroed but the draw still runs, > 1 = closes the DLAA/TAA gap.
   // The authored value is re-read through a staging copy every time and only then multiplied, so
   // we never compound our own output and the game can still change it at runtime.
   float game_sharpen_scale = 1.f;

   void ScaleGameSharpenStrength(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, MafiaDefinitiveEditionGameDeviceData& gd)
   {
      ID3D11Buffer* cbs[1] = {};
      native_device_context->PSGetConstantBuffers(0, 1, cbs);
      if (cbs[0] == nullptr)
      {
         return;
      }
      com_ptr<ID3D11Buffer> cb = cbs[0];

      D3D11_BUFFER_DESC src = {};
      cb->GetDesc(&src);
      // We only touch "cb0[0].z", so there has to be at least one float4, and anything huge is not
      // the constant buffer of a full screen pass.
      if (src.ByteWidth < 4 * sizeof(float) || src.ByteWidth > 4096)
      {
         return;
      }

      if (gd.cb_game_sharpen_staging == nullptr || gd.cb_game_sharpen_staging_size != src.ByteWidth)
      {
         gd.cb_game_sharpen_staging = nullptr;
         D3D11_BUFFER_DESC st = {};
         st.ByteWidth = src.ByteWidth;
         st.Usage = D3D11_USAGE_STAGING;
         st.BindFlags = 0;
         st.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
         if (SUCCEEDED(native_device->CreateBuffer(&st, nullptr, &gd.cb_game_sharpen_staging)))
         {
            gd.cb_game_sharpen_staging_size = src.ByteWidth;
         }
      }
      if (gd.cb_game_sharpen_staging == nullptr)
      {
         return;
      }

      native_device_context->CopyResource(gd.cb_game_sharpen_staging.get(), cb.get());

      std::vector<float> values(src.ByteWidth / sizeof(float));
      D3D11_MAPPED_SUBRESOURCE mapped = {};
      if (FAILED(native_device_context->Map(gd.cb_game_sharpen_staging.get(), 0, D3D11_MAP_READ, 0, &mapped)))
      {
         return;
      }
      memcpy(values.data(), mapped.pData, src.ByteWidth);
      native_device_context->Unmap(gd.cb_game_sharpen_staging.get(), 0);

      if (gd.game_sharpen_logged < 4)
      {
         ++gd.game_sharpen_logged;
         Probe::Line(std::format("[sharpen] game pass cb0[0] = {:.4f}, {:.4f}, {:.4f}, {:.4f} -> strength {:.4f} x {:.2f} = {:.4f}",
            values[0], values[1], values[2], values[3], values[2], game_sharpen_scale, values[2] * game_sharpen_scale));
      }
      gd.game_sharpen_authored = values[2];
      values[2] *= game_sharpen_scale;

      // WRITE_DISCARD drops the entire buffer, so the untouched floats have to be written back too.
      if (SUCCEEDED(native_device_context->Map(cb.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
      {
         memcpy(mapped.pData, values.data(), src.ByteWidth);
         native_device_context->Unmap(cb.get(), 0);
      }
      else if (gd.game_sharpen_write_failed == 0)
      {
         gd.game_sharpen_write_failed = 1;
         Probe::Line("[sharpen] FAILED to map the game constant buffer for writing, the strength cannot be changed");
      }
   }

   // ---- RCAS sharpening on the SR output --------------------------------------------------
   //
   // Runs right after DLSS/DLAA has written the frame. RCAS reads a SRV and writes an RTV, so
   // the SR result is first copied into a scratch texture; the pass then writes the sharpened
   // pixels back into the game's AA render target.
   void DrawSharpen(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context,
      DeviceData& device_data, MafiaDefinitiveEditionGameDeviceData& gd,
      ID3D11Resource* output_color)
   {
      if (gd.sharpen_amount <= 0.f || !output_color)
      {
         return;
      }

      com_ptr<ID3D11Texture2D> out_tex;
      if (FAILED(output_color->QueryInterface(&out_tex)) || !out_tex)
      {
         return;
      }
      D3D11_TEXTURE2D_DESC od = {};
      out_tex->GetDesc(&od);
      const UINT w = od.Width, h = od.Height;
      if (w < 64 || h < 64)
      {
         return;
      }

      // Named injected shaders live in maps the render thread otherwise only reads, so look them
      // up with find() - operator[] would default-insert on a miss.
      const auto vs_it = device_data.native_vertex_shaders.find(CompileTimeStringHash("Copy VS"));
      const auto ps_it = device_data.native_pixel_shaders.find(CompileTimeStringHash("MafiaDE Sharpen PS"));
      if (vs_it == device_data.native_vertex_shaders.end() || ps_it == device_data.native_pixel_shaders.end())
      {
         static bool warned = false;
         if (!warned)
         {
            warned = true;
            Probe::Line("[sharpen] Copy VS or MafiaDE Sharpen PS not found - skipping");
         }
         return;
      }

      // (Re)create the scratch texture on resolution change.
      if (!gd.tex_sharpen_src || gd.sharpen_w != w || gd.sharpen_h != h)
      {
         gd.tex_sharpen_srv = nullptr;
         gd.tex_sharpen_rtv = nullptr;
         gd.tex_sharpen_src = nullptr;
         D3D11_TEXTURE2D_DESC td = od;
         td.MipLevels = 1;
         td.ArraySize = 1;
         td.SampleDesc.Count = 1;
         td.Usage = D3D11_USAGE_DEFAULT;
         td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
         td.CPUAccessFlags = 0;
         td.MiscFlags = 0;
         if (SUCCEEDED(native_device->CreateTexture2D(&td, nullptr, &gd.tex_sharpen_src)) && gd.tex_sharpen_src && SUCCEEDED(native_device->CreateShaderResourceView(gd.tex_sharpen_src.get(), nullptr, &gd.tex_sharpen_srv)) && SUCCEEDED(native_device->CreateRenderTargetView(gd.tex_sharpen_src.get(), nullptr, &gd.tex_sharpen_rtv)))
         {
            gd.sharpen_w = w;
            gd.sharpen_h = h;
         }
      }

      // (Re)create the immutable CB on resolution or strength change.
      if (!gd.cb_sharpen || gd.sharpen_w != w || gd.sharpen_h != h || gd.sharpen_amount_cached != gd.sharpen_amount)
      {
         const float params[4] = {static_cast<float>(w), static_cast<float>(h), gd.sharpen_amount, 0.f};
         D3D11_BUFFER_DESC bd = {};
         bd.ByteWidth = sizeof(params);
         bd.Usage = D3D11_USAGE_IMMUTABLE;
         bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
         D3D11_SUBRESOURCE_DATA sd = {};
         sd.pSysMem = params;
         gd.cb_sharpen = nullptr;
         if (SUCCEEDED(native_device->CreateBuffer(&bd, &sd, &gd.cb_sharpen)))
         {
            gd.sharpen_amount_cached = gd.sharpen_amount;
         }
      }

      // An RTV for the game's AA target, cached across frames.
      if (gd.cached_output_ptr != output_color || !gd.cached_output_rtv)
      {
         gd.cached_output_rtv = nullptr;
         if (FAILED(native_device->CreateRenderTargetView(output_color, nullptr, &gd.cached_output_rtv)))
         {
            gd.cached_output_ptr = nullptr;
            return;
         }
         gd.cached_output_ptr = output_color;
      }

      if (!gd.tex_sharpen_src || !gd.tex_sharpen_srv || !gd.tex_sharpen_rtv || !gd.cb_sharpen || !gd.cached_output_rtv)
      {
         return;
      }

      if (gd.sharpen_logged < 3)
      {
         ++gd.sharpen_logged;
         Probe::Line(std::format("[sharpen] RCAS {}x{} amount={:.2} src={} dst={}",
            w, h, gd.sharpen_amount, Probe::Describe(gd.tex_sharpen_src.get()),
            Probe::Describe(output_color)));
      }

      native_device_context->CopyResource(gd.tex_sharpen_src.get(), output_color);

      // DrawCustomPixelShader does not restore state, so wrap it in the core's state stack.
      DrawStateStack<DrawStateStackType::FullGraphics> sharpen_state;
      sharpen_state.Cache(native_device_context, device_data.uav_max_count);

      ID3D11Buffer* cb = gd.cb_sharpen.get();
      native_device_context->PSSetConstantBuffers(0, 1, &cb);
      DrawCustomPixelShader(native_device_context,
         device_data.default_depth_stencil_state.get(),
         device_data.default_blend_state.get(),
         nullptr,
         vs_it->second.get(), ps_it->second.get(),
         gd.tex_sharpen_srv.get(), gd.cached_output_rtv.get(),
         w, h, false);

      sharpen_state.Restore(native_device_context);
   }

   DrawOrDispatchOverrideType OnDrawOrDispatch(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, reshade::api::shader_stage stages, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, bool is_custom_pass, bool& updated_cbuffers, std::function<void()>* original_draw_dispatch_func) override
   {
      if (is_custom_pass)
      {
         return DrawOrDispatchOverrideType::None; // Never touch our own passes
      }

      auto& game_device_data = GetGameDeviceData(device_data);

      ++Probe::s_draw_calls;

#if DEVELOPMENT
      // Diagnostics: remember every pixel shader drawn this frame. See "NotePixelShader" - this is
      // the only way to see what the game switches to when the AA quality changes, or what a
      // cutscene runs when the DLSS indicator has nothing to show.
      {
         uint32_t ps_hash = 0;
         for (auto v : original_shader_hashes.pixel_shaders)
         {
            if (v == SHADER_HASH_NONE) break;
            ps_hash = static_cast<uint32_t>(v);
            break;
         }
         Probe::NotePixelShader(ps_hash);
      }
#endif

#if DEVELOPMENT
      // Diagnostics: record every draw into a ring buffer, so that when the resolve hits we can
      // dump the sequence that led up to it. Costs an OMGetRenderTargets and a format lookup per
      // draw, so it is dev only.
      if (game_device_data.s_pre_taa_dumped == 0)
      {
         auto& slot = game_device_data.s_pre_taa_ring[game_device_data.s_pre_taa_ring_pos];
         for (auto v : original_shader_hashes.pixel_shaders)
         {
            if (v == SHADER_HASH_NONE) break;
            slot.hash = static_cast<uint32_t>(v);
            break;
         }
         slot.rtv[0] = 0;
         ID3D11RenderTargetView* r0[2] = {};
         native_device_context->OMGetRenderTargets(2, r0, nullptr);
         for (int q = 0; q < 2; ++q)
         {
            if (!r0[q]) continue;
            com_ptr<ID3D11Resource> rr;
            r0[q]->GetResource(&rr);
            std::string d = Probe::Describe(rr.get());
            snprintf(slot.rtv + strlen(slot.rtv), sizeof(slot.rtv) - strlen(slot.rtv), " RTV%d=%s", q, d.c_str());
            r0[q]->Release();
         }
         game_device_data.s_pre_taa_ring_pos = (game_device_data.s_pre_taa_ring_pos + 1) % kPreTaaRing;
      }

      // Diagnostics: record the draws that run after the tonemapper, dumped once at Present. Only
      // armed for one frame and only once the tonemapper has been seen, so the ring holds the real
      // frame tail instead of the whole frame (which would be tens of thousands of draws).
      if (game_device_data.s_tail_dumped == 0)
      {
         if (original_shader_hashes.Contains(shader_hashes_Tonemap))
         {
            game_device_data.s_tail_tonemap_seen = 1;
         }
         if (game_device_data.s_tail_tonemap_seen)
         {
            auto& tslot = game_device_data.s_tail_ring[game_device_data.s_tail_ring_pos];
            tslot.hash = 0;
            for (auto v : original_shader_hashes.pixel_shaders)
            {
               if (v == SHADER_HASH_NONE) break;
               tslot.hash = static_cast<uint32_t>(v);
               break;
            }
            tslot.rtv[0] = 0;
            ID3D11RenderTargetView* trtv[2] = {};
            native_device_context->OMGetRenderTargets(2, trtv, nullptr);
            for (int q = 0; q < 2; ++q)
            {
               if (!trtv[q]) continue;
               com_ptr<ID3D11Resource> rr;
               trtv[q]->GetResource(&rr);
               const std::string tdesc = Probe::Describe(rr.get());
               snprintf(tslot.rtv + strlen(tslot.rtv), sizeof(tslot.rtv) - strlen(tslot.rtv), " RTV%d=%s", q, tdesc.c_str());
               trtv[q]->Release();
            }
            game_device_data.s_tail_ring_pos = (game_device_data.s_tail_ring_pos + 1) % kTailRing;
         }
      }

      // Dev probe: rewrite the tonemapper's constants so the shader outputs linear, and optionally
      // unclamped, values. See the field declaration for why two floats are enough to try HDR.
      if (game_device_data.tm_probe_mode > 0 && original_shader_hashes.Contains(shader_hashes_Tonemap))
      {
         ID3D11Buffer* pcb = nullptr;
         native_device_context->PSGetConstantBuffers(0, 1, &pcb);
         if (pcb)
         {
            D3D11_BUFFER_DESC bd = {};
            pcb->GetDesc(&bd);
            // Float indices, not registers: cb0[3].x is float 12 and cb0[8].z is float 34. Writing
            // float 35 instead of 34 was the first version's bug, and it patched cb0[8].w, the
            // dither clamp, which changes nothing you can see.
            constexpr uint32_t kOpIndex = 12;  // cb0[3].x, the tonemap operator
            constexpr uint32_t kSqrtGate = 34; // cb0[8].z, > 0 enables the dithered sqrt encode
            if (bd.ByteWidth >= (kSqrtGate + 1) * sizeof(float) && bd.ByteWidth <= 4096)
            {
               // UpdateSubresource on the engine's own buffer is a no-op if that buffer is
               // immutable, and it returns void so the failure is invisible: the first version of
               // this probe printed success and changed nothing on screen. So instead of writing to
               // the engine's buffer, read it, patch a copy we own, and bind that in its place.
               if (game_device_data.cb_tm_staging[0] == nullptr || game_device_data.cb_tm_staging_size[0] != bd.ByteWidth)
               {
                  game_device_data.cb_tm_staging[0] = nullptr;
                  D3D11_BUFFER_DESC sd = {};
                  sd.ByteWidth = bd.ByteWidth;
                  sd.Usage = D3D11_USAGE_STAGING;
                  sd.BindFlags = 0;
                  sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                  if (SUCCEEDED(native_device->CreateBuffer(&sd, nullptr, &game_device_data.cb_tm_staging[0])))
                  {
                     game_device_data.cb_tm_staging_size[0] = bd.ByteWidth;
                  }
               }
               if (game_device_data.cb_tm_patch == nullptr || game_device_data.cb_tm_patch_size != bd.ByteWidth)
               {
                  game_device_data.cb_tm_patch = nullptr;
                  D3D11_BUFFER_DESC pd = {};
                  pd.ByteWidth = bd.ByteWidth;
                  pd.Usage = D3D11_USAGE_DEFAULT;
                  pd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
                  pd.CPUAccessFlags = 0;
                  if (SUCCEEDED(native_device->CreateBuffer(&pd, nullptr, &game_device_data.cb_tm_patch)))
                  {
                     game_device_data.cb_tm_patch_size = bd.ByteWidth;
                  }
               }
               if (game_device_data.cb_tm_staging[0] && game_device_data.cb_tm_patch)
               {
                  native_device_context->CopyResource(game_device_data.cb_tm_staging[0].get(), pcb);
                  D3D11_MAPPED_SUBRESOURCE pm = {};
                  if (SUCCEEDED(native_device_context->Map(game_device_data.cb_tm_staging[0].get(), 0, D3D11_MAP_READ, 0, &pm)))
                  {
                     std::vector<float> vals(static_cast<size_t>(bd.ByteWidth / sizeof(float)));
                     memcpy(vals.data(), pm.pData, bd.ByteWidth);
                     native_device_context->Unmap(game_device_data.cb_tm_staging[0].get(), 0);
                     const float old_op = vals[kOpIndex];
                     const float old_gate = vals[kSqrtGate];
                     game_device_data.tm_operator_seen = static_cast<int>(old_op);
                     game_device_data.tm_sqrt_gate_seen = old_gate > 0.0f ? 1 : 0;
                     vals[kSqrtGate] = 0.0f; // <= 0 disables the dithered sqrt encode: linear output
                     if (game_device_data.tm_probe_mode == 2)
                     {
                        vals[kOpIndex] = -2.0f;
                     }
                     else if (game_device_data.tm_probe_mode == 3)
                     {
                        vals[kOpIndex] = -3.0f;
                     }
                     D3D11_MAPPED_SUBRESOURCE wm = {};
                     // Filling the copy needs UpdateSubresource, not Map: D3D11_MAP_WRITE_DISCARD is
                     // only legal on a D3D11_USAGE_DYNAMIC buffer, and this one is USAGE_DEFAULT.
                     // Mapping it fails, which is why the second version of this probe printed
                     // nothing at all and changed nothing on screen.
                     native_device_context->UpdateSubresource(game_device_data.cb_tm_patch.get(), 0, nullptr, vals.data(), 0, 0);
                     (void)wm;
                     {
                        // Replace the binding the shader is about to read from. The engine re-binds
                        // its own buffer before every draw, so this only has to survive one draw.
                        ID3D11Buffer* patched = game_device_data.cb_tm_patch.get();
                        native_device_context->PSSetConstantBuffers(0, 1, &patched);
                        if (game_device_data.tm_probe_logged < 6)
                        {
                           game_device_data.tm_probe_logged++;
                           Probe::Line(std::format("[probe] mode {}: engine CB usage={:#x} bind={:#x} size={}; cb0[3].x {:.3f} -> {:.3f}, cb0[8].z {:.5f} -> 0, bound our copy",
                              game_device_data.tm_probe_mode, static_cast<uint32_t>(bd.Usage), static_cast<uint32_t>(bd.BindFlags),
                              bd.ByteWidth, old_op, vals[kOpIndex], old_gate));
                        }
                     }
                  }
               }
            }
         }
      }

      // Diagnostics: one shot, automatic dump of the tonemapper's constants and inputs. See the
      // field declaration for why this does not wait for a button press.
      if (game_device_data.s_tm_dump_state == 0 && game_device_data.aa_seen >= 30 && Probe::s_frame > 60 && original_shader_hashes.Contains(shader_hashes_Tonemap))
      {
         game_device_data.s_tm_dump_state = 1;
         Probe::Line(std::format("[tm] ==== automatic tonemapper dump at frame {} ====", Probe::s_frame));

         // CB0 and CB1, printed one float4 per line so the indices line up with the disassembly
         // ("cb0[3].x" is the operator index there).
         ID3D11Buffer* pcbs[2] = {};
         native_device_context->PSGetConstantBuffers(0, 2, pcbs);
         for (int slot = 0; slot < 2; slot++)
         {
            com_ptr<ID3D11Buffer> pcb = pcbs[slot];
            if (!pcb) continue;
            D3D11_BUFFER_DESC bd = {};
            pcb->GetDesc(&bd);
            const int floats = static_cast<int>(bd.ByteWidth / sizeof(float));
            if (floats <= 0 || floats > 256)
            {
               continue;
            }
            if (game_device_data.cb_tm_staging[slot] == nullptr || game_device_data.cb_tm_staging_size[slot] != bd.ByteWidth)
            {
               game_device_data.cb_tm_staging[slot] = nullptr;
               D3D11_BUFFER_DESC sd = {};
               sd.ByteWidth = bd.ByteWidth;
               sd.Usage = D3D11_USAGE_STAGING;
               sd.BindFlags = 0;
               sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
               if (SUCCEEDED(native_device->CreateBuffer(&sd, nullptr, &game_device_data.cb_tm_staging[slot])))
               {
                  game_device_data.cb_tm_staging_size[slot] = bd.ByteWidth;
               }
            }
            if (!game_device_data.cb_tm_staging[slot]) continue;
            native_device_context->CopyResource(game_device_data.cb_tm_staging[slot].get(), pcb.get());
            D3D11_MAPPED_SUBRESOURCE pm = {};
            if (!SUCCEEDED(native_device_context->Map(game_device_data.cb_tm_staging[slot].get(), 0, D3D11_MAP_READ, 0, &pm))) continue;
            const float* vals = static_cast<const float*>(pm.pData);
            Probe::Line(std::format("[tm] CB{} ByteWidth={} ({} floats)", slot, bd.ByteWidth, floats));
            for (int f = 0; f + 3 < floats; f += 4)
            {
               Probe::Line(std::format("[tm] cb{}[{}] = ({:.6f}, {:.6f}, {:.6f}, {:.6f})", slot, f / 4,
                  vals[f], vals[f + 1], vals[f + 2], vals[f + 3]));
            }
            native_device_context->Unmap(game_device_data.cb_tm_staging[slot].get(), 0);
         }

         // What the tonemapper has bound, so a replacement shader can be written against it: the
         // 3D texture is the grading LUT, t4 is the 1x1 exposure/white point pair.
         for (int t = 0; t <= 9; t++)
         {
            ID3D11ShaderResourceView* srv = nullptr;
            native_device_context->PSGetShaderResources(t, 1, &srv);
            if (!srv) continue;
            com_ptr<ID3D11Resource> res;
            srv->GetResource(&res);
            srv->Release();
            if (!res) continue;
            com_ptr<ID3D11Texture2D> tex2d;
            com_ptr<ID3D11Texture3D> tex3d;
            if (SUCCEEDED(res->QueryInterface(&tex2d)) && tex2d)
            {
               D3D11_TEXTURE2D_DESC d = {};
               tex2d->GetDesc(&d);
               Probe::Line(std::format("[tm] t{} = 2D {}x{}x{} mips={} format={:#x}", t, d.Width, d.Height, d.ArraySize, d.MipLevels, static_cast<uint32_t>(d.Format)));
               if (d.Width == 1 && d.Height == 1 && game_device_data.tex_tm_1x1 == nullptr)
               {
                  game_device_data.tex_tm_1x1 = tex2d;
               }
            }
            else if (SUCCEEDED(res->QueryInterface(&tex3d)) && tex3d)
            {
               D3D11_TEXTURE3D_DESC d = {};
               tex3d->GetDesc(&d);
               Probe::Line(std::format("[tm] t{} = 3D {}x{}x{} format={:#x}", t, d.Width, d.Height, d.Depth, static_cast<uint32_t>(d.Format)));
            }
            else
            {
               Probe::Line(std::format("[tm] t{} = not a texture (a buffer?)", t));
            }
         }

         // The 1x1 texture's four raw texels. Both the float and the integer reading are printed
         // because the format decides which one is meaningful.
         if (game_device_data.tex_tm_1x1)
         {
            D3D11_TEXTURE2D_DESC d = {};
            game_device_data.tex_tm_1x1->GetDesc(&d);
            D3D11_TEXTURE2D_DESC sd = d;
            sd.Usage = D3D11_USAGE_STAGING;
            sd.BindFlags = 0;
            sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            sd.MiscFlags = 0;
            sd.MipLevels = 1;
            sd.ArraySize = 1;
            sd.SampleDesc.Count = 1;
            sd.SampleDesc.Quality = 0;
            if (SUCCEEDED(native_device->CreateTexture2D(&sd, nullptr, &game_device_data.tex_tm_staging)))
            {
               native_device_context->CopyResource(game_device_data.tex_tm_staging.get(), game_device_data.tex_tm_1x1.get());
               D3D11_MAPPED_SUBRESOURCE pm = {};
               if (SUCCEEDED(native_device_context->Map(game_device_data.tex_tm_staging.get(), 0, D3D11_MAP_READ, 0, &pm)))
               {
                  // The format is R16G16B16A16_FLOAT, so the texel is eight halves, not four floats.
                  // Reading it as floats produced 2.19e12 nonsense on the first run of this probe.
                  // Only .x (exposure) and .y (white point) are read by the shader, and those are
                  // the two numbers the panel needs to show.
                  const uint8_t* raw = static_cast<const uint8_t*>(pm.pData);
                  auto half = [](const uint8_t* p) -> float
                  {
                     const uint16_t h = static_cast<uint16_t>(p[0] | (p[1] << 8));
                     const uint32_t sign = (h >> 15) ? 0x80000000u : 0u;
                     const uint32_t exp = (h >> 10) & 0x1F;
                     const uint32_t man = h & 0x3FF;
                     uint32_t bits;
                     if (exp == 0)
                     {
                        bits = sign;
                        if (man)
                        {
                           bits = sign | (127u - 14u) << 23 | (man << 13);
                        }
                     }
                     else if (exp == 31)
                     {
                        bits = sign | 0x7F800000u | (man << 13);
                     }
                     else
                     {
                        bits = sign | (exp + 127u - 15u) << 23 | (man << 13);
                     }
                     float out = 0.0f;
                     memcpy(&out, &bits, 4);
                     return out;
                  };
                  game_device_data.tm_exposure_seen = half(raw + 0);
                  game_device_data.tm_white_point_seen = half(raw + 2);
                  Probe::Line(std::format("[tm] 1x1 exposure = {:.6f}, white point = {:.4f}  (shader reads .x and .y)",
                     game_device_data.tm_exposure_seen, game_device_data.tm_white_point_seen));
                  native_device_context->Unmap(game_device_data.tex_tm_staging.get(), 0);
               }
            }
         }
         Probe::Line("[tm] ==== dump done ====");
      }

      // Diagnostics: on demand snapshot of the constant buffers of every pass that runs from the
      // tonemapper onwards, see the field declaration. Nothing is printed while the window is open;
      // the whole diff is printed when it closes, so a single button press answers the question.
      if (game_device_data.s_post_cb_arm > 0)
      {
         const bool is_tonemap = original_shader_hashes.Contains(shader_hashes_Tonemap);
         if (is_tonemap)
         {
            // From here to the end of the frame is the tail, and the tonemapper itself is worth a
            // look too: it is the pass that owns the automatic exposure and the white point.
            game_device_data.s_post_tonemap_seen = 1;
         }
         if (Probe::s_frame - game_device_data.s_post_cb_arm_frame >= kPostArmFrames)
         {
            game_device_data.s_post_cb_arm = 0;
            std::string seen;
            std::string diffs;
            int diff_slots = 0;
            for (int i = 0; i < game_device_data.s_post_count; i++)
            {
               const auto& e = game_device_data.s_post[i];
               seen += std::format(" {:08X}({}x)", e.hash, e.draws);
               for (int slot = 0; slot < kPostSlots; slot++)
               {
                  if (!e.base_set[slot]) continue;
                  std::string d;
                  int moved = 0;
                  for (int k = 0; k < kPostFloats; k++)
                  {
                     if (e.base[slot][k] == e.last[slot][k]) continue;
                     if (moved < 16)
                     {
                        d += std::format(" {}:{:.5f}->{:.5f}", k, e.base[slot][k], e.last[slot][k]);
                     }
                     moved++;
                  }
                  if (moved == 0 && !(e.from_tonemap && slot == 0)) continue;
                  if (moved > 0)
                  {
                     diff_slots++;
                     if (moved > 16)
                     {
                        d += std::format(" ...(+{} more)", moved - 16);
                     }
                     diffs += std::format("\n[post] CHG {:08X} s{}:{}", e.hash, slot, d);
                  }
                  // The diff says what moved but not what the value is, and for the tonemapper the
                  // absolute numbers are the point: cb0[3].x is the tonemap operator index and
                  // cb1[4]/cb1[5] are the Uncharted2 parameters Luma would have to reproduce. So
                  // print the whole of slot 0 whenever anything moved, and for the tonemapper even
                  // when nothing did.
                  if (slot == 0)
                  {
                     std::string all;
                     int nonzero = 0;
                     for (int k = 0; k < kPostFloats; k++)
                     {
                        if (e.last[slot][k] == 0.0f) continue;
                        all += std::format(" {}={:.5f}", k, e.last[slot][k]);
                        nonzero++;
                     }
                     if (nonzero > 0)
                     {
                        Probe::Line(std::format("[post] CB0 {:08X}:{}", e.hash, all));
                     }
                  }
               }
            }
            Probe::Line(std::format("[post] ==== window closed: {} frames, {} distinct passes after the tonemapper ====",
               kPostArmFrames, game_device_data.s_post_count));
            Probe::Line(std::format("[post] passes seen:{}", seen));
            if (diff_slots == 0)
            {
               Probe::Line(game_device_data.s_post_count == 0
                              ? "[post] RESULT: the tonemapper never ran while the window was open, so there was nothing to sample."
                              : "[post] RESULT: not one constant moved. This setting does not reach the post chain through a constant buffer.");
            }
            else
            {
               Probe::Line(std::format("[post] RESULT: {} pass/slot combinations moved:", diff_slots));
               Probe::Line(diffs);
            }
         }
         if (game_device_data.s_post_cb_arm > 0 && (is_tonemap || game_device_data.s_post_tonemap_seen))
         {
            uint32_t ph = 0;
            for (auto v : original_shader_hashes.pixel_shaders)
            {
               if (v == SHADER_HASH_NONE) break;
               ph = static_cast<uint32_t>(v);
               break;
            }
            if (ph != 0)
            {
               int post_index = -1;
               for (int i = 0; i < game_device_data.s_post_count; i++)
               {
                  if (game_device_data.s_post[i].hash == ph)
                  {
                     post_index = i;
                     break;
                  }
               }
               if (post_index < 0 && game_device_data.s_post_count < kPostMaxHashes)
               {
                  post_index = game_device_data.s_post_count++;
                  game_device_data.s_post[post_index].hash = ph;
               }
               // Sampling a constant buffer means a copy plus a map, which stalls the pipeline, so
               // only look at every fourth sampled draw. These are per frame constants.
               if (post_index >= 0 && (game_device_data.s_post[post_index].draws++ % kPostSampleStride) == 0)
               {
                  auto& entry = game_device_data.s_post[post_index];
                  if (is_tonemap)
                  {
                     entry.from_tonemap = true;
                  }
                  ID3D11Buffer* pcbs[kPostSlots] = {};
                  native_device_context->PSGetConstantBuffers(0, kPostSlots, pcbs);
                  for (int slot = 0; slot < kPostSlots; slot++)
                  {
                     com_ptr<ID3D11Buffer> pcb = pcbs[slot];
                     if (!pcb) continue;
                     D3D11_BUFFER_DESC pdesc = {};
                     pcb->GetDesc(&pdesc);
                     if (pdesc.ByteWidth < kPostFloats * sizeof(float) || pdesc.ByteWidth > 4096) continue;
                     if (game_device_data.cb_post_staging[slot] == nullptr || game_device_data.cb_post_staging_size[slot] != pdesc.ByteWidth)
                     {
                        game_device_data.cb_post_staging[slot] = nullptr;
                        D3D11_BUFFER_DESC pst = {};
                        pst.ByteWidth = pdesc.ByteWidth;
                        pst.Usage = D3D11_USAGE_STAGING;
                        pst.BindFlags = 0;
                        pst.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                        if (SUCCEEDED(native_device->CreateBuffer(&pst, nullptr, &game_device_data.cb_post_staging[slot])))
                        {
                           game_device_data.cb_post_staging_size[slot] = pdesc.ByteWidth;
                        }
                     }
                     if (!game_device_data.cb_post_staging[slot]) continue;
                     native_device_context->CopyResource(game_device_data.cb_post_staging[slot].get(), pcb.get());
                     D3D11_MAPPED_SUBRESOURCE pm = {};
                     if (!SUCCEEDED(native_device_context->Map(game_device_data.cb_post_staging[slot].get(), 0, D3D11_MAP_READ, 0, &pm))) continue;
                     float vals[kPostFloats];
                     memcpy(vals, pm.pData, sizeof(vals));
                     native_device_context->Unmap(game_device_data.cb_post_staging[slot].get(), 0);
                     // The baseline is the first sample of the window, i.e. before the setting is
                     // touched. "last" is refreshed on every sample and is the "after" side.
                     if (!entry.base_set[slot])
                     {
                        entry.base_set[slot] = true;
                        memcpy(entry.base[slot], vals, sizeof(vals));
                     }
                     memcpy(entry.last[slot], vals, sizeof(vals));
                  }
               }
            }
         }
      }

      // Diagnostics: motion vector sniffer (see the field declaration for why it is dev only).
      if (game_device_data.mv_sniffed_count < 4 && Probe::s_draw_calls >= game_device_data.mv_sniff_next)
      {
         game_device_data.mv_sniff_next = Probe::s_draw_calls + 200;
         ID3D11ShaderResourceView* sniff_srvs[8] = {};
         native_device_context->PSGetShaderResources(0, 8, sniff_srvs);
         for (UINT k = 0; k < 8; ++k)
         {
            if (!sniff_srvs[k]) continue;
            com_ptr<ID3D11Resource> res;
            sniff_srvs[k]->GetResource(&res);
            com_ptr<ID3D11Texture2D> tex;
            if (res && SUCCEEDED(res->QueryInterface(&tex)) && tex)
            {
               D3D11_TEXTURE2D_DESC d = {};
               tex->GetDesc(&d);
               const bool full_res = (d.Width >= 1600 && d.Height >= 900);
               const bool two_channel_float = (d.Format == DXGI_FORMAT_R16G16_FLOAT || d.Format == DXGI_FORMAT_R16G16_TYPELESS || d.Format == DXGI_FORMAT_R32G32_FLOAT || d.Format == DXGI_FORMAT_R32G32_TYPELESS);
               if (full_res && two_channel_float)
               {
                  bool already = false;
                  for (int q = 0; q < game_device_data.mv_sniffed_count; ++q)
                     if (game_device_data.mv_sniffed[q].get() == res.get())
                     {
                        already = true;
                        break;
                     }
                  if (!already && game_device_data.mv_sniffed_count < 4)
                  {
                     game_device_data.mv_sniffed[game_device_data.mv_sniffed_count++] = res;
                     if (game_device_data.mv_sniff_logged < 12)
                     {
                        ++game_device_data.mv_sniff_logged;
                        uint32_t h = 0;
                        for (auto v : original_shader_hashes.pixel_shaders)
                        {
                           if (v == SHADER_HASH_NONE) break;
                           h = static_cast<uint32_t>(v);
                           break;
                        }
                        Probe::Line(std::format("[MV sniff] full res two channel float: SRV[{}] {}x{} fmt {} bound by PS 0x{:08X}",
                           k, d.Width, d.Height, static_cast<int>(d.Format), h));
                     }
                  }
               }
            }
            sniff_srvs[k]->Release();
         }
      }
#endif // DEVELOPMENT

      // ---------------------------------------------------------------------------------
      // 1) Grab the main depth buffer. The resolve runs with depth testing disabled so it has no
      //    DSV bound; the geometry pass that fills the position buffer is the reliable place - but
      //    it is also reused for depth-only shadow draws, so every candidate is validated (see
      //    IsUsableMainDepth) and a rejection never overwrites the last good depth.
      // ---------------------------------------------------------------------------------
      if (original_shader_hashes.Contains(shader_hashes_PositionBufferFill))
      {
         ID3D11DepthStencilView* dsv = nullptr;
         native_device_context->OMGetRenderTargets(0, nullptr, &dsv);
         if (dsv != nullptr)
         {
            com_ptr<ID3D11Resource> candidate;
            dsv->GetResource(&candidate);
            dsv->Release();

            const float min_width = device_data.render_resolution.x > 0.f ? device_data.render_resolution.x * 0.9f : 1600.f;
            const float min_height = device_data.render_resolution.y > 0.f ? device_data.render_resolution.y * 0.9f : 900.f;
            if (IsUsableMainDepth(candidate.get(), min_width, min_height))
            {
               if (game_device_data.main_depth_logged == 0)
               {
                  game_device_data.main_depth_logged = 1;
                  Probe::Line(std::format("[depth] main depth accepted: {}", Probe::Describe(candidate.get())));
               }
#if DEVELOPMENT
               // One shot: which depth comparison the main geometry pass uses. This is the ground truth
               // for the "Inverted depth" flag, which decides how DLSS and DLSS-NR interpret the depth
               // we hand them: GREATER/GREATER_EQUAL means the engine renders reversed-Z (far = 0), so the
               // flag must be ON; LESS/LESS_EQUAL means normal Z (far = 1), so it must be OFF. An in game
               // A/B of the flag showed no visible difference at all, so reading this state is the only
               // way to settle it instead of guessing (it is a main depth candidate, so it is the main
               // pass and not one of the shadow draws that share this shader).
               if (game_device_data.depth_func_logged == 0)
               {
                  ID3D11DepthStencilState* depth_state = nullptr;
                  UINT stencil_ref = 0;
                  native_device_context->OMGetDepthStencilState(&depth_state, &stencil_ref);
                  if (depth_state != nullptr)
                  {
                     D3D11_DEPTH_STENCIL_DESC ds = {};
                     depth_state->GetDesc(&ds);
                     depth_state->Release();
                     game_device_data.depth_func_logged = 1;
                     const bool reversed_z = ds.DepthFunc == D3D11_COMPARISON_GREATER || ds.DepthFunc == D3D11_COMPARISON_GREATER_EQUAL;
                     Probe::Line(std::format("[depth] main pass depth state: DepthFunc={} ({}), DepthEnable={}, WriteAll={} => reversed-Z = {} => sr_inverted_depth must be {}",
                        static_cast<int>(ds.DepthFunc), DepthFuncName(ds.DepthFunc),
                        ds.DepthEnable ? "yes" : "no",
                        (ds.DepthWriteMask == D3D11_DEPTH_WRITE_MASK_ALL) ? "yes" : "no",
                        reversed_z ? "yes" : "no",
                        reversed_z ? "1 (ON)" : "0 (OFF)"));
                  }
               }
#endif
               game_device_data.main_depth = candidate;
            }
            else if (game_device_data.main_depth_rejected < 3)
            {
               ++game_device_data.main_depth_rejected;
               Probe::Line(std::format("[depth] rejected, not a full res single slice target: {}",
                  Probe::Describe(candidate.get())));
            }
         }
      }

#if DEVELOPMENT
      // Dev hook mode 1: trigger at the "PreTAA copy" instead of the resolve - 0xA75736C5 is a
      // plain full resolution colour copy (1 texture, 1 fetch, 8 instructions) that runs right
      // before the temporal resolve. Mafia III's mod hooks its equivalent because that game skips
      // its TAA shaders when TAA is off, so the colours there are the raw scene colours. Here it
      // went black: the copy runs several times per frame and the first one is far too early.
      const bool is_pre_taa_copy = game_device_data.dlss_hook_mode == 1 && original_shader_hashes.Contains(0xA75736C5u, reshade::api::shader_stage::pixel);
      if (is_pre_taa_copy && original_draw_dispatch_func)
      {
         (*original_draw_dispatch_func)(); // let the game do its copy first
      }

      if (game_device_data.skip_game_sharpen && original_shader_hashes.Contains(0x747C6210u, reshade::api::shader_stage::pixel))
      {
         return DrawOrDispatchOverrideType::Replaced; // skip the game's post-tonemap sharpener
      }

      if (game_device_data.dlss_hook_mode == 1 && original_shader_hashes.Contains(shader_hashes_AA))
      {
         // In PreTAA mode the game's own resolve must not run: it would overwrite our result.
         return DrawOrDispatchOverrideType::Replaced;
      }
#else
      const bool is_pre_taa_copy = false;
#endif

      // Both of the checks below have to run for EVERY draw, which is why they sit above the early
      // return further down rather than next to the resolve handling: that return drops every draw
      // which is not the resolve, so a check placed below it can only ever fire while the resolve is
      // already running, which is the exact opposite of what these two are for. Both were written
      // below it first and both were consequently dead - the pre-rendered-video counter stayed at
      // zero for ever (so the panel line never left "not seen"), and the "no temporal resolve is
      // running" warning could never appear at all.
      if (original_shader_hashes.Contains(shader_hashes_Tonemap))
      {
         game_device_data.tonemap_this_frame = true;
      }
      {
         uint32_t video_ps = 0;
         for (auto v : original_shader_hashes.pixel_shaders)
         {
            if (v == SHADER_HASH_NONE) break;
            video_ps = static_cast<uint32_t>(v);
            break;
         }
         if (video_ps == 0x00D96EAEu)
         {
            if (game_device_data.video_pass_draws == 0)
            {
               Probe::Line(std::format("[video] the video / letterbox pass (0x00D96EAE) drew for the first time at "
                                       "frame {} - what is on screen is a pre-rendered video, so there is no geometry "
                                       "for an anti aliasing pass to work on",
                  Probe::s_frame));
            }
            ++game_device_data.video_pass_draws;
         }
      }

      // Scale the strength of the game's own post-tonemap sharpening pass. This is *not* inside the
      // DEVELOPMENT block above: it is a shipping feature, whereas "skip_game_sharpen" (cancelling
      // the draw entirely, which turns the screen black) is a dev switch. Only the constant buffer
      // is touched, the draw itself always runs.
      if (game_sharpen_scale != 1.f && original_shader_hashes.Contains(shader_hashes_GameSharpen))
      {
         ScaleGameSharpenStrength(native_device, native_device_context, game_device_data);
      }

      // Nothing else below this point is of interest for draws that are not the temporal resolve.
      if (!is_pre_taa_copy && !original_shader_hashes.Contains(shader_hashes_AA))
      {
         return DrawOrDispatchOverrideType::None;
      }

      // Counted here, once, on every path that reaches the SR code below.
      ++game_device_data.aa_seen;
      ++game_device_data.aa_this_frame;

#if DEVELOPMENT
      // One shot: dump the draws that led up to the resolve, oldest first.
      if (game_device_data.s_pre_taa_dumped == 0)
      {
         game_device_data.s_pre_taa_dumped = 1;
         std::string dump = std::format("--- the {} draws BEFORE the resolve (oldest first) ---", kPreTaaRing);
         for (int q = 0; q < kPreTaaRing; ++q)
         {
            const auto& e = game_device_data.s_pre_taa_ring[(game_device_data.s_pre_taa_ring_pos + q) % kPreTaaRing];
            dump += std::format("\n  pre[{}] PS 0x{:08X}{}", q, e.hash, e.rtv);
         }
         Probe::Line(dump);
      }
#endif

      // Which resolve is this? Both variants are hooked and log separately.
      // (0x2B974E48 is not listed any more: it is FXAA over a single channel R32 buffer.)
      const char* resolve_name;
      int resolve_index;
      if (is_pre_taa_copy)
      {
         resolve_name = "0xA75736C5(PreTAA copy)";
         resolve_index = 2;
      }
      else if (original_shader_hashes.Contains(0x7B9B914Fu, reshade::api::shader_stage::pixel))
      {
         resolve_name = "7B9B914F(TAA)";
         resolve_index = 1;
      }
      else
      {
         resolve_name = "37B05605(TAA)";
         resolve_index = 0;
      }
      // Note the first sight of each resolve variant, and nothing else.
      //
      // This used to log every change of "which variant ran last", which in practice fires twice per
      // frame: 0x37B05605 runs first and 0x7B9B914F second (the character/skin layer resolve that
      // cancel_second_taa then throws away), so the two alternate and the log grew by two lines per
      // frame - 358 KB for a two minute session, and the alternation drowned out everything else.
      // What is actually worth knowing per variant is whether it has ever run at all, so this is a
      // one shot bit per variant now. The live value is on the panel instead.
      const uint32_t resolve_bit = 1u << static_cast<uint32_t>(resolve_index);
      if ((game_device_data.resolve_seen_mask & resolve_bit) == 0)
      {
         game_device_data.resolve_seen_mask |= resolve_bit;
         Probe::Line(std::format("[resolve] variant {} ({}) first seen at frame {}",
            resolve_index, resolve_name, Probe::s_frame));
      }
      game_device_data.resolve_last_index = resolve_index;

#if DEVELOPMENT
      // Diagnostics: a resolve after the first one in the frame is the engine resolving "another
      // part of the image" (this game: the second call covers the character/skin layer). Log the
      // target it writes, once, so we know exactly what cancel_second_taa throws away.
      if (game_device_data.aa_this_frame > 1 && game_device_data.extra_resolve_logged < 3)
      {
         ++game_device_data.extra_resolve_logged;
         ID3D11RenderTargetView* extra_rtv = nullptr;
         native_device_context->OMGetRenderTargets(1, &extra_rtv, nullptr);
         std::string extra_target = "(no RTV)";
         if (extra_rtv != nullptr)
         {
            com_ptr<ID3D11Resource> extra_resource;
            extra_rtv->GetResource(&extra_resource);
            extra_target = Probe::Describe(extra_resource.get());
            extra_rtv->Release();
         }
         Probe::Line(std::format("[resolve #{} in frame {}] {} writes {}", game_device_data.aa_this_frame,
            Probe::s_frame, resolve_name, extra_target));
      }
#endif

#if ENABLE_SR
      // ---------------------------------------------------------------------------------
      // Replace the game's AA with DLSS (DLAA: no upscaling, render res == output res).
      // ---------------------------------------------------------------------------------
      // The resolve is called more than once per frame (measured: 2 hooks per DLSS draw), the
      // later one for another part of the image - here the character / skin layer. DLSS only runs
      // once per frame and writes the whole frame, so a later occurrence is cancelled (see
      // cancel_second_taa) instead of letting the game's TAA re-resolve that layer over our
      // output; if SR is unavailable it is left alone so the game keeps its own AA.
      const bool is_first_aa_this_frame = (game_device_data.aa_this_frame == 1) || !game_device_data.cancel_second_taa;

      if (!is_first_aa_this_frame)
      {
         // Later occurrence in the same frame: DLSS already produced the whole frame.
         if (device_data.sr_type != SR::Type::None && !device_data.sr_suppressed && device_data.has_drawn_sr)
         {
            return DrawOrDispatchOverrideType::Replaced; // cancel, do not draw SR again
         }
         return DrawOrDispatchOverrideType::None; // SR not available - let the game do its AA
      }

      if (device_data.sr_type == SR::Type::None || device_data.sr_suppressed || device_data.has_drawn_sr)
      {
         return DrawOrDispatchOverrideType::None; // let the game do its own AA
      }

      auto* sr_instance_data = device_data.GetSRInstanceData();
      auto* sr_implementation = sr_implementations.count(device_data.sr_type) ? sr_implementations[device_data.sr_type].get() : nullptr;
      if (sr_instance_data == nullptr || sr_implementation == nullptr)
      {
         return DrawOrDispatchOverrideType::None;
      }

      com_ptr<ID3D11ShaderResourceView> srvs[8];
      native_device_context->PSGetShaderResources(0, ARRAYSIZE(srvs), &srvs[0]);
      com_ptr<ID3D11RenderTargetView> rtv;
      native_device_context->OMGetRenderTargets(1, &rtv, nullptr);
      for (UINT i = 0; i < ARRAYSIZE(srvs); ++i)
         game_device_data.aa_srv[i] = srvs[i];

      // Both resolve disassemblies use t0=current colour, t1=history and t2=UV motion.
      // RenoDX can upgrade either colour texture to FP16 after a video/effect. Scanning for the
      // first R11/FP16 then mistakes history for colour or colour for motion. Keep the roles fixed
      // and validate the live views instead; an R32 t0 is still not a usable colour input.
      const int slot = game_device_data.aa_auto_slots ? 0 : game_device_data.aa_source_slot;
      const int mv_slot = game_device_data.aa_auto_slots ? 2 : game_device_data.aa_mv_slot;
      const UINT render_width = static_cast<UINT>(device_data.render_resolution.x + 0.5f);
      const UINT render_height = static_cast<UINT>(device_data.render_resolution.y + 0.5f);
      com_ptr<ID3D11Resource> source_color;
      com_ptr<ID3D11Resource> motion_vectors;
      com_ptr<ID3D11Resource> output_color;
      const bool source_is_valid = slot >= 0 && slot < static_cast<int>(ARRAYSIZE(srvs)) && MafiaDE::ReadResolveInput(srvs[slot].get(), render_width, render_height, false, source_color);
      const bool motion_is_valid = mv_slot >= 0 && mv_slot < static_cast<int>(ARRAYSIZE(srvs)) && MafiaDE::ReadResolveInput(srvs[mv_slot].get(), render_width, render_height, true, motion_vectors);
      if (!source_is_valid || !motion_is_valid || source_color == motion_vectors)
      {
         ++game_device_data.input_rejections;
         if (game_device_data.input_rejections <= 3 || Probe::s_frame % 300 == 0)
         {
            Probe::Line(std::format("[AA inputs] frame={} resolve={} rejected colour[{}]={} motion[{}]={} alias={} total={}",
               Probe::s_frame, resolve_name, slot, source_is_valid, mv_slot, motion_is_valid,
               source_color == motion_vectors, game_device_data.input_rejections));
            for (UINT i = 0; i < 3; ++i)
            {
               com_ptr<ID3D11Resource> resource;
               D3D11_SHADER_RESOURCE_VIEW_DESC view_desc = {};
               if (srvs[i])
               {
                  srvs[i]->GetResource(&resource);
                  srvs[i]->GetDesc(&view_desc);
               }
               Probe::Line(std::format("  t{} view=0x{:X} resource=0x{:X} view_format={} {}", i,
                  reinterpret_cast<uintptr_t>(srvs[i].get()), reinterpret_cast<uintptr_t>(resource.get()),
                  static_cast<int>(view_desc.Format), Probe::Describe(resource.get())));
            }
         }
         return DrawOrDispatchOverrideType::None;
      }
      if (rtv) rtv->GetResource(&output_color);

      com_ptr<ID3D11Texture2D> output_texture;
      if (!source_color || !output_color || FAILED(output_color->QueryInterface(&output_texture)) || !output_texture)
      {
         return DrawOrDispatchOverrideType::None;
      }

      D3D11_TEXTURE2D_DESC output_desc = {};
      output_texture->GetDesc(&output_desc);
      D3D11_RENDER_TARGET_VIEW_DESC output_view_desc = {};
      rtv->GetDesc(&output_view_desc);
      // NGX writes a whole texture. A different slice, mip or aliased input cannot be represented
      // by this resource-only interface, and the no-UAV fallback must remain a same-format copy.
      if (output_desc.Width != render_width || output_desc.Height != render_height || output_desc.MipLevels != 1 || output_desc.ArraySize != 1 || output_desc.SampleDesc.Count != 1 || output_view_desc.ViewDimension != D3D11_RTV_DIMENSION_TEXTURE2D || output_view_desc.Texture2D.MipSlice != 0 || (output_desc.Format != DXGI_FORMAT_R11G11B10_FLOAT && output_desc.Format != DXGI_FORMAT_R16G16B16A16_FLOAT) || output_view_desc.Format != output_desc.Format || output_color == source_color || output_color == motion_vectors)
      {
         if (game_device_data.invalid_output_reports++ < 3)
            Probe::Line(std::format("[AA output] frame={} resolve={} rejected {} view_format={}",
               Probe::s_frame, resolve_name, Probe::Describe(output_color.get()), static_cast<int>(output_view_desc.Format)));
         return DrawOrDispatchOverrideType::None;
      }

      SR::SettingsData settings_data;
      settings_data.output_width = static_cast<unsigned int>(device_data.output_resolution.x + 0.5f);
      settings_data.output_height = static_cast<unsigned int>(device_data.output_resolution.y + 0.5f);
      settings_data.render_width = static_cast<unsigned int>(device_data.render_resolution.x + 0.5f);
      settings_data.render_height = static_cast<unsigned int>(device_data.render_resolution.y + 0.5f);
      settings_data.hdr = game_device_data.sr_hdr != 0;
      settings_data.inverted_depth = game_device_data.sr_inverted_depth != 0;
      settings_data.mvs_jittered = game_device_data.sr_mvs_jittered != 0;
      settings_data.auto_exposure = game_device_data.sr_auto_exposure != 0;
      const float mv_sign = game_device_data.mv_scale_sign > 0 ? 1.f : -1.f;
      settings_data.mvs_x_scale = (game_device_data.sr_mv_scale_by_resolution ? device_data.render_resolution.x : 1.f) * mv_sign;
      settings_data.mvs_y_scale = (game_device_data.sr_mv_scale_by_resolution ? device_data.render_resolution.y : 1.f) * mv_sign;
      settings_data.render_preset = dlss_render_preset;

      const bool settings_ok = sr_implementation->UpdateSettings(sr_instance_data, native_device_context, settings_data);
      if (!settings_ok)
      {
         // A failed feature recreation can leave no usable NGX instance to evaluate.
         device_data.force_reset_sr = true;
         ++game_device_data.settings_failures;
         if (game_device_data.settings_failures <= 3 || Probe::s_frame % 300 == 0)
            Probe::Line(std::format("[SR settings] frame={} resolve={} UpdateSettings failed, using game AA", Probe::s_frame, resolve_name));
         return DrawOrDispatchOverrideType::None;
      }

      // DLSS wants a UAV on the output. This game's AA target already has D3D11_BIND_UAV
      // (observed bind flags 0xA8), so normally we can use it directly.
      com_ptr<ID3D11Resource> dlss_output = output_color;
      const bool output_has_uav = (output_desc.BindFlags & D3D11_BIND_UNORDERED_ACCESS) != 0;
      if (!output_has_uav)
      {
         D3D11_TEXTURE2D_DESC desc = output_desc;
         desc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;
         game_device_data.dlss_output_color = nullptr;
         if (FAILED(native_device->CreateTexture2D(&desc, nullptr, &game_device_data.dlss_output_color)) || !game_device_data.dlss_output_color)
         {
            return DrawOrDispatchOverrideType::None;
         }
         dlss_output = game_device_data.dlss_output_color.get();
      }

      SR::SuperResolutionImpl::DrawData draw_data;
      draw_data.source_color = source_color.get();
      draw_data.output_color = dlss_output.get();

      // Keep the validated MV resource alive through Draw; missing motion falls back to TAA.
      draw_data.motion_vectors = motion_vectors.get();

      // No valid main depth has been seen yet (a scene that never runs the position fill pass, or
      // the very first frames): DLSS/DLAA cannot reproject without it, so leave the game's own AA
      // in charge for this frame rather than handing the SR a null or a wrong depth.
      if (!game_device_data.main_depth)
      {
         return DrawOrDispatchOverrideType::None;
      }
      draw_data.depth_buffer = game_device_data.main_depth.get();

      // Read the game's own sub-pixel jitter from the resolve's pixel shader constant buffer slot
      // 0, floats 32/33. Found by diffing dumps across frames: they are the only values that
      // change every frame and they sit in Luma's expected -0.5..0.5 pixel space, quantised to an
      // 8 phase Halton sequence. They are handed to DLSS as-is - no NDC conversion and no sign
      // flip, because DLSS has to be told the offset the game really applied to the projection.
      {
         ID3D11Buffer* cbs[1] = {};
         native_device_context->PSGetConstantBuffers(0, 1, cbs);
         if (cbs[0])
         {
            D3D11_BUFFER_DESC src = {};
            cbs[0]->GetDesc(&src);
            if (src.ByteWidth >= 136 * sizeof(float) && src.ByteWidth <= 4096)
            {
               if (!game_device_data.cb_staging || game_device_data.cb_staging_size != src.ByteWidth)
               {
                  game_device_data.cb_staging = nullptr;
                  D3D11_BUFFER_DESC st = {};
                  st.ByteWidth = src.ByteWidth;
                  st.Usage = D3D11_USAGE_STAGING;
                  st.BindFlags = 0;
                  st.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                  if (SUCCEEDED(native_device->CreateBuffer(&st, nullptr, &game_device_data.cb_staging)))
                  {
                     game_device_data.cb_staging_size = src.ByteWidth;
                  }
               }
               if (game_device_data.cb_staging)
               {
                  native_device_context->CopyResource(game_device_data.cb_staging.get(), cbs[0]);
                  D3D11_MAPPED_SUBRESOURCE m = {};
                  if (SUCCEEDED(native_device_context->Map(game_device_data.cb_staging.get(), 0, D3D11_MAP_READ, 0, &m)))
                  {
                     const float* f = static_cast<const float*>(m.pData);
                     game_device_data.jitter_px = float2(f[32], f[33]);
                     native_device_context->Unmap(game_device_data.cb_staging.get(), 0);
                  }
               }
            }
            cbs[0]->Release();
         }
      }

#if DEVELOPMENT
      const float2 jitter_value = game_device_data.jitter_one_frame_late
                                     ? game_device_data.jitter_prev
                                     : game_device_data.jitter_px;
      game_device_data.jitter_prev = game_device_data.jitter_px;
      const float jitter_sign = game_device_data.jitter_flip ? -1.f : 1.f;
#else
      const float2 jitter_value = game_device_data.jitter_px;
      const float jitter_sign = 1.f;
#endif
      draw_data.jitter_x = jitter_value.x * jitter_sign;
      draw_data.jitter_y = jitter_value.y * jitter_sign;
      draw_data.reset = device_data.force_reset_sr;

      const bool draw_ok = sr_implementation->Draw(sr_instance_data, native_device_context, draw_data);

      D3D11_SHADER_RESOURCE_VIEW_DESC colour_view_desc = {};
      srvs[slot]->GetDesc(&colour_view_desc);
      const uint32_t colour_format_bit = colour_view_desc.Format == DXGI_FORMAT_R11G11B10_FLOAT ? 1u : 2u;
      if (game_device_data.logged_each[resolve_index] == 0 || (game_device_data.logged_colour_formats[resolve_index] & colour_format_bit) == 0 || ((game_device_data.logged_draw_ok[resolve_index] != draw_ok || draw_data.reset) && Probe::s_frame - game_device_data.last_sr_log_frame[resolve_index] >= 60))
      {
         game_device_data.logged_each[resolve_index] = 1;
         game_device_data.logged_colour_formats[resolve_index] |= colour_format_bit;
         game_device_data.logged_draw_ok[resolve_index] = draw_ok;
         game_device_data.last_sr_log_frame[resolve_index] = Probe::s_frame;
         std::string srv_lines;
         for (UINT i = 0; i < ARRAYSIZE(game_device_data.aa_srv); ++i)
         {
            if (!game_device_data.aa_srv[i]) continue;
            com_ptr<ID3D11Resource> res;
            game_device_data.aa_srv[i]->GetResource(&res);
            D3D11_SHADER_RESOURCE_VIEW_DESC view_desc = {};
            game_device_data.aa_srv[i]->GetDesc(&view_desc);
            srv_lines += std::format("\n    SRV[{}] view=0x{:X} resource=0x{:X} view_format={} {}", i,
               reinterpret_cast<uintptr_t>(game_device_data.aa_srv[i].get()), reinterpret_cast<uintptr_t>(res.get()),
               static_cast<int>(view_desc.Format), Probe::Describe(res.get()));
         }
         Probe::Line(std::format("=== SR hook (frame {}) draw_ok={}  resolve={} ===\n"
                                 "  validated slots: colour={} motion={}\n"
                                 "  all SRVs the resolve had bound:{}\n"
                                 "  we used source : {}\n  output  : {}  (uav={})\n"
                                 "  depth   : {}\n  mv      : {}\n"
                                 "  jitter={:.4},{:.4} (cb0 {:.6},{:.6})  mv_scale={},{}  mvs_jittered={}  hdr={}  auto_exposure={}  inverted_depth={}\n"
                                 "  render  = {}x{}   output = {}x{}   min_res={}   UpdateSettings={}   reset={}",
            Probe::s_frame, draw_ok, resolve_name, slot, mv_slot, srv_lines,
            Probe::Describe(source_color.get()),
            Probe::Describe(output_color.get()), output_has_uav,
            Probe::Describe(game_device_data.main_depth.get()),
            Probe::Describe(draw_data.motion_vectors),
            draw_data.jitter_x, draw_data.jitter_y,
            game_device_data.jitter_px.x, game_device_data.jitter_px.y,
            settings_data.mvs_x_scale, settings_data.mvs_y_scale,
            settings_data.mvs_jittered, settings_data.hdr, settings_data.auto_exposure, settings_data.inverted_depth,
            static_cast<int>(settings_data.render_width), static_cast<int>(settings_data.render_height),
            static_cast<int>(settings_data.output_width), static_cast<int>(settings_data.output_height),
            sr_instance_data->min_resolution, settings_ok, draw_data.reset));
      }

      if (!draw_ok)
      {
         // DLSS refused this input set - fall back to the game's own AA for this frame.
         device_data.force_reset_sr = true;
         ++game_device_data.draw_failures;
         return DrawOrDispatchOverrideType::None;
      }

      // A reset is consumed only by a successful evaluation. Failed settings/draws must not lose it.
      device_data.force_reset_sr = false;
      if (draw_data.reset) ++game_device_data.sr_resets;
      ++game_device_data.dlss_drawn;
      device_data.has_drawn_sr = true;
      device_data.taa_detected = true;

      // The game's AA draw never ran, so nothing wrote the render target: push our result in.
      if (!output_has_uav)
      {
         native_device_context->CopyResource(output_color.get(), dlss_output.get());
      }

      // Sharpen the SR result before handing the frame back to the game.
      DrawSharpen(native_device, native_device_context, device_data, game_device_data, output_color.get());

      return DrawOrDispatchOverrideType::Replaced; // cancel the game's AA draw
#else
      return DrawOrDispatchOverrideType::None;
#endif // ENABLE_SR
   }

   // Log the game's output format without changing it.
   void OnInitSwapchain(reshade::api::swapchain* swapchain) override
   {
      const auto desc = swapchain->get_device()->get_resource_desc(swapchain->get_back_buffer(0));
      Probe::Line(std::format("[swapchain] game back buffer format: {} ({}x{}); Luma HDR upgrade and display composition disabled",
         static_cast<int>(desc.texture.format), desc.texture.width, desc.texture.height));
   }

   void OnPresent(ID3D11Device* native_device, DeviceData& device_data) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);

      ++Probe::s_frame;

#if DEVELOPMENT
      // Diagnostics: dump this frame's pixel shader set if it changed. See "EndFrameShaderSet" -
      // this is what shows the AA quality switch and the cutscene render path.
      Probe::EndFrameShaderSet();

      // The tail starts again at the next tonemapper, not at the next frame boundary, but the
      // boundary is the only place this hook runs, so clear it here. See the "s_post" declaration.
      game_device_data.s_post_tonemap_seen = 0;
#endif

#if DEVELOPMENT
      // One shot: the draws that ran between the tonemapper and Present. See the field declaration
      // for why (0x747C6210 never shows up, so we need to see what actually assembles the image).
      if (game_device_data.s_tail_dumped == 0 && game_device_data.s_tail_tonemap_seen)
      {
         game_device_data.s_tail_dumped = 1;
         std::string dump = std::format("[frame tail] last {} draws after the tonemapper, oldest first:\n", kTailRing);
         for (int i = 0; i < kTailRing; i++)
         {
            const auto& e = game_device_data.s_tail_ring[(game_device_data.s_tail_ring_pos + i) % kTailRing];
            if (e.hash == 0 && e.rtv[0] == 0) continue;
            char buf[16];
            snprintf(buf, sizeof(buf), "%08X", e.hash);
            dump += "  ";
            dump += (e.hash != 0 ? buf : "--------");
            dump += e.rtv;
            dump += "\n";
         }
         Probe::Line(dump);
      }
#endif

      // Frame boundary resets.
      //
      // "device_data.has_drawn_sr" is a latch: the core only ever *reads* it, the mod is expected
      // to clear it every frame (this is what Mafia III's mod does at the end of its frame
      // function). Without this the very first DLSS draw latches the flag and every later frame
      // skips the hook - we measured "resolve hooked 3962x, DLSS drawn 1x".
      const bool sr_drawn_this_frame = device_data.has_drawn_sr;
      const int resolves_this_frame = game_device_data.aa_this_frame;
      // A resolve can run while its inputs/settings are rejected. That is a history gap too,
      // even though aa_this_frame is nonzero; reset once when DLAA next succeeds.
      if (!sr_drawn_this_frame) device_data.force_reset_sr = true;
      device_data.has_drawn_sr = false;
      if (game_device_data.aa_this_frame == 0)
      {
         device_data.taa_detected = false;
      }
      // The world was drawn but no resolve ran, i.e. the game's Anti-Aliasing is not on Low. Counted
      // so the panel can say so rather than leaving the user to conclude the mod does nothing. Either
      // half changing resets the count, and a cutscene has no tonemapper either, so it does not
      // count towards this.
      if (game_device_data.tonemap_this_frame && game_device_data.aa_this_frame == 0)
      {
         game_device_data.no_resolve_frames = (std::min)(game_device_data.no_resolve_frames + 1, 100000);
      }
      else
      {
         game_device_data.no_resolve_frames = 0;
      }
      // The same count without the tonemapper condition, for the "DLSS:" readout: during a cutscene
      // no resolve runs either, so that line has to stop saying "active" - but a cutscene is not a
      // fault, which is why the warning keeps the tonemapper condition that this one drops.
      if (game_device_data.aa_this_frame == 0)
      {
         game_device_data.no_resolve_any_frames = (std::min)(game_device_data.no_resolve_any_frames + 1, 100000);
      }
      else
      {
         game_device_data.no_resolve_any_frames = 0;
      }
      game_device_data.tonemap_this_frame = false;
      game_device_data.aa_this_frame = 0;
      // Only log when the qualitative state changed, or every 300 frames.
      //
      // The condition used to compare the running totals (aa_seen / dlss_drawn). Those advance on
      // every frame that resolves, so it was true on essentially every frame and the 300-frame rate
      // limit never applied: a nine minute release-build session wrote 9486 of these lines into a
      // 1.1 MB log, which put the line cap about fifteen minutes out. The totals are still worth
      // having, but as a sampled rate - which is what the heartbeat is for.
      const int sr_state = (device_data.sr_type == SR::Type::None) ? 0
                                                                   : (game_device_data.dlss_drawn > 0 ? 2 : 1);
      const bool state_changed = (sr_state != game_device_data.last_sr_state);
      game_device_data.last_sr_state = sr_state;
      if (state_changed || Probe::s_frame % 300 == 0)
      {
         Probe::Line(std::format("[frame {}] draws={}  resolve hooked {}x, SR drawn {}x, sr={}  (resolve list size {}, posfill list size {})"
                                 "  resolves_this_frame={} sr_drawn_this_frame={} resets={} input_rejections={} settings_failures={} draw_failures={}",
            Probe::s_frame, Probe::s_draw_calls, game_device_data.aa_seen, game_device_data.dlss_drawn,
            device_data.sr_type == SR::Type::None ? "NO" : "yes",
            shader_hashes_AA.pixel_shaders.size(),
            shader_hashes_PositionBufferFill.pixel_shaders.size(),
            resolves_this_frame, sr_drawn_this_frame, game_device_data.sr_resets,
            game_device_data.input_rejections, game_device_data.settings_failures, game_device_data.draw_failures));
      }
   }

   void LoadConfigs() override
   {
      reshade::api::effect_runtime* runtime = nullptr;

      reshade::get_config_value(runtime, NAME, "GameSetting01", cb_luma_global_settings.GameSettings.GameSetting01);
      reshade::get_config_value(runtime, NAME, "GameSetting02", cb_luma_global_settings.GameSettings.GameSetting02);
      reshade::get_config_value(runtime, NAME, "TextureFormatUpgrades", texture_upgrades_enabled);
      ApplyTextureUpgradeSetting();
      // Same persistence as the texture upgrade switch above: "LoadConfigs" has no device, so this
      // has to be a member rather than something living in the per device data.
      float sharpen_scale_cfg = 1.f;
      reshade::get_config_value(runtime, NAME, "GameSharpenScale", sharpen_scale_cfg);
      game_sharpen_scale = (std::clamp)(sharpen_scale_cfg, 0.f, 2.f);
      // The one setting the panel exposes that the ini has to remember, because the measured best
      // answer for it contradicts the technically correct one. See "sr_mvs_jittered".
      int mvs_jittered_cfg = 0; // shipping default is OFF, so an absent key means OFF too
      reshade::get_config_value(runtime, NAME, "MVsJittered", mvs_jittered_cfg);
      g_mvs_jittered_ini = mvs_jittered_cfg != 0 ? 1 : 0;
      // DLSS render preset. "Default" is an unknown by design and is the worst place to be with
      // DLSS 310.9.x: it resolved to a model that mishandles reflective and transparent content in
      // this game. Measured on this title, over a rainy night scene and sunlit ground at noon:
      //
      //   preset   sharpness   power draw   noise flicker   reflective / transparent correctness
      //   E        lowest      lowest       lowest          not measured
      //   F        lowest      lowest       lowest          not measured
      //   J        high        low          lowest          WRONG
      //   K        highest     low          lowest          WRONG
      //   M        medium      very high    highest         CORRECT (matches the game's own TAA)
      //   L        high        highest      highest         not measured
      //
      // Reflective correctness is a separate axis from noise, and it is the one that decides this.
      // Measured on the same rainy night scene, against "none" mode (the game's own TAA, i.e. the
      // ground truth with no DLSS involved at all):
      //
      //   K: the neon behind the wet window is far too weak, its chroma is wrong, and the glow
      //      around the neon edges is missing entirely - emissive/transparent energy is being lost.
      //      mvs_jittered=OFF recovers a little chroma but it stays wrong.
      //   M: brightness and chroma both match "none" closely. Only sharpness is behind, and
      //      mvs_jittered=OFF makes it sharper than ON.
      //
      // That makes K unusable no matter how sharp or how quiet it is: losing an emissive layer is a
      // content error, and no post process can put it back, while M's softness is exactly the kind
      // of problem a sharpening pass can. So M is the default despite being the noisiest and one of
      // the most expensive presets - correctness first.
      //
      // The core's own tooltip for J/K ("issues with reflections and transparent effects /
      // volumetrics") is CORRECT and is exactly what was measured. An earlier version of this
      // comment claimed the opposite because the neon's *brightness* and three *noise* readings had
      // been lumped into one score. Do not "fix" this back.
      //
      // Open: L is the only candidate left for "correct AND sharp", and E/F for "correct and cheap"
      // (their reflection behaviour is unmeasured). Recorded in
      // REPORT.md §6.1.
      //
      // Only a default is overridden. A preset the user picked explicitly is never touched.
      if (dlss_render_preset == 0)
      {
         dlss_render_preset = 13; // NVSDK_NGX_DLSS_Hint_Render_Preset_M
         reshade::set_config_value(runtime, NAME, "DLSSRenderPreset", static_cast<int>(dlss_render_preset));
         Probe::Line("[preset] DLSS render preset was Default, pinned to M. M is the only measured preset "
                     "whose emissive/transparent layers (neon behind a wet window) match the game's own TAA; "
                     "K/J are sharper and quieter but lose that layer entirely. See "
                     "REPORT.md §6.1");
      }
      else
      {
         Probe::Line(std::format("[preset] DLSS render preset = {} (kept, the user picked it)", dlss_render_preset));
      }
   }

   void DrawImGuiSettings(DeviceData& device_data) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);

#if ENABLE_SR
      // The on/off choice is Luma's own "Super Resolution" combo in the core panel above us (that is
      // what "device_data.sr_type" follows), so we must not add a second control here. This block
      // used to write a "sr_user_type" variable that nothing in the mod ever read, which made three
      // "Selectable" lines look functional while changing nothing at all.
      //
      // "active" has to mean what a reader takes it to mean, which is "it is running". sr_type is
      // only the selection: it stays set while nothing is being resolved at all (Anti-Aliasing on
      // Medium/High), and a line claiming "DLSS: active" directly above a warning that nothing is
      // being replaced is the kind of contradiction that makes a whole panel look untrustworthy.
      const bool dlss_selected = (device_data.sr_type != SR::Type::None);
      // Two thresholds from the two counters, because the status line and the warning below it answer
      // different questions: "is it running right now" and "is it failing to run when it should". A
      // cutscene is neither - not running, but also not wrong.
      const bool resolve_running = (game_device_data.no_resolve_any_frames <= 90);
      const bool aa_setting_wrong = (game_device_data.no_resolve_frames > 90);
      const char* dlss_state = "inactive, the game runs its own AA";
      if (dlss_selected)
      {
         if (aa_setting_wrong)
         {
            dlss_state = "selected, but NOT running - Anti-Aliasing is not Low (see below)";
         }
         else if (!resolve_running)
         {
            dlss_state = "selected, idle right now - no 3D scene on screen to resolve";
         }
         else if (game_device_data.dlss_drawn == 0 && game_device_data.aa_seen > 0)
         {
            dlss_state = "selected, but every draw was refused - the game's own AA is in use";
         }
         else
         {
            dlss_state = "active";
         }
      }
      ImGui::Text("DLSS: %s", dlss_state);

      // Whether DLSS is actually *drawing* is a separate question from whether it is selected: a draw
      // can fail (the source slots or formats were not what DLSS accepts) and the mod then falls back
      // to the game's own AA for that frame without changing the line above.
      //
      // "running" likewise has to mean running now. resolve_last_index is which variant ran last, so
      // it keeps naming one long after the resolve has stopped - and the totals printed beside it
      // freeze at that same instant. Both would otherwise contradict the warning a few lines below.
      static const char* resolve_names[] = {"A (0x37B05605)", "B (0x7B9B914F)", "PreTAA copy (0xA75736C5)"};
      const char* resolve_now = (game_device_data.resolve_last_index >= 0 && game_device_data.resolve_last_index < 3)
                                   ? resolve_names[game_device_data.resolve_last_index]
                                   : "none seen yet";
      if (resolve_running)
      {
         ImGui::Text("  resolve running: %s   hooked %dx, DLSS drew %dx%s",
            resolve_now, game_device_data.aa_seen, game_device_data.dlss_drawn,
            game_device_data.dlss_drawn == 0 && game_device_data.aa_seen > 0
               ? "   <-- the resolve IS hooked, DLSS refuses every draw"
               : "");
      }
      else
      {
         ImGui::Text("  resolve NOT running (last was %s)   hooked %dx, DLSS drew %dx",
            resolve_now, game_device_data.aa_seen, game_device_data.dlss_drawn);
      }
      // Where the game's anti-aliasing setting stands, said out loud on every panel draw.
      //
      // This began as a warning that only appeared once the situation was already wrong, which
      // assumes the reader has been through these notes first. Plenty of people install a mod through
      // a manager that shows them no notes at all and then go straight into the game, so for them the
      // panel is the only place this will ever be said. It is therefore a permanent line, and it
      // turns into a loud warning when the state is actually wrong - the world being rendered with no
      // resolve running, which is what this game does at Medium and High (measured, REPORT.md §6.5).
      if (aa_setting_wrong)
      {
         ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.25f, 1.0f),
            "  [!] Anti-Aliasing is not LOW: no temporal resolve is running, so DLAA");
         ImGui::TextUnformatted("      has nothing to replace. Change it in the game's display settings.");
      }
      else
      {
         ImGui::TextUnformatted("  This mod requires the game's Anti-Aliasing to be set to LOW - Medium");
         ImGui::TextUnformatted("  and High run no temporal resolve, so DLAA cannot engage there.");
      }

      ImGui::Text("RCAS sharpen amount: %.2f%s", game_device_data.sharpen_amount,
         game_device_data.sharpen_amount == 0.f ? "  (off - the pass is skipped)" : "");
      if (ImGui::Button("Sharpen -0.05"))
      {
         game_device_data.sharpen_amount = (std::max)(-0.50f, game_device_data.sharpen_amount - 0.05f);
      }
      ImGui::SameLine();
      if (ImGui::Button("Sharpen +0.05"))
      {
         game_device_data.sharpen_amount = (std::min)(1.f, game_device_data.sharpen_amount + 0.05f);
      }
      if (ImGui::Button("Sharpen 0 (off)"))
      {
         game_device_data.sharpen_amount = 0.f;
      }
      // The MVJittered declaration, exposed because it is the one setting where the measured best
      // answer contradicts the technically correct one (see the long comment on "sr_mvs_jittered").
      // Shipping default is OFF: sharper and less noisy with DLSS 310.9.x, at the cost of a little
      // more edge aliasing. ON is the declaration the game's data says is right, and it comes out
      // softer and noisier. Either is defensible, so the user picks, and it is persisted.
      ImGui::Text("Motion vectors declared jittered: %s",
         game_device_data.sr_mvs_jittered ? "ON  (correct declaration, softer, noisier)"
                                          : "OFF (sharper, less noise, slightly more aliasing)");
      if (ImGui::Button(game_device_data.sr_mvs_jittered ? "  Set OFF (shipping default)" : "  Set ON (technically correct)"))
      {
         game_device_data.sr_mvs_jittered = !game_device_data.sr_mvs_jittered;
         g_mvs_jittered_ini = game_device_data.sr_mvs_jittered;
         reshade::set_config_value(nullptr, NAME, "MVsJittered", game_device_data.sr_mvs_jittered);
      }

      // The game's own post-tonemap sharpener is NOT exposed: the pass it lives in (0x747C6210) was
      // measured to never run during gameplay - the probe log never contains a single "[sharpen]"
      // line, and the pass prints on its first four draws - so scaling it does nothing at all. The
      // "ScaleGameSharpenStrength" code path is left in place in case a future title or a menu case
      // does run it, but a control that provably does nothing is worse than no control.
      // (The "negative RCAS values blur instead" trick also stays out: it is a development aid.)

#if DEVELOPMENT
      // ---------------------------------------------------------------------------------------
      // Everything below is a development switch. The defaults are the shipping values, they are
      // only exposed so a dev build can A/B them in game instead of rebuilding per guess.
      // ---------------------------------------------------------------------------------------
      ImGui::Separator();
      ImGui::Text("Dev switches:");
      ImGui::TextUnformatted("Output: game controlled (Luma HDR upgrade and display composition disabled)");
      if (ImGui::Button(game_device_data.cancel_second_taa ? "Second resolve in a frame: CANCELLED" : "Second resolve in a frame: allowed"))
      {
         game_device_data.cancel_second_taa = !game_device_data.cancel_second_taa;
      }
      if (ImGui::Button(game_device_data.jitter_one_frame_late ? "Jitter timing: previous frame (test)" : "Jitter timing: current frame"))
      {
         game_device_data.jitter_one_frame_late = !game_device_data.jitter_one_frame_late;
      }
      if (ImGui::Button(game_device_data.jitter_flip ? "Jitter sign: FLIPPED" : "Jitter sign: normal"))
      {
         game_device_data.jitter_flip = !game_device_data.jitter_flip;
      }
      // The game's temporal resolve computes "prevUV = curUV + t2.xy" while DLSS expects the opposite
      // sign convention for "mvs_x_scale"/"mvs_y_scale", so this is a per-game compatibility switch
      // that ships as "+1" ("as the game writes it"). Flipping it is only useful to diagnose a game
      // patch that changes the convention (it shows up as ghosting/smearing on motion), so it is not
      // exposed in the release panel.
      if (ImGui::Button(game_device_data.mv_scale_sign > 0 ? "MV sign: as the game writes it" : "MV sign: FLIPPED (test)"))
      {
         game_device_data.mv_scale_sign = -game_device_data.mv_scale_sign;
      }
      ImGui::Text("  jitter (pixel space) = %.6f, %.6f", game_device_data.jitter_px.x, game_device_data.jitter_px.y);
      if (ImGui::Button(game_device_data.dlss_hook_mode == 1 ? "SR trigger: PreTAA copy (0xA75736C5, Mafia III style)" : "SR trigger: the resolve itself"))
      {
         game_device_data.dlss_hook_mode = !game_device_data.dlss_hook_mode;
      }
      if (ImGui::Button(game_device_data.skip_game_sharpen ? "Game sharpen (0x747C6210): SKIPPED (can black screen)" : "Game sharpen (0x747C6210): allowed"))
      {
         game_device_data.skip_game_sharpen = !game_device_data.skip_game_sharpen;
      }
      // Frame tail constant buffer snapshot. Press it, change ONE setting in the game, and the whole
      // diff is written to the log when the ~15 s window closes: which passes ran after the
      // tonemapper, and which of their constants the setting moved.
      if (ImGui::Button(game_device_data.s_post_cb_arm > 0 ? "Snapshot post constants: ARMED" : "Snapshot post constants (15s)"))
      {
         for (int i = 0; i < game_device_data.s_post_count; i++)
         {
            game_device_data.s_post[i] = MafiaDefinitiveEditionGameDeviceData::PostPassLog();
         }
         game_device_data.s_post_count = 0;
         game_device_data.s_post_tonemap_seen = 0;
         game_device_data.s_post_cb_arm_frame = Probe::s_frame;
         game_device_data.s_post_cb_arm = 1;
         Probe::Line(std::format("[post] ==== ARMED at frame {}, window {} frames, baseline is now ====", Probe::s_frame, kPostArmFrames));
      }
      ImGui::SameLine();
      ImGui::TextUnformatted("(then change ONE graphics setting in the game)");

      // Tonemapper probe: patches the game's own constant buffer on the way to the draw. Mode 1 is
      // the interesting one, it makes the game output linear instead of gamma 2.0 while keeping the
      // exact same tone curve, so Luma can be switched to reading the image as linear and the
      // result has to be pixel identical. Modes 2 and 3 additionally pick an operator that does not
      // clamp, which is the actual HDR attempt.
      static const char* tm_probe_names[] = {
         "off (game default: Uncharted2 + gamma 2.0)",
         "linear out (Uncharted2, no sqrt encode)",
         "linear out + operator -2 (no clamp)",
         "linear out + operator -3 (filmic fit, no clamp)",
      };
      ImGui::TextUnformatted("tonemapper probe:");
      if (ImGui::Button(tm_probe_names[game_device_data.tm_probe_mode]))
      {
         game_device_data.tm_probe_mode = (game_device_data.tm_probe_mode + 1) % 4;
         game_device_data.tm_probe_logged = 0;
         game_device_data.s_tm_dump_state = 0; // re-arm the one shot dump, so the exposure readout refreshes
         Probe::Line(std::format("[probe] tonemapper probe mode {} -> {}", game_device_data.tm_probe_mode, tm_probe_names[game_device_data.tm_probe_mode]));
      }
      ImGui::SameLine();
      ImGui::TextUnformatted("(needs Luma set to read the image as LINEAR to look right)");

      if (ImGui::Button(game_device_data.aa_auto_slots ? "Slots: resolve layout t0/t2 (shipping default)" : "Slots: manual"))
      {
         game_device_data.aa_auto_slots = !game_device_data.aa_auto_slots;
      }
      // ReShade exposes the ImGui API as plain function pointers whose parameters have no
      // defaults, so a button is simpler than fighting SliderInt's signature.
      ImGui::Text("manual colour slot: %d   manual MV slot: %d",
         game_device_data.aa_source_slot, game_device_data.aa_mv_slot);
      if (ImGui::Button("Next colour slot"))
      {
         game_device_data.aa_source_slot = (game_device_data.aa_source_slot + 1) % 8;
      }
      ImGui::SameLine();
      if (ImGui::Button("Next MV slot"))
      {
         game_device_data.aa_mv_slot = (game_device_data.aa_mv_slot + 1) % 8;
      }
      if (ImGui::Button(game_device_data.sr_auto_exposure ? "Auto exposure: ON" : "Auto exposure: OFF"))
      {
         game_device_data.sr_auto_exposure = !game_device_data.sr_auto_exposure;
      }
      if (ImGui::Button(game_device_data.sr_inverted_depth ? "Inverted depth (reversed-Z): ON" : "Inverted depth (reversed-Z): OFF"))
      {
         game_device_data.sr_inverted_depth = !game_device_data.sr_inverted_depth;
      }
      if (ImGui::Button(game_device_data.sr_hdr ? "HDR input: ON" : "HDR input: OFF"))
      {
         game_device_data.sr_hdr = !game_device_data.sr_hdr;
      }
      if (ImGui::Button(game_device_data.sr_mv_scale_by_resolution ? "MV scale: x render resolution (UV space, correct)" : "MV scale: 1.0 (pixel space, wrong)"))
      {
         game_device_data.sr_mv_scale_by_resolution = !game_device_data.sr_mv_scale_by_resolution;
      }
      // (The MVJittered toggle used to live here. It is now in the shipping part of the panel,
      // because it is a user facing choice rather than a development experiment - see the comment
      // next to "sr_mvs_jittered".)
      ImGui::Text("MVs jittered: %s (switch it in the panel section above)", game_device_data.sr_mvs_jittered ? "ON" : "OFF");
      ImGui::SameLine();
      // The MVJittered flag is a correctness declaration, not a quality setting: it tells DLSS the
      // motion vectors already carry the sub pixel jitter. The disassembly settles it for this game
      // (the resolve does "prevUV = curUV + MV" and never reads the jitter floats), so it stays on.
      //
      // What the jitter *timing* is, is a separate question and the open one. Reading the resolve's
      // cb0[32]/[33] may hand us the NEXT frame's offset, because the engine builds next frame's
      // projection matrices before drawing this frame. When that is what happens, DLSS reprojects
      // with an offset that is off by up to a pixel, and the symptom set is exactly: speckle on
      // bright high frequency content (sunlit floors, sky) plus a softer image because the temporal
      // filter stops trusting its history. That is why this is one button that cycles all four
      // combinations instead of two separate flags: the four variants have to be compared against
      // each other in one sitting, on the same scene, before any of them is believed.
      static const char* jitter_names[] = {
         "jitter: as read",
         "jitter: one frame late",
         "jitter: sign flipped",
         "jitter: one frame late + sign flipped",
      };
      const int jitter_variant = (game_device_data.jitter_one_frame_late ? 1 : 0) + (game_device_data.jitter_flip ? 2 : 0);
      if (ImGui::Button(jitter_names[jitter_variant]))
      {
         const int next = (jitter_variant + 1) % 4;
         game_device_data.jitter_one_frame_late = (next == 1 || next == 3);
         game_device_data.jitter_flip = (next >= 2);
         Probe::Line(std::format("[jitter] variant {} -> {} (one_frame_late={}, flip={})",
            (jitter_variant + 1) % 4, jitter_names[(jitter_variant + 1) % 4],
            game_device_data.jitter_one_frame_late, game_device_data.jitter_flip));
      }
      // Texture format upgrades: kept OFF. They are what an HDR path would need, but upgrading
      // R11G11B10 to FP16 produced black/silver blocks on car paint and floors in this game.
      // LoadConfigs() re-applies whatever ReShade.ini holds.
      ImGui::Text("Texture format upgrades: %s",
         texture_format_upgrades_type == TextureFormatUpgradesType::AllowedEnabled ? "ENABLED" : "off (shipping default)");
      if (ImGui::Button("  Enable"))
      {
         texture_format_upgrades_type = TextureFormatUpgradesType::AllowedEnabled;
      }
      ImGui::SameLine();
      if (ImGui::Button("  Disable"))
      {
         texture_format_upgrades_type = TextureFormatUpgradesType::None;
      }

      reshade::api::effect_runtime* runtime = nullptr;
      if (ImGui::SliderFloat("Game Setting #01", &cb_luma_global_settings.GameSettings.GameSetting01, 0.f, 1.f, "%.3f"))
         reshade::set_config_value(runtime, NAME, "GameSetting01", cb_luma_global_settings.GameSettings.GameSetting01);
      DrawResetButton(cb_luma_global_settings.GameSettings.GameSetting01, default_luma_global_game_settings.GameSetting01, "GameSetting01", runtime);
      ImGui::Separator();
#endif // DEVELOPMENT
#endif // ENABLE_SR
   }

   void PrintImGuiAbout() override
   {
      ImGui::Text("Mafia: Definitive Edition Luma mod", "");
      ImGui::Text("TAA replaced by DLSS / DLAA + RCAS sharpening", "");
      ImGui::Text("Probe log: LumaMafiaDE-probe.log next to the game executable", "");
      ImGui::Text("Built on the Luma Framework by Pumbo: https://github.com/Filoppi/Luma-Framework/", "");
   }
};

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
   if (ul_reason_for_call == DLL_PROCESS_ATTACH)
   {
      Probe::Line("[DllMain] entered DLL_PROCESS_ATTACH");
      g_own_module = hModule; // so OnLoad can report it; see the comment on g_own_module

      // Every step is logged, and the whole thing is wrapped, because the loader only ever
      // reports "1114" no matter what went wrong. An access violation inside DllMain does NOT
      // kill the process, it just becomes ERROR_DLL_INIT_FAILED, so without this there is
      // nothing to go on: the last logged step is the only clue.
      try
      {
         // NOTE: the third parameter (mod_website) MUST be a real string. globals.h's
         // SetGlobals() does a plain strncpy(WEBSITE, mod_website, ...) with no null check, so
         // passing nullptr dereferences address 0 - and the stock Luma _Template/main.cpp does
         // exactly that ("nullptr /*E.g. Nexus link*/").
         const char* project_name = PROJECT_NAME;
         const char* cleared_project_name = (project_name[0] == '_') ? (project_name + 1) : project_name;
         uint32_t mod_version = 1;
         Globals::SetGlobals(cleared_project_name, "Mafia: Definitive Edition Luma mod",
            "https://github.com/Filoppi/Luma-Framework/", mod_version);
         Probe::Line("[DllMain] step 1/5 SetGlobals OK");

         // DLAA only: leave output to the game, as in the other DLSS-only mods.
         swapchain_format_upgrade_type = TextureFormatUpgradesType::None;
         swapchain_upgrade_type = SwapchainUpgradeType::None;
         force_disable_display_composition = true;
         prevent_fullscreen_state = false;

         // Texture format upgrades stay OFF, even though DLSS nominally wants FP16 (the game keeps
         // its HDR buffers in R11G11B10_FLOAT) and Luma's own template enables them by default.
         // Reason: with them on, car paint and floors showed black/silver blocks (with or without
         // DLSS running). DLSS accepts the R11G11B10 inputs fine - what it actually needed was the
         // correct colour source slot. LoadConfigs() re-applies whatever ReShade.ini holds.
         texture_format_upgrades_type = TextureFormatUpgradesType::None;
         texture_upgrade_formats = {
            reshade::api::format::r8g8b8a8_unorm,
            reshade::api::format::r8g8b8a8_unorm_srgb,
            reshade::api::format::r8g8b8a8_typeless,
            reshade::api::format::b8g8r8a8_unorm,
            reshade::api::format::b8g8r8a8_unorm_srgb,
            reshade::api::format::b8g8r8a8_typeless,
            reshade::api::format::r10g10b10a2_unorm,
            reshade::api::format::r10g10b10a2_typeless,
            reshade::api::format::r11g11b10_float,
         };
         texture_format_upgrades_lut_size = 32;
         texture_format_upgrades_lut_dimensions = LUTDimensions::_2D;
         // No1Px must stay in the flags. Without it Luma asserts
         // "Upgrading 1x1 resource by aspect ratio, this is possibly unwanted"
         // (resource_upgrades.inl:214) on tiny helper resources such as the 1x1 exposure
         // buffer, popping a modal dialog every time. Note TextureFormatUpgrades2DSizeFilters::None
         // is defined as No1Px, i.e. Luma's own default includes it - the stock template's flag
         // combination drops it.
         texture_format_upgrades_2d_size_filters = (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainResolution | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainAspectRatio | (uint32_t)TextureFormatUpgrades2DSizeFilters::No1Px;

         enable_samplers_upgrade = false;
         Probe::Line("[DllMain] step 2/5 texture upgrades configured");

#if DEVELOPMENT
         // Names shown by the analyzer overlay. The three starred entries are the ones that must
         // never be cancelled - see the header block.
         forced_shader_names.emplace(static_cast<uint32_t>(std::stoul("37B05605", nullptr, 16)), "TAA resolve A <- replaced by DLSS");
         forced_shader_names.emplace(static_cast<uint32_t>(std::stoul("7B9B914F", nullptr, 16)), "TAA resolve B <- replaced by DLSS");
         forced_shader_names.emplace(static_cast<uint32_t>(std::stoul("2B974E48", nullptr, 16)), "FXAA on single-channel R32 (NOT the colour AA)");
         forced_shader_names.emplace(static_cast<uint32_t>(std::stoul("121A4A96", nullptr, 16)), "Position buffer fill (depth capture)");
         forced_shader_names.emplace(static_cast<uint32_t>(std::stoul("A75736C5", nullptr, 16)), "PreTAA copy (runs several times per frame) *");
         forced_shader_names.emplace(static_cast<uint32_t>(std::stoul("5C8BCE6A", nullptr, 16)), "Bloom bright pass *");
         forced_shader_names.emplace(static_cast<uint32_t>(std::stoul("747C6210", nullptr, 16)), "Post-tonemap sharpen *");
         forced_shader_names.emplace(static_cast<uint32_t>(std::stoul("916B1D65", nullptr, 16)), "Tonemap");
         forced_shader_names.emplace(static_cast<uint32_t>(std::stoul("A6CCD59B", nullptr, 16)), "Depth of field");
         forced_shader_names.emplace(static_cast<uint32_t>(std::stoul("D980FA68", nullptr, 16)), "LowHealth");
         forced_shader_names.emplace(static_cast<uint32_t>(std::stoul("3EB9D976", nullptr, 16)), "DOF variant 2");
         forced_shader_names.emplace(static_cast<uint32_t>(std::stoul("00D96EAE", nullptr, 16)), "Video / letterbox");
         forced_shader_names.emplace(static_cast<uint32_t>(std::stoul("A7799306", nullptr, 16)), "Alpha / mask pass");
#endif
         Probe::Line("[DllMain] step 3/5 forced shader names OK");

         game = new MafiaDefinitiveEditionGame();
         Probe::Line("[DllMain] step 4/5 game instance created");

         Probe::Line("[DllMain] calling CoreMain ...");
         CoreMain(hModule, ul_reason_for_call, lpReserved);
         Probe::Line("[DllMain] step 5/5 CoreMain returned normally");
      }
      catch (const std::exception& e)
      {
         Probe::Line(std::format("[DllMain] !! C++ exception escaped: {}", e.what()));
      }
      catch (...)
      {
         Probe::Line("[DllMain] !! unknown C++ exception escaped");
      }

      return TRUE;
   }

   CoreMain(hModule, ul_reason_for_call, lpReserved);

   return TRUE;
}
