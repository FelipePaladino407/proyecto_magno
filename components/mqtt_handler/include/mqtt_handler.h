#ifndef MQTT_HANDLER_H
#define MQTT_HANDLER_H

void iniciar_mqtt(void);
void mqtt_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);
void procesar_y_publicar_qr(Product producto_escaneado);
void mqtt_event_handler_pantalla(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);

#endif // MQTT_HANDLER_H
