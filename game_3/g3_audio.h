#ifndef G3_AUDIO_H
#define G3_AUDIO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void Game3Audio_PlayTone(uint16_t freq_hz, uint16_t duration_ms, uint8_t volume_percent);
void Game3Audio_Stop(void);
uint8_t Game3Audio_IsPlaying(void);
void Game3Audio_TIM6_IRQHandler(void);

#ifdef __cplusplus
}
#endif

#endif /* G3_AUDIO_H */
