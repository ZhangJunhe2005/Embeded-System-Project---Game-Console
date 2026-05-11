/* Game 1 module extracted from Core/Src/main.c. */
// Phase 1 minimal board runtime
// - Keep only joystick -> pseudo-3D background preview path
// - Disable game/audio/other systems for resource-focused bring-up

#include "Game_1.h"
#include "main.h"
#include "usart.h"
#include "gpio.h"
#include "adc.h"

void PowerMonitor_Init(void);
void PowerMonitor_Tick(uint32_t now);
void WS2812_Show_Pixels(const uint8_t *rgb, uint8_t count, uint8_t brightness);
void WS2812_Off(void);

#include "Joystick.h"
#include "ST7789V2_Driver.h"
#include "LayerBackgroundIndexed.h"
#include "GunImagesIndexed.h"
#include "KuwuIndexed.h"
#include "Robot1Indexed.h"
#include "BloodhoundIndexed.h"
#include "AdBgDebugRef.h"
#include "Audio.h"
#include "WeaponFireSfx8.h"
#include "Buzzer.h"
#include "LCD.h"
#include "InputHandler.h"

extern Buzzer_cfg_t buzzer_cfg;

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#define FLASH_BASE_ADDR 0x08000000UL
#define FLASH_TOTAL_BYTES (1024UL * 1024UL)
#define RAM_BASE_ADDR 0x20000000UL
#define RAM_TOTAL_BYTES (96UL * 1024UL)
#define FRAME_BUDGET_MS 16.67f
#define LCD_Y_OFFSET 0
#define ENABLE_RUNTIME_LOGS 0
#define PS2_TEST_MODE 0
#define QSPI_TEST_MODE 0
#define LCD_BLINK_ONLY_TEST 0
#define LCD_DIAG_MODE 0
#define LCD_DIAG_DMA_FILL 0
#define WEAPON_MODE_R99 0
#define WEAPON_MODE_KNIFE 1
#define WEAPON_LAYER_MODE WEAPON_MODE_KNIFE

#define PS2_RX_BUF_SIZE 64
#define PS2_RX_SAMPLE_DELAY_US 15U
#define PS2_SCREEN_TEST 1
#define PS2_PRINT_RAW 0
#define PS2_PACKET_LIMIT 80
#define PS2_CURSOR_PARTIAL_RENDER 1
#define AIM_INPUT_JOYSTICK 0
#define AIM_INPUT_MOUSE 1
#define AIM_INPUT_MODE AIM_INPUT_MOUSE
#define MOUSE_LOOK_GAIN_X_Q8 384
#define MOUSE_LOOK_GAIN_Y_Q8 384
#define MOUSE_ADS_GAIN_Q8 128
#define MOUSE_PACKET_LIMIT 32
#define MOUSE_FRAME_DELTA_LIMIT 18
#define MOUSE_BTN_LEFT 0x01U
#define MOUSE_BTN_RIGHT 0x02U
#define MOUSE_BTN_MIDDLE 0x04U
#define ROBOT_TEST_ENABLE 1
#define ROBOT_WORLD_X 240
#define ROBOT_WORLD_Y 220
#define ROBOT_GRID_X_STEP 12
#define ROBOT_GRID_Y_STEP (-3)
#define ROBOT_GRID_Y_TO_X 2
#define ROBOT_PATROL_RANGE_PX 28
#define ROBOT_PATROL_PERIOD_MS 2200U
#define ROBOT_RANDOM_RANGE_PX 52
#define ROBOT_RANDOM_STEP_PX 3
#define ROBOT_WALK_FRAME_MS 140U
#define ROBOT_DIE_FRAME_MS 80U
#define ROBOT_DIE_TIMELINE_FRAMES 12U
#define ROBOT_LEVEL_ADVANCE_DELAY_MS 2000U
#define ROBOT_ULTIMATE_BG_X 0
#define ROBOT_ULTIMATE_BG_Y (-2)
#define BLOODHOUND_WORLD_X 330
#define BLOODHOUND_WORLD_Y 260
#define BLOODHOUND_TRIGGER_RANGE_X 100
#define BLOODHOUND_TRIGGER_RANGE_Y 75
#define ROBOT_ARMOR_MAX 125
#define ROBOT_HEALTH_MAX 100
#define ROBOT_RESPAWN_DELAY_MS 2000U
#define R99_DAMAGE_PER_SHOT 13
#define FUZHU_DAMAGE_PER_SHOT 50
#define HEPING_CROSSHAIR_HALF 18
#define R99_MAG_SIZE 27U
#define HEPING_MAG_SIZE 5U
#define FUZHU_MAG_SIZE 5U
#define ULTIMATE_R99_MAG_LIMIT 3U
#define R99_FIRE_INTERVAL_MS 56U
#define R99_CRACK_SFX_HOLD_MS 220U
#define R99_KILL_SFX_HOLD_MS 900U
#define FUZHU_FIRE_INTERVAL_MS 240U
#define HEPING_FIRE_INTERVAL_MS 1000U
#define INA219_MONITOR_ENABLE 1
#define INA219_I2C_ADDR 0x40U
#define INA219_CALIBRATION_100UA 4096U
#define INA219_PIN_SCL GPIO_PIN_8
#define INA219_PIN_SDA GPIO_PIN_9
#define INA219_GPIO GPIOB
#define MENU_WS2812_TEST_ENABLE 1
#define MENU_WS2812_COUNT 10U
#define MENU_WS2812_BRIGHTNESS 6U
#define APEX_WS2812_COUNT MENU_WS2812_COUNT
#define APEX_WS2812_BRIGHTNESS_LIMIT 4U
#define APEX_WS2812_HP_BRIGHTNESS 3U
#define APEX_WS2812_FLASH_BRIGHTNESS 4U
#define APEX_WS2812_REFRESH_MS 120U
#define APEX_WS2812_KILL_STEP_MS 85U
#define MENU_WS2812_WHITE_TEST 1
#define MENU_WS2812_FORCE_BLACK_ONLY 0
#define MENU_WS2812_STARTUP_OFF_MS 2000U
#define WS2812_PWM_TIMER_HZ 80000000U
#define WS2812_PWM_BIT_HZ 800000U
#define WS2812_PWM_PERIOD_TICKS ((WS2812_PWM_TIMER_HZ / WS2812_PWM_BIT_HZ) - 1U)
#define WS2812_PWM_DUTY_0 28U
#define WS2812_PWM_DUTY_1 58U
#define WS2812_PWM_RESET_SLOTS 64U
#define WS2812_PWM_DMA_LEN ((MENU_WS2812_COUNT * 24U) + WS2812_PWM_RESET_SLOTS)

#if INA219_MONITOR_ENABLE
static uint8_t power_monitor_ready = 0U;
#endif

typedef enum {
  ROBOT_LEVEL_R99_FIXED = 0,
  ROBOT_LEVEL_HEPING_FIXED,
  ROBOT_LEVEL_FUZHU_RANDOM,
  ROBOT_LEVEL_ULTIMATE,
  ROBOT_LEVEL_COUNT,
} RobotLevel;

typedef enum {
  GAME_FLOW_INTRO_IDLE = 0,
  GAME_FLOW_INTRO,
  GAME_FLOW_STAGE_ACTIVE,
  GAME_FLOW_RETRY_IDLE,
  GAME_FLOW_RETRY_DIALOG,
  GAME_FLOW_END_DIALOG,
  GAME_FLOW_END_MENU,
  GAME_FLOW_END_IDLE,
  GAME_FLOW_SELECT_WEAPON,
  GAME_FLOW_SELECT_MOVE,
  GAME_FLOW_FREE_ACTIVE,
  GAME_FLOW_FREE_MENU,
  GAME_FLOW_FREE_REST,
} GameFlowState;

typedef enum {
  ROBOT_MOVE_FIXED = 0,
  ROBOT_MOVE_PATROL,
  ROBOT_MOVE_RANDOM,
  ROBOT_MOVE_ULTIMATE,
} RobotMoveKind;

static volatile uint8_t ps2_rx_buf[PS2_RX_BUF_SIZE];
static volatile uint8_t ps2_rx_head = 0;
static volatile uint8_t ps2_rx_tail = 0;
static volatile uint32_t ps2_rx_overflow = 0;
static volatile uint32_t ps2_frame_error = 0;
static volatile uint32_t ps2_parity_error = 0;
static volatile uint32_t ps2_clk_falling_edges = 0;
static volatile uint8_t ps2_last_err_byte = 0;
static volatile uint8_t ps2_last_err_parity = 0;
static volatile uint8_t ps2_last_err_stop = 0;
static volatile uint8_t ps2_last_err_ones = 0;
static volatile uint32_t ps2_phase_recovered = 0;
static volatile uint8_t ps2_phase_fix_enabled = 1U;
static volatile uint8_t ps2_tx_active = 0;
static uint8_t ps2_mouse_buttons = 0;
static uint32_t apex_led_next_update_ms = 0U;
static uint32_t apex_led_kill_start_ms = 0U;
static RobotLevel apex_led_kill_level = ROBOT_LEVEL_R99_FIXED;
static uint8_t apex_led_kill_active = 0U;
static uint8_t apex_led_cache_valid = 0U;
static uint8_t apex_led_last_mode = 0xFFU;
static uint8_t apex_led_last_level = 0xFFU;
static uint8_t apex_led_last_lit = 0xFFU;
static uint8_t apex_led_last_phase = 0xFFU;

static volatile uint8_t ps2_bit_count = 0;
static volatile uint8_t ps2_data_byte = 0;
static volatile uint8_t ps2_parity_bit = 0;

extern uint8_t __data_source_end;
extern uint8_t _ebss;

static uint16_t swap16(uint16_t v);
static int clamp_i(int v, int lo, int hi);
static int abs_i(int v);
static int qspi_bg_read_line(int x, int y, int src_y, uint16_t *line);
static void lcd_wait_dma_spi_idle(void);

typedef enum {
  R99_SFX_NONE,
  R99_SFX_WALL,
  R99_SFX_SHIELD,
  R99_SFX_BODY,
  R99_SFX_CRACK,
  R99_SFX_KILL,
} R99SfxKind;

typedef enum {
  FIRE_WEAPON_R99,
  FIRE_WEAPON_HEPING,
  FIRE_WEAPON_FUZHU,
} FireWeaponKind;

static const Audio_Sound_t *weapon_feedback_sound(FireWeaponKind weapon, R99SfxKind kind)
{
  if (weapon == FIRE_WEAPON_HEPING) {
    switch (kind) {
      case R99_SFX_WALL:
        return &heping_wall_sfx8;
      case R99_SFX_SHIELD:
        return &heping_shield_sfx8;
      case R99_SFX_BODY:
        return &heping_body_sfx8;
      case R99_SFX_CRACK:
        return &heping_crack_sfx8;
      case R99_SFX_KILL:
        return &heping_kill_sfx8;
      default:
        return NULL;
    }
  }
  if (weapon == FIRE_WEAPON_FUZHU) {
    switch (kind) {
      case R99_SFX_WALL:
        return &fuzhu_wall_sfx8;
      case R99_SFX_SHIELD:
        return &fuzhu_shield_sfx8;
      case R99_SFX_BODY:
        return &fuzhu_body_sfx8;
      case R99_SFX_CRACK:
        return &fuzhu_crack_sfx8;
      case R99_SFX_KILL:
        return &fuzhu_kill_sfx8;
      default:
        return NULL;
    }
  }
  switch (kind) {
    case R99_SFX_WALL:
      return &r99_wall_sfx8;
    case R99_SFX_SHIELD:
      return &r99_shield_sfx8;
    case R99_SFX_BODY:
      return &r99_body_sfx8;
    case R99_SFX_CRACK:
      return &r99_crack_sfx8;
    case R99_SFX_KILL:
      return &r99_kill_sfx8;
    default:
      return NULL;
  }
}

static const Audio_Sound_t *weapon_draw_sound(FireWeaponKind weapon)
{
  if (weapon == FIRE_WEAPON_HEPING) {
    return &heping_draw_sfx8;
  }
  if (weapon == FIRE_WEAPON_FUZHU) {
    return &fuzhu_draw_sfx8;
  }
  return &r99_draw_sfx8;
}

static const Audio_Sound_t *weapon_reload_sound(FireWeaponKind weapon)
{
  if (weapon == FIRE_WEAPON_HEPING) {
    return &heping_reload_sfx8;
  }
  if (weapon == FIRE_WEAPON_FUZHU) {
    return &fuzhu_reload_sfx8;
  }
  return &r99_reload_sfx8;
}

static void weapon_play_action_sfx(const Audio_Sound_t *sound)
{
  if (sound != NULL) {
    Audio_Play(sound);
  }
}

static uint32_t weapon_fire_interval_ms(FireWeaponKind weapon)
{
  if (weapon == FIRE_WEAPON_HEPING) {
    return HEPING_FIRE_INTERVAL_MS;
  }
  if (weapon == FIRE_WEAPON_FUZHU) {
    return FUZHU_FIRE_INTERVAL_MS;
  }
  return R99_FIRE_INTERVAL_MS;
}

static uint8_t weapon_is_single_shot(FireWeaponKind weapon)
{
  return (weapon == FIRE_WEAPON_HEPING || weapon == FIRE_WEAPON_FUZHU) ? 1U : 0U;
}

static uint8_t weapon_mag_size(FireWeaponKind weapon)
{
  if (weapon == FIRE_WEAPON_HEPING) {
    return HEPING_MAG_SIZE;
  }
  if (weapon == FIRE_WEAPON_FUZHU) {
    return FUZHU_MAG_SIZE;
  }
  return R99_MAG_SIZE;
}

static const char *weapon_display_name(FireWeaponKind weapon)
{
  if (weapon == FIRE_WEAPON_HEPING) {
    return "PEACEKEEPER";
  }
  if (weapon == FIRE_WEAPON_FUZHU) {
    return "WINGMAN";
  }
  return "R99";
}

static FireWeaponKind next_fire_weapon(FireWeaponKind weapon)
{
  if (weapon == FIRE_WEAPON_R99) {
    return FIRE_WEAPON_HEPING;
  }
  if (weapon == FIRE_WEAPON_HEPING) {
    return FIRE_WEAPON_FUZHU;
  }
  return FIRE_WEAPON_R99;
}

static FireWeaponKind weapon_for_robot_level(RobotLevel level)
{
  if (level == ROBOT_LEVEL_HEPING_FIXED) {
    return FIRE_WEAPON_HEPING;
  }
  if (level == ROBOT_LEVEL_FUZHU_RANDOM) {
    return FIRE_WEAPON_FUZHU;
  }
  return FIRE_WEAPON_R99;
}

static RobotLevel next_robot_level(RobotLevel level)
{
  return (RobotLevel)(((uint8_t)level + 1U) % (uint8_t)ROBOT_LEVEL_COUNT);
}

static int robot_armor_max_for_level(RobotLevel level)
{
  if (level == ROBOT_LEVEL_HEPING_FIXED) {
    return 75;
  }
  if (level == ROBOT_LEVEL_FUZHU_RANDOM) {
    return 100;
  }
  if (level == ROBOT_LEVEL_ULTIMATE) {
    return 125;
  }
  return 50;
}

static uint16_t robot_armor_color_for_level(RobotLevel level)
{
  if (level == ROBOT_LEVEL_HEPING_FIXED) {
    return swap16(0x04DFU);
  }
  if (level == ROBOT_LEVEL_FUZHU_RANDOM) {
    return swap16(0xA81FU);
  }
  if (level == ROBOT_LEVEL_ULTIMATE) {
    return swap16(0xFAE4U);
  }
  return swap16(0xBDF7U);
}

static uint8_t apex_ws2812_limit_brightness(uint8_t brightness)
{
  if (brightness > APEX_WS2812_BRIGHTNESS_LIMIT) {
    brightness = APEX_WS2812_BRIGHTNESS_LIMIT;
  }
  return brightness;
}

static void apex_ws2812_level_color(RobotLevel level, uint8_t *r, uint8_t *g, uint8_t *b)
{
  if (level == ROBOT_LEVEL_HEPING_FIXED) {
    *r = 0U;
    *g = 110U;
    *b = 255U;
  } else if (level == ROBOT_LEVEL_FUZHU_RANDOM) {
    *r = 170U;
    *g = 0U;
    *b = 255U;
  } else if (level == ROBOT_LEVEL_ULTIMATE) {
    *r = 255U;
    *g = 70U;
    *b = 0U;
  } else {
    *r = 220U;
    *g = 220U;
    *b = 220U;
  }
}

static void apex_ws2812_next_level_color(RobotLevel level, uint8_t *r, uint8_t *g, uint8_t *b)
{
  if (level == ROBOT_LEVEL_ULTIMATE) {
    *r = 255U;
    *g = 0U;
    *b = 0U;
    return;
  }
  apex_ws2812_level_color(next_robot_level(level), r, g, b);
}

static void apex_ws2812_clear_cache(void)
{
  apex_led_cache_valid = 0U;
  apex_led_last_mode = 0xFFU;
  apex_led_last_level = 0xFFU;
  apex_led_last_lit = 0xFFU;
  apex_led_last_phase = 0xFFU;
}

static void apex_ws2812_init(void)
{
  apex_led_next_update_ms = 0U;
  apex_led_kill_start_ms = 0U;
  apex_led_kill_level = ROBOT_LEVEL_R99_FIXED;
  apex_led_kill_active = 0U;
  apex_ws2812_clear_cache();
  WS2812_Off();
}

static void apex_ws2812_start_kill_fx(RobotLevel level, uint32_t now)
{
  apex_led_kill_level = level;
  apex_led_kill_start_ms = now;
  apex_led_kill_active = 1U;
  apex_led_next_update_ms = 0U;
  apex_ws2812_clear_cache();
}

static void apex_ws2812_show_total_bar(RobotLevel level, int armor, int health)
{
  uint8_t pixels[APEX_WS2812_COUNT * 3U];
  uint8_t r = 0U;
  uint8_t g = 0U;
  uint8_t b = 0U;
  uint8_t lit_count = 0U;
  int armor_max = robot_armor_max_for_level(level);
  int total_max = armor_max + ROBOT_HEALTH_MAX;
  int remain = clamp_i(armor, 0, armor_max) + clamp_i(health, 0, ROBOT_HEALTH_MAX);

  if (total_max > 0 && remain > 0) {
    lit_count = (uint8_t)(((uint32_t)remain * APEX_WS2812_COUNT + (uint32_t)total_max - 1U) / (uint32_t)total_max);
    if (lit_count == 0U) {
      lit_count = 1U;
    }
    if (lit_count > APEX_WS2812_COUNT) {
      lit_count = APEX_WS2812_COUNT;
    }
  }
  if (apex_led_cache_valid
      && apex_led_last_mode == 1U
      && apex_led_last_level == (uint8_t)level
      && apex_led_last_lit == lit_count) {
    return;
  }

  for (uint8_t i = 0; i < (APEX_WS2812_COUNT * 3U); i++) {
    pixels[i] = 0U;
  }
  apex_ws2812_level_color(level, &r, &g, &b);
  for (uint8_t i = 0; i < lit_count; i++) {
    pixels[(uint16_t)i * 3U] = r;
    pixels[(uint16_t)i * 3U + 1U] = g;
    pixels[(uint16_t)i * 3U + 2U] = b;
  }

  apex_led_cache_valid = 1U;
  apex_led_last_mode = 1U;
  apex_led_last_level = (uint8_t)level;
  apex_led_last_lit = lit_count;
  apex_led_last_phase = 0xFFU;
  WS2812_Show_Pixels(pixels, APEX_WS2812_COUNT, apex_ws2812_limit_brightness(APEX_WS2812_HP_BRIGHTNESS));
}

static void apex_ws2812_show_kill_fx(uint32_t now)
{
  uint8_t pixels[APEX_WS2812_COUNT * 3U];
  uint8_t from_r = 0U;
  uint8_t from_g = 0U;
  uint8_t from_b = 0U;
  uint8_t to_r = 0U;
  uint8_t to_g = 0U;
  uint8_t to_b = 0U;
  uint32_t elapsed = now - apex_led_kill_start_ms;
  uint8_t phase = (uint8_t)(elapsed / APEX_WS2812_KILL_STEP_MS);

  uint8_t phase_limit = (apex_led_kill_level == ROBOT_LEVEL_ULTIMATE) ? 12U : 14U;

  if (phase >= phase_limit) {
    apex_led_kill_active = 0U;
    apex_ws2812_clear_cache();
    return;
  }
  if (apex_led_cache_valid
      && apex_led_last_mode == 2U
      && apex_led_last_level == (uint8_t)apex_led_kill_level
      && apex_led_last_phase == phase) {
    return;
  }

  apex_ws2812_level_color(apex_led_kill_level, &from_r, &from_g, &from_b);
  apex_ws2812_next_level_color(apex_led_kill_level, &to_r, &to_g, &to_b);
  for (uint8_t i = 0; i < (APEX_WS2812_COUNT * 3U); i++) {
    pixels[i] = 0U;
  }

  if (apex_led_kill_level == ROBOT_LEVEL_ULTIMATE) {
    uint8_t on = (uint8_t)((phase & 1U) == 0U);
    uint8_t span = (uint8_t)(1U + (phase / 2U));
    for (uint8_t i = 0; i < APEX_WS2812_COUNT; i++) {
      uint8_t dist = (i < 5U) ? (uint8_t)(4U - i) : (uint8_t)(i - 5U);
      if (on && dist <= span) {
        pixels[(uint16_t)i * 3U] = 255U;
        pixels[(uint16_t)i * 3U + 1U] = (uint8_t)((phase < 6U) ? 20U : 0U);
        pixels[(uint16_t)i * 3U + 2U] = 0U;
      }
    }
  } else if (phase < 6U) {
    uint8_t sweep = (uint8_t)(((uint16_t)(phase + 1U) * APEX_WS2812_COUNT) / 6U);
    for (uint8_t i = 0; i < APEX_WS2812_COUNT; i++) {
      uint8_t use_to = (uint8_t)(i < sweep);
      pixels[(uint16_t)i * 3U] = use_to ? to_r : from_r;
      pixels[(uint16_t)i * 3U + 1U] = use_to ? to_g : from_g;
      pixels[(uint16_t)i * 3U + 2U] = use_to ? to_b : from_b;
    }
  } else {
    uint8_t on = (uint8_t)(((phase - 6U) & 1U) == 0U);
    if (on) {
      for (uint8_t i = 0; i < APEX_WS2812_COUNT; i++) {
        pixels[(uint16_t)i * 3U] = to_r;
        pixels[(uint16_t)i * 3U + 1U] = to_g;
        pixels[(uint16_t)i * 3U + 2U] = to_b;
      }
    }
  }

  apex_led_cache_valid = 1U;
  apex_led_last_mode = 2U;
  apex_led_last_level = (uint8_t)apex_led_kill_level;
  apex_led_last_phase = phase;
  apex_led_last_lit = 0xFFU;
  WS2812_Show_Pixels(pixels, APEX_WS2812_COUNT, apex_ws2812_limit_brightness(APEX_WS2812_FLASH_BRIGHTNESS));
}

static void apex_ws2812_update(uint32_t now,
                               GameFlowState flow,
                               RobotLevel level,
                               int armor,
                               int health,
                               uint32_t robot_dead_ms)
{
  uint8_t active = (uint8_t)((flow == GAME_FLOW_STAGE_ACTIVE || flow == GAME_FLOW_FREE_ACTIVE) ? 1U : 0U);
  if ((int32_t)(now - apex_led_next_update_ms) < 0) {
    return;
  }
  if (apex_led_kill_active) {
    apex_ws2812_show_kill_fx(now);
    apex_led_next_update_ms = now + APEX_WS2812_KILL_STEP_MS;
    return;
  }
  if (!active || robot_dead_ms != 0U) {
    if (!apex_led_cache_valid || apex_led_last_mode != 0U) {
      WS2812_Off();
      apex_led_cache_valid = 1U;
      apex_led_last_mode = 0U;
      apex_led_last_level = 0xFFU;
      apex_led_last_lit = 0U;
      apex_led_last_phase = 0xFFU;
    }
    apex_led_next_update_ms = now + APEX_WS2812_REFRESH_MS;
    return;
  }
  apex_ws2812_show_total_bar(level, armor, health);
  apex_led_next_update_ms = now + APEX_WS2812_REFRESH_MS;
}

static const char *stage_brief_line0(RobotLevel level)
{
  if (level == ROBOT_LEVEL_HEPING_FIXED) {
    return "NEXT TRIAL PATROL PREY";
  }
  if (level == ROBOT_LEVEL_FUZHU_RANDOM) {
    return "NEXT TRIAL RANDOM PREY";
  }
  if (level == ROBOT_LEVEL_ULTIMATE) {
    return "FINAL TRIAL PILOT PREY";
  }
  return "FIRST TRIAL PATROL PREY";
}

static const char *stage_brief_line1(RobotLevel level)
{
  if (level == ROBOT_LEVEL_HEPING_FIXED) {
    return "PEACEKEEPER AWAITS";
  }
  if (level == ROBOT_LEVEL_FUZHU_RANDOM) {
    return "WINGMAN AWAITS";
  }
  if (level == ROBOT_LEVEL_ULTIMATE) {
    return "R99 AWAITS";
  }
  return "R99 AWAITS";
}

static FireWeaponKind render_crosshair_weapon = FIRE_WEAPON_R99;
static uint8_t render_crosshair_ads = 0U;
static FireWeaponKind render_hud_weapon = FIRE_WEAPON_R99;
static uint8_t render_hud_visible = 0U;
static uint8_t render_hud_ammo = R99_MAG_SIZE;
static uint16_t render_stage_shots = 0U;
static uint16_t render_stage_hits = 0U;
static int8_t render_stage_result = 0;
static uint8_t render_dialog_visible = 0U;
static const char *render_dialog_line0 = "";
static const char *render_dialog_line1 = "";
static uint8_t render_menu_visible = 0U;
static const char *render_menu_a = "";
static const char *render_menu_b = "";
static const char *render_menu_c = "";
static uint8_t render_menu_selected = 0U;
static uint8_t render_bloodhound_visible = 0U;
static int render_bloodhound_x = 0;
static int render_bloodhound_y = 0;
static uint8_t render_bloodhound_frame = 0U;
#define HIT_PARTICLE_COUNT 24U
#define HIT_PARTICLE_STEP_MS 16U
typedef struct {
  uint8_t active;
  uint8_t life;
  uint8_t size;
  int16_t x_q4;
  int16_t y_q4;
  int8_t vx_q4;
  int8_t vy_q4;
  uint16_t color;
} HitParticle;
static HitParticle hit_particles[HIT_PARTICLE_COUNT];
static uint8_t hit_particle_next = 0U;
static uint32_t hit_particle_last_update_ms = 0U;
static uint8_t hit_particle_rng = 0x5AU;
#define ULTIMATE_TRAIL_COUNT 12U
#define ULTIMATE_TRAIL_SAMPLE_MS 45U
typedef struct {
  uint8_t active;
  int16_t bg_x;
  int16_t bg_y;
} UltimateTrailPoint;
static UltimateTrailPoint ultimate_trail[ULTIMATE_TRAIL_COUNT];
static uint8_t ultimate_trail_next = 0U;
static uint8_t ultimate_trail_filled = 0U;
static uint32_t ultimate_trail_last_sample_ms = 0U;
static int bg_src_x_dynamic = 0;
static int bg_src_y_dynamic = 0;
static int mouse_look_x = 0;
static int mouse_look_y = 0;
static int mouse_look_x_q8 = 0;
static int mouse_look_y_q8 = 0;
static uint8_t mouse_look_initialized = 0;
static const int8_t robot_random_step_pattern[] = {
  -20, -16, 22, -24, 18, -28, 24, -18, -22, 30, -26, 20, -18, 24,
};
static const uint16_t robot_random_hold_pattern[] = {
  180, 120, 220, 160, 260, 140, 240, 110, 180, 230, 130, 210, 250, 150,
};
static const char *intro_dialog[][2] = {
  {"I AM BLOTH HOONDR", "YOU CAN CALL ME BLOODHOUND"},
  {"I WILL GUIDE YOU", "THROUGH THE PROVING GROUND"},
  {"FOUR HUNTS AWAIT", "R99 PEACEKEEPER WINGMAN"},
  {"THE FINAL PREY", "MOVES LIKE A PILOT"},
  {"SURVIVE THE TRIAL", "AND BEGIN THE HUNT"},
};
static const char *retry_dialog[][2] = {
  {"DO NOT LET DOUBT", "TAKE ROOT"},
  {"READY YOURSELF", "HUNT AGAIN"},
};
static const char *end_dialog[][2] = {
  {"WELL FOUGHT", "FUTURE PILOT"},
  {"CHOOSE A TRIAL", "OR RETURN TO REST"},
  {"TRUE BATTLE", "WILL FIND YOU SOON"},
};
static const char *end_dialog_keep_training[][2] = {
  {"THE PATH IS LONG", "KEEP YOUR FOCUS"},
  {"TRAIN AGAIN", "UNTIL THE HUNT IS YOURS"},
  {"CHOOSE A TRIAL", "OR GO REST"},
};

static int robot_damage_for_weapon(FireWeaponKind weapon, uint8_t heping_coverage)
{
  if (weapon == FIRE_WEAPON_FUZHU) {
    return FUZHU_DAMAGE_PER_SHOT;
  }
  if (weapon == FIRE_WEAPON_HEPING) {
    if (heping_coverage == 0U) {
      return 0;
    }
    if (heping_coverage < 33U) {
      return 33;
    }
    if (heping_coverage <= 66U) {
      return 66;
    }
    return 99;
  }
  return R99_DAMAGE_PER_SHOT;
}

static uint8_t robot_level_passed(RobotLevel level, uint16_t shots, uint16_t hits)
{
  uint16_t acc = (shots == 0U) ? 0U : (uint16_t)(((uint32_t)hits * 100U) / (uint32_t)shots);
  if (level == ROBOT_LEVEL_R99_FIXED) {
    return (acc >= 30U) ? 1U : 0U;
  }
  if (level == ROBOT_LEVEL_HEPING_FIXED) {
    return (shots <= 8U) ? 1U : 0U;
  }
  if (level == ROBOT_LEVEL_FUZHU_RANDOM) {
    return (shots <= 10U) ? 1U : 0U;
  }
  if (level == ROBOT_LEVEL_ULTIMATE) {
    return (acc >= 10U) ? 1U : 0U;
  }
  return 1U;
}

static uint8_t robot_stage_passed(RobotLevel level, FireWeaponKind weapon, uint16_t shots, uint16_t hits)
{
  if (level == ROBOT_LEVEL_ULTIMATE) {
    if (weapon == FIRE_WEAPON_HEPING) {
      return (shots <= 8U) ? 1U : 0U;
    }
    return (shots <= (uint16_t)(R99_MAG_SIZE * ULTIMATE_R99_MAG_LIMIT)) ? 1U : 0U;
  }
  return robot_level_passed(level, shots, hits);
}

static FireWeaponKind menu_weapon_for_index(uint8_t idx)
{
  if (idx == 1U) {
    return FIRE_WEAPON_HEPING;
  }
  if (idx == 2U) {
    return FIRE_WEAPON_FUZHU;
  }
  return FIRE_WEAPON_R99;
}

static RobotLevel free_level_for_selection(FireWeaponKind weapon, uint8_t move_idx)
{
  if (move_idx == 2U) {
    return ROBOT_LEVEL_ULTIMATE;
  }
  if (weapon == FIRE_WEAPON_HEPING) {
    return ROBOT_LEVEL_HEPING_FIXED;
  }
  if (weapon == FIRE_WEAPON_FUZHU || move_idx == 1U) {
    return ROBOT_LEVEL_FUZHU_RANDOM;
  }
  return ROBOT_LEVEL_R99_FIXED;
}

static uint8_t bloodhound_frame_for_grid_y(int grid_y)
{
  uint8_t best = 0U;
  int best_d = 32767;
  for (uint8_t i = 0; i < BLOODHOUND_FRAME_COUNT; i++) {
    int d = abs_i(grid_y - (int)bloodhound_grid_y[i]);
    if (d < best_d) {
      best_d = d;
      best = i;
    }
  }
  return best;
}

static int gun_image_for_weapon(FireWeaponKind weapon, uint8_t ads)
{
  if (weapon == FIRE_WEAPON_HEPING) {
    return GUN_IMAGE_HEPING_NORMAL;
  }
  if (weapon == FIRE_WEAPON_FUZHU) {
    return ads ? GUN_IMAGE_FUZHU_ADS : GUN_IMAGE_FUZHU_NORMAL;
  }
  return ads ? GUN_IMAGE_R99_ADS : GUN_IMAGE_R99_NORMAL;
}

static int r99_sfx_priority(R99SfxKind kind)
{
  if (kind == R99_SFX_KILL) {
    return 3;
  }
  if (kind == R99_SFX_CRACK) {
    return 2;
  }
  return 1;
}

static void r99_play_feedback_sfx(R99SfxKind kind,
                                  FireWeaponKind weapon,
                                  R99SfxKind *last_kind,
                                  uint32_t *special_hold_until_ms,
                                  uint32_t now)
{
  const Audio_Sound_t *sound = weapon_feedback_sound(weapon, kind);
  if (sound == NULL) {
    return;
  }
  if (*last_kind == R99_SFX_KILL && kind != R99_SFX_KILL && Audio_IsPlaying()) {
    return;
  }
  if (*special_hold_until_ms != 0U && (int32_t)(now - *special_hold_until_ms) < 0) {
    if (r99_sfx_priority(kind) <= r99_sfx_priority(*last_kind)) {
      return;
    }
  }
  if (kind != *last_kind || !Audio_IsPlaying()) {
    Audio_Play(sound);
  }
  *last_kind = kind;
  if (kind == R99_SFX_KILL) {
    *special_hold_until_ms = now + R99_KILL_SFX_HOLD_MS;
  } else if (kind == R99_SFX_CRACK) {
    *special_hold_until_ms = now + R99_CRACK_SFX_HOLD_MS;
  } else {
    *special_hold_until_ms = 0U;
  }
}

static R99SfxKind robot_apply_damage_hit(int *armor, int *health, int damage)
{
  int old_armor = *armor;
  int old_health = *health;
  int dmg = damage;

  if (*armor > 0) {
    int armor_dmg = (*armor < dmg) ? *armor : dmg;
    *armor -= armor_dmg;
    dmg -= armor_dmg;
  }
  if (dmg > 0) {
    *health = clamp_i(*health - dmg, 0, ROBOT_HEALTH_MAX);
  }

  if (old_health > 0 && *health <= 0) {
    return R99_SFX_KILL;
  }
  if (old_armor > 0 && *armor <= 0) {
    return R99_SFX_CRACK;
  }
  if (old_armor > 0) {
    return R99_SFX_SHIELD;
  }
  return R99_SFX_BODY;
}

extern ST7789V2_cfg_t cfg0;
extern Joystick_cfg_t joystick_cfg;
extern Joystick_t joystick_data;
extern Joystick_cfg_t joystick2_cfg;
extern Joystick_t joystick2_data;

static uint16_t line_buffer0[ST7789V2_WIDTH];
static uint16_t line_buffer1[ST7789V2_WIDTH];
static uint16_t bg_old_line[ST7789V2_WIDTH];
static uint16_t bg_new_line[ST7789V2_WIDTH];
#define BG_STRIPE_LINES 4
static uint16_t bg_old_stripe[BG_STRIPE_LINES][ST7789V2_WIDTH];
static uint16_t bg_new_stripe[BG_STRIPE_LINES][ST7789V2_WIDTH];
static uint16_t lcd_bg_band[BG_STRIPE_LINES][ST7789V2_WIDTH];
static uint16_t leftwall_lut_swapped[256];
static uint16_t center_lut_swapped[256];
static uint16_t rightwall_lut_swapped[256];
static uint16_t gun_image_lut_swapped[256];
static uint16_t kuwu_lut_swapped[256];
static uint16_t robot1_lut_swapped[256];
static uint16_t qspi_weapon_lut_swapped[256];

typedef struct {
  uint16_t width;
  uint16_t height;
  uint16_t qiedao_frames;
  uint16_t stored_frames;
  uint16_t kuwu_frame;
  uint16_t blank_frame;
  uint16_t qieqiang_start;
  uint16_t qieqiang_frames;
  uint16_t shouqiang_start;
  uint16_t shouqiang_frames;
  uint16_t qieqiang_timeline_frames;
  uint16_t shouqiang_timeline_frames;
  uint32_t qieqiang_timeline_offset;
  uint32_t shouqiang_timeline_offset;
  uint16_t anim_count;
  uint32_t anim_table_offset;
  uint32_t frame_meta_offset;
  uint32_t frame_stride;
  uint32_t lut_offset;
  uint32_t data_offset;
  uint32_t total_size;
  uint32_t body_crc32;
} QspiWeaponAsset;

typedef struct {
  int16_t x;
  int16_t y;
  uint16_t w;
  uint16_t h;
  uint32_t data_offset;
} QspiWeaponFrameMeta;

typedef struct {
  uint16_t width;
  uint16_t height;
  int16_t x_min;
  int16_t x_max;
  int16_t y_min;
  int16_t y_max;
  uint16_t count;
  uint16_t format;
  uint32_t frame_stride;
  uint32_t lut_offset;
  uint32_t data_offset;
  uint32_t total_size;
  uint32_t body_crc32;
} QspiBackgroundAsset;

static QspiWeaponAsset qspi_weapon_asset;
static QspiBackgroundAsset qspi_bg_asset;
static const volatile uint8_t *qspi_asset_mm = 0;
static uint8_t qspi_weapon_ready = 0;
static uint8_t qspi_bg_ready = 0;
static uint16_t qspi_bg_lut_swapped[256];
static uint16_t qspi_bg_lut_cache[2][256];
static uint32_t qspi_bg_lut_cache_id[2] = {0xFFFFFFFFUL, 0xFFFFFFFFUL};
static uint8_t qspi_bg_lut_cache_next = 0U;
static uint8_t qspi_bg_ref_cache_ready = 0;

// Tuned to match the selected PC preview profile (sync tune A).
#define YAW_RANGE 28
#define PITCH_RANGE (LAYER_Y_SHIFT_MAX / 2)
#define MAX_LAYOUT_DELTA 43
#define CENTER_W 126
#define CENTER_DRAW_MAX 46
#define SIDE_MIN_W 14
#define SEAM_OVERLAP 1
#define CROSSHAIR_HALF 4
#define LOOK_YAW_INPUT_GAIN 1.35f
#define LOOK_PITCH_INPUT_GAIN 0.90f
#define LOOK_STRAFE_INPUT_GAIN 1.00f
#define LOOK_FB_INPUT_GAIN 1.00f
#define STRAFE_RANGE 14
#define FB_RANGE 12
#define STRAFE_LEFT_GAIN_NUM 110
#define STRAFE_CENTER_GAIN_NUM 55
#define STRAFE_RIGHT_GAIN_NUM 95
#define STRAFE_GAIN_DEN 100
#define FB_DEPTH_SHIFT 36
#define FB_CENTER_DELTA 16
#define WEAPON_KNIFE_OFFSET_X 0
#define WEAPON_KNIFE_OFFSET_Y 2
#define WEAPON_QIEDAO_FRAME_MS 45U
#define WEAPON_QIEDAO_RENDER_FRAME_MS 66U
#define WEAPON_QIEQIANG_FRAME_MS 40U
#define TRIGGER_DEBOUNCE_MS 20U
#define R99_RECOIL_KICK_Q8 (-10 * 256)
#define R99_RECOIL_MIN_Q8 (-14 * 256)
#define W25Q_TEST_ADDR 0x00FF0000UL
#define W25Q_FAST_BOOT 1
#define W25Q_ASSET_BASE 0x00000000UL
#define W25Q_ASSET_MAGIC "APXWPN01"
#define W25Q_ASSET_HEADER_SIZE 64U
#define W25Q_ASSET_MAX_SIZE (3UL * 1024UL * 1024UL)
#define W25Q_ASSET_FORCE_UPLOAD 0
#define W25Q_ASSET_REQUIRE_MULTI_ANIM 1
#define W25Q_BG_BASE 0x00300000UL
#define W25Q_BG_MAGIC "APXBG001"
#define W25Q_BG_HEADER_SIZE 64U
#define W25Q_BG_MAX_SIZE (12UL * 1024UL * 1024UL)
#define W25Q_BG_FORCE_UPLOAD 0
#define W25Q_BG_REQUIRE_PER_FRAME_LUT 1
#define W25Q_BG_ENABLE 1
#define BG_VIEW_X 0
#define BG_VIEW_Y 52
#define BG_VIEW_W 240
#define BG_VIEW_H 135
#define WEAPON_VIEW_Y_OFFSET 8
#define LCD_BLACK_MAINTAIN_LINES 3
#define BG_TRANSITION_MS 80U
#define BG_MOVE_REPEAT_MS 105U
#define BG_STICK_THRESHOLD 0.55f
#define W25Q_BG_TRANSITION_BLEND 0
#define W25Q_BG_TRANSITION_THREE_PHASE 0
#define BG_BLEND_START 96U
#define BG_BLEND_END 160U
#define BG_LOOK_STEP_PX 1
#define W25Q_BG_SERIAL_PREP 1
#define W25Q_BG_COLOR_DEBUG_BARS 0
#define W25Q_BG_INTERNAL_REF_OVERLAY 0
#define W25Q_BG_QSPI_REF_OVERLAY 0
#define W25Q_BG_SWAP_AFTER_READ 1
#define W25Q_BG_READ_SINGLE_LINE 0
#define W25Q_BG_FAST_SINGLE_READ 1
#define W25Q_BG_LOCK_CENTER 0
#define W25Q_BG_MAIN_INTERNAL_REF_TEST 0
#define W25Q_BG_BLOCKING_LCD_TEST 0
#define W25Q_BG_FULL_FRAME_CACHE_TEST 0
#define W25Q_DISABLE_WEAPON_OVERLAY_TEST 0
#define W25Q_BG_REINIT_BEFORE_FRAME 1
#define W25Q_BG_REINIT_BEFORE_STRIPE_READ 0
#define W25Q_BG_READ_RETRY_ON_FAIL 1
#define W25Q_CACHE_CURRENT_FRAME 1
#define W25Q_CACHE_KUWU_FRAME 0
#define W25Q_CACHE_MAX_FRAME_BYTES 26000U
#define W25Q_KNIFE_SOLID_DEBUG 0
#define W25Q_KNIFE_FORCE_KUWU 0

#if W25Q_BG_QSPI_REF_OVERLAY
static uint16_t qspi_bg_ref_cache[AD_BG_DEBUG_REF_W * AD_BG_DEBUG_REF_H];
#endif

#if W25Q_BG_FULL_FRAME_CACHE_TEST
static uint16_t qspi_bg_full_cache[BG_VIEW_H][BG_VIEW_W];
static uint8_t qspi_bg_full_cache_ready = 0;
#endif

#if W25Q_CACHE_CURRENT_FRAME || W25Q_CACHE_KUWU_FRAME
static uint8_t qspi_weapon_frame_cache[W25Q_CACHE_MAX_FRAME_BYTES];
static uint8_t qspi_weapon_frame_cache_ready = 0;
static int qspi_weapon_frame_cache_idx = -2;
static uint8_t qspi_weapon_cache_fail_reported = 0;
static uint8_t qspi_weapon_cache_ok_reported = 0;
#endif
#if W25Q_CACHE_CURRENT_FRAME && W25Q_CACHE_KUWU_FRAME
static uint8_t qspi_weapon_kuwu_cache[W25Q_CACHE_MAX_FRAME_BYTES];
static uint8_t qspi_weapon_kuwu_cache_ready = 0;
#endif
#define QSPI_WEAPON_MAX_FRAMES 256U
#define QSPI_WEAPON_MAX_ANIMS 16U
#define QSPI_WEAPON_MAX_TIMELINE 512U
typedef enum {
  WPN_ANIM_KUWU_DRAW = 0,
  WPN_ANIM_KUWU_STOW = 1,
  WPN_ANIM_R99_DRAW = 2,
  WPN_ANIM_R99_STOW = 3,
  WPN_ANIM_R99_RELOAD = 4,
  WPN_ANIM_R99_ADS = 5,
  WPN_ANIM_FUZHU_DRAW = 6,
  WPN_ANIM_FUZHU_STOW = 7,
  WPN_ANIM_FUZHU_RELOAD = 8,
  WPN_ANIM_FUZHU_ADS = 9,
  WPN_ANIM_FUZHU_FIRE = 10,
  WPN_ANIM_HEPING_DRAW = 11,
  WPN_ANIM_HEPING_STOW = 12,
  WPN_ANIM_HEPING_RELOAD = 13,
  WPN_ANIM_HEPING_PUMP = 14,
} WeaponAnimId;

typedef struct {
  uint16_t id;
  uint16_t timeline_start;
  uint16_t timeline_count;
  uint16_t frame_ms;
  uint16_t first_frame;
  uint16_t real_frames;
} QspiWeaponAnim;

static int16_t qspi_weapon_timeline[QSPI_WEAPON_MAX_TIMELINE];
static QspiWeaponAnim qspi_weapon_anim[QSPI_WEAPON_MAX_ANIMS];
static uint16_t qspi_weapon_anim_count = 0U;
static QspiWeaponFrameMeta qspi_weapon_frame_meta[QSPI_WEAPON_MAX_FRAMES];

static const uint16_t yaw_ease_q15[YAW_RANGE + 1] = {
  0, 601, 1381, 2246, 3172, 4146, 5160, 6208, 7287, 8393,
  9525, 10679, 11854, 13049, 14263, 15494, 16741, 18005, 19283, 20576,
  21882, 23201, 24533, 25877, 27233, 28601, 29979, 31368, 32767
};

static const int8_t gun_idle_x[32] = {
  0, 0, 0, 1, 1, 1, 1, 1,
  1, 1, 1, 0, 0, 0, 0, 0,
  0, 0, 0, -1, -1, -1, -1, -1,
  -1, -1, -1, 0, 0, 0, 0, 0
};

static const int8_t gun_idle_y[32] = {
  0, 0, 1, 1, 1, 2, 2, 2,
  2, 2, 1, 1, 1, 0, 0, 0,
  0, 0, -1, -1, -1, -2, -2, -2,
  -2, -2, -1, -1, -1, 0, 0, 0
};

static int qspi_wait_not_busy(uint32_t timeout_ms)
{
  uint32_t start = HAL_GetTick();
  while ((QUADSPI->SR & QUADSPI_SR_BUSY) != 0U) {
    if ((HAL_GetTick() - start) >= timeout_ms) {
      return 0;
    }
  }
  return 1;
}

static void qspi_gpio_init_for_w25q(void)
{
  GPIO_InitTypeDef gi = {0};

  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  gi.Mode = GPIO_MODE_AF_PP;
  gi.Pull = GPIO_NOPULL;
  gi.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  gi.Alternate = GPIO_AF10_QUADSPI;

  // PB10=CLK, PB11=NCS, PB1=IO0, PB0=IO1
  gi.Pin = GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_1 | GPIO_PIN_0;
  HAL_GPIO_Init(GPIOB, &gi);

  // PA7=IO2, PA6=IO3
  gi.Pin = GPIO_PIN_7 | GPIO_PIN_6;
  HAL_GPIO_Init(GPIOA, &gi);
}

static void qspi_config_clock(uint32_t prescaler)
{
  if ((QUADSPI->SR & QUADSPI_SR_BUSY) != 0U) {
    QUADSPI->CR |= QUADSPI_CR_ABORT;
    uint32_t start = HAL_GetTick();
    while (((QUADSPI->CR & QUADSPI_CR_ABORT) != 0U) && ((HAL_GetTick() - start) < 100U)) {
    }
  }
  QUADSPI->CR &= ~QUADSPI_CR_EN;
  QUADSPI->FCR = QUADSPI_FCR_CTCF | QUADSPI_FCR_CTEF | QUADSPI_FCR_CTOF | QUADSPI_FCR_CSMF;
  QUADSPI->CR = (prescaler << QUADSPI_CR_PRESCALER_Pos)
              | (3U << QUADSPI_CR_FTHRES_Pos)
              | QUADSPI_CR_SSHIFT
              | QUADSPI_CR_EN;
}

static void qspi_test_init(void)
{
  qspi_gpio_init_for_w25q();

  __HAL_RCC_QSPI_FORCE_RESET();
  __HAL_RCC_QSPI_RELEASE_RESET();
  __HAL_RCC_QSPI_CLK_ENABLE();

  QUADSPI->CR = 0;
  while ((QUADSPI->SR & QUADSPI_SR_BUSY) != 0U) {
  }

  // W25Q128B = 16 MB => FSIZE = log2(bytes) - 1 = 23.
  // CSHT=2 was the known-good baseline for the first stable 10 MHz quad-read test.
  QUADSPI->DCR = (23U << QUADSPI_DCR_FSIZE_Pos)
               | (2U << QUADSPI_DCR_CSHT_Pos);
  QUADSPI->FCR = QUADSPI_FCR_CTCF | QUADSPI_FCR_CTEF | QUADSPI_FCR_CTOF | QUADSPI_FCR_CSMF;
  qspi_config_clock(3U); // 80 MHz / (3 + 1) = 20 MHz fast-read baseline.
}

static void qspi_bg_recover_before_stream(void)
{
  if ((QUADSPI->SR & QUADSPI_SR_BUSY) != 0U) {
    QUADSPI->CR |= QUADSPI_CR_ABORT;
    uint32_t start = HAL_GetTick();
    while (((QUADSPI->CR & QUADSPI_CR_ABORT) != 0U) && ((HAL_GetTick() - start) < 10U)) {
    }
  }

  QUADSPI->FCR = QUADSPI_FCR_CTCF | QUADSPI_FCR_CTEF | QUADSPI_FCR_CTOF | QUADSPI_FCR_CSMF;
  QUADSPI->DCR = (23U << QUADSPI_DCR_FSIZE_Pos)
               | (2U << QUADSPI_DCR_CSHT_Pos);
  qspi_config_clock(3U);
}

static int qspi_read_jedec_id(uint8_t id[3])
{
  if (!qspi_wait_not_busy(100U)) {
    return 0;
  }

  QUADSPI->FCR = QUADSPI_FCR_CTCF | QUADSPI_FCR_CTEF | QUADSPI_FCR_CTOF | QUADSPI_FCR_CSMF;
  QUADSPI->DLR = 2U; // 3 bytes: manufacturer, memory type, capacity
  QUADSPI->CCR = 0x9FU
              | QUADSPI_CCR_IMODE_0
              | QUADSPI_CCR_DMODE_0
              | QUADSPI_CCR_FMODE_0;

  volatile uint8_t *dr8 = (volatile uint8_t *)&QUADSPI->DR;
  for (int i = 0; i < 3; i++) {
    uint32_t start = HAL_GetTick();
    while (((QUADSPI->SR >> 8) & 0x1FU) == 0U) {
      if ((HAL_GetTick() - start) >= 100U) {
        return 0;
      }
    }
    id[i] = *dr8;
  }

  uint32_t start = HAL_GetTick();
  while ((QUADSPI->SR & QUADSPI_SR_TCF) == 0U) {
    if ((HAL_GetTick() - start) >= 100U) {
      return 0;
    }
  }
  QUADSPI->FCR = QUADSPI_FCR_CTCF;
  return 1;
}

static int qspi_command_no_data(uint8_t instruction, uint32_t timeout_ms)
{
  if (!qspi_wait_not_busy(timeout_ms)) {
    return 0;
  }
  QUADSPI->FCR = QUADSPI_FCR_CTCF | QUADSPI_FCR_CTEF | QUADSPI_FCR_CTOF | QUADSPI_FCR_CSMF;
  QUADSPI->CCR = instruction | QUADSPI_CCR_IMODE_0;
  uint32_t start = HAL_GetTick();
  while ((QUADSPI->SR & QUADSPI_SR_TCF) == 0U) {
    if ((HAL_GetTick() - start) >= timeout_ms) {
      return 0;
    }
  }
  QUADSPI->FCR = QUADSPI_FCR_CTCF;
  return 1;
}

static int qspi_read_status(uint8_t instruction, uint8_t *value)
{
  if (!qspi_wait_not_busy(100U)) {
    return 0;
  }
  QUADSPI->FCR = QUADSPI_FCR_CTCF | QUADSPI_FCR_CTEF | QUADSPI_FCR_CTOF | QUADSPI_FCR_CSMF;
  QUADSPI->DLR = 0U;
  QUADSPI->CCR = instruction
              | QUADSPI_CCR_IMODE_0
              | QUADSPI_CCR_DMODE_0
              | QUADSPI_CCR_FMODE_0;
  volatile uint8_t *dr8 = (volatile uint8_t *)&QUADSPI->DR;
  uint32_t start = HAL_GetTick();
  while (((QUADSPI->SR >> 8) & 0x1FU) == 0U) {
    if ((HAL_GetTick() - start) >= 100U) {
      return 0;
    }
  }
  *value = *dr8;
  start = HAL_GetTick();
  while ((QUADSPI->SR & QUADSPI_SR_TCF) == 0U) {
    if ((HAL_GetTick() - start) >= 100U) {
      return 0;
    }
  }
  QUADSPI->FCR = QUADSPI_FCR_CTCF;
  return 1;
}

static int qspi_wait_wip_clear(uint32_t timeout_ms)
{
  uint32_t start = HAL_GetTick();
  uint8_t sr1 = 0;
  do {
    if (!qspi_read_status(0x05U, &sr1)) {
      return 0;
    }
    if ((sr1 & 0x01U) == 0U) {
      return 1;
    }
    HAL_Delay(1);
  } while ((HAL_GetTick() - start) < timeout_ms);
  return 0;
}

static int qspi_write_enable(void)
{
  if (!qspi_command_no_data(0x06U, 100U)) {
    return 0;
  }
  uint8_t sr1 = 0;
  if (!qspi_read_status(0x05U, &sr1)) {
    return 0;
  }
  return (sr1 & 0x02U) ? 1 : 0;
}

static int qspi_erase_sector_4k(uint32_t addr)
{
  if (!qspi_write_enable()) {
    return 0;
  }
  if (!qspi_wait_not_busy(100U)) {
    return 0;
  }
  QUADSPI->FCR = QUADSPI_FCR_CTCF | QUADSPI_FCR_CTEF | QUADSPI_FCR_CTOF | QUADSPI_FCR_CSMF;
  QUADSPI->CCR = 0x20U
              | QUADSPI_CCR_IMODE_0
              | QUADSPI_CCR_ADMODE_0
              | (2U << QUADSPI_CCR_ADSIZE_Pos);
  QUADSPI->AR = addr & 0x00FFFFFFUL;
  uint32_t start = HAL_GetTick();
  while ((QUADSPI->SR & QUADSPI_SR_TCF) == 0U) {
    if ((HAL_GetTick() - start) >= 100U) {
      return 0;
    }
  }
  QUADSPI->FCR = QUADSPI_FCR_CTCF;
  return qspi_wait_wip_clear(5000U);
}

static int qspi_page_program(uint32_t addr, const uint8_t *data, uint32_t len)
{
  if (len == 0U || len > 256U) {
    return 0;
  }
  if (!qspi_write_enable()) {
    return 0;
  }
  if (!qspi_wait_not_busy(100U)) {
    return 0;
  }
  QUADSPI->FCR = QUADSPI_FCR_CTCF | QUADSPI_FCR_CTEF | QUADSPI_FCR_CTOF | QUADSPI_FCR_CSMF;
  QUADSPI->DLR = len - 1U;
  QUADSPI->CCR = 0x02U
              | QUADSPI_CCR_IMODE_0
              | QUADSPI_CCR_ADMODE_0
              | (2U << QUADSPI_CCR_ADSIZE_Pos)
              | QUADSPI_CCR_DMODE_0;
  QUADSPI->AR = addr & 0x00FFFFFFUL;
  volatile uint8_t *dr8 = (volatile uint8_t *)&QUADSPI->DR;
  for (uint32_t i = 0; i < len; i++) {
    uint32_t start = HAL_GetTick();
    while (((QUADSPI->SR >> 8) & 0x1FU) >= 16U) {
      if ((HAL_GetTick() - start) >= 100U) {
        return 0;
      }
    }
    *dr8 = data[i];
  }
  uint32_t start = HAL_GetTick();
  while ((QUADSPI->SR & QUADSPI_SR_TCF) == 0U) {
    if ((HAL_GetTick() - start) >= 100U) {
      return 0;
    }
  }
  QUADSPI->FCR = QUADSPI_FCR_CTCF;
  return qspi_wait_wip_clear(1000U);
}

static int qspi_read_data(uint32_t addr, uint8_t *data, uint32_t len)
{
  if (len == 0U) {
    return 1;
  }
  if (!qspi_wait_not_busy(100U)) {
    return 0;
  }
  QUADSPI->FCR = QUADSPI_FCR_CTCF | QUADSPI_FCR_CTEF | QUADSPI_FCR_CTOF | QUADSPI_FCR_CSMF;
  QUADSPI->DLR = len - 1U;
  QUADSPI->CCR = 0x03U
              | QUADSPI_CCR_IMODE_0
              | QUADSPI_CCR_ADMODE_0
              | (2U << QUADSPI_CCR_ADSIZE_Pos)
              | QUADSPI_CCR_DMODE_0
              | QUADSPI_CCR_FMODE_0;
  QUADSPI->AR = addr & 0x00FFFFFFUL;
  volatile uint8_t *dr8 = (volatile uint8_t *)&QUADSPI->DR;
  for (uint32_t i = 0; i < len; i++) {
    uint32_t start = HAL_GetTick();
    while (((QUADSPI->SR >> 8) & 0x1FU) == 0U) {
      if ((HAL_GetTick() - start) >= 100U) {
        return 0;
      }
    }
    data[i] = *dr8;
  }
  uint32_t start = HAL_GetTick();
  while ((QUADSPI->SR & QUADSPI_SR_TCF) == 0U) {
    if ((HAL_GetTick() - start) >= 100U) {
      return 0;
    }
  }
  QUADSPI->FCR = QUADSPI_FCR_CTCF;
  return 1;
}

static int qspi_quad_read_data(uint32_t addr, uint8_t *data, uint32_t len)
{
  if (len == 0U) {
    return 1;
  }
  if (!qspi_wait_not_busy(100U)) {
    return 0;
  }
  QUADSPI->FCR = QUADSPI_FCR_CTCF | QUADSPI_FCR_CTEF | QUADSPI_FCR_CTOF | QUADSPI_FCR_CSMF;
  QUADSPI->DLR = len - 1U;
  QUADSPI->CCR = 0x6BU
              | QUADSPI_CCR_IMODE_0
              | QUADSPI_CCR_ADMODE_0
              | (2U << QUADSPI_CCR_ADSIZE_Pos)
              | (8U << QUADSPI_CCR_DCYC_Pos)
              | QUADSPI_CCR_DMODE_0
              | QUADSPI_CCR_DMODE_1
              | QUADSPI_CCR_FMODE_0;
  QUADSPI->AR = addr & 0x00FFFFFFUL;
  volatile uint8_t *dr8 = (volatile uint8_t *)&QUADSPI->DR;
  for (uint32_t i = 0; i < len; i++) {
    uint32_t start = HAL_GetTick();
    while (((QUADSPI->SR >> 8) & 0x1FU) == 0U) {
      if ((HAL_GetTick() - start) >= 100U) {
        return 0;
      }
    }
    data[i] = *dr8;
  }
  uint32_t start = HAL_GetTick();
  while ((QUADSPI->SR & QUADSPI_SR_TCF) == 0U) {
    if ((HAL_GetTick() - start) >= 100U) {
      return 0;
    }
  }
  QUADSPI->FCR = QUADSPI_FCR_CTCF;
  return 1;
}

static int qspi_fast_read_data(uint32_t addr, uint8_t *data, uint32_t len)
{
  if (len == 0U) {
    return 1;
  }
  if (!qspi_wait_not_busy(100U)) {
    return 0;
  }
  QUADSPI->FCR = QUADSPI_FCR_CTCF | QUADSPI_FCR_CTEF | QUADSPI_FCR_CTOF | QUADSPI_FCR_CSMF;
  QUADSPI->DLR = len - 1U;
  QUADSPI->CCR = 0x0BU
              | QUADSPI_CCR_IMODE_0
              | QUADSPI_CCR_ADMODE_0
              | (2U << QUADSPI_CCR_ADSIZE_Pos)
              | (8U << QUADSPI_CCR_DCYC_Pos)
              | QUADSPI_CCR_DMODE_0
              | QUADSPI_CCR_FMODE_0;
  QUADSPI->AR = addr & 0x00FFFFFFUL;
  volatile uint8_t *dr8 = (volatile uint8_t *)&QUADSPI->DR;
  for (uint32_t i = 0; i < len; i++) {
    uint32_t start = HAL_GetTick();
    while (((QUADSPI->SR >> 8) & 0x1FU) == 0U) {
      if ((HAL_GetTick() - start) >= 100U) {
        return 0;
      }
    }
    data[i] = *dr8;
  }
  uint32_t start = HAL_GetTick();
  while ((QUADSPI->SR & QUADSPI_SR_TCF) == 0U) {
    if ((HAL_GetTick() - start) >= 100U) {
      return 0;
    }
  }
  QUADSPI->FCR = QUADSPI_FCR_CTCF;
  return 1;
}

static int qspi_enter_memory_mapped_quad_read(void)
{
  if (!qspi_wait_not_busy(100U)) {
    return 0;
  }
  QUADSPI->FCR = QUADSPI_FCR_CTCF | QUADSPI_FCR_CTEF | QUADSPI_FCR_CTOF | QUADSPI_FCR_CSMF;
  QUADSPI->CCR = 0x6BU
              | QUADSPI_CCR_IMODE_0
              | QUADSPI_CCR_ADMODE_0
              | (2U << QUADSPI_CCR_ADSIZE_Pos)
              | (8U << QUADSPI_CCR_DCYC_Pos)
              | QUADSPI_CCR_DMODE_0
              | QUADSPI_CCR_DMODE_1
              | QUADSPI_CCR_FMODE_0
              | QUADSPI_CCR_FMODE_1;
  return 1;
}

static int qspi_enter_memory_mapped_fast_read(void)
{
  if (!qspi_wait_not_busy(100U)) {
    return 0;
  }
  QUADSPI->FCR = QUADSPI_FCR_CTCF | QUADSPI_FCR_CTEF | QUADSPI_FCR_CTOF | QUADSPI_FCR_CSMF;
  QUADSPI->CCR = 0x0BU
              | QUADSPI_CCR_IMODE_0
              | QUADSPI_CCR_ADMODE_0
              | (2U << QUADSPI_CCR_ADSIZE_Pos)
              | (8U << QUADSPI_CCR_DCYC_Pos)
              | QUADSPI_CCR_DMODE_0
              | QUADSPI_CCR_FMODE_0
              | QUADSPI_CCR_FMODE_1;
  return 1;
}

static int qspi_abort_memory_mapped(void)
{
  QUADSPI->CR |= QUADSPI_CR_ABORT;
  uint32_t start = HAL_GetTick();
  while ((QUADSPI->CR & QUADSPI_CR_ABORT) != 0U) {
    if ((HAL_GetTick() - start) >= 100U) {
      return 0;
    }
  }
  return qspi_wait_not_busy(100U);
}

static void qspi_run_rw_test(void)
{
  uint8_t sr1 = 0, sr2 = 0;
  if (qspi_read_status(0x05U, &sr1) && qspi_read_status(0x35U, &sr2)) {
    printf("[QSPI] SR1=0x%02X SR2=0x%02X QE=%u\n", sr1, sr2, (unsigned)((sr2 >> 1) & 1U));
  } else {
    printf("[QSPI] status read FAILED\n");
    return;
  }

  uint8_t tx[256];
  uint8_t rx[256];
  for (int i = 0; i < 256; i++) {
    tx[i] = (uint8_t)(0xA5U ^ (uint8_t)i);
    rx[i] = 0;
  }

  printf("[QSPI] Erase 4KB sector at 0x%06lX...\n", (unsigned long)W25Q_TEST_ADDR);
  if (!qspi_erase_sector_4k(W25Q_TEST_ADDR)) {
    printf("[QSPI] erase FAILED SR=0x%08lX\n", (unsigned long)QUADSPI->SR);
    return;
  }

  if (!qspi_read_data(W25Q_TEST_ADDR, rx, sizeof(rx))) {
    printf("[QSPI] blank read FAILED\n");
    return;
  }
  int blank_ok = 1;
  for (int i = 0; i < 256; i++) {
    if (rx[i] != 0xFFU) {
      blank_ok = 0;
      break;
    }
  }
  printf("[QSPI] blank check: %s\n", blank_ok ? "OK" : "FAILED");

  printf("[QSPI] Program 256 bytes...\n");
  if (!qspi_page_program(W25Q_TEST_ADDR, tx, sizeof(tx))) {
    printf("[QSPI] program FAILED SR=0x%08lX\n", (unsigned long)QUADSPI->SR);
    return;
  }

  memset(rx, 0, sizeof(rx));
  if (!qspi_read_data(W25Q_TEST_ADDR, rx, sizeof(rx))) {
    printf("[QSPI] verify read FAILED\n");
    return;
  }
  int verify_ok = 1;
  for (int i = 0; i < 256; i++) {
    if (rx[i] != tx[i]) {
      printf("[QSPI] mismatch at %d: got 0x%02X expected 0x%02X\n", i, rx[i], tx[i]);
      verify_ok = 0;
      break;
    }
  }
  printf("[QSPI] program/read verify: %s\n", verify_ok ? "OK" : "FAILED");

  qspi_config_clock(7U); // 80 MHz / (7 + 1) = 10 MHz
  printf("[QSPI] final test: 10MHz, 0x6B quad output read, dummy=8, SSHIFT=1, GPIO speed VERY_HIGH, CSHT=2\n");

  memset(rx, 0, sizeof(rx));
  if (!qspi_quad_read_data(W25Q_TEST_ADDR, rx, sizeof(rx))) {
    printf("[QSPI] 10MHz quad read FAILED SR=0x%08lX\n", (unsigned long)QUADSPI->SR);
    return;
  }
  verify_ok = 1;
  for (int i = 0; i < 256; i++) {
    if (rx[i] != tx[i]) {
      printf("[QSPI] 10MHz quad mismatch at %d: got 0x%02X expected 0x%02X\n", i, rx[i], tx[i]);
      verify_ok = 0;
      break;
    }
  }
  printf("[QSPI] 10MHz quad read verify: %s\n", verify_ok ? "OK" : "FAILED");

  if (!qspi_enter_memory_mapped_quad_read()) {
    printf("[QSPI] 10MHz memory-mapped enter FAILED SR=0x%08lX\n", (unsigned long)QUADSPI->SR);
    return;
  }
  const volatile uint8_t *mm = (const volatile uint8_t *)(QSPI_BASE + W25Q_TEST_ADDR);
  verify_ok = 1;
  for (int i = 0; i < 256; i++) {
    uint8_t v = mm[i];
    if (v != tx[i]) {
      printf("[QSPI] 10MHz mm quad mismatch at %d: got 0x%02X expected 0x%02X\n", i, v, tx[i]);
      verify_ok = 0;
      break;
    }
  }
  printf("[QSPI] 10MHz memory-mapped quad verify: %s\n", verify_ok ? "OK" : "FAILED");
  if (!qspi_abort_memory_mapped()) {
    printf("[QSPI] 10MHz memory-mapped abort FAILED SR=0x%08lX CR=0x%08lX\n",
           (unsigned long)QUADSPI->SR,
           (unsigned long)QUADSPI->CR);
  }
}

static uint16_t read_le16(const uint8_t *p)
{
  return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t read_le32(const uint8_t *p)
{
  return (uint32_t)p[0]
       | ((uint32_t)p[1] << 8)
       | ((uint32_t)p[2] << 16)
       | ((uint32_t)p[3] << 24);
}

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, uint32_t len)
{
  crc = ~crc;
  for (uint32_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (uint32_t bit = 0; bit < 8U; bit++) {
      crc = (crc >> 1) ^ (0xEDB88320UL & (0UL - (crc & 1UL)));
    }
  }
  return ~crc;
}

static int qspi_weapon_parse_header(const uint8_t hdr[W25Q_ASSET_HEADER_SIZE], QspiWeaponAsset *out)
{
  if (memcmp(hdr, W25Q_ASSET_MAGIC, 8U) != 0) {
    return 0;
  }
  uint32_t version = read_le32(&hdr[8]);
  if (version != 4UL && version != 5UL) {
    return 0;
  }

  out->width = read_le16(&hdr[12]);
  out->height = read_le16(&hdr[14]);
  out->qiedao_frames = read_le16(&hdr[16]);
  out->stored_frames = read_le16(&hdr[18]);
  out->kuwu_frame = read_le16(&hdr[20]);
  out->blank_frame = read_le16(&hdr[22]);
  out->qieqiang_start = 0U;
  out->qieqiang_frames = 0U;
  out->shouqiang_start = 0U;
  out->shouqiang_frames = 0U;
  out->qieqiang_timeline_offset = 0U;
  out->shouqiang_timeline_offset = 0U;
  out->qieqiang_timeline_frames = 0U;
  out->shouqiang_timeline_frames = 0U;
  out->anim_count = 0U;
  out->anim_table_offset = 0U;
  out->frame_meta_offset = 0U;
  out->frame_stride = read_le32(&hdr[24]);
  out->lut_offset = read_le32(&hdr[28]);
  out->data_offset = read_le32(&hdr[32]);
  out->total_size = read_le32(&hdr[36]);
  out->body_crc32 = read_le32(&hdr[40]);
  if (version == 5UL) {
    out->anim_count = read_le16(&hdr[44]);
    out->anim_table_offset = read_le32(&hdr[48]);
    out->frame_meta_offset = read_le32(&hdr[52]);
    out->qieqiang_timeline_frames = read_le16(&hdr[56]);
    out->shouqiang_timeline_frames = read_le16(&hdr[58]);
  } else if (version >= 2UL) {
    out->qieqiang_start = read_le16(&hdr[44]);
    out->qieqiang_frames = read_le16(&hdr[46]);
  }
  if (version != 5UL && version >= 3UL) {
    out->shouqiang_start = read_le16(&hdr[48]);
    out->shouqiang_frames = read_le16(&hdr[50]);
    out->qieqiang_timeline_offset = read_le32(&hdr[52]);
    out->shouqiang_timeline_offset = read_le32(&hdr[56]);
    out->qieqiang_timeline_frames = read_le16(&hdr[60]);
    out->shouqiang_timeline_frames = read_le16(&hdr[62]);
  }

  if (out->width == 0U || out->height == 0U || out->stored_frames == 0U) {
    return 0;
  }
  if (out->frame_stride == 0U || out->frame_stride > W25Q_CACHE_MAX_FRAME_BYTES) {
    return 0;
  }
  if (out->lut_offset < W25Q_ASSET_HEADER_SIZE || out->data_offset < (out->lut_offset + 512U)) {
    return 0;
  }
  if (out->total_size > W25Q_ASSET_MAX_SIZE) {
    return 0;
  }
  const uint32_t frame_meta_offset = (version == 5UL)
                                   ? out->frame_meta_offset
                                   : (out->shouqiang_timeline_offset + (uint32_t)out->shouqiang_timeline_frames * 2U);
  if (out->data_offset < (frame_meta_offset + (uint32_t)out->stored_frames * 12U)) {
    return 0;
  }
  if (out->kuwu_frame >= out->stored_frames) {
    return 0;
  }
  if (version == 5UL) {
    if (out->anim_count == 0U || out->anim_count > QSPI_WEAPON_MAX_ANIMS) {
      return 0;
    }
    if (out->stored_frames > QSPI_WEAPON_MAX_FRAMES) {
      return 0;
    }
    if (out->anim_table_offset < out->lut_offset + 512U
        || out->frame_meta_offset < out->anim_table_offset + (uint32_t)out->anim_count * 16U
        || out->data_offset < out->frame_meta_offset + (uint32_t)out->stored_frames * 12U) {
      return 0;
    }
    return 1;
  }
  if (out->qieqiang_frames == 0U) {
    return 0;
  }
  if ((uint32_t)out->qieqiang_start + (uint32_t)out->qieqiang_frames > (uint32_t)out->stored_frames) {
    return 0;
  }
  if (version >= 3UL) {
    if (out->shouqiang_frames == 0U || out->qieqiang_timeline_frames == 0U || out->shouqiang_timeline_frames == 0U) {
      return 0;
    }
    if ((uint32_t)out->shouqiang_start + (uint32_t)out->shouqiang_frames > (uint32_t)out->stored_frames) {
      return 0;
    }
    if (out->qieqiang_timeline_frames > 64U || out->shouqiang_timeline_frames > 64U) {
      return 0;
    }
    if (out->qieqiang_timeline_offset < out->lut_offset + 512U
        || out->shouqiang_timeline_offset < out->qieqiang_timeline_offset
        || out->data_offset < out->shouqiang_timeline_offset) {
      return 0;
    }
    if (out->qieqiang_timeline_offset + (uint32_t)out->qieqiang_timeline_frames * 2U > out->total_size
        || out->shouqiang_timeline_offset + (uint32_t)out->shouqiang_timeline_frames * 2U > out->total_size) {
      return 0;
    }
  }
  return 1;
}

static int qspi_weapon_check_existing(void)
{
  uint8_t hdr[W25Q_ASSET_HEADER_SIZE];
  if (!qspi_read_data(W25Q_ASSET_BASE, hdr, sizeof(hdr))) {
    return 0;
  }
  if (!qspi_weapon_parse_header(hdr, &qspi_weapon_asset)) {
    return 0;
  }
#if W25Q_ASSET_REQUIRE_MULTI_ANIM
  if (qspi_weapon_asset.anim_count == 0U) {
    printf("[ASSET] old weapon asset found; multi-animation asset required\n");
    return 0;
  }
#endif

#if W25Q_FAST_BOOT
  return 1;
#else
  uint8_t buf[256];
  uint32_t crc = 0;
  uint32_t remain = qspi_weapon_asset.total_size - W25Q_ASSET_HEADER_SIZE;
  uint32_t addr = W25Q_ASSET_BASE + W25Q_ASSET_HEADER_SIZE;
  while (remain > 0U) {
    uint32_t chunk = (remain > sizeof(buf)) ? sizeof(buf) : remain;
    if (!qspi_read_data(addr, buf, chunk)) {
      return 0;
    }
    crc = crc32_update(crc, buf, chunk);
    addr += chunk;
    remain -= chunk;
  }
  if (crc != qspi_weapon_asset.body_crc32) {
    printf("[ASSET] CRC mismatch: got 0x%08lX expected 0x%08lX\n",
           (unsigned long)crc,
           (unsigned long)qspi_weapon_asset.body_crc32);
    return 0;
  }
  return 1;
#endif
}

static int qspi_weapon_load_timeline(uint32_t offset, uint16_t count, int16_t *dst)
{
  uint8_t raw[128];
  uint16_t done = 0U;
  if (count == 0U || count > QSPI_WEAPON_MAX_TIMELINE) {
    return 0;
  }
  while (done < count) {
    uint16_t chunk = (uint16_t)(count - done);
    if (chunk > (uint16_t)(sizeof(raw) / 2U)) {
      chunk = (uint16_t)(sizeof(raw) / 2U);
    }
    if (!qspi_read_data(W25Q_ASSET_BASE + offset + (uint32_t)done * 2U, raw, (uint32_t)chunk * 2U)) {
      return 0;
    }
    for (uint16_t i = 0; i < chunk; i++) {
      dst[done + i] = (int16_t)((uint16_t)raw[i * 2U] | ((uint16_t)raw[i * 2U + 1U] << 8));
      if (dst[done + i] >= (int16_t)qspi_weapon_asset.stored_frames) {
        return 0;
      }
    }
    done = (uint16_t)(done + chunk);
  }
  return 1;
}

static QspiWeaponAnim *qspi_weapon_find_anim(uint16_t id)
{
  for (uint16_t i = 0; i < qspi_weapon_anim_count; i++) {
    if (qspi_weapon_anim[i].id == id) {
      return &qspi_weapon_anim[i];
    }
  }
  return NULL;
}

static int qspi_weapon_load_multi_anims(void)
{
  uint8_t raw[QSPI_WEAPON_MAX_ANIMS * 16U];
  uint16_t timeline_used = 0U;
  if (qspi_weapon_asset.anim_count == 0U || qspi_weapon_asset.anim_count > QSPI_WEAPON_MAX_ANIMS) {
    return 0;
  }
  if (!qspi_read_data(W25Q_ASSET_BASE + qspi_weapon_asset.anim_table_offset,
                      raw,
                      (uint32_t)qspi_weapon_asset.anim_count * 16U)) {
    return 0;
  }
  qspi_weapon_anim_count = qspi_weapon_asset.anim_count;
  for (uint16_t i = 0; i < qspi_weapon_anim_count; i++) {
    const uint8_t *p = &raw[(uint32_t)i * 16U];
    uint32_t timeline_offset = read_le32(&p[4]);
    uint16_t timeline_count = read_le16(&p[2]);
    if ((uint32_t)timeline_used + (uint32_t)timeline_count > QSPI_WEAPON_MAX_TIMELINE) {
      return 0;
    }
    qspi_weapon_anim[i].id = read_le16(&p[0]);
    qspi_weapon_anim[i].timeline_start = timeline_used;
    qspi_weapon_anim[i].timeline_count = timeline_count;
    qspi_weapon_anim[i].frame_ms = read_le16(&p[8]);
    qspi_weapon_anim[i].first_frame = read_le16(&p[12]);
    qspi_weapon_anim[i].real_frames = read_le16(&p[14]);
    if (qspi_weapon_anim[i].first_frame >= qspi_weapon_asset.stored_frames
        || (uint32_t)qspi_weapon_anim[i].first_frame + (uint32_t)qspi_weapon_anim[i].real_frames > (uint32_t)qspi_weapon_asset.stored_frames) {
      return 0;
    }
    if (!qspi_weapon_load_timeline(timeline_offset,
                                   timeline_count,
                                   &qspi_weapon_timeline[timeline_used])) {
      return 0;
    }
    timeline_used = (uint16_t)(timeline_used + timeline_count);
  }
  return 1;
}

static uint8_t qspi_weapon_anim_exists(uint16_t id)
{
  return (qspi_weapon_find_anim(id) != NULL) ? 1U : 0U;
}

static int qspi_weapon_anim_frame(uint16_t id, uint32_t elapsed_ms, uint8_t reverse, uint8_t *done)
{
  QspiWeaponAnim *anim = qspi_weapon_find_anim(id);
  if (done) {
    *done = 1U;
  }
  if (!anim || anim->timeline_count == 0U) {
    return -1;
  }
  uint32_t frame_ms = anim->frame_ms;
  if (frame_ms == 0U) {
    frame_ms = WEAPON_QIEQIANG_FRAME_MS;
  }
  uint32_t idx = elapsed_ms / frame_ms;
  if (idx >= (uint32_t)anim->timeline_count) {
    return -1;
  }
  if (reverse) {
    idx = (uint32_t)anim->timeline_count - 1U - idx;
  }
  if (done) {
    *done = 0U;
  }
  return (int)qspi_weapon_timeline[anim->timeline_start + (uint16_t)idx];
}

static uint16_t weapon_draw_anim_for_kind(FireWeaponKind weapon)
{
  if (weapon == FIRE_WEAPON_FUZHU) {
    return WPN_ANIM_FUZHU_DRAW;
  }
  if (weapon == FIRE_WEAPON_HEPING) {
    return WPN_ANIM_HEPING_DRAW;
  }
  return WPN_ANIM_R99_DRAW;
}

static uint16_t weapon_stow_anim_for_kind(FireWeaponKind weapon)
{
  if (weapon == FIRE_WEAPON_FUZHU) {
    return WPN_ANIM_FUZHU_STOW;
  }
  if (weapon == FIRE_WEAPON_HEPING) {
    return WPN_ANIM_HEPING_STOW;
  }
  return WPN_ANIM_R99_STOW;
}

static uint16_t weapon_reload_anim_for_kind(FireWeaponKind weapon)
{
  if (weapon == FIRE_WEAPON_FUZHU) {
    return WPN_ANIM_FUZHU_RELOAD;
  }
  if (weapon == FIRE_WEAPON_HEPING) {
    return WPN_ANIM_HEPING_RELOAD;
  }
  return WPN_ANIM_R99_RELOAD;
}

static uint16_t weapon_ads_anim_for_kind(FireWeaponKind weapon)
{
  if (weapon == FIRE_WEAPON_FUZHU) {
    return WPN_ANIM_FUZHU_ADS;
  }
  if (weapon == FIRE_WEAPON_HEPING) {
    return 0xFFFFU;
  }
  return WPN_ANIM_R99_ADS;
}

static uint16_t weapon_fire_anim_for_kind(FireWeaponKind weapon)
{
  if (weapon == FIRE_WEAPON_FUZHU) {
    return WPN_ANIM_FUZHU_FIRE;
  }
  if (weapon == FIRE_WEAPON_HEPING) {
    return WPN_ANIM_HEPING_PUMP;
  }
  return 0xFFFFU;
}

static int qspi_weapon_load_frame_meta(void)
{
  uint8_t raw[QSPI_WEAPON_MAX_FRAMES * 12U];
  if (qspi_weapon_asset.stored_frames == 0U || qspi_weapon_asset.stored_frames > QSPI_WEAPON_MAX_FRAMES) {
    return 0;
  }
  const uint32_t meta_offset = (qspi_weapon_asset.frame_meta_offset != 0U)
                             ? qspi_weapon_asset.frame_meta_offset
                             : (qspi_weapon_asset.shouqiang_timeline_offset
                                + (uint32_t)qspi_weapon_asset.shouqiang_timeline_frames * 2U);
  if (!qspi_read_data(W25Q_ASSET_BASE + meta_offset, raw, (uint32_t)qspi_weapon_asset.stored_frames * 12U)) {
    return 0;
  }
  for (uint16_t i = 0; i < qspi_weapon_asset.stored_frames; i++) {
    const uint8_t *p = &raw[(uint32_t)i * 12U];
    QspiWeaponFrameMeta *m = &qspi_weapon_frame_meta[i];
    m->x = (int16_t)read_le16(&p[0]);
    m->y = (int16_t)read_le16(&p[2]);
    m->w = read_le16(&p[4]);
    m->h = read_le16(&p[6]);
    m->data_offset = read_le32(&p[8]);
    if ((uint32_t)m->w * (uint32_t)m->h > qspi_weapon_asset.frame_stride) {
      return 0;
    }
    if (m->w > qspi_weapon_asset.width || m->h > qspi_weapon_asset.height) {
      return 0;
    }
    if (m->w > 0U && m->h > 0U) {
      if (m->x < 0 || m->y < 0) {
        return 0;
      }
      if ((uint32_t)m->x + (uint32_t)m->w > (uint32_t)qspi_weapon_asset.width
          || (uint32_t)m->y + (uint32_t)m->h > (uint32_t)qspi_weapon_asset.height) {
        return 0;
      }
      if (m->data_offset < qspi_weapon_asset.data_offset
          || m->data_offset + (uint32_t)m->w * (uint32_t)m->h > qspi_weapon_asset.total_size) {
        return 0;
      }
    }
  }
  return 1;
}

static int qspi_weapon_receive_and_program(void)
{
  uint8_t hdr[W25Q_ASSET_HEADER_SIZE];
  printf("[ASSET] Send qiedao asset binary now.\n");
  printf("[ASSET] Waiting for %lu-byte header at 115200 8N1...\n", (unsigned long)sizeof(hdr));
  if (HAL_UART_Receive(&huart2, hdr, sizeof(hdr), HAL_MAX_DELAY) != HAL_OK) {
    return 0;
  }
  if (!qspi_weapon_parse_header(hdr, &qspi_weapon_asset)) {
    printf("[ASSET] invalid header\n");
    return 0;
  }
  printf("[ASSET] size=%lu body_crc=0x%08lX frames=%u %ux%u\n",
         (unsigned long)qspi_weapon_asset.total_size,
         (unsigned long)qspi_weapon_asset.body_crc32,
         (unsigned)qspi_weapon_asset.stored_frames,
         (unsigned)qspi_weapon_asset.width,
         (unsigned)qspi_weapon_asset.height);

  const uint32_t sectors = (qspi_weapon_asset.total_size + 4095UL) / 4096UL;
  for (uint32_t s = 0; s < sectors; s++) {
    if (!qspi_erase_sector_4k(W25Q_ASSET_BASE + s * 4096UL)) {
      printf("[ASSET] erase failed at sector %lu\n", (unsigned long)s);
      return 0;
    }
    if ((s & 0x0FUL) == 0U) {
      printf("[ASSET] erased %lu/%lu sectors\n", (unsigned long)(s + 1U), (unsigned long)sectors);
    }
  }

  if (!qspi_page_program(W25Q_ASSET_BASE, hdr, W25Q_ASSET_HEADER_SIZE)) {
    printf("[ASSET] header program failed\n");
    return 0;
  }
  printf("[ASSET] ready for body\n");

  uint8_t page[256];
  uint32_t addr = W25Q_ASSET_BASE + W25Q_ASSET_HEADER_SIZE;
  uint32_t written = W25Q_ASSET_HEADER_SIZE;
  uint32_t crc = 0;

  while (written < qspi_weapon_asset.total_size) {
    uint32_t page_count = qspi_weapon_asset.total_size - written;
    uint32_t page_room = 256U - (addr & 0xFFU);
    if (page_count > page_room) {
      page_count = page_room;
    }
    if (page_count > sizeof(page)) {
      page_count = sizeof(page);
    }

    if (HAL_UART_Receive(&huart2, page, (uint16_t)page_count, HAL_MAX_DELAY) != HAL_OK) {
      return 0;
    }
    crc = crc32_update(crc, page, page_count);

    if (!qspi_page_program(addr, page, page_count)) {
      printf("[ASSET] program failed at 0x%06lX\n", (unsigned long)addr);
      return 0;
    }
    const uint8_t ack = 0x06U;
    HAL_UART_Transmit(&huart2, (uint8_t *)&ack, 1U, HAL_MAX_DELAY);
    addr += page_count;
    written += page_count;

  }

  if (crc != qspi_weapon_asset.body_crc32) {
    printf("[ASSET] receive CRC failed: got 0x%08lX expected 0x%08lX\n",
           (unsigned long)crc,
           (unsigned long)qspi_weapon_asset.body_crc32);
    return 0;
  }
  printf("[ASSET] upload/program OK\n");
  return qspi_weapon_check_existing();
}

static int qspi_weapon_prepare(void)
{
  qspi_test_init();
#if W25Q_ASSET_FORCE_UPLOAD
  if (!qspi_weapon_receive_and_program()) {
    return 0;
  }
#else
  if (!qspi_weapon_check_existing()) {
    if (!qspi_weapon_receive_and_program()) {
      return 0;
    }
  } else {
    printf("[ASSET] existing qiedao asset OK\n");
  }
#endif

#if W25Q_CACHE_CURRENT_FRAME || W25Q_CACHE_KUWU_FRAME
  uint8_t lut_raw[512];
  if (!qspi_read_data(W25Q_ASSET_BASE + qspi_weapon_asset.lut_offset, lut_raw, sizeof(lut_raw))) {
    printf("[ASSET] LUT indirect read failed\n");
    return 0;
  }
  for (uint32_t i = 0; i < 256U; i++) {
    uint16_t c = (uint16_t)lut_raw[i * 2U] | ((uint16_t)lut_raw[i * 2U + 1U] << 8);
    qspi_weapon_lut_swapped[i] = swap16(c);
  }
  if (qspi_weapon_asset.anim_count > 0U) {
    if (!qspi_weapon_load_multi_anims()) {
      printf("[ASSET] weapon multi-animation load failed\n");
      return 0;
    }
  } else {
    uint16_t timeline_used = 0U;
    if ((uint32_t)qspi_weapon_asset.qieqiang_timeline_frames
        + (uint32_t)qspi_weapon_asset.shouqiang_timeline_frames > QSPI_WEAPON_MAX_TIMELINE) {
      printf("[ASSET] legacy weapon timeline too long\n");
      return 0;
    }
    if (!qspi_weapon_load_timeline(qspi_weapon_asset.qieqiang_timeline_offset,
                                   qspi_weapon_asset.qieqiang_timeline_frames,
                                   &qspi_weapon_timeline[timeline_used])) {
      printf("[ASSET] legacy qieqiang timeline load failed\n");
      return 0;
    }
    qspi_weapon_anim[0].id = WPN_ANIM_R99_DRAW;
    qspi_weapon_anim[0].timeline_start = timeline_used;
    qspi_weapon_anim[0].timeline_count = qspi_weapon_asset.qieqiang_timeline_frames;
    qspi_weapon_anim[0].frame_ms = WEAPON_QIEQIANG_FRAME_MS;
    qspi_weapon_anim[0].first_frame = qspi_weapon_asset.qieqiang_start;
    qspi_weapon_anim[0].real_frames = qspi_weapon_asset.qieqiang_frames;
    timeline_used = (uint16_t)(timeline_used + qspi_weapon_asset.qieqiang_timeline_frames);
    if (!qspi_weapon_load_timeline(qspi_weapon_asset.shouqiang_timeline_offset,
                                   qspi_weapon_asset.shouqiang_timeline_frames,
                                   &qspi_weapon_timeline[timeline_used])) {
      printf("[ASSET] legacy shouqiang timeline load failed\n");
      return 0;
    }
    qspi_weapon_anim[1].id = WPN_ANIM_R99_STOW;
    qspi_weapon_anim[1].timeline_start = timeline_used;
    qspi_weapon_anim[1].timeline_count = qspi_weapon_asset.shouqiang_timeline_frames;
    qspi_weapon_anim[1].frame_ms = WEAPON_QIEQIANG_FRAME_MS;
    qspi_weapon_anim[1].first_frame = qspi_weapon_asset.shouqiang_start;
    qspi_weapon_anim[1].real_frames = qspi_weapon_asset.shouqiang_frames;
    qspi_weapon_anim_count = 2U;
  }
  if (!qspi_weapon_load_frame_meta()) {
    printf("[ASSET] weapon frame metadata load failed\n");
    return 0;
  }
#endif

#if W25Q_CACHE_CURRENT_FRAME
  qspi_asset_mm = 0;
  qspi_weapon_frame_cache_ready = 0U;
  qspi_weapon_frame_cache_idx = -2;
  qspi_weapon_cache_fail_reported = 0U;
  qspi_weapon_cache_ok_reported = 0U;
#if W25Q_CACHE_CURRENT_FRAME && W25Q_CACHE_KUWU_FRAME
  qspi_weapon_kuwu_cache_ready = 0U;
  QspiWeaponFrameMeta *kuwu_meta = &qspi_weapon_frame_meta[qspi_weapon_asset.kuwu_frame];
  uint32_t kuwu_size = (uint32_t)kuwu_meta->w * (uint32_t)kuwu_meta->h;
  if (kuwu_size <= W25Q_CACHE_MAX_FRAME_BYTES) {
    uint32_t addr = W25Q_ASSET_BASE + kuwu_meta->data_offset;
    uint32_t remain = kuwu_size;
    uint32_t pos = 0U;
    while (remain > 0U) {
      uint32_t chunk = (remain > 256U) ? 256U : remain;
      if (!qspi_fast_read_data(addr, &qspi_weapon_kuwu_cache[pos], chunk)) {
        printf("[ASSET] kuwu fixed cache failed\n");
        return 0;
      }
      addr += chunk;
      pos += chunk;
      remain -= chunk;
    }
    qspi_weapon_kuwu_cache_ready = 1U;
    printf("[ASSET] fixed kuwu frame cached in SRAM (%lu bytes)\n",
           (unsigned long)kuwu_size);
  } else {
    printf("[ASSET] kuwu frame too large for SRAM cache\n");
    return 0;
  }
#endif
  qspi_weapon_ready = 1U;
  printf("[ASSET] hybrid cache enabled: animation current-frame + fixed kuwu (%lu bytes), no memory-mapped\n",
         (unsigned long)qspi_weapon_asset.frame_stride);
  return 1;
#elif W25Q_CACHE_KUWU_FRAME
  qspi_asset_mm = 0;
  qspi_weapon_frame_cache_ready = 0U;
  QspiWeaponFrameMeta *kuwu_meta = &qspi_weapon_frame_meta[qspi_weapon_asset.kuwu_frame];
  uint32_t kuwu_size = (uint32_t)kuwu_meta->w * (uint32_t)kuwu_meta->h;
  if (kuwu_size <= W25Q_CACHE_MAX_FRAME_BYTES) {
    uint32_t addr = W25Q_ASSET_BASE + kuwu_meta->data_offset;
    uint32_t remain = kuwu_size;
    uint32_t pos = 0U;
    while (remain > 0U) {
      uint32_t chunk = (remain > 256U) ? 256U : remain;
      if (!qspi_fast_read_data(addr, &qspi_weapon_frame_cache[pos], chunk)) {
        printf("[ASSET] kuwu frame quad cache failed\n");
        return 0;
      }
      addr += chunk;
      pos += chunk;
      remain -= chunk;
    }
    qspi_weapon_frame_cache_idx = (int)qspi_weapon_asset.kuwu_frame;
    qspi_weapon_frame_cache_ready = 1U;
    printf("[ASSET] kuwu frame cached in SRAM by quad chunks (%lu bytes)\n",
           (unsigned long)kuwu_size);
  }
  else {
    printf("[ASSET] kuwu frame too large for SRAM cache\n");
    return 0;
  }

  if (!qspi_enter_memory_mapped_quad_read()) {
    printf("[ASSET] memory-mapped enter failed\n");
    return 0;
  }
  qspi_asset_mm = (const volatile uint8_t *)(QSPI_BASE + W25Q_ASSET_BASE);
  qspi_weapon_ready = 1U;
  printf("[ASSET] animation memory-mapped + fixed kuwu SRAM cache enabled\n");
  return 1;
#else
  if (!qspi_enter_memory_mapped_quad_read()) {
    printf("[ASSET] memory-mapped enter failed\n");
    return 0;
  }

  qspi_asset_mm = (const volatile uint8_t *)(QSPI_BASE + W25Q_ASSET_BASE);
  uint32_t mm_crc = 0;
  const volatile uint8_t *mm_body = qspi_asset_mm + W25Q_ASSET_HEADER_SIZE;
  uint32_t mm_remain = qspi_weapon_asset.total_size - W25Q_ASSET_HEADER_SIZE;
  while (mm_remain > 0U) {
    uint8_t tmp[128];
    uint32_t chunk = (mm_remain > sizeof(tmp)) ? sizeof(tmp) : mm_remain;
    for (uint32_t i = 0; i < chunk; i++) {
      tmp[i] = mm_body[i];
    }
    mm_crc = crc32_update(mm_crc, tmp, chunk);
    mm_body += chunk;
    mm_remain -= chunk;
  }
  printf("[ASSET] memory-mapped CRC: 0x%08lX %s\n",
         (unsigned long)mm_crc,
         (mm_crc == qspi_weapon_asset.body_crc32) ? "OK" : "FAILED");

  for (uint32_t i = 0; i < 256U; i++) {
    const volatile uint8_t *p = qspi_asset_mm + qspi_weapon_asset.lut_offset + i * 2U;
    uint16_t c = (uint16_t)p[0] | ((uint16_t)p[1] << 8);
    qspi_weapon_lut_swapped[i] = swap16(c);
  }

#if W25Q_CACHE_KUWU_FRAME
  qspi_weapon_frame_cache_ready = 0U;
  if (qspi_weapon_asset.frame_stride <= W25Q_CACHE_MAX_FRAME_BYTES) {
    const volatile uint8_t *src = qspi_asset_mm
                               + qspi_weapon_asset.data_offset
                               + (uint32_t)qspi_weapon_asset.kuwu_frame * qspi_weapon_asset.frame_stride;
    for (uint32_t i = 0; i < qspi_weapon_asset.frame_stride; i++) {
      qspi_weapon_frame_cache[i] = src[i];
    }
    qspi_weapon_frame_cache_idx = (int)qspi_weapon_asset.kuwu_frame;
    qspi_weapon_frame_cache_ready = 1U;
    printf("[ASSET] kuwu frame cached in SRAM (%lu bytes)\n",
           (unsigned long)qspi_weapon_asset.frame_stride);
  } else {
    printf("[ASSET] kuwu frame too large for SRAM cache\n");
  }
#endif

  qspi_weapon_ready = 1U;
  printf("[ASSET] qiedao asset mapped at 0x%08lX\n", (unsigned long)(QSPI_BASE + W25Q_ASSET_BASE));
  return 1;
#endif
}

static int16_t read_le_i16(const uint8_t *p)
{
  return (int16_t)read_le16(p);
}

static int qspi_bg_parse_header(const uint8_t hdr[W25Q_BG_HEADER_SIZE], QspiBackgroundAsset *out)
{
  if (memcmp(hdr, W25Q_BG_MAGIC, 8U) != 0) {
    return 0;
  }
  uint32_t version = read_le32(&hdr[8]);
  if (version != 1UL && version != 2UL && version != 3UL) {
    return 0;
  }

  out->width = read_le16(&hdr[12]);
  out->height = read_le16(&hdr[14]);
  out->x_min = read_le_i16(&hdr[16]);
  out->x_max = read_le_i16(&hdr[18]);
  out->y_min = read_le_i16(&hdr[20]);
  out->y_max = read_le_i16(&hdr[22]);
  out->count = read_le16(&hdr[24]);
  out->format = read_le16(&hdr[26]);
  out->frame_stride = read_le32(&hdr[28]);
  out->data_offset = read_le32(&hdr[32]);
  out->total_size = read_le32(&hdr[36]);
  out->body_crc32 = read_le32(&hdr[40]);
  out->lut_offset = 0U;
  if (version == 1UL) {
    out->format = 0U; // RGB565
  } else {
    if (out->format != 1U && out->format != 2U) {
      return 0;
    }
    out->lut_offset = W25Q_BG_HEADER_SIZE;
    if (version == 3UL) {
      out->lut_offset = read_le32(&hdr[44]);
      if (read_le32(&hdr[48]) != 512UL) {
        return 0;
      }
    }
  }

  if (out->width < BG_VIEW_W || out->height < BG_VIEW_H) {
    return 0;
  }
  if (out->x_min > out->x_max || out->y_min > out->y_max) {
    return 0;
  }
  uint32_t cols = (uint32_t)(out->x_max - out->x_min + 1);
  uint32_t rows = (uint32_t)(out->y_max - out->y_min + 1);
  if ((uint32_t)out->count != cols * rows) {
    return 0;
  }
  uint32_t expected_stride = (uint32_t)out->width * (uint32_t)out->height;
  if (out->format == 0U) {
    expected_stride *= 2U;
  }
  if (out->frame_stride != expected_stride) {
    return 0;
  }
  if (out->format == 1U && out->data_offset < (W25Q_BG_HEADER_SIZE + 512U)) {
    return 0;
  }
  if (out->format == 2U && out->data_offset < (out->lut_offset + (uint32_t)out->count * 512U)) {
    return 0;
  }
  if (out->data_offset < W25Q_BG_HEADER_SIZE || out->total_size > W25Q_BG_MAX_SIZE) {
    return 0;
  }
  if ((out->data_offset + out->frame_stride * (uint32_t)out->count) > out->total_size) {
    return 0;
  }
  return 1;
}

static int qspi_bg_check_existing(void)
{
  uint8_t hdr[W25Q_BG_HEADER_SIZE];
  if (!qspi_read_data(W25Q_BG_BASE, hdr, sizeof(hdr))) {
    return 0;
  }
  if (!qspi_bg_parse_header(hdr, &qspi_bg_asset)) {
    return 0;
  }
#if W25Q_BG_REQUIRE_PER_FRAME_LUT
  if (qspi_bg_asset.format == 1U) {
    printf("[BG] existing indexed8 asset uses global LUT; upgrade required\n");
    return 0;
  }
#endif

#if W25Q_FAST_BOOT
  return 1;
#else
  uint8_t buf[256];
  uint32_t crc = 0;
  uint32_t remain = qspi_bg_asset.total_size - W25Q_BG_HEADER_SIZE;
  uint32_t addr = W25Q_BG_BASE + W25Q_BG_HEADER_SIZE;
  while (remain > 0U) {
    uint32_t chunk = (remain > sizeof(buf)) ? sizeof(buf) : remain;
    if (!qspi_read_data(addr, buf, chunk)) {
      return 0;
    }
    crc = crc32_update(crc, buf, chunk);
    addr += chunk;
    remain -= chunk;
  }
  if (crc != qspi_bg_asset.body_crc32) {
    printf("[BG] CRC mismatch: got 0x%08lX expected 0x%08lX\n",
           (unsigned long)crc,
           (unsigned long)qspi_bg_asset.body_crc32);
    return 0;
  }
  return 1;
#endif
}

static void qspi_bg_print_quad_crc_sample(void)
{
  if (qspi_bg_asset.total_size <= W25Q_BG_HEADER_SIZE) {
    return;
  }

  uint8_t buf[256];
  uint32_t crc = 0;
  uint32_t remain = qspi_bg_asset.total_size - W25Q_BG_HEADER_SIZE;
  uint32_t addr = W25Q_BG_BASE + W25Q_BG_HEADER_SIZE;
  while (remain > 0U) {
    uint32_t chunk = (remain > sizeof(buf)) ? sizeof(buf) : remain;
    if (!qspi_quad_read_data(addr, buf, chunk)) {
      printf("[BG] quad CRC read failed at 0x%06lX\n", (unsigned long)addr);
      return;
    }
    crc = crc32_update(crc, buf, chunk);
    addr += chunk;
    remain -= chunk;
  }
  printf("[BG] quad CRC: 0x%08lX %s\n",
         (unsigned long)crc,
         (crc == qspi_bg_asset.body_crc32) ? "OK" : "FAILED");

  uint16_t sample[8];
  if (qspi_quad_read_data(W25Q_BG_BASE + qspi_bg_asset.data_offset,
                          (uint8_t *)sample,
                          sizeof(sample))) {
#if W25Q_BG_SWAP_AFTER_READ
    for (int i = 0; i < 8; i++) {
      sample[i] = swap16(sample[i]);
    }
#endif
    printf("[BG] sample swapped:");
    for (int i = 0; i < 8; i++) {
      printf(" %04X", sample[i]);
    }
    printf("\n");
  }
}

static int qspi_bg_cache_ref_0_0(void)
{
#if W25Q_BG_QSPI_REF_OVERLAY
  uint16_t line[BG_VIEW_W];
  int bg_x = clamp_i(0, (int)qspi_bg_asset.x_min, (int)qspi_bg_asset.x_max);
  int bg_y = clamp_i(0, (int)qspi_bg_asset.y_min, (int)qspi_bg_asset.y_max);

  qspi_bg_ref_cache_ready = 0U;
  for (int y = 0; y < AD_BG_DEBUG_REF_H; y++) {
    int src_y = (y * BG_VIEW_H) / AD_BG_DEBUG_REF_H;
    if (!qspi_bg_read_line(bg_x, bg_y, src_y, line)) {
      printf("[BG] ref cache read failed at y=%d\n", y);
      return 0;
    }
    for (int x = 0; x < AD_BG_DEBUG_REF_W; x++) {
      int src_x = (x * BG_VIEW_W) / AD_BG_DEBUG_REF_W;
      qspi_bg_ref_cache[(uint32_t)y * AD_BG_DEBUG_REF_W + (uint32_t)x] = line[src_x];
    }
  }

  qspi_bg_ref_cache_ready = 1U;
  printf("[BG] 0_0 ref cached in SRAM (%lu bytes)\n",
         (unsigned long)(sizeof(qspi_bg_ref_cache)));
  return 1;
#else
  qspi_bg_ref_cache_ready = 0U;
  return 1;
#endif
}

#if W25Q_BG_FULL_FRAME_CACHE_TEST
static int qspi_bg_cache_full_0_0(void)
{
  int bg_x = clamp_i(0, (int)qspi_bg_asset.x_min, (int)qspi_bg_asset.x_max);
  int bg_y = clamp_i(0, (int)qspi_bg_asset.y_min, (int)qspi_bg_asset.y_max);

  qspi_bg_full_cache_ready = 0U;
  for (int y = 0; y < BG_VIEW_H; y++) {
    if (!qspi_bg_read_line(bg_x, bg_y, y, qspi_bg_full_cache[y])) {
      printf("[BG] full 0_0 cache read failed at y=%d\n", y);
      return 0;
    }
  }
  qspi_bg_full_cache_ready = 1U;
  printf("[BG] full 0_0 frame cached in SRAM (%lu bytes)\n",
         (unsigned long)sizeof(qspi_bg_full_cache));
  return 1;
}
#endif

static int qspi_bg_receive_and_program(void)
{
  uint8_t hdr[W25Q_BG_HEADER_SIZE];
  printf("[BG] Send ad_background asset binary now.\n");
  printf("[BG] Waiting for %lu-byte header at 115200 8N1...\n", (unsigned long)sizeof(hdr));
  if (HAL_UART_Receive(&huart2, hdr, sizeof(hdr), HAL_MAX_DELAY) != HAL_OK) {
    return 0;
  }
  if (!qspi_bg_parse_header(hdr, &qspi_bg_asset)) {
    printf("[BG] invalid header magic=%.8s version=%lu size=%ux%u count=%u format=%u stride=%lu data_off=%lu total=%lu crc=0x%08lX\n",
           hdr,
           (unsigned long)read_le32(&hdr[8]),
           (unsigned)read_le16(&hdr[12]),
           (unsigned)read_le16(&hdr[14]),
           (unsigned)read_le16(&hdr[24]),
           (unsigned)read_le16(&hdr[26]),
           (unsigned long)read_le32(&hdr[28]),
           (unsigned long)read_le32(&hdr[32]),
           (unsigned long)read_le32(&hdr[36]),
           (unsigned long)read_le32(&hdr[40]));
    return 0;
  }
  printf("[BG] size=%lu body_crc=0x%08lX count=%u range x=%d..%d y=%d..%d %ux%u\n",
         (unsigned long)qspi_bg_asset.total_size,
         (unsigned long)qspi_bg_asset.body_crc32,
         (unsigned)qspi_bg_asset.count,
         (int)qspi_bg_asset.x_min,
         (int)qspi_bg_asset.x_max,
         (int)qspi_bg_asset.y_min,
         (int)qspi_bg_asset.y_max,
         (unsigned)qspi_bg_asset.width,
         (unsigned)qspi_bg_asset.height);

  const uint32_t sectors = (qspi_bg_asset.total_size + 4095UL) / 4096UL;
  for (uint32_t s = 0; s < sectors; s++) {
    if (!qspi_erase_sector_4k(W25Q_BG_BASE + s * 4096UL)) {
      printf("[BG] erase failed at sector %lu\n", (unsigned long)s);
      return 0;
    }
    if ((s & 0x0FUL) == 0U) {
      printf("[BG] erased %lu/%lu sectors\n", (unsigned long)(s + 1U), (unsigned long)sectors);
    }
  }

  if (!qspi_page_program(W25Q_BG_BASE, hdr, W25Q_BG_HEADER_SIZE)) {
    printf("[BG] header program failed\n");
    return 0;
  }
  printf("[BG] ready for body\n");

  uint8_t page[256];
  uint32_t addr = W25Q_BG_BASE + W25Q_BG_HEADER_SIZE;
  uint32_t written = W25Q_BG_HEADER_SIZE;
  uint32_t crc = 0;
  while (written < qspi_bg_asset.total_size) {
    uint32_t page_count = qspi_bg_asset.total_size - written;
    uint32_t page_room = 256U - (addr & 0xFFU);
    if (page_count > page_room) {
      page_count = page_room;
    }
    if (page_count > sizeof(page)) {
      page_count = sizeof(page);
    }

    if (HAL_UART_Receive(&huart2, page, (uint16_t)page_count, HAL_MAX_DELAY) != HAL_OK) {
      return 0;
    }
    crc = crc32_update(crc, page, page_count);

    if (!qspi_page_program(addr, page, page_count)) {
      printf("[BG] program failed at 0x%06lX\n", (unsigned long)addr);
      return 0;
    }
    const uint8_t ack = 0x06U;
    HAL_UART_Transmit(&huart2, (uint8_t *)&ack, 1U, HAL_MAX_DELAY);
    addr += page_count;
    written += page_count;
  }

  if (crc != qspi_bg_asset.body_crc32) {
    printf("[BG] receive CRC failed: got 0x%08lX expected 0x%08lX\n",
           (unsigned long)crc,
           (unsigned long)qspi_bg_asset.body_crc32);
    return 0;
  }
  printf("[BG] upload/program OK\n");
  return qspi_bg_check_existing();
}

static int qspi_bg_prepare(void)
{
  qspi_test_init();
#if W25Q_BG_FORCE_UPLOAD
  if (!qspi_bg_receive_and_program()) {
    return 0;
  }
#else
  if (!qspi_bg_check_existing()) {
    if (!qspi_bg_receive_and_program()) {
      return 0;
    }
  } else {
    printf("[BG] existing ad_background asset OK\n");
  }
#endif
  qspi_bg_ready = 1U;
  qspi_bg_lut_cache_id[0] = 0xFFFFFFFFUL;
  qspi_bg_lut_cache_id[1] = 0xFFFFFFFFUL;
  qspi_bg_lut_cache_next = 0U;
  if (qspi_bg_asset.format == 1U) {
    uint8_t lut_raw[512];
    if (!qspi_read_data(W25Q_BG_BASE + qspi_bg_asset.lut_offset, lut_raw, sizeof(lut_raw))) {
      printf("[BG] indexed LUT read failed\n");
      return 0;
    }
    for (uint32_t i = 0; i < 256U; i++) {
      uint16_t c = (uint16_t)lut_raw[i * 2U] | ((uint16_t)lut_raw[i * 2U + 1U] << 8);
      qspi_bg_lut_swapped[i] = swap16(c);
    }
  } else if (qspi_bg_asset.format == 2U) {
    uint8_t lut_raw[512];
    if (!qspi_read_data(W25Q_BG_BASE + qspi_bg_asset.lut_offset, lut_raw, sizeof(lut_raw))) {
      printf("[BG] indexed frame LUT read failed\n");
      return 0;
    }
    for (uint32_t i = 0; i < 256U; i++) {
      uint16_t c = (uint16_t)lut_raw[i * 2U] | ((uint16_t)lut_raw[i * 2U + 1U] << 8);
      qspi_bg_lut_swapped[i] = swap16(c);
      qspi_bg_lut_cache[0][i] = qspi_bg_lut_swapped[i];
    }
    qspi_bg_lut_cache_id[0] = 0U;
    qspi_bg_lut_cache_next = 1U;
  }

  printf("[BG] range x=%d..%d y=%d..%d count=%u stride=%lu format=%s base=0x%06lX\n",
         (int)qspi_bg_asset.x_min,
         (int)qspi_bg_asset.x_max,
         (int)qspi_bg_asset.y_min,
         (int)qspi_bg_asset.y_max,
         (unsigned)qspi_bg_asset.count,
         (unsigned long)qspi_bg_asset.frame_stride,
         (qspi_bg_asset.format == 2U) ? "indexed8-per-frame-lut" : ((qspi_bg_asset.format == 1U) ? "indexed8" : "rgb565"),
         (unsigned long)W25Q_BG_BASE);
#if !W25Q_FAST_BOOT
  qspi_bg_print_quad_crc_sample();
#endif
  (void)qspi_bg_cache_ref_0_0();
#if W25Q_BG_FULL_FRAME_CACHE_TEST
  (void)qspi_bg_cache_full_0_0();
#endif
  return 1;
}


static void perf_counter_init(void)
{
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

static void ps2_rx_push(uint8_t b)
{
  uint8_t next = (uint8_t)((ps2_rx_head + 1U) % PS2_RX_BUF_SIZE);
  if (next == ps2_rx_tail) {
    ps2_rx_overflow++;
    return;
  }
  ps2_rx_buf[ps2_rx_head] = b;
  ps2_rx_head = next;
}

static int ps2_rx_pop(uint8_t *out)
{
  if (ps2_rx_head == ps2_rx_tail) {
    return 0;
  }
  *out = ps2_rx_buf[ps2_rx_tail];
  ps2_rx_tail = (uint8_t)((ps2_rx_tail + 1U) % PS2_RX_BUF_SIZE);
  return 1;
}

static void ps2_delay_us(uint32_t us)
{
  uint32_t start = DWT->CYCCNT;
  uint32_t ticks = us * (SystemCoreClock / 1000000U);
  while ((DWT->CYCCNT - start) < ticks) {
  }
}

#if INA219_MONITOR_ENABLE
static void ina219_i2c_delay(void)
{
  ps2_delay_us(5);
}

static void ina219_scl_high(void)
{
  HAL_GPIO_WritePin(INA219_GPIO, INA219_PIN_SCL, GPIO_PIN_SET);
  ina219_i2c_delay();
}

static void ina219_scl_low(void)
{
  HAL_GPIO_WritePin(INA219_GPIO, INA219_PIN_SCL, GPIO_PIN_RESET);
  ina219_i2c_delay();
}

static void ina219_sda_high(void)
{
  HAL_GPIO_WritePin(INA219_GPIO, INA219_PIN_SDA, GPIO_PIN_SET);
  ina219_i2c_delay();
}

static void ina219_sda_low(void)
{
  HAL_GPIO_WritePin(INA219_GPIO, INA219_PIN_SDA, GPIO_PIN_RESET);
  ina219_i2c_delay();
}

static uint8_t ina219_sda_read(void)
{
  return (HAL_GPIO_ReadPin(INA219_GPIO, INA219_PIN_SDA) == GPIO_PIN_SET) ? 1U : 0U;
}

static void ina219_i2c_gpio_init(void)
{
  GPIO_InitTypeDef gi = {0};
  __HAL_RCC_GPIOB_CLK_ENABLE();
  HAL_GPIO_WritePin(INA219_GPIO, INA219_PIN_SCL | INA219_PIN_SDA, GPIO_PIN_SET);
  gi.Pin = INA219_PIN_SCL | INA219_PIN_SDA;
  gi.Mode = GPIO_MODE_OUTPUT_OD;
  gi.Pull = GPIO_PULLUP;
  gi.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(INA219_GPIO, &gi);
  ina219_scl_high();
  ina219_sda_high();
}

static void ina219_i2c_start(void)
{
  ina219_sda_high();
  ina219_scl_high();
  ina219_sda_low();
  ina219_scl_low();
}

static void ina219_i2c_stop(void)
{
  ina219_sda_low();
  ina219_scl_high();
  ina219_sda_high();
}

static uint8_t ina219_i2c_write_byte(uint8_t v)
{
  for (int i = 0; i < 8; i++) {
    if ((v & 0x80U) != 0U) {
      ina219_sda_high();
    } else {
      ina219_sda_low();
    }
    ina219_scl_high();
    ina219_scl_low();
    v <<= 1U;
  }
  ina219_sda_high();
  ina219_scl_high();
  uint8_t ack = (ina219_sda_read() == 0U) ? 1U : 0U;
  ina219_scl_low();
  return ack;
}

static uint8_t ina219_i2c_read_byte(uint8_t ack)
{
  uint8_t v = 0U;
  ina219_sda_high();
  for (int i = 0; i < 8; i++) {
    v <<= 1U;
    ina219_scl_high();
    if (ina219_sda_read()) {
      v |= 1U;
    }
    ina219_scl_low();
  }
  if (ack) {
    ina219_sda_low();
  } else {
    ina219_sda_high();
  }
  ina219_scl_high();
  ina219_scl_low();
  ina219_sda_high();
  return v;
}

static uint8_t ina219_write_reg(uint8_t reg, uint16_t value)
{
  ina219_i2c_start();
  if (!ina219_i2c_write_byte((uint8_t)(INA219_I2C_ADDR << 1U))) {
    ina219_i2c_stop();
    return 0U;
  }
  if (!ina219_i2c_write_byte(reg)
      || !ina219_i2c_write_byte((uint8_t)(value >> 8U))
      || !ina219_i2c_write_byte((uint8_t)value)) {
    ina219_i2c_stop();
    return 0U;
  }
  ina219_i2c_stop();
  return 1U;
}

static uint8_t ina219_read_reg(uint8_t reg, uint16_t *out)
{
  ina219_i2c_start();
  if (!ina219_i2c_write_byte((uint8_t)(INA219_I2C_ADDR << 1U))
      || !ina219_i2c_write_byte(reg)) {
    ina219_i2c_stop();
    return 0U;
  }
  ina219_i2c_start();
  if (!ina219_i2c_write_byte((uint8_t)((INA219_I2C_ADDR << 1U) | 1U))) {
    ina219_i2c_stop();
    return 0U;
  }
  uint8_t msb = ina219_i2c_read_byte(1U);
  uint8_t lsb = ina219_i2c_read_byte(0U);
  ina219_i2c_stop();
  *out = (uint16_t)(((uint16_t)msb << 8U) | lsb);
  return 1U;
}

static uint8_t ina219_init(void)
{
  ina219_i2c_gpio_init();
  HAL_Delay(2);
  if (!ina219_write_reg(0x00U, 0x399FU)) {
    return 0U;
  }
  if (!ina219_write_reg(0x05U, INA219_CALIBRATION_100UA)) {
    return 0U;
  }
  uint16_t cfg_read = 0U;
  return ina219_read_reg(0x00U, &cfg_read);
}

static uint8_t ina219_sample(int32_t *bus_mv, int32_t *shunt_uv, int32_t *current_x10_ma)
{
  uint16_t raw_bus = 0U;
  uint16_t raw_shunt = 0U;
  uint16_t raw_current = 0U;
  if (!ina219_read_reg(0x02U, &raw_bus)
      || !ina219_read_reg(0x01U, &raw_shunt)
      || !ina219_read_reg(0x04U, &raw_current)) {
    return 0U;
  }
  *bus_mv = (int32_t)((raw_bus >> 3U) * 4U);
  *shunt_uv = (int32_t)((int16_t)raw_shunt) * 10;
  *current_x10_ma = (int32_t)((int16_t)raw_current);
  return 1U;
}

static void ina219_print_sample(void)
{
  int32_t bus_mv = 0;
  int32_t shunt_uv = 0;
  int32_t current_x10_ma = 0;
  if (!ina219_write_reg(0x05U, INA219_CALIBRATION_100UA)
      || !ina219_sample(&bus_mv, &shunt_uv, &current_x10_ma)) {
    printf("[INA219] read failed\n");
    return;
  }
  int32_t current_abs = abs_i((int)current_x10_ma);
  printf("[INA219] bus=%ld.%03ldV shunt=%lduV current=%s%ld.%ldmA\n",
         (long)(bus_mv / 1000),
         (long)(bus_mv % 1000),
         (long)shunt_uv,
         (current_x10_ma < 0) ? "-" : "",
         (long)(current_abs / 10),
         (long)(current_abs % 10));
}

void PowerMonitor_Init(void)
{
  printf("\n=== INA219 Power Monitor ===\n");
  printf("Software I2C: SCL=PB8 SDA=PB9, addr=0x%02X\n", (unsigned)INA219_I2C_ADDR);
  if (ina219_init()) {
    power_monitor_ready = 1U;
    printf("[INA219] detected; current LSB=0.1mA, assumed shunt=0.1ohm\n");
  } else {
    power_monitor_ready = 0U;
    printf("[INA219] init failed; check VCC=3.3V, GND, SCL=PB8, SDA=PB9\n");
  }
}

void PowerMonitor_Tick(uint32_t now)
{
  static uint32_t last_sample_ms = 0;
  static uint32_t last_print_ms = 0;
  static int32_t peak_x10_ma = 0;
  static int32_t last_bus_mv = 0;
  static int32_t last_shunt_uv = 0;
  static int32_t last_current_x10_ma = 0;

  if (!power_monitor_ready) {
    return;
  }

  if ((now - last_sample_ms) >= 25U) {
    if (ina219_sample(&last_bus_mv, &last_shunt_uv, &last_current_x10_ma)) {
      int32_t current_abs = abs_i((int)last_current_x10_ma);
      if (current_abs > peak_x10_ma) {
        peak_x10_ma = current_abs;
      }
    }
    last_sample_ms = now;
  }

  if ((now - last_print_ms) >= 1000U) {
    int32_t current_abs = abs_i((int)last_current_x10_ma);
    printf("[INA219] bus=%ld.%03ldV shunt=%lduV current=%s%ld.%ldmA peak=%ld.%ldmA\n",
           (long)(last_bus_mv / 1000),
           (long)(last_bus_mv % 1000),
           (long)last_shunt_uv,
           (last_current_x10_ma < 0) ? "-" : "",
           (long)(current_abs / 10),
           (long)(current_abs % 10),
           (long)(peak_x10_ma / 10),
           (long)(peak_x10_ma % 10));
    peak_x10_ma = 0;
    last_print_ms = now;
  }
}
#else
void PowerMonitor_Init(void)
{
}

void PowerMonitor_Tick(uint32_t now)
{
  (void)now;
}
#endif

static void ps2_data_drive_low(void)
{
  GPIO_InitTypeDef gi = {0};
  HAL_GPIO_WritePin(PS2_DATA_GPIO_Port, PS2_DATA_Pin, GPIO_PIN_RESET);
  gi.Pin = PS2_DATA_Pin;
  gi.Mode = GPIO_MODE_OUTPUT_OD;
  gi.Pull = GPIO_PULLUP;
  gi.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(PS2_DATA_GPIO_Port, &gi);
}

static void ps2_data_release(void)
{
  GPIO_InitTypeDef gi = {0};
  HAL_GPIO_WritePin(PS2_DATA_GPIO_Port, PS2_DATA_Pin, GPIO_PIN_SET);
  gi.Pin = PS2_DATA_Pin;
  gi.Mode = GPIO_MODE_INPUT;
  gi.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(PS2_DATA_GPIO_Port, &gi);
}

static void ps2_clk_drive_low(void)
{
  GPIO_InitTypeDef gi = {0};
  HAL_GPIO_WritePin(PS2_CLK_GPIO_Port, PS2_CLK_Pin, GPIO_PIN_RESET);
  gi.Pin = PS2_CLK_Pin;
  gi.Mode = GPIO_MODE_OUTPUT_OD;
  gi.Pull = GPIO_PULLUP;
  gi.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(PS2_CLK_GPIO_Port, &gi);
}

static void ps2_clk_release_it(void)
{
  GPIO_InitTypeDef gi = {0};
  HAL_GPIO_WritePin(PS2_CLK_GPIO_Port, PS2_CLK_Pin, GPIO_PIN_SET);
  gi.Pin = PS2_CLK_Pin;
  gi.Mode = GPIO_MODE_IT_FALLING;
  gi.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(PS2_CLK_GPIO_Port, &gi);
}

static int ps2_wait_clk(GPIO_PinState target, uint32_t timeout_us)
{
  uint32_t t = 0;
  while (HAL_GPIO_ReadPin(PS2_CLK_GPIO_Port, PS2_CLK_Pin) != target) {
    if (t >= timeout_us) {
      return 0;
    }
    ps2_delay_us(2);
    t += 2;
  }
  return 1;
}

static int ps2_host_write_byte(uint8_t data)
{
  uint8_t parity = 1U; // odd parity
  __disable_irq();
  ps2_bit_count = 0U;
  ps2_data_byte = 0U;
  ps2_parity_bit = 0U;
  __HAL_GPIO_EXTI_CLEAR_IT(PS2_CLK_Pin);
  __enable_irq();
  ps2_tx_active = 1U;

  // Inhibit device communication, then request-to-send.
  ps2_clk_drive_low();
  ps2_delay_us(150);
  ps2_data_drive_low();
  ps2_delay_us(10);
  ps2_clk_release_it();

  // The first device-generated low period follows the request-to-send start bit.
  // Set bit0 during this low period; the device samples it on the next high.
  if (!ps2_wait_clk(GPIO_PIN_RESET, 15000U)) {
    ps2_tx_active = 0U;
    ps2_data_release();
    ps2_clk_release_it();
    return 0;
  }

  for (int i = 0; i < 8; i++) {
    uint8_t bit = (uint8_t)(data & 0x01U);
    if (bit) {
      ps2_data_release();
    } else {
      ps2_data_drive_low();
    }
    parity ^= bit;

    if (!ps2_wait_clk(GPIO_PIN_SET, 3000U) || !ps2_wait_clk(GPIO_PIN_RESET, 3000U)) {
      ps2_tx_active = 0U;
      ps2_data_release();
      ps2_clk_release_it();
      return 0;
    }
    data >>= 1;
  }

  // Parity bit.
  if (parity) {
    ps2_data_release();
  } else {
    ps2_data_drive_low();
  }
  if (!ps2_wait_clk(GPIO_PIN_SET, 3000U) || !ps2_wait_clk(GPIO_PIN_RESET, 3000U)) {
    ps2_tx_active = 0U;
    ps2_data_release();
    ps2_clk_release_it();
    return 0;
  }

  // Stop bit (release line high).
  ps2_data_release();
  if (!ps2_wait_clk(GPIO_PIN_SET, 3000U) || !ps2_wait_clk(GPIO_PIN_RESET, 3000U)) {
    ps2_tx_active = 0U;
    ps2_clk_release_it();
    return 0;
  }

  // ACK bit from device should drive DATA low before/during the next clock pulse.
  uint32_t ack_wait = 0U;
  while (HAL_GPIO_ReadPin(PS2_DATA_GPIO_Port, PS2_DATA_Pin) != GPIO_PIN_RESET) {
    if (ack_wait >= 3000U) {
      ps2_tx_active = 0U;
      ps2_clk_release_it();
      return 0;
    }
    ps2_delay_us(2);
    ack_wait += 2U;
  }
  if (!ps2_wait_clk(GPIO_PIN_SET, 3000U) || !ps2_wait_clk(GPIO_PIN_RESET, 3000U)) {
    ps2_tx_active = 0U;
    ps2_clk_release_it();
    return 0;
  }
  uint8_t ack = (HAL_GPIO_ReadPin(PS2_DATA_GPIO_Port, PS2_DATA_Pin) == GPIO_PIN_RESET) ? 1U : 0U;
  if (!ps2_wait_clk(GPIO_PIN_SET, 3000U)) {
    ps2_tx_active = 0U;
    ps2_clk_release_it();
    return 0;
  }

  __disable_irq();
  ps2_bit_count = 0U;
  ps2_data_byte = 0U;
  ps2_parity_bit = 0U;
  __HAL_GPIO_EXTI_CLEAR_IT(PS2_CLK_Pin);
  ps2_tx_active = 0U;
  __enable_irq();
  ps2_clk_release_it();
  ps2_data_release();
  return (ack == 1U) ? 1 : 0;
}

static int ps2_enable_streaming(void)
{
  return ps2_host_write_byte(0xF4U);
}

static int ps2_reset_device(void)
{
  return ps2_host_write_byte(0xFFU);
}

static void ps2_game_mouse_service(int *move_dx, int *move_dy, uint8_t *stream_ready, uint8_t *buttons)
{
  static uint8_t pkt[3] = {0};
  static uint8_t pkt_idx = 0;
  static uint8_t seen_bat = 0;
  static uint8_t seen_id = 0;
  static uint8_t stream_enabled = 0;
  static uint8_t reset_sent = 0;
  static uint8_t last_cmd_was_reset = 0;
  static uint8_t last_cmd_was_f4 = 0;
  static uint32_t boot_ms = 0;
  static uint32_t last_reset_try_ms = 0;
  static uint32_t last_f4_try_ms = 0;

  if (boot_ms == 0U) {
    boot_ms = HAL_GetTick();
    ps2_phase_fix_enabled = 1U;
  }

  *move_dx = 0;
  *move_dy = 0;
  if (buttons) {
    *buttons = ps2_mouse_buttons;
  }

  uint8_t b = 0;
  while (ps2_rx_pop(&b)) {
    if (stream_enabled) {
      if (pkt_idx == 0 && (b & 0x08U) == 0U) {
        continue;
      }
      if (pkt_idx == 0 && (b & 0xC0U) != 0U) {
        continue;
      }

      pkt[pkt_idx++] = b;
      if (pkt_idx == 3U) {
        int8_t dx = (int8_t)pkt[1];
        int8_t dy = (int8_t)pkt[2];
        uint8_t x_ovf = (pkt[0] >> 6U) & 0x01U;
        uint8_t y_ovf = (pkt[0] >> 7U) & 0x01U;
        ps2_mouse_buttons = pkt[0] & (MOUSE_BTN_LEFT | MOUSE_BTN_RIGHT | MOUSE_BTN_MIDDLE);
        if (buttons) {
          *buttons = ps2_mouse_buttons;
        }
        if (!x_ovf && !y_ovf && abs_i((int)dx) <= MOUSE_PACKET_LIMIT && abs_i((int)dy) <= MOUSE_PACKET_LIMIT) {
          *move_dx += (int)dx;
          *move_dy += (int)dy;
        }
        pkt_idx = 0U;
      }
      continue;
    }

    if (b == 0xAAU) {
      seen_bat = 1U;
      seen_id = 0U;
      last_f4_try_ms = HAL_GetTick();
      pkt_idx = 0U;
      continue;
    }

    if (b == 0xFAU) {
      if (last_cmd_was_reset) {
        last_cmd_was_reset = 0U;
      }
      if (last_cmd_was_f4) {
        stream_enabled = 1U;
        ps2_phase_fix_enabled = 0U;
        last_cmd_was_f4 = 0U;
        pkt_idx = 0U;
      }
      continue;
    }

    if (b == 0xFEU || b == 0xFCU) {
      stream_enabled = 0U;
      seen_bat = 0U;
      seen_id = 0U;
      reset_sent = 0U;
      last_cmd_was_reset = 0U;
      last_cmd_was_f4 = 0U;
      pkt_idx = 0U;
      continue;
    }

    if (b == 0x00U && !stream_enabled) {
      seen_id = 1U;
      continue;
    }
  }

  uint32_t now = HAL_GetTick();
  if (!stream_enabled
      && (now - boot_ms) >= 700U
      && (!reset_sent || (!seen_bat && (now - last_reset_try_ms) >= 2000U))
      && (now - last_reset_try_ms) >= 500U) {
    last_reset_try_ms = now;
    reset_sent = 1U;
    seen_bat = 0U;
    seen_id = 0U;
    ps2_phase_fix_enabled = 1U;
    last_cmd_was_reset = 1U;
    last_cmd_was_f4 = 0U;
    (void)ps2_reset_device();
  }

  if (!stream_enabled
      && seen_bat
      && !last_cmd_was_f4
      && (seen_id || (now - last_f4_try_ms) >= 700U)
      && (now - last_f4_try_ms) >= 500U) {
    last_f4_try_ms = now;
    last_cmd_was_f4 = 1U;
    (void)ps2_enable_streaming();
  }

  if (stream_ready) {
    *stream_ready = stream_enabled;
  }

  *move_dx = clamp_i(*move_dx, -MOUSE_FRAME_DELTA_LIMIT, MOUSE_FRAME_DELTA_LIMIT);
  *move_dy = clamp_i(*move_dy, -MOUSE_FRAME_DELTA_LIMIT, MOUSE_FRAME_DELTA_LIMIT);
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
  Input_EXTI_Callback(GPIO_Pin);

  if (GPIO_Pin != PS2_CLK_Pin) {
    return;
  }

  ps2_clk_falling_edges++;

  if (ps2_tx_active) {
    return;
  }

  ps2_delay_us(PS2_RX_SAMPLE_DELAY_US);
  uint8_t bit = (HAL_GPIO_ReadPin(PS2_DATA_GPIO_Port, PS2_DATA_Pin) == GPIO_PIN_SET) ? 1U : 0U;

  if (ps2_bit_count == 0U) {
    if (bit != 0U) {
      // Start bit must be 0; ignore noise until a valid start arrives.
      return;
    }
    ps2_data_byte = 0;
    ps2_parity_bit = 0;
    ps2_bit_count = 1U;
    return;
  }

  if (ps2_bit_count >= 1U && ps2_bit_count <= 8U) {
    ps2_data_byte |= (uint8_t)(bit << (ps2_bit_count - 1U));
    ps2_bit_count++;
    return;
  }

  if (ps2_bit_count == 9U) {
    ps2_parity_bit = bit;
    ps2_bit_count++;
    return;
  }

  if (ps2_bit_count == 10U) {
    uint8_t stop_ok = (bit == 1U) ? 1U : 0U;
    uint8_t ones = 0U;
    for (int i = 0; i < 8; i++) {
      ones += (uint8_t)((ps2_data_byte >> i) & 0x01U);
    }
    uint8_t parity_ok = ((uint8_t)(ones + ps2_parity_bit) & 0x01U) ? 1U : 0U;

    if (!stop_ok) {
      uint8_t recovered = (uint8_t)((ps2_data_byte & 0x7FU) << 1U);
      uint8_t recovered_ones = 0U;
      for (int i = 0; i < 8; i++) {
        recovered_ones += (uint8_t)((recovered >> i) & 0x01U);
      }
      uint8_t recovered_parity = (uint8_t)((ps2_data_byte >> 7U) & 0x01U);
      uint8_t recovered_parity_ok = ((uint8_t)(recovered_ones + recovered_parity) & 0x01U) ? 1U : 0U;
      if (ps2_phase_fix_enabled && bit == 0U && recovered_parity_ok) {
        ps2_phase_recovered++;
        ps2_rx_push(recovered);
      } else {
      ps2_frame_error++;
      ps2_last_err_byte = ps2_data_byte;
      ps2_last_err_parity = ps2_parity_bit;
      ps2_last_err_stop = bit;
      ps2_last_err_ones = ones;
      }
    } else if (!parity_ok) {
      ps2_parity_error++;
      ps2_last_err_byte = ps2_data_byte;
      ps2_last_err_parity = ps2_parity_bit;
      ps2_last_err_stop = bit;
      ps2_last_err_ones = ones;
    } else {
      ps2_rx_push(ps2_data_byte);
    }

    ps2_bit_count = 0U;
  }
}

/**
 * @brief Redirect printf to UART for debugging
 */
int _write(int file, char *ptr, int len) {
    HAL_UART_Transmit(&huart2, (uint8_t*)ptr, len, HAL_MAX_DELAY);
    return len;
}

static int clamp_i(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static float __attribute__((unused)) clamp_f(float v, float lo, float hi)
{
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

static int abs_i(int v)
{
    return (v < 0) ? -v : v;
}

static int sign_i(int v)
{
    if (v < 0) return -1;
    if (v > 0) return 1;
    return 0;
}

static uint16_t swap16(uint16_t v);

static void lcd_diag_fill_slow(uint16_t color_swapped)
{
  const uint8_t hi = (uint8_t)(color_swapped >> 8);
  const uint8_t lo = (uint8_t)(color_swapped & 0xFFU);

  ST7789V2_Set_Address_Window(&cfg0, 0, LCD_Y_OFFSET, 239, LCD_Y_OFFSET + 239);
  ST7789V2_Send_Command(&cfg0, ST7789_RAMWR);
  for (uint32_t i = 0; i < (uint32_t)ST7789V2_WIDTH * (uint32_t)ST7789V2_HEIGHT; i++) {
    ST7789V2_Send_Data(&cfg0, hi);
    ST7789V2_Send_Data(&cfg0, lo);
  }
}

#if PS2_TEST_MODE && PS2_SCREEN_TEST
static uint16_t ps2_cursor_bg_at(int x, int y)
{
  (void)x;
  (void)y;
  return swap16(0x0841U);
}

static void ps2_send_rect(int x0, int y0, int x1, int y1, uint16_t color)
{
  x0 = clamp_i(x0, 0, ST7789V2_WIDTH - 1);
  x1 = clamp_i(x1, 0, ST7789V2_WIDTH - 1);
  y0 = clamp_i(y0, 0, ST7789V2_HEIGHT - 1);
  y1 = clamp_i(y1, 0, ST7789V2_HEIGHT - 1);
  if (x0 > x1 || y0 > y1) {
    return;
  }
  const int w = x1 - x0 + 1;
  for (int x = 0; x < w; x++) {
    line_buffer0[x] = color;
  }
  for (int y = y0; y <= y1; y++) {
    ST7789V2_Set_Address_Window(&cfg0, (uint16_t)x0, (uint16_t)(y + LCD_Y_OFFSET), (uint16_t)x1, (uint16_t)(y + LCD_Y_OFFSET));
    ST7789V2_Send_Command(&cfg0, ST7789_RAMWR);
    ST7789V2_Send_Data_Block(&cfg0, (uint8_t*)line_buffer0, (uint32_t)w * 2U);
  }
}

static void ps2_draw_cursor_partial(int x, int y, uint16_t color)
{
  ps2_send_rect(x - 1, y - 8, x + 1, y + 8, color);
  ps2_send_rect(x - 8, y - 1, x + 8, y + 1, color);
  ps2_send_rect(x - 2, y - 2, x + 2, y + 2, swap16(0xFFFFU));
}

static void ps2_render_cursor_full(int cursor_x, int cursor_y, int last_dx, int last_dy, uint8_t stream_enabled)
{
  const uint16_t bg = swap16(0x0841U);
  const uint16_t grid = swap16(0x2104U);
  const uint16_t axis = swap16(0x4208U);
  const uint16_t cursor = swap16(stream_enabled ? 0x07E0U : 0xF800U);
  const uint16_t tail = swap16(0xFFFFU);

  for (int y = 0; y < ST7789V2_HEIGHT; y++) {
    for (int x = 0; x < ST7789V2_WIDTH; x++) {
      uint16_t c = bg;
      if ((x % 24) == 0 || (y % 24) == 0) {
        c = grid;
      }
      if (x == (ST7789V2_WIDTH / 2) || y == (ST7789V2_HEIGHT / 2)) {
        c = axis;
      }

      if ((abs_i(x - cursor_x) <= 1 && abs_i(y - cursor_y) <= 8) ||
          (abs_i(y - cursor_y) <= 1 && abs_i(x - cursor_x) <= 8)) {
        c = cursor;
      }
      if (abs_i(x - cursor_x) <= 2 && abs_i(y - cursor_y) <= 2) {
        c = tail;
      }

      // Tiny motion bars in the top-left corner: horizontal dx, vertical dy.
      if (y >= 6 && y <= 8 && x >= 8 && x < (8 + abs_i(last_dx))) {
        c = (last_dx >= 0) ? swap16(0x07E0U) : swap16(0xF800U);
      }
      if (x >= 6 && x <= 8 && y >= 12 && y < (12 + abs_i(last_dy))) {
        c = (last_dy >= 0) ? swap16(0x07E0U) : swap16(0xF800U);
      }

      line_buffer0[x] = c;
    }

    ST7789V2_Set_Address_Window(&cfg0, 0, (uint16_t)(y + LCD_Y_OFFSET), 239, (uint16_t)(y + LCD_Y_OFFSET));
    ST7789V2_Send_Command(&cfg0, ST7789_RAMWR);
    ST7789V2_Send_Data_Block(&cfg0, (uint8_t*)line_buffer0, ST7789V2_WIDTH * 2);
  }
}

static void ps2_render_cursor_test(int old_x, int old_y, int cursor_x, int cursor_y, int last_dx, int last_dy, uint8_t stream_enabled)
{
#if PS2_CURSOR_PARTIAL_RENDER
  ps2_send_rect(old_x - 9, old_y - 9, old_x + 9, old_y + 9, ps2_cursor_bg_at(old_x, old_y));
  ps2_draw_cursor_partial(cursor_x, cursor_y, swap16(stream_enabled ? 0x07E0U : 0xF800U));
  (void)last_dx;
  (void)last_dy;
#else
  ps2_render_cursor_full(cursor_x, cursor_y, last_dx, last_dy, stream_enabled);
  (void)old_x;
  (void)old_y;
#endif
}
#endif

static int sync_eased_delta(int yaw, int max_delta)
{
    int s = sign_i(yaw);
    int a = abs_i(yaw);
    if (a > YAW_RANGE) {
      a = YAW_RANGE;
    }
    if (s == 0 || a == 0 || max_delta <= 0) {
      return 0;
    }

    int te_q15 = (int)yaw_ease_q15[a];
    int out = (int)(((int64_t)max_delta * (int64_t)te_q15 + 16383) / 32767);
    return s * out;
}

static uint16_t swap16(uint16_t v)
{
  return (uint16_t)((v << 8) | (v >> 8));
}

static void init_swapped_luts(void)
{
  for (int i = 0; i < 256; i++) {
    leftwall_lut_swapped[i] = swap16(leftwall_lut565[i]);
    center_lut_swapped[i] = swap16(center_lut565[i]);
    rightwall_lut_swapped[i] = swap16(rightwall_lut565[i]);
    gun_image_lut_swapped[i] = swap16(gun_image_lut565[i]);
    kuwu_lut_swapped[i] = swap16(kuwu_lut565[i]);
    robot1_lut_swapped[i] = swap16(robot1_lut565[i]);
  }
}

static void blit_span_idx8(uint16_t *dst,
                           int dst_x,
                           const uint8_t *src_idx,
                           const uint16_t *lut565,
                           int src_x,
                           int w,
                           int src_w)
{
    if (w <= 0) return;
    if (dst_x < 0) {
      src_x += -dst_x;
      w += dst_x;
      dst_x = 0;
    }
    if (dst_x + w > ST7789V2_WIDTH) {
      w = ST7789V2_WIDTH - dst_x;
    }
    if (w > 0) {
      for (int i = 0; i < w; i++) {
            int sx = src_x + i;
            if (sx < 0) sx = 0;
            if (sx >= src_w) sx = src_w - 1;
            dst[dst_x + i] = lut565[src_idx[sx]];
      }
    }
}

  static void compose_scene_line(uint16_t *dst,
                   int src_y,
                   int left_src_w,
                   int center_src_w,
                   int right_src_w,
                   int x_left,
                   int left_x0,
                   int left_w,
                   int x_center,
                   int center_x0,
                   int center_w,
                   int x_right,
                   int right_x0,
                   int right_w)
  {
    const uint8_t *left_row_idx = &leftwall_idx8[src_y * left_src_w];
    const uint8_t *center_row_idx = &center_idx8[src_y * center_src_w];
    const uint8_t *right_row_idx = &rightwall_idx8[src_y * right_src_w];

    for (int x = 0; x < ST7789V2_WIDTH; x++) {
      dst[x] = 0x0000;
    }

    // Draw walls first, then let center overlap seams by 1px to hide tiny cracks.
    blit_span_idx8(dst, x_left, left_row_idx, leftwall_lut_swapped, left_x0, left_w, left_src_w);
    blit_span_idx8(dst, x_right, right_row_idx, rightwall_lut_swapped, right_x0, right_w, right_src_w);
    blit_span_idx8(dst,
                   x_center,
                   center_row_idx,
                   center_lut_swapped,
                   center_x0,
                   center_w,
                   center_src_w);
  }

  static uint8_t hud_font_3x5(char c, uint8_t row)
  {
    if (row >= 5U) {
      return 0U;
    }
    switch (c) {
      case '0': { static const uint8_t p[5] = {7, 5, 5, 5, 7}; return p[row]; }
      case '1': { static const uint8_t p[5] = {2, 6, 2, 2, 7}; return p[row]; }
      case '2': { static const uint8_t p[5] = {7, 1, 7, 4, 7}; return p[row]; }
      case '3': { static const uint8_t p[5] = {7, 1, 7, 1, 7}; return p[row]; }
      case '4': { static const uint8_t p[5] = {5, 5, 7, 1, 1}; return p[row]; }
      case '5': { static const uint8_t p[5] = {7, 4, 7, 1, 7}; return p[row]; }
      case '6': { static const uint8_t p[5] = {7, 4, 7, 5, 7}; return p[row]; }
      case '7': { static const uint8_t p[5] = {7, 1, 2, 2, 2}; return p[row]; }
      case '8': { static const uint8_t p[5] = {7, 5, 7, 5, 7}; return p[row]; }
      case '9': { static const uint8_t p[5] = {7, 5, 7, 1, 7}; return p[row]; }
      case 'A': { static const uint8_t p[5] = {2, 5, 7, 5, 5}; return p[row]; }
      case 'B': { static const uint8_t p[5] = {6, 5, 6, 5, 6}; return p[row]; }
      case 'C': { static const uint8_t p[5] = {7, 4, 4, 4, 7}; return p[row]; }
      case 'D': { static const uint8_t p[5] = {6, 5, 5, 5, 6}; return p[row]; }
      case 'E': { static const uint8_t p[5] = {7, 4, 6, 4, 7}; return p[row]; }
      case 'F': { static const uint8_t p[5] = {7, 4, 6, 4, 4}; return p[row]; }
      case 'G': { static const uint8_t p[5] = {7, 4, 5, 5, 7}; return p[row]; }
      case 'H': { static const uint8_t p[5] = {5, 5, 7, 5, 5}; return p[row]; }
      case 'I': { static const uint8_t p[5] = {7, 2, 2, 2, 7}; return p[row]; }
      case 'K': { static const uint8_t p[5] = {5, 5, 6, 5, 5}; return p[row]; }
      case 'L': { static const uint8_t p[5] = {4, 4, 4, 4, 7}; return p[row]; }
      case 'M': { static const uint8_t p[5] = {5, 7, 7, 5, 5}; return p[row]; }
      case 'N': { static const uint8_t p[5] = {5, 7, 7, 7, 5}; return p[row]; }
      case 'O': { static const uint8_t p[5] = {7, 5, 5, 5, 7}; return p[row]; }
      case 'P': { static const uint8_t p[5] = {6, 5, 6, 4, 4}; return p[row]; }
      case 'Q': { static const uint8_t p[5] = {7, 5, 5, 7, 1}; return p[row]; }
      case 'R': { static const uint8_t p[5] = {6, 5, 6, 5, 5}; return p[row]; }
      case 'S': { static const uint8_t p[5] = {7, 4, 7, 1, 7}; return p[row]; }
      case 'T': { static const uint8_t p[5] = {7, 2, 2, 2, 2}; return p[row]; }
      case 'U': { static const uint8_t p[5] = {5, 5, 5, 5, 7}; return p[row]; }
      case 'V': { static const uint8_t p[5] = {5, 5, 5, 5, 2}; return p[row]; }
      case 'W': { static const uint8_t p[5] = {5, 5, 7, 7, 5}; return p[row]; }
      case 'X': { static const uint8_t p[5] = {5, 5, 2, 5, 5}; return p[row]; }
      case 'Y': { static const uint8_t p[5] = {5, 5, 2, 2, 2}; return p[row]; }
      case 'Z': { static const uint8_t p[5] = {7, 1, 2, 4, 7}; return p[row]; }
      case '>': { static const uint8_t p[5] = {4, 2, 1, 2, 4}; return p[row]; }
      case '/': { static const uint8_t p[5] = {1, 1, 2, 4, 4}; return p[row]; }
      case '%': { static const uint8_t p[5] = {5, 1, 2, 4, 5}; return p[row]; }
      default: return 0U;
    }
  }

  static int hud_text_width(const char *s, int scale)
  {
    int n = 0;
    while (s && s[n] != '\0') {
      n++;
    }
    return (n <= 0) ? 0 : (((n * 4) - 1) * scale);
  }

  static void overlay_hud_text_line(uint16_t *dst, int y, int x0, int y0, const char *s, int scale, uint16_t color)
  {
    const int local_y = y - y0;
    if (local_y < 0 || local_y >= 5 * scale) {
      return;
    }
    const uint8_t row = (uint8_t)(local_y / scale);
    int x = x0;
    for (int i = 0; s && s[i] != '\0'; i++) {
      uint8_t bits = hud_font_3x5(s[i], row);
      for (int col = 0; col < 3; col++) {
        if ((bits & (uint8_t)(1U << (2 - col))) != 0U) {
          for (int sx = 0; sx < scale; sx++) {
            int px = x + col * scale + sx;
            if (px >= 0 && px < ST7789V2_WIDTH) {
              dst[px] = color;
            }
          }
        }
      }
      x += 4 * scale;
    }
  }

  static void overlay_hud_infinity_line(uint16_t *dst, int y, int cx, int y0, int scale, uint16_t color)
  {
    const int local_y = y - y0;
    if (local_y < 0 || local_y >= 5 * scale) {
      return;
    }
    const uint8_t row = (uint8_t)(local_y / scale);
    static const uint8_t inf[5] = {0x0A, 0x15, 0x15, 0x15, 0x0A};
    int x0 = cx - (5 * scale) / 2;
    for (int col = 0; col < 5; col++) {
      if ((inf[row] & (uint8_t)(1U << (4 - col))) != 0U) {
        for (int sx = 0; sx < scale; sx++) {
          int px = x0 + col * scale + sx;
          if (px >= 0 && px < ST7789V2_WIDTH) {
            dst[px] = color;
          }
        }
      }
    }
  }

  static void u16_to_dec(char *dst, uint16_t v)
  {
    char tmp[6];
    int n = 0;
    if (v == 0U) {
      dst[0] = '0';
      dst[1] = '\0';
      return;
    }
    while (v > 0U && n < (int)sizeof(tmp)) {
      tmp[n++] = (char)('0' + (v % 10U));
      v /= 10U;
    }
    for (int i = 0; i < n; i++) {
      dst[i] = tmp[n - 1 - i];
    }
    dst[n] = '\0';
  }

  static void append_str(char *dst, int *pos, const char *src)
  {
    while (src && *src != '\0' && *pos < 47) {
      dst[*pos] = *src;
      (*pos)++;
      src++;
    }
    dst[*pos] = '\0';
  }

  static void overlay_stage_stats_line(uint16_t *dst, int y)
  {
    if (y < 0 || y >= BG_VIEW_Y) {
      return;
    }
    if (render_dialog_visible) {
      if (y >= 4 && y < 14) {
        overlay_hud_text_line(dst, y, 8, 4, render_dialog_line0, 2, swap16(0xBDF7U));
      }
      if (y >= 17 && y < 27) {
        overlay_hud_text_line(dst, y, 8, 17, render_dialog_line1, 2, swap16(0xBDF7U));
      }
      if (render_menu_visible) {
        const uint16_t sel = swap16(0x07E0U);
        const uint16_t dim = swap16(0x8C71U);
        if (render_menu_c && render_menu_c[0] != '\0') {
          const char *choice = (render_menu_selected == 0U) ? render_menu_a
                               : ((render_menu_selected == 1U) ? render_menu_b : render_menu_c);
          if (y >= 34 && y < 44) {
            overlay_hud_text_line(dst, y, 8, 34, ">", 2, sel);
            overlay_hud_text_line(dst, y, 24, 34, choice, 2, sel);
          }
        } else if (y >= 30 && y < 40) {
          overlay_hud_text_line(dst, y, 8, 30, render_menu_selected == 0U ? ">" : "", 2, sel);
          overlay_hud_text_line(dst, y, 24, 30, render_menu_a, 2, render_menu_selected == 0U ? sel : dim);
        } else if (y >= 42 && y < 52) {
          overlay_hud_text_line(dst, y, 8, 42, render_menu_selected == 1U ? ">" : "", 2, sel);
          overlay_hud_text_line(dst, y, 24, 42, render_menu_b, 2, render_menu_selected == 1U ? sel : dim);
        }
      }
      return;
    }
    if (y < 8 || y >= 18) {
      return;
    }
    char acc_s[6];
    char hit_s[6];
    char shot_s[6];
    char line[48];
    int pos = 0;
    uint16_t acc = (render_stage_shots == 0U) ? 0U : (uint16_t)(((uint32_t)render_stage_hits * 100U) / (uint32_t)render_stage_shots);
    u16_to_dec(acc_s, acc);
    u16_to_dec(hit_s, render_stage_hits);
    u16_to_dec(shot_s, render_stage_shots);
    append_str(line, &pos, "ACC ");
    append_str(line, &pos, acc_s);
    append_str(line, &pos, "% HITS ");
    append_str(line, &pos, hit_s);
    append_str(line, &pos, "/");
    append_str(line, &pos, shot_s);
    if (render_stage_result > 0) {
      append_str(line, &pos, " PASS");
    } else if (render_stage_result < 0) {
      append_str(line, &pos, " FAIL");
    }
    overlay_hud_text_line(dst, y, 8, 8, line, 2, swap16(0xBDF7U));
  }

  static void overlay_ammo_hud_line(uint16_t *dst, int y)
  {
    if (!render_hud_visible || y < (BG_VIEW_Y + BG_VIEW_H) || y >= ST7789V2_HEIGHT) {
      return;
    }
    char ammo[3];
    uint8_t n = render_hud_ammo;
    if (n >= 10U) {
      ammo[0] = (char)('0' + (n / 10U));
      ammo[1] = (char)('0' + (n % 10U));
      ammo[2] = '\0';
    } else {
      ammo[0] = (char)('0' + n);
      ammo[1] = '\0';
    }

    const int scale = 2;
    const uint16_t text_color = swap16(0xBDF7U);
    const char *name = weapon_display_name(render_hud_weapon);
    const int y_name = BG_VIEW_Y + BG_VIEW_H + 10;
    const int x_name = 10;
    const int ammo_w = hud_text_width(ammo, scale);
    const int x_ammo = ST7789V2_WIDTH - ammo_w - 14;
    overlay_hud_text_line(dst, y, x_name, y_name, name, scale, text_color);
    overlay_hud_text_line(dst, y, x_ammo, y_name, ammo, scale, text_color);
    overlay_hud_infinity_line(dst, y, x_ammo + ammo_w / 2, y_name + 16, scale, text_color);
  }

  static uint16_t blend_white_swapped(uint16_t base_swapped, uint8_t alpha)
  {
    uint16_t base = swap16(base_swapped);
    uint32_t r = (base >> 11) & 0x1FU;
    uint32_t g = (base >> 5) & 0x3FU;
    uint32_t b = base & 0x1FU;
    r = (r * (255U - alpha) + 31U * alpha) / 255U;
    g = (g * (255U - alpha) + 63U * alpha) / 255U;
    b = (b * (255U - alpha) + 31U * alpha) / 255U;
    return swap16((uint16_t)((r << 11) | (g << 5) | b));
  }

  static void draw_crosshair_pixel(uint16_t *dst, int x)
  {
    if (x >= 0 && x < ST7789V2_WIDTH) {
      dst[x] = blend_white_swapped(dst[x], 150U);
    }
  }

static uint8_t active_low_pressed(GPIO_TypeDef *port, uint16_t pin)
{
  return (HAL_GPIO_ReadPin(port, pin) == GPIO_PIN_RESET) ? 1U : 0U;
}

static uint8_t apex_return_to_menu_requested(uint32_t now)
{
  static uint8_t was_pressed = 0U;
  static uint32_t pressed_ms = 0U;
  uint8_t pressed = active_low_pressed(B1_GPIO_Port, B1_Pin);

  if (pressed && !was_pressed) {
    pressed_ms = now;
  }
  was_pressed = pressed;
  return (pressed && (now - pressed_ms) >= 1200U) ? 1U : 0U;
}

  static void overlay_crosshair_line(uint16_t *dst, int y)
  {
    const int cx = ST7789V2_WIDTH / 2;
    const int cy = ST7789V2_HEIGHT / 2;
    const uint16_t cross_color = swap16(0xFFFF);
    if (render_crosshair_ads) {
      if (y >= cy - 1 && y <= cy + 1) {
        for (int x = cx - 1; x <= cx + 1; x++) {
          if (x >= 0 && x < ST7789V2_WIDTH) {
            dst[x] = swap16(0x07E0U);
          }
        }
      }
      return;
    }
    if (render_crosshair_weapon == FIRE_WEAPON_HEPING) {
      const int h = HEPING_CROSSHAIR_HALF;
      static const int8_t arc_x0[7] = {4, 2, 1, 0, 0, 1, 2};
      static const int8_t arc_len[7] = {5, 7, 8, 9, 9, 8, 6};
      const int top = cy - h;
      const int bottom = cy + h;
      if (y >= top && y < top + 7) {
        int row = y - top;
        int lx0 = cx - h + arc_x0[row];
        int lx1 = lx0 + arc_len[row] - 1;
        int rx1 = cx + h - arc_x0[row];
        int rx0 = rx1 - arc_len[row] + 1;
        for (int x = lx0; x <= lx1; x++) {
          if (x >= 0 && x < ST7789V2_WIDTH) dst[x] = cross_color;
        }
        for (int x = rx0; x <= rx1; x++) {
          if (x >= 0 && x < ST7789V2_WIDTH) dst[x] = cross_color;
        }
      }
      if (y <= bottom && y > bottom - 7) {
        int row = bottom - y;
        int lx0 = cx - h + arc_x0[row];
        int lx1 = lx0 + arc_len[row] - 1;
        int rx1 = cx + h - arc_x0[row];
        int rx0 = rx1 - arc_len[row] + 1;
        for (int x = lx0; x <= lx1; x++) {
          if (x >= 0 && x < ST7789V2_WIDTH) dst[x] = cross_color;
        }
        for (int x = rx0; x <= rx1; x++) {
          if (x >= 0 && x < ST7789V2_WIDTH) dst[x] = cross_color;
        }
      }
      return;
    }

    if (render_crosshair_weapon == FIRE_WEAPON_R99 || render_crosshair_weapon == FIRE_WEAPON_FUZHU) {
      const int gap = (render_crosshair_weapon == FIRE_WEAPON_FUZHU) ? 28 : 7;
      const int len = (render_crosshair_weapon == FIRE_WEAPON_FUZHU) ? 8 : 7;
      if (y == cy) {
        for (int x = cx - gap - len; x <= cx - gap; x++) {
          draw_crosshair_pixel(dst, x);
        }
        for (int x = cx + gap; x <= cx + gap + len; x++) {
          draw_crosshair_pixel(dst, x);
        }
      }
      if (y >= cy - gap - len && y <= cy - gap) {
        draw_crosshair_pixel(dst, cx);
      }
      if (y >= cy + gap && y <= cy + gap + len) {
        draw_crosshair_pixel(dst, cx);
      }
      return;
    }

    if (y == cy) {
      int x0 = cx - CROSSHAIR_HALF;
      int x1 = cx + CROSSHAIR_HALF;
      if (x0 < 0) x0 = 0;
      if (x1 >= ST7789V2_WIDTH) x1 = ST7789V2_WIDTH - 1;
      for (int x = x0; x <= x1; x++) {
        dst[x] = cross_color;
      }
    }

    if (y >= (cy - CROSSHAIR_HALF) && y <= (cy + CROSSHAIR_HALF)) {
      dst[cx] = cross_color;
    }
  }

typedef struct {
  uint8_t visible;
  uint8_t show_bar;
  uint8_t dissolve;
  uint8_t dissolve_step;
  uint8_t frame_idx;
  int x;
  int y;
  int bg_x;
  int bg_y;
  int armor;
  int armor_max;
  uint16_t armor_color;
  int health;
} RobotRenderState;

static uint8_t hit_particle_rand8(void)
{
  hit_particle_rng = (uint8_t)(hit_particle_rng * 37U + 17U);
  return hit_particle_rng;
}

static void hit_particles_clear(void)
{
  for (uint8_t i = 0U; i < HIT_PARTICLE_COUNT; i++) {
    hit_particles[i].active = 0U;
  }
  hit_particle_next = 0U;
  hit_particle_last_update_ms = 0U;
}

static uint16_t hit_particle_color_for_kind(R99SfxKind kind, uint16_t armor_color)
{
  if (kind == R99_SFX_KILL) {
    return swap16(0xF800U);
  }
  if (kind == R99_SFX_CRACK) {
    return swap16(0xFFFFU);
  }
  if (kind == R99_SFX_BODY) {
    return swap16(0xE8E4U);
  }
  if (kind == R99_SFX_SHIELD) {
    return armor_color;
  }
  return swap16(0xFFFFU);
}

static void hit_particles_spawn(const RobotRenderState *robot, R99SfxKind kind)
{
  if (!robot || !robot->visible || robot->frame_idx >= ROBOT1_FRAME_COUNT) {
    return;
  }
  const int w = (int)robot1_widths[robot->frame_idx];
  const int h = (int)robot1_heights[robot->frame_idx];
  if (w <= 0 || h <= 0) {
    return;
  }

  int cx = BG_VIEW_X + (BG_VIEW_W / 2);
  int cy = BG_VIEW_Y + (BG_VIEW_H / 2);
  cx = clamp_i(cx, robot->x + 3, robot->x + w - 4);
  cy = clamp_i(cy, robot->y + 4, robot->y + h - 5);
  cx = clamp_i(cx, BG_VIEW_X + 1, BG_VIEW_X + BG_VIEW_W - 2);
  cy = clamp_i(cy, BG_VIEW_Y + 1, BG_VIEW_Y + BG_VIEW_H - 2);

  uint8_t count = 8U;
  uint8_t life_base = 8U;
  if (kind == R99_SFX_CRACK) {
    count = 14U;
    life_base = 10U;
  } else if (kind == R99_SFX_KILL) {
    count = 20U;
    life_base = 12U;
  } else if (kind == R99_SFX_BODY) {
    count = 10U;
  }
  const uint16_t base_color = hit_particle_color_for_kind(kind, robot->armor_color);
  static const int8_t vx_pattern[] = {-14, -9, -5, 4, 8, 13, -11, 11};
  static const int8_t vy_pattern[] = {-13, -16, -10, -15, -8, -12, -5, -7};

  for (uint8_t i = 0U; i < count; i++) {
    HitParticle *p = &hit_particles[hit_particle_next];
    hit_particle_next = (uint8_t)((hit_particle_next + 1U) % HIT_PARTICLE_COUNT);
    uint8_t r = hit_particle_rand8();
    int jitter_x = (int)((r & 0x0FU) - 7);
    int jitter_y = (int)(((r >> 4) & 0x0FU) - 7);
    uint8_t idx = (uint8_t)(i & 7U);
    int vx = (int)vx_pattern[idx] + (int)((r & 3U) - 1);
    int vy = (int)vy_pattern[idx] - (int)((r >> 2) & 3U);
    if (kind == R99_SFX_KILL) {
      vx = (vx * 3) / 2;
      vy = (vy * 3) / 2;
    }
    p->active = 1U;
    p->life = (uint8_t)(life_base + (r & 3U));
    p->size = (kind == R99_SFX_KILL || kind == R99_SFX_CRACK) ? 2U : 1U;
    p->x_q4 = (int16_t)((cx + jitter_x) * 16);
    p->y_q4 = (int16_t)((cy + jitter_y) * 16);
    p->vx_q4 = (int8_t)vx;
    p->vy_q4 = (int8_t)vy;
    p->color = base_color;
  }
}

static void hit_particles_update(uint32_t now)
{
  if (hit_particle_last_update_ms == 0U) {
    hit_particle_last_update_ms = now;
    return;
  }
  uint32_t elapsed = now - hit_particle_last_update_ms;
  if (elapsed < HIT_PARTICLE_STEP_MS) {
    return;
  }
  uint8_t steps = (uint8_t)(elapsed / HIT_PARTICLE_STEP_MS);
  if (steps > 4U) {
    steps = 4U;
  }
  hit_particle_last_update_ms += (uint32_t)steps * HIT_PARTICLE_STEP_MS;

  for (uint8_t i = 0U; i < HIT_PARTICLE_COUNT; i++) {
    HitParticle *p = &hit_particles[i];
    if (!p->active) {
      continue;
    }
    for (uint8_t s = 0U; s < steps; s++) {
      p->x_q4 = (int16_t)(p->x_q4 + p->vx_q4);
      p->y_q4 = (int16_t)(p->y_q4 + p->vy_q4);
      if (p->vy_q4 < 18) {
        p->vy_q4 = (int8_t)(p->vy_q4 + 2);
      }
      if (p->life > 0U) {
        p->life--;
      }
    }
    int x = p->x_q4 / 16;
    int y = p->y_q4 / 16;
    if (p->life == 0U
        || x < BG_VIEW_X
        || x >= (BG_VIEW_X + BG_VIEW_W)
        || y < BG_VIEW_Y
        || y >= (BG_VIEW_Y + BG_VIEW_H)) {
      p->active = 0U;
    }
  }
}

static void ultimate_trail_clear(void)
{
  for (uint8_t i = 0U; i < ULTIMATE_TRAIL_COUNT; i++) {
    ultimate_trail[i].active = 0U;
  }
  ultimate_trail_next = 0U;
  ultimate_trail_filled = 0U;
  ultimate_trail_last_sample_ms = 0U;
}

static void ultimate_trail_sample(const RobotRenderState *robot, uint32_t now, uint8_t enabled)
{
  if (!enabled || !robot || !robot->visible || robot->frame_idx >= ROBOT1_FRAME_COUNT) {
    ultimate_trail_clear();
    return;
  }
  if (ultimate_trail_last_sample_ms != 0U && (now - ultimate_trail_last_sample_ms) < ULTIMATE_TRAIL_SAMPLE_MS) {
    return;
  }
  ultimate_trail_last_sample_ms = now;

  const int w = (int)robot1_widths[robot->frame_idx];
  const int h = (int)robot1_heights[robot->frame_idx];
  int cx = robot->x + (w / 2);
  int cy = robot->y + (h / 2);
  if (cx < BG_VIEW_X || cx >= (BG_VIEW_X + BG_VIEW_W)
      || cy < BG_VIEW_Y || cy >= (BG_VIEW_Y + BG_VIEW_H)) {
    return;
  }

  UltimateTrailPoint *p = &ultimate_trail[ultimate_trail_next];
  p->active = 1U;
  p->bg_x = (int16_t)robot->bg_x;
  p->bg_y = (int16_t)robot->bg_y;
  ultimate_trail_next = (uint8_t)((ultimate_trail_next + 1U) % ULTIMATE_TRAIL_COUNT);
  if (ultimate_trail_filled < ULTIMATE_TRAIL_COUNT) {
    ultimate_trail_filled++;
  }
}

typedef struct {
  uint16_t duration_ms;
  int16_t x0;
  int16_t y0;
  int16_t x1;
  int16_t y1;
  int16_t lift0;
  int16_t lift1;
  int16_t arc;
  uint8_t mode;
} RobotPathSegment;

static void overlay_kuwu_internal_line(uint16_t *dst, int y)
{
  if (y < BG_VIEW_Y || y >= (BG_VIEW_Y + BG_VIEW_H)) {
    return;
  }
  const int draw_x = BG_VIEW_X + (int)kuwu_canvas_x;
  const int draw_y = BG_VIEW_Y + (int)kuwu_canvas_y + WEAPON_VIEW_Y_OFFSET;
  const int src_y = y - draw_y;
  if (src_y < 0 || src_y >= (int)kuwu_height) {
    return;
  }

  int draw_x0 = draw_x;
  int draw_x1 = draw_x + (int)kuwu_width;
  if (draw_x0 < BG_VIEW_X) {
    draw_x0 = BG_VIEW_X;
  }
  if (draw_x1 > (BG_VIEW_X + BG_VIEW_W)) {
    draw_x1 = BG_VIEW_X + BG_VIEW_W;
  }
  if (draw_x0 >= draw_x1) {
    return;
  }

  const uint8_t *src = &kuwu_idx8[(uint32_t)src_y * (uint32_t)kuwu_width];
  for (int x = draw_x0; x < draw_x1; x++) {
    const int src_x = x - draw_x;
    const uint8_t idx = src[src_x];
    if (idx != KUWU_TRANSPARENT_INDEX) {
      dst[x] = kuwu_lut_swapped[idx];
    }
  }
}

#if ROBOT_TEST_ENABLE
enum {
  ROBOT_PATH_RUN = 0,
  ROBOT_PATH_SLIDE_ENTER,
  ROBOT_PATH_SLIDE_HOLD,
  ROBOT_PATH_JUMP,
  ROBOT_PATH_WALL,
  ROBOT_PATH_KICK,
  ROBOT_PATH_LAND,
  ROBOT_PATH_LURCH,
};

static const RobotPathSegment robot_ultimate_path[] = {
  {520, 218, 186, 225, 200, 0, 0, 0, ROBOT_PATH_RUN},
  {80,  225, 200, 227, 201, 0, 0, 0, ROBOT_PATH_SLIDE_ENTER},
  {230, 227, 201, 231, 204, 0, 0, 0, ROBOT_PATH_SLIDE_HOLD},
  {390, 231, 204, 300, 192, 0, 12, 10, ROBOT_PATH_JUMP},
  {55,  300, 192, 300, 192, 12, 12, 0, ROBOT_PATH_WALL},
  {610, 300, 192, 178, 196, 12, 0, 30, ROBOT_PATH_KICK},
  {110, 178, 196, 178, 204, 0, 0, 0, ROBOT_PATH_LAND},
  {230, 178, 204, 204, 203, 0, 2, 1, ROBOT_PATH_LURCH},
  {280, 204, 203, 170, 205, 2, 0, 2, ROBOT_PATH_LURCH},
  {210, 170, 205, 190, 204, 0, 1, 1, ROBOT_PATH_LURCH},
  {260, 190, 204, 214, 202, 1, 2, 2, ROBOT_PATH_LURCH},
  {240, 214, 202, 182, 204, 2, 0, 1, ROBOT_PATH_LURCH},
  {300, 182, 204, 168, 205, 0, 1, 2, ROBOT_PATH_LURCH},
  {220, 168, 205, 198, 203, 1, 2, 1, ROBOT_PATH_LURCH},
  {250, 198, 203, 178, 204, 2, 0, 1, ROBOT_PATH_LURCH},
};

static uint8_t robot_scale_for_grid_y(int grid_y)
{
  uint8_t best = 0U;
  int best_d = 32767;
  for (uint8_t i = 0; i < ROBOT1_SCALE_COUNT; i++) {
    int d = abs_i(grid_y - (int)robot1_grid_y[i]);
    if (d < best_d) {
      best_d = d;
      best = i;
    }
  }
  return best;
}

static uint8_t robot_frame_for_pose_scale(uint8_t pose, uint8_t scale_idx)
{
  if (pose >= ROBOT1_POSE_COUNT) {
    pose = 0U;
  }
  if (scale_idx >= ROBOT1_SCALE_COUNT) {
    scale_idx = 0U;
  }
  uint16_t frame = (uint16_t)pose * ROBOT1_SCALE_COUNT + scale_idx;
  if (frame >= ROBOT1_FRAME_COUNT) {
    return 0U;
  }
  return (uint8_t)frame;
}

static uint8_t robot_ultimate_scale_for_y(int foot_y)
{
  uint8_t best = 0U;
  int best_d = 32767;
  for (uint8_t i = 0; i < ROBOT1_ULTIMATE_SCALE_COUNT; i++) {
    int d = abs_i(foot_y - (int)robot1_ultimate_scale_y[i]);
    if (d < best_d) {
      best_d = d;
      best = i;
    }
  }
  return best;
}

static uint8_t robot_ultimate_frame(uint8_t image_idx, uint8_t scale_idx)
{
  if (image_idx >= ROBOT1_ULTIMATE_FRAME_COUNT) {
    image_idx = 0U;
  }
  if (scale_idx >= ROBOT1_ULTIMATE_SCALE_COUNT) {
    scale_idx = ROBOT1_ULTIMATE_SCALE_COUNT - 1U;
  }
  uint16_t frame = ROBOT1_ULTIMATE_OFFSET + (uint16_t)image_idx * ROBOT1_ULTIMATE_SCALE_COUNT + scale_idx;
  return (frame < ROBOT1_FRAME_COUNT) ? (uint8_t)frame : 0U;
}

static uint8_t robot_die_frame(uint8_t image_idx, uint8_t scale_idx)
{
  if (ROBOT1_DIE_FRAME_COUNT == 0U) {
    return 0U;
  }
  if (image_idx >= ROBOT1_DIE_FRAME_COUNT) {
    image_idx = ROBOT1_DIE_FRAME_COUNT - 1U;
  }
  if (scale_idx >= ROBOT1_ULTIMATE_SCALE_COUNT) {
    scale_idx = ROBOT1_ULTIMATE_SCALE_COUNT - 1U;
  }
  uint16_t frame = ROBOT1_DIE_OFFSET + (uint16_t)image_idx * ROBOT1_ULTIMATE_SCALE_COUNT + scale_idx;
  return (frame < ROBOT1_FRAME_COUNT) ? (uint8_t)frame : 0U;
}

static uint8_t robot_die_image_for_step(uint32_t step)
{
  if (step <= 1U) {
    return 0U;
  }
  if (step <= 3U) {
    return 1U;
  }
  if (step <= 7U) {
    return 2U;
  }
  return 3U;
}

static int robot_patrol_offset_at(uint32_t now)
{
  uint32_t half_period = ROBOT_PATROL_PERIOD_MS / 2U;
  int patrol_offset = 0;
  if (half_period > 0U) {
    uint32_t patrol_t = now % ROBOT_PATROL_PERIOD_MS;
    if (patrol_t < half_period) {
      patrol_offset = -ROBOT_PATROL_RANGE_PX
                    + (int)((patrol_t * (uint32_t)(ROBOT_PATROL_RANGE_PX * 2)) / half_period);
    } else {
      uint32_t t = patrol_t - half_period;
      patrol_offset = ROBOT_PATROL_RANGE_PX
                    - (int)((t * (uint32_t)(ROBOT_PATROL_RANGE_PX * 2)) / half_period);
    }
  }
  return patrol_offset;
}

static uint32_t robot_path_total_ms(void)
{
  uint32_t total = 0U;
  for (uint32_t i = 0; i < (sizeof(robot_ultimate_path) / sizeof(robot_ultimate_path[0])); i++) {
    total += robot_ultimate_path[i].duration_ms;
  }
  return total;
}

static uint32_t robot_path_lurch_start_ms(void)
{
  uint32_t total = 0U;
  for (uint32_t i = 0; i < (sizeof(robot_ultimate_path) / sizeof(robot_ultimate_path[0])); i++) {
    if (robot_ultimate_path[i].mode == ROBOT_PATH_LURCH) {
      return total;
    }
    total += robot_ultimate_path[i].duration_ms;
  }
  return total;
}

static uint32_t robot_path_lurch_total_ms(void)
{
  uint32_t total = 0U;
  for (uint32_t i = 0; i < (sizeof(robot_ultimate_path) / sizeof(robot_ultimate_path[0])); i++) {
    if (robot_ultimate_path[i].mode == ROBOT_PATH_LURCH) {
      total += robot_ultimate_path[i].duration_ms;
    }
  }
  return total;
}

static int robot_lerp_i(int a, int b, uint32_t q15)
{
  return a + (int)(((int32_t)(b - a) * (int32_t)q15) / 32768);
}

static uint32_t robot_smooth_q15(uint32_t q15)
{
  uint32_t q = (q15 > 32768U) ? 32768U : q15;
  uint32_t q2 = (q * q) / 32768U;
  uint32_t factor = 98304U - 2U * q;
  return (q2 * factor) / 32768U;
}

static uint8_t robot_ultimate_image_for_mode(uint8_t mode, uint32_t local_ms)
{
  switch (mode) {
    case ROBOT_PATH_RUN:
      return ((local_ms / 110U) & 1U) ? 1U : 0U; // dengqiang00/03
    case ROBOT_PATH_SLIDE_ENTER:
      return (local_ms < 70U) ? 2U : 3U; // 06 -> 07
    case ROBOT_PATH_SLIDE_HOLD:
      return 3U; // 07
    case ROBOT_PATH_JUMP: {
      static const uint8_t seq[] = {4U, 4U, 5U, 5U, 6U, 6U};
      uint32_t idx = local_ms / 65U;
      if (idx >= sizeof(seq)) {
        idx = sizeof(seq) - 1U;
      }
      return seq[idx];
    }
    case ROBOT_PATH_WALL:
      return 7U; // 25
    case ROBOT_PATH_KICK: {
      static const uint8_t seq[] = {8U, 8U, 9U, 9U, 10U, 10U, 10U};
      uint32_t idx = local_ms / 70U;
      if (idx >= sizeof(seq)) {
        idx = sizeof(seq) - 1U;
      }
      return seq[idx];
    }
    case ROBOT_PATH_LAND:
      return 11U; // luodi
    case ROBOT_PATH_LURCH:
      return 12U; // lurch
    default:
      return 0U;
  }
}

static void robot_ultimate_pose_at(uint32_t now,
                                   uint32_t level_start_ms,
                                   int *bg_x,
                                   int *bg_y,
                                   uint8_t *frame_idx)
{
  uint32_t path_ms = robot_path_total_ms();
  uint32_t elapsed = now - level_start_ms;
  uint32_t lurch_start_ms = robot_path_lurch_start_ms();
  uint32_t lurch_ms = robot_path_lurch_total_ms();
  uint32_t t = 0U;
  if (path_ms == 0U) {
    t = 0U;
  } else if (lurch_ms > 0U && elapsed >= lurch_start_ms) {
    t = lurch_start_ms + ((elapsed - lurch_start_ms) % lurch_ms);
  } else {
    t = (elapsed < path_ms) ? elapsed : (path_ms - 1U);
  }
  uint32_t seg_start = 0U;
  const RobotPathSegment *seg = &robot_ultimate_path[0];
  for (uint32_t i = 0; i < (sizeof(robot_ultimate_path) / sizeof(robot_ultimate_path[0])); i++) {
    if (t < seg_start + robot_ultimate_path[i].duration_ms) {
      seg = &robot_ultimate_path[i];
      break;
    }
    seg_start += robot_ultimate_path[i].duration_ms;
  }
  uint32_t local = t - seg_start;
  uint32_t p_q15 = (seg->duration_ms == 0U) ? 32768U : ((local * 32768U) / seg->duration_ms);
  uint32_t e_q15 = robot_smooth_q15(p_q15);
  int x = robot_lerp_i(seg->x0, seg->x1, e_q15);
  int y = robot_lerp_i(seg->y0, seg->y1, e_q15);
  int lift = robot_lerp_i(seg->lift0, seg->lift1, e_q15);
  lift += (int)(((int64_t)seg->arc * 4LL * (int64_t)p_q15 * (int64_t)(32768U - p_q15)) / (32768LL * 32768LL));
  y -= lift;
  uint8_t scale_idx = ROBOT1_ULTIMATE_SCALE_COUNT - 1U;
  if (seg->mode == ROBOT_PATH_RUN
      || seg->mode == ROBOT_PATH_SLIDE_ENTER
      || seg->mode == ROBOT_PATH_SLIDE_HOLD
      || seg->mode == ROBOT_PATH_LURCH) {
    scale_idx = robot_ultimate_scale_for_y(y);
  }
  if (bg_x) {
    *bg_x = x;
  }
  if (bg_y) {
    *bg_y = y;
  }
  if (frame_idx) {
    *frame_idx = robot_ultimate_frame(robot_ultimate_image_for_mode(seg->mode, local), scale_idx);
  }
}

static void overlay_robot_line(uint16_t *dst, int y, const RobotRenderState *robot)
{
  if (!robot || !robot->visible || robot->frame_idx >= ROBOT1_FRAME_COUNT) {
    return;
  }
  const int w = (int)robot1_widths[robot->frame_idx];
  const int h = (int)robot1_heights[robot->frame_idx];
  if (robot->x >= (BG_VIEW_X + BG_VIEW_W)
      || (robot->x + w) <= BG_VIEW_X
      || robot->y >= (BG_VIEW_Y + BG_VIEW_H)
      || (robot->y + h) <= BG_VIEW_Y) {
    return;
  }
  const int bar_w = 44;
  const int bar_h = 5;
  const int bar_y = robot->y - 10;
  if (robot->show_bar && y >= bar_y && y < (bar_y + bar_h)) {
    int bar_x = robot->x + (w / 2) - (bar_w / 2);
    bar_x = clamp_i(bar_x, BG_VIEW_X + 2, BG_VIEW_X + BG_VIEW_W - bar_w - 2);
    const int local_y = y - bar_y;
    const int armor_max = (robot->armor_max > 0) ? robot->armor_max : ROBOT_ARMOR_MAX;
    const int armor_fill = ((bar_w - 2) * clamp_i(robot->armor, 0, armor_max) + (armor_max / 2)) / armor_max;
    const int health_fill = ((bar_w - 2) * clamp_i(robot->health, 0, ROBOT_HEALTH_MAX) + (ROBOT_HEALTH_MAX / 2)) / ROBOT_HEALTH_MAX;
    const uint16_t border = swap16(0x2925U);
    const uint16_t bg = swap16(0x0841U);
    const uint16_t health = swap16(0xEF5DU);
    const uint16_t armor = robot->armor_color;
    for (int x = bar_x; x < bar_x + bar_w; x++) {
      const int local_x = x - bar_x;
      const int slant_cut = (local_y >= 3) ? 2 : ((local_y >= 1) ? 1 : 0);
      if (local_x >= bar_w - slant_cut) {
        continue;
      }
      if (local_y == 0 || local_y == (bar_h - 1) || local_x == 0 || local_x >= (bar_w - 1 - slant_cut)) {
        dst[x] = border;
      } else {
        uint16_t c = bg;
        const int fill_x = local_x - 1;
        if (fill_x >= 0 && fill_x < health_fill) {
          c = health;
        }
        if (fill_x >= 0 && fill_x < armor_fill) {
          c = armor;
        }
        dst[x] = c;
      }
    }
  }

  const int src_y = y - robot->y;
  if (src_y < 0 || src_y >= h) {
    return;
  }

  int draw_x0 = robot->x;
  int draw_x1 = robot->x + w;
  if (draw_x0 < BG_VIEW_X) {
    draw_x0 = BG_VIEW_X;
  }
  if (draw_x1 > (BG_VIEW_X + BG_VIEW_W)) {
    draw_x1 = BG_VIEW_X + BG_VIEW_W;
  }
  if (draw_x0 < 0) {
    draw_x0 = 0;
  }
  if (draw_x1 > ST7789V2_WIDTH) {
    draw_x1 = ST7789V2_WIDTH;
  }
  if (draw_x0 >= draw_x1) {
    return;
  }

  const uint8_t *src = robot1_frames[robot->frame_idx] + (uint32_t)src_y * (uint32_t)w;
  for (int x = draw_x0; x < draw_x1; x++) {
    const int src_x = x - robot->x;
    const uint8_t idx = src[src_x];
    if (idx != ROBOT1_TRANSPARENT_INDEX) {
      if (robot->dissolve) {
        uint8_t noise = (uint8_t)((src_x * 37 + src_y * 53 + (src_x ^ src_y) * 11) & 0xFF);
        uint8_t threshold = (uint8_t)(robot->dissolve_step * 22U);
        if (noise < threshold) {
          continue;
        }
      }
      dst[x] = robot1_lut_swapped[idx];
    }
  }
}

static void overlay_hit_particles_line(uint16_t *dst, int y)
{
  if (y < BG_VIEW_Y || y >= (BG_VIEW_Y + BG_VIEW_H)) {
    return;
  }
  for (uint8_t i = 0U; i < HIT_PARTICLE_COUNT; i++) {
    const HitParticle *p = &hit_particles[i];
    if (!p->active) {
      continue;
    }
    int px = p->x_q4 / 16;
    int py = p->y_q4 / 16;
    int size = (int)p->size;
    if (size < 1) {
      size = 1;
    }
    if (y < py || y >= (py + size)) {
      continue;
    }
    for (int x = px; x < px + size; x++) {
      if (x >= BG_VIEW_X && x < (BG_VIEW_X + BG_VIEW_W)) {
        dst[x] = p->color;
      }
    }
  }
}

static void overlay_ultimate_trail_line(uint16_t *dst, int y)
{
  if (y < BG_VIEW_Y || y >= (BG_VIEW_Y + BG_VIEW_H) || ultimate_trail_filled == 0U || !qspi_bg_ready) {
    return;
  }
  static const uint16_t colors[] = {
    0x981FU, 0xB81FU, 0xD81FU, 0xF81FU, 0xF9DFU, 0xFBBFU,
  };
  const int max_src_x = ((int)qspi_bg_asset.width > BG_VIEW_W) ? ((int)qspi_bg_asset.width - BG_VIEW_W) : 0;
  const int max_src_y = ((int)qspi_bg_asset.height > BG_VIEW_H) ? ((int)qspi_bg_asset.height - BG_VIEW_H) : 0;
  const int crop_x = clamp_i(bg_src_x_dynamic, 0, max_src_x);
  const int crop_y = clamp_i(bg_src_y_dynamic, 0, max_src_y);
  for (uint8_t age = 0U; age < ultimate_trail_filled; age++) {
    uint8_t newest = (ultimate_trail_next == 0U) ? (ULTIMATE_TRAIL_COUNT - 1U) : (uint8_t)(ultimate_trail_next - 1U);
    uint8_t idx = (uint8_t)((newest + ULTIMATE_TRAIL_COUNT - age) % ULTIMATE_TRAIL_COUNT);
    const UltimateTrailPoint *p = &ultimate_trail[idx];
    if (!p->active) {
      continue;
    }
    int sx = BG_VIEW_X + (int)p->bg_x - crop_x;
    int sy = BG_VIEW_Y + (int)p->bg_y - crop_y;
    if (sx < BG_VIEW_X - 16 || sx >= (BG_VIEW_X + BG_VIEW_W + 16)
        || sy < BG_VIEW_Y - 8 || sy >= (BG_VIEW_Y + BG_VIEW_H + 8)) {
      continue;
    }
    int len = 11 - (int)(age / 2U);
    if (len < 3) {
      len = 3;
    }
    int thickness = (age < 4U) ? 2 : 1;
    int slant = (age & 1U) ? 1 : -1;
    int cy = sy - (int)(age / 4U);
    if (y < cy || y >= (cy + thickness)) {
      continue;
    }
    int row = y - cy;
    int x0 = sx - len - (int)(age / 2U) + row * slant;
    int x1 = sx + 1 + row * slant;
    if (x0 < BG_VIEW_X) {
      x0 = BG_VIEW_X;
    }
    if (x1 > (BG_VIEW_X + BG_VIEW_W)) {
      x1 = BG_VIEW_X + BG_VIEW_W;
    }
    uint8_t color_idx = (uint8_t)(age / 2U);
    if (color_idx >= (uint8_t)(sizeof(colors) / sizeof(colors[0]))) {
      color_idx = (uint8_t)((sizeof(colors) / sizeof(colors[0])) - 1U);
    }
    uint16_t c = swap16(colors[color_idx]);
    for (int x = x0; x < x1; x++) {
      dst[x] = c;
    }
  }
}
#endif

static void overlay_bloodhound_line(uint16_t *dst, int y)
{
  if (!render_bloodhound_visible) {
    return;
  }
  if (y < BG_VIEW_Y || y >= (BG_VIEW_Y + BG_VIEW_H)) {
    return;
  }
  if (render_bloodhound_frame >= BLOODHOUND_FRAME_COUNT) {
    return;
  }
  const int w = (int)bloodhound_widths[render_bloodhound_frame];
  const int h = (int)bloodhound_heights[render_bloodhound_frame];
  const int src_y = y - render_bloodhound_y;
  if (src_y < 0 || src_y >= h) {
    return;
  }
  int draw_x0 = render_bloodhound_x;
  int draw_x1 = render_bloodhound_x + w;
  if (draw_x0 < BG_VIEW_X) {
    draw_x0 = BG_VIEW_X;
  }
  if (draw_x1 > (BG_VIEW_X + BG_VIEW_W)) {
    draw_x1 = BG_VIEW_X + BG_VIEW_W;
  }
  if (draw_x0 >= draw_x1) {
    return;
  }
  const uint8_t *src = &bloodhound_frames[render_bloodhound_frame][(uint32_t)src_y * (uint32_t)w];
  for (int x = draw_x0; x < draw_x1; x++) {
    int src_x = x - render_bloodhound_x;
    uint8_t idx = src[src_x];
    if (idx != BLOODHOUND_TRANSPARENT_INDEX) {
      dst[x] = swap16(bloodhound_lut565[idx]);
    }
  }
}

typedef enum {
  WEAPON_RENDER_GUN_IMAGE,
  WEAPON_RENDER_QSPI_KNIFE,
} WeaponRenderType;

typedef enum {
  HELD_WEAPON_KUWU,
  HELD_WEAPON_KUWU_STOW_TO_GUN,
  HELD_WEAPON_QIEQIANG_TO_R99,
  HELD_WEAPON_R99,
  HELD_WEAPON_SHOUQIANG_TO_KUWU,
  HELD_WEAPON_RELOAD,
  HELD_WEAPON_ADS_ANIM,
  HELD_WEAPON_FIRE_ANIM,
} HeldWeaponState;

typedef struct {
  WeaponRenderType type;
  int dx;
  int dy;
  int frame_idx;
  int gun_image_idx;
} WeaponRenderState;

static void overlay_gun_image_line(uint16_t *dst, int y, int image_idx, int dx, int dy)
{
  if (image_idx < 0 || image_idx >= (int)GUN_IMAGE_COUNT) {
    return;
  }
  if (y < BG_VIEW_Y || y >= (BG_VIEW_Y + BG_VIEW_H)) {
    return;
  }
  const int w = (int)gun_image_widths[image_idx];
  const int h = (int)gun_image_heights[image_idx];
  const int draw_x = BG_VIEW_X + (int)gun_image_x[image_idx] + dx;
  const int draw_y = BG_VIEW_Y + (int)gun_image_y[image_idx] + dy + WEAPON_VIEW_Y_OFFSET;
  const int src_y = y - draw_y;
  if (src_y < 0 || src_y >= h) {
    return;
  }
  int draw_x0 = draw_x;
  int draw_x1 = draw_x + w;
  if (draw_x0 < BG_VIEW_X) {
    draw_x0 = BG_VIEW_X;
  }
  if (draw_x1 > (BG_VIEW_X + BG_VIEW_W)) {
    draw_x1 = BG_VIEW_X + BG_VIEW_W;
  }
  if (draw_x0 >= draw_x1) {
    return;
  }

  const uint8_t *src = gun_image_frames[image_idx] + (uint32_t)src_y * (uint32_t)w;
  for (int x = draw_x0; x < draw_x1; x++) {
    const int src_x = x - draw_x;
    const uint8_t idx = src[src_x];
    if (idx != GUN_IMAGE_TRANSPARENT_INDEX) {
      dst[x] = gun_image_lut_swapped[idx];
    }
  }
}

static void overlay_qspi_weapon_line(uint16_t *dst, int y, int frame_idx, int weapon_x, int weapon_y)
{
  (void)weapon_x;
  (void)weapon_y;
  if (!qspi_weapon_ready || frame_idx < 0 || frame_idx >= (int)qspi_weapon_asset.stored_frames) {
    return;
  }
  if (y < BG_VIEW_Y || y >= (BG_VIEW_Y + BG_VIEW_H)) {
    return;
  }

  const QspiWeaponFrameMeta *meta = &qspi_weapon_frame_meta[frame_idx];
  if (meta->w == 0U || meta->h == 0U) {
    return;
  }
  const int draw_y = BG_VIEW_Y + (int)meta->y + WEAPON_VIEW_Y_OFFSET;
  const int src_y = y - draw_y;
  if (src_y < 0 || src_y >= (int)meta->h) {
    return;
  }

  const int draw_x = BG_VIEW_X + (int)meta->x;
  int draw_x0 = draw_x;
  int draw_x1 = draw_x + (int)meta->w;
  if (draw_x0 < BG_VIEW_X) {
    draw_x0 = BG_VIEW_X;
  }
  if (draw_x1 > (BG_VIEW_X + BG_VIEW_W)) {
    draw_x1 = BG_VIEW_X + BG_VIEW_W;
  }
  if (draw_x0 >= draw_x1) {
    return;
  }

#if !W25Q_CACHE_CURRENT_FRAME
  const uint32_t row_offset = meta->data_offset + (uint32_t)src_y * (uint32_t)meta->w;
#endif
  for (int x = draw_x0; x < draw_x1; x++) {
    const int src_x = x - draw_x;
#if W25Q_CACHE_CURRENT_FRAME || W25Q_CACHE_KUWU_FRAME
    uint8_t idx;
#if W25Q_CACHE_CURRENT_FRAME && W25Q_CACHE_KUWU_FRAME
    if (frame_idx == (int)qspi_weapon_asset.kuwu_frame && qspi_weapon_kuwu_cache_ready) {
      idx = qspi_weapon_kuwu_cache[(uint32_t)src_y * (uint32_t)meta->w + (uint32_t)src_x];
    } else if (qspi_weapon_frame_cache_ready && frame_idx == qspi_weapon_frame_cache_idx) {
#else
    if (qspi_weapon_frame_cache_ready && frame_idx == qspi_weapon_frame_cache_idx) {
#endif
      idx = qspi_weapon_frame_cache[(uint32_t)src_y * (uint32_t)meta->w + (uint32_t)src_x];
    } else {
#if W25Q_CACHE_CURRENT_FRAME
      return;
#else
      const volatile uint8_t *src = qspi_asset_mm + row_offset;
      idx = src[src_x];
#endif
    }
#else
    uint8_t idx;
    const volatile uint8_t *src = qspi_asset_mm + row_offset;
    idx = src[src_x];
#endif
    if (idx != 0U) {
#if W25Q_KNIFE_SOLID_DEBUG
      dst[x] = 0xFFFFU;
#else
      dst[x] = qspi_weapon_lut_swapped[idx];
#endif
    }
  }
}

#if W25Q_CACHE_CURRENT_FRAME
static void qspi_weapon_cache_frame(int frame_idx)
{
  if (!qspi_weapon_ready || frame_idx < 0 || frame_idx >= (int)qspi_weapon_asset.stored_frames) {
    qspi_weapon_frame_cache_ready = 0U;
    qspi_weapon_frame_cache_idx = frame_idx;
    return;
  }
  const QspiWeaponFrameMeta *meta = &qspi_weapon_frame_meta[frame_idx];
  const uint32_t frame_size = (uint32_t)meta->w * (uint32_t)meta->h;
  if (frame_size > W25Q_CACHE_MAX_FRAME_BYTES) {
    qspi_weapon_frame_cache_ready = 0U;
    qspi_weapon_frame_cache_idx = frame_idx;
    return;
  }
#if W25Q_CACHE_CURRENT_FRAME && W25Q_CACHE_KUWU_FRAME
  if (frame_idx == (int)qspi_weapon_asset.kuwu_frame && qspi_weapon_kuwu_cache_ready) {
    return;
  }
#endif
  if (qspi_weapon_frame_cache_ready && qspi_weapon_frame_cache_idx == frame_idx) {
    return;
  }

  uint32_t addr = W25Q_ASSET_BASE + meta->data_offset;
  uint32_t remain = frame_size;
  uint32_t pos = 0U;
  int read_ok = 1;
  while (remain > 0U) {
    uint32_t chunk = (remain > 256U) ? 256U : remain;
    if (!qspi_fast_read_data(addr, &qspi_weapon_frame_cache[pos], chunk)) {
      read_ok = 0;
      break;
    }
    addr += chunk;
    pos += chunk;
    remain -= chunk;
  }
  if (!read_ok) {
    qspi_weapon_frame_cache_ready = 0U;
    qspi_weapon_frame_cache_idx = frame_idx;
    if (!qspi_weapon_cache_fail_reported) {
      qspi_weapon_cache_fail_reported = 1U;
      printf("[ASSET] frame cache indirect read failed frame=%d SR=0x%08lX CR=0x%08lX\n",
             frame_idx,
             (unsigned long)QUADSPI->SR,
             (unsigned long)QUADSPI->CR);
    }
    return;
  }
  qspi_weapon_frame_cache_idx = frame_idx;
  qspi_weapon_frame_cache_ready = 1U;
  if (!qspi_weapon_cache_ok_reported) {
    qspi_weapon_cache_ok_reported = 1U;
    uint32_t nonzero = 0U;
    int min_x = (int)qspi_weapon_asset.width;
    int min_y = (int)qspi_weapon_asset.height;
    int max_x = -1;
    int max_y = -1;
    for (uint32_t y = 0; y < meta->h; y++) {
      for (uint32_t x = 0; x < meta->w; x++) {
        if (qspi_weapon_frame_cache[y * meta->w + x] != 0U) {
          nonzero++;
          if ((int)x < min_x) min_x = (int)x;
          if ((int)y < min_y) min_y = (int)y;
          if ((int)x > max_x) max_x = (int)x;
          if ((int)y > max_y) max_y = (int)y;
        }
      }
    }
    printf("[ASSET] frame cache indirect read OK frame=%d nonzero=%lu bounds=(%d,%d)-(%d,%d)\n",
           frame_idx,
           (unsigned long)nonzero,
           min_x,
           min_y,
           max_x,
           max_y);
    if (frame_idx == 0) {
      uint32_t frame_crc = crc32_update(0xFFFFFFFFUL, qspi_weapon_frame_cache, frame_size) ^ 0xFFFFFFFFUL;
      printf("[ASSET] frame0 bbox crc=0x%08lX bytes=%lu\n",
             (unsigned long)frame_crc,
             (unsigned long)frame_size);
    }
  }
}
#endif

static int weapon_y_in_view(int weapon_h, int offset_y, int dy)
{
  const int view_top = BG_VIEW_Y;
  const int view_bottom = BG_VIEW_Y + BG_VIEW_H;
  int y = view_bottom - weapon_h + offset_y + dy;
  if (weapon_h >= BG_VIEW_H) {
    return view_top;
  }
  return clamp_i(y, view_top, view_bottom - weapon_h);
}

static void overlay_weapon_line(uint16_t *dst, int y, const WeaponRenderState *weapon)
{
#if W25Q_DISABLE_WEAPON_OVERLAY_TEST
  (void)dst;
  (void)y;
  (void)weapon;
  return;
#endif

  if (weapon->type == WEAPON_RENDER_GUN_IMAGE) {
    overlay_gun_image_line(dst, y, weapon->gun_image_idx, weapon->dx, weapon->dy);
  } else
  if (weapon->type == WEAPON_RENDER_QSPI_KNIFE) {
    if (weapon->frame_idx == (int)qspi_weapon_asset.kuwu_frame) {
      overlay_kuwu_internal_line(dst, y);
      return;
    }
    overlay_qspi_weapon_line(dst, y, weapon->frame_idx, 0, 0);
  }
}

static int weapon_render_y_bounds(const WeaponRenderState *weapon, int *y0, int *y1)
{
  if (!weapon) {
    return 0;
  }
  if (weapon->type == WEAPON_RENDER_GUN_IMAGE
      && weapon->gun_image_idx >= 0
      && weapon->gun_image_idx < (int)GUN_IMAGE_COUNT) {
    const int image_idx = weapon->gun_image_idx;
    const int gun_y = BG_VIEW_Y + (int)gun_image_y[image_idx] + weapon->dy + WEAPON_VIEW_Y_OFFSET;
    *y0 = clamp_i(gun_y, BG_VIEW_Y, BG_VIEW_Y + BG_VIEW_H);
    *y1 = clamp_i(gun_y + (int)gun_image_heights[image_idx], BG_VIEW_Y, BG_VIEW_Y + BG_VIEW_H);
    return (*y0 < *y1) ? 1 : 0;
  }
  if (weapon->type == WEAPON_RENDER_QSPI_KNIFE
      && qspi_weapon_ready
      && weapon->frame_idx >= 0
      && weapon->frame_idx < (int)qspi_weapon_asset.stored_frames) {
    if (weapon->frame_idx == (int)qspi_weapon_asset.kuwu_frame) {
      const int weapon_y = BG_VIEW_Y + (int)kuwu_canvas_y + WEAPON_VIEW_Y_OFFSET;
      *y0 = clamp_i(weapon_y, BG_VIEW_Y, BG_VIEW_Y + BG_VIEW_H);
      *y1 = clamp_i(weapon_y + (int)kuwu_height, BG_VIEW_Y, BG_VIEW_Y + BG_VIEW_H);
      return (*y0 < *y1) ? 1 : 0;
    }
    const QspiWeaponFrameMeta *meta = &qspi_weapon_frame_meta[weapon->frame_idx];
    if (meta->w == 0U || meta->h == 0U) {
      return 0;
    }
    const int weapon_y = BG_VIEW_Y + (int)meta->y + WEAPON_VIEW_Y_OFFSET;
    *y0 = clamp_i(weapon_y, BG_VIEW_Y, BG_VIEW_Y + BG_VIEW_H);
    *y1 = clamp_i(weapon_y + (int)meta->h, BG_VIEW_Y, BG_VIEW_Y + BG_VIEW_H);
    return (*y0 < *y1) ? 1 : 0;
  }
  return 0;
}

typedef struct {
  int cur_x;
  int cur_y;
  int old_x;
  int old_y;
  int new_x;
  int new_y;
  uint8_t transitioning;
  uint32_t transition_start_ms;
  uint32_t last_step_ms;
} BackgroundNavState;

typedef struct {
  int old_x;
  int old_y;
  int new_x;
  int new_y;
  int move_x;
  int move_y;
  int block_w;
  int block_h;
  uint32_t progress;
  uint8_t same_frame;
} BackgroundRenderParams;

#if ROBOT_TEST_ENABLE
static RobotRenderState robot_make_render_state(const BackgroundNavState *bg,
                                                int armor,
                                                int health,
                                                uint32_t now,
                                                RobotLevel level,
                                                uint32_t level_start_ms,
                                                uint32_t death_start_ms,
                                                int random_offset)
{
  RobotRenderState robot = {0};
  if (!bg || !qspi_bg_ready) {
    return robot;
  }
  const int grid_x = bg->transitioning ? bg->new_x : bg->cur_x;
  const int grid_y = bg->transitioning ? bg->new_y : bg->cur_y;
  int robot_bg_x = ROBOT_WORLD_X - grid_x * ROBOT_GRID_X_STEP + grid_y * ROBOT_GRID_Y_TO_X;
  int robot_bg_y = ROBOT_WORLD_Y - grid_y * ROBOT_GRID_Y_STEP;
  uint8_t frame_idx = 0U;
  uint8_t show_bar = (health > 0) ? 1U : 0U;

  if (death_start_ms != 0U) {
    uint32_t die_step = (now - death_start_ms) / ROBOT_DIE_FRAME_MS;
    if (die_step >= ROBOT_DIE_TIMELINE_FRAMES) {
      return robot;
    }
    if (level == ROBOT_LEVEL_ULTIMATE) {
      robot_ultimate_pose_at(death_start_ms, level_start_ms, &robot_bg_x, &robot_bg_y, &frame_idx);
      robot.dissolve = 1U;
      robot.dissolve_step = (uint8_t)die_step;
    } else {
      uint8_t scale_idx = robot_scale_for_grid_y(grid_y);
      if (level == ROBOT_LEVEL_FUZHU_RANDOM) {
        robot_bg_x += random_offset;
      } else {
        robot_bg_x += robot_patrol_offset_at(death_start_ms);
      }
      frame_idx = robot_die_frame(robot_die_image_for_step(die_step), scale_idx);
    }
    show_bar = 0U;
  } else if (level == ROBOT_LEVEL_ULTIMATE) {
    robot_ultimate_pose_at(now, level_start_ms, &robot_bg_x, &robot_bg_y, &frame_idx);
  } else {
    const uint8_t scale_idx = robot_scale_for_grid_y(grid_y);
    if (level == ROBOT_LEVEL_FUZHU_RANDOM) {
      robot_bg_x += random_offset;
    } else {
      robot_bg_x += robot_patrol_offset_at(now);
    }
    uint8_t pose = (uint8_t)((now / ROBOT_WALK_FRAME_MS) % ROBOT1_POSE_COUNT);
    frame_idx = robot_frame_for_pose_scale(pose, scale_idx);
  }

  const int w = (int)robot1_widths[frame_idx];
  const int h = (int)robot1_heights[frame_idx];
  const int max_src_x = (int)qspi_bg_asset.width - BG_VIEW_W;
  const int max_src_y = (int)qspi_bg_asset.height - BG_VIEW_H;
  const int crop_x = clamp_i(bg_src_x_dynamic, 0, max_src_x);
  const int crop_y = clamp_i(bg_src_y_dynamic, 0, max_src_y);

  robot.visible = 1U;
  robot.show_bar = show_bar;
  robot.frame_idx = frame_idx;
  robot.bg_x = robot_bg_x;
  robot.bg_y = robot_bg_y - (h / 2);
  robot.x = BG_VIEW_X + robot_bg_x - crop_x - (w / 2);
  robot.y = BG_VIEW_Y + robot_bg_y - crop_y - h;
  robot.armor = armor;
  robot.armor_max = robot_armor_max_for_level(level);
  robot.armor_color = robot_armor_color_for_level(level);
  robot.health = health;
  if (robot.x >= (BG_VIEW_X + BG_VIEW_W)
      || (robot.x + w) <= BG_VIEW_X
      || robot.y >= (BG_VIEW_Y + BG_VIEW_H)
      || (robot.y + h) <= BG_VIEW_Y) {
    robot.visible = 0U;
  }
  return robot;
}

static uint8_t robot_crosshair_hit(const RobotRenderState *robot)
{
  if (!robot || !robot->visible || robot->frame_idx >= ROBOT1_FRAME_COUNT) {
    return 0U;
  }
  const int cross_x = ST7789V2_WIDTH / 2;
  const int cross_y = BG_VIEW_Y + (BG_VIEW_H / 2);
  const int w = (int)robot1_widths[robot->frame_idx];
  const int h = (int)robot1_heights[robot->frame_idx];
  if (cross_x < robot->x || cross_x >= (robot->x + w)
      || cross_y < robot->y || cross_y >= (robot->y + h)) {
    return 0U;
  }
  const int src_x = cross_x - robot->x;
  const int src_y = cross_y - robot->y;
  const uint8_t idx = robot1_frames[robot->frame_idx][(uint32_t)src_y * (uint32_t)w + (uint32_t)src_x];
  return (idx != ROBOT1_TRANSPARENT_INDEX) ? 1U : 0U;
}

static uint8_t robot_heping_coverage_percent(const RobotRenderState *robot)
{
  if (!robot || !robot->visible || robot->frame_idx >= ROBOT1_FRAME_COUNT) {
    return 0U;
  }
  const int cx = ST7789V2_WIDTH / 2;
  const int cy = ST7789V2_HEIGHT / 2;
  const int x0 = cx - HEPING_CROSSHAIR_HALF;
  const int x1 = cx + HEPING_CROSSHAIR_HALF;
  const int y0 = cy - HEPING_CROSSHAIR_HALF;
  const int y1 = cy + HEPING_CROSSHAIR_HALF;
  const int w = (int)robot1_widths[robot->frame_idx];
  const int h = (int)robot1_heights[robot->frame_idx];
  const uint8_t *src = robot1_frames[robot->frame_idx];
  uint32_t total = 0U;
  uint32_t covered = 0U;
  for (int sy = 0; sy < h; sy++) {
    int py = robot->y + sy;
    for (int sx = 0; sx < w; sx++) {
      uint8_t idx = src[(uint32_t)sy * (uint32_t)w + (uint32_t)sx];
      if (idx == ROBOT1_TRANSPARENT_INDEX) {
        continue;
      }
      total++;
      int px = robot->x + sx;
      if (px >= x0 && px <= x1 && py >= y0 && py <= y1) {
        covered++;
      }
    }
  }
  if (total == 0U || covered == 0U) {
    return 0U;
  }
  return (uint8_t)((covered * 100U + total / 2U) / total);
}
#endif

static int quantize_i(int v, int step)
{
  if (step <= 1) {
    return v;
  }
  return ((v + step / 2) / step) * step;
}

static uint32_t qspi_bg_id(int x, int y)
{
  const uint32_t cols = (uint32_t)(qspi_bg_asset.x_max - qspi_bg_asset.x_min + 1);
  const uint32_t xi = (uint32_t)(x - qspi_bg_asset.x_min);
  const uint32_t yi = (uint32_t)(y - qspi_bg_asset.y_min);
  return yi * cols + xi;
}

static uint32_t qspi_bg_frame_addr(int x, int y)
{
  return W25Q_BG_BASE
       + qspi_bg_asset.data_offset
       + qspi_bg_id(x, y) * qspi_bg_asset.frame_stride;
}

static const uint16_t *qspi_bg_lut_for_frame(uint32_t frame_id)
{
  if (qspi_bg_asset.format == 1U) {
    return qspi_bg_lut_swapped;
  }
  if (qspi_bg_asset.format != 2U) {
    return qspi_bg_lut_swapped;
  }
  for (uint32_t slot = 0; slot < 2U; slot++) {
    if (qspi_bg_lut_cache_id[slot] == frame_id) {
      return qspi_bg_lut_cache[slot];
    }
  }

  uint8_t slot = qspi_bg_lut_cache_next;
  qspi_bg_lut_cache_next = (uint8_t)((qspi_bg_lut_cache_next + 1U) & 1U);
  uint8_t lut_raw[512];
  uint32_t addr = W25Q_BG_BASE + qspi_bg_asset.lut_offset + frame_id * 512U;
  if (!qspi_read_data(addr, lut_raw, sizeof(lut_raw))) {
    return qspi_bg_lut_swapped;
  }
  for (uint32_t i = 0; i < 256U; i++) {
    uint16_t c = (uint16_t)lut_raw[i * 2U] | ((uint16_t)lut_raw[i * 2U + 1U] << 8);
    qspi_bg_lut_cache[slot][i] = swap16(c);
  }
  qspi_bg_lut_cache_id[slot] = frame_id;
  return qspi_bg_lut_cache[slot];
}

static int qspi_bg_read_line(int x, int y, int src_y, uint16_t *line)
{
  const int max_src_x = (int)qspi_bg_asset.width - BG_VIEW_W;
  const int max_src_y = (int)qspi_bg_asset.height - BG_VIEW_H;
  const int crop_x = clamp_i(bg_src_x_dynamic, 0, max_src_x);
  const int crop_y = clamp_i(bg_src_y_dynamic, 0, max_src_y);
  const uint32_t pixel_offset = ((uint32_t)(crop_y + src_y) * (uint32_t)qspi_bg_asset.width)
                              + (uint32_t)crop_x;
  uint32_t addr = qspi_bg_frame_addr(x, y) + pixel_offset;
  if (qspi_bg_asset.format == 0U) {
    addr = qspi_bg_frame_addr(x, y) + pixel_offset * 2U;
#if W25Q_BG_READ_SINGLE_LINE
    if (!qspi_read_data(addr, (uint8_t *)line, (uint32_t)BG_VIEW_W * 2U)) {
      return 0;
    }
#elif W25Q_BG_FAST_SINGLE_READ
    if (!qspi_fast_read_data(addr, (uint8_t *)line, (uint32_t)BG_VIEW_W * 2U)) {
      return 0;
    }
#else
    if (!qspi_quad_read_data(addr, (uint8_t *)line, (uint32_t)BG_VIEW_W * 2U)) {
#if W25Q_BG_READ_RETRY_ON_FAIL
      qspi_test_init();
      if (!qspi_quad_read_data(addr, (uint8_t *)line, (uint32_t)BG_VIEW_W * 2U)) {
        return 0;
      }
#else
      return 0;
#endif
    }
#endif
#if W25Q_BG_SWAP_AFTER_READ
    for (int i = 0; i < BG_VIEW_W; i++) {
      line[i] = swap16(line[i]);
    }
#endif
    return 1;
  }

  uint8_t idx_line[BG_VIEW_W];
#if W25Q_BG_READ_SINGLE_LINE
  if (!qspi_read_data(addr, idx_line, (uint32_t)BG_VIEW_W)) {
    return 0;
  }
#elif W25Q_BG_FAST_SINGLE_READ
  if (!qspi_fast_read_data(addr, idx_line, (uint32_t)BG_VIEW_W)) {
    return 0;
  }
#else
  if (!qspi_quad_read_data(addr, idx_line, (uint32_t)BG_VIEW_W)) {
#if W25Q_BG_READ_RETRY_ON_FAIL
    qspi_test_init();
    if (!qspi_quad_read_data(addr, idx_line, (uint32_t)BG_VIEW_W)) {
      return 0;
    }
#else
    return 0;
#endif
  }
#endif
  const uint16_t *lut = qspi_bg_lut_for_frame(qspi_bg_id(x, y));
  for (int i = 0; i < BG_VIEW_W; i++) {
    line[i] = lut[idx_line[i]];
  }
  return 1;
}

static uint8_t bg_block_score(int x, int y, int move_x, int move_y, int block_w, int block_h)
{
  const int bx = x / block_w;
  const int by = y / block_h;
  const int max_bx = (BG_VIEW_W - 1) / block_w;
  const int max_by = (BG_VIEW_H - 1) / block_h;
  int sx = 0;
  int sy = 0;

  if (max_bx > 0) {
    sx = (bx * 255) / max_bx;
    if (move_x > 0) {
      sx = 255 - sx;
    }
  }
  if (max_by > 0) {
    sy = (by * 255) / max_by;
    if (move_y > 0) {
      sy = 255 - sy;
    }
  }

  if (move_x != 0 && move_y != 0) {
    return (uint8_t)((sx + sy) / 2);
  }
  if (move_x != 0) {
    return (uint8_t)sx;
  }
  return (uint8_t)sy;
}

static void bg_prepare_render_params(const BackgroundNavState *bg,
                                     uint32_t now,
                                     BackgroundRenderParams *out)
{
  out->old_x = bg->cur_x;
  out->old_y = bg->cur_y;
  out->new_x = bg->cur_x;
  out->new_y = bg->cur_y;
  out->progress = 255U;

  if (bg->transitioning) {
    out->old_x = bg->old_x;
    out->old_y = bg->old_y;
    out->new_x = bg->new_x;
    out->new_y = bg->new_y;
    uint32_t elapsed = now - bg->transition_start_ms;
    out->progress = (elapsed >= BG_TRANSITION_MS) ? 255U : ((elapsed * 255U) / BG_TRANSITION_MS);
  }

  out->move_x = out->new_x - out->old_x;
  out->move_y = out->new_y - out->old_y;
#if W25Q_BG_TRANSITION_BLEND && W25Q_BG_TRANSITION_THREE_PHASE
  if (out->move_x != 0 || out->move_y != 0) {
    if (out->progress < BG_BLEND_START) {
      out->new_x = out->old_x;
      out->new_y = out->old_y;
      out->move_x = 0;
      out->move_y = 0;
      out->same_frame = 1U;
    } else if (out->progress > BG_BLEND_END) {
      out->old_x = out->new_x;
      out->old_y = out->new_y;
      out->move_x = 0;
      out->move_y = 0;
      out->same_frame = 1U;
    } else {
      uint32_t span = BG_BLEND_END - BG_BLEND_START;
      out->progress = (span == 0U) ? 255U : (((out->progress - BG_BLEND_START) * 255U) / span);
    }
  }
#elif !W25Q_BG_TRANSITION_BLEND
  if (out->move_x != 0 || out->move_y != 0) {
    out->old_x = out->new_x;
    out->old_y = out->new_y;
    out->move_x = 0;
    out->move_y = 0;
    out->progress = 255U;
  }
#endif
  out->same_frame = (out->old_x == out->new_x && out->old_y == out->new_y) ? 1U : 0U;
  out->block_w = (abs_i(out->move_x) >= abs_i(out->move_y)) ? 8 : 16;
  out->block_h = (abs_i(out->move_x) >= abs_i(out->move_y)) ? 16 : 8;
}

static int bg_prefetch_stripe(const BackgroundRenderParams *p, int src_y0, int line_count)
{
  if (line_count <= 0 || line_count > BG_STRIPE_LINES) {
    return 0;
  }

  for (int i = 0; i < line_count; i++) {
    int src_y = src_y0 + i;
    if (!qspi_bg_read_line(p->old_x, p->old_y, src_y, bg_old_stripe[i])) {
      return 0;
    }
    if (!p->same_frame) {
      if (!qspi_bg_read_line(p->new_x, p->new_y, src_y, bg_new_stripe[i])) {
        return 0;
      }
    }
  }
  return 1;
}

static void compose_w25q_bg_line_from_stripe(uint16_t *dst,
                                             int lcd_y,
                                             const BackgroundRenderParams *p,
                                             int stripe_src_y0,
                                             int stripe_line_count)
{
  for (int x = 0; x < ST7789V2_WIDTH; x++) {
    dst[x] = 0x0000U;
  }

  if (!qspi_bg_ready || lcd_y < BG_VIEW_Y || lcd_y >= (BG_VIEW_Y + BG_VIEW_H)) {
    return;
  }

  const int src_y = lcd_y - BG_VIEW_Y;
#if W25Q_BG_FULL_FRAME_CACHE_TEST
  if (qspi_bg_full_cache_ready) {
    for (int x = 0; x < BG_VIEW_W; x++) {
      dst[BG_VIEW_X + x] = qspi_bg_full_cache[src_y][x];
    }
    (void)p;
    (void)stripe_src_y0;
    (void)stripe_line_count;
    return;
  }
#endif
  const int stripe_i = src_y - stripe_src_y0;
  if (stripe_i < 0 || stripe_i >= stripe_line_count) {
    return;
  }

  if (p->same_frame) {
    for (int x = 0; x < BG_VIEW_W; x++) {
      dst[BG_VIEW_X + x] = bg_old_stripe[stripe_i][x];
    }
    return;
  }

  for (int x = 0; x < BG_VIEW_W; x++) {
    uint8_t score = bg_block_score(x, src_y, p->move_x, p->move_y, p->block_w, p->block_h);
    dst[BG_VIEW_X + x] = (score <= p->progress) ? bg_new_stripe[stripe_i][x] : bg_old_stripe[stripe_i][x];
  }
}

static void compose_w25q_bg_line(uint16_t *dst, int lcd_y, const BackgroundNavState *bg, uint32_t now)
{
  for (int x = 0; x < ST7789V2_WIDTH; x++) {
    dst[x] = 0x0000U;
  }

  if (!qspi_bg_ready || lcd_y < BG_VIEW_Y || lcd_y >= (BG_VIEW_Y + BG_VIEW_H)) {
    return;
  }

  const int src_y = lcd_y - BG_VIEW_Y;
#if W25Q_BG_FULL_FRAME_CACHE_TEST
  if (qspi_bg_full_cache_ready) {
    for (int x = 0; x < BG_VIEW_W; x++) {
      dst[BG_VIEW_X + x] = qspi_bg_full_cache[src_y][x];
    }
    (void)bg;
    (void)now;
    return;
  }
#endif
#if W25Q_BG_MAIN_INTERNAL_REF_TEST
  const int ref_y = (src_y * AD_BG_DEBUG_REF_H) / BG_VIEW_H;
  const uint16_t *ref = &ad_bg_debug_ref_rgb565[ref_y * AD_BG_DEBUG_REF_W];
  for (int x = 0; x < BG_VIEW_W; x++) {
    const int ref_x = (x * AD_BG_DEBUG_REF_W) / BG_VIEW_W;
    dst[BG_VIEW_X + x] = ref[ref_x];
  }
  (void)bg;
  (void)now;
  return;
#else
  int old_x = bg->cur_x;
  int old_y = bg->cur_y;
  int new_x = bg->cur_x;
  int new_y = bg->cur_y;
  uint32_t progress = 255U;
  if (bg->transitioning) {
    old_x = bg->old_x;
    old_y = bg->old_y;
    new_x = bg->new_x;
    new_y = bg->new_y;
    uint32_t elapsed = now - bg->transition_start_ms;
    progress = (elapsed >= BG_TRANSITION_MS) ? 255U : ((elapsed * 255U) / BG_TRANSITION_MS);
  }

  if (!qspi_bg_read_line(old_x, old_y, src_y, bg_old_line)) {
    return;
  }
  if (old_x == new_x && old_y == new_y) {
    for (int x = 0; x < BG_VIEW_W; x++) {
      dst[BG_VIEW_X + x] = bg_old_line[x];
    }
    return;
  }
  if (!qspi_bg_read_line(new_x, new_y, src_y, bg_new_line)) {
    for (int x = 0; x < BG_VIEW_W; x++) {
      dst[BG_VIEW_X + x] = bg_old_line[x];
    }
    return;
  }

  const int move_x = new_x - old_x;
  const int move_y = new_y - old_y;
  const int block_w = (abs_i(move_x) >= abs_i(move_y)) ? 8 : 16;
  const int block_h = (abs_i(move_x) >= abs_i(move_y)) ? 16 : 8;
  for (int x = 0; x < BG_VIEW_W; x++) {
    uint8_t score = bg_block_score(x, src_y, move_x, move_y, block_w, block_h);
    dst[BG_VIEW_X + x] = (score <= progress) ? bg_new_line[x] : bg_old_line[x];
  }
#endif
}

static void overlay_bg_debug_bars_line(uint16_t *dst, int y)
{
#if W25Q_BG_COLOR_DEBUG_BARS
  if (y < 6 || y >= 14) {
    return;
  }
  for (int x = 4; x < 28; x++) {
    dst[x] = swap16(0xF800U);
  }
  for (int x = 32; x < 56; x++) {
    dst[x] = swap16(0x07E0U);
  }
  for (int x = 60; x < 84; x++) {
    dst[x] = swap16(0x001FU);
  }
  for (int x = 88; x < 112; x++) {
    dst[x] = swap16(0xFFFFU);
  }
#else
  (void)dst;
  (void)y;
#endif
}

static void overlay_bg_internal_ref_line(uint16_t *dst, int y)
{
#if W25Q_BG_INTERNAL_REF_OVERLAY
  const int ref_x = 4;
  const int ref_y = ST7789V2_HEIGHT - AD_BG_DEBUG_REF_H - 4;
  const int src_y = y - ref_y;
  if (src_y < 0 || src_y >= AD_BG_DEBUG_REF_H) {
    return;
  }
  const uint16_t *src = &ad_bg_debug_ref_rgb565[src_y * AD_BG_DEBUG_REF_W];
  for (int x = 0; x < AD_BG_DEBUG_REF_W; x++) {
    dst[ref_x + x] = src[x];
  }
#else
  (void)dst;
  (void)y;
#endif
}

static void overlay_bg_qspi_ref_line(uint16_t *dst, int y)
{
#if W25Q_BG_QSPI_REF_OVERLAY
  if (!qspi_bg_ref_cache_ready) {
    return;
  }
  const int ref_x = 112;
  const int ref_y = ST7789V2_HEIGHT - AD_BG_DEBUG_REF_H - 4;
  const int src_y_small = y - ref_y;
  if (src_y_small < 0 || src_y_small >= AD_BG_DEBUG_REF_H) {
    return;
  }

  const uint16_t *src = &qspi_bg_ref_cache[src_y_small * AD_BG_DEBUG_REF_W];
  for (int x = 0; x < AD_BG_DEBUG_REF_W; x++) {
    dst[ref_x + x] = src[x];
  }
#else
  (void)dst;
  (void)y;
#endif
}

static void lcd_wait_dma_spi_idle(void)
{
  while (cfg0.dma.channel->CNDTR != 0U) {
  }
  while (cfg0.spi->SR & SPI_SR_BSY) {
  }
  cfg0.dma.channel->CCR &= ~DMA_CCR_EN;
}

#if W25Q_BG_BLOCKING_LCD_TEST
static void lcd_send_data_block_cpu(uint8_t *data, uint32_t length)
{
  lcd_wait_dma_spi_idle();

  gpio_write(cfg0.DC, 1);

  cfg0.spi->CR1 &= ~SPI_CR1_SPE;
  cfg0.spi->CR2 &= ~(SPI_CR2_DS_Msk | SPI_CR2_TXDMAEN);
  cfg0.spi->CR2 |= SPI_CR2_DS_0 | SPI_CR2_DS_1 | SPI_CR2_DS_2;
  cfg0.spi->CR1 |= SPI_CR1_SPE;

  gpio_write(cfg0.CS, 0);
  for (uint32_t i = 0; i < length; i++) {
    while ((cfg0.spi->SR & SPI_SR_TXE) == 0U) {
    }
    *((__IO uint8_t *)&cfg0.spi->DR) = data[i];
  }
  while (cfg0.spi->SR & SPI_SR_BSY) {
  }
  gpio_write(cfg0.CS, 1);
}
#endif

static void compose_w25q_bg_screen_line(uint16_t *dst,
                                        int y,
                                        const BackgroundNavState *bg,
                                        const WeaponRenderState *weapon,
                                        const RobotRenderState *robot,
                                        uint32_t now)
{
  compose_w25q_bg_line(dst, y, bg, now);
  overlay_bg_debug_bars_line(dst, y);
  overlay_bg_internal_ref_line(dst, y);
  overlay_bg_qspi_ref_line(dst, y);
  overlay_stage_stats_line(dst, y);
#if ROBOT_TEST_ENABLE
  overlay_ultimate_trail_line(dst, y);
  overlay_robot_line(dst, y, robot);
  overlay_hit_particles_line(dst, y);
#else
  (void)robot;
#endif
  overlay_bloodhound_line(dst, y);
  overlay_crosshair_line(dst, y);
  overlay_weapon_line(dst, y, weapon);
  overlay_ammo_hud_line(dst, y);
}

static int bg_ensure_stripe_for_lcd_y(const BackgroundRenderParams *bgp,
                                      int y,
                                      int *stripe_src_y0,
                                      int *stripe_line_count)
{
#if W25Q_BG_FULL_FRAME_CACHE_TEST
  if (qspi_bg_full_cache_ready) {
    (void)bgp;
    (void)y;
    (void)stripe_src_y0;
    (void)stripe_line_count;
    return 1;
  }
#endif

  if (!qspi_bg_ready || y < BG_VIEW_Y || y >= (BG_VIEW_Y + BG_VIEW_H)) {
    return 1;
  }

  const int src_y = y - BG_VIEW_Y;
  if (src_y >= *stripe_src_y0 && src_y < (*stripe_src_y0 + *stripe_line_count)) {
    return 1;
  }

  *stripe_src_y0 = src_y;
  *stripe_line_count = BG_VIEW_H - *stripe_src_y0;
  if (*stripe_line_count > BG_STRIPE_LINES) {
    *stripe_line_count = BG_STRIPE_LINES;
  }

  // Keep QSPI reads out of the LCD DMA window for the most conservative bring-up path.
  lcd_wait_dma_spi_idle();
#if W25Q_BG_REINIT_BEFORE_STRIPE_READ
  qspi_test_init();
#endif
  if (!bg_prefetch_stripe(bgp, *stripe_src_y0, *stripe_line_count)) {
    *stripe_src_y0 = -1;
    *stripe_line_count = 0;
    return 0;
  }
  return 1;
}

static void compose_w25q_bg_screen_line_from_stripe(uint16_t *dst,
                                                    int y,
                                                    const BackgroundRenderParams *bgp,
                                                    int stripe_src_y0,
                                                    int stripe_line_count,
                                                    const WeaponRenderState *weapon,
                                                    const RobotRenderState *robot)
{
  compose_w25q_bg_line_from_stripe(dst, y, bgp, stripe_src_y0, stripe_line_count);
  overlay_bg_debug_bars_line(dst, y);
  overlay_bg_internal_ref_line(dst, y);
  overlay_bg_qspi_ref_line(dst, y);
  overlay_stage_stats_line(dst, y);
#if ROBOT_TEST_ENABLE
  overlay_ultimate_trail_line(dst, y);
  overlay_robot_line(dst, y, robot);
  overlay_hit_particles_line(dst, y);
#else
  (void)robot;
#endif
  overlay_bloodhound_line(dst, y);
  overlay_crosshair_line(dst, y);
  overlay_weapon_line(dst, y, weapon);
  overlay_ammo_hud_line(dst, y);
}

static void lcd_clear_line_range_black(int y0, int y1)
{
  if (y0 < 0) {
    y0 = 0;
  }
  if (y1 > ST7789V2_HEIGHT) {
    y1 = ST7789V2_HEIGHT;
  }
  if (y0 >= y1) {
    return;
  }

  for (int x = 0; x < ST7789V2_WIDTH; x++) {
    line_buffer0[x] = 0x0000U;
  }

  for (int y = y0; y < y1; y++) {
    uint16_t lcd_y = (uint16_t)(y + LCD_Y_OFFSET);
    lcd_wait_dma_spi_idle();
    ST7789V2_Set_Address_Window(&cfg0, 0, lcd_y, 239, lcd_y);
    ST7789V2_Send_Command(&cfg0, ST7789_RAMWR);
#if W25Q_BG_BLOCKING_LCD_TEST
    lcd_send_data_block_cpu((uint8_t*)line_buffer0, ST7789V2_WIDTH * 2);
#else
    ST7789V2_Send_Data_Block(&cfg0, (uint8_t*)line_buffer0, ST7789V2_WIDTH * 2);
#endif
  }
  lcd_wait_dma_spi_idle();
}

static void lcd_clear_non_view_boot_frames(void)
{
  static uint8_t clear_count = 0U;
  static int top_maintain_y = 0;
  static int bottom_maintain_y = BG_VIEW_Y + BG_VIEW_H;
  if (clear_count < 8U) {
    lcd_clear_line_range_black(0, BG_VIEW_Y);
    lcd_clear_line_range_black(BG_VIEW_Y + BG_VIEW_H, ST7789V2_HEIGHT);
    clear_count++;
    return;
  }

  int y0 = top_maintain_y;
  int y1 = y0 + LCD_BLACK_MAINTAIN_LINES;
  if (y1 > BG_VIEW_Y) {
    y1 = BG_VIEW_Y;
  }
  lcd_clear_line_range_black(y0, y1);
  top_maintain_y = y1;
  if (top_maintain_y >= BG_VIEW_Y) {
    top_maintain_y = 0;
  }

  int by0 = bottom_maintain_y;
  int by1 = by0 + LCD_BLACK_MAINTAIN_LINES;
  if (by1 > ST7789V2_HEIGHT) {
    by1 = ST7789V2_HEIGHT;
  }
  lcd_clear_line_range_black(by0, by1);
  bottom_maintain_y = by1;
  if (bottom_maintain_y >= ST7789V2_HEIGHT) {
    bottom_maintain_y = BG_VIEW_Y + BG_VIEW_H;
  }
}

static void lcd_render_stage_stats_if_needed(void)
{
  static uint16_t last_shots = 0xFFFFU;
  static uint16_t last_hits = 0xFFFFU;
  static int8_t last_result = 127;
  static uint8_t last_dialog_visible = 0xFFU;
  static const char *last_line0 = 0;
  static const char *last_line1 = 0;
  static uint8_t last_menu_visible = 0xFFU;
  static const char *last_menu_a = 0;
  static const char *last_menu_b = 0;
  static const char *last_menu_c = 0;
  static uint8_t last_menu_selected = 0xFFU;
  if (render_dialog_visible) {
    return;
  }

  if (last_shots == render_stage_shots
      && last_hits == render_stage_hits
      && last_result == render_stage_result
      && last_dialog_visible == render_dialog_visible
      && last_line0 == render_dialog_line0
      && last_line1 == render_dialog_line1
      && last_menu_visible == render_menu_visible
      && last_menu_a == render_menu_a
      && last_menu_b == render_menu_b
      && last_menu_c == render_menu_c
      && last_menu_selected == render_menu_selected
      && !render_dialog_visible) {
    return;
  }

  for (int y = 0; y < BG_VIEW_Y; y++) {
    for (int x = 0; x < ST7789V2_WIDTH; x++) {
      line_buffer0[x] = 0x0000U;
    }
    overlay_stage_stats_line(line_buffer0, y);
    uint16_t lcd_y = (uint16_t)(y + LCD_Y_OFFSET);
    lcd_wait_dma_spi_idle();
    ST7789V2_Set_Address_Window(&cfg0, 0, lcd_y, 239, lcd_y);
    ST7789V2_Send_Command(&cfg0, ST7789_RAMWR);
#if W25Q_BG_BLOCKING_LCD_TEST
    lcd_send_data_block_cpu((uint8_t*)line_buffer0, ST7789V2_WIDTH * 2);
#else
    ST7789V2_Send_Data_Block(&cfg0, (uint8_t*)line_buffer0, ST7789V2_WIDTH * 2);
#endif
  }
  lcd_wait_dma_spi_idle();
  last_shots = render_stage_shots;
  last_hits = render_stage_hits;
  last_result = render_stage_result;
  last_dialog_visible = render_dialog_visible;
  last_line0 = render_dialog_line0;
  last_line1 = render_dialog_line1;
  last_menu_visible = render_menu_visible;
  last_menu_a = render_menu_a;
  last_menu_b = render_menu_b;
  last_menu_c = render_menu_c;
  last_menu_selected = render_menu_selected;
}

static void render_w25q_background_frame(const BackgroundNavState *bg,
                                         const WeaponRenderState *weapon,
                                         const RobotRenderState *robot)
{
  static WeaponRenderState last_weapon = {0};
  static uint8_t last_weapon_valid = 0U;
  static uint8_t last_hud_visible = 0U;
  uint16_t *tx_buf = line_buffer0;
  uint16_t *prep_buf = line_buffer1;
  uint32_t now = HAL_GetTick();
  BackgroundRenderParams bgp;
  bg_prepare_render_params(bg, now, &bgp);
  int stripe_src_y0 = -1;
  int stripe_line_count = 0;
  int render_y0 = 0;
  int render_y1 = BG_VIEW_Y + BG_VIEW_H;
  int weapon_y0 = 0;
  int weapon_y1 = 0;
  uint8_t weapon_changed = 0U;

  if (weapon_render_y_bounds(weapon, &weapon_y0, &weapon_y1)) {
    if (!last_weapon_valid
        || last_weapon.type != weapon->type
        || last_weapon.dx != weapon->dx
        || last_weapon.dy != weapon->dy
        || last_weapon.frame_idx != weapon->frame_idx) {
      weapon_changed = 1U;
    }
    if (weapon_changed) {
      if (weapon_y0 < render_y0) {
        render_y0 = weapon_y0;
      }
      if (weapon_y1 > render_y1) {
        render_y1 = weapon_y1;
      }
    }
  }
  if (render_hud_visible || last_hud_visible) {
    render_y1 = ST7789V2_HEIGHT;
  }
  render_y0 = clamp_i(render_y0, 0, ST7789V2_HEIGHT);
  render_y1 = clamp_i(render_y1, 0, ST7789V2_HEIGHT);
  if (render_y0 >= render_y1) {
    return;
  }

#if W25Q_BG_REINIT_BEFORE_FRAME
  lcd_wait_dma_spi_idle();
  qspi_test_init();
#endif
  lcd_clear_non_view_boot_frames();

  bg_ensure_stripe_for_lcd_y(&bgp, render_y0, &stripe_src_y0, &stripe_line_count);
  compose_w25q_bg_screen_line_from_stripe(tx_buf, render_y0, &bgp, stripe_src_y0, stripe_line_count, weapon, robot);

  for (int y = render_y0; y < render_y1; y++) {
    uint16_t lcd_y = (uint16_t)(y + LCD_Y_OFFSET);
    lcd_wait_dma_spi_idle();
    ST7789V2_Set_Address_Window(&cfg0, 0, lcd_y, 239, lcd_y);
    ST7789V2_Send_Command(&cfg0, ST7789_RAMWR);
#if W25Q_BG_BLOCKING_LCD_TEST
    lcd_send_data_block_cpu((uint8_t*)tx_buf, ST7789V2_WIDTH * 2);
#else
    ST7789V2_Send_Data_Block(&cfg0, (uint8_t*)tx_buf, ST7789V2_WIDTH * 2);
#endif

    if ((y + 1) < render_y1) {
      bg_ensure_stripe_for_lcd_y(&bgp, y + 1, &stripe_src_y0, &stripe_line_count);
      compose_w25q_bg_screen_line_from_stripe(prep_buf, y + 1, &bgp, stripe_src_y0, stripe_line_count, weapon, robot);

      uint16_t *tmp = tx_buf;
      tx_buf = prep_buf;
      prep_buf = tmp;
    }
  }

  lcd_wait_dma_spi_idle();
  last_weapon = *weapon;
  last_weapon_valid = 1U;
  last_hud_visible = render_hud_visible;
}

static void __attribute__((unused)) render_phase1_frame(int yaw,
                                                        int y_offset,
                                                        int strafe,
                                                        int move_fb,
                                                        const WeaponRenderState *weapon)
{
  const int left_src_w = (int)leftwall_idx8_width;
  const int center_src_w = (int)center_idx8_width;
  const int right_src_w = (int)rightwall_idx8_width;

    int center_w_base = CENTER_W + (move_fb * FB_CENTER_DELTA) / FB_RANGE;
    center_w_base = clamp_i(center_w_base, 96, ST7789V2_WIDTH - 2 * SIDE_MIN_W);
    if (center_w_base > center_src_w) {
      center_w_base = center_src_w;
    }

    const int side_total = ST7789V2_WIDTH - center_w_base;
    const int half = side_total / 2;
    const int d = sync_eased_delta(yaw, MAX_LAYOUT_DELTA);
    const int left_w = clamp_i(half + d, SIDE_MIN_W, side_total - SIDE_MIN_W);
    const int right_w = side_total - left_w;

    const int x_left = 0;
    const int x_center_base = (ST7789V2_WIDTH - center_w_base) / 2;
    const int center_shift = sync_eased_delta(yaw, CENTER_DRAW_MAX);
    int x_center = clamp_i(x_center_base + center_shift, 0, ST7789V2_WIDTH - center_w_base);
    // Keep center/side boundary synchronized to avoid exposing black seams at yaw extremes.
    x_center = clamp_i(x_center, left_w - SEAM_OVERLAP, left_w + SEAM_OVERLAP);
    const int x_right = ST7789V2_WIDTH - right_w;

    int left_x0 = left_src_w - left_w;
    int right_x0 = 0;

    int center_x0 = (center_src_w - center_w_base) / 2;
    int center_w = center_w_base;
    int center_draw_shift = 0;
    if (SEAM_OVERLAP > 0) {
      int left_extra = (center_x0 < SEAM_OVERLAP) ? center_x0 : SEAM_OVERLAP;
      int right_room = center_src_w - (center_x0 + center_w);
      int right_extra = (right_room < SEAM_OVERLAP) ? right_room : SEAM_OVERLAP;
      center_x0 -= left_extra;
      center_w += (left_extra + right_extra);
      center_draw_shift = left_extra;
    }
    const int x_center_draw = clamp_i(x_center - center_draw_shift, 0, ST7789V2_WIDTH - center_w);

    // Strafe is a source-window shift effect (no resize), with mild per-layer parallax.
    const int left_shift = (strafe * STRAFE_LEFT_GAIN_NUM) / STRAFE_GAIN_DEN;
    const int center_shift_x0 = (strafe * STRAFE_CENTER_GAIN_NUM) / STRAFE_GAIN_DEN;
    const int right_shift = (strafe * STRAFE_RIGHT_GAIN_NUM) / STRAFE_GAIN_DEN;

    left_x0 = clamp_i(left_x0 + left_shift, 0, left_src_w - left_w);
    center_x0 = clamp_i(center_x0 + center_shift_x0, 0, center_src_w - center_w);
    right_x0 = clamp_i(right_x0 + right_shift, 0, right_src_w - right_w);

    const int depth_shift = (move_fb * FB_DEPTH_SHIFT) / FB_RANGE;
    const int y0 = clamp_i(y_offset + depth_shift, 0, LAYER_Y_SHIFT_MAX);

    uint16_t *tx_buf = line_buffer0;
    uint16_t *prep_buf = line_buffer1;

    compose_scene_line(tx_buf, y0,
                       left_src_w, center_src_w, right_src_w,
                       x_left, left_x0, left_w,
                       x_center_draw, center_x0, center_w,
                       x_right, right_x0, right_w);
    overlay_crosshair_line(tx_buf, 0);
    overlay_weapon_line(tx_buf, 0, weapon);

    for (int y = 0; y < ST7789V2_HEIGHT; y++) {
      uint16_t lcd_y = (uint16_t)(y + LCD_Y_OFFSET);
      ST7789V2_Set_Address_Window(&cfg0, 0, lcd_y, 239, lcd_y);
      ST7789V2_Send_Command(&cfg0, ST7789_RAMWR);
      ST7789V2_Send_Data_Block(&cfg0, (uint8_t*)tx_buf, ST7789V2_WIDTH * 2);

      if ((y + 1) < ST7789V2_HEIGHT) {
        int next_src_y = y0 + y + 1;
        compose_scene_line(prep_buf, next_src_y,
                           left_src_w, center_src_w, right_src_w,
                           x_left, left_x0, left_w,
                           x_center_draw, center_x0, center_w,
                           x_right, right_x0, right_w);
        overlay_crosshair_line(prep_buf, y + 1);
        overlay_weapon_line(prep_buf, y + 1, weapon);

        uint16_t *tmp = tx_buf;
        tx_buf = prep_buf;
        prep_buf = tmp;
      }
    }

    while (cfg0.dma.channel->CNDTR != 0U) {
    }
    while (cfg0.spi->SR & SPI_SR_BSY) {
  }
}

void Game1_PrepareAssets(void)
{
    init_swapped_luts();

#if QSPI_TEST_MODE
    printf("\n=== W25Q128B QSPI JEDEC ID Test ===\n");
    printf("Wiring: CLK=PB10 CS=PB11 IO0=PB1 IO1=PB0 IO2=PA7 IO3=PA6\n");
    printf("LCD moved: BL=PC7 DC=PA9\n");
    qspi_test_init();
    qspi_run_rw_test();
    while (1)
    {
      uint8_t id[3] = {0};
      if (qspi_read_jedec_id(id)) {
        printf("[QSPI] JEDEC ID: %02X %02X %02X", id[0], id[1], id[2]);
        if (id[0] == 0xEF && id[1] == 0x40 && id[2] == 0x18) {
          printf("  W25Q128 detected");
        }
        printf("\n");
      } else {
        printf("[QSPI] JEDEC ID read FAILED, SR=0x%08lX CR=0x%08lX DCR=0x%08lX\n",
               (unsigned long)QUADSPI->SR,
               (unsigned long)QUADSPI->CR,
               (unsigned long)QUADSPI->DCR);
      }
      HAL_Delay(1000);
    }
#endif

#if WEAPON_LAYER_MODE == WEAPON_MODE_KNIFE && !W25Q_DISABLE_WEAPON_OVERLAY_TEST
    printf("\n=== W25Q qiedao asset mode ===\n");
    printf("QSPI: 10MHz indirect-read frame cache, asset base=0x%06lX\n", (unsigned long)W25Q_ASSET_BASE);
    if (!qspi_weapon_prepare()) {
      printf("[ASSET] prepare failed, halted.\n");
      while (1) {
        HAL_Delay(1000);
      }
    }
#endif

#if W25Q_BG_ENABLE
    printf("\n=== W25Q ad_background asset mode ===\n");
    printf("QSPI: 10MHz RGB565 line-read background, asset base=0x%06lX\n", (unsigned long)W25Q_BG_BASE);
    if (!qspi_bg_prepare()) {
      printf("[BG] prepare failed, halted.\n");
      while (1) {
        HAL_Delay(1000);
      }
    }
#endif
}

#if !PS2_TEST_MODE
MenuState Game1_Run(void)
{
    ps2_rx_head = 0U;
    ps2_rx_tail = 0U;
    ps2_mouse_buttons = 0U;
    mouse_look_initialized = 0U;
    apex_ws2812_init();

#if AIM_INPUT_MODE == AIM_INPUT_JOYSTICK
  int yaw_target = 0;
  int pitch_target = LAYER_Y_SHIFT_MAX / 2;
#endif
  int strafe_target = 0;
  int fb_target = 0;
#if AIM_INPUT_MODE == AIM_INPUT_JOYSTICK
  int yaw_cur = 0;
  int pitch_cur = LAYER_Y_SHIFT_MAX / 2;
#endif
  int strafe_cur = 0;
  int fb_cur = 0;
#if INA219_MONITOR_ENABLE
  uint32_t last_ina219_ms = 0;
  uint32_t last_ina219_sample_ms = 0;
  int32_t ina219_peak_x10_ma = 0;
  int32_t ina219_last_bus_mv = 0;
  int32_t ina219_last_shunt_uv = 0;
  int32_t ina219_last_current_x10_ma = 0;
#endif
  uint8_t trigger_raw_last = 0;
  uint8_t trigger_stable = 0;
  uint32_t trigger_changed_ms = HAL_GetTick();
  uint32_t last_fire_ms = 0;
  uint32_t last_idle_ms = HAL_GetTick();
  uint8_t idle_phase = 0;
  uint8_t fire_count = 0;
  uint8_t weapon_ammo_remaining = R99_MAG_SIZE;
  FireWeaponKind current_fire_weapon = FIRE_WEAPON_R99;
  R99SfxKind last_r99_sfx_kind = R99_SFX_NONE;
  uint32_t r99_special_sfx_hold_until_ms = 0U;
  uint8_t r99_trigger_audio_active = 0U;
  uint8_t trigger_fire_latched = 0U;
  int robot_armor = robot_armor_max_for_level(ROBOT_LEVEL_R99_FIXED);
  int robot_health = ROBOT_HEALTH_MAX;
  uint32_t robot_dead_ms = 0U;
  RobotLevel robot_level = ROBOT_LEVEL_R99_FIXED;
  uint32_t robot_level_start_ms = HAL_GetTick();
  int robot_random_offset = 0;
  int robot_random_target = 0;
  uint32_t robot_random_next_ms = HAL_GetTick();
  uint8_t robot_random_step_index = 0U;
  uint16_t stage_shots = 0U;
  uint16_t stage_hits = 0U;
  int8_t stage_result = 0;
  uint8_t stage_advance_wait_knife = 0U;
  uint8_t stage_advance_spawn_after_draw = 0U;
  uint8_t stage_start_wait_draw = 0U;
  uint8_t ultimate_try_peacekeeper = 0U;
  uint8_t ultimate_try_wait_draw = 0U;
  uint8_t ultimate_keep_training_end = 0U;
  GameFlowState game_flow = GAME_FLOW_INTRO_IDLE;
  uint8_t dialog_page = 0U;
  uint8_t end_menu_selected = 0U;
  uint8_t weapon_menu_idx = 0U;
  uint8_t move_menu_idx = 0U;
  uint8_t free_menu_selected = 0U;
  uint8_t free_select_active = 0U;
  FireWeaponKind free_weapon = FIRE_WEAPON_R99;
  int recoil_x_q8 = 0;
  int recoil_y_q8 = 0;
#if WEAPON_LAYER_MODE == WEAPON_MODE_KNIFE
  uint32_t qiedao_start_ms = HAL_GetTick();
  uint32_t last_qiedao_render_ms = 0;
  HeldWeaponState held_weapon = HELD_WEAPON_KUWU;
  uint32_t qieqiang_start_ms = 0;
  uint32_t shouqiang_start_ms = 0;
  uint32_t weapon_anim_start_ms = 0;
  uint16_t weapon_active_anim = 0xFFFFU;
  uint8_t weapon_active_anim_reverse = 0U;
  HeldWeaponState weapon_anim_return_state = HELD_WEAPON_R99;
  uint8_t mouse_middle_last = 0;
  uint8_t mouse_right_last = 0;
  uint8_t gun_ads_enabled = 0;
  uint8_t btn8_last = 0U;
#endif
  BackgroundNavState bg_nav = {
    .cur_x = 0,
    .cur_y = 0,
    .old_x = 0,
    .old_y = 0,
    .new_x = 0,
    .new_y = 0,
    .transitioning = 0U,
    .transition_start_ms = HAL_GetTick(),
    .last_step_ms = HAL_GetTick(),
  };
#if W25Q_BG_ENABLE
  if (qspi_bg_ready) {
    bg_nav.cur_x = clamp_i(0, (int)qspi_bg_asset.x_min, (int)qspi_bg_asset.x_max);
    bg_nav.cur_y = clamp_i(0, (int)qspi_bg_asset.y_min, (int)qspi_bg_asset.y_max);
    bg_nav.old_x = bg_nav.cur_x;
    bg_nav.old_y = bg_nav.cur_y;
    bg_nav.new_x = bg_nav.cur_x;
    bg_nav.new_y = bg_nav.cur_y;
  }
#endif
  hit_particles_clear();
  ultimate_trail_clear();

    while (1)
    {
        Joystick_Read(&joystick_cfg, &joystick_data);
        Joystick_Read(&joystick2_cfg, &joystick2_data);

      float strafe_norm = clamp_f(joystick_data.coord_mapped.x * LOOK_STRAFE_INPUT_GAIN, -1.0f, 1.0f);
      float fb_norm = clamp_f(joystick_data.coord_mapped.y * LOOK_FB_INPUT_GAIN, -1.0f, 1.0f);
#if AIM_INPUT_MODE == AIM_INPUT_JOYSTICK
      // Preview convention: yaw > 0 means looking left.
      float yaw_norm = clamp_f(-joystick2_data.coord_mapped.x * LOOK_YAW_INPUT_GAIN, -1.0f, 1.0f);
      float pitch_norm = clamp_f(-joystick2_data.coord_mapped.y * LOOK_PITCH_INPUT_GAIN, -1.0f, 1.0f);
      yaw_target = (int)lroundf(yaw_norm * (float)YAW_RANGE);
      pitch_target = (LAYER_Y_SHIFT_MAX / 2) + (int)lroundf(pitch_norm * (float)PITCH_RANGE);
#endif
      strafe_target = (int)lroundf(strafe_norm * (float)STRAFE_RANGE);
      fb_target = (int)lroundf(fb_norm * (float)FB_RANGE);
#if AIM_INPUT_MODE == AIM_INPUT_JOYSTICK
      pitch_target = clamp_i(pitch_target, 0, LAYER_Y_SHIFT_MAX);

      yaw_cur += (yaw_target - yaw_cur) / 5;
      pitch_cur += (pitch_target - pitch_cur) / 5;
#endif
      strafe_cur += (strafe_target - strafe_cur) / 5;
      fb_cur += (fb_target - fb_cur) / 5;
#if W25Q_BG_ENABLE
      if (qspi_bg_ready) {
        const int src_max_x = (int)qspi_bg_asset.width - BG_VIEW_W;
        const int src_max_y = (int)qspi_bg_asset.height - BG_VIEW_H;
#if AIM_INPUT_MODE == AIM_INPUT_MOUSE
        int mouse_dx = 0;
        int mouse_dy = 0;
        uint8_t mouse_ready = 0;
        ps2_game_mouse_service(&mouse_dx, &mouse_dy, &mouse_ready, &ps2_mouse_buttons);
        (void)mouse_ready;
        if (!mouse_look_initialized) {
          mouse_look_x = src_max_x / 2;
          mouse_look_y = src_max_y / 2;
          mouse_look_x_q8 = mouse_look_x << 8;
          mouse_look_y_q8 = mouse_look_y << 8;
          mouse_look_initialized = 1U;
        }
        int gain_x_q8 = MOUSE_LOOK_GAIN_X_Q8;
        int gain_y_q8 = MOUSE_LOOK_GAIN_Y_Q8;
        if (gun_ads_enabled) {
          gain_x_q8 = (gain_x_q8 * MOUSE_ADS_GAIN_Q8) / 256;
          gain_y_q8 = (gain_y_q8 * MOUSE_ADS_GAIN_Q8) / 256;
        }
        mouse_look_x_q8 += mouse_dx * gain_x_q8;
        mouse_look_y_q8 -= mouse_dy * gain_y_q8;
        mouse_look_x_q8 = clamp_i(mouse_look_x_q8, 0, src_max_x << 8);
        mouse_look_y_q8 = clamp_i(mouse_look_y_q8, 0, src_max_y << 8);
        mouse_look_x = mouse_look_x_q8 >> 8;
        mouse_look_y = mouse_look_y_q8 >> 8;
        bg_src_x_dynamic = mouse_look_x;
        bg_src_y_dynamic = mouse_look_y;
#else
        const int src_mid_x = src_max_x / 2;
        const int src_mid_y = src_max_y / 2;
        const int pitch_mid = LAYER_Y_SHIFT_MAX / 2;
        int look_src_x = src_mid_x - (yaw_cur * src_mid_x) / YAW_RANGE;
        int look_src_y = src_mid_y + ((pitch_cur - pitch_mid) * src_mid_y) / pitch_mid;
        bg_src_x_dynamic = clamp_i(quantize_i(look_src_x, BG_LOOK_STEP_PX), 0, src_max_x);
        bg_src_y_dynamic = clamp_i(quantize_i(look_src_y, BG_LOOK_STEP_PX), 0, src_max_y);
#endif
      }
#endif

      uint32_t now = HAL_GetTick();
      if (apex_return_to_menu_requested(now)) {
        Audio_Stop();
        WS2812_Off();
        apex_ws2812_clear_cache();
        ps2_mouse_buttons = 0U;
        render_hud_visible = 0U;
        render_dialog_visible = 0U;
        render_menu_visible = 0U;
        render_bloodhound_visible = 0U;
        hit_particles_clear();
        ultimate_trail_clear();
        break;
      }
      hit_particles_update(now);
#if ROBOT_TEST_ENABLE
      if (!stage_advance_wait_knife
          && !stage_advance_spawn_after_draw
          && !stage_start_wait_draw
          && robot_dead_ms != 0U
          && (now - robot_dead_ms) >= (ROBOT_DIE_FRAME_MS * ROBOT_DIE_TIMELINE_FRAMES + ROBOT_LEVEL_ADVANCE_DELAY_MS)) {
        if (game_flow == GAME_FLOW_FREE_ACTIVE) {
          game_flow = GAME_FLOW_FREE_MENU;
          free_menu_selected = 0U;
        } else if (stage_result > 0 && game_flow == GAME_FLOW_STAGE_ACTIVE && robot_level == ROBOT_LEVEL_ULTIMATE) {
          ultimate_keep_training_end = ultimate_try_peacekeeper ? 1U : 0U;
          game_flow = GAME_FLOW_END_IDLE;
          dialog_page = 0U;
          ultimate_try_peacekeeper = 0U;
        } else if (stage_result > 0 && game_flow == GAME_FLOW_STAGE_ACTIVE) {
          stage_advance_wait_knife = 1U;
        } else if (stage_result < 0 && game_flow == GAME_FLOW_STAGE_ACTIVE) {
          game_flow = GAME_FLOW_RETRY_IDLE;
          dialog_page = 0U;
          ultimate_try_peacekeeper = 0U;
          ultimate_try_wait_draw = 0U;
        }
        if (!stage_advance_wait_knife && game_flow != GAME_FLOW_FREE_MENU) {
          robot_level_start_ms = now;
          robot_armor = robot_armor_max_for_level(robot_level);
          robot_health = ROBOT_HEALTH_MAX;
          robot_dead_ms = 0U;
          stage_shots = 0U;
          stage_hits = 0U;
          stage_result = 0;
          robot_random_offset = 0;
          robot_random_target = 0;
          robot_random_next_ms = now;
          robot_random_step_index = 0U;
          current_fire_weapon = (game_flow == GAME_FLOW_FREE_ACTIVE) ? free_weapon : weapon_for_robot_level(robot_level);
          weapon_ammo_remaining = weapon_mag_size(current_fire_weapon);
          last_r99_sfx_kind = R99_SFX_NONE;
          r99_special_sfx_hold_until_ms = 0U;
          trigger_fire_latched = 0U;
          ultimate_try_peacekeeper = 0U;
          ultimate_try_wait_draw = 0U;
        }
      } else {
        current_fire_weapon = (game_flow == GAME_FLOW_FREE_ACTIVE) ? free_weapon : weapon_for_robot_level(robot_level);
      }
      if (game_flow == GAME_FLOW_STAGE_ACTIVE && robot_level == ROBOT_LEVEL_ULTIMATE && ultimate_try_peacekeeper) {
        current_fire_weapon = FIRE_WEAPON_HEPING;
      }
      render_crosshair_weapon = current_fire_weapon;
      render_crosshair_ads = 0U;

      if (robot_level == ROBOT_LEVEL_FUZHU_RANDOM && robot_dead_ms == 0U) {
        if ((int)(robot_random_target - robot_random_offset) > ROBOT_RANDOM_STEP_PX) {
          robot_random_offset += ROBOT_RANDOM_STEP_PX;
        } else if ((int)(robot_random_offset - robot_random_target) > ROBOT_RANDOM_STEP_PX) {
          robot_random_offset -= ROBOT_RANDOM_STEP_PX;
        } else {
          robot_random_offset = robot_random_target;
        }
        if (robot_random_offset == robot_random_target
            && ((now - robot_random_next_ms) >= 0x80000000UL || now >= robot_random_next_ms)) {
          const uint8_t pattern_count = (uint8_t)(sizeof(robot_random_step_pattern) / sizeof(robot_random_step_pattern[0]));
          uint8_t idx = (uint8_t)(robot_random_step_index % pattern_count);
          int next_target = robot_random_target + (int)robot_random_step_pattern[idx];
          if (next_target > ROBOT_RANDOM_RANGE_PX || next_target < -ROBOT_RANDOM_RANGE_PX) {
            next_target = robot_random_target - (int)robot_random_step_pattern[idx];
          }
          robot_random_target = clamp_i(next_target, -ROBOT_RANDOM_RANGE_PX, ROBOT_RANDOM_RANGE_PX);
          robot_random_next_ms = now + (uint32_t)robot_random_hold_pattern[idx];
          robot_random_step_index++;
        }
      }
#endif
#if INA219_MONITOR_ENABLE
      if ((now - last_ina219_sample_ms) >= 25U) {
        if (ina219_sample(&ina219_last_bus_mv, &ina219_last_shunt_uv, &ina219_last_current_x10_ma)) {
          int32_t current_abs = abs_i((int)ina219_last_current_x10_ma);
          if (current_abs > ina219_peak_x10_ma) {
            ina219_peak_x10_ma = current_abs;
          }
        }
        last_ina219_sample_ms = now;
      }
      if ((now - last_ina219_ms) >= 1000U) {
        int32_t current_abs = abs_i((int)ina219_last_current_x10_ma);
        printf("[INA219] bus=%ld.%03ldV shunt=%lduV current=%s%ld.%ldmA peak=%ld.%ldmA\n",
               (long)(ina219_last_bus_mv / 1000),
               (long)(ina219_last_bus_mv % 1000),
               (long)ina219_last_shunt_uv,
               (ina219_last_current_x10_ma < 0) ? "-" : "",
               (long)(current_abs / 10),
               (long)(current_abs % 10),
               (long)(ina219_peak_x10_ma / 10),
               (long)(ina219_peak_x10_ma % 10));
        ina219_peak_x10_ma = 0;
        last_ina219_ms = now;
      }
#endif
      WeaponRenderState weapon = {
        .type = WEAPON_RENDER_QSPI_KNIFE,
        .dx = 0,
        .dy = 0,
        .frame_idx = 0,
        .gun_image_idx = GUN_IMAGE_R99_NORMAL,
      };

#if WEAPON_LAYER_MODE == WEAPON_MODE_R99
      uint8_t trigger_raw = ((ps2_mouse_buttons & MOUSE_BTN_LEFT) != 0U) ? 1U : 0U;
      if (trigger_raw != trigger_raw_last) {
        trigger_raw_last = trigger_raw;
        trigger_changed_ms = now;
      }
      if ((now - trigger_changed_ms) >= TRIGGER_DEBOUNCE_MS) {
        trigger_stable = trigger_raw;
      }
        if (!trigger_stable) {
          last_r99_sfx_kind = R99_SFX_NONE;
          r99_special_sfx_hold_until_ms = 0U;
          trigger_fire_latched = 0U;
        if (!weapon_is_single_shot(current_fire_weapon) && r99_trigger_audio_active) {
            Audio_Stop();
            r99_trigger_audio_active = 0U;
          }
        }

      if (trigger_stable
          && weapon_ammo_remaining == 0U
          && !trigger_fire_latched) {
        if (!weapon_is_single_shot(current_fire_weapon) && r99_trigger_audio_active) {
          Audio_Stop();
        }
        Audio_Play(&needreload_sfx8);
        r99_trigger_audio_active = 0U;
        trigger_fire_latched = 1U;
      }

      if (trigger_stable
          && weapon_ammo_remaining > 0U
          && (!weapon_is_single_shot(current_fire_weapon) || !trigger_fire_latched)
          && (last_fire_ms == 0U || (now - last_fire_ms) >= weapon_fire_interval_ms(current_fire_weapon))) {
        R99SfxKind sfx_kind = R99_SFX_WALL;
#if ROBOT_TEST_ENABLE
        RobotRenderState shot_robot = robot_make_render_state(&bg_nav, robot_armor, robot_health, now, robot_level, robot_level_start_ms, robot_dead_ms, robot_random_offset);
        uint8_t heping_coverage = (current_fire_weapon == FIRE_WEAPON_HEPING) ? robot_heping_coverage_percent(&shot_robot) : 0U;
        uint8_t robot_hit = (robot_health > 0
                             && ((current_fire_weapon == FIRE_WEAPON_HEPING && heping_coverage > 0U)
                                 || (current_fire_weapon != FIRE_WEAPON_HEPING && robot_crosshair_hit(&shot_robot)))) ? 1U : 0U;
        uint8_t count_shot = (robot_dead_ms == 0U) ? 1U : 0U;
        if (count_shot) {
          stage_shots++;
          if (robot_hit) {
            stage_hits++;
          }
        }
        if (robot_hit) {
          sfx_kind = robot_apply_damage_hit(&robot_armor, &robot_health, robot_damage_for_weapon(current_fire_weapon, heping_coverage));
          hit_particles_spawn(&shot_robot, sfx_kind);
          if (robot_health <= 0 && robot_dead_ms == 0U) {
            robot_dead_ms = now;
            apex_ws2812_start_kill_fx(robot_level, now);
            stage_result = robot_stage_passed(robot_level, current_fire_weapon, stage_shots, stage_hits) ? 1 : -1;
          }
        }
#endif
        r99_play_feedback_sfx(sfx_kind, current_fire_weapon, &last_r99_sfx_kind, &r99_special_sfx_hold_until_ms, now);
        r99_trigger_audio_active = 1U;
        trigger_fire_latched = 1U;
        recoil_y_q8 += R99_RECOIL_KICK_Q8;
        if (recoil_y_q8 < R99_RECOIL_MIN_Q8) {
          recoil_y_q8 = R99_RECOIL_MIN_Q8;
        }
        recoil_x_q8 += ((fire_count & 1U) ? 1 : -1) * 256;
        fire_count++;
        weapon_ammo_remaining--;
        if (game_flow == GAME_FLOW_STAGE_ACTIVE
            && robot_level == ROBOT_LEVEL_ULTIMATE
            && current_fire_weapon == FIRE_WEAPON_R99
            && !ultimate_try_peacekeeper
            && robot_dead_ms == 0U
            && stage_shots >= (uint16_t)(R99_MAG_SIZE * ULTIMATE_R99_MAG_LIMIT)) {
          ultimate_try_peacekeeper = 1U;
          ultimate_try_wait_draw = 1U;
          robot_health = 0;
          robot_armor = 0;
          stage_result = 0;
          stage_advance_wait_knife = 0U;
          stage_advance_spawn_after_draw = 0U;
          if (r99_trigger_audio_active) {
            Audio_Stop();
            r99_trigger_audio_active = 0U;
          }
          last_r99_sfx_kind = R99_SFX_NONE;
          r99_special_sfx_hold_until_ms = 0U;
          trigger_fire_latched = 0U;
          held_weapon = HELD_WEAPON_SHOUQIANG_TO_KUWU;
          shouqiang_start_ms = now;
          gun_ads_enabled = 0U;
        }
        if (!weapon_is_single_shot(current_fire_weapon) && weapon_ammo_remaining == 0U) {
          if (r99_trigger_audio_active) {
            Audio_Stop();
            r99_trigger_audio_active = 0U;
          }
          last_r99_sfx_kind = R99_SFX_NONE;
          r99_special_sfx_hold_until_ms = 0U;
          trigger_fire_latched = 0U;
        }
        last_fire_ms = now;
      }

      if ((now - last_idle_ms) >= 80U) {
        idle_phase = (uint8_t)((idle_phase + 1U) & 31U);
        last_idle_ms = now;
      }

      recoil_y_q8 = (recoil_y_q8 * 86) / 100;
      recoil_x_q8 = (recoil_x_q8 * 80) / 100;
      if (abs_i(recoil_y_q8) < 24) {
        recoil_y_q8 = 0;
      }
      if (abs_i(recoil_x_q8) < 24) {
        recoil_x_q8 = 0;
      }

      const int gun_dx = (int)gun_idle_x[idle_phase] + (recoil_x_q8 / 256);
      const int gun_dy = (int)gun_idle_y[idle_phase] + (recoil_y_q8 / 256);

      weapon.type = WEAPON_RENDER_GUN_IMAGE;
      weapon.gun_image_idx = gun_image_for_weapon(current_fire_weapon, 0U);
      weapon.dx = gun_dx;
      weapon.dy = gun_dy;
#elif WEAPON_LAYER_MODE == WEAPON_MODE_KNIFE
#if W25Q_KNIFE_FORCE_KUWU
      weapon.type = WEAPON_RENDER_QSPI_KNIFE;
      weapon.dx = 0;
      weapon.dy = 0;
      weapon.frame_idx = (int)qspi_weapon_asset.kuwu_frame;
#else
      uint8_t qiedao_done = 1U;
      int qiedao_frame_idx = qspi_weapon_anim_frame(WPN_ANIM_KUWU_DRAW, now - qiedao_start_ms, 0U, &qiedao_done);
      uint8_t qiedao_active = qiedao_done ? 0U : 1U;
      uint8_t middle_down = ((ps2_mouse_buttons & MOUSE_BTN_MIDDLE) != 0U) ? 1U : 0U;
      uint8_t right_down = ((ps2_mouse_buttons & MOUSE_BTN_RIGHT) != 0U) ? 1U : 0U;
      uint8_t btn8_down = (HAL_GPIO_ReadPin(BTN8_GPIO_Port, BTN8_Pin) == GPIO_PIN_RESET) ? 1U : 0U;
      uint8_t bloodhound_near = 1U;
      if (qspi_bg_ready
          && (game_flow == GAME_FLOW_INTRO
              || game_flow == GAME_FLOW_INTRO_IDLE
              || game_flow == GAME_FLOW_RETRY_IDLE
              || game_flow == GAME_FLOW_RETRY_DIALOG
              || game_flow == GAME_FLOW_END_DIALOG
              || game_flow == GAME_FLOW_END_MENU
              || game_flow == GAME_FLOW_END_IDLE
              || game_flow == GAME_FLOW_SELECT_WEAPON
              || game_flow == GAME_FLOW_SELECT_MOVE)) {
        const int grid_x = bg_nav.transitioning ? bg_nav.new_x : bg_nav.cur_x;
        const int grid_y = bg_nav.transitioning ? bg_nav.new_y : bg_nav.cur_y;
        const int npc_bg_x = BLOODHOUND_WORLD_X - grid_x * ROBOT_GRID_X_STEP + grid_y * ROBOT_GRID_Y_TO_X;
        const int npc_bg_y = BLOODHOUND_WORLD_Y - grid_y * ROBOT_GRID_Y_STEP;
        const int max_src_x = (int)qspi_bg_asset.width - BG_VIEW_W;
        const int max_src_y = (int)qspi_bg_asset.height - BG_VIEW_H;
        const int crop_x = clamp_i(bg_src_x_dynamic, 0, max_src_x);
        const int crop_y = clamp_i(bg_src_y_dynamic, 0, max_src_y);
        const int screen_x = BG_VIEW_X + npc_bg_x - crop_x;
        const int screen_y = BG_VIEW_Y + npc_bg_y - crop_y;
        bloodhound_near = (abs_i(screen_x - (ST7789V2_WIDTH / 2)) <= BLOODHOUND_TRIGGER_RANGE_X
                           && abs_i(screen_y - (BG_VIEW_Y + BG_VIEW_H / 2)) <= BLOODHOUND_TRIGGER_RANGE_Y) ? 1U : 0U;
      }
      if (middle_down && !mouse_middle_last) {
        if (game_flow == GAME_FLOW_END_MENU) {
          end_menu_selected ^= 1U;
        } else if (game_flow == GAME_FLOW_FREE_MENU) {
          free_menu_selected = (uint8_t)((free_menu_selected + 1U) % 3U);
        } else if (game_flow == GAME_FLOW_SELECT_WEAPON) {
          weapon_menu_idx = (uint8_t)((weapon_menu_idx + 1U) % 3U);
        } else if (game_flow == GAME_FLOW_SELECT_MOVE) {
          move_menu_idx = (uint8_t)((move_menu_idx + 1U) % 3U);
        }
      }
      if (btn8_down && !btn8_last && bloodhound_near) {
        if (game_flow == GAME_FLOW_INTRO_IDLE) {
          game_flow = GAME_FLOW_INTRO;
          dialog_page = 0U;
        } else if (game_flow == GAME_FLOW_RETRY_IDLE) {
          game_flow = GAME_FLOW_RETRY_DIALOG;
          dialog_page = 0U;
        } else if (game_flow == GAME_FLOW_INTRO) {
          dialog_page++;
          if (dialog_page >= (uint8_t)(sizeof(intro_dialog) / sizeof(intro_dialog[0]))) {
            game_flow = GAME_FLOW_STAGE_ACTIVE;
            dialog_page = 0U;
            robot_level_start_ms = now;
            stage_start_wait_draw = 1U;
            ultimate_try_peacekeeper = 0U;
            ultimate_try_wait_draw = 0U;
          }
        } else if (game_flow == GAME_FLOW_RETRY_DIALOG) {
          dialog_page++;
          if (dialog_page >= (uint8_t)(sizeof(retry_dialog) / sizeof(retry_dialog[0]))) {
            game_flow = GAME_FLOW_STAGE_ACTIVE;
            dialog_page = 0U;
            robot_level_start_ms = now;
            stage_start_wait_draw = 1U;
            ultimate_try_peacekeeper = 0U;
            ultimate_try_wait_draw = 0U;
          }
        } else if (game_flow == GAME_FLOW_END_DIALOG) {
          dialog_page++;
          uint8_t end_count = ultimate_keep_training_end
                              ? (uint8_t)(sizeof(end_dialog_keep_training) / sizeof(end_dialog_keep_training[0]))
                              : (uint8_t)(sizeof(end_dialog) / sizeof(end_dialog[0]));
          if (dialog_page >= end_count) {
            game_flow = GAME_FLOW_END_MENU;
            dialog_page = 0U;
            end_menu_selected = 0U;
          }
        } else if (game_flow == GAME_FLOW_END_MENU) {
          if (end_menu_selected == 0U) {
            free_select_active = 1U;
            game_flow = GAME_FLOW_SELECT_WEAPON;
            weapon_menu_idx = 0U;
          } else {
            game_flow = GAME_FLOW_END_IDLE;
            dialog_page = 0U;
          }
        } else if (game_flow == GAME_FLOW_FREE_MENU) {
          if (free_menu_selected == 0U) {
            robot_armor = robot_armor_max_for_level(robot_level);
            robot_health = ROBOT_HEALTH_MAX;
            robot_dead_ms = 0U;
            stage_shots = 0U;
            stage_hits = 0U;
            stage_result = 0;
            robot_random_offset = 0;
            robot_random_target = 0;
            robot_random_next_ms = now;
            robot_random_step_index = 0U;
            current_fire_weapon = free_weapon;
            weapon_ammo_remaining = weapon_mag_size(current_fire_weapon);
            last_r99_sfx_kind = R99_SFX_NONE;
            r99_special_sfx_hold_until_ms = 0U;
            trigger_fire_latched = 0U;
            game_flow = GAME_FLOW_FREE_ACTIVE;
            robot_level_start_ms = now;
          } else if (free_menu_selected == 1U) {
            free_select_active = 1U;
            game_flow = GAME_FLOW_SELECT_WEAPON;
            weapon_menu_idx = 0U;
          } else {
            game_flow = GAME_FLOW_FREE_REST;
            dialog_page = 0U;
          }
        } else if (game_flow == GAME_FLOW_FREE_REST) {
          free_select_active = 1U;
          game_flow = GAME_FLOW_SELECT_WEAPON;
          weapon_menu_idx = 0U;
        } else if (game_flow == GAME_FLOW_END_IDLE) {
            game_flow = GAME_FLOW_END_DIALOG;
            dialog_page = 0U;
        } else if (game_flow == GAME_FLOW_SELECT_WEAPON) {
          free_weapon = menu_weapon_for_index(weapon_menu_idx);
          game_flow = GAME_FLOW_SELECT_MOVE;
          move_menu_idx = 0U;
        } else if (game_flow == GAME_FLOW_SELECT_MOVE) {
          robot_level = free_level_for_selection(free_weapon, move_menu_idx);
          current_fire_weapon = free_weapon;
          weapon_ammo_remaining = weapon_mag_size(current_fire_weapon);
          robot_armor = robot_armor_max_for_level(robot_level);
          robot_health = ROBOT_HEALTH_MAX;
          robot_dead_ms = 0U;
          stage_shots = 0U;
          stage_hits = 0U;
          stage_result = 0;
          stage_advance_wait_knife = 0U;
          stage_advance_spawn_after_draw = 0U;
          stage_start_wait_draw = 0U;
          ultimate_try_peacekeeper = 0U;
          ultimate_try_wait_draw = 0U;
          free_select_active = 0U;
          game_flow = GAME_FLOW_FREE_ACTIVE;
          robot_level_start_ms = now;
        }
      }
      uint8_t non_combat_mode = (game_flow != GAME_FLOW_STAGE_ACTIVE && game_flow != GAME_FLOW_FREE_ACTIVE) ? 1U : 0U;
      if (non_combat_mode) {
        if (!qiedao_active
            && held_weapon != HELD_WEAPON_KUWU
            && held_weapon != HELD_WEAPON_SHOUQIANG_TO_KUWU) {
          held_weapon = HELD_WEAPON_SHOUQIANG_TO_KUWU;
          shouqiang_start_ms = now;
        }
        gun_ads_enabled = 0U;
        trigger_stable = 0U;
        trigger_raw_last = 0U;
        last_r99_sfx_kind = R99_SFX_NONE;
        r99_special_sfx_hold_until_ms = 0U;
        if (r99_trigger_audio_active) {
          Audio_Stop();
          r99_trigger_audio_active = 0U;
        }
        btn8_last = btn8_down;
        mouse_middle_last = middle_down;
        mouse_right_last = right_down;
      }
      if (!non_combat_mode
          && ultimate_try_wait_draw
          && !qiedao_active
          && held_weapon == HELD_WEAPON_KUWU
          && middle_down
          && !mouse_middle_last) {
        ultimate_try_wait_draw = 0U;
        stage_advance_spawn_after_draw = 1U;
        current_fire_weapon = FIRE_WEAPON_HEPING;
        weapon_ammo_remaining = weapon_mag_size(current_fire_weapon);
        robot_armor = robot_armor_max_for_level(robot_level);
        robot_health = ROBOT_HEALTH_MAX;
        robot_dead_ms = 0U;
        stage_shots = 0U;
        stage_hits = 0U;
        stage_result = 0;
        robot_random_offset = 0;
        robot_random_target = 0;
        robot_random_next_ms = now;
        robot_random_step_index = 0U;
        last_r99_sfx_kind = R99_SFX_NONE;
        r99_special_sfx_hold_until_ms = 0U;
        trigger_fire_latched = 0U;
        gun_ads_enabled = 0U;
        if (qspi_weapon_anim_exists(WPN_ANIM_KUWU_STOW)) {
          held_weapon = HELD_WEAPON_KUWU_STOW_TO_GUN;
        } else {
          held_weapon = HELD_WEAPON_QIEQIANG_TO_R99;
        }
        qieqiang_start_ms = now;
        if (held_weapon == HELD_WEAPON_QIEQIANG_TO_R99) {
          weapon_play_action_sfx(weapon_draw_sound(current_fire_weapon));
        }
      }
      if (!non_combat_mode
          && stage_start_wait_draw
          && !qiedao_active
          && held_weapon == HELD_WEAPON_KUWU
          && middle_down
          && !mouse_middle_last) {
        stage_start_wait_draw = 0U;
        stage_advance_spawn_after_draw = 1U;
        current_fire_weapon = weapon_for_robot_level(robot_level);
        weapon_ammo_remaining = weapon_mag_size(current_fire_weapon);
        last_r99_sfx_kind = R99_SFX_NONE;
        r99_special_sfx_hold_until_ms = 0U;
        trigger_fire_latched = 0U;
        gun_ads_enabled = 0U;
        if (qspi_weapon_anim_exists(WPN_ANIM_KUWU_STOW)) {
          held_weapon = HELD_WEAPON_KUWU_STOW_TO_GUN;
        } else {
          held_weapon = HELD_WEAPON_QIEQIANG_TO_R99;
        }
        qieqiang_start_ms = now;
        if (held_weapon == HELD_WEAPON_QIEQIANG_TO_R99) {
          weapon_play_action_sfx(weapon_draw_sound(current_fire_weapon));
        }
      }
      if (!non_combat_mode
          && stage_advance_wait_knife
          && !qiedao_active
          && held_weapon == HELD_WEAPON_KUWU
          && middle_down
          && !mouse_middle_last) {
        robot_level = next_robot_level(robot_level);
        stage_advance_wait_knife = 0U;
        stage_advance_spawn_after_draw = 1U;
        ultimate_try_peacekeeper = 0U;
        ultimate_try_wait_draw = 0U;
        robot_armor = robot_armor_max_for_level(robot_level);
        robot_health = ROBOT_HEALTH_MAX;
        robot_dead_ms = 0U;
        stage_shots = 0U;
        stage_hits = 0U;
        stage_result = 0;
        robot_random_offset = 0;
        robot_random_target = 0;
        robot_random_next_ms = now;
        robot_random_step_index = 0U;
        current_fire_weapon = weapon_for_robot_level(robot_level);
        weapon_ammo_remaining = weapon_mag_size(current_fire_weapon);
        last_r99_sfx_kind = R99_SFX_NONE;
        r99_special_sfx_hold_until_ms = 0U;
        trigger_fire_latched = 0U;
        gun_ads_enabled = 0U;
        if (qspi_weapon_anim_exists(WPN_ANIM_KUWU_STOW)) {
          held_weapon = HELD_WEAPON_KUWU_STOW_TO_GUN;
        } else {
          held_weapon = HELD_WEAPON_QIEQIANG_TO_R99;
        }
        qieqiang_start_ms = now;
        if (held_weapon == HELD_WEAPON_QIEQIANG_TO_R99) {
          weapon_play_action_sfx(weapon_draw_sound(current_fire_weapon));
        }
      }
      if (!qiedao_active
          && !non_combat_mode
          && !ultimate_try_wait_draw
          && !stage_start_wait_draw
          && !stage_advance_wait_knife
          && held_weapon == HELD_WEAPON_KUWU
          && btn8_down
          && !btn8_last) {
        current_fire_weapon = weapon_for_robot_level(robot_level);
        last_r99_sfx_kind = R99_SFX_NONE;
        r99_special_sfx_hold_until_ms = 0U;
      }
      if (!qiedao_active
          && !non_combat_mode
          && !ultimate_try_wait_draw
          && !stage_start_wait_draw
          && !stage_advance_wait_knife
          && held_weapon == HELD_WEAPON_R99
          && btn8_down
          && !btn8_last) {
        uint16_t reload_anim = weapon_reload_anim_for_kind(current_fire_weapon);
        if (qspi_weapon_anim_exists(reload_anim)) {
          held_weapon = HELD_WEAPON_RELOAD;
          weapon_active_anim = reload_anim;
          weapon_active_anim_reverse = 0U;
          weapon_anim_start_ms = now;
          weapon_anim_return_state = HELD_WEAPON_R99;
          weapon_play_action_sfx(weapon_reload_sound(current_fire_weapon));
        }
      }
      btn8_last = btn8_down;
      if (!qiedao_active
          && !non_combat_mode
          && !ultimate_try_wait_draw
          && !stage_start_wait_draw
          && !stage_advance_wait_knife
          && held_weapon == HELD_WEAPON_KUWU
          && middle_down
          && !mouse_middle_last) {
        if (qspi_weapon_anim_exists(WPN_ANIM_KUWU_STOW)) {
          held_weapon = HELD_WEAPON_KUWU_STOW_TO_GUN;
        } else {
          held_weapon = HELD_WEAPON_QIEQIANG_TO_R99;
        }
        qieqiang_start_ms = now;
        if (held_weapon == HELD_WEAPON_QIEQIANG_TO_R99) {
          weapon_play_action_sfx(weapon_draw_sound(current_fire_weapon));
        }
        gun_ads_enabled = 0U;
      }
      if (!qiedao_active
          && !non_combat_mode
          && held_weapon == HELD_WEAPON_R99
          && middle_down
          && !mouse_middle_last) {
        held_weapon = HELD_WEAPON_SHOUQIANG_TO_KUWU;
        shouqiang_start_ms = now;
        gun_ads_enabled = 0U;
        trigger_stable = 0U;
        trigger_raw_last = 0U;
        last_r99_sfx_kind = R99_SFX_NONE;
        r99_special_sfx_hold_until_ms = 0U;
        if (r99_trigger_audio_active) {
          Audio_Stop();
          r99_trigger_audio_active = 0U;
        }
      }
      mouse_middle_last = middle_down;
      if (!non_combat_mode && !ultimate_try_wait_draw && !stage_start_wait_draw && !stage_advance_wait_knife && held_weapon == HELD_WEAPON_R99 && right_down && !mouse_right_last) {
        gun_ads_enabled ^= 1U;
        uint16_t ads_anim = weapon_ads_anim_for_kind(current_fire_weapon);
        if (qspi_weapon_anim_exists(ads_anim)) {
          held_weapon = HELD_WEAPON_ADS_ANIM;
          weapon_active_anim = ads_anim;
          weapon_active_anim_reverse = gun_ads_enabled ? 0U : 1U;
          weapon_anim_start_ms = now;
          weapon_anim_return_state = HELD_WEAPON_R99;
        }
      }
      mouse_right_last = right_down;

      if (qiedao_active) {
        if (last_qiedao_render_ms != 0U && (now - last_qiedao_render_ms) < WEAPON_QIEDAO_RENDER_FRAME_MS) {
          HAL_Delay(1);
          continue;
        }
        last_qiedao_render_ms = now;
      }
      weapon.type = WEAPON_RENDER_QSPI_KNIFE;
      weapon.dx = 0;
      weapon.dy = 0;
      weapon.frame_idx = (int)qspi_weapon_asset.kuwu_frame;
      if (qiedao_active) {
        weapon.frame_idx = qiedao_frame_idx;
      } else if (held_weapon == HELD_WEAPON_KUWU_STOW_TO_GUN) {
        uint8_t anim_done = 0U;
        int frame_idx = qspi_weapon_anim_frame(WPN_ANIM_KUWU_STOW, now - qieqiang_start_ms, 0U, &anim_done);
        if (anim_done) {
          held_weapon = HELD_WEAPON_QIEQIANG_TO_R99;
          qieqiang_start_ms = now;
          weapon_play_action_sfx(weapon_draw_sound(current_fire_weapon));
        } else {
          weapon.frame_idx = frame_idx;
        }
      } else if (held_weapon == HELD_WEAPON_QIEQIANG_TO_R99) {
        uint8_t anim_done = 0U;
        int frame_idx = qspi_weapon_anim_frame(weapon_draw_anim_for_kind(current_fire_weapon), now - qieqiang_start_ms, 0U, &anim_done);
        if (anim_done) {
          held_weapon = HELD_WEAPON_R99;
          if (stage_advance_spawn_after_draw) {
            robot_level_start_ms = now;
            robot_armor = robot_armor_max_for_level(robot_level);
            robot_health = ROBOT_HEALTH_MAX;
            robot_dead_ms = 0U;
            stage_shots = 0U;
            stage_hits = 0U;
            stage_result = 0;
            stage_advance_spawn_after_draw = 0U;
            robot_random_offset = 0;
            robot_random_target = 0;
            robot_random_next_ms = now;
            robot_random_step_index = 0U;
          }
        } else {
          weapon.frame_idx = frame_idx;
        }
      } else if (held_weapon == HELD_WEAPON_SHOUQIANG_TO_KUWU) {
        uint8_t anim_done = 0U;
        int frame_idx = qspi_weapon_anim_frame(weapon_stow_anim_for_kind(current_fire_weapon), now - shouqiang_start_ms, 0U, &anim_done);
        if (anim_done) {
          held_weapon = HELD_WEAPON_KUWU;
          qiedao_start_ms = now;
          last_qiedao_render_ms = 0U;
          weapon_play_action_sfx(&kuwu_draw_sfx8);
        } else {
          weapon.frame_idx = frame_idx;
        }
      } else if (held_weapon == HELD_WEAPON_RELOAD
                 || held_weapon == HELD_WEAPON_ADS_ANIM
                 || held_weapon == HELD_WEAPON_FIRE_ANIM) {
        uint8_t anim_done = 0U;
        int frame_idx = qspi_weapon_anim_frame(weapon_active_anim,
                                               now - weapon_anim_start_ms,
                                               weapon_active_anim_reverse,
                                               &anim_done);
        if (anim_done) {
          if (held_weapon == HELD_WEAPON_RELOAD) {
            weapon_ammo_remaining = weapon_mag_size(current_fire_weapon);
          }
          held_weapon = weapon_anim_return_state;
        } else {
          weapon.frame_idx = frame_idx;
        }
      }

      if (held_weapon == HELD_WEAPON_R99) {
        uint8_t trigger_raw = (!ultimate_try_wait_draw && !stage_start_wait_draw && !stage_advance_wait_knife && (ps2_mouse_buttons & MOUSE_BTN_LEFT) != 0U) ? 1U : 0U;
        if (trigger_raw != trigger_raw_last) {
          trigger_raw_last = trigger_raw;
          trigger_changed_ms = now;
        }
        if ((now - trigger_changed_ms) >= TRIGGER_DEBOUNCE_MS) {
          trigger_stable = trigger_raw;
        }
        if (!trigger_stable) {
          last_r99_sfx_kind = R99_SFX_NONE;
          r99_special_sfx_hold_until_ms = 0U;
          trigger_fire_latched = 0U;
          if (!weapon_is_single_shot(current_fire_weapon) && r99_trigger_audio_active) {
            Audio_Stop();
            r99_trigger_audio_active = 0U;
          }
        }

        if (trigger_stable
            && weapon_ammo_remaining == 0U
            && !trigger_fire_latched) {
          if (!weapon_is_single_shot(current_fire_weapon) && r99_trigger_audio_active) {
            Audio_Stop();
          }
          Audio_Play(&needreload_sfx8);
          r99_trigger_audio_active = 0U;
          trigger_fire_latched = 1U;
        }

        if (trigger_stable
            && weapon_ammo_remaining > 0U
            && (!weapon_is_single_shot(current_fire_weapon) || !trigger_fire_latched)
            && (last_fire_ms == 0U || (now - last_fire_ms) >= weapon_fire_interval_ms(current_fire_weapon))) {
          R99SfxKind sfx_kind = R99_SFX_WALL;
#if ROBOT_TEST_ENABLE
          RobotRenderState shot_robot = robot_make_render_state(&bg_nav, robot_armor, robot_health, now, robot_level, robot_level_start_ms, robot_dead_ms, robot_random_offset);
          uint8_t heping_coverage = (current_fire_weapon == FIRE_WEAPON_HEPING) ? robot_heping_coverage_percent(&shot_robot) : 0U;
          uint8_t robot_hit = (robot_health > 0
                               && ((current_fire_weapon == FIRE_WEAPON_HEPING && heping_coverage > 0U)
                                   || (current_fire_weapon != FIRE_WEAPON_HEPING && robot_crosshair_hit(&shot_robot)))) ? 1U : 0U;
          uint8_t count_shot = (robot_dead_ms == 0U) ? 1U : 0U;
          if (count_shot) {
            stage_shots++;
            if (robot_hit) {
              stage_hits++;
            }
          }
          if (robot_hit) {
            sfx_kind = robot_apply_damage_hit(&robot_armor, &robot_health, robot_damage_for_weapon(current_fire_weapon, heping_coverage));
            hit_particles_spawn(&shot_robot, sfx_kind);
            if (robot_health <= 0 && robot_dead_ms == 0U) {
              robot_dead_ms = now;
              apex_ws2812_start_kill_fx(robot_level, now);
              stage_result = robot_stage_passed(robot_level, current_fire_weapon, stage_shots, stage_hits) ? 1 : -1;
            }
          }
#endif
          r99_play_feedback_sfx(sfx_kind, current_fire_weapon, &last_r99_sfx_kind, &r99_special_sfx_hold_until_ms, now);
          r99_trigger_audio_active = 1U;
          trigger_fire_latched = 1U;
          recoil_y_q8 += R99_RECOIL_KICK_Q8;
          if (recoil_y_q8 < R99_RECOIL_MIN_Q8) {
            recoil_y_q8 = R99_RECOIL_MIN_Q8;
          }
          recoil_x_q8 += ((fire_count & 1U) ? 1 : -1) * 256;
          fire_count++;
          weapon_ammo_remaining--;
          if (game_flow == GAME_FLOW_STAGE_ACTIVE
              && robot_level == ROBOT_LEVEL_ULTIMATE
              && current_fire_weapon == FIRE_WEAPON_R99
              && !ultimate_try_peacekeeper
              && robot_dead_ms == 0U
              && stage_shots >= (uint16_t)(R99_MAG_SIZE * ULTIMATE_R99_MAG_LIMIT)) {
            ultimate_try_peacekeeper = 1U;
            ultimate_try_wait_draw = 1U;
            robot_health = 0;
            robot_armor = 0;
            stage_result = 0;
            stage_advance_wait_knife = 0U;
            stage_advance_spawn_after_draw = 0U;
            if (r99_trigger_audio_active) {
              Audio_Stop();
              r99_trigger_audio_active = 0U;
            }
            last_r99_sfx_kind = R99_SFX_NONE;
            r99_special_sfx_hold_until_ms = 0U;
            trigger_fire_latched = 0U;
            held_weapon = HELD_WEAPON_SHOUQIANG_TO_KUWU;
            shouqiang_start_ms = now;
            gun_ads_enabled = 0U;
          }
          if (!weapon_is_single_shot(current_fire_weapon) && weapon_ammo_remaining == 0U) {
            if (r99_trigger_audio_active) {
              Audio_Stop();
              r99_trigger_audio_active = 0U;
            }
            last_r99_sfx_kind = R99_SFX_NONE;
            r99_special_sfx_hold_until_ms = 0U;
            trigger_fire_latched = 0U;
          }
          last_fire_ms = now;
          uint16_t fire_anim = weapon_fire_anim_for_kind(current_fire_weapon);
          if (weapon_is_single_shot(current_fire_weapon) && qspi_weapon_anim_exists(fire_anim)) {
            held_weapon = HELD_WEAPON_FIRE_ANIM;
            weapon_active_anim = fire_anim;
            weapon_active_anim_reverse = 0U;
            weapon_anim_start_ms = now;
            weapon_anim_return_state = HELD_WEAPON_R99;
          }
        }

        if ((now - last_idle_ms) >= 80U) {
          idle_phase = (uint8_t)((idle_phase + 1U) & 31U);
          last_idle_ms = now;
        }

        recoil_y_q8 = (recoil_y_q8 * 86) / 100;
        recoil_x_q8 = (recoil_x_q8 * 80) / 100;
        if (abs_i(recoil_y_q8) < 24) {
          recoil_y_q8 = 0;
        }
        if (abs_i(recoil_x_q8) < 24) {
          recoil_x_q8 = 0;
        }

        weapon.type = WEAPON_RENDER_GUN_IMAGE;
        if (gun_ads_enabled) {
          weapon.dx = 0;
          weapon.dy = 0;
        } else {
          weapon.dx = (int)gun_idle_x[idle_phase] + (recoil_x_q8 / 256);
          weapon.dy = (int)gun_idle_y[idle_phase] + (recoil_y_q8 / 256);
        }
        weapon.gun_image_idx = gun_image_for_weapon(current_fire_weapon, gun_ads_enabled);
      }
      render_crosshair_ads = (held_weapon == HELD_WEAPON_R99
                              && gun_ads_enabled
                              && current_fire_weapon != FIRE_WEAPON_HEPING) ? 1U : 0U;
#endif
#if W25Q_CACHE_CURRENT_FRAME
      if (weapon.type == WEAPON_RENDER_QSPI_KNIFE) {
        if (weapon.frame_idx != (int)qspi_weapon_asset.kuwu_frame) {
          qspi_weapon_cache_frame(weapon.frame_idx);
        }
      }
#endif
#else
#error "Unsupported WEAPON_LAYER_MODE"
#endif

#if W25Q_BG_ENABLE
      if (qspi_bg_ready) {
#if W25Q_BG_LOCK_CENTER
        bg_nav.cur_x = clamp_i(0, (int)qspi_bg_asset.x_min, (int)qspi_bg_asset.x_max);
        bg_nav.cur_y = clamp_i(0, (int)qspi_bg_asset.y_min, (int)qspi_bg_asset.y_max);
        bg_nav.old_x = bg_nav.cur_x;
        bg_nav.old_y = bg_nav.cur_y;
        bg_nav.new_x = bg_nav.cur_x;
        bg_nav.new_y = bg_nav.cur_y;
        bg_nav.transitioning = 0U;
#else
        if (robot_level == ROBOT_LEVEL_ULTIMATE) {
          bg_nav.cur_x = clamp_i(ROBOT_ULTIMATE_BG_X, (int)qspi_bg_asset.x_min, (int)qspi_bg_asset.x_max);
          bg_nav.cur_y = clamp_i(ROBOT_ULTIMATE_BG_Y, (int)qspi_bg_asset.y_min, (int)qspi_bg_asset.y_max);
          bg_nav.old_x = bg_nav.cur_x;
          bg_nav.old_y = bg_nav.cur_y;
          bg_nav.new_x = bg_nav.cur_x;
          bg_nav.new_y = bg_nav.cur_y;
          bg_nav.transitioning = 0U;
        } else {
        if (bg_nav.transitioning && (now - bg_nav.transition_start_ms) >= BG_TRANSITION_MS) {
          bg_nav.cur_x = bg_nav.new_x;
          bg_nav.cur_y = bg_nav.new_y;
          bg_nav.old_x = bg_nav.cur_x;
          bg_nav.old_y = bg_nav.cur_y;
          bg_nav.transitioning = 0U;
        }

        if (!bg_nav.transitioning && (now - bg_nav.last_step_ms) >= BG_MOVE_REPEAT_MS) {
          int step_x = 0;
          int step_y = 0;
          if (strafe_norm > BG_STICK_THRESHOLD) {
            step_x = 1;
          } else if (strafe_norm < -BG_STICK_THRESHOLD) {
            step_x = -1;
          }
          if (fb_norm > BG_STICK_THRESHOLD) {
            step_y = 1;
          } else if (fb_norm < -BG_STICK_THRESHOLD) {
            step_y = -1;
          }

          if (step_x != 0 || step_y != 0) {
            int target_x = clamp_i(bg_nav.cur_x + step_x, (int)qspi_bg_asset.x_min, (int)qspi_bg_asset.x_max);
            int target_y = clamp_i(bg_nav.cur_y + step_y, (int)qspi_bg_asset.y_min, (int)qspi_bg_asset.y_max);
            bg_nav.last_step_ms = now;
            if (target_x != bg_nav.cur_x || target_y != bg_nav.cur_y) {
              bg_nav.old_x = bg_nav.cur_x;
              bg_nav.old_y = bg_nav.cur_y;
              bg_nav.new_x = target_x;
              bg_nav.new_y = target_y;
              bg_nav.transition_start_ms = now;
              bg_nav.transitioning = 1U;
            }
          }
        }
        }
#endif
      }
#endif

#if W25Q_BG_ENABLE
      render_hud_weapon = current_fire_weapon;
      render_hud_ammo = weapon_ammo_remaining;
      render_stage_shots = stage_shots;
      render_stage_hits = stage_hits;
      render_stage_result = stage_result;
      render_dialog_visible = 0U;
      render_menu_visible = 0U;
      render_menu_c = "";
      render_bloodhound_visible = 0U;
      if (ultimate_try_wait_draw && (held_weapon != HELD_WEAPON_KUWU || qiedao_active)) {
        render_dialog_visible = 1U;
        render_dialog_line0 = "TRY THIS";
        render_dialog_line1 = "RETURN TO THE BLADE";
      } else if (ultimate_try_wait_draw && held_weapon == HELD_WEAPON_KUWU && !qiedao_active) {
        render_dialog_visible = 1U;
        render_dialog_line0 = "TRY THIS";
        render_dialog_line1 = "> PEACEKEEPER";
      } else if (stage_advance_wait_knife && (held_weapon != HELD_WEAPON_KUWU || qiedao_active)) {
        render_dialog_visible = 1U;
        render_dialog_line0 = "PREY FALLEN";
        render_dialog_line1 = "RETURN TO THE BLADE";
      } else if (stage_start_wait_draw && held_weapon == HELD_WEAPON_KUWU && !qiedao_active) {
        render_dialog_visible = 1U;
        render_dialog_line0 = stage_brief_line0(robot_level);
        render_dialog_line1 = stage_brief_line1(robot_level);
      } else if (stage_advance_wait_knife && held_weapon == HELD_WEAPON_KUWU && !qiedao_active) {
        RobotLevel next_level = next_robot_level(robot_level);
        render_dialog_visible = 1U;
        render_dialog_line0 = stage_brief_line0(next_level);
        render_dialog_line1 = stage_brief_line1(next_level);
      } else if (game_flow == GAME_FLOW_INTRO_IDLE) {
        render_bloodhound_visible = 1U;
      } else if (game_flow == GAME_FLOW_INTRO) {
        render_dialog_visible = 1U;
        render_bloodhound_visible = 1U;
        render_dialog_line0 = intro_dialog[dialog_page][0];
        render_dialog_line1 = intro_dialog[dialog_page][1];
      } else if (game_flow == GAME_FLOW_RETRY_IDLE) {
        render_bloodhound_visible = 1U;
      } else if (game_flow == GAME_FLOW_RETRY_DIALOG) {
        render_dialog_visible = 1U;
        render_bloodhound_visible = 1U;
        render_dialog_line0 = retry_dialog[dialog_page][0];
        render_dialog_line1 = retry_dialog[dialog_page][1];
      } else if (game_flow == GAME_FLOW_END_DIALOG) {
        render_dialog_visible = 1U;
        render_bloodhound_visible = 1U;
        if (ultimate_keep_training_end) {
          render_dialog_line0 = end_dialog_keep_training[dialog_page][0];
          render_dialog_line1 = end_dialog_keep_training[dialog_page][1];
        } else {
          render_dialog_line0 = end_dialog[dialog_page][0];
          render_dialog_line1 = end_dialog[dialog_page][1];
        }
      } else if (game_flow == GAME_FLOW_END_MENU) {
        render_dialog_visible = 1U;
        render_bloodhound_visible = 1U;
        render_dialog_line0 = "CHOOSE YOUR PATH";
        render_dialog_line1 = "JS SEL BL RLD BW OK";
        render_menu_visible = 1U;
        render_menu_a = "FREE PRACTICE";
        render_menu_b = "GO REST";
        render_menu_selected = end_menu_selected;
      } else if (game_flow == GAME_FLOW_FREE_MENU) {
        render_dialog_visible = 1U;
        render_dialog_line0 = "TRAINING COMPLETE";
        render_dialog_line1 = "CHOOSE YOUR NEXT HUNT";
        render_menu_visible = 1U;
        render_menu_a = "RUN IT BACK";
        render_menu_b = "CHANGE TRIAL";
        render_menu_c = "GO REST";
        render_menu_selected = free_menu_selected;
      } else if (game_flow == GAME_FLOW_FREE_REST) {
        render_dialog_visible = 1U;
        render_dialog_line0 = "REST WELL";
        render_dialog_line1 = "BL TO TRAIN AGAIN";
      } else if (game_flow == GAME_FLOW_SELECT_WEAPON) {
        static const char *weapon_names[] = {"> R99", "> PEACEKEEPER", "> WINGMAN"};
        render_dialog_visible = 1U;
        render_bloodhound_visible = free_select_active ? 0U : 1U;
        render_dialog_line0 = "CHOOSE YOUR WEAPON";
        render_dialog_line1 = weapon_names[weapon_menu_idx];
      } else if (game_flow == GAME_FLOW_SELECT_MOVE) {
        static const char *move_names[] = {"> PATROL TARGET", "> RANDOM TARGET", "> PILOT MOVEMENT"};
        render_dialog_visible = 1U;
        render_bloodhound_visible = free_select_active ? 0U : 1U;
        render_dialog_line0 = "CHOOSE YOUR PREY";
        render_dialog_line1 = move_names[move_menu_idx];
      } else if (game_flow == GAME_FLOW_END_IDLE) {
        render_bloodhound_visible = 1U;
      }
      if (render_bloodhound_visible && qspi_bg_ready) {
        const int grid_x = bg_nav.transitioning ? bg_nav.new_x : bg_nav.cur_x;
        const int grid_y = bg_nav.transitioning ? bg_nav.new_y : bg_nav.cur_y;
        const int npc_bg_x = BLOODHOUND_WORLD_X - grid_x * ROBOT_GRID_X_STEP + grid_y * ROBOT_GRID_Y_TO_X;
        const int npc_bg_y = BLOODHOUND_WORLD_Y - grid_y * ROBOT_GRID_Y_STEP;
        const int max_src_x = (int)qspi_bg_asset.width - BG_VIEW_W;
        const int max_src_y = (int)qspi_bg_asset.height - BG_VIEW_H;
        const int crop_x = clamp_i(bg_src_x_dynamic, 0, max_src_x);
        const int crop_y = clamp_i(bg_src_y_dynamic, 0, max_src_y);
        render_bloodhound_frame = bloodhound_frame_for_grid_y(grid_y);
        const int npc_w = (int)bloodhound_widths[render_bloodhound_frame];
        const int npc_h = (int)bloodhound_heights[render_bloodhound_frame];
        render_bloodhound_x = BG_VIEW_X + npc_bg_x - crop_x - (npc_w / 2);
        render_bloodhound_y = BG_VIEW_Y + npc_bg_y - crop_y - npc_h;
      }
      apex_ws2812_update(now, game_flow, robot_level, robot_armor, robot_health, robot_dead_ms);
#if WEAPON_LAYER_MODE == WEAPON_MODE_KNIFE
      render_hud_visible = (held_weapon == HELD_WEAPON_R99
                            || held_weapon == HELD_WEAPON_RELOAD
                            || held_weapon == HELD_WEAPON_ADS_ANIM
                            || held_weapon == HELD_WEAPON_FIRE_ANIM) ? 1U : 0U;
#else
      render_hud_visible = 1U;
#endif
      RobotRenderState robot = {0};
#if ROBOT_TEST_ENABLE
      if ((game_flow == GAME_FLOW_STAGE_ACTIVE && !ultimate_try_wait_draw && !stage_start_wait_draw && !stage_advance_spawn_after_draw)
          || game_flow == GAME_FLOW_FREE_ACTIVE) {
        robot = robot_make_render_state(&bg_nav, robot_armor, robot_health, now, robot_level, robot_level_start_ms, robot_dead_ms, robot_random_offset);
      }
      ultimate_trail_sample(&robot, now, (uint8_t)((robot_level == ROBOT_LEVEL_ULTIMATE && robot_dead_ms == 0U) ? 1U : 0U));
#endif
      render_w25q_background_frame(&bg_nav, &weapon, &robot);
#else
      render_phase1_frame(yaw_cur, pitch_cur, strafe_cur, fb_cur, &weapon);
#endif

    }
    return MENU_STATE_HOME;
}
#endif







