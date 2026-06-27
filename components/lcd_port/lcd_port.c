/*
 * Puerto LCD + LVGL para la placa LCD.
 *
 * Este componente se encarga solamente de inicializar el hardware de pantalla,
 * conectar esp_lcd con LVGL y correr la tarea periodica de LVGL. La UI concreta
 * vive en lcd_manager como plantean los muchachos de http.
 */

#include "lcd_port.h"

#include <assert.h>
#include <stdint.h>
#include <sys/param.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_st7789.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lvgl.h"

static const char *TAG = "LCD_PORT";

/*
 * En el aporte del equipo LCD se uso SPI2_HOST. Dejamos esa decision en el
 * puerto, para que lcd_manager no dependa de pines ni perifericos.
 */
#define LCD_HOST SPI2_HOST

#define LCD_PIXEL_CLOCK_HZ      (20 * 1000 * 1000)
#define LCD_BK_LIGHT_ON_LEVEL   0
#define LCD_BK_LIGHT_OFF_LEVEL  (!LCD_BK_LIGHT_ON_LEVEL)

#define LCD_PIN_SCLK            15
#define LCD_PIN_MOSI            9
#define LCD_PIN_MISO            8
#define LCD_PIN_DC              13
#define LCD_PIN_RST             16
#define LCD_PIN_CS              11
#define LCD_PIN_BK_LIGHT        6

#define LCD_H_RES               240
#define LCD_V_RES               320
#define LCD_CMD_BITS            8
#define LCD_PARAM_BITS          8

#define LVGL_DRAW_BUF_LINES     20
#define LVGL_TICK_PERIOD_MS     2
#define LVGL_TASK_MAX_DELAY_MS  500
#define LVGL_TASK_MIN_DELAY_MS  10
#define LVGL_TASK_STACK_SIZE    (4 * 1024)
#define LVGL_TASK_PRIORITY      2

static SemaphoreHandle_t s_lvgl_mutex = NULL;
static bool s_initialized = false;

static bool lcd_notify_lvgl_flush_ready(esp_lcd_panel_io_handle_t panel_io,
                                        esp_lcd_panel_io_event_data_t *edata,
                                        void *user_ctx)
{
    (void)panel_io;
    (void)edata;

    lv_display_t *display = (lv_display_t *)user_ctx;
    lv_display_flush_ready(display);

    return false;
}

static void lcd_lvgl_port_update_callback(lv_display_t *display)
{
    esp_lcd_panel_handle_t panel_handle = lv_display_get_user_data(display);
    lv_display_rotation_t rotation = lv_display_get_rotation(display);

    switch (rotation) {
    case LV_DISPLAY_ROTATION_0:
        esp_lcd_panel_swap_xy(panel_handle, false);
        esp_lcd_panel_mirror(panel_handle, true, false);
        break;

    case LV_DISPLAY_ROTATION_90:
        esp_lcd_panel_swap_xy(panel_handle, true);
        esp_lcd_panel_mirror(panel_handle, true, true);
        break;

    case LV_DISPLAY_ROTATION_180:
        esp_lcd_panel_swap_xy(panel_handle, false);
        esp_lcd_panel_mirror(panel_handle, false, true);
        break;

    case LV_DISPLAY_ROTATION_270:
        esp_lcd_panel_swap_xy(panel_handle, true);
        esp_lcd_panel_mirror(panel_handle, false, false);
        break;

    default:
        break;
    }
}

static void lcd_lvgl_flush_cb(lv_display_t *display, const lv_area_t *area, uint8_t *px_map)
{
    lcd_lvgl_port_update_callback(display);

    esp_lcd_panel_handle_t panel_handle = lv_display_get_user_data(display);

    int offset_x1 = area->x1;
    int offset_x2 = area->x2;
    int offset_y1 = area->y1;
    int offset_y2 = area->y2;

    /* El ST7789 espera RGB565 big-endian. */
    lv_draw_sw_rgb565_swap(px_map,
                           (offset_x2 + 1 - offset_x1) *
                           (offset_y2 + 1 - offset_y1));

    esp_lcd_panel_draw_bitmap(panel_handle,
                              offset_x1,
                              offset_y1,
                              offset_x2 + 1,
                              offset_y2 + 1,
                              px_map);
}

static void lcd_increase_lvgl_tick(void *arg)
{
    (void)arg;
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

bool lcd_port_lock(uint32_t timeout_ms)
{
    if (s_lvgl_mutex == NULL) {
        return false;
    }

    TickType_t timeout_ticks = (timeout_ms == LCD_PORT_LOCK_FOREVER)
                                  ? portMAX_DELAY
                                  : pdMS_TO_TICKS(timeout_ms);

    return xSemaphoreTake(s_lvgl_mutex, timeout_ticks) == pdTRUE;
}

void lcd_port_unlock(void)
{
    if (s_lvgl_mutex != NULL) {
        xSemaphoreGive(s_lvgl_mutex);
    }
}

bool lcd_port_is_initialized(void)
{
    return s_initialized;
}

static void lcd_lvgl_task(void *arg)
{
    (void)arg;

    ESP_LOGI(TAG, "Iniciando tarea LVGL");

    while (true) {
        uint32_t time_till_next_ms = LVGL_TASK_MIN_DELAY_MS;

        if (lcd_port_lock(LCD_PORT_LOCK_FOREVER)) {
            time_till_next_ms = lv_timer_handler();
            lcd_port_unlock();
        }

        time_till_next_ms = MAX(time_till_next_ms, LVGL_TASK_MIN_DELAY_MS);
        time_till_next_ms = MIN(time_till_next_ms, LVGL_TASK_MAX_DELAY_MS);

        vTaskDelay(pdMS_TO_TICKS(time_till_next_ms));
    }
}

void lcd_port_init(void)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "lcd_port_init() llamado mas de una vez");
        return;
    }

    s_lvgl_mutex = xSemaphoreCreateMutex();
    if (s_lvgl_mutex == NULL) {
        ESP_LOGE(TAG, "No se pudo crear el mutex de LVGL");
        return;
    }

    ESP_LOGI(TAG, "Apagando backlight");
    gpio_config_t backlight_config = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << LCD_PIN_BK_LIGHT,
    };
    ESP_ERROR_CHECK(gpio_config(&backlight_config));
    gpio_set_level(LCD_PIN_BK_LIGHT, LCD_BK_LIGHT_OFF_LEVEL);

    ESP_LOGI(TAG, "Inicializando bus SPI de LCD");
    spi_bus_config_t bus_config = {
        .sclk_io_num = LCD_PIN_SCLK,
        .mosi_io_num = LCD_PIN_MOSI,
        .miso_io_num = LCD_PIN_MISO,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .max_transfer_sz = LCD_H_RES * LVGL_DRAW_BUF_LINES * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &bus_config, SPI_DMA_CH_AUTO));

    ESP_LOGI(TAG, "Creando IO SPI para panel LCD");
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = LCD_PIN_DC,
        .cs_gpio_num = LCD_PIN_CS,
        .pclk_hz = LCD_PIXEL_CLOCK_HZ,
        .lcd_cmd_bits = LCD_CMD_BITS,
        .lcd_param_bits = LCD_PARAM_BITS,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST,
                                             &io_config,
                                             &io_handle));

    ESP_LOGI(TAG, "Inicializando controlador ST7789");
    esp_lcd_panel_handle_t panel_handle = NULL;
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = LCD_PIN_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle, &panel_config, &panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_handle, true, false));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

    ESP_LOGI(TAG, "Inicializando LVGL");
    lv_init();

    lv_display_t *display = lv_display_create(LCD_H_RES, LCD_V_RES);
    assert(display != NULL);

    size_t draw_buffer_size = LCD_H_RES * LVGL_DRAW_BUF_LINES * sizeof(lv_color16_t);
    void *draw_buffer = spi_bus_dma_memory_alloc(LCD_HOST, draw_buffer_size, 0);
    assert(draw_buffer != NULL);

    lv_display_set_buffers(display,
                           draw_buffer,
                           NULL,
                           draw_buffer_size,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_user_data(display, panel_handle);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(display, lcd_lvgl_flush_cb);

    ESP_LOGI(TAG, "Instalando tick periodico de LVGL");
    const esp_timer_create_args_t lvgl_tick_timer_args = {
        .callback = lcd_increase_lvgl_tick,
        .name = "lvgl_tick",
    };
    esp_timer_handle_t lvgl_tick_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_tick_timer, LVGL_TICK_PERIOD_MS * 1000));

    ESP_LOGI(TAG, "Registrando callback de flush listo");
    const esp_lcd_panel_io_callbacks_t callbacks = {
        .on_color_trans_done = lcd_notify_lvgl_flush_ready,
    };
    ESP_ERROR_CHECK(esp_lcd_panel_io_register_event_callbacks(io_handle, &callbacks, display));

    ESP_LOGI(TAG, "Encendiendo backlight");
    gpio_set_level(LCD_PIN_BK_LIGHT, LCD_BK_LIGHT_ON_LEVEL);

    s_initialized = true;

    ESP_LOGI(TAG, "Creando tarea LVGL");
    xTaskCreate(lcd_lvgl_task,
                "LVGL",
                LVGL_TASK_STACK_SIZE,
                NULL,
                LVGL_TASK_PRIORITY,
                NULL);
}

