#ifndef EV_QUEUE_H
#define EV_QUEUE_H

#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"

#define EV_BTN_OK EV_BTN_SELECT

typedef enum {
    EV_SETUP,
    EV_SETUP_FAILURE,
    EV_SETUP_SUCCESS,

    EV_QR_SCANNED,
    EV_QR_RECEIVED,

    EV_MQTT_PUBLISH_SUCCESS,
    EV_MQTT_PUBLISH_FAILURE,

    EV_PRODUCT_FOUND,
    EV_PRODUCT_NOT_FOUND,

    EV_STOCK_UPDATE_SUCCESS,
    EV_STOCK_UPDATE_FAILURE,

    EV_BTN_UP,
    EV_BTN_DOWN,
    EV_BTN_SELECT,
    EV_BTN_RETURN,

    EV_TIMEOUT
} EventType;

/* Must be called before any producer or consumer starts. */
esp_err_t ev_queue_init(void);

/* Post from a normal task context.
 * Returns false if the queue is full or not initialised. */
bool ev_queue_post(EventType event);

/* Post from an ISR.
 * pxHigherPriorityTaskWoken follows FreeRTOS convention. */
bool ev_queue_post_from_isr(EventType event, BaseType_t *pxHigherPriorityTaskWoken);

/* Block until an event is available.
 * timeout_ms == 0  → portMAX_DELAY (block forever).
 * Returns false on timeout. */
bool ev_queue_receive(EventType *out_event, uint32_t timeout_ms);

#endif
