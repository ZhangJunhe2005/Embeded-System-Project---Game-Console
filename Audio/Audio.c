#include "Audio.h"
#include "stm32l4xx_hal.h"
#include <math.h>

/**
 * @file Audio.c
 * @brief DAC audio driver using direct register access
 *
 * DAC1 Channel 1 (PA4) outputs 12-bit samples at AUDIO_SAMPLE_RATE,
 * triggered by TIM6 update interrupt.
 */

/* ---- Sine lookup table (256 entries, 8-bit unsigned) ---- */
static const uint8_t sine_table[256] = {
    128,131,134,137,140,143,146,149,152,155,158,162,165,168,170,173,
    176,179,182,184,187,190,192,195,197,200,202,204,207,209,211,213,
    215,217,219,221,223,225,226,228,230,231,233,234,235,237,238,239,
    240,241,242,243,244,244,245,246,246,247,247,248,248,248,248,248,
    255,248,248,248,248,248,247,247,246,246,245,244,244,243,242,241,
    240,239,238,237,235,234,233,231,230,228,226,225,223,221,219,217,
    215,213,211,209,207,204,202,200,197,195,192,190,187,184,182,179,
    176,173,170,168,165,162,158,155,152,149,146,143,140,137,134,131,
    128,125,122,119,116,113,110,107,104,101, 98, 94, 91, 88, 86, 83,
     80, 77, 74, 72, 69, 66, 64, 61, 59, 56, 54, 52, 49, 47, 45, 43,
     41, 39, 37, 35, 33, 31, 30, 28, 26, 25, 23, 22, 21, 19, 18, 17,
     16, 15, 14, 13, 12, 12, 11, 10, 10,  9,  9,  8,  8,  8,  8,  8,
      0,  8,  8,  8,  8,  8,  9,  9, 10, 10, 11, 12, 12, 13, 14, 15,
     16, 17, 18, 19, 21, 22, 23, 25, 26, 28, 30, 31, 33, 35, 37, 39,
     41, 43, 45, 47, 49, 52, 54, 56, 59, 61, 64, 66, 69, 72, 74, 77,
     80, 83, 86, 88, 91, 94, 98,101,104,107,110,113,116,119,122,125
};

/* ---- Playback state ---- */
static volatile Audio_State_t audio_state = AUDIO_IDLE;
static volatile const uint8_t* audio_ptr = 0;
static volatile const uint16_t* audio12_ptr = 0;
static volatile uint32_t audio_remaining = 0;
static volatile uint8_t  audio_volume = 50;
static volatile uint8_t  audio_is_12bit = 0;

/* ---- Tone generator state ---- */
static volatile uint32_t tone_phase = 0;       // Q24.8 fixed-point phase accumulator
static volatile uint32_t tone_phase_inc = 0;   // Phase increment per sample
static volatile uint8_t  tone_active = 0;
static volatile uint8_t  tone_volume = 50;

/* ---- DAC register helpers ---- */
static inline void dac_write(uint16_t val12)
{
    // DAC1 Channel 1, 12-bit right-aligned
    DAC1->DHR12R1 = val12 & 0x0FFF;
}

static inline void dac_enable(void)
{
    DAC1->CR |= DAC_CR_EN1;
    // Small delay for DAC startup
    volatile uint32_t d = 100;
    while (d--);
}

static inline void dac_disable(void)
{
    DAC1->CR &= ~DAC_CR_EN1;
}

void Audio_Init(void)
{
    // 1. Enable DAC1 clock
    RCC->APB1ENR1 |= RCC_APB1ENR1_DAC1EN;

    // 2. Configure PA4 as analog mode
    // Enable GPIOA clock (should already be enabled)
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN;
    // PA4 = analog mode (MODER bits [9:8] = 0b11)
    GPIOA->MODER |= (3u << (4 * 2));

    // 3. Configure DAC1 Channel 1
    // - No trigger (software/free-running), we feed from ISR
    // - No wave generation
    // - Output buffer enabled (lower impedance)
    DAC1->CR = 0;  // Clear all
    // EN1 will be set when we start playback

    // 4. Reconfigure TIM6 for 8kHz sample rate
    // 80MHz / (Prescaler+1) / (Period+1) = 8000
    // Prescaler=0, Period=9999 → 80MHz / 1 / 10000 = 8000 Hz
    RCC->APB1ENR1 |= RCC_APB1ENR1_TIM6EN;

    TIM6->CR1 = 0;
    TIM6->PSC = 0;              // No prescaler
    TIM6->ARR = (SystemCoreClock / AUDIO_SAMPLE_RATE) - 1U;
    TIM6->DIER = TIM_DIER_UIE;  // Enable update interrupt
    TIM6->CR1 = TIM_CR1_ARPE;   // Auto-reload preload enable

    // 5. Enable TIM6 interrupt in NVIC
    HAL_NVIC_SetPriority(TIM6_DAC_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(TIM6_DAC_IRQn);

    // 6. Enable DAC and set to midpoint (silence)
    dac_enable();
    dac_write(AUDIO_DAC_MID);

    // 7. Keep TIM6 stopped until playback starts.
    TIM6->CR1 &= ~TIM_CR1_CEN;
}

void Audio_Play(const Audio_Sound_t* sound)
{
    if (!sound || !sound->samples || sound->length == 0) return;

    // Atomic-ish update: disable interrupt briefly
    __disable_irq();
    tone_active = 0;
    audio_is_12bit = 0;
    audio_ptr = sound->samples;
    audio12_ptr = 0;
    audio_remaining = sound->length;
    audio_volume = sound->volume > 100 ? 100 : sound->volume;
    audio_state = AUDIO_PLAYING;
    TIM6->CNT = 0;
    TIM6->SR = 0;
    TIM6->CR1 |= TIM_CR1_CEN;
    __enable_irq();
}

void Audio_Play12(const Audio_Sound12_t* sound)
{
    if (!sound || !sound->samples || sound->length == 0) return;

    __disable_irq();
    tone_active = 0;
    audio_is_12bit = 1;
    audio_ptr = 0;
    audio12_ptr = sound->samples;
    audio_remaining = sound->length;
    audio_volume = sound->volume > 100 ? 100 : sound->volume;
    audio_state = AUDIO_PLAYING;
    TIM6->CNT = 0;
    TIM6->SR = 0;
    TIM6->CR1 |= TIM_CR1_CEN;
    __enable_irq();
}

void Audio_Stop(void)
{
    __disable_irq();
    audio_state = AUDIO_IDLE;
    tone_active = 0;
    audio_is_12bit = 0;
    audio_remaining = 0;
    TIM6->CR1 &= ~TIM_CR1_CEN;
    __enable_irq();
    dac_write(AUDIO_DAC_MID);
}

uint8_t Audio_IsPlaying(void)
{
    return (audio_state == AUDIO_PLAYING || tone_active) ? 1 : 0;
}

void Audio_PlayTone(uint32_t freq_hz, uint32_t duration_ms, uint8_t volume)
{
    if (freq_hz == 0 || duration_ms == 0) return;

    // Phase increment: (freq * 256) / SAMPLE_RATE in Q24.8 fixed point
    // = freq * 256 / AUDIO_SAMPLE_RATE
    uint32_t inc = (freq_hz * 256) / AUDIO_SAMPLE_RATE;
    if (inc == 0) inc = 1;

    __disable_irq();
    audio_state = AUDIO_IDLE;
    audio_is_12bit = 0;
    audio_remaining = 0;
    tone_phase = 0;
    tone_phase_inc = inc;
    tone_volume = volume > 100 ? 100 : volume;
    tone_active = 1;
    TIM6->CNT = 0;
    TIM6->SR = 0;
    TIM6->CR1 |= TIM_CR1_CEN;
    __enable_irq();

    // Block for duration
    HAL_Delay(duration_ms);

    // Stop tone
    __disable_irq();
    tone_active = 0;
    TIM6->CR1 &= ~TIM_CR1_CEN;
    __enable_irq();
    dac_write(AUDIO_DAC_MID);
}

/**
 * @brief TIM6 ISR handler — called at 8kHz
 * Output next sample to DAC
 */
void Audio_TIM6_IRQHandler(void)
{
    uint16_t dac_val = AUDIO_DAC_MID;  // Default: silence

    if (tone_active) {
        // Sine wave tone generation
        uint8_t idx = (tone_phase >> 8) & 0xFF;
        uint8_t raw = sine_table[idx];
        tone_phase += tone_phase_inc;

        // Scale by volume: (raw - 128) * volume / 100 + 2048
        int32_t sample = ((int32_t)raw - 128) * tone_volume / 100;
        dac_val = (uint16_t)(AUDIO_DAC_MID + sample * 16);  // Scale 8-bit to 12-bit range

    } else if (audio_state == AUDIO_PLAYING && audio_remaining > 0) {
        if (audio_is_12bit) {
            // 12-bit PCM playback path
            uint16_t raw12 = *audio12_ptr++ & 0x0FFF;
            audio_remaining--;

            // Scale around midpoint: (raw - 2048) * volume / 100 + 2048
            int32_t sample = ((int32_t)raw12 - AUDIO_DAC_MID) * audio_volume / 100;
            dac_val = (uint16_t)(AUDIO_DAC_MID + sample);
        } else {
            // 8-bit PCM playback path
            uint8_t raw = *audio_ptr++;
            audio_remaining--;

            // Scale by volume: (raw - 128) * volume / 100 + 2048
            int32_t sample = ((int32_t)raw - 128) * audio_volume / 100;
            dac_val = (uint16_t)(AUDIO_DAC_MID + sample * 16);
        }

        if (audio_remaining == 0) {
            audio_state = AUDIO_IDLE;
            TIM6->CR1 &= ~TIM_CR1_CEN;
        }
    }

    // Clamp to 12-bit range
    if (dac_val > 4095) dac_val = 4095;

    dac_write(dac_val);
}
