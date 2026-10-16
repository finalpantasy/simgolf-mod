#ifndef SIMGOLF_CRASH_RECORD_H
#define SIMGOLF_CRASH_RECORD_H
#include <windows.h>
#include <stdint.h>
#include <stdio.h>

// Independent of stdout/stderr: a GUI launch can lack usable CRT streams.
// Keep exception breadcrumbs even if stream redirection failed or was closed.
static int host_write_crash_record(const char *log_path, EXCEPTION_POINTERS *ep) {
  if (!log_path || !ep || !ep->ExceptionRecord || !ep->ContextRecord) return 0;
  char path[MAX_PATH + 32], line[1024];
  int n = snprintf(path, sizeof path, "%s.crash.txt", log_path);
  if (n < 0 || n >= (int)sizeof path) return 0;
  HANDLE f = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (f == INVALID_HANDLE_VALUE) return 0;
  EXCEPTION_RECORD *e = ep->ExceptionRecord;
  CONTEXT *c = ep->ContextRecord;
  n = snprintf(line, sizeof line,
    "exception %08lx at %08lx thread %lu\r\nparameters %lu %08lx %08lx\r\n",
    e->ExceptionCode, (DWORD)(uintptr_t)e->ExceptionAddress, GetCurrentThreadId(),
    e->NumberParameters, e->NumberParameters > 0 ? (DWORD)e->ExceptionInformation[0] : 0,
    e->NumberParameters > 1 ? (DWORD)e->ExceptionInformation[1] : 0);
  DWORD written; int ok = WriteFile(f, line, n, &written, NULL) && written == (DWORD)n;
#if defined(_M_IX86) || defined(__i386__)
  n = snprintf(line, sizeof line,
    "EIP %08lx ESP %08lx EBP %08lx EAX %08lx EBX %08lx ECX %08lx EDX %08lx ESI %08lx EDI %08lx\r\n",
    c->Eip, c->Esp, c->Ebp, c->Eax, c->Ebx, c->Ecx, c->Edx, c->Esi, c->Edi);
  ok = WriteFile(f, line, n, &written, NULL) && written == (DWORD)n && ok;
  DWORD words[128]; SIZE_T bytes = 0;
  // ReadProcessMemory returns failure for an inaccessible stack instead of
  // causing a second exception while reporting the original one.
  if (ReadProcessMemory(GetCurrentProcess(), (const void *)(uintptr_t)c->Esp, words, sizeof words, &bytes)) {
    for (unsigned i = 0; i < bytes / sizeof(DWORD); i += 8) {
      n = snprintf(line, sizeof line, "stack %08lx:", c->Esp + i * 4);
      for (unsigned j = i; j < i + 8 && j < bytes / sizeof(DWORD); ++j)
        n += snprintf(line + n, sizeof line - n, " %08lx", words[j]);
      n += snprintf(line + n, sizeof line - n, "\r\n");
      ok = WriteFile(f, line, n, &written, NULL) && written == (DWORD)n && ok;
    }
  }
#endif
  FlushFileBuffers(f); CloseHandle(f); return ok;
}
#endif
