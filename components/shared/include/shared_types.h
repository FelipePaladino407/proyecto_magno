#ifndef SHARED_TYPES_H
#define SHARED_TYPES_H

#include <stdint.h>

#define PRODUCT_ID_MAX_LEN      16
#define PRODUCT_NAME_MAX_LEN    32

typedef struct {
    char id[PRODUCT_ID_MAX_LEN];
    char name[PRODUCT_NAME_MAX_LEN];
    uint32_t stock;
} Product;

typedef struct {
  Product product;
  uint32_t timestamp;
  char state[16];      // OK, ERROR o ingresoMANUAL
} HistoryEntry;


#endif

