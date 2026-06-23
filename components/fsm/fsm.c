#include "fsm.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "esp_log.h"
#include "product_db.h"

#include <stdio.h>
#include <string.h>

extern QueueHandle_t fsm_event_queue;

static const char *TAG = "FSM";

static State current_state;
static Product active_product;
static bool active_product_valid;
static uint32_t selected_quantity;
static char last_error[64];
static FsmCallbacks s_callbacks;
static bool s_auto_events_enabled = true;

typedef struct {
    State state;
    EventType event;
    State next_state;
    void (*action)(void);
} Transition;

static void safe_copy(char *dest, const char *src, size_t dest_size)
{
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

static void display_message(const char *title, const char *message)
{
    ESP_LOGI("LCD_SIM", "%s | %s", title != NULL ? title : "", message != NULL ? message : "");

    if (s_callbacks.display_message != NULL) {
        s_callbacks.display_message(title, message);
    }
}

static void display_product(const Product *product)
{
    if (product == NULL) {
        display_message("Producto", "Producto nulo");
        return;
    }

    ESP_LOGI("LCD_SIM", "Mostrar en LCD -> ID:%s | Nombre:%s | Stock:%lu",
             product->id,
             product->name,
             (unsigned long)product->stock);

    if (s_callbacks.display_product != NULL) {
        s_callbacks.display_product(product);
    }
}

static void post_event_or_log(EventType event)
{
    if (!s_auto_events_enabled) {
        ESP_LOGI(TAG, "Evento automatico deshabilitado: %d", event);
        return;
    }

    if (!fsm_post_event(event)) {
        ESP_LOGW(TAG, "No se pudo postear evento automatico: %d", event);
    }
}

bool fsm_post_event(EventType event)
{
    if (fsm_event_queue == NULL) {
        ESP_LOGW(TAG, "fsm_event_queue no inicializada. Evento perdido: %d", event);
        return false;
    }

    return xQueueSend(fsm_event_queue, &event, pdMS_TO_TICKS(10)) == pdPASS;
}

static void return_to_idle(void)
{
    ESP_LOGI(TAG, "Volviendo a IDLE");
    active_product_valid = false;
    selected_quantity = 1;
}

static void handle_wifi_connected(void)
{
    ESP_LOGI(TAG, "Wi-Fi conectado, listo para operar");
}

static void handle_mqtt_connected(void)
{
    ESP_LOGI(TAG, "MQTT conectado");

    if (s_callbacks.flush_pending != NULL) {
        if (!s_callbacks.flush_pending()) {
            ESP_LOGW(TAG, "No se pudieron reenviar todos los eventos pendientes");
        }
    }
}

static void handle_mqtt_disconnected(void)
{
    ESP_LOGW(TAG, "MQTT desconectado");
}

static void handle_qr_scan(void)
{
    ESP_LOGI(TAG, "Procesando QR recibido");

    if (!active_product_valid || active_product.id[0] == '\0') {
        safe_copy(last_error, "QR invalido o vacio", sizeof(last_error));
        post_event_or_log(EV_SCAN_INVALID);
        return;
    }

    post_event_or_log(EV_SCAN_SUCCESS);
}

static void handle_scan_invalid(void)
{
    const char *message = last_error[0] != '\0' ? last_error : "QR invalido";

    ESP_LOGW(TAG, "%s", message);
    display_message("QR invalido", "No se agrega nada");

    if (s_callbacks.publish_error != NULL) {
        (void)s_callbacks.publish_error(message);
    }

    post_event_or_log(EV_TIMEOUT);
}

static void lookup_product(void)
{
    Product stored_product;
    memset(&stored_product, 0, sizeof(stored_product));

    if (!active_product_valid || active_product.id[0] == '\0') {
        safe_copy(last_error, "No hay producto activo", sizeof(last_error));
        post_event_or_log(EV_SCAN_INVALID);
        return;
    }

    if (!product_db_find_by_id(active_product.id, &stored_product)) {
        snprintf(last_error, sizeof(last_error), "Producto no registrado: %s", active_product.id);
        ESP_LOGW(TAG, "Producto no registrado -> ID=%s | Nombre=%s",
                 active_product.id,
                 active_product.name);
        post_event_or_log(EV_PRODUCT_NOT_FOUND);
        return;
    }

    /* El producto existe en el catalogo local. Se usa el nombre/stock guardado como fuente de verdad. */
    active_product = stored_product;
    active_product_valid = true;
    selected_quantity = 1;

    ESP_LOGI(TAG, "Producto reconocido -> ID=%s | Nombre=%s | Stock actual=%lu",
             active_product.id,
             active_product.name,
             (unsigned long)active_product.stock);

    post_event_or_log(EV_PRODUCT_FOUND);
}

static void prompt_add_product(void)
{
    char message[96];
    snprintf(message, sizeof(message), "Desea agregar %s?", active_product.name);
    display_message("Producto reconocido", message);
}

static void handle_product_not_found(void)
{
    ESP_LOGW(TAG, "%s", last_error[0] != '\0' ? last_error : "Producto no registrado");
    display_message("No registrado", "Producto fuera del catalogo");

    /* Nueva regla: si no esta en catalogo, no se abre ingreso manual viejo. */
    post_event_or_log(EV_TIMEOUT);
}

static void cancel_add_product(void)
{
    display_message("Cancelado", "No se modifico el stock");
    return_to_idle();
}

static void prompt_quantity_selection(void)
{
    selected_quantity = 1;

    char message[96];
    snprintf(message, sizeof(message), "Cantidad: %lu", (unsigned long)selected_quantity);
    display_message("Seleccione cantidad", message);
}

static void update_quantity_display(void)
{
    char message[96];
    snprintf(message, sizeof(message), "Cantidad: %lu", (unsigned long)selected_quantity);
    display_message("Seleccione cantidad", message);
}

static void increment_quantity(void)
{
    if (selected_quantity < 99) {
        selected_quantity++;
    }

    update_quantity_display();
}

static void decrement_quantity(void)
{
    if (selected_quantity > 1) {
        selected_quantity--;
    }

    update_quantity_display();
}

static void update_stock_with_selected_quantity(void)
{
    Product updated_product;
    memset(&updated_product, 0, sizeof(updated_product));

    if (!active_product_valid) {
        safe_copy(last_error, "No hay producto activo para actualizar", sizeof(last_error));
        ESP_LOGE(TAG, "%s", last_error);
        post_event_or_log(EV_SCAN_INVALID);
        return;
    }

    if (!product_db_add_stock(active_product.id, selected_quantity, &updated_product)) {
        snprintf(last_error, sizeof(last_error), "No se pudo actualizar stock: %s", active_product.id);
        ESP_LOGE(TAG, "%s", last_error);
        post_event_or_log(EV_SCAN_INVALID);
        return;
    }

    active_product = updated_product;
    active_product_valid = true;

    ESP_LOGI(TAG, "Stock actualizado -> ID=%s | Nombre=%s | Agregado=%lu | Stock=%lu",
             active_product.id,
             active_product.name,
             (unsigned long)selected_quantity,
             (unsigned long)active_product.stock);

    post_event_or_log(EV_STOCK_UPDATED);
}

static void display_updated_product(void)
{
    display_product(&active_product);

    /* En la demo se continua solo hacia publicacion luego de mostrar. */
    post_event_or_log(EV_TIMEOUT);
}

static void execute_mqtt_publish(void)
{
    bool accepted = false;

    ESP_LOGI(TAG, "Solicitando publicacion MQTT del producto confirmado");

    if (s_callbacks.publish_qr != NULL) {
        accepted = s_callbacks.publish_qr(&active_product);
    } else {
        ESP_LOGW(TAG, "No hay callback MQTT registrado. Se simula aceptado.");
        accepted = true;
    }

    post_event_or_log(accepted ? EV_MQTT_PUBLISH_SUCCESS : EV_MQTT_PUBLISH_FAILURE);
}

static void handle_mqtt_publish_success(void)
{
    ESP_LOGI(TAG, "Publicacion MQTT aceptada por el handler");
    return_to_idle();
}

static void save_to_local_buffer(void)
{
    bool stored = false;

    if (active_product_valid && s_callbacks.store_pending_qr != NULL) {
        stored = s_callbacks.store_pending_qr(&active_product);
    }

    if (stored) {
        ESP_LOGW(TAG, "Fallo de publicacion. Producto guardado como pendiente en NVS");
        display_message("Sin conexion", "Producto guardado pendiente");
    } else {
        ESP_LOGE(TAG, "Fallo de publicacion y no se pudo guardar pendiente en NVS");
        display_message("Error", "No se pudo guardar pendiente");
    }

    return_to_idle();
}

static const Transition transition_table[] = {
    {STATE_IDLE, EV_QR_CAPTURED, STATE_SCAN_PROCESSING, handle_qr_scan},
    {STATE_IDLE, EV_WIFI_CONNECT_SUCCESS, STATE_IDLE, handle_wifi_connected},
    {STATE_IDLE, EV_MQTT_CONNECT_SUCCESS, STATE_IDLE, handle_mqtt_connected},
    {STATE_IDLE, EV_MQTT_CONNECT_FAILURE, STATE_IDLE, handle_mqtt_disconnected},

    {STATE_SCAN_PROCESSING, EV_SCAN_INVALID, STATE_ERROR_DISPLAY, handle_scan_invalid},
    {STATE_SCAN_PROCESSING, EV_SCAN_SUCCESS, STATE_LOCAL_DB_LOOKUP, lookup_product},

    {STATE_LOCAL_DB_LOOKUP, EV_PRODUCT_FOUND, STATE_PROMPT_ADD_PRODUCT, prompt_add_product},
    {STATE_LOCAL_DB_LOOKUP, EV_PRODUCT_NOT_FOUND, STATE_ERROR_DISPLAY, handle_product_not_found},

    {STATE_PROMPT_ADD_PRODUCT, EV_BTN_CONFIRM, STATE_QUANTITY_SELECTION, prompt_quantity_selection},
    {STATE_PROMPT_ADD_PRODUCT, EV_BTN_CANCEL, STATE_IDLE, cancel_add_product},
    {STATE_PROMPT_ADD_PRODUCT, EV_TIMEOUT, STATE_IDLE, return_to_idle},

    {STATE_QUANTITY_SELECTION, EV_BTN_UP, STATE_QUANTITY_SELECTION, increment_quantity},
    {STATE_QUANTITY_SELECTION, EV_BTN_DOWN, STATE_QUANTITY_SELECTION, decrement_quantity},
    {STATE_QUANTITY_SELECTION, EV_BTN_CONFIRM, STATE_STOCK_UPDATING, update_stock_with_selected_quantity},
    {STATE_QUANTITY_SELECTION, EV_BTN_CANCEL, STATE_IDLE, cancel_add_product},

    {STATE_STOCK_UPDATING, EV_STOCK_UPDATED, STATE_DISPLAY_PRODUCT_INFO, display_updated_product},
    {STATE_STOCK_UPDATING, EV_SCAN_INVALID, STATE_ERROR_DISPLAY, handle_scan_invalid},

    {STATE_DISPLAY_PRODUCT_INFO, EV_TIMEOUT, STATE_MQTT_PUBLISHING, execute_mqtt_publish},

    {STATE_MQTT_PUBLISHING, EV_MQTT_PUBLISH_SUCCESS, STATE_IDLE, handle_mqtt_publish_success},
    {STATE_MQTT_PUBLISHING, EV_MQTT_PUBLISH_FAILURE, STATE_IDLE, save_to_local_buffer},

    {STATE_ERROR_DISPLAY, EV_TIMEOUT, STATE_IDLE, return_to_idle},
    {STATE_ERROR_DISPLAY, EV_BTN_CONFIRM, STATE_IDLE, return_to_idle},
    {STATE_ERROR_DISPLAY, EV_BTN_CANCEL, STATE_IDLE, return_to_idle},
};

void fsm_init(void)
{
    current_state = STATE_IDLE;
    memset(&active_product, 0, sizeof(active_product));
    active_product_valid = false;
    selected_quantity = 1;
    last_error[0] = '\0';
}

State fsm_get_current_state(void)
{
    return current_state;
}

void fsm_register_callbacks(const FsmCallbacks *callbacks)
{
    memset(&s_callbacks, 0, sizeof(s_callbacks));

    if (callbacks != NULL) {
        s_callbacks = *callbacks;
    }
}

void fsm_set_auto_events_enabled(bool enabled)
{
    s_auto_events_enabled = enabled;
}

uint32_t fsm_get_selected_quantity(void)
{
    return selected_quantity;
}

void fsm_set_selected_quantity(uint32_t quantity)
{
    if (quantity == 0) {
        quantity = 1;
    }

    if (quantity > 99) {
        quantity = 99;
    }

    selected_quantity = quantity;
}

void fsm_set_active_product(Product prod)
{
    active_product = prod;
    active_product_valid = true;
}

bool fsm_on_qr_detected(const char *id, const char *name)
{
    memset(&active_product, 0, sizeof(active_product));
    safe_copy(active_product.id, id, sizeof(active_product.id));
    safe_copy(active_product.name, name, sizeof(active_product.name));
    active_product.stock = 0;
    active_product_valid = active_product.id[0] != '\0';

    ESP_LOGI(TAG, "Callback QR -> ID=%s | Nombre=%s", active_product.id, active_product.name);

    return fsm_post_event(EV_QR_CAPTURED);
}

bool fsm_on_qr_invalid(const char *reason)
{
    memset(&active_product, 0, sizeof(active_product));
    active_product_valid = false;
    safe_copy(last_error, reason != NULL ? reason : "QR invalido", sizeof(last_error));

    return fsm_post_event(EV_QR_CAPTURED);
}

void fsm_execute_transition(EventType event)
{
    const int table_size = sizeof(transition_table) / sizeof(transition_table[0]);

    if (current_state == STATE_IDLE &&
        (event == EV_MQTT_PUBLISH_SUCCESS || event == EV_MQTT_PUBLISH_FAILURE)) {
        ESP_LOGD(TAG, "Ignorando ACK MQTT tardio en IDLE");
        return;
    }

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

