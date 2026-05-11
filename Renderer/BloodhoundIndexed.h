#ifndef BLOODHOUND_INDEXED_H
#define BLOODHOUND_INDEXED_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BLOODHOUND_FRAME_COUNT 6
#define BLOODHOUND_TRANSPARENT_INDEX 0

extern const uint8_t * const bloodhound_frames[BLOODHOUND_FRAME_COUNT];
extern const int8_t bloodhound_grid_y[BLOODHOUND_FRAME_COUNT];
extern const uint16_t bloodhound_widths[BLOODHOUND_FRAME_COUNT];
extern const uint16_t bloodhound_heights[BLOODHOUND_FRAME_COUNT];
extern const uint16_t bloodhound_lut565[256];

#ifdef __cplusplus
}
#endif

#endif /* BLOODHOUND_INDEXED_H */
