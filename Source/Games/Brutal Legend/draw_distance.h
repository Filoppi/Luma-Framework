#pragma once

// Draw Distance and Engine Detail for Brutal Legend (Win32 only).
// Settings: "Draw Distance Override" (config key "DrawDistanceOverride", default off) and "Draw Distance" (config key "DrawDistance", default 2.0, slider 0.5-8.0).
// Engine Detail (advanced): six override+value pairs on the same render block B, plus Detail Culling Distance and Shadow Distance (B+0xBC, B+0xC4),
// and a Force High-Res Texture Mips bool (T = *(*(base+0x1005D8C) + 0xD4), bytes T+0x44/T+0x45). See g_engine. Each has config keys "<name>" and "<name>Override".
// The game's render block B = *(uintptr_t*)(base + 0x1005ACC - 0x400000) + 0x188 holds four draw-distance floats
// (+0xA4, +0xA8, +0xB4, +0xB8). The game's own option writes 0.5..2.0 into all four.
// Override on: once per frame (present callback, render thread) DrawDistance is written to all four if any differs.
// Override off: nothing is written; the game's value returns the next time the player changes the in-game option.
// Engine Detail override on: the value is written every frame. Override turned off: the game default is written once, then nothing.
// B is validated every frame (VirtualQuery result cached per block pointer value). Invalid: nothing is done.
// Logs: "[BL DrawDistance]", once when B first becomes valid. Dev knobs (dd.*) are DEVELOPMENT only and drive the same g_engine values.

#include <windows.h>
#include <cstdarg>
#include <cstdint>
#include <cstdio>

namespace BLDrawDistance
{
   // Engine detail overrides, written to B + off while the override is on.
   struct EngineDetail
   {
      const char* key;           // config key of the value
      const char* override_key;  // config key of the override toggle
      uintptr_t off;             // offset from B
      float def;                 // game default, written once when the override turns off
      float lo;                  // slider min (config values are clamped to [lo, hi])
      float hi;                  // slider max
      bool hard;                 // true: values are clamped to [lo, hi] when applied too
      float value;
      bool override_on;
      bool was_on;               // override_on as of the last applied frame
      bool capture_def;          // true: def is read from the game at the first valid frame (before any write)
      bool def_captured;         // def has been captured
   };
   // Order: LODDistance (+0xA0), DetailCulling (+0x8C), ShaderLOD (+0x90), ShadowRange (+0x94), AmbientMeshDensity (+0x98), AmbientMeshCulling (+0x9C),
   // DetailCullDistance (+0xBC, R+0x244), ShadowDistance (+0xC4, R+0x24C)
   static EngineDetail g_engine[8] = {
      { "LODDistance", "LODDistanceOverride", 0xA0, 1.5f, 0.25f, 8.f, false, 1.5f, false, false, false, false },
      { "DetailCulling", "DetailCullingOverride", 0x8C, 1.f, 0.25f, 8.f, false, 1.f, false, false, false, false },
      { "ShaderLOD", "ShaderLODOverride", 0x90, 1.f, 0.25f, 8.f, false, 1.f, false, false, false, false },
      { "ShadowRange", "ShadowRangeOverride", 0x94, 1.f, 0.25f, 8.f, false, 1.f, false, false, false, false },
      { "AmbientMeshDensity", "AmbientMeshDensityOverride", 0x98, 1.f, 0.f, 1.f, true, 1.f, false, false, false, false },
      { "AmbientMeshCulling", "AmbientMeshCullingOverride", 0x9C, 8.f, 1.f, 32.f, false, 8.f, false, false, false, false },
      { "DetailCullDistance", "DetailCullDistanceOverride", 0xBC, 1.f, 0.25f, 8.f, false, 1.f, false, false, true, false },
      { "ShadowDistance", "ShadowDistanceOverride", 0xC4, 1.f, 0.25f, 8.f, false, 1.f, false, false, true, false },
   };

   // Clamps a config or UI value to the slider range of e.
   static float ClampEngine(const EngineDetail& e, float v)
   {
      if (v < e.lo) return e.lo;
      if (v > e.hi) return e.hi;
      return v;
   }

#if DEVELOPMENT
   // Dev value: 1 when the block is valid. Registered with Mcp in main.cpp.
   static uint32_t g_dev_valid = 0;
#endif
} // namespace BLDrawDistance

#if defined(_M_IX86)
namespace BLDrawDistance
{
   static constexpr uintptr_t kImageBase = 0x400000;
   static constexpr uintptr_t kVaRenderGlobal = 0x1005ACC;  // R = *(uintptr_t*)(base + VA - kImageBase); B = R + kOffBlock
   static constexpr uintptr_t kOffBlock = 0x188;
   static constexpr uintptr_t kOffDrawDist[4] = { 0xA4, 0xA8, 0xB4, 0xB8 };
   static constexpr uintptr_t kBlockSpan = 0xD0;  // covers B+0x00..B+0xCF (R+0x188..R+0x257: DetailCullDistance, ShadowDistance)
   static constexpr uintptr_t kVaTexGlobal = 0x1005D8C;  // G = base + VA - kImageBase; H = *G; T = *(H + kOffTexTable)
   static constexpr uintptr_t kOffTexTable = 0xD4;
   static constexpr uintptr_t kOffTexMips[2] = { 0x44, 0x45 };
   static constexpr uintptr_t kTexSpan = 0x46;     // covers T+0x00..T+0x45

   static uintptr_t g_base = 0;
   static uintptr_t g_cached_r = 0;   // last R value checked
   static bool g_cached_ok = false;   // result of the VirtualQuery check for g_cached_r
   static bool g_logged = false;

   static uintptr_t g_tex_t = 0;              // table T the captured mip defaults belong to
   static uint8_t g_tex_def[2] = { 0, 0 };    // original bytes at T+0x44 / T+0x45
   static bool g_tex_def_captured = false;
   static bool g_tex_written = false;         // 1s were written to g_tex_t and a restore is owed

   static void Log(reshade::log::level lv, const char* fmt, ...)
   {
      char buf[512];
      va_list args;
      va_start(args, fmt);
      vsnprintf(buf, sizeof(buf), fmt, args);
      va_end(args);
      reshade::log::message(lv, buf);
   }

   // True if [b, b + kBlockSpan) lies in one committed read/write region.
   static bool RangeIsReadWrite(uintptr_t b, uintptr_t span = kBlockSpan)
   {
      MEMORY_BASIC_INFORMATION mbi = {};
      if (VirtualQuery(reinterpret_cast<const void*>(b), &mbi, sizeof(mbi)) == 0)
         return false;
      if (mbi.State != MEM_COMMIT)
         return false;
      const DWORD prot = mbi.Protect & ~static_cast<DWORD>(PAGE_GUARD | PAGE_NOCACHE | PAGE_WRITECOMBINE);
      if (prot != PAGE_READWRITE && prot != PAGE_EXECUTE_READWRITE)
         return false;
      const uintptr_t region_begin = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
      const uintptr_t region_end = region_begin + mbi.RegionSize;
      return b >= region_begin && b + span <= region_end;
   }

   // Returns the validated block B, or 0.
   static uintptr_t ResolveBlock()
   {
      if (!g_base)
         g_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
      if (!g_base)
         return 0;
      const uintptr_t r = *reinterpret_cast<const uintptr_t*>(g_base + (kVaRenderGlobal - kImageBase));
      if (r == 0)
         return 0;
      if (r != g_cached_r)
      {
         g_cached_r = r;
         g_cached_ok = RangeIsReadWrite(r + kOffBlock);
      }
      return g_cached_ok ? r + kOffBlock : 0;
   }

   // Returns the validated texture table T (both pointer hops checked), or 0. h_out/g_out receive H and G for logging.
   static uintptr_t ResolveTexture(uintptr_t& h_out, uintptr_t& g_out)
   {
      h_out = 0;
      g_out = 0;
      if (!g_base)
         g_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
      if (!g_base)
         return 0;
      const uintptr_t g = g_base + (kVaTexGlobal - kImageBase);
      if (!RangeIsReadWrite(g, sizeof(uintptr_t)))
         return 0;
      const uintptr_t h = *reinterpret_cast<const uintptr_t*>(g);
      if (h == 0 || !RangeIsReadWrite(h + kOffTexTable, sizeof(uintptr_t)))
         return 0;
      const uintptr_t t = *reinterpret_cast<const uintptr_t*>(h + kOffTexTable);
      if (t == 0 || !RangeIsReadWrite(t, kTexSpan))
         return 0;
      g_out = g;
      h_out = h;
      return t;
   }

   // Force High-Res Texture Mips: writes 1 to T+0x44 and T+0x45 every frame while on; restores the captured bytes once when turned off.
   static void ApplyTextureMips(bool on)
   {
      uintptr_t h = 0;
      uintptr_t g = 0;
      const uintptr_t t = ResolveTexture(h, g);
      if (!t)
         return;  // invalid: nothing is written; a pending restore waits for T to be valid again

      if (t != g_tex_t)
      {
         // First valid table, or the game replaced it. Bytes of a replaced table are not restored.
         g_tex_t = t;
         g_tex_def_captured = false;
         g_tex_written = false;
         Log(reshade::log::level::info, "[BL DrawDistance] texture mips: global %p, object %p, table %p", reinterpret_cast<void*>(g), reinterpret_cast<void*>(h), reinterpret_cast<void*>(t));
      }
      if (!g_tex_def_captured)
      {
         for (int i = 0; i < 2; i++)
            g_tex_def[i] = *reinterpret_cast<const uint8_t*>(t + kOffTexMips[i]);
         g_tex_def_captured = true;
         Log(reshade::log::level::info, "[BL DrawDistance] texture mips default bytes %02X %02X", g_tex_def[0], g_tex_def[1]);
      }

      if (on)
      {
         for (uintptr_t off : kOffTexMips)
            *reinterpret_cast<uint8_t*>(t + off) = 1;
         g_tex_written = true;
      }
      else if (g_tex_written)
      {
         for (int i = 0; i < 2; i++)
            *reinterpret_cast<uint8_t*>(t + kOffTexMips[i]) = g_tex_def[i];
         g_tex_written = false;
      }
   }

   static void Apply(bool override_on, float draw_distance)
   {
      const uintptr_t b = ResolveBlock();
#if DEVELOPMENT
      g_dev_valid = b ? 1u : 0u;
#endif
      if (!b)
         return;

      if (!g_logged)
      {
         g_logged = true;
         Log(reshade::log::level::info, "[BL DrawDistance] block at %p", reinterpret_cast<void*>(b));
      }

      // Game defaults for entries that capture them: read before this frame writes anything.
      for (EngineDetail& e : g_engine)
      {
         if (e.capture_def && !e.def_captured)
         {
            e.def = *reinterpret_cast<const float*>(b + e.off);
            e.def_captured = true;
            Log(reshade::log::level::info, "[BL DrawDistance] %s address %p, default %.4f", e.key, reinterpret_cast<void*>(b + e.off), e.def);
         }
      }

      if (override_on)
      {
         bool differs = false;
         for (uintptr_t off : kOffDrawDist)
         {
            if (*reinterpret_cast<const float*>(b + off) != draw_distance)
               differs = true;
         }
         if (differs)
         {
            for (uintptr_t off : kOffDrawDist)
               *reinterpret_cast<float*>(b + off) = draw_distance;
         }
      }

      for (EngineDetail& e : g_engine)
      {
         if (e.override_on)
         {
            const float v = e.hard ? ClampEngine(e, e.value) : e.value;
            *reinterpret_cast<float*>(b + e.off) = v;
         }
         else if (e.was_on)
         {
            // Override just turned off: restore the game default once.
            *reinterpret_cast<float*>(b + e.off) = e.def;
         }
         e.was_on = e.override_on;
      }
   }
} // namespace BLDrawDistance

static void DrawDistance_OnPresent(bool override_on, float draw_distance, bool force_texture_mips)
{
   BLDrawDistance::Apply(override_on, draw_distance);
   BLDrawDistance::ApplyTextureMips(force_texture_mips);
}
#else
static void DrawDistance_OnPresent(bool, float, bool) {}
#endif
