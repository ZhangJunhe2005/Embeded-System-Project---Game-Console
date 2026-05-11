#include "music.h"

#include "Audio.h"
#include "stm32l4xx_hal.h"

#define GAME2_MUSIC_MAX_VOLUME_PERCENT 6U
#define GAME2_MUSIC_DEFAULT_VOLUME_PERCENT 3U
#define GAME2_SFX_DEFAULT_VOLUME_PERCENT 6U
#define GAME2_MUSIC_SAMPLE_RATE 8000U
#define GAME2_MUSIC_DUTY_PERCENT 10U
#define GAME2_MUSIC_DAC_MID AUDIO_DAC_MID
#define GAME2_MUSIC_DAC_MAX 4095U
#define GAME2_MUSIC_PHASE_TOP_BIT 0x80000000UL
#define GAME2_MUSIC_HALF_PHASE 0x80000000UL

typedef struct {
    uint16_t freq;   /* Hz, 0 means rest */
    uint16_t ms;     /* Duration in milliseconds */
} Note;

static const Note bgm[] = {
    { 349,  381},
    {   0,  127},
    { 415,  254},
    {   0,  127},
    { 349,  127},
    {   0,  127},
    { 349,  127},
    { 466,  191},
    {   0,   64},
    { 349,  127},
    {   0,  127},
    { 311,  127},
    {   0,  127},
    { 349,  381},
    {   0,  127},
    { 523,  318},
    {   0,   64},
    { 349,  191},
    {   0,   64},
    { 349,  127},
    { 554,  127},
    {   0,  127},
    { 523,  127},
    {   0,  127},
    { 415,  127},
    {   0,  127},
    { 349,  254},
    { 523,  254},
    {   0,   64},
    { 698,  127},
    {   0,   64},
    { 349,  127},
    {   0,   64},
    { 311,   64},
    {   0,  127},
    { 311,  127},
    {   0,   64},
    { 262,   64},
    {   0,  127},
    { 392,  254},
    {   0,   64},
    { 349,  699},
    {   0, 1525},
    { 349,  318},
    {   0,  191},
    { 415,  254},
    {   0,  127},
    { 349,  127},
    {   0,  127},
    { 349,  127},
    { 466,  191},
    {   0,   64},
    { 349,  127},
    {   0,  127},
    { 311,  127},
    {   0,  127},
    { 349,  381},
    {   0,  127},
    { 523,  318},
    {   0,   64},
    { 349,  191},
    {   0,   64},
    { 349,  127},
    { 554,  127},
    {   0,  127},
    { 523,  127},
    {   0,  127},
    { 415,  127},
    {   0,  127},
    { 349,  254},
    { 523,  254},
    {   0,   64},
    { 698,  127},
    {   0,   64},
    { 349,  127},
    {   0,   64},
    { 311,   64},
    {   0,  127},
    { 311,  127},
    {   0,   64},
    { 262,   64},
    {   0,  127},
    { 392,  254},
    {   0,   64},
    { 349,  699},
    {   0, 1525},
    {  44,  381},
    {   0,  127},
    {  87,  254},
    {   0,  127},
    {  78,  127},
    {   0,  127},
    { 156,   64},
    {   0,   64},
    {  33,  127},
    {   0,  127},
    {  65,  127},
    {   0,  127},
    {  78,  127},
    {   0,  127},
    {  44,  318},
    {   0,  191},
    {  87,  127},
    {   0,  508},
    {  44,  127},
    {   0,   64},
    {  65,   64},
    {   0,  127},
    {  78,  127},
    {   0,  127},
    {  87,  127},
    {   0,  127},
    {  35,  381},
    {   0,  127},
    {  69,  254},
    {   0,  381},
    {  39,  127},
    {   0,   64},
    {  78,   64},
    {   0,  127},
    {  65,  191},
    {   0,   64},
    {  78,  191},
    {   0,   64},
    {  44,  445},
    {   0,  699},
    {  78,   64},
    {   0,   64},
    {  65,  127},
    {   0,  127},
    {  58,  127},
    {   0,  127},
    {  52,  127},
    {   0,  127},
    {  44,  381},
    {   0,  127},
    {  87,  254},
    {   0,  127},
    {  78,  127},
    {   0,  127},
    { 156,   64},
    {   0,   64},
    {  33,  127},
    {   0,  127},
    {  65,  127},
    {   0,  127},
    {  78,  127},
    {   0,  127},
    {  44,  318},
    {   0,  191},
    {  87,  127},
    {   0,  508},
    {  44,  127},
    {   0,   64},
    {  65,   64},
    {   0,  127},
    {  78,  127},
    {   0,  127},
    {  87,  127},
    {   0,  127},
    {  35,  381},
    {   0,  127},
    {  69,  254},
    {   0,  381},
    {  39,  127},
    {   0,   64},
    {  78,   64},
    {   0,  127},
    {  65,  191},
    {   0,   64},
    {  78,  191},
    {   0,   64},
    {  44,  445},
    {   0,  699},
    {  78,   64},
    {   0,   64},
    {  65,  127},
    {   0,  127},
    {  58,  127},
    {   0,  127},
    {  52,  127},
    {   0,  127},
    { 349,  318},
    {  44,   64},
    {   0,  127},
    { 415,  254},
    {   0,  127},
    { 349,  127},
    {   0,  127},
    { 349,  127},
    { 466,  191},
    {   0,   64},
    { 349,  127},
    {  65,   64},
    {   0,   64},
    { 311,  127},
    {   0,  127},
    { 349,  381},
    {   0,  127},
    { 523,  318},
    {   0,   64},
    { 349,  191},
    {   0,   64},
    { 349,  127},
    { 554,  127},
    {  65,   64},
    {   0,   64},
    { 523,  127},
    {  78,   64},
    {   0,   64},
    { 415,  127},
    {  87,   64},
    {   0,   64},
    { 349,  254},
    { 523,  254},
    {   0,   64},
    { 698,  127},
    {  69,   64},
    { 349,  127},
    {   0,   64},
    { 311,   64},
    {   0,  127},
    { 311,  127},
    {   0,   64},
    { 262,   64},
    {  78,   64},
    {   0,   64},
    { 392,  254},
    {   0,   64},
    { 349,  699},
    {   0,  636},
    {  78,   64},
    {   0,   64},
    {  65,  127},
    {   0,  127},
    {  58,  127},
    {   0,  127},
    {  52,  127},
    {   0,  127},
    { 349,  318},
    {  44,   64},
    {   0,  127},
    { 415,  254},
    {   0,  127},
    { 349,  127},
    {   0,  127},
    { 349,  127},
    { 466,  191},
    {   0,   64},
    { 349,  127},
    {  65,   64},
    {   0,   64},
    { 311,  127},
    {   0,  127},
    { 349,  381},
    {   0,  127},
    { 523,  318},
    {   0,   64},
    { 349,  191},
    {   0,   64},
    { 349,  127},
    { 554,  127},
    {  65,   64},
    {   0,   64},
    { 523,  127},
    {  78,   64},
    {   0,   64},
    { 415,  127},
    {  87,   64},
    {   0,   64},
    { 349,  254},
    { 523,  254},
    {   0,   64},
    { 698,  127},
    {  69,   64},
    { 349,  127},
    {   0,   64},
    { 311,   64},
    {   0,  127},
    { 311,  127},
    {   0,   64},
    { 262,   64},
    {  78,   64},
    {   0,   64},
    { 392,  254},
    {   0,   64},
    { 349,  699},
    {   0,  636},
    {  78,   64},
    {   0,   64},
    {  65,  127},
    {   0,  127},
    {  58,  127},
    {   0,  127},
    {  52,  127},
    {   0,  127},
    {  44,  381},
    {   0,  127},
    {  87,  254},
    {   0,  127},
    {  78,  127},
    {   0,  127},
    { 156,   64},
    {   0,   64},
    {  33,  127},
    {   0,  127},
    {  65,  127},
    {   0,  127},
    {  78,  127},
    {   0,  127},
    {  44,  318},
    {   0,  191},
    {  87,  127},
    {   0,  508},
    {  44,  127},
    {   0,   64},
    {  65,   64},
    {   0,  127},
    {  78,  127},
    {   0,  127},
    {  87,  127},
    {   0,  127},
    {  35,  381},
    {   0,  127},
    {  69,  254},
    {   0,  381},
    {  39,  127},
    {   0,   64},
    {  78,   64},
    {   0,  127},
    {  65,  191},
    {   0,   64},
    {  78,  191},
    {   0,   64},
    {  44,  445},
    {   0,  699},
    {  78,   64},
    {   0,   64},
    {  65,  127},
    {   0,  127},
    {  58,  127},
    {   0,  127},
    {  52,  127},
    {   0,  127},
    {  44,  381},
    {   0,  127},
    {  87,  254},
    {   0,  127},
    {  78,  127},
    {   0,  127},
    { 156,   64},
    {   0,   64},
    {  33,  127},
    {   0,  127},
    {  65,  127},
    {   0,  127},
    {  78,  127},
    {   0,  127},
    {  44,  318},
    {   0,  191},
    {  87,  127},
    {   0,  508},
    {  44,  127},
    {   0,   64},
    {  65,   64},
    {   0,  127},
    {  78,  127},
    {   0,  127},
    {  87,  127},
    {   0,  127},
    {  35,  381},
    {   0,  127},
    {  69,  254},
    {   0,  381},
    {  39,  127},
    {   0,   64},
    {  78,   64},
    {   0,  127},
    {  65,  191},
    {   0,   64},
    {  78,  191},
    {   0,   64},
    {  44,  445},
    {   0,  699},
    {  78,   64},
    {   0,   64},
    {  65,  127},
    {   0,  127},
    {  58,  127},
    {   0,  127},
    {  52,  127},
    {   0,  127},
    {  44,   64},
    {   0, 1907},
    {  44,  381},
    {   0,  127},
    {  87,  254},
    {   0,  127},
    {  78,  127},
    {   0,  127},
    { 156,   64},
    {   0,   64},
    {  33,  127},
    {   0,  127},
    {  65,  127},
    {   0,  127},
    {  78,  127},
    {   0,  127},
    {  44,  318},
    {   0,  191},
    {  87,  127},
    {   0,  508},
    {  44,  127},
    {   0,   64},
    {  65,   64},
    {   0,  127},
    {  78,  127},
    {   0,  127},
    {  87,  127},
    {   0,  127},
    {  35,  381},
    {   0,  127},
    {  69,  254},
    {   0,  381},
    {  39,  127},
    {   0,   64},
    {  78,   64},
    {   0,  127},
    {  65,  191},
    {   0,   64},
    {  78,  191},
    {   0,   64},
    {  44,  445},
    {   0,  699},
    {  78,   64},
    {   0,   64},
    {  65,  127},
    {   0,  127},
    {  58,  127},
    {   0,  127},
    {  52,  127},
    {   0,  127},
    {  44,  381},
    {   0,  127},
    {  87,  254},
    {   0,  127},
    {  78,  127},
    {   0,  127},
    { 156,   64},
    {   0,   64},
    {  33,  127},
    {   0,  127},
    {  65,  127},
    {   0,  127},
    {  78,  127},
    {   0,  127},
    {  44,  318},
    {   0,  191},
    {  87,  127},
    {   0,  508},
    {  44,  127},
    {   0,   64},
    {  65,   64},
    {   0,  127},
    {  78,  127},
    {   0,  127},
    {  87,  127},
    {   0,  127},
    {  35,  381},
    {   0,  127},
    {  69,  254},
    {   0,  381},
    {  39,  127},
    {   0,   64},
    {  78,   64},
    {   0,  127},
    {  65,  191},
    {   0,   64},
    {  78,  191},
    {   0,   64},
    {  44,  445},
    {   0,  699},
    {  78,   64},
    {   0,   64},
    {  65,  127},
    {   0,  127},
    {  58,  127},
    {   0,  127},
    {  52,  127},
    {   0,  127},
    { 349,  318},
    {  44,   64},
    {   0,  127},
    { 415,  318},
    {   0,   64},
    { 349,  127},
    {   0,  127},
    { 349,  127},
    { 466,  127},
    {  33,   64},
    {   0,   64},
    { 349,  127},
    {  65,   64},
    {   0,   64},
    { 311,   64},
    {  78,   64},
    {   0,  127},
    { 349,  381},
    {   0,  127},
    { 523,  318},
    {   0,   64},
    { 349,  127},
    {   0,  127},
    { 349,  127},
    { 554,  127},
    {  65,   64},
    {   0,   64},
    { 523,  127},
    {  78,   64},
    {   0,   64},
    { 415,  127},
    {  87,   64},
    {   0,   64},
    { 349,  254},
    { 523,  191},
    {   0,   64},
    { 698,  254},
    {  69,   64},
    { 349,   64},
    { 311,  127},
    {   0,  127},
    { 311,   64},
    {  39,   64},
    {   0,   64},
    { 262,   64},
    {  78,   64},
    {   0,   64},
    { 392,  191},
    {   0,   64},
    { 349,  763},
    {   0,  636},
    {  78,   64},
    {   0,   64},
    {  65,  127},
    {   0,  127},
    {  58,  127},
    {   0,  127},
    {  52,  127},
    {   0,  127},
    { 349,   64},
    {  44,  318},
    {   0,  127},
    { 415,   64},
    {  87,  191},
    {   0,  127},
    { 349,   64},
    {  78,   64},
    {   0,  127},
    { 349,   64},
    {   0,   64},
    { 466,   64},
    {  33,   64},
    {   0,  127},
    { 349,   64},
    {  65,   64},
    {   0,  127},
    { 311,   64},
    {  78,   64},
    {   0,  127},
    { 349,   64},
    {  44,  254},
    {   0,  191},
    { 523,   64},
    {  87,   64},
    {   0,  254},
    { 349,   64},
    {   0,  191},
    { 349,   64},
    {  44,   64},
    {   0,   64},
    {  65,   64},
    {   0,  127},
    { 523,   64},
    {  78,   64},
    {   0,  127},
    { 415,   64},
    {  87,   64},
    {   0,  127},
    { 349,   64},
    {  35,  191},
    { 523,   64},
    {  35,   64},
    {   0,  127},
    { 698,   64},
    {  69,  191},
    { 349,   64},
    {   0,   64},
    { 311,   64},
    {   0,  191},
    { 311,   64},
    {  39,   64},
    {   0,   64},
    {  78,   64},
    {   0,  127},
    { 392,   64},
    {  65,  127},
    {   0,   64},
    { 349,   64},
    {  78,  127},
    {   0,   64},
    {  44,  445},
    {   0,  699},
    { 104,   64},
    {   0,   64},
    {  78,  127},
    {   0,  127},
    {  87,   64},
    {   0,  191},
    {  98,   64},
    {   0,  191},
    { 104,  127},
    {   0, 1017},
    { 104,   64},
    {   0,   64},
    {  78,  127},
    {   0,  127},
    { 104,   64},
    {   0,  191},
    {  78,  127},
    {   0,  127},
    {  87,  127},
    {   0, 1017},
    {  87,   64},
    {   0,   64},
    {  78,  127},
    {   0,  127},
    {  87,   64},
    {   0,  191},
    {  78,  127},
    {   0,  127},
    {  92,  127},
    {   0, 1144},
    {  92,   64},
    {  78,   64},
    {   0,  127},
    {  92,   64},
    {   0,  191},
    { 104,   64},
    {   0,  191},
    {  78,  127},
    {   0, 1017},
    { 104,   64},
    {   0,   64},
    {  78,  127},
    {   0,  127},
    {  87,   64},
    {   0,  191},
    {  98,   64},
    {   0,  191},
    { 104,   64},
    {   0, 1080},
    { 104,   64},
    {   0,   64},
    {  78,  127},
    {   0,  127},
    { 104,   64},
    {   0,  191},
    {  78,  127},
    {   0,  127},
    {  87,   64},
    {   0, 1080},
    {  87,   64},
    {   0,   64},
    {  78,  127},
    {   0,  127},
    {  87,   64},
    {   0,  191},
    {  78,  127},
    {   0,  127},
    {  92,   64},
    {   0, 1080},
    {  92,   64},
    {   0,   64},
    {  78,  127},
    {   0,  127},
    {  92,   64},
    {   0,  191},
    { 104,   64},
    {   0,  191},
    {  78,  127},
    {   0, 1017},
    { 104,   64},
    {   0,   64},
    {  78,  127},
    {   0,  127},
    {  87,   64},
    {   0,  191},
    {  98,   64},
    {   0,  191},
    { 104,   64},
    {   0, 1080},
    { 104,   64},
    {   0,   64},
    {  78,  127},
    {   0,  127},
    { 104,  127},
    {   0,  127},
    {  78,  127},
    {   0,  127},
    {  87,  127},
    {   0, 1017},
    {  87,   64},
    {   0,   64},
    {  78,  127},
    {   0,  127},
    {  87,  127},
    {   0,  127},
    {  78,  127},
    {   0,  127},
    {  92,  127},
    {   0, 1017},
    {  92,   64},
    {   0,   64},
    {  78,  127},
    {   0,  127},
    {  92,   64},
    {   0,  191},
    { 104,   64},
    {   0,  191},
    {  78,  127},
    {   0, 1017},
    { 104,   64},
    {   0,   64},
    {  78,  127},
    {   0,  127},
    {  87,   64},
    {   0,  191},
    {  98,   64},
    {   0,  191},
    { 831,   64},
    {   0,  318},
    { 831,   64},
    {   0,  318},
    { 831,   64},
    {   0,  318},
    { 831,   64},
    {   0,   64},
    {  78,  127},
    {   0,  127},
    { 932,   64},
    {   0,  191},
    {1047,  127},
    {   0,  127},
    { 831,   64},
    {   0,  318},
    { 831,   64},
    {   0,  318},
    { 831,   64},
    {   0,  318},
    { 831,   64},
    {   0,   64},
    {  78,  127},
    {   0,  127},
    { 932,   64},
    {   0,  191},
    {1047,  127},
    {  78,   64},
    {   0,   64},
    { 831,   64},
    {   0,  318},
    { 831,   64},
    {   0,  318},
    { 831,   64},
    {   0,  318},
    { 831,   64},
    {   0,   64},
    {  78,  127},
    { 831,   64},
    {   0,   64},
    { 932,   64},
    {   0,  191},
    {1047,   64},
    {   0,  191},
    { 831,  127},
    {  78,   64},
    {   0,  191},
    { 831,   64},
    {  78,   64},
    {   0,  254},
    { 831,  127},
    {  78,   64},
    {   0,  191},
    { 831,   64},
    {   0,  191},
    { 831,   64},
    {   0,   64},
    { 932,   64},
    {   0,  191},
    {1047,   64},
    {   0,  191},
    { 831,   64},
    {  78,   64},
    {   0,  254},
    { 831,   64},
    {  78,   64},
    {   0,  254},
    { 831,   64},
    {  78,   64},
    {   0,  254},
    { 831,   64},
    {   0,  191},
    { 831,  127},
    {   0,  254},
    {1047,   64},
    {   0,  191},
    {  44,  381},
    {   0,  127},
    {  87,  254},
    {   0,  127},
    {  78,  127},
    {   0,  127},
    { 156,   64},
    {   0,   64},
    {  33,  127},
    {   0,  127},
    {  65,  127},
    {   0,  127},
    {  78,  127},
    {   0,  127},
    {  44,  318},
    {   0,  191},
    {  87,  127},
    {   0,  508},
    {  44,  127},
    {   0,   64},
    {  65,   64},
    {   0,  127},
    {  78,  127},
    {   0,  127},
    {  87,  127},
    {   0,  127},
    {  35,  381},
    {   0,  127},
    {  69,  254},
    {   0,  381},
    {  39,  127},
    {   0,   64},
    {  78,   64},
    {   0,  127},
    {  65,  191},
    {   0,   64},
    {  78,  191},
    {   0,   64},
    {  44,  445},
    {   0,  699},
    {  78,   64},
    {   0,   64},
    {  65,  127},
    {   0,  127},
    {  58,  127},
    {   0,  127},
    {  52,  127},
    {   0,  127},
    {  44,  381},
    {   0,  127},
    {  87,  254},
    {   0,  127},
    {  78,  127},
    {   0,  127},
    { 156,   64},
    {   0,   64},
    {  33,  127},
    {   0,  127},
    {  65,  127},
    {   0,  127},
    {  78,  127},
    {   0,  127},
    {  44,  318},
    {   0,  191},
    {  87,  127},
    {   0,  508},
    {  44,  127},
    {   0,   64},
    {  65,   64},
    {   0,  127},
    {  78,  127},
    {   0,  127},
    {  87,  127},
    {   0,  127},
    {  35,  381},
    {   0,  127},
    {  69,  254},
    {   0,  381},
    {  39,  127},
    {   0,   64},
    {  78,   64},
    {   0,  127},
    {  65,  191},
    {   0,   64},
    {  78,  191},
    {   0,   64},
    {  44,  445},
    {   0,  699},
    {  78,   64},
    {   0,   64},
    {  65,  127},
    {   0,  127},
    {  58,  127},
    {   0,  127},
    {  52,  127},
    {   0,  127},
    {  35,  381},
    {   0,  127},
    {  69,  254},
    {   0,  381},
    {  39,  127},
    {   0,   64},
    {  78,   64},
    {   0,  127},
    {  65,  191},
    {   0,   64},
    {  78,  191},
    {   0,   64},
    {  44,  445},
    {   0,  699},
    {  78,   64},
    {   0,   64},
    {  65,  127},
    {   0,  127},
    {  58,  127},
    {   0,  127},
    {  52,  127},
    {   0,  127},
    { 349,  318},
    {  44,   64},
    {   0,  127},
    { 415,  254},
    {   0,  127},
    { 349,  127},
    {   0,  127},
    { 349,  127},
    { 466,  191},
    {   0,   64},
    { 349,  127},
    {  65,   64},
    {   0,   64},
    { 311,  127},
    {   0,  127},
    { 349,  381},
    {   0,  127},
    { 523,  318},
    {   0,   64},
    { 349,  191},
    {   0,   64},
    { 349,  127},
    { 554,  127},
    {  65,   64},
    {   0,   64},
    { 523,  127},
    {  78,   64},
    {   0,   64},
    { 415,  127},
    {  87,   64},
    {   0,   64},
    { 349,  254},
    { 523,  254},
    {   0,   64},
    { 698,  127},
    {  69,   64},
    { 349,  127},
    {   0,   64},
    { 311,   64},
    {   0,  127},
    { 311,  127},
    {   0,   64},
    { 262,   64},
    {  78,   64},
    {   0,   64},
    { 392,  254},
    {   0,   64},
    { 349,  699},
    {   0,  636},
    {  78,   64},
    {   0,   64},
    {  65,  127},
    {   0,  127},
    {  58,  127},
    {   0,  127},
    {  52,  127},
    {   0,  127},
    { 349,  318},
    {  44,   64},
    {   0,  127},
    { 415,  254},
    {   0,  127},
    { 349,  127},
    {   0,  127},
    { 349,  127},
    { 466,  191},
    {   0,   64},
    { 349,  127},
    {  65,   64},
    {   0,   64},
    { 311,  127},
    {   0,  127},
    { 349,  381},
    {   0,  127},
    { 523,  318},
    {   0,   64},
    { 349,  191},
    {   0,   64},
    { 349,  127},
    { 554,  127},
    {  65,   64},
    {   0,   64},
    { 523,  127},
    {  78,   64},
    {   0,   64},
    { 415,  127},
    {  87,   64},
    {   0,   64},
    { 349,  254},
    { 523,  254},
    {   0,   64},
    { 698,  127},
    {  69,   64},
    { 349,  127},
    {   0,   64},
    { 311,   64},
    {   0,  127},
    { 311,  127},
    {   0,   64},
    { 262,   64},
    {  78,   64},
    {   0,   64},
    { 392,  254},
    {   0,   64},
    { 349,  699},
    {   0,  636},
    {  78,   64},
    {   0,   64},
    {  65,  127},
    {   0,  127},
    {  58,  127},
    {   0,  127},
    {  52,  127},
    {0, 500},
};

static const Note menu_move_sfx[] = {
    {880, 24},
    {0, 12},
    {1175, 28},
};

static const Note menu_confirm_sfx[] = {
    {1047, 32},
    {1319, 48},
};

static const Note menu_back_sfx[] = {
    {659, 36},
    {523, 56},
};

static const Note boost_sfx[] = {
    {659, 36},
    {784, 36},
    {988, 44},
    {1319, 64},
};

static const Note boost_ready_sfx[] = {
    {784, 38},
    {988, 38},
    {1175, 42},
    {1568, 72},
};

static const Note boost_trigger_sfx[] = {
    {1319, 42},
    {1568, 42},
    {1976, 54},
    {2637, 82},
};

static const Note checkpoint_sfx[] = {
    {1047, 42},
    {1319, 42},
    {1568, 56},
    {2093, 76},
};

static const Note crash_sfx[] = {
    {220, 70},
    {175, 82},
    {131, 120},
};

static const Note game_over_sfx[] = {
    {523, 96},
    {392, 112},
    {330, 132},
    {262, 220},
};

typedef struct {
    const Note *notes;
    uint32_t count;
    uint32_t index;
    uint32_t samples_left;
    uint32_t phase;
    uint32_t phase_step;
    uint16_t amplitude;
    uint8_t active;
    uint8_t loop;
} Game2MusicVoice;

static volatile Game2MusicVoice bgm_voice;
static volatile Game2MusicVoice sfx_voice;
static volatile uint32_t music_duty_width;

static uint32_t game2_music_note_count(void)
{
    return (uint32_t)(sizeof(bgm) / sizeof(bgm[0]));
}

static uint32_t game2_music_sequence_count(const Note *notes, uint32_t bytes)
{
    (void)notes;
    return bytes / (uint32_t)sizeof(Note);
}

static uint16_t game2_music_clamp_volume(uint8_t volume_percent)
{
    if (volume_percent > GAME2_MUSIC_MAX_VOLUME_PERCENT) {
        volume_percent = GAME2_MUSIC_MAX_VOLUME_PERCENT;
    }
    return (uint16_t)((((uint32_t)GAME2_MUSIC_DAC_MID - 1U) * volume_percent) / 100U);
}

static void game2_music_write_dac(uint16_t value)
{
    DAC1->DHR12R1 = value & 0x0FFFU;
}

static void game2_music_configure_dac_timer(void)
{
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN;
    GPIOA->MODER |= (3U << (4U * 2U));

    RCC->APB1ENR1 |= RCC_APB1ENR1_DAC1EN;
    DAC1->CR = 0U;
    DAC1->CR |= DAC_CR_EN1;

    RCC->APB1ENR1 |= RCC_APB1ENR1_TIM6EN;
    TIM6->CR1 = 0U;
    TIM6->PSC = 0U;
    TIM6->ARR = (SystemCoreClock / GAME2_MUSIC_SAMPLE_RATE) - 1U;
    TIM6->DIER = TIM_DIER_UIE;
    TIM6->CR1 = TIM_CR1_ARPE;
    TIM6->CNT = 0U;
    TIM6->SR = 0U;

    HAL_NVIC_SetPriority(TIM6_DAC_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(TIM6_DAC_IRQn);
}

static uint8_t game2_music_load_current_note(volatile Game2MusicVoice *voice)
{
    uint32_t guard = voice->count;

    while (guard > 0U) {
        const Note *note;

        if (voice->index >= voice->count) {
            if (voice->loop && voice->count > 0U) {
                voice->index = 0U;
            } else {
                voice->active = 0U;
                voice->samples_left = 0U;
                voice->phase_step = 0U;
                return 0U;
            }
        }

        note = &voice->notes[voice->index++];
        guard--;

        if (note->ms == 0U) {
            continue;
        }

        voice->samples_left = (((uint32_t)note->ms * GAME2_MUSIC_SAMPLE_RATE) + 999U) / 1000U;
        if (voice->samples_left == 0U) {
            voice->samples_left = 1U;
        }

        voice->phase = 0U;
        if (note->freq == 0U) {
            voice->phase_step = 0U;
        } else {
            voice->phase_step = (uint32_t)(((uint64_t)note->freq << 32) / GAME2_MUSIC_SAMPLE_RATE);
        }
        return 1U;
    }

    voice->active = 0U;
    voice->samples_left = 0U;
    voice->phase_step = 0U;
    return 0U;
}

static uint16_t game2_music_render_voice_sample(volatile Game2MusicVoice *voice)
{
    uint16_t sample = GAME2_MUSIC_DAC_MID;

    if (!voice->active) {
        return sample;
    }

    if (voice->samples_left == 0U && !game2_music_load_current_note(voice)) {
        return sample;
    }

    if (voice->phase_step != 0U) {
        uint32_t half_phase = voice->phase & (GAME2_MUSIC_HALF_PHASE - 1U);
        if (half_phase < music_duty_width) {
            if ((voice->phase & GAME2_MUSIC_PHASE_TOP_BIT) == 0U) {
                sample = (uint16_t)(GAME2_MUSIC_DAC_MID + voice->amplitude);
            } else {
                sample = (uint16_t)(GAME2_MUSIC_DAC_MID - voice->amplitude);
            }
        }
        if (sample > GAME2_MUSIC_DAC_MAX) {
            sample = GAME2_MUSIC_DAC_MAX;
        }
        voice->phase += voice->phase_step;
    }

    if (voice->samples_left > 0U) {
        voice->samples_left--;
    }

    return sample;
}

static void game2_music_start_sequence(volatile Game2MusicVoice *voice,
                                       const Note *notes,
                                       uint32_t count,
                                       uint8_t loop,
                                       uint8_t volume_percent)
{
    voice->notes = notes;
    voice->count = count;
    voice->index = 0U;
    voice->samples_left = 0U;
    voice->phase = 0U;
    voice->phase_step = 0U;
    voice->amplitude = game2_music_clamp_volume(volume_percent);
    voice->loop = loop;
    voice->active = (notes != 0 && count > 0U) ? 1U : 0U;

    if (voice->active) {
        (void)game2_music_load_current_note(voice);
    }
}

static void game2_music_start_sfx(const Note *notes, uint32_t count)
{
    __disable_irq();
    game2_music_configure_dac_timer();
    music_duty_width = (uint32_t)(((uint64_t)GAME2_MUSIC_HALF_PHASE * GAME2_MUSIC_DUTY_PERCENT) / 100U);
    game2_music_start_sequence(&sfx_voice, notes, count, 0U, GAME2_SFX_DEFAULT_VOLUME_PERCENT);
    if (sfx_voice.active || bgm_voice.active) {
        TIM6->CR1 |= TIM_CR1_CEN;
    }
    __enable_irq();
}

void Game2Music_Start(void)
{
    Audio_Stop();

    __disable_irq();
    music_duty_width = (uint32_t)(((uint64_t)GAME2_MUSIC_HALF_PHASE * GAME2_MUSIC_DUTY_PERCENT) / 100U);
    game2_music_configure_dac_timer();
    game2_music_start_sequence(&bgm_voice, bgm, game2_music_note_count(), 1U, GAME2_MUSIC_DEFAULT_VOLUME_PERCENT);
    if (bgm_voice.active || sfx_voice.active) {
        TIM6->CR1 |= TIM_CR1_CEN;
    }
    __enable_irq();
}

void Game2Music_Stop(void)
{
    __disable_irq();
    bgm_voice.active = 0U;
    bgm_voice.samples_left = 0U;
    bgm_voice.phase_step = 0U;
    sfx_voice.active = 0U;
    sfx_voice.samples_left = 0U;
    sfx_voice.phase_step = 0U;
    TIM6->CR1 &= ~TIM_CR1_CEN;
    __enable_irq();
    game2_music_write_dac(GAME2_MUSIC_DAC_MID);
    Audio_Init();
}

void Game2Music_StopBgm(void)
{
    __disable_irq();
    bgm_voice.active = 0U;
    bgm_voice.samples_left = 0U;
    bgm_voice.phase_step = 0U;
    if (!sfx_voice.active) {
        TIM6->CR1 &= ~TIM_CR1_CEN;
    }
    __enable_irq();
}

uint8_t Game2Music_IsPlaying(void)
{
    return (bgm_voice.active || sfx_voice.active) ? 1U : 0U;
}

void Game2Music_TIM6_IRQHandler(void)
{
    uint16_t sample = GAME2_MUSIC_DAC_MID;

    if (sfx_voice.active) {
        sample = game2_music_render_voice_sample(&sfx_voice);
    } else if (bgm_voice.active) {
        sample = game2_music_render_voice_sample(&bgm_voice);
    } else {
        TIM6->CR1 &= ~TIM_CR1_CEN;
    }

    if (!sfx_voice.active && !bgm_voice.active) {
        TIM6->CR1 &= ~TIM_CR1_CEN;
    }

    game2_music_write_dac(sample);
}

void Game2Music_PlayMenuMove(void)
{
    game2_music_start_sfx(menu_move_sfx, game2_music_sequence_count(menu_move_sfx, sizeof(menu_move_sfx)));
}

void Game2Music_PlayMenuConfirm(void)
{
    game2_music_start_sfx(menu_confirm_sfx, game2_music_sequence_count(menu_confirm_sfx, sizeof(menu_confirm_sfx)));
}

void Game2Music_PlayMenuBack(void)
{
    game2_music_start_sfx(menu_back_sfx, game2_music_sequence_count(menu_back_sfx, sizeof(menu_back_sfx)));
}

void Game2Music_PlayBoostSfx(void)
{
    game2_music_start_sfx(boost_sfx, game2_music_sequence_count(boost_sfx, sizeof(boost_sfx)));
}

void Game2Music_PlayBoostReadySfx(void)
{
    game2_music_start_sfx(boost_ready_sfx, game2_music_sequence_count(boost_ready_sfx, sizeof(boost_ready_sfx)));
}

void Game2Music_PlayBoostTriggerSfx(void)
{
    game2_music_start_sfx(boost_trigger_sfx, game2_music_sequence_count(boost_trigger_sfx, sizeof(boost_trigger_sfx)));
}

void Game2Music_PlayCheckpointSfx(void)
{
    game2_music_start_sfx(checkpoint_sfx, game2_music_sequence_count(checkpoint_sfx, sizeof(checkpoint_sfx)));
}

void Game2Music_PlayCrashSfx(void)
{
    game2_music_start_sfx(crash_sfx, game2_music_sequence_count(crash_sfx, sizeof(crash_sfx)));
}

void Game2Music_PlayGameOverSfx(void)
{
    game2_music_start_sfx(game_over_sfx, game2_music_sequence_count(game_over_sfx, sizeof(game_over_sfx)));
}
