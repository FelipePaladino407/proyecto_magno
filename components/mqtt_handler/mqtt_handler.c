#include "mqtt_handler.h"

#include "cJSON.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "mqtt_client.h"

#include "fsm.h"
#include "logger.h"
#include "ntp_handler.h"
#include "pending_queue.h"
#include "product_db.h"
#include "shared_types.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static const char *TAG = "MQTT_APP";

extern QueueHandle_t fsm_event_queue;

#ifndef DEVICE_IS_LCD
#define DEVICE_IS_LCD 1
#endif

#if DEVICE_IS_LCD
static const char *DEVICE_ID = "LCD_01";
#else
static const char *DEVICE_ID = "CAM_01";
#endif

/* Broker unico para comunicacion CAM->LCD y para el topic que lee la integration de ThingsBoard. */
static const char *HIVEMQ_URI         = "mqtt://broker.hivemq.com:1883";
static const char *TOPIC_COMUNICACION = "ucuiot/magno/x7k2/scan";
static const char *TOPIC_TELEMETRY_HV = "ucuiot/magno/x7k2/telemetry";

static esp_mqtt_client_handle_t cliente_hivemq = NULL;

static Logger *logger_local_mqtt    = NULL;
static Logger *logger_recibido_mqtt = NULL;

static void safe_copy(char *dest, const char *src, size_t dest_size)
{
    if (dest == NULL || dest_size == 0) {
        return;
    }

    if (src == NULL) {
        dest[0] = '\0';
        return;
    }

    strncpy(dest, src, dest_size - 1);
    dest[dest_size - 1] = '\0';
}

static void post_fsm_event(EventType ev)
{
    if (fsm_event_queue == NULL) {
        ESP_LOGW(TAG, "fsm_event_queue no inicializada. Evento MQTT perdido: %d", ev);
        return;
    }

    if (xQueueSend(fsm_event_queue, &ev, pdMS_TO_TICKS(10)) != pdPASS) {
        ESP_LOGW(TAG, "No se pudo enviar evento MQTT a FSM: %d", ev);
    }
}

static time_t timestamp_or_fallback(void)
{
    time_t timestamp = 0;

    if (!get_timestamp(&timestamp) || timestamp == 0) {
        timestamp = time(NULL);
    }

    /* La cola pendiente no acepta timestamp 0. Si aun no hay NTP, dejamos un marcador minimo. */
    if (timestamp == 0) {
        timestamp = 1;
    }

    return timestamp;
}

static const char *topic_for_outgoing_state(const char *estado)
{
#if DEVICE_IS_LCD
    (void)estado;
    return TOPIC_TELEMETRY_HV;
#else
    (void)estado;
    return TOPIC_COMUNICACION;
#endif
}

static bool build_scan_payload(char *payload,
                               size_t payload_size,
                               Product producto,
                               const char *estado,
                               time_t timestamp)
{
    if (payload == NULL || payload_size == 0 || estado == NULL) {
        return false;
    }

    int written = snprintf(payload,
                           payload_size,
                           "{\"device_id\":\"%s\",\"id\":\"%s\",\"producto\":\"%s\"," 
                           "\"stock\":%lu,\"timestamp\":%lld,\"estado\":\"%s\"}",
                           DEVICE_ID,
                           producto.id,
                           producto.name,
                           (unsigned long)producto.stock,
                           (long long)timestamp,
                           estado);

    return written > 0 && (size_t)written < payload_size;
}

static bool build_telemetry_payload(char *payload,
                                    size_t payload_size,
                                    Product producto,
                                    const char *estado,
                                    time_t timestamp)
{
    if (payload == NULL || payload_size == 0 || estado == NULL) {
        return false;
    }

    int written;

    if (strcmp(estado, "ERROR") == 0) {
        written = snprintf(payload,
                           payload_size,
                           "{\"ts\":%lld,\"values\":{\"last_error\":\"%s\",\"last_estado\":\"ERROR\"}}",
                           (long long)timestamp * 1000LL,
                           producto.name);
    } else {
        written = snprintf(payload,
                           payload_size,
                           "{\"ts\":%lld,\"values\":{\"%s\":%lu,\"last_product_id\":\"%s\"," 
                           "\"last_product_name\":\"%s\",\"last_estado\":\"%s\"}}",
                           (long long)timestamp * 1000LL,
                           producto.name,
                           (unsigned long)producto.stock,
                           producto.id,
                           producto.name,
                           estado);
    }

    return written > 0 && (size_t)written < payload_size;
}

static bool build_payload_for_topic(char *payload,
                                    size_t payload_size,
                                    const char *topic,
                                    Product producto,
                                    const char *estado,
                                    time_t timestamp)
{
    if (strcmp(topic, TOPIC_TELEMETRY_HV) == 0) {
        return build_telemetry_payload(payload, payload_size, producto, estado, timestamp);
    }

    return build_scan_payload(payload, payload_size, producto, estado, timestamp);
}

static bool publish_with_timestamp(Product producto,
                                   const char *estado,
                                   const char *topic,
                                   time_t timestamp,
                                   bool guardar_en_logger_local)
{
    if (estado == NULL || topic == NULL || topic[0] == '\0' || timestamp == 0) {
        ESP_LOGW(TAG, "No se puede publicar: evento invalido");
        return false;
    }

    if (cliente_hivemq == NULL) {
        ESP_LOGW(TAG, "HiveMQ no conectado, no se publica ahora");
        return false;
    }

    char payload[512];
    if (!build_payload_for_topic(payload, sizeof(payload), topic, producto, estado, timestamp)) {
        ESP_LOGW(TAG, "No se pudo construir payload MQTT");
        return false;
    }

    int msg_id = esp_mqtt_client_publish(cliente_hivemq, topic, payload, 0, 1, 0);
    if (msg_id < 0) {
        ESP_LOGW(TAG, "No se pudo publicar mensaje MQTT");
        return false;
    }

    if (guardar_en_logger_local) {
        if (logger_local_mqtt != NULL) {
            logger_push(logger_local_mqtt, producto, timestamp, estado);
        } else {
            ESP_LOGW(TAG, "No hay logger local configurado");
        }
    }

    ESP_LOGI(TAG, "Publicado en %s: %s", topic, payload);
    return true;
}

static bool publish_now(Product producto, const char *estado, const char *topic)
{
    time_t timestamp = timestamp_or_fallback();
    return publish_with_timestamp(producto, estado, topic, timestamp, true);
}

static bool store_pending(Product producto, const char *estado, const char *topic)
{
    time_t timestamp = timestamp_or_fallback();
    return pending_queue_push(producto, timestamp, estado, topic);
}

static bool parse_scan_payload(const char *mensaje, Product *out_product, time_t *out_timestamp, char *out_estado, size_t estado_size)
{
    if (mensaje == NULL || out_product == NULL || out_timestamp == NULL || out_estado == NULL || estado_size == 0) {
        return false;
    }

    memset(out_product, 0, sizeof(*out_product));
    *out_timestamp = 0;
    out_estado[0] = '\0';

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
        ESP_LOGI(TAG, "Mensaje propio recibido, ignorado");
        cJSON_Delete(json);
        return true;
    }

    if (!(cJSON_IsString(id) && cJSON_IsString(producto) && cJSON_IsNumber(stock) &&
          cJSON_IsNumber(timestamp_json) && cJSON_IsString(estado_json))) {
        ESP_LOGE(TAG, "Error al procesar el formato del mensaje");
        cJSON_Delete(json);
        return false;
    }

    safe_copy(out_product->id, id->valuestring, sizeof(out_product->id));
    safe_copy(out_product->name, producto->valuestring, sizeof(out_product->name));
    out_product->stock = (uint32_t)stock->valuedouble;
    *out_timestamp = (time_t)timestamp_json->valuedouble;
    safe_copy(out_estado, estado_json->valuestring, estado_size);

    cJSON_Delete(json);
    return true;
}

static bool procesar_json_recibido(const char *mensaje)
{
    Product producto_recibido;
    time_t timestamp = 0;
    char estado[16];

    if (!parse_scan_payload(mensaje, &producto_recibido, &timestamp, estado, sizeof(estado))) {
        return false;
    }

    if (producto_recibido.id[0] == '\0') {
        return true;
    }

    if (logger_recibido_mqtt != NULL) {
        logger_push(logger_recibido_mqtt, producto_recibido, timestamp, estado);
    } else {
        ESP_LOGW(TAG, "No hay logger recibido configurado");
    }

    ESP_LOGI(TAG,
             "Struct reconstruido -> ID:%s | Nombre:%s | Stock:%lu | Timestamp:%lld | Estado:%s",
             producto_recibido.id,
             producto_recibido.name,
             (unsigned long)producto_recibido.stock,
             (long long)timestamp,
             estado);

#if DEVICE_IS_LCD
    /* En LCD el mensaje de CAM entra a la FSM. No se modifica stock ni se reenvia a TB aqui.
       La FSM pregunta por LCD/touch, actualiza product_db y recien despues publica telemetria. */
    if (strcmp(estado, "ERROR") == 0) {
        fsm_on_qr_invalid(producto_recibido.name[0] != '\0' ? producto_recibido.name : "Error recibido por MQTT");
    } else {
        fsm_on_qr_detected(producto_recibido.id, producto_recibido.name);
    }
#endif

    return true;
}

static void publicar_catalogo_inicial(esp_mqtt_client_handle_t client)
{
    if (client == NULL) {
        return;
    }

    int i = 0;

    while (i < CATALOGO_SIZE) {
        char values[640] = "";
        int len = 0;

        for (int j = 0; j < 10 && i < CATALOGO_SIZE; j++, i++) {
            Product producto_db;
            uint32_t stock_actual = 0;

            if (product_db_find_by_id(catalogo_completo[i].id, &producto_db)) {
                stock_actual = producto_db.stock;
            }

            int written = snprintf(values + len,
                                   sizeof(values) - (size_t)len,
                                   "%s\"%s\":%lu",
                                   (j == 0) ? "" : ",",
                                   catalogo_completo[i].nombre,
                                   (unsigned long)stock_actual);
            if (written < 0 || written >= (int)(sizeof(values) - (size_t)len)) {
                ESP_LOGW(TAG, "Payload de catalogo inicial truncado");
                break;
            }
            len += written;
        }

        time_t ts = timestamp_or_fallback();
        char payload[768];
        snprintf(payload, sizeof(payload),
                 "{\"ts\":%lld,\"values\":{%s}}",
                 (long long)ts * 1000LL,
                 values);

        esp_mqtt_client_publish(client, TOPIC_TELEMETRY_HV, payload, 0, 1, 0);
        ESP_LOGI(TAG, "Catalogo inicial publicado [%d/%d]: %s", i, CATALOGO_SIZE, payload);

        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

void mqtt_handler_set_loggers(Logger *logger_local, Logger *logger_recibido)
{
    logger_local_mqtt    = logger_local;
    logger_recibido_mqtt = logger_recibido;
}

static void mqtt_hivemq_event_handler(void *handler_args,
                                      esp_event_base_t base,
                                      int32_t event_id,
                                      void *event_data)
{
    (void)handler_args;
    (void)base;

    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "HiveMQ: conectado como %s", DEVICE_ID);
        cliente_hivemq = client;

        esp_mqtt_client_subscribe(client, "control/configuracion", 0);

#if DEVICE_IS_LCD
        esp_mqtt_client_subscribe(client, TOPIC_COMUNICACION, 0);
        ESP_LOGI(TAG, "LCD: suscrito a %s", TOPIC_COMUNICACION);
        publicar_catalogo_inicial(client);
#else
        ESP_LOGI(TAG, "CAM: publicara scans en %s", TOPIC_COMUNICACION);
#endif

        post_fsm_event(EV_MQTT_CONNECT_SUCCESS);
        esp_mqtt_client_publish(client, "ESP_CONNECTED", DEVICE_ID, 0, 1, 0);
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "HiveMQ: desconectado");
        cliente_hivemq = NULL;
        post_fsm_event(EV_MQTT_CONNECT_FAILURE);
        break;

    case MQTT_EVENT_DATA: {
        char topic[128];
        char mensaje[512];

        int topic_len = event->topic_len < (int)sizeof(topic) - 1 ? event->topic_len : (int)sizeof(topic) - 1;
        int data_len  = event->data_len  < (int)sizeof(mensaje) - 1 ? event->data_len : (int)sizeof(mensaje) - 1;

        memcpy(topic, event->topic, topic_len);
        topic[topic_len] = '\0';

        memcpy(mensaje, event->data, data_len);
        mensaje[data_len] = '\0';

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
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "HiveMQ: error MQTT");
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
}

bool procesar_y_publicar_qr(Product producto_escaneado)
{
    return publish_now(producto_escaneado, "OK", topic_for_outgoing_state("OK"));
}

bool procesar_y_publicar_manual(Product producto_manual)
{
    return publish_now(producto_manual, "MANUAL", topic_for_outgoing_state("MANUAL"));
}

bool procesar_y_publicar_error(const char *mensaje_error)
{
    Product producto_error;
    memset(&producto_error, 0, sizeof(producto_error));

    safe_copy(producto_error.id, "ERROR", sizeof(producto_error.id));
    safe_copy(producto_error.name, mensaje_error != NULL ? mensaje_error : "Error desconocido", sizeof(producto_error.name));
    producto_error.stock = 0;

    const char *topic = topic_for_outgoing_state("ERROR");

    if (publish_now(producto_error, "ERROR", topic)) {
        return true;
    }

    /* Los errores no pasan por save_to_local_buffer de la FSM, por eso se guardan aca. */
    return store_pending(producto_error, "ERROR", topic);
}

bool mqtt_handler_store_pending_qr(Product producto_escaneado)
{
    return store_pending(producto_escaneado, "OK", topic_for_outgoing_state("OK"));
}

bool mqtt_handler_flush_pending(void)
{
    if (cliente_hivemq == NULL) {
        ESP_LOGW(TAG, "HiveMQ no conectado: no se puede reenviar cola pendiente");
        return false;
    }

    int initial_count = pending_queue_count();
    if (initial_count == 0) {
        ESP_LOGI(TAG, "No hay eventos pendientes para reenviar");
        return true;
    }

    ESP_LOGI(TAG, "Reenviando %d evento(s) pendiente(s) desde NVS", initial_count);

    while (pending_queue_count() > 0) {
        PendingMqttEvent pending_event;
        memset(&pending_event, 0, sizeof(pending_event));

        if (!pending_queue_peek(&pending_event)) {
            ESP_LOGW(TAG, "No se pudo leer el proximo evento pendiente");
            return false;
        }

        if (!publish_with_timestamp(pending_event.product,
                                    pending_event.state,
                                    pending_event.topic,
                                    pending_event.timestamp,
                                    true)) {
            ESP_LOGW(TAG, "Se corta reenvio: no se pudo publicar el pendiente mas viejo");
            return false;
        }

        if (!pending_queue_pop()) {
            ESP_LOGW(TAG, "Publicado pendiente pero no se pudo quitar de NVS");
            return false;
        }
    }

    ESP_LOGI(TAG, "Todos los eventos pendientes fueron reenviados");
    return true;
}

