#ifndef DIAGNOSTIC_TEST_TLHELP32_H
#define DIAGNOSTIC_TEST_TLHELP32_H
#include "windows.h"
#define TH32CS_SNAPMODULE 8
typedef struct {
  DWORD dwSize;
  unsigned char *modBaseAddr;
  DWORD modBaseSize;
  char szModule[MAX_PATH];
} MODULEENTRY32;
DWORD GetCurrentProcessId(void);
HANDLE CreateToolhelp32Snapshot(DWORD, DWORD);
int Module32First(HANDLE, MODULEENTRY32 *);
int Module32Next(HANDLE, MODULEENTRY32 *);
#endif
