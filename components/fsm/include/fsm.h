#ifndef FSM
#define FSM

typedef enum {
  EV_QR_DETECT,
  EV_QR_VALID,
  EV_BTN_SELECT,
  EV_CONNECT_FAIL,
  EV_SENT_SUCCESS
} EventType ;

typedef enum {
  IDLE,
  QR_PROCESSING,
  SHOWING_INFO,
  MQTT_SENDING,
  MANUAL_MENU
} State;

void fsm_init();
State fsm_get_current_state();
void fsm_execute_transition(EventType event);


#endif // !FSM
