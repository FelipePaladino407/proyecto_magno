#include "qr_handler.h"
#include "esp_camera.h" // esp_camera_init, esp_camera_fb_get, etc.
#include "esp_log.h"    // ESP_LOGI, ESP_LOGE
#include "freertos/FreeRTOS.h"
#include "freertos/task.h" // xTaskCreate, vTaskDelay, vTaskDelete
#include "qr_handler_config.h"
#include "quirc.h" // quirc_new, quirc_resize, quirc_decode, etc.
#include <stdio.h>
#include <string.h> // memcpy

static const char *TAG = "qr_handler";

static qr_detected_callback_t s_qr_callback = NULL;

void qr_handler_set_callback(qr_detected_callback_t callback) {
    s_qr_callback = callback;
}

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
            vTaskDelay(pdMS_TO_TICKS(50));
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

                char raw_payload[256] = {0};
                int payload_len = data.payload_len;

                // limitar por seguridad para generar overflow en el buffer
                if (payload_len >= sizeof(raw_payload)) {
                    payload_len = sizeof(raw_payload) - 1;
                }

                // copiar los bytes reales y forzar el terminador nullete
                memcpy(raw_payload, data.payload, payload_len);
                raw_payload[payload_len] = '\0';

                char id[257] = {0};
                char product_name[257] = {0};

                const char *sep = strchr(raw_payload, '|');

                if (sep != NULL) {
                    size_t id_len = sep - raw_payload;

                    // limitar la copia del id para no overflowar buffer
                    if (id_len >= sizeof(id)) {
                        id_len = sizeof(id) - 1;
                    }

                    strncpy(id, raw_payload, id_len);
                    id[id_len] = '\0'; // strncpy no garantiza el \0 si se llega al limite

                    strncpy(product_name, sep + 1, sizeof(product_name) - 1);
                    product_name[sizeof(product_name) - 1] = '\0';
                } else {
                    strncpy(id, raw_payload, sizeof(id) - 1);
                    id[sizeof(id) - 1] = '\0';
                }

                char json_payload[192] = {0};
                snprintf(json_payload, sizeof(json_payload), "{\"id\":\"%s\",\"name\":\"%s\"}", id, product_name);
                ESP_LOGI(TAG, "QR completo: %s", json_payload);

                if (s_qr_callback != NULL) {
                    s_qr_callback(id, product_name);
                } else {
                    ESP_LOGW(TAG, "QR detectado pero no hay callback registrado");
                }
                vTaskDelay(pdMS_TO_TICKS(3000));
            }
        }
    }
}

void qr_handler_init(void) {
    if (esp_camera_init(&QR_CAMERA_CONFIG) != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo inicializar la cámara.");
        return;
    }
    sensor_t *s = esp_camera_sensor_get();
    s->set_contrast(s, -1);    // más contraste: -2 a 2
    s->set_brightness(s, -1);  // brillo normal
    s->set_saturation(s, -2); // menos saturación (ya es grayscale pero ayuda)
    s->set_sharpness(s, 0);   // más nitidez
    // s->set_aec2(s, 1);
    s->set_awb_gain(s,1);
    ESP_LOGI(TAG, "Cámara inicializada.");
    xTaskCreate(qr_scan_task, "qr_scan", 32768, NULL, 4, NULL);
}
