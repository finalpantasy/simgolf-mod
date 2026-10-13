#ifndef SIMGOLF_CAMERA_TILES_H
#define SIMGOLF_CAMERA_TILES_H
void camera_tiles_capture(int x, int y, int side);
int camera_tiles_restore(int x, int y);
void camera_tiles_reset(void);
void camera_tiles_save_field(int fd, int reading);
int camera_tiles_written(int fd, const char *path);
void camera_tiles_closed(int fd);
const char *crt_real_path(unsigned fd);
#endif
