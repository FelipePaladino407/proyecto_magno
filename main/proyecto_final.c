#include "freertos/idf_additions.h"
#include "freertos/projdefs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "device_role.h"
#include "qr_handler.h"
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
#include "pending_queue.h"
#include "touchpad.h"
#include "lcd_port.h"
#include "lcd_manager.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

static const char *TAG = "MAIN";

/*
 * En rol CAM queda NULL a proposito: la placa de camara no levanta la FSM
 * principal ni debe recibir eventos de touch/LCD.
 */
QueueHandle_t fsm_event_queue = NULL;

static Logger logger_local;
static Logger logger_recibido;

/*
 * Demo mixta para la placa LCD:
 * - La camara se simula con entradas fake.
 * - El touch NO se simula: confirmar, subir, bajar y cancelar se hacen
 *   con los botones touch reales.
 * - La LCD es real: la FSM llama a lcd_manager mediante callbacks.
 */
#define ENABLE_FAKE_QR_DEMO 1

#if DEVICE_IS_CAM
static void safe_copy_main(char *dest, const char *src, size_t dest_size)
{
    if (dest == NULL || dest_size == 0) {
        return;
    }

    if (src == NULL) {
        dest[0] = '\0';
        return;
    }

    strncpy(dest, src, dest_size - 1);
    dest[dest_size - 1] = '\0';
}
#endif

#if DEVICE_IS_LCD
static void lcd_show_waiting_cb(void)
{
    lcd_show_waiting();
}

static void lcd_show_product_prompt_cb(const Product *product, uint32_t add_amount)
{
    lcd_show_product(product, (int)add_amount);
}

static void lcd_show_quantity_selection_cb(const Product *product, uint32_t add_amount)
{
    lcd_show_product(product, (int)add_amount);
}

static void lcd_show_success_cb(const Product *product)
{
    lcd_show_added(product);
}

static void lcd_show_cancelled_cb(void)
{
    lcd_show_cancelled();
}

static void lcd_show_error_cb(const char *message)
{
    lcd_show_error(message != NULL ? message : "Error desconocido");
}

static bool mqtt_publish_qr_cb(const Product *product)
{
    if (product == NULL) {
        return false;
    }

    /*
     * Adapter entre FSM y mqtt_handler.
     * La FSM llama a este callback cuando ya actualizo el stock local.
     * MQTT decide internamente si publica o si informa fallo para guardar pending.
     */
    return procesar_y_publicar(*product);
}

static bool mqtt_publish_error_cb(const char *message)
{
    return procesar_y_publicar_error(message != NULL ? message : "Error desconocido");
}

static bool mqtt_store_pending_qr_cb(const Product *product)
{
    if (product == NULL) {
        return false;
    }

    return mqtt_handler_store_pending(*product);
}

static bool mqtt_flush_pending_cb(void)
{
    return mqtt_handler_flush_pending();
}
#endif

static void network_services_task(void *pvParameters)
{
    (void)pvParameters;

    wifi_manager_status_t status;

    while (true) {
        wifi_manager_get_status(&status);

        if (status.connected) {
            ESP_LOGI(TAG, "WiFi listo con IP %s. Inicializando NTP y MQTT...", status.ip_address);
            init_time();
            iniciar_mqtt();
            vTaskDelete(NULL);
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

#if DEVICE_IS_LCD
static void post_touch_event(EventType event)
{
    if (!fsm_post_event(event)) {
        ESP_LOGW(TAG, "No se pudo enviar evento touch a la FSM: %d", event);
    }
}

#if ENABLE_FAKE_QR_DEMO
static void demo_wait_until_fsm_finishes_flow(void)
{
    /*
     * Damos tiempo a que la FSM procese el EV_QR_CAPTURED y salga de IDLE.
     * Si no hacemos esto, la task demo podria ver IDLE demasiado pronto y
     * disparar el siguiente QR antes de que arranque el flujo anterior.
     */
    for (int i = 0; i < 40; i++) {
        if (fsm_get_current_state() != STATE_IDLE) {
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }

    while (fsm_get_current_state() != STATE_IDLE) {
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    vTaskDelay(pdMS_TO_TICKS(3000));
}

static void fake_qr_demo_task(void *pvParameters)
{
    (void)pvParameters;

    vTaskDelay(pdMS_TO_TICKS(25000));

    ESP_LOGI(TAG, "[FAKE_CAMERA] QR valido: LAC-LEC-001/Leche Entera 1L");
    fsm_on_qr_detected("LAC-LEC-001", "Leche Entera 1L");

    ESP_LOGI(TAG, "[DEMO] Camara fake enviada. Ahora usen el touch real:");
    ESP_LOGI(TAG, "[DEMO] CONFIRM para aceptar producto, UP/DOWN para cantidad, CONFIRM para publicar, CANCEL para cancelar");

    demo_wait_until_fsm_finishes_flow();

    ESP_LOGI(TAG, "[FAKE_CAMERA] QR no registrado: PROD999/Galletas");
    fsm_on_qr_detected("PROD999", "Galletas");
    demo_wait_until_fsm_finishes_flow();

    ESP_LOGI(TAG, "[FAKE_CAMERA] QR invalido");
    fsm_on_qr_invalid("No se pudo decodificar QR");
    demo_wait_until_fsm_finishes_flow();

    ESP_LOGI(TAG, "[DEMO] Fin de camara fake");
    vTaskDelete(NULL);
}
#endif

static void fsm_task(void *pvParameters)
{
    (void)pvParameters;

    EventType incoming_event;
    ESP_LOGI(TAG, "FSM Task started");

    while (true) {
        if (xQueueReceive(fsm_event_queue, &incoming_event, portMAX_DELAY) == pdPASS) {
            ESP_LOGI(TAG, "EVENT RECEIVED: %d", incoming_event);
            fsm_execute_transition(incoming_event);
        }
    }
}

static void load_catalog(void)
{
    uint32_t loaded = product_db_load_default_catalog();

    ESP_LOGI(TAG,
             "Catalogo compartido cargado en product_db -> %lu/%d productos. Stocks persistidos conservados si existian.",
             (unsigned long)loaded,
             CATALOGO_SIZE);
}

static void app_lcd_init(void)
{
    ESP_LOGI(TAG, "Inicializando rol LCD + TOUCH");

    fsm_event_queue = xQueueCreate(20, sizeof(EventType));
    if (fsm_event_queue == NULL) {
        ESP_LOGE(TAG, "CRITICAL: Failed to create FSM Event Queue");
        return;
    }

    lcd_port_init();
    lcd_manager_init();

    FsmCallbacks callbacks = {
        .show_waiting = lcd_show_waiting_cb,
        .show_product_prompt = lcd_show_product_prompt_cb,
        .show_quantity_selection = lcd_show_quantity_selection_cb,
        .show_success = lcd_show_success_cb,
        .show_cancelled = lcd_show_cancelled_cb,
        .show_error = lcd_show_error_cb,
        .publish_qr = mqtt_publish_qr_cb,
        .publish_error = mqtt_publish_error_cb,
        .store_pending_qr = mqtt_store_pending_qr_cb,
        .flush_pending = mqtt_flush_pending_cb,
    };

    product_db_init();
    load_catalog();

    /* Los tests usan solo la tabla de transiciones. No registramos callbacks reales hasta terminar. */
//    fsm_init();
//    fsm_run_transition_tests();

    fsm_init();
    fsm_register_callbacks(&callbacks);
    fsm_set_auto_events_enabled(true);

    xTaskCreate(&fsm_task, "FSM_TASK", 4096, NULL, 1, NULL);

    /*
     * Si van a usar solo touchpad, conviene dejar los botones fisicos desactivados
     * para no generar eventos duplicados o confusos.
     */
//    button_int_config();

    touchpad_init();
    touchpad_start_task();

#if ENABLE_FAKE_QR_DEMO
    xTaskCreate(&fake_qr_demo_task, "FAKE_QR_DEMO", 4096, NULL, 1, NULL);
#endif
}
#endif // DEVICE_IS_LCD

#if DEVICE_IS_CAM
static void camera_on_qr_detected(const char *id, const char *product_name)
{
    Product product;
    memset(&product, 0, sizeof(product));

    safe_copy_main(product.id, id, sizeof(product.id));
    safe_copy_main(product.name, product_name, sizeof(product.name));
    product.stock = 0;

    if (product.id[0] == '\0') {
        ESP_LOGW(TAG, "QR detectado sin ID valido");
        (void)procesar_y_publicar_error("QR sin ID valido");
        return;
    }

    ESP_LOGI(TAG,
             "CAM QR -> id=%s | nombre=%s. Publicando scan MQTT o guardando pendiente",
             product.id,
             product.name);

    if (!procesar_y_publicar(product)) {
        if (!mqtt_handler_store_pending(product)) {
            ESP_LOGE(TAG, "No se pudo guardar scan pendiente de camara");
        }
    }
}

static void app_camera_init(void)
{
    ESP_LOGI(TAG, "Inicializando rol CAMARA QR");

    qr_handler_set_callback(camera_on_qr_detected);
    qr_handler_init();
}
#endif // DEVICE_IS_CAM

static void app_network_init(void)
{
    rgb_led_init();

    ESP_ERROR_CHECK(nvs_storage_init());
    pending_queue_init();
    ESP_ERROR_CHECK(wifi_manager_init());
    ESP_ERROR_CHECK(http_handler_start());

    ESP_LOGI(TAG, "Conectate a la red %s", wifi_manager_get_ap_ssid());
    ESP_LOGI(TAG, "Abre en el navegador: http://192.168.4.1");

    logger_init(&logger_local, "local");
    logger_init(&logger_recibido, "recibido");
    mqtt_handler_set_loggers(&logger_local, &logger_recibido);

    xTaskCreate(&network_services_task, "NETWORK_SERVICES", 4096, NULL, 1, NULL);
}

void app_main(void)
{
    ESP_LOGI(TAG, "Arrancando Proyecto Magno con DEVICE_ROLE=%d", DEVICE_ROLE);

#if DEVICE_IS_LCD
    /*
     * En LCD primero se levanta la FSM para que los eventos WiFi/MQTT no
     * lleguen antes de que exista fsm_event_queue.
     */
    app_lcd_init();
    app_network_init();

#elif DEVICE_IS_CAM
    /*
     * En CAM primero se inicializa NVS/pending_queue/WiFi; asi cualquier QR
     * escaneado antes de MQTT queda persistido correctamente.
     */
    app_network_init();
    app_camera_init();
#else
    ESP_LOGE(TAG, "DEVICE_ROLE invalido: %d", DEVICE_ROLE);
#endif

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
