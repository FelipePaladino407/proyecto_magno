#ifndef APP_LOGIC_H
#define APP_LOGIC_H

#include "shared_types.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    Product current_product;
    bool current_product_valid;
    uint32_t current_product_quantity;
    char last_error[64];
} AppContext;

// Getters
AppContext* app_logic_get_context(void);
bool app_logic_get_active_product(Product *out);
uint32_t app_logic_get_selected_quantity(void);
const char *app_logic_get_last_error(void);

// Setters
void app_logic_set_pending_scan(const char *id, const char *name);
void app_logic_set_selected_quantity(uint32_t quantity);
void app_logic_set_active_product(const Product *product);
void app_logic_set_last_error(const char *msg);
void app_logic_reset_context(void);

void app_logic_board_a_init(void);
void app_logic_board_b_init(void);

#endif
