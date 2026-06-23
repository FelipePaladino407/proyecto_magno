#ifndef FSM_H
#define FSM_H

#include <stdbool.h>
#include "ev_queue.h"

/* Alias semánticos: el touch tiene SELECT/EXIT, la FSM los interpreta como confirmar/cancelar. */
#define EV_BTN_CONFIRM EV_BTN_SELECT
#define EV_BTN_CANCEL  EV_BTN_EXIT

typedef enum {
    STATE_IDLE = 0,

    // placa A
    STATE_SCAN_PROCESSING,
    STATE_LOCAL_DB_LOOKUP,
    STATE_PROMPT_ADD_PRODUCT,
    STATE_QUANTITY_SELECTION,
    STATE_STOCK_UPDATING,
    STATE_DISPLAY_PRODUCT_INFO,

    // placa B
    STATE_WAITING_FOR_SCAN,
    STATE_REMOTE_DB_LOOKUP,
    STATE_REMOTE_STOCK_UPDATING,

    // comun
    STATE_MQTT_PUBLISHING,
    STATE_ERROR_DISPLAY,

    STATE_COUNT
} State;

void fsm_init(void);
void fsm_execute_transition(EventType event);
State fsm_get_state(void);
void fsm_task(void *pvParameters);

#endif // FSM_H
