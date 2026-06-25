#include "lcd_manager.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"


#include "driver/gpio.h"
#include "driver/spi_master.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"

#include "shared.h"



#define LCD_HOST SPI3_HOST

#define LCD_PIN_MOSI GPIO_NUM_9
#define LCD_PIN_CLK  GPIO_NUM_15
#define LCD_PIN_CS   GPIO_NUM_11
#define LCD_PIN_DC   GPIO_NUM_13
#define LCD_PIN_RST  GPIO_NUM_16

#define LCD_H_RES 320
#define LCD_V_RES 240

// Se usa un framebuffer de media pantalla para no reservar
// 153600 bytes contiguos de RAM interna
// Cada pantalla se dibuja y transmite en dos mitades de 120 líneas.
#define LCD_BUFFER_V_RES 120

#define LCD_PIXEL_CLOCK_HZ (40 * 1000 * 1000)

#define LCD_CMD_BITS   8
#define LCD_PARAM_BITS 8

#define LCD_FRAME_PIXELS (LCD_H_RES * LCD_BUFFER_V_RES)
#define LCD_FRAME_BYTES  (LCD_FRAME_PIXELS * sizeof(uint16_t))

static const char *TAG = "KALUGA_LCD";

static esp_lcd_panel_handle_t lcd_panel = NULL;
static esp_lcd_panel_io_handle_t lcd_io = NULL;
static uint16_t *framebuffer = NULL;


static int framebuffer_y_origin = 0;



#define SWAP_BYTES_16(value)                                      \
    (uint16_t)((((uint16_t)(value) & 0x00FFU) << 8) |            \
               (((uint16_t)(value) & 0xFF00U) >> 8))

#define RGB565_RAW(r, g, b)                                       \
    (uint16_t)((((uint16_t)(r) & 0xF8U) << 8) |                  \
               (((uint16_t)(g) & 0xFCU) << 3) |                  \
               (((uint16_t)(b)) >> 3))

#define RGB565(r, g, b) SWAP_BYTES_16(RGB565_RAW((r), (g), (b)))

#define COLOR_BLACK   RGB565(0, 0, 0)
#define COLOR_WHITE   RGB565(255, 255, 255)
#define COLOR_RED     RGB565(255, 0, 0)
#define COLOR_GREEN   RGB565(0, 255, 0)
#define COLOR_BLUE    RGB565(0, 0, 255)
#define COLOR_YELLOW  RGB565(255, 255, 0)
#define COLOR_CYAN    RGB565(0, 255, 255)
#define COLOR_MAGENTA RGB565(255, 0, 255)
#define COLOR_ORANGE  RGB565(255, 128, 0)

//Framebuffer

static void framebuffer_fill(uint16_t color)
{
    if (framebuffer == NULL) {
        return;
    }

    for (int i = 0; i < LCD_FRAME_PIXELS; i++) {
        framebuffer[i] = color;
    }
}

static void framebuffer_draw_pixel(
    int x,
    int y,
    uint16_t color
)
{
    if (framebuffer == NULL) {
        return;
    }

    int local_y = y - framebuffer_y_origin;

    if (x < 0 || x >= LCD_H_RES ||
        local_y < 0 || local_y >= LCD_BUFFER_V_RES) {
        return;
    }

    framebuffer[(local_y * LCD_H_RES) + x] = color;
}

static void lcd_update_screen(void)
{
    if (lcd_panel == NULL || framebuffer == NULL) {
        ESP_LOGE(
            TAG,
            "La pantalla o el framebuffer no están inicializados"
        );

        return;
    }

    ESP_ERROR_CHECK(
        esp_lcd_panel_draw_bitmap(
            lcd_panel,
            0,
            framebuffer_y_origin,
            LCD_H_RES,
            framebuffer_y_origin + LCD_BUFFER_V_RES,
            framebuffer
        )
    );

    // esp_lcd envía los píxeles mediante DMA en segundo plano.
    // Esta llamada espera a que termine la transferencia antes de
    // volver a reutilizar el mismo framebuffer para la otra mitad.
    ESP_ERROR_CHECK(
        esp_lcd_panel_io_tx_param(
            lcd_io,
            -1,
            NULL,
            0
        )
    );
}

typedef void (*lcd_draw_function_t)(void *context);

static void lcd_render_full_screen(
    uint16_t background_color,
    lcd_draw_function_t draw_function,
    void *context
)
{
    if (framebuffer == NULL || lcd_panel == NULL || lcd_io == NULL) {
        ESP_LOGE(
            TAG,
            "La pantalla o el framebuffer no están inicializados"
        );

        return;
    }

    for (
        framebuffer_y_origin = 0;
        framebuffer_y_origin < LCD_V_RES;
        framebuffer_y_origin += LCD_BUFFER_V_RES
    ) {
        framebuffer_fill(background_color);
        draw_function(context);
        lcd_update_screen();
    }

    framebuffer_y_origin = 0;
}

// Fuente de 5x7 pixeles
// Cada número representa una fila de 5 píxeles. 
// Ejemplo:
// 01110 = .###.
// 10001 = #...#


static const uint8_t font_5x7[26][7] = {

    // A
    {
        0x0E,
        0x11,
        0x11,
        0x1F,
        0x11,
        0x11,
        0x11
    },

    // B 
    {
        0x1E,
        0x11,
        0x11,
        0x1E,
        0x11,
        0x11,
        0x1E
    },

    // C
    {
        0x0E,
        0x11,
        0x10,
        0x10,
        0x10,
        0x11,
        0x0E
    },

    // D
    {
        0x1E,
        0x11,
        0x11,
        0x11,
        0x11,
        0x11,
        0x1E
    },

    // E
    {
        0x1F,
        0x10,
        0x10,
        0x1E,
        0x10,
        0x10,
        0x1F
    },

    // F
    {
        0x1F,
        0x10,
        0x10,
        0x1E,
        0x10,
        0x10,
        0x10
    },

    // G
    {
        0x0E,
        0x11,
        0x10,
        0x17,
        0x11,
        0x11,
        0x0F
    },

    // H
    {
        0x11,
        0x11,
        0x11,
        0x1F,
        0x11,
        0x11,
        0x11
    },

    // I
    {
        0x1F,
        0x04,
        0x04,
        0x04,
        0x04,
        0x04,
        0x1F
    },

    // J
    {
        0x07,
        0x02,
        0x02,
        0x02,
        0x12,
        0x12,
        0x0C
    },

    // K
    {
        0x11,
        0x12,
        0x14,
        0x18,
        0x14,
        0x12,
        0x11
    },

    // L
    {
        0x10,
        0x10,
        0x10,
        0x10,
        0x10,
        0x10,
        0x1F
    },

    // M
    {
        0x11,
        0x1B,
        0x15,
        0x15,
        0x11,
        0x11,
        0x11
    },

    // N
    {
        0x11,
        0x19,
        0x15,
        0x15,
        0x13,
        0x11,
        0x11
    },

    // O
    {
        0x0E,
        0x11,
        0x11,
        0x11,
        0x11,
        0x11,
        0x0E
    },

    // P
    {
        0x1E,
        0x11,
        0x11,
        0x1E,
        0x10,
        0x10,
        0x10
    },

    // Q
    {
        0x0E,
        0x11,
        0x11,
        0x11,
        0x15,
        0x12,
        0x0D
    },

    // R
    {
        0x1E,
        0x11,
        0x11,
        0x1E,
        0x14,
        0x12,
        0x11
    },

    // S
    {
        0x0F,
        0x10,
        0x10,
        0x0E,
        0x01,
        0x01,
        0x1E
    },

    // T
    {
        0x1F,
        0x04,
        0x04,
        0x04,
        0x04,
        0x04,
        0x04
    },

    // U
    {
        0x11,
        0x11,
        0x11,
        0x11,
        0x11,
        0x11,
        0x0E
    },

    // V
    {
        0x11,
        0x11,
        0x11,
        0x11,
        0x11,
        0x0A,
        0x04
    },

    // W
    {
        0x11,
        0x11,
        0x11,
        0x15,
        0x15,
        0x15,
        0x0A
    },

    // X
    {
        0x11,
        0x11,
        0x0A,
        0x04,
        0x0A,
        0x11,
        0x11
    },

    // Y
    {
        0x11,
        0x11,
        0x0A,
        0x04,
        0x04,
        0x04,
        0x04
    },

    // Z
    {
        0x1F,
        0x01,
        0x02,
        0x04,
        0x08,
        0x10,
        0x1F
    }
};

//Espacio

static const uint8_t glyph_space[7] = {
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00
};

//Signo de pregunta por defecto si no reconoce caracter

static const uint8_t glyph_question[7] = {
    0x0E,
    0x11,
    0x01,
    0x02,
    0x04,
    0x00,
    0x04
};

// Ñ

static const uint8_t glyph_enye[7] = {
    0x0A,
    0x15,
    0x11,
    0x19,
    0x15,
    0x13,
    0x11
};

// Código interno para representar la Ñ

#define FONT_CHARACTER_ENYE 0x0100

//Dibujo de una letra

static const uint8_t *font_get_glyph(uint16_t character)
{
    //Convierte letras minusculas a mayusculas
    if (character >= 'a' && character <= 'z') {
        character = character - ('a' - 'A');
    }

    if (character >= 'A' && character <= 'Z') {
        return font_5x7[character - 'A'];
    }

    if (character == FONT_CHARACTER_ENYE) {
        return glyph_enye;
    }

    if (character == ' ') {
        return glyph_space;
    }

    return glyph_question;
}

// Texto UTF-8

static uint16_t font_read_next_character(
    const unsigned char **text_pointer
)
{
    const unsigned char *text = *text_pointer;

    if (*text == '\0') {
        return 0;
    }

    /*
     * Caracteres ASCII normales.
     */
    if (text[0] < 0x80) {
        uint16_t character = text[0];

        *text_pointer = text + 1;

        return character;
    }

    /*
     * Ñ mayúscula en UTF-8:
     * C3 91
     */
    if (text[0] == 0xC3 && text[1] == 0x91) {
        *text_pointer = text + 2;

        return FONT_CHARACTER_ENYE;
    }

    /*
     * ñ minúscula en UTF-8:
     * C3 B1
     */
    if (text[0] == 0xC3 && text[1] == 0xB1) {
        *text_pointer = text + 2;

        return FONT_CHARACTER_ENYE;
    }

    //Signo de pregunta para caracter no reconocido
    *text_pointer = text + 1;

    return '?';
}

//Dibujar caracter

static void framebuffer_draw_char(
    int x,
    int y,
    uint16_t character,
    int scale,
    uint16_t foreground_color,
    uint16_t background_color
)
{
    if (scale <= 0) {
        return;
    }

    const uint8_t *glyph = font_get_glyph(character);

    for (int glyph_y = 0; glyph_y < 7; glyph_y++) {

        for (int glyph_x = 0; glyph_x < 5; glyph_x++) {

            /*
             * La máscara revisa cada uno de los cinco bits.
             *
             * glyph_x = 0 revisa el bit 4.
             * glyph_x = 4 revisa el bit 0.
             */
            bool pixel_is_on =
                (glyph[glyph_y] & (1U << (4 - glyph_x))) != 0;

            uint16_t pixel_color =
                pixel_is_on
                    ? foreground_color
                    : background_color;

            /*
             * Agranda cada píxel según el valor de scale.
             */
            for (int scale_y = 0; scale_y < scale; scale_y++) {

                for (int scale_x = 0; scale_x < scale; scale_x++) {

                    int pixel_x =
                        x + (glyph_x * scale) + scale_x;

                    int pixel_y =
                        y + (glyph_y * scale) + scale_y;

                    framebuffer_draw_pixel(
                        pixel_x,
                        pixel_y,
                        pixel_color
                    );
                }
            }
        }
    }
}

//Dibujar texto

static void framebuffer_draw_text(
    int x,
    int y,
    const char *text,
    int scale,
    uint16_t foreground_color,
    uint16_t background_color
)
{
    if (text == NULL || scale <= 0) {
        return;
    }

    int initial_x = x;
    int cursor_x = x;
    int cursor_y = y;

    const unsigned char *current_character =
        (const unsigned char *)text;

    while (*current_character != '\0') {

        uint16_t character =
            font_read_next_character(&current_character);

        /*
         * Permite usar \n para cambiar de línea.
         */
        if (character == '\n') {
            cursor_x = initial_x;
            cursor_y += 8 * scale;

            continue;
        }

        framebuffer_draw_char(
            cursor_x,
            cursor_y,
            character,
            scale,
            foreground_color,
            background_color
        );

        /*
         * Cinco columnas de la letra y una columna
         * de separación.
         */
        cursor_x += 6 * scale;
    }
}

//Inicialización de la pantalla

static void lcd_init(void)
{
    ESP_LOGI(TAG, "Inicializando bus SPI");

    spi_bus_config_t bus_config = {
        .sclk_io_num = LCD_PIN_CLK,
        .mosi_io_num = LCD_PIN_MOSI,
        .miso_io_num = GPIO_NUM_NC,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .max_transfer_sz = LCD_FRAME_BYTES
    };

    ESP_ERROR_CHECK(
        spi_bus_initialize(
            LCD_HOST,
            &bus_config,
            SPI_DMA_CH_AUTO
        )
    );

    ESP_LOGI(TAG, "Creando interfaz SPI para la LCD");

    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = LCD_PIN_DC,
        .cs_gpio_num = LCD_PIN_CS,
        .pclk_hz = LCD_PIXEL_CLOCK_HZ,
        .lcd_cmd_bits = LCD_CMD_BITS,
        .lcd_param_bits = LCD_PARAM_BITS,
        .spi_mode = 0,
        .trans_queue_depth = 1
    };

    ESP_ERROR_CHECK(
        esp_lcd_new_panel_io_spi(
            (esp_lcd_spi_bus_handle_t)LCD_HOST,
            &io_config,
            &lcd_io
        )
    );

    ESP_LOGI(TAG, "Creando controlador ST7789");

    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = LCD_PIN_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16
    };

    ESP_ERROR_CHECK(
        esp_lcd_new_panel_st7789(
            lcd_io,
            &panel_config,
            &lcd_panel
        )
    );

    ESP_LOGI(TAG, "Reiniciando pantalla");

    ESP_ERROR_CHECK(
        esp_lcd_panel_reset(lcd_panel)
    );

    ESP_LOGI(TAG, "Inicializando pantalla");

    ESP_ERROR_CHECK(
        esp_lcd_panel_init(lcd_panel)
    );

    ESP_ERROR_CHECK(
        esp_lcd_panel_mirror(
            lcd_panel,
            true,
            false
        )
    );

    ESP_ERROR_CHECK(
        esp_lcd_panel_swap_xy(
            lcd_panel,
            true
        )
    );

    ESP_ERROR_CHECK(
        esp_lcd_panel_invert_color(
            lcd_panel,
            false
        )
    );

    ESP_ERROR_CHECK(
        esp_lcd_panel_disp_on_off(
            lcd_panel,
            true
        )
    );

    ESP_LOGI(TAG, "LCD inicializada correctamente");
}

void lcd_manager_init(void)
{
    lcd_init();

    ESP_LOGI(
        TAG,
        "Reservando %u bytes para el framebuffer",
        (unsigned int)LCD_FRAME_BYTES
    );

    framebuffer = heap_caps_malloc(
        LCD_FRAME_BYTES,
        MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL
    );

    if (framebuffer == NULL) {
        ESP_LOGE(
            TAG,
            "No se pudo reservar memoria para el framebuffer"
        );

        return;
    }
}

static void lcd_draw_waiting(void *context)
{
    (void)context;

    framebuffer_draw_text(
        50,
        40,
        "CONTROL STOCK",
        3,
        COLOR_CYAN,
        COLOR_BLACK);

    framebuffer_draw_text(
        50,
        120,
        "ESPERANDO QR",
        3,
        COLOR_WHITE,
        COLOR_BLACK);
}

void lcd_show_waiting(void)
{
    lcd_render_full_screen(
        COLOR_BLACK,
        lcd_draw_waiting,
        NULL
    );
}

typedef struct {
    const Product *product;
    int add_amount;
} lcd_product_context_t;

static void lcd_draw_product_screen(void *context)
{
    lcd_product_context_t *screen =
        (lcd_product_context_t *)context;

    char buffer[64];

    framebuffer_draw_text(
        10,
        10,
        "PRODUCTO",
        3,
        COLOR_YELLOW,
        COLOR_BLACK);

    framebuffer_draw_text(
        10,
        60,
        screen->product->name,
        3,
        COLOR_WHITE,
        COLOR_BLACK);

    snprintf(
        buffer,
        sizeof(buffer),
        "STOCK: %lu",
        (unsigned long)screen->product->stock);

    framebuffer_draw_text(
        10,
        110,
        buffer,
        2,
        COLOR_GREEN,
        COLOR_BLACK);

    snprintf(
        buffer,
        sizeof(buffer),
        "AGREGAR: %d",
        screen->add_amount);

    framebuffer_draw_text(
        10,
        150,
        buffer,
        2,
        COLOR_CYAN,
        COLOR_BLACK);

    framebuffer_draw_text(
        10,
        200,
        "+  -  OK  X",
        2,
        COLOR_WHITE,
        COLOR_BLACK);
}

void lcd_show_product_screen(
    const Product *product,
    int add_amount)
{
    if (product == NULL) {
        lcd_show_error("PRODUCTO NULO");
        return;
    }

    lcd_product_context_t context = {
        .product = product,
        .add_amount = add_amount
    };

    lcd_render_full_screen(
        COLOR_BLACK,
        lcd_draw_product_screen,
        &context
    );
}

static void lcd_draw_success(void *context)
{
    Product *product = (Product *)context;
    char buffer[64];

    framebuffer_draw_text(
        20,
        40,
        "ACTUALIZADO",
        4,
        COLOR_BLACK,
        COLOR_GREEN);

    framebuffer_draw_text(
        20,
        110,
        product->name,
        3,
        COLOR_BLACK,
        COLOR_GREEN);

    snprintf(
        buffer,
        sizeof(buffer),
        "STOCK %lu",
        (unsigned long)product->stock);

    framebuffer_draw_text(
        20,
        170,
        buffer,
        2,
        COLOR_BLACK,
        COLOR_GREEN);
}

void lcd_show_success(
    Product *product)
{
    if (product == NULL) {
        lcd_show_error("PRODUCTO NULO");
        return;
    }

    lcd_render_full_screen(
        COLOR_GREEN,
        lcd_draw_success,
        product
    );
}

static void lcd_draw_cancelled(void *context)
{
    (void)context;

    framebuffer_draw_text(
        30,
        80,
        "OPERACION",
        3,
        COLOR_BLACK,
        COLOR_ORANGE);

    framebuffer_draw_text(
        40,
        130,
        "CANCELADA",
        3,
        COLOR_BLACK,
        COLOR_ORANGE);
}

void lcd_show_cancelled(void)
{
    lcd_render_full_screen(
        COLOR_ORANGE,
        lcd_draw_cancelled,
        NULL
    );
}

static void lcd_draw_error(void *context)
{
    const char *msg = (const char *)context;

    framebuffer_draw_text(
        70,
        40,
        "ERROR",
        4,
        COLOR_WHITE,
        COLOR_RED);

    framebuffer_draw_text(
        20,
        130,
        msg,
        2,
        COLOR_WHITE,
        COLOR_RED);
}

void lcd_show_error(
    const char *msg)
{
    if (msg == NULL) {
        msg = "DESCONOCIDO";
    }

    lcd_render_full_screen(
        COLOR_RED,
        lcd_draw_error,
        (void *)msg
    );
}
