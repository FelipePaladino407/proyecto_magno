#ifndef HASH_TABLE_H
#define HASH_TABLE_H

#include <stdbool.h>

#define HASH_TABLE_SIZE 32
#define PRODUCT_ID_MAX_LEN 32
#define PRODUCT_NAME_MAX_LEN 64
#define PRODUCT_CATEGORY_MAX_LEN 32

typedef enum {
    // hola, el que este empty quiere decir que nunca hubo nada en esa celda.
    ENTRY_EMPTY,
    // significa que en esa celda ya hay un producto valido.
    ENTRY_OCCUPIED,
    // significa que en esa celda habia algo, pero fue exterminado.
    ENTRY_DELETED
} EntryStatus;

typedef struct {
    char id[PRODUCT_ID_MAX_LEN];
    char name[PRODUCT_NAME_MAX_LEN];
    int stock;
    char category[PRODUCT_CATEGORY_MAX_LEN];
} Product;

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

bool hash_table_update_stock(HashTable *table, const char *id, int new_stock);

bool hash_table_is_full(HashTable *table);

#endif
