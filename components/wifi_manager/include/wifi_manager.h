#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <stdbool.h>
#include "esp_err.h"

// Red creada por el ESP32 para abrir la pagina de configuracion. 
#define WIFI_MANAGER_AP_SSID       "ESP32"
#define WIFI_MANAGER_AP_PASS       "config123"
#define WIFI_MANAGER_AP_CHANNEL    6
#define WIFI_MANAGER_MAX_STA_CONN  4

typedef struct {
    bool credentials_saved;
    bool connected;
    char sta_ssid[33];
    char ip_address[16];
} wifi_manager_status_t;

/*
 * Inicializa NVS, esp_netif, el bucle de eventos y el Wi-Fi una sola vez.
 * El ESP32 queda en modo AP+STA:
 *   - AP: permite entrar a la pagina de configuracion.
 *   - STA: intenta conectarse a la red guardada, si existe.
 */
esp_err_t wifi_manager_init(void);

// Guarda las credenciales en NVS e intenta conectarse inmediatamente.
esp_err_t wifi_manager_set_credentials(const char *ssid, const char *password);

// Borra las credenciales guardadas y desconecta la interfaz STA.
esp_err_t wifi_manager_clear_credentials(void);

// Copia el estado actual del Wi-Fi en la estructura recibida
void wifi_manager_get_status(wifi_manager_status_t *status);

// Devuelve el nombre de la red de configuracion creada por el ESP32.
const char *wifi_manager_get_ap_ssid(void);

// Compatibilidad con el codigo anterior. Ambas inicializan el modo AP+STA.
void wifi_init_softap(void);
void wifi_init_sta(void);


#endif