#ifndef MQTT_HANDLER_H
#define MQTT_HANDLER_H

#include <stdbool.h>

#include "logger.h"
#include "shared_types.h"


// Arranca el cliente MQTT y lo conecta a HiveMQ.
void iniciar_mqtt(void);

// Inyecta los loggers que usa el modulo (local y de mensajes recibidos).
void mqtt_handler_set_loggers(Logger *logger_local, Logger *logger_recibido);

// Publica un producto escaneado con estado "OK".
bool procesar_y_publicar(Product producto);

// Publica un evento de error con un mensaje libre.
bool procesar_y_publicar_error(const char *mensaje_error);

// Publica un mensaje de log/diagnostico en TOPIC_LOGI.
bool procesar_y_publicar_LOGI(const char *LOGI);

// Llamada por la FSM una vez que actualizo el stock real en la hash table
// a partir de un HistoryEntry recibido por fsm_mqtt_data_queue. Construye
// el payload sin device_id (ya filtrado en procesar_json_recibido) y lo
// publica en TOPIC_TELEMETRY_HV. Si falla, lo guarda en pending_queue.
bool mqtt_reenviar_a_thingsboard(HistoryEntry entry);

// Callback que usa la FSM para guardar un producto pendiente cuando
// procesar_y_publicar() falla (ej. sin conexion).
bool mqtt_handler_store_pending(Product producto);

// Reenvia todo lo guardado en pending_queue. La FSM lo llama al recibir
// EV_MQTT_CONNECT_SUCCESS.
bool mqtt_handler_flush_pending(void);


#endif // MQTT_HANDLER_H
