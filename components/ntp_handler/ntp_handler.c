#include <stdio.h>
#include <time.h>
#include <stdlib.h>

#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "ntp_handler.h"

static const char *TAG = "NTP_HANDLER";

void init_time(void)
{
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org"); // Se crea la configuracion SNTP usando el servidor pool.ntp.org para sacar la hora de internet

    esp_netif_sntp_init(&config); // Se inicializa SNTP con la configuracion anterior

    esp_err_t ret = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(10000)); // Se espera hasta 10 segundos a que la hora se sincronice

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Hora sincronizada con NTP");
    } else {
        ESP_LOGW(TAG, "No se pudo sincronizar la hora con NTP");
    }

    // Uruguay es UTC-3. 
    setenv("TZ", "<-03>3", 1); // TZ: nombre de la variable donde se guarda la zona horaria. "<-03>3": indica UTC-3. 1: permite sobrescribir TZ si ya tenia otro valor.
    tzset(); // Sacado de https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/system_time.html
}

bool get_timestamp(time_t *timestamp)
{
    if (timestamp == NULL) { // Se verifica que el puntero exista
        return false;
    }

    time_t ahora; // Variable donde se guarda la hora actual en formato time_t
    time(&ahora); // Se obtiene la hora actual del sistema

    struct tm timeinfo; // Estructura usada solo para comprobar que la hora ya este sincronizada
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