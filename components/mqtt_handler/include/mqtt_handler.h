#ifndef MQTT_HANDLER_H
#define MQTT_HANDLER_H

#include "logger.h"
#include "shared_types.h"

#include <stdbool.h>

/* Guarda los punteros a los loggers que usa mqtt_handler. */
void mqtt_handler_set_loggers(Logger *logger_local, Logger *logger_recibido);

/* Arranca el cliente MQTT y lo conecta al broker HiveMQ. */
void iniciar_mqtt(void);

/*
 * Publica un producto valido.
 *
 * Se conserva el nombre original de los muchachos MQTT para no inventar
 * una distincion artificial entre "QR" y "manual". Y para que no me peguen
 * El origen del producto lo resuelve la FSM/camara/LCD; MQTT solo publica
 * un producto ya aceptado con estado OK.
 */
bool procesar_y_publicar(Product producto);

/* Publica un error con estado ERROR. Si falla, mqtt_handler lo guarda como pendiente. */
bool procesar_y_publicar_error(const char *mensaje_error);

/*
 * NUEVO E IMPORTANTE ---- Cola offline persistente.
 * La FSM puede llamar store_pending si falla publicar un producto valido,
 * y flush_pending cuando MQTT vuelve a conectar.
 */
bool mqtt_handler_store_pending(Product producto);
bool mqtt_handler_flush_pending(void);

#endif // MQTT_HANDLER_H
