#include "app_logic.h"
#include "esp_log.h"
#include "ev_queue.h"
#include "product_db.h"
#include <stdio.h>
#include <string.h>

/* ─────────────────────────────────────────────
 * Esta es la logica de la Placa A
 *
 *  Todo callback debe eventualmente postear un evento
 *  usando ev_queue_post() para que la FSM pueda avanzar
 * ───────────────────────────────────────────── */

static const char *TAG = "APP_A";

static void on_display_message(const char *title, const char *message) {
    ESP_LOGI("LCD", "%s | %s", title != NULL ? title : "", message != NULL ? message : "");

    // aca tambien falta la logica del display
}

static void on_display_product(const Product *product) {
    if (product == NULL) {
        on_display_message("Producto", "Nulo");
        return;
    }

    ESP_LOGI("LCD", "ID:%s | Nombre:%s | Stock:%lu", product->id, product->name, (unsigned long)product->stock);

    // aca tambien falta la logica del display
}

static void on_enter_scan_processing(void) {
    Product pending;
    bool has_product = app_logic_get_active_product(&pending);

    if (!has_product || pending.id[0] == '\0') {
        app_logic_set_last_error("QR invalido o vacio");
        ev_queue_post(EV_SCAN_INVALID);
        return;
    }

    ev_queue_post(EV_SCAN_SUCCESS);
}

static void on_enter_db_lookup(void) {
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

        ESP_LOGW(TAG, "Producto no registrado -> ID=%s", pending.id);
        ev_queue_post(EV_PRODUCT_NOT_FOUND);
        return;
    }

    /* Update FSM with the authoritative DB copy. */
    app_logic_set_active_product(&stored);

    ESP_LOGI(TAG, "Producto encontrado -> ID=%s | Nombre=%s | Stock=%lu", stored.id, stored.name,
             (unsigned long)stored.stock);

    ev_queue_post(EV_PRODUCT_FOUND);
}

static void on_enter_stock_updating(void) {
    Product current;

    if (!app_logic_get_active_product(&current)) {
        app_logic_set_last_error("No hay producto activo para actualizar");
        ev_queue_post(EV_SCAN_INVALID);
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
        ev_queue_post(EV_SCAN_INVALID);
        return;
    }

    app_logic_set_active_product(&updated);

    ESP_LOGI(TAG, "Stock actualizado -> ID=%s | Agregado=%lu | Stock=%lu", updated.id, (unsigned long)qty,
             (unsigned long)updated.stock);

    ev_queue_post(EV_STOCK_UPDATED);
}

static void on_enter_mqtt_publishing(void) {
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

static void on_mqtt_publish_success(void) {
    ESP_LOGI(TAG, "Publicacion MQTT confirmada");
    // aca tengo que ver que hago
}

static void on_mqtt_publish_failure(void) {
    ESP_LOGW(TAG, "Publicacion MQTT fallida — pendiente en buffer local");
    // aca tengo que ver que hago
}

static void on_enter_error_display(const char *error_msg) {
    on_display_message("Error", error_msg);
    // aca puede que haya que mandar el error al logger tmb
}

/*
 * Registration de los callbacks placa A
 */
void app_logic_board_a_init(void) {
    static const AppLogicCallbacks k_board_a = {
        .on_display_message = on_display_message,
        .on_display_product = on_display_product,
        .on_enter_scan_processing = on_enter_scan_processing,
        .on_enter_db_lookup = on_enter_db_lookup,
        .on_enter_stock_updating = on_enter_stock_updating,
        .on_enter_mqtt_publishing = on_enter_mqtt_publishing,
        .on_mqtt_publish_success = on_mqtt_publish_success,
        .on_mqtt_publish_failure = on_mqtt_publish_failure,
        .on_enter_error_display = on_enter_error_display,

        //
        .on_link_transmit = NULL,
        .on_link_scan_received = NULL,
    };

    app_logic_register(&k_board_a);
}
