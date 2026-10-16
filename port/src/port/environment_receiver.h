#ifndef SIMGOLF_ENVIRONMENT_RECEIVER_H
#define SIMGOLF_ENVIRONMENT_RECEIVER_H
#include <stdint.h>
// Caller owns width*height bytes and width*height queue entries. 0 unclassified,
// 64 visible course objects, 127 ground, 160 painted water, 255 black background.
int environment_receiver_build(uint8_t *out, int *queue, int width, int height,
                               const uint16_t *course, const uint8_t *terrain);
#endif
