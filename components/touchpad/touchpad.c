#include "touchpad.h"
#include "esp_log.h"
#include <inttypes.h>
#include <string.h>

#include "driver/touch_sens.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "fsm.h"

static const char *TAG = "TOUCHPAD";

/* ─── Configuración ──────────────────────────────────────────────── */

#define TOUCHPAD_CHANNEL_NUM     4      // hay 4 botones táctiles, gpio 6 y 11 dan conflicto con el LCD
#define TOUCHPAD_INIT_SCAN_TIMES 3      // escanea 3 veces al arrancar para calibrar
#define TOUCHPAD_THRESH_RATIO    0.05f // si la señal varía 5% del umbral, se considera "tocado" (es relativo)

static const int s_channel_id[] = { 1, 2, 3, 5 }; // canales físicos del hardware, gpio 1, 2, 3 y 5

/* ─── Estado interno ─────────────────────────────────────────────── */

static touch_sensor_handle_t  s_sens_handle = NULL; // handle del sensor
static touch_channel_handle_t s_chan_handle[TOUCHPAD_CHANNEL_NUM];  // handles de los canales
static uint32_t               s_threshold[TOUCHPAD_CHANNEL_NUM][TOUCH_SAMPLE_CFG_NUM];
static bool                   s_init_done = false;   // flag que indica si el touchpad fue inicializado
static TaskHandle_t          s_task_handle = NULL;   // handle de la task

/* ─── Configuración de la task ───────────────────────────────────── */

#define TOUCHPAD_TASK_STACK_SIZE 4096 
#define TOUCHPAD_TASK_PRIORITY   1
#define TOUCHPAD_POLL_PERIOD_MS    50   // frecuencia de polling de los botones
#define TOUCHPAD_QUEUE_SEND_TIMEOUT_MS 100  // tiempo de espera despues de enviar un evento a fsm_event_queue

extern QueueHandle_t fsm_event_queue;  // cola de eventos de la FSM, definida en fsm.c

static const EventType event_map[TOUCHPAD_CHANNEL_NUM] = {
    EV_BTN_UP,      /*  Boton VOL_UP     */
    EV_BTN_CONFIRM, /*  Boton PLAY/PAUSE */
    EV_BTN_DOWN,    /*  Boton VOL_DOWN   */
    EV_BTN_CANCEL,  /*  Boton RECORD     */
};

static const char *button_names[TOUCHPAD_CHANNEL_NUM] = {
    "VOL_UP (contador +)",
    "PLAY/PAUSE (confirmar)",
    "VOL_DOWN (contador -)",
    "RECORD (cancelar)",
};

/* ─── Escaneo inicial y calibración ──────────────────────────────── */

static void touchpad_initial_scanning(void)
{
    ESP_ERROR_CHECK(touch_sensor_enable(s_sens_handle)); // habilita el sensor de los botones

    for (int i = 0; i < TOUCHPAD_INIT_SCAN_TIMES; i++) {
        ESP_ERROR_CHECK(touch_sensor_trigger_oneshot_scanning(s_sens_handle, 2000)); // escaneo de 2 segundos
    }

    ESP_ERROR_CHECK(touch_sensor_disable(s_sens_handle)); // deshabilita el sensor de los botones

    for (int i = 0; i < TOUCHPAD_CHANNEL_NUM; i++) { // por cada canal

        uint32_t benchmark[TOUCH_SAMPLE_CFG_NUM];
        memset(benchmark, 0, sizeof(benchmark));

        /* ESP32-S2 es hw_ver2 → siempre soporta benchmark */
        ESP_ERROR_CHECK(touch_channel_read_data(
            s_chan_handle[i], TOUCH_CHAN_DATA_TYPE_BENCHMARK, benchmark)); // lee el benchmark

        ESP_LOGI(TAG, "[touchpad] CH %2d ->", s_channel_id[i]);

        /* Calcular umbral = benchmark * ratio, y reconfigurarlo */
        touch_channel_config_t chan_cfg = {
            .active_thresh    = {0},          /* se rellena abajo         */
            .charge_speed     = TOUCH_CHARGE_SPEED_7,
            .init_charge_volt = TOUCH_INIT_CHARGE_VOLT_DEFAULT,
        };

        for (int j = 0; j < TOUCH_SAMPLE_CFG_NUM; j++) {
            uint32_t thresh = (uint32_t)(benchmark[j] * TOUCHPAD_THRESH_RATIO);
            if (thresh == 0) thresh = 1;      /* mínimo 1 para no ignorar */
            chan_cfg.active_thresh[j] = thresh;  //aplica el umbral calculado
            s_threshold[i][j]         = thresh;  //guarda el umbral para compararlo en touchpad_is_pressed()
            ESP_LOGI(TAG, "  [%d] bm=%" PRIu32 " thr=%" PRIu32, j, benchmark[j], thresh);
        }

        ESP_LOGI(TAG, "\n");
        ESP_ERROR_CHECK(touch_sensor_reconfig_channel(s_chan_handle[i], &chan_cfg));
    }
}

/* ─── Task ───────────────────────────────────────────────────────── */

static void touchpad_task(void *pvParameters){
    (void)pvParameters;

    bool was_pressed[TOUCHPAD_CHANNEL_NUM] = {false};
    TickType_t last_wake_time = xTaskGetTickCount();

    ESP_LOGI(TAG, "touchpad_task iniciada");

    while (1){
        for (uint8_t i = 0; i < TOUCHPAD_CHANNEL_NUM; i++){
            bool pressed = touchpad_is_pressed(i);

            if (pressed && !was_pressed[i]){
                EventType event = event_map[i];
                if (xQueueSend(fsm_event_queue, &event, pdMS_TO_TICKS(TOUCHPAD_QUEUE_SEND_TIMEOUT_MS)) != 1){
                    ESP_LOGW(TAG, "No se pudo enviar evento %d a la cola de FSM", button_names[i]);
                } else {
                    ESP_LOGI(TAG, "Evento %d enviado a la cola de FSM", button_names[i]);
                }
            }
            was_pressed[i] = pressed;
        }
        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(TOUCHPAD_POLL_PERIOD_MS));
    }
}

/* ─── API pública ────────────────────────────────────────────────── */

void touchpad_init(void)
{
    /* 1. Configuracion de muestras
     *    TOUCH_SENSOR_V2_DEFAULT_SAMPLE_CONFIG(chg_times, volt_low, volt_high)
     *    es la macro oficial para ESP32-S2 / S3 (hw_ver2).
     *    TOUCH_SAMPLE_CFG_NUM == 1 en el ESP32-S2.                           */
    touch_sensor_sample_config_t sample_cfg[TOUCH_SAMPLE_CFG_NUM] = {
        TOUCH_SENSOR_V2_DEFAULT_SAMPLE_CONFIG(
            500,                      /* charge/discharge times (0-0xFFFF)  */
            TOUCH_VOLT_LIM_L_0V5,     /* voltaje bajo de referencia          */
            TOUCH_VOLT_LIM_H_2V7      /* voltaje alto de referencia          */
        ),
    };

    /* 2. Crear controlador */
    touch_sensor_config_t sens_cfg =
        TOUCH_SENSOR_DEFAULT_BASIC_CONFIG(TOUCH_SAMPLE_CFG_NUM, sample_cfg);

    ESP_ERROR_CHECK(touch_sensor_new_controller(&sens_cfg, &s_sens_handle));

    /* 3. Registrar canales con config inicial (umbral=1, se calibra luego) */
    touch_channel_config_t chan_cfg = {
        .active_thresh    = {1},
        .charge_speed     = TOUCH_CHARGE_SPEED_7,
        .init_charge_volt = TOUCH_INIT_CHARGE_VOLT_DEFAULT,
    };

    for (int i = 0; i < TOUCHPAD_CHANNEL_NUM; i++) {    // registra cada canal con la configuración inicial
        ESP_ERROR_CHECK(touch_sensor_new_channel(
            s_sens_handle, s_channel_id[i], &chan_cfg, &s_chan_handle[i]));

        touch_chan_info_t info = {};
        ESP_ERROR_CHECK(touch_sensor_get_channel_info(s_chan_handle[i], &info));
        ESP_LOGI(TAG, "[touchpad] CH %2d habilitado en GPIO%d\n",
               s_channel_id[i], info.chan_gpio);
    }

    ESP_LOGI(TAG, "=================================");

    /* 4. Filtro */  // reduce el ruido de la señal, configuado con los valores por defecto de la macro oficial
    touch_sensor_filter_config_t filter_cfg = TOUCH_SENSOR_DEFAULT_FILTER_CONFIG();
    ESP_ERROR_CHECK(touch_sensor_config_filter(s_sens_handle, &filter_cfg));

    /* 5. Escaneo inicial para calibrar umbrales reales */
    touchpad_initial_scanning();

    /* 6. Arrancar escaneo continuo (polling) */
    ESP_ERROR_CHECK(touch_sensor_enable(s_sens_handle)); // habilita el sensor
    ESP_ERROR_CHECK(touch_sensor_start_continuous_scanning(s_sens_handle)); // arranca el escaneo continuo

    s_init_done = true;

    ESP_LOGI(TAG, "[touchpad] Listo. Canales activos: ");
    for (int i = 0; i < TOUCHPAD_CHANNEL_NUM; i++) {
        ESP_LOGI(TAG, "%d%s", s_channel_id[i],
                 i < TOUCHPAD_CHANNEL_NUM - 1 ? ", " : "\n");
    }
}

bool touchpad_is_pressed(uint8_t button_index)
{
    if (!s_init_done || button_index >= TOUCHPAD_CHANNEL_NUM) {  // chequea si no está inicializado o el índice es inválido
        return false;                                            // si se cumple alguna, devuelve false
    }

    uint32_t smooth[TOUCH_SAMPLE_CFG_NUM];  // lee el valor filtrado de la señal
    memset(smooth, 0, sizeof(smooth));

    if (touch_channel_read_data(s_chan_handle[button_index],TOUCH_CHAN_DATA_TYPE_SMOOTH,smooth) != ESP_OK) {
        return false;
    }

    uint32_t benchmark[TOUCH_SAMPLE_CFG_NUM];   // lee el benchmark de la señal
        memset(benchmark, 0, sizeof(benchmark));

    if (touch_channel_read_data(s_chan_handle[button_index],TOUCH_CHAN_DATA_TYPE_BENCHMARK,benchmark) != ESP_OK) {
        return false;
    }

    /*
     * ESP32-S2 hw_ver2: se activa cuando (smooth - benchmark) >= umbral.
     * Basta que UNA muestra supere para considerar el canal presionado.
     */
    for (int j = 0; j < TOUCH_SAMPLE_CFG_NUM; j++) {
        if (smooth[j] > benchmark[j] && (smooth[j] - benchmark[j]) >= s_threshold[button_index][j]) {
            return true;
        }
    }

    return false;
}

void touchpad_start_task(void)
{
    if (!s_init_done) {
        ESP_LOGW(TAG, "touchpad_start_task: touchpad no inicializado");
        return;
    }

    if (fsm_event_queue == NULL) {
        ESP_LOGW(TAG, "touchpad_start_task: fsm_event_queue no inicializada");
        return;
    }

    if (s_task_handle != NULL) {
        ESP_LOGW(TAG, "touchpad_start_task: task ya iniciada");
        return;
    }

    xTaskCreate(&touchpad_task, "touchpad_task", TOUCHPAD_TASK_STACK_SIZE, NULL, TOUCHPAD_TASK_PRIORITY, &s_task_handle);

}