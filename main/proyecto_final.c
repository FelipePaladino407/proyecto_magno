#include "freertos/idf_additions.h"
#include "freertos/projdefs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "fsm.h"
#include "logger.h"
#include "ntp_handler.h"
#include "mqtt_handler.h"
#include "wifi_manager.h"
#include "http_handler.h"
#include "nvs.h"
#include "rgb_led.h"
#include <stdint.h>

static const char *TAG = "MAIN";
QueueHandle_t fsm_event_queue = NULL;
static Logger logger_local;
static Logger logger_recibido;

void fsm_task(void *pvParameters) {
    EventType incoming_event;
    ESP_LOGI(TAG, "FSM Task started");
    while (1) {
        if (xQueueReceive(fsm_event_queue, &incoming_event, portMAX_DELAY) == pdPASS) {
            ESP_LOGI(TAG, "EVENT RECEIVED: %d", incoming_event);
            fsm_execute_transition(incoming_event);
        }
    }
}

void app_main(void) {
    fsm_event_queue = xQueueCreate(20, sizeof(EventType));
    if (fsm_event_queue == NULL) {
        ESP_LOGE(TAG, "CRITICAL: Failed to create FSM Event Queue");
        return;
    }

    fsm_init();
    rgb_led_init();

    ESP_ERROR_CHECK(nvs_storage_init());
    ESP_ERROR_CHECK(wifi_manager_init());
    ESP_ERROR_CHECK(http_handler_start());

    ESP_LOGI(TAG, "Conectate a la red %s", wifi_manager_get_ap_ssid());
    ESP_LOGI(TAG, "Abre en el navegador: http://192.168.4.1");

    logger_init(&logger_local, "local");
    logger_init(&logger_recibido, "recibido");
    mqtt_handler_set_loggers(&logger_local, &logger_recibido);

    // NTP y MQTT después del wifi
    init_time();
    iniciar_mqtt();

    xTaskCreate(&fsm_task, "FSM_TASK", 4096, NULL, 1, NULL);
}
