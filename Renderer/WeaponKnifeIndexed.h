#ifndef WEAPON_KNIFE_INDEXED_H
#define WEAPON_KNIFE_INDEXED_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const uint8_t weapon_knife_idx8[];
extern const uint16_t weapon_knife_lut565[256];
extern const uint16_t weapon_knife_width;
extern const uint16_t weapon_knife_height;
extern const uint16_t weapon_knife_qiedao_frames;
extern const uint16_t weapon_knife_total_frames;
extern const uint16_t weapon_knife_kuwu_frame;
extern const uint32_t weapon_knife_frame_stride;

#define WEAPON_KNIFE_TRANSPARENT_INDEX 0
#define WEAPON_KNIFE_BLANK_SEQUENCE_FRAME 15

#ifdef __cplusplus
}
#endif

#endif /* WEAPON_KNIFE_INDEXED_H */
