#ifndef HASH_TABLE_H
#define HASH_TABLE_H

#include <stdbool.h>
#include <stdint.h>
#include "shared_types.h"

#define HASH_TABLE_SIZE 32

typedef enum {
    ENTRY_EMPTY,
    ENTRY_OCCUPIED,
    ENTRY_DELETED
} EntryStatus;

typedef struct {
    Product product;
    EntryStatus status;
} HashEntry;

typedef struct {
    HashEntry entries[HASH_TABLE_SIZE];
    int count;
} HashTable;

void hash_table_init(HashTable *table);

bool hash_table_insert(HashTable *table, Product product);

Product *hash_table_find(HashTable *table, const char *id);

bool hash_table_remove(HashTable *table, const char *id);

bool hash_table_update_stock(HashTable *table, const char *id, uint32_t new_stock);

bool hash_table_is_full(HashTable *table);

#endif
