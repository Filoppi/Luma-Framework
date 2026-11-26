#pragma once
#if defined(_M_IX86)
// Audio output upgrade for Brutal Legend's statically linked FMOD Ex 4.32.09 (BrutalLegend.exe).
// Hooks EventSystem::init, EventSystem::getSystemObject, System::setSoftwareFormat and System::setSpeakerMode with safetyhook.
// The System object is forced to WASAPI output, the resampler and speaker mode can be overridden.
// All-or-nothing: every target signature is verified before anything is hooked. The global EventSystem must be null (FMOD not yet started).
// Logs go to the ReShade log with the prefix "[BL FMOD]".

#include <windows.h>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <utility>
#include <safetyhook.hpp>

namespace BLFmodUpgrade
{
   struct Settings
   {
      int output_mode = 1; // 0 vanilla, 1 WASAPI
      int resampler = 3; // 0 vanilla, 1 linear, 2 cubic, 3 spline (matches FMOD_DSP_RESAMPLER values)
      int speaker_mode = 0; // 0 vanilla/auto, 1 stereo, 2 5.1, 3 7.1
      bool float_output = true; // true: mix as 32-bit float (FMOD_SOUND_FORMAT_PCMFLOAT), false: keep the game's format
   };

   static constexpr int kSoundFormatPcmFloat = 5; // FMOD_SOUND_FORMAT_PCMFLOAT
   static constexpr int kOutputAutodetect = 0; // FMOD_OUTPUTTYPE_AUTODETECT
   static constexpr int kOutputWasapi = 8; // FMOD_OUTPUTTYPE_WASAPI
   static constexpr int kSpeakerStereo = 2; // FMOD_SPEAKERMODE_STEREO
   static constexpr int kSpeaker51 = 5; // FMOD_SPEAKERMODE_5POINT1
   static constexpr int kSpeaker71 = 6; // FMOD_SPEAKERMODE_7POINT1

   // RVAs from the module base.
   static constexpr uintptr_t kRvaEventInit = 0x7F21D0; // EventSystem::init (stdcall, ret 0x14)
   static constexpr uintptr_t kRvaEventGetSystemObject = 0x7F22A0; // EventSystem::getSystemObject (stdcall, ret 8)
   static constexpr uintptr_t kRvaSysSetSoftwareFormat = 0x824F77; // System::setSoftwareFormat (stdcall, ret 0x18)
   static constexpr uintptr_t kRvaSysSetSpeakerMode = 0x825070; // System::setSpeakerMode (stdcall, ret 8)
   static constexpr uintptr_t kRvaSysSetOutput = 0x824ED2; // System::setOutput (stdcall, ret 8), called by us, not hooked
   static constexpr uintptr_t kRvaGlobalEventSystem = 0xC05A80; // global EventSystem pointer

   static const uint8_t kSigEventInit[16] = { 0x8B, 0x4C, 0x24, 0x04, 0x8D, 0x44, 0x24, 0x04, 0x50, 0x51, 0xE8, 0x41, 0x93, 0x00, 0x00, 0x83 };
   static const uint8_t kSigEventGetSystemObject[16] = { 0x8B, 0x4C, 0x24, 0x04, 0x8D, 0x44, 0x24, 0x04, 0x50, 0x51, 0xE8, 0x71, 0x92, 0x00, 0x00, 0x83 };
   static const uint8_t kSigSystemThunk[11] = { 0x55, 0x8B, 0xEC, 0x8D, 0x45, 0x08, 0x50, 0xFF, 0x75, 0x08, 0xE8 };

   static uintptr_t g_base = 0;
   static Settings g_settings;
   static safetyhook::InlineHook g_hook_init;
   static safetyhook::InlineHook g_hook_get_system_object;
   static safetyhook::InlineHook g_hook_set_software_format;
   static safetyhook::InlineHook g_hook_set_speaker_mode;
   static void* g_sys = nullptr; // System object from getSystemObject
   static bool g_output_done = false; // setOutput(WASAPI) was attempted once
   static bool g_forced_wasapi = false; // setOutput(WASAPI) returned OK
   static bool g_forced_float = false; // setSoftwareFormat was changed to PCMFLOAT

   static void Log(reshade::log::level lv, const char* fmt, ...)
   {
      char buf[512];
      va_list args;
      va_start(args, fmt);
      vsnprintf(buf, sizeof(buf), fmt, args);
      va_end(args);
      reshade::log::message(lv, buf);
   }

   // Direct call to the (unhooked) System::setOutput thunk.
   static int CallSetOutput(void* sys, int type)
   {
      using Fn = int(__stdcall*)(void*, int);
      return reinterpret_cast<Fn>(g_base + kRvaSysSetOutput)(sys, type);
   }

   // Compares `n` bytes at rva against `expect`. Logs the bytes actually found on mismatch.
   static bool MatchBytes(uintptr_t rva, const uint8_t* expect, size_t n, const char* name)
   {
      const uint8_t* p = reinterpret_cast<const uint8_t*>(g_base + rva);
      if (IsBadReadPtr(p, n))
      {
         Log(reshade::log::level::warning, "[BL FMOD] %s at RVA 0x%06X: unreadable", name, static_cast<unsigned>(rva));
         return false;
      }
      if (memcmp(p, expect, n) == 0)
         return true;
      char found[64] = {};
      for (size_t i = 0; i < n && i < 16; ++i)
         snprintf(found + i * 3, sizeof(found) - i * 3, "%02X ", p[i]);
      Log(reshade::log::level::warning, "[BL FMOD] %s at RVA 0x%06X: bytes mismatch (found %s)", name, static_cast<unsigned>(rva), found);
      return false;
   }

   static int __stdcall HookedEventInit(void* evsys, int maxch, unsigned flags, void* extra, unsigned eventflags)
   {
      int r = g_hook_init.stdcall<int>(evsys, maxch, flags, extra, eventflags);
      if (r != 0 && (g_forced_wasapi || g_forced_float) && g_sys)
      {
         if (g_forced_float)
            Log(reshade::log::level::warning, "[BL FMOD] EventSystem::init failed (%d) with float output forced, retrying once", r);
         Log(reshade::log::level::warning, "[BL FMOD] EventSystem::init failed (%d) with WASAPI, retrying with AUTODETECT output", r);
         const int so = CallSetOutput(g_sys, kOutputAutodetect);
         r = g_hook_init.stdcall<int>(evsys, maxch, flags, extra, eventflags);
         g_forced_wasapi = false;
         g_forced_float = false;
         Log(reshade::log::level::info, "[BL FMOD] setOutput(AUTODETECT) -> %d, EventSystem::init retry -> %d", so, r);
         return r;
      }
      if (r == 0)
         Log(reshade::log::level::info, "[BL FMOD] EventSystem::init OK (wasapi forced=%d)", g_forced_wasapi ? 1 : 0);
      else
         Log(reshade::log::level::warning, "[BL FMOD] EventSystem::init failed -> %d", r);
      return r;
   }

   static int __stdcall HookedGetSystemObject(void* evsys, void** out_sys)
   {
      const int r = g_hook_get_system_object.stdcall<int>(evsys, out_sys);
      if (r == 0 && out_sys && *out_sys && g_settings.output_mode != 0 && !g_output_done)
      {
         g_output_done = true;
         g_sys = *out_sys;
         const int so = CallSetOutput(g_sys, kOutputWasapi);
         g_forced_wasapi = (so == 0);
         Log(so == 0 ? reshade::log::level::info : reshade::log::level::warning, "[BL FMOD] System::setOutput(WASAPI) on %p -> %d", g_sys, so);
      }
      return r;
   }

   static int __stdcall HookedSetSoftwareFormat(void* sys, int rate, int fmt, int outch, int maxinch, int resampler)
   {
      const int new_resampler = g_settings.resampler != 0 ? g_settings.resampler : resampler;
      const int new_fmt = g_settings.float_output ? kSoundFormatPcmFloat : fmt;
      if (g_settings.float_output)
         g_forced_float = true;
      Log(reshade::log::level::info, "[BL FMOD] setSoftwareFormat rate=%d fmt=%d->%d outch=%d maxinch=%d resampler=%d -> %d",
          rate, fmt, new_fmt, outch, maxinch, resampler, new_resampler);
      return g_hook_set_software_format.stdcall<int>(sys, rate, new_fmt, outch, maxinch, new_resampler);
   }

   static int __stdcall HookedSetSpeakerMode(void* sys, int mode)
   {
      int new_mode = mode;
      switch (g_settings.speaker_mode)
      {
      case 1: new_mode = kSpeakerStereo; break;
      case 2: new_mode = kSpeaker51; break;
      case 3: new_mode = kSpeaker71; break;
      default: break;
      }
      Log(reshade::log::level::info, "[BL FMOD] setSpeakerMode mode=%d -> %d", mode, new_mode);
      return g_hook_set_speaker_mode.stdcall<int>(sys, new_mode);
   }

   static void Install(const Settings& settings)
   {
      g_settings = settings;
      g_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));

      const void* const* global_evsys = reinterpret_cast<const void* const*>(g_base + kRvaGlobalEventSystem);
      if (!IsBadReadPtr(global_evsys, sizeof(void*)) && *global_evsys != nullptr)
      {
         Log(reshade::log::level::warning, "[BL FMOD] FMOD already initialized, audio upgrade inactive");
         return;
      }

      // Verify everything first. Any mismatch hooks nothing.
      bool all_ok = true;
      all_ok = MatchBytes(kRvaEventInit, kSigEventInit, sizeof(kSigEventInit), "EventSystem::init") && all_ok;
      all_ok = MatchBytes(kRvaEventGetSystemObject, kSigEventGetSystemObject, sizeof(kSigEventGetSystemObject), "EventSystem::getSystemObject") && all_ok;
      all_ok = MatchBytes(kRvaSysSetSoftwareFormat, kSigSystemThunk, sizeof(kSigSystemThunk), "System::setSoftwareFormat") && all_ok;
      all_ok = MatchBytes(kRvaSysSetSpeakerMode, kSigSystemThunk, sizeof(kSigSystemThunk), "System::setSpeakerMode") && all_ok;
      all_ok = MatchBytes(kRvaSysSetOutput, kSigSystemThunk, sizeof(kSigSystemThunk), "System::setOutput") && all_ok;
      if (!all_ok)
      {
         Log(reshade::log::level::warning, "[BL FMOD] signature mismatch, audio upgrade inactive (nothing hooked)");
         return;
      }

      auto h_init = safetyhook::InlineHook::create(reinterpret_cast<void*>(g_base + kRvaEventInit), reinterpret_cast<void*>(&HookedEventInit));
      auto h_get = safetyhook::InlineHook::create(reinterpret_cast<void*>(g_base + kRvaEventGetSystemObject), reinterpret_cast<void*>(&HookedGetSystemObject));
      auto h_fmt = safetyhook::InlineHook::create(reinterpret_cast<void*>(g_base + kRvaSysSetSoftwareFormat), reinterpret_cast<void*>(&HookedSetSoftwareFormat));
      auto h_spk = safetyhook::InlineHook::create(reinterpret_cast<void*>(g_base + kRvaSysSetSpeakerMode), reinterpret_cast<void*>(&HookedSetSpeakerMode));
      if (!h_init || !h_get || !h_fmt || !h_spk)
      {
         Log(reshade::log::level::warning, "[BL FMOD] inline hook creation failed (init=%d get=%d fmt=%d spk=%d), audio upgrade inactive",
             h_init ? 1 : 0, h_get ? 1 : 0, h_fmt ? 1 : 0, h_spk ? 1 : 0);
         return; // the Result objects unhook whatever was created
      }
      g_hook_init = std::move(*h_init);
      g_hook_get_system_object = std::move(*h_get);
      g_hook_set_software_format = std::move(*h_fmt);
      g_hook_set_speaker_mode = std::move(*h_spk);

      Log(reshade::log::level::info, "[BL FMOD] audio upgrade installed: output=%d resampler=%d speaker=%d",
          g_settings.output_mode, g_settings.resampler, g_settings.speaker_mode);
   }
} // namespace BLFmodUpgrade

// Called from OnInit (main.cpp), next to InstallSdlRedirect.
static void InstallFmodUpgrade(const BLFmodUpgrade::Settings& settings)
{
   BLFmodUpgrade::Install(settings);
}
#else
template <typename T>
static void InstallFmodUpgrade(const T&) {}
#endif
