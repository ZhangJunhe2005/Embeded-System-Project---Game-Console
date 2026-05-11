#include "AudioSFX.h"
#include <math.h>

/**
 * @file AudioSFX.c
 * @brief Sound effect sample data generated at init-time and stored in Flash
 *
 * Since we can't run code at compile-time in C, we use a Python-style approach:
 * pre-computed arrays. Each sample is 8-bit unsigned (0-255, 128 = silence).
 */

/* We generate sample data using init function instead of huge const arrays.
 * This saves Flash and allows parameterized sounds.
 * Samples are generated once into RAM buffers at startup. */

/* ---- Static RAM buffers for generated SFX ---- */
static uint8_t gunshot_buf[1600];   // 200ms
static uint8_t crack_buf[800];      // 100ms
static uint8_t kill_buf[4000];      // 500ms
static uint8_t beep_buf[800];       // 100ms

/* ---- Sound descriptors (filled by AudioSFX_Generate) ---- */
Audio_Sound_t sfx_gunshot = { .samples = 0, .length = 1600, .volume = 30 };
Audio_Sound_t sfx_crack   = { .samples = 0, .length = 800,  .volume = 30 };
Audio_Sound_t sfx_kill    = { .samples = 0, .length = 4000, .volume = 30 };
Audio_Sound_t sfx_beep    = { .samples = 0, .length = 800,  .volume = 30 };

/* Simple PRNG for noise generation (xorshift32) */
static uint32_t rng_state = 0xDEADBEEF;
static uint8_t noise_byte(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return (uint8_t)(rng_state & 0xFF);
}

/**
 * @brief Generate all sound effects into RAM buffers
 * Call once at startup, after Audio_Init()
 * Uses ~7.2KB RAM for buffers
 */
void AudioSFX_Generate(void)
{
    uint32_t i;

    /* ---- Gunshot: noise burst with exponential decay ---- */
    for (i = 0; i < 1600; i++) {
        float env = expf(-(float)i / 300.0f);  // Fast decay
        int16_t noise = (int16_t)noise_byte() - 128;
        int16_t sample = (int16_t)(noise * env);
        gunshot_buf[i] = (uint8_t)(sample + 128);
    }
    sfx_gunshot.samples = gunshot_buf;

    /* ---- Shield crack: chirp sweep 3000Hz -> 500Hz ---- */
    {
        float phase = 0.0f;
        for (i = 0; i < 800; i++) {
            float t = (float)i / 800.0f;
            float freq = 3000.0f - 2500.0f * t;  // Sweep down
            float env = 1.0f - t;  // Linear decay
            phase += freq / 8000.0f;
            float s = sinf(phase * 6.2832f) * env;
            crack_buf[i] = (uint8_t)((int16_t)(s * 100) + 128);
        }
    }
    sfx_crack.samples = crack_buf;

    /* ---- Kill confirm: descending tone 1000Hz -> 400Hz ---- */
    {
        float phase = 0.0f;
        for (i = 0; i < 4000; i++) {
            float t = (float)i / 4000.0f;
            float freq = 1000.0f - 600.0f * t;
            float env = 1.0f - t * 0.5f;  // Slow decay
            phase += freq / 8000.0f;
            float s = sinf(phase * 6.2832f) * env;
            kill_buf[i] = (uint8_t)((int16_t)(s * 100) + 128);
        }
    }
    sfx_kill.samples = kill_buf;

    /* ---- Beep: 800Hz sine ---- */
    {
        float phase = 0.0f;
        for (i = 0; i < 800; i++) {
            phase += 800.0f / 8000.0f;
            float s = sinf(phase * 6.2832f);
            beep_buf[i] = (uint8_t)((int16_t)(s * 100) + 128);
        }
    }
    sfx_beep.samples = beep_buf;
}
