#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file Audio.h
 * @brief DAC-based audio playback for STM32L476
 *
 * Uses DAC1 Channel 1 (PA4) with TIM6 interrupt at AUDIO_SAMPLE_RATE.
 * Direct register access — no HAL DAC driver needed.
 *
 * Wiring: PA4 → PAM8403 L-IN → 8ohm Speaker
 */

#define AUDIO_SAMPLE_RATE  16000  // 16kHz sample rate
#define AUDIO_DAC_BITS     12     // 12-bit DAC (0-4095)
#define AUDIO_DAC_MID      2048   // Midpoint (silence)

/** Audio playback state */
typedef enum {
    AUDIO_IDLE = 0,
    AUDIO_PLAYING
} Audio_State_t;

/** Sound effect descriptor — points to const sample data in Flash */
typedef struct {
    const uint8_t* samples;     // 8-bit unsigned PCM data (Flash)
    uint32_t       length;      // Number of samples
    uint8_t        volume;      // 0-100 playback volume
} Audio_Sound_t;

/** 12-bit PCM descriptor (unsigned 0-4095) */
typedef struct {
    const uint16_t* samples;    // 12-bit unsigned PCM data (Flash)
    uint32_t        length;     // Number of samples
    uint8_t         volume;     // 0-100 playback volume
} Audio_Sound12_t;

/**
 * @brief Initialize DAC1 CH1 (PA4) and TIM6 for audio playback
 * Must be called after SystemClock_Config and MX_GPIO_Init
 */
void Audio_Init(void);

/**
 * @brief Play a sound effect (non-blocking)
 * Replaces any currently playing sound.
 * @param sound  Pointer to sound descriptor
 */
void Audio_Play(const Audio_Sound_t* sound);

/**
 * @brief Play a 12-bit PCM sound (non-blocking)
 * Replaces any currently playing sound.
 * @param sound  Pointer to 12-bit sound descriptor
 */
void Audio_Play12(const Audio_Sound12_t* sound);

/**
 * @brief Stop playback immediately
 */
void Audio_Stop(void);

/**
 * @brief Check if audio is currently playing
 * @return 1 if playing, 0 if idle
 */
uint8_t Audio_IsPlaying(void);

/**
 * @brief Play a blocking tone (sine wave) for testing
 * @param freq_hz  Tone frequency in Hz
 * @param duration_ms  Duration in milliseconds
 * @param volume  Volume 0-100
 */
void Audio_PlayTone(uint32_t freq_hz, uint32_t duration_ms, uint8_t volume);

/**
 * @brief Called from TIM6 ISR — do not call directly
 */
void Audio_TIM6_IRQHandler(void);

#ifdef __cplusplus
}
#endif
