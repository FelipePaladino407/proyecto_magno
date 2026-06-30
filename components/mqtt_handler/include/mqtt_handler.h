#ifndef MQTT_HANDLER_H
#define MQTT_HANDLER_H

#include "shared_types.h"
#include "logger.h"

void mqtt_handler_set_loggers(Logger *logger_local, Logger *logger_recibido); // guarda los punteros a los loggers que va a usar mqtt_handler

void iniciar_mqtt(void); // arranca el cliente mqtt y lo conecta al broker

bool procesar_y_publicar(Product producto_escaneado); // recibe un producto escaneado bien y lo publica con estado OK

bool procesar_y_publicar_LOGI(char *LOGI); // recibe un producto escaneado bien y lo publica con estado OK

bool procesar_y_publicar_error(const char *mensaje_error); // recibe un texto de error y lo publica con estado ERROR

extern QueueHandle_t fsm_mqtt_data_queue;

void mqtt_reenviar_a_thingsboard(const char *device_id_origen,
                                  Product producto,
                                  time_t timestamp,
                                  const char *estado);

#endif