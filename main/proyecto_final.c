#include "freertos/idf_additions.h"
#include "freertos/projdefs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "fsm.h"
#include "logger.h"
#include "ntp_handler.h"
#include "mqtt_handler.h"
#include "wifi_manager.h"
#include "http_handler.h"
#include "nvs.h"
#include "rgb_led.h"
#include "unit_test.h"
#include "input_handler.h"
#include <stdint.h>
#include <stdlib.h>  

static const char *catalogo[] = {
    "Leche Entera 1L",        "Leche Descremada 1L",     "Yogur Natural 200g",
    "Yogur Frutilla 200g",    "Queso Mozzarella 400g",   "Queso Colonia 300g",
    "Manteca 200g",           "Crema de Leche 200ml",    "Huevos x12",
    "Pan de Molde 500g",      "Pan Lactal Integral 400g","Facturas x6",
    "Arroz Largo Fino 1kg",   "Fideos Spaghetti 500g",   "Fideos Mono 500g",
    "Harina 0000 1kg",        "Azucar Blanca 1kg",       "Aceite Girasol 900ml",
    "Aceite de Oliva 500ml",  "Sal Fina 500g",           "Lentejas 500g",
    "Garbanzos 500g",         "Porotos Negros 500g",     "Tomate en Lata 400g",
    "Atun al Natural 170g",   "Atun en Aceite 170g",     "Arvejas en Lata 300g",
    "Choclo en Lata 300g",    "Mermelada Frutilla 390g", "Dulce de Leche 400g",
    "Cafe Molido 250g",       "Te Negro x20",            "Agua Mineral 1.5L",
    "Jugo de Naranja 1L",     "Gaseosa Cola 2L",         "Gaseosa Naranja 2L",
    "Cerveza Lata 473ml",     "Vino Tinto 750ml",        "Papas Fritas 150g",
    "Galletitas Dulces 200g", "Galletitas Saladas 150g", "Chocolate 100g",
    "Mayonesa 500g",          "Ketchup 400g",            "Mostaza 200g",
    "Jabon en Polvo 1kg",     "Lavandina 1L",            "Detergente 750ml",
    "Papel Higienico x4",     "Shampoo 400ml",
};
#define CATALOGO_SIZE (sizeof(catalogo) / sizeof(catalogo[0]))

static const char *TAG = "MAIN";
QueueHandle_t fsm_event_queue = NULL;
static Logger logger_local;
static Logger logger_recibido;

void fsm_task(void *pvParameters) {
    EventType incoming_event;
    ESP_LOGI(TAG, "FSM Task started");
    while (1) {
        if (xQueueReceive(fsm_event_queue, &incoming_event, portMAX_DELAY) == pdPASS) {
            ESP_LOGI(TAG, "EVENT RECEIVED: %d", incoming_event);
            fsm_execute_transition(incoming_event);
        }
    }
}

void fsm_run_transition_tests(void);

void app_main(void) {
    fsm_event_queue = xQueueCreate(20, sizeof(EventType));
    if (fsm_event_queue == NULL) {
        ESP_LOGE(TAG, "CRITICAL: Failed to create FSM Event Queue");
        return;
    }
  
    fsm_init();
  
    fsm_init();
    rgb_led_init();

    ESP_ERROR_CHECK(nvs_storage_init());
    ESP_ERROR_CHECK(wifi_manager_init());
    // Hardcode de credenciales (reemplazar antes de flashear):
    // Descomentar y modificar con tu SSID/clave si quieres que el dispositivo
    // se conecte directamente sin usar el portal de configuración.
    // ESP_ERROR_CHECK(wifi_manager_set_credentials("TU_SSID", "TU_PASS"));
    ESP_ERROR_CHECK(http_handler_start());

    ESP_LOGI(TAG, "Conectate a la red %s", wifi_manager_get_ap_ssid());
    ESP_LOGI(TAG, "Abre en el navegador: http://192.168.4.1");

    logger_init(&logger_local, "local");
    logger_init(&logger_recibido, "recibido");
    mqtt_handler_set_loggers(&logger_local, &logger_recibido);

    // NTP y MQTT después del wifi
    init_time();
    iniciar_mqtt();

    xTaskCreate(&fsm_task, "FSM_TASK", 4096, NULL, 1, NULL);
    fsm_run_transition_tests();
    
    ESP_LOGI(TAG, "Esperando conexion WiFi y MQTT...");
    vTaskDelay(pdMS_TO_TICKS(8000)); 

    srand((unsigned int)time(NULL));


    while (true) {
        vTaskDelay(pdMS_TO_TICKS(2000));

        int idx = rand() % CATALOGO_SIZE;

        Product p = {
            .stock = (rand() % 50) + 1
        };
        snprintf(p.id,   sizeof(p.id),   "%011d", idx);
        snprintf(p.name, sizeof(p.name), "%s", catalogo[idx]);

        procesar_y_publicar(p);
    }
}
