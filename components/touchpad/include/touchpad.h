
#pragma once

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Inicializa el controlador touch con los canales 1, 2, 3 y 5.
 *        Realiza el escaneo inicial para calibrar los benchmarks y umbrales.
 */
void touchpad_init(void);

/**
 * @brief Consulta si un botón está siendo presionado (polling, sin FreeRTOS).
 *
 * @param button_index  Índice lógico del botón (0 a 3)
 * @return true  si el canal está activo (presionado)
 * @return false si no está presionado o el índice es inválido
 */
bool touchpad_is_pressed(uint8_t button_index);
void touchpad_start_task(void);