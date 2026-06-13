// #include "freertos/idf_additions.h"
// #include "freertos/projdefs.h"
// #include "fsm.h"
// #include <freertos/FreeRTOS.h>

// static QueueHandle_t fsm_event_queue;

// void fsm_task(void *pvParameters) {
//   // EventType event_uuu = (EventType*) pvParameters;
//   while (1) {
//     EventType incoming_event;
//     if (xQueueReceive(fsm_event_queue, &incoming_event, pdMS_TO_TICKS(100)) ==
//         pdPASS) {
//       fsm_execute_transition(incoming_event);
//     }
//   }
//   vTaskDelay(pdMS_TO_TICKS(100));
// }

// void app_main(void) {
//   fsm_init();
//   xTaskCreate(&fsm_task, "FSM_TASK", 4096, NULL, 1, NULL);
// }
