#include "include/fsm.h"
#include <esp_log.h>

static State current_state;
static char *TAG = "FSM";

// no está en el header porque los otros muchachos no necesitan verlo
typedef struct {
  State state;
  EventType event;
  State next_state;
  void (*action)(void);
} Transition;

static void handle_qr_scan() { ESP_LOGI(TAG, "Executing handle_qr_scan"); }
static void example() {ESP_LOGI(TAG, "Función ejemplo porque las demás no están implementadas");}

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
    {STATE_MQTT_PUBLISHING, EV_MQTT_PUBLISH_FAILURE, STATE_IDLE, example}, // lo mismo pero acá la función guarda en un buffer

};

// -----------------------------------------------------
// -----------------------------------------------------

void fsm_init() { current_state = STATE_IDLE; }
State fsm_get_current_state() { return current_state; }

// O(n), donde n es la cantidad de entradas de la tabla de estados
void fsm_execute_transition(EventType event) {
  int table_size = sizeof(transition_table) / sizeof(transition_table[0]);

  for (int i = 0; i < table_size; i++) {
    if (transition_table[i].state == current_state &&
        transition_table[i].event == event) {
      current_state = transition_table[i].next_state;
      transition_table[i].action();
    }
  }
}
