#ifndef ROBOT1_INDEXED_H
#define ROBOT1_INDEXED_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ROBOT1_FRAME_COUNT 120
#define ROBOT1_POSE_COUNT 3
#define ROBOT1_SCALE_COUNT 6
#define ROBOT1_NORMAL_OFFSET 0
#define ROBOT1_DIE_OFFSET 18
#define ROBOT1_DIE_FRAME_COUNT 4
#define ROBOT1_ULTIMATE_OFFSET 42
#define ROBOT1_ULTIMATE_FRAME_COUNT 13
#define ROBOT1_ULTIMATE_SCALE_COUNT 6
#define ROBOT1_TRANSPARENT_INDEX 0

extern const uint8_t * const robot1_frames[ROBOT1_FRAME_COUNT];
extern const int8_t robot1_grid_y[ROBOT1_SCALE_COUNT];
extern const int16_t robot1_ultimate_scale_y[ROBOT1_ULTIMATE_SCALE_COUNT];
extern const uint16_t robot1_widths[ROBOT1_FRAME_COUNT];
extern const uint16_t robot1_heights[ROBOT1_FRAME_COUNT];
extern const uint16_t robot1_lut565[256];

#ifdef __cplusplus
}
#endif

#endif /* ROBOT1_INDEXED_H */
