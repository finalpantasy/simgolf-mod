#ifndef SIMGOLF_CRASH_RECORD_H
#define SIMGOLF_CRASH_RECORD_H
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <tlhelp32.h>

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
  for (DWORD i = 2; i < e->NumberParameters && i < EXCEPTION_MAXIMUM_PARAMETERS; i++) {
    n = snprintf(line, sizeof line, "parameter %lu %08lx\r\n", i, (DWORD)e->ExceptionInformation[i]);
    ok = WriteFile(f, line, n, &written, NULL) && written == (DWORD)n && ok;
  }
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
  // Driver/Windows faults are outside the fixed game image. Record the
  // loaded image ranges so an ASLR address and stack can identify the DLL.
  // Use only basenames: installation and user directory names are irrelevant.
  HANDLE snapshot = INVALID_HANDLE_VALUE;
  DWORD snapshot_error = 0;
  // Windows can return ERROR_BAD_LENGTH while another thread loads a DLL.
  // Retry that documented transient failure, with a bounded crash-time cost.
  for (int attempt = 0; attempt < 8; attempt++) {
    snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
    if (snapshot != INVALID_HANDLE_VALUE) break;
    snapshot_error = GetLastError();
    if (snapshot_error != ERROR_BAD_LENGTH) break;
  }
  if (snapshot != INVALID_HANDLE_VALUE) {
    MODULEENTRY32 module = {0};
    module.dwSize = sizeof module;
    if (Module32First(snapshot, &module)) do {
      uintptr_t base = (uintptr_t)module.modBaseAddr;
      uintptr_t fault = (uintptr_t)e->ExceptionAddress;
      n = snprintf(line, sizeof line, "module %08lx size %08lx %s%s\r\n",
        (DWORD)base, module.modBaseSize, module.szModule,
        fault >= base && fault - base < module.modBaseSize ? " [fault]" : "");
      ok = WriteFile(f, line, n, &written, NULL) && written == (DWORD)n && ok;
    } while (Module32Next(snapshot, &module));
    CloseHandle(snapshot);
  } else {
    n = snprintf(line, sizeof line, "module snapshot failed error %lu\r\n", snapshot_error);
    ok = WriteFile(f, line, n, &written, NULL) && written == (DWORD)n && ok;
    // At least identify the fault image even when snapshot allocation fails.
    MEMORY_BASIC_INFORMATION memory;
    if (VirtualQuery(e->ExceptionAddress, &memory, sizeof memory) && memory.Type == MEM_IMAGE) {
      char module_path[MAX_PATH];
      DWORD length = GetModuleFileNameA((HMODULE)memory.AllocationBase, module_path, sizeof module_path);
      if (length && length < sizeof module_path) {
        const char *name = strrchr(module_path, '\\');
        n = snprintf(line, sizeof line, "fault module %08lx %s\r\n",
          (DWORD)(uintptr_t)memory.AllocationBase, name ? name + 1 : module_path);
        ok = WriteFile(f, line, n, &written, NULL) && written == (DWORD)n && ok;
      }
    }
  }
  FlushFileBuffers(f); CloseHandle(f); return ok;
}
#endif
