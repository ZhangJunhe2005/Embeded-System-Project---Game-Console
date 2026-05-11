#ifndef INPUT_HANDLER_H
#define INPUT_HANDLER_H

#include <stdint.h>
#include "stm32l4xx_hal.h"

typedef struct {
    uint8_t btn1_pressed;
    uint8_t btn2_pressed;
    uint8_t btn3_pressed;
    uint8_t btn1_down;
    uint8_t btn2_down;
    uint8_t btn3_down;
} InputState;

extern InputState current_input;

void Input_Init(void);
void Input_Read(void);
void Input_EXTI_Callback(uint16_t gpio_pin);

#endif
