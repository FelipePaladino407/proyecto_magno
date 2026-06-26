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

static void action_setup(void);
static void action_reset_to_idle(void);
static void action_throw_error(void);
static void action_db_lookup(void);
static void action_prompt_add_product(void);
static void action_enter_quantity_selection(void);
static void action_quantity_up(void);
static void action_quantity_down(void);
static void action_stock_update(void);
static void action_product_overview(void);
static void action_mqtt_publish(void);

#endif
