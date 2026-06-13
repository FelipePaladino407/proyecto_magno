#include "mqtt_client.h"
#include "esp_log.h"
#include "mqtt_handler.h"

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "¡Conectado al broker MQTT!");

            esp_mqtt_client_subscribe(client, "control/configuracion", 0);
            
            // Ejemplo: Publicar un dato 
            esp_mqtt_client_publish(client, "sensor/distancia_piernas", "0.85", 0, 1, 0);
            break;

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "Desconectado del broker MQTT");
            break;

        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "Mensaje recibido en el tema: %.*s", event->topic_len, event->topic);
            ESP_LOGI(TAG, "Datos: %.*s", event->data_len, event->data);
            break;

        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "Ha ocurrido un error en MQTT");
            break;

        default:
            break;
    }
}

static const char *TAG = "MQTT_APP";
void iniciar_mqtt(void) {
    // Configuración básica apuntando a un broker público de prueba
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = "mqtt://broker.hivemq.com", 
    };

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    
    // Manejador de eventos que creamos arriba
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    
    // Arrancamos el cliente
    esp_mqtt_client_start(client);
}
