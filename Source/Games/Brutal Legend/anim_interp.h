#pragma once

// Smooth character animation for Brutal Legend (Win32 only). Setting "Smooth Character Animation" (config key "AnimInterp", default on).
// Needs Engine Frame Pacing Fix: frame_pacing.h's paced blend sets the record pair (AnimInterp_SetPair), and the bone palette copy
// at 0x822340 is redirected to PaletteStub, which blends each skinned palette between the same model's palettes at the two ticks.
// Anything that does not match the palette layout, or has no earlier tick, passes through unchanged.
// Every site is signature-checked before anything is written; any mismatch patches nothing and logs once.
// Logs: "[BL AnimInterp]". The counters are shown in the DEVELOPMENT Mcp registrations (main.cpp).

#include <windows.h>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace BLAnimInterp
{
   // Dev counters. Defined in every config (main.cpp registers them with Mcp under DEVELOPMENT).
   static uint32_t g_dev_blended = 0;     // palettes returned blended
   static uint32_t g_dev_pass = 0;        // palettes returned unchanged while enabled + pair valid
   static uint32_t g_dev_new_entries = 0; // cache entries created
   static uint32_t g_dev_collision = 0;   // same model+count entry within radius claimed this tick, rejected by content
   static uint32_t g_dev_bone_snap = 0;   // bones snapped to the cur tick (quat angle > 60 deg)
   static int g_mode = 0;                 // dev: 0 = dual quaternion blend, 1 = linear lerp of all floats, 2 = cache logic, always return P, 3 = nlerp + separate translation lerp
} // namespace BLAnimInterp

#if defined(_M_IX86)
namespace BLAnimInterp
{
   static constexpr uintptr_t kImageBase = 0x400000;
   static constexpr uintptr_t kVaPalette = 0x822340;                        // eax = context, 4 stack args (P, count, remap, n), ret 0x10
   static constexpr uintptr_t kVaPaletteCalls[2] = { 0x81D511, 0x81D874 }; // E8 rel32 -> kVaPalette
   static constexpr uint32_t kMaxBones = 256;
   static constexpr size_t kCacheSize = 64;
   static constexpr float kMatchDist2 = 8.f * 8.f; // position match radius (units), squared
   static constexpr float kMaxOrthoErr = 0.02f;    // column orthogonality limit per bone; above it the bone is lerped
   static constexpr float kSnapDotMin = 0.8660254f; // cos(30 deg): |dot(qa,qb)| below it = quat angle above 60 deg, snap

   static const uint8_t kSigPalette[12] = { 0x55, 0x8B, 0xEC, 0x8B, 0x48, 0x04, 0x8B, 0x91, 0x40, 0x01, 0x00, 0x00 };

   static uintptr_t g_base = 0;
   static uintptr_t g_palette_orig = 0; // runtime address of kVaPalette (read by PaletteStub)
   static bool g_mismatch_logged = false;
   static std::atomic<bool> g_enabled{ true };

   // Pair state (render thread, written by frame_pacing.h's BlendWrap through the AnimInterp_* functions).
   static bool g_pair_valid = false;
   static int32_t g_pair_old = 0;
   static int32_t g_pair_new = 0;
   static float g_pair_alpha = 0.f; // weight of the new tick
   static uint32_t g_frame_id = 0;  // incremented per pair (memo key)

   struct Entry
   {
      bool occupied = false;
      uint32_t model = 0;
      uint32_t count = 0;
      int32_t cur_tick = 0;
      int32_t claim_tick = 0; // tick this entry was last matched on
      float cur_pos[3] = { 0.f, 0.f, 0.f };
      std::vector<float> cur_pal;
      bool prev_valid = false;
      int32_t prev_tick = 0;
      float prev_pos[3] = { 0.f, 0.f, 0.f };
      std::vector<float> prev_pal;
      std::vector<float> out_pal;
      uint32_t memo_frame = 0;
      uint32_t last_used_frame = 0;
   };
   static Entry g_cache[kCacheSize];

   static void Log(reshade::log::level lv, const char* fmt, ...)
   {
      char buf[512];
      va_list args;
      va_start(args, fmt);
      vsnprintf(buf, sizeof(buf), fmt, args);
      va_end(args);
      reshade::log::message(lv, buf);
   }

   static void Inc(uint32_t& v)
   {
      InterlockedIncrement(reinterpret_cast<volatile LONG*>(&v));
   }

   static uint8_t* Site(uintptr_t va)
   {
      return reinterpret_cast<uint8_t*>(g_base + (va - kImageBase));
   }

   static bool MatchBytes(uintptr_t va, const uint8_t* expect, size_t n, const char* name)
   {
      const uint8_t* p = Site(va);
      if (IsBadReadPtr(p, n) || memcmp(p, expect, n) != 0)
      {
         if (!g_mismatch_logged)
         {
            g_mismatch_logged = true;
            Log(reshade::log::level::error, "[BL AnimInterp] %s at VA 0x%06X: bytes mismatch or unreadable", name, static_cast<unsigned>(va));
         }
         return false;
      }
      return true;
   }

   static bool CallTargetIs(uintptr_t call_va, uintptr_t target_va)
   {
      const uint8_t* p = Site(call_va);
      if (IsBadReadPtr(p, 5) || p[0] != 0xE8)
         return false;
      int32_t rel = 0;
      memcpy(&rel, p + 1, 4);
      return reinterpret_cast<uintptr_t>(p + 5) + rel == reinterpret_cast<uintptr_t>(Site(target_va));
   }

   // E8 rel32 reaches the target from the site (signed 32-bit displacement).
   static bool RelFits(uint8_t* at, const void* to)
   {
      const int64_t d = static_cast<int64_t>(reinterpret_cast<uintptr_t>(to)) - (static_cast<int64_t>(reinterpret_cast<uintptr_t>(at)) + 5);
      return d >= INT32_MIN && d <= INT32_MAX;
   }

   static bool WriteBytes(uint8_t* dst, const uint8_t* src, size_t n)
   {
      DWORD old_protect = 0;
      if (!VirtualProtect(dst, n, PAGE_EXECUTE_READWRITE, &old_protect))
         return false;
      memcpy(dst, src, n);
      DWORD tmp = 0;
      VirtualProtect(dst, n, old_protect, &tmp);
      FlushInstructionCache(GetCurrentProcess(), dst, n);
      return true;
   }

   // E8 rel32 to `to` over `len` bytes (extra bytes become NOPs).
   static bool WriteCall(uint8_t* at, const void* to, size_t len)
   {
      uint8_t buf[8];
      buf[0] = 0xE8;
      const int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(to) - (reinterpret_cast<uintptr_t>(at) + 5));
      memcpy(buf + 1, &rel, 4);
      for (size_t i = 5; i < len; ++i) buf[i] = 0x90;
      return WriteBytes(at, buf, len);
   }

   // Splits one bone (3 float4 rows: R[r][0..2], t[r]) into a unit-column rotation R, per-column scales s and translation t.
   // False = degenerate scale or reflection (caller falls back to a plain lerp).
   static bool Decompose(const float* b, float R[3][3], float* s, float* t)
   {
      for (int c = 0; c < 3; ++c)
      {
         const float x = b[c], y = b[4 + c], z = b[8 + c];
         s[c] = std::sqrt(x * x + y * y + z * z);
         if (!(s[c] >= 1e-6f))
            return false;
      }
      for (int r = 0; r < 3; ++r)
      {
         for (int c = 0; c < 3; ++c)
            R[r][c] = b[r * 4 + c] / s[c];
         t[r] = b[r * 4 + 3];
      }
      const float det = R[0][0] * (R[1][1] * R[2][2] - R[1][2] * R[2][1])
                      - R[0][1] * (R[1][0] * R[2][2] - R[1][2] * R[2][0])
                      + R[0][2] * (R[1][0] * R[2][1] - R[1][1] * R[2][0]);
      return det >= 0.f; // NaN fails
   }

   // Shepperd's method. q = (w, x, y, z).
   static void QuatFromMat(const float R[3][3], float* q)
   {
      const float tr = R[0][0] + R[1][1] + R[2][2];
      float w, x, y, z;
      if (tr > 0.f)
      {
         const float s = std::sqrt(tr + 1.f) * 2.f;
         w = 0.25f * s;
         x = (R[2][1] - R[1][2]) / s;
         y = (R[0][2] - R[2][0]) / s;
         z = (R[1][0] - R[0][1]) / s;
      }
      else if (R[0][0] > R[1][1] && R[0][0] > R[2][2])
      {
         const float s = std::sqrt(std::fmax(1.f + R[0][0] - R[1][1] - R[2][2], 1e-12f)) * 2.f;
         w = (R[2][1] - R[1][2]) / s;
         x = 0.25f * s;
         y = (R[0][1] + R[1][0]) / s;
         z = (R[0][2] + R[2][0]) / s;
      }
      else if (R[1][1] > R[2][2])
      {
         const float s = std::sqrt(std::fmax(1.f + R[1][1] - R[0][0] - R[2][2], 1e-12f)) * 2.f;
         w = (R[0][2] - R[2][0]) / s;
         x = (R[0][1] + R[1][0]) / s;
         y = 0.25f * s;
         z = (R[1][2] + R[2][1]) / s;
      }
      else
      {
         const float s = std::sqrt(std::fmax(1.f + R[2][2] - R[0][0] - R[1][1], 1e-12f)) * 2.f;
         w = (R[1][0] - R[0][1]) / s;
         x = (R[0][2] + R[2][0]) / s;
         y = (R[1][2] + R[2][1]) / s;
         z = 0.25f * s;
      }
      q[0] = w;
      q[1] = x;
      q[2] = y;
      q[3] = z;
   }

   static void MatFromQuat(const float* q, float R[3][3])
   {
      const float w = q[0], x = q[1], y = q[2], z = q[3];
      R[0][0] = 1.f - 2.f * (y * y + z * z);
      R[0][1] = 2.f * (x * y - z * w);
      R[0][2] = 2.f * (x * z + y * w);
      R[1][0] = 2.f * (x * y + z * w);
      R[1][1] = 1.f - 2.f * (x * x + z * z);
      R[1][2] = 2.f * (y * z - x * w);
      R[2][0] = 2.f * (x * z - y * w);
      R[2][1] = 2.f * (y * z + x * w);
      R[2][2] = 1.f - 2.f * (x * x + y * y);
   }

   static void LerpBone(const float* a, const float* b, float w, float* out)
   {
      for (int k = 0; k < 12; ++k)
         out[k] = a[k] + (b[k] - a[k]) * w;
   }

   // Max column orthogonality error |dot(ci,cj)| / (si*sj) over column pairs. Degenerate column = huge.
   static float OrthoError(const float* b)
   {
      float s[3];
      for (int c = 0; c < 3; ++c)
      {
         const float x = b[c], y = b[4 + c], z = b[8 + c];
         s[c] = std::sqrt(x * x + y * y + z * z);
         if (!(s[c] >= 1e-6f))
            return 1e30f;
      }
      float err = 0.f;
      for (int i = 0; i < 3; ++i)
         for (int j = i + 1; j < 3; ++j)
         {
            const float d = b[i] * b[j] + b[4 + i] * b[4 + j] + b[8 + i] * b[8 + j];
            err = std::fmax(err, std::fabs(d) / (s[i] * s[j]));
         }
      return err;
   }

   // Shared per-bone guards. Lerp = non-orthogonal or degenerate bone (either side). Snap = quat angle > 60 deg.
   // Ok = qa, qb (qb sign-fixed toward qa), column scales sa/sb and translations ta/tb filled in.
   enum class BonePath { Ok, Lerp, Snap };
   static BonePath PrepBone(const float* a, const float* b, float* qa, float* qb, float* sa, float* sb, float* ta, float* tb)
   {
      if (!(OrthoError(a) <= kMaxOrthoErr) || !(OrthoError(b) <= kMaxOrthoErr))
         return BonePath::Lerp;
      float Ra[3][3], Rb[3][3];
      if (!Decompose(a, Ra, sa, ta) || !Decompose(b, Rb, sb, tb))
         return BonePath::Lerp;
      QuatFromMat(Ra, qa);
      QuatFromMat(Rb, qb);
      float dot = 0.f;
      for (int k = 0; k < 4; ++k)
         dot += qa[k] * qb[k];
      if (std::fabs(dot) < kSnapDotMin)
         return BonePath::Snap;
      if (dot < 0.f)
      {
         for (int k = 0; k < 4; ++k)
            qb[k] = -qb[k];
      }
      return BonePath::Ok;
   }

   // Hamilton product, quaternions as (w,x,y,z). o must not alias a or b.
   static void QuatMul(const float* a, const float* b, float* o)
   {
      o[0] = a[0] * b[0] - a[1] * b[1] - a[2] * b[2] - a[3] * b[3];
      o[1] = a[0] * b[1] + a[1] * b[0] + a[2] * b[3] - a[3] * b[2];
      o[2] = a[0] * b[2] - a[1] * b[3] + a[2] * b[0] + a[3] * b[1];
      o[3] = a[0] * b[3] + a[1] * b[2] - a[2] * b[1] + a[3] * b[0];
   }

   // Mode 3: nlerp rotation, column scales and translation lerped separately.
   static void InterpBoneNlerp(const float* a, const float* b, float w, float* out)
   {
      float qa[4], qb[4], sa[3], sb[3], ta[3], tb[3];
      switch (PrepBone(a, b, qa, qb, sa, sb, ta, tb))
      {
      case BonePath::Lerp:
         LerpBone(a, b, w, out);
         return;
      case BonePath::Snap:
         Inc(g_dev_bone_snap);
         memcpy(out, b, 12u * sizeof(float));
         return;
      default:
         break;
      }
      float q[4];
      for (int k = 0; k < 4; ++k)
         q[k] = qa[k] + (qb[k] - qa[k]) * w;
      const float len = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
      if (!(len > 1e-6f))
      {
         LerpBone(a, b, w, out);
         return;
      }
      for (int k = 0; k < 4; ++k)
         q[k] /= len;

      float R[3][3];
      MatFromQuat(q, R);
      float s[3];
      for (int c = 0; c < 3; ++c)
         s[c] = sa[c] + (sb[c] - sa[c]) * w;
      for (int r = 0; r < 3; ++r)
      {
         for (int c = 0; c < 3; ++c)
            out[r * 4 + c] = R[r][c] * s[c];
         out[r * 4 + 3] = ta[r] + (tb[r] - ta[r]) * w;
      }
   }

   // Mode 0 (default): dual quaternion linear blend. Rotation and translation are blended as one dual quaternion,
   // so the translation follows the same screw motion as the rotation instead of being lerped on its own.
   static void InterpBoneDQ(const float* a, const float* b, float w, float* out)
   {
      float qa[4], qb[4], sa[3], sb[3], ta[3], tb[3];
      switch (PrepBone(a, b, qa, qb, sa, sb, ta, tb))
      {
      case BonePath::Lerp:
         LerpBone(a, b, w, out);
         return;
      case BonePath::Snap:
         Inc(g_dev_bone_snap);
         memcpy(out, b, 12u * sizeof(float));
         return;
      default:
         break;
      }
      // Dual parts d = 0.5 * (t,0) * q, using the sign-fixed qb so (q, d) stays one transform.
      const float tqa[4] = { 0.f, ta[0], ta[1], ta[2] };
      const float tqb[4] = { 0.f, tb[0], tb[1], tb[2] };
      float da[4], db[4];
      QuatMul(tqa, qa, da);
      QuatMul(tqb, qb, db);
      for (int k = 0; k < 4; ++k)
      {
         da[k] *= 0.5f;
         db[k] *= 0.5f;
      }
      float qr[4], qd[4];
      for (int k = 0; k < 4; ++k)
      {
         qr[k] = qa[k] + (qb[k] - qa[k]) * w;
         qd[k] = da[k] + (db[k] - da[k]) * w;
      }
      const float len = std::sqrt(qr[0] * qr[0] + qr[1] * qr[1] + qr[2] * qr[2] + qr[3] * qr[3]);
      if (!(len > 1e-6f))
      {
         LerpBone(a, b, w, out);
         return;
      }
      for (int k = 0; k < 4; ++k)
      {
         qr[k] /= len;
         qd[k] /= len;
      }

      // t = 2 * qd * conj(qr), vector part.
      const float cqr[4] = { qr[0], -qr[1], -qr[2], -qr[3] };
      float tp[4];
      QuatMul(qd, cqr, tp);

      float R[3][3];
      MatFromQuat(qr, R);
      float s[3];
      for (int c = 0; c < 3; ++c)
         s[c] = sa[c] + (sb[c] - sa[c]) * w;
      for (int r = 0; r < 3; ++r)
      {
         for (int c = 0; c < 3; ++c)
            out[r * 4 + c] = R[r][c] * s[c];
         out[r * 4 + 3] = 2.f * tp[r + 1];
      }
   }

   static void CopyFloats(std::vector<float>& dst, const void* src, size_t n)
   {
      dst.resize(n);
      memcpy(dst.data(), src, n * sizeof(float));
   }

   // Valid palette: readable, count in [1, 256], header P-0x40 == P and P-0x3C == count. Outputs the record's model key and position.
   static bool ReadPalette(const void* P, uint32_t count, uint32_t* model, float* pos)
   {
      const uintptr_t p = reinterpret_cast<uintptr_t>(P);
      if (count < 1 || count > kMaxBones || p < 0x200)
         return false;
      if (IsBadReadPtr(P, static_cast<size_t>(count) * 48u))
         return false;
      const uintptr_t x0 = p - 0x160;
      if (IsBadReadPtr(reinterpret_cast<const void*>(x0), 0x128))
         return false;
      const uint8_t* rec = reinterpret_cast<const uint8_t*>(x0);
      uint32_t hdr_ptr = 0, hdr_count = 0;
      memcpy(&hdr_ptr, rec + 0x120, 4);   // P-0x40
      memcpy(&hdr_count, rec + 0x124, 4); // P-0x3C
      if (hdr_ptr != static_cast<uint32_t>(p) || hdr_count != count)
         return false;
      memcpy(model, rec + 0x70, 4);
      memcpy(pos, rec + 0x30, 12);
      return true;
   }

   // Nearest same-model, same-count entry within the match radius that is not yet claimed this tick. nullptr = none.
   static Entry* FindEntry(uint32_t model, uint32_t count, const float* pos, int32_t tick)
   {
      Entry* best = nullptr;
      float best_d2 = kMatchDist2;
      for (Entry& e : g_cache)
      {
         if (!e.occupied || e.model != model || e.count != count || e.claim_tick == tick)
            continue;
         const float dx = e.cur_pos[0] - pos[0];
         const float dy = e.cur_pos[1] - pos[1];
         const float dz = e.cur_pos[2] - pos[2];
         const float d2 = dx * dx + dy * dy + dz * dz;
         if (d2 <= best_d2)
         {
            best = &e;
            best_d2 = d2;
         }
      }
      return best;
   }

   // Empty slot, else the least recently used one.
   static Entry* AllocEntry()
   {
      Entry* victim = nullptr;
      for (Entry& e : g_cache)
      {
         if (!e.occupied)
            return &e;
         if (!victim || e.last_used_frame < victim->last_used_frame)
            victim = &e;
      }
      return victim;
   }

   // Blend result for entry e, whose cur palette is P at the current tick. Memoized per pair.
   static const void* BlendFor(Entry* e, const void* P, uint32_t count)
   {
      if (g_mode == 2)
      {
         Inc(g_dev_pass);
         return P;
      }
      if (!e->prev_valid || e->prev_tick != g_pair_old)
      {
         Inc(g_dev_pass);
         return P;
      }
      const float dx = e->cur_pos[0] - e->prev_pos[0];
      const float dy = e->cur_pos[1] - e->prev_pos[1];
      const float dz = e->cur_pos[2] - e->prev_pos[2];
      if (dx * dx + dy * dy + dz * dz > kMatchDist2)
      {
         Inc(g_dev_pass);
         return P;
      }

      if (e->memo_frame != g_frame_id)
      {
         e->out_pal.resize(static_cast<size_t>(count) * 12u);
         for (uint32_t b = 0; b < count; ++b)
         {
            if (g_mode == 1)
               LerpBone(&e->prev_pal[b * 12u], &e->cur_pal[b * 12u], g_pair_alpha, &e->out_pal[b * 12u]);
            else if (g_mode == 3)
               InterpBoneNlerp(&e->prev_pal[b * 12u], &e->cur_pal[b * 12u], g_pair_alpha, &e->out_pal[b * 12u]);
            else
               InterpBoneDQ(&e->prev_pal[b * 12u], &e->cur_pal[b * 12u], g_pair_alpha, &e->out_pal[b * 12u]);
         }
         e->memo_frame = g_frame_id;
      }
      Inc(g_dev_blended);
      return e->out_pal.data();
   }

   // cdecl (P, count) -> the palette to copy: P itself, or the blended palette held in the cache.
   static const void* __cdecl BlendPalette(const void* P, uint32_t count)
   {
      if (!g_enabled.load(std::memory_order_relaxed) || !g_pair_valid)
         return P;

      uint32_t model = 0;
      float pos[3] = { 0.f, 0.f, 0.f };
      if (!ReadPalette(P, count, &model, pos))
      {
         Inc(g_dev_pass);
         return P;
      }

      const int32_t tick = g_pair_new;

      // Step 2: exact content match among entries already claimed this tick (same model+count, within radius).
      Entry* e = nullptr;
      bool near_rejected = false;
      for (Entry& c : g_cache)
      {
         if (!c.occupied || c.model != model || c.count != count || c.claim_tick != tick)
            continue;
         const float dx = c.cur_pos[0] - pos[0];
         const float dy = c.cur_pos[1] - pos[1];
         const float dz = c.cur_pos[2] - pos[2];
         if (dx * dx + dy * dy + dz * dz > kMatchDist2)
            continue;
         if (memcmp(c.cur_pal.data(), P, static_cast<size_t>(count) * 48u) == 0)
         {
            e = &c;
            break;
         }
         near_rejected = true;
      }
      if (e)
      {
         e->last_used_frame = g_frame_id;
         return BlendFor(e, P, count);
      }
      if (near_rejected)
         Inc(g_dev_collision);

      // Step 1: nearest entry not yet claimed this tick.
      e = FindEntry(model, count, pos, tick);
      if (!e)
      {
         e = AllocEntry();
         e->occupied = true;
         e->model = model;
         e->count = count;
         e->cur_tick = tick;
         e->claim_tick = tick;
         memcpy(e->cur_pos, pos, sizeof(e->cur_pos));
         CopyFloats(e->cur_pal, P, static_cast<size_t>(count) * 12u);
         e->prev_valid = false;
         e->memo_frame = 0;
         e->last_used_frame = g_frame_id;
         Inc(g_dev_new_entries);
         Inc(g_dev_pass);
         return P;
      }

      e->last_used_frame = g_frame_id;
      if (tick > e->cur_tick)
      {
         e->prev_valid = true;
         e->prev_tick = e->cur_tick;
         memcpy(e->prev_pos, e->cur_pos, sizeof(e->prev_pos));
         e->prev_pal = e->cur_pal;
      }
      else
      {
         e->prev_valid = false;
      }
      e->cur_tick = tick;
      e->claim_tick = tick;
      memcpy(e->cur_pos, pos, sizeof(e->cur_pos));
      CopyFloats(e->cur_pal, P, static_cast<size_t>(count) * 12u);
      e->memo_frame = 0;
      return BlendFor(e, P, count);
   }

   // Redirected from the two palette call sites. Replaces the P argument with BlendPalette's result and runs the original
   // with eax (context) preserved. The stack layout on entry: [esp] = return, [esp+4] = P, [esp+8] = count.
   static __declspec(naked) void PaletteStub()
   {
      __asm
      {
         push eax
         push ecx
         push edx
         push dword ptr [esp + 14h]   // count
         push dword ptr [esp + 14h]   // P
         call BlendPalette
         add esp, 8
         mov [esp + 10h], eax         // P argument slot
         pop edx
         pop ecx
         pop eax
         jmp dword ptr [g_palette_orig]
      }
   }

#if DEVELOPMENT
   // Startup self-test (pure math). Logs once: a == b returns a, and a 50 deg spin about Y around pivot c keeps c fixed at w = 0.5.
   static void SelfTestDQ()
   {
      const float c[3] = { 1.5f, 0.4f, 0.f };
      const float ang = 50.f * 3.14159265358979f / 180.f;
      const float cs = std::cos(ang), sn = std::sin(ang);
      const float R[3][3] = { { cs, 0.f, sn }, { 0.f, 1.f, 0.f }, { -sn, 0.f, cs } };
      float spin[12];
      for (int r = 0; r < 3; ++r)
      {
         float rc = 0.f;
         for (int k = 0; k < 3; ++k)
         {
            spin[r * 4 + k] = R[r][k];
            rc += R[r][k] * c[k];
         }
         spin[r * 4 + 3] = c[r] - rc;
      }
      const float identity[12] = { 1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f };

      float out[12];
      InterpBoneDQ(spin, spin, 0.5f, out);
      float err_same = 0.f;
      for (int k = 0; k < 12; ++k)
         err_same = std::fmax(err_same, std::fabs(out[k] - spin[k]));

      InterpBoneDQ(identity, spin, 0.5f, out);
      float err_pivot = 0.f;
      for (int r = 0; r < 3; ++r)
      {
         float v = out[r * 4 + 3] - c[r];
         for (int k = 0; k < 3; ++k)
            v += out[r * 4 + k] * c[k];
         err_pivot = std::fmax(err_pivot, std::fabs(v));
      }

      const bool ok = err_same < 1e-4f && err_pivot < 1e-4f;
      Log(ok ? reshade::log::level::info : reshade::log::level::error,
          "[BL AnimInterp] DQ self-test %s (a==b err %.2e, pivot err %.2e)", ok ? "passed" : "FAILED", static_cast<double>(err_same), static_cast<double>(err_pivot));
   }
#endif

   static void Install()
   {
#if DEVELOPMENT
      SelfTestDQ();
#endif
      g_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
      if (!g_base)
         return;

      // Verify everything first. Any mismatch patches nothing.
      bool ok = MatchBytes(kVaPalette, kSigPalette, sizeof(kSigPalette), "Palette copy");
      for (uintptr_t c : kVaPaletteCalls)
      {
         if (!CallTargetIs(c, kVaPalette))
         {
            if (!g_mismatch_logged) { g_mismatch_logged = true; Log(reshade::log::level::error, "[BL AnimInterp] palette call at VA 0x%06X: target mismatch", static_cast<unsigned>(c)); }
            ok = false;
         }
         else if (!RelFits(Site(c), reinterpret_cast<void*>(&PaletteStub)))
         {
            Log(reshade::log::level::error, "[BL AnimInterp] stub out of rel32 range of palette call 0x%06X", static_cast<unsigned>(c));
            ok = false;
         }
      }
      if (!ok)
      {
         Log(reshade::log::level::error, "[BL AnimInterp] signature mismatch, smooth character animation inactive (nothing patched)");
         return;
      }

      g_palette_orig = g_base + (kVaPalette - kImageBase);
      for (uintptr_t c : kVaPaletteCalls)
         WriteCall(Site(c), reinterpret_cast<void*>(&PaletteStub), 5);
      Log(reshade::log::level::info, "[BL AnimInterp] bone palette redirects installed (2 call sites)");
   }
} // namespace BLAnimInterp

// Called from frame_pacing.h's BlendWrap (paced path): the record ticks and the blend weight of the new tick.
static void AnimInterp_SetPair(int32_t tick_old, int32_t tick_new, float alpha_new)
{
   BLAnimInterp::g_pair_valid = true;
   BLAnimInterp::g_pair_old = tick_old;
   BLAnimInterp::g_pair_new = tick_new;
   BLAnimInterp::g_pair_alpha = alpha_new;
   ++BLAnimInterp::g_frame_id;
}

static void AnimInterp_ClearPair()
{
   BLAnimInterp::g_pair_valid = false;
}

// Called from OnPresent (main.cpp) with the "Smooth Character Animation" setting.
static void AnimInterp_SetEnabled(bool enabled)
{
   BLAnimInterp::g_enabled.store(enabled, std::memory_order_relaxed);
}

static bool AnimInterp_Enabled()
{
   return BLAnimInterp::g_enabled.load(std::memory_order_relaxed);
}

// Called from OnInit (main.cpp), after InstallFramePacing.
static void InstallAnimInterp()
{
   BLAnimInterp::Install();
}
#else
static void AnimInterp_SetPair(int32_t, int32_t, float) {}
static void AnimInterp_ClearPair() {}
static void AnimInterp_SetEnabled(bool) {}
static bool AnimInterp_Enabled() { return false; }
static void InstallAnimInterp() {}
#endif
