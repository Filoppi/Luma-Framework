#pragma once

#include "memory_scan.h"

// Frostbite's settings containers (the ones the "-Container.Field" launch options set) and their fields, found in memory: NFS16.exe is
// packed, so its code and type info only exist once the game runs.
namespace Frostbite
{
   // A field to look up in a class's type info
   struct FieldOffset
   {
      const char* name;
      int offset = -1; // -1 = not found
   };

   // The engine's settings lookup, "container = lookup(manager, name)". It compares names by their text, so once found it can be asked
   // for any container, not only the one it was found by.
   class SettingsLookup
   {
   public:
      // From the code that looks up the container "known_name": all the call sites found have to agree on the manager and the
      // function, and there have to be at least two (NFS16.exe has five for "WorldRender")
      bool Find(const char* known_name);
      // The live container "name", or null
      uint8_t* GetContainer(const char* name) const;

   private:
      static bool DecodeCall(const uint8_t* lea, SettingsLookup& call);
      static uint8_t* CallGuarded(const uint8_t* function, void* manager, const char* name);

      const uint8_t* manager = nullptr; // Where the settings manager pointer is stored
      const uint8_t* function = nullptr;
   };

   namespace Detail
   {
      // Frostbite's class type info records, as laid out in NFS16.exe. A class record: name, uint16 flags, uint16 size, ..., uint16
      // field count, ..., the field records. A field record: name, uint16 flags, uint16 offset in the class, then the field's type.
      constexpr size_t kClassSize = 0x0A;
      constexpr size_t kClassFieldCount = 0x1A;
      constexpr size_t kClassFields = 0x30;
      constexpr size_t kFieldRecordSize = 0x18;
      constexpr size_t kFieldOffset = 0x0A;
      constexpr size_t kMaxNameLength = 64;

      // Sets the offset of the wanted field a field record describes, if it describes one
      inline void ReadFieldRecord(const uint8_t* record, uint16_t class_size, std::span<FieldOffset> fields)
      {
         const char* name = MemoryScan::Read<const char*>(record);
         const uint16_t offset = MemoryScan::Read<uint16_t>(record + kFieldOffset);
         if (offset >= class_size || !MemoryScan::IsPrintableString(name, kMaxNameLength))
            return;
         for (FieldOffset& field : fields)
            if (std::strcmp(name, field.name) == 0)
               field.offset = offset;
      }

      // The class size, if "record" is a class record with all the wanted fields (0 otherwise)
      inline uint16_t ReadClassRecord(const uint8_t* record, std::span<FieldOffset> fields)
      {
         if (!MemoryScan::IsReadable(record, kClassFields + sizeof(void*)))
            return 0;
         const uint16_t class_size = MemoryScan::Read<uint16_t>(record + kClassSize);
         const uint16_t field_count = MemoryScan::Read<uint16_t>(record + kClassFieldCount);
         const uint8_t* field_records = MemoryScan::Read<const uint8_t*>(record + kClassFields);
         if (field_count == 0 || !MemoryScan::IsReadable(field_records, size_t(field_count) * kFieldRecordSize))
            return 0;

         for (FieldOffset& field : fields)
            field.offset = -1;
         for (size_t i = 0; i < field_count; i++)
            ReadFieldRecord(field_records + i * kFieldRecordSize, class_size, fields);
         const bool all_found = std::all_of(fields.begin(), fields.end(), [](const FieldOffset& field)
            { return field.offset >= 0; });
         if (!all_found)
            return 0;
         return class_size;
      }
   } // namespace Detail

   // Looks up the offsets of "fields" in a class's type info: the class record that points to the class name and has them all. Returns
   // the class size (0 if not found).
   inline uint16_t FindClassFields(const char* class_name, std::span<FieldOffset> fields)
   {
      for (const char* name : MemoryScan::FindStrings(class_name))
         for (const uint8_t* record : MemoryScan::FindPointersTo(name))
            if (const uint16_t class_size = Detail::ReadClassRecord(record, fields))
               return class_size;
      return 0;
   }

   // A call site is a "lea rdx" of a copy of the name, next to the manager's load and the call. Any disagreement means it's not the lookup.
   inline bool SettingsLookup::Find(const char* known_name)
   {
      // Every copy of it in the exe
      const std::vector<const char*> names = MemoryScan::FindStrings(known_name);
      if (names.empty())
         return false;

      const std::vector<System::BytePattern> lea_rdx = {0x48, 0x8D, 0x15, System::ANY, System::ANY, System::ANY, System::ANY}; // lea rdx, [rip+x]
      SettingsLookup agreed;
      size_t call_sites = 0;
      for (const uint8_t* lea : MemoryScan::FindPattern(lea_rdx, MemoryScan::RegionKind::Executable))
      {
         const auto* target = reinterpret_cast<const char*>(MemoryScan::GetRipTarget(lea, 3, 7));
         SettingsLookup call;
         // The cheap name check first: there are many leas, and decoding queries the memory's protection
         if (!Contains(names, target) || !DecodeCall(lea, call))
            continue;
         if (call_sites > 0 && (call.manager != agreed.manager || call.function != agreed.function))
            return false;
         agreed = call;
         call_sites++;
      }
      if (call_sites < 2)
         return false;
      *this = agreed;
      return true;
   }

   // The manager is read at each call: the engine creates it at startup, so it's null until then
   inline uint8_t* SettingsLookup::GetContainer(const char* name) const
   {
      if (!MemoryScan::IsReadable(manager, sizeof(void*)))
         return nullptr;
      void* const manager_instance = MemoryScan::Read<void*>(manager);
      if (!manager_instance)
         return nullptr;
      return CallGuarded(function, manager_instance, name);
   }

   // The code reading a container does "mov rcx, [rip+manager]; lea rdx, [rip+name]; call lookup; test rax, rax", with the first two in
   // either order. This decodes it around the lea.
   inline bool SettingsLookup::DecodeCall(const uint8_t* lea, SettingsLookup& call)
   {
      auto is_mov_rcx = [](const uint8_t* p) // mov rcx, [rip+x]
      { return p[0] == 0x48 && p[1] == 0x8B && p[2] == 0x0D; };
      auto is_call_test = [](const uint8_t* p) // call x; test rax, rax
      { return p[0] == 0xE8 && p[5] == 0x48 && p[6] == 0x85 && p[7] == 0xC0; };

      // Room for the mov (7 bytes) on either side of the lea (7), then the call (5) and the test (3)
      if (!MemoryScan::IsReadable(lea - 7, 7 + 7 + 7 + 5 + 3))
         return false;
      const bool mov_first = is_mov_rcx(lea - 7) && is_call_test(lea + 7);
      const bool lea_first = is_mov_rcx(lea + 7) && is_call_test(lea + 14);
      if (!mov_first && !lea_first)
         return false;
      const uint8_t* mov = lea + 7;
      const uint8_t* call_instruction = lea + 14;
      if (mov_first)
      {
         mov = lea - 7;
         call_instruction = lea + 7;
      }
      call.manager = MemoryScan::GetRipTarget(mov, 3, 7);
      call.function = MemoryScan::GetRipTarget(call_instruction, 1, 5);
      return true;
   }

   // Under structured exception handling, in case the function isn't the lookup after all (so no C++ objects in here)
   inline uint8_t* SettingsLookup::CallGuarded(const uint8_t* function, void* manager, const char* name)
   {
      using Lookup = uint8_t* (*)(void* manager, const char* name);
      __try
      {
         return reinterpret_cast<Lookup>(const_cast<uint8_t*>(function))(manager, name);
      }
      __except (EXCEPTION_EXECUTE_HANDLER)
      {
         return nullptr;
      }
   }
} // namespace Frostbite
