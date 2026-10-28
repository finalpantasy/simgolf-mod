"""The authoritative picker filename must never alias a truncated name."""
from pathlib import Path
import json
import unittest
import test_publication_allocations as allocations

ROOT = Path(__file__).resolve().parents[2]


class FileListIdentity(unittest.TestCase):
    run_c = allocations.PublicationAllocations.run_c
    def test_notice_draws_before_original_surface_changes(self):
        cfg = json.loads((ROOT / 'port/tools/translate/golf.frontend.json').read_text())
        for name, next_line in (('FUN_0043b610', 'local_9f4 = iVar3 + -0x10;'),
                                ('FUN_0043a8c0', 'local_604 = iVar2 + -0x10;')):
            anchor = 'FUN_004762d0(PTR_DAT_004c1570,0x51b360,0,0,0);\n    ' + next_line
            patches = dict(cfg['patches'][name])
            self.assertEqual(patches.get(anchor), 'fe_list_notice();\n    ' + anchor)
            self.assertIn([anchor, 1], cfg['required_patch_counts'].get(name, []))

    def test_long_names_cannot_alias_a_shorter_file(self):
        source = (ROOT / 'port/src/port/golf/bounds_fixes.c').read_text(encoding='utf-8')
        listing = source[source.index('#define LIST '):source.index('// Sprite lookup')]
        listing = listing.replace('#define LIST 0x80b130u', '#define LIST ((uintptr_t)file_slots)')
        listing = listing.replace('#define TEXT 0x51a068u', '#define TEXT ((uintptr_t)text_buffer)')
        for addr, value in [('0x4c8a34', '"shadow"'), ('0x4c8a2c', '"Shadow"'), ('0x4c8a28', '"\\n"')]:
            listing = listing.replace('(const char *)(uintptr_t)' + addr, value)
        # The renderer is outside this filename-identity test.
        preamble = r'''
#define _CRT_SECURE_NO_WARNINGS
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef const char *LPCSTR;
typedef void *HANDLE;
typedef struct { char cFileName[260]; } _WIN32_FIND_DATAA;
static char file_slots[10000], text_buffer[4096];
static const char *names[4];
static int at, total, enabled = 1;
static int bounds_on(void) { return enabled; }
static int FUN_00404b70(char *s, int x, int y, unsigned col) {
  (void)s; (void)x; (void)y; (void)col; return 0;
}
static HANDLE FindFirstFileA(LPCSTR pattern, _WIN32_FIND_DATAA *fd) {
  (void)pattern; at = 0;
  if (!total) return (HANDLE)0xffffffff;
  strcpy(fd->cFileName, names[0]); return (HANDLE)1;
}
static int FindNextFileA(HANDLE h, _WIN32_FIND_DATAA *fd) {
  (void)h; if (++at >= total) return 0;
  strcpy(fd->cFileName, names[at]); return 1;
}
static void FindClose(HANDLE h) { (void)h; }
'''
        self.run_c('file_identity', preamble + listing + r'''
int main(int argc, char **argv) {
  (void)argc; (void)argv;
  char short_name[100], a[140], b[140], hundred[101];
  memset(short_name, 'A', 95); strcpy(short_name + 95, ".sve");
  strcpy(a, short_name); strcat(a, "one.sve");
  strcpy(b, short_name); strcat(b, "two.sve");
  memset(hundred, 'B', 96); strcpy(hundred + 96, ".sve");
  names[0] = a; names[1] = b; names[2] = short_name; names[3] = hundred; total = 4;
  int count = FUN_0043d2a0("Saved Games/*.sve", "", "");
  assert(count == 1);
  assert(file_list_omitted() == 3);
  assert(!strcmp(file_slots, short_name));
  assert(file_slots[100] == 0);
  assert(strstr(text_buffer, "one.sve") == NULL);
  assert(strstr(text_buffer, "two.sve") == NULL);
  names[0] = "Normal.sve"; total = 1; text_buffer[0] = 0;
  assert(FUN_0043d2a0("Saved Games/*.sve", "", "") == 1);
  assert(!strcmp(file_slots, "Normal.sve"));
  assert(file_list_omitted() == 0);
  total = 0;
  assert(FUN_0043d2a0("Saved Games/*.sve", "", "") == 0);
  assert(file_list_omitted() == 0);
  enabled = 0; total = 1;
  assert(FUN_0043d2a0("Saved Games/*.sve", "", "") == 1);
  assert(!strcmp(file_slots, "Normal.sve"));
  return 0;
}
''', 0)
