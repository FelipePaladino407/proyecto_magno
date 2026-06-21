#include "unit_test.h"
#include "fsm.h"
#include "esp_log.h"

#include <stdbool.h>
#include <stddef.h>

static const char *TAG = "FSM_TEST";

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

static bool run_fsm_case(const char *test_name,
                         const EventType *events,
                         size_t event_count,
                         State expected_state) {
    fsm_init();

    ESP_LOGI(TAG, "------------------------------");
    ESP_LOGI(TAG, "Running test: %s", test_name);
    ESP_LOGI(TAG, "Initial state: %d", fsm_get_current_state());

    for (size_t i = 0; i < event_count; i++) {
        State before = fsm_get_current_state();

        ESP_LOGI(TAG,
                 "Step %d: state %d + event %d",
                 (int)i,
                 before,
                 events[i]);

        fsm_execute_transition(events[i]);

        State after = fsm_get_current_state();

        ESP_LOGI(TAG,
                 "Step %d result: state %d",
                 (int)i,
                 after);
    }

    State final_state = fsm_get_current_state();

    if (final_state == expected_state) {
        ESP_LOGI(TAG,
                 "PASS: %s | final state = %d",
                 test_name,
                 final_state);
        return true;
    } else {
        ESP_LOGE(TAG,
                 "FAIL: %s | expected state = %d, got state = %d",
                 test_name,
                 expected_state,
                 final_state);
        return false;
    }
}

void fsm_run_transition_tests(void) {
    fsm_set_auto_events_enabled(false);

    int passed = 0;
    int total = 0;

#define RUN_TEST(name, seq, expected)                  \
    do {                                               \
        total++;                                       \
        if (run_fsm_case(name, seq, ARRAY_SIZE(seq), expected)) { \
            passed++;                                  \
        }                                              \
    } while (0)

    const EventType t1[] = {
        EV_QR_CAPTURED
    };
    RUN_TEST("IDLE + QR_CAPTURED -> SCAN_PROCESSING",
             t1,
             STATE_SCAN_PROCESSING);

    const EventType t2[] = {
        EV_BTN_SELECT
    };
    RUN_TEST("IDLE + BTN_SELECT -> MANUAL_SELECTION",
             t2,
             STATE_MANUAL_SELECTION);

    const EventType t3[] = {
        EV_QR_CAPTURED,
        EV_SCAN_INVALID
    };
    RUN_TEST("SCAN_PROCESSING + SCAN_INVALID -> ERROR_DISPLAY",
             t3,
             STATE_ERROR_DISPLAY);

    const EventType t4[] = {
        EV_QR_CAPTURED,
        EV_SCAN_INVALID,
        EV_BTN_SELECT
    };
    RUN_TEST("ERROR_DISPLAY + BTN_SELECT -> IDLE",
             t4,
             STATE_IDLE);

    const EventType t5[] = {
        EV_QR_CAPTURED,
        EV_SCAN_INVALID,
        EV_TIMEOUT
    };
    RUN_TEST("ERROR_DISPLAY + TIMEOUT -> IDLE",
             t5,
             STATE_IDLE);

    const EventType t6[] = {
        EV_QR_CAPTURED,
        EV_SCAN_SUCCESS
    };
    RUN_TEST("SCAN_PROCESSING + SCAN_SUCCESS -> LOCAL_DB_LOOKUP",
             t6,
             STATE_LOCAL_DB_LOOKUP);

    const EventType t7[] = {
        EV_QR_CAPTURED,
        EV_SCAN_SUCCESS,
        EV_PRODUCT_FOUND
    };
    RUN_TEST("LOCAL_DB_LOOKUP + PRODUCT_FOUND -> DISPLAY_PRODUCT_INFO",
             t7,
             STATE_DISPLAY_PRODUCT_INFO);

    const EventType t8[] = {
        EV_QR_CAPTURED,
        EV_SCAN_SUCCESS,
        EV_PRODUCT_NOT_FOUND
    };
    RUN_TEST("LOCAL_DB_LOOKUP + PRODUCT_NOT_FOUND -> PROMPT_ADD_NEW",
             t8,
             STATE_PROMPT_ADD_NEW);

    const EventType t9[] = {
        EV_QR_CAPTURED,
        EV_SCAN_SUCCESS,
        EV_PRODUCT_NOT_FOUND,
        EV_BTN_EXIT
    };
    RUN_TEST("PROMPT_ADD_NEW + BTN_EXIT -> IDLE",
             t9,
             STATE_IDLE);

    const EventType t10[] = {
        EV_QR_CAPTURED,
        EV_SCAN_SUCCESS,
        EV_PRODUCT_NOT_FOUND,
        EV_TIMEOUT
    };
    RUN_TEST("PROMPT_ADD_NEW + TIMEOUT -> IDLE",
             t10,
             STATE_IDLE);

    const EventType t11[] = {
        EV_QR_CAPTURED,
        EV_SCAN_SUCCESS,
        EV_PRODUCT_NOT_FOUND,
        EV_BTN_SELECT
    };
    RUN_TEST("PROMPT_ADD_NEW + BTN_SELECT -> DISPLAY_PRODUCT_INFO",
             t11,
             STATE_DISPLAY_PRODUCT_INFO);

    const EventType t12[] = {
        EV_QR_CAPTURED,
        EV_SCAN_SUCCESS,
        EV_PRODUCT_FOUND,
        EV_STOCK_UPDATED
    };
    RUN_TEST("DISPLAY_PRODUCT_INFO + STOCK_UPDATED -> MQTT_PUBLISHING",
             t12,
             STATE_MQTT_PUBLISHING);

    const EventType t13[] = {
        EV_QR_CAPTURED,
        EV_SCAN_SUCCESS,
        EV_PRODUCT_FOUND,
        EV_STOCK_UPDATED,
        EV_MQTT_PUBLISH_SUCCESS
    };
    RUN_TEST("MQTT_PUBLISHING + MQTT_PUBLISH_SUCCESS -> IDLE",
             t13,
             STATE_IDLE);

    const EventType t14[] = {
        EV_QR_CAPTURED,
        EV_SCAN_SUCCESS,
        EV_PRODUCT_FOUND,
        EV_STOCK_UPDATED,
        EV_MQTT_PUBLISH_FAILURE
    };
    RUN_TEST("MQTT_PUBLISHING + MQTT_PUBLISH_FAILURE -> IDLE",
             t14,
             STATE_IDLE);

    const EventType t15[] = {
        EV_SCAN_SUCCESS
    };
    RUN_TEST("Invalid event from IDLE should stay in IDLE",
             t15,
             STATE_IDLE);

#undef RUN_TEST

    ESP_LOGI(TAG, "==============================");
    ESP_LOGI(TAG, "FSM TEST SUMMARY: %d/%d passed", passed, total);
    ESP_LOGI(TAG, "==============================");

    fsm_set_auto_events_enabled(true);
}

