#ifndef LOGGER_H
#define LOGGER_H

#include <stdbool.h>
#include <time.h>

#include "shared_types.h"

#define LOGGER_SIZE 20

void logger_init(void); // Inicializa el logger y carga desde NVS lo que habia guardado

bool logger_push(Product product, time_t timestamp, const char state[16]); // Guarda un producto con su time y estado en el buffer circular y en NVS. Devuelve true si se guardo bien o false si hubo un error

int logger_count(void); // Devuelve cuantos eventos hay guardados

void logger_print(void); // Muestra por consola los eventos guardados

#endif