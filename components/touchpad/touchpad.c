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

static const char *TAG = "TOUCHPAD";

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

    for (int i = 0; i < TOUCHPAD_NUM_BUTTONS; i++) {
        uint32_t benchmark[TOUCH_SAMPLE_CFG_NUM];
        memset(benchmark, 0, sizeof(benchmark));

        ESP_ERROR_CHECK(touch_channel_read_data(
            s_chan_handle[i], TOUCH_CHAN_DATA_TYPE_BENCHMARK, benchmark)); // lee el benchmark

        ESP_LOGI(TAG, "[touchpad] CH %2d ->", s_channel_id[i]);

        touch_channel_config_t chan_cfg = {
            .active_thresh    = {0},
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
}

/* ─── Detecta cuando se apreta un boton ───────────────────────────────────────────────────────── */

static bool touchpad_is_pressed(uint8_t button_index)
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

/* ─── Task ───────────────────────────────────────────────────────── */

static void touchpad_task(void *pvParameters){  // Task que hace polling continuo de los botones y envía eventos a la cola de FSM
    (void)pvParameters;

    bool was_pressed[TOUCHPAD_CHANNEL_NUM] = {false};   // guarda el estado anterior de cada botón
    TickType_t last_wake_time = xTaskGetTickCount();    // para usar vTaskDelayUntil() y mantener un periodo constante

    ESP_LOGI(TAG, "touchpad_task iniciada");

    while (1){
        for (uint8_t i = 0; i < TOUCHPAD_CHANNEL_NUM; i++){ // por cada botón
            bool pressed = touchpad_is_pressed(i);  // chequea si el botón está presionado

            if (pressed && !was_pressed[i]){    // si el botón acaba de ser presionado (flanco ascendente)
                EventType event = event_map[i];     // obtiene el evento correspondiente al botón
                if (xQueueSend(fsm_event_queue, &event, pdMS_TO_TICKS(TOUCHPAD_QUEUE_SEND_TIMEOUT_MS)) != 1){   // intenta enviar el evento a la cola de FSM
                    ESP_LOGW(TAG, "No se pudo enviar evento %d a la cola de FSM", button_names[i]); // si falla, loguea un warning
                } else {
                    ESP_LOGI(TAG, "Evento %d enviado a la cola de FSM", button_names[i]);   // si se envía correctamente, loguea un info
                }
            }
            was_pressed[i] = pressed;   // actualiza el estado anterior del botón
        }
        vTaskDelayUntil(&last_wake_time,     pdMS_TO_TICKS(TOUCHPAD_POLL_PERIOD_MS));   // espera hasta el siguiente periodo de polling
    }
}

/* ─── API pública ────────────────────────────────────────────────── */

void touchpad_init(void)
{
    if (s_init_done) {
        ESP_LOGW(TAG, "touchpad_init() llamado mas de una vez; se ignora");
        return;
    }

    touch_sensor_sample_config_t sample_cfg[TOUCH_SAMPLE_CFG_NUM] = {
        TOUCH_SENSOR_V2_DEFAULT_SAMPLE_CONFIG(
            500,
            TOUCH_VOLT_LIM_L_0V5,
            TOUCH_VOLT_LIM_H_2V7
        ),
    };

    touch_sensor_config_t sens_cfg =
        TOUCH_SENSOR_DEFAULT_BASIC_CONFIG(TOUCH_SAMPLE_CFG_NUM, sample_cfg);

    ESP_ERROR_CHECK(touch_sensor_new_controller(&sens_cfg, &s_sens_handle));

    touch_channel_config_t chan_cfg = {
        .active_thresh    = {1},
        .charge_speed     = TOUCH_CHARGE_SPEED_7,
        .init_charge_volt = TOUCH_INIT_CHARGE_VOLT_DEFAULT,
    };

    for (int i = 0; i < TOUCHPAD_CHANNEL_NUM; i++) {    // registra cada canal con la configuración inicial
        ESP_ERROR_CHECK(touch_sensor_new_channel(
            s_sens_handle,
            s_channel_id[i],
            &chan_cfg,
            &s_chan_handle[i]
        ));

        touch_chan_info_t info = {0};
        ESP_ERROR_CHECK(touch_sensor_get_channel_info(s_chan_handle[i], &info));
        ESP_LOGI(TAG, "[touchpad] CH %2d habilitado en GPIO%d\n",
               s_channel_id[i], info.chan_gpio);
    }

    ESP_LOGI(TAG, "=================================");

    /* 4. Filtro */  // reduce el ruido de la señal, configuado con los valores por defecto de la macro oficial
    touch_sensor_filter_config_t filter_cfg = TOUCH_SENSOR_DEFAULT_FILTER_CONFIG();
    ESP_ERROR_CHECK(touch_sensor_config_filter(s_sens_handle, &filter_cfg));

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
