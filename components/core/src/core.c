#include "core.h"
#include "esp_err.h"
#include "esp_log.h"
#include "ev_queue.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/projdefs.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "http_handler.h"
#include "input_handler.h"
#include "lcd_manager.h"
#include "lcd_port.h"
#include "logger.h"
#include "mqtt_handler.h"
#include "ntp_handler.h"
#include "product_db.h"
#include "qr_handler.h"
#include "rgb_led.h"
#include "shared_types.h"
#include "wifi_manager.h"
#include <stdbool.h>

static const char *TAG = "CORE";
static DeviceMode s_device_mode;
static SystemContext s_context;
static Logger logger_local;
static Logger logger_recibido;

void safe_copy(char *dest, const char *src, size_t dest_size) {
    if (dest == NULL || dest_size == 0) {
        return;
    }

    if (src == NULL) {
        dest[0] = '\0';
        return;
    }

    strncpy(dest, src, dest_size - 1);
    dest[dest_size - 1] = '\0';
}

DeviceMode sys_get_mode(void) {
    return s_device_mode;
}

void sys_set_mode(DeviceMode mode) {
    s_device_mode = mode;
    ESP_LOGI(TAG, "Device mode set to %d", mode);
}

// context para la logica
SystemContext *sys_get_context(void) {
    return &s_context;
}

bool sys_get_active_product(Product *out) {
    if (s_context.current_product_valid && out != NULL) {
        *out = s_context.current_product;
        return true;
    }
    return false;
}

uint32_t sys_get_selected_quantity(void) {
    return s_context.current_product_quantity;
}

const char *sys_get_last_error(void) {
    return s_context.last_error;
}

void sys_set_pending_scan(const char *id, const char *name, bool valid) {
    memset(&s_context.current_product, 0, sizeof(Product));

    safe_copy(s_context.current_product.id, id != NULL ? id : "", sizeof(s_context.current_product.id));

    safe_copy(s_context.current_product.name, name != NULL ? name : "", sizeof(s_context.current_product.name));

    s_context.current_product.stock = 0;
    s_context.current_product_valid = valid;
}

void sys_set_selected_quantity(uint32_t quantity) {
    s_context.current_product_quantity = quantity;
}

void sys_set_active_product(const Product *product) {
    if (product != NULL) {
        s_context.current_product = *product;
        s_context.current_product_valid = true;
    } else {
        s_context.current_product_valid = false;
    }
}

void sys_set_last_error(const char *msg) {
    if (msg != NULL) {
        strncpy(s_context.last_error, msg, sizeof(s_context.last_error) - 1);
        s_context.last_error[sizeof(s_context.last_error) - 1] = '\0';
    } else {
        s_context.last_error[0] = '\0';
    }
}

void sys_reset_context(void) {
    s_context.current_product_valid = false;
    s_context.current_product_quantity = 1;
    s_context.last_error[0] = '\0';
}

void action_setup(void) {
    esp_err_t err = ESP_OK;

    err |= wifi_manager_init();
    err |= http_handler_init();
    err |= rgb_led_init();
    err |= ntp_clock_init();

    if (s_device_mode == DEVICE_MODE_LCD) {
        err |= product_db_init();
        err |= button_int_config();
        lcd_port_init();

        product_db_load_default_catalog();

        logger_init(&logger_local, "local");
        logger_init(&logger_recibido, "recibido");
        mqtt_handler_set_loggers(&logger_local, &logger_recibido);
    } else {
        // err |= qr_scanner_handler_init();
    }

    err |= mqtt_handler_init();

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Setup success");
        ev_queue_post(EV_SETUP_SUCCESS);
    } else {
        ESP_LOGE(TAG, "Setup failure");
        ev_queue_post(EV_SETUP_FAILURE);
    }
}

void action_retry_setup(void) {
    vTaskDelay(pdMS_TO_TICKS(1000));
    ev_queue_post(EV_SETUP);
}

void action_reset_to_idle(void) {
    sys_reset_context();
}

void action_throw_error(void) {
    const char *err = sys_get_last_error();
    const char *msg = (err != NULL && err[0] != '\0') ? err : "Error desconocido";
    ESP_LOGE(TAG, "Error display: %s", msg);

    // mostrar error en el display
}

void action_db_lookup(void) {
    Product pending;

    if (!sys_get_active_product(&pending) || pending.id[0] == '\0') {
        sys_set_last_error("No hay producto activo");
        ev_queue_post(EV_PRODUCT_NOT_FOUND);
        return;
    }

    Product stored;
    memset(&stored, 0, sizeof(stored));

    if (!product_db_find_by_id(pending.id, &stored)) {
        char err[64];
        snprintf(err, sizeof(err), "Producto no registrado: %s", pending.id);
        sys_set_last_error(err);
        ESP_LOGE(TAG, "Producto no registrado -> ID=%s", pending.id);
        ev_queue_post(EV_PRODUCT_NOT_FOUND);
        return;
    }

    sys_set_active_product(&stored);

    ESP_LOGI(TAG, "Producto encontrado -> ID=%s | Nombre=%s | Stock=%lu", stored.id, stored.name,
             (unsigned long)stored.stock);

    return;
}

void action_prompt_add_product(void) {
}

void action_enter_quantity_selection(void) {
    sys_set_selected_quantity(1);

    char msg[32];
    snprintf(msg, sizeof(msg), "Cantidad: 1");

    // show changes on display
}

void action_quantity_up(void) {
    uint32_t qty = sys_get_selected_quantity();
    if (qty < 99) {
        qty++;
        sys_set_selected_quantity(qty);
    }
    char msg[32];
    snprintf(msg, sizeof(msg), "Cantidad: %lu", (unsigned long)qty);

    // show changes on display
}

void action_quantity_down(void) {
    uint32_t qty = sys_get_selected_quantity();
    if (qty > 1) {
        qty--;
        sys_set_selected_quantity(qty);
    }
    char msg[32];
    snprintf(msg, sizeof(msg), "Cantidad: %lu", (unsigned long)qty);

    // show changes on display
}

void action_stock_update(void) {
    Product current;

    if (!sys_get_active_product(&current)) {
        sys_set_last_error("No hay producto activo para actualizar");
        ev_queue_post(EV_STOCK_UPDATE_FAILURE);
        return;
    }

    uint32_t qty = sys_get_selected_quantity();
    Product updated;
    memset(&updated, 0, sizeof(updated));

    if (!product_db_add_stock(current.id, qty, &updated)) {
        char err[64];
        snprintf(err, sizeof(err), "No se pudo actualizar stock: %s", current.id);
        sys_set_last_error(err);

        ESP_LOGE(TAG, "%s", err);
        ev_queue_post(EV_STOCK_UPDATE_FAILURE);
        return;
    }

    sys_set_active_product(&updated);

    ESP_LOGI(TAG, "Stock actualizado -> ID=%s | Agregado=%lu | Stock=%lu", updated.id, (unsigned long)qty,
             (unsigned long)updated.stock);

    ev_queue_post(EV_STOCK_UPDATE_SUCCESS);
}

void action_product_overview(void) {
    // get active product
    Product product;

    ESP_LOGI("LCD_B", "ID:%s | Nombre:%s | Stock:%lu", product.id, product.name, (unsigned long)product.stock);

    // aca el display lo tiene que mostrar
}

void action_mqtt_publish(void) {
    Product product;

    if (!sys_get_active_product(&product)) {
        ESP_LOGW(TAG, "MQTT publish: no active product");
        ev_queue_post(EV_MQTT_PUBLISH_FAILURE);
        return;
    }

    ESP_LOGI(TAG, "Publicando por MQTT -> ID=%s", product.id);

    // aca falta llamar a la logica del mqtt para hacer el publish
    ev_queue_post(EV_MQTT_PUBLISH_SUCCESS);
}

// ==============================================================
bool sys_on_qr_detected(const char *id, const char *name) {
    sys_set_pending_scan(id, name, true);

    ESP_LOGI(TAG, "QR detected -> ID=%s | Name=%s", id, name);

    return ev_queue_post(EV_QR_RECEIVED);
}

bool sys_on_qr_invalid(const char *reason) {
    sys_set_pending_scan("", "", false);
    sys_set_last_error(reason != NULL ? reason : "Invalid QR");

    ESP_LOGW(TAG, "Invalid QR: %s", sys_get_last_error());

    return ev_queue_post(EV_QR_RECEIVED);
}
