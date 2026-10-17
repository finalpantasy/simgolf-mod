// Optional course furniture. Coordinates and prices stay independent of old save layouts.
#ifndef SIMGOLF_COURSE_LAMPS_H
#define SIMGOLF_COURSE_LAMPS_H
#include <stdint.h>
#define LAMP_LIMIT 256
#define LAMP_STYLES 6
typedef struct { char id[24], name[32]; int cost; float radius, height; uint8_t r,g,b; } LampStyle;
typedef struct { uint16_t row,col; uint8_t style,reserved; uint16_t paid; int16_t offset_row,offset_col; } CourseLamp;
typedef struct { int count; CourseLamp lamps[LAMP_LIMIT]; } LampState;
typedef struct { float x,y; int style; } LampPoint;
typedef struct { int enabled,count,zoom,selected,hover_valid,hover_style,rotation; float hover_x,hover_y; LampStyle styles[LAMP_STYLES]; LampPoint points[LAMP_LIMIT]; } LampFrame;
void lamps_sprite_rgba(int style,uint32_t out[32*64]);
const char *lamps_art_id(int style);
int lamps_load_styles(LampStyle out[LAMP_STYLES],const char *path);
int lamps_at(const LampState *s,int row,int col);
int lamps_place(LampState *s,const LampStyle *styles,int style,int row,int col,int w,int h,int allowed,int *cash,int sandbox);
int lamps_remove(LampState *s,int row,int col);
int lamps_world_row(const CourseLamp *p);
int lamps_world_col(const CourseLamp *p);
int lamps_nearest(const LampState *s,int world_row,int world_col,int radius);
int lamps_place_at(LampState *s,const LampStyle *styles,int style,int world_row,int world_col,int w,int h,int spacing,int *cash,int sandbox);
int lamps_remove_at(LampState *s,int world_row,int world_col,int radius);
float lamps_falloff(float distance,float radius);
int lamps_save(const LampState *s,const char *save,int w,int h);
// -1 corrupt/mismatched, 0 absent, 1 valid. Invalid input never changes the destination.
int lamps_load(LampState *s,const char *save,int w,int h);
#ifndef __EMSCRIPTEN__
void lamps_game_reset(void);
void lamps_game_save_field(int fd,int reading);
int lamps_game_written(int fd,const char *path);
void lamps_game_closed(int fd);
void lamps_game_course_done(void);
void lamps_game_select(int style);
int lamps_game_enabled(void);
int lamps_game_selected(void);
const LampStyle *lamps_game_styles(void);
void lamps_game_frame(LampFrame *out);
int lamps_game_probe(int x,int y,int *row,int *col,int *occupied,int *count,int *world_row,int *world_col);
#else
#define lamps_game_reset() ((void)0)
#define lamps_game_save_field(fd,reading) ((void)0)
#define lamps_game_written(fd,path) 1
#define lamps_game_closed(fd) ((void)0)
#define lamps_game_course_done() ((void)0)
#endif
#endif
