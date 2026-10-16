// Native pointer validation, kept outside the generated Ghidra type headers.
#include <windows.h>
#include <stdint.h>

int portrait_memory_readable(uintptr_t p, unsigned size) {
  if (p < 0x10000 || p + size < p) return 0;
  uintptr_t end = p + size;
  while (p < end) {
    MEMORY_BASIC_INFORMATION m;
    if (!VirtualQuery((const void *)p, &m, sizeof m) || m.State != MEM_COMMIT ||
        (m.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return 0;
    uintptr_t next = (uintptr_t)m.BaseAddress + m.RegionSize;
    if (next <= p) return 0;
    p = next;
  }
  return 1;
}
