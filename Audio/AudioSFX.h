#pragma once
#include "Audio.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file AudioSFX.h
 * @brief Pre-generated sound effects stored in Flash
 *
 * All samples are 8-bit unsigned PCM at 8kHz.
 * Generated at compile time using const arrays.
 */

/* ---- Gunshot: 1600 samples = 200ms ---- */
/* White noise burst with fast exponential decay */
extern Audio_Sound_t sfx_gunshot;

/* ---- Shield crack: 800 samples = 100ms ---- */
/* High-frequency chirp sweeping down */
extern Audio_Sound_t sfx_crack;

/* ---- Kill confirm: 4000 samples = 500ms ---- */
/* Descending tone 1000Hz -> 400Hz */
extern Audio_Sound_t sfx_kill;

/* ---- Beep: 800 samples = 100ms ---- */
/* Simple 800Hz sine beep */
extern Audio_Sound_t sfx_beep;

/**
 * @brief Generate all sound effects into RAM buffers
 * Call once at startup, after Audio_Init()
 */
void AudioSFX_Generate(void);

#ifdef __cplusplus
}
#endif
