#ifndef APEX_MENU_THUMB_INCLUDED_H
#define APEX_MENU_THUMB_INCLUDED_H

#include <stdint.h>

#define APEX_MENU_THUMB_W 96U
#define APEX_MENU_THUMB_H 58U
#define APEX_MENU_THUMB_TRANSPARENT_INDEX 255U

extern const uint8_t apex_menu_thumb_idx8[APEX_MENU_THUMB_W * APEX_MENU_THUMB_H];
extern const uint16_t apex_menu_thumb_lut565[256];

#endif /* APEX_MENU_THUMB_INCLUDED_H */
