#include "include/input_handler.h"
#include "freertos/FreeRTOS.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "hal/gpio_types.h"
#include "fsm.h"
#include "freertos/queue.h"

static const char *TAG = "INPUT_MODULE";
extern QueueHandle_t fsm_event_queue; 

static void IRAM_ATTR button_isr_handler(void *arg) {
    uintptr_t gpio_num = (uintptr_t)arg;
    EventType event_to_send;

    if (gpio_num == BUTTON_SELECT_PIN) {
        event_to_send = EV_BTN_SELECT;
    } else if (gpio_num == BUTTON_UP_PIN) {
        event_to_send = EV_BTN_UP;
    } else if (gpio_num == BUTTON_DOWN_PIN) {
        event_to_send = EV_BTN_DOWN;
    } else {
        return; 
    }

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

// MUY IMPORTANTE NO USAR EL xQueueSend común. 
    xQueueSendFromISR(fsm_event_queue, &event_to_send, &xHigherPriorityTaskWoken);

    if (xHigherPriorityTaskWoken == pdTRUE) {
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}

void button_int_config(void) {
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_NEGEDGE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << BUTTON_SELECT_PIN) | 
                        (1ULL << BUTTON_UP_PIN)    | 
                        (1ULL << BUTTON_DOWN_PIN),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_ENABLE 
    };

    gpio_config(&io_conf);

    gpio_install_isr_service(0);

    gpio_isr_handler_add(BUTTON_SELECT_PIN, button_isr_handler, (void *)BUTTON_SELECT_PIN);
    gpio_isr_handler_add(BUTTON_UP_PIN,    button_isr_handler, (void *)BUTTON_UP_PIN);
    gpio_isr_handler_add(BUTTON_DOWN_PIN,  button_isr_handler, (void *)BUTTON_DOWN_PIN);
}
