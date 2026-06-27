#include "qr_handler.h"
#include "esp_camera.h" // esp_camera_init, esp_camera_fb_get, etc.
#include "esp_log.h"    // ESP_LOGI, ESP_LOGE
#include "freertos/FreeRTOS.h"
#include "freertos/task.h" // xTaskCreate, vTaskDelay, vTaskDelete
#include "quirc.h"         // quirc_new, quirc_resize, quirc_decode, etc.
#include <string.h>        // memcpy

static const char *TAG = "qr_handler";

static void qr_scan_task(void *pvParameters) {
    struct quirc *qr = quirc_new();
    if (qr == NULL) {
        ESP_LOGE(TAG, "quirc_new() falló");
        vTaskDelete(NULL);
        return;
    }

    while (1) {
        camera_fb_t *fb = esp_camera_fb_get();
        if (fb == NULL) {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        quirc_resize(qr, fb->width, fb->height);
        uint8_t *quirc_buf = quirc_begin(qr, NULL, NULL);
        memcpy(quirc_buf, fb->buf, fb->len);
        quirc_end(qr);
        esp_camera_fb_return(fb);

        int num_codes = quirc_count(qr);
        for (int i = 0; i < num_codes; i++) {
            struct quirc_code code_struct;
            struct quirc_data data;
            quirc_extract(qr, i, &code_struct);
            if (quirc_decode(&code_struct, &data) == QUIRC_SUCCESS) {
                ESP_LOGI(TAG, "QR: %s", (char *)data.payload);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

esp_err_t qr_scanner_handler_init(void) {
    esp_err_t ret;

    ret = esp_camera_init(&QR_CAMERA_CONFIG);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo inicializar la cámara. Error: 0x%x", ret);
        return ret;
    }

    ESP_LOGI(TAG, "Cámara inicializada.");

    BaseType_t task_created = xTaskCreate(qr_scan_task, "qr_scan", 32768, NULL, 4, NULL);
    if (task_created != pdPASS) {
        ESP_LOGE(TAG, "No se pudo crear la tarea qr_scan");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "QR handler initialized successfully");
    return ESP_OK;
}
