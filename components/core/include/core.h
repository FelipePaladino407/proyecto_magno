#ifndef APP_LOGIC_H
#define APP_LOGIC_H

#include "shared_types.h"
#include <stdbool.h>
#include <stdint.h>
#include <sys/stat.h>

typedef struct {
    Product current_product;
    bool current_product_valid;
    uint32_t current_product_quantity;
    char last_error[64];
} SystemContext;

DeviceMode sys_get_mode(void);
void sys_set_mode(DeviceMode);

// Getters
SystemContext* sys_get_context(void);
bool sys_get_active_product(Product *out);
uint32_t sys_get_selected_quantity(void);
const char *sys_get_last_error(void);

// Setters
void sys_set_pending_scan(const char *id, const char *name, bool valid);
void sys_set_selected_quantity(uint32_t quantity);
void sys_set_active_product(const Product *product);
void sys_set_last_error(const char *msg);
void sys_reset_context(void);

// whatever
bool sys_on_qr_detected(const char *id, const char *name);
bool sys_on_qr_invalid(const char *reason);

void safe_copy(char *dest, const char *src, size_t dest_size);

void action_setup(void);
void action_retry_setup(void);
void action_reset_to_idle(void);
void action_throw_error(void);
void action_db_lookup(void);
void action_prompt_add_product(void);
void action_enter_quantity_selection(void);
void action_quantity_up(void);
void action_quantity_down(void);
void action_product_overview(void);
void action_stock_update(void);
void action_mqtt_publish(void);

#endif
