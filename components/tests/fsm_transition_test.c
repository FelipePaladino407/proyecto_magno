#include "esp_log.h"
#include "fsm.h"
#include "tests.h"
#include <stdbool.h>
#include <stddef.h>

static const char *TAG = "FSM_TEST";

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

static bool run_fsm_case(const char *test_name, const EventType *events, size_t event_count, State expected_state) {
    fsm_init();

    ESP_LOGI(TAG, "------------------------------");
    ESP_LOGI(TAG, "Running test: %s", test_name);
    ESP_LOGI(TAG, "Initial state: %d", fsm_get_state());

    for (size_t i = 0; i < event_count; i++) {
        State before = fsm_get_state();

        ESP_LOGI(TAG, "Step %d: state %d + event %d", (int)i, before, events[i]);

        fsm_execute_transition(events[i]);

        State after = fsm_get_state();

        ESP_LOGI(TAG, "Step %d result: state %d", (int)i, after);
    }

    State final_state = fsm_get_state();

    if (final_state == expected_state) {
        ESP_LOGI(TAG, "PASS: %s | final state = %d", test_name, final_state);
        return true;
    }

    ESP_LOGE(TAG, "FAIL: %s | expected state = %d, got state = %d", test_name, expected_state, final_state);
    return false;
}

void fsm_run_transition_tests(void) {
    int passed = 0;
    int total = 0;

#define RUN_TEST(name, seq, expected)                                                                                  \
    do {                                                                                                               \
        total++;                                                                                                       \
        if (run_fsm_case(name, seq, ARRAY_SIZE(seq), expected)) {                                                      \
            passed++;                                                                                                  \
        }                                                                                                              \
    } while (0)

    const EventType t1[] = {EV_QR_CAPTURED};
    RUN_TEST("IDLE + QR_CAPTURED -> SCAN_PROCESSING", t1, STATE_SCAN_PROCESSING);

    const EventType t2[] = {EV_QR_CAPTURED, EV_SCAN_INVALID};
    RUN_TEST("SCAN_PROCESSING + SCAN_INVALID -> ERROR_DISPLAY", t2, STATE_ERROR_DISPLAY);

    const EventType t3[] = {EV_QR_CAPTURED, EV_SCAN_INVALID, EV_TIMEOUT};
    RUN_TEST("ERROR_DISPLAY + TIMEOUT -> IDLE", t3, STATE_IDLE);

    const EventType t4[] = {EV_QR_CAPTURED, EV_SCAN_SUCCESS};
    RUN_TEST("SCAN_PROCESSING + SCAN_SUCCESS -> LOCAL_DB_LOOKUP", t4, STATE_LOCAL_DB_LOOKUP);

    const EventType t5[] = {EV_QR_CAPTURED, EV_SCAN_SUCCESS, EV_PRODUCT_FOUND};
    RUN_TEST("PRODUCT_FOUND -> PROMPT_ADD_PRODUCT", t5, STATE_PROMPT_ADD_PRODUCT);

    const EventType t6[] = {EV_QR_CAPTURED, EV_SCAN_SUCCESS, EV_PRODUCT_NOT_FOUND};
    RUN_TEST("PRODUCT_NOT_FOUND -> ERROR_DISPLAY", t6, STATE_ERROR_DISPLAY);

    const EventType t7[] = {EV_QR_CAPTURED, EV_SCAN_SUCCESS, EV_PRODUCT_FOUND, EV_BTN_CONFIRM};
    RUN_TEST("PROMPT_ADD_PRODUCT + CONFIRM -> QUANTITY_SELECTION", t7, STATE_QUANTITY_SELECTION);

    const EventType t8[] = {EV_QR_CAPTURED, EV_SCAN_SUCCESS, EV_PRODUCT_FOUND, EV_BTN_CANCEL};
    RUN_TEST("PROMPT_ADD_PRODUCT + CANCEL -> IDLE", t8, STATE_IDLE);

    const EventType t9[] = {EV_QR_CAPTURED, EV_SCAN_SUCCESS, EV_PRODUCT_FOUND, EV_BTN_CONFIRM, EV_BTN_UP};
    RUN_TEST("QUANTITY_SELECTION + UP -> QUANTITY_SELECTION", t9, STATE_QUANTITY_SELECTION);

    const EventType t10[] = {EV_QR_CAPTURED, EV_SCAN_SUCCESS, EV_PRODUCT_FOUND, EV_BTN_CONFIRM, EV_BTN_DOWN};
    RUN_TEST("QUANTITY_SELECTION + DOWN -> QUANTITY_SELECTION", t10, STATE_QUANTITY_SELECTION);

    const EventType t11[] = {EV_QR_CAPTURED, EV_SCAN_SUCCESS, EV_PRODUCT_FOUND, EV_BTN_CONFIRM, EV_BTN_CONFIRM};
    RUN_TEST("QUANTITY_SELECTION + CONFIRM -> STOCK_UPDATING", t11, STATE_STOCK_UPDATING);

    const EventType t12[] = {EV_QR_CAPTURED, EV_SCAN_SUCCESS, EV_PRODUCT_FOUND,
                             EV_BTN_CONFIRM, EV_BTN_CONFIRM,  EV_STOCK_UPDATED};
    RUN_TEST("STOCK_UPDATING + STOCK_UPDATED -> DISPLAY_PRODUCT_INFO", t12, STATE_DISPLAY_PRODUCT_INFO);

    const EventType t13[] = {EV_QR_CAPTURED, EV_SCAN_SUCCESS,  EV_PRODUCT_FOUND, EV_BTN_CONFIRM,
                             EV_BTN_CONFIRM, EV_STOCK_UPDATED, EV_TIMEOUT};
    RUN_TEST("DISPLAY_PRODUCT_INFO + TIMEOUT -> MQTT_PUBLISHING", t13, STATE_MQTT_PUBLISHING);

    const EventType t14[] = {EV_QR_CAPTURED, EV_SCAN_SUCCESS,  EV_PRODUCT_FOUND, EV_BTN_CONFIRM,
                             EV_BTN_CONFIRM, EV_STOCK_UPDATED, EV_TIMEOUT,       EV_MQTT_PUBLISH_SUCCESS};
    RUN_TEST("MQTT_PUBLISHING + SUCCESS -> IDLE", t14, STATE_IDLE);

    const EventType t15[] = {EV_SCAN_SUCCESS};
    RUN_TEST("Invalid event from IDLE should stay in IDLE", t15, STATE_IDLE);

#undef RUN_TEST
    ESP_LOGI(TAG, "==============================");
    ESP_LOGI(TAG, "FSM TEST SUMMARY: %d/%d passed", passed, total);
    ESP_LOGI(TAG, "==============================");
}
