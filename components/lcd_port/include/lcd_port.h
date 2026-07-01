#ifndef LCD_PORT_H
#define LCD_PORT_H

#include <stdbool.h>
#include <stdint.h>

#define LCD_PORT_LOCK_FOREVER UINT32_MAX

/*
 * Inicializa el hardware de la pantalla, el driver esp_lcd y LVGL.
 * Debe llamarse una sola vez antes de lcd_manager_init().
 */
void lcd_port_init(void);

/*
 * LVGL no es thread-safe. Cualquier modulo que modifique objetos LVGL
 * debe tomar este lock antes y liberarlo despues.
 */
bool lcd_port_lock(uint32_t timeout_ms);
void lcd_port_unlock(void);

bool lcd_port_is_initialized(void);

#endif
