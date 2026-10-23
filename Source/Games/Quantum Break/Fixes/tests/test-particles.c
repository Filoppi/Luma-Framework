#include "../ParticleFix.c"
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
int main(void)
{
   BYTE* image = (BYTE*)VirtualAlloc(NULL, 0x534000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
   assert(image);
   const BYTE guard[] = {0x0f, 0x28, 0xd0, 0x85, 0xd2, 0x74, 0x42, 0x83, 0xea, 0x01, 0x74, 0x6f, 0x83, 0xea, 0x01, 0x74, 0x21, 0xb8, 0x03, 0, 0, 0, 0x83, 0xfa, 0x01, 0x75, 0x65, 0xf3, 0x0f, 0x10, 0x05, 0xb5, 0x5b, 0x33, 0};
   const BYTE half_guard[] = {0xf3, 0x0f, 0x10, 0x05, 0x6e, 0x5c, 0x33, 0};
   assert(!QBP_Initialize((HMODULE)image));
   memcpy(image + 0x1fd7d0, guard, sizeof guard);
   memcpy(image + 0x1fd802, half_guard, sizeof half_guard);
   float *q = (float*)(image + 0x5333a8), *h = (float*)(image + 0x533478);
   q[0] = 4;
   q[1] = -1;
   q[2] = 50;
   q[3] = 4;
   h[0] = 20;
   h[1] = -1;
   h[2] = 50;
   h[3] = 20;
   assert(QBP_Initialize((HMODULE)image) && !QBP_GetEnabled());
   assert(QBP_SetEnabled(1) && QBP_GetEnabled() && q[0] == -1 && h[0] == -1);
   assert(QBP_SetEnabled(1));
   assert(QBP_SetEnabled(0) && q[0] == 4 && h[0] == 20 && !QBP_GetEnabled());
   q[0] = 7;
   h[0] = 30;
   assert(QBP_SetEnabled(1));
   QBP_Shutdown();
   assert(q[0] == 7 && h[0] == 30);
   assert(QBP_SetEnabled(1));
   q[0] = 3;
   QBP_Shutdown();
   assert(q[0] == 3 && h[0] == 30);
   h[0] = NAN;
   assert(!QBP_SetEnabled(1) && q[0] == 3 && !QBP_GetEnabled());
   h[0] = 20;
   q[1] = 0;
   assert(!QBP_Initialize((HMODULE)image) && !QBP_SetEnabled(1));
   q[1] = -1;
   image[0x1fd7d0] ^= 1;
   assert(!QBP_Initialize((HMODULE)image));
   image[0x1fd7d0] ^= 1;
   assert(!QBP_Initialize(NULL) && !QBP_SetEnabled(1));
   VirtualFree(image, 0, MEM_RELEASE);
   puts("PASS: particle code/layout guards, full-resolution thresholds, idempotent toggles, restoration, recapture, and external-setting preservation.");
   return 0;
}
