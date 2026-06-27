#include "core.h"
#include "esp_err.h"
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
#include "wifi_manager.h"
#include <stdbool.h>

static const char *TAG = "MAIN";

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

void setup(void) {
    ESP_ERROR_CHECK(nvs_storage_init());   // NVS
    ESP_ERROR_CHECK(wifi_manager_init());  // WIFI
    ESP_ERROR_CHECK(http_handler_start()); // HTTP
    ESP_ERROR_CHECK(core_init());          // CORE

    if (product_db_init() != ESP_OK) {
        ev_queue_post(EV_SETUP_FAILURE);
    } // ProductDB

    if (button_int_config() != ESP_OK) {
        ev_queue_post(EV_SETUP_FAILURE);
    } // BTN

    if (rgb_led_init() != ESP_OK) {
        ev_queue_post(EV_SETUP_FAILURE);
    } // RGB LEDs

    load_demo_catalog();
}

void app_main(void) {
    logger_init(&logger_local, "local");
    logger_init(&logger_recibido, "recibido");
    mqtt_handler_set_loggers(&logger_local, &logger_recibido);

    ntp_init();
    mqtt_handler_init();

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
