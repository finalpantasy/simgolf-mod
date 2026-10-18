#ifndef DIAGNOSTIC_TEST_WINDOWS_H
#define DIAGNOSTIC_TEST_WINDOWS_H
#include <stdint.h>
#include <stddef.h>
typedef unsigned long DWORD;
#define MAX_PATH 260
#define INVALID_FILE_ATTRIBUTES ((DWORD)-1)
#define FILE_ATTRIBUTE_DIRECTORY 16
#define ERROR_FILE_NOT_FOUND 2
#define MOVEFILE_REPLACE_EXISTING 1
DWORD GetFileAttributesA(const char *);
DWORD GetLastError(void);
int MoveFileExA(const char *, const char *, DWORD);
typedef void *HANDLE;
typedef void *HMODULE;
typedef const char *LPCSTR;
typedef size_t SIZE_T;
typedef struct { unsigned short wYear, wMonth, wDay, wHour, wMinute, wSecond; } SYSTEMTIME;
typedef struct { DWORD ExceptionCode, NumberParameters; void *ExceptionAddress; uintptr_t ExceptionInformation[2]; } EXCEPTION_RECORD;
typedef struct { DWORD Eip, Esp, Ebp, Eax, Ebx, Ecx, Edx, Esi, Edi; } CONTEXT;
typedef struct { EXCEPTION_RECORD *ExceptionRecord; CONTEXT *ContextRecord; } EXCEPTION_POINTERS;
#define FILE_APPEND_DATA 4
#define FILE_SHARE_READ 1
#define OPEN_ALWAYS 4
#define FILE_ATTRIBUTE_NORMAL 128
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)
#define GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS 4
#define GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT 2
HANDLE CreateFileA(const char *, DWORD, DWORD, void *, DWORD, DWORD, HANDLE);
void GetSystemTime(SYSTEMTIME *);
int WriteFile(HANDLE, const void *, DWORD, DWORD *, void *);
DWORD GetCurrentThreadId(void);
int GetModuleHandleExA(DWORD, LPCSTR, HMODULE *);
DWORD GetModuleFileNameA(HMODULE, char *, DWORD);
HANDLE GetCurrentProcess(void);
int ReadProcessMemory(HANDLE, const void *, void *, SIZE_T, SIZE_T *);
int FlushFileBuffers(HANDLE);
int CloseHandle(HANDLE);
#endif
