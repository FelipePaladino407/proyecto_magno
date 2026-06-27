#ifndef INPUT_HANDLER
#define INPUT_HANDLER

#include "esp_err.h"

#define BUTTON_SELECT_PIN GPIO_NUM_1
#define BUTTON_UP_PIN     GPIO_NUM_2
#define BUTTON_DOWN_PIN   GPIO_NUM_3

esp_err_t button_int_config(void);

#endif // !INPUT_HANDLER
