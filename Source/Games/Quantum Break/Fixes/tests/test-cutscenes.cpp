#include "../CutsceneFix.cpp"
#undef NDEBUG
#include <cassert>
#include <cstdio>

static void* seen_track;
static float seen_time;
static void* test_track_pointer;
static unsigned camera_order, light_count;
static unsigned end_callbacks, releases;
static bool native_end_immediate;
static DWORD end_thread;
static unsigned render_calls;
static bool rendered_begin, rendered_update, rendered_end;
static float rendered_sample;
static void event_render_mock(void* self, int mode, float time)
{
   ++render_calls;
   auto* event = static_cast<BYTE*>(self);
   const int start = *reinterpret_cast<int*>(event + 8), duration = *reinterpret_cast<int*>(event + 0xc);
   const float last = static_cast<float>(duration > 0 ? start + duration - 1 : start);
   const float stop = last + (patched && duration > 0 ? 1.0f : 0.0f);
   if (mode == 0 && time >= stop && event[0xe1])
   {
      rendered_end = true;
      event[0xe1] = 0;
   }
   if (mode == 1 && time >= start && time < stop && event[0xe0])
   {
      rendered_begin = true;
      event[0xe0] = 0;
   }
   if (mode == 2 && time >= start && time < stop)
   {
      rendered_update = true;
      rendered_sample = time < last ? time : last;
   }
}
static void event_end_mock(void* self)
{
   ++end_callbacks;
   end_thread = GetCurrentThreadId();
   static_cast<BYTE*>(self)[0xe1] = native_end_immediate ? 0 : 1;
}
static void finish_event_mock(void* self)
{
   assert(GetCurrentThreadId() == end_thread && static_cast<BYTE*>(self)[0xe1] == 1);
   ++releases;
   static_cast<BYTE*>(self)[0xe1] = 0;
}
static void track_mock(void* self, float value)
{
   seen_track = self;
   seen_time = value;
}
static void timeline_mock(void* self)
{
   assert(timeline_scope == self);
   track_render(test_track_pointer, 99.25f);
}
static void camera_mock(void* self, void* arg2, void* arg3, void* arg4)
{
   assert(self == reinterpret_cast<void*>(1) && arg2 == reinterpret_cast<void*>(2) && arg3 == reinterpret_cast<void*>(3) && arg4 == reinterpret_cast<void*>(4));
   camera_order++;
}
static void lights_mock(void* self)
{
   assert(self == game + 0xb000 && camera_order > light_count);
   light_count++;
}
static void cut_mock(void*)
{
}

int main()
{
   assert(QBCF_Initialize() == 0); // An unrelated executable must remain untouched.
   game = static_cast<BYTE*>(VirtualAlloc(nullptr, 0x1200000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
   assert(game);
   for (unsigned i = 0; i < 5; i++)
      std::memcpy(game + contexts[i], guards[i], guard_lengths[i]);
   initialized = true;
   for (int i = 0; i < 3; i++)
   {
      assert(QBMF_SetCutsceneEnabled(1) == 1 && QBMF_GetCutsceneEnabled() == 1 && QBMF_CutscenePreference() == 1);
      for (const auto& p : patches)
         assert(!std::memcmp(game + p.rva, p.replacement, p.length));
      assert(QBMF_SetCutsceneEnabled(0) == 1 && QBMF_GetCutsceneEnabled() == 0 && QBMF_CutscenePreference() == 0);
      for (const auto& p : patches)
         assert(!std::memcmp(game + p.rva, p.original, p.length));
   }
   game[patches[4].rva] = 0x90;
   assert(QBMF_SetCutsceneEnabled(1) == 0 && QBMF_GetCutsceneEnabled() == 0);
   for (unsigned i = 0; i < 4; i++)
      assert(!std::memcmp(game + patches[i].rva, patches[i].original, patches[i].length));
   game[patches[4].rva] = patches[4].original[0];

   float chosen;
   assert(choose_camera_time(99, 100, 99.25f, 100, chosen) && chosen == 100);
   assert(!choose_camera_time(100, 101, 100.25f, 100, chosen) && chosen == 100.25f);
   assert(!choose_camera_time(99, 100, 99.25f, 105, chosen) && chosen == 99.25f);
   assert(!choose_camera_time(100, 99, 99.25f, 100, chosen) && chosen == 99.25f);
   assert(!choose_camera_time(90, 100, 99.25f, 100, chosen) && chosen == 99.25f);
   assert(!choose_camera_time(NAN, 100, 99.25f, 100, chosen));

   auto *timeline = game + 0x6000, *track = game + 0x7000, *event = game + 0x8000, *settings = game + 0x9000;
   *reinterpret_cast<int*>(timeline + 0x1d8) = 1;
   timeline[0x774] = 1;
   *reinterpret_cast<float*>(timeline + 0x618) = 99;
   *reinterpret_cast<float*>(timeline + 0x614) = 100;
   *reinterpret_cast<BYTE***>(track) = reinterpret_cast<BYTE**>(game + 0xa000);
   *reinterpret_cast<uint64_t*>(track + 8) = 1;
   *reinterpret_cast<BYTE**>(game + 0xa000) = event;
   *reinterpret_cast<BYTE**>(event) = game + 0xaadbd8;
   *reinterpret_cast<int*>(event + 8) = 100;
   *reinterpret_cast<BYTE**>(event + 0x20) = settings;
   original_track = &track_mock;
   original_timeline = &timeline_mock;
   test_track_pointer = track;
   assert(QBMF_SetCutsceneEnabled(1));
   timeline_render(timeline);
   assert(seen_track == track && seen_time == 100 && timeline_scope == nullptr && calls.load() == 0);
   timeline[0x774] = 0;
   timeline_render(timeline);
   assert(seen_time == 99.25f);
   timeline[0x774] = 1;
   *reinterpret_cast<int*>(settings + 0x1d8) = 3;
   timeline_render(timeline);
   assert(seen_time == 99.25f);
   *reinterpret_cast<int*>(settings + 0x1d8) = 0;
   *reinterpret_cast<BYTE**>(event) = game + 0xa95e88;
   timeline_render(timeline);
   assert(seen_time == 99.25f);
   *reinterpret_cast<BYTE**>(event) = game + 0xaadbd8;
   assert(QBMF_SetCutsceneEnabled(0));
   timeline_render(timeline);
   assert(seen_time == 99.25f);

   original_event_end = &event_end_mock;
   finish_camera_event = &finish_event_mock;
   *reinterpret_cast<BYTE**>(event + 0x28) = track;
   *reinterpret_cast<BYTE**>(track + 0x40) = timeline;
   *reinterpret_cast<int*>(timeline + 0x624) = 190;
   *reinterpret_cast<int*>(event + 8) = 0;
   *reinterpret_cast<int*>(event + 0xc) = 190;
   // Reproduce the captured final 190-frame shot. Its native callback queues
   // an end, then the sequence stops without another render pass to consume it.
   assert(QBMF_SetCutsceneEnabled(1));
   camera_event_end(event);
   assert(end_callbacks == 1 && releases == 1 && !event[0xe1] && calls.load() == 0);
   // A native immediate release must never run twice.
   native_end_immediate = true;
   camera_event_end(event);
   assert(releases == 1);
   native_end_immediate = false;
   // Internal shots retain the deferred release used for clean camera cuts.
   *reinterpret_cast<int*>(event + 0xc) = 20;
   camera_event_end(event);
   assert(releases == 1 && event[0xe1] == 1);
   // Interrupted/stopped sequences must also flush that pending native end.
   *reinterpret_cast<int*>(timeline + 0x1d8) = 3;
   camera_event_end(event);
   assert(releases == 2 && !event[0xe1]);
   *reinterpret_cast<int*>(timeline + 0x1d8) = 0;
   camera_event_end(event);
   assert(releases == 3 && !event[0xe1]);
   // A paused internal shot, foreign event or disabled option stays native.
   *reinterpret_cast<int*>(timeline + 0x1d8) = 2;
   camera_event_end(event);
   assert(releases == 3 && event[0xe1]);
   *reinterpret_cast<int*>(timeline + 0x1d8) = 1;
   *reinterpret_cast<int*>(event + 0xc) = 190;
   *reinterpret_cast<BYTE**>(event) = game + 0xa95e88;
   camera_event_end(event);
   assert(releases == 3 && event[0xe1]);
   *reinterpret_cast<BYTE**>(event) = game + 0xaadbd8;
   assert(QBMF_SetCutsceneEnabled(0));
   camera_event_end(event);
   assert(releases == 3 && event[0xe1]);
   assert(QBMF_SetCutsceneEnabled(1));
   // Missing ownership and invalid ranges never trigger a guessed cleanup.
   *reinterpret_cast<BYTE**>(track + 0x40) = nullptr;
   camera_event_end(event);
   assert(releases == 3);
   *reinterpret_cast<BYTE**>(track + 0x40) = timeline;
   *reinterpret_cast<int*>(event + 0xc) = 0;
   camera_event_end(event);
   assert(releases == 3);
   *reinterpret_cast<int*>(event + 0xc) = -1;
   camera_event_end(event);
   assert(releases == 3);
   *reinterpret_cast<int*>(event + 0xc) = 190;
   *reinterpret_cast<int*>(event + 8) = -1;
   camera_event_end(event);
   assert(releases == 3);
   *reinterpret_cast<int*>(event + 8) = 190;
   camera_event_end(event);
   assert(releases == 3);
   *reinterpret_cast<int*>(event + 8) = 100;
   *reinterpret_cast<int*>(event + 0xc) = 0x7fffffff;
   camera_event_end(event);
   assert(releases == 4 && !event[0xe1]); // No signed addition overflow.

   // A short gameplay animation must keep its original final-frame handoff.
   timeline[0x774] = 0;
   *reinterpret_cast<int*>(event + 8) = 0;
   *reinterpret_cast<int*>(event + 0xc) = 190;
   camera_event_end(event);
   assert(releases == 4 && event[0xe1]);
   original_event_render = &event_render_mock;
   rendered_end = false;
   camera_event_render(event, 0, 189.0f);
   assert(rendered_end && !event[0xe1]);
   // Compare native and corrected event outcomes around every fractional edge,
   // for both ordinary animations and cinematic internal/final shots.
   const float samples[] = {9.0f, 10.0f, 10.25f, 18.75f, 19.0f, 19.25f, 19.999f, 20.0f, 20.25f};
   const int durations[] = {0, 1, 10};
   for (int active = 0; active <= 1; active++)
      for (int cinematic = 0; cinematic <= 1; cinematic++)
         for (int terminal = 0; terminal <= 1; terminal++)
            for (int duration : durations)
            {
               assert(QBMF_SetCutsceneEnabled(active));
               timeline[0x774] = static_cast<BYTE>(cinematic);
               *reinterpret_cast<int*>(timeline + 0x624) = terminal ? 20 : 200;
               *reinterpret_cast<int*>(event + 8) = 10;
               *reinterpret_cast<int*>(event + 0xc) = duration;
               const float last = static_cast<float>(duration > 0 ? 10 + duration - 1 : 10);
               const bool extended = active && cinematic && !(terminal && 10 + duration >= 20) && duration > 0;
               const float stop = last + (extended ? 1.0f : 0.0f);
               for (float time : samples)
                  for (int mode = 0; mode <= 2; mode++)
                  {
                     event[0xe0] = event[0xe1] = 1;
                     rendered_begin = rendered_update = rendered_end = false;
                     camera_event_render(event, mode, time);
                     assert(rendered_begin == (mode == 1 && time >= 10 && time < stop));
                     assert(rendered_update == (mode == 2 && time >= 10 && time < stop));
                     assert(rendered_end == (mode == 0 && time >= stop));
                     if (rendered_update) assert(rendered_sample == (time < last ? time : last));
                     assert(*reinterpret_cast<int*>(event + 0xc) == duration);
                  }
            }
   assert(render_calls > 0 && calls.load() == 0);

   original_camera = &camera_mock;
   original_cut = &cut_mock;
   refresh_lights = &lights_mock;
   *reinterpret_cast<BYTE**>(game + 0xf27410) = game + 0xb000;
   assert(QBMF_SetCutsceneEnabled(1));
   camera_cut(nullptr);
   camera_update(reinterpret_cast<void*>(1), reinterpret_cast<void*>(2), reinterpret_cast<void*>(3), reinterpret_cast<void*>(4));
   camera_update(reinterpret_cast<void*>(1), reinterpret_cast<void*>(2), reinterpret_cast<void*>(3), reinterpret_cast<void*>(4));
   assert(camera_order == 2 && light_count == 1);
   assert(QBMF_SetCutsceneEnabled(0));
   camera_cut(nullptr);
   camera_update(reinterpret_cast<void*>(1), reinterpret_cast<void*>(2), reinterpret_cast<void*>(3), reinterpret_cast<void*>(4));
   assert(light_count == 1);

   // The cutscene module must leave hooks owned by the mouse module installed.
   assert(MH_Initialize() == MH_OK);
   void* input_target = reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "RegisterRawInputDevices"));
   void* input_original;
   assert(MH_CreateHook(input_target, reinterpret_cast<void*>(&cut_mock), &input_original) == MH_OK);
   assert(QBMF_SetCutsceneEnabled(1));
   QBCF_Shutdown();
   QBCF_Shutdown();
   assert(!QBMF_GetCutsceneEnabled() && calls.load() == 0);
   for (const auto& p : patches)
      assert(!std::memcmp(game + p.rva, p.original, p.length));
   assert(MH_RemoveHook(input_target) == MH_OK);
   assert(MH_Uninitialize() == MH_OK);
   VirtualFree(game, 0, MEM_RELEASE);
   game = nullptr;
   puts("PASS: cutscene byte restoration and guards, native gameplay animation timing, fractional internal/final-shot boundaries, terminal/stopped cinematic release on the engine thread, native off state, light refresh, and independent mouse hooks.");
}
