#ifndef GAME2_ASSETS_H
#define GAME2_ASSETS_H

#include <stdint.h>

#define GAME2_CAR_SPR_W               40u
#define GAME2_CAR_SPR_H               40u
#define GAME2_CAR_SPR_BYTES           (GAME2_CAR_SPR_W * GAME2_CAR_SPR_H * 2u)
#define GAME2_OBSTACLE_SPR_W          16u
#define GAME2_OBSTACLE_SPR_H          16u
#define GAME2_OBSTACLE_SPR_BYTES      (GAME2_OBSTACLE_SPR_W * GAME2_OBSTACLE_SPR_H)
#define GAME2_TRAFFIC_SPR_W           20u
#define GAME2_TRAFFIC_SPR_H           20u
#define GAME2_TRAFFIC_SPR_BYTES       (GAME2_TRAFFIC_SPR_W * GAME2_TRAFFIC_SPR_H * 2u)
#define GAME2_HOUSE_SPR_W             32u
#define GAME2_HOUSE_SPR_H             32u
#define GAME2_HOUSE_SPR_BYTES         (GAME2_HOUSE_SPR_W * GAME2_HOUSE_SPR_H)
#define GAME2_CITY_BUILDING_SPR_W     40u
#define GAME2_CITY_BUILDING_SPR_H     40u
#define GAME2_CITY_BUILDING_SPR_BYTES (GAME2_CITY_BUILDING_SPR_W * GAME2_CITY_BUILDING_SPR_H * 2u)

extern const uint8_t car_hard_left_sprite_rgb565[GAME2_CAR_SPR_BYTES];
extern const uint8_t car_left_sprite_rgb565[GAME2_CAR_SPR_BYTES];
extern const uint8_t car_little_left_sprite_rgb565[GAME2_CAR_SPR_BYTES];
extern const uint8_t car_center_sprite_rgb565[GAME2_CAR_SPR_BYTES];
extern const uint8_t car_little_right_sprite_rgb565[GAME2_CAR_SPR_BYTES];
extern const uint8_t car_right_sprite_rgb565[GAME2_CAR_SPR_BYTES];
extern const uint8_t car_hard_right_sprite_rgb565[GAME2_CAR_SPR_BYTES];

extern const uint8_t suv_hard_left_sprite_rgb565[GAME2_CAR_SPR_BYTES];
extern const uint8_t suv_left_sprite_rgb565[GAME2_CAR_SPR_BYTES];
extern const uint8_t suv_little_left_sprite_rgb565[GAME2_CAR_SPR_BYTES];
extern const uint8_t suv_center_sprite_rgb565[GAME2_CAR_SPR_BYTES];
extern const uint8_t suv_little_right_sprite_rgb565[GAME2_CAR_SPR_BYTES];
extern const uint8_t suv_right_sprite_rgb565[GAME2_CAR_SPR_BYTES];
extern const uint8_t suv_hard_right_sprite_rgb565[GAME2_CAR_SPR_BYTES];

extern const uint8_t hatchback_hard_left_sprite_rgb565[GAME2_CAR_SPR_BYTES];
extern const uint8_t hatchback_left_sprite_rgb565[GAME2_CAR_SPR_BYTES];
extern const uint8_t hatchback_little_left_sprite_rgb565[GAME2_CAR_SPR_BYTES];
extern const uint8_t hatchback_center_sprite_rgb565[GAME2_CAR_SPR_BYTES];
extern const uint8_t hatchback_little_right_sprite_rgb565[GAME2_CAR_SPR_BYTES];
extern const uint8_t hatchback_right_sprite_rgb565[GAME2_CAR_SPR_BYTES];
extern const uint8_t hatchback_hard_right_sprite_rgb565[GAME2_CAR_SPR_BYTES];

extern const uint8_t obstacle_traffic_sprite[GAME2_TRAFFIC_SPR_BYTES];
extern const uint8_t obstacle_yellow_sprite[GAME2_OBSTACLE_SPR_BYTES];
extern const uint8_t obstacle_boost_sprite[GAME2_OBSTACLE_SPR_BYTES];

extern const uint8_t roadside_house_red_sprite[GAME2_HOUSE_SPR_BYTES];
extern const uint8_t roadside_house_blue_sprite[GAME2_HOUSE_SPR_BYTES];
extern const uint8_t roadside_house_shop_sprite[GAME2_HOUSE_SPR_BYTES];

extern const uint8_t city_building1_sprite_rgb565[GAME2_CITY_BUILDING_SPR_BYTES];
extern const uint8_t city_building2_sprite_rgb565[GAME2_CITY_BUILDING_SPR_BYTES];
extern const uint8_t city_building3_sprite_rgb565[GAME2_CITY_BUILDING_SPR_BYTES];
extern const uint8_t city_building4_sprite_rgb565[GAME2_CITY_BUILDING_SPR_BYTES];

#endif
