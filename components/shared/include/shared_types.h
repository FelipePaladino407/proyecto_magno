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
    char state[16];   // OK, MANUAL o ERROR
} HistoryEntry;

typedef struct {
    char id[16];
    char nombre[32];
} ProductoCatalogo;

#define CATALOGO_SIZE 50

extern const ProductoCatalogo catalogo_completo[CATALOGO_SIZE];

#endif