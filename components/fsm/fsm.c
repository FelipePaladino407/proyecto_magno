#include "fsm.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "esp_log.h"
#include "product_db.h"

#include <stdio.h>
#include <string.h>

extern QueueHandle_t fsm_event_queue;

static State current_state;
static const char *TAG = "FSM";

static Product active_product;
static char active_error[64];
static FsmCallbacks s_callbacks;
static bool s_auto_events_enabled = true;

typedef struct {
    State state;
    EventType event;
    State next_state;
    void (*action)(void);
} Transition;

static void safe_copy(char *dest, const char *src, size_t dest_size) {
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

static void display_message(const char *title, const char *message) {
    if (s_callbacks.display_message != NULL) {
        s_callbacks.display_message(title, message);
        return;
    }

    ESP_LOGI(TAG, "[LCD_SIM] %s | %s", title ? title : "", message ? message : "");
}

static void display_product(const Product *product) {
    if (product == NULL) {
        return;
    }

    if (s_callbacks.display_product != NULL) {
        s_callbacks.display_product(product);
        return;
    }

    ESP_LOGI(TAG,
             "[LCD_SIM] Producto: ID=%s | Nombre=%s | Stock=%lu",
             product->id,
             product->name,
             (unsigned long)product->stock);
}

bool fsm_post_event(EventType event) {
    if (!s_auto_events_enabled) {
        return false;
    }

    if (fsm_event_queue == NULL) {
        ESP_LOGW(TAG, "No existe fsm_event_queue; no se pudo enviar evento %d", event);
        return false;
    }

    return xQueueSend(fsm_event_queue, &event, pdMS_TO_TICKS(100)) == pdPASS;
}

static void post_event_or_log(EventType event) {
    if (!s_auto_events_enabled) {
        return;
    }

    if (!fsm_post_event(event)) {
        ESP_LOGW(TAG, "No se pudo encolar evento automatico %d", event);
    }
}

static void clear_active_context(void) {
    memset(&active_product, 0, sizeof(active_product));
    active_error[0] = '\0';
}

// -----------------------------------------------------
// Acciones de la FSM
// -----------------------------------------------------

static void handle_qr_scan(void) {
    ESP_LOGI(TAG, "Procesando QR recibido");

    if (active_product.id[0] == '\0') {
        if (active_error[0] == '\0') {
            safe_copy(active_error, "QR vacio o invalido", sizeof(active_error));
        }
        post_event_or_log(EV_SCAN_INVALID);
        return;
    }

    post_event_or_log(EV_SCAN_SUCCESS);
}

static void handle_wifi(void) {
    ESP_LOGI(TAG, "Wi-Fi conectado, listo para operar");
}

static void handle_mqtt_connected(void) {
    ESP_LOGI(TAG, "MQTT conectado, listo para publicar/recibir");
}

static void handle_mqtt_disconnected(void) {
    ESP_LOGW(TAG, "MQTT desconectado");
}

static void handle_scan_error(void) {
    const char *message = active_error[0] != '\0' ? active_error : "No se pudo leer el QR";
    ESP_LOGW(TAG, "%s", message);
    display_message("QR invalido", message);

    if (s_callbacks.publish_error != NULL) {
        (void)s_callbacks.publish_error(message);
    }
}

static void lookup_product_and_update_stock(void) {
    Product catalog_product;

    if (product_db_find_by_id(active_product.id, &catalog_product)) {
        Product updated_product;

        const char *name_to_use = catalog_product.name[0] != '\0'
                                      ? catalog_product.name
                                      : active_product.name;

        if (product_db_register_scan(catalog_product.id, name_to_use, &updated_product)) {
            active_product = updated_product;
            ESP_LOGI(TAG,
                     "Producto encontrado y stock actualizado -> ID=%s | Nombre=%s | Stock=%lu",
                     active_product.id,
                     active_product.name,
                     (unsigned long)active_product.stock);
            post_event_or_log(EV_PRODUCT_FOUND);
            return;
        }

        safe_copy(active_error,
                  "Producto encontrado, pero no se pudo actualizar stock",
                  sizeof(active_error));
        post_event_or_log(EV_SCAN_INVALID);
        return;
    }

    ESP_LOGW(TAG,
             "Producto no registrado -> ID=%s | Nombre=%s",
             active_product.id,
             active_product.name);
    post_event_or_log(EV_PRODUCT_NOT_FOUND);
}

static void prompt_add_new_product(void) {
    char message[96];
    snprintf(message,
             sizeof(message),
             "Agregar %s (%s)?",
             active_product.name[0] != '\0' ? active_product.name : "producto",
             active_product.id);

    display_message("Producto no registrado", message);
}

static void confirm_add_new_product(void) {
    Product updated_product;

    if (product_db_register_scan(active_product.id, active_product.name, &updated_product)) {
        active_product = updated_product;
        ESP_LOGI(TAG,
                 "Producto agregado manualmente -> ID=%s | Nombre=%s | Stock=%lu",
                 active_product.id,
                 active_product.name,
                 (unsigned long)active_product.stock);
        display_product(&active_product);
        post_event_or_log(EV_STOCK_UPDATED);
        return;
    }

    safe_copy(active_error, "No se pudo agregar producto", sizeof(active_error));
    display_message("Error", active_error);
}

static void display_active_product(void) {
    display_product(&active_product);
    post_event_or_log(EV_STOCK_UPDATED);
}

static void display_received_product(void) {
    ESP_LOGI(TAG,
             "Producto recibido por MQTT -> ID=%s | Nombre=%s | Stock=%lu",
             active_product.id,
             active_product.name,
             (unsigned long)active_product.stock);
    display_product(&active_product);
}

static void execute_mqtt_publish(void) {
    bool accepted = false;

    ESP_LOGI(TAG, "Solicitando publicacion MQTT del producto activo");

    if (s_callbacks.publish_qr != NULL) {
        accepted = s_callbacks.publish_qr(&active_product);
    } else {
        ESP_LOGW(TAG, "No hay callback publish_qr registrado");
    }

    post_event_or_log(accepted ? EV_MQTT_PUBLISH_SUCCESS : EV_MQTT_PUBLISH_FAILURE);
}

static void handle_publish_success(void) {
    ESP_LOGI(TAG, "Publicacion MQTT aceptada por el handler");
    clear_active_context();
}

static void save_to_local_buffer(void) {
    ESP_LOGW(TAG, "Fallo de publicacion. El producto queda para persistencia/local buffer si se implementa NVS");
    clear_active_context();
}

static void return_to_idle(void) {
    clear_active_context();
    ESP_LOGI(TAG, "Volviendo a IDLE");
}

static void manual_selection_placeholder(void) {
    display_message("Modo manual", "Menu LCD/touch pendiente de integrar");
}

// -----------------------------------------------------
// Tabla de transiciones
// -----------------------------------------------------

static const Transition transition_table[] = {
    {STATE_IDLE, EV_QR_CAPTURED, STATE_SCAN_PROCESSING, handle_qr_scan},
    {STATE_IDLE, EV_BTN_SELECT, STATE_MANUAL_SELECTION, manual_selection_placeholder},
    {STATE_IDLE, EV_WIFI_CONNECT_SUCCESS, STATE_IDLE, handle_wifi},
    {STATE_IDLE, EV_MQTT_CONNECT_SUCCESS, STATE_IDLE, handle_mqtt_connected},
    {STATE_IDLE, EV_MQTT_CONNECT_FAILURE, STATE_IDLE, handle_mqtt_disconnected},
    {STATE_IDLE, EV_MQTT_PRODUCT_RECEIVED, STATE_DISPLAY_PRODUCT_INFO, display_received_product},
    {STATE_IDLE, EV_MQTT_PUBLISH_SUCCESS, STATE_IDLE, NULL},
    {STATE_IDLE, EV_MQTT_PUBLISH_FAILURE, STATE_IDLE, NULL},

    {STATE_SCAN_PROCESSING, EV_SCAN_INVALID, STATE_ERROR_DISPLAY, handle_scan_error},
    {STATE_SCAN_PROCESSING, EV_SCAN_SUCCESS, STATE_LOCAL_DB_LOOKUP, lookup_product_and_update_stock},

    {STATE_ERROR_DISPLAY, EV_BTN_SELECT, STATE_IDLE, return_to_idle},
    {STATE_ERROR_DISPLAY, EV_TIMEOUT, STATE_IDLE, return_to_idle},

    {STATE_LOCAL_DB_LOOKUP, EV_PRODUCT_FOUND, STATE_DISPLAY_PRODUCT_INFO, display_active_product},
    {STATE_LOCAL_DB_LOOKUP, EV_PRODUCT_NOT_FOUND, STATE_PROMPT_ADD_NEW, prompt_add_new_product},

    {STATE_PROMPT_ADD_NEW, EV_BTN_EXIT, STATE_IDLE, return_to_idle},
    {STATE_PROMPT_ADD_NEW, EV_TIMEOUT, STATE_IDLE, return_to_idle},
    {STATE_PROMPT_ADD_NEW, EV_BTN_SELECT, STATE_DISPLAY_PRODUCT_INFO, confirm_add_new_product},

    {STATE_DISPLAY_PRODUCT_INFO, EV_STOCK_UPDATED, STATE_MQTT_PUBLISHING, execute_mqtt_publish},
    {STATE_DISPLAY_PRODUCT_INFO, EV_TIMEOUT, STATE_IDLE, return_to_idle},

    {STATE_MQTT_PUBLISHING, EV_MQTT_PUBLISH_SUCCESS, STATE_IDLE, handle_publish_success},
    {STATE_MQTT_PUBLISHING, EV_MQTT_PUBLISH_FAILURE, STATE_IDLE, save_to_local_buffer},
};

// -----------------------------------------------------
// API publica de la FSM
// -----------------------------------------------------

void fsm_init(void) {
    current_state = STATE_IDLE;
    clear_active_context();
}

State fsm_get_current_state(void) {
    return current_state;
}

void fsm_register_callbacks(const FsmCallbacks *callbacks) {
    if (callbacks == NULL) {
        memset(&s_callbacks, 0, sizeof(s_callbacks));
        return;
    }

    s_callbacks = *callbacks;
}

void fsm_set_auto_events_enabled(bool enabled) {
    s_auto_events_enabled = enabled;
}

bool fsm_on_qr_detected(const char *id, const char *name) {
    clear_active_context();

    if (id == NULL || id[0] == '\0') {
        safe_copy(active_error, "QR sin ID", sizeof(active_error));
        return fsm_post_event(EV_QR_CAPTURED);
    }

    safe_copy(active_product.id, id, sizeof(active_product.id));
    safe_copy(active_product.name, name, sizeof(active_product.name));
    active_product.stock = 0;

    ESP_LOGI(TAG,
             "Callback QR -> ID=%s | Nombre=%s",
             active_product.id,
             active_product.name);

    return fsm_post_event(EV_QR_CAPTURED);
}

bool fsm_on_qr_invalid(const char *reason) {
    clear_active_context();
    safe_copy(active_error,
              reason != NULL ? reason : "QR invalido",
              sizeof(active_error));

    ESP_LOGW(TAG, "Callback QR invalido -> %s", active_error);
    return fsm_post_event(EV_QR_CAPTURED);
}

bool fsm_on_mqtt_product_received(const Product *product) {
    clear_active_context();

    if (product == NULL || product->id[0] == '\0') {
        safe_copy(active_error, "Producto MQTT invalido", sizeof(active_error));
        return false;
    }

    active_product = *product;
    return fsm_post_event(EV_MQTT_PRODUCT_RECEIVED);
}

void fsm_execute_transition(EventType event) {
    int table_size = sizeof(transition_table) / sizeof(transition_table[0]);

    for (int i = 0; i < table_size; i++) {
        if (transition_table[i].state == current_state &&
            transition_table[i].event == event) {

            State previous_state = current_state;
            current_state = transition_table[i].next_state;

            ESP_LOGI(TAG,
                     "Transition: state %d + event %d -> state %d",
                     previous_state,
                     event,
                     current_state);

            if (transition_table[i].action != NULL) {
                transition_table[i].action();
            }

            return;
        }
    }

    ESP_LOGW(TAG,
             "No transition found for state %d + event %d",
             current_state,
             event);
}
