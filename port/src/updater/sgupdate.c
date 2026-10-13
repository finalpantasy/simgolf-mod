// sgupdate.exe: the small updater of SimGolf Remastered (simgolf mod, card task-80).
//
//   sgupdate --prepare --zip Z --sha FILE --stage DIR
//       checks the release zip against its .sha256 file and unpacks it into DIR (DIR\app\...).
//       exit 0 ok, 2 checksum does not match, 3 cannot unpack, 1 anything else. Changes nothing else.
//   sgupdate --apply --app DIR --stage DIR\app [--wait-pid N] [--restart] [--fail-after N]
//       waits for the game to quit, moves the old PROGRAM files into app\updates\backup-<time>, copies the new
//       ones in, and starts the game again. Any failure puts every moved file back (rollback), and the old
//       game is started again. --fail-after N makes the Nth item fail (tests of the rollback).
//
// Only program files are touched (see ITEMS). Never the player's game files (image\, game\), the HD art
// pack, saves, golf.ini, options.ini, mods.ini or the mods folder.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "bcrypt.lib")

// path relative to the app folder; a trailing slash is a folder
static const char PLAYER_EXE[] = "Sim Golf Remastered.exe";
static const char *const ITEMS[] = {"golf.exe", PLAYER_EXE, "SDL2.dll", "sgsetup.exe", "sgupdate.exe", "jgld-nops.json", "version.txt", "mods.ini.default", "fonts/", "ui/",
                                    "data/tables/", "mods-examples/"};
#define NITEMS ((int)(sizeof ITEMS / sizeof ITEMS[0]))

static char g_log_path[MAX_PATH];
static void logf_(const char *fmt, ...) {
  va_list ap;
  char line[600];
  va_start(ap, fmt);
  vsnprintf(line, sizeof line, fmt, ap);
  va_end(ap);
  fprintf(stderr, "sgupdate: %s\n", line);
  if (g_log_path[0]) {
    FILE *f = fopen(g_log_path, "ab");
    if (f) {
      fprintf(f, "%s\r\n", line);
      fclose(f);
    }
  }
}

static int exists(const char *p) { return GetFileAttributesA(p) != INVALID_FILE_ATTRIBUTES; }
static int is_dir(const char *p) {
  DWORD a = GetFileAttributesA(p);
  return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

static void mkdirs(const char *path) {
  char tmp[MAX_PATH];
  snprintf(tmp, sizeof tmp, "%s", path);
  for (char *p = tmp + 3; *p; p++)
    if (*p == '\\' || *p == '/') {
      char c = *p;
      *p = 0;
      CreateDirectoryA(tmp, NULL);
      *p = c;
    }
  CreateDirectoryA(tmp, NULL);
}

static void mkparent(const char *path) {
  char tmp[MAX_PATH];
  snprintf(tmp, sizeof tmp, "%s", path);
  char *s = strrchr(tmp, '\\');
  if (s) {
    *s = 0;
    mkdirs(tmp);
  }
}

static int rm_tree(const char *path) {
  if (!exists(path)) return 1;
  if (!is_dir(path)) {
    SetFileAttributesA(path, FILE_ATTRIBUTE_NORMAL);
    return DeleteFileA(path) != 0;
  }
  char pat[MAX_PATH + 4];
  snprintf(pat, sizeof pat, "%s\\*", path);
  WIN32_FIND_DATAA fd;
  HANDLE h = FindFirstFileA(pat, &fd);
  int ok = 1;
  if (h != INVALID_HANDLE_VALUE) {
    do {
      if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, "..")) continue;
      char sub[MAX_PATH];
      snprintf(sub, sizeof sub, "%s\\%s", path, fd.cFileName);
      ok &= rm_tree(sub);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
  }
  return ok && RemoveDirectoryA(path) != 0;
}

static int copy_tree(const char *from, const char *to) {
  if (!is_dir(from)) {
    mkparent(to);
    return CopyFileA(from, to, FALSE) != 0;
  }
  mkdirs(to);
  char pat[MAX_PATH + 4];
  snprintf(pat, sizeof pat, "%s\\*", from);
  WIN32_FIND_DATAA fd;
  HANDLE h = FindFirstFileA(pat, &fd);
  int ok = 1;
  if (h != INVALID_HANDLE_VALUE) {
    do {
      if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, "..")) continue;
      char a[MAX_PATH], b[MAX_PATH];
      snprintf(a, sizeof a, "%s\\%s", from, fd.cFileName);
      snprintf(b, sizeof b, "%s\\%s", to, fd.cFileName);
      ok &= copy_tree(a, b);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
  }
  return ok;
}

static void winpath(char *s) {
  for (; *s; s++)
    if (*s == '/') *s = '\\';
}

// ------------------------------------------------------------------ prepare

static int sha256_file(const char *path, char out[65]) {
  FILE *f = fopen(path, "rb");
  if (!f) return 0;
  BCRYPT_ALG_HANDLE alg = NULL;
  BCRYPT_HASH_HANDLE hash = NULL;
  unsigned char digest[32];
  int ok = 0;
  if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, NULL, 0) == 0 && BCryptCreateHash(alg, &hash, NULL, 0, NULL, 0, 0) == 0) {
    static unsigned char buf[1 << 16];
    size_t n;
    ok = 1;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0)
      if (BCryptHashData(hash, buf, (ULONG)n, 0) != 0) ok = 0;
    if (ok && BCryptFinishHash(hash, digest, 32, 0) != 0) ok = 0;
  }
  if (hash) BCryptDestroyHash(hash);
  if (alg) BCryptCloseAlgorithmProvider(alg, 0);
  fclose(f);
  if (ok)
    for (int i = 0; i < 32; i++) sprintf(out + i * 2, "%02x", digest[i]);
  return ok;
}

// the first 64 hex digits of a "hash  filename" file
static int read_expected(const char *path, char out[65]) {
  FILE *f = fopen(path, "rb");
  if (!f) return 0;
  char line[512] = "";
  size_t n = fread(line, 1, sizeof line - 1, f);
  fclose(f);
  line[n] = 0;
  int i = 0;
  while (line[i] == ' ' || line[i] == '\t' || line[i] == '\xEF' || line[i] == '\xBB' || line[i] == '\xBF') i++;
  for (int k = 0; k < 64; k++) {
    char c = line[i + k];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return 0;
    out[k] = (char)((c >= 'A' && c <= 'F') ? c + 32 : c);
  }
  out[64] = 0;
  return 1;
}

static int do_prepare(const char *zip, const char *sha, const char *stage) {
  char want[65], got[65];
  if (!read_expected(sha, want)) {
    logf_("prepare: no checksum in %s", sha);
    return 2;
  }
  if (!sha256_file(zip, got)) {
    logf_("prepare: cannot read %s", zip);
    return 1;
  }
  if (strcmp(want, got)) {
    logf_("prepare: CHECKSUM MISMATCH (expected %s, got %s): the zip is not used", want, got);
    return 2;
  }
  logf_("prepare: checksum ok (%s)", got);
  rm_tree(stage);
  mkdirs(stage);
  char sys[MAX_PATH], cmd[3 * MAX_PATH];
  GetSystemDirectoryA(sys, sizeof sys);
  snprintf(cmd, sizeof cmd, "\"%s\\tar.exe\" -xf \"%s\" -C \"%s\" --strip-components=1", sys, zip, stage);
  STARTUPINFOA si = {sizeof si};
  PROCESS_INFORMATION pi;
  si.dwFlags = STARTF_USESHOWWINDOW;
  si.wShowWindow = SW_HIDE;
  DWORD code = 1;
  if (CreateProcessA(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
    WaitForSingleObject(pi.hProcess, 5 * 60 * 1000);
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
  }
  char exe[MAX_PATH];
  snprintf(exe, sizeof exe, "%s\\app\\golf.exe", stage);
  if (code != 0 || !exists(exe)) {
    logf_("prepare: cannot unpack (tar exit %lu, golf.exe %s)", (unsigned long)code, exists(exe) ? "there" : "missing");
    rm_tree(stage);
    return 3;
  }
  logf_("prepare: unpacked to %s", stage);
  return 0;
}

// ------------------------------------------------------------------ apply

static int do_apply(const char *app, const char *stage, DWORD pid, int restart, int fail_after) {
  char upd[MAX_PATH];
  snprintf(upd, sizeof upd, "%s\\updates", app);
  mkdirs(upd);
  snprintf(g_log_path, sizeof g_log_path, "%s\\update.log", upd);
  logf_("apply: app %s stage %s", app, stage);
  if (pid) {
    HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (h) {
      if (WaitForSingleObject(h, 60000) != WAIT_OBJECT_0) {
        CloseHandle(h);
        logf_("apply: the game did not quit within 60 s, nothing changed");
        return 2;
      }
      CloseHandle(h);
    }
  }
  char exe[MAX_PATH];
  snprintf(exe, sizeof exe, "%s\\golf.exe", stage);
  if (!exists(exe)) {
    logf_("apply: %s is missing, nothing changed", exe);
    return 1;
  }
  SYSTEMTIME st;
  GetLocalTime(&st);
  char backup[MAX_PATH];
  snprintf(backup, sizeof backup, "%s\\backup-%04d%02d%02d-%02d%02d%02d", upd, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
  mkdirs(backup);
  int moved[NITEMS] = {0}, copied[NITEMS] = {0}, failed = 0, nitem = 0;
  for (int i = 0; i < NITEMS && !failed; i++) {
    char rel[MAX_PATH], src[MAX_PATH], dst[MAX_PATH], bak[MAX_PATH];
    snprintf(rel, sizeof rel, "%s", ITEMS[i]);
    winpath(rel);
    size_t rl = strlen(rel);
    if (rl && rel[rl - 1] == '\\') rel[rl - 1] = 0;
    // The named player program uses the same verified payload, including older zips.
    snprintf(src, sizeof src, "%s\\%s", stage, !strcmp(rel, PLAYER_EXE) ? "golf.exe" : rel);
    snprintf(dst, sizeof dst, "%s\\%s", app, rel);
    snprintf(bak, sizeof bak, "%s\\%s", backup, rel);
    if (!exists(src)) continue;   // an older release without this item: leave the player's as is
    nitem++;
    if (fail_after > 0 && nitem == fail_after) {
      logf_("apply: test failure at item %d (%s)", nitem, rel);
      failed = 1;
      break;
    }
    if (exists(dst)) {
      mkparent(bak);
      if (!MoveFileExA(dst, bak, 0)) {
        logf_("apply: cannot move %s aside (error %lu)", rel, (unsigned long)GetLastError());
        failed = 1;
        break;
      }
      moved[i] = 1;
    }
    if (!copy_tree(src, dst)) {
      logf_("apply: cannot copy %s (error %lu)", rel, (unsigned long)GetLastError());
      copied[i] = 1;   // may be half there: the rollback removes it
      failed = 1;
      break;
    }
    copied[i] = 1;
  }
  if (!failed) {
    char a[MAX_PATH], b[MAX_PATH];
    snprintf(a, sizeof a, "%s\\golf.exe", app);
    HANDLE h1 = CreateFileA(a, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL), h2 = CreateFileA(exe, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    LARGE_INTEGER s1 = {0}, s2 = {1};
    if (h1 != INVALID_HANDLE_VALUE) GetFileSizeEx(h1, &s1);
    if (h2 != INVALID_HANDLE_VALUE) GetFileSizeEx(h2, &s2);
    if (h1 != INVALID_HANDLE_VALUE) CloseHandle(h1);
    if (h2 != INVALID_HANDLE_VALUE) CloseHandle(h2);
    (void)b;
    if (s1.QuadPart != s2.QuadPart) {
      logf_("apply: the new golf.exe does not match the release");
      failed = 1;
    }
  }
  if (failed) {
    logf_("apply: FAILED, rolling back");
    for (int i = NITEMS - 1; i >= 0; i--) {
      if (!copied[i] && !moved[i]) continue;
      char rel[MAX_PATH], dst[MAX_PATH], bak[MAX_PATH];
      snprintf(rel, sizeof rel, "%s", ITEMS[i]);
      winpath(rel);
      size_t rl = strlen(rel);
      if (rl && rel[rl - 1] == '\\') rel[rl - 1] = 0;
      snprintf(dst, sizeof dst, "%s\\%s", app, rel);
      snprintf(bak, sizeof bak, "%s\\%s", backup, rel);
      if (copied[i]) rm_tree(dst);
      if (moved[i] && !MoveFileExA(bak, dst, 0)) logf_("apply: ROLLBACK of %s failed, the old copy is in %s", rel, bak);
    }
    FILE *f;
    char res[MAX_PATH];
    snprintf(res, sizeof res, "%s\\last_update.txt", upd);
    if ((f = fopen(res, "wb"))) {
      fprintf(f, "failed\r\n");
      fclose(f);
    }
    rm_tree(backup);
  } else {
    logf_("apply: ok, %d items replaced; the old program files are in %s", nitem, backup);
    char res[MAX_PATH];
    FILE *f;
    snprintf(res, sizeof res, "%s\\last_update.txt", upd);
    if ((f = fopen(res, "wb"))) {
      fprintf(f, "ok\r\n");
      fclose(f);
    }
    // keep only the newest backup
    char pat[MAX_PATH + 16];
    snprintf(pat, sizeof pat, "%s\\backup-*", upd);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h != INVALID_HANDLE_VALUE) {
      do {
        char p[MAX_PATH];
        snprintf(p, sizeof p, "%s\\%s", upd, fd.cFileName);
        if (_stricmp(p, backup) < 0) rm_tree(p);
      } while (FindNextFileA(h, &fd));
      FindClose(h);
    }
    char st[MAX_PATH];
    snprintf(st, sizeof st, "%s", stage);
    char *s = strrchr(st, '\\');
    if (s && !strncmp(s + 1, "app", 3)) {
      *s = 0;
      char *q = strrchr(st, '\\');
      if (q && !strncmp(q + 1, "stage-", 6)) rm_tree(st);   // the stage-<version> folder this updater was given
    }
  }
  if (restart) {
    char g[MAX_PATH + 16];
    snprintf(g, sizeof g, "\"%s\\%s\"", app, PLAYER_EXE);
    STARTUPINFOA si = {sizeof si};
    PROCESS_INFORMATION pi;
    if (CreateProcessA(NULL, g, NULL, NULL, FALSE, 0, NULL, app, &si, &pi)) {
      CloseHandle(pi.hThread);
      CloseHandle(pi.hProcess);
      logf_("apply: game started again");
    } else logf_("apply: could not start the game (error %lu)", (unsigned long)GetLastError());
  }
  return failed ? 1 : 0;
}

int main(int argc, char **argv) {
  const char *zip = NULL, *sha = NULL, *stage = NULL, *app = NULL;
  int prepare = 0, apply = 0, restart = 0, fail_after = 0;
  DWORD pid = 0;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--prepare")) prepare = 1;
    else if (!strcmp(argv[i], "--apply")) apply = 1;
    else if (!strcmp(argv[i], "--restart")) restart = 1;
    else if (i + 1 < argc && !strcmp(argv[i], "--zip")) zip = argv[++i];
    else if (i + 1 < argc && !strcmp(argv[i], "--sha")) sha = argv[++i];
    else if (i + 1 < argc && !strcmp(argv[i], "--stage")) stage = argv[++i];
    else if (i + 1 < argc && !strcmp(argv[i], "--app")) app = argv[++i];
    else if (i + 1 < argc && !strcmp(argv[i], "--wait-pid")) pid = (DWORD)atoi(argv[++i]);
    else if (i + 1 < argc && !strcmp(argv[i], "--fail-after")) fail_after = atoi(argv[++i]);
  }
  if (prepare && zip && sha && stage) return do_prepare(zip, sha, stage);
  if (apply && app && stage) return do_apply(app, stage, pid, restart, fail_after);
  fprintf(stderr, "usage: sgupdate --prepare --zip Z --sha FILE --stage DIR | --apply --app DIR --stage DIR [--wait-pid N] [--restart]\n");
  return 64;
}
