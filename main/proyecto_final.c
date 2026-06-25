#include "freertos/idf_additions.h"
#include "freertos/projdefs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

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
#include "product_db.h"

#include <stdbool.h>
#include <stdint.h>

static const char *TAG = "MAIN";

QueueHandle_t fsm_event_queue = NULL;

static Logger logger_local;
static Logger logger_recibido;

#define ENABLE_FAKE_QR_DEMO 1

static void lcd_display_product_cb(const Product *product)
{
    if (product == NULL) {
        return;
    }

    ESP_LOGI("LCD_SIM", "Callback LCD producto -> ID:%s | Nombre:%s | Stock:%lu",
             product->id,
             product->name,
             (unsigned long)product->stock);
}

static void lcd_display_message_cb(const char *title, const char *message)
{
    ESP_LOGI("LCD_SIM", "Callback LCD mensaje -> %s | %s",
             title != NULL ? title : "",
             message != NULL ? message : "");
}

static bool mqtt_publish_qr_cb(const Product *product)
{
    if (product == NULL) {
        return false;
    }

    return procesar_y_publicar(*product); // este ya devuelve true si sale bien.
}

static bool mqtt_publish_error_cb(const char *message)
{
   return procesar_y_publicar_error(message != NULL ? message : "Error desconocido");
}

static void post_touch_event(EventType event)
{
    if (!fsm_post_event(event)) {
        ESP_LOGW(TAG, "No se pudo enviar evento touch fake: %d", event);
    }
}

#if ENABLE_FAKE_QR_DEMO
static void fake_qr_demo_task(void *pvParameters)
{
    (void)pvParameters;

    vTaskDelay(pdMS_TO_TICKS(25000));

    ESP_LOGI(TAG, "[FAKE_CAMERA] QR valido: PROD001/Cigarros");
    fsm_on_qr_detected("PROD001", "Cigarros");

    vTaskDelay(pdMS_TO_TICKS(20000));
    ESP_LOGI(TAG, "[FAKE_TOUCH] Confirmar que se quiere agregar PROD001");
    post_touch_event(EV_BTN_CONFIRM);

    vTaskDelay(pdMS_TO_TICKS(1000));
    ESP_LOGI(TAG, "[FAKE_TOUCH] Subir cantidad a 2");
    post_touch_event(EV_BTN_UP);

    vTaskDelay(pdMS_TO_TICKS(1000));
    ESP_LOGI(TAG, "[FAKE_TOUCH] Subir cantidad a 3");
    post_touch_event(EV_BTN_UP);

    vTaskDelay(pdMS_TO_TICKS(1000));
    ESP_LOGI(TAG, "[FAKE_TOUCH] Confirmar cantidad seleccionada");
    post_touch_event(EV_BTN_CONFIRM);

    vTaskDelay(pdMS_TO_TICKS(7000));
    ESP_LOGI(TAG, "[FAKE_CAMERA] QR no registrado: PROD999/Galletas");
    fsm_on_qr_detected("PROD999", "Galletas");

    vTaskDelay(pdMS_TO_TICKS(7000));
    ESP_LOGI(TAG, "[FAKE_CAMERA] QR invalido");
    fsm_on_qr_invalid("No se pudo decodificar QR");

    vTaskDelete(NULL);
}
#endif

void fsm_task(void *pvParameters)
{
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

static void load_demo_catalog(void)
{
    Product product;

    if (product_db_upsert_product("PROD001", "Cigarros", 0, &product)) {
        ESP_LOGI(TAG, "Producto demo cargado -> ID:%s | Nombre:%s | Stock:%lu",
                 product.id,
                 product.name,
                 (unsigned long)product.stock);
    }

    if (product_db_upsert_product("PROD002", "Brownies", 0, &product)) {
        ESP_LOGI(TAG, "Producto demo cargado -> ID:%s | Nombre:%s | Stock:%lu",
                 product.id,
                 product.name,
                 (unsigned long)product.stock);
    }
}

void app_main(void)
{
    fsm_event_queue = xQueueCreate(20, sizeof(EventType));
    if (fsm_event_queue == NULL) {
        ESP_LOGE(TAG, "CRITICAL: Failed to create FSM Event Queue");
        return;
    }

    FsmCallbacks callbacks = {
        .display_product = lcd_display_product_cb,
        .display_message = lcd_display_message_cb,
        .publish_qr = mqtt_publish_qr_cb,
        .publish_error = mqtt_publish_error_cb,
    };

    product_db_init();
    load_demo_catalog();

    /* Los tests usan solo la tabla de transiciones. No registramos callbacks reales hasta terminar. */
//    fsm_init();
//    fsm_run_transition_tests();

    fsm_init();
    fsm_register_callbacks(&callbacks);
    fsm_set_auto_events_enabled(true);

    xTaskCreate(&fsm_task, "FSM_TASK", 4096, NULL, 1, NULL);
    button_int_config();

    rgb_led_init();

    ESP_ERROR_CHECK(nvs_storage_init());
    ESP_ERROR_CHECK(wifi_manager_init());
    ESP_ERROR_CHECK(http_handler_start());

    ESP_LOGI(TAG, "Conectate a la red %s", wifi_manager_get_ap_ssid());
    ESP_LOGI(TAG, "Abre en el navegador: http://192.168.4.1");

    logger_init(&logger_local, "local");
    logger_init(&logger_recibido, "recibido");
    mqtt_handler_set_loggers(&logger_local, &logger_recibido);

    init_time();
    iniciar_mqtt();

#if ENABLE_FAKE_QR_DEMO
    xTaskCreate(&fake_qr_demo_task, "FAKE_QR_DEMO", 4096, NULL, 1, NULL);
#endif

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

