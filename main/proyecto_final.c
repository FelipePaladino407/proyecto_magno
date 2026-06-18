#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "fsm.h"
#include "input_handler.h"
#include "product_db.h"

#include <stdint.h>

static const char *TAG = "MAIN";

QueueHandle_t fsm_event_queue = NULL;

void fsm_task(void *pvParameters)
{
    EventType incoming_event;

    ESP_LOGI(TAG, "FSM Task started");

    while (1) {
        if (xQueueReceive(fsm_event_queue, &incoming_event, portMAX_DELAY) == pdPASS) {
            ESP_LOGI(TAG, "EVENT RECEIVED: %d", incoming_event);
            fsm_execute_transition(incoming_event);
        }
    }
}

/*
 * Task temporal para probar sin cámara.
 * Simula que llegó un QR después de 2 segundos.
 */
static void fake_qr_test_task(void *pvParameters)
{
    EventType ev = EV_QR_CAPTURED;

    vTaskDelay(pdMS_TO_TICKS(2000));

    ESP_LOGI(TAG, "Enviando brownies podridos al Mullin..."); // PRIMER QR
    xQueueSend(fsm_event_queue, &ev, pdMS_TO_TICKS(100));

    vTaskDelay(pdMS_TO_TICKS(2000));

    // los dos eventos van a tirar lo mismo
    ESP_LOGI(TAG, "Limpiando la heladera de Alfredo...");
    xQueueSend(fsm_event_queue, &ev, pdMS_TO_TICKS(100));

    vTaskDelete(NULL);
}

void app_main(void)
{
    product_db_init();
    fsm_init();

    fsm_event_queue = xQueueCreate(20, sizeof(EventType));

    if (fsm_event_queue == NULL) {
        ESP_LOGE(TAG, "CRITICAL: Failed to create FSM Event Queue");
        return;
    }

    // boton
    button_int_config();

    xTaskCreate(&fsm_task, "FSM_TASK", 4096, NULL, 1, NULL);

    // Task de prueba sin cámara ni MQTT.
    xTaskCreate(&fake_qr_test_task, "FAKE_QR_TEST", 4096, NULL, 1, NULL);
}
