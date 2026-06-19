#include "fsm.h"
#include "mqtt_handler.h"
#include "shared_types.h"
#include <esp_log.h>

static State current_state;
static const char *TAG = "FSM";

// Variable estática local para guardar el producto que se está procesando
static Product active_product;

// No está en el header porque los otros módulos no necesitan verlo
typedef struct {
    State state;
    EventType event;
    State next_state;
    void (*action)(void);
} Transition;

// -----------------------------------------------------
// Acciones de la FSM
// -----------------------------------------------------

static void handle_qr_scan(void) {
    ESP_LOGI(TAG, "Executing handle_qr_scan");
}

// Esto se debería usar desde QR_HANDLER para cargar un producto cuando lo detecta
void fsm_set_active_product(Product prod) {
    active_product = prod;
}

static void example(void) {
    ESP_LOGI(TAG, "Example function, uuu");
    ESP_LOGI(TAG, "CURRENT STATE: %d", current_state);
}

static void execute_mqtt_publish(void) {
#ifdef FSM_TEST_MODE
    ESP_LOGI(TAG, "[FSM_TEST_MODE] Simulando publicación MQTT.");
    return;
#else
    ESP_LOGI(TAG, "FSM requesting MQTT handler to publish...");

    procesar_y_publicar_qr(active_product);

    // MQTT_EVENT_PUBLISHED en mqtt_handler.c manda SUCCESS
    // de forma asíncrona cuando el broker responde.
#endif
}

static void save_to_local_buffer(void) {
    ESP_LOGW(TAG, "Fallo de publicación. Guardando producto en NVS.");
    // Esta parte después la implementa Piero
}

// -----------------------------------------------------
// Tabla de transiciones
// -----------------------------------------------------

static const Transition transition_table[] = {
    {STATE_IDLE, EV_QR_CAPTURED, STATE_SCAN_PROCESSING, handle_qr_scan},
    {STATE_IDLE, EV_BTN_SELECT, STATE_MANUAL_SELECTION, example},

    {STATE_SCAN_PROCESSING, EV_SCAN_INVALID, STATE_ERROR_DISPLAY, example},
    {STATE_SCAN_PROCESSING, EV_SCAN_SUCCESS, STATE_LOCAL_DB_LOOKUP, example},

    {STATE_ERROR_DISPLAY, EV_BTN_SELECT, STATE_IDLE, example},
    {STATE_ERROR_DISPLAY, EV_TIMEOUT, STATE_IDLE, example},

    {STATE_LOCAL_DB_LOOKUP, EV_PRODUCT_FOUND, STATE_DISPLAY_PRODUCT_INFO, example},
    {STATE_LOCAL_DB_LOOKUP, EV_PRODUCT_NOT_FOUND, STATE_PROMPT_ADD_NEW, example},

    {STATE_PROMPT_ADD_NEW, EV_BTN_EXIT, STATE_IDLE, example},
    {STATE_PROMPT_ADD_NEW, EV_TIMEOUT, STATE_IDLE, example},
    {STATE_PROMPT_ADD_NEW, EV_BTN_SELECT, STATE_DISPLAY_PRODUCT_INFO, example},

    {STATE_DISPLAY_PRODUCT_INFO, EV_STOCK_UPDATED, STATE_MQTT_PUBLISHING, execute_mqtt_publish},

    {STATE_MQTT_PUBLISHING, EV_MQTT_PUBLISH_SUCCESS, STATE_IDLE, example},
    {STATE_MQTT_PUBLISHING, EV_MQTT_PUBLISH_FAILURE, STATE_IDLE, save_to_local_buffer},
};

// -----------------------------------------------------
// API pública de la FSM
// -----------------------------------------------------

void fsm_init(void) {
    current_state = STATE_IDLE;
}

State fsm_get_current_state(void) {
    return current_state;
}

// O(n), donde n es la cantidad de entradas de la tabla de estados
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
