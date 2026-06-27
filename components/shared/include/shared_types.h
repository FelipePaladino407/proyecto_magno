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
    char id[16];
    char nombre[32];
} ProductoCatalogo;

typedef struct {
    Product product;
    time_t timestamp;
    char state[16]; // OK, ERROR o ingreso MANUAL
} HistoryEntry;

#define CATALOGO_SIZE 30

extern const ProductoCatalogo catalogo_completo[CATALOGO_SIZE];

#endif // SHARED_TYPES_H
