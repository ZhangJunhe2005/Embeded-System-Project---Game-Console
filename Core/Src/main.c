// Shared console shell for the three-game STM32 console.

#include "main.h"
#include "usart.h"
#include "gpio.h"
#include "adc.h"

#include "Joystick.h"
#include "ST7789V2_Driver.h"
#include "ApexMenuThumb.h"
#include "KuwuIndexed.h"
#include "BloodhoundIndexed.h"
#include "Audio.h"
#include "Buzzer.h"
#include "LCD.h"
#include "InputHandler.h"
#include "Game_1.h"
#include "Game_2.h"
#include "assets.h"
#include "music.h"
#include "Game_3.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

void SystemClock_Config(void);
void PeriphCommonClock_Config(void);
void PowerMonitor_Init(void);
void PowerMonitor_Tick(uint32_t now);

extern Buzzer_cfg_t buzzer_cfg;

#define LCD_Y_OFFSET 0
#define ENABLE_RUNTIME_LOGS 0
#define LCD_BLINK_ONLY_TEST 0
#define W25Q_BG_BLOCKING_LCD_TEST 0
#define MENU_WS2812_TEST_ENABLE 1
#define MENU_WS2812_COUNT 10U
#define MENU_WS2812_BRIGHTNESS 6U
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
#define WS2812_BRIGHTNESS_LIMIT MENU_WS2812_BRIGHTNESS
#define WS2812_DMA_REQUEST_TIM4_CH2 6U

ST7789V2_cfg_t cfg0 = {
    .setup_done = 0,
    .spi = SPI2,
    .RST = {.port = GPIOB, .pin = GPIO_PIN_2},
    .BL = {.port = GPIOC, .pin = GPIO_PIN_7},
    .DC = {.port = GPIOA, .pin = GPIO_PIN_9},
    .CS = {.port = GPIOB, .pin = GPIO_PIN_12},
    .MOSI = {.port = GPIOB, .pin = GPIO_PIN_15},
    .SCLK = {.port = GPIOB, .pin = GPIO_PIN_13},
    .dma = {.instance = DMA1, .channel = DMA1_Channel5}
};

Joystick_cfg_t joystick_cfg = {
    .adc = &hadc1,
    .x_channel = ADC_CHANNEL_1,
    .y_channel = ADC_CHANNEL_2,
    .sampling_time = ADC_SAMPLETIME_47CYCLES_5,
    .center_x = JOYSTICK_DEFAULT_CENTER_X,
    .center_y = JOYSTICK_DEFAULT_CENTER_Y,
    .deadzone = JOYSTICK_DEADZONE,
    .setup_done = 0
};

Joystick_t joystick_data;

Joystick_cfg_t joystick2_cfg = {
    .adc = &hadc1,
    .x_channel = ADC_CHANNEL_3,
    .y_channel = ADC_CHANNEL_4,
    .sampling_time = ADC_SAMPLETIME_47CYCLES_5,
    .center_x = JOYSTICK_DEFAULT_CENTER_X,
    .center_y = JOYSTICK_DEFAULT_CENTER_Y,
    .deadzone = JOYSTICK_DEADZONE,
    .setup_done = 0
};

Joystick_t joystick2_data;

static uint8_t game2_lcd_work_ram[LCD_WORK_BUFFER_BYTES] __attribute__((section(".ram2_bss"), aligned(4)));
static uint16_t line_buffer0[ST7789V2_WIDTH];
static uint16_t ws2812_pwm_dma[WS2812_PWM_DMA_LEN];

static uint16_t swap16(uint16_t v);
static int clamp_i(int v, int lo, int hi);
static int abs_i(int v);
static void lcd_wait_dma_spi_idle(void);
static void external_led_strip_idle(void);
static void external_led_strip_menu_test(uint32_t now);
static void external_sevenseg_idle(void);
static void main_menu_sevenseg_init(void);
static void main_menu_sevenseg_display_digit(uint8_t digit, uint8_t dp_on);

static void perf_counter_init(void)
{
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0U;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

static int clamp_i(int v, int lo, int hi)
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
  return (v > 0) - (v < 0);
}

static uint16_t swap16(uint16_t v)
{
  return (uint16_t)((v << 8) | (v >> 8));
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

static int hud_text_width(const char *s, int scale)
{
  int n = 0;
  while (s && s[n] != '\0') {
    n++;
  }
  return (n <= 0) ? 0 : (((n * 4) - 1) * scale);
}

typedef enum {
  MAIN_MENU_APEX = 0,
  MAIN_MENU_GAME2,
  MAIN_MENU_GAME3,
  MAIN_MENU_COUNT,
} MainMenuSelection;

static uint8_t active_low_pressed(GPIO_TypeDef *port, uint16_t pin)
{
  return (HAL_GPIO_ReadPin(port, pin) == GPIO_PIN_RESET) ? 1U : 0U;
}

static uint8_t active_low_pressed_debounced(GPIO_TypeDef *port, uint16_t pin, uint32_t debounce_ms)
{
  uint8_t first = active_low_pressed(port, pin);
  HAL_Delay(debounce_ms);
  return (first && active_low_pressed(port, pin)) ? 1U : 0U;
}

static void menu_line_fill(uint16_t *dst, int x0, int x1, uint16_t color)
{
  if (x0 > x1) {
    int t = x0;
    x0 = x1;
    x1 = t;
  }
  if (x1 < 0 || x0 >= ST7789V2_WIDTH) {
    return;
  }
  if (x0 < 0) {
    x0 = 0;
  }
  if (x1 >= ST7789V2_WIDTH) {
    x1 = ST7789V2_WIDTH - 1;
  }
  for (int x = x0; x <= x1; x++) {
    dst[x] = color;
  }
}

static void menu_line_rect(uint16_t *dst, int y, int x0, int y0, int w, int h, uint16_t color)
{
  if (w <= 0 || h <= 0 || y < y0 || y >= (y0 + h)) {
    return;
  }
  menu_line_fill(dst, x0, x0 + w - 1, color);
}

static void menu_line_frame(uint16_t *dst, int y, int x0, int y0, int w, int h, uint16_t color)
{
  if (w <= 0 || h <= 0 || y < y0 || y >= (y0 + h)) {
    return;
  }
  if (y == y0 || y == (y0 + h - 1)) {
    menu_line_fill(dst, x0, x0 + w - 1, color);
  } else {
    menu_line_fill(dst, x0, x0, color);
    menu_line_fill(dst, x0 + w - 1, x0 + w - 1, color);
  }
}

static void menu_line_circle(uint16_t *dst, int y, int cx, int cy, int r, uint16_t color, uint8_t fill)
{
  int dy = y - cy;
  int dx = r;

  if (dy < -r || dy > r || r <= 0) {
    return;
  }
  while (dx > 0 && (dx * dx + dy * dy) > (r * r)) {
    dx--;
  }
  if (fill) {
    menu_line_fill(dst, cx - dx, cx + dx, color);
  } else {
    menu_line_fill(dst, cx - dx, cx - dx, color);
    menu_line_fill(dst, cx + dx, cx + dx, color);
  }
}

static void menu_line_diag(uint16_t *dst, int y, int x0, int y0, int x1, int y1, int width, uint16_t color)
{
  int x;
  if (y0 == y1) {
    if (y == y0) {
      menu_line_fill(dst, x0, x1, color);
    }
    return;
  }
  if ((y < y0 && y < y1) || (y > y0 && y > y1)) {
    return;
  }
  x = x0 + ((x1 - x0) * (y - y0)) / (y1 - y0);
  menu_line_fill(dst, x - width, x + width, color);
}

static const uint8_t menu_g3_knight_silk_sprite[16 * 16] = {
  255, 255, 255, 255, 255,  13,  13,  13,  13,  13,  13, 255, 255, 255, 255, 255,
  255, 255, 255, 255,  13,  13,   1,   1,   1,   1,  13,  13, 255, 255, 255, 255,
  255, 255, 255,  13,  13,   6,   6,   1,   1,   6,   6,  13,  13, 255, 255, 255,
  255, 255, 255,  13,   6,   6,   1,   6,   6,   1,   6,   6,  13, 255, 255, 255,
  255, 255, 255,  13,   6,   6,   6,   6,   6,   6,   6,   6,  13, 255, 255, 255,
  255, 255, 255, 255,  13,  13,   6,   6,   6,   6,  13,  13, 255, 255, 255, 255,
  255, 255, 255,  14,  14,  13,  13,  14,  14,  13,  13,  14,  14, 255, 255, 255,
  255, 255,  14,  14,   1,  14,  14,  14,  14,  14,   1,  14,  14,  14, 255, 255,
  255, 255,  14,  14,  14,  14,  13,  14,  14,  13,  14,  14,  14,  14, 255, 255,
  255, 255,  13,  14,  14,  14,  14,  14,  14,  14,  14,  14,  14,  13, 255, 255,
  255, 255, 255,  13,  14,  14,  14,  14,  14,  14,  14,  14,  13, 255, 255, 255,
  255, 255, 255, 255,  13,  14,  14,  13,  13,  14,  14,  13, 255, 255, 255, 255,
  255, 255, 255, 255,  13,  13, 255, 255, 255, 255,  13,  13, 255, 255, 255, 255,
  255, 255, 255,  13,  13, 255, 255, 255, 255, 255, 255,  13,  13, 255, 255, 255,
  255, 255, 255,   0,   0, 255, 255, 255, 255, 255, 255,   0,   0, 255, 255, 255,
  255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255
};

static const uint8_t menu_g3_enemy_sprite[16 * 16] = {
  255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
  255, 255, 255, 255,  13, 255, 255, 255, 255, 255, 255,  13, 255, 255, 255, 255,
  255, 255, 255,  13,   2,  13, 255, 255, 255, 255,  13,   2,  13, 255, 255, 255,
  255, 255, 255, 255,  13,   2,   2,   2,   2,   2,   2,  13, 255, 255, 255, 255,
  255, 255, 255,  13,   2,   2,   1,   2,   2,   1,   2,   2,  13, 255, 255, 255,
  255, 255,  13,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,  13, 255, 255,
  255, 255,  13,   2,  13,   2,   2,   5,   5,   2,   2,  13,   2,  13, 255, 255,
  255, 255, 255,  13,   2,   2,   2,   2,   2,   2,   2,   2,  13, 255, 255, 255,
  255, 255, 255, 255,  13,   2,   2,  13,  13,   2,   2,  13, 255, 255, 255, 255,
  255, 255, 255,  13,   2,   2, 255, 255, 255, 255,   2,   2,  13, 255, 255, 255,
  255, 255,  13,   2,   2, 255, 255, 255, 255, 255, 255,   2,   2,  13, 255, 255,
  255, 255,   0,   0, 255, 255, 255, 255, 255, 255, 255, 255,   0,   0, 255, 255,
  255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
  255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
  255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
  255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255
};

static uint16_t menu_lcd_index_to_color(uint8_t pixel)
{
  static const uint16_t lut[16] = {
    LCD_COLOUR_0,  LCD_COLOUR_1,  LCD_COLOUR_2,  LCD_COLOUR_3,
    LCD_COLOUR_4,  LCD_COLOUR_5,  LCD_COLOUR_6,  LCD_COLOUR_7,
    LCD_COLOUR_8,  LCD_COLOUR_9,  LCD_COLOUR_10, LCD_COLOUR_11,
    LCD_COLOUR_12, LCD_COLOUR_13, LCD_COLOUR_14, LCD_COLOUR_15,
  };
  return lut[pixel & 0x0FU];
}

static void menu_draw_rgb565_sprite_resized_line(uint16_t *dst,
                                                 int y,
                                                 int x0,
                                                 int y0,
                                                 int src_w,
                                                 int src_h,
                                                 const uint8_t *sprite,
                                                 int out_w,
                                                 int out_h,
                                                 uint16_t transparent)
{
  if (sprite == 0 || src_w <= 0 || src_h <= 0 || out_w <= 0 || out_h <= 0) {
    return;
  }
  int local_y = y - y0;
  if (local_y < 0 || local_y >= out_h) {
    return;
  }
  int sy = (local_y * src_h) / out_h;
  for (int ox = 0; ox < out_w; ox++) {
    int sx = (ox * src_w) / out_w;
    uint32_t idx = (uint32_t)((sy * src_w) + sx) * 2U;
    uint16_t color = (uint16_t)(((uint16_t)sprite[idx] << 8) | sprite[idx + 1U]);
    if (color == transparent) {
      continue;
    }
    int x = x0 + ox;
    if (x >= 0 && x < ST7789V2_WIDTH) {
      dst[x] = swap16(color);
    }
  }
}

static void menu_draw_indexed_sprite_line(uint16_t *dst,
                                          int y,
                                          int x0,
                                          int y0,
                                          int src_w,
                                          int src_h,
                                          const uint8_t *sprite,
                                          uint8_t scale,
                                          uint8_t transparent)
{
  if (scale == 0U || sprite == 0) {
    return;
  }
  int local_y = y - y0;
  if (local_y < 0 || local_y >= src_h * (int)scale) {
    return;
  }
  int sy = local_y / (int)scale;
  for (int sx = 0; sx < src_w; sx++) {
    uint8_t pixel = sprite[(uint32_t)(sy * src_w) + (uint32_t)sx];
    if (pixel == transparent) {
      continue;
    }
    for (uint8_t dx = 0; dx < scale; dx++) {
      int x = x0 + sx * (int)scale + dx;
      if (x >= 0 && x < ST7789V2_WIDTH) {
        dst[x] = menu_lcd_index_to_color(pixel);
      }
    }
  }
}

static void menu_draw_indexed_sprite_resized_line(uint16_t *dst,
                                                  int y,
                                                  int x0,
                                                  int y0,
                                                  int src_w,
                                                  int src_h,
                                                  const uint8_t *sprite,
                                                  int out_w,
                                                  int out_h,
                                                  uint8_t transparent)
{
  if (sprite == 0 || src_w <= 0 || src_h <= 0 || out_w <= 0 || out_h <= 0) {
    return;
  }
  int local_y = y - y0;
  if (local_y < 0 || local_y >= out_h) {
    return;
  }
  int sy = (local_y * src_h) / out_h;
  for (int ox = 0; ox < out_w; ox++) {
    int sx = (ox * src_w) / out_w;
    uint8_t pixel = sprite[(uint32_t)(sy * src_w) + (uint32_t)sx];
    if (pixel == transparent) {
      continue;
    }
    int x = x0 + ox;
    if (x >= 0 && x < ST7789V2_WIDTH) {
      dst[x] = menu_lcd_index_to_color(pixel);
    }
  }
}

static void menu_draw_rgb565_u16_resized_line(uint16_t *dst,
                                              int y,
                                              int x0,
                                              int y0,
                                              int src_w,
                                              int src_h,
                                              const uint16_t *sprite,
                                              int out_w,
                                              int out_h)
{
  if (sprite == 0 || src_w <= 0 || src_h <= 0 || out_w <= 0 || out_h <= 0) {
    return;
  }
  int local_y = y - y0;
  if (local_y < 0 || local_y >= out_h) {
    return;
  }
  int sy = (local_y * src_h) / out_h;
  for (int ox = 0; ox < out_w; ox++) {
    int sx = (ox * src_w) / out_w;
    int x = x0 + ox;
    if (x >= 0 && x < ST7789V2_WIDTH) {
      dst[x] = sprite[(uint32_t)sy * (uint32_t)src_w + (uint32_t)sx];
    }
  }
}

static void menu_draw_indexed_lut_resized_line_clipped(uint16_t *dst,
                                                       int y,
                                                       int x0,
                                                       int y0,
                                                       int src_w,
                                                       int src_h,
                                                       const uint8_t *sprite,
                                                       const uint16_t *lut565,
                                                       int out_w,
                                                       int out_h,
                                                       uint8_t transparent,
                                                       int clip_x0,
                                                       int clip_x1)
{
  if (sprite == 0 || lut565 == 0 || src_w <= 0 || src_h <= 0 || out_w <= 0 || out_h <= 0) {
    return;
  }
  if (clip_x0 < 0) {
    clip_x0 = 0;
  }
  if (clip_x1 >= ST7789V2_WIDTH) {
    clip_x1 = ST7789V2_WIDTH - 1;
  }
  if (clip_x0 > clip_x1) {
    return;
  }
  int local_y = y - y0;
  if (local_y < 0 || local_y >= out_h) {
    return;
  }
  int sy = (local_y * src_h) / out_h;
  for (int ox = 0; ox < out_w; ox++) {
    int sx = (ox * src_w) / out_w;
    uint8_t pixel = sprite[(uint32_t)sy * (uint32_t)src_w + (uint32_t)sx];
    int x = x0 + ox;
    if (pixel != transparent && x >= clip_x0 && x <= clip_x1) {
      dst[x] = swap16(lut565[pixel]);
    }
  }
}

static void menu_draw_indexed_lut_resized_line(uint16_t *dst,
                                               int y,
                                               int x0,
                                               int y0,
                                               int src_w,
                                               int src_h,
                                               const uint8_t *sprite,
                                               const uint16_t *lut565,
                                               int out_w,
                                               int out_h,
                                               uint8_t transparent)
{
  menu_draw_indexed_lut_resized_line_clipped(dst,
                                            y,
                                            x0,
                                            y0,
                                            src_w,
                                            src_h,
                                            sprite,
                                            lut565,
                                            out_w,
                                            out_h,
                                            transparent,
                                            0,
                                            ST7789V2_WIDTH - 1);
}

static void menu_draw_apex_preview_line(uint16_t *dst, int y, int px, int py, int pw, int ph)
{
  const uint8_t bloodhound_frame = 0U;
  const int bloodhound_w = (int)bloodhound_widths[bloodhound_frame];
  const int bloodhound_h = (int)bloodhound_heights[bloodhound_frame];
  const int bloodhound_out_h = 72;
  const int bloodhound_out_w = (bloodhound_w * bloodhound_out_h) / bloodhound_h;
  const int bloodhound_x = px + 132;
  const int bloodhound_y = py + 75 - bloodhound_out_h;
  const int kuwu_out_w = 150;
  const int kuwu_out_h = ((int)kuwu_height * kuwu_out_w) / (int)kuwu_width;
  const int kuwu_x = px + pw - kuwu_out_w + 10;
  const int kuwu_y = py + ph - kuwu_out_h - 2;

  if (y < py || y >= py + ph) {
    return;
  }
  menu_draw_indexed_lut_resized_line(dst,
                                     y,
                                     px,
                                     py,
                                     (int)APEX_MENU_THUMB_W,
                                     (int)APEX_MENU_THUMB_H,
                                     apex_menu_thumb_idx8,
                                     apex_menu_thumb_lut565,
                                     pw,
                                     ph,
                                     APEX_MENU_THUMB_TRANSPARENT_INDEX);
  menu_draw_indexed_lut_resized_line(dst,
                                     y,
                                     bloodhound_x,
                                     bloodhound_y,
                                     bloodhound_w,
                                     bloodhound_h,
                                     bloodhound_frames[bloodhound_frame],
                                     bloodhound_lut565,
                                     bloodhound_out_w,
                                     bloodhound_out_h,
                                     BLOODHOUND_TRANSPARENT_INDEX);
  menu_draw_indexed_lut_resized_line_clipped(dst,
                                             y,
                                             kuwu_x,
                                             kuwu_y,
                                             (int)kuwu_width,
                                             (int)kuwu_height,
                                             kuwu_idx8,
                                             kuwu_lut565,
                                             kuwu_out_w,
                                             kuwu_out_h,
                                             KUWU_TRANSPARENT_INDEX,
                                             px,
                                             px + pw - 1);
}

static void menu_draw_racing_preview_line(uint16_t *dst, int y, int px, int py, int pw, int ph)
{
  const uint16_t sky = swap16(0x867DU);
  const uint16_t grass = swap16(0x2B26U);
  const uint16_t asphalt = swap16(0x0000U);
  const uint16_t lane = swap16(0xFFFFU);
  const uint16_t shoulder_yellow = swap16(0xFFE0U);
  const uint16_t shoulder_white = swap16(0xFFFFU);
  const uint16_t cloud = swap16(0xFFFFU);
  const uint16_t cloud_shadow = swap16(0xC618U);
  const uint16_t tree_leaf = swap16(0x05E0U);
  const uint16_t tree_trunk = swap16(0x8A22U);
  const int horizon = py + 32;
  const int bottom = py + ph - 1;
  const int cx = px + pw / 2;
  const int road_max_depth = bottom - horizon;
  const int car_w = (int)GAME2_CAR_SPR_W / 2;
  const int car_h = (int)GAME2_CAR_SPR_H / 2;
  const int sign_w = (int)GAME2_OBSTACLE_SPR_W / 2;
  const int sign_h = (int)GAME2_OBSTACLE_SPR_H / 2;
  const int boost_y = horizon + 22;
  const int hazard_y = horizon + 34;
  const int boost_half = 8 + ((boost_y - horizon) * 32) / road_max_depth;
  const int hazard_half = 8 + ((hazard_y - horizon) * 32) / road_max_depth;
  const int boost_x = (cx - boost_half) + ((boost_half * 2) / 8) - (sign_w / 2);
  const int hazard_x = (cx - hazard_half) + ((hazard_half * 2 * 5) / 8) - (sign_w / 2);

  if (y < py || y >= py + ph) {
    return;
  }
  menu_line_rect(dst, y, px, py, pw, horizon - py, sky);
  menu_line_rect(dst, y, px, horizon, pw, bottom - horizon + 1, grass);
  menu_line_circle(dst, y, px + 164, py + 17, 10, swap16(0xFFE0U), 1);

  menu_line_circle(dst, y, px + 34, py + 16, 7, cloud, 1);
  menu_line_circle(dst, y, px + 43, py + 13, 9, cloud, 1);
  menu_line_circle(dst, y, px + 53, py + 17, 6, cloud_shadow, 1);
  menu_line_rect(dst, y, px + 32, py + 18, 28, 5, cloud);
  menu_line_circle(dst, y, px + 128, py + 22, 5, cloud, 1);
  menu_line_circle(dst, y, px + 136, py + 19, 7, cloud, 1);
  menu_line_circle(dst, y, px + 145, py + 22, 5, cloud_shadow, 1);
  menu_line_rect(dst, y, px + 126, py + 23, 25, 4, cloud);

  menu_line_rect(dst, y, px + 27, horizon - 3, 3, 13, tree_trunk);
  menu_line_circle(dst, y, px + 28, horizon - 7, 8, tree_leaf, 1);
  menu_line_rect(dst, y, px + 54, horizon - 2, 3, 11, tree_trunk);
  menu_line_circle(dst, y, px + 55, horizon - 6, 7, tree_leaf, 1);
  menu_line_rect(dst, y, px + 172, horizon - 3, 3, 13, tree_trunk);
  menu_line_circle(dst, y, px + 173, horizon - 7, 8, tree_leaf, 1);
  menu_line_rect(dst, y, px + 190, horizon - 2, 3, 10, tree_trunk);
  menu_line_circle(dst, y, px + 191, horizon - 6, 6, tree_leaf, 1);

  if (y >= horizon) {
    int depth = y - horizon;
    int drive_half = 8 + (depth * 32) / road_max_depth;
    int drive_left = cx - drive_half;
    int drive_right = cx + drive_half;
    int left = drive_left - 6;
    int right = drive_right + 6;
    if (left < px + 8) left = px + 8;
    if (right > px + pw - 9) right = px + pw - 9;

    menu_line_fill(dst, left, right, asphalt);
    menu_line_fill(dst, left, left + 1, shoulder_yellow);
    menu_line_fill(dst, right - 1, right, shoulder_yellow);
    menu_line_fill(dst, drive_left - 1, drive_left, shoulder_white);
    menu_line_fill(dst, drive_right, drive_right + 1, shoulder_white);

    if ((depth % 18) < 9) {
      int road_w = drive_right - drive_left;
      for (int i = 1; i <= 3; i++) {
        int lx = drive_left + (road_w * i) / 4;
        menu_line_fill(dst, lx, lx, lane);
      }
    }
  }

  menu_draw_indexed_sprite_resized_line(dst,
                                        y,
                                        px + 18,
                                        horizon + 30,
                                        (int)GAME2_HOUSE_SPR_W,
                                        (int)GAME2_HOUSE_SPR_H,
                                        roadside_house_red_sprite,
                                        32,
                                        32,
                                        255U);
  menu_draw_indexed_sprite_resized_line(dst,
                                        y,
                                        px + 154,
                                        horizon + 24,
                                        (int)GAME2_HOUSE_SPR_W,
                                        (int)GAME2_HOUSE_SPR_H,
                                        roadside_house_blue_sprite,
                                        32,
                                        32,
                                        255U);
  menu_draw_indexed_sprite_resized_line(dst,
                                        y,
                                        px + 38,
                                        horizon + 3,
                                        (int)GAME2_HOUSE_SPR_W,
                                        (int)GAME2_HOUSE_SPR_H,
                                        roadside_house_shop_sprite,
                                        32,
                                        32,
                                        255U);

  menu_draw_indexed_sprite_resized_line(dst,
                                        y,
                                        boost_x,
                                        boost_y,
                                        (int)GAME2_OBSTACLE_SPR_W,
                                        (int)GAME2_OBSTACLE_SPR_H,
                                        obstacle_boost_sprite,
                                        sign_w,
                                        sign_h,
                                        255U);
  menu_draw_indexed_sprite_resized_line(dst,
                                        y,
                                        hazard_x,
                                        hazard_y,
                                        (int)GAME2_OBSTACLE_SPR_W,
                                        (int)GAME2_OBSTACLE_SPR_H,
                                        obstacle_yellow_sprite,
                                        sign_w,
                                        sign_h,
                                        255U);
  menu_draw_rgb565_sprite_resized_line(dst,
                                       y,
                                       px + (pw - car_w) / 2,
                                       py + ph - car_h - 4,
                                       (int)GAME2_CAR_SPR_W,
                                       (int)GAME2_CAR_SPR_H,
                                       car_center_sprite_rgb565,
                                       car_w,
                                       car_h,
                                       0x0000U);
}

static void menu_draw_knight_preview_line(uint16_t *dst, int y, int px, int py, int pw, int ph)
{
  const uint16_t sky = swap16(0x6B7DU);
  const uint16_t cave = swap16(0x2104U);
  const uint16_t platform = swap16(0x7BEFU);
  const uint16_t platform_hi = swap16(0xBDF7U);
  const uint16_t flag = swap16(0x07E0U);
  const int ground_y = py + ph - 26;

  if (y < py || y >= py + ph) {
    return;
  }
  menu_line_rect(dst, y, px, py, pw, ph, sky);
  menu_line_circle(dst, y, px + 33, py + 24, 13, swap16(0xFFFFU), 1);
  menu_line_rect(dst, y, px, ground_y, pw, py + ph - ground_y, cave);
  menu_line_rect(dst, y, px, ground_y, pw, 5, platform_hi);
  menu_line_rect(dst, y, px + 20, py + 61, 48, 10, platform);
  menu_line_rect(dst, y, px + 112, py + 50, 46, 10, platform);
  menu_line_rect(dst, y, px + 20, py + 61, 48, 3, platform_hi);
  menu_line_rect(dst, y, px + 112, py + 50, 46, 3, platform_hi);

  menu_draw_indexed_sprite_line(dst,
                                y,
                                px + 54,
                                ground_y - 32,
                                16,
                                16,
                                menu_g3_knight_silk_sprite,
                                2U,
                                255U);
  menu_line_rect(dst, y, px + 84, ground_y - 18, 19, 3, swap16(0xFFFFU));
  menu_draw_indexed_sprite_line(dst,
                                y,
                                px + 118,
                                ground_y - 24,
                                16,
                                16,
                                menu_g3_enemy_sprite,
                                2U,
                                255U);

  menu_line_rect(dst, y, px + 174, ground_y - 46, 3, 46, swap16(0xFFFFU));
  menu_line_rect(dst, y, px + 177, ground_y - 45, 20, 13, flag);
  for (int i = 0; i < 5; i++) {
    menu_line_rect(dst, y, px + 10 + i * 10, py + 8, 7, 5, i < 4 ? swap16(0xF800U) : swap16(0x4A49U));
  }
}

static void menu_draw_preview_line(uint16_t *dst, int y, uint8_t selected)
{
  const int px = 18;
  const int py = 42;
  const int pw = 204;
  const int ph = 104;
  const uint16_t border = swap16(0xBDF7U);
  const uint16_t shadow = swap16(0x1082U);

  menu_line_rect(dst, y, px + 4, py + 5, pw, ph, shadow);
  menu_line_frame(dst, y, px - 1, py - 1, pw + 2, ph + 2, border);
  if (selected == MAIN_MENU_APEX) {
    menu_draw_apex_preview_line(dst, y, px, py, pw, ph);
  } else if (selected == MAIN_MENU_GAME2) {
    menu_draw_racing_preview_line(dst, y, px, py, pw, ph);
  } else {
    menu_draw_knight_preview_line(dst, y, px, py, pw, ph);
  }
  menu_line_frame(dst, y, px, py, pw, ph, swap16(0x0000U));
}

static int menu_title_x(const char *s, int center_x, int scale)
{
  return center_x - hud_text_width(s, scale) / 2;
}

static void menu_render_screen(uint8_t selected)
{
  static const char *items[MAIN_MENU_COUNT] = {
    "APEX TRAINING",
    "TURBO OVERDRIVE",
    "SILK KNIGHT",
  };
  static const int title_centers[MAIN_MENU_COUNT] = {40, 120, 200};
  const uint16_t bg0 = swap16(0x0000U);
  const uint16_t bg1 = swap16(0x0841U);
  const uint16_t title = swap16(0xBDF7U);
  const uint16_t sel = swap16(0xFFE0U);
  const uint16_t dim = swap16(0x6B4DU);
  const uint16_t rail = swap16(0x31A6U);
  const uint16_t knob = swap16(0x07E0U);

  for (int y = 0; y < ST7789V2_HEIGHT; y++) {
    for (int x = 0; x < ST7789V2_WIDTH; x++) {
      line_buffer0[x] = (y < 160) ? bg0 : bg1;
    }

    overlay_hud_text_line(line_buffer0, y, 42, 12, "GAME CONSOLE", 3, title);
    overlay_hud_text_line(line_buffer0, y, 16, 29, "STICK LEFT/RIGHT", 1, dim);
    overlay_hud_text_line(line_buffer0, y, 176, 29, "BW OK", 1, dim);
    menu_draw_preview_line(line_buffer0, y, selected);

    for (uint8_t i = 0; i < MAIN_MENU_COUNT; i++) {
      uint16_t color = (i == selected) ? sel : dim;
      int tx = menu_title_x(items[i], title_centers[i], 1);
      overlay_hud_text_line(line_buffer0, y, tx, 166, items[i], 1, color);
      if (i == selected) {
        menu_line_rect(line_buffer0, y, title_centers[i] - 22, 177, 44, 3, sel);
      }
    }

    overlay_hud_text_line(line_buffer0, y, menu_title_x(items[selected], 120, 2), 190, items[selected], 2, title);
    menu_line_rect(line_buffer0, y, 42, 219, 156, 4, rail);
    for (uint8_t i = 0; i < MAIN_MENU_COUNT; i++) {
      menu_line_circle(line_buffer0, y, 42 + i * 78, 221, 5, (i == selected) ? knob : dim, 1);
    }
    menu_line_rect(line_buffer0, y, 42 + selected * 78 - 12, 228, 24, 5, knob);

    uint16_t lcd_y = (uint16_t)(y + LCD_Y_OFFSET);
    lcd_wait_dma_spi_idle();
    ST7789V2_Set_Address_Window(&cfg0, 0, lcd_y, 239, lcd_y);
    ST7789V2_Send_Command(&cfg0, ST7789_RAMWR);
#if W25Q_BG_BLOCKING_LCD_TEST
    lcd_send_data_block_cpu((uint8_t*)line_buffer0, ST7789V2_WIDTH * 2);
#else
    ST7789V2_Send_Data_Block(&cfg0, (uint8_t*)line_buffer0, ST7789V2_WIDTH * 2);
#endif
    lcd_wait_dma_spi_idle();
  }
}

static MainMenuSelection main_menu_run(void)
{
  uint8_t selected = MAIN_MENU_APEX;
  Direction last_direction = CENTRE;
  uint8_t last_btn8 = active_low_pressed(BTN8_GPIO_Port, BTN8_Pin);
  uint32_t last_render_ms = 0U;

  Audio_Stop();
  Game2Music_Stop();
  main_menu_sevenseg_init();
  main_menu_sevenseg_display_digit((uint8_t)(selected + 1U), 0U);

  while (1) {
    uint32_t now = HAL_GetTick();
    PowerMonitor_Tick(now);
    external_led_strip_menu_test(now);
    Joystick_Read(&joystick_cfg, &joystick_data);
    Direction dir = joystick_data.direction;
    uint8_t go_left = (uint8_t)(dir == W || dir == NW || dir == SW);
    uint8_t go_right = (uint8_t)(dir == E || dir == NE || dir == SE);
    uint8_t was_left = (uint8_t)(last_direction == W || last_direction == NW || last_direction == SW);
    uint8_t was_right = (uint8_t)(last_direction == E || last_direction == NE || last_direction == SE);

    if (go_right && !was_right) {
      selected = (uint8_t)((selected + 1U) % MAIN_MENU_COUNT);
      main_menu_sevenseg_display_digit((uint8_t)(selected + 1U), 0U);
      Game2Music_PlayMenuMove();
      last_render_ms = 0U;
    } else if (go_left && !was_left) {
      selected = (selected == 0U) ? (MAIN_MENU_COUNT - 1U) : (uint8_t)(selected - 1U);
      main_menu_sevenseg_display_digit((uint8_t)(selected + 1U), 0U);
      Game2Music_PlayMenuMove();
      last_render_ms = 0U;
    }
    last_direction = dir;

    uint8_t btn8 = active_low_pressed(BTN8_GPIO_Port, BTN8_Pin);
    if (btn8 && !last_btn8 && active_low_pressed_debounced(BTN8_GPIO_Port, BTN8_Pin, 30U)) {
      uint32_t audio_restore_deadline;
      main_menu_sevenseg_display_digit((uint8_t)(selected + 1U), 1U);
      Game2Music_PlayMenuConfirm();
      while (active_low_pressed(BTN8_GPIO_Port, BTN8_Pin)) {
        HAL_Delay(5);
      }
      audio_restore_deadline = HAL_GetTick() + 120U;
      while (Game2Music_IsPlaying() && ((int32_t)(HAL_GetTick() - audio_restore_deadline) < 0)) {
        HAL_Delay(2);
      }
      Game2Music_Stop();
      return (MainMenuSelection)selected;
    }
    last_btn8 = btn8;

    if (last_render_ms == 0U || (now - last_render_ms) >= 80U) {
      menu_render_screen(selected);
      last_render_ms = now;
    }
    HAL_Delay(10);
  }
}

static void menu_show_unimplemented(MainMenuSelection selected)
{
  const char *title_text = (selected == MAIN_MENU_GAME2) ? "GAME 2" : "GAME 3";
  const uint16_t bg = swap16(0x0000U);
  const uint16_t title = swap16(0xBDF7U);
  const uint16_t dim = swap16(0x6B4DU);

  for (int y = 0; y < ST7789V2_HEIGHT; y++) {
    for (int x = 0; x < ST7789V2_WIDTH; x++) {
      line_buffer0[x] = bg;
    }
    overlay_hud_text_line(line_buffer0, y, 72, 70, title_text, 4, title);
    overlay_hud_text_line(line_buffer0, y, 48, 122, "NOT READY", 3, dim);
    overlay_hud_text_line(line_buffer0, y, 66, 210, "BW BACK", 2, dim);

    uint16_t lcd_y = (uint16_t)(y + LCD_Y_OFFSET);
    lcd_wait_dma_spi_idle();
    ST7789V2_Set_Address_Window(&cfg0, 0, lcd_y, 239, lcd_y);
    ST7789V2_Send_Command(&cfg0, ST7789_RAMWR);
#if W25Q_BG_BLOCKING_LCD_TEST
    lcd_send_data_block_cpu((uint8_t*)line_buffer0, ST7789V2_WIDTH * 2);
#else
    ST7789V2_Send_Data_Block(&cfg0, (uint8_t*)line_buffer0, ST7789V2_WIDTH * 2);
#endif
    lcd_wait_dma_spi_idle();
  }

  while (active_low_pressed(BTN8_GPIO_Port, BTN8_Pin)) {
    HAL_Delay(5);
  }
  while (!active_low_pressed(BTN8_GPIO_Port, BTN8_Pin)) {
    HAL_Delay(10);
  }
  while (active_low_pressed(BTN8_GPIO_Port, BTN8_Pin)) {
    HAL_Delay(5);
  }
}


static void lcd_wait_dma_spi_idle(void)
{
  if (!cfg0.setup_done || cfg0.dma.channel == NULL || cfg0.spi == NULL) {
    return;
  }
  while (cfg0.dma.channel->CNDTR != 0U) {
  }
  while (cfg0.spi->SR & SPI_SR_BSY) {
  }
}

static void external_led_strip_idle(void)
{
  GPIO_InitTypeDef gi = {0};
  __HAL_RCC_GPIOB_CLK_ENABLE();
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7, GPIO_PIN_RESET);
  gi.Pin = GPIO_PIN_7;
  gi.Mode = GPIO_MODE_OUTPUT_PP;
  gi.Pull = GPIO_PULLDOWN;
  gi.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &gi);
}

static void external_led_strip_pwm_pin_init(void)
{
  GPIO_InitTypeDef gi = {0};
  __HAL_RCC_GPIOB_CLK_ENABLE();
  gi.Pin = GPIO_PIN_7;
  gi.Mode = GPIO_MODE_AF_PP;
  gi.Pull = GPIO_NOPULL;
  gi.Speed = GPIO_SPEED_FREQ_HIGH;
  gi.Alternate = GPIO_AF2_TIM4;
  HAL_GPIO_Init(GPIOB, &gi);
}

static void external_led_strip_pwm_timer_init(void)
{
  __HAL_RCC_TIM4_CLK_ENABLE();

  TIM4->CR1 = 0U;
  TIM4->CR2 = 0U;
  TIM4->SMCR = 0U;
  TIM4->DIER = 0U;
  TIM4->CCER &= ~TIM_CCER_CC2E;
  TIM4->PSC = 0U;
  TIM4->ARR = WS2812_PWM_PERIOD_TICKS;
  TIM4->CCR2 = 0U;
  TIM4->CCMR1 &= ~(TIM_CCMR1_CC2S | TIM_CCMR1_OC2M | TIM_CCMR1_OC2PE);
  TIM4->CCMR1 |= (TIM_CCMR1_OC2M_1 | TIM_CCMR1_OC2M_2 | TIM_CCMR1_OC2PE);
  TIM4->CCER &= ~TIM_CCER_CC2P;
  TIM4->EGR = TIM_EGR_UG;
}

static void external_led_strip_dma_stop(void)
{
  TIM4->DIER &= ~TIM_DIER_CC2DE;
  TIM4->CCER &= ~TIM_CCER_CC2E;
  TIM4->CR1 &= ~TIM_CR1_CEN;
  DMA1_Channel4->CCR &= ~DMA_CCR_EN;
  DMA1->IFCR = DMA_IFCR_CGIF4 | DMA_IFCR_CTCIF4 | DMA_IFCR_CHTIF4 | DMA_IFCR_CTEIF4;
  TIM4->CCR2 = 0U;
  external_led_strip_idle();
}

static void external_led_strip_append_byte(uint32_t *idx, uint8_t value)
{
  for (int bit = 7; bit >= 0; bit--) {
    ws2812_pwm_dma[(*idx)++] = (value & (uint8_t)(1U << bit)) ? WS2812_PWM_DUTY_1 : WS2812_PWM_DUTY_0;
  }
}

static uint8_t external_led_strip_limit_brightness(uint8_t brightness)
{
  return (brightness > WS2812_BRIGHTNESS_LIMIT) ? WS2812_BRIGHTNESS_LIMIT : brightness;
}

static void external_led_strip_transmit_pwm_buffer(void)
{
  external_led_strip_dma_stop();
  external_led_strip_pwm_pin_init();
  external_led_strip_pwm_timer_init();

  __HAL_RCC_DMA1_CLK_ENABLE();
  DMA1_Channel4->CCR &= ~DMA_CCR_EN;
  DMA1_CSELR->CSELR &= ~DMA_CSELR_C4S;
  DMA1_CSELR->CSELR |= (WS2812_DMA_REQUEST_TIM4_CH2 << DMA_CSELR_C4S_Pos);
  DMA1_Channel4->CPAR = (uint32_t)&TIM4->CCR2;
  DMA1_Channel4->CMAR = (uint32_t)ws2812_pwm_dma;
  DMA1_Channel4->CNDTR = WS2812_PWM_DMA_LEN;
  DMA1_Channel4->CCR = DMA_CCR_DIR | DMA_CCR_MINC | DMA_CCR_PSIZE_0 | DMA_CCR_MSIZE_0 | DMA_CCR_PL_1;
  DMA1->IFCR = DMA_IFCR_CGIF4 | DMA_IFCR_CTCIF4 | DMA_IFCR_CHTIF4 | DMA_IFCR_CTEIF4;

  TIM4->CNT = 0U;
  TIM4->CCR2 = 0U;
  TIM4->SR = 0U;
  TIM4->CCER |= TIM_CCER_CC2E;
  TIM4->DIER |= TIM_DIER_CC2DE;
  DMA1_Channel4->CCR |= DMA_CCR_EN;
  TIM4->CR1 |= TIM_CR1_CEN;

  uint32_t start_ms = HAL_GetTick();
  while ((DMA1->ISR & DMA_ISR_TCIF4) == 0U) {
    if ((HAL_GetTick() - start_ms) > 5U) {
      break;
    }
  }
  external_led_strip_dma_stop();
}

static void external_led_strip_send_pwm_dma(uint8_t phase,
                                            uint8_t marker_r,
                                            uint8_t marker_g,
                                            uint8_t marker_b,
                                            uint8_t solid_r,
                                            uint8_t solid_g,
                                            uint8_t solid_b,
                                            uint8_t solid_mode)
{
  uint32_t idx = 0U;

  for (uint8_t i = 0; i < MENU_WS2812_COUNT; i++) {
    uint8_t g = solid_mode ? solid_g : 0U;
    uint8_t r = solid_mode ? solid_r : 0U;
    uint8_t b = solid_mode ? solid_b : 0U;
    if (!solid_mode && i == (uint8_t)(phase % MENU_WS2812_COUNT)) {
      r = marker_r;
      g = marker_g;
      b = marker_b;
    }
    external_led_strip_append_byte(&idx, g);
    external_led_strip_append_byte(&idx, r);
    external_led_strip_append_byte(&idx, b);
  }
  while (idx < WS2812_PWM_DMA_LEN) {
    ws2812_pwm_dma[idx++] = 0U;
  }

  external_led_strip_transmit_pwm_buffer();
}

static void external_led_strip_send_pixel_buffer(const uint8_t *rgb,
                                                 uint8_t count,
                                                 uint8_t brightness)
{
  uint32_t idx = 0U;
  brightness = external_led_strip_limit_brightness(brightness);

  if (count > MENU_WS2812_COUNT) {
    count = MENU_WS2812_COUNT;
  }

  for (uint8_t i = 0; i < MENU_WS2812_COUNT; i++) {
    uint8_t r = 0U;
    uint8_t g = 0U;
    uint8_t b = 0U;

    if (rgb != NULL && i < count) {
      r = (uint8_t)(((uint16_t)rgb[(uint16_t)i * 3U] * brightness) / 255U);
      g = (uint8_t)(((uint16_t)rgb[(uint16_t)i * 3U + 1U] * brightness) / 255U);
      b = (uint8_t)(((uint16_t)rgb[(uint16_t)i * 3U + 2U] * brightness) / 255U);
    }
    external_led_strip_append_byte(&idx, g);
    external_led_strip_append_byte(&idx, r);
    external_led_strip_append_byte(&idx, b);
  }
  while (idx < WS2812_PWM_DMA_LEN) {
    ws2812_pwm_dma[idx++] = 0U;
  }

  external_led_strip_transmit_pwm_buffer();
}

void WS2812_Show_All(uint8_t r, uint8_t g, uint8_t b, uint8_t brightness)
{
  brightness = external_led_strip_limit_brightness(brightness);
  uint8_t sr = (uint8_t)(((uint16_t)r * brightness) / 255U);
  uint8_t sg = (uint8_t)(((uint16_t)g * brightness) / 255U);
  uint8_t sb = (uint8_t)(((uint16_t)b * brightness) / 255U);
  external_led_strip_send_pwm_dma(0U, 0U, 0U, 0U, sr, sg, sb, 1U);
}

void WS2812_Show_Pixels(const uint8_t *rgb, uint8_t count, uint8_t brightness)
{
  external_led_strip_send_pixel_buffer(rgb, count, brightness);
}

void WS2812_Off(void)
{
  external_led_strip_send_pwm_dma(0U, 0U, 0U, 0U, 0U, 0U, 0U, 1U);
}

static void external_led_strip_show_menu_test(uint8_t phase)
{
#if MENU_WS2812_TEST_ENABLE
#if MENU_WS2812_FORCE_BLACK_ONLY
  external_led_strip_send_pwm_dma(phase, 0U, 0U, 0U, 0U, 0U, 0U, 0U);
#else
  uint8_t brightness = (HAL_GetTick() < MENU_WS2812_STARTUP_OFF_MS) ? 0U : MENU_WS2812_BRIGHTNESS;
#if MENU_WS2812_WHITE_TEST
  external_led_strip_send_pwm_dma(phase, brightness, brightness, brightness, 0U, 0U, 0U, 0U);
#else
  external_led_strip_send_pwm_dma(phase, 0U, brightness, 0U, 0U, 0U, 0U, 0U);
#endif
#endif
#else
  (void)phase;
#endif
}

static void external_led_strip_menu_test(uint32_t now)
{
#if MENU_WS2812_TEST_ENABLE
  static uint32_t last_test_ms = 0U;
  static uint8_t phase = 0U;
  if (last_test_ms == 0U || (now - last_test_ms) >= 700U) {
    external_led_strip_show_menu_test(phase);
    phase = (uint8_t)((phase + 1U) % MENU_WS2812_COUNT);
    last_test_ms = now;
  }
#else
  (void)now;
#endif
}

#define MENU_SEG_ACTIVE_HIGH 1
#define MENU_SEG_A_PORT GPIOA
#define MENU_SEG_A_PIN GPIO_PIN_10
#define MENU_SEG_B_PORT GPIOB
#define MENU_SEG_B_PIN GPIO_PIN_4
#define MENU_SEG_C_PORT GPIOC
#define MENU_SEG_C_PIN GPIO_PIN_10
#define MENU_SEG_D_PORT GPIOC
#define MENU_SEG_D_PIN GPIO_PIN_9
#define MENU_SEG_E_PORT GPIOC
#define MENU_SEG_E_PIN GPIO_PIN_8
#define MENU_SEG_F_PORT GPIOB
#define MENU_SEG_F_PIN GPIO_PIN_6
#define MENU_SEG_G_PORT GPIOB
#define MENU_SEG_G_PIN GPIO_PIN_5
#define MENU_SEG_DP_PORT GPIOC
#define MENU_SEG_DP_PIN GPIO_PIN_11

static uint8_t menu_sevenseg_cache_valid = 0U;
static uint8_t menu_sevenseg_last_mask = 0U;
static uint8_t menu_sevenseg_last_dp = 0U;

static void menu_sevenseg_write(GPIO_TypeDef *port, uint16_t pin, uint8_t on)
{
#if MENU_SEG_ACTIVE_HIGH
  HAL_GPIO_WritePin(port, pin, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
#else
  HAL_GPIO_WritePin(port, pin, on ? GPIO_PIN_RESET : GPIO_PIN_SET);
#endif
}

static void main_menu_sevenseg_pin_output(GPIO_TypeDef *port, uint16_t pin)
{
  GPIO_InitTypeDef gi = {0};
  gi.Pin = pin;
  gi.Mode = GPIO_MODE_OUTPUT_PP;
  gi.Pull = GPIO_NOPULL;
  gi.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(port, &gi);
}

static void main_menu_sevenseg_all_off(void)
{
  menu_sevenseg_write(MENU_SEG_A_PORT, MENU_SEG_A_PIN, 0U);
  menu_sevenseg_write(MENU_SEG_B_PORT, MENU_SEG_B_PIN, 0U);
  menu_sevenseg_write(MENU_SEG_C_PORT, MENU_SEG_C_PIN, 0U);
  menu_sevenseg_write(MENU_SEG_D_PORT, MENU_SEG_D_PIN, 0U);
  menu_sevenseg_write(MENU_SEG_E_PORT, MENU_SEG_E_PIN, 0U);
  menu_sevenseg_write(MENU_SEG_F_PORT, MENU_SEG_F_PIN, 0U);
  menu_sevenseg_write(MENU_SEG_G_PORT, MENU_SEG_G_PIN, 0U);
  menu_sevenseg_write(MENU_SEG_DP_PORT, MENU_SEG_DP_PIN, 0U);
  menu_sevenseg_cache_valid = 0U;
}

static void main_menu_sevenseg_init(void)
{
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();

  main_menu_sevenseg_pin_output(MENU_SEG_A_PORT, MENU_SEG_A_PIN);
  main_menu_sevenseg_pin_output(MENU_SEG_B_PORT, MENU_SEG_B_PIN);
  main_menu_sevenseg_pin_output(MENU_SEG_C_PORT, MENU_SEG_C_PIN);
  main_menu_sevenseg_pin_output(MENU_SEG_D_PORT, MENU_SEG_D_PIN);
  main_menu_sevenseg_pin_output(MENU_SEG_E_PORT, MENU_SEG_E_PIN);
  main_menu_sevenseg_pin_output(MENU_SEG_F_PORT, MENU_SEG_F_PIN);
  main_menu_sevenseg_pin_output(MENU_SEG_G_PORT, MENU_SEG_G_PIN);
  main_menu_sevenseg_pin_output(MENU_SEG_DP_PORT, MENU_SEG_DP_PIN);

  main_menu_sevenseg_all_off();
}

static void main_menu_sevenseg_display_digit(uint8_t digit, uint8_t dp_on)
{
  static const uint8_t lut[10] = {
    0x7EU, 0x30U, 0x6DU, 0x79U, 0x33U,
    0x5BU, 0x5FU, 0x70U, 0x7FU, 0x7BU
  };
  uint8_t mask = lut[digit % 10U];
  dp_on = dp_on ? 1U : 0U;

  if (menu_sevenseg_cache_valid &&
      menu_sevenseg_last_mask == mask &&
      menu_sevenseg_last_dp == dp_on) {
    return;
  }

  menu_sevenseg_cache_valid = 1U;
  menu_sevenseg_last_mask = mask;
  menu_sevenseg_last_dp = dp_on;
  menu_sevenseg_write(MENU_SEG_A_PORT, MENU_SEG_A_PIN, (mask & 0x40U) != 0U);
  menu_sevenseg_write(MENU_SEG_B_PORT, MENU_SEG_B_PIN, (mask & 0x20U) != 0U);
  menu_sevenseg_write(MENU_SEG_C_PORT, MENU_SEG_C_PIN, (mask & 0x10U) != 0U);
  menu_sevenseg_write(MENU_SEG_D_PORT, MENU_SEG_D_PIN, (mask & 0x08U) != 0U);
  menu_sevenseg_write(MENU_SEG_E_PORT, MENU_SEG_E_PIN, (mask & 0x04U) != 0U);
  menu_sevenseg_write(MENU_SEG_F_PORT, MENU_SEG_F_PIN, (mask & 0x02U) != 0U);
  menu_sevenseg_write(MENU_SEG_G_PORT, MENU_SEG_G_PIN, (mask & 0x01U) != 0U);
  menu_sevenseg_write(MENU_SEG_DP_PORT, MENU_SEG_DP_PIN, dp_on);
}

static void external_sevenseg_pin_idle(GPIO_TypeDef *port, uint16_t pin)
{
  GPIO_InitTypeDef gi = {0};
  HAL_GPIO_WritePin(port, pin, GPIO_PIN_RESET);
  gi.Pin = pin;
  gi.Mode = GPIO_MODE_OUTPUT_PP;
  gi.Pull = GPIO_NOPULL;
  gi.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(port, &gi);
}

static void external_sevenseg_idle(void)
{
  external_sevenseg_pin_idle(MENU_SEG_A_PORT, MENU_SEG_A_PIN);
  external_sevenseg_pin_idle(MENU_SEG_B_PORT, MENU_SEG_B_PIN);
  external_sevenseg_pin_idle(MENU_SEG_C_PORT, MENU_SEG_C_PIN);
  external_sevenseg_pin_idle(MENU_SEG_D_PORT, MENU_SEG_D_PIN);
  external_sevenseg_pin_idle(MENU_SEG_E_PORT, MENU_SEG_E_PIN);
  external_sevenseg_pin_idle(MENU_SEG_F_PORT, MENU_SEG_F_PIN);
  external_sevenseg_pin_idle(MENU_SEG_G_PORT, MENU_SEG_G_PIN);
  external_sevenseg_pin_idle(MENU_SEG_DP_PORT, MENU_SEG_DP_PIN);
  menu_sevenseg_cache_valid = 0U;
}


// ===== Main Function =====

int main(void)
{
    HAL_Init();
    SystemClock_Config();
    PeriphCommonClock_Config();

    MX_GPIO_Init();
    external_led_strip_idle();
    external_sevenseg_idle();
    HAL_NVIC_SetPriority(EXTI15_10_IRQn, 2, 0);
    MX_USART2_UART_Init();
    perf_counter_init();
    Audio_Init();

    PowerMonitor_Init();
    Game1_PrepareAssets();

#if LCD_BLINK_ONLY_TEST
    while (1)
    {
      HAL_GPIO_WritePin(GPIOC, GPIO_PIN_7, GPIO_PIN_SET);
      HAL_Delay(500);
      HAL_GPIO_WritePin(GPIOC, GPIO_PIN_7, GPIO_PIN_RESET);
      HAL_Delay(500);
    }
#endif

    MX_ADC1_Init();
    ST7789V2_Init(&cfg0);

    Joystick_Init(&joystick_cfg);
    Joystick_Init(&joystick2_cfg);

    printf("\n=== Shared Game Console ===\n");
    printf("Calibrating joysticks... keep centered\n");
    Joystick_Calibrate(&joystick_cfg);
    Joystick_Calibrate(&joystick2_cfg);
    printf("Calibration done.\n");

    perf_counter_init();

    while (1) {
      external_sevenseg_idle();
      MainMenuSelection menu_selection = main_menu_run();
      if (menu_selection == MAIN_MENU_GAME2) {
        Input_Init();
        if (LCD_Attach_Work_Buffer(game2_lcd_work_ram, sizeof(game2_lcd_work_ram))) {
          Game2_Run();
          lcd_wait_dma_spi_idle();
          buzzer_off(&buzzer_cfg);
          LCD_Set_Palette(PALETTE_DEFAULT);
          LCD_Detach_Work_Buffer();
          ST7789V2_Init(&cfg0);
        } else {
          menu_show_unimplemented(menu_selection);
        }
        continue;
      }
      if (menu_selection == MAIN_MENU_GAME3) {
        Input_Init();
        if (LCD_Attach_Work_Buffer(game2_lcd_work_ram, sizeof(game2_lcd_work_ram))) {
          Game3_Run();
          lcd_wait_dma_spi_idle();
          buzzer_off(&buzzer_cfg);
          LCD_Set_Palette(PALETTE_DEFAULT);
          LCD_Detach_Work_Buffer();
          ST7789V2_Init(&cfg0);
        } else {
          menu_show_unimplemented(menu_selection);
        }
        continue;
      }
      if (menu_selection != MAIN_MENU_APEX) {
        menu_show_unimplemented(menu_selection);
        continue;
      }
      (void)Game1_Run();
    }
}

// ==== AUTO-GENERATED STM32 FUNCTIONS ====
// DO NOT EDIT UNLESS YOU KNOW WHAT YOU ARE DOING! 


/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 1;
  RCC_OscInitStruct.PLL.PLLN = 10;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV7;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief Peripherals Common Clock Configuration
  * @retval None
  */
void PeriphCommonClock_Config(void)
{
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  /** Initializes the peripherals clock
  */
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_RNG|RCC_PERIPHCLK_ADC;
  PeriphClkInit.AdcClockSelection = RCC_ADCCLKSOURCE_PLLSAI1;
  PeriphClkInit.RngClockSelection = RCC_RNGCLKSOURCE_PLLSAI1;
  PeriphClkInit.PLLSAI1.PLLSAI1Source = RCC_PLLSOURCE_HSI;
  PeriphClkInit.PLLSAI1.PLLSAI1M = 1;
  PeriphClkInit.PLLSAI1.PLLSAI1N = 8;
  PeriphClkInit.PLLSAI1.PLLSAI1P = RCC_PLLP_DIV7;
  PeriphClkInit.PLLSAI1.PLLSAI1Q = RCC_PLLQ_DIV4;
  PeriphClkInit.PLLSAI1.PLLSAI1R = RCC_PLLR_DIV2;
  PeriphClkInit.PLLSAI1.PLLSAI1ClockOut = RCC_PLLSAI1_48M2CLK|RCC_PLLSAI1_ADC1CLK;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler();
  }
}



/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
