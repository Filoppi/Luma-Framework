#pragma once

#include <atomic>
#include <cstdint>
#include <Windows.h>

namespace Display
{
   // Observes the game's messages without replacing its window procedure. Only present reads HDR metadata.
   struct ChangeNotifications
   {
      static inline std::atomic<HWND> window{nullptr};
      static inline std::atomic<uint64_t> revision{1};
      HHOOK hook = nullptr;
      HPOWERNOTIFY brightness_notification = nullptr;
      // GUID_VIDEO_CURRENT_MONITOR_BRIGHTNESS, kept local to avoid an extra GUID-library dependency.
      static constexpr GUID brightness = {0x8ffee2c6, 0x2d01, 0x46be, {0xad, 0xb9, 0x39, 0x8a, 0xdd, 0xc5, 0xb4, 0xff}};

      static LRESULT CALLBACK OnMessage(int code, WPARAM wParam, LPARAM lParam)
      {
         if (code >= 0)
         {
            const auto& message = *reinterpret_cast<const CWPSTRUCT*>(lParam);
            if (message.hwnd == window.load(std::memory_order_relaxed))
            {
               bool changed = message.message == WM_DISPLAYCHANGE || message.message == WM_DEVICECHANGE ||
                  (message.message == WM_ACTIVATE && LOWORD(message.wParam) != WA_INACTIVE);
               if (message.message == WM_POWERBROADCAST)
               {
                  const auto* setting = reinterpret_cast<const POWERBROADCAST_SETTING*>(message.lParam);
                  changed = message.wParam == PBT_APMRESUMEAUTOMATIC ||
                     (message.wParam == PBT_POWERSETTINGCHANGE && setting && IsEqualGUID(setting->PowerSetting, brightness));
               }
               if (changed)
                  revision.fetch_add(1, std::memory_order_relaxed);
            }
         }
         return CallNextHookEx(nullptr, code, wParam, lParam);
      }

      void Stop()
      {
         window.store(nullptr, std::memory_order_relaxed);
         if (brightness_notification)
            UnregisterPowerSettingNotification(brightness_notification);
         brightness_notification = nullptr;
         if (hook)
            UnhookWindowsHookEx(hook);
         hook = nullptr;
      }

      void Watch(HWND hwnd)
      {
         if (hwnd == window.load(std::memory_order_relaxed))
            return;
         Stop();
         DWORD process = 0;
         const DWORD thread = GetWindowThreadProcessId(hwnd, &process);
         if (!thread || process != GetCurrentProcessId())
            return; // Never install a global or cross-process hook.
         window.store(hwnd, std::memory_order_relaxed);
         hook = SetWindowsHookExW(WH_CALLWNDPROC, OnMessage, nullptr, thread);
         if (hook)
            brightness_notification = RegisterPowerSettingNotification(hwnd, &brightness, DEVICE_NOTIFY_WINDOW_HANDLE);
         revision.fetch_add(1, std::memory_order_relaxed);
      }
   };

   inline ChangeNotifications change_notifications;
}
