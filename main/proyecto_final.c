#include "freertos/idf_additions.h"
#include "freertos/projdefs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "fsm.h"
#include "wifi_manager.h"
#include "web_server.h"

static const char *TAG = "main";

QueueHandle_t fsm_event_queue;

void fsm_task(void *pvParameters) {
  while (1) {
    EventType incoming_event;
    if (xQueueReceive(fsm_event_queue, &incoming_event, pdMS_TO_TICKS(100)) ==
        pdPASS) {
      fsm_execute_transition(incoming_event);
    }
  }
  vTaskDelay(pdMS_TO_TICKS(100));
}

void app_main(void) {
  fsm_event_queue = xQueueCreate(20, sizeof(EventType));  // primero la cola

  fsm_init();

  ESP_ERROR_CHECK(wifi_manager_init());
  ESP_ERROR_CHECK(web_server_start());

  ESP_LOGI(TAG, "Conectate a la red %s", wifi_manager_get_ap_ssid());
  ESP_LOGI(TAG, "Abre en el navegador: http://192.168.4.1");

  xTaskCreate(&fsm_task, "FSM_TASK", 4096, NULL, 1, NULL);
}
