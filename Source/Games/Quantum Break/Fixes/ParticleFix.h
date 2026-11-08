#pragma once
#include <windows.h>
int QBP_Initialize(HMODULE renderer);
int QBP_SetEnabled(int value);
int QBP_GetEnabled(void);
void QBP_Shutdown(void);
