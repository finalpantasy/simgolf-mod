// Game file access. The game opens DOS paths relative to its install folder
// ("Interface\\TitleBASE.pcx", "Saved Games\\x.sve"). The port mounts the
// user's copy of the game at /game in the Emscripten FS: files listed in the
// manifest are fetched from the server on first open, and paths are matched
// case-insensitively as on Windows.

#ifndef SIMGOLF_PORT_FS_H
#define SIMGOLF_PORT_FS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FS_GAME_ROOT "/game"

void fs_init(void);
// Maps a game path to an absolute FS path in `out`. For reading, the file must
// exist (it's fetched if needed); for writing, missing directories are an
// error but a missing file is fine. Returns 0 on failure.
int fs_resolve(const char *dospath, char *out, int outlen, int for_write);

// Persistence (IndexedDB) for files the game writes: call after closing a
// file that was opened for writing, or after deleting one.
void fs_written(const char *realpath);
void fs_deleted(const char *realpath);

// task-114f: saves that survive a crash or a power cut. fs_commit asks the OS to put a file's written data on the
// disk (FlushFileBuffers / fsync); fs_replace moves a finished temporary file over the real one in one step
// (the old file stays whole until then). fs_safe_saves: mods.ini [fixes] safe_saves (default 1; 0 writes saves in
// place without flushing, as the original).
int fs_commit(int fd);
int fs_replace(const char *tmp, const char *dst);
int fs_safe_saves(void);

// Result of the most recent .sve write through the game's CRT. Reset before
// requesting a save so an open failure cannot be mistaken for an older success.
void crt_reset_last_save_write(void);
int crt_last_save_write(void); // 1 complete, 0 failed, -1 no save was opened

// Directory listing for FindFirstFile: calls fn for each entry of the
// resolved directory whose name matches the DOS wildcard pattern.
// modified is the last-write time in Windows FILETIME ticks (100ns since
// 1601), or zero if unavailable. It travels with the filename, not a UI row.
typedef void (*fs_list_fn)(void *ctx, const char *name, int is_dir, unsigned size, uint64_t modified);
int fs_list(const char *dospattern, fs_list_fn fn, void *ctx);

#ifdef __cplusplus
}
#endif
#endif  // SIMGOLF_PORT_FS_H
