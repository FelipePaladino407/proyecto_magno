#include "lcd_manager.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "esp_log.h"
#include "lcd_port.h"
#include "lvgl.h"

static const char *TAG = "LCD_MANAGER";

static lv_obj_t *s_title = NULL;
static lv_obj_t *s_line1 = NULL;
static lv_obj_t *s_line2 = NULL;
static lv_obj_t *s_line3 = NULL;
static bool s_initialized = false;

static bool lcd_manager_lock(void)
{
    if (!lcd_port_is_initialized()) {
        ESP_LOGW(TAG, "lcd_port no esta inicializado");
        return false;
    }

    if (!lcd_port_lock(LCD_PORT_LOCK_FOREVER)) {
        ESP_LOGW(TAG, "No se pudo tomar el lock de LVGL");
        return false;
    }

    return true;
}

static void lcd_manager_unlock(void)
{
    lcd_port_unlock();
}

static void lcd_set_label_text(lv_obj_t *label, const char *text)
{
    if (label != NULL) {
        lv_label_set_text(label, text != NULL ? text : "");
    }
}

static void lcd_set_screen_text(const char *title,
                                const char *line1,
                                const char *line2,
                                const char *line3)
{
    if (!lcd_manager_lock()) {
        return;
    }

    lcd_set_label_text(s_title, title);
    lcd_set_label_text(s_line1, line1);
    lcd_set_label_text(s_line2, line2);
    lcd_set_label_text(s_line3, line3);

    lcd_manager_unlock();
}

static lv_obj_t *lcd_create_label(lv_obj_t *parent,
                                  int32_t width,
                                  const lv_font_t *font)
{
    lv_obj_t *label = lv_label_create(parent);

    lv_obj_set_width(label, width);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);

    if (font != NULL) {
        lv_obj_set_style_text_font(label, font, 0);
    }

    return label;
}

void lcd_manager_init(void)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "lcd_manager_init() llamado mas de una vez");
        lcd_show_waiting();
        return;
    }

    if (!lcd_manager_lock()) {
        ESP_LOGE(TAG, "No se puede inicializar lcd_manager sin lcd_port_init()");
        return;
    }

    lv_obj_t *screen = lv_screen_active();
    lv_obj_clean(screen);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x111111), 0);

    s_title = lcd_create_label(screen, 220, NULL);
    s_line1 = lcd_create_label(screen, 220, NULL);
    s_line2 = lcd_create_label(screen, 220, NULL);
    s_line3 = lcd_create_label(screen, 220, NULL);

    lv_obj_align(s_title, LV_ALIGN_TOP_MID, 0, 24);
    lv_obj_align(s_line1, LV_ALIGN_TOP_MID, 0, 86);
    lv_obj_align(s_line2, LV_ALIGN_TOP_MID, 0, 136);
    lv_obj_align(s_line3, LV_ALIGN_TOP_MID, 0, 186);

    lv_obj_set_style_text_color(s_title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_color(s_line1, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_color(s_line2, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_color(s_line3, lv_color_hex(0xFFFFFF), 0);

    s_initialized = true;

    lcd_manager_unlock();

    lcd_show_waiting();

    ESP_LOGI(TAG, "LCD manager inicializado");
}

void lcd_show_waiting(void)
{
    ESP_LOGI(TAG, "Pantalla: esperando QR");

    lcd_set_screen_text("CONTROL STOCK",
                        "Esperando QR",
                        "Escanee un producto",
                        "");
}

void lcd_show_product(const Product *product, int cantidad_agregar)
{
    if (product == NULL) {
        lcd_show_error("Producto nulo");
        return;
    }

    char stock_line[64];
    char amount_line[64];

    if (cantidad_agregar < 1) {
        cantidad_agregar = 1;
    }

    snprintf(stock_line,
             sizeof(stock_line),
             "Stock actual: %lu",
             (unsigned long)product->stock);

    snprintf(amount_line,
             sizeof(amount_line),
             "Agregar: %d   OK confirma",
             cantidad_agregar);

    ESP_LOGI(TAG,
             "Pantalla producto -> ID:%s | Nombre:%s | Stock:%lu | Agregar:%d",
             product->id,
             product->name,
             (unsigned long)product->stock,
             cantidad_agregar);

    lcd_set_screen_text("PRODUCTO",
                        product->name,
                        stock_line,
                        amount_line);
}

void lcd_show_added(const Product *product)
{
    if (product == NULL) {
        lcd_show_error("Producto nulo");
        return;
    }

    char stock_line[64];

    snprintf(stock_line,
             sizeof(stock_line),
             "Nuevo stock: %lu",
             (unsigned long)product->stock);

    ESP_LOGI(TAG,
             "Pantalla agregado -> ID:%s | Nombre:%s | Stock:%lu",
             product->id,
             product->name,
             (unsigned long)product->stock);

    lcd_set_screen_text("AGREGADO",
                        product->name,
                        stock_line,
                        "Publicado por MQTT...");
}

void lcd_show_cancelled(void)
{
    ESP_LOGI(TAG, "Pantalla: operacion cancelada");

    lcd_set_screen_text("CANCELADO",
                        "Operacion cancelada",
                        "No se modifico el stock",
                        "");
}

void lcd_show_error(const char *mensaje)
{
    ESP_LOGW(TAG, "Pantalla error -> %s", mensaje != NULL ? mensaje : "Error desconocido");

    lcd_set_screen_text("ERROR",
                        mensaje != NULL ? mensaje : "Error desconocido",
                        "No se agrego stock",
                        "");
}


