#include "mqtt_client.h"
#include "esp_log.h"

#include "fsm.h"
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
extern QueueHandle_t fsm_event_queue;

static esp_mqtt_client_handle_t cliente_hivemq      = NULL;
static esp_mqtt_client_handle_t cliente_thingsboard = NULL;

static Logger *logger_local_mqtt    = NULL;
static Logger *logger_recibido_mqtt = NULL;

///////////////////////////////
//Parametros de conexión MQTT//
///////////////////////////////
static const char *DEVICE_ID = "CAM_01";

// HiveMQ Cloud — comunicación entre placas
static const char *HIVEMQ_URI         = "mqtt://broker.hivemq.com:1883";
static const char *TOPIC_COMUNICACION = "ucuiot/magno/x7k2/scan";

// ThingsBoard Cloud — solo LCD publica aquí
static const char *TB_URI           = "mqtt://mqtt.thingsboard.cloud:1883";
static const char *TB_TOKEN         = "M53eRKClWvrZIWVSQRVL";
static const char *TOPIC_TELEMETRY  = "v1/devices/me/telemetry";
static const char *TOPIC_ATTRIBUTES = "v1/devices/me/attributes";

#define DEVICE_IS_LCD 0

static const char *catalogo[] = {
    "Leche Entera 1L",        "Leche Descremada 1L",     "Yogur Natural 200g",
    "Yogur Frutilla 200g",    "Queso Mozzarella 400g",   "Queso Colonia 300g",
    "Manteca 200g",           "Crema de Leche 200ml",    "Huevos x12",
    "Pan de Molde 500g",      "Pan Lactal Integral 400g","Facturas x6",
    "Arroz Largo Fino 1kg",   "Fideos Spaghetti 500g",   "Fideos Mono 500g",
    "Harina 0000 1kg",        "Azucar Blanca 1kg",       "Aceite Girasol 900ml",
    "Aceite de Oliva 500ml",  "Sal Fina 500g",           "Lentejas 500g",
    "Garbanzos 500g",         "Porotos Negros 500g",     "Tomate en Lata 400g",
    "Atun al Natural 170g",   "Atun en Aceite 170g",     "Arvejas en Lata 300g",
    "Choclo en Lata 300g",    "Mermelada Frutilla 390g", "Dulce de Leche 400g",
    "Cafe Molido 250g",       "Te Negro x20",            "Agua Mineral 1.5L",
    "Jugo de Naranja 1L",     "Gaseosa Cola 2L",         "Gaseosa Naranja 2L",
    "Cerveza Lata 473ml",     "Vino Tinto 750ml",        "Papas Fritas 150g",
    "Galletitas Dulces 200g", "Galletitas Saladas 150g", "Chocolate 100g",
    "Mayonesa 500g",          "Ketchup 400g",            "Mostaza 200g",
    "Jabon en Polvo 1kg",     "Lavandina 1L",            "Detergente 750ml",
    "Papel Higienico x4",     "Shampoo 400ml",
};
#define CATALOGO_SIZE (sizeof(catalogo) / sizeof(catalogo[0]))

static void publicar_catalogo_inicial(esp_mqtt_client_handle_t client)
{
    char payload[512];
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

        esp_mqtt_client_publish(client, TOPIC_COMUNICACION, payload, 0, 1, 0);
        ESP_LOGI(TAG, "Catalogo inicial publicado [%d/%d]: %s",
                 i, (int)CATALOGO_SIZE, payload);

        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

void mqtt_handler_set_loggers(Logger *logger_local, Logger *logger_recibido)
{
    logger_local_mqtt    = logger_local;
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

    cJSON *device_id      = cJSON_GetObjectItem(json, "device_id");
    cJSON *id             = cJSON_GetObjectItem(json, "id");
    cJSON *producto       = cJSON_GetObjectItem(json, "producto");
    cJSON *stock          = cJSON_GetObjectItem(json, "stock");
    cJSON *timestamp_json = cJSON_GetObjectItem(json, "timestamp");
    cJSON *estado_json    = cJSON_GetObjectItem(json, "estado");

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

#if DEVICE_IS_LCD
        if (cliente_thingsboard != NULL) {
            char payload_tb[256];
            snprintf(payload_tb, sizeof(payload_tb),
                     "{\"ts\":%lld,\"values\":{\"%s\":%lu}}",
                     (long long)timestamp * 1000LL,
                     producto_recibido.name,
                     (unsigned long)producto_recibido.stock);
            esp_mqtt_client_publish(cliente_thingsboard, TOPIC_TELEMETRY, payload_tb, 0, 1, 0);

            char attr_payload[256];
            snprintf(attr_payload, sizeof(attr_payload),
                     "{\"ultimo_id\":\"%s\",\"ultimo_nombre\":\"%s\",\"ultimo_estado\":\"%s\"}",
                     producto_recibido.id, producto_recibido.name, estado);
            esp_mqtt_client_publish(cliente_thingsboard, TOPIC_ATTRIBUTES, attr_payload, 0, 1, 0);
        } else {
            ESP_LOGW(TAG, "ThingsBoard no conectado, no se reenvio");
        }
#endif

        cJSON_Delete(json);
        return true;
    }

    ESP_LOGE(TAG, "Error al procesar el formato del mensaje");
    cJSON_Delete(json);
    return false;
}

// ─── Event handler HiveMQ ────────────────────────────────────────────────────

static void mqtt_hivemq_event_handler(void *handler_args, esp_event_base_t base,
                                      int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    EventType ev;

    switch ((esp_mqtt_event_id_t)event_id) {

    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "HiveMQ: conectado!");
        cliente_hivemq = client;

        esp_mqtt_client_subscribe(client, "control/configuracion", 0);

#if DEVICE_IS_LCD
        esp_mqtt_client_subscribe(client, TOPIC_COMUNICACION, 0);
        ESP_LOGI(TAG, "LCD: suscrito a %s", TOPIC_COMUNICACION);
        publicar_catalogo_inicial(client);
#else
        ESP_LOGI(TAG, "CAM: publicara en %s", TOPIC_COMUNICACION);
#endif
        ev = EV_MQTT_CONNECT_SUCCESS;
        xQueueSend(fsm_event_queue, &ev, pdMS_TO_TICKS(10));
        esp_mqtt_client_publish(client, "ESP_CONNECTED", DEVICE_ID, 0, 1, 0);
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "HiveMQ: desconectado");
        cliente_hivemq = NULL;
        ev = EV_MQTT_CONNECT_FAILURE;
        xQueueSend(fsm_event_queue, &ev, pdMS_TO_TICKS(10));
        break;

    case MQTT_EVENT_DATA: {
        char topic[128];
        char mensaje[256];

        int topic_len = event->topic_len < (int)sizeof(topic) - 1   ? event->topic_len  : (int)sizeof(topic) - 1;
        int data_len  = event->data_len  < (int)sizeof(mensaje) - 1 ? event->data_len   : (int)sizeof(mensaje) - 1;

        memcpy(topic,   event->topic, topic_len);  topic[topic_len]   = '\0';
        memcpy(mensaje, event->data,  data_len);   mensaje[data_len]  = '\0';

        ESP_LOGI(TAG, "HiveMQ mensaje en topic: %s", topic);
        ESP_LOGI(TAG, "HiveMQ datos: %s", mensaje);

#if DEVICE_IS_LCD
        if (strcmp(topic, TOPIC_COMUNICACION) == 0) {
            procesar_json_recibido(mensaje);
        } else {
            ESP_LOGI(TAG, "Topic desconocido, ignorado");
        }
#else
        ESP_LOGW(TAG, "CAM recibio mensaje inesperado en topic: %s", topic);
#endif
        break;
    }

    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "HiveMQ: publicacion confirmada");
        ev = EV_MQTT_PUBLISH_SUCCESS;
        xQueueSend(fsm_event_queue, &ev, pdMS_TO_TICKS(10));
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "HiveMQ: error MQTT");
        if (event->error_handle != NULL) {
            ESP_LOGE(TAG, "Tipo: %d", event->error_handle->error_type);
            ESP_LOGE(TAG, "esp-tls: 0x%x", event->error_handle->esp_tls_last_esp_err);
            ESP_LOGE(TAG, "socket errno: %d", event->error_handle->esp_transport_sock_errno);
        }
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
            ev = EV_MQTT_CONNECT_FAILURE;
            xQueueSend(fsm_event_queue, &ev, pdMS_TO_TICKS(10));
        }
        break;

    default:
        break;
    }
}

// ─── Event handler ThingsBoard ───────────────────────────────────────────────

static void mqtt_thingsboard_event_handler(void *handler_args, esp_event_base_t base,
                                           int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;

    switch ((esp_mqtt_event_id_t)event_id) {

    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "ThingsBoard: conectado!");
        cliente_thingsboard = event->client;
        esp_mqtt_client_publish(event->client, "ESP_CONNECTED", DEVICE_ID, 0, 1, 0);
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "ThingsBoard: desconectado");
        cliente_thingsboard = NULL;
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "ThingsBoard: error MQTT");
        if (event->error_handle != NULL) {
            ESP_LOGE(TAG, "Tipo: %d", event->error_handle->error_type);
            ESP_LOGE(TAG, "esp-tls: 0x%x", event->error_handle->esp_tls_last_esp_err);
            ESP_LOGE(TAG, "socket errno: %d", event->error_handle->esp_transport_sock_errno);
        }
        break;

    default:
        break;
    }
}

// ─── Inicio MQTT ─────────────────────────────────────────────────────────────

void iniciar_mqtt(void)
{
    esp_mqtt_client_config_t hivemq_cfg = {
    .broker.address.uri = HIVEMQ_URI,
    };

    esp_mqtt_client_handle_t hive_client = esp_mqtt_client_init(&hivemq_cfg);
    if (hive_client == NULL) {
        ESP_LOGE(TAG, "No se pudo crear cliente HiveMQ");
        return;
    }
    esp_mqtt_client_register_event(hive_client, ESP_EVENT_ANY_ID, mqtt_hivemq_event_handler, NULL);
    esp_mqtt_client_start(hive_client);

#if DEVICE_IS_LCD
    esp_mqtt_client_config_t tb_cfg = {
        .broker.address.uri   = TB_URI,
        .credentials.username = TB_TOKEN,
    };

    esp_mqtt_client_handle_t tb_client = esp_mqtt_client_init(&tb_cfg);
    if (tb_client == NULL) {
        ESP_LOGE(TAG, "No se pudo crear cliente ThingsBoard");
        return;
    }
    esp_mqtt_client_register_event(tb_client, ESP_EVENT_ANY_ID, mqtt_thingsboard_event_handler, NULL);
    esp_mqtt_client_start(tb_client);
#endif
}

// ─── Publicación ─────────────────────────────────────────────────────────────

static bool publicar_evento(Product producto, const char *estado)
{
    time_t timestamp = 0;

    if (!get_timestamp(&timestamp)) {
        ESP_LOGW(TAG, "No se pudo obtener timestamp valido");
        return false;
    }

    if (logger_local_mqtt != NULL) {
        logger_push(logger_local_mqtt, producto, timestamp, estado);
    } else {
        ESP_LOGW(TAG, "No hay logger local configurado");
    }

#if DEVICE_IS_LCD
    if (cliente_thingsboard == NULL) {
        ESP_LOGW(TAG, "ThingsBoard no conectado, quedo guardado en logger");
        return false;
    }

    char payload[256];
    snprintf(payload, sizeof(payload),
             "{\"ts\":%lld,\"values\":{\"%s\":%lu}}",
             (long long)timestamp * 1000LL,
             producto.name,
             (unsigned long)producto.stock);

    esp_mqtt_client_publish(cliente_thingsboard, TOPIC_TELEMETRY, payload, 0, 1, 0);
    ESP_LOGI(TAG, "LCD telemetria publicada: %s", payload);

    char attr_payload[256];
    snprintf(attr_payload, sizeof(attr_payload),
             "{\"ultimo_id\":\"%s\",\"ultimo_nombre\":\"%s\",\"ultimo_estado\":\"%s\"}",
             producto.id, producto.name, estado);

    esp_mqtt_client_publish(cliente_thingsboard, TOPIC_ATTRIBUTES, attr_payload, 0, 1, 0);
    ESP_LOGI(TAG, "LCD attribute publicado: %s", attr_payload);

#else
    if (cliente_hivemq == NULL) {
        ESP_LOGW(TAG, "HiveMQ no conectado, quedo guardado en logger");
        return false;
    }

    char payload[256];
    snprintf(payload, sizeof(payload),
             "{\"device_id\":\"%s\",\"id\":\"%s\",\"producto\":\"%s\","
             "\"stock\":%lu,\"timestamp\":%lld,\"estado\":\"%s\"}",
             DEVICE_ID,
             producto.id,
             producto.name,
             (unsigned long)producto.stock,
             (long long)timestamp,
             estado);

    int msg_id = esp_mqtt_client_publish(cliente_hivemq, TOPIC_COMUNICACION, payload, 0, 1, 0);
    if (msg_id < 0) {
        ESP_LOGW(TAG, "No se pudo publicar mensaje MQTT");
        return false;
    }
    ESP_LOGI(TAG, "CAM scan publicado en HiveMQ: %s", payload);
#endif

    return true;
}

bool procesar_y_publicar_qr(Product producto_escaneado)
{
    return publicar_evento(producto_escaneado, "OK");
}

bool procesar_y_publicar_error(const char *mensaje_error)
{
    Product producto_error;
    memset(&producto_error, 0, sizeof(producto_error));

    strncpy(producto_error.id, "ERROR", sizeof(producto_error.id) - 1);
    producto_error.id[sizeof(producto_error.id) - 1] = '\0';

    strncpy(producto_error.name, mensaje_error, sizeof(producto_error.name) - 1);
    producto_error.name[sizeof(producto_error.name) - 1] = '\0';

    producto_error.stock = 0;

    return publicar_evento(producto_error, "ERROR");
}