#pragma once
#include <stdbool.h>
#include <stddef.h>

typedef bool (*QBMF_GetConfigFn)(void*, void*, const char*, const char*, char*, size_t*);
typedef void (*QBMF_SetConfigFn)(void*, void*, const char*, const char*, const char*);
void QBMF_LoadSettings(void* module, QBMF_GetConfigFn get, QBMF_SetConfigFn set);
int QBMF_RawMousePreference(void);
int QBMF_BlurPreference(void);
void QBMF_SaveRawMousePreference(int value);
void QBMF_SaveBlurPreference(int value);

int QBMF_CutscenePreference(void);
void QBMF_SaveCutscenePreference(int value);

int QBMF_BloomPreference(void);
void QBMF_SaveBloomPreference(int value);
int QBMF_FlaresPreference(void);
void QBMF_SaveFlaresPreference(int value);

int QBMF_ParticlePreference(void);
void QBMF_SaveParticlePreference(int value);
int QBMF_DepthOfFieldPreference(void);
void QBMF_SaveDepthOfFieldPreference(int value);
