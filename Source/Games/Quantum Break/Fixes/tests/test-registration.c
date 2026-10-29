#include "../MouseFix.c"
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

static RAWINPUTDEVICE mouse_registration(void)
{
   RAWINPUTDEVICE devices[64];
   UINT count = 64;
   UINT n = GetRegisteredRawInputDevices(devices, &count, sizeof devices[0]);
   assert(n != (UINT)-1 && n <= 64);
   for (UINT i = 0; i < n; i++)
      if (devices[i].usUsagePage == 1 && devices[i].usUsage == 2) return devices[i];
   return (RAWINPUTDEVICE){0};
}
static void expect_mouse(DWORD flags, HWND window)
{
   RAWINPUTDEVICE device = mouse_registration();
   assert(device.usUsagePage == 1 && device.usUsage == 2 && device.dwFlags == flags && device.hwndTarget == window);
}
int main(void)
{
   BYTE bloom_native = 1, flare_native = 1, blur_native = 1;
   bloom_flag = &bloom_native;
   flare_flag = &flare_native;
   blur_flag = &blur_native;
   assert(QBMF_SetBloomDisabled(1) == 1 && bloom_native == 0 && flare_native == 1 && blur_native == 1 && QBMF_BloomPreference() == 1);
   assert(QBMF_GetBloomEnabled() == 0);
   assert(QBMF_SetBloomDisabled(0) == 1 && bloom_native == 1 && flare_native == 1 && blur_native == 1 && QBMF_BloomPreference() == 0);
   assert(QBMF_GetBloomEnabled() == 1);
   assert(QBMF_SetBloomDisabled(1) == 1 && bloom_native == 0 && flare_native == 1);
   assert(apply_bloom_disabled(0) == 1 && bloom_native == 1 && flare_native == 1);
   for (BYTE bloom_initial = 0; bloom_initial <= 1; bloom_initial++)
      for (BYTE flare_initial = 0; flare_initial <= 1; flare_initial++)
      {
         for (int disable_bloom = 0; disable_bloom <= 1; disable_bloom++)
            for (int disable_flares = 0; disable_flares <= 1; disable_flares++)
            {
               bloom_native = bloom_initial;
               flare_native = flare_initial;
               bloom_captured = false;
               flare_captured = false;
               assert(QBMF_SetBloomDisabled(disable_bloom) && flare_native == flare_initial);
               assert(QBMF_SetFlaresDisabled(disable_flares));
               assert(bloom_native == (disable_bloom ? 0 : bloom_initial) && flare_native == (disable_flares ? 0 : flare_initial));
               assert(QBMF_GetBloomEnabled() == bloom_native && QBMF_GetFlaresEnabled() == flare_native);
               assert(QBMF_SetBloomDisabled(disable_bloom) && QBMF_SetFlaresDisabled(disable_flares));
               assert(apply_bloom_disabled(0) && bloom_native == bloom_initial && flare_native == (disable_flares ? 0 : flare_initial));
               assert(apply_flares_disabled(0) && flare_native == flare_initial && blur_native == 1);
            }
      }
   flare_flag = NULL;
   bloom_native = 1;
   bloom_captured = false;
   assert(QBMF_SetBloomDisabled(1) == 1 && bloom_native == 0 && QBMF_GetBloomEnabled() == 0);
   assert(QBMF_SetFlaresDisabled(1) == 0 && QBMF_GetFlaresEnabled() == -1);
   bloom_flag = NULL;
   assert(QBMF_SetBloomDisabled(1) == 0 && QBMF_GetBloomEnabled() == -1);
   flare_flag = &flare_native;
   flare_native = 1;
   flare_captured = false;
   assert(QBMF_SetFlaresDisabled(1) == 1 && flare_native == 0 && QBMF_GetFlaresEnabled() == 0);
   assert(apply_flares_disabled(0) && flare_native == 1);
   flare_flag = NULL;
   blur_flag = NULL;
   BYTE dof_native;
   for (BYTE initial = 0; initial <= 1; initial++)
   {
      dof_native = initial;
      depth_of_field_flag = &dof_native;
      depth_of_field_captured = false;
      assert(QBMF_SetDepthOfFieldDisabled(1) == 1 && dof_native == 0 && QBMF_GetDepthOfFieldEnabled() == 0);
      assert(QBMF_SetDepthOfFieldDisabled(1) == 1 && dof_native == 0);
      assert(QBMF_SetDepthOfFieldDisabled(0) == 1 && dof_native == initial && QBMF_DepthOfFieldPreference() == 0);
   }
   depth_of_field_flag = NULL;
   assert(QBMF_SetDepthOfFieldDisabled(1) == 0 && QBMF_GetDepthOfFieldEnabled() == -1);
   puts("PASS: independent bloom/flares for all original states and toggle combinations, separate restoration and missing-feature handling, plus depth-of-field restoration.");
   self_module = GetModuleHandleW(NULL);
   process_id = GetCurrentProcessId();
   assert(capture_registration());
   assert(original_registration.usUsagePage == 1 && original_registration.usUsage == 2 && original_registration.dwFlags == 0 && original_registration.hwndTarget == NULL);
   WNDCLASSW cls = {0};
   cls.lpfnWndProc = input_wndproc;
   cls.hInstance = self_module;
   cls.lpszClassName = L"MouseFixRegistrationTest";
   assert(RegisterClassW(&cls));
   input_window = CreateWindowExW(0, cls.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, self_module, NULL);
   assert(input_window);
   register_target = (void*)GetProcAddress(GetModuleHandleW(L"user32.dll"), "RegisterRawInputDevices");
   assert(MH_Initialize() == MH_OK);
   assert(MH_CreateHook(register_target, register_raw_input, (LPVOID*)&original_register) == MH_OK);
   assert(MH_EnableHook(register_target) == MH_OK);

   assert(SendMessageW(input_window, change_mode, 1, 0) == 1);
   expect_mouse(RIDEV_INPUTSINK, input_window);
   RAWINPUTDEVICE game_devices[] = {{1, 2, 0, NULL}, {1, 6, 0, NULL}};
   assert(RegisterRawInputDevices(game_devices, 2, sizeof game_devices[0]));
   expect_mouse(RIDEV_INPUTSINK, input_window);
   assert(registration_redirects == 1);
   assert(game_devices[0].dwFlags == 0 && game_devices[0].hwndTarget == NULL);
   RAWINPUTDEVICE devices[64];
   UINT count = 64, n = GetRegisteredRawInputDevices(devices, &count, sizeof devices[0]);
   bool keyboard = false;
   for (UINT i = 0; i < n; i++)
      if (devices[i].usUsagePage == 1 && devices[i].usUsage == 6)
      {
         keyboard = true;
         assert(devices[i].dwFlags == 0 && devices[i].hwndTarget == NULL);
      }
   assert(keyboard);

   assert(SendMessageW(input_window, change_mode, 0, 0) == 1);
   expect_mouse(0, NULL);
   assert(enabled == 0);
   assert(RegisterRawInputDevices(game_devices, 2, sizeof game_devices[0]));
   expect_mouse(0, NULL);
   assert(registration_redirects == 1);
   assert(SendMessageW(input_window, change_mode, 1, 0) == 1);
   expect_mouse(RIDEV_INPUTSINK, input_window);
   assert(RegisterRawInputDevices(game_devices, 2, sizeof game_devices[0]));
   expect_mouse(RIDEV_INPUTSINK, input_window);
   assert(registration_redirects == 2);

   RAWINPUTDEVICE invalid = {1, 2, RIDEV_REMOVE, input_window};
   assert(!RegisterRawInputDevices(&invalid, 1, sizeof invalid));
   assert(GetLastError() == ERROR_INVALID_PARAMETER);
   assert(enabled == 1);
   expect_mouse(RIDEV_INPUTSINK, input_window);
   HWND alternate = CreateWindowExW(0, cls.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, self_module, NULL);
   assert(alternate);
   RAWINPUTDEVICE foreign = {1, 2, RIDEV_INPUTSINK, alternate};
   assert(RegisterRawInputDevices(&foreign, 1, sizeof foreign));
   assert(enabled == 0);
   expect_mouse(RIDEV_INPUTSINK, alternate);

   assert(SendMessageW(input_window, change_mode, 0, 0) == 1);
   expect_mouse(0, NULL);
   assert(MH_DisableHook(register_target) == MH_OK);
   assert(MH_RemoveHook(register_target) == MH_OK);
   DestroyWindow(alternate);
   DestroyWindow(input_window);
   UnregisterClassW(cls.lpszClassName, self_module);
   MH_Uninitialize();
   puts("PASS: absent startup registration, on/off restoration, late game re-registration, keyboard preservation, invalid requests, and alternate input owner.");
   return 0;
}
