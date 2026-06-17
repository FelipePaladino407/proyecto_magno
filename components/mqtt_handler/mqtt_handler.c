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

static const char *DEVICE_ID = "LCD_01"; // por esto de conectar varias placas en simultaneo, cada una tiene un ID distinto para identificarlas en el broker
#define DEVICE_IS_LCD 1                  // 1 si esta placa es LCD, 0 si esta placa es camara

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

    cJSON *device_id = cJSON_GetObjectItem(json, "device_id");
    cJSON *id = cJSON_GetObjectItem(json, "id");
    cJSON *producto = cJSON_GetObjectItem(json, "producto");
    cJSON *stock = cJSON_GetObjectItem(json, "stock");
    cJSON *timestamp_json = cJSON_GetObjectItem(json, "timestamp");
    cJSON *estado_json = cJSON_GetObjectItem(json, "estado");

    if (cJSON_IsString(device_id) && strcmp(device_id->valuestring, DEVICE_ID) == 0) {
        ESP_LOGI(TAG, "Mensaje propio recibido, no se guarda en logger recibido");
        cJSON_Delete(json);
        return true;
    }

    if (cJSON_IsString(id) && cJSON_IsString(producto) && cJSON_IsNumber(stock) && cJSON_IsNumber(timestamp_json) && cJSON_IsString(estado_json)) {

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

        ESP_LOGI(TAG,
                 "Struct reconstruido -> ID: %s, Nombre: %s, Stock: %lu, Timestamp: %lld, Estado: %s",
                 producto_recibido.id,
                 producto_recibido.name,
                 (unsigned long)producto_recibido.stock,
                 (long long)timestamp,
                 estado);

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

        esp_mqtt_client_subscribe(client, "control/configuracion", 0);

#if DEVICE_IS_LCD
        esp_mqtt_client_subscribe(client, "sistema/escaner/evento", 0);
        esp_mqtt_client_subscribe(client, "sistema/escaner/error", 0);
#endif

        esp_mqtt_client_publish(client, "ESP_CONNECTED", DEVICE_ID, 0, 1, 0);
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

        int topic_len = event->topic_len;
        int data_len = event->data_len;

        if (topic_len >= sizeof(topic)) {
            topic_len = sizeof(topic) - 1;
        }

        if (data_len >= sizeof(mensaje)) {
            data_len = sizeof(mensaje) - 1;
        }

        memcpy(topic, event->topic, topic_len);
        topic[topic_len] = '\0';

        memcpy(mensaje, event->data, data_len);
        mensaje[data_len] = '\0';

        if (strcmp(topic, "sistema/escaner/evento") == 0 || strcmp(topic, "sistema/escaner/error") == 0) {
            procesar_json_recibido(mensaje);
        } else {
            ESP_LOGI(TAG, "Mensaje recibido en un topic que no es de productos, no se guarda en logger");
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
    // Configuracion basica apuntando a un broker publico de prueba
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = "mqtt://mqtt-dashboard.com:1883",
    };

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);

    if (client == NULL) {
        ESP_LOGE(TAG, "No se pudo crear el cliente MQTT");
        return;
    }

    // Manejador de eventos
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);

    // Arrancamos el cliente
    esp_mqtt_client_start(client);
}

static void publicar_evento(Product producto, const char *estado, const char *topic)
{
    time_t timestamp = 0;

    if (!get_timestamp(&timestamp)) { // Guarda la hora actual como time_t
        ESP_LOGW(TAG, "No se pudo obtener timestamp valido");
        return;
    }

    if (logger_local_mqtt != NULL) {
        logger_push(logger_local_mqtt, producto, timestamp, estado); // Lo mete al logger junto con la struct product que tiene el producto, su id, stock, timestamp y estado
    } else {
        ESP_LOGW(TAG, "No hay logger local configurado");
    }

    if (cliente_mqtt_global == NULL) {
        ESP_LOGW(TAG, "MQTT no conectado, no se publica pero ya quedo guardado en el logger");
        return; // para asegurar que estamos conectados
    }

    char payload[256];

    // Empaquetamos el struct en un string JSON con ID, Nombre, Stock, Timestamp y Estado
    snprintf(payload,
             sizeof(payload),
             "{\"device_id\":\"%s\",\"id\":\"%s\",\"producto\":\"%s\",\"stock\":%lu,\"timestamp\":%lld,\"estado\":\"%s\"}",
             DEVICE_ID,
             producto.id,
             producto.name,
             (unsigned long)producto.stock,
             (long long)timestamp,
             estado);

    // Publicamos el texto armado
    esp_mqtt_client_publish(cliente_mqtt_global, topic, payload, 0, 1, 0);

    ESP_LOGI(TAG, "Producto publicado: %s", payload);
}

void procesar_y_publicar_qr(Product producto_escaneado)
{
    publicar_evento(producto_escaneado, "OK", "sistema/escaner/evento");
}

void procesar_y_publicar_manual(Product producto_manual)
{
    publicar_evento(producto_manual, "MANUAL", "sistema/escaner/evento");
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

    publicar_evento(producto_error, "ERROR", "sistema/escaner/error");
}