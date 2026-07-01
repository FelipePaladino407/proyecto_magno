#include "mqtt_handler.h"

#include "device_role.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "mqtt_client.h"
#include "esp_log.h"

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

#include "cJSON.h"

static const char *TAG = "MQTT_APP";
extern QueueHandle_t fsm_event_queue;

static esp_mqtt_client_handle_t cliente_hivemq = NULL;

static Logger *logger_local_mqtt    = NULL;
static Logger *logger_recibido_mqtt = NULL;

#if DEVICE_IS_LCD
#define MQTT_SCAN_QUEUE_LENGTH 10
#define MQTT_SCAN_DISPATCH_DELAY_MS 200

static QueueHandle_t s_scan_queue = NULL;
static TaskHandle_t s_scan_dispatch_task_handle = NULL;

static void scan_dispatch_task(void *pvParameters)
{
    (void)pvParameters;

    Product scan;

    while (true) {
        if (xQueueReceive(s_scan_queue, &scan, portMAX_DELAY) != pdPASS) {
            continue;
        }

        bool delivered = false;

        while (!delivered) {
            if (fsm_get_current_state() == STATE_IDLE) {
                delivered = fsm_on_qr_detected(scan.id, scan.name);

                if (delivered) {
                    ESP_LOGI(TAG,
                             "Scan MQTT entregado a FSM -> ID=%s | Nombre=%s",
                             scan.id,
                             scan.name);
                    break;
                }
            }

            vTaskDelay(pdMS_TO_TICKS(MQTT_SCAN_DISPATCH_DELAY_MS));
        }
    }
}

static bool ensure_scan_dispatcher(void)
{
    if (s_scan_queue == NULL) {
        s_scan_queue = xQueueCreate(MQTT_SCAN_QUEUE_LENGTH, sizeof(Product));
        if (s_scan_queue == NULL) {
            ESP_LOGE(TAG, "No se pudo crear cola local de scans MQTT");
            return false;
        }
    }

    if (s_scan_dispatch_task_handle == NULL) {
        BaseType_t ok = xTaskCreate(&scan_dispatch_task,
                                    "MQTT_SCAN_DISPATCH",
                                    4096,
                                    NULL,
                                    1,
                                    &s_scan_dispatch_task_handle);
        if (ok != pdPASS) {
            ESP_LOGE(TAG, "No se pudo crear task MQTT_SCAN_DISPATCH");
            return false;
        }
    }

    return true;
}

static bool enqueue_scan_for_fsm(const Product *scan)
{
    if (scan == NULL || scan->id[0] == '\0') {
        return false;
    }

    if (!ensure_scan_dispatcher()) {
        return false;
    }

    if (xQueueSend(s_scan_queue, scan, pdMS_TO_TICKS(100)) != pdPASS) {
        ESP_LOGW(TAG,
                 "Cola local de scans llena. Se descarta scan -> ID=%s | Nombre=%s",
                 scan->id,
                 scan->name);
        return false;
    }

    ESP_LOGI(TAG, "Scan MQTT encolado para FSM -> ID=%s | Nombre=%s", scan->id, scan->name);
    return true;
}
#endif

///////////////////////////////
//Parametros de conexion MQTT//
///////////////////////////////
#if DEVICE_IS_LCD
static const char *DEVICE_ID = "LCD_01";
#else
static const char *DEVICE_ID = "CAM_01";
#endif

// HiveMQ publico — broker unico para todo
static const char *HIVEMQ_URI         = "mqtt://broker.hivemq.com:1883";
static const char *TOPIC_COMUNICACION = "ucuiot/magno/x7k2/scan";      // CAM -> LCD + ThingsBoard Integration
static const char *TOPIC_TELEMETRY_HV = "ucuiot/magno/x7k2/telemetry"; // LCD -> ThingsBoard Integration
static const char *TOPIC_LOGI         = "ucuiot/magno/x7k2/LOGI";

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

#if DEVICE_IS_LCD
/*
 * CAMBIO AGREGADO:
 * evita repetir xQueueSend y, sobre todo, evita crashear si la cola FSM aun no existe.
 * Se mantiene porque la FSM LCD sigue usando EV_MQTT_CONNECT_SUCCESS para hacer flush_pending.
 */
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
#endif

/*
 * CAMBIO AGREGADO:
 * pending_queue no acepta timestamp 0. Si NTP todavia no esta listo,
 * se usa time(NULL) y, como ultimo fallback, 1.
 */
static time_t timestamp_or_fallback(void)
{
    time_t timestamp = 0;

    if (!get_timestamp(&timestamp) || timestamp == 0) {
        timestamp = time(NULL);
    }

    if (timestamp == 0) {
        timestamp = 1;
    }

    return timestamp;
}

/*
 * CAMBIO AGREGADO:
 * La CAM publica scans hacia TOPIC_COMUNICACION.
 * La LCD publica eventos finales/fake demo hacia TOPIC_TELEMETRY_HV para el dashboard.
 */
 
static const char *topic_salida(void)
{
#if DEVICE_IS_LCD
    return TOPIC_TELEMETRY_HV;
#else
    return TOPIC_COMUNICACION;
#endif
}

/*
 * Formato plano original del equipo MQTT.
 * Se conserva para no romper la Integration/dashboard que esperaba campos:
 * device_id, id, producto, stock, timestamp y estado.
 */
static bool construir_payload_plano(char *payload,
                                    size_t payload_size,
                                    const char *device_id,
                                    Product producto,
                                    time_t timestamp,
                                    const char *estado)
{
    if (payload == NULL || payload_size == 0 || device_id == NULL || estado == NULL) {
        return false;
    }

    int written = snprintf(payload,
                           payload_size,
                           "{\"device_id\":\"%s\",\"id\":\"%s\",\"producto\":\"%s\","
                           "\"stock\":%lu,\"timestamp\":%lld,\"estado\":\"%s\"}",
                           device_id,
                           producto.id,
                           producto.name,
                           (unsigned long)producto.stock,
                           (long long)timestamp,
                           estado);

    return written > 0 && (size_t)written < payload_size;
}

/*
 * CAMBIO AGREGADO:
 * publica un evento ya timestamped. Lo usa la publicacion normal y tambien flush_pending.
 * Mantiene el JSON plano de ellos; no usa telemetry_formatter.
 */
static bool publicar_evento_con_timestamp(Product producto,
                                          const char *estado,
                                          const char *topic,
                                          time_t timestamp,
                                          bool guardar_logger_local)
{
    if (estado == NULL || topic == NULL || topic[0] == '\0' || timestamp == 0) {
        ESP_LOGW(TAG, "Evento MQTT invalido, no se publica");
        return false;
    }

    if (guardar_logger_local) {
        if (logger_local_mqtt != NULL) {
            logger_push(logger_local_mqtt, producto, timestamp, estado);
        } else {
            ESP_LOGW(TAG, "No hay logger local configurado");
        }
    }

    if (cliente_hivemq == NULL) {
        ESP_LOGW(TAG, "HiveMQ no conectado, no se publica ahora");
        return false;
    }

    char payload[256];
    if (!construir_payload_plano(payload, sizeof(payload), DEVICE_ID, producto, timestamp, estado)) {
        ESP_LOGW(TAG, "No se pudo construir payload MQTT");
        return false;
    }

    int msg_id = esp_mqtt_client_publish(cliente_hivemq, topic, payload, 0, 1, 0);
    if (msg_id < 0) {
        ESP_LOGW(TAG, "No se pudo publicar mensaje MQTT");
        return false;
    }

    ESP_LOGI(TAG, "Publicado en %s: %s", topic, payload);
    return true;
}

/*
 * CAMBIO AGREGADO:
 * funcion comun para guardar eventos offline.
 */
static bool guardar_pendiente(Product producto,
                              const char *estado,
                              const char *topic,
                              time_t timestamp)
{
    if (estado == NULL) {
        estado = "OK";
    }

    if (topic == NULL) {
        topic = TOPIC_TELEMETRY_HV;
    }

    if (timestamp == 0) {
        timestamp = timestamp_or_fallback();
    }

    bool ok = pending_queue_push(producto, timestamp, estado, topic);

    if (ok) {
        ESP_LOGI(TAG,
                 "Evento guardado pendiente -> topic=%s | id=%s | nombre=%s | stock=%lu | estado=%s",
                 topic,
                 producto.id,
                 producto.name,
                 (unsigned long)producto.stock,
                 estado);
    } else {
        ESP_LOGE(TAG, "No se pudo guardar evento pendiente");
    }

    return ok;
}

#if DEVICE_IS_LCD
static void publicar_catalogo_inicial(esp_mqtt_client_handle_t client)
{
    char payload[512];
    int i = 0;

    while (i < (int)CATALOGO_SIZE) {
        char values[400] = "";
        int len = 0;

        for (int j = 0; j < 10 && i < (int)CATALOGO_SIZE; j++, i++) {
            Product producto_db;
            uint32_t stock_actual = 0;

            // Consultar a la hash table si el producto ya existe mediante su ID
            if (product_db_find_by_id(catalogo_completo[i].id, &producto_db)) {
                stock_actual = producto_db.stock; // Si existe, levantamos su stock real
            }

            len += snprintf(values + len, sizeof(values) - len,
                            "%s\"%s\":%lu",
                            (j == 0) ? "" : ",",
                            catalogo_completo[i].nombre,
                            (unsigned long)stock_actual);
        }

        time_t ts = timestamp_or_fallback();

        snprintf(payload, sizeof(payload),
                 "{\"ts\":%lld,\"values\":{%s}}",
                 (long long)ts * 1000LL,
                 values);

        esp_mqtt_client_publish(client, TOPIC_TELEMETRY_HV, payload, 0, 1, 0);
        ESP_LOGI(TAG, "Catalogo inicial publicado [%d/%d]: %s",
                 i, (int)CATALOGO_SIZE, payload);

        vTaskDelay(pdMS_TO_TICKS(200));
    }
}
#endif

void mqtt_handler_set_loggers(Logger *logger_local, Logger *logger_recibido)
{
    logger_local_mqtt    = logger_local;
    logger_recibido_mqtt = logger_recibido;
}

/*
 * CAMBIO IMPORTANTE (combinado con tu fix de stock):
 * La LCD ya NO reenvia directo a ThingsBoard aca. En cambio, arma un
 * HistoryEntry y lo encola en fsm_mqtt_data_queue. La FSM lo recibe,
 * actualiza el stock real en la hash table, y recien ahi llama a
 * mqtt_reenviar_a_thingsboard() con el stock correcto.
 *
 * Esto reemplaza el reenvio directo que tenia el doc 4 (que mandaba el
 * stock del JSON recibido, sin pasar por la FSM).
 */
#if DEVICE_IS_LCD
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
        ESP_LOGI(TAG, "Mensaje propio recibido, ignorado");
        cJSON_Delete(json);
        return true;
    }

    if (cJSON_IsString(id) && cJSON_IsString(producto) && cJSON_IsNumber(stock) &&
        cJSON_IsNumber(timestamp_json) && cJSON_IsString(estado_json)) {

        safe_copy(producto_recibido.id, id->valuestring, sizeof(producto_recibido.id));
        safe_copy(producto_recibido.name, producto->valuestring, sizeof(producto_recibido.name));
        producto_recibido.stock = (uint32_t)stock->valuedouble;
        timestamp = (time_t)timestamp_json->valuedouble;
        safe_copy(estado, estado_json->valuestring, sizeof(estado));

        if (timestamp == 0) {
            timestamp = timestamp_or_fallback();
        }

        if (logger_recibido_mqtt != NULL) {
            logger_push(logger_recibido_mqtt, producto_recibido, timestamp, estado);
        } else {
            ESP_LOGW(TAG, "No hay logger recibido configurado");
        }

        ESP_LOGI(TAG, "Struct reconstruido -> ID: %s, Nombre: %s, Stock: %lu, Timestamp: %lld, Estado: %s",
                 producto_recibido.id, producto_recibido.name,
                 (unsigned long)producto_recibido.stock, (long long)timestamp, estado);

#if DEVICE_IS_LCD
        /*
         * La camara solo informa que producto escaneo.
         * El stock del JSON recibido no es fuente de verdad.
         * La FSM/product_db calcularan el stock real luego de la confirmacion.
         *
         * No llamamos directo a fsm_on_qr_detected() porque si llegan varios
         * scans juntos mientras la FSM esta ocupada se perderian. Primero se
         * encolan y una task los entrega cuando la FSM vuelve a IDLE.
         */
        if (!enqueue_scan_for_fsm(&producto_recibido)) {
            ESP_LOGW(TAG, "No se pudo encolar QR recibido por MQTT para la FSM");
        }
#endif

        cJSON_Delete(json);
        return true;
    }

    ESP_LOGE(TAG, "Error al procesar el formato del mensaje");
    cJSON_Delete(json);
    return false;
}
#endif

/*
 * Publica hacia ThingsBoard usando SIEMPRE el stock real almacenado en product_db.
 * La FSM llama a procesar_y_publicar() despues de actualizar stock; desde ahi se
 * arma el HistoryEntry y se entra a esta funcion.
 */
bool mqtt_reenviar_a_thingsboard(HistoryEntry entry)
{
    const char *estado = entry.state[0] != '\0' ? entry.state : "OK";

    if (entry.timestamp == 0) {
        entry.timestamp = timestamp_or_fallback();
    }

    Product producto_actual;
    memset(&producto_actual, 0, sizeof(producto_actual));

    if (!product_db_find_by_id(entry.product.id, &producto_actual)) {
        ESP_LOGW(TAG,
                 "No se pudo reenviar a ThingsBoard: producto no encontrado en product_db -> ID=%s",
                 entry.product.id);
        return false;
    }

    return publicar_evento_con_timestamp(producto_actual,
                                         estado,
                                         TOPIC_TELEMETRY_HV,
                                         entry.timestamp,
                                         true);
}
                              

// ─── Event handler HiveMQ ────────────────────────────────────────────────────

static void mqtt_hivemq_event_handler(void *handler_args, esp_event_base_t base,
                                      int32_t event_id, void *event_data)
{
    (void)handler_args;
    (void)base;

    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
#if DEVICE_IS_LCD
    EventType ev;
#endif

    switch ((esp_mqtt_event_id_t)event_id) {

    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "HiveMQ: conectado como %s", DEVICE_ID);
        cliente_hivemq = client;

        esp_mqtt_client_subscribe(client, "control/configuracion", 0);

#if DEVICE_IS_LCD
        esp_mqtt_client_subscribe(client, TOPIC_COMUNICACION, 0);
        ESP_LOGI(TAG, "LCD: suscrito a %s", TOPIC_COMUNICACION);
        ensure_scan_dispatcher();
        publicar_catalogo_inicial(client);

        ev = EV_MQTT_CONNECT_SUCCESS;
        post_fsm_event(ev); // la FSM LCD usa este evento para ejecutar mqtt_handler_flush_pending()
#else
        ESP_LOGI(TAG, "CAM: publicara en %s", TOPIC_COMUNICACION);
        mqtt_handler_flush_pending(); // la CAM no levanta FSM; reenvia scans pendientes directo
#endif
        esp_mqtt_client_publish(client, "ESP_CONNECTED", DEVICE_ID, 0, 1, 0);
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "HiveMQ: desconectado");
        cliente_hivemq = NULL;
#if DEVICE_IS_LCD
        ev = EV_MQTT_CONNECT_FAILURE;
        post_fsm_event(ev);
#endif
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
        /*
         * CAMBIO:
         * Antes se enviaba EV_MQTT_PUBLISH_SUCCESS a la FSM aca.
         * Se elimina ese envio porque la FSM actual decide exito/fallo por el return bool
         * de procesar_y_publicar(). Mandar otro evento aca puede generar duplicados.
         */
        ESP_LOGI(TAG, "HiveMQ: publicacion confirmada");
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "HiveMQ: error MQTT");
        if (event->error_handle != NULL) {
            ESP_LOGE(TAG, "Tipo: %d", event->error_handle->error_type);
            ESP_LOGE(TAG, "esp-tls: 0x%x", event->error_handle->esp_tls_last_esp_err);
            ESP_LOGE(TAG, "socket errno: %d", event->error_handle->esp_transport_sock_errno);

            if (event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
#if DEVICE_IS_LCD
                ev = EV_MQTT_CONNECT_FAILURE;
                post_fsm_event(ev);
#endif
            }
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
}

// ─── Publicacion ─────────────────────────────────────────────────────────────

static bool publicar_evento(Product producto, const char *estado)
{
    time_t timestamp = timestamp_or_fallback();
    const char *topic = topic_salida();

    return publicar_evento_con_timestamp(producto, estado, topic, timestamp, true);
}

bool procesar_y_publicar(Product producto)
{
#if DEVICE_IS_LCD
    HistoryEntry entry;
    memset(&entry, 0, sizeof(entry));

    entry.product = producto;
    entry.timestamp = timestamp_or_fallback();
    safe_copy(entry.state, "OK", sizeof(entry.state));

    return mqtt_reenviar_a_thingsboard(entry);
#else
    return publicar_evento(producto, "OK");
#endif
}

bool procesar_y_publicar_error(const char *mensaje_error)
{
    Product producto_error;
    memset(&producto_error, 0, sizeof(producto_error));

    safe_copy(producto_error.id, "ERROR", sizeof(producto_error.id));
    safe_copy(producto_error.name, mensaje_error != NULL ? mensaje_error : "Error desconocido", sizeof(producto_error.name));
    producto_error.stock = 0;

    const char *topic = topic_salida();

    if (publicar_evento(producto_error, "ERROR")) {
        return true;
    }

    /*
     * CAMBIO AGREGADO:
     * Los errores no siempre pasan por el callback store_pending de la FSM,
     * entonces se guardan aca si no se pudieron publicar.
     */
    return guardar_pendiente(producto_error, "ERROR", topic, 0);
}

/*
 * CAMBIO AGREGADO (fix de compilacion respecto a tu version):
 * Tu version tenia `bool procesar_y_publicar_LOGI(char *LOGI)` haciendo
 * esp_mqtt_client_publish(..., *LOGI, ...), lo cual desreferencia el
 * puntero (pasa un char en vez de char*) y no compila. Se corrige la firma
 * y se valida cliente_hivemq antes de publicar.
 */
bool procesar_y_publicar_LOGI(const char *LOGI)
{
    if (LOGI == NULL) {
        return false;
    }

    if (cliente_hivemq == NULL) {
        ESP_LOGW(TAG, "HiveMQ no conectado, no se publica LOGI");
        return false;
    }

    int msg_id = esp_mqtt_client_publish(cliente_hivemq, TOPIC_LOGI, LOGI, 0, 1, 0);
    return msg_id >= 0;
}

/*
 * Callback usado por la FSM cuando publicar un producto valido falla.
 *
 * CAMBIO DE LIMPIEZA:
 * Antes se llamaba mqtt_handler_store_pending_qr(...). Se renombro porque no hay una
 * cola distinta para QR/manual. Todo producto valido pendiente se guarda igual.
 */
bool mqtt_handler_store_pending(Product producto)
{
    return guardar_pendiente(producto, "OK", topic_salida(), 0);
}

/*
 * CAMBIO AGREGADO:
 * Reenvia la cola persistente FIFO cuando MQTT vuelve.
 * La FSM lo invoca al recibir EV_MQTT_CONNECT_SUCCESS.
 */
bool mqtt_handler_flush_pending(void)
{
    if (cliente_hivemq == NULL) {
        ESP_LOGW(TAG, "HiveMQ no conectado: no se puede reenviar cola pendiente");
        return false;
    }

    int count = pending_queue_count();
    if (count == 0) {
        ESP_LOGI(TAG, "No hay eventos pendientes para reenviar");
        return true;
    }

    ESP_LOGI(TAG, "Reenviando %d evento(s) pendiente(s)", count);

    while (pending_queue_count() > 0) {
        PendingMqttEvent ev_pendiente;
        memset(&ev_pendiente, 0, sizeof(ev_pendiente));

        if (!pending_queue_peek(&ev_pendiente)) {
            ESP_LOGW(TAG, "No se pudo leer el proximo evento pendiente");
            return false;
        }

        if (!publicar_evento_con_timestamp(ev_pendiente.product,
                                           ev_pendiente.state,
                                           ev_pendiente.topic,
                                           ev_pendiente.timestamp,
                                           false)) {
            ESP_LOGW(TAG, "No se pudo publicar el pendiente mas viejo. Se corta flush");
            return false;
        }

        if (!pending_queue_pop()) {
            ESP_LOGW(TAG, "Publicado pendiente, pero no se pudo quitar de NVS");
            return false;
        }
    }

    ESP_LOGI(TAG, "Todos los eventos pendientes fueron reenviados");
    return true;
}
