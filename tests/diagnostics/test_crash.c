#include <assert.h>
#include <stdlib.h>
#include "port/crash_record.h"
static int registers_written, core_flushed, snapshot_fails, snapshots;
HANDLE CreateFileA(const char *p, DWORD access, DWORD share, void *security, DWORD creation, DWORD attrs, HANDLE template) {
  (void)share; (void)security; (void)attrs; (void)template;
  assert(access == FILE_APPEND_DATA && creation == OPEN_ALWAYS);
  registers_written = core_flushed = 0;
  FILE *f = fopen(p, "ab"); return f ? (HANDLE)f : INVALID_HANDLE_VALUE;
}
void GetSystemTime(SYSTEMTIME *t) { *t = (SYSTEMTIME){2026, 10, 18, 12, 0, 0}; }
int WriteFile(HANDLE f, const void *p, DWORD n, DWORD *written, void *overlap) {
  if (n >= 4 && !memcmp(p, "EIP ", 4)) registers_written = 1;
  (void)overlap; *written = fwrite(p, 1, n, (FILE *)f); return *written == n;
}
DWORD GetCurrentThreadId(void) { return 42; }
int GetModuleHandleExA(DWORD flags, LPCSTR addr, HMODULE *module) {
  assert(registers_written && core_flushed);
  assert(flags == 6); assert((uintptr_t)addr == 0x6ebf397b);
  *module = (HMODULE)(uintptr_t)0x6eb00000; return 1;
}
DWORD GetModuleFileNameA(HMODULE m, char *out, DWORD cap) {
  (void)m; const char *path = "C:\\Users\\Private\\game\\golf.exe";
  assert(strlen(path) < cap); strcpy(out, path); return strlen(out);
}
HANDLE GetCurrentProcess(void) { return NULL; }
int ReadProcessMemory(HANDLE p, const void *from, void *to, SIZE_T size, SIZE_T *bytes) {
  (void)p; (void)from; (void)to; (void)size; *bytes = 0; return 0;
}
int FlushFileBuffers(HANDLE f) { if (registers_written) core_flushed = 1; return fflush((FILE *)f) == 0; }
DWORD GetCurrentProcessId(void) { return 123; }
HANDLE CreateToolhelp32Snapshot(DWORD flags, DWORD pid) {
  assert(registers_written && core_flushed && flags==TH32CS_SNAPMODULE && pid==123);
  snapshots++;
  return snapshot_fails ? INVALID_HANDLE_VALUE : (HANDLE)(uintptr_t)1;
}
int Module32First(HANDLE snapshot, MODULEENTRY32 *module) {
  assert(snapshot==(HANDLE)(uintptr_t)1 && module->dwSize==sizeof *module);
  module->modBaseAddr=(unsigned char *)(uintptr_t)0x6eb00000;
  module->modBaseSize=0x100000;strcpy(module->szModule,"golf.exe");return 1;
}
int Module32Next(HANDLE snapshot, MODULEENTRY32 *module) {(void)module;assert(snapshot==(HANDLE)(uintptr_t)1);return 0;}
int CloseHandle(HANDLE f) { return f==(HANDLE)(uintptr_t)1 ? 1 : fclose((FILE *)f) == 0; }
int main(void) {
  EXCEPTION_RECORD e = {0xc0000005, 2, (void *)(uintptr_t)0x6ebf397b, {0, 8}};
  CONTEXT c = {0}; EXCEPTION_POINTERS ep = {&e, &c};
  assert(!host_write_crash_record(NULL, &ep));
  assert(host_write_crash_record("golf.log", &ep));
  assert(host_write_crash_record("golf.log", &ep));
  snapshot_fails=1;assert(host_write_crash_record("golf.log", &ep));assert(snapshots==3);
  FILE *f = fopen("golf.log.crash.txt", "rb"); assert(f);
  char out[4096]; size_t n = fread(out, 1, sizeof out - 1, f); out[n] = 0; fclose(f);
  char *first = strstr(out, "crash_record_version 2"); assert(first);
  assert(strstr(first + 1, "crash_record_version 2"));
  assert(strstr(out, "module golf.exe base 6eb00000 offset 000f397b"));
  assert(strstr(out, "module 6eb00000 size 00100000 golf.exe [fault]"));
  assert(strstr(out, "utc 2026-10-18T12:00:00Z"));
  assert(strstr(out, "EIP ")); assert(!strstr(out, "Private"));
  puts("crash writer tests passed"); return 0;
}
