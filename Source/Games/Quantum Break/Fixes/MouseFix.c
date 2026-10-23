// Quantum Break MouseFix: event-driven raw mouse collection.
// Original mod implementation; no game assets or decompiled code are included.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include <stddef.h>
#include <wchar.h>
#include "../../../External/reshade/deps/minhook/include/MinHook.h"
#include "MouseFix_Settings.h"
#include "ParticleFix.h"

typedef void (*CameraFn)(void*, const float*, float, float);
typedef BOOL(WINAPI* RegisterRawFn)(PCRAWINPUTDEVICE, UINT, UINT);
typedef UINT(WINAPI* GetRawFn)(HRAWINPUT, UINT, LPVOID, PUINT, UINT);

extern int QBCF_Initialize(void);
extern void QBCF_Shutdown(void);
extern int QBMF_SetCutsceneEnabled(int);
static HMODULE self_module;
static BYTE *game, *blur_flag, *bloom_flag, *flare_flag;
static BYTE *depth_of_field_flag, original_depth_of_field;
static bool depth_of_field_captured, depth_of_field_disabled;
static BYTE original_bloom, original_flare;
static bool bloom_captured, bloom_disabled, flare_captured, flare_disabled;
static BYTE original_blur;
static bool blur_captured, blur_disabled, initialized, hook_created, hook_enabled;
static CameraFn original_camera;
static RegisterRawFn original_register = RegisterRawInputDevices;
static void* register_target;
static bool register_hook_created, register_hook_enabled;
static GetRawFn original_get_raw = GetRawInputData;
static void* get_raw_target;
static bool get_raw_hook_created, get_raw_hook_enabled;
static const wchar_t controls_property[] = L"MouseFix.NativeMouseControls";
static HWND controls_window;
static UINT controls_message;
static SRWLOCK controls_lock = SRWLOCK_INIT;
static RAWINPUT controls_queue[2048];
static unsigned controls_read, controls_count;
static bool controls_posted;
#ifdef _MSC_VER
static __declspec(thread) const RAWINPUT* dispatch_packet;
#else
static _Thread_local const RAWINPUT* dispatch_packet;
#endif
static volatile LONG64 controls_forwarded;
static SRWLOCK registration_lock = SRWLOCK_INIT;
static volatile LONG64 registration_redirects;
static HANDLE thread_handle, thread_ready;
static DWORD thread_id, process_id;
static HWND input_window;
static RAWINPUTDEVICE original_registration;
static volatile LONG enabled, overlay_capture, worker_ready, last_error;
static SRWLOCK pending_lock = SRWLOCK_INIT;
static int64_t pending_x, pending_y;
static HWND last_foreground;
static bool last_foreground_is_game;
static LARGE_INTEGER frequency;
static LONGLONG last_camera_time;
static const char* status = "Not initialized";
static volatile LONG64 reports, accepted_reports, total_x, total_y, applied_x, applied_y, camera_calls, discarded_reports;
static const UINT change_mode = WM_APP + 31;

static void clear_pending(void)
{
   AcquireSRWLockExclusive(&pending_lock);
   pending_x = pending_y = 0;
   ReleaseSRWLockExclusive(&pending_lock);
}
static bool game_focused(void)
{
   HWND foreground = GetForegroundWindow();
   DWORD pid = 0;
   if (foreground) GetWindowThreadProcessId(foreground, &pid);
   return pid == process_id;
}
static UINT WINAPI get_raw_input(HRAWINPUT handle, UINT command, LPVOID data, PUINT bytes, UINT header_size)
{
   if (!dispatch_packet || handle != (HRAWINPUT)dispatch_packet) return original_get_raw(handle, command, data, bytes, header_size);
   if (!bytes || header_size != sizeof(RAWINPUTHEADER) || (command != RID_INPUT && command != RID_HEADER))
   {
      SetLastError(ERROR_INVALID_PARAMETER);
      return (UINT)-1;
   }
   const UINT required = command == RID_HEADER ? sizeof(RAWINPUTHEADER) : sizeof(RAWINPUT);
   if (!data)
   {
      *bytes = required;
      return 0;
   }
   if (*bytes < required)
   {
      *bytes = required;
      SetLastError(ERROR_INSUFFICIENT_BUFFER);
      return (UINT)-1;
   }
   memcpy(data, dispatch_packet, required);
   *bytes = required;
   return required;
}
static LRESULT CALLBACK controls_wndproc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
   WNDPROC previous = (WNDPROC)(uintptr_t)GetPropW(window, controls_property);
   if (message == controls_message && wparam == (WPARAM)self_module)
   {
      for (unsigned n = 0; n < 2048; n++)
      {
         RAWINPUT packet;
         AcquireSRWLockExclusive(&controls_lock);
         if (!controls_count)
         {
            controls_posted = false;
            ReleaseSRWLockExclusive(&controls_lock);
            break;
         }
         packet = controls_queue[controls_read];
         controls_read = (controls_read + 1) % 2048;
         --controls_count;
         ReleaseSRWLockExclusive(&controls_lock);
         if (previous && game_focused() && !InterlockedCompareExchange(&overlay_capture, 0, 0))
         {
            const RAWINPUT* saved = dispatch_packet;
            dispatch_packet = &packet;
            if (IsWindowUnicode(window))
               CallWindowProcW(previous, window, WM_INPUT, RIM_INPUT, (LPARAM)&packet);
            else
               CallWindowProcA(previous, window, WM_INPUT, RIM_INPUT, (LPARAM)&packet);
            dispatch_packet = saved;
            InterlockedIncrement64(&controls_forwarded);
         }
      }
      AcquireSRWLockExclusive(&controls_lock);
      if (controls_count)
         PostMessageW(window, controls_message, (WPARAM)self_module, 0);
      else
         controls_posted = false;
      ReleaseSRWLockExclusive(&controls_lock);
      return 0;
   }
   if (!previous) return DefWindowProcW(window, message, wparam, lparam);
   if (IsWindowUnicode(window)) return CallWindowProcW(previous, window, message, wparam, lparam);
   return CallWindowProcA(previous, window, message, wparam, lparam);
}
static bool attach_controls_window(HWND window)
{
   if (window == controls_window && GetPropW(window, controls_property)) return true;
   DWORD pid = 0;
   if (!window || !GetWindowThreadProcessId(window, &pid) || pid != process_id) return false;
   if (!controls_message) controls_message = RegisterWindowMessageW(L"MouseFix.NativeMouseControls.Dispatch");
   if (!controls_message) return false;
   if (!GetPropW(window, controls_property))
   {
      WNDPROC previous = (WNDPROC)GetWindowLongPtrW(window, GWLP_WNDPROC);
      if (!previous || !SetPropW(window, controls_property, (HANDLE)(uintptr_t)previous)) return false;
      SetLastError(0);
      LONG_PTR result = SetWindowLongPtrW(window, GWLP_WNDPROC, (LONG_PTR)&controls_wndproc);
      if (!result && GetLastError())
      {
         RemovePropW(window, controls_property);
         return false;
      }
      if (result) SetPropW(window, controls_property, (HANDLE)result);
   }
   controls_window = window;
   return true;
}
static bool forward_controls(HWND window, const RAWINPUT* input)
{
   if (!input->data.mouse.usButtonFlags) return true;
   if (!attach_controls_window(window)) return false;
   AcquireSRWLockExclusive(&controls_lock);
   if (controls_count == 2048)
   {
      ReleaseSRWLockExclusive(&controls_lock);
      return false;
   }
   RAWINPUT* packet = &controls_queue[(controls_read + controls_count) % 2048];
   *packet = *input;
   packet->header.dwSize = sizeof(*packet);
   packet->header.wParam = RIM_INPUT;
   // The native game receives original button/wheel data, without duplicate motion.
   packet->data.mouse.lLastX = packet->data.mouse.lLastY = 0;
   ++controls_count;
   bool success = true;
   if (!controls_posted)
   {
      success = PostMessageW(window, controls_message, (WPARAM)self_module, 0) != FALSE;
      controls_posted = success;
   }
   if (!success)
   {
      controls_count = 0;
      controls_read = 0;
   }
   ReleaseSRWLockExclusive(&controls_lock);
   return success;
}
static bool capture_registration(void)
{
   RAWINPUTDEVICE devices[64];
   UINT count = 64;
   UINT result = GetRegisteredRawInputDevices(devices, &count, sizeof devices[0]);
   if (result == (UINT)-1 || result > 64) return false;
   for (UINT i = 0; i < result; i++)
      if (devices[i].usUsagePage == 1 && devices[i].usUsage == 2)
      {
         original_registration = devices[i];
         // Keep the native registration for restoring the complete input path.
         // Other raw-input owners are not silently displaced.
         if (devices[i].dwFlags != 0 || devices[i].hwndTarget != NULL) return false;
         return true;
      }
   // ReShade may initialize before the game registers its mouse. The native
   // fallback is still the game's normal foreground registration, not removal.
   original_registration = (RAWINPUTDEVICE){1, 2, 0, NULL};
   return true;
}
static bool restore_registration_locked(void)
{
   RAWINPUTDEVICE device = original_registration;
   return original_register(&device, 1, sizeof device) != FALSE;
}
static bool restore_registration(void)
{
   AcquireSRWLockExclusive(&registration_lock);
   bool result = restore_registration_locked();
   ReleaseSRWLockExclusive(&registration_lock);
   return result;
}
static BOOL WINAPI register_raw_input(PCRAWINPUTDEVICE devices, UINT count, UINT size)
{
   if (!devices || size != sizeof(RAWINPUTDEVICE) || count > 64) return original_register(devices, count, size);
   RAWINPUTDEVICE replacement[64], native_mouse = {0};
   bool native_request = false, mouse_changed = false, redirected = false;
   memcpy(replacement, devices, count * sizeof replacement[0]);
   AcquireSRWLockExclusive(&registration_lock);
   for (UINT i = 0; i < count; i++)
      if (devices[i].usUsagePage == 1 && devices[i].usUsage == 2)
      {
         mouse_changed = true;
         if (devices[i].dwFlags == 0 && devices[i].hwndTarget == NULL)
         {
            native_mouse = devices[i];
            native_request = true;
            if (InterlockedCompareExchange(&enabled, 0, 0) && input_window)
            {
               replacement[i].dwFlags = RIDEV_INPUTSINK;
               replacement[i].hwndTarget = input_window;
               redirected = true;
            }
         }
      }
   BOOL result = original_register(replacement, count, size);
   DWORD error = GetLastError();
   if (result)
   {
      if (native_request) original_registration = native_mouse;
      if (redirected)
         InterlockedIncrement64(&registration_redirects);
      else if (mouse_changed && InterlockedCompareExchange(&enabled, 0, 0))
      {
         InterlockedExchange(&enabled, 0);
         clear_pending();
         status = "Mouse registration changed; original input active";
      }
   }
   ReleaseSRWLockExclusive(&registration_lock);
   SetLastError(error);
   return result;
}
static void process_raw(const RAWINPUT* input)
{
   if (input->header.dwType != RIM_TYPEMOUSE) return;
   InterlockedIncrement64(&reports);
   if (!InterlockedCompareExchange(&enabled, 0, 0)) return;
   const RAWMOUSE* mouse = &input->data.mouse;
   HWND foreground = GetForegroundWindow();
   if (foreground != last_foreground)
   {
      DWORD pid = 0;
      if (foreground) GetWindowThreadProcessId(foreground, &pid);
      last_foreground = foreground;
      last_foreground_is_game = pid == process_id;
      clear_pending();
   }
   if (!last_foreground_is_game || InterlockedCompareExchange(&overlay_capture, 0, 0))
   {
      InterlockedIncrement64(&discarded_reports);
      clear_pending();
      return;
   }
   if (!forward_controls(foreground, input))
   {
      InterlockedExchange(&enabled, 0);
      restore_registration();
      clear_pending();
      status = "Mouse control handoff failed; original input restored";
      return;
   }
   if (mouse->usFlags & MOUSE_MOVE_ABSOLUTE)
   {
      InterlockedExchange(&enabled, 0);
      restore_registration();
      clear_pending();
      status = "Absolute pointing device detected; original input restored";
      return;
   }
   AcquireSRWLockExclusive(&pending_lock);
   pending_x += mouse->lLastX;
   pending_y += mouse->lLastY;
   ReleaseSRWLockExclusive(&pending_lock);
   InterlockedIncrement64(&accepted_reports);
   InterlockedAdd64(&total_x, mouse->lLastX);
   InterlockedAdd64(&total_y, mouse->lLastY);
}
static void drain_raw_queue(void)
{
   // Large, aligned stack buffer for bursts from high polling-rate devices.
   union
   {
      max_align_t alignment;
      BYTE bytes[65536];
   } buffer;
   for (unsigned pass = 0; pass < 32; pass++)
   {
      UINT bytes = sizeof buffer.bytes;
      UINT count = GetRawInputBuffer((RAWINPUT*)buffer.bytes, &bytes, sizeof(RAWINPUTHEADER));
      if (count == 0 || count == (UINT)-1) break;
      RAWINPUT* input = (RAWINPUT*)buffer.bytes;
      for (UINT i = 0; i < count; i++)
      {
         if ((BYTE*)input < buffer.bytes || (BYTE*)input + sizeof(RAWINPUTHEADER) > buffer.bytes + sizeof buffer.bytes) break;
         if (input->header.dwSize < sizeof(RAWINPUTHEADER) || (BYTE*)input + input->header.dwSize > buffer.bytes + sizeof buffer.bytes) break;
         process_raw(input);
         input = (RAWINPUT*)(((uintptr_t)input + input->header.dwSize + 7) & ~(uintptr_t)7);
      }
   }
}
static LRESULT CALLBACK input_wndproc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
   if (message == WM_INPUT)
   {
      RAWINPUT input;
      UINT bytes = sizeof input;
      if (GetRawInputData((HRAWINPUT)lparam, RID_INPUT, &input, &bytes, sizeof(RAWINPUTHEADER)) != (UINT)-1) process_raw(&input);
      drain_raw_queue();
      return DefWindowProcW(window, message, wparam, lparam);
   }
   if (message == change_mode)
   {
      if (wparam)
      {
         clear_pending();
         last_foreground = NULL;
         last_foreground_is_game = false;
         RAWINPUTDEVICE device = {1, 2, RIDEV_INPUTSINK, window};
         AcquireSRWLockExclusive(&registration_lock);
         if (!original_register(&device, 1, sizeof device))
         {
            InterlockedExchange(&last_error, (LONG)GetLastError());
            status = "Raw input registration failed";
            ReleaseSRWLockExclusive(&registration_lock);
            return 0;
         }
         InterlockedExchange(&enabled, 1);
         ReleaseSRWLockExclusive(&registration_lock);
         status = "Event-driven raw mouse input active";
         return 1;
      }
      AcquireSRWLockExclusive(&registration_lock);
      InterlockedExchange(&enabled, 0);
      bool restored = restore_registration_locked();
      ReleaseSRWLockExclusive(&registration_lock);
      clear_pending();
      status = restored ? "Original mouse input active" : "Could not restore raw input registration";
      return restored ? 1 : 0;
   }
   if (message == WM_CLOSE)
   {
      SendMessageW(window, change_mode, 0, 0);
      DestroyWindow(window);
      return 0;
   }
   if (message == WM_DESTROY)
   {
      PostQuitMessage(0);
      return 0;
   }
   return DefWindowProcW(window, message, wparam, lparam);
}
static DWORD WINAPI input_thread(void* unused)
{
   (void)unused;
   SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
   WNDCLASSW cls = {0};
   cls.lpfnWndProc = input_wndproc;
   cls.hInstance = self_module;
   cls.lpszClassName = L"QuantumBreakMouseFixRawInput";
   if (!RegisterClassW(&cls))
   {
      InterlockedExchange(&last_error, (LONG)GetLastError());
      status = "Input window registration failed";
      SetEvent(thread_ready);
      return 1;
   }
   input_window = CreateWindowExW(0, cls.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, self_module, NULL);
   if (!input_window)
   {
      InterlockedExchange(&last_error, (LONG)GetLastError());
      status = "Input window creation failed";
      SetEvent(thread_ready);
      UnregisterClassW(cls.lpszClassName, self_module);
      return 1;
   }
   InterlockedExchange(&worker_ready, 1);
   SetEvent(thread_ready);
   MSG message;
   while (GetMessageW(&message, NULL, 0, 0) > 0)
   {
      TranslateMessage(&message);
      DispatchMessageW(&message);
   }
   input_window = NULL;
   InterlockedExchange(&worker_ready, 0);
   UnregisterClassW(cls.lpszClassName, self_module);
   return 0;
}
static float axis_scale(float value)
{
   if (value <= 0) return 0.005f;
   if (value >= 1) return 0.1f;
   return (value * 0.95f + 0.05f) * 0.1f;
}
static bool native_camera_input_allowed(void)
{
   // Preserve the input restrictions applied by the original camera caller.
   if ((*(uint32_t*)(game + 0x11763f0) & 0xfffffffdU) != 0) return false;
   void* world = *(void**)(game + 0x11763c0);
   if (!world) return false;
   typedef unsigned char (*StateFn)(void*);
   return !((StateFn)(game + 0x3ad050))(world) && !((StateFn)(game + 0x3ad090))(world);
}
static void direct_camera(void* camera, const float* axes, float horizontal, float vertical)
{
   InterlockedIncrement64(&camera_calls);
   if (!InterlockedCompareExchange(&enabled, 0, 0))
   {
      original_camera(camera, axes, horizontal, vertical);
      return;
   }
   LARGE_INTEGER time;
   QueryPerformanceCounter(&time);
   bool resumed = !last_camera_time || (time.QuadPart - last_camera_time) > (frequency.QuadPart / 4);
   last_camera_time = time.QuadPart;
   int64_t x, y;
   AcquireSRWLockExclusive(&pending_lock);
   x = pending_x;
   y = pending_y;
   pending_x = pending_y = 0;
   ReleaseSRWLockExclusive(&pending_lock);
   if (resumed || !game_focused() || InterlockedCompareExchange(&overlay_capture, 0, 0) || !native_camera_input_allowed()) x = y = 0;
   float scale = *(float*)(game + 0xef4d68);
   float replacement[2] = {(float)x * axis_scale(*(float*)(game + 0xef4d60)) * scale, (float)y * axis_scale(*(float*)(game + 0xef4d64)) * scale};
   if (!isfinite(replacement[0]) || !isfinite(replacement[1])) replacement[0] = replacement[1] = 0;
   InterlockedAdd64(&applied_x, x);
   InterlockedAdd64(&applied_y, y);
   original_camera(camera, replacement, horizontal, vertical);
}

const char* QBMF_GetStatus(void)
{
   return status;
}
int64_t QBMF_GetRegistrationRedirects(void)
{
   return InterlockedCompareExchange64(&registration_redirects, 0, 0);
}
int64_t QBMF_GetForwardedControls(void)
{
   return InterlockedCompareExchange64(&controls_forwarded, 0, 0);
}
int QBMF_GetRawMouseEnabled(void)
{
   return (int)InterlockedCompareExchange(&enabled, 0, 0);
}
void QBMF_SetOverlayCapture(int capture)
{
   InterlockedExchange(&overlay_capture, capture ? 1 : 0);
}
static int apply_raw_mouse_enabled(int value)
{
   if (!hook_enabled || !InterlockedCompareExchange(&worker_ready, 0, 0) || !input_window) return 0;
   return (int)SendMessageW(input_window, change_mode, value ? 1 : 0, 0);
}
static int apply_motion_blur_disabled(int disabled)
{
   if (!blur_flag) return 0;
   if (!blur_captured)
   {
      original_blur = *blur_flag;
      blur_captured = true;
   }
   *blur_flag = disabled ? 0 : original_blur;
   blur_disabled = disabled != 0;
   return 1;
}
static int apply_depth_of_field_disabled(int disabled)
{
   if (!depth_of_field_flag) return 0;
   if (!depth_of_field_captured)
   {
      original_depth_of_field = *depth_of_field_flag;
      depth_of_field_captured = true;
   }
   *depth_of_field_flag = disabled ? 0 : original_depth_of_field;
   depth_of_field_disabled = disabled != 0;
   return 1;
}
int QBMF_SetDepthOfFieldDisabled(int disabled)
{
   if (!apply_depth_of_field_disabled(disabled)) return 0;
   QBMF_SaveDepthOfFieldPreference(disabled);
   return 1;
}
int QBMF_GetDepthOfFieldEnabled(void)
{
   return depth_of_field_flag ? *depth_of_field_flag : -1;
}
static int apply_bloom_disabled(int disabled)
{
   if (!bloom_flag) return 0;
   if (!bloom_captured)
   {
      original_bloom = *bloom_flag;
      bloom_captured = true;
   }
   *bloom_flag = disabled ? 0 : original_bloom;
   bloom_disabled = disabled != 0;
   return 1;
}
static int apply_flares_disabled(int disabled)
{
   if (!flare_flag) return 0;
   if (!flare_captured)
   {
      original_flare = *flare_flag;
      flare_captured = true;
   }
   *flare_flag = disabled ? 0 : original_flare;
   flare_disabled = disabled != 0;
   return 1;
}
int QBMF_SetFlaresDisabled(int disabled)
{
   if (!apply_flares_disabled(disabled)) return 0;
   QBMF_SaveFlaresPreference(disabled);
   return 1;
}
int QBMF_GetFlaresEnabled(void)
{
   return flare_flag ? *flare_flag : -1;
}
int QBMF_SetBloomDisabled(int disabled)
{
   if (!apply_bloom_disabled(disabled)) return 0;
   QBMF_SaveBloomPreference(disabled);
   return 1;
}
int QBMF_GetBloomEnabled(void)
{
   return bloom_flag ? *bloom_flag : -1;
}
int QBMF_SetHighResolutionParticles(int value)
{
   if (!QBP_SetEnabled(value)) return 0;
   QBMF_SaveParticlePreference(value);
   return 1;
}
int QBMF_GetHighResolutionParticles(void)
{
   return QBP_GetEnabled();
}
int QBMF_SetRawMouseEnabled(int value)
{
   if (!apply_raw_mouse_enabled(value)) return 0;
   QBMF_SaveRawMousePreference(value);
   return 1;
}
int QBMF_SetMotionBlurDisabled(int disabled)
{
   if (!apply_motion_blur_disabled(disabled)) return 0;
   QBMF_SaveBlurPreference(disabled);
   return 1;
}
int QBMF_GetMotionBlurEnabled(void)
{
   return blur_flag ? *blur_flag : -1;
}
void QBMF_GetCounters(int64_t* out)
{
   volatile LONG64* c[] = {&reports, &accepted_reports, &total_x, &total_y, &applied_x, &applied_y, &camera_calls, &discarded_reports};
   for (unsigned i = 0; i < 8; i++)
      out[i] = InterlockedCompareExchange64(c[i], 0, 0);
}
int QBMF_Initialize(void)
{
   if (initialized) return hook_enabled ? 1 : 0;
   wchar_t path[MAX_PATH];
   GetModuleFileNameW(NULL, path, MAX_PATH);
   wchar_t* name = wcsrchr(path, L'\\');
   if (!name || _wcsicmp(name + 1, L"QuantumBreak.exe"))
   {
      status = "Unsupported process";
      return 0;
   }
   process_id = GetCurrentProcessId();
   game = (BYTE*)GetModuleHandleW(NULL);
   HMODULE renderer = GetModuleHandleW(L"renderer_x64_f.dll");
   if (!renderer)
   {
      status = "Renderer is not loaded";
      return 0;
   }
   const BYTE expected[] = {0x48, 0x89, 0x5c, 0x24, 0x10, 0x57, 0x48, 0x81, 0xec, 0x80, 0, 0, 0};
   if (memcmp(game + 0x4856e0, expected, sizeof expected))
   {
      status = "Unsupported camera code";
      return 0;
   }
   blur_flag = (BYTE*)GetProcAddress(renderer, "?RenderVectorBlur@RenderOptions@rend@@2_NA");
   bloom_flag = (BYTE*)GetProcAddress(renderer, "?RenderBloom@RenderOptions@rend@@2_NA");
   flare_flag = (BYTE*)GetProcAddress(renderer, "?RenderFlares@RenderOptions@rend@@2_NA");
   depth_of_field_flag = (BYTE*)GetProcAddress(renderer, "?RenderDepthOfField@RenderOptions@rend@@2_NA");
   if (depth_of_field_flag && *depth_of_field_flag > 1) depth_of_field_flag = NULL;
   if (bloom_flag && *bloom_flag > 1) bloom_flag = NULL;
   if (flare_flag && *flare_flag > 1) flare_flag = NULL;
   if (!blur_flag || *blur_flag > 1 || !capture_registration())
   {
      status = "Unsupported raw input registration or renderer";
      return 0;
   }
   if (!QueryPerformanceFrequency(&frequency))
   {
      status = "Performance counter unavailable";
      return 0;
   }
   MH_STATUS mh = MH_Initialize();
   if (mh != MH_OK && mh != MH_ERROR_ALREADY_INITIALIZED)
   {
      status = "Hook initialization failed";
      return 0;
   }
   register_target = (void*)GetProcAddress(GetModuleHandleW(L"user32.dll"), "RegisterRawInputDevices");
   if (!register_target || MH_CreateHook(register_target, register_raw_input, (LPVOID*)&original_register) != MH_OK)
   {
      status = "Input registration hook creation failed";
      return 0;
   }
   register_hook_created = true;
   if (MH_EnableHook(register_target) != MH_OK)
   {
      status = "Input registration hook activation failed";
      return 0;
   }
   register_hook_enabled = true;
   get_raw_target = (void*)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetRawInputData");
   if (!get_raw_target || MH_CreateHook(get_raw_target, get_raw_input, (LPVOID*)&original_get_raw) != MH_OK)
   {
      status = "Mouse controls hook creation failed";
      return 0;
   }
   get_raw_hook_created = true;
   if (MH_EnableHook(get_raw_target) != MH_OK)
   {
      status = "Mouse controls hook activation failed";
      return 0;
   }
   get_raw_hook_enabled = true;
   if (MH_CreateHook(game + 0x4856e0, direct_camera, (LPVOID*)&original_camera) != MH_OK)
   {
      status = "Camera hook creation failed";
      return 0;
   }
   hook_created = true;
   if (MH_EnableHook(game + 0x4856e0) != MH_OK)
   {
      status = "Camera hook activation failed";
      return 0;
   }
   hook_enabled = true;
   initialized = true;
   HMODULE pinned;
   GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN, (LPCWSTR)(uintptr_t)&direct_camera, &pinned);
   thread_ready = CreateEventW(NULL, TRUE, FALSE, NULL);
   if (!thread_ready)
   {
      status = "Input thread event creation failed";
      return 0;
   }
   thread_handle = CreateThread(NULL, 0, input_thread, NULL, 0, &thread_id);
   if (!thread_handle)
   {
      status = "Input thread creation failed";
      return 0;
   }
   if (WaitForSingleObject(thread_ready, 5000) != WAIT_OBJECT_0 || !worker_ready)
   {
      status = "Input thread did not become ready";
      return 0;
   }
   CloseHandle(thread_ready);
   thread_ready = NULL;
   apply_bloom_disabled(QBMF_BloomPreference());
   QBMF_SetFlaresDisabled(QBMF_FlaresPreference());
   apply_depth_of_field_disabled(QBMF_DepthOfFieldPreference());
   apply_motion_blur_disabled(QBMF_BlurPreference());
   return apply_raw_mouse_enabled(QBMF_RawMousePreference());
}
void QBMF_StopIntegrated(void);
int QBMF_StartIntegrated(HMODULE addon, QBMF_GetConfigFn get, QBMF_SetConfigFn set)
{
   self_module = addon;
   if (GetModuleHandleW(L"MouseFix.addon64") || GetModuleHandleW(L"MouseFix.addon"))
   {
      status = "Remove the standalone Mouse Fix and restart";
      return 0;
   }
   QBMF_LoadSettings(addon, get, set);
   if (!QBMF_Initialize())
   {
      QBMF_StopIntegrated();
      return 0;
   }
   if (QBCF_Initialize()) QBMF_SetCutsceneEnabled(QBMF_CutscenePreference());
   if (QBP_Initialize(GetModuleHandleW(L"renderer_x64_f.dll"))) QBMF_SetHighResolutionParticles(QBMF_ParticlePreference());
   return initialized;
}
void QBMF_StopIntegrated(void)
{
   QBP_Shutdown();
   QBCF_Shutdown();
   if (input_window) SendMessageW(input_window, change_mode, 0, 0);
   if (input_window) PostMessageW(input_window, WM_CLOSE, 0, 0);
   if (thread_handle)
   {
      WaitForSingleObject(thread_handle, 3000);
      CloseHandle(thread_handle);
      thread_handle = NULL;
   }
   if (controls_window && IsWindow(controls_window))
   {
      SendMessageW(controls_window, controls_message, (WPARAM)self_module, 0);
      if ((WNDPROC)GetWindowLongPtrW(controls_window, GWLP_WNDPROC) == controls_wndproc)
      {
         WNDPROC previous = (WNDPROC)(uintptr_t)GetPropW(controls_window, controls_property);
         if (previous) SetWindowLongPtrW(controls_window, GWLP_WNDPROC, (LONG_PTR)previous);
         RemovePropW(controls_window, controls_property);
      }
   }
   if (get_raw_hook_enabled)
   {
      MH_DisableHook(get_raw_target);
      get_raw_hook_enabled = false;
   }
   if (get_raw_hook_created)
   {
      MH_RemoveHook(get_raw_target);
      get_raw_hook_created = false;
      original_get_raw = GetRawInputData;
   }
   if (hook_enabled)
   {
      MH_DisableHook(game + 0x4856e0);
      hook_enabled = false;
   }
   if (hook_created)
   {
      MH_RemoveHook(game + 0x4856e0);
      hook_created = false;
   }
   if (register_hook_enabled)
   {
      MH_DisableHook(register_target);
      register_hook_enabled = false;
   }
   if (register_hook_created)
   {
      MH_RemoveHook(register_target);
      register_hook_created = false;
      original_register = RegisterRawInputDevices;
   }
   if (blur_disabled) apply_motion_blur_disabled(0);
   if (bloom_disabled) apply_bloom_disabled(0);
   if (flare_disabled) apply_flares_disabled(0);
   if (depth_of_field_disabled) apply_depth_of_field_disabled(0);
   if (thread_ready)
   {
      CloseHandle(thread_ready);
      thread_ready = NULL;
   }
   initialized = false;
   last_camera_time = 0;
}
