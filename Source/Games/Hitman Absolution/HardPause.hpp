#pragma once

// GOG 1.0.447.0: the game owns this timer; no thread or rendering suspension is used.
namespace HMAHardPause
{
   static bool requested = false;
   static bool available = false;
   static bool owned = false;
   static uint32_t available_status = 0;
   static uint32_t actual_status = 0;
   static bool key_down = false;
   static volatile char* pause_byte = nullptr;

   static bool Writable(const void* p)
   {
      MEMORY_BASIC_INFORMATION info = {};
      if (!VirtualQuery(p, &info, sizeof(info)) || info.State != MEM_COMMIT || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)))
         return false;
      const DWORD protect = info.Protect & 0xff;
      return protect == PAGE_READWRITE || protect == PAGE_WRITECOPY || protect == PAGE_EXECUTE_READWRITE || protect == PAGE_EXECUTE_WRITECOPY;
   }

   static void Init()
   {
      auto* base = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
      if (!base) return;
      auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
      if (dos->e_magic != IMAGE_DOS_SIGNATURE) return;
      auto* nt = reinterpret_cast<IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
      if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
          nt->FileHeader.TimeDateStamp != 0x5c9a0bf7 || nt->OptionalHeader.SizeOfImage != 0x2224000 ||
          nt->OptionalHeader.CheckSum != 0x2088538) return;
      const auto* sections = IMAGE_FIRST_SECTION(nt);
      const IMAGE_SECTION_HEADER* text = nullptr;
      const IMAGE_SECTION_HEADER* data = nullptr;
      for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i)
      {
         if (memcmp(sections[i].Name, ".text\0\0\0", 8) == 0) text = &sections[i];
         if (memcmp(sections[i].Name, ".data\0\0\0", 8) == 0) data = &sections[i];
      }
      if (!text || !data || !(text->Characteristics & IMAGE_SCN_MEM_EXECUTE) || !(data->Characteristics & IMAGE_SCN_MEM_WRITE)) return;
      const uint8_t signature[] = {0xb9,0,0,0,0,0x89,0x4d,0xfc,0x8b,0x4e,0x54,0x89,0x45,0xf8};
      uint8_t* match = nullptr;
      for (uint32_t i = 0; i + sizeof(signature) <= text->Misc.VirtualSize; ++i)
      {
         auto* p = base + text->VirtualAddress + i;
         if (p[0] == signature[0] && memcmp(p + 5, signature + 5, sizeof(signature) - 5) == 0)
         {
            if (match) return;
            match = p;
         }
      }
      if (!match) return;
      uint32_t address = 0;
      memcpy(&address, match + 1, sizeof(address));
      auto* manager = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(address));
      auto* data_begin = base + data->VirtualAddress;
      auto* data_end = data_begin + data->Misc.VirtualSize;
      if (manager < data_begin || manager > data_end - 0x88 || !Writable(manager + 0x68)) return;
      // Independently verify the game routine that sets this exact byte.
      auto* instruction = base + 0x26cb50;
      uint32_t operand = 0;
      memcpy(&operand, instruction + 2, sizeof(operand));
      if (instruction[0] != 0xc6 || instruction[1] != 0x05 || operand != address + 0x68 || instruction[6] != 1 || instruction[7] != 0xc3) return;
      pause_byte = reinterpret_cast<volatile char*>(manager + 0x68);
      available = true;
      available_status = 1;
   }

   static void Resume()
   {
      if (owned && pause_byte && Writable(const_cast<const char*>(pause_byte)))
         _InterlockedCompareExchange8(pause_byte, 0, 1);
      owned = false;
   }

   static void Update(bool hotkey_enabled = true, bool pause_on_focus_loss = false)
   {
      const bool focused = game_window && GetForegroundWindow() == game_window;
      const bool down = hotkey_enabled && focused && (GetAsyncKeyState(VK_PAUSE) & 0x8000);
      if (down && !key_down && available) requested = !requested;
      key_down = down;
      if (!available || !pause_byte || !Writable(const_cast<const char*>(pause_byte))) { requested = false; return; }
      const bool want_pause = requested || (pause_on_focus_loss && game_window && !focused);
      if (want_pause && !owned)
      {
         const char prior = _InterlockedCompareExchange8(pause_byte, 1, 0);
         if (prior == 0) owned = true;
         else requested = false; // Do not claim a pause already owned by the game.
      }
      else if (!want_pause) Resume();
      else if (*pause_byte != 1) { owned = false; requested = false; }
      actual_status = *pause_byte == 1 ? 1u : 0u;
   }
}

