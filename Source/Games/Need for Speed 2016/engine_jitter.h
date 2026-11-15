#pragma once

#include "frostbite_settings.h"
#include "log.h"

// The engine's TAA jitter settings, in Frostbite's "WorldRender" settings container. The engine reads them every frame and rebuilds its
// jitter sequence when they change, so the mod sets them there, as the Mass Effect Andromeda mod does.
class EngineJitter
{
public:
   // Looks up the settings and the engine's jitter table, once: a second lookup would take the mod's settings for the game's own
   void Find(const Frostbite::SettingsLookup& lookup);

   // Sets this frame's jitter. While an SR type is selected: Halton (2, 3) with its phase count (the game's own long correlated
   // multi-jittered sequence leaves SR soft), the standard one in the engine's table if it was found, else the engine's own.
   // Otherwise the game's own settings.
   void Apply(bool sr_selected, uint32_t sr_phases);

   // For the Info tab
   std::string Describe() const;

private:
   bool FindTable();
   static bool IsTable(const uint8_t* pointers, size_t floats);
   uint32_t GetCount() const;
   bool GetUseCmj() const;
   static const char* GetSequenceName(bool use_cmj);
   void Set(uint32_t count, bool use_cmj);
   void WriteStandardHalton(uint32_t count);

   bool searched = false;
   uint8_t* container = nullptr;
   int count_offset = -1;   // "TemporalAAJitterCount" (uint32): the sequence length
   int use_cmj_offset = -1; // "TemporalAAJitterUseCmj" (bool): the engine's table, else its own Halton (2, 3) from index 0
   // The game's own settings: a 1024 point correlated multi-jittered table
   uint32_t game_count = 0;
   bool game_use_cmj = false;
   // Where the engine keeps its table's begin and end (a vector of float2 pixel offsets in [-0.5, 0.5])
   float* const* table_begin = nullptr;
   float* const* table_end = nullptr;
   bool writes_standard_halton = false; // This frame
};

// The field offsets from the type info, the live container through the settings lookup, then the table by its data
inline void EngineJitter::Find(const Frostbite::SettingsLookup& lookup)
{
   if (searched)
      return;
   searched = true;
   Frostbite::FieldOffset fields[] = {{"TemporalAAJitterCount"}, {"TemporalAAJitterUseCmj"}};
   const uint16_t container_size = Frostbite::FindClassFields("WorldRenderSettings", fields);
   if (container_size == 0)
   {
      LogDiagnostic(reshade::log::level::warning, "the engine's jitter settings weren't found in its type info, so SR gets the game's own jitter");
      return;
   }
   uint8_t* const found_container = lookup.GetContainer("WorldRender");
   if (!found_container || !MemoryScan::IsReadable(found_container, container_size))
   {
      LogDiagnostic(reshade::log::level::warning, "the engine's \"WorldRender\" settings weren't found, so SR gets the game's own jitter");
      return;
   }
   container = found_container;
   count_offset = fields[0].offset;
   use_cmj_offset = fields[1].offset;
   game_count = GetCount();
   game_use_cmj = GetUseCmj();

   // Only the game's own long table is distinctive enough to be found by its data (launch options can turn it off)
   constexpr uint32_t kMinTablePoints = 64;
   const bool table_found = game_use_cmj && game_count >= kMinTablePoints && FindTable();
   if (!table_found)
      LogDiagnostic(reshade::log::level::warning, "the engine's jitter table wasn't found, so SR gets the engine's own Halton (which starts at the pixel corner)");
   LogDiagnostic(reshade::log::level::info, "the engine's jitter settings were found: {} points, {} (the game's own)", game_count, GetSequenceName(game_use_cmj));
}

// At every present, before the engine's next frame reads the settings
inline void EngineJitter::Apply(bool sr_selected, uint32_t sr_phases)
{
   if (!container)
      return;
   writes_standard_halton = sr_selected && table_begin;
   if (!sr_selected)
   {
      Set(game_count, game_use_cmj);
      return;
   }
   // The engine uses its table while CMJ is on
   Set(sr_phases, writes_standard_halton);
   if (writes_standard_halton)
      WriteStandardHalton(sr_phases);
}

// The live settings and the game's own, e.g. "8 points, the standard Halton in its table (the game's own: 1024 points, its table)"
inline std::string EngineJitter::Describe() const
{
   if (!container && searched)
      return "not found (see the log)";
   if (!container)
      return "not looked up yet (at the first frame in game)";
   const char* sequence = GetSequenceName(GetUseCmj());
   if (writes_standard_halton)
      sequence = "the standard Halton in its table";
   return std::format("{} points, {} (the game's own: {} points, {})", GetCount(), sequence, game_count, GetSequenceName(game_use_cmj));
}

// Which sequence the engine uses: its table while CMJ is on
inline const char* EngineJitter::GetSequenceName(bool use_cmj)
{
   if (use_cmj)
      return "its table";
   return "its own Halton";
}

// The engine's jitter table, found by its data (Mass Effect Andromeda finds it by code, which differs in NFS16.exe): the one pair of
// pointers in the exe's data that spans exactly the game's own number of points
inline bool EngineJitter::FindTable()
{
   const size_t floats = size_t(game_count) * 2;
   std::vector<const uint8_t*> candidates;
   for (const MemoryScan::Region& region : MemoryScan::GetImageRegions(MemoryScan::RegionKind::Writable))
      for (const uint8_t* pointers = region.begin; pointers + 2 * sizeof(void*) <= region.end; pointers += sizeof(void*))
         if (IsTable(pointers, floats))
            candidates.push_back(pointers);
   if (candidates.size() != 1)
      return false;
   table_begin = reinterpret_cast<float* const*>(candidates[0]);
   table_end = reinterpret_cast<float* const*>(candidates[0] + sizeof(void*));
   return true;
}

// Whether a begin and end pointer pair spans "floats" pixel offsets, all within [-0.5, 0.5] and not all the same
inline bool EngineJitter::IsTable(const uint8_t* pointers, size_t floats)
{
   const float* begin = MemoryScan::Read<const float*>(pointers);
   const float* end = MemoryScan::Read<const float*>(pointers + sizeof(void*));
   if (end < begin || size_t(end - begin) != floats || !MemoryScan::IsReadable(begin, floats * sizeof(float)))
      return false;
   const bool in_range = std::all_of(begin, end, [](float f)
      { return f >= -0.5f && f <= 0.5f; });
   const bool varied = std::any_of(begin, end, [&](float f)
      { return f != begin[0]; });
   return in_range && varied;
}

// The live jitter count (the field's offset needn't be aligned)
inline uint32_t EngineJitter::GetCount() const
{
   return MemoryScan::Read<uint32_t>(container + count_offset);
}

// Whether the engine uses its table (else its own Halton)
inline bool EngineJitter::GetUseCmj() const
{
   return container[use_cmj_offset] != 0;
}

// Writes only changes: the engine rebuilds its sequence when they change
inline void EngineJitter::Set(uint32_t count, bool use_cmj)
{
   if (GetCount() == count && GetUseCmj() == use_cmj)
      return;
   std::memcpy(container + count_offset, &count, sizeof(count));
   container[use_cmj_offset] = uint8_t(use_cmj);
}

// Fills the engine's table with the standard Halton (2, 3), the one SR implementations generate themselves: from index 1, so without the
// engine's pixel corner sample, and centred. The engine resizes the table in the frame after a count change, so this waits until it has
// "count" points. x is negated because SR gets the engine's x mirrored.
inline void EngineJitter::WriteStandardHalton(uint32_t count)
{
   float* points = *table_begin;
   const float* end = *table_end;
   if (!points || end < points || size_t(end - points) != size_t(count) * 2)
      return;
   for (uint32_t i = 0; i < count; i++)
   {
      points[i * 2] = -SR::HaltonSequence(i, 2);
      points[i * 2 + 1] = SR::HaltonSequence(i, 3);
   }
}
