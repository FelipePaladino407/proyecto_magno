#ifndef FSM_H
#define FSM_H

#include <stdbool.h>
#include "shared_types.h"

typedef enum {
    EV_WIFI_CONNECT_SUCCESS,
    EV_QR_CAPTURED,
    EV_SCAN_SUCCESS,
    EV_SCAN_INVALID,
    EV_PRODUCT_FOUND,
    EV_PRODUCT_NOT_FOUND,
    EV_STOCK_UPDATED,
    EV_MQTT_CONNECT_FAILURE,
    EV_MQTT_CONNECT_SUCCESS,
    EV_MQTT_PRODUCT_RECEIVED,
    EV_MQTT_PUBLISH_SUCCESS,
    EV_MQTT_PUBLISH_FAILURE,
    EV_BTN_UP,
    EV_BTN_DOWN,
    EV_BTN_SELECT,
    EV_BTN_EXIT,
    EV_TIMEOUT
} EventType;

typedef enum {
    STATE_IDLE,
    STATE_SCAN_PROCESSING,
    STATE_LOCAL_DB_LOOKUP,
    STATE_PROMPT_ADD_NEW,
    STATE_DISPLAY_PRODUCT_INFO,
    STATE_MANUAL_SELECTION,
    STATE_MQTT_INIT,
    STATE_MQTT_PUBLISHING,
    STATE_ERROR_DISPLAY
} State;

typedef struct {
    void (*display_product)(const Product *product);
    void (*display_message)(const char *title, const char *message);

    bool (*publish_qr)(const Product *product);
    bool (*publish_manual)(const Product *product);
    bool (*publish_error)(const char *message);
} FsmCallbacks;

void fsm_init(void);
State fsm_get_current_state(void);
void fsm_execute_transition(EventType event);

void fsm_register_callbacks(const FsmCallbacks *callbacks);
void fsm_set_auto_events_enabled(bool enabled);
bool fsm_post_event(EventType event);

bool fsm_on_qr_detected(const char *id, const char *name);
bool fsm_on_qr_invalid(const char *reason);
bool fsm_on_mqtt_product_received(const Product *product);

#endif // FSM_H
