#pragma once
#include <string.h>

// Byte positions match the original single-byte save-name format.
static int text_selection_key(char *text, unsigned *cursor, unsigned *length,
                              int *anchor, int ch, int vk, int shift, unsigned capacity) {
  unsigned n = (unsigned)strlen(text), pos = *cursor > n ? n : *cursor;
  if (*anchor > (int)n) *anchor = (int)n;
  unsigned lo = *anchor >= 0 && (unsigned)*anchor < pos ? (unsigned)*anchor : pos;
  unsigned hi = *anchor >= 0 && (unsigned)*anchor > pos ? (unsigned)*anchor : pos;
  if (!ch && (vk == 0x24 || vk == 0x23 || vk == 0x25 || vk == 0x27)) {
    unsigned next = pos;
    if (vk == 0x24) next = 0;
    else if (vk == 0x23) next = n;
    else if (!shift && lo != hi) next = vk == 0x25 ? lo : hi;
    else if (vk == 0x25 && pos) next--;
    else if (vk == 0x27 && pos < n) next++;
    if (shift) { if (*anchor < 0) *anchor = (int)pos; }
    else *anchor = -1;
    *cursor = next; *length = n; return 1;
  }
  int printable = ch >= 32 && ch <= 255;
  if (ch != 8 && !(ch == 0 && vk == 0x2e) && !printable) return 0;
  if (lo != hi) {
    memmove(text + lo, text + hi, n - hi + 1);
    n -= hi - lo; pos = lo;
  } else if (ch == 8 && pos) {
    memmove(text + pos - 1, text + pos, n - pos + 1); pos--; n--;
  } else if (!ch && vk == 0x2e && pos < n) {
    memmove(text + pos, text + pos + 1, n - pos); n--;
  }
  if (printable && n + 1 < capacity) {
    memmove(text + pos + 1, text + pos, n - pos + 1);
    text[pos++] = (char)ch; n++;
  }
  *anchor = -1; *cursor = pos; *length = n; return 1;
}
