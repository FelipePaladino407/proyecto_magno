#ifndef FSM_H
#define FSM_H

#include <stdbool.h>
#include <stdint.h>

#include "shared_types.h"

/*
 * La FSM se piensa como la lógica de la placa 2:
 * recibe QR ya decodificados, pregunta por LCD/touch, actualiza stock y publica.
 */
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
    EV_MQTT_PUBLISH_SUCCESS,
    EV_MQTT_PUBLISH_FAILURE,
    EV_BTN_UP,
    EV_BTN_DOWN,
    EV_BTN_SELECT,
    EV_BTN_EXIT,
    EV_TIMEOUT
} EventType;

/* Alias semánticos: el touch tiene SELECT/EXIT, la FSM los interpreta como confirmar/cancelar. */
#define EV_BTN_CONFIRM EV_BTN_SELECT
#define EV_BTN_CANCEL  EV_BTN_EXIT

typedef enum {
    STATE_IDLE,
    STATE_SCAN_PROCESSING,
    STATE_LOCAL_DB_LOOKUP,
    STATE_PROMPT_ADD_PRODUCT,
    STATE_QUANTITY_SELECTION,
    STATE_STOCK_UPDATING,
    STATE_DISPLAY_PRODUCT_INFO,
    STATE_MQTT_INIT,
    STATE_MQTT_PUBLISHING,
    STATE_ERROR_DISPLAY,
    STATE_MANUAL_SELECTION = STATE_QUANTITY_SELECTION,
    STATE_PROMPT_ADD_NEW = STATE_PROMPT_ADD_PRODUCT
} State;

typedef struct {
    void (*display_product)(const Product *product);
    void (*display_message)(const char *title, const char *message);

    bool (*publish_qr)(const Product *product);
    bool (*publish_error)(const char *message);

    /* Usados para la cola offline persistente cuando MQTT no esta disponible. */
    bool (*store_pending_qr)(const Product *product);
    bool (*flush_pending)(void);
} FsmCallbacks;

void fsm_init(void);
State fsm_get_current_state(void);
void fsm_execute_transition(EventType event);

bool fsm_post_event(EventType event);
void fsm_register_callbacks(const FsmCallbacks *callbacks);
void fsm_set_auto_events_enabled(bool enabled);

/* Entrada usada por cámara/MQTT/UART cuando ya existe un QR decodificado. */
bool fsm_on_qr_detected(const char *id, const char *name);
bool fsm_on_qr_invalid(const char *reason);

/* Compatibilidad con código anterior de qr_handler. */
void fsm_set_active_product(Product prod);

uint32_t fsm_get_selected_quantity(void);
void fsm_set_selected_quantity(uint32_t quantity);

#endif // FSM_H

