// Cached presentation-only terrain receivers. No game state or source art writes.
#ifndef SIMGOLF_COURSE_RELIEF_H
#define SIMGOLF_COURSE_RELIEF_H
#include <stdint.h>
#include "port/water_native.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct CourseRelief {
  float *lift;                 // source pixels above the common reference plane
  uint8_t *features;           // packed normal X/Z, valley retention, fog height/material
  int width,height;
  long serial,map_serial;
  float datum,height_pixels,max_lift;
} CourseRelief;
int course_relief_material(const char *name);
int course_relief_build(CourseRelief *out,const HdtScene *scene,int width,int height);
void course_relief_destroy(CourseRelief *field);
double course_relief_sample(const CourseRelief *field,double x,double y);
double course_relief_valley(const HdtScene *scene,double x,double z);
// Shadow-plane coordinates for the light ray through an actual receiver.
void course_relief_shadow(double x,double y,double lift,double sx,double sy,double *u,double *v);
#ifdef __cplusplus
}
#endif
#endif
