// Original implementation: select the engine's existing full-resolution particle path.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <math.h>
#include <string.h>
#include "ParticleFix.h"
static volatile float *quarter_threshold, *half_threshold;
static float saved_quarter, saved_half;
static volatile LONG enabled;
static SRWLOCK particle_lock = SRWLOCK_INIT;
int QBP_Initialize(HMODULE renderer)
{
   if (QBP_GetEnabled()) return 1;
   quarter_threshold = half_threshold = NULL;
   if (!renderer) return 0;
   BYTE* base = (BYTE*)renderer;
   const BYTE guard[] = {0x0f, 0x28, 0xd0, 0x85, 0xd2, 0x74, 0x42, 0x83, 0xea, 0x01, 0x74, 0x6f, 0x83, 0xea, 0x01, 0x74, 0x21, 0xb8, 0x03, 0x00, 0x00, 0x00, 0x83, 0xfa, 0x01, 0x75, 0x65, 0xf3, 0x0f, 0x10, 0x05, 0xb5, 0x5b, 0x33, 0x00};
   const BYTE half_guard[] = {0xf3, 0x0f, 0x10, 0x05, 0x6e, 0x5c, 0x33, 0x00};
   if (memcmp(base + 0x1fd7d0, guard, sizeof guard) || memcmp(base + 0x1fd802, half_guard, sizeof half_guard)) return 0;
   float *quarter = (float*)(base + 0x5333a8), *half = (float*)(base + 0x533478);
   if (quarter[1] != -1.0f || quarter[2] != 50.0f || quarter[3] != 4.0f || half[1] != -1.0f || half[2] != 50.0f || half[3] != 20.0f) return 0;
   if (!isfinite(*quarter) || *quarter < -1.0f || *quarter > 50.0f || !isfinite(*half) || *half < -1.0f || *half > 50.0f) return 0;
   quarter_threshold = quarter;
   half_threshold = half;
   return 1;
}
int QBP_SetEnabled(int value)
{
   AcquireSRWLockExclusive(&particle_lock);
   if (!quarter_threshold || !half_threshold)
   {
      ReleaseSRWLockExclusive(&particle_lock);
      return 0;
   }
   if (value && !enabled)
   {
      saved_quarter = *quarter_threshold;
      saved_half = *half_threshold;
      if (!isfinite(saved_quarter) || saved_quarter < -1.0f || saved_quarter > 50.0f || !isfinite(saved_half) || saved_half < -1.0f || saved_half > 50.0f)
      {
         ReleaseSRWLockExclusive(&particle_lock);
         return 0;
      }
      *quarter_threshold = -1.0f;
      *half_threshold = -1.0f;
      InterlockedExchange(&enabled, 1);
   }
   else if (!value && enabled)
   {
      if (*quarter_threshold == -1.0f) *quarter_threshold = saved_quarter;
      if (*half_threshold == -1.0f) *half_threshold = saved_half;
      InterlockedExchange(&enabled, 0);
   }
   ReleaseSRWLockExclusive(&particle_lock);
   return 1;
}
int QBP_GetEnabled(void)
{
   return InterlockedCompareExchange(&enabled, 0, 0) != 0;
}
void QBP_Shutdown(void)
{
   QBP_SetEnabled(0);
}
