#include "wifi_manager.h"

#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_mac.h"
#include "device_role.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "nvs.h"
/* nvs_flash_init() lo hace el main via nvs_storage_init() — no lo llamamos acá */

#include "fsm.h"
#include "rgb_led.h"

/* Cola de la FSM declarada en main/proyecto_final.c */
extern QueueHandle_t fsm_event_queue;

static const char *TAG = "wifi_manager";

// Estado interno del Wi-Fi. Protegido por s_status_mux.
static bool s_initialized       = false;
static bool s_credentials_saved = false;
static bool s_sta_connected     = false;
static char s_sta_ssid[33]      = {0};
static char s_ip_address[16]    = {0};
static portMUX_TYPE s_status_mux = portMUX_INITIALIZER_UNLOCKED;


// Carga las credenciales Wi-Fi guardadas en NVS. 
// Devuelve ESP_OK si tuvo exito o un error de lo contrario.
static esp_err_t load_credentials(char *ssid, size_t ssid_size,
                                   char *password, size_t password_size)
{
    esp_err_t err;

    err = nvs_storage_get_str("ssid",
                              ssid,
                              ssid_size);

    if (err != ESP_OK)
        return err;

    err = nvs_storage_get_str("password",
                              password,
                              password_size);

    return err;
}

// Guarda las credenciales Wi-Fi en NVS.
// Devuelve ESP_OK si tuvo exito o un error de lo contrario.

static esp_err_t save_credentials(const char *ssid, const char *password)
{
    esp_err_t err;

    err = nvs_storage_set_str("ssid", ssid);

    if (err != ESP_OK)
        return err;

    return nvs_storage_set_str("password", password);
}

// Actualiza el estado interno para reflejar que la STA está desconectada.
static void set_status_disconnected(void)
{
    portENTER_CRITICAL(&s_status_mux);
    s_sta_connected  = false;
    s_ip_address[0]  = '\0';
    portEXIT_CRITICAL(&s_status_mux);
}

// Actualiza el estado interno para reflejar que la STA está conectada 
// y almacena la IP obtenida.
static void set_status_connected(const char *ip_address)
{
    portENTER_CRITICAL(&s_status_mux);
    s_sta_connected = true;
    strlcpy(s_ip_address, ip_address, sizeof(s_ip_address));
    portEXIT_CRITICAL(&s_status_mux);
}

// Handler para eventos de Wi-Fi e IP. Maneja eventos de conexión/desconexión
// y actualiza el estado interno en consecuencia. También notifica a la FSM
// cuando la conexión Wi-Fi es exitosa.
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data)
{
    (void)arg;

    if (event_base == WIFI_EVENT) {
        switch (event_id) {

        case WIFI_EVENT_AP_STACONNECTED: {
            const wifi_event_ap_staconnected_t *ev =
                (const wifi_event_ap_staconnected_t *)event_data;
            ESP_LOGI(TAG, "Dispositivo conectado al AP. AID=%d", ev->aid);
            break;
        }

        case WIFI_EVENT_AP_STADISCONNECTED: {
            const wifi_event_ap_stadisconnected_t *ev =
                (const wifi_event_ap_stadisconnected_t *)event_data;
            ESP_LOGI(TAG, "Dispositivo desconectado del AP. AID=%d", ev->aid);
            break;
        }

        case WIFI_EVENT_STA_START:
            rgb_led_set_color(255, 255, 255); // Blanco
            if (s_credentials_saved) {
                ESP_LOGI(TAG, "Intentando conectar la interfaz STA...");
                esp_wifi_connect();
            }
            break;

        case WIFI_EVENT_STA_CONNECTED:
            ESP_LOGI(TAG, "STA asociada al router. Esperando direccion IP...");
            break;

        case WIFI_EVENT_STA_DISCONNECTED: {
            const wifi_event_sta_disconnected_t *ev =
                (const wifi_event_sta_disconnected_t *)event_data;
            set_status_disconnected();
            rgb_led_set_color(255, 0, 0); // Rojo
            ESP_LOGW(TAG, "STA desconectada. Motivo=%d", ev->reason);
            if (s_credentials_saved) esp_wifi_connect();

            break;
        }

        default:
            break;
        }
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *ev = (const ip_event_got_ip_t *)event_data;
        char ip_string[16];
        snprintf(ip_string, sizeof(ip_string), IPSTR,
                 IP2STR(&ev->ip_info.ip));
        set_status_connected(ip_string);
        rgb_led_set_color(0, 255, 0); // Verde
        ESP_LOGI(TAG, "STA conectada. IP obtenida: %s", ip_string);

        /* Notificar a la FSM que el Wi-Fi esta listo solo en la placa LCD.
         * La placa CAM no levanta FSM principal, por lo que fsm_event_queue queda NULL.
         */
#if DEVICE_IS_LCD
        if (fsm_event_queue != NULL) {
            EventType ev_wifi = EV_WIFI_CONNECT_SUCCESS;
            xQueueSend(fsm_event_queue, &ev_wifi, 0);
        }
#endif
    }
}

// Llena una estructura wifi_config_t con la configuración de STA dada el SSID y
// la contraseña.
static void fill_sta_config(wifi_config_t *config,
                             const char *ssid,
                             const char *password)
{
    memset(config, 0, sizeof(*config));
    memcpy(config->sta.ssid,     ssid,     strlen(ssid));
    memcpy(config->sta.password, password, strlen(password));
}

// Inicializa el Wi-Fi en modo AP+STA. Carga las credenciales guardadas (si
// existen) y se conecta a la red configurada. También inicia el AP para el portal de configuración.
esp_err_t wifi_manager_init(void)
{
    if (s_initialized) return ESP_OK;

    /* NVS ya inicializado por el main via nvs_storage_init() */

    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;

    esp_netif_create_default_wifi_ap();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init_cfg), TAG,
                        "No se pudo inicializar el driver Wi-Fi");

    ESP_RETURN_ON_ERROR(
        esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                            wifi_event_handler, NULL, NULL),
        TAG, "No se pudo registrar el handler Wi-Fi");

    ESP_RETURN_ON_ERROR(
        esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                            wifi_event_handler, NULL, NULL),
        TAG, "No se pudo registrar el handler IP");

    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG,
                        "No se pudo configurar el almacenamiento Wi-Fi");

    wifi_config_t ap_config = {
        .ap = {
            .channel         = WIFI_MANAGER_AP_CHANNEL,
            .password        = WIFI_MANAGER_AP_PASS,
            .max_connection  = WIFI_MANAGER_MAX_STA_CONN,
            .authmode        = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg         = { .required = false },
        },
    };

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf((char *)ap_config.ap.ssid, sizeof(ap_config.ap.ssid),
        "ESP32-%02X%02X%02X", mac[3], mac[4], mac[5]);
    ap_config.ap.ssid_len = strlen((char *)ap_config.ap.ssid);

    if (strlen(WIFI_MANAGER_AP_PASS) == 0)
        ap_config.ap.authmode = WIFI_AUTH_OPEN;

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_APSTA), TAG,
                        "No se pudo seleccionar el modo AP+STA");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &ap_config), TAG,
                        "No se pudo configurar el AP");

    char saved_ssid[33] = {0};
    char saved_pass[64] = {0};
    if (load_credentials(saved_ssid, sizeof(saved_ssid),
                         saved_pass,  sizeof(saved_pass)) == ESP_OK &&
        saved_ssid[0] != '\0') {

        wifi_config_t sta_config;
        fill_sta_config(&sta_config, saved_ssid, saved_pass);
        ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &sta_config), TAG,
                            "No se pudo cargar la configuracion STA");

        portENTER_CRITICAL(&s_status_mux);
        s_credentials_saved = true;
        strlcpy(s_sta_ssid, saved_ssid, sizeof(s_sta_ssid));
        portEXIT_CRITICAL(&s_status_mux);

        ESP_LOGI(TAG, "Credenciales cargadas para SSID: %s", saved_ssid);
    } else {
        ESP_LOGI(TAG, "No hay credenciales Wi-Fi guardadas");
    }

    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "No se pudo iniciar el Wi-Fi");

    s_initialized = true;
    ESP_LOGI(TAG, "Portal disponible en la red %s", WIFI_MANAGER_AP_SSID);
    ESP_LOGI(TAG, "Conectate y abre http://192.168.4.1");

    return ESP_OK;
}

// Guarda las credenciales Wi-Fi y se conecta a la red configurada. 
// Si ya había una conexión activa, se desconecta primero.
esp_err_t wifi_manager_set_credentials(const char *ssid, const char *password)
{
    if (!s_initialized)          
        return ESP_ERR_INVALID_STATE;
    if (!ssid || !password)      
        return ESP_ERR_INVALID_ARG;

    size_t ssid_len = strlen(ssid);
    size_t pass_len = strlen(password);

    if (ssid_len == 0 || ssid_len > 32)                        
        return ESP_ERR_INVALID_ARG;
    if (pass_len > 63 || (pass_len > 0 && pass_len < 8))       
        return ESP_ERR_INVALID_ARG;

    ESP_RETURN_ON_ERROR(save_credentials(ssid, password), TAG,
                        "No se pudieron guardar las credenciales");

    wifi_config_t sta_config;
    fill_sta_config(&sta_config, ssid, password);

    portENTER_CRITICAL(&s_status_mux);
    s_credentials_saved = false;
    s_sta_connected     = false;
    s_ip_address[0]     = '\0';
    portEXIT_CRITICAL(&s_status_mux);

    esp_err_t dc_err = esp_wifi_disconnect();
    if (dc_err != ESP_OK && dc_err != ESP_ERR_WIFI_NOT_CONNECT)
        ESP_LOGW(TAG, "esp_wifi_disconnect: %s", esp_err_to_name(dc_err));

    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &sta_config), TAG,
                        "No se pudo aplicar la configuracion STA");

    portENTER_CRITICAL(&s_status_mux);
    s_credentials_saved = true;
    s_sta_connected     = false;
    strlcpy(s_sta_ssid, ssid, sizeof(s_sta_ssid));
    s_ip_address[0]     = '\0';
    portEXIT_CRITICAL(&s_status_mux);

    ESP_LOGI(TAG, "Credenciales guardadas. Conectando a: %s", ssid);
    return esp_wifi_connect();
}

// Elimina las credenciales Wi-Fi guardadas y desconecta la interfaz STA si está conectada.
esp_err_t wifi_manager_clear_credentials(void)
{
    if (!s_initialized) 
        return ESP_ERR_INVALID_STATE;

    esp_err_t err = nvs_storage_clear();

    if (err != ESP_OK)
        return err;

    portENTER_CRITICAL(&s_status_mux);
    s_credentials_saved = false;
    s_sta_connected     = false;
    s_sta_ssid[0]       = '\0';
    s_ip_address[0]     = '\0';
    portEXIT_CRITICAL(&s_status_mux);

    esp_err_t dc_err = esp_wifi_disconnect();
    if (dc_err != ESP_OK && dc_err != ESP_ERR_WIFI_NOT_CONNECT)
        return dc_err;

    ESP_LOGI(TAG, "Credenciales Wi-Fi eliminadas");
    return ESP_OK;
}

// Llena la estructura de estado con la información actual del Wi-Fi.
void wifi_manager_get_status(wifi_manager_status_t *status)
{
    if (!status) 
        return;

    portENTER_CRITICAL(&s_status_mux);
    status->credentials_saved = s_credentials_saved;
    status->connected         = s_sta_connected;
    strlcpy(status->sta_ssid,   s_sta_ssid,   sizeof(status->sta_ssid));
    strlcpy(status->ip_address, s_ip_address, sizeof(status->ip_address));
    portEXIT_CRITICAL(&s_status_mux);
}

// Devuelve el SSID del AP para el portal de configuración.
const char *wifi_manager_get_ap_ssid(void)
{
    return WIFI_MANAGER_AP_SSID;
}

// Funciones de inicialización específicas para cada modo (AP y STA).
void wifi_init_softap(void) { ESP_ERROR_CHECK(wifi_manager_init()); }
void wifi_init_sta(void)    { ESP_ERROR_CHECK(wifi_manager_init()); }
