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

static const Transition transition_table[] = {
    {IDLE, EV_QR_DETECT, QR_PROCESSING, handle_qr_scan},

};

// -----------------------------------------------------
// -----------------------------------------------------

void fsm_init() { current_state = IDLE; }
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
