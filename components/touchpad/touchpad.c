#include "touchpad.h"

#include <stdio.h>
#include <inttypes.h>
#include <string.h>

#include "driver/touch_sens.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_rom_sys.h"

static const char *TAG = "TOUCHPAD";

/* ─── Configuración ──────────────────────────────────────────────── */

#define TOUCHPAD_INIT_SCAN_TIMES 3
#define TOUCHPAD_THRESH_RATIO    0.05f

/*
 * Botones lógicos:
 * 0 -> UP
 * 1 -> CONFIRM
 * 2 -> DOWN
 * 3 -> CANCEL
 *
 * Canales físicos del ESP32-S2 usados:
 * channel 1, 2, 3 y 5.
 *
 * Según comentaron, GPIO 6 y 11 dan conflicto con el LCD,
 * por eso no se usan.
 */
static const int s_channel_id[TOUCHPAD_NUM_BUTTONS] = {1, 2, 3, 5};

/* ─── Estado interno ─────────────────────────────────────────────── */

static touch_sensor_handle_t  s_sens_handle = NULL;
static touch_channel_handle_t s_chan_handle[TOUCHPAD_NUM_BUTTONS];
static uint32_t               s_threshold[TOUCHPAD_NUM_BUTTONS][TOUCH_SAMPLE_CFG_NUM];

static bool s_initialized = false;
static bool s_was_pressed[TOUCHPAD_NUM_BUTTONS] = {false};

/* ─── Escaneo inicial y calibración ─────────────────────────────── */

static void touchpad_initial_scanning(void)
{
    ESP_ERROR_CHECK(touch_sensor_enable(s_sens_handle));

    for (int i = 0; i < TOUCHPAD_INIT_SCAN_TIMES; i++) {
        ESP_ERROR_CHECK(touch_sensor_trigger_oneshot_scanning(s_sens_handle, 2000));
    }

    ESP_ERROR_CHECK(touch_sensor_disable(s_sens_handle));

    ESP_LOGI(TAG, "Benchmarks y umbrales iniciales:");

    for (int i = 0; i < TOUCHPAD_NUM_BUTTONS; i++) {
        uint32_t benchmark[TOUCH_SAMPLE_CFG_NUM];
        memset(benchmark, 0, sizeof(benchmark));

        ESP_ERROR_CHECK(touch_channel_read_data(
            s_chan_handle[i],
            TOUCH_CHAN_DATA_TYPE_BENCHMARK,
            benchmark
        ));

        touch_channel_config_t chan_cfg = {
            .active_thresh    = {0},
            .charge_speed     = TOUCH_CHARGE_SPEED_7,
            .init_charge_volt = TOUCH_INIT_CHARGE_VOLT_DEFAULT,
        };

        for (int j = 0; j < TOUCH_SAMPLE_CFG_NUM; j++) {
            uint32_t thresh = (uint32_t)(benchmark[j] * TOUCHPAD_THRESH_RATIO);

            if (thresh == 0) {
                thresh = 1;
            }

            chan_cfg.active_thresh[j] = thresh;
            s_threshold[i][j] = thresh;

            ESP_LOGI(TAG,
                     "Boton logico %d | CH %d | muestra %d | benchmark=%" PRIu32 " | threshold=%" PRIu32,
                     i,
                     s_channel_id[i],
                     j,
                     benchmark[j],
                     thresh);
        }

        ESP_ERROR_CHECK(touch_sensor_reconfig_channel(s_chan_handle[i], &chan_cfg));
    }
}

/* ─── API pública ────────────────────────────────────────────────── */

void touchpad_init(void)
{
    if (s_initialized) {
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

    for (int i = 0; i < TOUCHPAD_NUM_BUTTONS; i++) {
        ESP_ERROR_CHECK(touch_sensor_new_channel(
            s_sens_handle,
            s_channel_id[i],
            &chan_cfg,
            &s_chan_handle[i]
        ));

        touch_chan_info_t info = {0};
        ESP_ERROR_CHECK(touch_sensor_get_channel_info(s_chan_handle[i], &info));

        ESP_LOGI(TAG,
                 "Boton logico %d -> TOUCH CH %d habilitado en GPIO%d",
                 i,
                 s_channel_id[i],
                 info.chan_gpio);
    }

    touch_sensor_filter_config_t filter_cfg = TOUCH_SENSOR_DEFAULT_FILTER_CONFIG();
    ESP_ERROR_CHECK(touch_sensor_config_filter(s_sens_handle, &filter_cfg));

    touchpad_initial_scanning();

    ESP_ERROR_CHECK(touch_sensor_enable(s_sens_handle));
    ESP_ERROR_CHECK(touch_sensor_start_continuous_scanning(s_sens_handle));

    memset(s_was_pressed, 0, sizeof(s_was_pressed));
    s_initialized = true;

    ESP_LOGI(TAG, "Touchpad listo. Canales activos: %d, %d, %d, %d",
             s_channel_id[0],
             s_channel_id[1],
             s_channel_id[2],
             s_channel_id[3]);
}

bool touchpad_is_pressed(uint8_t button_index)
{
    if (!s_initialized) {
        return false;
    }

    if (button_index >= TOUCHPAD_NUM_BUTTONS) {
        return false;
    }

    uint32_t smooth[TOUCH_SAMPLE_CFG_NUM];
    memset(smooth, 0, sizeof(smooth));

    if (touch_channel_read_data(
            s_chan_handle[button_index],
            TOUCH_CHAN_DATA_TYPE_SMOOTH,
            smooth
        ) != ESP_OK) {
        return false;
    }

    uint32_t benchmark[TOUCH_SAMPLE_CFG_NUM];
    memset(benchmark, 0, sizeof(benchmark));

    if (touch_channel_read_data(
            s_chan_handle[button_index],
            TOUCH_CHAN_DATA_TYPE_BENCHMARK,
            benchmark
        ) != ESP_OK) {
        return false;
    }

    /*
     * Usamos diferencia absoluta porque, según la calibración y el hardware,
     * la señal puede subir o bajar al tocar.
     */
    for (int j = 0; j < TOUCH_SAMPLE_CFG_NUM; j++) {
        uint32_t delta = smooth[j] > benchmark[j]
                       ? smooth[j] - benchmark[j]
                       : benchmark[j] - smooth[j];

        if (delta >= s_threshold[button_index][j]) {
            return true;
        }
    }

    return false;
}

bool touchpad_get_pressed_button(touchpad_button_t *button)
{
    if (button == NULL) {
        return false;
    }

    if (!s_initialized) {
        return false;
    }

    bool found = false;
    touchpad_button_t detected_button = TOUCHPAD_BUTTON_UP;

    for (uint8_t i = 0; i < TOUCHPAD_NUM_BUTTONS; i++) {
        bool pressed = touchpad_is_pressed(i);

        /*
         * Flanco ascendente:
         * antes no estaba presionado, ahora sí.
         */
        if (pressed && !s_was_pressed[i] && !found) {
            detected_button = (touchpad_button_t)i;
            found = true;
        }

        s_was_pressed[i] = pressed;
    }

    if (found) {
        *button = detected_button;
        return true;
    }

    return false;
}

const char *touchpad_button_name(touchpad_button_t button)
{
    switch (button) {
        case TOUCHPAD_BUTTON_UP:
            return "UP";

        case TOUCHPAD_BUTTON_CONFIRM:
            return "CONFIRM";

        case TOUCHPAD_BUTTON_DOWN:
            return "DOWN";

        case TOUCHPAD_BUTTON_CANCEL:
            return "CANCEL";

        default:
            return "UNKNOWN";
    }
}
