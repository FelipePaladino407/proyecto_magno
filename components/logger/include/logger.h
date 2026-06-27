#ifndef LOGGER_H
#define LOGGER_H

#include <stdbool.h>
#include <time.h>
#include "esp_err.h"
#include "shared_types.h"

#define LOGGER_SIZE 20

typedef struct {
    HistoryEntry buffer[LOGGER_SIZE];

    // head marca donde esta el evento mas viejo guardado
    // tail donde se va a guardar el proximo evento
    // y count guarda cuantos eventos hay actualmente en el buffer

    int head;
    int tail;
    int count;

    char nombre[16];

    char nvs_key_buffer[16];
    char nvs_key_head[16];
    char nvs_key_tail[16];
    char nvs_key_count[16];
} Logger;

esp_err_t logger_init(Logger *logger, const char *nombre); // Inicializa el logger y carga desde NVS lo que habia guardado

bool logger_push(Logger *logger, Product product, time_t timestamp, const char *state); // Guarda un producto con su timestamp y estado en el buffer circular y en NVS. Devuelve true si se guardo bien o false si hubo un error

int logger_count(Logger *logger); // Devuelve cuantos eventos hay guardados en el logger

void logger_print(Logger *logger); // Muestra por consola los eventos guardados en el logger

#endif
