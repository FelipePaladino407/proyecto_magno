#include "mqtt_handler.h"

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
extern QueueHandle_t fsm_event_queue;    //Cola utilizada para enviar eventos a la FSM. Se define en fsm.c
QueueHandle_t fsm_mqtt_data_queue;

static esp_mqtt_client_handle_t cliente_hivemq = NULL;  //Cola de traspaso de eventos MQTT. Para enviar stock total.

static Logger *logger_local_mqtt    = NULL;   //Logger local para guardar los Productos que se publican.
static Logger *logger_recibido_mqtt = NULL;  //Logger local para guardar los Productos que se reciben desde HiveMQ.

bool procesar_y_publicar_LOGI(const char *LOGI);
///////////////////////////////
//Parametros de conexion MQTT//
///////////////////////////////

/*
 * CAMBIO RESPECTO AL ARCHIVO ORIGINAL DEL EQUIPO MQTT:
 * antes DEVICE_ID y DEVICE_IS_LCD quedaban hardcodeados a mano dentro del .c.
 * Ahora se dejan configurables desde CMake/menu de compilacion.
 * Si no se define nada, este proyecto compila como LCD para sostener la demo actual.
 *
 * Para compilar CAM:
 *   target_compile_definitions(${COMPONENT_LIB} PRIVATE DEVICE_IS_LCD=0)
 *
 * Para compilar LCD:
 *   target_compile_definitions(${COMPONENT_LIB} PRIVATE DEVICE_IS_LCD=1)
 */
#ifndef DEVICE_IS_LCD     //Booleano para definir si el dispositivo es LCD o CAM.
#define DEVICE_IS_LCD 1
#endif

#if DEVICE_IS_LCD
static const char *DEVICE_ID = "LCD_01";
#else
static const char *DEVICE_ID = "CAM_01";    //Definicion del DIVICE_ID en funcion ddel del dispotivivo(CAMBIAR SI SE USAN MULTIPLES CAM)
#endif

// HiveMQ publico — broker unico para todo
static const char *HIVEMQ_URI         = "mqtt://broker.hivemq.com:1883";
static const char *TOPIC_COMUNICACION = "ucuiot/magno/x7k2/scan";      // CAM -> LCD + ThingsBoard Integration
static const char *TOPIC_TELEMETRY_HV = "ucuiot/magno/x7k2/telemetry"; // LCD -> ThingsBoard Integration
static const char *TOPIC_LOGI         = "ucuiot/magno/x7k2/LOGI";

static void safe_copy(char *dest, const char *src, size_t dest_size)//como strncpy pero evita crashear si src es NULL y asegura terminacion nula sin tener que hacerlo a cada rato el "/0"
{
    if (dest == NULL || dest_size == 0) { //Se asegura de que el puntero de destino sea válido y que el tamaño del búfer disponible sea mayor a cero.
        return;
    }

    if (src == NULL) {   //Aseguirse de que el puntero de origen no sea nulo. Si es nulo, se establece la cadena de destino como una cadena vacía.
        dest[0] = '\0';
        return;
    }

    strncpy(dest, src, dest_size - 1);
    dest[dest_size - 1] = '\0';
}

/*
 * CAMBIO AGREGADO:
 * evita repetir xQueueSend y, sobre todo, evita crashear si la cola FSM aun no existe.
 * Se mantiene porque la FSM sigue usando EV_MQTT_CONNECT_SUCCESS para hacer flush_pending (cola de pendientes).
 */
static void post_fsm_event(EventType ev)//FSM tiene una cola de eventos a dodne mandar las cosas que pasan
{
    if (fsm_event_queue == NULL) { //Si la cola de eventos de la FSM no está inicializada, se registra una advertencia y se retorna sin enviar el evento.
        ESP_LOGW(TAG, "fsm_event_queue no inicializada. Evento MQTT perdido: %d", ev);
        procesar_y_publicar_LOGI("WARN: cola FSM no inicializada, evento MQTT perdido");
        return;
    }

    if (xQueueSend(fsm_event_queue, &ev, pdMS_TO_TICKS(10)) != pdPASS) { //Si no se pudo enviar el evento a la cola de eventos de la FSM, se registra una advertencia.
        ESP_LOGW(TAG, "No se pudo enviar evento MQTT a FSM: %d", ev);
        procesar_y_publicar_LOGI("WARN: no se pudo enviar evento MQTT a FSM");
    }
}

/*
 * CAMBIO AGREGADO:
 * pending_queue no acepta timestamp 0. Si NTP todavia no esta listo,
 * se usa time(NULL) y, como ultimo fallback, 1.
 */
static time_t timestamp_or_fallback(void)
{
    time_t timestamp = 0;

    if (!get_timestamp(&timestamp) || timestamp == 0) {
        timestamp = time(NULL);//guarda en timestamp la hora actual en formato time_t 
    }

    if (timestamp == 0) { //Si sigue sin ser posible obtener un timestamp válido, se devuelve 1 como último recurso.
        timestamp = 1;
    }

    return timestamp;
}

/*
 * CAMBIO AGREGADO:
 * La CAM publica scans hacia TOPIC_COMUNICACION.
 * La LCD publica eventos finales/fake demo hacia TOPIC_TELEMETRY_HV para el dashboard.
 */
 
static const char *topic_salida(void)    //Devuelve el topic de salida según el tipo de dispositivo (LCD o CAM).
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
static bool construir_payload_plano(char *payload,   //Ubicacion en donde se guarda el JSON
                                    size_t payload_size,
                                    const char *device_id,
                                    Product producto,
                                    time_t timestamp,
                                    const char *estado)
{
    if (payload == NULL || payload_size == 0 || device_id == NULL || estado == NULL) {   //Se asegura de que los punteros de entrada sean válidos y que el tamaño del búfer sea mayor a cero.
        return false;
    }

    int written = snprintf(payload, //snprintf devuelve un int de cuantos caracteres escribio 
                           payload_size, //Respeta estrictamente el límite
                           "{\"device_id\":\"%s\",\"id\":\"%s\",\"producto\":\"%s\","
                           "\"stock\":%lu,\"timestamp\":%lld,\"estado\":\"%s\"}",
                           device_id,
                           producto.id,
                           producto.name,
                           (unsigned long)producto.stock,
                           (long long)timestamp,
                           estado);

    return written > 0 && (size_t)written < payload_size;//verifica que escribio algo y que no se paso del tamaño del buffer
}//devuelve true si esas dos cosas se cumplen 

/*
 * CAMBIO AGREGADO:
 * publica un evento ya timestamped. Lo usa la publicacion normal y tambien flush_pending.
 * Mantiene el JSON plano de ellos; no usa telemetry_formatter.
 */
static bool publicar_evento_con_timestamp(Product producto,    //Funcion usada para publicar el evento en MQTT
                                          const char *estado,
                                          const char *topic,
                                          time_t timestamp,
                                          bool guardar_logger_local)
{
    if (estado == NULL || topic == NULL || topic[0] == '\0' || timestamp == 0) {    //se asegura de que los punteros de entrada sean válidos y que el topic no esté vacío y que el timestamp sea válido.
        ESP_LOGW(TAG, "Evento MQTT invalido, no se publica");
        procesar_y_publicar_LOGI("WARN: evento MQTT invalido, no se publica");
        return false;
    }

    if (guardar_logger_local) {                                           //Antes de publicar, guarda el evento en el logger local si está configurado. Esto permite mantener un registro de los eventos publicados.
        if (logger_local_mqtt != NULL) {                                 //Permite giardar en el logger_local si esta configurdo.
            logger_push(logger_local_mqtt, producto, timestamp, estado);
        } else {
            ESP_LOGW(TAG, "No hay logger local configurado");
            procesar_y_publicar_LOGI("WARN: no hay logger local configurado");
        }
    }

    if (cliente_hivemq == NULL) {                                   //Se asegura que el cliente de hiveMQ este conectado.
        ESP_LOGW(TAG, "HiveMQ no conectado, no se publica ahora");
        procesar_y_publicar_LOGI("WARN: HiveMQ no conectado, no se publica ahora");
        return false;
    }

    char payload[256];
    if (!construir_payload_plano(payload, sizeof(payload), DEVICE_ID, producto, timestamp, estado)) {  //Asegira que la funcion de playload se crea adecuadamente.
        ESP_LOGW(TAG, "No se pudo construir payload MQTT");
        procesar_y_publicar_LOGI("WARN: no se pudo construir payload MQTT");
        return false;
    }

    int msg_id = esp_mqtt_client_publish(cliente_hivemq, topic, payload, 0, 1, 0);     //Publica el evento en el topic especificado.Segun la placa 
    if (msg_id < 0) {     //Devuelve el id del mensaje si el envio fue ecxitoso pero si no fue exitoso devuelve un valor negativo.
        ESP_LOGW(TAG, "No se pudo publicar mensaje MQTT");
        procesar_y_publicar_LOGI("WARN: no se pudo publicar mensaje MQTT");
        return false;
    }

    ESP_LOGI(TAG, "Publicado en %s: %s", topic, payload);
    procesar_y_publicar_LOGI("INFO: evento publicado por MQTT");
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

    if (timestamp == 0) {                           //Aseguramos de que el timestamp sea válido. Si no lo es, se obtiene un timestamp de respaldo.
        timestamp = timestamp_or_fallback();
    }

    bool ok = pending_queue_push(producto, timestamp, estado, topic);     //Pusheamos en el pending_queue el evento pendiente. Esto permite almacenar eventos que no se pudieron publicar en MQTT para reenviarlos más tarde.

    if (ok) {
        ESP_LOGI(TAG,
                 "Evento guardado pendiente -> topic=%s | id=%s | nombre=%s | stock=%lu | estado=%s",
                 topic,
                 producto.id,
                 producto.name,
                 (unsigned long)producto.stock,
                 estado);
        procesar_y_publicar_LOGI("INFO: evento guardado como pendiente");
    } else {
        ESP_LOGE(TAG, "No se pudo guardar evento pendiente");
        procesar_y_publicar_LOGI("ERROR: no se pudo guardar evento pendiente");
    }

    return ok;
}

//Funcion auxiliar implementada para publicar el caralogo inicial en el ThingsBoard. 
//Se publica en bloques de 10 productos para no saturar el broker y se espera 200ms entre cada bloque.
static void publicar_catalogo_inicial(esp_mqtt_client_handle_t client)
{
    char payload[512];                          //Definimos un buffer para almacenar el payload que se enviará a ThingsBoard.
    int i = 0;                                  //Variable de control para iterar sobre el catálogo completo de productos.

    while (i < (int)CATALOGO_SIZE) {           //Mientras queden productos en el catálogo, se construye un bloque de hasta 10 productos y se publica en el topic de telemetría de HiveMQ.
        char values[400] = "";
        int len = 0;

        for (int j = 0; j < 10 && i < (int)CATALOGO_SIZE; j++, i++) { //   Contrulle el mensaje en JSON a enviar(10 productos)
            Product producto_db;
            uint32_t stock_actual = 0;

            // Consultar a la hash table si el producto ya existe mediante su ID
            if (product_db_find_by_id(catalogo_completo[i].id, &producto_db)) { //Catga el stock real del producto si exste.
                stock_actual = producto_db.stock; // Si existe, levantamos su stock real
            }

            len += snprintf(values + len, sizeof(values) - len, //Crea la estrictira JSON NOMBRE DEL PRODUCTO:STOCK ACTUAL.
                            "%s\"%s\":%lu",
                            (j == 0) ? "" : ",",
                            catalogo_completo[i].nombre,
                            (unsigned long)stock_actual);
        }

        time_t ts = timestamp_or_fallback();

        snprintf(payload, sizeof(payload),           //Crea el payload final en formato JSON con el timestamp y los valores de los productos.
                 "{\"ts\":%lld,\"values\":{%s}}",
                 (long long)ts * 1000LL,
                 values);

        esp_mqtt_client_publish(client, TOPIC_TELEMETRY_HV, payload, 0, 1, 0);
        ESP_LOGI(TAG, "Catalogo inicial publicado [%d/%d]: %s",
                 i, (int)CATALOGO_SIZE, payload);
        procesar_y_publicar_LOGI("INFO: catalogo inicial publicado");

        vTaskDelay(pdMS_TO_TICKS(200)); //Espera 200ms entre cada bloque de 10 productos para no saturar el broker.
    }
}

void mqtt_handler_set_loggers(Logger *logger_local, Logger *logger_recibido)//Recive los loggers de la FSM y los guarda en variables locales para poder usarlos en el resto del código.
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
 //Funcion que se encarga de procesar y gestionar los mensajes recibidos.
static bool procesar_json_recibido(const char *mensaje)
{
    Product producto_recibido;                                    //Crea una estructura de product vacia paa almacenar los datos del producto recibido. Se inicializa a cero para evitar valores basura.
    memset(&producto_recibido, 0, sizeof(producto_recibido));     //Aloca memoria para la estructura de producto y la inicializa a cero para evitar valores basura.

    time_t timestamp = 0;              //Inicializa el timestamp a cero. Se usará para almacenar el timestamp del producto recibido.
    char estado[16] = "";              //Inicializa el estado a una cadena vacía. Se usará para almacenar el estado del producto recibido.

    cJSON *json = cJSON_Parse(mensaje);   //Convierte el mensaje recibido en un objeto JSON para poder acceder a sus campos de manera estructurada.
    if (json == NULL) {
        ESP_LOGE(TAG, "Error al procesar el formato del mensaje");
        procesar_y_publicar_LOGI("ERROR: formato de mensaje MQTT invalido");
        return false;
    }

    cJSON *device_id      = cJSON_GetObjectItem(json, "device_id"); //Se obtienen los campos del JSON recibido segun la estructura esperada: device_id, id, producto, stock, timestamp y estado.
    cJSON *id             = cJSON_GetObjectItem(json, "id");
    cJSON *producto       = cJSON_GetObjectItem(json, "producto");
    cJSON *stock          = cJSON_GetObjectItem(json, "stock");
    cJSON *timestamp_json = cJSON_GetObjectItem(json, "timestamp");
    cJSON *estado_json    = cJSON_GetObjectItem(json, "estado");

    if (cJSON_IsString(device_id) && strcmp(device_id->valuestring, DEVICE_ID) == 0) {  //Si el mensaje recibido proviene del mismo dispositivo (misma device_id), se ignora el mensaje para evitar procesarlo dos veces.
        ESP_LOGI(TAG, "Mensaje propio recibido, ignorado");
        procesar_y_publicar_LOGI("INFO: mensaje propio recibido e ignorado");
        cJSON_Delete(json);
        return true;
    }

    if (cJSON_IsString(id) && cJSON_IsString(producto) && cJSON_IsNumber(stock) &&  //Se asegura de que los campos del JSON recibido sean del tipo esperado (string para id y producto, number para stock y timestamp, string para estado).
        cJSON_IsNumber(timestamp_json) && cJSON_IsString(estado_json)) {

        safe_copy(producto_recibido.id, id->valuestring, sizeof(producto_recibido.id));    //Carga la información del producto recibido en la estructura producto_recibido, asegurando que no se desborde el tamaño de los campos.
        safe_copy(producto_recibido.name, producto->valuestring, sizeof(producto_recibido.name));
        producto_recibido.stock = (uint32_t)stock->valuedouble;  //Al ser un valor numerico, se convierte a uint32_t para almacenarlo en la estructura producto_recibido.
        timestamp = (time_t)timestamp_json->valuedouble;
        safe_copy(estado, estado_json->valuestring, sizeof(estado));

        if (timestamp == 0) {                    //Asegura que el timestamp sea válido. Si no lo es, se obtiene un timestamp de respaldo.
            timestamp = timestamp_or_fallback();
        }

        if (logger_recibido_mqtt != NULL) {     //Si ya esta configurado el logger recibido, se guarda el producto recibido en el logger para mantener un registro de los productos recibidos desde HiveMQ.
            logger_push(logger_recibido_mqtt, producto_recibido, timestamp, estado);
        } else {
            ESP_LOGW(TAG, "No hay logger recibido configurado");
            procesar_y_publicar_LOGI("WARN: no hay logger recibido configurado");
        }

        ESP_LOGI(TAG, "Struct reconstruido -> ID: %s, Nombre: %s, Stock: %lu, Timestamp: %lld, Estado: %s",  //LOGI de la información del producto recibido para verificar que se haya reconstruido correctamente a partir del JSON recibido.
                 producto_recibido.id, producto_recibido.name,
                 (unsigned long)producto_recibido.stock, (long long)timestamp, estado);
        procesar_y_publicar_LOGI("INFO: producto recibido y reconstruido desde JSON");

#if DEVICE_IS_LCD
        if (fsm_mqtt_data_queue != NULL) {     //Guarda el producto recibido en la cola de datos MQTT de la FSM para que la FSM lo procese y actualice el stock real en la hash table antes de reenviarlo a ThingsBoard.
            HistoryEntry ev_data;             
            memset(&ev_data, 0, sizeof(ev_data));

            ev_data.product   = producto_recibido;                                          //Copia la información del producto recibido en la estructura HistoryEntry para enviarla a la FSM.
            ev_data.timestamp = timestamp;
            safe_copy(ev_data.state, estado, sizeof(ev_data.state));

            if (xQueueSend(fsm_mqtt_data_queue, &ev_data, pdMS_TO_TICKS(10)) != pdTRUE) {   //Manda a la queue y evalua si se mando correctamente 
                ESP_LOGW(TAG, "fsm_mqtt_data_queue llena, dato descartado");
                procesar_y_publicar_LOGI("WARN: cola MQTT de FSM llena, dato descartado");
            }
        } else {
            ESP_LOGW(TAG, "fsm_mqtt_data_queue no inicializada");
            procesar_y_publicar_LOGI("WARN: cola MQTT de FSM no inicializada");
        }
#endif

        cJSON_Delete(json);       //Limpia la estructura json 
        return true;
    }

    ESP_LOGE(TAG, "Error al procesar el formato del mensaje");                      //Si no se procesa retorna ERROR
    procesar_y_publicar_LOGI("ERROR: formato de mensaje MQTT invalido");      //Manda ese error por el logi 
    cJSON_Delete(json);                                                          
    return false;
}

/*
 * Publica hacia ThingsBoard usando SIEMPRE el stock real almacenado en product_db.
 * La FSM llama a procesar_y_publicar() despues de actualizar stock; desde ahi se
 * arma el HistoryEntry y se entra a esta funcion.
 */
bool mqtt_reenviar_a_thingsboard(HistoryEntry entry)
{
    const char *estado = entry.state[0] != '\0' ? entry.state : "OK";      //Confirma que el estado sea OK

    if (entry.timestamp == 0) {                                            //Verifica que el timestamp sea válido. Si no lo es, se obtiene un timestamp de respaldo.
        entry.timestamp = timestamp_or_fallback();
    }

    Product producto_actual;                                                //Crea una estructura de producto vacía para almacenar la información del producto actual que se va a reenviar a ThingsBoard.
    memset(&producto_actual, 0, sizeof(producto_actual)); 

    if (!product_db_find_by_id(entry.product.id, &producto_actual)) {             //Usa una funcion auiliar de profuct_db para buscar el producto en la hash table y cargar su stock real en producto_actual. Si no se encuentra, se loguea un warning y se retorna false.
        ESP_LOGW(TAG,
                 "No se pudo reenviar a ThingsBoard: producto no encontrado en product_db -> ID=%s",
                 entry.product.id);
        return false;
    }

    return publicar_evento_con_timestamp(producto_actual,                               //Publica el producto actual con stock real hacia ThingsBoard usando la funcion publicar_evento_con_timestamp. Se pasa el estado y el timestamp del HistoryEntry recibido.
                                         estado,
                                         TOPIC_TELEMETRY_HV,
                                         entry.timestamp,
                                         true);
}
    

// ─── Event handler HiveMQ ────────────────────────────────────────────────────

static void mqtt_hivemq_event_handler(void *handler_args, esp_event_base_t base,  //Funcion que maneja los eventos de HiveMQ. Se registra como callback en iniciar_mqtt() y se encarga de procesar los eventos de conexión, desconexión, publicación y recepción de mensajes MQTT.
                                      int32_t event_id, void *event_data)
{
    (void)handler_args;
    (void)base;

    esp_mqtt_event_handle_t event = event_data;                          
    esp_mqtt_client_handle_t client = event->client;
    EventType ev;

    switch ((esp_mqtt_event_id_t)event_id) {

    case MQTT_EVENT_CONNECTED:   //Se encarga de asegirarse que el cliente HiveMQ este conectado y suscrito a los topics correspondientes. Si la conexión es exitosa, se publica un mensaje de log indicando que se ha conectado y se suscribe a los topics de control y comunicación.
        cliente_hivemq = client;
        ESP_LOGI(TAG, "HiveMQ: conectado como %s", DEVICE_ID);
        procesar_y_publicar_LOGI("INFO: HiveMQ conectado");
#if DEVICE_IS_LCD
        esp_mqtt_client_subscribe(client, TOPIC_COMUNICACION, 0);           //Si es el dispositivo LCD se siscribe al topico de comunicacion 
        ESP_LOGI(TAG, "LCD: suscrito a %s", TOPIC_COMUNICACION);                       //Emite un LOGI indicando que se suscribio al topico de comunicacion
        procesar_y_publicar_LOGI("INFO: LCD suscrita al topic de comunicacion"); //Publica el LOGI en TOPIC_LOGI indicando que se suscribio al topico de comunicacion
        publicar_catalogo_inicial(client);                                            // publica el catalogo inicial de productos en bloques de 10 productos hacia ThingsBoard para que el dashboard tenga la información inicial de stock.
 #else
        ESP_LOGI(TAG, "CAM: publicara en %s", TOPIC_COMUNICACION);
        procesar_y_publicar_LOGI("INFO: CAM lista para publicar en topic de comunicacion");
#endif
        ev = EV_MQTT_CONNECT_SUCCESS;             
        post_fsm_event(ev); // la FSM usa este evento para ejecutar mqtt_handler_flush_pending() //Evento para mandar a la FSM que indica que la conexión MQTT fue exitosa. La FSM lo usa para ejecutar mqtt_handler_flush_pending() y reenviar los eventos pendientes almacenados en pending_queue.
        esp_mqtt_client_publish(client, "ESP_CONNECTED", DEVICE_ID, 0, 1, 0);
        break;

    case MQTT_EVENT_DISCONNECTED:                //Evento que indica que se desconecto el MQTT
        ESP_LOGI(TAG, "HiveMQ: desconectado");
        procesar_y_publicar_LOGI("INFO: HiveMQ desconectado");
        cliente_hivemq = NULL;
        ev = EV_MQTT_CONNECT_FAILURE;     
        post_fsm_event(ev);  //Agreaga a la cola de eventos de la fsm la desconexion 
        break;

    case MQTT_EVENT_DATA: {
        char topic[128];     //Crea un buffer para almacenar el topic del mensaje recibido y otro para almacenar el mensaje en sí. 
        char mensaje[256];

        int topic_len = event->topic_len < (int)sizeof(topic) - 1   ? event->topic_len  : (int)sizeof(topic) - 1; //condición ? si_verdadero : si_falso(casteo a int para evitar warnings de compilación)
        int data_len  = event->data_len  < (int)sizeof(mensaje) - 1 ? event->data_len   : (int)sizeof(mensaje) - 1;

        memcpy(topic,   event->topic, topic_len);  topic[topic_len]   = '\0'; //Copias la información del topic y del mensaje recibido en los buffers correspondientes, asegurando que estén terminados en nulo para evitar desbordamientos de buffer.
        memcpy(mensaje, event->data,  data_len);   mensaje[data_len]  = '\0';

        ESP_LOGI(TAG, "HiveMQ mensaje en topic: %s", topic);
        procesar_y_publicar_LOGI("INFO: mensaje MQTT recibido en un topic");
        ESP_LOGI(TAG, "HiveMQ datos: %s", mensaje);
        procesar_y_publicar_LOGI("INFO: datos MQTT recibidos");

#if DEVICE_IS_LCD
        if (strcmp(topic, TOPIC_COMUNICACION) == 0) {        //Procesamos el mensaje recibido  si es la pantalla(la CAM no preocesa)
            procesar_json_recibido(mensaje);
        } else {
            ESP_LOGI(TAG, "Topic desconocido, ignorado");
            procesar_y_publicar_LOGI("INFO: topic desconocido ignorado");
        }
#else
        ESP_LOGW(TAG, "CAM recibio mensaje inesperado en topic: %s", topic);
        procesar_y_publicar_LOGI("WARN: CAM recibio mensaje MQTT inesperado");
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

    case MQTT_EVENT_ERROR:           //Publicamos el error
        ESP_LOGE(TAG, "HiveMQ: error MQTT");
        procesar_y_publicar_LOGI("ERROR: HiveMQ error MQTT");
        if (event->error_handle != NULL) {
            ESP_LOGE(TAG, "Tipo: %d", event->error_handle->error_type);
            procesar_y_publicar_LOGI("ERROR: tipo de error MQTT");
            ESP_LOGE(TAG, "esp-tls: 0x%x", event->error_handle->esp_tls_last_esp_err);
            procesar_y_publicar_LOGI("ERROR: error TLS en MQTT");
            ESP_LOGE(TAG, "socket errno: %d", event->error_handle->esp_transport_sock_errno);
            procesar_y_publicar_LOGI("ERROR: socket errno en MQTT");

            if (event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
                ev = EV_MQTT_CONNECT_FAILURE; 
                post_fsm_event(ev);
            }
        }
        break;

    default:
        break;
    }
}

// ─── Inicio MQTT ─────────────────────────────────────────────────────────────

void iniciar_mqtt(void)    //Iniciamos mqtt con la configuracion de HiveMQ y registramos el event handler para manejar los eventos de conexión, desconexión, publicación y recepción de mensajes MQTT.
{
    esp_mqtt_client_config_t hivemq_cfg = {   //Carga la configuración del cliente MQTT para conectarse a HiveMQ. Se especifica la URI del broker.
        .broker.address.uri = HIVEMQ_URI,
    };

    esp_mqtt_client_handle_t hive_client = esp_mqtt_client_init(&hivemq_cfg);   //El sistema reserva la memoria necesaria y crea la "maquinaria" para manejar MQTT.
    if (hive_client == NULL) {
        ESP_LOGE(TAG, "No se pudo crear cliente HiveMQ");
        procesar_y_publicar_LOGI("ERROR: no se pudo crear cliente HiveMQ");
        return;
    }
    esp_mqtt_client_register_event(hive_client, ESP_EVENT_ANY_ID, mqtt_hivemq_event_handler, NULL);//Se registra el event handler para manejar los eventos de conexión, desconexión, publicación y recepción de mensajes MQTT.
    esp_mqtt_client_start(hive_client);//Inicia todo el proceso de conexión y comunicación con HiveMQ. Esto incluye la conexión al broker, la suscripción a los topics y la publicación de mensajes.
}

// ─── Publicacion ─────────────────────────────────────────────────────────────

static bool publicar_evento(Product producto, const char *estado)   //Usa la funcion auxiliar publicar_evento_con_timestamp para publicar un evento MQTT con el producto y estado especificados. Se obtiene un timestamp de respaldo si no se puede obtener uno válido.
{
    time_t timestamp = timestamp_or_fallback();
    const char *topic = topic_salida();

    return publicar_evento_con_timestamp(producto, estado, topic, timestamp, true);
}

/*
 * Publica un producto valido con estado OK.
 *
 * CAMBIO DE LIMPIEZA:
 * Se vuelve al nombre original del equipo MQTT: procesar_y_publicar(...).
 * No se separa entre "QR" y "manual" porque hoy MQTT no necesita conocer
 * el origen del producto. La FSM puede haber confirmado un QR, una seleccion
 * manual o una cantidad, pero para MQTT el resultado es el mismo: producto OK.
 */
bool procesar_y_publicar(Product producto)
{
    return publicar_evento(producto, "OK");
}

bool procesar_y_publicar_error(const char *mensaje_error)   //Creamos una estructura Producto pero con valores de error y la publicamos en el topic de salida. Si no se puede publicar, se guarda como pendiente para reenviarla más tarde.
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
    if (cliente_hivemq == NULL) {    //Realisa las confirmaciones que este disponible el broker.
        ESP_LOGW(TAG, "HiveMQ no conectado: no se puede reenviar cola pendiente");
        procesar_y_publicar_LOGI("WARN: HiveMQ no conectado, no se puede reenviar cola pendiente");
        return false;
    }

    int count = pending_queue_count();                          //Confirma que hayan eventos pendientes 
    if (count == 0) {
        ESP_LOGI(TAG, "No hay eventos pendientes para reenviar");
        procesar_y_publicar_LOGI("INFO: no hay eventos pendientes para reenviar");
        return true;
    }

    ESP_LOGI(TAG, "Reenviando %d evento(s) pendiente(s)", count);
    procesar_y_publicar_LOGI("INFO: reenviando eventos pendientes");

    while (pending_queue_count() > 0) {                       //Mientras halla algo en pendiente publico.
        PendingMqttEvent ev_pendiente;                      
        memset(&ev_pendiente, 0, sizeof(ev_pendiente));

        if (!pending_queue_peek(&ev_pendiente)) {                     //peek (espiar) en lugar de un pop (extraer).
            ESP_LOGW(TAG, "No se pudo leer el proximo evento pendiente");       //Para asegurarse que se puede leer.
            procesar_y_publicar_LOGI("WARN: no se pudo leer el proximo evento pendiente");
            return false;
        }
 
        if (!publicar_evento_con_timestamp(ev_pendiente.product,   //Publico el evento pendiente mas viejo. Si no se puede publicar, se corta el flush y se retorna false.
                                           ev_pendiente.state,
                                           ev_pendiente.topic,
                                           ev_pendiente.timestamp,
                                           false)) {
            ESP_LOGW(TAG, "No se pudo publicar el pendiente mas viejo. Se corta flush");
            procesar_y_publicar_LOGI("WARN: no se pudo publicar pendiente, se corta flush");
            return false;
        }

        if (!pending_queue_pop()) {                                                        //Quito el pendiente de la lista 
            ESP_LOGW(TAG, "Publicado pendiente, pero no se pudo quitar de NVS");
            procesar_y_publicar_LOGI("WARN: pendiente publicado pero no se pudo quitar de NVS");
            return false;
        }
    }

    ESP_LOGI(TAG, "Todos los eventos pendientes fueron reenviados");
    procesar_y_publicar_LOGI("INFO: todos los eventos pendientes fueron reenviados");
    return true;
}