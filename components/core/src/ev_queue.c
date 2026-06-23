#include "ev_queue.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <stdbool.h>

#define EV_QUEUE_LENGTH 16

static QueueHandle_t ev_queue;

const char *TAG = "EV_QUEUE";

void ev_queue_init(void) {
    if (ev_queue != NULL) {
        ESP_LOGW(TAG, "ev_queue_init called more than once — ignored");
        return;
    }

    ev_queue = xQueueCreate(EV_QUEUE_LENGTH, sizeof(EventType));
    ESP_LOGI(TAG, "Event queue created");
}

bool ev_queue_post(EventType event) {
    if (ev_queue == NULL) {
        ESP_LOGW(TAG, "ev_queue_post: queue not initialised, event %d lost", event);
        return false;
    }

    bool ok = xQueueSend(ev_queue, &event, pdMS_TO_TICKS(10)) == pdPASS;

    if (!ok) {
        ESP_LOGW(TAG, "ev_queue_post: queue full, event %d dropped", event);
    }

    return ok;
}

bool ev_queue_post_from_isr(EventType event, BaseType_t *pxHigherPriorityTaskWoken) {
    if (ev_queue == NULL) {
        return false;
    }

    return xQueueSendFromISR(ev_queue, &event, pxHigherPriorityTaskWoken) == pdPASS;
}

bool ev_queue_receive(EventType *out_event, uint32_t timeout_ms) {
    if (ev_queue == NULL || out_event == NULL) {
        return false;
    }

    TickType_t ticks = (timeout_ms == 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);

    return xQueueReceive(ev_queue, out_event, ticks) == pdPASS;
}
