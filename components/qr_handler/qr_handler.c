#include "qr_handler.h"
#include "qr_handler_config.h"
#include <string.h>           // memcpy
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"    // xTaskCreate, vTaskDelay, vTaskDelete
#include "esp_log.h"          // ESP_LOGI, ESP_LOGE
#include "esp_camera.h"       // esp_camera_init, esp_camera_fb_get, etc.
#include "quirc.h"            // quirc_new, quirc_resize, quirc_decode, etc.

static const char *TAG = "qr_handler";

static void qr_scan_task(void *pvParameters)
{
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
                vTaskDelay(pdMS_TO_TICKS(3000));  // Espera 3 segundos antes de continuar para evitar lecturas repetidas
            }
        }

        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

void qr_handler_init(void)
{
    if (esp_camera_init(&QR_CAMERA_CONFIG) != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo inicializar la cámara.");
        return;
    }
    sensor_t *s = esp_camera_sensor_get();
    s->set_contrast(s, 2);      // más contraste: -2 a 2
    s->set_brightness(s, 0);    // brillo normal
    s->set_saturation(s, -2);   // menos saturación (ya es grayscale pero ayuda)
    s->set_sharpness(s, 2);     // más nitidez
    
    ESP_LOGI(TAG, "Cámara inicializada.");
    xTaskCreate(qr_scan_task, "qr_scan", 32768, NULL, 4, NULL);
}