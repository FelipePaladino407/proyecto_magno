#include "logger.h"
#include "esp_err.h"
#include "esp_log.h"
#include "nvs.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static const char *TAG = "LOGGER";

// para el nvs segun el nombre del logger
static void logger_armar_claves(Logger *logger, const char *nombre) {
    strncpy(logger->nombre, nombre, sizeof(logger->nombre) - 1);
    logger->nombre[sizeof(logger->nombre) - 1] = '\0';

    snprintf(logger->nvs_key_buffer, sizeof(logger->nvs_key_buffer), "%.9s_buf", logger->nombre);

    snprintf(logger->nvs_key_head, sizeof(logger->nvs_key_head), "%.9s_head", logger->nombre);

    snprintf(logger->nvs_key_tail, sizeof(logger->nvs_key_tail), "%.9s_tail", logger->nombre);

    snprintf(logger->nvs_key_count, sizeof(logger->nvs_key_count), "%.9s_count", logger->nombre);
}

static esp_err_t logger_save(Logger *logger) {
    esp_err_t err;
    esp_err_t final_err = ESP_OK;

    if (logger == NULL) {
        ESP_LOGE(TAG, "logger_save: logger is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    err = nvs_storage_set_blob(logger->nvs_key_buffer, logger->buffer, sizeof(logger->buffer));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "No se pudo guardar el buffer en NVS (0x%x)", err);
        final_err = err;
    }

    err = nvs_storage_set_int(logger->nvs_key_head, (int32_t)logger->head);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "No se pudo guardar head en NVS (0x%x)", err);
        if (final_err == ESP_OK) {
            final_err = err;
        }
    }

    err = nvs_storage_set_int(logger->nvs_key_tail, (int32_t)logger->tail);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "No se pudo guardar tail en NVS (0x%x)", err);
        if (final_err == ESP_OK) {
            final_err = err;
        }
    }

    err = nvs_storage_set_int(logger->nvs_key_count, (int32_t)logger->count);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "No se pudo guardar count en NVS (0x%x)", err);
        if (final_err == ESP_OK) {
            final_err = err;
        }
    }

    if (final_err != ESP_OK) {
        ESP_LOGW(TAG, "Algunos datos no se guardaron correctamente en NVS");
    }

    return final_err;
}

esp_err_t logger_init(Logger *logger, const char *nombre) {
    esp_err_t ret;

    if (logger == NULL || nombre == NULL) {
        ESP_LOGE(TAG, "logger_init: invalid arguments (logger=%p, nombre=%s)", logger, nombre);
        return ESP_ERR_INVALID_ARG;
    }

    memset(logger, 0, sizeof(Logger));

    logger_armar_claves(logger, nombre);

    logger->head = 0;
    logger->tail = 0;
    logger->count = 0;

    size_t buffer_size = sizeof(logger->buffer);

    ret = nvs_storage_get_blob(logger->nvs_key_buffer, logger->buffer, &buffer_size);
    if (ret != ESP_OK || buffer_size != sizeof(logger->buffer)) {
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

    if (logger->head < 0 || logger->head >= LOGGER_SIZE || logger->tail < 0 || logger->tail >= LOGGER_SIZE ||
        logger->count < 0 || logger->count > LOGGER_SIZE) {

        ESP_LOGW(TAG, "Datos invalidos en NVS para logger %s, reinicio logger", logger->nombre);

        logger->head = 0;
        logger->tail = 0;
        logger->count = 0;

        memset(logger->buffer, 0, sizeof(logger->buffer));

        ret = logger_save(logger);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "logger_init: logger_save failed (0x%x)", ret);
            return ret;
        }
    }

    ESP_LOGI(TAG, "Logger %s inicializado: head=%d tail=%d count=%d", logger->nombre, logger->head, logger->tail,
             logger->count);

    return ESP_OK;
}

bool logger_push(Logger *logger, Product product, time_t timestamp, const char *state) {
    if (logger == NULL || timestamp == 0 || state == NULL) {
        return false;
    }

    memset(&logger->buffer[logger->tail], 0, sizeof(logger->buffer[logger->tail]));

    logger->buffer[logger->tail].product = product;
    logger->buffer[logger->tail].timestamp = timestamp;

    strncpy(logger->buffer[logger->tail].state, state, sizeof(logger->buffer[logger->tail].state) - 1);
    logger->buffer[logger->tail].state[sizeof(logger->buffer[logger->tail].state) - 1] = '\0';

    logger->tail = (logger->tail + 1) % LOGGER_SIZE; // Esto hace que el buffer sea circular: cuando tail llega al final
                                                     // vuelve a 0, en los otros casos el resto es tail+1

    if (logger->count < LOGGER_SIZE) {
        logger->count++; // Cuenta cuantos eventos hay guardados, hasta el maximo de LOGGER_SIZE
    } else {
        // Cuando count = LOGGER_SIZE, el buffer ya esta lleno, entonces al guardar un nuevo evento se sobrescribe el
        // mas viejo, que es el que marca head. Por eso se avanza head tambien
        logger->head = (logger->head + 1) % LOGGER_SIZE; // Vuelve a 0 igual que tail
        ESP_LOGW(TAG, "Logger %s lleno: se sobrescribio el evento mas viejo", logger->nombre);
    }

    logger_save(logger); // Cada vez que se guarda un evento, tambien se actualiza NVS

    ESP_LOGI(TAG, "Guardado en logger %s: %s | %s | %lu | %lld | %s", logger->nombre, product.id, product.name,
             (unsigned long)product.stock, (long long)timestamp, state);

    return true;
}

int logger_count(Logger *logger) {
    if (logger == NULL) {
        return 0;
    }

    return logger->count;
}

void logger_print(Logger *logger) {
    if (logger == NULL) {
        return;
    }

    ESP_LOGI(TAG, "Eventos en logger %s: %d", logger->nombre, logger->count);

    for (int i = 0; i < logger->count; i++) {
        int index = (logger->head + i) % LOGGER_SIZE;

        ESP_LOGI(TAG, "[%d] %s | %s | %lu | %lld | %s", i, logger->buffer[index].product.id,
                 logger->buffer[index].product.name, (unsigned long)logger->buffer[index].product.stock,
                 (long long)logger->buffer[index].timestamp, logger->buffer[index].state);
    }
}

// Simplificacion de la referencia: https://embeddedartistry.com/blog/2017/05/17/creating-a-circular-buffer-in-c-and-c/
