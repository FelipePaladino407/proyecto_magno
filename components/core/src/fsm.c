#include "fsm.h"
#include "core.h"
#include "esp_log.h"
#include "ev_queue.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <string.h>

static const char *TAG = "FSM";
static State current_state;

static const Transition transition_table_a[] = {
    {STATE_SETUP, EV_SETUP_SUCCESS, STATE_IDLE, action_reset_to_idle}, //
    {STATE_SETUP, EV_SETUP_FAILURE, STATE_SETUP, action_setup},        //

    {STATE_IDLE, EV_QR_SCANNED, STATE_MQTT_PUBLISHING, action_mqtt_publish}, //

    {STATE_MQTT_PUBLISHING, EV_MQTT_PUBLISH_SUCCESS, STATE_IDLE, action_reset_to_idle}, //
    {STATE_MQTT_PUBLISHING, EV_MQTT_PUBLISH_FAILURE, STATE_ERROR, action_throw_error},  //

    {STATE_ERROR, EV_BTN_SELECT, STATE_IDLE, action_reset_to_idle}, //
    {STATE_ERROR, EV_BTN_RETURN, STATE_IDLE, action_reset_to_idle}, //
};

static const Transition transition_table_b[] = {
    {STATE_SETUP, EV_SETUP_SUCCESS, STATE_IDLE, action_reset_to_idle}, //
    {STATE_SETUP, EV_SETUP_FAILURE, STATE_SETUP, action_setup},        //

    {STATE_IDLE, EV_QR_RECEIVED, STATE_DB_LOOKUP, action_db_lookup}, //

    {STATE_DB_LOOKUP, EV_PRODUCT_FOUND, STATE_PROMPT_ADD_PRODUCT, action_prompt_add_product}, //
    {STATE_DB_LOOKUP, EV_PRODUCT_NOT_FOUND, STATE_ERROR, action_throw_error},                 //

    {STATE_PROMPT_ADD_PRODUCT, EV_BTN_SELECT, STATE_QUANTITY_SELECTION, action_enter_quantity_selection}, //
    {STATE_PROMPT_ADD_PRODUCT, EV_BTN_RETURN, STATE_IDLE, action_reset_to_idle},                          //

    {STATE_QUANTITY_SELECTION, EV_BTN_UP, STATE_QUANTITY_SELECTION, action_quantity_up},     //
    {STATE_QUANTITY_SELECTION, EV_BTN_DOWN, STATE_QUANTITY_SELECTION, action_quantity_down}, //
    {STATE_QUANTITY_SELECTION, EV_BTN_SELECT, STATE_STOCK_UPDATING, action_stock_update},
    {STATE_QUANTITY_SELECTION, EV_BTN_RETURN, STATE_IDLE, action_reset_to_idle}, //

    {STATE_STOCK_UPDATING, EV_STOCK_UPDATE_SUCCESS, STATE_PRODUCT_OVERVIEW, action_product_overview}, //
    {STATE_STOCK_UPDATING, EV_STOCK_UPDATE_FAILURE, STATE_ERROR, action_throw_error},                 //

    {STATE_PRODUCT_OVERVIEW, EV_BTN_SELECT, STATE_MQTT_PUBLISHING, action_mqtt_publish}, //

    {STATE_MQTT_PUBLISHING, EV_MQTT_PUBLISH_SUCCESS, STATE_IDLE, action_reset_to_idle}, //
    {STATE_MQTT_PUBLISHING, EV_MQTT_PUBLISH_FAILURE, STATE_ERROR, action_throw_error},  //

    {STATE_ERROR, EV_BTN_SELECT, STATE_MQTT_PUBLISHING, action_mqtt_publish}, //
    {STATE_ERROR, EV_BTN_RETURN, STATE_IDLE, action_reset_to_idle},           //
};

void fsm_init(void) {
    current_state = STATE_IDLE;
    sys_reset_context();
    xTaskCreate(&fsm_task, "FSM_TASK", 4096, NULL, 1, NULL);
}

void fsm_task(void *pvParameters) {
    ESP_LOGI(TAG, "FSM task started");

    EventType event;

    while (1) {
        // La idea es bloquear indefinidamente hasta que llegue una task.
        if (ev_queue_receive(&event, 0)) {
            fsm_execute_transition(event);
        }
    }
}

void fsm_execute_transition(EventType event, const Transition transition_table[], size_t table_length) {
    if (current_state == STATE_IDLE && (event == EV_MQTT_PUBLISH_SUCCESS || event == EV_MQTT_PUBLISH_FAILURE)) {
        ESP_LOGD(TAG, "Ignorando ACK MQTT tardio en IDLE");
        return;
    }

    for (size_t i = 0; i < table_length; i++) {
        if (transition_table[i].state == current_state && transition_table[i].event == event) {

            State previous_state = current_state;
            current_state = transition_table[i].next_state;

            ESP_LOGI(TAG, "Transition: state %d + event %d -> state %d", previous_state, event, current_state);

            if (transition_table[i].action != NULL) {
                transition_table[i].action();
            }

            return;
        }
    }

    ESP_LOGW(TAG, "No transition found for state %d + event %d", current_state, event);
}

State fsm_get_state(void) {
    return current_state;
}
