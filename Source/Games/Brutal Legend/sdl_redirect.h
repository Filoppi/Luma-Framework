#pragma once
#if defined(_M_IX86)
// Redirects Brutal Legend's statically linked SDL 2.0.3 input calls to a modern SDL2.dll placed next to the game exe.
// One virtual controller (g_vgc / g_vjoy) is handed to the game; it follows the most recently used real pad.
// All-or-nothing for the main entry points: every signature and call site is verified before anything is patched. The rumble expiry hooks are an optional pair, checked afterwards and installed together or skipped on mismatch.
// Logs go to the ReShade log with the prefix "[BL SDL]".

#include <windows.h>
#include <intrin.h>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace BLSdlRedirect
{
   typedef uint8_t Uint8;
   typedef uint16_t Uint16;
   typedef int16_t Sint16;
   typedef int32_t Sint32;
   typedef uint32_t Uint32;

   struct SDL_Joystick;
   struct SDL_GameController;
   struct SDL_RWops;
   struct SDL_Event
   {
      Uint32 type; // offset 0 in every SDL2 event
      Uint8 pad[52]; // total 56 bytes
   };
   struct SDL_version
   {
      Uint8 major, minor, patch;
   };

   static constexpr Uint32 kInitJoyGc = 0x2200; // SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER
   static constexpr Uint32 kEvAxisMotion = 0x650; // SDL_CONTROLLERAXISMOTION
   static constexpr Uint32 kEvButtonDown = 0x651; // SDL_CONTROLLERBUTTONDOWN
   static constexpr Uint32 kEvButtonUp = 0x652; // SDL_CONTROLLERBUTTONUP
   static constexpr Uint32 kEvDeviceAdded = 0x653; // SDL_CONTROLLERDEVICEADDED (which = device index)
   static constexpr Uint32 kEvDeviceRemoved = 0x654; // SDL_CONTROLLERDEVICEREMOVED (which = instance id)
   static constexpr int kAxisThreshold = 16000;
   static constexpr int kMaxLogLines = 200;
#if DEVELOPMENT
   static constexpr int kMaxRLogLines = 20000; // maximum lines written by the rumble trace
#endif

   // Modern SDL2 function table (SDLCALL is __cdecl on x86). Members are named after the SDL_ symbols without the prefix.
   struct ModernSDL
   {
      void (__cdecl* GetVersion)(SDL_version*) = nullptr;
      int (__cdecl* InitSubSystem)(Uint32) = nullptr;
      void (__cdecl* QuitSubSystem)(Uint32) = nullptr;
      void (__cdecl* PumpEvents)() = nullptr;
      int (__cdecl* PollEvent)(SDL_Event*) = nullptr;
      int (__cdecl* SetHint)(const char*, const char*) = nullptr;
      int (__cdecl* NumJoysticks)() = nullptr;
      const char* (__cdecl* JoystickNameForIndex)(int) = nullptr;
      int (__cdecl* JoystickNumAxes)(SDL_Joystick*) = nullptr;
      int (__cdecl* JoystickNumHats)(SDL_Joystick*) = nullptr;
      int (__cdecl* JoystickNumButtons)(SDL_Joystick*) = nullptr;
      Sint16 (__cdecl* JoystickGetAxis)(SDL_Joystick*, int) = nullptr;
      Uint8 (__cdecl* JoystickGetHat)(SDL_Joystick*, int) = nullptr;
      Uint8 (__cdecl* JoystickGetButton)(SDL_Joystick*, int) = nullptr;
      int (__cdecl* JoystickGetAttached)(SDL_Joystick*) = nullptr;
      Sint32 (__cdecl* JoystickInstanceID)(SDL_Joystick*) = nullptr;
      const char* (__cdecl* JoystickName)(SDL_Joystick*) = nullptr;
      void (__cdecl* JoystickClose)(SDL_Joystick*) = nullptr;
      int (__cdecl* IsGameController)(int) = nullptr;
      SDL_GameController* (__cdecl* GameControllerOpen)(int) = nullptr;
      void (__cdecl* GameControllerClose)(SDL_GameController*) = nullptr;
      Sint16 (__cdecl* GameControllerGetAxis)(SDL_GameController*, int) = nullptr;
      Uint8 (__cdecl* GameControllerGetButton)(SDL_GameController*, int) = nullptr;
      int (__cdecl* GameControllerGetAttached)(SDL_GameController*) = nullptr;
      const char* (__cdecl* GameControllerName)(SDL_GameController*) = nullptr;
      SDL_Joystick* (__cdecl* GameControllerGetJoystick)(SDL_GameController*) = nullptr;
      int (__cdecl* GameControllerRumble)(SDL_GameController*, Uint16, Uint16, Uint32) = nullptr;
      int (__cdecl* GameControllerAddMapping)(const char*) = nullptr;
      int (__cdecl* GameControllerAddMappingsFromRW)(SDL_RWops*, int) = nullptr;
      SDL_RWops* (__cdecl* RWFromFile)(const char*, const char*) = nullptr;
      const char* (__cdecl* GetError)() = nullptr;
      const char* (__cdecl* GameControllerGetStringForButton)(int) = nullptr; // optional, not required by LoadModernSDL
   };
   static ModernSDL g_sdl;

   static uintptr_t g_base = 0;
   static void* g_poll_tramp = nullptr;
   static std::wstring g_exe_dir; // with trailing backslash

   // ---- Controller state (everything below is protected by g_mtx) ----
   struct Pad
   {
      SDL_GameController* gc;
      Sint32 id; // SDL joystick instance id
      std::string name;
   };
   static std::mutex g_mtx;
   static std::vector<Pad> g_pads;
   static Sint32 g_active_id = -1;
   static std::string g_active_name;
   static bool g_present = false; // decided once, at the first init
   static bool g_decided = false;
   static int g_log_lines = 0;

   // Tokens handed to the game in place of real controller pointers.
   static int g_vgc_tag = 0;
   static int g_vjoy_tag = 0;
   static SDL_GameController* VGC() { return reinterpret_cast<SDL_GameController*>(&g_vgc_tag); }
   static SDL_Joystick* VJoy() { return reinterpret_cast<SDL_Joystick*>(&g_vjoy_tag); }

   static void Log(reshade::log::level lv, const char* fmt, ...)
   {
      char buf[512];
      va_list args;
      va_start(args, fmt);
      vsnprintf(buf, sizeof(buf), fmt, args);
      va_end(args);
      reshade::log::message(lv, buf);
   }

   // Capped log for runtime device messages. Call only with g_mtx held.
   static void LogDev(const char* fmt, ...)
   {
      if (g_log_lines >= kMaxLogLines) return;
      ++g_log_lines;
      char buf[512];
      va_list args;
      va_start(args, fmt);
      vsnprintf(buf, sizeof(buf), fmt, args);
      va_end(args);
      reshade::log::message(reshade::log::level::info, buf);
   }

#if DEVELOPMENT
   // ---- Rumble trace ----
   // Every line is prefixed with GetTickCount(). RLogRun collapses identical consecutive lines (compared without the prefix).
   static int g_rlog_total = 0;
   static char g_run_line[512] = {};
   static bool g_run_active = false;
   static unsigned g_run_repeats = 0;

   static void REmit(const char* body)
   {
      if (g_rlog_total >= kMaxRLogLines) return;
      ++g_rlog_total;
      char out[640];
      snprintf(out, sizeof(out), "[BL SDL] t=%u %s", static_cast<unsigned>(GetTickCount()), body);
      reshade::log::message(reshade::log::level::info, out);
   }
   // Emits the pending run's repeat count (if any) and clears the run state.
   static void RFlushRun()
   {
      if (g_run_repeats > 0)
      {
         char note[96];
         snprintf(note, sizeof(note), "  (previous line repeated %u times)", g_run_repeats);
         REmit(note);
      }
      g_run_active = false;
      g_run_repeats = 0;
   }
   static void RLog(const char* fmt, ...)
   {
      RFlushRun();
      char body[512];
      va_list args;
      va_start(args, fmt);
      vsnprintf(body, sizeof(body), fmt, args);
      va_end(args);
      REmit(body);
   }
   static void RLogRun(const char* fmt, ...)
   {
      char body[512];
      va_list args;
      va_start(args, fmt);
      vsnprintf(body, sizeof(body), fmt, args);
      va_end(args);
      if (g_run_active && strcmp(body, g_run_line) == 0)
      {
         ++g_run_repeats;
         return;
      }
      RFlushRun();
      REmit(body);
      snprintf(g_run_line, sizeof(g_run_line), "%s", body);
      g_run_active = true;
   }
   static uintptr_t CallerRva(void* ret) { return reinterpret_cast<uintptr_t>(ret) - reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)); }
#endif

#if DEVELOPMENT
#define BL_RLOG(...) RLog(__VA_ARGS__)
#define BL_RLOG_RUN(...) RLogRun(__VA_ARGS__)
#else
#define BL_RLOG(...) ((void)0)
#define BL_RLOG_RUN(...) ((void)0)
#endif

   // Haptic handle: the game only passes it back to our functions.
   struct Haptic
   {
      SDL_Joystick* joy;
   };

   // ---- Modern SDL2 loading ----
   static bool LoadModernSDL(HMODULE mod)
   {
      bool ok = true;
#define BL_SDL_LOAD(name) ok = ok && ((*reinterpret_cast<FARPROC*>(&g_sdl.name) = GetProcAddress(mod, "SDL_" #name)) != nullptr)
      BL_SDL_LOAD(GetVersion);
      BL_SDL_LOAD(InitSubSystem);
      BL_SDL_LOAD(QuitSubSystem);
      BL_SDL_LOAD(PumpEvents);
      BL_SDL_LOAD(PollEvent);
      BL_SDL_LOAD(SetHint);
      BL_SDL_LOAD(NumJoysticks);
      BL_SDL_LOAD(JoystickNameForIndex);
      BL_SDL_LOAD(JoystickNumAxes);
      BL_SDL_LOAD(JoystickNumHats);
      BL_SDL_LOAD(JoystickNumButtons);
      BL_SDL_LOAD(JoystickGetAxis);
      BL_SDL_LOAD(JoystickGetHat);
      BL_SDL_LOAD(JoystickGetButton);
      BL_SDL_LOAD(JoystickGetAttached);
      BL_SDL_LOAD(JoystickInstanceID);
      BL_SDL_LOAD(JoystickName);
      BL_SDL_LOAD(JoystickClose);
      BL_SDL_LOAD(IsGameController);
      BL_SDL_LOAD(GameControllerOpen);
      BL_SDL_LOAD(GameControllerClose);
      BL_SDL_LOAD(GameControllerGetAxis);
      BL_SDL_LOAD(GameControllerGetButton);
      BL_SDL_LOAD(GameControllerGetAttached);
      BL_SDL_LOAD(GameControllerName);
      BL_SDL_LOAD(GameControllerGetJoystick);
      BL_SDL_LOAD(GameControllerRumble);
      BL_SDL_LOAD(GameControllerAddMapping);
      BL_SDL_LOAD(GameControllerAddMappingsFromRW);
      BL_SDL_LOAD(RWFromFile);
      BL_SDL_LOAD(GetError);
#undef BL_SDL_LOAD
      *reinterpret_cast<FARPROC*>(&g_sdl.GameControllerGetStringForButton) = GetProcAddress(mod, "SDL_GameControllerGetStringForButton");
      return ok;
   }

   // ---- Controller bookkeeping helpers (call with g_mtx held) ----
   static int FindPad(Sint32 id)
   {
      for (size_t i = 0; i < g_pads.size(); ++i)
         if (g_pads[i].id == id) return static_cast<int>(i);
      return -1;
   }
   static SDL_GameController* ActiveGC()
   {
      const int i = FindPad(g_active_id);
      return i < 0 ? nullptr : g_pads[i].gc;
   }
   static SDL_GameController* ResolveGC(SDL_GameController* gc) { return gc == VGC() ? ActiveGC() : gc; }
   static SDL_Joystick* ResolveJoy(SDL_Joystick* j)
   {
      if (j != VJoy()) return j;
      SDL_GameController* gc = ActiveGC();
      return gc ? g_sdl.GameControllerGetJoystick(gc) : nullptr;
   }
   static const char* ActiveNameOrDefault() { return g_active_name.empty() ? "Controller" : g_active_name.c_str(); }

   // Makes `id` the active pad. Stops rumble on the previous pad.
   static void SetActive(Sint32 id)
   {
      if (id == g_active_id) return;
      if (SDL_GameController* old = ActiveGC()) g_sdl.GameControllerRumble(old, 0, 0, 0);
      g_active_id = id;
      const int i = FindPad(id);
      if (i < 0)
      {
         g_active_name.clear();
         LogDev("[BL SDL] no active pad");
         return;
      }
      g_active_name = g_pads[i].name;
      LogDev("[BL SDL] active pad: %s (instance %d)", g_active_name.c_str(), static_cast<int>(id));
   }

   static Sint32 EvWhich(const SDL_Event& e)
   {
      Sint32 v = 0;
      memcpy(&v, reinterpret_cast<const uint8_t*>(&e) + 8, 4);
      return v;
   }

#if DEVELOPMENT
   // Logs controller button events (SDL_ControllerButtonEvent: instance id at +8, button at +12).
   static void TraceButton(const SDL_Event& e, const char* dir)
   {
      Uint8 btn = 0;
      memcpy(&btn, reinterpret_cast<const uint8_t*>(&e) + 12, 1);
      const char* nm = g_sdl.GameControllerGetStringForButton ? g_sdl.GameControllerGetStringForButton(static_cast<int>(btn)) : nullptr;
      BL_RLOG("Button %s instance=%d button=%u name=%s", dir, static_cast<int>(EvWhich(e)), static_cast<unsigned>(btn), nm ? nm : "?");
   }
#endif

   static void HandleEvent(const SDL_Event& e)
   {
      std::lock_guard<std::mutex> lk(g_mtx);
      switch (e.type)
      {
      case kEvDeviceAdded:
      {
         const int idx = EvWhich(e);
         if (!g_sdl.IsGameController(idx)) break;
         SDL_GameController* gc = g_sdl.GameControllerOpen(idx);
         if (!gc) break;
         const Sint32 id = g_sdl.JoystickInstanceID(g_sdl.GameControllerGetJoystick(gc));
         if (FindPad(id) >= 0)
         {
            g_sdl.GameControllerClose(gc); // already tracked: drop the extra reference
            break;
         }
         const char* nm = g_sdl.GameControllerName(gc);
         g_pads.push_back(Pad{gc, id, nm ? nm : "Controller"});
         LogDev("[BL SDL] pad added: %s (instance %d)", g_pads.back().name.c_str(), static_cast<int>(id));
         if (g_active_id < 0) SetActive(id);
         break;
      }
      case kEvDeviceRemoved:
      {
         const Sint32 id = EvWhich(e);
         const int i = FindPad(id);
         if (i < 0) break;
         const bool was_active = (id == g_active_id);
         if (was_active) SetActive(-1); // stops rumble on it while still valid
         SDL_GameController* gc = g_pads[i].gc;
         LogDev("[BL SDL] pad removed: %s (instance %d)", g_pads[i].name.c_str(), static_cast<int>(id));
         g_pads.erase(g_pads.begin() + i);
         g_sdl.GameControllerClose(gc);
         if (was_active && !g_pads.empty()) SetActive(g_pads.front().id);
         break;
      }
      case kEvButtonDown:
      {
#if DEVELOPMENT
         TraceButton(e, "down");
#endif
         const Sint32 id = EvWhich(e);
         if (FindPad(id) >= 0) SetActive(id);
         break;
      }
      case kEvButtonUp:
#if DEVELOPMENT
         TraceButton(e, "up");
#endif
         break;
      case kEvAxisMotion:
      {
         const Sint32 id = EvWhich(e);
         Sint16 v = 0;
         memcpy(&v, reinterpret_cast<const uint8_t*>(&e) + 16, 2);
         if ((v > kAxisThreshold || v < -kAxisThreshold) && FindPad(id) >= 0) SetActive(id);
         break;
      }
      default:
         break;
      }
   }

   // ---- PollEvent: modern events are consumed here and never reach the game ----
   static int __cdecl MyPollEvent(SDL_Event* ev)
   {
      auto orig = reinterpret_cast<int(__cdecl*)(SDL_Event*)>(g_poll_tramp);
      if (!ev)
      {
         g_sdl.PumpEvents();
         return orig(nullptr);
      }
      SDL_Event tmp{};
      while (g_sdl.PollEvent(&tmp)) HandleEvent(tmp);
      return orig(ev);
   }

   // ---- Joystick entry replacements (virtual joystick forwards to the active pad) ----
   static int __cdecl MyJoyNumAxes(SDL_Joystick* j)
   {
      std::lock_guard<std::mutex> lk(g_mtx);
      SDL_Joystick* a = ResolveJoy(j);
      return a ? g_sdl.JoystickNumAxes(a) : 0;
   }
   static int __cdecl MyJoyNumHats(SDL_Joystick* j)
   {
      std::lock_guard<std::mutex> lk(g_mtx);
      SDL_Joystick* a = ResolveJoy(j);
      return a ? g_sdl.JoystickNumHats(a) : 0;
   }
   static int __cdecl MyJoyNumButtons(SDL_Joystick* j)
   {
      std::lock_guard<std::mutex> lk(g_mtx);
      SDL_Joystick* a = ResolveJoy(j);
      return a ? g_sdl.JoystickNumButtons(a) : 0;
   }
   static Sint16 __cdecl MyJoyGetAxis(SDL_Joystick* j, int axis)
   {
      std::lock_guard<std::mutex> lk(g_mtx);
      SDL_Joystick* a = ResolveJoy(j);
      return a ? g_sdl.JoystickGetAxis(a, axis) : 0;
   }
   static Uint8 __cdecl MyJoyGetHat(SDL_Joystick* j, int hat)
   {
      std::lock_guard<std::mutex> lk(g_mtx);
      SDL_Joystick* a = ResolveJoy(j);
      return a ? g_sdl.JoystickGetHat(a, hat) : 0;
   }
   static Uint8 __cdecl MyJoyGetButton(SDL_Joystick* j, int button)
   {
      std::lock_guard<std::mutex> lk(g_mtx);
      SDL_Joystick* a = ResolveJoy(j);
      return a ? g_sdl.JoystickGetButton(a, button) : 0;
   }
   static int __cdecl MyJoyGetAttached(SDL_Joystick* j)
   {
      std::lock_guard<std::mutex> lk(g_mtx);
      if (j == VJoy()) return ActiveGC() ? 1 : 0;
      return g_sdl.JoystickGetAttached(j);
   }
   static Sint32 __cdecl MyJoyInstanceID(SDL_Joystick* j)
   {
      std::lock_guard<std::mutex> lk(g_mtx);
      if (j == VJoy()) return 0;
      return g_sdl.JoystickInstanceID(j);
   }
   static const char* __cdecl MyJoyName(SDL_Joystick* j)
   {
      std::lock_guard<std::mutex> lk(g_mtx);
      if (j == VJoy()) return ActiveNameOrDefault();
      return g_sdl.JoystickName(j);
   }
   static void __cdecl MyJoyClose(SDL_Joystick* j)
   {
      std::lock_guard<std::mutex> lk(g_mtx);
      if (j == VJoy()) return;
      g_sdl.JoystickClose(j);
   }
   // The game must not open joysticks itself: slots use the virtual controller.
   static SDL_Joystick* __cdecl MyJoyOpen(int) { return nullptr; }

   // ---- Haptic shim (old Haptic API on top of SDL_GameControllerRumble) ----
   // Effect expiry: SetRumble only accepts prio >= this+0x100, and nothing lowers it when an effect ends.
   // Per gamepad object: the time (GetTickCount) its last accepted effect ends. Past that, the priority is reset to 0.
   static std::mutex g_expiry_mtx;
   static std::unordered_map<uint8_t*, DWORD> g_rumble_expiry;

   // True (and the entry is erased) when `self` has an expired effect.
   static bool TakeExpired(uint8_t* self, DWORD now)
   {
      std::lock_guard<std::mutex> lk(g_expiry_mtx);
      auto it = g_rumble_expiry.find(self);
      if (it == g_rumble_expiry.end() || static_cast<LONG>(now - it->second) < 0) return false;
      g_rumble_expiry.erase(it);
      return true;
   }
   static void SetExpiry(uint8_t* self, DWORD until)
   {
      std::lock_guard<std::mutex> lk(g_expiry_mtx);
      g_rumble_expiry[self] = until;
   }
   // TakeExpired compares signed differences, so an expiry 2^31 ms or more ahead would read as already expired.
   static constexpr DWORD kMaxEffectMs = 0x7FFFFFFFu - 1;
   static DWORD EffectMs(float d)
   {
      if (!(d > 0.0f)) return 1;
      const float ms_f = d * 1000.0f;
      if (static_cast<double>(ms_f) >= 2147483647.0) return kMaxEffectMs;
      const DWORD ms = static_cast<DWORD>(ms_f);
      return ms < 1 ? 1 : ms;
   }

   // Per-frame rumble update (thiscall, RVA 0x412790): the full state is traced before every update.
   // The input manager (pointer at RVA 0xC15440) can force strength 0 (+0x2C set, or +0x28 > 0), otherwise it scales by +0x24.
   static void* g_rumble_update_tramp = nullptr;
   static void __fastcall MyRumbleUpdate(uint8_t* self, void*)
   {
      if (TakeExpired(self, GetTickCount()))
      {
         *reinterpret_cast<float*>(self + 0xF4) = 0.0f;
         *reinterpret_cast<float*>(self + 0xF8) = 0.0f;
         *reinterpret_cast<float*>(self + 0x100) = 0.0f;
         BL_RLOG("rumble effect expired: prio reset (update) this=%p", static_cast<void*>(self));
      }
#if DEVELOPMENT
      const uint8_t* mgr = *reinterpret_cast<uint8_t* const*>(reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)) + 0xC15440);
      float m24 = 0.0f;
      int m28 = 0;
      unsigned m2c = 0;
      if (mgr)
      {
         m24 = *reinterpret_cast<const float*>(mgr + 0x24);
         m28 = *reinterpret_cast<const int*>(mgr + 0x28);
         m2c = mgr[0x2C];
      }
      BL_RLOG_RUN("UpdateState this=%p f4=%g f8=%g fc=%g prio=%g +108=%g +10C=%d/%g +110=%u +114=%g +130=%p mgr=%p m24=%g m28=%d m2C=%u",
              static_cast<void*>(self),
              *reinterpret_cast<const float*>(self + 0xF4), *reinterpret_cast<const float*>(self + 0xF8),
              *reinterpret_cast<const float*>(self + 0xFC), *reinterpret_cast<const float*>(self + 0x100),
              *reinterpret_cast<const float*>(self + 0x108),
              *reinterpret_cast<const int*>(self + 0x10C), *reinterpret_cast<const float*>(self + 0x10C),
              static_cast<unsigned>(self[0x110]),
              *reinterpret_cast<const float*>(self + 0x114),
              *reinterpret_cast<void* const*>(self + 0x130), static_cast<const void*>(mgr),
              m24, m28, m2c);
#endif
      using Fn = void(__thiscall*)(uint8_t*);
      reinterpret_cast<Fn>(g_rumble_update_tramp)(self);
   }

   // Game gamepad SetRumble(left, right, seconds, priority): stores the values only if priority >= this+0x100.
   static void* g_setrumble_tramp = nullptr;
   static int __fastcall MySetRumble(uint8_t* self, void*, float l, float r, float d, float p)
   {
#if DEVELOPMENT
      const uintptr_t caller = CallerRva(_ReturnAddress());
      const uintptr_t caller2 = CallerRva(((void**)_AddressOfReturnAddress())[6]);
#endif
      if (TakeExpired(self, GetTickCount()))
      {
         *reinterpret_cast<float*>(self + 0x100) = 0.0f;
         BL_RLOG("rumble effect expired: prio reset (SetRumble) this=%p", static_cast<void*>(self));
      }
      const float cur = *reinterpret_cast<float*>(self + 0x100);
      const float prio_before = cur;
      BL_RLOG("SetRumble this=%p l=%.3f r=%.3f dur=%.3f prio=%g cur_prio=%g accepted=%d f4=%.3f f8=%.3f caller=0x%X caller2=0x%X",
           static_cast<void*>(self), l, r, d, p, cur, (p >= cur) ? 1 : 0,
           *reinterpret_cast<float*>(self + 0xF4), *reinterpret_cast<float*>(self + 0xF8),
           static_cast<unsigned>(caller), static_cast<unsigned>(caller2));
      using Fn = int(__thiscall*)(uint8_t*, float, float, float, float);
      const int ret = reinterpret_cast<Fn>(g_setrumble_tramp)(self, l, r, d, p);
      if (p >= prio_before) SetExpiry(self, GetTickCount() + EffectMs(d));
      BL_RLOG("SetRumble after this=%p f4=%.3f f8=%.3f fc=%.3f prio=%g", static_cast<void*>(self),
           *reinterpret_cast<float*>(self + 0xF4), *reinterpret_cast<float*>(self + 0xF8),
           *reinterpret_cast<float*>(self + 0xFC), *reinterpret_cast<float*>(self + 0x100));
      return ret;
   }

   static int __cdecl MyJoyIsHaptic(SDL_Joystick*) { return 1; }
   static void* __cdecl MyHapticOpen(SDL_Joystick* j)
   {
      if (!j) return nullptr;
      void* h = new Haptic{j};
      BL_RLOG("HapticOpen -> %p caller=0x%X", h, static_cast<unsigned>(CallerRva(_ReturnAddress())));
      return h;
   }
   static int __cdecl MyRumbleSupported(void*) { return 1; }
   static int __cdecl MyRumbleInit(void*) { return 0; }
   static int __cdecl MyRumblePlay(void* h, float strength, Uint32 ms)
   {
#if DEVELOPMENT
      const uintptr_t caller = CallerRva(_ReturnAddress());
#endif
      int r = -3; // -3: no call made (null handle)
      if (h)
      {
         float v = strength * 65535.0f;
         if (!(v > 0.0f)) v = 0.0f;
         else if (v > 65535.0f) v = 65535.0f;
         const Uint16 s = static_cast<Uint16>(v);
         std::lock_guard<std::mutex> lk(g_mtx);
         static unsigned calls = 0, fails = 0;
         SDL_GameController* gc = ActiveGC();
         // SDL skips the device write when the values match the current ones, and some pads drop rumble that isn't re-sent:
         // nudge the low motor by 1 at most every 250 ms so a held rumble keeps reaching the pad.
         static Uint16 sent_lo = 0, sent_hi = 0;
         static DWORD sent_tick = 0;
         const DWORD t = GetTickCount();
         Uint16 lo = s;
         if (s && lo == sent_lo && s == sent_hi && t - sent_tick >= 250) lo = s ^ 1;
         if (lo != sent_lo || s != sent_hi) sent_tick = t;
         sent_lo = lo;
         sent_hi = s;
         r = gc ? g_sdl.GameControllerRumble(gc, lo, s, ms) : -2;
         ++calls;
         if (r != 0 && fails++ < 20)
            reshade::log::message(reshade::log::level::warning, (std::string("[BL SDL] rumble failed r=") + std::to_string(r) + " call " + std::to_string(calls) + ": " + (r == -2 ? "no active pad" : g_sdl.GetError())).c_str());
      }
      BL_RLOG_RUN("Play h=%p strength=%.3f ms=%u caller=0x%X result=%d", h, strength, ms, static_cast<unsigned>(caller), r);
      return 0;
   }
   static int __cdecl MyRumbleStop(void* h)
   {
#if DEVELOPMENT
      const uintptr_t caller = CallerRva(_ReturnAddress());
#endif
      int r = -3; // -3: no call made (null handle)
      if (h)
      {
         std::lock_guard<std::mutex> lk(g_mtx);
         SDL_GameController* gc = ActiveGC();
         r = gc ? g_sdl.GameControllerRumble(gc, 0, 0, 0) : -2;
      }
      BL_RLOG_RUN("Stop h=%p caller=0x%X result=%d", h, static_cast<unsigned>(caller), r);
      return 0;
   }
   static void __cdecl MyHapticClose(void* h)
   {
      BL_RLOG("HapticClose h=%p caller=0x%X", h, static_cast<unsigned>(CallerRva(_ReturnAddress())));
      if (!h) return;
      MyRumbleStop(h);
      delete static_cast<Haptic*>(h);
   }

   // ---- GameController entry replacements ----
   // Slot 0 is the virtual controller when a pad was present at init; no other slot exists.
   static int __cdecl MyIsGameController(int index)
   {
      std::lock_guard<std::mutex> lk(g_mtx);
      return (index == 0 && g_present) ? 1 : 0;
   }
   static SDL_GameController* __cdecl MyGCOpen(int index)
   {
      std::lock_guard<std::mutex> lk(g_mtx);
      return (index == 0 && g_present) ? VGC() : nullptr;
   }
   static Sint16 __cdecl MyGCGetAxis(SDL_GameController* gc, int axis)
   {
      std::lock_guard<std::mutex> lk(g_mtx);
      SDL_GameController* a = ResolveGC(gc);
      return a ? g_sdl.GameControllerGetAxis(a, axis) : 0;
   }
   static Uint8 __cdecl MyGCGetButton(SDL_GameController* gc, int button)
   {
      std::lock_guard<std::mutex> lk(g_mtx);
      SDL_GameController* a = ResolveGC(gc);
      return a ? g_sdl.GameControllerGetButton(a, button) : 0;
   }
   static int __cdecl MyGCGetAttached(SDL_GameController* gc)
   {
      std::lock_guard<std::mutex> lk(g_mtx);
      if (gc == VGC()) return ActiveGC() ? 1 : 0;
      return g_sdl.GameControllerGetAttached(gc);
   }
   static const char* __cdecl MyGCName(SDL_GameController* gc)
   {
      std::lock_guard<std::mutex> lk(g_mtx);
      if (gc == VGC()) return ActiveNameOrDefault();
      return g_sdl.GameControllerName(gc);
   }
   static SDL_Joystick* __cdecl MyGCGetJoystick(SDL_GameController* gc)
   {
      std::lock_guard<std::mutex> lk(g_mtx);
      if (gc == VGC()) return VJoy();
      return g_sdl.GameControllerGetJoystick(gc);
   }
   static void __cdecl MyGCClose(SDL_GameController* gc)
   {
      std::lock_guard<std::mutex> lk(g_mtx);
      if (gc == VGC()) return;
      g_sdl.GameControllerClose(gc);
   }
   // Old-format mappings (incl. an "xinput" default) would break modern XInput: drop those.
   static int __cdecl MyAddMapping(const char* s)
   {
      if (!s) return 0;
      if (strncmp(s, "xinput,", 7) == 0) return 0;
      return g_sdl.GameControllerAddMapping(s);
   }

   // ---- SDL_NumJoysticks thunk (0x7750C0) ----
   static int __cdecl MyNumJoysticks()
   {
      static int calls = 0;
      std::lock_guard<std::mutex> lk(g_mtx);
      const int n = g_present ? 1 : 0;
      if (calls < 3)
      {
         ++calls;
         Log(reshade::log::level::info, "[BL SDL] SDL_NumJoysticks call %d returned %d", calls, n);
      }
      return n;
   }

   // ---- Init / quit (call-site replacements) ----
   static void LoadMappingsFile()
   {
      const std::wstring wpath = g_exe_dir + L"gamecontrollerdb.txt";
      if (GetFileAttributesW(wpath.c_str()) == INVALID_FILE_ATTRIBUTES) return;
      const int len = WideCharToMultiByte(CP_UTF8, 0, wpath.c_str(), -1, nullptr, 0, nullptr, nullptr);
      if (len <= 1) return;
      std::string upath(static_cast<size_t>(len - 1), '\0');
      WideCharToMultiByte(CP_UTF8, 0, wpath.c_str(), -1, &upath[0], len, nullptr, nullptr);
      SDL_RWops* rw = g_sdl.RWFromFile(upath.c_str(), "rb");
      if (!rw)
      {
         Log(reshade::log::level::warning, "[BL SDL] gamecontrollerdb.txt present but could not be opened");
         return;
      }
      const int n = g_sdl.GameControllerAddMappingsFromRW(rw, 1); // frees rw
      Log(reshade::log::level::info, "[BL SDL] gamecontrollerdb.txt: %d mapping(s) added", n);
   }

   // Waits (max 500 ms) until the joystick count is > 0 and unchanged for 100 ms, so devices exist before they are opened.
   static int SettleDevices()
   {
      const DWORD start = GetTickCount();
      int last = -1;
      DWORD stable_since = start;
      for (;;)
      {
         const DWORD now = GetTickCount();
         if (now - start >= 500) break;
         g_sdl.PumpEvents();
         const int n = g_sdl.NumJoysticks();
         if (n != last)
         {
            last = n;
            stable_since = now;
         }
         else if (n > 0 && now - stable_since >= 100)
            break;
         Sleep(10);
      }
      return g_sdl.NumJoysticks();
   }

   // Opens every game controller found after settling, logs each device, and decides g_present once.
   static void OpenInitialPads(int n)
   {
      std::lock_guard<std::mutex> lk(g_mtx);
      for (const Pad& p : g_pads) g_sdl.GameControllerClose(p.gc);
      g_pads.clear();
      g_active_id = -1;
      g_active_name.clear();

      for (int i = 0; i < n; ++i)
      {
         const char* dev = g_sdl.JoystickNameForIndex(i);
         const int is_gc = g_sdl.IsGameController(i);
         Log(reshade::log::level::info, "[BL SDL] device %d: name=%s controller=%d", i, dev ? dev : "?", is_gc);
         if (!is_gc) continue;
         SDL_GameController* gc = g_sdl.GameControllerOpen(i);
         if (!gc) continue;
         const Sint32 id = g_sdl.JoystickInstanceID(g_sdl.GameControllerGetJoystick(gc));
         if (FindPad(id) >= 0)
         {
            g_sdl.GameControllerClose(gc);
            continue;
         }
         const char* nm = g_sdl.GameControllerName(gc);
         g_pads.push_back(Pad{gc, id, nm ? nm : "Controller"});
      }
      if (!g_decided)
      {
         g_present = !g_pads.empty();
         g_decided = true;
      }
      if (!g_pads.empty()) SetActive(g_pads.front().id);
      Log(reshade::log::level::info, "[BL SDL] controllers opened: %u, virtual controller present=%d", static_cast<unsigned>(g_pads.size()), g_present ? 1 : 0);
   }

   static int __cdecl MyInit(Uint32)
   {
      g_sdl.SetHint("SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS", "1");
      g_sdl.SetHint("SDL_NO_SIGNAL_HANDLERS", "1");
      g_sdl.SetHint("SDL_JOYSTICK_THREAD", "1");
      g_sdl.SetHint("SDL_GAMECONTROLLER_USE_BUTTON_LABELS", "0"); // positional: south button = A, also on Nintendo pads
      g_sdl.SetHint("SDL_JOYSTICK_HIDAPI_PS4_RUMBLE", "1");
      g_sdl.SetHint("SDL_JOYSTICK_HIDAPI_PS5_RUMBLE", "1");
      // Xbox-style pads: use the plain XInput driver. RAWINPUT/WGI route rumble through a device correlation that can silently stop working.
      g_sdl.SetHint("SDL_JOYSTICK_RAWINPUT", "0");
      g_sdl.SetHint("SDL_JOYSTICK_WGI", "0");
      g_sdl.SetHint("SDL_JOYSTICK_HIDAPI_JOY_CONS", "1");
      g_sdl.SetHint("SDL_JOYSTICK_HIDAPI_COMBINE_JOY_CONS", "1");

      const int r = g_sdl.InitSubSystem(kInitJoyGc);
      if (r != 0) return r;
      LoadMappingsFile();
      OpenInitialPads(SettleDevices());
      return r;
   }

   static void __cdecl MyQuit(Uint32)
   {
      {
         std::lock_guard<std::mutex> lk(g_mtx);
         for (const Pad& p : g_pads) g_sdl.GameControllerClose(p.gc);
         g_pads.clear();
         g_active_id = -1;
         g_active_name.clear();
      }
      g_sdl.QuitSubSystem(kInitJoyGc);
   }

   // ---- Patching helpers ----
   static bool WriteBytes(uint8_t* dst, const uint8_t* src, size_t n)
   {
      DWORD old_protect = 0;
      if (!VirtualProtect(dst, n, PAGE_EXECUTE_READWRITE, &old_protect)) return false;
      memcpy(dst, src, n);
      DWORD tmp = 0;
      VirtualProtect(dst, n, old_protect, &tmp);
      FlushInstructionCache(GetCurrentProcess(), dst, n);
      return true;
   }

   // Writes E9 rel32 to `to` over `len` bytes (extra bytes become NOPs).
   static bool WriteJmp(uint8_t* at, void* to, size_t len)
   {
      uint8_t buf[16];
      buf[0] = 0xE9;
      const int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(to) - (reinterpret_cast<uintptr_t>(at) + 5));
      memcpy(buf + 1, &rel, 4);
      for (size_t i = 5; i < len; ++i) buf[i] = 0x90;
      return WriteBytes(at, buf, len);
   }

   // Trampoline = displaced `len` bytes + E9 back to target+len.
   static void* MakeTrampoline(uint8_t* target, size_t len)
   {
      uint8_t* tramp = static_cast<uint8_t*>(VirtualAlloc(nullptr, 32, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
      if (!tramp) return nullptr;
      memcpy(tramp, target, len);
      tramp[len] = 0xE9;
      const int32_t back = static_cast<int32_t>(reinterpret_cast<uintptr_t>(target + len) - (reinterpret_cast<uintptr_t>(tramp + len) + 5));
      memcpy(tramp + len + 1, &back, 4);
      return tramp;
   }

   // Byte-compares the first 8 bytes against "55 8B EC ..." hex text.
   static bool MatchSig(uintptr_t rva, const char* sig)
   {
      const uint8_t* p = reinterpret_cast<const uint8_t*>(g_base + rva);
      if (IsBadReadPtr(p, 8)) return false;
      for (int i = 0; i < 8; ++i)
      {
         unsigned v = 0;
         if (sscanf(sig + i * 3, "%2x", &v) != 1 || v != p[i]) return false;
      }
      return true;
   }

   static bool CallTargetIs(uintptr_t call_rva, uintptr_t target_rva)
   {
      const uint8_t* p = reinterpret_cast<const uint8_t*>(g_base + call_rva);
      if (IsBadReadPtr(p, 5) || p[0] != 0xE8) return false;
      int32_t rel = 0;
      memcpy(&rel, p + 1, 4);
      return reinterpret_cast<uintptr_t>(p + 5) + rel == g_base + target_rva;
   }

   static bool JmpTargetIs(uintptr_t rva, uintptr_t target_rva)
   {
      const uint8_t* p = reinterpret_cast<const uint8_t*>(g_base + rva);
      if (IsBadReadPtr(p, 5) || p[0] != 0xE9) return false;
      int32_t rel = 0;
      memcpy(&rel, p + 1, 4);
      return reinterpret_cast<uintptr_t>(p + 5) + rel == g_base + target_rva;
   }

   static bool PatchCallSite(uintptr_t call_rva, void* to)
   {
      uint8_t* p = reinterpret_cast<uint8_t*>(g_base + call_rva);
      const int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(to) - (reinterpret_cast<uintptr_t>(p) + 5));
      return WriteBytes(p + 1, reinterpret_cast<const uint8_t*>(&rel), 4);
   }

   struct Entry
   {
      uintptr_t rva;
      const char* sig;
      const char* name;
      void* detour;
   };

   static void Install()
   {
      g_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));

      wchar_t exe_path[MAX_PATH] = {};
      const DWORD len = GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
      std::wstring full(exe_path, len);
      const size_t slash = full.find_last_of(L'\\');
      g_exe_dir = (slash == std::wstring::npos) ? std::wstring() : full.substr(0, slash + 1);

      HMODULE mod = LoadLibraryW((g_exe_dir + L"SDL2.dll").c_str());
      if (!mod || !LoadModernSDL(mod))
      {
         Log(reshade::log::level::warning, "[BL SDL] SDL2.dll not found/incomplete, using built-in SDL");
         return;
      }

      const char* kDef = "55 8B EC 56 8B 75 08 56";
      const Entry entries[] = {
         // Joystick
         {0x775140, kDef, "JoystickNumAxes", reinterpret_cast<void*>(&MyJoyNumAxes)},
         {0x775160, kDef, "JoystickNumHats", reinterpret_cast<void*>(&MyJoyNumHats)},
         {0x7751A0, kDef, "JoystickNumButtons", reinterpret_cast<void*>(&MyJoyNumButtons)},
         {0x7751C0, kDef, "JoystickGetAxis", reinterpret_cast<void*>(&MyJoyGetAxis)},
         {0x775200, kDef, "JoystickGetHat", reinterpret_cast<void*>(&MyJoyGetHat)},
         {0x7752B0, kDef, "JoystickGetButton", reinterpret_cast<void*>(&MyJoyGetButton)},
         {0x7752F0, kDef, "JoystickGetAttached", reinterpret_cast<void*>(&MyJoyGetAttached)},
         {0x775320, kDef, "JoystickInstanceID", reinterpret_cast<void*>(&MyJoyInstanceID)},
         {0x775340, kDef, "JoystickName", reinterpret_cast<void*>(&MyJoyName)},
         {0x775360, "55 8B EC 56 8B 75 08 85", "JoystickClose", reinterpret_cast<void*>(&MyJoyClose)},
         {0x775990, "55 8B EC 57 8B 7D 08 85", "JoystickOpen", reinterpret_cast<void*>(&MyJoyOpen)},
         // Haptic shim (0x776390 = RumbleSupported, 0x7763D0 = RumbleInit)
         {0x775CD0, kDef, "JoystickIsHaptic", reinterpret_cast<void*>(&MyJoyIsHaptic)},
         {0x775D10, "55 8B EC 57 8B 7D 08 57", "HapticOpenFromJoystick", reinterpret_cast<void*>(&MyHapticOpen)},
         {0x776390, "55 8B EC 83 EC 44 57 8B", "RumbleSupported", reinterpret_cast<void*>(&MyRumbleSupported)},
         {0x7763D0, "55 8B EC 57 8B 7D 08 8B", "RumbleInit", reinterpret_cast<void*>(&MyRumbleInit)},
         {0x776420, "55 8B EC 56 8B 75 08 8B", "RumblePlay", reinterpret_cast<void*>(&MyRumblePlay)},
         {0x7764B0, "55 8B EC 56 8B 75 08 8B", "RumbleStop", reinterpret_cast<void*>(&MyRumbleStop)},
         {0x776620, "55 8B EC 56 8B 75 08 8B", "HapticClose", reinterpret_cast<void*>(&MyHapticClose)},
         // GameController
         {0x776FA0, "55 8B EC 8B 45 08 50 E8", "IsGameController", reinterpret_cast<void*>(&MyIsGameController)},
         {0x776FC0, "55 8B EC 53 8B 5D 08 56", "GameControllerOpen", reinterpret_cast<void*>(&MyGCOpen)},
         {0x7770F0, "55 8B EC 8B 45 08 85 C0", "GameControllerGetAxis", reinterpret_cast<void*>(&MyGCGetAxis)},
         {0x777140, "55 8B EC 56 8B 75 08 85", "GameControllerGetButton", reinterpret_cast<void*>(&MyGCGetButton)},
         {0x7771C0, "55 8B EC 8B 45 08 85 C0", "GameControllerGetAttached", reinterpret_cast<void*>(&MyGCGetAttached)},
         {0x7771E0, "55 8B EC 8B 45 08 85 C0", "GameControllerName", reinterpret_cast<void*>(&MyGCName)},
         {0x777200, "55 8B EC 8B 45 08 85 C0", "GameControllerGetJoystick", reinterpret_cast<void*>(&MyGCGetJoystick)},
         {0x7772D0, "55 8B EC 56 8B 75 08 85", "GameControllerClose", reinterpret_cast<void*>(&MyGCClose)},
         {0x776D50, "55 8B EC 83 EC 24 53 8B", "GameControllerAddMapping", reinterpret_cast<void*>(&MyAddMapping)},
      };
      int failed = 0; // patch writes that returned false
      const Entry poll = {0x778060, "55 8B EC 8B 45 08 6A 00", "SDL_PollEvent", reinterpret_cast<void*>(&MyPollEvent)};

      // Verify everything first. Any mismatch patches nothing.
      bool all_ok = true;
      for (const Entry& e : entries)
      {
         if (!MatchSig(e.rva, e.sig))
         {
            Log(reshade::log::level::warning, "[BL SDL] %s at RVA 0x%06X: bytes mismatch", e.name, static_cast<unsigned>(e.rva));
            all_ok = false;
         }
      }
      if (!MatchSig(poll.rva, poll.sig))
      {
         Log(reshade::log::level::warning, "[BL SDL] %s at RVA 0x%06X: bytes mismatch", poll.name, static_cast<unsigned>(poll.rva));
         all_ok = false;
      }
      if (!JmpTargetIs(0x7750C0, 0x773190))
      {
         Log(reshade::log::level::warning, "[BL SDL] SDL_NumJoysticks thunk 0x7750C0 mismatch");
         all_ok = false;
      }
      if (!CallTargetIs(0x40BFEA, 0x774C50))
      {
         Log(reshade::log::level::warning, "[BL SDL] Init call site 0x40BFEA mismatch");
         all_ok = false;
      }
      if (!CallTargetIs(0x40C314, 0x774E60))
      {
         Log(reshade::log::level::warning, "[BL SDL] Quit call site 0x40C314 mismatch");
         all_ok = false;
      }
      if (!all_ok) return;

      // Trampoline for PollEvent must exist before anything is patched.
      g_poll_tramp = MakeTrampoline(reinterpret_cast<uint8_t*>(g_base + poll.rva), 6);
      if (!g_poll_tramp)
      {
         Log(reshade::log::level::warning, "[BL SDL] trampoline allocation failed, patching nothing");
         return;
      }

      for (const Entry& e : entries)
         if (!WriteJmp(reinterpret_cast<uint8_t*>(g_base + e.rva), e.detour, 5)) ++failed;
      if (!WriteJmp(reinterpret_cast<uint8_t*>(g_base + poll.rva), poll.detour, 6)) ++failed;
      if (!WriteJmp(reinterpret_cast<uint8_t*>(g_base + 0x7750C0), reinterpret_cast<void*>(&MyNumJoysticks), 5)) ++failed;
      if (!PatchCallSite(0x40BFEA, reinterpret_cast<void*>(&MyInit))) ++failed;
      if (!PatchCallSite(0x40C314, reinterpret_cast<void*>(&MyQuit))) ++failed;

      // Effect expiry hooks: SetRumble (thiscall, RVA 0x411B00) and the per-frame update (RVA 0x412790).
      // Installed together or not at all, so the two always agree on the priority state.
      {
         uint8_t* sr = reinterpret_cast<uint8_t*>(g_base + 0x411B00);
         uint8_t* up = reinterpret_cast<uint8_t*>(g_base + 0x412790);
         static const uint8_t sr_sig[] = {0x55, 0x8B, 0xEC, 0xD9, 0x45, 0x14};
         static const uint8_t up_sig[] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x0C, 0x56};
         if (memcmp(sr, sr_sig, sizeof(sr_sig)) != 0 || memcmp(up, up_sig, sizeof(up_sig)) != 0)
            Log(reshade::log::level::warning, "[BL SDL] rumble expiry hooks skipped: bytes mismatch, neither installed");
         else
         {
            void* t_sr = MakeTrampoline(sr, 6);
            void* t_up = MakeTrampoline(up, 6);
            if (!t_sr || !t_up)
               Log(reshade::log::level::warning, "[BL SDL] rumble expiry hooks skipped: trampoline allocation failed");
            else
            {
               g_setrumble_tramp = t_sr;
               g_rumble_update_tramp = t_up;
               if (!WriteJmp(sr, reinterpret_cast<void*>(&MySetRumble), 6)) ++failed;
               if (!WriteJmp(up, reinterpret_cast<void*>(&MyRumbleUpdate), 6)) ++failed;
            }
         }
      }

      SDL_version v{};
      g_sdl.GetVersion(&v);
      if (failed > 0)
         Log(reshade::log::level::warning, "[BL SDL] redirect incomplete: %d patch write(s) failed", failed);
      else
         Log(reshade::log::level::info, "[BL SDL] redirected to SDL2 %u.%u.%u: %u entries, PollEvent hook, NumJoysticks, 2 call sites",
          static_cast<unsigned>(v.major), static_cast<unsigned>(v.minor), static_cast<unsigned>(v.patch),
          static_cast<unsigned>(sizeof(entries) / sizeof(entries[0])));
   }
} // namespace BLSdlRedirect

// Called from OnInit (main.cpp). Not DEVELOPMENT-gated.
static void InstallSdlRedirect()
{
   BLSdlRedirect::Install();
}
#else
static void InstallSdlRedirect() {}
#endif
