#include "include/fsm.h"

#include "product_db.h"
#include "shared_types.h"

#include <esp_log.h>
#include <inttypes.h>
#include <stdbool.h>
#include <string.h>

static State current_state;
static const char *TAG = "FSM";

// no está en el header porque los otros muchachos no necesitan verlo
typedef struct {
    State state;
    EventType event;
    State next_state;
    void (*action)(void);
} Transition;

static void handle_qr_scan(void)
{
    ESP_LOGI(TAG, "Executing fake QR scan...");

    /*
     * Simulación de QR.
     * Esto representa lo que más adelante vendría desde qr_handler.
     */
    const char *fake_qr_id = "PROD001";
    const char *fake_qr_name = "Bujias";

    Product stored_product;
    memset(&stored_product, 0, sizeof(Product));

    bool registered = product_db_register_scan(fake_qr_id, fake_qr_name, &stored_product);

    if (!registered) {
        ESP_LOGE(TAG, "No se pudo registrar el producto con ID: %s", fake_qr_id);
        current_state = STATE_IDLE;
        return;
    }

    ESP_LOGI(TAG,
             "Producto registrado/actualizado -> ID: %s | Nombre: %s | Stock: %" PRIu32,
             stored_product.id,
             stored_product.name,
             stored_product.stock);

    Product found_product;
    memset(&found_product, 0, sizeof(Product));

    bool found = product_db_find_by_id(fake_qr_id, &found_product);

    if (found) {
        ESP_LOGI(TAG,
                 "Producto encontrado en product_db/hash_table -> ID: %s | Nombre: %s | Stock: %" PRIu32,
                 found_product.id,
                 found_product.name,
                 found_product.stock);
    } else {
        ESP_LOGE(TAG, "ERROR: El producto no apareció en la tabla hash");
    }

    ESP_LOGI(TAG, "Cantidad de productos en product_db: %" PRIu32, product_db_get_count());

    /*
     * Para esta prueba mínima, volvemos manualmente a IDLE.
     * Más adelante esto debería ser otra transición de la FSM.
     */
    current_state = STATE_IDLE;
}

static void example(void)
{
    ESP_LOGI(TAG, "Función ejemplo porque las demás no están implementadas");
    ESP_LOGI(TAG, "CURRENT STATE: %d", current_state);
}

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

    {STATE_DISPLAY_PRODUCT_INFO, EV_STOCK_UPDATED, STATE_MQTT_PUBLISHING, example},
    {STATE_MQTT_PUBLISHING, EV_MQTT_PUBLISH_SUCCESS, STATE_IDLE, example},
    {STATE_MQTT_PUBLISHING, EV_MQTT_PUBLISH_FAILURE, STATE_IDLE, example},
};

void fsm_init(void)
{
    current_state = STATE_IDLE;
}

State fsm_get_current_state(void)
{
    return current_state;
}

// O(n), donde n es la cantidad de entradas de la tabla de estados
void fsm_execute_transition(EventType event)
{
    int table_size = sizeof(transition_table) / sizeof(transition_table[0]);

    for (int i = 0; i < table_size; i++) {
        if (transition_table[i].state == current_state &&
            transition_table[i].event == event) {

            ESP_LOGI(TAG,
                     "Transition: state %d + event %d -> state %d",
                     current_state,
                     event,
                     transition_table[i].next_state);

            current_state = transition_table[i].next_state;
            transition_table[i].action();

            break;
        }
    }
}
