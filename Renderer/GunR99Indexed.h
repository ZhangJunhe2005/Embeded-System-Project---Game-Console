#ifndef R99_GUN_INDEXED_H
#define R99_GUN_INDEXED_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const uint8_t r99_idx8[];
extern const uint16_t r99_idx8_width;
extern const uint16_t r99_idx8_height;
extern const uint16_t r99_lut565[256];

#define R99_TRANSPARENT_INDEX 0

#ifdef __cplusplus
}
#endif

#endif /* R99_GUN_INDEXED_H */
