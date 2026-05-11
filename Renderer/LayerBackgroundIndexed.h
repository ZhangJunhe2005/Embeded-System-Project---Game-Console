#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const uint8_t leftwall_idx8[];
extern const uint16_t leftwall_idx8_width;
extern const uint16_t leftwall_idx8_height;
extern const uint16_t leftwall_lut565[256];

extern const uint8_t center_idx8[];
extern const uint16_t center_idx8_width;
extern const uint16_t center_idx8_height;
extern const uint16_t center_lut565[256];

extern const uint8_t rightwall_idx8[];
extern const uint16_t rightwall_idx8_width;
extern const uint16_t rightwall_idx8_height;
extern const uint16_t rightwall_lut565[256];

#define LAYER_WIN_LEFT   72
#define LAYER_WIN_CENTER 96
#define LAYER_WIN_RIGHT  72

#define LAYER_DRAW_X_LEFT   0
#define LAYER_DRAW_X_CENTER 72
#define LAYER_DRAW_X_RIGHT  168

#define LAYER_SRC_W_LEFT   136
#define LAYER_SRC_W_CENTER 148
#define LAYER_SRC_W_RIGHT  136
#define LAYER_SHIFT_MAX_LEFT   (LAYER_SRC_W_LEFT - LAYER_WIN_LEFT)
#define LAYER_SHIFT_MAX_CENTER (LAYER_SRC_W_CENTER - LAYER_WIN_CENTER)
#define LAYER_SHIFT_MAX_RIGHT  (LAYER_SRC_W_RIGHT - LAYER_WIN_RIGHT)

#define LAYER_SCREEN_H 240
#define LAYER_SOURCE_H 420
#define LAYER_Y_SHIFT_MAX (LAYER_SOURCE_H - LAYER_SCREEN_H)

#ifdef __cplusplus
}
#endif
