#ifndef LOGGER_H
#define LOGGER_H

#include <stdbool.h>
#include "shared_types.h"

#define LOGGER_SIZE 20

void logger_init(void);// Inicializa el logger vacio

bool logger_push(Product producto, const char *fecha_hora, const char *estado);// Guarda un producto con su fecha/hora en el buffer circular y si hubo un error. Devuelve true si se guardo bien o false si hubo un error (por ejemplo fecha_hora o estado es NULL)

int logger_count(void);// Devuelve cuantos eventos hay guardados

void logger_print(void);// Muestra por consola los eventos guardados

#endif