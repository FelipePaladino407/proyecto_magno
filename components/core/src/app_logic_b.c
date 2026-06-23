#include "app_logic.h"
#include "esp_log.h"
#include "ev_queue.h"
#include "product_db.h"
#include <stdio.h>
#include <string.h>

/* ─────────────────────────────────────────────
 * Esta es la logica de la Placa B
 *
 * ───────────────────────────────────────────── */

static const char *TAG = "APP_B";

static void on_display_message(const char *title, const char *message) {
    ESP_LOGI("LCD_B", "%s | %s", title != NULL ? title : "", message != NULL ? message : "");

    // bueno loco aca hay iria la logica del display pa mostrar el mensaje
}

static void on_display_product(const Product *product) {
    if (product == NULL)
        return;

    ESP_LOGI("LCD_B", "ID:%s | Nombre:%s | Stock:%lu", product->id, product->name, (unsigned long)product->stock);
}

static void on_link_scan_received(void) {
    Product pending;
    if (app_logic_get_active_product(&pending)) {
        ESP_LOGI(TAG, "Scan recibido de Board A -> ID=%s", pending.id);
    }
}

static void on_enter_db_lookup(void) {
    Product pending;

    if (!app_logic_get_active_product(&pending) || pending.id[0] == '\0') {
        app_logic_set_last_error("No hay producto activo (Board B)");
        ev_queue_post(EV_PRODUCT_NOT_FOUND);
        return;
    }

    Product stored;
    memset(&stored, 0, sizeof(stored));

    if (!product_db_find_by_id(pending.id, &stored)) {
        char err[64];
        snprintf(err, sizeof(err), "Producto no registrado: %s", pending.id);
        app_logic_set_last_error(err);

        ESP_LOGW(TAG, "%s", err);
        ev_queue_post(EV_PRODUCT_NOT_FOUND);
        return;
    }

    app_logic_set_active_product(&stored);

    ESP_LOGI(TAG, "Producto encontrado (Board B) -> ID=%s | Stock=%lu", stored.id, (unsigned long)stored.stock);

    ev_queue_post(EV_PRODUCT_FOUND);
}

static void on_enter_stock_updating(void) {
    Product current;

    if (!app_logic_get_active_product(&current)) {
        app_logic_set_last_error("No hay producto activo (Board B)");
        ev_queue_post(EV_SCAN_INVALID);
        return;
    }

    // revisar xq no me acuerdo
    uint32_t qty = app_logic_get_selected_quantity();
    Product updated;
    memset(&updated, 0, sizeof(updated));

    if (!product_db_add_stock(current.id, qty, &updated)) {
        char err[64];
        snprintf(err, sizeof(err), "No se pudo actualizar stock (B): %s", current.id);
        app_logic_set_last_error(err);

        ESP_LOGE(TAG, "%s", err);
        ev_queue_post(EV_SCAN_INVALID);
        return;
    }

    app_logic_set_active_product(&updated);

    ESP_LOGI(TAG, "Stock actualizado (Board B) -> ID=%s | Agregado=%lu | Stock=%lu", updated.id, (unsigned long)qty,
             (unsigned long)updated.stock);

    ev_queue_post(EV_STOCK_UPDATED);
}

static void on_enter_mqtt_publishing(void) {
    Product product;

    if (!app_logic_get_active_product(&product)) {
        ev_queue_post(EV_MQTT_PUBLISH_FAILURE);
        return;
    }

    ESP_LOGI(TAG, "Publicando por MQTT (Board B) -> ID=%s", product.id);

    // falta call mqtt_handler_publish_product(&product) igual que en la otra placa
    ev_queue_post(EV_MQTT_PUBLISH_SUCCESS);
}

static void on_mqtt_publish_success(void) {
    ESP_LOGI(TAG, "MQTT OK (Board B)");
}

static void on_mqtt_publish_failure(void) {
    ESP_LOGW(TAG, "MQTT fallo (Board B) — guardando localmente");
    /* TODO: nvs_storage / local buffer */
}

static void on_enter_error_display(const char *error_msg) {
    on_display_message("Error (B)", error_msg);
}

/*
 * Registration de los callbacks placa B
 */
void app_logic_board_b_init(void) {
    static const AppLogicCallbacks k_board_b = {
        .on_display_message = on_display_message,
        .on_display_product = on_display_product,
        .on_enter_scan_processing = NULL, // la placa B no tiene CAM
        .on_enter_db_lookup = on_enter_db_lookup,
        .on_enter_stock_updating = on_enter_stock_updating,
        .on_enter_mqtt_publishing = on_enter_mqtt_publishing,
        .on_mqtt_publish_success = on_mqtt_publish_success,
        .on_mqtt_publish_failure = on_mqtt_publish_failure,
        .on_enter_error_display = on_enter_error_display,
        .on_link_transmit = NULL, //
        .on_link_scan_received = on_link_scan_received,
    };

    app_logic_register(&k_board_b);
}
