#include "fsm.h"
#include "app_logic.h"
#include "esp_log.h"
#include "ev_queue.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "FSM";

typedef struct {
    State state;
    EventType event;
    State next_state;
    void (*action)(void);
} Transition;

static State current_state;

State fsm_get_state(void) {
    return current_state;
}

// para mas prolijidad, creo que podria usar inline
static void post(EventType ev) {
    ev_queue_post(ev);
}

// hace get de los registered callbacks, creo que podria usar inline tmb
static const AppLogicCallbacks *cb(void) {
    return app_logic_get();
}

/* ─────────────────────────────────────────────
 * Action implementations
 * Each action may ONLY:
 *   1. Read/write FSM-owned state variables.
 *   2. Call cb()->... callbacks.
 *   3. Call post() to chain events.
 * ───────────────────────────────────────────── */

static void action_reset_to_idle(void) {
    app_logic_reset_context();
}

static void action_wifi_connected(void) {
    ESP_LOGI(TAG, "Wi-Fi connected");
}

static void action_mqtt_connected(void) {
    ESP_LOGI(TAG, "MQTT connected");
}

static void action_mqtt_disconnected(void) {
    ESP_LOGW(TAG, "MQTT disconnected");
}

static void action_enter_scan_processing(void) {
    if (cb()->on_enter_scan_processing != NULL) {
        cb()->on_enter_scan_processing();
    } else {
        // fallback
        Product p;
        if (app_logic_get_active_product(&p)) {
            post(EV_SCAN_SUCCESS);
        } else {
            app_logic_set_last_error("QR invalido o vacio");
            post(EV_SCAN_INVALID);
        }
    }
}

static void action_enter_error_display(void) {
    const char *err = app_logic_get_last_error();
    const char *msg = (err != NULL && err[0] != '\0') ? err : "Error desconocido";
    ESP_LOGW(TAG, "Error display: %s", msg);

    if (cb()->on_enter_error_display != NULL) {
        cb()->on_enter_error_display(msg);
    }

    // auto-advance after showing the error?
    post(EV_TIMEOUT);
}

static void action_enter_db_lookup(void) {
    /* Board-specific logic must post EV_PRODUCT_FOUND or
     * EV_PRODUCT_NOT_FOUND (and call fsm_set_active_product
     * before posting EV_PRODUCT_FOUND). */
    if (cb()->on_enter_db_lookup != NULL) {
        cb()->on_enter_db_lookup();
    } else {
        app_logic_set_last_error("Sin callback de DB");
        post(EV_PRODUCT_NOT_FOUND);
    }
}

static void action_product_found(void) {
    app_logic_set_selected_quantity(1);
    Product p;
    if (app_logic_get_active_product(&p)) {
        ESP_LOGI(TAG, "Producto encontrado -> ID=%s | Nombre=%s | Stock=%lu", p.id, p.name, (unsigned long)p.stock);
        char msg[96];
        snprintf(msg, sizeof(msg), "Desea agregar %s?", p.name);
        if (cb()->on_display_message != NULL) {
            cb()->on_display_message("Producto reconocido", msg);
        }
    }
}

static void action_product_not_found(void) {
    const char *last_err = app_logic_get_last_error();
    const char *err = (last_err != NULL && last_err[0] != '\0') ? last_err : "Producto no registrado";
    ESP_LOGW(TAG, "%s", err);

    if (cb()->on_display_message != NULL) {
        cb()->on_display_message("No registrado", "Producto fuera del catalogo");
    }

    post(EV_TIMEOUT);
}

static void action_enter_quantity_selection(void) {
    app_logic_set_selected_quantity(1);

    char msg[32];
    snprintf(msg, sizeof(msg), "Cantidad: 1");

    if (cb()->on_display_message != NULL) {
        cb()->on_display_message("Seleccione cantidad", msg);
    }
}

static void action_quantity_up(void) {
    uint32_t qty = app_logic_get_selected_quantity();
    if (qty < 99) {
        qty++;
        app_logic_set_selected_quantity(qty);
    }
    char msg[32];
    snprintf(msg, sizeof(msg), "Cantidad: %lu", (unsigned long)qty);

    if (cb()->on_display_message != NULL) {
        cb()->on_display_message("Seleccione cantidad", msg);
    }
}

static void action_quantity_down(void) {
    uint32_t qty = app_logic_get_selected_quantity();
    if (qty > 1) {
        qty--;
        app_logic_set_selected_quantity(qty);
    }
    char msg[32];
    snprintf(msg, sizeof(msg), "Cantidad: %lu", (unsigned long)qty);

    if (cb()->on_display_message != NULL) {
        cb()->on_display_message("Seleccione cantidad", msg);
    }
}

static void action_cancel(void) {
    if (cb()->on_display_message != NULL) {
        cb()->on_display_message("Cancelado", "No se modifico el stock");
    }
    action_reset_to_idle();
}

static void action_enter_stock_updating(void) {
    if (cb()->on_enter_stock_updating != NULL) {
        cb()->on_enter_stock_updating();
    } else {
        app_logic_set_last_error("Sin callback de stock");
        post(EV_SCAN_INVALID);
    }
}

static void action_stock_updated(void) {
    Product p;
    if (app_logic_get_active_product(&p)) {
        uint32_t qty = app_logic_get_selected_quantity();
        ESP_LOGI(TAG, "Stock actualizado -> ID=%s | Agregado=%lu | Stock=%lu", p.id, (unsigned long)qty,
                 (unsigned long)p.stock);
        if (cb()->on_display_product != NULL) {
            cb()->on_display_product(&p);
        }
    }

    // auto-advance para publicar despues de mostrar resultado
    post(EV_TIMEOUT);
}

static void action_enter_mqtt_publishing(void) {
    ESP_LOGI(TAG, "Iniciando publicacion MQTT");

    if (cb()->on_enter_mqtt_publishing != NULL) {
        cb()->on_enter_mqtt_publishing();
    } else {
        ESP_LOGW(TAG, "Sin callback MQTT, simulando exito");
        post(EV_MQTT_PUBLISH_SUCCESS);
    }
}

static void action_mqtt_publish_success(void) {
    ESP_LOGI(TAG, "Publicacion MQTT exitosa");

    if (cb()->on_mqtt_publish_success != NULL) {
        cb()->on_mqtt_publish_success();
    }

    action_reset_to_idle();
}

static void action_mqtt_publish_failure(void) {
    ESP_LOGW(TAG, "Fallo en publicacion MQTT");

    if (cb()->on_mqtt_publish_failure != NULL) {
        cb()->on_mqtt_publish_failure();
    }

    action_reset_to_idle();
}

static const Transition transition_table[] = {
    // ENTRIES DE LA PLACA [A]
    {STATE_IDLE, EV_WIFI_CONNECT_SUCCESS, STATE_IDLE, action_wifi_connected},
    {STATE_IDLE, EV_MQTT_CONNECT_SUCCESS, STATE_IDLE, action_mqtt_connected},
    {STATE_IDLE, EV_MQTT_CONNECT_FAILURE, STATE_IDLE, action_mqtt_disconnected},

    {STATE_IDLE, EV_QR_CAPTURED, STATE_SCAN_PROCESSING, action_enter_scan_processing},

    {STATE_SCAN_PROCESSING, EV_SCAN_SUCCESS, STATE_LOCAL_DB_LOOKUP, action_enter_db_lookup},
    {STATE_SCAN_PROCESSING, EV_SCAN_INVALID, STATE_ERROR_DISPLAY, action_enter_error_display},

    {STATE_LOCAL_DB_LOOKUP, EV_PRODUCT_FOUND, STATE_PROMPT_ADD_PRODUCT, action_product_found},
    {STATE_LOCAL_DB_LOOKUP, EV_PRODUCT_NOT_FOUND, STATE_ERROR_DISPLAY, action_product_not_found},

    {STATE_PROMPT_ADD_PRODUCT, EV_BTN_CONFIRM, STATE_QUANTITY_SELECTION, action_enter_quantity_selection},
    {STATE_PROMPT_ADD_PRODUCT, EV_BTN_CANCEL, STATE_IDLE, action_cancel},
    {STATE_PROMPT_ADD_PRODUCT, EV_TIMEOUT, STATE_IDLE, action_reset_to_idle},

    {STATE_QUANTITY_SELECTION, EV_BTN_UP, STATE_QUANTITY_SELECTION, action_quantity_up},
    {STATE_QUANTITY_SELECTION, EV_BTN_DOWN, STATE_QUANTITY_SELECTION, action_quantity_down},
    {STATE_QUANTITY_SELECTION, EV_BTN_CONFIRM, STATE_STOCK_UPDATING, action_enter_stock_updating},
    {STATE_QUANTITY_SELECTION, EV_BTN_CANCEL, STATE_IDLE, action_cancel},

    {STATE_STOCK_UPDATING, EV_STOCK_UPDATED, STATE_DISPLAY_PRODUCT_INFO, action_stock_updated},
    {STATE_STOCK_UPDATING, EV_SCAN_INVALID, STATE_ERROR_DISPLAY, action_enter_error_display},

    {STATE_DISPLAY_PRODUCT_INFO, EV_TIMEOUT, STATE_MQTT_PUBLISHING, action_enter_mqtt_publishing},

    // ENTRIES DE LA PLACA [B]
    // {STATE_WAITING_FOR_SCAN, EV_LINK_SCAN_RECEIVED, STATE_REMOTE_DB_LOOKUP, action_enter_db_lookup},

    {STATE_REMOTE_DB_LOOKUP, EV_PRODUCT_FOUND, STATE_REMOTE_STOCK_UPDATING, action_product_found},
    {STATE_REMOTE_DB_LOOKUP, EV_PRODUCT_NOT_FOUND, STATE_ERROR_DISPLAY, action_product_not_found},

    {STATE_REMOTE_STOCK_UPDATING, EV_STOCK_UPDATED, STATE_MQTT_PUBLISHING, action_enter_mqtt_publishing},
    {STATE_REMOTE_STOCK_UPDATING, EV_SCAN_INVALID, STATE_ERROR_DISPLAY, action_enter_error_display},

    // ENTRIES COMUNES A AMBAS PLACAS
    {STATE_MQTT_PUBLISHING, EV_MQTT_PUBLISH_SUCCESS, STATE_IDLE, action_mqtt_publish_success},
    {STATE_MQTT_PUBLISHING, EV_MQTT_PUBLISH_FAILURE, STATE_IDLE, action_mqtt_publish_failure},

    {STATE_ERROR_DISPLAY, EV_TIMEOUT, STATE_IDLE, action_reset_to_idle},
    {STATE_ERROR_DISPLAY, EV_BTN_CONFIRM, STATE_IDLE, action_reset_to_idle},
    {STATE_ERROR_DISPLAY, EV_BTN_CANCEL, STATE_IDLE, action_reset_to_idle},
};

void fsm_init(void) {
    current_state = STATE_IDLE;
    app_logic_reset_context();
}

void fsm_task(void *pvParameters) {
    (void)pvParameters;

    ESP_LOGI(TAG, "FSM task started");

    EventType event;

    while (1) {
        // La idea es bloquear indefinidamente hasta que llegue una task.
        if (ev_queue_receive(&event, 0)) {
            fsm_execute_transition(event);
        }
    }
}

void fsm_execute_transition(EventType event) {
    const int table_lenght = sizeof(transition_table) / sizeof(transition_table[0]);

    if (current_state == STATE_IDLE && (event == EV_MQTT_PUBLISH_SUCCESS || event == EV_MQTT_PUBLISH_FAILURE)) {
        ESP_LOGD(TAG, "Ignorando ACK MQTT tardio en IDLE");
        return;
    } // puede ser

    for (int i = 0; i < table_lenght; i++) {
        if (transition_table[i].state == current_state && transition_table[i].event == event) {

            State previous_state = current_state;
            current_state = transition_table[i].next_state;

            ESP_LOGI(TAG, "Transition: state %d + event %d -> state %d", previous_state, event, current_state);

            if (transition_table[i].action != NULL) {
                transition_table[i].action();
            }

            return;
        }
    }

    ESP_LOGW(TAG, "No transition found for state %d + event %d", current_state, event);
}
