#include "../MouseFix_Settings.h"
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <string.h>

static char saved_raw[16], saved_blur[16], saved_cutscenes[16], saved_bloom[16], saved_particles[16], saved_dof[16], saved_flares[16];
static unsigned writes;
static void* module = (void*)0x1234;
static bool get_value(void* owner, void* runtime, const char* section, const char* key, char* out, size_t* length)
{
   assert(owner == module && runtime == NULL && !strcmp(section, "MouseFix"));
   const char* value = !strcmp(key, "RawMouseInput") ? saved_raw : !strcmp(key, "DisableMotionBlur")  ? saved_blur
                                                                : !strcmp(key, "UnlockCutsceneFPS")   ? saved_cutscenes
                                                                : !strcmp(key, "DisableBloom")        ? saved_bloom
                                                                : !strcmp(key, "DisableDepthOfField") ? saved_dof
                                                                : !strcmp(key, "DisableFlares")       ? saved_flares
                                                                                                      : saved_particles;
   if (!*value) return false;
   assert(strlen(value) + 1 <= *length);
   strcpy(out, value);
   *length = strlen(value) + 1;
   return true;
}
static void set_value(void* owner, void* runtime, const char* section, const char* key, const char* value)
{
   assert(owner == module && runtime == NULL && !strcmp(section, "MouseFix"));
   char* out = !strcmp(key, "RawMouseInput") ? saved_raw : !strcmp(key, "DisableMotionBlur")  ? saved_blur
                                                        : !strcmp(key, "UnlockCutsceneFPS")   ? saved_cutscenes
                                                        : !strcmp(key, "DisableBloom")        ? saved_bloom
                                                        : !strcmp(key, "DisableDepthOfField") ? saved_dof
                                                        : !strcmp(key, "DisableFlares")       ? saved_flares
                                                                                              : saved_particles;
   strcpy(out, value);
   writes++;
}
static void reload(void)
{
   QBMF_LoadSettings(module, get_value, set_value);
}
int main(void)
{
   reload();
   assert(QBMF_RawMousePreference() == 1 && QBMF_BlurPreference() == 1 && writes == 0);
   QBMF_SaveRawMousePreference(0);
   reload();
   assert(QBMF_RawMousePreference() == 0 && QBMF_BlurPreference() == 1 && writes == 1);
   QBMF_SaveBlurPreference(0);
   reload();
   assert(QBMF_RawMousePreference() == 0 && QBMF_BlurPreference() == 0 && writes == 2);
   QBMF_SaveRawMousePreference(1);
   reload();
   assert(QBMF_RawMousePreference() == 1 && QBMF_BlurPreference() == 0 && writes == 3);
   QBMF_SaveBlurPreference(1);
   reload();
   assert(QBMF_RawMousePreference() == 1 && QBMF_BlurPreference() == 1 && writes == 4);
   saved_raw[0] = '\0';
   strcpy(saved_blur, "0");
   reload();
   assert(QBMF_RawMousePreference() == 1 && QBMF_BlurPreference() == 0 && writes == 4);
   strcpy(saved_raw, "invalid");
   reload();
   assert(QBMF_RawMousePreference() == 1 && QBMF_BlurPreference() == 0 && writes == 4);
   saved_raw[0] = saved_blur[0] = saved_cutscenes[0] = '\0';
   reload();
   assert(QBMF_RawMousePreference() == 1 && QBMF_BlurPreference() == 1 && QBMF_CutscenePreference() == 1);
   QBMF_SaveCutscenePreference(0);
   reload();
   assert(QBMF_CutscenePreference() == 0 && QBMF_RawMousePreference() == 1 && QBMF_BlurPreference() == 1);
   QBMF_SaveRawMousePreference(0);
   QBMF_SaveBlurPreference(0);
   reload();
   assert(QBMF_CutscenePreference() == 0 && QBMF_RawMousePreference() == 0 && QBMF_BlurPreference() == 0);
   QBMF_SaveCutscenePreference(1);
   reload();
   assert(QBMF_CutscenePreference() == 1 && QBMF_RawMousePreference() == 0 && QBMF_BlurPreference() == 0);
   saved_bloom[0] = '\0';
   reload();
   assert(QBMF_BloomPreference() == 1);
   QBMF_SaveBloomPreference(0);
   reload();
   assert(QBMF_BloomPreference() == 0 && QBMF_CutscenePreference() == 1 && QBMF_RawMousePreference() == 0 && QBMF_BlurPreference() == 0);
   QBMF_SaveBloomPreference(1);
   reload();
   assert(QBMF_BloomPreference() == 1);
   strcpy(saved_bloom, "invalid");
   reload();
   assert(QBMF_BloomPreference() == 1);
   saved_particles[0] = '\0';
   reload();
   assert(QBMF_ParticlePreference() == 1);
   QBMF_SaveParticlePreference(0);
   reload();
   assert(QBMF_ParticlePreference() == 0 && QBMF_BloomPreference() == 1);
   QBMF_SaveBloomPreference(0);
   reload();
   assert(QBMF_ParticlePreference() == 0 && QBMF_BloomPreference() == 0);
   QBMF_SaveParticlePreference(1);
   reload();
   assert(QBMF_ParticlePreference() == 1 && QBMF_BloomPreference() == 0);
   strcpy(saved_particles, "invalid");
   reload();
   assert(QBMF_ParticlePreference() == 1);
   assert(QBMF_DepthOfFieldPreference() == 1);
   QBMF_SaveDepthOfFieldPreference(0);
   reload();
   assert(QBMF_DepthOfFieldPreference() == 0 && QBMF_ParticlePreference() == 1 && QBMF_BloomPreference() == 0);
   QBMF_SaveBloomPreference(1);
   reload();
   assert(QBMF_DepthOfFieldPreference() == 0);
   QBMF_SaveDepthOfFieldPreference(1);
   reload();
   assert(QBMF_DepthOfFieldPreference() == 1 && QBMF_BloomPreference() == 1);
   strcpy(saved_dof, "invalid");
   reload();
   assert(QBMF_DepthOfFieldPreference() == 1);
   // Migrate either old combined choice without changing the visible effects.
   saved_flares[0] = '\0';
   QBMF_SaveBloomPreference(0);
   reload();
   assert(QBMF_FlaresPreference() == 0);
   QBMF_SaveBloomPreference(1);
   reload();
   assert(QBMF_FlaresPreference() == 1);
   QBMF_SaveFlaresPreference(QBMF_FlaresPreference()); // Same as feature startup.
   QBMF_SaveBloomPreference(0);
   reload();
   assert(QBMF_BloomPreference() == 0 && QBMF_FlaresPreference() == 1);
   QBMF_SaveFlaresPreference(0);
   QBMF_SaveBloomPreference(1);
   reload();
   assert(QBMF_BloomPreference() == 1 && QBMF_FlaresPreference() == 0 && QBMF_DepthOfFieldPreference() == 1);
   QBMF_SaveFlaresPreference(1);
   QBMF_SaveBloomPreference(0);
   reload();
   assert(QBMF_FlaresPreference() == 1 && QBMF_BloomPreference() == 0);
   puts("PASS: first-launch defaults, independent choices, off/on reloads, and missing/invalid values.");
   return 0;
}
