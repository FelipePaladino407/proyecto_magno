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
extern QueueHandle_t fsm_event_queue; //usa cola de eventos de la fsm definida en otro archivo

static esp_mqtt_client_handle_t cliente_hivemq = NULL; //cliente hivemq actico

static Logger *logger_local_mqtt    = NULL; //logger donde se guardan los eventos publicados por este dispositivo (LCD y Cam lo tienen)
static Logger *logger_recibido_mqtt = NULL;//logger donde se guardan los eventos recibidos por este dispositivo (LCD)

// Permite mandar por MQTT los mismos avisos importantes que aparecen en el monitor serial.
bool procesar_y_publicar_LOGI(const char *LOGI);//se pone aca para que el compilador "la vea" porque se llama antes de ser definida que esta abajo del todo 

#if DEVICE_IS_LCD
#define MQTT_SCAN_QUEUE_LENGTH 10 //cola para guardar 10 productos escaneados por la CAM y recibidos por la LCD antes de que la FSM los procese
#define MQTT_SCAN_DISPATCH_DELAY_MS 200//si FSM esta ocupada, espera 200ms antes de reintentar entregar el scan a la FSM

static QueueHandle_t s_scan_queue = NULL;//cola donde se guardan los scans de cam que llegaron por MQTT y esperan a que la FSM LCD los procese
static TaskHandle_t s_scan_dispatch_task_handle = NULL;//guarda el handle (identificador) de la task que despacha va revisando esa cola

static void scan_dispatch_task(void *pvParameters)//saca productos de la cola ys  elos pas a ala fsm cuando esta esta en IDLE (no haciendo nada"")")
{
    (void)pvParameters;//no se usa le parametro,para evitar warnings

    Product scan;//donde se va a guardar el producto recibido desde la cola 

    while (true) {
        if (xQueueReceive(s_scan_queue, &scan, portMAX_DELAY) != pdPASS) {//espera infinito para sacar de la cola y pone en el product scan lo que recibio ,sino funciona esa instruccion 
            continue;//continue lleva al inicio dle while 
        }

        bool delivered = false;//todavia no se entrego ese producto a al fsm 

        while (!delivered) {//intenta entregar el producto a al fsm en un while infinito
            if (fsm_get_current_state() == STATE_IDLE) {//recien cuando fsm esta en estado IDLE osea no haciendo nada se entrega
                delivered = fsm_on_qr_detected(scan.id, scan.name);//le manda a la fsm llego este producto con esta ID de la camara 

                if (delivered) {//si se entrego correctamente a la fsm,esa fucion de antes da true y  se logea que se entrego correctamente a la fsm
                    ESP_LOGI(TAG,
                             "Scan MQTT entregado a FSM -> ID=%s | Nombre=%s",
                             scan.id,
                             scan.name);
                    {//esas llaves son un bloque local dodne existe esa variable logi_mqtt,afuera no existe asi no hay lio si hay dos logi a mandar dentro de la misma funcion 
                        char logi_mqtt[128];//si el mensaje a copiar es mas largo que los 127+/0 lo corta y lo termina de manera segura con /0 asi que no pasa nada 
                        snprintf(logi_mqtt, sizeof(logi_mqtt),
                                 "I: Scan MQTT entregado a FSM ID=%s nombre=%s",
                                 scan.id, scan.name);
                        procesar_y_publicar_LOGI(logi_mqtt);
                    }
                    break;
                }
            }

            vTaskDelay(pdMS_TO_TICKS(MQTT_SCAN_DISPATCH_DELAY_MS));//cada este tiempo 200ms reintenta mandar a la fsm lo escaneado 
        }
    }
}

static bool ensure_scan_dispatcher(void)//asegura que la cola y la task de despachar scans a la fsm esten creadas sino las crea y si no puede crear alguna devuelve false
{
    if (s_scan_queue == NULL) {//si al cola para mandra scans de cam a fsm no esta creada, la crea
        s_scan_queue = xQueueCreate(MQTT_SCAN_QUEUE_LENGTH, sizeof(Product));//de 10 productos de tamaño Product
        if (s_scan_queue == NULL) {//si ni asi se crea return false
            ESP_LOGE(TAG, "No se pudo crear cola local de scans MQTT");
            procesar_y_publicar_LOGI("E: No se pudo crear cola local de scans MQTT");
            return false;
        }
    }

    if (s_scan_dispatch_task_handle == NULL) {//si la tarea dodne se saca de esa cola ys e manda no existe la crea
        BaseType_t ok = xTaskCreate(&scan_dispatch_task,//funcion a ejecutar por la tarea 
                                    "MQTT_SCAN_DISPATCH",//nombre de la tarea 
                                    4096,//srack size de la tarea 
                                    NULL,//parametros de la tarea
                                    1,//prioridad de la tarea 0 la mas baja esa de basura 
                                    &s_scan_dispatch_task_handle);//guarda el handle de la tarea creada en la variable global s_scan_dispatch_task_handle
        if (ok != pdPASS) {//si no se pudo crear la task return false
            ESP_LOGE(TAG, "No se pudo crear task MQTT_SCAN_DISPATCH");
            procesar_y_publicar_LOGI("E: No se pudo crear task MQTT_SCAN_DISPATCH");
            return false;
        }
    }

    return true;
}

static bool enqueue_scan_for_fsm(const Product *scan)//recibe la direccion de un producto escaneado por la cam   
{
    if (scan == NULL || scan->id[0] == '\0') {//si no hay producto o el id del producto es vacio return false
        return false;
    }

    if (!ensure_scan_dispatcher()) {//asegura que la cola y la task de despachar scans a la fsm esten creadas sino las crea y si no puede crear alguna devuelve false
        return false;
    }

    if (xQueueSend(s_scan_queue, scan, pdMS_TO_TICKS(100)) != pdPASS) {//manda el producto a la cola epserando max 100 ms sino la cola esta lllena return false
        ESP_LOGW(TAG,
                 "Cola local de scans llena. Se descarta scan -> ID=%s | Nombre=%s",
                 scan->id,
                 scan->name);
        {
            char logi_mqtt[128];
            snprintf(logi_mqtt, sizeof(logi_mqtt),
                     "W: Cola scans llena, se descarta ID=%s nombre=%s",
                     scan->id, scan->name);
            procesar_y_publicar_LOGI(logi_mqtt);
        }
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
static const char *HIVEMQ_URI         = "mqtt://broker.hivemq.com:1883";//Dirección del broker publico de HiveMQ
static const char *TOPIC_COMUNICACION = "ucuiot/magno/x7k2/scan";      // CAM -> LCD + ThingsBoard Integration
static const char *TOPIC_TELEMETRY_HV = "ucuiot/magno/x7k2/telemetry"; // LCD -> ThingsBoard Integration

static void safe_copy(char *dest, const char *src, size_t dest_size)//para copiar un string en otra variable de forma segura,aseguurando que el detsino quee con el cierre de string "/0"
{
    if (dest == NULL || dest_size == 0) {//destino no valido o tamaño 0 return
        return;
    }

    if (src == NULL) {//si el origen es nulo, se pone el primer caracter del destino en 0 para que sea un string vacio
        dest[0] = '\0';
        return;
    }

    strncpy(dest, src, dest_size - 1);//copia el string src en dest hasta dest_size-1 caracteres para dejar espacio para el null terminator
    dest[dest_size - 1] = '\0';//cierra el string 
}

#if DEVICE_IS_LCD
/*
 * CAMBIO AGREGADO:
 * evita repetir xQueueSend y, sobre todo, evita crashear si la cola FSM aun no existe.
 * Se mantiene porque la FSM LCD sigue usando EV_MQTT_CONNECT_SUCCESS para hacer flush_pending.
 */
static void post_fsm_event(EventType ev)//recibe un evento de tipo EventType como EV_MQTT_CONNECT_SUCCESS y lo manda a la cola de eventos de la fsm
{
    if (fsm_event_queue == NULL) {//si la cola creada en otro archivo (extern arriba del todo) no fue inicializada
        ESP_LOGW(TAG, "fsm_event_queue no inicializada. Evento MQTT perdido: %d", ev);
        {
            char logi_mqtt[96];
            snprintf(logi_mqtt, sizeof(logi_mqtt),
                     "W: fsm_event_queue no inicializada, evento perdido=%d", ev);
            procesar_y_publicar_LOGI(logi_mqtt);
        }
        return;
    }

    if (xQueueSend(fsm_event_queue, &ev, pdMS_TO_TICKS(10)) != pdPASS) {//si en 10ms no pudo enviarlo a la cola de eventos 
        ESP_LOGW(TAG, "No se pudo enviar evento MQTT a FSM: %d", ev);
        {
            char logi_mqtt[96];
            snprintf(logi_mqtt, sizeof(logi_mqtt),
                     "W: No se pudo enviar evento MQTT a FSM=%d", ev);
            procesar_y_publicar_LOGI(logi_mqtt);
        }
    }
}
#endif

/*
 * CAMBIO AGREGADO:
 * pending_queue no acepta timestamp 0. Si NTP todavia no esta listo,
 * se usa time(NULL) y, como ultimo fallback, 1.
 */
static time_t timestamp_or_fallback(void)//para intentar obtener si o si un tiempo valido
{//devuelve un time_t osea la cantidad de sgeundos desde el 1ero de enero de 1970
    time_t timestamp = 0;

    if (!get_timestamp(&timestamp) || timestamp == 0) {//si al funcion ya creada para obtenerlo falla o da 0, se usa time(NULL) que devuelve el tiempo en segundos desde 1970
        timestamp = time(NULL);//devuelvela hora actual del sistema en segundos,puede estar desincronizada si NTP no esta pronto o dar 0
    }

    if (timestamp == 0) {
        timestamp = 1;//como ultimo recurso lo pone en 1 porque pending_queue no acepta timestamp 0
    }

    return timestamp;
}

/*
 * CAMBIO AGREGADO:
 * La CAM publica scans hacia TOPIC_COMUNICACION.
 * La LCD publica eventos finales/fake demo hacia TOPIC_TELEMETRY_HV para el dashboard.
 */
 
static const char *topic_salida(void)//
{
#if DEVICE_IS_LCD
    return TOPIC_TELEMETRY_HV;//topico conectadoa  thingboard
#else
    return TOPIC_COMUNICACION;//si es camara topico de comunciacion con LCD
#endif
}

/*
 * Formato plano original del equipo MQTT.
 * Se conserva para no romper la Integration/dashboard que esperaba campos:
 * device_id, id, producto, stock, timestamp y estado.
 */

 //Construye el JSON
static bool construir_payload_plano(char *payload, //Buffer dodne escribir el JSON
                                    size_t payload_size,//tama;od el buffer
                                    const char *device_id,//ID del dispositivo que envia el mensaje
                                    Product producto,//id,nombre dle producto y stock
                                    time_t timestamp,//tiempoens segundos
                                    const char *estado)//OK
{
    if (payload == NULL || payload_size == 0 || device_id == NULL || estado == NULL) {//se revisa que algun campo no sea invalido,si lo es return false
        return false;
    }
//snprintf arma el JSON y devuelve cuantos caracteres escribio
//{"device_id":"LCD_01","id":"P001","producto":"Producto 1","stock":10,"timestamp":1697040000,"estado":"OK"}
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

    return written > 0 && (size_t)written < payload_size;//vuelve si efectivamente se escribio algo y si lo escrito no se corto por falta de espacio en el buffer 
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
    if (estado == NULL || topic == NULL || topic[0] == '\0' || timestamp == 0) {//se revisa que los parametros no sean invalidos(si no hay estado,no hay topico oe ste etsa vacio o si timestamp es 0)
        ESP_LOGW(TAG, "Evento MQTT invalido, no se publica");
        procesar_y_publicar_LOGI("W: Evento MQTT invalido, no se publica");
        return false;
    }

    if (guardar_logger_local) {//si se puso como true el ultimo parametro de la fucnion tambien lo pushea en el logger de los eventos publicados por este dispositivo (LCD y Cam lo tienen)
        if (logger_local_mqtt != NULL) {//si no hay un logger local configurado, no se guarda en el logger
            logger_push(logger_local_mqtt, producto, timestamp, estado);
        } else {
            ESP_LOGW(TAG, "No hay logger local configurado");
            procesar_y_publicar_LOGI("W: No hay logger local configurado");
        }
    }

    if (cliente_hivemq == NULL) {//si no hay cliente conectado a HiveMQ, no se publica y se devuelve false,en la fsm se va a guardar en pending_queue para reintentar despues
        ESP_LOGW(TAG, "HiveMQ no conectado, no se publica ahora");
        // No se llama a procesar_y_publicar_LOGI() porque justamente no hay conexion MQTT. 
        return false;
    }

    char payload[256];//arma el JSON del producto para depsues publicarlo en HiveMQ
    if (!construir_payload_plano(payload, sizeof(payload), DEVICE_ID, producto, timestamp, estado)) {
        ESP_LOGW(TAG, "No se pudo construir payload MQTT");
        procesar_y_publicar_LOGI("W: No se pudo construir payload MQTT");
        return false;
    }

    int msg_id = esp_mqtt_client_publish(cliente_hivemq, topic, payload, 0, 1, 0);//cliente.topico,mensaje,laego automatico de la string,QoS 1 y sin retain (guardar ese valor como ultimo valor dle topico no se recibe eso apenas te suscribis)
    if (msg_id < 0) {//msj_id es un identificador de mensaje que devuelve la funcion publish,si es negativo hubo un error al publicar
        ESP_LOGW(TAG, "No se pudo publicar mensaje MQTT");
        // No se llama a procesar_y_publicar_LOGI() porque fallo la publicacion MQTT. 
        return false;
    }

    ESP_LOGI(TAG, "Publicado en %s: %s", topic, payload);
    return true;
}

/*
 * CAMBIO AGREGADO:
 * funcion comun para guardar eventos offline.
 */
static bool guardar_pendiente(Product producto, //para cuando no se puede publicar,se guarda en pending_queue para reintentar despues
                              const char *estado,
                              const char *topic,
                              time_t timestamp)
{
    if (estado == NULL) {//usa estado OK por defecto
        estado = "OK";
    }

    if (topic == NULL) {//usa topico de telemetria por defecto
        topic = TOPIC_TELEMETRY_HV;
    }

    if (timestamp == 0) {//llama a esta funcion que si o si da a un tiempo disinto de 0 e intentar obtener de nuevo la hora 
        timestamp = timestamp_or_fallback();
    }

    bool ok = pending_queue_push(producto, timestamp, estado, topic);//pushea a la cola el producto con su estado,topico y timestamp para reintentar despues de que se conecte a HiveMQ

    if (ok) {
        ESP_LOGI(TAG,
                 "Evento guardado pendiente -> topic=%s | id=%s | nombre=%s | stock=%lu | estado=%s",
                 topic,
                 producto.id,
                 producto.name,
                 (unsigned long)producto.stock,
                 estado);
        {
            char logi_mqtt[160];
            snprintf(logi_mqtt, sizeof(logi_mqtt),
                     "I: Evento pendiente guardado topic=%s nombre=%s stock=%lu ",
                     topic, producto.name, (unsigned long)producto.stock);
            procesar_y_publicar_LOGI(logi_mqtt);
        }
    } else {
        ESP_LOGE(TAG, "No se pudo guardar evento pendiente");
        procesar_y_publicar_LOGI("E: No se pudo guardar evento pendiente");
    }

    return ok;
}

#if DEVICE_IS_LCD
static void publicar_catalogo_inicial(esp_mqtt_client_handle_t client)//se ejecuta apenas la LCD se conecta a HiveMQ y publica el catalogo completo de productos con su stock real en bloques de 10 productos por mensaje
{
    char payload[512];//JSON a enviar a thingboard a tarves de hiveMQ,el JSON tiene un campo ts con el timestamp y un campo values con los productos y su stock real
    int i = 0;//indice dle producto dentro del catalogo

    while (i < (int)CATALOGO_SIZE) {
        char values[400] = "";//parte interna del JSON 
        int len = 0;//len es cuantos caracteres ya ecsribio

        for (int j = 0; j < 10 && i < (int)CATALOGO_SIZE; j++, i++) {//va publicando de a 10 productos maximo hasta acabar el catalogo
            Product producto_db;//aca guarda el producto si lo encuentra la hash table product_db por su id para obtener su stock real
            uint32_t stock_actual = 0;

            // Consultar a la hash table si el producto ya existe mediante su ID si lo encuentra devuelve true y guarda el producto en producto_db, si no lo encuentra devuelve false y stock_actual queda en 0
            if (product_db_find_by_id(catalogo_completo[i].id, &producto_db)) {
                stock_actual = producto_db.stock; // Si existe, levantamos su stock real sino stock queda en 0
            }

            len += snprintf(values + len, sizeof(values) - len, //agrega un producto al JSON en al posicion values+len entonces le queda de espacio sizeof(values) - len,
                            "%s\"%s\":%lu",
                            (j == 0) ? "" : ",",//si es es el primer producto no pone coma,sino pone coma para separar del producto anterior
                            catalogo_completo[i].nombre,
                            (unsigned long)stock_actual);
        }

        time_t ts = timestamp_or_fallback();
//le agrega un tiempo a toda esa mandada de datos 
        snprintf(payload, sizeof(payload),
                 "{\"ts\":%lld,\"values\":{%s}}",
                 (long long)ts * 1000LL, //convierte el tiempo en segunso a ms para que thingboard lo interprete bien
                 values);

//queda {"ts":1697040000000,"values":{"Producto 1":10,"Producto 2":5,...}} 

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
 * CAMBIO IMPORTANTE:
 * Se conserva la idea del equipo MQTT: si la LCD recibe un scan de CAM,
 * NO lo manda a la FSM. Lo reenvia directo a TOPIC_TELEMETRY_HV para que
 * ThingsBoard/dashboard siga funcionando como antes.
 *
 * Esto reemplaza el reenvio directo que tenia el doc 4 (que mandaba el
 * stock del JSON recibido, sin pasar por la FSM). OK
 */
#if DEVICE_IS_LCD
static bool procesar_json_recibido(const char *mensaje)
{
    Product producto_recibido;//donde va a reconstruir el producto que llego 
    memset(&producto_recibido, 0, sizeof(producto_recibido));

    time_t timestamp = 0;
    char estado[16] = "";

    cJSON *json = cJSON_Parse(mensaje);//intenta convertir el mensaje a un objeto JSON
    if (json == NULL) {//si = MULL es que JSON vino mal formado 
        ESP_LOGE(TAG, "Error al procesar el formato del mensaje");
        procesar_y_publicar_LOGI("E: Error al procesar formato del mensaje MQTT");
        return false;
    }
    //busca las claves dentro del JSON y las guarda en variables de tipo cJSON
    cJSON *device_id      = cJSON_GetObjectItem(json, "device_id");
    cJSON *id             = cJSON_GetObjectItem(json, "id");
    cJSON *producto       = cJSON_GetObjectItem(json, "producto");
    cJSON *stock          = cJSON_GetObjectItem(json, "stock");
    cJSON *timestamp_json = cJSON_GetObjectItem(json, "timestamp");
    cJSON *estado_json    = cJSON_GetObjectItem(json, "estado");

    if (cJSON_IsString(id) && cJSON_IsString(producto) && cJSON_IsNumber(stock) &&
        cJSON_IsNumber(timestamp_json) && cJSON_IsString(estado_json)) {//se asegura que tenga elf ormato esperado

        safe_copy(producto_recibido.id, id->valuestring, sizeof(producto_recibido.id));//ID del producto
        safe_copy(producto_recibido.name, producto->valuestring, sizeof(producto_recibido.name));//Nombre dle producto
        producto_recibido.stock = (uint32_t)stock->valuedouble;//stock
        timestamp = (time_t)timestamp_json->valuedouble;//tiempo
        safe_copy(estado, estado_json->valuestring, sizeof(estado));//estado

        if (timestamp == 0) {
            timestamp = timestamp_or_fallback();//es un fallo grave para pending_queue.c si hubiera timestamp 0,pero aca no es tan grave,asi que se pone un timestamp valido para que la fsm lo procese    
        }

        if (logger_recibido_mqtt != NULL) {//lo guqrda en el logger de las cosas recibdias por mqtt
            logger_push(logger_recibido_mqtt, producto_recibido, timestamp, estado);
        } else {
            ESP_LOGW(TAG, "No hay logger recibido configurado");
            procesar_y_publicar_LOGI("W: No hay logger recibido configurado");
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
        if (!enqueue_scan_for_fsm(&producto_recibido)) {//se pone en la cola de las cosas que llegaron a la LCD para que al fsm lo procese y de un stock actualizado
            ESP_LOGW(TAG, "No se pudo encolar QR recibido por MQTT para la FSM");
            procesar_y_publicar_LOGI("W: No se pudo encolar QR recibido por MQTT para FSM");
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
bool mqtt_reenviar_a_thingsboard(HistoryEntry entry)//history entry ya tiene estado,tiempo,devide id y ademas el producto con su id y nombre
{
    const char *estado = entry.state[0] != '\0' ? entry.state : "OK";//si no hay estado (strg vacio) pone estado ok por default

    if (entry.timestamp == 0) {
        entry.timestamp = timestamp_or_fallback();
    }

    Product producto_actual;
    memset(&producto_actual, 0, sizeof(producto_actual));

    if (!product_db_find_by_id(entry.product.id, &producto_actual)) {//esto guarda en producto_actual el producto con su stock real,si no lo encuentra devuelve false y no se publica nada
        ESP_LOGW(TAG,
                 "No se pudo reenviar a ThingsBoard: producto no encontrado en product_db -> ID=%s",
                 entry.product.id);
        {
            char logi_mqtt[128];
            snprintf(logi_mqtt, sizeof(logi_mqtt),
                     "W: No se pudo reenviar a ThingsBoard, producto no encontrado ID=%s",
                     entry.product.id);
            procesar_y_publicar_LOGI(logi_mqtt);
        }
        return false;
    }
//llega aca si encontro el producto en al hash table product_db y lo guarda en producto_actual con su stock real,entonces llama a publicar_evento_con_timestamp() que arma el JSON y lo publica en HiveMQ
    return publicar_evento_con_timestamp(producto_actual,
                                         estado,
                                         TOPIC_TELEMETRY_HV,//la lcd mandaa thingboard
                                         entry.timestamp,
                                         true);//true para guardarlo en el logger local de eventos publicados por este dispositivo (LCD y Cam lo tienen)
}
                              

// ─── Event handler HiveMQ ────────────────────────────────────────────────────

static void mqtt_hivemq_event_handler(void *handler_args, esp_event_base_t base,
                                      int32_t event_id, void *event_data)
{
    (void)handler_args;//no se usan entonces se hace eso para evitar warnings
    (void)base;

    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
#if DEVICE_IS_LCD
    EventType ev;
#endif

    switch ((esp_mqtt_event_id_t)event_id) {

    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "HiveMQ: conectado como %s", DEVICE_ID);
        cliente_hivemq = client;//de aca sale el cliente que se usa para suscribirse y publicar a los topicos
        {
            char logi_mqtt[96];
            snprintf(logi_mqtt, sizeof(logi_mqtt),
                     "I: HiveMQ conectado como %s", DEVICE_ID);
            procesar_y_publicar_LOGI(logi_mqtt);
        }

        esp_mqtt_client_subscribe(client, "control/configuracion", 0);

#if DEVICE_IS_LCD
        esp_mqtt_client_subscribe(client, TOPIC_COMUNICACION, 0);//el lcd tiene que suscribirse al canal de comunicacion con al camara
        ESP_LOGI(TAG, "LCD: suscrito a %s", TOPIC_COMUNICACION);
        {
            char logi_mqtt[128];
            snprintf(logi_mqtt, sizeof(logi_mqtt),
                     "I: LCD suscrita a %s", TOPIC_COMUNICACION);
            procesar_y_publicar_LOGI(logi_mqtt);
        }
        ensure_scan_dispatcher();//se asegura que exista esta cola de la fsm donde se guarda lo que llega de la camara a la LCD a taves de MQTT sino lo crea la cola y la tarea que va sacando de esa cola
        publicar_catalogo_inicial(client);

        ev = EV_MQTT_CONNECT_SUCCESS;//se avsia a la fsm que se conecto a MQTT
        post_fsm_event(ev); // la FSM LCD usa este evento para ejecutar mqtt_handler_flush_pending() (reintentar publicar lo que no se pudo por que no estaba conectado a mqtt)
#else
        ESP_LOGI(TAG, "CAM: publicara en %s", TOPIC_COMUNICACION);
        {
            char logi_mqtt[128];
            snprintf(logi_mqtt, sizeof(logi_mqtt),
                     "I: CAM publicara en %s", TOPIC_COMUNICACION);
            procesar_y_publicar_LOGI(logi_mqtt);
        }
        mqtt_handler_flush_pending(); // la CAM no levanta FSM; reenvia scans pendientes directo apenas se conecta 
#endif
        esp_mqtt_client_publish(client, "ESP_CONNECTED", DEVICE_ID, 0, 1, 0);
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "HiveMQ: desconectado");
        // No se llama a procesar_y_publicar_LOGI() porque al desconectarse ya no hay MQTT para enviarlo.
        cliente_hivemq = NULL;
#if DEVICE_IS_LCD
        ev = EV_MQTT_CONNECT_FAILURE;
        post_fsm_event(ev);//avisa a la fsm que se deconecto de HiveMQ para que la fsm sepa que no puede publicar y guarde en pending_queue
#endif
        break;

    case MQTT_EVENT_DATA: {//cuando llega un mensaje por mqtt
        char topic[128];//topico donde llego el mensaje
        char mensaje[256];//mensaje que llego en ese topico

        //se calcula cuanto se puede copiar,si el topico o le mensaje entran en el buffer se copia todo sino se trunca para que no se desborde el buffer y se cierra con \0
        int topic_len = event->topic_len < (int)sizeof(topic) - 1   ? event->topic_len  : (int)sizeof(topic) - 1;//-1 para dejar espacio para el /0
        int data_len  = event->data_len  < (int)sizeof(mensaje) - 1 ? event->data_len   : (int)sizeof(mensaje) - 1;

        //porque event->topic es un puntero al comienzo dle topic y event->topic_len es la cantidad de bytes (caracteres) que mid ele topic
        memcpy(topic,   event->topic, topic_len);  topic[topic_len]   = '\0';//entonces se copia a un buffer creado y se cierra con /0 para que sea un string valido
        memcpy(mensaje, event->data,  data_len);   mensaje[data_len]  = '\0';//para poder tratarlo como un string 

        ESP_LOGI(TAG, "HiveMQ mensaje en topic: %s", topic);
        ESP_LOGI(TAG, "HiveMQ datos: %s", mensaje);

#if DEVICE_IS_LCD
        if (strcmp(topic, TOPIC_COMUNICACION) == 0) {//si el topico es el de comunicacion entre la camara y la LCD,entonces se procesa el mensaje recibido
            procesar_json_recibido(mensaje);
        } else {
            ESP_LOGI(TAG, "Topic desconocido, ignorado");
            procesar_y_publicar_LOGI("I:(LCD) Topic desconocido, ignorado");
        }
#else
        ESP_LOGW(TAG, "CAM recibio mensaje inesperado en topic: %s", topic);//no deberia pasar
        {
            char logi_mqtt[180];
            snprintf(logi_mqtt, sizeof(logi_mqtt),
                     "W: CAM recibio mensaje inesperado topic=%s", topic);
            procesar_y_publicar_LOGI(logi_mqtt);
        }
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
        // No se llama a procesar_y_publicar_LOGI() aca para evitar un bucle:
        // publicar un LOGI tambien genera MQTT_EVENT_PUBLISHED.
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "HiveMQ: error MQTT");
        procesar_y_publicar_LOGI("E: HiveMQ error MQTT");
        if (event->error_handle != NULL) {//ve si ESP-IDF da informacion detallada del error
            ESP_LOGE(TAG, "Tipo: %d", event->error_handle->error_type);//que tipo de erorr general fue por ejemplo usuario mal,constrase;a,cliente no autorizado ,tcp_trasnport osea la conecion entre esp y el brocker ,etc
            {
                char logi_mqtt[96];
                snprintf(logi_mqtt, sizeof(logi_mqtt),
                         "E: HiveMQ tipo error=%d", event->error_handle->error_type);
                procesar_y_publicar_LOGI(logi_mqtt);
            }
            ESP_LOGE(TAG, "esp-tls: 0x%x", event->error_handle->esp_tls_last_esp_err);//ESP-TLS es la aprte que maneja las conexioens seguras tipo TLS/SSL.seguridad encriptada tipo mqtts://  en vez de mqtt:// .error tipo certificado mal o vencido ,no s epudo verificar el servidor,fallo al conexion segura
            {
                char logi_mqtt[96];
                snprintf(logi_mqtt, sizeof(logi_mqtt),
                         "E: HiveMQ esp-tls=0x%x", event->error_handle->esp_tls_last_esp_err);
                procesar_y_publicar_LOGI(logi_mqtt);
            }
            ESP_LOGE(TAG, "socket errno: %d", event->error_handle->esp_transport_sock_errno);//error en el socket TCP es basicamente la conexion red entre la placa y el brocker tipo conexion rechazada,sin internet,host no encontrado,tiemout,se corto a conexion,direcciond le brocker mal escrita o cosas asi
            {
                char logi_mqtt[96];
                snprintf(logi_mqtt, sizeof(logi_mqtt),
                         "E: HiveMQ socket errno=%d", event->error_handle->esp_transport_sock_errno);
                procesar_y_publicar_LOGI(logi_mqtt);
            } 

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
//de lo hecho en clase 
void iniciar_mqtt(void)//se llama en app_main() por eso no static ,se usa para inicializar el cliente MQTT y conectarse a HiveMQ
{   //crea a configuracion del cliente MQTT con la direccion del brocker publico de HiveMQ
    esp_mqtt_client_config_t hivemq_cfg = {
        .broker.address.uri = HIVEMQ_URI,//mqtt://broker.hivemq.com:1883
    };

    esp_mqtt_client_handle_t hive_client = esp_mqtt_client_init(&hivemq_cfg);//crea el cliente con esa configuracion
    if (hive_client == NULL) {
        ESP_LOGE(TAG, "No se pudo crear cliente HiveMQ");
        // No se llama a procesar_y_publicar_LOGI() porque todavia no existe cliente MQTT.
        return;
    }
    esp_mqtt_client_register_event(hive_client, ESP_EVENT_ANY_ID, mqtt_hivemq_event_handler, NULL);//registra el calback asi cuando hay cualquier evento(porque pone ESP_EVENT_ANY_ID ),terminna yendo arriba dodne hace el switch case de los eventos que pueden pasar en HiveMQ
    esp_mqtt_client_start(hive_client);//efecivamente inciai el cliente y si logra conectarse dispaar el evento de conexion MQTT_EVENT_CONNECTED que va a ir al callback mqtt_hivemq_event_handler,ahi es cuando recien se guarda el cliente_hivemq = client; y se suscribe a los topicos y se publica el catalogo inicial si es LCD
}

// ─── Publicacion ─────────────────────────────────────────────────────────────

//PRIVADA 
static bool publicar_evento(Product producto, const char *estado)//es una funcion auxilioar que a aprtir del producto y sue stado consigue el tiempo y el topic al cual mandar segun sea camara o LCD
{
    time_t timestamp = timestamp_or_fallback();
    const char *topic = topic_salida();

    return publicar_evento_con_timestamp(producto, estado, topic, timestamp, true);//funcion donde de verdad se inteta publicar sino va a la cola de pending_queue para reintentar despues cuando se conecte a HiveMQ
    //true porque guarda en el logger local ,cuando se quiere llamar a esa funcion porque se reintenta publiccar dede al cola ya no hayq ue guardar en el logger porque ya se hizo 
}
//PUBLICA 
bool procesar_y_publicar(Product producto)
{
#if DEVICE_IS_LCD
    HistoryEntry entry;
    memset(&entry, 0, sizeof(entry));

    entry.product = producto;
    entry.timestamp = timestamp_or_fallback();
    safe_copy(entry.state, "OK", sizeof(entry.state));

    return mqtt_reenviar_a_thingsboard(entry);//si es LCD publica el producto con su stock real y el estado OK en el topico de telemetria de thingboard
#else
    return publicar_evento(producto, "OK");//si es camara publica solo el producto con stock = 0 en el topico de comunciacion ,depsues la otra fucnion le asigna un tiempo y lo publica en HiveMQ
#endif
}

bool procesar_y_publicar_error(const char *mensaje_error)//el nombre del producto es el mensaje de eror ,su estado es "ERROR" y el stock es 0, se publica en el topico de salida (TOPIC_TELEMETRY_HV o TOPIC_COMUNICACION segun sea LCD o CAM)  
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
    return guardar_pendiente(producto_error, "ERROR", topic, 0);//donde se guarda a  pending_queue_push,pasa tiempo 0,se llama a timestamp_or_fallback() para que le ponga un tiempo valido
}

/*
 * CAMBIO AGREGADO (fix de compilacion):
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
        // No se llama a procesar_y_publicar_LOGI() aca para evitar recursividad: esta es la funcion que intenta publicar LOGI.
        return false;
    }

    int msg_id = esp_mqtt_client_publish(cliente_hivemq, TOPIC_LOGI, LOGI, 0, 1, 0);
    return msg_id >= 0;
}

/*
 * Callback usado por la FSM cuando publicar si un producto valido falla.
 *
 * CAMBIO DE LIMPIEZA:
 * Antes se llamaba mqtt_handler_store_pending_qr(...). Se renombro porque no hay una
 * cola distinta para QR/manual. Todo producto valido pendiente se guarda igual.
 */
//PUBLICA
bool mqtt_handler_store_pending(Product producto)
{
    return guardar_pendiente(producto, "OK", topic_salida(), 0);//guarda en la pending_queue  ,tiempo en 0 entonces esa funcion llama a timestamp_or_fallback() para que le ponga un tiempo valido
}

/*
 * CAMBIO AGREGADO:
 * Reenvia la cola persistente FIFO cuando MQTT vuelve.
 * La FSM lo invoca al recibir EV_MQTT_CONNECT_SUCCESS.
 */
//PUBLICA
bool mqtt_handler_flush_pending(void)//devuelve true si pudo reenviar todos los productos pendientes
{
    if (cliente_hivemq == NULL) {//si no se esta conectado da false 
        ESP_LOGW(TAG, "HiveMQ no conectado: no se puede reenviar cola pendiente");
        // No se llama a procesar_y_publicar_LOGI() porque justamente no hay conexion MQTT.
        return false;
    }

    int count = pending_queue_count();//ve la cantidad de productos a enviar
    if (count == 0) {
        ESP_LOGI(TAG, "No hay eventos pendientes para reenviar");
        return true;//salio bien ,se quiso reenviar cuando volvio la conexion solo que no habai nada que enviar entonces true
    }

    ESP_LOGI(TAG, "Reenviando %d evento(s) pendiente(s)", count);
    {
        char logi_mqtt[96];
        snprintf(logi_mqtt, sizeof(logi_mqtt),
                 "I: Reenviando %d eventos pendientes", count);
        procesar_y_publicar_LOGI(logi_mqtt);
    }

    while (pending_queue_count() > 0) {//mientras siga habiendo eventos pendientes en la cola,hace pending_queue_count() de nuevo porque eso se va actualizando a medida que se va sacando con  pending_queue_pop()
        PendingMqttEvent ev_pendiente;
        memset(&ev_pendiente, 0, sizeof(ev_pendiente));
        //PEEK significa mirar!
        if (!pending_queue_peek(&ev_pendiente)) {//solo LEE el evenot mas viejo y lo guarda en ev_pendiente,si no puede leerlo devuelve false
            ESP_LOGW(TAG, "No se pudo leer el proximo evento pendiente");
            procesar_y_publicar_LOGI("W: No se pudo leer proximo evento pendiente");
            return false;
        }

        if (!publicar_evento_con_timestamp(ev_pendiente.product, //si no puede publicar este evento pendiente devuelve false y no se saca de los pendientes
                                           ev_pendiente.state,
                                           ev_pendiente.topic,
                                           ev_pendiente.timestamp,
                                           false)) {//no guada en el logger local porque y fue guardado la primera vez cuanod se quiso publicar
            ESP_LOGW(TAG, "No se pudo publicar el pendiente mas viejo. Se corta flush");
            // No se llama a procesar_y_publicar_LOGI() porque fallo una publicacion MQTT.
            return false;
        }

        if (!pending_queue_pop()) {//cuando vio que se pudo publicar recien ahi lo saca de la cola de productos a enviar
            ESP_LOGW(TAG, "Publicado pendiente, pero no se pudo quitar de NVS");
            procesar_y_publicar_LOGI("W: Pendiente publicado, pero no se pudo sacar de NVS");
            return false;
        }
    }

    ESP_LOGI(TAG, "Todos los eventos pendientes fueron reenviados");
    procesar_y_publicar_LOGI("I: Todos los eventos pendientes fueron reenviados");
    return true;
}
/*
 * Mejoras futuras / limitaciones:
 *
 * - Agregar una cola para los LOGI/W/E enviados por MQTT. Ahora  si HiveMQ
 *   está desconectado, esos mensajes no se publican y se pierden. La pending_queue
 *   guarda eventos de productos, no mensajes de log genericos
 *
 * - Mejorar el envío de LOGI con variables. Ahora varios mensajes se arman a manopla
 *   con buffers locales y snprintf; a futuro se podria hacer una función auxiliar
 *   que reciba el formato y arme el string internamente, para dejar el codigo más limpio
 *
 * - Se evita publicar logs MQTT desde eventos generados por la propia publicación,
 *   como MQTT_EVENT_PUBLISHED, para no provocar un bucle de mensajes. Aparte los
 *   problemas asociados a la conexión con el broker no siempre pueden enviarse por MQTT,
 *   porque si la conexión fallo no hay canal disponible para publicarlos
 *
 * - Separar mejor el log mandado por MQTT del mqtt_handler.c. Actualmente la función
 *   procesar_y_publicar_LOGI() está dentro del modulo MQTT, por lo que usarla desde
 *   otros módulos aumenta el acoplamiento entre archivos y bueno puede generar dependencia cirucalar. 
 *   A futuro podria hacerse un modulo aparte , cuidando que no dependa circularmente de MQTT
 *   o de la FSM
 *
 * - Hay que revisar los tamaños de buffers usados para mensajes recibidos, payloads y LOGI.
 *   Si un mensaje es más largo de lo esperado, puede quedar truncado. No necesariamente
 *   rompe el programa, pero se puede perder parte de la información o bueno dar mas espacio
 *   del necesario no optimizando el codigo
 *
 * - Agregar reintentos de sincronización NTP si la hora no se obtiene al inicio.
 *   Actualmente mqtt_handler.c usa una función auxiliar para asegurar que el timestamp
 *   no sea 0, porque la pendng_queue rechaza eventos con timestamp 0 y eso no se supo hasta hace poco.
 *   Sería más prolijo resolver esto desde ntp.c o con una política común de tiempo válido
 *
 * - Evaluar reducir escrituras en NVS del logger si se generan muchos eventos seguidos.
 *   Actualmente cada logger_push() guarda el buffer completo y los índices en NVS.
 *   A futuro se podría guardar cada cierto tiempo o cada cierta cantidad de eventos ,
 *   usando un contador. En esta versión se eligio guardar cada evento para comprobar
 *    el funcionamiento de la persistencia en NVS, aunque a largo plazo no
 *   ir sobreescribiendo la memoria flash a cada rato (se desgasta)
 */
