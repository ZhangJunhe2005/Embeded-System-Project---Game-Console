#ifndef GUN_IMAGES_INDEXED_H
#define GUN_IMAGES_INDEXED_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  GUN_IMAGE_R99_NORMAL,
  GUN_IMAGE_R99_ADS,
  GUN_IMAGE_HEPING_NORMAL,
  GUN_IMAGE_FUZHU_NORMAL,
  GUN_IMAGE_FUZHU_ADS,
  GUN_IMAGE_COUNT
} GunImageId;

#define GUN_IMAGE_TRANSPARENT_INDEX 0

extern const uint8_t * const gun_image_frames[GUN_IMAGE_COUNT];
extern const uint16_t gun_image_widths[GUN_IMAGE_COUNT];
extern const uint16_t gun_image_heights[GUN_IMAGE_COUNT];
extern const int16_t gun_image_x[GUN_IMAGE_COUNT];
extern const int16_t gun_image_y[GUN_IMAGE_COUNT];
extern const uint16_t gun_image_lut565[256];

#ifdef __cplusplus
}
#endif

#endif /* GUN_IMAGES_INDEXED_H */
