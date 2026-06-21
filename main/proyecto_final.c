#include "freertos/FreeRTOS.h"
#include "freertos/projdefs.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "fsm.h"
#include "http_handler.h"
#include "input_handler.h"
#include "logger.h"
#include "mqtt_handler.h"
#include "ntp_handler.h"
#include "nvs.h"
#include "product_db.h"
#include "rgb_led.h"
#include "unit_test.h"
#include "wifi_manager.h"

#include <stdbool.h>
#include <stdint.h>

static const char *TAG = "MAIN";
static const char *LCD_TAG = "LCD_SIM";

QueueHandle_t fsm_event_queue = NULL;
static Logger logger_local;
static Logger logger_recibido;

#define ENABLE_FAKE_CAMERA_DEMO 1

static void lcd_display_product_cb(const Product *product) {
    if (product == NULL) {
        return;
    }

    ESP_LOGI(LCD_TAG,
             "Mostrar en LCD -> ID:%s | Nombre:%s | Stock:%lu",
             product->id,
             product->name,
             (unsigned long)product->stock);
}

static void lcd_display_message_cb(const char *title, const char *message) {
    ESP_LOGI(LCD_TAG,
             "Mostrar en LCD -> %s | %s",
             title ? title : "",
             message ? message : "");
}

static bool mqtt_publish_qr_cb(const Product *product) {
    if (product == NULL) {
        return false;
    }

    procesar_y_publicar_qr(*product);
    return true;
}

static bool mqtt_publish_manual_cb(const Product *product) {
    if (product == NULL) {
        return false;
    }

    procesar_y_publicar_manual(*product);
    return true;
}

static bool mqtt_publish_error_cb(const char *message) {
    if (message == NULL) {
        return false;
    }

    procesar_y_publicar_error(message);
    return true;
}

static void register_fsm_callbacks(void) {
    FsmCallbacks callbacks = {
        .display_product = lcd_display_product_cb,
        .display_message = lcd_display_message_cb,
        .publish_qr = mqtt_publish_qr_cb,
        .publish_manual = mqtt_publish_manual_cb,
        .publish_error = mqtt_publish_error_cb,
    };

    fsm_register_callbacks(&callbacks);
}

static void seed_demo_products(void) {
    Product product;

    if (product_db_upsert_product("PROD001", "Cigarros", 0, &product)) {
        ESP_LOGI(TAG,
                 "Producto demo cargado -> ID:%s | Nombre:%s | Stock:%lu",
                 product.id,
                 product.name,
                 (unsigned long)product.stock);
    }

    if (product_db_upsert_product("PROD002", "Alfajor", 0, &product)) {
        ESP_LOGI(TAG,
                 "Producto demo cargado -> ID:%s | Nombre:%s | Stock:%lu",
                 product.id,
                 product.name,
                 (unsigned long)product.stock);
    }
}

void fsm_task(void *pvParameters) {
    (void)pvParameters;

    EventType incoming_event;
    ESP_LOGI(TAG, "FSM Task started");

    while (1) {
        if (xQueueReceive(fsm_event_queue, &incoming_event, portMAX_DELAY) == pdPASS) {
            ESP_LOGI(TAG, "EVENT RECEIVED: %d", incoming_event);
            fsm_execute_transition(incoming_event);
        }
    }
}

static void fake_camera_task(void *pvParameters) {
    (void)pvParameters;

    vTaskDelay(pdMS_TO_TICKS(5000));

    ESP_LOGI(TAG, "[FAKE_CAMERA] QR valido: PROD001/Cigarros");
    fsm_on_qr_detected("PROD001", "Cigarros");

    vTaskDelay(pdMS_TO_TICKS(7000));

    ESP_LOGI(TAG, "[FAKE_CAMERA] Mismo QR de nuevo: PROD001/Cigarros");
    fsm_on_qr_detected("PROD001", "Cigarros");

    vTaskDelay(pdMS_TO_TICKS(7000));

    ESP_LOGI(TAG, "[FAKE_CAMERA] QR no registrado: PROD999/Galletas");
    fsm_on_qr_detected("PROD999", "Galletas");

    vTaskDelay(pdMS_TO_TICKS(2000));

    ESP_LOGI(TAG, "[FAKE_TOUCH] Confirmar agregado manual de PROD999");
    fsm_post_event(EV_BTN_SELECT);

    vTaskDelete(NULL);
}

void app_main(void) {
    fsm_event_queue = xQueueCreate(20, sizeof(EventType));
    if (fsm_event_queue == NULL) {
        ESP_LOGE(TAG, "CRITICAL: Failed to create FSM Event Queue");
        return;
    }

    fsm_init();
    product_db_init();
    seed_demo_products();

    rgb_led_init();

    ESP_ERROR_CHECK(nvs_storage_init());
    ESP_ERROR_CHECK(wifi_manager_init());
    ESP_ERROR_CHECK(http_handler_start());

    ESP_LOGI(TAG, "Conectate a la red %s", wifi_manager_get_ap_ssid());
    ESP_LOGI(TAG, "Abre en el navegador: http://192.168.4.1");

    logger_init(&logger_local, "local");
    logger_init(&logger_recibido, "recibido");
    mqtt_handler_set_loggers(&logger_local, &logger_recibido);

    register_fsm_callbacks();

    // NTP y MQTT despues del Wi-Fi. Si no hay credenciales, el portal AP igual queda funcionando.
    init_time();
    iniciar_mqtt();

    // Los tests se corren antes de prender la tarea real para evitar mezclar pruebas con eventos reales.
    fsm_run_transition_tests();
    xQueueReset(fsm_event_queue);
    fsm_init();

    xTaskCreate(&fsm_task, "FSM_TASK", 4096, NULL, 1, NULL);

#if ENABLE_FAKE_CAMERA_DEMO
    xTaskCreate(&fake_camera_task, "FAKE_CAMERA_TASK", 4096, NULL, 1, NULL);
#endif

    // Cuando el touch real este listo, descomentar:
    // button_int_config();

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

