// Save records (simgolf mod, task-11): which mods a save was made with.
//
// The .sve format is the original game's and stays byte-for-byte unchanged:
// the original golf.exe and every older build load our saves, and we load
// theirs. The record is a sidecar next to the save, "<name>.sve.mods.toml",
// tied to the save by its size and CRC-32 so a save that something else
// rewrote later is never matched with a stale record.

#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "mods/mods_core.h"
#include "tomlc99/toml.h"

uint32_t mods_crc32(uint32_t crc, const void *data, size_t len) {
  static uint32_t table[256];
  if (!table[1]) {
    for (uint32_t i = 0; i < 256; i++) {
      uint32_t c = i;
      for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
      table[i] = c;
    }
  }
  const unsigned char *p = (const unsigned char *)data;
  crc = ~crc;
  while (len--) crc = table[(crc ^ *p++) & 0xff] ^ (crc >> 8);
  return ~crc;
}

int mods_file_crc(const char *path, uint32_t *size, uint32_t *crc) {
  FILE *f = fopen(path, "rb");
  if (!f) return 0;
  unsigned char buf[65536];
  size_t n;
  uint32_t c = 0, total = 0;
  int ok = 1;
  while ((n = fread(buf, 1, sizeof buf, f)) > 0) {
    if (n > UINT32_MAX - total) { ok = 0; break; }
    c = mods_crc32(c, buf, n);
    total += (uint32_t)n;
  }
  if (ferror(f)) ok = 0;
  if (fclose(f)) ok = 0;
  if (!ok) return 0;
  *size = total;
  *crc = c;
  return 1;
}

int saverec_format(const ModSaveRec *rec, char *out, size_t outlen) {
  size_t k = (size_t)snprintf(out, outlen,
                              "# Mods this SimGolf save was made with (docs/modding/CORE.md). The .sve next\n"
                              "# to it is the original format, unchanged; this file only records the mods.\n"
                              "format = \"simgolf-save-mods\"\n"
                              "format_version = %d\n"
                              "platform = \"%s\"\n"
                              "profile = \"%s\"\n"
                              "save_size = %u\n"
                              "save_crc32 = \"%08x\"\n",
                              rec->format, rec->platform, rec->profile, (unsigned)rec->save_size,
                              (unsigned)rec->save_crc);
  for (int i = 0; i < rec->n && k < outlen; i++)
    k += (size_t)snprintf(out + k, outlen - k, "\n[[mods]]\nid = \"%s\"\nversion = \"%s\"\n", rec->id[i],
                          rec->version[i]);
  return k < outlen;
}

int saverec_parse(ModSaveRec *rec, char *text) {
  memset(rec, 0, sizeof *rec);
  char err[200];
  toml_table_t *t = toml_parse(text, err, sizeof err);
  if (!t) return 0;
  toml_datum_t fmt = toml_string_in(t, "format");
  int ok = fmt.ok && !strcmp(fmt.u.s, "simgolf-save-mods");
  if (fmt.ok) free(fmt.u.s);
  toml_datum_t ver = toml_int_in(t, "format_version");
  // Newer formats keep these fields; anything we do not know is ignored.
  rec->format = ver.ok && ver.u.i >= 1 && ver.u.i <= INT_MAX ? (int)ver.u.i : 0;
  if (rec->format < 1) ok = 0;
  toml_datum_t d = toml_string_in(t, "platform");
  if (d.ok) { snprintf(rec->platform, sizeof rec->platform, "%s", d.u.s); free(d.u.s); }
  d = toml_string_in(t, "profile");
  if (d.ok) { snprintf(rec->profile, sizeof rec->profile, "%s", d.u.s); free(d.u.s); }
  d = toml_int_in(t, "save_size");
  if (d.ok && d.u.i >= 0 && (uint64_t)d.u.i <= UINT32_MAX) rec->save_size = (uint32_t)d.u.i;
  else ok = 0;
  d = toml_string_in(t, "save_crc32");
  if (d.ok) {
    size_t n = strlen(d.u.s);
    if (n >= 1 && n <= 8 && strspn(d.u.s, "0123456789abcdefABCDEF") == n)
      rec->save_crc = (uint32_t)strtoul(d.u.s, NULL, 16);
    else ok = 0;
    free(d.u.s);
  }
  else ok = 0;
  toml_array_t *mods = toml_array_in(t, "mods");
  for (int i = 0; mods && i < toml_array_nelem(mods) && rec->n < 64; i++) {
    toml_table_t *m = toml_table_at(mods, i);
    if (!m) continue;
    toml_datum_t id = toml_string_in(m, "id"), v = toml_string_in(m, "version");
    if (id.ok) {
      snprintf(rec->id[rec->n], sizeof rec->id[0], "%s", id.u.s);
      snprintf(rec->version[rec->n], sizeof rec->version[0], "%s", v.ok ? v.u.s : "?");
      rec->n++;
    }
    if (id.ok) free(id.u.s);
    if (v.ok) free(v.u.s);
  }
  toml_free(t);
  return ok;
}

int saverec_check(const ModSaveRec *rec, const ModSet *s, const ModResolution *r, ModDiags *out) {
  int warns = 0;
  for (int i = 0; i < rec->n; i++) {
    int k = modset_find(s, rec->id[i]);
    if (k < 0 || !resolution_has(r, k)) {
      mdiag_add(out, MD_WARN, "save_mod_missing", rec->id[i],
                "this save was made with the mod %s %s, which is %s now; things it added may be missing or behave "
                "differently",
                rec->id[i], rec->version[i], k < 0 ? "not installed" : "not loaded");
      warns++;
    } else if (strcmp(s->m[k].version, rec->version[i])) {
      mdiag_add(out, MD_WARN, "save_mod_version", rec->id[i], "this save was made with %s %s, now %s %s is loaded",
                rec->id[i], rec->version[i], rec->id[i], s->m[k].version);
      warns++;
    }
  }
  for (int i = 0; i < r->norder; i++) {
    const char *id = s->m[r->order[i]].id;
    int found = 0;
    for (int j = 0; j < rec->n; j++) found |= !strcmp(rec->id[j], id);
    if (!found)
      mdiag_add(out, MD_NOTE, "save_mod_added", id, "%s is loaded now but was not when this save was made", id);
  }
  return warns;
}
