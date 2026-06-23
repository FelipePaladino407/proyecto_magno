#include "app_logic.h"
#include "esp_log.h"
#include "ev_queue.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/projdefs.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "fsm.h"
#include "http_handler.h"
#include "input_handler.h"
#include "logger.h"
#include "mqtt_handler.h"
#include "ntp_handler.h"
#include "nvs.h"
#include "product_db.h"
#include "rgb_led.h"
#include "tests.h"
#include "wifi_manager.h"
#include <stdbool.h>
#include <stdint.h>

static const char *TAG = "MAIN";

QueueHandle_t fsm_event_queue = NULL;

static Logger logger_local;
static Logger logger_recibido;

static void load_demo_catalog(void) {
    Product product;

    if (product_db_upsert_product("PROD001", "Cigarros", 0, &product)) {
        ESP_LOGI(TAG, "Producto demo cargado -> ID:%s | Nombre:%s | Stock:%lu", product.id, product.name,
                 (unsigned long)product.stock);
    }

    if (product_db_upsert_product("PROD002", "Brownies", 0, &product)) {
        ESP_LOGI(TAG, "Producto demo cargado -> ID:%s | Nombre:%s | Stock:%lu", product.id, product.name,
                 (unsigned long)product.stock);
    }
}

void app_main(void) {
    fsm_event_queue = xQueueCreate(20, sizeof(EventType));
    if (fsm_event_queue == NULL) {
        ESP_LOGE(TAG, "CRITICAL: Failed to create FSM Event Queue");
        return;
    }

    product_db_init();
    load_demo_catalog();
    fsm_init();

    app_logic_board_a_init();

    xTaskCreate(&fsm_task, "FSM_TASK", 4096, NULL, 1, NULL);
    button_int_config();

    rgb_led_init();

    ESP_ERROR_CHECK(nvs_storage_init());
    ESP_ERROR_CHECK(wifi_manager_init());
    ESP_ERROR_CHECK(http_handler_start());

    ESP_LOGI(TAG, "Conectate a la red %s", wifi_manager_get_ap_ssid());
    ESP_LOGI(TAG, "Abre en el navegador: http://192.168.4.1");

    logger_init(&logger_local, "local");
    logger_init(&logger_recibido, "recibido");
    mqtt_handler_set_loggers(&logger_local, &logger_recibido);

    init_time();
    iniciar_mqtt();

    nvs_test();

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
