#ifndef KUWU_INDEXED_H
#define KUWU_INDEXED_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const uint8_t kuwu_idx8[];
extern const uint16_t kuwu_lut565[256];
extern const uint16_t kuwu_width;
extern const uint16_t kuwu_height;
extern const int16_t kuwu_canvas_x;
extern const int16_t kuwu_canvas_y;

#define KUWU_TRANSPARENT_INDEX 0

#ifdef __cplusplus
}
#endif

#endif /* KUWU_INDEXED_H */
