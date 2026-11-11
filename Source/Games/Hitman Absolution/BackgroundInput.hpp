#pragma once
#if DEVELOPMENT
#include <Xinput.h>
#include <atomic>
#if defined(_M_IX86)
#include <safetyhook.hpp>
#endif

// Patch only the executable's imports, leaving ReShade and other DLLs untouched.
namespace HMABackgroundInput
{
   static std::atomic<bool> allow{true};
   static SHORT (WINAPI* async_key)(int) = nullptr;
   static BOOL (WINAPI* keyboard_state)(PBYTE) = nullptr;
   static DWORD (WINAPI* xinput_state)(DWORD, XINPUT_STATE*) = nullptr;
   struct Patch { void** slot; void* original; void* replacement; };
   static Patch patches[3] = {};
   static unsigned patch_count = 0;
   static bool Blocked();
#if defined(_M_IX86)
   static safetyhook::MidHook mouse_hook, joystick_hook, keyboard_hook;
   static bool direct_input_available = false;
   static uintptr_t image_base = 0;
   static void Mouse(safetyhook::Context& ctx)
   {
      if (Blocked() && ctx.eax == 0) memset(reinterpret_cast<void*>(ctx.esi + 0x2b4), 0, 0x14);
   }
   static void Joystick(safetyhook::Context& ctx)
   {
      if (Blocked() && ctx.eax == 0)
      {
         auto* state = reinterpret_cast<uint8_t*>(ctx.ebp - 0x130);
         memset(state, 0, 0x110);
         memset(state + 0x20, 0xff, 0x10); // Four centered POV hats.
      }
   }
   static void Keyboard(safetyhook::Context& ctx)
   {
      if (!Blocked()) return;
      auto* self = reinterpret_cast<uint8_t*>(ctx.esi);
      auto* held = *reinterpret_cast<uint8_t**>(self + 0x28);
      const int count = *reinterpret_cast<int*>(self + 0x30);
      if (held && count > 0 && count <= 512)
      {
         using ReleaseKey = void (__thiscall*)(void*, int, int, const void*);
         auto release = reinterpret_cast<ReleaseKey>(image_base + 0x36e200);
         for (int key = 0; key < count; ++key)
            if (held[key]) release(self, key, 0, self + 0x58);
      }
      *reinterpret_cast<float*>(self + 0x16ac) = 0.f;
      if (ctx.eax == 0) *reinterpret_cast<DWORD*>(ctx.ebp - 4) = 0;
   }
   static void InstallDirectInput()
   {
      auto* base = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
      if (!base) return;
      auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
      if (dos->e_magic != IMAGE_DOS_SIGNATURE) return;
      auto* nt = reinterpret_cast<IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
      if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
          nt->FileHeader.TimeDateStamp != 0x5c9a0bf7 || nt->OptionalHeader.SizeOfImage != 0x2224000 ||
          nt->OptionalHeader.CheckSum != 0x2088538) return;
      const uint8_t mouse_bytes[] = {0x85, 0xc0, 0x74, 0x0f, 0x5f};
      const uint8_t joystick_bytes[] = {0x85, 0xc0, 0x74, 0x0f, 0xc6, 0x86, 0xac, 0, 0, 0, 0};
      const uint8_t keyboard_bytes[] = {0x85, 0xc0, 0x74, 0x0f, 0x5b};
      auto* mouse = base + 0x2cb03f;
      auto* joystick = base + 0x2cb457;
      auto* keyboard = base + 0x2cbfc0;
      if (memcmp(mouse, mouse_bytes, sizeof(mouse_bytes)) || memcmp(joystick, joystick_bytes, sizeof(joystick_bytes)) ||
          memcmp(keyboard, keyboard_bytes, sizeof(keyboard_bytes))) return;
      image_base = reinterpret_cast<uintptr_t>(base);
      mouse_hook = safetyhook::create_mid(mouse, Mouse);
      joystick_hook = safetyhook::create_mid(joystick, Joystick);
      keyboard_hook = safetyhook::create_mid(keyboard, Keyboard);
      direct_input_available = !!mouse_hook && !!joystick_hook && !!keyboard_hook;
      if (!direct_input_available) { mouse_hook = {}; joystick_hook = {}; keyboard_hook = {}; }
   }
#else
   static bool direct_input_available = false;
   static void InstallDirectInput() {}
#endif

   static bool Blocked()
   {
      HWND window = game_window;
      return !allow.load(std::memory_order_relaxed) && window && IsWindow(window) && GetForegroundWindow() != window;
   }
   static SHORT WINAPI AsyncKey(int key)
   {
      const SHORT state = async_key(key);
      return Blocked() ? 0 : state;
   }
   static BOOL WINAPI KeyboardState(PBYTE keys)
   {
      const BOOL result = keyboard_state(keys);
      if (result && keys && Blocked()) memset(keys, 0, 256);
      return result;
   }
   static DWORD WINAPI XInputState(DWORD index, XINPUT_STATE* state)
   {
      const DWORD result = xinput_state(index, state);
      if (result != ERROR_SUCCESS || !state || index >= 4) return result;
      if (Blocked()) memset(&state->Gamepad, 0, sizeof(state->Gamepad));
      return result;
   }
   static void Install()
   {
      // Try once per process: "patch_count" only counts IAT patches, so without this the DirectInput mid-hooks
      // would be re-created on every present when no import matched (stacking new hooks on still-live ones).
      static bool install_attempted = false;
      if (install_attempted) return;
      install_attempted = true;
      InstallDirectInput();
      auto* base = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
      if (!base) return;
      auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
      if (dos->e_magic != IMAGE_DOS_SIGNATURE) return;
      auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
      if (nt->Signature != IMAGE_NT_SIGNATURE) return;
      const auto& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
      if (!directory.VirtualAddress) return;
      auto* descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + directory.VirtualAddress);
      for (; descriptor->Name; ++descriptor)
      {
         if (!descriptor->OriginalFirstThunk) continue;
         auto* names = reinterpret_cast<IMAGE_THUNK_DATA*>(base + descriptor->OriginalFirstThunk);
         auto* slots = reinterpret_cast<IMAGE_THUNK_DATA*>(base + descriptor->FirstThunk);
         for (; names->u1.AddressOfData; ++names, ++slots)
         {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            const char* name = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData)->Name;
            void* replacement = nullptr;
            void** original = nullptr;
            if (!strcmp(name, "GetAsyncKeyState")) { replacement = reinterpret_cast<void*>(&AsyncKey); original = reinterpret_cast<void**>(&async_key); }
            else if (!strcmp(name, "GetKeyboardState")) { replacement = reinterpret_cast<void*>(&KeyboardState); original = reinterpret_cast<void**>(&keyboard_state); }
            else if (!strcmp(name, "XInputGetState")) { replacement = reinterpret_cast<void*>(&XInputState); original = reinterpret_cast<void**>(&xinput_state); }
            if (!replacement || *original) continue;
            auto** slot = reinterpret_cast<void**>(&slots->u1.Function);
            DWORD protect = 0;
            if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &protect)) continue;
            *original = *slot;
            InterlockedExchangePointer(slot, replacement);
            DWORD ignored;
            VirtualProtect(slot, sizeof(*slot), protect, &ignored);
            patches[patch_count++] = {slot, *original, replacement};
         }
      }
   }
   static void Uninstall()
   {
#if defined(_M_IX86)
      mouse_hook = {}; joystick_hook = {}; keyboard_hook = {};
      direct_input_available = false;
#endif
      for (unsigned i = 0; i < patch_count; ++i)
      {
         auto& patch = patches[i];
         DWORD protect = 0;
         if (VirtualProtect(patch.slot, sizeof(*patch.slot), PAGE_READWRITE, &protect))
         {
            InterlockedCompareExchangePointer(patch.slot, patch.original, patch.replacement);
            DWORD ignored;
            VirtualProtect(patch.slot, sizeof(*patch.slot), protect, &ignored);
         }
      }
      patch_count = 0;
      // Clear the trampolines' saved originals so their null state matches the restored IAT slots.
      async_key = nullptr;
      keyboard_state = nullptr;
      xinput_state = nullptr;
   }
}
#endif
