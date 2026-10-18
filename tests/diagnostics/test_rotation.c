#include <assert.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "port/log_rotation.h"
static DWORD error;
static int deny;
DWORD GetLastError(void) { return error; }
DWORD GetFileAttributesA(const char *p) {
  struct stat st;
  if (stat(p, &st)) { error = errno == ENOENT ? ERROR_FILE_NOT_FOUND : 5; return INVALID_FILE_ATTRIBUTES; }
  return S_ISDIR(st.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : 0;
}
int MoveFileExA(const char *a, const char *b, DWORD flags) {
  assert(flags == MOVEFILE_REPLACE_EXISTING);
  if (deny) { error = 5; return 0; }
  if (!rename(a, b)) return 1;
  error = errno == ENOENT ? ERROR_FILE_NOT_FOUND : 5;
  return 0;
}
static void write_log(int n) { FILE *f = fopen("golf.log", "w"); assert(f); fprintf(f, "%d", n); fclose(f); }
static int read_log(const char *p) { FILE *f = fopen(p, "r"); int n = -1; assert(f); assert(fscanf(f, "%d", &n) == 1); fclose(f); return n; }
int main(void) {
  assert(host_rotate_log("golf.log"));
  for (int i = 0; i < 9; ++i) { write_log(i); assert(host_rotate_log("golf.log")); }
  assert(read_log("golf.log.1") == 8);
  assert(read_log("golf.log.5") == 4);
  assert(access("golf.log.6", F_OK) != 0);
  write_log(9); deny = 1;
  assert(!host_rotate_log("golf.log"));
  assert(read_log("golf.log") == 9);
  assert(read_log("golf.log.1") == 8);
  deny = 0;
  assert(mkdir("folder", 0700) == 0);
  assert(!host_rotate_log("folder"));
  puts("rotation tests passed");
  return 0;
}
