#pragma once

#include <stdint.h>
#include <stdbool.h>

#define TOUCHPAD_NUM_BUTTONS 4
#define TOUCHPAD_POLL_INTERVAL_MS 50

typedef enum {
    TOUCHPAD_BUTTON_UP = 0,
    TOUCHPAD_BUTTON_CONFIRM = 1,
    TOUCHPAD_BUTTON_DOWN = 2,
    TOUCHPAD_BUTTON_CANCEL = 3
} touchpad_button_t;

/**
 * @brief Inicializa el controlador touch con los canales físicos configurados.
 *
 * Canales usados actualmente:
 * botón lógico 0 -> TOUCH channel 1
 * botón lógico 1 -> TOUCH channel 2
 * botón lógico 2 -> TOUCH channel 3
 * botón lógico 3 -> TOUCH channel 5
 */
void touchpad_init(void);

/**
 * @brief Consulta si un botón está siendo presionado actualmente.
 *
 * Esta función lee el estado físico actual. Puede devolver true muchas veces
 * si se mantiene el dedo sobre el botón.
 *
 * @param button_index Índice lógico del botón: 0 a TOUCHPAD_NUM_BUTTONS - 1.
 * @return true si está presionado.
 * @return false si no está presionado, si el índice es inválido o si no está inicializado.
 */
void touchpad_start_task(void);
bool touchpad_is_pressed(uint8_t button_index);