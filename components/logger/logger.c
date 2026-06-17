#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#include "esp_err.h"
#include "esp_log.h"
#include "logger.h"
#include "nvs.h"

static const char *TAG = "LOGGER";

static void logger_armar_claves(Logger *logger, const char *nombre)//para el nvs segun el nombre del logger
{
    strncpy(logger->nombre, nombre, sizeof(logger->nombre) - 1);
    logger->nombre[sizeof(logger->nombre) - 1] = '\0';

    snprintf(logger->nvs_key_buffer,
             sizeof(logger->nvs_key_buffer),
             "%s_buf",
             logger->nombre);

    snprintf(logger->nvs_key_head,
             sizeof(logger->nvs_key_head),
             "%s_head",
             logger->nombre);

    snprintf(logger->nvs_key_tail,
             sizeof(logger->nvs_key_tail),
             "%s_tail",
             logger->nombre);

    snprintf(logger->nvs_key_count,
             sizeof(logger->nvs_key_count),
             "%s_count",
             logger->nombre);
}

static void logger_save(Logger *logger)
{
    esp_err_t err;

    err = nvs_storage_set_blob(logger->nvs_key_buffer, logger->buffer, sizeof(logger->buffer)); // Guarda el buffer entero en NVS
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "No se pudo guardar el buffer en NVS");
    }

    err = nvs_storage_set_int(logger->nvs_key_head, (int32_t)logger->head); // Guarda head en NVS
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "No se pudo guardar head en NVS");
    }

    err = nvs_storage_set_int(logger->nvs_key_tail, (int32_t)logger->tail); // Guarda tail en NVS
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "No se pudo guardar tail en NVS");
    }

    err = nvs_storage_set_int(logger->nvs_key_count, (int32_t)logger->count); // Guarda count en NVS
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "No se pudo guardar count en NVS");
    }
}

void logger_init(Logger *logger, const char *nombre)
{
    if (logger == NULL || nombre == NULL) {
        return;
    }

    nvs_storage_init(); // Inicializa el almacenamiento NVS

    memset(logger, 0, sizeof(Logger));

    logger_armar_claves(logger, nombre);

    logger->head = 0;
    logger->tail = 0;
    logger->count = 0;

    size_t buffer_size = sizeof(logger->buffer);

    esp_err_t err = nvs_storage_get_blob(logger->nvs_key_buffer, logger->buffer, &buffer_size); // Carga lo guardado en el buffer desde el almacenamiento NVS

    if (err != ESP_OK || buffer_size != sizeof(logger->buffer)) {
        memset(logger->buffer, 0, sizeof(logger->buffer));
        ESP_LOGW(TAG, "No habia buffer valido en NVS para logger %s, arranca vacio", logger->nombre);
    }

    int32_t valor = 0;

    if (nvs_storage_get_int(logger->nvs_key_head, &valor) == ESP_OK) {
        logger->head = (int)valor;
    }

    if (nvs_storage_get_int(logger->nvs_key_tail, &valor) == ESP_OK) {
        logger->tail = (int)valor;
    }

    if (nvs_storage_get_int(logger->nvs_key_count, &valor) == ESP_OK) {
        logger->count = (int)valor;
    }

    if (logger->head < 0 || logger->head >= LOGGER_SIZE ||
        logger->tail < 0 || logger->tail >= LOGGER_SIZE ||
        logger->count < 0 || logger->count > LOGGER_SIZE) {

        ESP_LOGW(TAG, "Datos invalidos en NVS para logger %s, reinicio logger", logger->nombre);

        logger->head = 0;
        logger->tail = 0;
        logger->count = 0;

        memset(logger->buffer, 0, sizeof(logger->buffer));

        logger_save(logger);
    }

    ESP_LOGI(TAG,
             "Logger %s inicializado desde NVS: head=%d tail=%d count=%d",
             logger->nombre,
             logger->head,
             logger->tail,
             logger->count);
}

bool logger_push(Logger *logger, Product product, time_t timestamp, const char *state)
{
    if (logger == NULL || timestamp == 0 || state == NULL) {
        return false;
    }

    memset(&logger->buffer[logger->tail], 0, sizeof(logger->buffer[logger->tail]));

    logger->buffer[logger->tail].product = product;
    logger->buffer[logger->tail].timestamp = timestamp;

    strncpy(logger->buffer[logger->tail].state, state, sizeof(logger->buffer[logger->tail].state) - 1);
    logger->buffer[logger->tail].state[sizeof(logger->buffer[logger->tail].state) - 1] = '\0';

    logger->tail = (logger->tail + 1) % LOGGER_SIZE; // Esto hace que el buffer sea circular: cuando tail llega al final vuelve a 0, en los otros casos el resto es tail+1

    if (logger->count < LOGGER_SIZE) {
        logger->count++; // Cuenta cuantos eventos hay guardados, hasta el maximo de LOGGER_SIZE
    } else {
        // Cuando count = LOGGER_SIZE, el buffer ya esta lleno, entonces al guardar un nuevo evento se sobrescribe el mas viejo, que es el que marca head.
        // Por eso se avanza head tambien
        logger->head = (logger->head + 1) % LOGGER_SIZE; // Vuelve a 0 igual que tail
        ESP_LOGW(TAG, "Logger %s lleno: se sobrescribio el evento mas viejo", logger->nombre);
    }

    logger_save(logger); // Cada vez que se guarda un evento, tambien se actualiza NVS

    ESP_LOGI(TAG,
             "Guardado en logger %s: %s | %s | %lu | %lld | %s",
             logger->nombre,
             product.id,
             product.name,
             (unsigned long)product.stock,
             (long long)timestamp,
             state);

    return true;
}

int logger_count(Logger *logger)
{
    if (logger == NULL) {
        return 0;
    }

    return logger->count;
}

void logger_print(Logger *logger)
{
    if (logger == NULL) {
        return;
    }

    ESP_LOGI(TAG, "Eventos en logger %s: %d", logger->nombre, logger->count);

    for (int i = 0; i < logger->count; i++) {
        int index = (logger->head + i) % LOGGER_SIZE;

        ESP_LOGI(TAG,
                 "[%d] %s | %s | %lu | %lld | %s",
                 i,
                 logger->buffer[index].product.id,
                 logger->buffer[index].product.name,
                 (unsigned long)logger->buffer[index].product.stock,
                 (long long)logger->buffer[index].timestamp,
                 logger->buffer[index].state);
    }
}

// Simplificacion de la referencia: https://embeddedartistry.com/blog/2017/05/17/creating-a-circular-buffer-in-c-and-c/