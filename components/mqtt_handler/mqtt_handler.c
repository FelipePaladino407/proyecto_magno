#include "mqtt_client.h"
#include "esp_log.h"
#include "mqtt_handler.h"
#include "shared_types.h"
#include "logger.h"
#include "ntp_handler.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "cJSON.h"

static const char *TAG = "MQTT_APP";
static esp_mqtt_client_handle_t cliente_mqtt_global = NULL;

static const char *DEVICE_ID = "ESP32_01";//por esto de conectar varias placas en simultaneo, cada una tiene un ID distinto para identificarlas en el broker

static bool procesar_json_recibido(const char *mensaje)
{
    Product producto_recibido;
    memset(&producto_recibido, 0, sizeof(producto_recibido));

    char fecha_hora[32] = "";
    char estado[16] = "";

    cJSON *json = cJSON_Parse(mensaje);

    if (json == NULL) {
        ESP_LOGE(TAG, "Error al procesar el formato del mensaje");
        return false;
    }

    cJSON *id = cJSON_GetObjectItem(json, "id");
    cJSON *producto = cJSON_GetObjectItem(json, "producto");
    cJSON *stock = cJSON_GetObjectItem(json, "stock");
    cJSON *fecha = cJSON_GetObjectItem(json, "fecha_hora");
    cJSON *estado_json = cJSON_GetObjectItem(json, "estado");

    if (cJSON_IsString(id) && cJSON_IsString(producto) && cJSON_IsNumber(stock) && cJSON_IsString(fecha) && cJSON_IsString(estado_json)) {

        strncpy(producto_recibido.id, id->valuestring, sizeof(producto_recibido.id) - 1);
        producto_recibido.id[sizeof(producto_recibido.id) - 1] = '\0';

        strncpy(producto_recibido.name, producto->valuestring, sizeof(producto_recibido.name) - 1);
        producto_recibido.name[sizeof(producto_recibido.name) - 1] = '\0';

        producto_recibido.stock = (uint32_t)stock->valuedouble;

        strncpy(fecha_hora, fecha->valuestring, sizeof(fecha_hora) - 1);
        fecha_hora[sizeof(fecha_hora) - 1] = '\0';

        strncpy(estado, estado_json->valuestring, sizeof(estado) - 1);
        estado[sizeof(estado) - 1] = '\0';

        logger_push(producto_recibido, fecha_hora, estado);

        ESP_LOGI(TAG, "Struct reconstruido -> ID: %s, Nombre: %s, Stock: %lu, Estado: %s",
                 producto_recibido.id,
                 producto_recibido.name,
                 (unsigned long)producto_recibido.stock,
                 estado);

        cJSON_Delete(json);
        return true;
    }

    ESP_LOGE(TAG, "Error al procesar el formato del mensaje");

    cJSON_Delete(json);
    return false;
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "¡Conectado al broker MQTT!");
            cliente_mqtt_global = client;
            esp_mqtt_client_subscribe(client, "control/configuracion", 0);
            esp_mqtt_client_subscribe(client, "sistema/escaner/evento", 0);
            esp_mqtt_client_subscribe(client, "sistema/escaner/error", 0);
            esp_mqtt_client_publish(client, "ESP_CONNECTED", "1", 0, 1, 0);
            break;

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "Desconectado del broker MQTT");
            cliente_mqtt_global = NULL;
            break;

        case MQTT_EVENT_DATA:
        {
            ESP_LOGI(TAG, "Producto recibido en el tema: %.*s", event->topic_len, event->topic);
            ESP_LOGI(TAG, "Datos: %.*s", event->data_len, event->data);

            char mensaje[256];
            snprintf(mensaje, sizeof(mensaje), "%.*s", event->data_len, event->data);

            procesar_json_recibido(mensaje);

            break;
        }

        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "Ha ocurrido un error en MQTT");
            break;

        default:
            break;
    }
}

void iniciar_mqtt(void) {
    // Configuración básica apuntando a un broker público de prueba
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = "mqtts://1cff5a3e91744d43b7af27dc81364b6d.s1.eu.hivemq.cloud:8883", 
    };

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    
    // Manejador de eventos
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    
    // Arrancamos el cliente
    esp_mqtt_client_start(client);
}


static void publicar_evento(Product producto, const char *estado, const char *topic)
{
    char fecha_hora[32];

    get_fecha_hora(fecha_hora, sizeof(fecha_hora)); // guarda la fecha y hora en fecha_hora

    logger_push(producto, fecha_hora, estado); // lo mete al logger junto con la struc product que tiene el producto, su id, stock y estado

    if (cliente_mqtt_global == NULL) return; // Asegurar que estamos conectados

    char payload[256];

    // Empaquetamos el struct en un string JSON con ID, Nombre, Stock, FechaHora y Estado
    snprintf(payload,
             sizeof(payload),
             "{\"device_id\":\"%s\",\"id\":\"%s\",\"producto\":\"%s\",\"stock\":%lu,\"fecha_hora\":\"%s\",\"estado\":\"%s\"}",
             DEVICE_ID,
             producto.id,
             producto.name,
             (unsigned long)producto.stock,
             fecha_hora,
             estado);

    // Publicamos el texto armado
    esp_mqtt_client_publish(cliente_mqtt_global, topic, payload, 0, 1, 0);
    ESP_LOGI(TAG, "Producto publicado: %s", payload);
}


void procesar_y_publicar_qr(Product producto_escaneado) {
    publicar_evento(producto_escaneado, "OK", "sistema/escaner/evento");
}

void procesar_y_publicar_manual(Product producto_manual) {
    publicar_evento(producto_manual, "MANUAL", "sistema/escaner/evento");
}

void procesar_y_publicar_error(const char *mensaje_error) {
    Product producto_error;
    memset(&producto_error, 0, sizeof(producto_error));

    strncpy(producto_error.id, "ERROR", sizeof(producto_error.id) - 1);
    producto_error.id[sizeof(producto_error.id) - 1] = '\0';

    strncpy(producto_error.name, mensaje_error, sizeof(producto_error.name) - 1);
    producto_error.name[sizeof(producto_error.name) - 1] = '\0';

    producto_error.stock = 0;

    publicar_evento(producto_error, "ERROR", "sistema/escaner/error");
}
