#ifndef MQTT_HANDLER_H
#define MQTT_HANDLER_H

#include "shared_types.h"
#include "logger.h"
#include <stdbool.h>

void mqtt_handler_set_loggers(Logger *logger_local, Logger *logger_recibido); // guarda los punteros a los loggers que va a usar mqtt_handler

esp_err_t mqtt_handler_init(void); // arranca el cliente mqtt y lo conecta al broker

bool procesar_y_publicar_qr(Product producto_escaneado); // recibe un producto escaneado bien y lo publica con estado OK

bool procesar_y_publicar_manual(Product producto_manual); // recibe un producto ingresado manualmente y lo publica con estado MANUAL

bool procesar_y_publicar_error(const char *mensaje_error); // recibe un texto de error y lo publica con estado ERROR

/* Cola offline: guarda eventos no publicados y los reenvia cuando MQTT vuelve. */
bool mqtt_handler_store_pending_qr(Product producto_escaneado);
bool mqtt_handler_flush_pending(void);

#endif
