#include "core.h"
#include "esp_log.h"
#include "ev_queue.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/projdefs.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "fsm.h"
#include "logger.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "shared_types.h"
#include <stdbool.h>
#include <stdint.h>

static const char *TAG = "MAIN";

void app_main(void) {
    ESP_ERROR_CHECK(nvs_storage_init());
    ESP_ERROR_CHECK(ev_queue_init());

    int32_t mode = 0;
    esp_err_t err = nvs_storage_get_int("mode", &mode);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        // No hay modo guardado, usar CAM por defecto
        ESP_LOGI(TAG, "No se encontró modo guardado, usando CAM por defecto");
    }

    if (mode == 0) {
        sys_set_mode(DEVICE_MODE_CAM);
    } else if (mode == 1) {
        sys_set_mode(DEVICE_MODE_LCD);
    } else {
        sys_set_mode(0);
    }

    fsm_init();

    ev_queue_post(EV_SETUP); // sin esto no arranca

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
