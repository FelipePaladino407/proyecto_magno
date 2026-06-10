#ifndef SHARED_TYPES_H
#define SHARED_TYPES_H
#include <stdint.h>

typedef struct {
    char id[16];          // ID UNICO (ej., "BEDFORD_0002")
    char name[32];        // nombre (ej. "Bedford TK Diesel 3.0")
    uint32_t stock;       // contador stock actual 
} Product;

typedef struct {
    uint32_t timestamp;   // NTP
    char product_id[16];   
} HistoryEntry;

#endif
