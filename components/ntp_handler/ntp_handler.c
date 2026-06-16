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
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org"); // Se crea la configuración SNTP usando el servidor pool.ntp.org para sacar la hora de internet

    esp_netif_sntp_init(&config); // Se inicializa SNTP con la configuración anterior

    esp_err_t ret = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(10000)); // Se espera hasta 10 segundos a que la hora se sincronice

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Hora sincronizada con NTP");
    } else {
        ESP_LOGW(TAG, "No se pudo sincronizar la hora con NTP");
    }

    // Uruguay es UTC-3
    setenv("TZ", "<-03>3", 1);//TZ:nombre de la variable donde se guarda la zona horaria."<-03>3": indica UTC−3. 1: permite sobrescribir TZ si ya tenía otro valor.
    tzset();
//sacado de https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/system_time.html
}

bool get_fecha_hora(char *fecha_hora, size_t max_len)
{
    if (fecha_hora == NULL || max_len == 0) {// Se verifica que el buffer exista y que tenga tamaño válido
        return false;
    }

    time_t ahora; // Variable donde se guarda la hora actual en formato interno del sistema (segundos pasados desde 1970-01-01 00:00:00 UTC)
    time(&ahora);// Se obtiene la hora actual del sistema

    struct tm timeinfo; // Estructura donde se separa la hora en año, mes, día, hora, minuto y segundo
    localtime_r(&ahora, &timeinfo);// Se convierte la hora actual a hora local, usando la zona horaria configurada

    // tm_year guarda los años pasados desde 1900 (por eso 2020-1900S). Si el año calculado es menor a 2020,
    // se asume que la hora todavía no se sincronizó bien con NTP.
    if (timeinfo.tm_year < (2020 - 1900)) {
        snprintf(fecha_hora, max_len, "HORA_NO_SYNC");
        return false;
    }

    strftime(fecha_hora,max_len,"%Y-%m-%d %H:%M:%S",&timeinfo);
    // Convierte la hora separada en timeinfo a un texto con formato "año-mes-día hora:minuto:segundo".
    return true;
}