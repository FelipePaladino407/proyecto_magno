#ifndef PRODUCT_DB_H
#define PRODUCT_DB_H

#include <stdbool.h>
#include <stdint.h>
#include "shared_types.h"

void product_db_init(void);

bool product_db_upsert_product(const char *id,
                               const char *name,
                               uint32_t stock,
                               Product *out_product);

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
