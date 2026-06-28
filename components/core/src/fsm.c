#include "fsm.h"
#include "core.h"
#include "esp_log.h"
#include "ev_queue.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "shared_types.h"
#include <string.h>

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

static const char *TAG = "FSM";
static State current_state;
static DeviceMode s_device_mode;

static const Transition transition_table_a[] = {
    {STATE_SETUP, EV_SETUP, STATE_SETUP, action_setup},
    {STATE_SETUP, EV_SETUP_SUCCESS, STATE_IDLE, action_reset_to_idle},
    {STATE_SETUP, EV_SETUP_FAILURE, STATE_SETUP, action_retry_setup},

    {STATE_IDLE, EV_QR_SCANNED, STATE_MQTT_PUBLISHING, action_mqtt_publish}, //

    {STATE_MQTT_PUBLISHING, EV_MQTT_PUBLISH_SUCCESS, STATE_IDLE, action_reset_to_idle}, //
    {STATE_MQTT_PUBLISHING, EV_MQTT_PUBLISH_FAILURE, STATE_ERROR, action_throw_error},  //

    {STATE_ERROR, EV_BTN_SELECT, STATE_IDLE, action_reset_to_idle}, //
    {STATE_ERROR, EV_BTN_RETURN, STATE_IDLE, action_reset_to_idle}, //
};

static const Transition transition_table_b[] = {
    {STATE_SETUP, EV_SETUP, STATE_SETUP, action_setup},
    {STATE_SETUP, EV_SETUP_SUCCESS, STATE_IDLE, action_reset_to_idle},
    {STATE_SETUP, EV_SETUP_FAILURE, STATE_SETUP, action_retry_setup},

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

void fsm_init() {
    current_state = STATE_SETUP;
    s_device_mode = sys_get_mode();
    ESP_LOGI(TAG, "FSM mode %d", s_device_mode);

    sys_reset_context();

    if (s_device_mode == DEVICE_MODE_LCD) {
        ESP_LOGI(TAG, "FSM initialized in LCD mode (B)");
    } else {
        ESP_LOGI(TAG, "FSM initialized in CAM mode (A)");
    }

    xTaskCreate(&fsm_task, "FSM_TASK", 2048, NULL, 1, NULL);
    ESP_LOGI(TAG, "FSM task created");
}

static void fsm_execute_transition(EventType event, const Transition transition_table[], size_t table_length) {
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

void fsm_task(void *pvParameters) {
    (void)pvParameters;

    const Transition *table;
    size_t table_length;

    if (s_device_mode == DEVICE_MODE_LCD) {
        table = transition_table_b;
        table_length = ARRAY_SIZE(transition_table_b);
    } else {
        table = transition_table_a;
        table_length = ARRAY_SIZE(transition_table_a);
    }

    EventType event;

    while (1) {
        if (ev_queue_receive(&event, 0)) {
            fsm_execute_transition(event, table, table_length);
        }
    }
}

State fsm_get_state(void) {
    return current_state;
}
