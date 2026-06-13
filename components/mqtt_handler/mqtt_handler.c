#include "mqtt_client.h"
#include "esp_log.h"
#include "mqtt_handler.h"
#include "shared_types.h"

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "¡Conectado al broker MQTT!");
            esp_mqtt_client_subscribe(client, "control/configuracion", 0);
            esp_mqtt_client_publish(client, "ESP_CONNECTED", "1", 0, 1, 0);
            break;

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "Desconectado del broker MQTT");
            break;
        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "Producto recibido en el tema: %.*s", event->topic_len, event->topic);
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
        .broker.address.uri = "mqtts://1cff5a3e91744d43b7af27dc81364b6d.s1.eu.hivemq.cloud:8883", 
    };

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    
    // Manejador de eventos
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    
    // Arrancamos el cliente
    esp_mqtt_client_start(client);
}
void procesar_y_publicar_qr(Product producto_escaneado) {
    if (cliente_mqtt_global == NULL) return; // Asegurar que estamos conectados

    char payload[100];
    // Empaquetamos el struct en un string: "ID|Nombre|Stock"
    snprintf(payload, sizeof(payload), "%s|%s|%lu", 
             producto_escaneado.id, 
             producto_escaneado.name, 
             producto_escaneado.stock);

    // Publicamos el texto armado
    esp_mqtt_client_publish(cliente_mqtt_global, "sistema/escaner/lectura_qr", payload, 0, 1, 0);
    ESP_LOGI(TAG, "Producto publicado: %s", payload);
}
static void mqtt_event_handler_pantalla(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "Conectado. Suscribiendo a lectura_qr...");
            esp_mqtt_client_subscribe(client, "sistema/escaner/lectura_qr", 0);
            cliente_mqtt_global = client;
            break;

        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "Dato recibido en tema: %.*s", event->topic_len, event->topic);
            
            char qr_recibido[100];
            snprintf(qr_recibido, sizeof(qr_recibido), "%.*s", event->data_len, event->data);
            Product producto_recibido;
            // Usamos sscanf para leer el formato "ID|Nombre|Stock"
            int compartimento = 0;     // Empieza en 0 (ID)
            int posicion = 0;          // Pocicion dentro del compartimento
            char texto_stock[15] = ""; // Guardar el número como texto

            // El for avanza letra por letra hasta que encuentra el final del texto ('\0')
            for (int i = 0; qr_recibido[i] != '\0'; i++) {
                char letra = qr_recibido[i];
            
                if (letra == '|') {
                    // Encontramos una barra: cambiamos de caja y reseteamos la posición
                    compartimento++;
                    posicion = 0; 
                } else {
                    if (compartimento == 0) {              // No es una barra, así que guardamos la letra donde corresponda
                        producto_recibido.id[posicion] = letra;
                        producto_recibido.id[posicion + 1] = '\0'; // Asegura el fin de la palabra
                    } 
                    else if (compartimento == 1) {
                        producto_recibido.name[posicion] = letra;
                        producto_recibido.name[posicion + 1] = '\0';
                    } 
                    else if (compartimento == 2) {
                        texto_stock[posicion] = letra;
                        texto_stock[posicion + 1] = '\0';
                    }
                    posicion++; // Avanzamos el espacio para la siguiente letra
                }
            }
                producto_recibido.stock = atoi(texto_stock);
                ESP_LOGI(TAG, "Struct reconstruido -> ID: %s, Nombre: %s, Stock: %lu", 
                         producto_recibido.id, producto_recibido.name, producto_recibido.stock);
            
            } else {
                ESP_LOGE(TAG, "Error al procesar el formato del mensaje");
            }
            break;
            
        default:
            break;
    }