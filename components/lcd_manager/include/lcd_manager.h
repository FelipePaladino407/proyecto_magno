#ifndef LCD_MANAGER_H
#define LCD_MANAGER_H

#include "shared.h"

void lcd_manager_init(void);

void lcd_show_waiting(void);

void lcd_show_product_screen(
    const Product *product,
    int add_amount);

void lcd_show_success(
    Product *product);

void lcd_show_cancelled(void);

void lcd_show_error(
    const char *msg);

#endif