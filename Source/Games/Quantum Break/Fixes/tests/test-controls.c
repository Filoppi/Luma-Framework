#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static HWND test_foreground;
static HWND WINAPI foreground_for_test(void)
{
   return test_foreground;
}
#define GetForegroundWindow foreground_for_test
#include "../MouseFix.c"
#undef GetForegroundWindow
#undef NDEBUG
#include <assert.h>
#include <stdio.h>

int QBMF_RegisterUI(HMODULE addon, HMODULE reshade)
{
   (void)addon;
   (void)reshade;
   return 1;
}
void QBMF_UnregisterUI(HMODULE addon, HMODULE reshade)
{
   (void)addon;
   (void)reshade;
}
int QBCF_Initialize(void)
{
   return 0;
}
void QBCF_Shutdown(void)
{
}
int QBMF_SetCutsceneEnabled(int value)
{
   (void)value;
   return 0;
}
static RAWINPUT received[64];
static unsigned received_count;
static LRESULT CALLBACK native_window(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
   if (message == WM_INPUT)
   {
      UINT size = 0;
      assert(GetRawInputData((HRAWINPUT)lparam, RID_INPUT, NULL, &size, sizeof(RAWINPUTHEADER)) == 0);
      assert(size == sizeof(RAWINPUT));
      RAWINPUTHEADER header;
      UINT header_size = sizeof header;
      assert(GetRawInputData((HRAWINPUT)lparam, RID_HEADER, &header, &header_size, sizeof header) == sizeof header);
      assert(header.dwType == RIM_TYPEMOUSE && header.wParam == RIM_INPUT);
      BYTE small;
      UINT short_size = 1;
      assert(GetRawInputData((HRAWINPUT)lparam, RID_INPUT, &small, &short_size, sizeof header) == (UINT)-1);
      assert(GetLastError() == ERROR_INSUFFICIENT_BUFFER && short_size == sizeof(RAWINPUT));
      size = sizeof(RAWINPUT);
      assert(received_count < 64);
      assert(GetRawInputData((HRAWINPUT)lparam, RID_INPUT, &received[received_count++], &size, sizeof header) == sizeof(RAWINPUT));
      return 0;
   }
   return DefWindowProcW(window, message, wparam, lparam);
}
static void pump(void)
{
   MSG msg;
   while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
   {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
   }
}
int main(void)
{
   self_module = GetModuleHandleW(NULL);
   process_id = GetCurrentProcessId();
   WNDCLASSW cls = {0};
   cls.lpfnWndProc = native_window;
   cls.hInstance = self_module;
   cls.lpszClassName = L"MouseFixControlsTest";
   assert(RegisterClassW(&cls));
   HWND window = CreateWindowExW(0, cls.lpszClassName, L"", 0, 0, 0, 1, 1, NULL, NULL, self_module, NULL);
   assert(window);
   test_foreground = window;
   assert(MH_Initialize() == MH_OK);
   get_raw_target = (void*)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetRawInputData");
   assert(MH_CreateHook(get_raw_target, get_raw_input, (LPVOID*)&original_get_raw) == MH_OK);
   assert(MH_EnableHook(get_raw_target) == MH_OK);
   const USHORT flags[] = {RI_MOUSE_LEFT_BUTTON_DOWN, RI_MOUSE_LEFT_BUTTON_UP, RI_MOUSE_RIGHT_BUTTON_DOWN, RI_MOUSE_RIGHT_BUTTON_UP, RI_MOUSE_MIDDLE_BUTTON_DOWN, RI_MOUSE_MIDDLE_BUTTON_UP, RI_MOUSE_BUTTON_4_DOWN, RI_MOUSE_BUTTON_4_UP, RI_MOUSE_BUTTON_5_DOWN, RI_MOUSE_BUTTON_5_UP, RI_MOUSE_WHEEL, RI_MOUSE_WHEEL, RI_MOUSE_HWHEEL, RI_MOUSE_LEFT_BUTTON_DOWN | RI_MOUSE_RIGHT_BUTTON_UP};
   for (unsigned i = 0; i < sizeof flags / sizeof flags[0]; i++)
   {
      RAWINPUT input = {0};
      input.header.dwType = RIM_TYPEMOUSE;
      input.header.dwSize = sizeof input;
      input.header.hDevice = (HANDLE)(uintptr_t)0x1234;
      input.header.wParam = RIM_INPUTSINK;
      input.data.mouse.usButtonFlags = flags[i];
      input.data.mouse.usButtonData = (USHORT)(SHORT)(i == 11 ? -120 : 120);
      input.data.mouse.lLastX = 127;
      input.data.mouse.lLastY = -53;
      assert(forward_controls(window, &input));
      assert(input.data.mouse.lLastX == 127 && input.data.mouse.lLastY == -53);
   }
   pump();
   assert(received_count == sizeof flags / sizeof flags[0]);
   for (unsigned i = 0; i < received_count; i++)
   {
      assert(received[i].data.mouse.usButtonFlags == flags[i]);
      assert(received[i].data.mouse.usButtonData == (USHORT)(SHORT)(i == 11 ? -120 : 120));
      assert(received[i].data.mouse.lLastX == 0 && received[i].data.mouse.lLastY == 0);
      assert(received[i].header.hDevice == (HANDLE)(uintptr_t)0x1234);
   }
   RAWINPUT motion = {0};
   motion.header.dwType = RIM_TYPEMOUSE;
   motion.data.mouse.lLastX = 1000;
   assert(forward_controls(window, &motion));
   pump();
   assert(received_count == 14);
   RAWINPUT button = {0};
   button.header.dwType = RIM_TYPEMOUSE;
   button.data.mouse.usButtonFlags = RI_MOUSE_LEFT_BUTTON_DOWN;
   assert(forward_controls(window, &button));
   test_foreground = NULL;
   pump();
   assert(received_count == 14);
   test_foreground = window;
   assert(forward_controls(window, &button));
   overlay_capture = 1;
   pump();
   assert(received_count == 14);
   overlay_capture = 0;
   assert(forward_controls(window, &button));
   pump();
   assert(received_count == 15);
   UINT invalid_size = sizeof(RAWINPUT);
   assert(GetRawInputData(NULL, RID_INPUT, &button, &invalid_size, sizeof(RAWINPUTHEADER)) == (UINT)-1);
   WNDPROC previous = (WNDPROC)(uintptr_t)GetPropW(window, controls_property);
   assert(previous == native_window);
   SetWindowLongPtrW(window, GWLP_WNDPROC, (LONG_PTR)previous);
   RemovePropW(window, controls_property);
   assert(MH_DisableHook(get_raw_target) == MH_OK);
   assert(MH_RemoveHook(get_raw_target) == MH_OK);
   DestroyWindow(window);
   UnregisterClassW(cls.lpszClassName, self_module);
   MH_Uninitialize();
   puts("PASS: all five buttons, vertical/horizontal wheel, signed wheel delta, ordered native delivery, no duplicate movement, foreground/overlay filtering, and real handle passthrough.");
   return 0;
}
