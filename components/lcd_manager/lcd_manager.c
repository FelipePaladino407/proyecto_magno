#include "lcd_manager.h"
#include "lvgl.h"
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"

static const char *TAG = "lcd_manager";

SemaphoreHandle_t lvgl_mutex;

static lv_obj_t *title;
static lv_obj_t *line1;
static lv_obj_t *line2;
static lv_obj_t *line3;

static void lcd_lock(void)
{
    xSemaphoreTake(lvgl_mutex, portMAX_DELAY);
}

static void lcd_unlock(void)
{
    xSemaphoreGive(lvgl_mutex);
}

void lcd_manager_init(void)
{
    lvgl_mutex = xSemaphoreCreateMutex();

    lv_obj_clean(lv_screen_active());

    title = lv_label_create(lv_screen_active());
    line1 = lv_label_create(lv_screen_active());
    line2 = lv_label_create(lv_screen_active());
    line3 = lv_label_create(lv_screen_active());

    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);
    lv_obj_align(line1, LV_ALIGN_TOP_MID, 0, 80);
    lv_obj_align(line2, LV_ALIGN_TOP_MID, 0, 120);
    lv_obj_align(line3, LV_ALIGN_TOP_MID, 0, 160);

    lcd_show_waiting();

    ESP_LOGI(TAG, "LCD manager initialized");

}

void lcd_show_waiting(void)
{
    lcd_lock();
    lv_label_set_text(
        title,
        "ESPERANDO QR");

    lv_label_set_text(
        line1,
        "Escanee un producto");

    lv_label_set_text(
        line2,
        "");

    lv_label_set_text(
        line3,
        "");
    lcd_unlock();
}

void lcd_show_product(
    const Product *product,
    int cantidad_agregar)
{
    lcd_lock();
    char buffer[64];

    lv_label_set_text(
        title,
        "PRODUCTO");

    snprintf(
        buffer,
        sizeof(buffer),
        "%s",
        product->name);

    lv_label_set_text(
        line1,
        buffer);

    snprintf(
        buffer,
        sizeof(buffer),
        "Stock actual: %lu",
        (unsigned long)
        product->stock);

    lv_label_set_text(
        line2,
        buffer);

    snprintf(
        buffer,
        sizeof(buffer),
        "Agregar: %d",
        cantidad_agregar);

    lv_label_set_text(
        line3,
        buffer);
    lcd_unlock();
}

void lcd_show_added(
    const Product *product)
{
    lcd_lock();
    char buffer[64];

    lv_label_set_text(
        title,
        "AGREGADO");

    snprintf(
        buffer,
        sizeof(buffer),
        "%s",
        product->name);

    lv_label_set_text(
        line1,
        buffer);

    snprintf(
        buffer,
        sizeof(buffer),
        "Nuevo stock: %lu",
        (unsigned long)
        product->stock);

    lv_label_set_text(
        line2,
        buffer);

    lv_label_set_text(
        line3,
        "");
    lcd_unlock();
}

void lcd_show_cancelled(void)
{
    lcd_lock();
    lv_label_set_text(
        title,
        "CANCELADO");

    lv_label_set_text(
        line1,
        "Operacion cancelada");

    lv_label_set_text(
        line2,
        "");

    lv_label_set_text(
        line3,
        "");
    lcd_unlock();
}

void lcd_show_error(
    const char *mensaje)
{
    lcd_lock();
    lv_label_set_text(
        title,
        "ERROR");

    lv_label_set_text(
        line1,
        mensaje);

    lv_label_set_text(
        line2,
        "");

    lv_label_set_text(
        line3,
        "");
    lcd_unlock();
}