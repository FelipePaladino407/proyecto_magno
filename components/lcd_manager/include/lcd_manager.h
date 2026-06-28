#ifndef LCD_MANAGER_H
#define LCD_MANAGER_H

#include "shared_types.h"

void lcd_manager_init(void);

void lcd_show_waiting(void);

void lcd_show_product(const Product *product, int cantidad_agregar);

void lcd_show_added(const Product *product);

void lcd_show_cancelled(void);

void lcd_show_error(const char *mensaje);

#endif
