#include "mqtt_client.h"
#include "esp_log.h"

#include "mqtt_handler.h"
#include "shared_types.h"
#include "logger.h"
#include "ntp_handler.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>

#include "cJSON.h"

static const char *TAG = "MQTT_APP";

static esp_mqtt_client_handle_t cliente_mqtt_global = NULL;

static Logger *logger_local_mqtt = NULL;
static Logger *logger_recibido_mqtt = NULL;

///////////////////////////////
//Parametros de conexión MQTT//
//////////////////////////////
static const char *DEVICE_ID = "LCD_01";
static const char *DEVICE_TOKEN = "M53eRKClWvrZIWVSQRVL";
static const char *topic_select_comunication = "v1/sdevice/me/telemetry";
static const char *topic_select_telemetry = "v1/sdevice/me/telemetry";
#define DEVICE_IS_LCD 1

static const char *catalogo[] = {
    "Leche Entera 1L",      "Leche Descremada 1L",  "Yogur Natural 200g",
    "Yogur Frutilla 200g",  "Queso Mozzarella 400g","Queso Colonia 300g",
    "Manteca 200g",         "Crema de Leche 200ml", "Huevos x12",
    "Pan de Molde 500g",    "Pan Lactal Integral 400g","Facturas x6",
    "Arroz Largo Fino 1kg", "Fideos Spaghetti 500g","Fideos Mono 500g",
    "Harina 0000 1kg",      "Azucar Blanca 1kg",    "Aceite Girasol 900ml",
    "Aceite de Oliva 500ml","Sal Fina 500g",         "Lentejas 500g",
    "Garbanzos 500g",       "Porotos Negros 500g",  "Tomate en Lata 400g",
    "Atun al Natural 170g", "Atun en Aceite 170g",  "Arvejas en Lata 300g",
    "Choclo en Lata 300g",  "Mermelada Frutilla 390g","Dulce de Leche 400g",
    "Cafe Molido 250g",     "Te Negro x20",          "Agua Mineral 1.5L",
    "Jugo de Naranja 1L",   "Gaseosa Cola 2L",       "Gaseosa Naranja 2L",
    "Cerveza Lata 473ml",   "Vino Tinto 750ml",      "Papas Fritas 150g",
    "Galletitas Dulces 200g","Galletitas Saladas 150g","Chocolate 100g",
    "Mayonesa 500g",        "Ketchup 400g",          "Mostaza 200g",
    "Jabon en Polvo 1kg",   "Lavandina 1L",          "Detergente 750ml",
    "Papel Higienico x4",   "Shampoo 400ml",
};
#define CATALOGO_SIZE (sizeof(catalogo) / sizeof(catalogo[0]))

static void publicar_catalogo_inicial(esp_mqtt_client_handle_t client)
{
    char payload[512];
    // ThingsBoard acepta múltiples keys en un solo mensaje
    // {"ts": ms, "values": {"Leche Entera 1L": 0, "Yogur Natural 200g": 0, ...}}
    // Pero 50 productos en 512 bytes no entra, mandamos de a 10

    int i = 0;
    while (i < (int)CATALOGO_SIZE) {
        char values[400] = "";
        int len = 0;

        for (int j = 0; j < 10 && i < (int)CATALOGO_SIZE; j++, i++) {
            len += snprintf(values + len, sizeof(values) - len,
                            "%s\"%s\":0",
                            (j == 0) ? "" : ",",
                            catalogo[i]);
        }

        time_t ts = 0;
        get_timestamp(&ts);

        snprintf(payload, sizeof(payload),
                 "{\"ts\":%lld,\"values\":{%s}}",
                 (long long)ts * 1000LL,
                 values);

        esp_mqtt_client_publish(client, topic_select, payload, 0, 1, 0);
        ESP_LOGI(TAG, "Catalogo inicial publicado [%d/%d]: %s", 
                 i, (int)CATALOGO_SIZE, payload);

        vTaskDelay(pdMS_TO_TICKS(200)); // pequeña pausa entre batches
    }
}

void mqtt_handler_set_loggers(Logger *logger_local, Logger *logger_recibido)
{
    logger_local_mqtt = logger_local;
    logger_recibido_mqtt = logger_recibido;
}

static bool procesar_json_recibido(const char *mensaje)
{
    Product producto_recibido;
    memset(&producto_recibido, 0, sizeof(producto_recibido));

    time_t timestamp = 0;
    char estado[16] = "";

    cJSON *json = cJSON_Parse(mensaje);

    if (json == NULL) {
        ESP_LOGE(TAG, "Error al procesar el formato del mensaje");
        return false;
    }

    cJSON *device_id    = cJSON_GetObjectItem(json, "device_id");
    cJSON *id           = cJSON_GetObjectItem(json, "id");
    cJSON *producto     = cJSON_GetObjectItem(json, "producto");
    cJSON *stock        = cJSON_GetObjectItem(json, "stock");
    cJSON *timestamp_json = cJSON_GetObjectItem(json, "timestamp");
    cJSON *estado_json  = cJSON_GetObjectItem(json, "estado");

    if (cJSON_IsString(device_id) && strcmp(device_id->valuestring, DEVICE_ID) == 0) {
        ESP_LOGI(TAG, "Mensaje propio recibido, no se guarda en logger recibido");
        cJSON_Delete(json);
        return true;
    }

    if (cJSON_IsString(id) && cJSON_IsString(producto) && cJSON_IsNumber(stock) &&
        cJSON_IsNumber(timestamp_json) && cJSON_IsString(estado_json)) {

        strncpy(producto_recibido.id, id->valuestring, sizeof(producto_recibido.id) - 1);
        producto_recibido.id[sizeof(producto_recibido.id) - 1] = '\0';

        strncpy(producto_recibido.name, producto->valuestring, sizeof(producto_recibido.name) - 1);
        producto_recibido.name[sizeof(producto_recibido.name) - 1] = '\0';

        producto_recibido.stock = (uint32_t)stock->valuedouble;
        timestamp = (time_t)timestamp_json->valuedouble;

        strncpy(estado, estado_json->valuestring, sizeof(estado) - 1);
        estado[sizeof(estado) - 1] = '\0';

        if (logger_recibido_mqtt != NULL) {
            logger_push(logger_recibido_mqtt, producto_recibido, timestamp, estado);
        } else {
            ESP_LOGW(TAG, "No hay logger recibido configurado");
        }

        ESP_LOGI(TAG, "Struct reconstruido -> ID: %s, Nombre: %s, Stock: %lu, Timestamp: %lld, Estado: %s",
                 producto_recibido.id, producto_recibido.name,
                 (unsigned long)producto_recibido.stock, (long long)timestamp, estado);

        cJSON_Delete(json);
        return true;
    }

    ESP_LOGE(TAG, "Error al procesar el formato del mensaje");
    cJSON_Delete(json);
    return false;
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;

    switch ((esp_mqtt_event_id_t)event_id) {

    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "Conectado al broker MQTT!");
        cliente_mqtt_global = client;

        
        esp_mqtt_client_subscribe(client, "v1/devices/me/telemetry", 0);
        esp_mqtt_client_publish(client, "ESP_CONNECTED", DEVICE_ID, 0, 1, 0);

        publicar_catalogo_inicial(client); 
        
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "Desconectado del broker MQTT");
        cliente_mqtt_global = NULL;
        break;

    case MQTT_EVENT_DATA: {
        ESP_LOGI(TAG, "Producto recibido en el tema: %.*s", event->topic_len, event->topic);
        ESP_LOGI(TAG, "Datos: %.*s", event->data_len, event->data);

        char topic[128];
        char mensaje[256];

        int topic_len = event->topic_len < (int)sizeof(topic) - 1 ? event->topic_len : (int)sizeof(topic) - 1;
        int data_len  = event->data_len  < (int)sizeof(mensaje) - 1 ? event->data_len  : (int)sizeof(mensaje) - 1;

        memcpy(topic, event->topic, topic_len);
        topic[topic_len] = '\0';

        memcpy(mensaje, event->data, data_len);
        mensaje[data_len] = '\0';

        if (strcmp(topic, topic_select) == 0) {
            procesar_json_recibido(mensaje);
        } else {
            ESP_LOGI(TAG, "Topic desconocido, mensaje ignorado");
        }

        break;
    }

    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "Ha ocurrido un error en MQTT");
        if (event->error_handle != NULL) {
            ESP_LOGE(TAG, "Tipo de error MQTT: %d", event->error_handle->error_type);
            ESP_LOGE(TAG, "Error esp-tls: 0x%x", event->error_handle->esp_tls_last_esp_err);
            ESP_LOGE(TAG, "Error socket errno: %d", event->error_handle->esp_transport_sock_errno);
        }
        break;

    default:
        break;
    }
}

void iniciar_mqtt(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = "mqtt://mqtt.thingsboard.cloud:1883",
        .credentials.username = DEVICE_TOKEN
    };

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);

    if (client == NULL) {
        ESP_LOGE(TAG, "No se pudo crear el cliente MQTT");
        return;
    }

    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);
}

static void publicar_evento(Product producto, const char *estado)
{
    time_t timestamp = 0;

    if (!get_timestamp(&timestamp)) {
        ESP_LOGW(TAG, "No se pudo obtener timestamp valido");
        return;
    }

    if (logger_local_mqtt != NULL) {
        logger_push(logger_local_mqtt, producto, timestamp, estado);
    } else {
        ESP_LOGW(TAG, "No hay logger local configurado");
    }

    if (cliente_mqtt_global == NULL) {
        ESP_LOGW(TAG, "MQTT no conectado, no se publica pero ya quedo guardado en el logger");
        return;
    }

    char payload[256];

        snprintf(payload, sizeof(payload),
            "{\"ts\":%lld,\"values\":{\"%s\":%lu}}",
            (long long)timestamp * 1000LL,   // ThingsBoard usa millisegundos
            producto.name,                    // key dinámica = nombre del producto
            (unsigned long)producto.stock);   // value = stock

    esp_mqtt_client_publish(cliente_mqtt_global, topic_select, payload, 0, 1, 0);
    ESP_LOGI(TAG, "Producto publicado: %s", payload);

    // Attribute separado: último producto escaneado (legible en dashboard)
    char attr_payload[256];
    snprintf(attr_payload, sizeof(attr_payload),
             "{\"ultimo_id\":\"%s\",\"ultimo_nombre\":\"%s\",\"ultimo_estado\":\"%s\"}",
             producto.id,
             producto.name,
             estado);

    esp_mqtt_client_publish(cliente_mqtt_global, "v1/devices/me/attributes", attr_payload, 0, 1, 0);
    ESP_LOGI(TAG, "Attribute publicado: %s", attr_payload);
}

void procesar_y_publicar_qr(Product producto_escaneado)
{
    publicar_evento(producto_escaneado, "OK");
}

void procesar_y_publicar_error(const char *mensaje_error)
{
    Product producto_error;
    memset(&producto_error, 0, sizeof(producto_error));

    strncpy(producto_error.id, "ERROR", sizeof(producto_error.id) - 1);
    producto_error.id[sizeof(producto_error.id) - 1] = '\0';

    strncpy(producto_error.name, mensaje_error, sizeof(producto_error.name) - 1);
    producto_error.name[sizeof(producto_error.name) - 1] = '\0';

    producto_error.stock = 0;

    publicar_evento(producto_error, "ERROR");
}