#pragma clang diagnostic error "-Wimplicit-function-declaration"
// sgsetup.exe: the native installer's worker (simgolf mod task-76). The
// player's setup (tools/release/setup/setup.ps1) calls it with their own
// copy of the game; nothing is downloaded or shipped. Same steps as the
// browser installer (web/pages/install-worker.js):
//   sgsetup install <disc image> <golf.exe> <install folder>
// reads the disc (.iso cooked 2048-byte sectors, or a raw .bin/.img/.mdf
// with 2352-byte sectors), copies the InstallShield cabinets to a scratch
// folder, unpacks the "Program Files (ENGLISH)" group with unshield into
// <install folder>/game, copies the loose root files the game also reads,
// and writes <install folder>/image/golf.bin and jgld.bin (golf.exe and
// jgld.dll mapped as the loader maps them; jgld gets the NOP patches listed
// in jgld-nops.json next to this exe).
// Output: "progress <done> <total> <file>" lines on stdout, "ok ..." at the
// end; "error: <plain sentence>" on stderr and exit 2 for a wrong file, 1
// for anything else.
#include <windows.h>
#include <bcrypt.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libunshield.h"

#define GROUP "Program Files (ENGLISH)"
#define NOCD_SHA1 "6f05a3ae3b5050ed82dc0010c03842a2e0ab166b"
static void fail(int code, const char *msg);

typedef struct { const char *path; unsigned long long size; const char *sha256; } GameFile;
#ifndef SGSETUP_MANIFEST_HEADER
#define SGSETUP_MANIFEST_HEADER "game-files.h"
#endif
#include SGSETUP_MANIFEST_HEADER

static int file_hash(const char *path, LPCWSTR algorithm, char *hex) {
  FILE *f = fopen(path, "rb");
  if (!f) return 0;
  BCRYPT_ALG_HANDLE alg = NULL;
  BCRYPT_HASH_HANDLE hash = NULL;
  DWORD size = 0, used = 0;
  unsigned char digest[32], block[65536];
  int ok = BCryptOpenAlgorithmProvider(&alg, algorithm, NULL, 0) >= 0;
  if (ok) ok = BCryptGetProperty(alg, BCRYPT_HASH_LENGTH, (PUCHAR)&size, sizeof size, &used, 0) >= 0 && size <= sizeof digest;
  if (ok) ok = BCryptCreateHash(alg, &hash, NULL, 0, NULL, 0, 0) >= 0;
  size_t n;
  while (ok && (n = fread(block, 1, sizeof block, f)) != 0)
    ok = BCryptHashData(hash, block, (ULONG)n, 0) >= 0;
  ok = ok && !ferror(f);
  if (ok) ok = BCryptFinishHash(hash, digest, size, 0) >= 0;
  if (hash) BCryptDestroyHash(hash);
  if (alg) BCryptCloseAlgorithmProvider(alg, 0);
  fclose(f);
  if (ok) {
    for (DWORD i = 0; i < size; i++) sprintf(hex + i * 2, "%02x", digest[i]);
    hex[size * 2] = 0;
  }
  return ok;
}

static int joined_path(char *out, size_t size, const char *root, const char *rel) {
  int n = snprintf(out, size, "%s\\%s", root, rel);
  return n >= 0 && (size_t)n < size;
}

// Do not follow junctions or symlinks in a folder supplied to the installer.
static int regular_source(const char *path) {
  char p[MAX_PATH * 3];
  if (strlen(path) >= sizeof p) return 0;
  strcpy(p, path);
  for (char *c = p; ; c++) {
    if (*c == '/' || *c == '\\' || !*c) {
      char save = *c;
      *c = 0;
      if (strlen(p) > 2) {
        DWORD a = GetFileAttributesA(p);
        if (a == INVALID_FILE_ATTRIBUTES || (a & FILE_ATTRIBUTE_REPARSE_POINT)) return 0;
        if (!save && (a & FILE_ATTRIBUTE_DIRECTORY)) return 0;
      }
      *c = save;
      if (!save) break;
    }
  }
  return 1;
}

static void safe_destination(const char *path) {
  char p[MAX_PATH * 3];
  DWORD n = GetFullPathNameA(path, sizeof p, p, NULL);
  if (!n || n >= sizeof p) fail(2, "the install folder path is too long");
  for (char *c = p; ; c++) {
    if (*c == '\\' || *c == '/' || !*c) {
      char save = *c;
      *c = 0;
      DWORD a = GetFileAttributesA(p);
      if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_REPARSE_POINT))
        fail(2, "choose an install folder without junctions or symbolic links");
      *c = save;
      if (!save) break;
    }
  }
}

static int validate_folder(const char *source) {
  DWORD attr = GetFileAttributesA(source);
  if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY))
    fail(2, "choose the existing SimGolf folder, not its golf.exe");
  int bad = 0;
  for (size_t i = 0; i < sizeof game_files / sizeof game_files[0]; i++) {
    const GameFile *g = &game_files[i];
    char p[MAX_PATH * 3], hex[65];
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (!joined_path(p, sizeof p, source, g->path) || !regular_source(p) ||
        !GetFileAttributesExA(p, GetFileExInfoStandard, &a)) {
      fprintf(stderr, "error: missing or unreadable game file: %s\n", g->path);
      bad++;
    } else if ((((unsigned long long)a.nFileSizeHigh << 32) | a.nFileSizeLow) != g->size ||
               !file_hash(p, BCRYPT_SHA256_ALGORITHM, hex) || strcmp(hex, g->sha256)) {
      fprintf(stderr, "error: changed game file: %s\n", g->path);
      bad++;
    }
  }
  if (bad) fail(2, "these game files do not match the original English disc; use a clean matching folder or the disc image instead. Nothing was copied");
  printf("ok %u game files checked\n", (unsigned)(sizeof game_files / sizeof game_files[0]));
  return 0;
}

static void separate_folders(const char *source, const char *out) {
  char s[MAX_PATH * 3], d[MAX_PATH * 3];
  DWORD sn = GetFullPathNameA(source, sizeof s, s, NULL), dn = GetFullPathNameA(out, sizeof d, d, NULL);
  if (!sn || !dn || sn >= sizeof s || dn >= sizeof d) fail(2, "the source or install folder path is too long");
  while (sn && (s[sn-1] == '\\' || s[sn-1] == '/')) s[--sn] = 0;
  while (dn && (d[dn-1] == '\\' || d[dn-1] == '/')) d[--dn] = 0;
  if ((!_strnicmp(s, d, sn) && (!d[sn] || d[sn] == '\\' || d[sn] == '/')) ||
      (!_strnicmp(d, s, dn) && (!s[dn] || s[dn] == '\\' || s[dn] == '/')))
    fail(2, "choose a separate install folder outside the existing SimGolf folder");
}

static void fail(int code, const char *msg) {
  fprintf(stderr, "error: %s\n", msg);
  exit(code);
}

static void mkdirs(const char *path) {   // every folder of path (a folder path)
  char b[MAX_PATH * 2];
  snprintf(b, sizeof b, "%s", path);
  for (char *p = b; *p; p++)
    if ((*p == '/' || *p == '\\') && p > b && p[-1] != ':') {
      char c = *p;
      *p = 0;
      CreateDirectoryA(b, NULL);
      *p = c;
    }
  CreateDirectoryA(b, NULL);
}

static void mkparent(const char *file) {
  char b[MAX_PATH * 2];
  snprintf(b, sizeof b, "%s", file);
  char *s = strrchr(b, '/'), *t = strrchr(b, '\\');
  if (t > s) s = t;
  if (s) {
    *s = 0;
    mkdirs(b);
  }
}

static uint8_t *read_all(const char *path, size_t *n) {
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  _fseeki64(f, 0, SEEK_END);
  long long sz = _ftelli64(f);
  _fseeki64(f, 0, SEEK_SET);
  uint8_t *b = sz > 0 && sz < (1 << 30) ? malloc((size_t)sz) : NULL;
  if (b && fread(b, 1, (size_t)sz, f) != (size_t)sz) {
    free(b);
    b = NULL;
  }
  fclose(f);
  *n = b ? (size_t)sz : 0;
  return b;
}

static int write_all(const char *path, const void *p, size_t n) {
  safe_destination(path);
  mkparent(path);
  FILE *f = fopen(path, "wb");
  if (!f) return 0;
  int ok = fwrite(p, 1, n, f) == n;
  return fclose(f) == 0 && ok;
}

// ---------------------------------------------------------------- disc image (ISO 9660, Joliet names)

typedef struct {
  FILE *f;
  int raw, data_off, joliet;
  uint8_t root[34];
} Disc;

typedef struct {
  char name[256];
  int dir;
  uint32_t lba, size;
} Entry;

static int disc_sector(Disc *d, uint32_t lba, uint8_t *out) {
  long long at = (long long)lba * (d->raw ? 2352 : 2048) + d->data_off;
  if (_fseeki64(d->f, at, SEEK_SET)) return 0;
  return fread(out, 1, 2048, d->f) == 2048;
}

static int disc_open(Disc *d, const char *path) {
  memset(d, 0, sizeof *d);
  d->f = fopen(path, "rb");
  if (!d->f) return 0;
  uint8_t h[16];
  if (fread(h, 1, 16, d->f) != 16) return 0;
  int sync = h[0] == 0 && h[11] == 0;
  for (int i = 1; i < 11; i++) sync &= h[i] == 0xff;
  d->raw = sync;
  d->data_off = sync ? (h[15] == 2 ? 24 : 16) : 0;
  int have = 0;
  uint8_t s[2048];
  for (uint32_t lba = 16; lba < 64 && disc_sector(d, lba, s); lba++) {
    if (memcmp(s + 1, "CD001", 5) || s[0] == 255) break;
    if (s[0] == 1 && !have) memcpy(d->root, s + 156, 34), have = 1;
    if (s[0] == 2 && s[88] == 0x25 && s[89] == 0x2f && (s[90] == 0x40 || s[90] == 0x43 || s[90] == 0x45))
      memcpy(d->root, s + 156, 34), have = 1, d->joliet = 1;
  }
  return have;
}

static uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

// The root folder's entries; *n is set to the count.
static Entry *disc_root(Disc *d, int *n) {
  uint32_t lba = le32(d->root + 2), size = le32(d->root + 10);
  if (size > (16u << 20)) return NULL;   // task-114p: a root folder is a few sectors; a damaged disc can claim 4 GB
  uint32_t ns = (size + 2047) / 2048;
  uint8_t *data = malloc((size_t)ns * 2048 + 1);
  if (!data) return NULL;
  for (uint32_t i = 0; i < ns; i++)
    if (!disc_sector(d, lba + i, data + (size_t)i * 2048)) {
      free(data);
      return NULL;
    }
  Entry *out = NULL;
  int cap = 0;
  *n = 0;
  for (uint32_t p = 0; p < size;) {
    uint8_t len = data[p];
    if (!len) {
      p = (p / 2048 + 1) * 2048;
      continue;
    }
    const uint8_t *e = data + p;
    // a record must fit its sector run, hold the 33 byte header and carry a name that fits inside it
    if (len < 34 || p + len > ns * 2048u || 33u + e[32] > len) break;
    int nl = e[32];
    if (!(nl == 1 && e[33] <= 1)) {
      if (*n == cap) {
        Entry *grown = realloc(out, (size_t)(cap = cap ? cap * 2 : 64) * sizeof *out);
        if (!grown) { free(out); free(data); *n = 0; return NULL; }
        out = grown;
      }
      Entry *x = &out[(*n)++];
      memset(x, 0, sizeof *x);
      int k = 0;
      if (d->joliet)
        for (int i = 0; i + 1 < nl && k < 254; i += 2) {
          unsigned c = e[33 + i] << 8 | e[34 + i];
          x->name[k++] = c < 128 ? (char)c : '_';
        }
      else
        for (int i = 0; i < nl && k < 254; i++) x->name[k++] = (char)e[33 + i];
      x->name[k] = 0;
      char *semi = strstr(x->name, ";1");
      if (semi && !semi[2]) *semi = 0;
      size_t l = strlen(x->name);
      if (l && x->name[l - 1] == '.') x->name[l - 1] = 0;
      x->dir = (e[25] & 2) != 0;
      x->lba = le32(e + 2);
      x->size = le32(e + 10);
    }
    p += len;
  }
  free(data);
  return out;
}

static int disc_copy(Disc *d, const Entry *e, const char *out) {
  safe_destination(out);
  mkparent(out);
  FILE *f = fopen(out, "wb");
  if (!f) return 0;
  uint8_t s[2048];
  uint32_t left = e->size;
  int ok = 1;
  for (uint32_t lba = e->lba; left && ok; lba++) {
    uint32_t k = left < 2048 ? left : 2048;
    ok = disc_sector(d, lba, s) && fwrite(s, 1, k, f) == k;
    left -= k;
  }
  return fclose(f) == 0 && ok;
}

// ---------------------------------------------------------------- PE images

static uint8_t *map_pe(const uint8_t *b, size_t n, size_t *out_n) {
  if (n < 0x40 || b[0] != 'M' || b[1] != 'Z') return NULL;
  uint32_t pe = le32(b + 0x3c);
  if ((size_t)pe + 24 + 64 > n || memcmp(b + pe, "PE\0\0", 4)) return NULL;   // size_t: pe near 4 GB must not wrap
  unsigned nsec = b[pe + 6] | b[pe + 7] << 8, opt_size = b[pe + 20] | b[pe + 21] << 8;
  uint32_t opt = pe + 24, size = le32(b + opt + 56), hdr = le32(b + opt + 60);
  if (!size || size > (64u << 20)) return NULL;
  if ((size_t)opt + opt_size > n || hdr > size) return NULL;
  uint8_t *img = calloc(1, size);
  if (!img) return NULL;
  memcpy(img, b, hdr < size && hdr < n ? hdr : (size < n ? size : n));
  for (unsigned i = 0; i < nsec; i++) {
    const uint8_t *s = b + opt + opt_size + i * 40;
    if ((size_t)(s - b) + 40 > n) break;
    uint32_t vsz = le32(s + 8), va = le32(s + 12), rsz = le32(s + 16), rp = le32(s + 20);
    uint32_t k = rsz;
    if (vsz && vsz < k) k = vsz;
    if (va >= size) continue;
    if (k > size - va) k = size - va;
    if (rp >= n) continue;
    if (k > n - rp) k = (uint32_t)(n - rp);
    memcpy(img + va, b + rp, k);
  }
  *out_n = size;
  return img;
}

// jgld-nops.json: [[offset, length], ...], applied as 0x90 bytes.
static int apply_nops(uint8_t *img, size_t n, const char *json) {
  size_t jn = 0;
  char *t = (char *)read_all(json, &jn);
  if (!t) return -1;
  t = realloc(t, jn + 1);
  t[jn] = 0;
  long v[2];
  int k = 0, count = 0;
  for (char *p = t; *p;) {
    if (*p >= '0' && *p <= '9') {
      v[k++] = strtol(p, &p, 10);
      if (k == 2) {
        if (v[0] >= 0 && v[1] > 0 && (size_t)(v[0] + v[1]) <= n) memset(img + v[0], 0x90, (size_t)v[1]), count++;
        k = 0;
      }
    } else p++;
  }
  free(t);
  return count;
}

// ---------------------------------------------------------------- install

static int is_root_extra(const char *name) {   // loose disc-root files the game reads
  static const char *const k[] = {"autosavebase.pcx", "autosavebuttons.pcx", "jackal.pcx", "jackal.txt"};
  for (unsigned i = 0; i < sizeof k / sizeof k[0]; i++)
    if (!_stricmp(name, k[i])) return 1;
  return 0;
}

static int is_cab(const char *name) {   // data1.hdr, dataN.cab
  if (!_stricmp(name, "data1.hdr")) return 1;
  size_t l = strlen(name);
  if (l < 9 || _strnicmp(name, "data", 4) || _stricmp(name + l - 4, ".cab")) return 0;
  for (size_t i = 4; i < l - 4; i++)
    if (name[i] < '0' || name[i] > '9') return 0;
  return 1;
}

static void rmtree(const char *dir) {   // the scratch folder: plain files, no links
  char pat[MAX_PATH * 2];
  snprintf(pat, sizeof pat, "%s\\*", dir);
  WIN32_FIND_DATAA fd;
  HANDLE h = FindFirstFileA(pat, &fd);
  if (h != INVALID_HANDLE_VALUE) {
    do {
      if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, "..")) continue;
      char p[MAX_PATH * 2];
      snprintf(p, sizeof p, "%s\\%s", dir, fd.cFileName);
      if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) DeleteFileA(p);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
  }
  RemoveDirectoryA(dir);
}

static int install(const char *disc_path, const char *exe_path, const char *out, int from_folder) {
  char nops[MAX_PATH], gamedir[MAX_PATH * 2], scratch[MAX_PATH * 2], p[MAX_PATH * 3];
  GetModuleFileNameA(NULL, nops, MAX_PATH);
  char *sl = strrchr(nops, '\\');
  snprintf(sl ? sl + 1 : nops, MAX_PATH - (sl ? (size_t)(sl + 1 - nops) : 0), "jgld-nops.json");
  snprintf(gamedir, sizeof gamedir, "%s\\game", out);
  snprintf(scratch, sizeof scratch, "%s\\setup-scratch", out);
  safe_destination(out);
  safe_destination(gamedir);
  safe_destination(scratch);

  char hash[65];
  if (!file_hash(exe_path, BCRYPT_SHA1_ALGORITHM, hash) || strcmp(hash, NOCD_SHA1))
    fail(2, "the remaster runs version 1.02 program code and needs the exact version 1.02 no-CD golf.exe; the 1.03 golf.exe cannot replace it");
  if (from_folder) {
    separate_folders(disc_path, out);
    validate_folder(disc_path); // Complete preflight before any destination writes.
  }
  size_t en = 0;
  uint8_t *exe = read_all(exe_path, &en);
  if (!exe) fail(2, "the no-CD golf.exe could not be read");
  size_t gn = 0;
  uint8_t *gimg = map_pe(exe, en, &gn);
  if (!gimg) fail(2, "that golf.exe is not a Windows program");

  int total = 0, files = 0;
  char jgld[MAX_PATH * 3] = "";
  if (from_folder) {
    total = (int)(sizeof game_files / sizeof game_files[0]);
    for (int i = 0; i < total; i++) {
      char src[MAX_PATH * 3];
      const GameFile *g = &game_files[i];
      if (!joined_path(src, sizeof src, disc_path, g->path) || !joined_path(p, sizeof p, gamedir, g->path))
        fail(2, "a game file path is too long");
      safe_destination(p);
      mkparent(p);
      if (!CopyFileA(src, p, FALSE)) fail(1, "copying the checked game files failed (is the disk full?)");
      // Check copied bytes too, in case a source file changed after preflight.
      if (!file_hash(p, BCRYPT_SHA256_ALGORITHM, hash) || strcmp(hash, g->sha256))
        fail(2, "a game file changed while copying; close the original game and run setup again");
      files++;
      if (!_stricmp(g->path, "jgld.dll")) snprintf(jgld, sizeof jgld, "%s", p);
      if (i % 25 == 0) printf("progress %d %d %s\n", i, total, g->path), fflush(stdout);
    }
    // Keep player records out of the import; an empty original table is sufficient.
    snprintf(p, sizeof p, "%s\\top10.sve", gamedir);
    if (GetFileAttributesA(p) == INVALID_FILE_ATTRIBUTES && !write_all(p, "", 0))
      fail(1, "creating the high-score table failed");
  } else {
  Disc d;
  if (!disc_open(&d, disc_path)) fail(2, "that file does not look like a CD image (.iso, .bin, .img or .mdf)");
  int ne = 0;
  Entry *root = disc_root(&d, &ne);
  if (!root) fail(2, "the disc image could not be read");
  int cabs = 0, hdr = 0;
  for (int i = 0; i < ne; i++)
    if (!root[i].dir && is_cab(root[i].name)) cabs++, hdr |= !_stricmp(root[i].name, "data1.hdr");
  if (!hdr || cabs < 2) fail(2, "that disc image is not the SimGolf CD (no installer cabinets found)");

  mkdirs(scratch);
  printf("progress 0 0 reading the disc\n");
  fflush(stdout);
  for (int i = 0; i < ne; i++)
    if (!root[i].dir && is_cab(root[i].name)) {
      snprintf(p, sizeof p, "%s\\%s", scratch, root[i].name);
      if (!disc_copy(&d, &root[i], p)) fail(1, "copying the installer from the disc failed (is the disk full?)");
    }

  snprintf(p, sizeof p, "%s\\data1.cab", scratch);
  Unshield *us = unshield_open(p);
  if (!us) fail(2, "the installer on that disc image could not be opened");
  UnshieldFileGroup *g = unshield_file_group_find(us, GROUP);
  if (!g) fail(2, "that disc image is not the SimGolf CD (no game files in the installer)");
  mkdirs(gamedir);
  total = (int)(g->last_file - g->first_file + 1);
  int done = 0;
  for (int i = (int)g->first_file; i <= (int)g->last_file; i++, done++) {
    if (!unshield_file_is_valid(us, i)) continue;
    const char *dir = unshield_directory_name(us, unshield_file_directory(us, i));
    const char *name = unshield_file_name(us, i);
    if (!name || !*name) continue;
    char rel[MAX_PATH * 2];
    snprintf(rel, sizeof rel, "%s%s%s", dir ? dir : "", dir && *dir ? "\\" : "", name);
    char *r = rel;
    while (*r == '\\') r++;
    snprintf(p, sizeof p, "%s\\%s", gamedir, r);
    safe_destination(p);
    mkparent(p);
    if (!unshield_file_save(us, i, p)) {
      fprintf(stderr, "error: unpacking %s failed\n", r);
      return 1;
    }
    files++;
    if (!_stricmp(name, "jgld.dll")) snprintf(jgld, sizeof jgld, "%s", p);
    if (done % 25 == 0) printf("progress %d %d %s\n", done, total, r), fflush(stdout);
  }
  unshield_close(us);
  for (int i = 0; i < ne; i++)
    if (!root[i].dir && is_root_extra(root[i].name)) {
      snprintf(p, sizeof p, "%s\\%s", gamedir, root[i].name);
      if (!disc_copy(&d, &root[i], p)) fail(1, "copying from the disc failed");
      files++;
    }
  fclose(d.f);
  rmtree(scratch);
  }
  if (!jgld[0]) fail(2, "jgld.dll was not in the installer; is this the right disc?");

  size_t jn = 0, jin = 0;
  uint8_t *jraw = read_all(jgld, &jn), *jimg = jraw ? map_pe(jraw, jn, &jin) : NULL;
  if (!jimg) fail(1, "jgld.dll could not be read");
  int nn = apply_nops(jimg, jin, nops);
  if (nn < 0) fail(1, "jgld-nops.json is missing next to sgsetup.exe");
  snprintf(p, sizeof p, "%s\\image\\golf.bin", out);
  if (!write_all(p, gimg, gn)) fail(1, "writing image\\golf.bin failed");
  snprintf(p, sizeof p, "%s\\image\\jgld.bin", out);
  if (!write_all(p, jimg, jin)) fail(1, "writing image\\jgld.bin failed");
  printf("progress %d %d done\n", total, total);
  printf("ok %d game files, golf.bin %u bytes, jgld.bin %u bytes, %d patches\n", files, (unsigned)gn, (unsigned)jin, nn);
  return 0;
}

int main(int argc, char **argv) {
  if (argc == 5 && !strcmp(argv[1], "install")) return install(argv[2], argv[3], argv[4], 0);
  if (argc == 6 && !strcmp(argv[1], "install") && !strcmp(argv[2], "--game-dir"))
    return install(argv[3], argv[4], argv[5], 1);
  if (argc == 3 && !strcmp(argv[1], "check-folder")) return validate_folder(argv[2]);
  fprintf(stderr, "usage: sgsetup install <disc image> <1.02 golf.exe> <install folder>\n"
                  "       sgsetup install --game-dir <game folder> <1.02 golf.exe> <install folder>\n"
                  "       sgsetup check-folder <game folder>\n");
  return 2;
}
