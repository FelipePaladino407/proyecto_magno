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
#include "telemetry_formatter.h"

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

/* Broker unico para comunicacion CAM->LCD y telemetria leida por ThingsBoard Integration. */
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

static const char *topic_for_outgoing_state(void)
{
#if DEVICE_IS_LCD
    return TOPIC_TELEMETRY_HV;
#else
    return TOPIC_COMUNICACION;
#endif
}

static bool build_payload_for_topic(char *payload,
                                    size_t payload_size,
                                    const char *topic,
                                    Product product,
                                    const char *state,
                                    time_t timestamp)
{
    if (topic == NULL) {
        return false;
    }

    if (strcmp(topic, TOPIC_TELEMETRY_HV) == 0) {
        return telemetry_build_telemetry_payload(product, state, timestamp, payload, payload_size);
    }

    return telemetry_build_scan_payload(DEVICE_ID, product, state, timestamp, payload, payload_size);
}

static bool publish_with_timestamp(Product product,
                                   const char *state,
                                   const char *topic,
                                   time_t timestamp,
                                   bool save_in_local_logger)
{
    if (state == NULL || topic == NULL || topic[0] == '\0' || timestamp == 0) {
        ESP_LOGW(TAG, "No se puede publicar: evento invalido");
        return false;
    }

    if (cliente_hivemq == NULL) {
        ESP_LOGW(TAG, "HiveMQ no conectado, no se publica ahora");
        return false;
    }

    char payload[768];
    if (!build_payload_for_topic(payload, sizeof(payload), topic, product, state, timestamp)) {
        ESP_LOGW(TAG, "No se pudo construir payload MQTT");
        return false;
    }

    int msg_id = esp_mqtt_client_publish(cliente_hivemq, topic, payload, 0, 1, 0);
    if (msg_id < 0) {
        ESP_LOGW(TAG, "No se pudo publicar mensaje MQTT");
        return false;
    }

    if (save_in_local_logger) {
        if (logger_local_mqtt != NULL) {
            logger_push(logger_local_mqtt, product, timestamp, state);
        } else {
            ESP_LOGW(TAG, "No hay logger local configurado");
        }
    }

    ESP_LOGI(TAG, "Publicado en %s: %s", topic, payload);
    return true;
}

static bool publish_now(Product product, const char *state, const char *topic)
{
    time_t timestamp = timestamp_or_fallback();
    return publish_with_timestamp(product, state, topic, timestamp, true);
}

static bool store_pending(Product product, const char *state, const char *topic)
{
    time_t timestamp = timestamp_or_fallback();
    return pending_queue_push(product, timestamp, state, topic);
}

static bool parse_scan_payload(const char *message,
                               Product *out_product,
                               time_t *out_timestamp,
                               char *out_state,
                               size_t state_size)
{
    if (message == NULL || out_product == NULL || out_timestamp == NULL ||
        out_state == NULL || state_size == 0) {
        return false;
    }

    memset(out_product, 0, sizeof(*out_product));
    *out_timestamp = 0;
    out_state[0] = '\0';

    cJSON *json = cJSON_Parse(message);
    if (json == NULL) {
        ESP_LOGE(TAG, "Error al procesar el formato del mensaje");
        return false;
    }

    cJSON *device_id      = cJSON_GetObjectItem(json, "device_id");
    cJSON *id             = cJSON_GetObjectItem(json, "id");
    cJSON *product_name   = cJSON_GetObjectItem(json, "producto");
    cJSON *stock          = cJSON_GetObjectItem(json, "stock");
    cJSON *timestamp_json = cJSON_GetObjectItem(json, "timestamp");
    cJSON *state_json     = cJSON_GetObjectItem(json, "estado");

    if (cJSON_IsString(device_id) && strcmp(device_id->valuestring, DEVICE_ID) == 0) {
        ESP_LOGI(TAG, "Mensaje propio recibido, ignorado");
        cJSON_Delete(json);
        return true;
    }

    if (!(cJSON_IsString(id) && cJSON_IsString(product_name) && cJSON_IsNumber(stock) &&
          cJSON_IsNumber(timestamp_json) && cJSON_IsString(state_json))) {
        ESP_LOGE(TAG, "Error al procesar el formato del mensaje");
        cJSON_Delete(json);
        return false;
    }

    safe_copy(out_product->id, id->valuestring, sizeof(out_product->id));
    safe_copy(out_product->name, product_name->valuestring, sizeof(out_product->name));
    out_product->stock = (uint32_t)stock->valuedouble;
    *out_timestamp = (time_t)timestamp_json->valuedouble;
    safe_copy(out_state, state_json->valuestring, state_size);

    cJSON_Delete(json);
    return true;
}

static bool procesar_json_recibido(const char *message)
{
    Product received_product;
    time_t timestamp = 0;
    char state[16];

    if (!parse_scan_payload(message, &received_product, &timestamp, state, sizeof(state))) {
        return false;
    }

    if (received_product.id[0] == '\0') {
        return true;
    }

    if (logger_recibido_mqtt != NULL) {
        logger_push(logger_recibido_mqtt, received_product, timestamp, state);
    } else {
        ESP_LOGW(TAG, "No hay logger recibido configurado");
    }

    ESP_LOGI(TAG,
             "Struct reconstruido -> ID:%s | Nombre:%s | Stock:%lu | Timestamp:%lld | Estado:%s",
             received_product.id,
             received_product.name,
             (unsigned long)received_product.stock,
             (long long)timestamp,
             state);

#if DEVICE_IS_LCD
    /* En LCD, el scan de CAM entra a la FSM. La FSM valida catalogo, pregunta cantidad,
       actualiza product_db y recien despues publica telemetria a ThingsBoard. */
    if (strcmp(state, "ERROR") == 0) {
        fsm_on_qr_invalid(received_product.name[0] != '\0' ? received_product.name : "Error recibido por MQTT");
    } else {
        fsm_on_qr_detected(received_product.id, received_product.name);
    }
#endif

    return true;
}

static void publicar_catalogo_inicial(esp_mqtt_client_handle_t client)
{
    if (client == NULL) {
        return;
    }

    size_t i = 0;
    const size_t catalog_size = telemetry_catalog_size();

    while (i < catalog_size) {
        char payload[768];
        size_t next_index = i;
        time_t timestamp = timestamp_or_fallback();

        if (!telemetry_build_catalog_batch_payload(i,
                                                   10,
                                                   timestamp,
                                                   payload,
                                                   sizeof(payload),
                                                   &next_index)) {
            ESP_LOGW(TAG, "No se pudo construir payload de catalogo inicial");
            break;
        }

        esp_mqtt_client_publish(client, TOPIC_TELEMETRY_HV, payload, 0, 1, 0);
        ESP_LOGI(TAG, "Catalogo inicial publicado [%u/%u]: %s",
                 (unsigned)next_index,
                 (unsigned)catalog_size,
                 payload);

        i = next_index;
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
        char message[512];

        int topic_len = event->topic_len < (int)sizeof(topic) - 1 ? event->topic_len : (int)sizeof(topic) - 1;
        int data_len  = event->data_len  < (int)sizeof(message) - 1 ? event->data_len : (int)sizeof(message) - 1;

        memcpy(topic, event->topic, topic_len);
        topic[topic_len] = '\0';

        memcpy(message, event->data, data_len);
        message[data_len] = '\0';

        ESP_LOGI(TAG, "HiveMQ mensaje en topic: %s", topic);
        ESP_LOGI(TAG, "HiveMQ datos: %s", message);

#if DEVICE_IS_LCD
        if (strcmp(topic, TOPIC_COMUNICACION) == 0) {
            procesar_json_recibido(message);
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
    return publish_now(producto_escaneado, "OK", topic_for_outgoing_state());
}

bool procesar_y_publicar_manual(Product producto_manual)
{
    return publish_now(producto_manual, "MANUAL", topic_for_outgoing_state());
}

bool procesar_y_publicar_error(const char *mensaje_error)
{
    Product error_product;
    memset(&error_product, 0, sizeof(error_product));

    safe_copy(error_product.id, "ERROR", sizeof(error_product.id));
    safe_copy(error_product.name, mensaje_error != NULL ? mensaje_error : "Error desconocido", sizeof(error_product.name));
    error_product.stock = 0;

    const char *topic = topic_for_outgoing_state();

    if (publish_now(error_product, "ERROR", topic)) {
        return true;
    }

    /* Los errores no pasan por save_to_local_buffer de la FSM, por eso se guardan aca. */
    return store_pending(error_product, "ERROR", topic);
}

bool mqtt_handler_store_pending_qr(Product producto_escaneado)
{
    return store_pending(producto_escaneado, "OK", topic_for_outgoing_state());
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

