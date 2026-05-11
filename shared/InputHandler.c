#include "InputHandler.h"

#include "main.h"
#include "stm32l4xx_hal.h"

InputState current_input = {0};

typedef struct {
    GPIO_TypeDef *port;
    uint16_t pin;
    uint8_t raw_down;
    uint8_t stable_down;
    uint32_t raw_change_ms;
    uint32_t last_press_ms;
} ButtonEdge;

#define INPUT_DEBOUNCE_MS       40U
#define INPUT_PRESS_LOCKOUT_MS  160U

static ButtonEdge button_confirm = {BTN8_GPIO_Port, BTN8_Pin, 0U, 0U, 0U, 0U};
static ButtonEdge button_pause = {BTN4_GPIO_Port, BTN4_Pin, 0U, 0U, 0U, 0U};
static ButtonEdge button_back = {GPIOC, GPIO_PIN_12, 0U, 0U, 0U, 0U};

static uint8_t button_raw_down(const ButtonEdge *button)
{
    return (HAL_GPIO_ReadPin(button->port, button->pin) == GPIO_PIN_RESET) ? 1U : 0U;
}

static uint8_t button_read_pressed_edge(ButtonEdge *button, uint32_t now)
{
    uint8_t raw_down = button_raw_down(button);
    uint8_t pressed = 0U;

    if (raw_down != button->raw_down) {
        button->raw_down = raw_down;
        button->raw_change_ms = now;
    }

    if ((uint32_t)(now - button->raw_change_ms) >= INPUT_DEBOUNCE_MS
        && button->stable_down != button->raw_down) {
        button->stable_down = button->raw_down;

        if (button->stable_down
            && (uint32_t)(now - button->last_press_ms) >= INPUT_PRESS_LOCKOUT_MS) {
            pressed = 1U;
            button->last_press_ms = now;
        }
    }

    return pressed;
}

void Input_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    __HAL_RCC_GPIOC_CLK_ENABLE();
    GPIO_InitStruct.Pin = GPIO_PIN_12;
    GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);
    HAL_NVIC_SetPriority(EXTI15_10_IRQn, 2, 0);
    HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);

    uint32_t now = HAL_GetTick();
    button_confirm.raw_down = button_raw_down(&button_confirm);
    button_pause.raw_down = button_raw_down(&button_pause);
    button_back.raw_down = button_raw_down(&button_back);
    button_confirm.stable_down = button_confirm.raw_down;
    button_pause.stable_down = button_pause.raw_down;
    button_back.stable_down = button_back.raw_down;
    button_confirm.raw_change_ms = now;
    button_pause.raw_change_ms = now;
    button_back.raw_change_ms = now;
    button_confirm.last_press_ms = now - INPUT_PRESS_LOCKOUT_MS;
    button_pause.last_press_ms = now - INPUT_PRESS_LOCKOUT_MS;
    button_back.last_press_ms = now - INPUT_PRESS_LOCKOUT_MS;

    current_input.btn1_pressed = 0U;
    current_input.btn2_pressed = 0U;
    current_input.btn3_pressed = 0U;
    current_input.btn1_down = button_pause.stable_down;
    current_input.btn2_down = button_confirm.stable_down;
    current_input.btn3_down = button_back.stable_down;
}

void Input_Read(void)
{
    uint32_t now = HAL_GetTick();

    current_input.btn1_pressed = button_read_pressed_edge(&button_pause, now);
    current_input.btn2_pressed = button_read_pressed_edge(&button_confirm, now);
    current_input.btn3_pressed = button_read_pressed_edge(&button_back, now);
    current_input.btn1_down = button_pause.stable_down;
    current_input.btn2_down = button_confirm.stable_down;
    current_input.btn3_down = button_back.stable_down;
}

void Input_EXTI_Callback(uint16_t gpio_pin)
{
    (void)gpio_pin;
}
