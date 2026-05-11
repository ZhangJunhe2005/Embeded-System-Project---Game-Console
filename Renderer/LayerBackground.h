#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const uint16_t leftwall_rgb565[];
extern const uint16_t leftwall_rgb565_width;
extern const uint16_t leftwall_rgb565_height;

extern const uint16_t center_rgb565[];
extern const uint16_t center_rgb565_width;
extern const uint16_t center_rgb565_height;

extern const uint16_t rightwall_rgb565[];
extern const uint16_t rightwall_rgb565_width;
extern const uint16_t rightwall_rgb565_height;

#define LAYER_WIN_LEFT   72
#define LAYER_WIN_CENTER 96
#define LAYER_WIN_RIGHT  72

#define LAYER_DRAW_X_LEFT   0
#define LAYER_DRAW_X_CENTER 72
#define LAYER_DRAW_X_RIGHT  168

#define LAYER_SRC_W_LEFT   117
#define LAYER_SRC_W_CENTER 126
#define LAYER_SRC_W_RIGHT  117
#define LAYER_SHIFT_MAX_LEFT   (LAYER_SRC_W_LEFT - LAYER_WIN_LEFT)
#define LAYER_SHIFT_MAX_CENTER (LAYER_SRC_W_CENTER - LAYER_WIN_CENTER)
#define LAYER_SHIFT_MAX_RIGHT  (LAYER_SRC_W_RIGHT - LAYER_WIN_RIGHT)

#define LAYER_SCREEN_H 240
#define LAYER_SOURCE_H 360
#define LAYER_Y_SHIFT_MAX (LAYER_SOURCE_H - LAYER_SCREEN_H)

#ifdef __cplusplus
}
#endif
