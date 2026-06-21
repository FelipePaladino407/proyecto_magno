#include "product_db.h"

#include <string.h>
#include "hash_table.h"

static HashTable s_product_table;

static void safe_copy(char *dest, const char *src, size_t dest_size)
{
    if (dest == NULL || dest_size == 0) {
        return;
    }

    if (src == NULL) {
        dest[0] = '\0';
        return;
    }

    strncpy(dest, src, dest_size - 1);
    dest[dest_size - 1] = '\0';
}

void product_db_init(void)
{
    hash_table_init(&s_product_table);
}

bool product_db_upsert_product(const char *id,
                               const char *name,
                               uint32_t stock,
                               Product *out_product)
{
    if (id == NULL || name == NULL || id[0] == '\0') {
        return false;
    }

    Product product;
    memset(&product, 0, sizeof(Product));
    safe_copy(product.id, id, sizeof(product.id));
    safe_copy(product.name, name, sizeof(product.name));
    product.stock = stock;

    if (!hash_table_insert(&s_product_table, product)) {
        return false;
    }

    if (out_product != NULL) {
        *out_product = product;
    }

    return true;
}

bool product_db_register_scan(const char *id,
                              const char *name,
                              Product *out_product)
{
    if (id == NULL || name == NULL || id[0] == '\0') {
        return false;
    }

    Product *existing_product = hash_table_find(&s_product_table, id);

    if (existing_product != NULL) {
        existing_product->stock++;

        if (out_product != NULL) {
            *out_product = *existing_product;
        }

        return true;
    }

    if (hash_table_is_full(&s_product_table)) {
        return false;
    }

    Product new_product;
    memset(&new_product, 0, sizeof(Product));

    safe_copy(new_product.id, id, sizeof(new_product.id));
    safe_copy(new_product.name, name, sizeof(new_product.name));
    new_product.stock = 1;

    if (!hash_table_insert(&s_product_table, new_product)) {
        return false;
    }

    if (out_product != NULL) {
        *out_product = new_product;
    }

    return true;
}

bool product_db_find_by_id(const char *id,
                           Product *out_product)
{
    if (id == NULL || out_product == NULL) {
        return false;
    }

    Product *product = hash_table_find(&s_product_table, id);

    if (product == NULL) {
        return false;
    }

    *out_product = *product;
    return true;
}

bool product_db_decrement_stock(const char *id,
                                Product *out_product)
{
    if (id == NULL) {
        return false;
    }

    Product *product = hash_table_find(&s_product_table, id);

    if (product == NULL) {
        return false;
    }

    if (product->stock == 0) {
        return false;
    }

    product->stock--;

    if (out_product != NULL) {
        *out_product = *product;
    }

    return true;
}

bool product_db_remove_product(const char *id)
{
    if (id == NULL) {
        return false;
    }

    return hash_table_remove(&s_product_table, id);
}

uint32_t product_db_get_count(void)
{
    return (uint32_t)s_product_table.count;
}

