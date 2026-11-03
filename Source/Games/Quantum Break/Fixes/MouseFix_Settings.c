#include "MouseFix_Settings.h"

static void* owner;
static QBMF_GetConfigFn get_config;
static QBMF_SetConfigFn set_config;
static int raw_mouse = 1, disable_blur = 1, unlock_cutscenes = 1, disable_bloom = 1, disable_flares = 1, high_resolution_particles = 1, disable_depth_of_field = 1;
static const char section[] = "MouseFix";

static int load_bool_default(const char* key, int fallback)
{
   char value[16] = {0};
   size_t length = sizeof value;
   if (get_config && get_config(owner, NULL, section, key, value, &length))
   {
      if (value[0] == '0' && value[1] == '\0') return 0;
      if (value[0] == '1' && value[1] == '\0') return 1;
   }
   return fallback;
}
static int load_bool(const char* key)
{
   return load_bool_default(key, 1);
}
void QBMF_LoadSettings(void* module, QBMF_GetConfigFn get, QBMF_SetConfigFn set)
{
   owner = module;
   get_config = get;
   set_config = set;
   raw_mouse = load_bool("RawMouseInput");
   disable_blur = load_bool("DisableMotionBlur");
   unlock_cutscenes = load_bool("UnlockCutsceneFPS");
   disable_bloom = load_bool("DisableBloom");
   high_resolution_particles = load_bool("HighResolutionParticles");
   disable_depth_of_field = load_bool("DisableDepthOfField");
   // Preserve the old combined bloom/flares choice when upgrading. Startup
   // saves this initial flare choice so later bloom changes remain independent.
   disable_flares = load_bool_default("DisableFlares", disable_bloom);
}
int QBMF_RawMousePreference(void)
{
   return raw_mouse;
}
int QBMF_BlurPreference(void)
{
   return disable_blur;
}
void QBMF_SaveRawMousePreference(int value)
{
   raw_mouse = value != 0;
   if (set_config) set_config(owner, NULL, section, "RawMouseInput", raw_mouse ? "1" : "0");
}
void QBMF_SaveBlurPreference(int value)
{
   disable_blur = value != 0;
   if (set_config) set_config(owner, NULL, section, "DisableMotionBlur", disable_blur ? "1" : "0");
}

int QBMF_CutscenePreference(void)
{
   return unlock_cutscenes;
}
void QBMF_SaveCutscenePreference(int value)
{
   unlock_cutscenes = value != 0;
   if (set_config) set_config(owner, NULL, section, "UnlockCutsceneFPS", unlock_cutscenes ? "1" : "0");
}

int QBMF_BloomPreference(void)
{
   return disable_bloom;
}
void QBMF_SaveBloomPreference(int value)
{
   disable_bloom = value != 0;
   if (set_config) set_config(owner, NULL, section, "DisableBloom", disable_bloom ? "1" : "0");
}
int QBMF_FlaresPreference(void)
{
   return disable_flares;
}
void QBMF_SaveFlaresPreference(int value)
{
   disable_flares = value != 0;
   if (set_config) set_config(owner, NULL, section, "DisableFlares", disable_flares ? "1" : "0");
}

int QBMF_ParticlePreference(void)
{
   return high_resolution_particles;
}
void QBMF_SaveParticlePreference(int value)
{
   high_resolution_particles = value != 0;
   if (set_config) set_config(owner, NULL, section, "HighResolutionParticles", high_resolution_particles ? "1" : "0");
}
int QBMF_DepthOfFieldPreference(void)
{
   return disable_depth_of_field;
}
void QBMF_SaveDepthOfFieldPreference(int value)
{
   disable_depth_of_field = value != 0;
   if (set_config) set_config(owner, NULL, section, "DisableDepthOfField", disable_depth_of_field ? "1" : "0");
}
