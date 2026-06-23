#include "app_logic.h"
#include <string.h>

static AppLogicCallbacks s_callbacks;
static AppContext s_context;

void app_logic_register(const AppLogicCallbacks *callbacks) {
    memset(&s_callbacks, 0, sizeof(s_callbacks));

    if (callbacks != NULL) {
        s_callbacks = *callbacks;
    }
}

const AppLogicCallbacks *app_logic_get(void) {
    return &s_callbacks;
}

// context para la logica
AppContext *app_logic_get_context(void) {
    return &s_context;
}

bool app_logic_get_active_product(Product *out) {
    if (s_context.current_product_valid && out != NULL) {
        *out = s_context.current_product;
        return true;
    }
    return false;
}

uint32_t app_logic_get_selected_quantity(void) {
    return s_context.current_product_quantity;
}

const char *app_logic_get_last_error(void) {
    return s_context.last_error;
}

void app_logic_set_pending_scan(const char *id, const char *name) {
    memset(&s_context.current_product, 0, sizeof(Product));
    if (id != NULL) {
        strncpy(s_context.current_product.id, id, sizeof(s_context.current_product.id) - 1);
        s_context.current_product.id[sizeof(s_context.current_product.id) - 1] = '\0';
    }
    if (name != NULL) {
        strncpy(s_context.current_product.name, name, sizeof(s_context.current_product.name) - 1);
        s_context.current_product.name[sizeof(s_context.current_product.name) - 1] = '\0';
    }
    s_context.current_product.stock = 0;
    s_context.current_product_valid = (id != NULL && id[0] != '\0');
}

void app_logic_set_selected_quantity(uint32_t quantity) {
    s_context.current_product_quantity = quantity;
}

void app_logic_set_active_product(const Product *product) {
    if (product != NULL) {
        s_context.current_product = *product;
        s_context.current_product_valid = true;
    } else {
        s_context.current_product_valid = false;
    }
}

void app_logic_set_last_error(const char *msg) {
    if (msg != NULL) {
        strncpy(s_context.last_error, msg, sizeof(s_context.last_error) - 1);
        s_context.last_error[sizeof(s_context.last_error) - 1] = '\0';
    } else {
        s_context.last_error[0] = '\0';
    }
}

void app_logic_reset_context(void) {
    s_context.current_product_valid = false;
    s_context.current_product_quantity = 1;
    s_context.last_error[0] = '\0';
}
