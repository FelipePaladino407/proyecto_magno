#ifndef SHARED_TYPES_H
#define SHARED_TYPES_H

#include <stdint.h>
#include <time.h>

typedef struct {
    char id[16];
    char name[32];
    uint32_t stock;
} Product;

typedef struct {
    Product product;
    time_t timestamp;
    char state[16]; // OK, ERROR o ingresoMANUAL
} HistoryEntry;

#endif