#ifndef PRODUCT_DB_H
#define PRODUCT_DB_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "shared_types.h"

void product_db_init(void);

uint32_t product_db_load_from_nvs(void);

/* Carga un catalogo con stock inicial 0. Si el producto ya existe, conserva su stock. */
uint32_t product_db_load_catalog(const ProductoCatalogo *catalog,
                                 size_t catalog_size);

/* Carga el catalogo compartido definido en shared_types.c. */
uint32_t product_db_load_default_catalog(void);

/* Inserta o actualiza un producto del catalogo local. */
bool product_db_upsert_product(const char *id,
                               const char *name,
                               uint32_t stock,
                               Product *out_product);

/* Nueva API: escanear no suma stock; solo la confirmacion manual suma cantidad. */
bool product_db_add_stock(const char *id,
                          uint32_t quantity,
                          Product *out_product);

/* Compatibilidad con codigo anterior. Registra un escaneo como suma de 1. */
bool product_db_register_scan(const char *id,
                              const char *name,
                              Product *out_product);

bool product_db_find_by_id(const char *id,
                           Product *out_product);

bool product_db_decrement_stock(const char *id,
                                Product *out_product);

bool product_db_remove_product(const char *id);

uint32_t product_db_get_count(void);

#endif

