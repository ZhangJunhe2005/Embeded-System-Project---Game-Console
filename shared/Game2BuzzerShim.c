#include "Buzzer.h"

Buzzer_cfg_t buzzer_cfg = {0};

void buzzer_init(Buzzer_cfg_t* cfg)
{
    (void)cfg;
}

uint8_t buzzer_is_running(Buzzer_cfg_t* cfg)
{
    (void)cfg;
    return 0U;
}

void buzzer_off(Buzzer_cfg_t* cfg)
{
    (void)cfg;
}

void buzzer_tone(Buzzer_cfg_t* cfg, uint32_t freq_hz, uint8_t volume_percent)
{
    (void)cfg;
    (void)freq_hz;
    (void)volume_percent;
}

void buzzer_note(Buzzer_cfg_t* cfg, Buzzer_Note_t note, uint8_t volume_percent)
{
    (void)cfg;
    (void)note;
    (void)volume_percent;
}
