#include "g3_audio.h"

#include "Audio.h"
#include "stm32l4xx_hal.h"

#define G3_AUDIO_MAX_VOLUME_PERCENT 6U
#define G3_AUDIO_SAMPLE_RATE 8000U
#define G3_AUDIO_DUTY_PERCENT 10U
#define G3_AUDIO_DAC_MID AUDIO_DAC_MID
#define G3_AUDIO_DAC_MAX 4095U
#define G3_AUDIO_PHASE_TOP_BIT 0x80000000UL
#define G3_AUDIO_HALF_PHASE 0x80000000UL

typedef struct {
    uint32_t samples_left;
    uint32_t phase;
    uint32_t phase_step;
    uint32_t duty_width;
    uint16_t amplitude;
    uint8_t active;
} G3AudioTone;

static volatile G3AudioTone g3_tone;

static uint16_t g3_audio_clamp_volume(uint8_t volume_percent)
{
    if (volume_percent > G3_AUDIO_MAX_VOLUME_PERCENT) {
        volume_percent = G3_AUDIO_MAX_VOLUME_PERCENT;
    }
    return (uint16_t)((((uint32_t)G3_AUDIO_DAC_MID - 1U) * volume_percent) / 100U);
}

static void g3_audio_write_dac(uint16_t value)
{
    DAC1->DHR12R1 = value & 0x0FFFU;
}

static void g3_audio_configure_dac_timer(void)
{
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN;
    GPIOA->MODER |= (3U << (4U * 2U));

    RCC->APB1ENR1 |= RCC_APB1ENR1_DAC1EN;
    DAC1->CR = 0U;
    DAC1->CR |= DAC_CR_EN1;

    RCC->APB1ENR1 |= RCC_APB1ENR1_TIM6EN;
    TIM6->CR1 = 0U;
    TIM6->PSC = 0U;
    TIM6->ARR = (SystemCoreClock / G3_AUDIO_SAMPLE_RATE) - 1U;
    TIM6->DIER = TIM_DIER_UIE;
    TIM6->CR1 = TIM_CR1_ARPE;
    TIM6->CNT = 0U;
    TIM6->SR = 0U;

    HAL_NVIC_SetPriority(TIM6_DAC_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(TIM6_DAC_IRQn);
}

void Game3Audio_PlayTone(uint16_t freq_hz, uint16_t duration_ms, uint8_t volume_percent)
{
    if (duration_ms == 0U || freq_hz == 0U) {
        Game3Audio_Stop();
        return;
    }

    Audio_Stop();

    __disable_irq();
    g3_audio_configure_dac_timer();
    g3_tone.samples_left = (((uint32_t)duration_ms * G3_AUDIO_SAMPLE_RATE) + 999U) / 1000U;
    if (g3_tone.samples_left == 0U) {
        g3_tone.samples_left = 1U;
    }
    g3_tone.phase = 0U;
    g3_tone.phase_step = (uint32_t)(((uint64_t)freq_hz << 32) / G3_AUDIO_SAMPLE_RATE);
    g3_tone.duty_width = (uint32_t)(((uint64_t)G3_AUDIO_HALF_PHASE * G3_AUDIO_DUTY_PERCENT) / 100U);
    g3_tone.amplitude = g3_audio_clamp_volume(volume_percent);
    g3_tone.active = 1U;
    TIM6->CR1 |= TIM_CR1_CEN;
    __enable_irq();
}

void Game3Audio_Stop(void)
{
    __disable_irq();
    g3_tone.active = 0U;
    g3_tone.samples_left = 0U;
    g3_tone.phase_step = 0U;
    TIM6->CR1 &= ~TIM_CR1_CEN;
    __enable_irq();
    g3_audio_write_dac(G3_AUDIO_DAC_MID);
    Audio_Init();
}

uint8_t Game3Audio_IsPlaying(void)
{
    return g3_tone.active ? 1U : 0U;
}

void Game3Audio_TIM6_IRQHandler(void)
{
    uint16_t sample = G3_AUDIO_DAC_MID;

    if (g3_tone.active && g3_tone.samples_left > 0U) {
        uint32_t half_phase = g3_tone.phase & (G3_AUDIO_HALF_PHASE - 1U);
        if (half_phase < g3_tone.duty_width) {
            if ((g3_tone.phase & G3_AUDIO_PHASE_TOP_BIT) == 0U) {
                sample = (uint16_t)(G3_AUDIO_DAC_MID + g3_tone.amplitude);
            } else {
                sample = (uint16_t)(G3_AUDIO_DAC_MID - g3_tone.amplitude);
            }
        }
        if (sample > G3_AUDIO_DAC_MAX) {
            sample = G3_AUDIO_DAC_MAX;
        }

        g3_tone.phase += g3_tone.phase_step;
        g3_tone.samples_left--;
    } else {
        g3_tone.active = 0U;
        TIM6->CR1 &= ~TIM_CR1_CEN;
    }

    if (g3_tone.samples_left == 0U) {
        g3_tone.active = 0U;
        TIM6->CR1 &= ~TIM_CR1_CEN;
    }

    g3_audio_write_dac(sample);
}
