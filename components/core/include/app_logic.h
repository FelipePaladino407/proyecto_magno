#ifndef APP_LOGIC_H
#define APP_LOGIC_H

#include "shared_types.h"
#include <stdbool.h>
#include <stdint.h>

// Ahora las variables estan encapsuladas en un struct
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

// Callbacks
typedef struct {
    void (*on_display_message)(const char *title, const char *message);
    void (*on_display_product)(const Product *product);
    void (*on_enter_scan_processing)(void);
    void (*on_enter_db_lookup)(void);
    void (*on_enter_stock_updating)(void);
    void (*on_enter_mqtt_publishing)(void);
    void (*on_mqtt_publish_success)(void);
    void (*on_mqtt_publish_failure)(void);
    void (*on_link_transmit)(const Product *product, uint32_t quantity);
    void (*on_link_scan_received)(void);
    void (*on_enter_error_display)(const char *error_msg);

} AppLogicCallbacks;

void app_logic_register(const AppLogicCallbacks *callbacks);

const AppLogicCallbacks *app_logic_get(void);

void app_logic_board_a_init(void);
void app_logic_board_b_init(void);

#endif
