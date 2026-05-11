#include "Game_2.h"
#include "InputHandler.h"
#include "Menu.h"
#include "LCD.h"
#include "Buzzer.h"
#include "Joystick.h"
#include "assets.h"
#include "music.h"
#include "main.h"
#include "stm32l4xx_hal.h"
#include <stdio.h>

extern ST7789V2_cfg_t cfg0;
extern Buzzer_cfg_t buzzer_cfg;  // Buzzer control
extern Joystick_cfg_t joystick_cfg;
extern Joystick_t joystick_data;
extern void WS2812_Show_Pixels(const uint8_t *rgb, uint8_t count, uint8_t brightness);
extern void WS2812_Off(void);

static uint8_t game2_back_pressed(void)
{
    return (current_input.btn1_pressed || current_input.btn3_pressed) ? 1U : 0U;
}

static uint8_t game2_force_console_return = 0U;

static uint8_t game2_pc13_return_requested(uint32_t now)
{
    static uint8_t was_pressed = 0U;
    static uint32_t pressed_ms = 0U;
    uint8_t pressed = (HAL_GPIO_ReadPin(B1_GPIO_Port, B1_Pin) == GPIO_PIN_RESET) ? 1U : 0U;

    if (pressed && !was_pressed) {
        pressed_ms = now;
    }
    was_pressed = pressed;

    if (pressed && (now - pressed_ms) >= 1200U) {
        game2_force_console_return = 1U;
        return 1U;
    }
    return 0U;
}

/**
 * @brief Game 2: Pseudo-3D Survival Racing (fixed-point, no extra framebuffer)
 */

#define GAME2_FRAME_TIME_MS      40u   // Steering/input response baseline used by the simulation
#define GAME2_RENDER_WAIT_MS     16u   // Render pacing cap: lower = smoother if the frame finishes early
#define GAME2_SIM_FALLBACK_MS    40u   // Catch-up chunk on abnormal stalls (limits loop count, keeps wall-clock pace)
#define GAME2_SIM_STEP_MS         5u   // Higher-frequency fixed simulation substep for smoother motion with smaller deltas
#define GAME2_GAMEPLAY_TIME_SCALE_PCT 34u // Preserve pre-optimization driving pace while rendering faster
#define GAME2_VISUAL_TICK_MS     40u   // Animation cadence independent of render FPS

#define Q8_SHIFT                 8      // Fixed-point precision bits (Q8 format)
#define Q8_ONE                   (1 << Q8_SHIFT) // Fixed-point value of 1.0

#define TRACK_SEGMENTS           64     // Road segment buffer size: larger = longer non-repeating track
#define MAX_OBJECTS              14     // Max simultaneous world objects (traffic/hazard/checkpoint/boost)

#define ROAD_HORIZON_Y           (ST7789V2_HEIGHT / 3)      // Horizon line Y: smaller = more sky, larger = more road
#define ROAD_BOTTOM_Y            (ST7789V2_HEIGHT - 1)      // Road render bottom line
#define ROAD_BASE_HALF_WIDTH     28                         // Far-road half width near horizon
#define ROAD_NEAR_HALF_WIDTH     108                        // Near-road half width at screen bottom
#define ROAD_MAX_CENTER_SWAY     84                         // Max road center lateral swing for curves
#define ROAD_SURFACE_COLOR        0                          // Asphalt base: near-black
#define ROAD_SURFACE_SHADE_COLOR 13                          // Asphalt grain: deep gray speckles
#define ROADSIDE_COUNTRY_GRASS_COLOR 3                       // Country roadside base: green
#define ROADSIDE_COUNTRY_MOSAIC_COLOR 0                       // Country roadside texture flecks
#define ROADSIDE_CITY_CEMENT_COLOR 13                         // City roadside base: LCD_COLOUR_13 / RGB565_GREY
#define ROADSIDE_CITY_MOSAIC_COLOR 0                          // City roadside texture flecks: black
#define ROADSIDE_HOUSE_COUNT      12                          // Number of deterministic scenery slots
#define ROADSIDE_HOUSE_SPACING_UNITS 54                       // World spacing between roadside houses
#define ROADSIDE_CITY_BUILDING_COUNT 8                         // Fewer large city towers to balance render cost
#define ROADSIDE_CITY_BUILDING_SPACING_UNITS 84                // Wider city spacing keeps visual density reasonable
#define ROADSIDE_HOUSE_FAR_SIZE_PX 12                          // Far houses match 8x8 pickup/obstacle size
#define ROADSIDE_HOUSE_NEAR_SIZE_PX 96                        // Near houses become larger than the player car
#define CITY_BUILDING_MODULE_FAR_SIZE_PX 20                    // Far city building module size
#define CITY_BUILDING_MODULE_NEAR_SIZE_PX 92                   // Near city building module size
#define CITY_BUILDING_MODULE_OVERLAP_PX 1                      // Hide transparent seams between stacked modules
#define PROJECTED_OBSTACLE_FAR_SIZE_PX 8                      // Matches the original 8x8 sprite at far distance
#define PROJECTED_OBSTACLE_NEAR_SIZE_PX 32                    // Matches the original 4x obstacle near-field size

#define PLAYER_MAX_LANE_Q8       (600)  // Lane clamp limit (left/right reach)
#define PLAYER_EDGE_LANE_Q8      (500)  // Edge zone start: beyond this triggers scrape slowdown/time drain
#define CAMERA_LANE_PIX_DIV      4      // Camera follow strength: smaller = stronger side camera shift
#define STEER_TARGET_Q8          (150)  // Steering target magnitude
#define STEER_RESPONSE_DIV       5      // Steering smoothing: smaller = snappier response
#define STEER_RECENTER_EXTRA_DIV 1      // Extra damping only while returning steering to center
#define STEER_LANE_MOVE_DIV      50     // Sports Car baseline lateral movement divisor
#define CAR_POSE_XPROC_LITTLE    120    // Joystick threshold for slight left/right pose
#define CAR_POSE_XPROC_MID       580    // Joystick threshold for soft left/right pose
#define CAR_POSE_XPROC_HARD      980    // Joystick threshold for hard left/right pose
#define CAR_POSE_FILTER_TC_MS     75u   // Pose smoothing low-pass time constant (ms)
#define CAR_POSE_STEP_MS          18u   // Minimum time between adjacent pose steps (ms)
#define CAR_POSE_CENTER_DEADZONE  90    // Stick deadzone for snap-to-center pose target
#define CAR_POSE_RETURN_TC_MS     30u   // Faster filter while stick is centered
#define CAR_POSE_RETURN_STEP_MS   10u   // Faster pose-step cadence while returning to center

#define CAR_SPR_HD_W             GAME2_CAR_SPR_W // Source sprite width
#define CAR_SPR_HD_H             GAME2_CAR_SPR_H // Source sprite height
#define CAR_RENDER_W             40     // Final on-screen player car width
#define CAR_RENDER_H             40     // Final on-screen player car height
#define CAR_POSE_COUNT           7      // hard-left/left/little-left/middle/little-right/right/hard-right
#define USE_CAR_RGB565_SPRITES   1      // 1 = use RGB565 car sprites
#define RGB565_TRANSPARENT        0x0000u // Transparent color key in car sprites

// Checkpoint pacing/reward tuning (distance-scheduled, not random chance)
#define CHECKPOINT_INTERVAL_BASE_UNITS       260u  // Base distance between checkpoint spawns
#define CHECKPOINT_INTERVAL_STEP_DIST_UNITS  900u  // Progress distance per interval increase step
#define CHECKPOINT_INTERVAL_STEP_UNITS       95u   // Added interval distance per step
#define CHECKPOINT_INTERVAL_MAX_STEPS        10    // Max interval growth steps
#define CHECKPOINT_INTERVAL_MIN_UNITS        180u  // Hard minimum spawn interval
#define CHECKPOINT_SPAWN_JITTER_UNITS        35    // Random spawn offset to reduce predictability

#define CHECKPOINT_REWARD_BASE_MS            15000  // Base time reward when passing checkpoint
#define CHECKPOINT_REWARD_STEP_DIST_UNITS    800u // Progress distance per reward growth step
#define CHECKPOINT_REWARD_STEP_MS            3000   // Extra reward time per step
#define CHECKPOINT_REWARD_MAX_STEPS          10    // Max reward growth steps

#define PLAYER_HP_MAX                    100   // Full health
#define CHECKPOINT_HEAL_BASE_HP          4     // Base HP restored by checkpoint
#define CHECKPOINT_HEAL_STEP_HP          2     // Extra HP restore per progress step
#define CHECKPOINT_HEAL_MAX_STEPS        7     // Max heal growth steps

#define TRAFFIC_HIT_SPEED_NUM            1     // Traffic collision speed multiplier numerator
#define TRAFFIC_HIT_SPEED_DEN            2     // Traffic collision speed multiplier denominator
#define TRAFFIC_HIT_HP_LOSS              32    // HP loss on traffic collision

#define BOOST_TARGET_SPEED_Q8            (240 << Q8_SHIFT) // Normal top speed cap
#define BOOST_ACTIVE_SPEED_Q8            (350 << Q8_SHIFT) // Forced speed while boost is active
#define BOOST_METER_MAX_Q8               (100 << Q8_SHIFT) // Boost meter full scale (100%)
#define BOOST_CHARGE_PER_PICKUP_Q8       (20 << Q8_SHIFT)  // Boost gain per lightning pickup
#define BOOST_DURATION_MS                3000              // Full boost duration when activated

#define YELLOW_HIT_SPEED_NUM             3     // Yellow-hit speed multiplier numerator
#define YELLOW_HIT_SPEED_DEN             4     // Yellow-hit speed multiplier denominator
#define YELLOW_HIT_TIME_PENALTY_MS       1500  // Time loss on yellow obstacle hit

#define SPAWN_TRAFFIC_WEIGHT_PCT         45    // Traffic spawn weight (%)
#define SPAWN_BOOST_WEIGHT_PCT           20    // Boost pickup spawn weight (%)
#define SPAWN_YELLOW_WEIGHT_PCT          35    // Yellow hazard spawn weight (%)

#define HUD_PENALTY_SHOW_MS              900u  // Duration for showing accumulated time-loss text
#define HUD_PENALTY_MAX_SHOW_MS          9900  // Clamp for displayed penalty value
#define PLAYER_FLASH_TOGGLE_MS           70u   // Flash on/off interval after traffic hit
#define PLAYER_FLASH_TOTAL_TOGGLES       4u    // Total flash toggles (2 full blinks)
#define YELLOW_SWAY_TOTAL_MS             240u  // Total shake animation duration after yellow hit
#define YELLOW_SWAY_TOGGLE_MS            60u   // Pose-sweep direction switch interval during shake
#define BOOST_FLAME_SHIFT_LITTLE_PX      3    // Exhaust X shift for little-left/little-right pose (px)
#define BOOST_FLAME_SHIFT_SOFT_PX        6    // Exhaust X shift for left/right pose (px)
#define BOOST_FLAME_SHIFT_HARD_PX        9    // Exhaust X shift for hard-left/hard-right pose (px)

#define GAME2_WS2812_COUNT               10u  // Shared external RGB strip length
#define GAME2_WS2812_BRIGHTNESS_LIMIT     4u  // Very low cap: protects USB/board power when several LEDs are on
#define GAME2_WS2812_SPEED_BRIGHTNESS      3u  // Normal speed bar current limit
#define GAME2_WS2812_FLASH_BRIGHTNESS      4u  // Short event flash current limit
#define GAME2_WS2812_REFRESH_MS          120u  // Avoid blocking the render loop with DMA waits every frame
#define GAME2_WS2812_FLASH_INTERVAL_MS    85u
#define GAME2_WS2812_FLASH_TOGGLES         6u  // 3 visible flashes: on/off/on/off/on/off
#define GAME2_WS2812_READY_INTERVAL_MS   140u
#define GAME2_WS2812_MENU_CHASE_MS       700u  // Match the console menu's low-power single-pixel chase
#define GAME2_WS2812_MENU_BRIGHTNESS       4u

#define GAME2_MENU_ITEMS                  5u   // selectable rows: Start / Map / Difficulty / Vehicle / How To
#define GAME2_HELP_PAGES                  6u   // traffic / yellow / checkpoint / boost / hud-1 / hud-2

#define GAME2_TITLE_TEXT                  "Pseudo-3D Racing"

#define GAME2_RECORD_FLASH_PAGE_ADDR      0x080FF800u  // Last 2KB page on STM32L476RG (1MB flash)
#define GAME2_RECORD_FLASH_MAGIC          0x47325243u  // 'G2RC'

typedef struct {
    int16_t curvature_q8;   // [-128, 128] ~= [-0.5, 0.5]
    uint16_t length_units;  // Segment length in world units
} RoadSegment;

typedef struct {
    int32_t distance_q8;
    int16_t lane_q8;
    uint8_t type;           // 0=car, 1=yellow obstacle, 2=checkpoint, 3=boost item
    uint8_t active;
} Object;

typedef enum {
    GAME2_DIFFICULTY_EASY = 0,
    GAME2_DIFFICULTY_HARD,
    GAME2_DIFFICULTY_EXTREME,
    GAME2_DIFFICULTY_COUNT
} Game2Difficulty;

typedef enum {
    GAME2_VEHICLE_SUV = 0,
    GAME2_VEHICLE_HATCHBACK,
    GAME2_VEHICLE_SPORTS,
    GAME2_VEHICLE_COUNT
} Game2Vehicle;

typedef enum {
    GAME2_MAP_COUNTRY = 0,
    GAME2_MAP_CITY,
    GAME2_MAP_COUNT
} Game2Map;

typedef struct {
    const char *name;
    uint8_t spawn_traffic_pct;
    uint8_t spawn_boost_pct;
    uint8_t spawn_yellow_pct;
    uint16_t checkpoint_interval_scale_pct;
    uint8_t traffic_speed_num;
    uint8_t traffic_speed_den;
    uint16_t traffic_hp_loss;
    uint8_t yellow_speed_num;
    uint8_t yellow_speed_den;
    uint16_t yellow_time_penalty_ms;
    uint16_t curve_strength_pct;
    uint16_t curve_throw_scale_pct;
} Game2DifficultyCfg;

typedef struct {
    const char *name;
    int32_t speed_cap_q8;
    uint8_t steer_response_div;
    uint16_t steer_lane_move_div;
    int16_t hp_max;
    uint8_t traffic_keep_pct;
    uint8_t yellow_keep_pct;
    uint8_t traffic_hp_scale_pct;
    uint8_t yellow_time_scale_pct;
    uint8_t boost_pickup_scale_pct;
    uint8_t ui_speed;
    uint8_t ui_handling;
    uint8_t ui_hp;
    uint8_t ui_armor;
    uint8_t ui_boost;
} Game2VehicleCfg;

static const Game2DifficultyCfg game2_difficulty_cfg[GAME2_DIFFICULTY_COUNT] = {
    {"Easy",    40u, 25u, 35u,  90u, 11u,20u, 22u, 3u, 4u, 1100u,  90u, 115u},
    {"Hard",    50u, 18u, 34u, 225u,  9u,20u, 36u, 7u,10u, 1800u, 135u, 160u},
    {"Extreme", 68u, 12u, 24u, 385u,  7u,20u, 52u,13u,20u, 2600u, 185u, 220u},
};

static const Game2VehicleCfg game2_vehicle_cfg[GAME2_VEHICLE_COUNT] = {
    {"SUV",        (210 << Q8_SHIFT), 6u, 64u, 140, 130u,125u, 70u,  70u, 150u, 35u, 20u, 95u, 95u, 100u},
    {"Hatchback",  (230 << Q8_SHIFT), 4u, 40u,  80,  80u, 80u,130u, 130u, 125u, 70u, 100u, 20u, 30u, 65u},
    {"Sports Car", (240 << Q8_SHIFT), 5u, 50u, 100, 100u,100u, 100u, 100u, 100u, 60u, 60u, 60u, 35u},
};

static const char *game2_map_names[GAME2_MAP_COUNT] = {
    "Country",
    "City",
};

static RoadSegment track[TRACK_SEGMENTS];
static Object objects[MAX_OBJECTS];
static int32_t object_render_distance_q8[MAX_OBJECTS];
static uint8_t object_render_valid[MAX_OBJECTS];
static uint32_t roadside_house_render_id[ROADSIDE_HOUSE_COUNT];
static int32_t roadside_house_render_distance_q8[ROADSIDE_HOUSE_COUNT];
static uint8_t roadside_house_render_valid[ROADSIDE_HOUSE_COUNT];

static uint8_t track_head = 0;
static uint8_t track_tail = 0;
static int32_t segment_progress_q8 = 0;

static int16_t player_lane_q8 = 0;
static int16_t steering_q8 = 0;
static int32_t speed_q8 = 0;

static int32_t time_left_ms = 0;
static uint32_t distance_score_units = 0;
static uint32_t distance_score_q8 = 0;
static uint32_t frame_counter = 0;
static uint32_t visual_tick_accum_ms = 0;
static int16_t player_hp = PLAYER_HP_MAX;
static int32_t hud_penalty_show_ms = 0;
static uint16_t hud_penalty_timer_ms = 0;
static uint32_t checkpoint_next_spawn_units = 0;
static uint16_t player_flash_timer_ms = 0;
static uint16_t yellow_sway_timer_ms = 0;
static uint16_t yellow_sway_step_ms = 0;
static int8_t yellow_sway_dir = 1;
static int16_t player_pose_filtered_x = 0;
static uint8_t player_pose_visual_idx = 3;
static uint16_t player_pose_step_timer_ms = 0;

static uint32_t rng_state = 0xA35F1234u;

static int16_t road_center_cache[ST7789V2_HEIGHT];
static uint16_t road_half_cache[ST7789V2_HEIGHT];

static int32_t road_dist_q8_lut[ST7789V2_HEIGHT];
static int16_t road_delta_units_lut[ST7789V2_HEIGHT];
static int16_t road_bend_gain_q8_lut[ST7789V2_HEIGHT];
static uint8_t road_projection_lut_ready = 0;
static int32_t track_lookup_end_q8[TRACK_SEGMENTS];
static int16_t track_lookup_curve_q8[TRACK_SEGMENTS];

static uint8_t rgb565_to_lcd_index(uint16_t c);
static void init_roadside_house_render_state(void);

static uint8_t city_building_indexed_sprite[4][GAME2_CITY_BUILDING_SPR_W * GAME2_CITY_BUILDING_SPR_H];
static uint8_t city_building_sprite_ready = 0;

#if USE_CAR_RGB565_SPRITES
static uint8_t car_pose_opaque_mask_bits[GAME2_VEHICLE_COUNT][CAR_POSE_COUNT][(CAR_SPR_HD_W * CAR_SPR_HD_H + 7) / 8];
static uint8_t car_pose_opaque_mask_ready[GAME2_VEHICLE_COUNT] = {0};
static uint8_t car_pose_indexed_sprite[CAR_POSE_COUNT][CAR_SPR_HD_W * CAR_SPR_HD_H];
static Game2Vehicle car_pose_indexed_vehicle = GAME2_VEHICLE_COUNT;
static uint8_t car_pose_indexed_ready = 0;
static uint8_t obstacle_traffic_indexed_sprite[GAME2_TRAFFIC_SPR_W * GAME2_TRAFFIC_SPR_H];
static uint8_t obstacle_traffic_dilated_mask[GAME2_TRAFFIC_SPR_W * GAME2_TRAFFIC_SPR_H];
static uint8_t obstacle_yellow_dilated_mask[GAME2_OBSTACLE_SPR_BYTES];
static uint8_t obstacle_boost_dilated_mask[GAME2_OBSTACLE_SPR_BYTES];
static uint8_t obstacle_mask_ready = 0;
#endif

#if USE_CAR_RGB565_SPRITES
// Sprite data moved to game_2/assets.c (declared in game_2/assets.h).
#endif

// Boost system state
static int32_t boost_meter_q8 = 0;
static uint8_t boost_active = 0;
static uint32_t game2_led_next_update_ms = 0;
static uint32_t game2_led_flash_start_ms = 0;
static uint8_t game2_led_flash_active = 0;
static uint8_t game2_led_flash_r = 0;
static uint8_t game2_led_flash_g = 0;
static uint8_t game2_led_flash_b = 0;

static uint32_t buzzer_off_tick = 0;
static uint32_t scrape_beep_tick = 0;
static uint8_t buzzer_active = 0;
static uint8_t game_over = 0;
static Game2Difficulty game2_selected_difficulty = GAME2_DIFFICULTY_HARD;
static Game2Vehicle game2_selected_vehicle = GAME2_VEHICLE_SPORTS;
static Game2Map game2_selected_map = GAME2_MAP_COUNTRY;
static uint32_t game2_best_distance_units = 0;
static uint8_t game2_best_loaded = 0;

static uint8_t rt_spawn_traffic_pct = SPAWN_TRAFFIC_WEIGHT_PCT;
static uint8_t rt_spawn_boost_pct = SPAWN_BOOST_WEIGHT_PCT;
static uint8_t rt_spawn_yellow_pct = SPAWN_YELLOW_WEIGHT_PCT;
static uint16_t rt_checkpoint_interval_scale_pct = 100u;
static uint16_t rt_traffic_hp_loss = TRAFFIC_HIT_HP_LOSS;
static uint16_t rt_yellow_time_penalty_ms = YELLOW_HIT_TIME_PENALTY_MS;
static uint16_t rt_curve_strength_pct = 100u;
static uint16_t rt_curve_throw_scale_pct = 125u;
static int16_t rt_player_hp_max = PLAYER_HP_MAX;
static int32_t rt_speed_cap_q8 = BOOST_TARGET_SPEED_Q8;
static uint8_t rt_steer_response_div = STEER_RESPONSE_DIV;
static uint16_t rt_steer_lane_move_div = STEER_LANE_MOVE_DIV;
static uint8_t rt_traffic_keep_pct = 100u;
static uint8_t rt_yellow_keep_pct = 100u;
static int32_t rt_boost_pickup_q8 = BOOST_CHARGE_PER_PICKUP_Q8;

static const uint8_t* game2_vehicle_pose_sprite(Game2Vehicle vehicle, uint8_t pose_idx) {
    if (pose_idx > 6u) {
        pose_idx = 3u;
    }

    if (vehicle == GAME2_VEHICLE_SUV) {
        if (pose_idx == 0u) return suv_hard_left_sprite_rgb565;
        if (pose_idx == 1u) return suv_left_sprite_rgb565;
        if (pose_idx == 2u) return suv_little_left_sprite_rgb565;
        if (pose_idx == 3u) return suv_center_sprite_rgb565;
        if (pose_idx == 4u) return suv_little_right_sprite_rgb565;
        if (pose_idx == 5u) return suv_right_sprite_rgb565;
        return suv_hard_right_sprite_rgb565;
    }

    if (vehicle == GAME2_VEHICLE_HATCHBACK) {
        if (pose_idx == 0u) return hatchback_hard_left_sprite_rgb565;
        if (pose_idx == 1u) return hatchback_left_sprite_rgb565;
        if (pose_idx == 2u) return hatchback_little_left_sprite_rgb565;
        if (pose_idx == 3u) return hatchback_center_sprite_rgb565;
        if (pose_idx == 4u) return hatchback_little_right_sprite_rgb565;
        if (pose_idx == 5u) return hatchback_right_sprite_rgb565;
        return hatchback_hard_right_sprite_rgb565;
    }

    if (pose_idx == 0u) return car_hard_left_sprite_rgb565;
    if (pose_idx == 1u) return car_left_sprite_rgb565;
    if (pose_idx == 2u) return car_little_left_sprite_rgb565;
    if (pose_idx == 3u) return car_center_sprite_rgb565;
    if (pose_idx == 4u) return car_little_right_sprite_rgb565;
    if (pose_idx == 5u) return car_right_sprite_rgb565;
    return car_hard_right_sprite_rgb565;
}

static const uint8_t* game2_vehicle_hard_left_sprite(Game2Vehicle vehicle) {
    return game2_vehicle_pose_sprite(vehicle, 0u);
}

static void game2_refresh_runtime_tuning(void) {
    const Game2DifficultyCfg *d_cfg;
    const Game2VehicleCfg *v_cfg;
    uint16_t sum;
    uint32_t scaled;
    uint16_t base_keep_pct;

    d_cfg = &game2_difficulty_cfg[game2_selected_difficulty];
    v_cfg = &game2_vehicle_cfg[game2_selected_vehicle];

    rt_spawn_traffic_pct = d_cfg->spawn_traffic_pct;
    rt_spawn_boost_pct = d_cfg->spawn_boost_pct;
    rt_spawn_yellow_pct = d_cfg->spawn_yellow_pct;
    sum = (uint16_t)rt_spawn_traffic_pct + (uint16_t)rt_spawn_boost_pct + (uint16_t)rt_spawn_yellow_pct;
    if (sum != 100u) {
        rt_spawn_yellow_pct = (uint8_t)(100u - rt_spawn_traffic_pct - rt_spawn_boost_pct);
    }

    rt_checkpoint_interval_scale_pct = d_cfg->checkpoint_interval_scale_pct;
    rt_curve_strength_pct = d_cfg->curve_strength_pct;
    rt_curve_throw_scale_pct = d_cfg->curve_throw_scale_pct;

    base_keep_pct = (uint16_t)((100u * d_cfg->traffic_speed_num) / d_cfg->traffic_speed_den);
    rt_traffic_keep_pct = (uint8_t)((base_keep_pct * v_cfg->traffic_keep_pct) / 100u);
    if (rt_traffic_keep_pct < 20u) {
        rt_traffic_keep_pct = 20u;
    }

    base_keep_pct = (uint16_t)((100u * d_cfg->yellow_speed_num) / d_cfg->yellow_speed_den);
    rt_yellow_keep_pct = (uint8_t)((base_keep_pct * v_cfg->yellow_keep_pct) / 100u);
    if (rt_yellow_keep_pct < 20u) {
        rt_yellow_keep_pct = 20u;
    }

    scaled = ((uint32_t)d_cfg->traffic_hp_loss * (uint32_t)v_cfg->traffic_hp_scale_pct) / 100u;
    if (scaled < 1u) {
        scaled = 1u;
    }
    rt_traffic_hp_loss = (uint16_t)scaled;

    scaled = ((uint32_t)d_cfg->yellow_time_penalty_ms * (uint32_t)v_cfg->yellow_time_scale_pct) / 100u;
    if (scaled < 100u) {
        scaled = 100u;
    }
    rt_yellow_time_penalty_ms = (uint16_t)scaled;

    rt_player_hp_max = v_cfg->hp_max;
    rt_speed_cap_q8 = v_cfg->speed_cap_q8;
    rt_steer_response_div = v_cfg->steer_response_div;
    rt_steer_lane_move_div = v_cfg->steer_lane_move_div;
    rt_boost_pickup_q8 = ((BOOST_CHARGE_PER_PICKUP_Q8 * (int32_t)v_cfg->boost_pickup_scale_pct) / 100);
    if (rt_boost_pickup_q8 < (5 << Q8_SHIFT)) {
        rt_boost_pickup_q8 = (5 << Q8_SHIFT);
    }
}

static void game2_apply_difficulty(Game2Difficulty difficulty) {
    if ((uint8_t)difficulty >= GAME2_DIFFICULTY_COUNT) {
        difficulty = GAME2_DIFFICULTY_HARD;
    }
    game2_selected_difficulty = difficulty;
    game2_refresh_runtime_tuning();
}

static void game2_apply_vehicle(Game2Vehicle vehicle) {
    if ((uint8_t)vehicle >= GAME2_VEHICLE_COUNT) {
        vehicle = GAME2_VEHICLE_SPORTS;
    }
    game2_selected_vehicle = vehicle;
#if USE_CAR_RGB565_SPRITES
    car_pose_indexed_ready = 0u;
#endif
    game2_refresh_runtime_tuning();
}

static void game2_apply_map(Game2Map map) {
    if ((uint8_t)map >= GAME2_MAP_COUNT) {
        map = GAME2_MAP_CITY;
    }
    game2_selected_map = map;
    init_roadside_house_render_state();
}

static void game2_flash_load_best_record(void) {
    uint64_t raw0;
    uint64_t raw1;
    uint32_t magic;
    uint32_t best;

    if (game2_best_loaded) {
        return;
    }

    raw0 = *(const uint64_t*)GAME2_RECORD_FLASH_PAGE_ADDR;
    raw1 = *(const uint64_t*)(GAME2_RECORD_FLASH_PAGE_ADDR + 8u);

    if (raw1 == (~raw0)) {
        magic = (uint32_t)(raw0 >> 32);
        best = (uint32_t)(raw0 & 0xFFFFFFFFu);
        if (magic == GAME2_RECORD_FLASH_MAGIC) {
            game2_best_distance_units = best;
        }
    }

    game2_best_loaded = 1u;
}

static void game2_flash_write_best_record(uint32_t best_distance_units) {
    HAL_StatusTypeDef st;
    FLASH_EraseInitTypeDef erase;
    uint32_t page_error = 0u;
    uint64_t raw0 = (((uint64_t)GAME2_RECORD_FLASH_MAGIC) << 32) | (uint64_t)best_distance_units;
    uint64_t raw1 = ~raw0;

    HAL_FLASH_Unlock();

    erase.TypeErase = FLASH_TYPEERASE_PAGES;
    erase.Banks = FLASH_BANK_2;
    erase.Page = 255u;
    erase.NbPages = 1u;

    st = HAL_FLASHEx_Erase(&erase, &page_error);
    if (st == HAL_OK) {
        st = HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, GAME2_RECORD_FLASH_PAGE_ADDR, raw0);
    }
    if (st == HAL_OK) {
        st = HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, GAME2_RECORD_FLASH_PAGE_ADDR + 8u, raw1);
    }

    HAL_FLASH_Lock();
}

static void game2_commit_best_distance_if_needed(uint32_t distance_units) {
    if (distance_units > game2_best_distance_units) {
        game2_best_distance_units = distance_units;
        game2_flash_write_best_record(game2_best_distance_units);
    }
}

static void push_time_penalty_feedback(int32_t penalty_ms) {
    if (penalty_ms <= 0) {
        return;
    }

    hud_penalty_show_ms += penalty_ms;
    if (hud_penalty_show_ms > HUD_PENALTY_MAX_SHOW_MS) {
        hud_penalty_show_ms = HUD_PENALTY_MAX_SHOW_MS;
    }
    hud_penalty_timer_ms = HUD_PENALTY_SHOW_MS;
}

static uint8_t game2_ws2812_limit_brightness(uint8_t brightness) {
    if (brightness > GAME2_WS2812_BRIGHTNESS_LIMIT) {
        brightness = GAME2_WS2812_BRIGHTNESS_LIMIT;
    }
    return brightness;
}

static void game2_ws2812_fill(uint8_t pixels[GAME2_WS2812_COUNT * 3u],
                              uint8_t r,
                              uint8_t g,
                              uint8_t b) {
    for (uint8_t i = 0; i < GAME2_WS2812_COUNT; i++) {
        pixels[(uint16_t)i * 3u] = r;
        pixels[(uint16_t)i * 3u + 1u] = g;
        pixels[(uint16_t)i * 3u + 2u] = b;
    }
}

static void game2_ws2812_show_solid(uint8_t r, uint8_t g, uint8_t b, uint8_t brightness) {
    uint8_t pixels[GAME2_WS2812_COUNT * 3u];
    game2_ws2812_fill(pixels, r, g, b);
    WS2812_Show_Pixels(pixels, GAME2_WS2812_COUNT, game2_ws2812_limit_brightness(brightness));
}

static void game2_ws2812_show_menu_chase(uint32_t now) {
    static uint8_t phase = 0u;
    uint8_t pixels[GAME2_WS2812_COUNT * 3u];

    if ((int32_t)(now - game2_led_next_update_ms) < 0) {
        return;
    }

    game2_ws2812_fill(pixels, 0u, 0u, 0u);
    pixels[(uint16_t)phase * 3u] = 255u;
    pixels[(uint16_t)phase * 3u + 1u] = 255u;
    pixels[(uint16_t)phase * 3u + 2u] = 255u;

    WS2812_Show_Pixels(pixels, GAME2_WS2812_COUNT,
                       game2_ws2812_limit_brightness(GAME2_WS2812_MENU_BRIGHTNESS));

    phase = (uint8_t)((phase + 1u) % GAME2_WS2812_COUNT);
    game2_led_next_update_ms = now + GAME2_WS2812_MENU_CHASE_MS;
}

static void game2_ws2812_start_flash(uint8_t r, uint8_t g, uint8_t b) {
    game2_led_flash_active = 1u;
    game2_led_flash_start_ms = HAL_GetTick();
    game2_led_flash_r = r;
    game2_led_flash_g = g;
    game2_led_flash_b = b;
    game2_led_next_update_ms = 0u;
}

static void game2_ws2812_reset_state(void) {
    game2_led_flash_active = 0u;
    game2_led_flash_start_ms = 0u;
    game2_led_flash_r = 0u;
    game2_led_flash_g = 0u;
    game2_led_flash_b = 0u;
    game2_led_next_update_ms = 0u;
    WS2812_Off();
}

static void game2_ws2812_show_speed_bar(void) {
    uint8_t pixels[GAME2_WS2812_COUNT * 3u];
    int32_t min_speed_q8 = (60 << Q8_SHIFT);
    int32_t max_speed_q8 = boost_active ? BOOST_ACTIVE_SPEED_Q8 : rt_speed_cap_q8;
    int32_t speed_range_q8 = max_speed_q8 - min_speed_q8;
    int32_t speed_over_min_q8 = speed_q8 - min_speed_q8;
    uint8_t lit_count = 0u;

    game2_ws2812_fill(pixels, 0u, 0u, 0u);

    if (speed_range_q8 <= 0) {
        speed_range_q8 = Q8_ONE;
    }
    if (speed_over_min_q8 < 0) {
        speed_over_min_q8 = 0;
    }

    lit_count = (uint8_t)((speed_over_min_q8 * GAME2_WS2812_COUNT + speed_range_q8 - 1) / speed_range_q8);
    if (lit_count > GAME2_WS2812_COUNT) {
        lit_count = GAME2_WS2812_COUNT;
    }

    for (uint8_t i = 0; i < lit_count; i++) {
        uint8_t r = 0u;
        uint8_t g = 255u;
        uint8_t b = 0u;

        if (boost_active) {
            b = 255u;
            g = 70u;
        } else if (i >= 8u) {
            r = 255u;
            g = 0u;
        } else if (i >= 5u) {
            r = 255u;
            g = 180u;
        }

        pixels[(uint16_t)i * 3u] = r;
        pixels[(uint16_t)i * 3u + 1u] = g;
        pixels[(uint16_t)i * 3u + 2u] = b;
    }

    WS2812_Show_Pixels(pixels, GAME2_WS2812_COUNT,
                       game2_ws2812_limit_brightness(GAME2_WS2812_SPEED_BRIGHTNESS));
}

static void game2_ws2812_show_boost_ready(uint32_t now) {
    uint8_t pixels[GAME2_WS2812_COUNT * 3u];
    uint8_t swap = (uint8_t)((now / GAME2_WS2812_READY_INTERVAL_MS) & 1u);

    for (uint8_t i = 0; i < GAME2_WS2812_COUNT; i++) {
        uint8_t red_side = (uint8_t)(((i & 1u) ^ swap) == 0u);
        pixels[(uint16_t)i * 3u] = red_side ? 255u : 0u;
        pixels[(uint16_t)i * 3u + 1u] = 0u;
        pixels[(uint16_t)i * 3u + 2u] = red_side ? 0u : 255u;
    }

    WS2812_Show_Pixels(pixels, GAME2_WS2812_COUNT,
                       game2_ws2812_limit_brightness(GAME2_WS2812_FLASH_BRIGHTNESS));
}

static void game2_ws2812_update(uint32_t now) {
    if ((int32_t)(now - game2_led_next_update_ms) < 0) {
        return;
    }

    if (game2_led_flash_active) {
        uint32_t elapsed = now - game2_led_flash_start_ms;
        uint32_t phase = elapsed / GAME2_WS2812_FLASH_INTERVAL_MS;

        if (phase >= GAME2_WS2812_FLASH_TOGGLES) {
            game2_led_flash_active = 0u;
            game2_led_next_update_ms = now;
        } else {
            if ((phase & 1u) == 0u) {
                game2_ws2812_show_solid(game2_led_flash_r,
                                        game2_led_flash_g,
                                        game2_led_flash_b,
                                        GAME2_WS2812_FLASH_BRIGHTNESS);
            } else {
                WS2812_Off();
            }
            game2_led_next_update_ms = game2_led_flash_start_ms +
                ((phase + 1u) * GAME2_WS2812_FLASH_INTERVAL_MS);
            return;
        }
    }

    if (game_over) {
        WS2812_Off();
        game2_led_next_update_ms = now + 500u;
    } else if (!boost_active && boost_meter_q8 >= BOOST_METER_MAX_Q8) {
        game2_ws2812_show_boost_ready(now);
        game2_led_next_update_ms = now + GAME2_WS2812_READY_INTERVAL_MS;
    } else {
        game2_ws2812_show_speed_bar();
        game2_led_next_update_ms = now + GAME2_WS2812_REFRESH_MS;
    }
}

static void boost_add_charge_q8(int32_t add_q8) {
    int32_t before_q8 = boost_meter_q8;

    boost_meter_q8 += add_q8;
    if (boost_meter_q8 > BOOST_METER_MAX_Q8) {
        boost_meter_q8 = BOOST_METER_MAX_Q8;
    }

    if (before_q8 < BOOST_METER_MAX_Q8 && boost_meter_q8 >= BOOST_METER_MAX_Q8) {
        Game2Music_PlayBoostReadySfx();
        game2_led_next_update_ms = 0u;
    }
}

static uint8_t boost_meter_percent(void) {
    int32_t pct = (boost_meter_q8 + (Q8_ONE / 2)) >> Q8_SHIFT;
    if (pct < 0) {
        pct = 0;
    }
    if (pct > 100) {
        pct = 100;
    }
    return (uint8_t)pct;
}

static int16_t projected_y_from_distance_units(int32_t dist_units) {
    if (dist_units < 8) {
        dist_units = 8;
    }
    if (dist_units > 620) {
        dist_units = 620;
    }
    return (int16_t)(ROAD_HORIZON_Y + (1800 / dist_units));
}

static int16_t projected_road_half_from_y(int16_t y) {
    int16_t span = (ROAD_BOTTOM_Y - ROAD_HORIZON_Y);
    int16_t depth;

    if (y < ROAD_HORIZON_Y) {
        y = ROAD_HORIZON_Y;
    }
    if (y > ROAD_BOTTOM_Y) {
        y = ROAD_BOTTOM_Y;
    }

    depth = (int16_t)(y - ROAD_HORIZON_Y + 1);
    return (int16_t)(ROAD_BASE_HALF_WIDTH + ((int32_t)(ROAD_NEAR_HALF_WIDTH - ROAD_BASE_HALF_WIDTH) * depth) / span);
}

static int16_t lane_q8_to_road_px(int16_t lane_q8, int16_t road_half_px) {
    int16_t usable_half = (int16_t)(road_half_px - 3);
    if (usable_half < 1) {
        usable_half = 1;
    }
    return (int16_t)(((int32_t)lane_q8 * usable_half) / PLAYER_MAX_LANE_Q8);
}

static void init_road_projection_lut(void) {
    const int16_t span = (ROAD_BOTTOM_Y - ROAD_HORIZON_Y);
    int32_t prev_dist_q8;

    if (road_projection_lut_ready) {
        return;
    }

    prev_dist_q8 = ((int32_t)(620 << Q8_SHIFT) / (span + 1)) + (6 << Q8_SHIFT);
    for (int16_t y = ROAD_BOTTOM_Y; y >= ROAD_HORIZON_Y; y--) {
        int16_t depth = (int16_t)(y - ROAD_HORIZON_Y + 1);
        int32_t dist_q8 = ((int32_t)(620 << Q8_SHIFT) / depth) + (6 << Q8_SHIFT);
        int32_t delta_units = (dist_q8 - prev_dist_q8) >> Q8_SHIFT;

        if (delta_units < 1) {
            delta_units = 1;
        }

        road_dist_q8_lut[y] = dist_q8;
        road_delta_units_lut[y] = (int16_t)delta_units;
        road_bend_gain_q8_lut[y] = (int16_t)(128 + ((int32_t)depth * 240) / span);
        prev_dist_q8 = dist_q8;
    }

    road_projection_lut_ready = 1;
}

static void draw_lane_marker_triplet(int16_t x, int16_t y) {
    if (y < 0 || y >= ST7789V2_HEIGHT) {
        return;
    }

    if (x > 0 && x < ST7789V2_WIDTH) {
        LCD_Set_Pixel((uint16_t)(x - 1), (uint16_t)y, 1);
    }
    if (x >= 0 && x < ST7789V2_WIDTH) {
        LCD_Set_Pixel((uint16_t)x, (uint16_t)y, 1);
    }
    if (x >= 0 && x < ST7789V2_WIDTH - 1) {
        LCD_Set_Pixel((uint16_t)(x + 1), (uint16_t)y, 1);
    }
}

 

static uint8_t pose_index_from_x_processed(int16_t pose_x) {
    if (pose_x <= -CAR_POSE_XPROC_HARD) {
        return 0u;
    }
    if (pose_x <= -CAR_POSE_XPROC_MID) {
        return 1u;
    }
    if (pose_x <= -CAR_POSE_XPROC_LITTLE) {
        return 2u;
    }
    if (pose_x >= CAR_POSE_XPROC_HARD) {
        return 6u;
    }
    if (pose_x >= CAR_POSE_XPROC_MID) {
        return 5u;
    }
    if (pose_x >= CAR_POSE_XPROC_LITTLE) {
        return 4u;
    }
    return 3u;
}

static void update_player_pose_smoothing(uint16_t dt_ms) {
    int32_t dx;
    int32_t step;
    int16_t raw_x;
    int16_t raw_abs;
    uint8_t target_pose;
    uint16_t tc_ms = CAR_POSE_FILTER_TC_MS;
    uint16_t step_ms = CAR_POSE_STEP_MS;

    if (dt_ms == 0u) {
        return;
    }

    raw_x = joystick_data.x_processed;
    raw_abs = (raw_x < 0) ? (int16_t)(-raw_x) : raw_x;

    // Center return path: reduce damping so the pose does not linger at little-left/right.
    if (raw_abs <= CAR_POSE_CENTER_DEADZONE) {
        tc_ms = CAR_POSE_RETURN_TC_MS;
        step_ms = CAR_POSE_RETURN_STEP_MS;
    }

    dx = (int32_t)raw_x - (int32_t)player_pose_filtered_x;
    step = (dx * (int32_t)dt_ms) / (int32_t)tc_ms;
    if (dx > 0 && step > dx) {
        step = dx;
    } else if (dx < 0 && step < dx) {
        step = dx;
    }
    player_pose_filtered_x = (int16_t)((int32_t)player_pose_filtered_x + step);

    if (raw_abs <= CAR_POSE_CENTER_DEADZONE) {
        target_pose = 3u;
    } else {
        target_pose = pose_index_from_x_processed(player_pose_filtered_x);
    }

    player_pose_step_timer_ms = (uint16_t)(player_pose_step_timer_ms + dt_ms);

    while (player_pose_step_timer_ms >= step_ms) {
        if (player_pose_visual_idx < target_pose) {
            player_pose_visual_idx++;
        } else if (player_pose_visual_idx > target_pose) {
            player_pose_visual_idx--;
        } else {
            break;
        }
        player_pose_step_timer_ms = (uint16_t)(player_pose_step_timer_ms - step_ms);
    }
}

static uint8_t player_pose_index_current(void) {
    uint8_t pose_idx = player_pose_visual_idx;

    if (yellow_sway_timer_ms > 0u) {
        uint8_t sway_phase = 6;
        if (YELLOW_SWAY_TOGGLE_MS > 0u) {
            sway_phase = (uint8_t)((yellow_sway_step_ms * 7u) / YELLOW_SWAY_TOGGLE_MS);
            if (sway_phase > 6u) {
                sway_phase = 6u;
            }
        }
        pose_idx = (yellow_sway_dir < 0) ? (uint8_t)(6u - sway_phase) : sway_phase;
    }

    return pose_idx;
}

static const uint8_t* player_car_sprite_by_pose(uint8_t pose_idx) {
    return game2_vehicle_pose_sprite(game2_selected_vehicle, pose_idx);
}

static int16_t projected_sprite_size_from_y(int16_t y, int16_t far_size_px, int16_t near_size_px) {
    int16_t span = (ROAD_BOTTOM_Y - ROAD_HORIZON_Y);
    int16_t depth;

    if (y < ROAD_HORIZON_Y) {
        y = ROAD_HORIZON_Y;
    }
    if (y > ROAD_BOTTOM_Y) {
        y = ROAD_BOTTOM_Y;
    }

    depth = (int16_t)(y - ROAD_HORIZON_Y + 1);
    return (int16_t)(far_size_px + (((int32_t)(near_size_px - far_size_px) * depth) / span));
}

static int32_t smooth_render_distance_q8(int32_t previous_q8, int32_t target_q8) {
    int32_t render_q8 = target_q8;
    int32_t moved_q8 = previous_q8 - target_q8;
    int32_t lag_q8;
    int32_t end_units = target_q8 >> Q8_SHIFT;
    int32_t max_step_q8 = (5 << Q8_SHIFT);
    int32_t max_lag_q8 = (10 << Q8_SHIFT);

    // Slot reused for a far new object: snap to target to avoid stale trail.
    if (moved_q8 < 0 || moved_q8 > (140 << Q8_SHIFT)) {
        return target_q8;
    }

    // Near-field: tighten per-frame render step so approach looks continuous.
    if (end_units < 100) {
        max_step_q8 = (2 << Q8_SHIFT);
        max_lag_q8 = (4 << Q8_SHIFT);
    } else if (end_units < 160) {
        max_step_q8 = (3 << Q8_SHIFT);
        max_lag_q8 = (6 << Q8_SHIFT);
    }

    if (moved_q8 > max_step_q8) {
        render_q8 = previous_q8 - max_step_q8;
    }

    // Avoid visual lag growing too large, otherwise objects can be removed
    // by gameplay while their smoothed render position is still far away.
    lag_q8 = render_q8 - target_q8;
    if (lag_q8 > max_lag_q8) {
        render_q8 = target_q8 + max_lag_q8;
    }

    // In very near field, prioritize correctness over smoothing.
    if (end_units < 56) {
        render_q8 = target_q8;
    }

    return render_q8;
}

static const uint8_t* obstacle_dilated_mask_by_type(uint8_t type) {
    if (type == 0u) {
        return obstacle_traffic_dilated_mask;
    }
    if (type == 1u) {
        return obstacle_yellow_dilated_mask;
    }
    if (type == 3u) {
        return obstacle_boost_dilated_mask;
    }
    return 0;
}

static void build_indexed_sprite_from_rgb565(const uint8_t *src, uint8_t *dst, uint16_t pixels) {
    for (uint16_t i = 0; i < pixels; i++) {
        uint32_t pos = (uint32_t)i * 2u;
        uint16_t c = (uint16_t)((((uint16_t)src[pos]) << 8) | src[pos + 1u]);
        dst[i] = (c == RGB565_TRANSPARENT) ? 255u : rgb565_to_lcd_index(c);
    }
}

static uint8_t rgb565_to_city_building_index(uint16_t c) {
    uint8_t idx = rgb565_to_lcd_index(c);

    if (idx == 12u) {
        uint8_t r = (uint8_t)((c >> 11) & 0x1Fu);
        uint8_t g = (uint8_t)((c >> 5) & 0x3Fu);
        uint8_t b = (uint8_t)(c & 0x1Fu);
        uint16_t luma = (uint16_t)((uint16_t)r * 2u + (uint16_t)g + (uint16_t)b * 2u);

        return (luma < 92u) ? 11u : 13u;
    }

    return idx;
}

static void build_city_building_indexed_sprite(const uint8_t *src, uint8_t *dst, uint16_t pixels) {
    for (uint16_t i = 0; i < pixels; i++) {
        uint32_t pos = (uint32_t)i * 2u;
        uint16_t c = (uint16_t)((((uint16_t)src[pos]) << 8) | src[pos + 1u]);
        dst[i] = (c == RGB565_TRANSPARENT) ? 255u : rgb565_to_city_building_index(c);
    }
}

static void init_city_building_sprites(void) {
    if (city_building_sprite_ready) {
        return;
    }

    build_city_building_indexed_sprite(city_building1_sprite_rgb565,
                                       city_building_indexed_sprite[0],
                                       (uint16_t)(GAME2_CITY_BUILDING_SPR_W * GAME2_CITY_BUILDING_SPR_H));
    build_city_building_indexed_sprite(city_building2_sprite_rgb565,
                                       city_building_indexed_sprite[1],
                                       (uint16_t)(GAME2_CITY_BUILDING_SPR_W * GAME2_CITY_BUILDING_SPR_H));
    build_city_building_indexed_sprite(city_building3_sprite_rgb565,
                                       city_building_indexed_sprite[2],
                                       (uint16_t)(GAME2_CITY_BUILDING_SPR_W * GAME2_CITY_BUILDING_SPR_H));
    build_city_building_indexed_sprite(city_building4_sprite_rgb565,
                                       city_building_indexed_sprite[3],
                                       (uint16_t)(GAME2_CITY_BUILDING_SPR_W * GAME2_CITY_BUILDING_SPR_H));
    city_building_sprite_ready = 1u;
}

static void build_obstacle_dilated_mask(const uint8_t *src, uint8_t *dst, int16_t spr_w, int16_t spr_h) {
    for (int16_t y = 0; y < spr_h; y++) {
        for (int16_t x = 0; x < spr_w; x++) {
            uint8_t filled = 0;
            for (int16_t oy = -1; oy <= 1 && !filled; oy++) {
                int16_t sy = (int16_t)(y + oy);
                if (sy < 0 || sy >= spr_h) {
                    continue;
                }
                for (int16_t ox = -1; ox <= 1; ox++) {
                    int16_t sx = (int16_t)(x + ox);
                    if (sx < 0 || sx >= spr_w) {
                        continue;
                    }
                    if (src[sy * spr_w + sx] != 255u) {
                        filled = 1;
                        break;
                    }
                }
            }
            dst[y * spr_w + x] = filled;
        }
    }
}

static void car_mask_set(Game2Vehicle vehicle, uint8_t pose, uint16_t pix_idx, uint8_t opaque) {
    uint16_t byte_idx = (uint16_t)(pix_idx >> 3);
    uint8_t bit = (uint8_t)(1u << (pix_idx & 0x07u));
    if (opaque) {
        car_pose_opaque_mask_bits[vehicle][pose][byte_idx] |= bit;
    } else {
        car_pose_opaque_mask_bits[vehicle][pose][byte_idx] &= (uint8_t)(~bit);
    }
}

static uint8_t car_mask_get(Game2Vehicle vehicle, uint8_t pose, uint16_t pix_idx) {
    uint16_t byte_idx = (uint16_t)(pix_idx >> 3);
    uint8_t bit = (uint8_t)(1u << (pix_idx & 0x07u));
    return (uint8_t)((car_pose_opaque_mask_bits[vehicle][pose][byte_idx] & bit) != 0u);
}

static void init_collision_masks(void) {
#if USE_CAR_RGB565_SPRITES
    if (!car_pose_opaque_mask_ready[game2_selected_vehicle]) {
        for (uint8_t pose = 0; pose < CAR_POSE_COUNT; pose++) {
            const uint8_t *spr = game2_vehicle_pose_sprite(game2_selected_vehicle, pose);
            for (uint16_t b = 0; b < (CAR_SPR_HD_W * CAR_SPR_HD_H + 7) / 8; b++) {
                car_pose_opaque_mask_bits[game2_selected_vehicle][pose][b] = 0u;
            }
            for (uint16_t i = 0; i < (CAR_SPR_HD_W * CAR_SPR_HD_H); i++) {
                uint32_t pos = (uint32_t)i * 2u;
                uint16_t c = (uint16_t)((((uint16_t)spr[pos]) << 8) | spr[pos + 1u]);
                car_mask_set(game2_selected_vehicle, pose, i, (uint8_t)(c != RGB565_TRANSPARENT));
            }
        }
        car_pose_opaque_mask_ready[game2_selected_vehicle] = 1u;
    }

    if (!obstacle_mask_ready) {
        build_indexed_sprite_from_rgb565(obstacle_traffic_sprite,
                                         obstacle_traffic_indexed_sprite,
                                         (uint16_t)(GAME2_TRAFFIC_SPR_W * GAME2_TRAFFIC_SPR_H));
        build_obstacle_dilated_mask(obstacle_traffic_indexed_sprite,
                                    obstacle_traffic_dilated_mask,
                                    (int16_t)GAME2_TRAFFIC_SPR_W,
                                    (int16_t)GAME2_TRAFFIC_SPR_H);
        build_obstacle_dilated_mask(obstacle_yellow_sprite,
                                    obstacle_yellow_dilated_mask,
                                    (int16_t)GAME2_OBSTACLE_SPR_W,
                                    (int16_t)GAME2_OBSTACLE_SPR_H);
        build_obstacle_dilated_mask(obstacle_boost_sprite,
                                    obstacle_boost_dilated_mask,
                                    (int16_t)GAME2_OBSTACLE_SPR_W,
                                    (int16_t)GAME2_OBSTACLE_SPR_H);
        obstacle_mask_ready = 1u;
    }
#endif
}

static uint8_t sprite_collision_player_object_at_distance(const Object* obj, int32_t obj_distance_q8) {
    const uint8_t* obj_sprite = obstacle_dilated_mask_by_type(obj->type);
    uint8_t pose_idx;
    int32_t dist_units;
    int16_t y;
    int16_t center;
    int16_t half;
    int16_t x;
    int16_t obj_size;
    int16_t obj_x;
    int16_t obj_y;
    int16_t obj_w;
    int16_t obj_h;
    int16_t obj_src_w = (int16_t)GAME2_OBSTACLE_SPR_W;
    int16_t obj_src_h = (int16_t)GAME2_OBSTACLE_SPR_H;
    int16_t car_x;
    int16_t car_y;
    int16_t car_w;
    int16_t car_h;
    int16_t left;
    int16_t top;
    int16_t right;
    int16_t bottom;

    if (obj_sprite == 0) {
        return 0;
    }
    if (obj->type == 0u) {
        obj_src_w = (int16_t)GAME2_TRAFFIC_SPR_W;
        obj_src_h = (int16_t)GAME2_TRAFFIC_SPR_H;
    }

    init_collision_masks();

    dist_units = obj_distance_q8 >> Q8_SHIFT;
    if (dist_units < 8 || dist_units > 620) {
        return 0;
    }

    y = projected_y_from_distance_units(dist_units);
    if (y <= ROAD_HORIZON_Y || y >= ROAD_BOTTOM_Y - 2) {
        return 0;
    }

    center = road_center_cache[y];
    half = (int16_t)road_half_cache[y];
    if (half <= 0) {
        // Fallback for early frames before road cache is populated.
        half = projected_road_half_from_y(y);
        center = (int16_t)(ST7789V2_WIDTH / 2);
    }

    x = (int16_t)(center + lane_q8_to_road_px(obj->lane_q8, half));
    obj_size = projected_sprite_size_from_y(y, PROJECTED_OBSTACLE_FAR_SIZE_PX, PROJECTED_OBSTACLE_NEAR_SIZE_PX);

    obj_x = (int16_t)(x - (obj_size / 2));
    obj_y = (int16_t)(y - (obj_size / 2));
    obj_w = obj_size;
    obj_h = obj_size;

    pose_idx = player_pose_index_current();
    car_x = (ST7789V2_WIDTH / 2) - (int16_t)(CAR_RENDER_W / 2);
    car_y = (int16_t)(ST7789V2_HEIGHT - CAR_RENDER_H - 2);
    car_w = (int16_t)CAR_RENDER_W;
    car_h = (int16_t)CAR_RENDER_H;

    left = (car_x > obj_x) ? car_x : obj_x;
    top = (car_y > obj_y) ? car_y : obj_y;
    right = (int16_t)(((car_x + car_w - 1) < (obj_x + obj_w - 1)) ? (car_x + car_w - 1) : (obj_x + obj_w - 1));
    bottom = (int16_t)(((car_y + car_h - 1) < (obj_y + obj_h - 1)) ? (car_y + car_h - 1) : (obj_y + obj_h - 1));

    if (left > right || top > bottom) {
        return 0;
    }

    for (int16_t sy = top; sy <= bottom; sy++) {
        int16_t car_py = (int16_t)(((int32_t)(sy - car_y) * CAR_SPR_HD_H) / car_h);
        int16_t obj_py = (int16_t)(((int32_t)(sy - obj_y) * obj_src_h) / obj_h);
        if (car_py < 0 || car_py >= (int16_t)CAR_SPR_HD_H || obj_py < 0 || obj_py >= obj_src_h) {
            continue;
        }

        for (int16_t sx = left; sx <= right; sx++) {
            int16_t car_px = (int16_t)(((int32_t)(sx - car_x) * CAR_SPR_HD_W) / car_w);
            int16_t obj_px = (int16_t)(((int32_t)(sx - obj_x) * obj_src_w) / obj_w);

            if (car_px < 0 || car_px >= (int16_t)CAR_SPR_HD_W || obj_px < 0 || obj_px >= obj_src_w) {
                continue;
            }

            if (!car_mask_get(game2_selected_vehicle, pose_idx, (uint16_t)(car_py * CAR_SPR_HD_W + car_px))) {
                continue;
            }

            if (obj_sprite[obj_py * obj_src_w + obj_px]) {
                return 1;
            }
        }
    }

    return 0;
}

static uint32_t rand_u32(void) {
    rng_state = (rng_state * 1664525u) + 1013904223u;
    return rng_state;
}

static int32_t rand_range(int32_t min_v, int32_t max_v) {
    uint32_t span = (uint32_t)(max_v - min_v + 1);
    return min_v + (int32_t)(rand_u32() % span);
}

static int16_t clamp_s16(int16_t v, int16_t min_v, int16_t max_v) {
    if (v < min_v) return min_v;
    if (v > max_v) return max_v;
    return v;
}

static int32_t clamp_s32(int32_t v, int32_t min_v, int32_t max_v) {
    if (v < min_v) return min_v;
    if (v > max_v) return max_v;
    return v;
}

static int32_t abs_s32(int32_t v) {
    return (v < 0) ? -v : v;
}

#if USE_CAR_RGB565_SPRITES
static const uint16_t palette_rgb565_ref[16] = {
    LCD_COLOUR_0,  LCD_COLOUR_1,  LCD_COLOUR_2,  LCD_COLOUR_3,
    LCD_COLOUR_4,  LCD_COLOUR_5,  LCD_COLOUR_6,  LCD_COLOUR_7,
    LCD_COLOUR_8,  LCD_COLOUR_9,  LCD_COLOUR_10, LCD_COLOUR_11,
    LCD_COLOUR_12, LCD_COLOUR_13, LCD_COLOUR_14, LCD_COLOUR_15
};

static uint8_t rgb565_to_lcd_index(uint16_t c) {
    int32_t cr = (int32_t)((c >> 11) & 0x1Fu);
    int32_t cg = (int32_t)((c >> 5) & 0x3Fu);
    int32_t cb = (int32_t)(c & 0x1Fu);
    uint8_t best_idx = 0;
    uint32_t best_d = 0xFFFFFFFFu;

    for (uint8_t i = 0; i < 16; i++) {
        uint16_t p = (uint16_t)((palette_rgb565_ref[i] >> 8) | (palette_rgb565_ref[i] << 8));
        int32_t pr = (int32_t)((p >> 11) & 0x1Fu);
        int32_t pg = (int32_t)((p >> 5) & 0x3Fu);
        int32_t pb = (int32_t)(p & 0x1Fu);
        int32_t dr = cr - pr;
        int32_t dg = cg - pg;
        int32_t db = cb - pb;
        uint32_t dist = (uint32_t)(dr * dr + dg * dg + db * db);

        if (dist < best_d) {
            best_d = dist;
            best_idx = i;
        }
    }

    return best_idx;
}

static const uint8_t* game2_player_car_indexed_sprite(uint8_t pose_idx) {
    if (!car_pose_indexed_ready || car_pose_indexed_vehicle != game2_selected_vehicle) {
        for (uint8_t pose = 0u; pose < CAR_POSE_COUNT; pose++) {
            const uint8_t *src = game2_vehicle_pose_sprite(game2_selected_vehicle, pose);
            for (uint16_t i = 0u; i < (CAR_SPR_HD_W * CAR_SPR_HD_H); i++) {
                uint32_t idx = (uint32_t)i * 2u;
                uint16_t c = (uint16_t)((((uint16_t)src[idx]) << 8) | src[idx + 1u]);
                car_pose_indexed_sprite[pose][i] = (c == RGB565_TRANSPARENT) ? 255u : rgb565_to_lcd_index(c);
            }
        }

        car_pose_indexed_vehicle = game2_selected_vehicle;
        car_pose_indexed_ready = 1u;
    }

    if (pose_idx >= CAR_POSE_COUNT) {
        pose_idx = 3u;
    }
    return car_pose_indexed_sprite[pose_idx];
}

static void game2_draw_rgb565_sprite_truecolor_scaled(const uint16_t x0,
                                                      const uint16_t y0,
                                                      const uint16_t nrows,
                                                      const uint16_t ncols,
                                                      const uint8_t *sprite_rgb565,
                                                      const uint8_t scale,
                                                      const uint16_t transparent_rgb565) {
    uint16_t out_w;
    uint16_t out_h;
    uint16_t line_buf[CAR_SPR_HD_W * 2u];

    if (scale == 0u || sprite_rgb565 == 0) {
        return;
    }

    out_w = (uint16_t)(ncols * scale);
    out_h = (uint16_t)(nrows * scale);

    if ((x0 + out_w) > ST7789V2_WIDTH || (y0 + out_h) > ST7789V2_HEIGHT) {
        return;
    }

    for (uint16_t oy = 0u; oy < out_h; oy++) {
        uint16_t sy = (uint16_t)(oy / scale);
        uint16_t draw_y = (uint16_t)(y0 + oy);

        for (uint16_t ox = 0u; ox < out_w; ox++) {
            uint16_t sx = (uint16_t)(ox / scale);
            uint32_t idx = (uint32_t)(sy * ncols + sx) * 2u;
            uint16_t c = (uint16_t)((((uint16_t)sprite_rgb565[idx]) << 8) | sprite_rgb565[idx + 1u]);

            if (c == transparent_rgb565) {
                uint8_t bg_idx = LCD_Get_Pixel((uint16_t)(x0 + ox), draw_y);
                line_buf[ox] = palette_rgb565_ref[bg_idx & 0x0Fu];
            } else {
                line_buf[ox] = (uint16_t)((c >> 8) | (c << 8));
            }
        }

        ST7789V2_Set_Address_Window(&cfg0, x0, draw_y, (uint16_t)(x0 + out_w - 1u), draw_y);
        ST7789V2_Send_Command(&cfg0, ST7789_RAMWR);
        ST7789V2_Send_Data_Block(&cfg0, (uint8_t*)line_buf, (uint32_t)(out_w * 2u));
        // ST7789 data block uses DMA and returns early; wait before reusing line_buf.
        while (cfg0.spi->SR & SPI_SR_BSY) {
            ;
        }
    }
}

#endif

static void play_beep(uint16_t freq_hz, uint8_t vol, uint16_t duration_ms) {
    buzzer_tone(&buzzer_cfg, freq_hz, vol);
    buzzer_off_tick = HAL_GetTick() + duration_ms;
    buzzer_active = 1;
}

static void buzzer_service(void) {
    if (buzzer_active && HAL_GetTick() >= buzzer_off_tick) {
        buzzer_off(&buzzer_cfg);
        buzzer_active = 0;
    }
}

static void generate_segment(uint8_t idx) {
    uint8_t prev = (uint8_t)((idx + TRACK_SEGMENTS - 1u) % TRACK_SEGMENTS);
    int16_t prev_curv = track[prev].curvature_q8;
    int16_t delta = (int16_t)rand_range(-24, 24);
    int16_t curv;
    int16_t curve_limit;

    delta = (int16_t)((delta * (int16_t)rt_curve_strength_pct) / 100);
    curve_limit = (int16_t)((96 * (int16_t)rt_curve_strength_pct) / 100);
    if (curve_limit < 56) {
        curve_limit = 56;
    }
    if (curve_limit > 124) {
        curve_limit = 124;
    }

    curv = (int16_t)(prev_curv + delta);

    track[idx].curvature_q8 = clamp_s16(curv, (int16_t)(-curve_limit), curve_limit);
    track[idx].length_units = (uint16_t)rand_range(34, 88);
}

static void init_track(void) {
    track_head = 0;
    track_tail = 0;
    segment_progress_q8 = 0;

    track[TRACK_SEGMENTS - 1].curvature_q8 = (int16_t)rand_range(-18, 18);
    track[TRACK_SEGMENTS - 1].length_units = (uint16_t)rand_range(56, 92);

    for (uint8_t i = 0; i < TRACK_SEGMENTS; i++) {
        generate_segment(i);
    }

    // Keep near-field smoother without forcing it to be almost straight.
    for (uint8_t i = 0; i < 3; i++) {
        int16_t c = track[i].curvature_q8;
        track[i].curvature_q8 = (int16_t)((c * 3) / 4);
    }

    track_tail = TRACK_SEGMENTS - 1;
}

static void consume_track_distance(int32_t delta_q8) {
    segment_progress_q8 += delta_q8;

    while (1) {
        uint16_t cur_len_units = track[track_head].length_units;
        int32_t cur_len_q8 = ((int32_t)cur_len_units << Q8_SHIFT);
        if (segment_progress_q8 < cur_len_q8) {
            break;
        }

        segment_progress_q8 -= cur_len_q8;
        track_head = (uint8_t)((track_head + 1u) % TRACK_SEGMENTS);
        track_tail = (uint8_t)((track_tail + 1u) % TRACK_SEGMENTS);
        generate_segment(track_tail);
    }
}

static int16_t curvature_at_distance_q8(int32_t ahead_q8) {
    uint8_t idx = track_head;
    int32_t remaining_q8 = segment_progress_q8 + ahead_q8;

    for (uint8_t guard = 0; guard < TRACK_SEGMENTS; guard++) {
        int32_t len_q8 = ((int32_t)track[idx].length_units << Q8_SHIFT);
        if (remaining_q8 < len_q8) {
            return track[idx].curvature_q8;
        }
        remaining_q8 -= len_q8;
        idx = (uint8_t)((idx + 1u) % TRACK_SEGMENTS);
    }

    return track[track_head].curvature_q8;
}

static int16_t curvature_smoothed_q8(int32_t ahead_q8) {
    int32_t s0 = ahead_q8 - (20 << Q8_SHIFT);
    int32_t s2 = ahead_q8 + (20 << Q8_SHIFT);

    if (s0 < 0) {
        s0 = 0;
    }

    int32_t c0 = curvature_at_distance_q8(s0);
    int32_t c1 = curvature_at_distance_q8(ahead_q8);
    int32_t c2 = curvature_at_distance_q8(s2);

    return (int16_t)((c0 + (c1 * 2) + c2) / 4);
}

static void build_track_lookup_cache(void) {
    uint8_t idx = track_head;
    int32_t end_q8 = -segment_progress_q8;

    for (uint8_t i = 0; i < TRACK_SEGMENTS; i++) {
        end_q8 += ((int32_t)track[idx].length_units << Q8_SHIFT);
        track_lookup_end_q8[i] = end_q8;
        track_lookup_curve_q8[i] = track[idx].curvature_q8;
        idx = (uint8_t)((idx + 1u) % TRACK_SEGMENTS);
    }
}

static int16_t curvature_lookup_cached_q8(int32_t ahead_q8) {
    uint8_t lo = 0u;
    uint8_t hi = TRACK_SEGMENTS - 1u;

    while (lo < hi) {
        uint8_t mid = (uint8_t)(lo + ((hi - lo) >> 1));
        if (ahead_q8 < track_lookup_end_q8[mid]) {
            hi = mid;
        } else {
            lo = (uint8_t)(mid + 1u);
        }
    }

    return track_lookup_curve_q8[lo];
}

static int16_t curvature_smoothed_cached_q8(int32_t ahead_q8) {
    int32_t s0 = ahead_q8 - (20 << Q8_SHIFT);
    int32_t s2 = ahead_q8 + (20 << Q8_SHIFT);
    int32_t c0;
    int32_t c1;
    int32_t c2;

    if (s0 < 0) {
        s0 = 0;
    }

    c0 = curvature_lookup_cached_q8(s0);
    c1 = curvature_lookup_cached_q8(ahead_q8);
    c2 = curvature_lookup_cached_q8(s2);
    return (int16_t)((c0 + (c1 * 2) + c2) / 4);
}

static void init_objects(void) {
    for (uint8_t i = 0; i < MAX_OBJECTS; i++) {
        objects[i].active = 0;
        objects[i].distance_q8 = 0;
        objects[i].lane_q8 = 0;
        objects[i].type = 0;
        object_render_distance_q8[i] = 0;
        object_render_valid[i] = 0;
    }
}

static void init_roadside_house_render_state(void) {
    for (uint8_t i = 0; i < ROADSIDE_HOUSE_COUNT; i++) {
        roadside_house_render_id[i] = 0u;
        roadside_house_render_distance_q8[i] = 0;
        roadside_house_render_valid[i] = 0u;
    }
}

static int32_t farthest_object_distance_q8(void) {
    int32_t max_d = 0;
    for (uint8_t i = 0; i < MAX_OBJECTS; i++) {
        if (objects[i].active && objects[i].distance_q8 > max_d) {
            max_d = objects[i].distance_q8;
        }
    }
    return max_d;
}

static uint16_t checkpoint_spawn_interval_units(void) {
    int32_t steps = (int32_t)(distance_score_units / CHECKPOINT_INTERVAL_STEP_DIST_UNITS);
    int32_t interval;

    if (steps > CHECKPOINT_INTERVAL_MAX_STEPS) {
        steps = CHECKPOINT_INTERVAL_MAX_STEPS;
    }

    interval = CHECKPOINT_INTERVAL_BASE_UNITS + (steps * CHECKPOINT_INTERVAL_STEP_UNITS);
    interval = (interval * (int32_t)rt_checkpoint_interval_scale_pct) / 100;
    if (interval < CHECKPOINT_INTERVAL_MIN_UNITS) {
        interval = CHECKPOINT_INTERVAL_MIN_UNITS;
    }

    return (uint16_t)interval;
}

static int32_t checkpoint_reward_ms(void) {
    // Reward grows with progress to compensate for lower checkpoint frequency later.
    int32_t bonus_steps = (int32_t)(distance_score_units / CHECKPOINT_REWARD_STEP_DIST_UNITS);
    if (bonus_steps > CHECKPOINT_REWARD_MAX_STEPS) {
        bonus_steps = CHECKPOINT_REWARD_MAX_STEPS;
    }
    return CHECKPOINT_REWARD_BASE_MS + (bonus_steps * CHECKPOINT_REWARD_STEP_MS);
}

static int16_t checkpoint_heal_hp(void) {
    int32_t steps = (int32_t)(distance_score_units / CHECKPOINT_REWARD_STEP_DIST_UNITS);
    if (steps > CHECKPOINT_HEAL_MAX_STEPS) {
        steps = CHECKPOINT_HEAL_MAX_STEPS;
    }
    return (int16_t)(CHECKPOINT_HEAL_BASE_HP + (steps * CHECKPOINT_HEAL_STEP_HP));
}

static uint8_t checkpoint_active_on_track(void) {
    for (uint8_t i = 0; i < MAX_OBJECTS; i++) {
        if (objects[i].active && objects[i].type == 2) {
            return 1;
        }
    }
    return 0;
}

static void try_spawn_checkpoint_by_distance(void) {
    int16_t spawn_idx = -1;

    if (distance_score_units < checkpoint_next_spawn_units) {
        return;
    }

    if (checkpoint_active_on_track()) {
        return;
    }

    // Prefer an inactive slot first.
    for (uint8_t i = 0; i < MAX_OBJECTS; i++) {
        if (!objects[i].active) {
            spawn_idx = (int16_t)i;
            break;
        }
    }

    // If full, replace the farthest non-checkpoint object so checkpoint timing is not delayed.
    if (spawn_idx < 0) {
        int32_t far_d = -1;
        for (uint8_t i = 0; i < MAX_OBJECTS; i++) {
            if (objects[i].active && objects[i].type != 2 && objects[i].distance_q8 > far_d) {
                far_d = objects[i].distance_q8;
                spawn_idx = (int16_t)i;
            }
        }
    }

    if (spawn_idx < 0) {
        return;
    }

    {
        int32_t base = farthest_object_distance_q8() + ((int32_t)rand_range(150, 230) << Q8_SHIFT);
        int32_t next_interval = checkpoint_spawn_interval_units();
        int32_t jitter = rand_range(-CHECKPOINT_SPAWN_JITTER_UNITS, CHECKPOINT_SPAWN_JITTER_UNITS);
        int32_t next_spawn = (int32_t)distance_score_units + next_interval + jitter;

        if (base < (180 << Q8_SHIFT)) {
            base = (180 << Q8_SHIFT) + ((int32_t)rand_range(0, 70) << Q8_SHIFT);
        }

        // Keep the first checkpoint reasonably close instead of pushing it beyond farthest traffic.
        if (distance_score_units < (uint32_t)(CHECKPOINT_INTERVAL_BASE_UNITS + 80u)) {
            int32_t first_base = ((int32_t)rand_range(185, 265) << Q8_SHIFT);
            if (base > first_base) {
                base = first_base;
            }
        }

        if (next_spawn < (int32_t)distance_score_units + (int32_t)CHECKPOINT_INTERVAL_MIN_UNITS) {
            next_spawn = (int32_t)distance_score_units + (int32_t)CHECKPOINT_INTERVAL_MIN_UNITS;
        }

        objects[spawn_idx].distance_q8 = base;
        objects[spawn_idx].lane_q8 = 0;
        objects[spawn_idx].type = 2;
        objects[spawn_idx].active = 1;
        checkpoint_next_spawn_units = (uint32_t)next_spawn;
    }
}

static void spawn_one_object(void) {
    for (uint8_t i = 0; i < MAX_OBJECTS; i++) {
        if (!objects[i].active) {
            int32_t base = farthest_object_distance_q8() + ((int32_t)rand_range(72, 150) << Q8_SHIFT);
            if (base < (120 << Q8_SHIFT)) {
                base = (120 << Q8_SHIFT) + ((int32_t)rand_range(0, 80) << Q8_SHIFT);
            }

            int32_t lane_pick = rand_range(0, 3);
            if (lane_pick == 0) objects[i].lane_q8 = -450;
            else if (lane_pick == 1) objects[i].lane_q8 = -150;
            else if (lane_pick == 2) objects[i].lane_q8 = 150;
            else objects[i].lane_q8 = 450;
            objects[i].lane_q8 += (int16_t)rand_range(-24, 24);

            {
                int32_t non_cp_roll = rand_range(0, 99);
                if (non_cp_roll < rt_spawn_traffic_pct) {
                    objects[i].type = 0; // traffic car (most frequent)
                } else if (non_cp_roll < ((int32_t)rt_spawn_traffic_pct + (int32_t)rt_spawn_boost_pct)) {
                    objects[i].type = 3; // boost item (medium)
                } else {
                    objects[i].type = 1; // yellow hazard (least frequent)
                }
            }

            objects[i].distance_q8 = base;
            objects[i].active = 1;
            return;
        }
    }
}

static void maintain_object_density(void) {
    uint8_t active_count = 0;
    for (uint8_t i = 0; i < MAX_OBJECTS; i++) {
        if (objects[i].active) {
            active_count++;
        }
    }

    while (active_count < 8) {
        spawn_one_object();
        active_count++;
    }
}

static void update_objects_and_collisions(int32_t delta_q8) {
    for (uint8_t i = 0; i < MAX_OBJECTS; i++) {
        int32_t prev_distance_q8;
        const int32_t near_min_q8 = (1 << Q8_SHIFT);
        const int32_t near_max_q8 = (18 << Q8_SHIFT);

        if (!objects[i].active) {
            continue;
        }

        prev_distance_q8 = objects[i].distance_q8;
        objects[i].distance_q8 -= delta_q8;
        if (objects[i].distance_q8 < -(12 << Q8_SHIFT)) {
            objects[i].active = 0;
            continue;
        }

        // Accept both current-frame overlap and crossing-through overlap to avoid tunneling.
        if (prev_distance_q8 >= near_min_q8 && objects[i].distance_q8 <= near_max_q8) {
            if (objects[i].type == 2) {
                // Checkpoint is rendered as a full-width road gate, so collision follows depth only.
                time_left_ms += checkpoint_reward_ms();
                if (time_left_ms > 999000) {
                    time_left_ms = 999000;
                }
                player_hp += checkpoint_heal_hp();
                if (player_hp > rt_player_hp_max) {
                    player_hp = rt_player_hp_max;
                }
                Game2Music_PlayCheckpointSfx();
                game2_ws2812_start_flash(0u, 255u, 0u);
                objects[i].active = 0;
                continue;
            }

            {
                int32_t sample_start_q8 = objects[i].distance_q8;
                int32_t sample_end_q8 = prev_distance_q8;
                uint8_t hit = 0;

                if (sample_start_q8 < near_min_q8) {
                    sample_start_q8 = near_min_q8;
                }
                if (sample_end_q8 > near_max_q8) {
                    sample_end_q8 = near_max_q8;
                }

                if (sample_start_q8 <= sample_end_q8) {
                    for (uint8_t s = 0; s < 3u; s++) {
                        int32_t sample_q8 = sample_start_q8 + ((sample_end_q8 - sample_start_q8) * (int32_t)s) / 2;
                        if (sprite_collision_player_object_at_distance(&objects[i], sample_q8)) {
                            hit = 1;
                            break;
                        }
                    }
                }

                if (!hit) {
                    continue;
                }
            }

            if (objects[i].type == 3) {
                // Boost pickup gain depends on selected vehicle.
                boost_add_charge_q8(rt_boost_pickup_q8);
                Game2Music_PlayBoostSfx();
                game2_ws2812_start_flash(0u, 0u, 255u);
                objects[i].active = 0;
                continue;
            }

            if (boost_active) {
                // During boost, ignore all slowdown/collision effects.
                objects[i].active = 0;
                continue;
            }

            {
                if (objects[i].type == 1) {
                    // Yellow hazard: lowest penalty, short time loss.
                    speed_q8 = (speed_q8 * rt_yellow_keep_pct) / 100;
                    if (speed_q8 < (60 << Q8_SHIFT)) {
                        speed_q8 = (60 << Q8_SHIFT);
                    }
                    time_left_ms -= rt_yellow_time_penalty_ms;
                    push_time_penalty_feedback(rt_yellow_time_penalty_ms);
                    if (time_left_ms < 0) {
                        time_left_ms = 0;
                    }
                    yellow_sway_timer_ms = YELLOW_SWAY_TOTAL_MS;
                    yellow_sway_step_ms = 0;
                    yellow_sway_dir = (objects[i].lane_q8 >= player_lane_q8) ? -1 : 1;
                    Game2Music_PlayCrashSfx();
                    game2_ws2812_start_flash(255u, 180u, 0u);
                } else {
                    // Traffic-car collision is the heaviest: HP loss + strong speed cut.
                    speed_q8 = (speed_q8 * rt_traffic_keep_pct) / 100;
                    if (speed_q8 < (60 << Q8_SHIFT)) {
                        speed_q8 = (60 << Q8_SHIFT);
                    }
                    player_hp -= (int16_t)rt_traffic_hp_loss;
                    if (player_hp < 0) {
                        player_hp = 0;
                    }
                    player_flash_timer_ms = PLAYER_FLASH_TOGGLE_MS * PLAYER_FLASH_TOTAL_TOGGLES;
                    Game2Music_PlayCrashSfx();
                    game2_ws2812_start_flash(255u, 0u, 0u);
                }
                objects[i].active = 0;
            }
        }
    }
}

static void draw_boost_panel(void) {
    // Deprecated: integrated into top dashboard HUD.
}

static void game2_draw_hline_fast(int16_t x0, int16_t x1, int16_t y, uint8_t color) {
    LCD_Draw_HLine_Fast(x0, x1, y, color);
}

static void game2_draw_rect_fast(int16_t x,
                                 int16_t y,
                                 int16_t w,
                                 int16_t h,
                                 uint8_t color,
                                 uint8_t fill) {
    if (w <= 0 || h <= 0) {
        return;
    }

    if (fill) {
        for (int16_t yy = y; yy < (int16_t)(y + h); yy++) {
            game2_draw_hline_fast(x, (int16_t)(x + w - 1), yy, color);
        }
    } else {
        game2_draw_hline_fast(x, (int16_t)(x + w - 1), y, color);
        game2_draw_hline_fast(x, (int16_t)(x + w - 1), (int16_t)(y + h - 1), color);
        for (int16_t yy = y; yy < (int16_t)(y + h); yy++) {
            if (x >= 0 && x < ST7789V2_WIDTH && yy >= 0 && yy < ST7789V2_HEIGHT) {
                LCD_Set_Pixel((uint16_t)x, (uint16_t)yy, color);
            }
            if ((x + w - 1) >= 0 && (x + w - 1) < ST7789V2_WIDTH && yy >= 0 && yy < ST7789V2_HEIGHT) {
                LCD_Set_Pixel((uint16_t)(x + w - 1), (uint16_t)yy, color);
            }
        }
    }
}

static void draw_country_tree(int16_t x, int16_t base_y, int16_t h, uint8_t leaf_col) {
    int16_t crown_w = (int16_t)(h + 2);
    int16_t trunk_h = (int16_t)((h / 3) + 1);
    int16_t trunk_w = (h > 12) ? 2 : 1;
    int16_t crown_top = (int16_t)(base_y - h);
    int16_t crown_base = (int16_t)(base_y - trunk_h);
    int16_t cx = (int16_t)(x + crown_w / 2);

    game2_draw_rect_fast((int16_t)(cx - trunk_w / 2), crown_base, trunk_w, trunk_h, 12, 1);

    for (int16_t py = crown_top; py <= crown_base; py++) {
        int16_t rel = (int16_t)(py - crown_top);
        int16_t half = (int16_t)(1 + ((int32_t)crown_w * rel) / (2 * (h - trunk_h + 1)));
        if (half > crown_w / 2) {
            half = (int16_t)(crown_w / 2);
        }
        game2_draw_hline_fast((int16_t)(cx - half), (int16_t)(cx + half), py, leaf_col);
    }

    if (h > 10) {
        LCD_Set_Pixel((uint16_t)clamp_s16((int16_t)(cx - 2), 0, ST7789V2_WIDTH - 1),
                      (uint16_t)clamp_s16((int16_t)(crown_top + 3), 0, ST7789V2_HEIGHT - 1),
                      10);
    }
}

static void draw_country_vegetation_layer(int16_t skyline_y,
                                          int16_t shift_px,
                                          int16_t spacing,
                                          int16_t height_base,
                                          int16_t height_var,
                                          uint8_t count,
                                          uint8_t leaf_col,
                                          int16_t base_offset) {
    int16_t wrap_w = (int16_t)(spacing * count);
    int16_t shift = (wrap_w > 0) ? (int16_t)(shift_px % wrap_w) : 0;

    for (uint8_t i = 0u; i < count; i++) {
        int16_t x = (int16_t)(i * spacing - shift);
        int16_t h = (int16_t)(height_base + ((i * 7 + i / 2) % height_var));
        int16_t base_y = (int16_t)(skyline_y - base_offset + ((i * 5) % 3));

        if (x < -spacing) {
            x = (int16_t)(x + wrap_w);
        }
        if (x > ST7789V2_WIDTH + spacing) {
            continue;
        }

        draw_country_tree(x, base_y, h, leaf_col);
    }
}

static void draw_sky_parallax(int16_t sky_shift_px) {
    const int16_t sky_w = ST7789V2_WIDTH;
    const int16_t sky_h = ROAD_HORIZON_Y;
    const int16_t skyline_y = ROAD_HORIZON_Y;
    int16_t shift_build = (int16_t)((sky_shift_px * 2) / 3);
    int16_t shift_cloud = (int16_t)(sky_shift_px / 3);

    game2_draw_rect_fast(0, 0, sky_w, sky_h, (game2_selected_map == GAME2_MAP_COUNTRY) ? 14 : 9, 1);

    // Distant skyline with parallax.
    if (game2_selected_map == GAME2_MAP_CITY) {
        for (int16_t i = 0; i < 14; i++) {
            int16_t w = (int16_t)(8 + ((i * 5) % 9));
            int16_t h = (int16_t)(6 + ((i * 7) % 13));
            int16_t x = (int16_t)(i * 20 - (shift_build % 20));

            if (x < -w) {
                x = (int16_t)(x + 280);
            }
            if (x >= sky_w) {
                continue;
            }

            if (skyline_y - h > 1) {
                game2_draw_rect_fast(x, (int16_t)(skyline_y - h), w, h, 8, 1);
                game2_draw_rect_fast(x, (int16_t)(skyline_y - h), w, h, 1, 0);
                if (w > 9 && h > 8) {
                    LCD_Set_Pixel((uint16_t)(x + 3), (uint16_t)(skyline_y - h + 3), 15);
                    LCD_Set_Pixel((uint16_t)(x + 5), (uint16_t)(skyline_y - h + 5), 15);
                }
            }
        }
    }

    if (game2_selected_map == GAME2_MAP_COUNTRY) {
        int16_t shift_far_trees = (int16_t)(sky_shift_px / 5);
        int16_t shift_near_trees = (int16_t)((sky_shift_px * 2) / 3);

        draw_country_vegetation_layer(skyline_y, shift_far_trees, 16, 7, 6, 18u, 3, 1);
        draw_country_vegetation_layer(skyline_y, shift_near_trees, 28, 12, 10, 11u, 3, 0);
    }

    // Cloud clusters with slower parallax.
    for (int16_t i = 0; i < 6; i++) {
        int16_t cloud_x = (int16_t)(18 + i * 42 - (shift_cloud % 42));
        // Keep clouds slightly above skyline so they are not covered by top HUD.
        int16_t cloud_y = (int16_t)(skyline_y - 34 + ((i * 9) % 14));
        if (cloud_y < 3) {
            cloud_y = 3;
        }
        if (cloud_x < -26) {
            cloud_x = (int16_t)(cloud_x + 252);
        }
        if (cloud_x > sky_w - 2 || cloud_y > sky_h - 7) {
            continue;
        }

        game2_draw_rect_fast(cloud_x, cloud_y, 10, 4, 1, 1);
        game2_draw_rect_fast((int16_t)(cloud_x + 6), (int16_t)(cloud_y - 2), 8, 5, 1, 1);
        game2_draw_rect_fast((int16_t)(cloud_x + 12), cloud_y, 10, 4, 1, 1);
        game2_draw_hline_fast(cloud_x, (int16_t)(cloud_x + 21), (int16_t)(cloud_y + 4), 13);
    }
}

static uint32_t roadside_scenery_hash(uint32_t v) {
    v ^= v >> 16;
    v *= 0x7feb352du;
    v ^= v >> 15;
    v *= 0x846ca68bu;
    v ^= v >> 16;
    return v;
}

static const uint8_t* roadside_house_sprite_by_index(uint8_t idx) {
    if (idx == 0u) {
        return roadside_house_red_sprite;
    }
    if (idx == 1u) {
        return roadside_house_blue_sprite;
    }
    return roadside_house_shop_sprite;
}

static uint8_t roadside_ground_color(void) {
    return (game2_selected_map == GAME2_MAP_COUNTRY) ? ROADSIDE_COUNTRY_GRASS_COLOR : ROADSIDE_CITY_CEMENT_COLOR;
}

static uint8_t roadside_mosaic_color(void) {
    return (game2_selected_map == GAME2_MAP_COUNTRY) ? ROADSIDE_COUNTRY_MOSAIC_COLOR : ROADSIDE_CITY_MOSAIC_COLOR;
}

static void draw_roadside_ground_line(int16_t left, int16_t right, int16_t y) {
    game2_draw_hline_fast(0, ST7789V2_WIDTH - 1, y, roadside_ground_color());
}

static void draw_indexed_sprite_resized_clipped(int16_t x0,
                                                int16_t y0,
                                                uint16_t nrows,
                                                uint16_t ncols,
                                                const uint8_t *sprite,
                                                uint16_t out_w,
                                                uint16_t out_h,
                                                int16_t clip_left,
                                                int16_t clip_right,
                                                int16_t clip_top,
                                                int16_t clip_bottom) {
    int16_t dst_left;
    int16_t dst_top;
    int16_t dst_right;
    int16_t dst_bottom;

    if (out_w == 0u || out_h == 0u || sprite == 0) {
        return;
    }

    if (clip_left < 0) {
        clip_left = 0;
    }
    if (clip_right >= ST7789V2_WIDTH) {
        clip_right = ST7789V2_WIDTH - 1;
    }
    if (clip_top < 0) {
        clip_top = 0;
    }
    if (clip_bottom >= ST7789V2_HEIGHT) {
        clip_bottom = ST7789V2_HEIGHT - 1;
    }
    if (clip_left > clip_right || clip_top > clip_bottom) {
        return;
    }

    dst_left = x0;
    dst_top = y0;
    dst_right = (int16_t)(x0 + (int16_t)out_w - 1);
    dst_bottom = (int16_t)(y0 + (int16_t)out_h - 1);

    if (dst_left < clip_left) dst_left = clip_left;
    if (dst_top < clip_top) dst_top = clip_top;
    if (dst_right > clip_right) dst_right = clip_right;
    if (dst_bottom > clip_bottom) dst_bottom = clip_bottom;

    if (dst_left > dst_right || dst_top > dst_bottom) {
        return;
    }

    if (out_w == ncols && out_h == nrows) {
        for (int16_t py = dst_top; py <= dst_bottom; py++) {
            const uint8_t *row = &sprite[(uint32_t)(py - y0) * ncols];
            int16_t px = dst_left;

            while (px <= dst_right) {
                uint8_t pixel = row[px - x0];
                int16_t run_start;

                if (pixel == 255u) {
                    px++;
                    continue;
                }

                run_start = px;
                px++;
                while (px <= dst_right && row[px - x0] == pixel) {
                    px++;
                }
                game2_draw_hline_fast(run_start, (int16_t)(px - 1), py, pixel);
            }
        }
        return;
    }

    if (out_w <= ncols && out_h <= nrows) {
        for (int16_t py = dst_top; py <= dst_bottom; py++) {
            uint16_t sy = (uint16_t)(((uint32_t)(py - y0) * nrows) / out_h);
            int16_t px = dst_left;

            while (px <= dst_right) {
                uint16_t sx = (uint16_t)(((uint32_t)(px - x0) * ncols) / out_w);
                uint8_t pixel = sprite[(uint32_t)sy * ncols + sx];
                int16_t run_start;

                if (pixel == 255u) {
                    px++;
                    continue;
                }

                run_start = px;
                px++;
                while (px <= dst_right) {
                    sx = (uint16_t)(((uint32_t)(px - x0) * ncols) / out_w);
                    if (sprite[(uint32_t)sy * ncols + sx] != pixel) {
                        break;
                    }
                    px++;
                }
                game2_draw_hline_fast(run_start, (int16_t)(px - 1), py, pixel);
            }
        }
        return;
    }

    for (uint16_t sy = 0u; sy < nrows; sy++) {
        int16_t py0 = (int16_t)(y0 + (int16_t)(((uint32_t)sy * out_h) / nrows));
        int16_t py1 = (int16_t)(y0 + (int16_t)((((uint32_t)sy + 1u) * out_h) / nrows) - 1);

        if (py1 < dst_top || py0 > dst_bottom) {
            continue;
        }
        if (py0 < dst_top) {
            py0 = dst_top;
        }
        if (py1 > dst_bottom) {
            py1 = dst_bottom;
        }

        {
            const uint8_t *row = &sprite[(uint32_t)sy * ncols];
            uint16_t sx = 0u;

            while (sx < ncols) {
                uint8_t pixel = row[sx];
                uint16_t run_start = sx;
                int16_t px0;
                int16_t px1;

                sx++;
                while (sx < ncols && row[sx] == pixel) {
                    sx++;
                }

                if (pixel == 255u) {
                    continue;
                }

                px0 = (int16_t)(x0 + (int16_t)(((uint32_t)run_start * out_w) / ncols));
                px1 = (int16_t)(x0 + (int16_t)(((uint32_t)sx * out_w) / ncols) - 1);

                if (px1 < dst_left || px0 > dst_right) {
                    continue;
                }
                if (px0 < dst_left) {
                    px0 = dst_left;
                }
                if (px1 > dst_right) {
                    px1 = dst_right;
                }

                for (int16_t py = py0; py <= py1; py++) {
                    game2_draw_hline_fast(px0, px1, py, pixel);
                }
            }
        }
    }
}

static const uint8_t* city_building_module_sprite(uint8_t part) {
    init_city_building_sprites();
    if (part > 3u) {
        part = 0u;
    }
    return city_building_indexed_sprite[part];
}

static void draw_city_building_stack(int16_t x,
                                     int16_t y,
                                     uint16_t module_size,
                                     uint8_t module_count,
                                     uint32_t hash,
                                     int16_t clip_left,
                                     int16_t clip_right) {
    int16_t module_step = (int16_t)(module_size - CITY_BUILDING_MODULE_OVERLAP_PX);
    int16_t top_y;

    if (module_step < 1) {
        module_step = 1;
    }

    if (module_count < 2u) {
        module_count = 2u;
    } else if (module_count > 4u) {
        module_count = 4u;
    }

    top_y = (int16_t)(y - (int16_t)module_size - ((int16_t)(module_count - 1u) * module_step) + 1);

    for (uint8_t i = 0u; i < module_count; i++) {
        uint8_t part = (uint8_t)((hash >> (uint8_t)(8u + (i * 3u))) & 0x03u);
        int16_t module_y = (int16_t)(top_y + ((int16_t)i * module_step));

        draw_indexed_sprite_resized_clipped(x,
                                            module_y,
                                            GAME2_CITY_BUILDING_SPR_H,
                                            GAME2_CITY_BUILDING_SPR_W,
                                            city_building_module_sprite(part),
                                            module_size,
                                            module_size,
                                            clip_left,
                                            clip_right,
                                            0,
                                            ROAD_BOTTOM_Y);
    }
}

static void draw_roadside_houses(void) {
    uint16_t scenery_spacing = (game2_selected_map == GAME2_MAP_CITY) ?
                               ROADSIDE_CITY_BUILDING_SPACING_UNITS :
                               ROADSIDE_HOUSE_SPACING_UNITS;
    int16_t scenery_count = (game2_selected_map == GAME2_MAP_CITY) ?
                            ROADSIDE_CITY_BUILDING_COUNT :
                            ROADSIDE_HOUSE_COUNT;
    uint32_t first_house_id = (distance_score_units / scenery_spacing) + 1u;

    for (int16_t slot = scenery_count; slot > 0; slot--) {
        uint8_t render_slot = (uint8_t)(slot - 1);
        uint32_t house_id = first_house_id + (uint32_t)(slot - 1);
        uint32_t h = roadside_scenery_hash(house_id);
        int32_t jitter_units = (int32_t)((h >> 8) & 0x1Fu) - 15;
        int32_t world_units = ((int32_t)house_id * scenery_spacing) + jitter_units;
        int32_t target_q8 = (world_units << Q8_SHIFT) - (int32_t)distance_score_q8;
        int32_t render_q8 = target_q8;
        int32_t target_units = target_q8 >> Q8_SHIFT;
        int32_t dist_units;
        int16_t y;
        int16_t center;
        int16_t half;
        int16_t left;
        int16_t right;
        int16_t house_size;
        int16_t side_gap;
        int16_t away_jitter;
        int16_t draw_x;
        int16_t draw_y;
        int16_t clip_left;
        int16_t clip_right;
        const uint8_t *sprite;

        if (target_units < 8 || target_units > 620) {
            roadside_house_render_valid[render_slot] = 0u;
            continue;
        }

        if (!game_over &&
            roadside_house_render_valid[render_slot] &&
            roadside_house_render_id[render_slot] == house_id) {
            render_q8 = smooth_render_distance_q8(roadside_house_render_distance_q8[render_slot], target_q8);
        }

        roadside_house_render_id[render_slot] = house_id;
        roadside_house_render_distance_q8[render_slot] = render_q8;
        roadside_house_render_valid[render_slot] = 1u;

        dist_units = render_q8 >> Q8_SHIFT;
        if (dist_units < 8 || dist_units > 620) {
            continue;
        }

        y = projected_y_from_distance_units(dist_units);
        if (y <= ROAD_HORIZON_Y || y >= ROAD_BOTTOM_Y - 2) {
            continue;
        }

        center = road_center_cache[y];
        half = (int16_t)road_half_cache[y];
        if (half <= 0) {
            half = projected_road_half_from_y(y);
            center = (int16_t)(ST7789V2_WIDTH / 2);
        }

        house_size = projected_sprite_size_from_y(y, ROADSIDE_HOUSE_FAR_SIZE_PX, ROADSIDE_HOUSE_NEAR_SIZE_PX);
        side_gap = (int16_t)(2 + (house_size / 14));
        away_jitter = (int16_t)(((h >> 16) & 0x07u) * (1 + (house_size / 20)));
        left = (int16_t)(center - half);
        right = (int16_t)(center + half);

        if (game2_selected_map == GAME2_MAP_CITY) {
            uint8_t module_count = (uint8_t)(2u + ((h >> 20) % 3u));
            int16_t module_size = projected_sprite_size_from_y(y,
                                                               CITY_BUILDING_MODULE_FAR_SIZE_PX,
                                                               CITY_BUILDING_MODULE_NEAR_SIZE_PX);
            int16_t module_step = (int16_t)(module_size - CITY_BUILDING_MODULE_OVERLAP_PX);
            int16_t building_h;
            int16_t building_w = module_size;

            if (module_step < 1) {
                module_step = 1;
            }

            building_h = (int16_t)((int16_t)module_size + ((int16_t)(module_count - 1u) * module_step));
            side_gap = (int16_t)(2 + (module_size / 12));
            away_jitter = (int16_t)(((h >> 16) & 0x07u) * (1 + (module_size / 18)));
            draw_y = (int16_t)(y - building_h + 1);

            if (draw_y >= ST7789V2_HEIGHT || y < 0) {
                continue;
            }

            if ((h & 0x01u) == 0u) {
                draw_x = (int16_t)(left - side_gap - building_w - away_jitter);
                clip_left = 0;
                clip_right = (int16_t)(left - side_gap - 1);
                if ((draw_x + building_w) <= 0 || clip_right < 0 || (draw_x + building_w) > left) {
                    continue;
                }
            } else {
                draw_x = (int16_t)(right + side_gap + away_jitter);
                clip_left = (int16_t)(right + side_gap);
                clip_right = ST7789V2_WIDTH - 1;
                if (draw_x >= ST7789V2_WIDTH || clip_left >= ST7789V2_WIDTH || draw_x < right) {
                    continue;
                }
            }

            draw_city_building_stack(draw_x,
                                     y,
                                     (uint16_t)module_size,
                                     module_count,
                                     h,
                                     clip_left,
                                     clip_right);
            continue;
        }

        draw_y = (int16_t)(y - house_size + 1);

        if (draw_y < 0 || y >= ST7789V2_HEIGHT) {
            continue;
        }

        if ((h & 0x01u) == 0u) {
            draw_x = (int16_t)(left - side_gap - house_size - away_jitter);
            clip_left = 0;
            clip_right = (int16_t)(left - side_gap - 1);
            if ((draw_x + house_size) <= 0 || clip_right < 0 || (draw_x + house_size) > left) {
                continue;
            }
        } else {
            draw_x = (int16_t)(right + side_gap + away_jitter);
            clip_left = (int16_t)(right + side_gap);
            clip_right = ST7789V2_WIDTH - 1;
            if (draw_x >= ST7789V2_WIDTH || clip_left >= ST7789V2_WIDTH || draw_x < right) {
                continue;
            }
        }

        sprite = roadside_house_sprite_by_index((uint8_t)((h >> 4) % 3u));
        draw_indexed_sprite_resized_clipped(draw_x,
                                            draw_y,
                                            GAME2_HOUSE_SPR_H,
                                            GAME2_HOUSE_SPR_W,
                                            sprite,
                                            (uint16_t)house_size,
                                            (uint16_t)house_size,
                                            clip_left,
                                            clip_right,
                                            0,
                                            ROAD_BOTTOM_Y);
    }
}

static void draw_road_scanlines(void) {
    const int16_t screen_cx = ST7789V2_WIDTH / 2;
    const int16_t span = (ROAD_BOTTOM_Y - ROAD_HORIZON_Y);
    const int16_t bottom_center = (int16_t)(screen_cx - lane_q8_to_road_px(player_lane_q8, ROAD_NEAR_HALF_WIDTH));

    int32_t center_accum_q8 = 0;

    init_road_projection_lut();
    build_track_lookup_cache();

    {
        int16_t sky_shift_px = (int16_t)(lane_q8_to_road_px(player_lane_q8, ROAD_NEAR_HALF_WIDTH) / 2);
        sky_shift_px += (int16_t)(curvature_smoothed_cached_q8(44 << Q8_SHIFT) / 3);
        draw_sky_parallax(sky_shift_px);
    }

    for (int16_t y = ROAD_BOTTOM_Y; y >= ROAD_HORIZON_Y; y--) {
        int16_t depth = (int16_t)(y - ROAD_HORIZON_Y + 1);
        int32_t dist_q8 = road_dist_q8_lut[y];
        int16_t curve = curvature_smoothed_cached_q8(dist_q8);
        int32_t delta_units = road_delta_units_lut[y];

        // Integrate curvature by world distance with depth gain for natural near-field bending.
        int32_t bend_gain_q8 = road_bend_gain_q8_lut[y];
        int32_t bend_step_q8 = ((int32_t)curve * delta_units * bend_gain_q8) / 256;
        center_accum_q8 += bend_step_q8;

        int16_t center = (int16_t)(bottom_center + (center_accum_q8 >> Q8_SHIFT));
        center = clamp_s16(center,
                           (int16_t)(screen_cx - ROAD_MAX_CENTER_SWAY),
                           (int16_t)(screen_cx + ROAD_MAX_CENTER_SWAY));

        int16_t half = (int16_t)(ROAD_BASE_HALF_WIDTH + ((int32_t)(ROAD_NEAR_HALF_WIDTH - ROAD_BASE_HALF_WIDTH) * depth) / span);
        int16_t left = (int16_t)(center - half);
        int16_t right = (int16_t)(center + half);

        road_center_cache[y] = center;
        road_half_cache[y] = (uint16_t)half;

        draw_roadside_ground_line(left, right, y);
        game2_draw_hline_fast(left, right, y, ROAD_SURFACE_COLOR);

        // Roadside mosaic flecks stay outside the asphalt.
        {
            uint32_t cement_phase = (distance_score_units >> 1) + (uint32_t)(y * 3);
            int16_t left_start = (int16_t)(cement_phase & 0x07u);
            int16_t right_start = (int16_t)(right + 1 + (int16_t)(cement_phase & 0x07u));
            uint8_t mosaic_color = roadside_mosaic_color();

            for (int16_t gx = left_start; gx < left; gx += 11) {
                if ((((gx + y + (int16_t)cement_phase) & 0x0Fu) < 3u) && gx >= 0 && gx < ST7789V2_WIDTH) {
                    LCD_Set_Pixel((uint16_t)gx, (uint16_t)y, mosaic_color);
                }
            }

            for (int16_t gx = right_start; gx < ST7789V2_WIDTH; gx += 11) {
                if ((((gx + y + (int16_t)cement_phase) & 0x0Fu) < 3u) && gx >= 0 && gx < ST7789V2_WIDTH) {
                    LCD_Set_Pixel((uint16_t)gx, (uint16_t)y, mosaic_color);
                }
            }
        }

        // Draw warning shoulders that match the slowdown edge zone width.
        int16_t edge_band = (int16_t)(((int32_t)half * (PLAYER_MAX_LANE_Q8 - PLAYER_EDGE_LANE_Q8)) / PLAYER_MAX_LANE_Q8);
        if (edge_band < 2) {
            edge_band = 2;
        }

        int16_t left_edge_end = (int16_t)(left + edge_band - 1);
        int16_t right_edge_start = (int16_t)(right - edge_band + 1);
        uint8_t shoulder_col = ((((distance_score_units >> 1) + (uint32_t)(dist_q8 >> Q8_SHIFT) + (uint32_t)(y >> 1)) & 0x08u) == 0u) ? 5 : 1;

        if (left <= left_edge_end) {
            game2_draw_hline_fast(left, left_edge_end, y, shoulder_col);
        }
        if (right_edge_start <= right) {
            game2_draw_hline_fast(right_edge_start, right, y, shoulder_col);
        }

        if (left + 1 >= 0 && left + 1 < ST7789V2_WIDTH) {
            LCD_Set_Pixel((uint16_t)(left + 1), (uint16_t)y, 6);
        }
        if (right - 1 >= 0 && right - 1 < ST7789V2_WIDTH) {
            LCD_Set_Pixel((uint16_t)(right - 1), (uint16_t)y, 6);
        }

        if ((((distance_score_units >> 2) + (uint32_t)(dist_q8 >> Q8_SHIFT)) & 0x0Fu) < 4u) {
            int16_t lane_div_1 = (int16_t)(center - (half / 2));
            int16_t lane_div_2 = center;
            int16_t lane_div_3 = (int16_t)(center + (half / 2));
            if (lane_div_1 >= 0 && lane_div_1 < ST7789V2_WIDTH) {
                draw_lane_marker_triplet(lane_div_1, y);
                if (y > ROAD_HORIZON_Y + 1) {
                    draw_lane_marker_triplet(lane_div_1, (int16_t)(y - 1));
                }
            }
            if (lane_div_2 >= 0 && lane_div_2 < ST7789V2_WIDTH) {
                draw_lane_marker_triplet(lane_div_2, y);
                if (y > ROAD_HORIZON_Y + 1) {
                    draw_lane_marker_triplet(lane_div_2, (int16_t)(y - 1));
                }
            }
            if (lane_div_3 >= 0 && lane_div_3 < ST7789V2_WIDTH) {
                draw_lane_marker_triplet(lane_div_3, y);
                if (y > ROAD_HORIZON_Y + 1) {
                    draw_lane_marker_triplet(lane_div_3, (int16_t)(y - 1));
                }
            }
        }
    }
}

static void draw_object_projected_at_distance_q8(const Object* obj, int32_t distance_q8) {
    int32_t dist_units = distance_q8 >> Q8_SHIFT;
    if (dist_units < 8 || dist_units > 620) {
        return;
    }

    int16_t y = projected_y_from_distance_units(dist_units);
    if (y <= ROAD_HORIZON_Y || y >= ROAD_BOTTOM_Y - 2) {
        return;
    }

    int16_t center = road_center_cache[y];
    int16_t half = (int16_t)road_half_cache[y];
    int16_t x = (int16_t)(center + lane_q8_to_road_px(obj->lane_q8, half));
    int16_t obj_size = projected_sprite_size_from_y(y, PROJECTED_OBSTACLE_FAR_SIZE_PX, PROJECTED_OBSTACLE_NEAR_SIZE_PX);
    const uint8_t* obj_sprite = 0;
    uint16_t obj_src_w = GAME2_OBSTACLE_SPR_W;
    uint16_t obj_src_h = GAME2_OBSTACLE_SPR_H;

    if (obj->type == 0) {
        init_collision_masks();
        obj_sprite = obstacle_traffic_indexed_sprite;
        obj_src_w = GAME2_TRAFFIC_SPR_W;
        obj_src_h = GAME2_TRAFFIC_SPR_H;
    } else if (obj->type == 1) {
        obj_sprite = obstacle_yellow_sprite;
    } else if (obj->type == 3) {
        obj_sprite = obstacle_boost_sprite;
    }

    if (obj_sprite != 0) {
        int16_t draw_x = (int16_t)(x - (obj_size / 2));
        int16_t draw_y = (int16_t)(y - (obj_size / 2));
        draw_indexed_sprite_resized_clipped(draw_x,
                                            draw_y,
                                            obj_src_h,
                                            obj_src_w,
                                            obj_sprite,
                                            (uint16_t)obj_size,
                                            (uint16_t)obj_size,
                                            0,
                                            ST7789V2_WIDTH - 1,
                                            0,
                                            ROAD_BOTTOM_Y);
    } else {
        int16_t gate_half = (int16_t)(half - (half / 8));
        int16_t gate_left = (int16_t)(center - gate_half);
        int16_t gate_width = (int16_t)(gate_half * 2);
        int16_t scale = (int16_t)(1 + ((y - ROAD_HORIZON_Y) * 3) / (ROAD_BOTTOM_Y - ROAD_HORIZON_Y));
        int16_t gate_h = (int16_t)(2 + scale);

        if (gate_left < 1) {
            gate_left = 1;
        }
        if (gate_left + gate_width >= ST7789V2_WIDTH - 1) {
            gate_width = (int16_t)(ST7789V2_WIDTH - 2 - gate_left);
        }

        if (gate_width > 6) {
            game2_draw_rect_fast(gate_left, (int16_t)(y - gate_h), gate_width, (int16_t)(gate_h + 1), 3, 0);
            game2_draw_hline_fast(gate_left, (int16_t)(gate_left + gate_width - 1), (int16_t)(y - gate_h / 2), 3);
        }
    }
}

static void draw_player_car(void) {
    uint8_t pose_idx = player_pose_index_current();
#if USE_CAR_RGB565_SPRITES
    const uint8_t* car_sprite_indexed = game2_player_car_indexed_sprite(pose_idx);
#else
    const uint8_t* car_sprite_rgb565 = player_car_sprite_by_pose(pose_idx);
#endif
    uint8_t flash_on = 0;

    int16_t base_x = (ST7789V2_WIDTH / 2) - (int16_t)(CAR_RENDER_W / 2);
    int16_t base_y = (int16_t)(ST7789V2_HEIGHT - CAR_RENDER_H - 2);

#if USE_CAR_RGB565_SPRITES
    draw_indexed_sprite_resized_clipped(base_x,
                                        base_y,
                                        CAR_SPR_HD_H,
                                        CAR_SPR_HD_W,
                                        car_sprite_indexed,
                                        (uint16_t)CAR_RENDER_W,
                                        (uint16_t)CAR_RENDER_H,
                                        0,
                                        ST7789V2_WIDTH - 1,
                                        0,
                                        ST7789V2_HEIGHT - 1);
#endif

    if (player_flash_timer_ms > 0u) {
        uint16_t flash_total_ms = (uint16_t)(PLAYER_FLASH_TOGGLE_MS * PLAYER_FLASH_TOTAL_TOGGLES);
        uint16_t elapsed_ms = (uint16_t)(flash_total_ms - player_flash_timer_ms);
        if (((elapsed_ms / PLAYER_FLASH_TOGGLE_MS) & 0x01u) == 0u) {
            flash_on = 1;
        }
    }

#if USE_CAR_RGB565_SPRITES
    // Cached indexed car sprite has already been drawn above.
#else
    LCD_Draw_Sprite_RGB565_Scaled((uint16_t)base_x,
                                  (uint16_t)base_y,
                                  CAR_SPR_HD_H,
                                  CAR_SPR_HD_W,
                                  car_sprite_rgb565,
                                  1,
                                  RGB565_TRANSPARENT);
#endif

    if (flash_on) {
        for (uint16_t py = 0; py < CAR_SPR_HD_H; py++) {
            for (uint16_t px = 0; px < CAR_SPR_HD_W; px++) {
#if USE_CAR_RGB565_SPRITES
                uint8_t pix = car_sprite_indexed[py * CAR_SPR_HD_W + px];
                if (pix != 255u) {
#else
                uint32_t idx = (uint32_t)(py * CAR_SPR_HD_W + px) * 2u;
                uint16_t pix = (uint16_t)(((uint16_t)car_sprite_rgb565[idx] << 8) | car_sprite_rgb565[idx + 1u]);
                if (pix != RGB565_TRANSPARENT) {
#endif
                    int16_t fx0 = (int16_t)(base_x + (int16_t)(((uint32_t)px * CAR_RENDER_W) / CAR_SPR_HD_W));
                    int16_t fy0 = (int16_t)(base_y + (int16_t)(((uint32_t)py * CAR_RENDER_H) / CAR_SPR_HD_H));
                    int16_t fx1 = (int16_t)(base_x + (int16_t)((((uint32_t)px + 1u) * CAR_RENDER_W) / CAR_SPR_HD_W) - 1);
                    int16_t fy1 = (int16_t)(base_y + (int16_t)((((uint32_t)py + 1u) * CAR_RENDER_H) / CAR_SPR_HD_H) - 1);
                    if (fx1 < fx0) {
                        fx1 = fx0;
                    }
                    if (fy1 < fy0) {
                        fy1 = fy0;
                    }
                    game2_draw_rect_fast(fx0,
                                         fy0,
                                         (int16_t)(fx1 - fx0 + 1),
                                         (int16_t)(fy1 - fy0 + 1),
                                         1,
                                         1);
                }
            }
        }
    }

    if (boost_active) {
        int16_t exhaust_shift_x = 0;
        int16_t left_x;
        int16_t right_x;
        int16_t tail_y = (int16_t)(base_y + CAR_RENDER_H - 5);
        int16_t flick = (int16_t)(frame_counter & 0x03u);
        int16_t h_outer = (int16_t)(10 + (flick & 0x01u));
        int16_t h_mid = (int16_t)(8 + (flick & 0x01u));
        int16_t h_inner = (int16_t)(6 + (flick & 0x01u));

        // When car sprite leans left, the rear visually shifts right (and vice versa).
        if (pose_idx == 0u) {
            exhaust_shift_x = BOOST_FLAME_SHIFT_HARD_PX;
        } else if (pose_idx == 1u) {
            exhaust_shift_x = BOOST_FLAME_SHIFT_SOFT_PX;
        } else if (pose_idx == 2u) {
            exhaust_shift_x = BOOST_FLAME_SHIFT_LITTLE_PX;
        } else if (pose_idx == 4u) {
            exhaust_shift_x = -BOOST_FLAME_SHIFT_LITTLE_PX;
        } else if (pose_idx == 5u) {
            exhaust_shift_x = -BOOST_FLAME_SHIFT_SOFT_PX;
        } else if (pose_idx == 6u) {
            exhaust_shift_x = -BOOST_FLAME_SHIFT_HARD_PX;
        }

        left_x = (int16_t)(base_x + 7 + exhaust_shift_x);
        right_x = (int16_t)(base_x + CAR_RENDER_W - 8 + exhaust_shift_x);

        if (tail_y - h_outer >= 0) {
            // Left nozzle: wider layered plume.
            LCD_Draw_Line((uint16_t)(left_x - 1), (uint16_t)tail_y,
                          (uint16_t)(left_x - 6), (uint16_t)(tail_y - h_outer), 2);
            LCD_Draw_Line((uint16_t)left_x, (uint16_t)tail_y,
                          (uint16_t)(left_x - 5), (uint16_t)(tail_y - h_outer), 2);
            LCD_Draw_Line((uint16_t)(left_x + 1), (uint16_t)tail_y,
                          (uint16_t)(left_x - 4), (uint16_t)(tail_y - h_outer), 2);

            LCD_Draw_Line((uint16_t)(left_x - 1), (uint16_t)(tail_y - 1),
                          (uint16_t)(left_x - 5), (uint16_t)(tail_y - h_mid), 5);
            LCD_Draw_Line((uint16_t)left_x, (uint16_t)(tail_y - 1),
                          (uint16_t)(left_x - 4), (uint16_t)(tail_y - h_mid), 5);
            LCD_Draw_Line((uint16_t)(left_x + 1), (uint16_t)(tail_y - 1),
                          (uint16_t)(left_x - 3), (uint16_t)(tail_y - h_mid), 5);

            LCD_Draw_Line((uint16_t)left_x, (uint16_t)(tail_y - 1),
                          (uint16_t)(left_x - 3), (uint16_t)(tail_y - h_inner), 6);
            LCD_Draw_Line((uint16_t)(left_x + 1), (uint16_t)(tail_y - 2),
                          (uint16_t)(left_x - 2), (uint16_t)(tail_y - h_inner + 1), 1);

            // Right nozzle: wider layered plume.
            LCD_Draw_Line((uint16_t)(right_x + 1), (uint16_t)tail_y,
                          (uint16_t)(right_x + 6), (uint16_t)(tail_y - h_outer), 2);
            LCD_Draw_Line((uint16_t)right_x, (uint16_t)tail_y,
                          (uint16_t)(right_x + 5), (uint16_t)(tail_y - h_outer), 2);
            LCD_Draw_Line((uint16_t)(right_x - 1), (uint16_t)tail_y,
                          (uint16_t)(right_x + 4), (uint16_t)(tail_y - h_outer), 2);

            LCD_Draw_Line((uint16_t)(right_x + 1), (uint16_t)(tail_y - 1),
                          (uint16_t)(right_x + 5), (uint16_t)(tail_y - h_mid), 5);
            LCD_Draw_Line((uint16_t)right_x, (uint16_t)(tail_y - 1),
                          (uint16_t)(right_x + 4), (uint16_t)(tail_y - h_mid), 5);
            LCD_Draw_Line((uint16_t)(right_x - 1), (uint16_t)(tail_y - 1),
                          (uint16_t)(right_x + 3), (uint16_t)(tail_y - h_mid), 5);

            LCD_Draw_Line((uint16_t)right_x, (uint16_t)(tail_y - 1),
                          (uint16_t)(right_x + 3), (uint16_t)(tail_y - h_inner), 6);
            LCD_Draw_Line((uint16_t)(right_x - 1), (uint16_t)(tail_y - 2),
                          (uint16_t)(right_x + 2), (uint16_t)(tail_y - h_inner + 1), 1);
        }
    }
}

static void draw_boost_speed_lines(void) {
    if (!boost_active) {
        return;
    }

    int16_t cx = (int16_t)(ST7789V2_WIDTH / 2);
    int16_t phase = (int16_t)(frame_counter & 0x1Fu);

    for (uint8_t i = 0; i < 10; i++) {
        int16_t base_y = (int16_t)(8 + i * 22);
        int16_t base_x = (int16_t)(10 + i * 24);
        int16_t travel = (int16_t)(8 + ((phase + i * 3) & 0x1Fu));

        base_y = (int16_t)(base_y % (ST7789V2_HEIGHT - 10));
        base_x = (int16_t)(base_x % (ST7789V2_WIDTH - 10));

        // From left border toward center, length grows every frame.
        LCD_Draw_Line(1, (uint16_t)base_y,
                      (uint16_t)clamp_s16((int16_t)(1 + travel), 0, cx - 6),
                      (uint16_t)clamp_s16((int16_t)(base_y + 2), 0, ST7789V2_HEIGHT - 1), 1);

        // From right border toward center.
        LCD_Draw_Line((uint16_t)(ST7789V2_WIDTH - 2), (uint16_t)base_y,
                      (uint16_t)clamp_s16((int16_t)(ST7789V2_WIDTH - 2 - travel), cx + 6, ST7789V2_WIDTH - 1),
                      (uint16_t)clamp_s16((int16_t)(base_y + 2), 0, ST7789V2_HEIGHT - 1), 1);

        // Afterimage head accent.
        if ((i & 0x01u) == 0u) {
            LCD_Set_Pixel((uint16_t)clamp_s16((int16_t)(2 + travel), 0, ST7789V2_WIDTH - 1), (uint16_t)base_y, 14);
            LCD_Set_Pixel((uint16_t)clamp_s16((int16_t)(ST7789V2_WIDTH - 3 - travel), 0, ST7789V2_WIDTH - 1), (uint16_t)base_y, 14);
        }
    }
}

static void draw_hud(void) {
    char line[32];
    const int16_t panel_x = 4;
    const int16_t panel_y = 4;
    const int16_t panel_w = (int16_t)(ST7789V2_WIDTH - 8);
    const int16_t panel_h = 28;
    const int16_t inner_x = (int16_t)(panel_x + 4);
    const int16_t gauge_gap = 4;
    const int16_t gauge_w = (int16_t)((panel_w - 8 - gauge_gap * 2) / 3);
    const int16_t gauge_y = (int16_t)(panel_y + 14);
    const int16_t gauge_h = 8;

    int16_t x_spd = inner_x;
    int16_t x_boost = (int16_t)(x_spd + gauge_w + gauge_gap);
    int16_t x_hp = (int16_t)(x_boost + gauge_w + gauge_gap);

    int16_t speed_now = (int16_t)(speed_q8 >> Q8_SHIFT);
    int16_t speed_max = (int16_t)(BOOST_ACTIVE_SPEED_Q8 >> Q8_SHIFT);
    int16_t spd_fill_w;
    int16_t bst_fill_w;
    int16_t hp_fill_w;
    uint8_t boost_pct = boost_meter_percent();
    uint8_t boost_col = boost_active ? 6 : 14;

    if (speed_now < 0) {
        speed_now = 0;
    }
    if (speed_now > speed_max) {
        speed_now = speed_max;
    }

    spd_fill_w = (int16_t)(((gauge_w - 2) * speed_now) / speed_max);
    if (spd_fill_w < 0) {
        spd_fill_w = 0;
    }

    bst_fill_w = (int16_t)(((gauge_w - 2) * boost_pct) / 100);
    hp_fill_w = (int16_t)(((gauge_w - 2) * player_hp) / rt_player_hp_max);

    if (!boost_active && boost_pct >= 100u) {
        boost_col = ((frame_counter & 0x04u) == 0u) ? 2 : 4;
    }

    game2_draw_rect_fast(panel_x, panel_y, panel_w, panel_h, 9, 1);
    game2_draw_rect_fast(panel_x, panel_y, panel_w, panel_h, 1, 0);

    LCD_printString("SPD", x_spd, panel_y + 4, 1, 1);
    LCD_printString("BST", x_boost, panel_y + 4, 1, 1);
    LCD_printString("HP", x_hp, panel_y + 4, 1, 1);

    game2_draw_rect_fast(x_spd, gauge_y, gauge_w, gauge_h, 1, 0);
    game2_draw_rect_fast(x_boost, gauge_y, gauge_w, gauge_h, 1, 0);
    game2_draw_rect_fast(x_hp, gauge_y, gauge_w, gauge_h, 1, 0);

    for (uint8_t k = 1; k < 4; k++) {
        int16_t tick_off = (int16_t)((k * (gauge_w - 2)) / 4);
        LCD_Draw_Line((uint16_t)(x_spd + 1 + tick_off), (uint16_t)gauge_y,
                      (uint16_t)(x_spd + 1 + tick_off), (uint16_t)(gauge_y + gauge_h - 1), 8);
        LCD_Draw_Line((uint16_t)(x_boost + 1 + tick_off), (uint16_t)gauge_y,
                      (uint16_t)(x_boost + 1 + tick_off), (uint16_t)(gauge_y + gauge_h - 1), 8);
        LCD_Draw_Line((uint16_t)(x_hp + 1 + tick_off), (uint16_t)gauge_y,
                      (uint16_t)(x_hp + 1 + tick_off), (uint16_t)(gauge_y + gauge_h - 1), 8);
    }

    if (spd_fill_w > 0) {
        uint8_t spd_col = boost_active ? 2 : ((speed_now > 200) ? 14 : 1);
        game2_draw_rect_fast((int16_t)(x_spd + 1), (int16_t)(gauge_y + 1), spd_fill_w, (int16_t)(gauge_h - 2), spd_col, 1);
    }
    if (bst_fill_w > 0) {
        game2_draw_rect_fast((int16_t)(x_boost + 1), (int16_t)(gauge_y + 1), bst_fill_w, (int16_t)(gauge_h - 2), boost_col, 1);
    }
    if (hp_fill_w > 0) {
        uint8_t hp_col = (player_hp > 60) ? 3 : ((player_hp > 30) ? 6 : 5);
        game2_draw_rect_fast((int16_t)(x_hp + 1), (int16_t)(gauge_y + 1), hp_fill_w, (int16_t)(gauge_h - 2), hp_col, 1);
    }

    // Time and distance are shown outside the dashboard block.
    game2_draw_rect_fast(4, 36, 86, 14, 0, 1);
    game2_draw_rect_fast(4, 36, 86, 14, 1, 0);
    sprintf(line, "TIME:%2ld", time_left_ms / 1000);
    LCD_printString(line, 8, 40, 1, 1);

    game2_draw_rect_fast((int16_t)(ST7789V2_WIDTH - 94), 36, 90, 14, 0, 1);
    game2_draw_rect_fast((int16_t)(ST7789V2_WIDTH - 94), 36, 90, 14, 1, 0);
    sprintf(line, "DST:%4lu", distance_score_units);
    LCD_printString(line, (uint16_t)(ST7789V2_WIDTH - 90), 40, 1, 1);

    if (hud_penalty_timer_ms > 0u && hud_penalty_show_ms > 0) {
        int32_t tenths = (hud_penalty_show_ms + 50) / 100;
        int32_t whole = tenths / 10;
        int32_t frac = tenths % 10;
        sprintf(line, "-%ld.%lds", whole, frac);
        LCD_printString(line, 52, 40, 5, 1);
    }

    if (!game_over) {
        LCD_printString("BL/JS Exit", 8, ST7789V2_HEIGHT - 12, 1, 1);
    } else {
        game2_draw_rect_fast(44, 100, 152, 64, 0, 1);
        game2_draw_rect_fast(44, 100, 152, 64, 2, 0);
        LCD_printString("GAME OVER", 66, 112, 2, 2);
        LCD_printString("BL Retry", 78, 136, 1, 1);
        LCD_printString("BW Menu", 79, 148, 1, 1);
    }
}

static void game2_draw_title_turbo_overdrive(void);
static void game2_run_map_menu(void);
static void game2_run_vehicle_menu(void);

static void game2_draw_main_menu(uint8_t selected_row) {
    char line[32];
    const char *diff_name = game2_difficulty_cfg[game2_selected_difficulty].name;
    const char *map_name = game2_map_names[game2_selected_map];
    const char *vehicle_name = game2_vehicle_cfg[game2_selected_vehicle].name;

    LCD_Fill_Buffer(0);
    LCD_Draw_Rect(0, 0, ST7789V2_WIDTH, 48, 9, 1);
    LCD_Draw_Rect(0, 0, ST7789V2_WIDTH, 48, 1, 0);
    game2_draw_title_turbo_overdrive();
    LCD_printString(GAME2_TITLE_TEXT, 72, 32, 1, 1);

    LCD_Draw_Rect(16, 54, ST7789V2_WIDTH - 32, 26, 13, 0);
    sprintf(line, "BEST DIST: %4lu", game2_best_distance_units);
    LCD_printString(line, 34, 62, 1, 1);

    LCD_Draw_Rect(16, 84, ST7789V2_WIDTH - 32, 22, (selected_row == 0u) ? 6 : 8, 0);
    LCD_printString((selected_row == 0u) ? "> START GAME" : "  START GAME", 28, 91, (selected_row == 0u) ? 6 : 1, 1);

    LCD_Draw_Rect(16, 110, ST7789V2_WIDTH - 32, 22, (selected_row == 1u) ? 6 : 8, 0);
    LCD_printString((selected_row == 1u) ? "> MAP" : "  MAP", 28, 117, (selected_row == 1u) ? 6 : 1, 1);
    LCD_printString(map_name, 156, 117, (selected_row == 1u) ? 6 : 14, 1);

    LCD_Draw_Rect(16, 136, ST7789V2_WIDTH - 32, 22, (selected_row == 2u) ? 6 : 8, 0);
    sprintf(line, "%s", diff_name);
    LCD_printString((selected_row == 2u) ? "> DIFFICULTY" : "  DIFFICULTY", 28, 143, (selected_row == 2u) ? 6 : 1, 1);
    LCD_printString(line, 156, 143, (selected_row == 2u) ? 6 : 14, 1);

    LCD_Draw_Rect(16, 162, ST7789V2_WIDTH - 32, 22, (selected_row == 3u) ? 6 : 8, 0);
    LCD_printString((selected_row == 3u) ? "> VEHICLE" : "  VEHICLE", 28, 169, (selected_row == 3u) ? 6 : 1, 1);
    LCD_printString(vehicle_name, 154, 169, (selected_row == 3u) ? 6 : 14, 1);

    LCD_Draw_Rect(16, 188, ST7789V2_WIDTH - 32, 22, (selected_row == 4u) ? 6 : 8, 0);
    LCD_printString((selected_row == 4u) ? "> HOW TO PLAY" : "  HOW TO PLAY", 28, 195, (selected_row == 4u) ? 6 : 1, 1);

    LCD_printString("JOY UP/DOWN: Select", 18, 216, 13, 1);
    LCD_printString("BW: OK   BL/JS: Exit", 18, 228, 13, 1);
    LCD_Refresh(&cfg0);
}

static void game2_draw_title_turbo_overdrive(void) {
    const char *title = "TURBO OVERDRIVE";
    const uint16_t x = 30u;
    const uint16_t y = 10u;

    // Layered overprint title for a stacked multi-font-like look.
    LCD_printString(title, (uint16_t)(x + 2u), (uint16_t)(y + 2u), 0, 2);
    LCD_printString(title, (uint16_t)(x + 1u), (uint16_t)(y + 1u), 9, 2);
    LCD_printString(title, (uint16_t)(x - 1u), y, 14, 2);
    LCD_printString(title, x, y, 6, 2);
}

static void game2_draw_help_page(uint8_t page_idx) {
    LCD_Fill_Buffer(0);
    LCD_Draw_Rect(0, 0, ST7789V2_WIDTH, 40, 9, 1);
    LCD_Draw_Rect(0, 0, ST7789V2_WIDTH, 40, 1, 0);
    LCD_printString("HOW TO PLAY", 72, 12, 1, 1);

    if (page_idx == 0u) {
        LCD_printString("Traffic Car", 20, 52, 1, 1);
        init_collision_masks();
        draw_indexed_sprite_resized_clipped(24, 72, GAME2_TRAFFIC_SPR_H, GAME2_TRAFFIC_SPR_W,
                                            obstacle_traffic_indexed_sprite, 40u, 40u, 0, ST7789V2_WIDTH - 1, 0, ST7789V2_HEIGHT - 1);
        LCD_printString("Hit: Heavy slow", 94, 86, 1, 1);
        LCD_printString("and HP damage", 94, 100, 1, 1);
    } else if (page_idx == 1u) {
        LCD_printString("Yellow Hazard", 20, 52, 1, 1);
        draw_indexed_sprite_resized_clipped(24, 72, GAME2_OBSTACLE_SPR_H, GAME2_OBSTACLE_SPR_W,
                                            obstacle_yellow_sprite, 40u, 40u, 0, ST7789V2_WIDTH - 1, 0, ST7789V2_HEIGHT - 1);
        LCD_printString("Hit: medium slow", 94, 86, 1, 1);
        LCD_printString("and time penalty", 94, 100, 1, 1);
    } else if (page_idx == 2u) {
        LCD_printString("Checkpoint Gate", 20, 52, 1, 1);
        LCD_Draw_Rect(24, 88, 88, 8, 3, 0);
        LCD_Draw_Line(24, 92, 111, 92, 3);
        LCD_printString("Pass: +time", 124, 84, 1, 1);
        LCD_printString("and +HP", 124, 98, 1, 1);
    } else if (page_idx == 3u) {
        LCD_printString("Boost Pickup", 20, 52, 1, 1);
        draw_indexed_sprite_resized_clipped(24, 72, GAME2_OBSTACLE_SPR_H, GAME2_OBSTACLE_SPR_W,
                                            obstacle_boost_sprite, 40u, 40u, 0, ST7789V2_WIDTH - 1, 0, ST7789V2_HEIGHT - 1);
        LCD_printString("Collect to charge", 94, 86, 1, 1);
        LCD_printString("BW to boost", 94, 100, 1, 1);
    } else if (page_idx == 4u) {
        // Zoomed top dashboard (SPD / BST / HP)
        LCD_printString("HUD: TOP DASHBOARD", 20, 52, 1, 1);

        LCD_Draw_Rect(20, 68, 200, 58, 9, 1);
        LCD_Draw_Rect(20, 68, 200, 58, 1, 0);

        LCD_printString("SPD", 30, 76, 1, 1);
        LCD_printString("BST", 90, 76, 1, 1);
        LCD_printString("HP", 150, 76, 1, 1);

        LCD_Draw_Rect(30, 92, 50, 12, 1, 0);
        LCD_Draw_Rect(90, 92, 50, 12, 1, 0);
        LCD_Draw_Rect(150, 92, 50, 12, 1, 0);

        LCD_Draw_Rect(32, 94, 32, 8, 14, 1);
        LCD_Draw_Rect(92, 94, 20, 8, 6, 1);
        LCD_Draw_Rect(152, 94, 40, 8, 3, 1);

        LCD_printString("SPD: current speed", 22, 138, 1, 1);
        LCD_printString("BST: boost energy", 22, 152, 1, 1);
        LCD_printString("HP : car health", 22, 166, 1, 1);
    } else {
        // Zoomed info area below dashboard
        LCD_printString("HUD: INFO PANELS", 20, 52, 1, 1);

        LCD_Draw_Rect(20, 68, 92, 22, 1, 0);
        LCD_Draw_Rect(128, 68, 92, 22, 1, 0);
        LCD_printString("TIME: 45", 28, 76, 1, 1);
        LCD_printString("DST: 128", 136, 76, 1, 1);

        LCD_Draw_Rect(64, 100, 112, 22, 5, 0);
        LCD_printString("-1.5s", 94, 108, 5, 1);

        LCD_printString("TIME: remaining time", 22, 138, 1, 1);
        LCD_printString("DST : total distance", 22, 152, 1, 1);
        LCD_printString("-Xs : recent penalty", 22, 166, 1, 1);
        LCD_printString("BL/JS Exit shown", 22, 180, 1, 1);
    }

    {
        char line[20];
        sprintf(line, "PAGE %u/%u", (unsigned)(page_idx + 1u), (unsigned)GAME2_HELP_PAGES);
        LCD_printString(line, 90, 196, 13, 1);
    }
    LCD_printString("JOY LEFT/RIGHT", 24, 214, 13, 1);
    LCD_printString("BW/BL/JS Back", 24, 228, 13, 1);
    LCD_Refresh(&cfg0);
}

static void game2_run_help_menu(void) {
    uint8_t page = 0u;
    uint8_t prev_left = 0u;
    uint8_t prev_right = 0u;

    while (1) {
        uint8_t left_now;
        uint8_t right_now;

        Input_Read();
        Joystick_Read(&joystick_cfg, &joystick_data);
        buzzer_service();
        game2_ws2812_show_menu_chase(HAL_GetTick());

        if (game2_force_console_return || game2_pc13_return_requested(HAL_GetTick())) {
            break;
        }

        left_now = (uint8_t)(joystick_data.direction == W || joystick_data.direction == NW || joystick_data.direction == SW);
        right_now = (uint8_t)(joystick_data.direction == E || joystick_data.direction == NE || joystick_data.direction == SE);

        if (left_now && !prev_left) {
            page = (uint8_t)((page + GAME2_HELP_PAGES - 1u) % GAME2_HELP_PAGES);
            Game2Music_PlayMenuMove();
        }
        if (right_now && !prev_right) {
            page = (uint8_t)((page + 1u) % GAME2_HELP_PAGES);
            Game2Music_PlayMenuMove();
        }

        prev_left = left_now;
        prev_right = right_now;

        game2_draw_help_page(page);

        if (current_input.btn2_pressed || game2_back_pressed()) {
            Game2Music_PlayMenuBack();
            break;
        }

        HAL_Delay(20);
    }
}

static void game2_run_map_menu(void) {
    uint8_t cursor = (uint8_t)game2_selected_map;
    uint8_t prev_up = 0u;
    uint8_t prev_down = 0u;

    while (1) {
        uint8_t up_now;
        uint8_t down_now;

        Input_Read();
        Joystick_Read(&joystick_cfg, &joystick_data);
        buzzer_service();
        game2_ws2812_show_menu_chase(HAL_GetTick());

        if (game2_force_console_return || game2_pc13_return_requested(HAL_GetTick())) {
            break;
        }

        up_now = (uint8_t)(joystick_data.direction == N || joystick_data.direction == NE || joystick_data.direction == NW);
        down_now = (uint8_t)(joystick_data.direction == S || joystick_data.direction == SE || joystick_data.direction == SW);

        if (up_now && !prev_up) {
            cursor = (uint8_t)((cursor + GAME2_MAP_COUNT - 1u) % GAME2_MAP_COUNT);
            Game2Music_PlayMenuMove();
        }
        if (down_now && !prev_down) {
            cursor = (uint8_t)((cursor + 1u) % GAME2_MAP_COUNT);
            Game2Music_PlayMenuMove();
        }

        prev_up = up_now;
        prev_down = down_now;

        LCD_Fill_Buffer(0);
        LCD_Draw_Rect(12, 18, ST7789V2_WIDTH - 24, 28, 9, 1);
        LCD_Draw_Rect(12, 18, ST7789V2_WIDTH - 24, 28, 1, 0);
        LCD_printString("SELECT MAP", 72, 28, 1, 1);

        for (uint8_t i = 0; i < GAME2_MAP_COUNT; i++) {
            uint16_t y = (uint16_t)(82 + i * 38);
            uint8_t selected = (uint8_t)(i == cursor);
            LCD_Draw_Rect(24, y, ST7789V2_WIDTH - 48, 26, selected ? 6 : 8, 0);
            LCD_printString(game2_map_names[i],
                            84,
                            (uint16_t)(y + 9),
                            selected ? 6 : 1,
                            1);
            if (selected) {
                LCD_printString(">", 54, (uint16_t)(y + 9), 6, 1);
            }
        }

        LCD_printString("Country: grass + houses", 24, 172, 13, 1);
        LCD_printString("City: concrete + towers", 24, 186, 13, 1);
        LCD_printString("BW: Apply", 24, 210, 13, 1);
        LCD_printString("BL/JS: Back", 24, 224, 13, 1);
        LCD_Refresh(&cfg0);

        if (current_input.btn2_pressed) {
            game2_apply_map((Game2Map)cursor);
            Game2Music_PlayMenuConfirm();
            break;
        }
        if (game2_back_pressed()) {
            Game2Music_PlayMenuBack();
            break;
        }

        HAL_Delay(20);
    }
}

static void game2_run_difficulty_menu(void) {
    uint8_t cursor = (uint8_t)game2_selected_difficulty;
    uint8_t prev_up = 0u;
    uint8_t prev_down = 0u;

    while (1) {
        uint8_t up_now;
        uint8_t down_now;

        Input_Read();
        Joystick_Read(&joystick_cfg, &joystick_data);
        buzzer_service();
        game2_ws2812_show_menu_chase(HAL_GetTick());

        if (game2_force_console_return || game2_pc13_return_requested(HAL_GetTick())) {
            break;
        }

        up_now = (uint8_t)(joystick_data.direction == N || joystick_data.direction == NE || joystick_data.direction == NW);
        down_now = (uint8_t)(joystick_data.direction == S || joystick_data.direction == SE || joystick_data.direction == SW);

        if (up_now && !prev_up) {
            cursor = (uint8_t)((cursor + GAME2_DIFFICULTY_COUNT - 1u) % GAME2_DIFFICULTY_COUNT);
            Game2Music_PlayMenuMove();
        }
        if (down_now && !prev_down) {
            cursor = (uint8_t)((cursor + 1u) % GAME2_DIFFICULTY_COUNT);
            Game2Music_PlayMenuMove();
        }

        prev_up = up_now;
        prev_down = down_now;

        LCD_Fill_Buffer(0);
        LCD_Draw_Rect(12, 18, ST7789V2_WIDTH - 24, 28, 9, 1);
        LCD_Draw_Rect(12, 18, ST7789V2_WIDTH - 24, 28, 1, 0);
        LCD_printString("SELECT DIFFICULTY", 34, 28, 1, 1);

        for (uint8_t i = 0; i < GAME2_DIFFICULTY_COUNT; i++) {
            uint16_t y = (uint16_t)(72 + i * 34);
            uint8_t selected = (uint8_t)(i == cursor);
            LCD_Draw_Rect(24, y, ST7789V2_WIDTH - 48, 24, selected ? 6 : 8, 0);
            LCD_printString(game2_difficulty_cfg[i].name,
                            84,
                            (uint16_t)(y + 8),
                            selected ? 6 : 1,
                            1);
            if (selected) {
                LCD_printString(">", 54, (uint16_t)(y + 8), 6, 1);
            }
        }

        LCD_printString("BW: Apply", 24, 206, 13, 1);
        LCD_printString("BL/JS: Back", 24, 220, 13, 1);
        LCD_Refresh(&cfg0);

        if (current_input.btn2_pressed) {
            game2_apply_difficulty((Game2Difficulty)cursor);
            Game2Music_PlayMenuConfirm();
            break;
        }
        if (game2_back_pressed()) {
            Game2Music_PlayMenuBack();
            break;
        }

        HAL_Delay(20);
    }
}

static void game2_draw_stat_bar(uint16_t x, uint16_t y, const char *label, uint8_t pct, uint8_t color) {
    uint16_t w = 84u;
    uint16_t h = 8u;
    uint16_t fill_w;

    if (pct > 100u) {
        pct = 100u;
    }

    fill_w = (uint16_t)(((w - 2u) * pct) / 100u);
    LCD_printString(label, x, (uint16_t)(y - 9u), 1, 1);
    LCD_Draw_Rect(x, y, w, h, 1, 0);
    if (fill_w > 0u) {
        LCD_Draw_Rect((uint16_t)(x + 1u), (uint16_t)(y + 1u), fill_w, (uint16_t)(h - 2u), color, 1);
    }
}

static void game2_draw_vehicle_menu(Game2Vehicle cursor) {
    const Game2VehicleCfg *cfg = &game2_vehicle_cfg[cursor];
    char title[32];

    LCD_Fill_Buffer(0);
    LCD_Draw_Rect(10, 6, ST7789V2_WIDTH - 20, 18, 9, 1);
    LCD_Draw_Rect(10, 6, ST7789V2_WIDTH - 20, 18, 1, 0);
    sprintf(title, "SELECT VEHICLE: %s", cfg->name);
    LCD_printString(title, 16, 12, 1, 1);

    game2_draw_stat_bar(8, 36, "Top Speed", cfg->ui_speed, 6);
    game2_draw_stat_bar(8, 54, "Handling", cfg->ui_handling, 14);
    game2_draw_stat_bar(8, 72, "HP", cfg->ui_hp, 3);
    game2_draw_stat_bar(128, 36, "Armor", cfg->ui_armor, 2);
    game2_draw_stat_bar(128, 54, "Boost Gain", cfg->ui_boost, 5);

    for (uint8_t i = 0; i < GAME2_VEHICLE_COUNT; i++) {
        uint16_t x = (uint16_t)(6u + i * 78u);
        uint16_t y = 96u;
        uint8_t selected = (uint8_t)(i == cursor);

        LCD_Draw_Rect(x, y, 72, 84, selected ? 6 : 8, 0);
#if !USE_CAR_RGB565_SPRITES
        LCD_Draw_Sprite_RGB565_Scaled((uint16_t)(x + 16u), (uint16_t)(y + 8u),
                                      CAR_SPR_HD_H, CAR_SPR_HD_W,
                                      game2_vehicle_hard_left_sprite((Game2Vehicle)i), 1,
                                      RGB565_TRANSPARENT);
#endif
        LCD_printString(game2_vehicle_cfg[i].name, (uint16_t)(x + 6u), (uint16_t)(y + 66u), selected ? 6 : 1, 1);
        if (selected) {
            LCD_printString("< >", (uint16_t)(x + 22u), (uint16_t)(y + 74u), 6, 1);
        }
    }

    LCD_printString("JOY LEFT/RIGHT: Select", 14, 188, 13, 1);
    LCD_printString("BW: OK", 14, 202, 13, 1);
    LCD_printString("BL/JS: Back", 14, 216, 13, 1);
    LCD_Refresh(&cfg0);

#if USE_CAR_RGB565_SPRITES
    for (uint8_t i = 0; i < GAME2_VEHICLE_COUNT; i++) {
        uint16_t x = (uint16_t)(6u + i * 78u);
        uint16_t y = 96u;
        game2_draw_rgb565_sprite_truecolor_scaled((uint16_t)(x + 16u),
                                                  (uint16_t)(y + 8u),
                                                  CAR_SPR_HD_H,
                                                  CAR_SPR_HD_W,
                                                  game2_vehicle_hard_left_sprite((Game2Vehicle)i),
                                                  1u,
                                                  RGB565_TRANSPARENT);
    }
#endif
}

static void game2_run_vehicle_menu(void) {
    Game2Vehicle cursor = game2_selected_vehicle;
    uint8_t prev_left = 0u;
    uint8_t prev_right = 0u;
    uint8_t need_redraw = 1u;

    while (1) {
        uint8_t left_now;
        uint8_t right_now;

        Input_Read();
        Joystick_Read(&joystick_cfg, &joystick_data);
        buzzer_service();
        game2_ws2812_show_menu_chase(HAL_GetTick());

        if (game2_force_console_return || game2_pc13_return_requested(HAL_GetTick())) {
            break;
        }

        left_now = (uint8_t)(joystick_data.direction == W || joystick_data.direction == NW || joystick_data.direction == SW);
        right_now = (uint8_t)(joystick_data.direction == E || joystick_data.direction == NE || joystick_data.direction == SE);

        if (left_now && !prev_left) {
            cursor = (Game2Vehicle)((cursor + GAME2_VEHICLE_COUNT - 1u) % GAME2_VEHICLE_COUNT);
            Game2Music_PlayMenuMove();
            need_redraw = 1u;
        }
        if (right_now && !prev_right) {
            cursor = (Game2Vehicle)((cursor + 1u) % GAME2_VEHICLE_COUNT);
            Game2Music_PlayMenuMove();
            need_redraw = 1u;
        }

        prev_left = left_now;
        prev_right = right_now;

        if (need_redraw) {
            game2_draw_vehicle_menu(cursor);
            need_redraw = 0u;
        }

        if (current_input.btn2_pressed) {
            game2_apply_vehicle(cursor);
            Game2Music_PlayMenuConfirm();
            break;
        }
        if (game2_back_pressed()) {
            Game2Music_PlayMenuBack();
            break;
        }

        HAL_Delay(20);
    }
}

static uint8_t game2_run_start_menu(void) {
    uint8_t selected_row = 0u;
    uint8_t prev_up = 0u;
    uint8_t prev_down = 0u;

    while (1) {
        uint8_t up_now;
        uint8_t down_now;

        Input_Read();
        Joystick_Read(&joystick_cfg, &joystick_data);
        buzzer_service();
        game2_ws2812_show_menu_chase(HAL_GetTick());

        if (game2_force_console_return || game2_pc13_return_requested(HAL_GetTick())) {
            return 0u;
        }

        up_now = (uint8_t)(joystick_data.direction == N || joystick_data.direction == NE || joystick_data.direction == NW);
        down_now = (uint8_t)(joystick_data.direction == S || joystick_data.direction == SE || joystick_data.direction == SW);

        if (up_now && !prev_up) {
            selected_row = (uint8_t)((selected_row + GAME2_MENU_ITEMS - 1u) % GAME2_MENU_ITEMS);
            Game2Music_PlayMenuMove();
        }
        if (down_now && !prev_down) {
            selected_row = (uint8_t)((selected_row + 1u) % GAME2_MENU_ITEMS);
            Game2Music_PlayMenuMove();
        }

        prev_up = up_now;
        prev_down = down_now;

        game2_draw_main_menu(selected_row);

        if (game2_back_pressed()) {
            Game2Music_PlayMenuBack();
            return 0u;
        }

        if (current_input.btn2_pressed) {
            Game2Music_PlayMenuConfirm();
            if (selected_row == 0u) {
                return 1u;
            }
            if (selected_row == 1u) {
                game2_run_map_menu();
            } else if (selected_row == 2u) {
                game2_run_difficulty_menu();
            } else if (selected_row == 3u) {
                game2_run_vehicle_menu();
            } else {
                game2_run_help_menu();
            }
        }

        HAL_Delay(20);
    }
}

static void reset_race_state(void) {
    game2_apply_difficulty(game2_selected_difficulty);

    init_road_projection_lut();
    init_collision_masks();

    player_lane_q8 = 0;
    steering_q8 = 0;
    speed_q8 = (130 << Q8_SHIFT);
    time_left_ms = 45000;
    distance_score_units = 0;
    distance_score_q8 = 0;
    frame_counter = 0;
    visual_tick_accum_ms = 0;
    player_hp = rt_player_hp_max;
    hud_penalty_show_ms = 0;
    hud_penalty_timer_ms = 0;
    game_over = 0;
    scrape_beep_tick = 0;
    checkpoint_next_spawn_units = checkpoint_spawn_interval_units();
    boost_meter_q8 = 0;
    boost_active = 0;
    player_flash_timer_ms = 0;
    yellow_sway_timer_ms = 0;
    yellow_sway_step_ms = 0;
    yellow_sway_dir = 1;
    player_pose_filtered_x = 0;
    player_pose_visual_idx = 3u;
    player_pose_step_timer_ms = 0u;
    game2_ws2812_reset_state();

    init_track();
    init_objects();
    init_roadside_house_render_state();
    maintain_object_density();
}

static void simulate_active_gameplay_step(uint16_t dt_ms, int16_t steer_target_q8) {
    int32_t steer_error;
    int32_t steer_delta;
    int32_t steer_response_div;

    if (dt_ms == 0u) {
        return;
    }

    if (current_input.btn2_down && !boost_active && boost_meter_q8 >= BOOST_METER_MAX_Q8) {
        boost_active = 1;
        game2_led_next_update_ms = 0u;
        Game2Music_PlayBoostTriggerSfx();
    }

    // Steering damping is now vehicle-specific via rt_steer_response_div.
    steer_response_div = (int32_t)rt_steer_response_div;
    if (steer_target_q8 == 0) {
        steer_response_div += STEER_RECENTER_EXTRA_DIV;
    }

    steer_error = (int32_t)steer_target_q8 - (int32_t)steering_q8;
    steer_delta = (steer_error * (int32_t)dt_ms) / (int32_t)(steer_response_div * GAME2_FRAME_TIME_MS);
    steering_q8 = (int16_t)((int32_t)steering_q8 + steer_delta);

    player_lane_q8 += (int16_t)((steering_q8 * (int16_t)dt_ms) / (int16_t)rt_steer_lane_move_div);

    // Entering a bend without matching steering pushes the car toward the outer edge.
    {
        int16_t curve_near = curvature_smoothed_q8(8 << Q8_SHIFT);
        int32_t curve_drift = -((int32_t)curve_near * (int32_t)(speed_q8 >> Q8_SHIFT) * (int32_t)dt_ms) / 42000;
        curve_drift = (curve_drift * (int32_t)rt_curve_throw_scale_pct) / 100;
        if (((int32_t)steering_q8 * (int32_t)curve_near) > 0) {
            // Steering into the bend counters centrifugal drift.
            curve_drift = (curve_drift * 2) / 5;
        } else {
            // No steering / wrong-way steering makes edge throw more obvious.
            curve_drift = (curve_drift * 6) / 5;
        }
        player_lane_q8 += (int16_t)curve_drift;
    }

    if (yellow_sway_timer_ms > 0u) {
        yellow_sway_step_ms = (uint16_t)(yellow_sway_step_ms + dt_ms);
        while (yellow_sway_step_ms >= YELLOW_SWAY_TOGGLE_MS) {
            yellow_sway_step_ms = (uint16_t)(yellow_sway_step_ms - YELLOW_SWAY_TOGGLE_MS);
            yellow_sway_dir = (int8_t)(-yellow_sway_dir);
        }

        if (dt_ms >= yellow_sway_timer_ms) {
            yellow_sway_timer_ms = 0;
            yellow_sway_step_ms = 0;
        } else {
            yellow_sway_timer_ms = (uint16_t)(yellow_sway_timer_ms - dt_ms);
        }
    }

    update_player_pose_smoothing(dt_ms);

    player_lane_q8 = clamp_s16(player_lane_q8, -PLAYER_MAX_LANE_Q8, PLAYER_MAX_LANE_Q8);

    // Speed control is bypassed while boost is active.
    if (!boost_active) {
        if (joystick_data.direction == N || joystick_data.direction == NW || joystick_data.direction == NE) {
            speed_q8 += (int32_t)(15 * (int32_t)dt_ms);
        } else if (joystick_data.direction == S || joystick_data.direction == SW || joystick_data.direction == SE) {
            speed_q8 -= (int32_t)(19 * (int32_t)dt_ms);
        } else {
            speed_q8 -= (int32_t)(5 * (int32_t)dt_ms);
        }
        speed_q8 = clamp_s32(speed_q8, (60 << Q8_SHIFT), rt_speed_cap_q8);
    } else {
        speed_q8 = BOOST_ACTIVE_SPEED_Q8;
    }

    // Scraping near guardrail slows the car and drains time.
    {
        int32_t lane_abs = abs_s32((int32_t)player_lane_q8);
        if (!boost_active && lane_abs > PLAYER_EDGE_LANE_Q8) {
            int32_t lane_span = (PLAYER_MAX_LANE_Q8 - PLAYER_EDGE_LANE_Q8);
            int32_t over = lane_abs - PLAYER_EDGE_LANE_Q8;
            int32_t intensity_q8 = (over << Q8_SHIFT) / lane_span;
            if (intensity_q8 > Q8_ONE) {
                intensity_q8 = Q8_ONE;
            }

            speed_q8 -= ((int32_t)(280 + (int16_t)((220 * intensity_q8) >> Q8_SHIFT)) * (int32_t)dt_ms);
            if (speed_q8 < (60 << Q8_SHIFT)) {
                speed_q8 = (60 << Q8_SHIFT);
            }

            {
                int32_t drain_per_sec_ms = (int32_t)(250 + ((500 * intensity_q8) >> Q8_SHIFT));
                int32_t scrape_penalty_ms = (drain_per_sec_ms * (int32_t)dt_ms) / 1000;
                time_left_ms -= scrape_penalty_ms;
                push_time_penalty_feedback(scrape_penalty_ms);
            }
            if (time_left_ms < 0) {
                time_left_ms = 0;
            }

            if ((HAL_GetTick() - scrape_beep_tick) > 180u) {
                play_beep(520, 14, 24);
                scrape_beep_tick = HAL_GetTick();
            }
        }
    }

    {
        int32_t delta_q8 = (speed_q8 * (int32_t)dt_ms) / 1000;
        consume_track_distance(delta_q8);
        update_objects_and_collisions(delta_q8);
        maintain_object_density();

        distance_score_q8 += (uint32_t)delta_q8;
        distance_score_units = (uint32_t)(distance_score_q8 >> Q8_SHIFT);
        try_spawn_checkpoint_by_distance();
    }

    if (boost_active) {
        boost_meter_q8 -= (int32_t)(((int64_t)BOOST_METER_MAX_Q8 * (int64_t)dt_ms) / BOOST_DURATION_MS);
        if (boost_meter_q8 <= 0) {
            boost_meter_q8 = 0;
            boost_active = 0;
            play_beep(1100, 22, 50);
        }
    }

    if (player_hp <= 0 && !game_over) {
        player_hp = 0;
        game_over = 1;
        Game2Music_StopBgm();
        Game2Music_PlayGameOverSfx();
    }

    if (hud_penalty_timer_ms > 0u) {
        if (dt_ms >= hud_penalty_timer_ms) {
            hud_penalty_timer_ms = 0;
            hud_penalty_show_ms = 0;
        } else {
            hud_penalty_timer_ms = (uint16_t)(hud_penalty_timer_ms - dt_ms);
        }
    }

    if (player_flash_timer_ms > 0u) {
        if (dt_ms >= player_flash_timer_ms) {
            player_flash_timer_ms = 0;
        } else {
            player_flash_timer_ms = (uint16_t)(player_flash_timer_ms - dt_ms);
        }
    }
}

static uint32_t game2_scaled_gameplay_dt(uint32_t raw_dt_ms) {
    uint32_t scaled = (raw_dt_ms * GAME2_GAMEPLAY_TIME_SCALE_PCT + 50u) / 100u;

    if (raw_dt_ms > 0u && scaled == 0u) {
        scaled = 1u;
    }
    if (scaled > GAME2_SIM_FALLBACK_MS) {
        scaled = GAME2_SIM_FALLBACK_MS;
    }

    return scaled;
}

static void game2_advance_visual_ticks(uint32_t raw_dt_ms) {
    visual_tick_accum_ms += raw_dt_ms;
    while (visual_tick_accum_ms >= GAME2_VISUAL_TICK_MS) {
        frame_counter++;
        visual_tick_accum_ms -= GAME2_VISUAL_TICK_MS;
    }
}

MenuState Game2_Run(void) {
    game2_force_console_return = 0U;
    game2_flash_load_best_record();
    LCD_Set_Palette(PALETTE_CUSTOM);

    while (1) {
        uint32_t prev_tick;

        if (!game2_run_start_menu()) {
            Game2Music_Stop();
            game2_ws2812_reset_state();
            buzzer_off(&buzzer_cfg);
            LCD_Set_Palette(PALETTE_DEFAULT);
            return MENU_STATE_HOME;
        }

        reset_race_state();
        Game2Music_Start();
        Game2Music_PlayMenuConfirm();
        prev_tick = HAL_GetTick();

        while (1) {
            uint8_t obj_was_active[MAX_OBJECTS];
            uint32_t frame_start = HAL_GetTick();
            uint32_t raw_dt_ms = frame_start - prev_tick;
            uint32_t dt_ms = game2_scaled_gameplay_dt(raw_dt_ms);
            uint32_t clock_dt_ms = raw_dt_ms;
            prev_tick = frame_start;

            if (clock_dt_ms > 250u) {
                clock_dt_ms = 250u;
            }
            game2_advance_visual_ticks(raw_dt_ms);

            Input_Read();
            Joystick_Read(&joystick_cfg, &joystick_data);
            buzzer_service();

            if (game2_force_console_return || game2_pc13_return_requested(frame_start)) {
                game2_commit_best_distance_if_needed(distance_score_units);
                Game2Music_Stop();
                game2_ws2812_reset_state();
                buzzer_off(&buzzer_cfg);
                LCD_Set_Palette(PALETTE_DEFAULT);
                return MENU_STATE_HOME;
            }

            if (game2_back_pressed()) {
                game2_commit_best_distance_if_needed(distance_score_units);
                Game2Music_Stop();
                game2_ws2812_reset_state();
                break;
            }

            for (uint8_t i = 0; i < MAX_OBJECTS; i++) {
                obj_was_active[i] = objects[i].active;
            }

            if (game_over) {
                if (current_input.btn2_pressed) {
                    reset_race_state();
                    Game2Music_Start();
                    Game2Music_PlayMenuConfirm();
                }
            } else {
                int16_t steer_target_q8 = 0;
                uint32_t sim_ms_remaining = dt_ms;

                if (joystick_data.direction == W || joystick_data.direction == NW || joystick_data.direction == SW) {
                    steer_target_q8 = -STEER_TARGET_Q8;
                } else if (joystick_data.direction == E || joystick_data.direction == NE || joystick_data.direction == SE) {
                    steer_target_q8 = STEER_TARGET_Q8;
                }
                while (sim_ms_remaining > 0u && !game_over) {
                    uint16_t step_ms;

                    if (sim_ms_remaining > GAME2_SIM_STEP_MS) {
                        step_ms = GAME2_SIM_STEP_MS;
                    } else {
                        step_ms = (uint16_t)sim_ms_remaining;
                    }

                    simulate_active_gameplay_step(step_ms, steer_target_q8);
                    sim_ms_remaining -= step_ms;
                }

                time_left_ms -= (int32_t)clock_dt_ms;
                if (!game_over && (time_left_ms <= 0 || player_hp <= 0)) {
                    time_left_ms = 0;
                    if (player_hp < 0) {
                        player_hp = 0;
                    }
                    game_over = 1;
                    Game2Music_StopBgm();
                    Game2Music_PlayGameOverSfx();
                    game2_commit_best_distance_if_needed(distance_score_units);
                }
            }

            draw_road_scanlines();
            draw_roadside_houses();

            for (uint8_t i = 0; i < MAX_OBJECTS; i++) {
                if (!objects[i].active) {
                    object_render_valid[i] = 0u;
                    continue;
                }

                {
                    int32_t target_q8 = objects[i].distance_q8;
                    int32_t render_q8 = target_q8;

                    if (!game_over && obj_was_active[i] && object_render_valid[i] && objects[i].type != 2u) {
                        render_q8 = smooth_render_distance_q8(object_render_distance_q8[i], target_q8);
                    }

                    object_render_distance_q8[i] = render_q8;
                    object_render_valid[i] = 1u;
                    draw_object_projected_at_distance_q8(&objects[i], render_q8);
                }
            }

            draw_boost_speed_lines();
            draw_player_car();
            draw_hud();
            draw_boost_panel();

            LCD_Refresh(&cfg0);
            game2_ws2812_update(HAL_GetTick());

            {
                uint32_t frame_time = HAL_GetTick() - frame_start;
                if (frame_time < GAME2_RENDER_WAIT_MS) {
                    HAL_Delay(GAME2_RENDER_WAIT_MS - frame_time);
                }
            }
        }

        Game2Music_Stop();
        game2_ws2812_reset_state();
    }
}
