#ifndef FSM_H
#define FSM_H

#include <stdbool.h>
#include "ev_queue.h"

typedef enum {
    STATE_SETUP,
    STATE_IDLE,

    STATE_SCAN_PROCESSING,
    STATE_DB_LOOKUP,
    STATE_PROMPT_ADD_PRODUCT,
    STATE_QUANTITY_SELECTION,
    STATE_STOCK_UPDATING,
    STATE_PRODUCT_OVERVIEW,

    STATE_MQTT_PUBLISHING,
    STATE_ERROR,
} State;

// todos los estados tienen un timer que puede cortar todo con un EV_TIMEOUT?

void fsm_init(void);
void fsm_execute_transition(EventType event);
State fsm_get_state(void);
void fsm_task(void *pvParameters);

#endif // FSM_H
