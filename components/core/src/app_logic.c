#include "app_logic.h"
#include "esp_log.h"
#include "ev_queue.h"
#include "product_db.h"
#include <string.h>

static const char *TAG = "APP_LOGIC";

static AppContext s_context;

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

static void action_setup(void) {
}

static void action_reset_to_idle(void) {
    app_logic_reset_context();
}

static void action_throw_error(void) {
    const char *err = app_logic_get_last_error();
    const char *msg = (err != NULL && err[0] != '\0') ? err : "Error desconocido";
    ESP_LOGE(TAG, "Error display: %s", msg);

    // mostrar error en el display
}

static void action_db_lookup(void) {
    Product pending;

    if (!app_logic_get_active_product(&pending) || pending.id[0] == '\0') {
        app_logic_set_last_error("No hay producto activo");
        ev_queue_post(EV_PRODUCT_NOT_FOUND);
        return;
    }

    Product stored;
    memset(&stored, 0, sizeof(stored));

    if (!product_db_find_by_id(pending.id, &stored)) {
        char err[64];
        snprintf(err, sizeof(err), "Producto no registrado: %s", pending.id);
        app_logic_set_last_error(err);
        ESP_LOGE(TAG, "Producto no registrado -> ID=%s", pending.id);
        ev_queue_post(EV_PRODUCT_NOT_FOUND);
        return;
    }

    app_logic_set_active_product(&stored);

    ESP_LOGI(TAG, "Producto encontrado -> ID=%s | Nombre=%s | Stock=%lu", stored.id, stored.name,
             (unsigned long)stored.stock);

    return;
}

static void action_prompt_add_product(void) {
}

static void action_enter_quantity_selection(void) {
    app_logic_set_selected_quantity(1);

    char msg[32];
    snprintf(msg, sizeof(msg), "Cantidad: 1");

    // show changes on display
}

static void action_quantity_up(void) {
    uint32_t qty = app_logic_get_selected_quantity();
    if (qty < 99) {
        qty++;
        app_logic_set_selected_quantity(qty);
    }
    char msg[32];
    snprintf(msg, sizeof(msg), "Cantidad: %lu", (unsigned long)qty);

    // show changes on display
}

static void action_quantity_down(void) {
    uint32_t qty = app_logic_get_selected_quantity();
    if (qty > 1) {
        qty--;
        app_logic_set_selected_quantity(qty);
    }
    char msg[32];
    snprintf(msg, sizeof(msg), "Cantidad: %lu", (unsigned long)qty);

    // show changes on display
}

static void action_stock_update(void) {
    Product current;

    if (!app_logic_get_active_product(&current)) {
        app_logic_set_last_error("No hay producto activo para actualizar");
        ev_queue_post(EV_STOCK_UPDATE_FAILURE);
        return;
    }

    uint32_t qty = app_logic_get_selected_quantity();
    Product updated;
    memset(&updated, 0, sizeof(updated));

    if (!product_db_add_stock(current.id, qty, &updated)) {
        char err[64];
        snprintf(err, sizeof(err), "No se pudo actualizar stock: %s", current.id);
        app_logic_set_last_error(err);

        ESP_LOGE(TAG, "%s", err);
        ev_queue_post(EV_STOCK_UPDATE_FAILURE);
        return;
    }

    app_logic_set_active_product(&updated);

    ESP_LOGI(TAG, "Stock actualizado -> ID=%s | Agregado=%lu | Stock=%lu", updated.id, (unsigned long)qty,
             (unsigned long)updated.stock);

    ev_queue_post(EV_STOCK_UPDATE_SUCCESS);
}

static void action_product_overview(void) {
    // get active product
    Product product;

    ESP_LOGI("LCD_B", "ID:%s | Nombre:%s | Stock:%lu", product.id, product.name, (unsigned long)product.stock);

    // aca el display lo tiene que mostrar
}

static void action_mqtt_publish(void) {
    Product product;

    if (!app_logic_get_active_product(&product)) {
        ESP_LOGW(TAG, "MQTT publish: no active product");
        ev_queue_post(EV_MQTT_PUBLISH_FAILURE);
        return;
    }

    ESP_LOGI(TAG, "Publicando por MQTT -> ID=%s", product.id);

    // aca falta llamar a la logica del mqtt para hacer el publish
    ev_queue_post(EV_MQTT_PUBLISH_SUCCESS);
}
