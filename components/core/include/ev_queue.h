#ifndef EV_QUEUE_H
#define EV_QUEUE_H

#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"

/*
 * La FSM se piensa como la lógica de la placa 2:
 * recibe QR ya decodificados, pregunta por LCD/touch, actualiza stock y publica.
 */
typedef enum {
    EV_WIFI_CONNECT_SUCCESS,
    EV_QR_CAPTURED,
    EV_SCAN_SUCCESS,
    EV_SCAN_INVALID,
    EV_PRODUCT_FOUND,
    EV_PRODUCT_NOT_FOUND,
    EV_STOCK_UPDATED,
    EV_MQTT_CONNECT_FAILURE,
    EV_MQTT_CONNECT_SUCCESS,
    EV_MQTT_PUBLISH_SUCCESS,
    EV_MQTT_PUBLISH_FAILURE,
    EV_BTN_UP,
    EV_BTN_DOWN,
    EV_BTN_SELECT,
    EV_BTN_EXIT,
    EV_TIMEOUT
} EventType;

/* Must be called before any producer or consumer starts. */
void ev_queue_init(void);

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
