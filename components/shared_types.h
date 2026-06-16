#ifndef SHARED_TYPES_H
#define SHARED_TYPES_H

#include <stdint.h>

typedef struct {
    char id[16];
    char name[32];
    uint32_t stock;
} Product;

typedef struct {
    Product producto;
    char fecha_hora[32];
    char estado[16];      // OK, ERROR o ingresoMANUAL
} HistoryEntry;

#endif