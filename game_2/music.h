#ifndef GAME2_MUSIC_H
#define GAME2_MUSIC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void Game2Music_Start(void);
void Game2Music_Stop(void);
void Game2Music_StopBgm(void);
uint8_t Game2Music_IsPlaying(void);
void Game2Music_TIM6_IRQHandler(void);

void Game2Music_PlayMenuMove(void);
void Game2Music_PlayMenuConfirm(void);
void Game2Music_PlayMenuBack(void);
void Game2Music_PlayBoostSfx(void);
void Game2Music_PlayBoostReadySfx(void);
void Game2Music_PlayBoostTriggerSfx(void);
void Game2Music_PlayCheckpointSfx(void);
void Game2Music_PlayCrashSfx(void);
void Game2Music_PlayGameOverSfx(void);

#ifdef __cplusplus
}
#endif

#endif /* GAME2_MUSIC_H */
