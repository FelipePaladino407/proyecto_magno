#include "ntp_handler.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <time.h>

static const char *TAG = "NTP_HANDLER";

esp_err_t ntp_clock_init(void) {
    esp_err_t ret;

    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");

    ret = esp_netif_sntp_init(&config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SNTP. Error: 0x%x", ret);
        return ret;
    }

    ESP_LOGI(TAG, "SNTP initialized, waiting for time synchronization...");

    ret = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(10000));
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Time synchronized with NTP successfully");
    } else {
        ESP_LOGW(TAG, "Could not synchronize time with NTP (0x%x). Continuing with default time.", ret);
        // Continue execution - this is not fatal, time will be set but may be incorrect
    }

    // Uruguay is UTC-3
    setenv("TZ", "<-03>3", 1);
    tzset();

    ESP_LOGI(TAG, "Timezone set to UTC-3 (Uruguay)");

    return ESP_OK; // Always returns ESP_OK since NTP sync failure is not fatal
}

bool get_timestamp(time_t *timestamp) {
    if (timestamp == NULL) { // Se verifica que el puntero exista
        return false;
    }

    time_t ahora; // Variable donde se guarda la hora actual en formato time_t
    time(&ahora); // Se obtiene la hora actual del sistema

    struct tm timeinfo;             // Estructura usada solo para comprobar que la hora ya este sincronizada
    localtime_r(&ahora, &timeinfo); // Se convierte la hora actual a hora local, usando la zona horaria configurada

    // tm_year guarda los a;os pasados desde 1900. Si el a;o calculado es menor a 2020,
    // se asume que la hora todavia no se sincronizo bien con NTP
    if (timeinfo.tm_year < (2020 - 1900)) {
        *timestamp = 0;
        return false;
    }

    *timestamp = ahora; // Se guarda la hora como time_t en el puntero recibido
    return true;
}
