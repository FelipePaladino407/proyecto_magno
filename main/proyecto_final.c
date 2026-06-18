#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "fsm.h"
#include "logger.h"
#include "ntp_handler.h"
#include "mqtt_handler.h"
#include "input_handler.h"
#include <stdint.h>

/*
* IMPORTANTE: DEFINIR FORMA DE CAMBIAR EL COMPORTAMIENTO EN BASE AL
* DISPOSITIVO A UTILIZAR
* */

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
  fsm_init();
  fsm_event_queue = xQueueCreate(20, sizeof(EventType));

  if (fsm_event_queue == NULL) {
    ESP_LOGE(TAG, "CRITICAL: Failed to create FSM Event Queue");
    return;
  }

  logger_init(&logger_local, "local");
  logger_init(&logger_recibido, "recibido");

  mqtt_handler_set_loggers(&logger_local, &logger_recibido);

  //ojo antes de init_time() tiene que estar inicializado WiFi
  init_time();
  iniciar_mqtt();

  // CONFIGURACION DE INTERRUPCIONES DE GPIO
  // IRRELEVANTE E INUTIL
  // button_int_config();

  xTaskCreate(&fsm_task, "FSM_TASK", 4096, NULL, 1, NULL);
}
