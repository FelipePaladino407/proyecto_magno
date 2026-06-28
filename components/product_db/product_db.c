#include "product_db.h"
#include "esp_err.h"
#include "hash_table.h"
#include <stddef.h>
#include <string.h>

static HashTable s_product_table;

const ProductoCatalogo catalogo_completo[CATALOGO_SIZE] = {{"LAC-LEC-001", "Leche Entera 1L"},
                                                           {"LAC-LEC-002", "Leche Descremada 1L"},
                                                           {"LAC-YOG-001", "Yogur Natural 200g"},
                                                           {"LAC-YOG-002", "Yogur Frutilla 200g"},
                                                           {"LAC-QUE-001", "Queso Mozzarella 400g"},
                                                           {"LAC-QUE-002", "Queso Colonia 300g"},
                                                           {"LAC-MAN-001", "Manteca 200g"},
                                                           {"LAC-CRE-001", "Crema de Leche 200ml"},
                                                           {"GRA-HUE-012", "Huevos x12"},
                                                           {"PAN-MOL-001", "Pan de Molde 500g"},
                                                           {"PAN-INT-001", "Pan Lactal Integral 400g"},
                                                           {"PAN-FAC-006", "Facturas x6"},
                                                           {"GRA-ARR-001", "Arroz Largo Fino 1kg"},
                                                           {"GRA-FID-001", "Fideos Spaghetti 500g"},
                                                           {"GRA-FID-002", "Fideos Mono 500g"},
                                                           {"GRA-HAR-001", "Harina 0000 1kg"},
                                                           {"GRA-AZU-001", "Azucar Blanca 1kg"},
                                                           {"GRA-ACE-001", "Aceite Girasol 900ml"},
                                                           {"GRA-ACE-002", "Aceite de Oliva 500ml"},
                                                           {"GRA-SAL-001", "Sal Fina 500g"},
                                                           {"GRA-LEN-001", "Lentejas 500g"},
                                                           {"GRA-GAR-001", "Garbanzos 500g"},
                                                           {"GRA-POR-001", "Porotos Negros 500g"},
                                                           {"CON-TOM-001", "Tomate en Lata 400g"},
                                                           {"CON-ATU-001", "Atun al Natural 170g"},
                                                           {"CON-ATU-002", "Atun en Aceite 170g"},
                                                           {"CON-ARV-001", "Arvejas en Lata 300g"},
                                                           {"CON-CHO-001", "Choclo en Lata 300g"},
                                                           {"CON-MER-001", "Mermelada Frutilla 390g"},
                                                           {"CON-DUL-001", "Dulce de Leche 400g"}};

static void safe_copy(char *dest, const char *src, size_t dest_size) {
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

esp_err_t product_db_init(void) {
    return hash_table_init(&s_product_table);
}

uint32_t product_db_load_catalog(const ProductoCatalogo *catalog, size_t catalog_size) {
    if (catalog == NULL || catalog_size == 0) {
        return 0;
    }

    uint32_t loaded = 0;

    for (size_t i = 0; i < catalog_size; i++) {
        Product existing_product;
        uint32_t stock = 0;

        if (catalog[i].id[0] == '\0') {
            continue;
        }

        if (product_db_find_by_id(catalog[i].id, &existing_product)) {
            stock = existing_product.stock;
        }

        Product ignored;
        if (product_db_upsert_product(catalog[i].id, catalog[i].nombre, stock, &ignored)) {
            loaded++;
        }
    }

    return loaded;
}

uint32_t product_db_load_default_catalog(void) {
    return product_db_load_catalog(catalogo_completo, CATALOGO_SIZE);
}

bool product_db_upsert_product(const char *id, const char *name, uint32_t stock, Product *out_product) {
    if (id == NULL || name == NULL || id[0] == '\0') {
        return false;
    }

    Product product;
    memset(&product, 0, sizeof(product));

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

bool product_db_add_stock(const char *id, uint32_t quantity, Product *out_product) {
    if (id == NULL || id[0] == '\0' || quantity == 0) {
        return false;
    }

    Product *product = hash_table_find(&s_product_table, id);

    if (product == NULL) {
        return false;
    }

    product->stock += quantity;

    if (out_product != NULL) {
        *out_product = *product;
    }

    return true;
}

bool product_db_register_scan(const char *id, const char *name, Product *out_product) {
    Product existing_product;

    if (id == NULL || name == NULL || id[0] == '\0') {
        return false;
    }

    if (product_db_find_by_id(id, &existing_product)) {
        return product_db_add_stock(id, 1, out_product);
    }

    return product_db_upsert_product(id, name, 1, out_product);
}

bool product_db_find_by_id(const char *id, Product *out_product) {
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

bool product_db_decrement_stock(const char *id, Product *out_product) {
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

bool product_db_remove_product(const char *id) {
    if (id == NULL) {
        return false;
    }

    return hash_table_remove(&s_product_table, id);
}

uint32_t product_db_get_count(void) {
    return (uint32_t)s_product_table.count;
}
