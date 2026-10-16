// fs.h for the native Windows build (simgolf mod, task-2): the game's DOS
// paths map straight onto the real game folder (host_game_dir()). Windows
// paths are case-insensitive already, so there is no manifest and nothing to
// fetch, and saves are ordinary files in the folder's "Saved Games".

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <ctype.h>
#include <direct.h>
#include <io.h>
#include <stdio.h>
#include <string.h>

#include "port/fs.h"
#include "port/host.h"
#include "mods/mods.h"
#include "mods/living_club.h"

// The folder a game-relative path lives in: the per-instance user folder
// (SIMGOLF_USER_DIR, task-13) for saves and snapshots, else the game folder.
static const char *base_for(const char *rel) {
  // Redirect only the save/snapshot *directories*, not paths whose names
  // merely begin with the same characters (such as "Saved Games Backup").
  int saves = !_strnicmp(rel, "Saved Games", 11) && (rel[11] == 0 || rel[11] == '/');
  int snaps = !_strnicmp(rel, "Snapshots", 9) && (rel[9] == 0 || rel[9] == '/');
  return saves || snaps ? host_user_dir() : host_game_dir();
}

void fs_init(void) {
  char p[MAX_PATH + 32];
  // The retail installer creates these and the game never does (see fs.c).
  snprintf(p, sizeof p, "%s/Saved Games", host_user_dir());
  _mkdir(p);
  snprintf(p, sizeof p, "%s/Snapshots", host_user_dir());
  _mkdir(p);
  fprintf(stderr, "fs: game files from %s\n", host_game_dir());
}

// DOS path -> path relative to the game folder ('/' separated), as fs.c does:
// drive letters, the reported install folder C:\SimGolf and "./" go away.
static int normalize(const char *dos, char *out, int outlen) {
  if (!dos || !out || outlen <= 0) return 0;
  char tmp[512];
  if (strlen(dos) >= sizeof tmp) return 0;
  int j = 0;
  for (const char *p = dos; *p && j < (int)sizeof tmp - 1; p++) tmp[j++] = *p == '\\' ? '/' : *p;
  tmp[j] = 0;
  const char *s = tmp;
  if (isalpha((unsigned char)s[0]) && s[1] == ':') s += 2;
  if (!_strnicmp(s, "/simgolf/", 9)) s += 9;
  while (*s == '/') s++;
  int k = 0;
  while (*s && k < outlen - 1) {
    if (s[0] == '.' && s[1] == '/' && (k == 0 || out[k - 1] == '/')) { s += 2; continue; }
    if (s[0] == '/' && k > 0 && out[k - 1] == '/') { s++; continue; }
    out[k++] = *s++;
  }
  out[k] = 0;
  return *s == 0;
}

// task-114j: a game path never climbs out of the game folder ("..\..\x" from a hostile save or mod).
static int has_parent_part(const char *rel) {
  for (const char *p = rel; *p;) {
    const char *e = strchr(p, '/');
    size_t n = e ? (size_t)(e - p) : strlen(p);
    if (n == 2 && p[0] == '.' && p[1] == '.') return 1;
    p += n;
    if (*p == '/') p++;
  }
  return 0;
}

int fs_resolve(const char *dospath, char *out, int outlen, int for_write) {
  if (!dospath || !out || outlen <= 0) return 0;
  char rel[512];
  if (!normalize(dospath, rel, sizeof rel)) return 0;
  if (has_parent_part(rel)) { fprintf(stderr, "fs: refused a path with a parent part: %s\n", dospath); return 0; }
  int n = snprintf(out, outlen, "%s/%s", base_for(rel), rel);
  if (n < 0 || n >= outlen) { out[0] = 0; return 0; }
  harness_note_file(out);
  DWORD a = GetFileAttributesA(out);
  if (a != INVALID_FILE_ATTRIBUTES) {
    if (!for_write) mods_save_opened(out);   // a .sve: check its mod record (task-11)
    return 1;
  }
  if (!for_write) {
    // Authored roster companions travel with the program, without modifying
    // installed themes. Existing player copies above retain precedence.
    const char *theme_end = !_strnicmp(rel, "Themes/", 7) ? strchr(rel + 7, '/') : NULL;
    const char *name = strrchr(rel, '/');
    const char *ext = name ? strrchr(name, '.') : NULL;
    int pro = theme_end && !_strnicmp(rel + 7, "Championship/", 13) && name == theme_end && ext && !_stricmp(ext, ".pro");
    int character = theme_end && !_strnicmp(theme_end, "/New Roster/", 12) && name == theme_end + 11 && ext && !_stricmp(ext, ".chr");
    if ((pro || character) && mods_get_int("gameplay.new_roster", 1)) {
      n = snprintf(out, outlen, "%s/ui/characters/%s", host_exe_dir(), name + 1);
      if (n < 0 || n >= outlen) { out[0] = 0; return 0; }
      a = GetFileAttributesA(out);
      if (a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY)) return 1;
    }
    return 0;
  }
  // Writing: the file may be missing, its folder may not.
  char dir[600];
  snprintf(dir, sizeof dir, "%s", out);
  char *slash = strrchr(dir, '/');
  if (!slash) return 0;
  *slash = 0;
  a = GetFileAttributesA(dir);
  return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

// A .sve the game wrote gets a record of the loaded mods next to it (task-11).
void fs_written(const char *realpath) { mods_save_written(realpath); living_written(realpath); }
void fs_deleted(const char *realpath) { (void)realpath; }

int fs_commit(int fd) { return _commit(fd) == 0; }
int fs_replace(const char *tmp, const char *dst) { return MoveFileExA(tmp, dst, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0; }
int fs_safe_saves(void) { return mods_get_int("fixes.safe_saves", 1); }

int fs_list(const char *dospattern, fs_list_fn fn, void *ctx) {
  char rel[512], pat[600];
  if (!fn || !normalize(dospattern, rel, sizeof rel)) return 0;
  if (has_parent_part(rel)) return 0;
  int len = snprintf(pat, sizeof pat, "%s/%s", base_for(rel), rel);
  if (len < 0 || len >= (int)sizeof pat) return 0;
  WIN32_FIND_DATAA fd;
  HANDLE h = FindFirstFileA(pat, &fd);
  if (h == INVALID_HANDLE_VALUE) return 0;
  int n = 0;
  do {
    if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, "..")) continue;
    fn(ctx, fd.cFileName, (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0, fd.nFileSizeLow);
    n++;
  } while (FindNextFileA(h, &fd));
  FindClose(h);
  return n;
}
