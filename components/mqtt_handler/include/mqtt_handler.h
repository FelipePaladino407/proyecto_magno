#ifndef MQTT_HANDLER_H
#define MQTT_HANDLER_H

#include "shared_types.h"
#include "logger.h"

void mqtt_handler_set_loggers(Logger *logger_local, Logger *logger_recibido); // guarda los punteros a los loggers que va a usar mqtt_handler

void iniciar_mqtt(void); // arranca el cliente mqtt y lo conecta al broker

void procesar_y_publicar_qr(Product producto_escaneado); // recibe un producto escaneado bien y lo publica con estado OK

void procesar_y_publicar_manual(Product producto_manual); // recibe un producto ingresado manualmente y lo publica con estado MANUAL

void procesar_y_publicar_error(const char *mensaje_error); // recibe un texto de error y lo publica con estado ERROR

#endif