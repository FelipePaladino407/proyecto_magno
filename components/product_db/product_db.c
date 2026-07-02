#include "product_db.h"

#include "nvs.h"
#include "esp_log.h"
#include "esp_err.h"

#include <string.h>
#include <stddef.h>
#include "hash_table.h"

static HashTable s_product_table;


// IMPLEMENTACION COMPLETA DE NVS EN PRODUCT_DB
static const char *TAG = "product_db";

#define PRODUCT_DB_NVS_KEY          "pdb_blob"
#define PRODUCT_DB_NVS_MAGIC        0x50444231u  /* "PDB1" */
#define PRODUCT_DB_NVS_VERSION      1
#define PRODUCT_DB_NVS_MAX_PRODUCTS 64

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t count;
    Product products[PRODUCT_DB_NVS_MAX_PRODUCTS];
} ProductDbSnapshot;

static bool product_db_upsert_product_internal(const char *id,
                                               const char *name,
                                               uint32_t stock,
                                               Product *out_product,
                                               bool persist);

static void snapshot_init(ProductDbSnapshot *snapshot)
{
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->magic = PRODUCT_DB_NVS_MAGIC;
    snapshot->version = PRODUCT_DB_NVS_VERSION;
    snapshot->count = 0;
}

static esp_err_t product_db_read_snapshot(ProductDbSnapshot *snapshot)
{
    if (snapshot == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    snapshot_init(snapshot);

    if (!nvs_storage_is_ready()) {
        return ESP_ERR_INVALID_STATE;
    }

    size_t size = sizeof(*snapshot);
    esp_err_t err = nvs_storage_get_blob(PRODUCT_DB_NVS_KEY, snapshot, &size);

    if (err == ESP_ERR_NOT_FOUND) {
        snapshot_init(snapshot);
        return err;
    }

    if (err != ESP_OK) {
        snapshot_init(snapshot);
        return err;
    }

    if (size != sizeof(*snapshot) ||
        snapshot->magic != PRODUCT_DB_NVS_MAGIC ||
        snapshot->version != PRODUCT_DB_NVS_VERSION ||
        snapshot->count > PRODUCT_DB_NVS_MAX_PRODUCTS) {
        ESP_LOGW(TAG, "Snapshot product_db invalido, se ignora");
        snapshot_init(snapshot);
        return ESP_ERR_INVALID_STATE;
    }

    return ESP_OK;
}

static bool product_db_save_product_to_nvs(const Product *product)
{
    if (product == NULL || product->id[0] == '\0') {
        return false;
    }

    if (!nvs_storage_is_ready()) {
        return true; /* Para no romper tests sin NVS */
    }

    ProductDbSnapshot snapshot;
    esp_err_t err = product_db_read_snapshot(&snapshot);

    if (err != ESP_OK && err != ESP_ERR_NOT_FOUND) {
        snapshot_init(&snapshot);
    }

    for (uint16_t i = 0; i < snapshot.count; i++) {
        if (strcmp(snapshot.products[i].id, product->id) == 0) {
            snapshot.products[i] = *product;
            err = nvs_storage_set_blob(PRODUCT_DB_NVS_KEY, &snapshot, sizeof(snapshot));
            return err == ESP_OK;
        }
    }

    if (snapshot.count >= PRODUCT_DB_NVS_MAX_PRODUCTS) {
        ESP_LOGE(TAG, "No hay espacio en snapshot NVS de product_db");
        return false;
    }

    snapshot.products[snapshot.count++] = *product;

    err = nvs_storage_set_blob(PRODUCT_DB_NVS_KEY, &snapshot, sizeof(snapshot));
    return err == ESP_OK;
}

static bool product_db_remove_product_from_nvs(const char *id)
{
    if (id == NULL || id[0] == '\0') {
        return false;
    }

    if (!nvs_storage_is_ready()) {
        return true;
    }

    ProductDbSnapshot snapshot;
    esp_err_t err = product_db_read_snapshot(&snapshot);

    if (err != ESP_OK) {
        return err == ESP_ERR_NOT_FOUND;
    }

    for (uint16_t i = 0; i < snapshot.count; i++) {
        if (strcmp(snapshot.products[i].id, id) == 0) {
            for (uint16_t j = i; j + 1 < snapshot.count; j++) {
                snapshot.products[j] = snapshot.products[j + 1];
            }

            snapshot.count--;

            err = nvs_storage_set_blob(PRODUCT_DB_NVS_KEY, &snapshot, sizeof(snapshot));
            return err == ESP_OK;
        }
    }

    return true;
}

uint32_t product_db_load_from_nvs(void)
{
    if (!nvs_storage_is_ready()) {
        ESP_LOGW(TAG, "NVS no inicializada, no se restaura product_db");
        return 0;
    }

    ProductDbSnapshot snapshot;
    esp_err_t err = product_db_read_snapshot(&snapshot);

    if (err == ESP_ERR_NOT_FOUND) {
        ESP_LOGI(TAG, "No hay product_db guardada en NVS");
        return 0;
    }

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "No se pudo leer product_db desde NVS: %s", esp_err_to_name(err));
        return 0;
    }

    uint32_t loaded = 0;

    for (uint16_t i = 0; i < snapshot.count; i++) {
        Product ignored;

        if (snapshot.products[i].id[0] == '\0') {
            continue;
        }

        if (product_db_upsert_product_internal(snapshot.products[i].id,
                                       snapshot.products[i].name,
                                       snapshot.products[i].stock,
                                       &ignored,
                                       false)) {
            loaded++;
        }
    }

    ESP_LOGI(TAG, "Restaurados %lu productos desde NVS", (unsigned long)loaded);
    return loaded;
}

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

uint32_t product_db_load_catalog(const ProductoCatalogo *catalog,
                                 size_t catalog_size)
{
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
        // cambio aca asi el catalogo base no se escribe en nvs en cada boot.
        if (product_db_upsert_product_internal(catalog[i].id, catalog[i].nombre, stock, &ignored, false)) {
            loaded++;
        }
    }

    return loaded;
}

uint32_t product_db_load_default_catalog(void)
{
    return product_db_load_catalog(catalogo_completo, CATALOGO_SIZE);
}

// IMPORTANTE ADICION PARA PRODUCT_DB CON NVS
static bool product_db_upsert_product_internal(const char *id,
                                               const char *name,
                                               uint32_t stock,
                                               Product *out_product,
                                               bool persist)
{
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

    if (persist) {
        if (!product_db_save_product_to_nvs(&product)) {
            ESP_LOGW(TAG, "No se pudo persistir producto %s en NVS", product.id);
        }
    }

    if (out_product != NULL) {
        *out_product = product;
    }

    return true;
}

bool product_db_upsert_product(const char *id,
                               const char *name,
                               uint32_t stock,
                               Product *out_product)
{
    return product_db_upsert_product_internal(id, name, stock, out_product, true);
}


bool product_db_add_stock(const char *id,
                          uint32_t quantity,
                          Product *out_product)
{
    if (id == NULL || id[0] == '\0' || quantity == 0) {
        return false;
    }

    Product *product = hash_table_find(&s_product_table, id);

    if (product == NULL) {
        return false;
    }

    product->stock += quantity;

    if (!product_db_save_product_to_nvs(product)) {
        ESP_LOGW(TAG, "Stock actualizado en RAM, pero no se pudo guardar en NVS: %s", product->id);
    }

    if (out_product != NULL) {
        *out_product = *product;
    }

    return true;
}

bool product_db_register_scan(const char *id,
                              const char *name,
                              Product *out_product)
{
    Product existing_product;

    if (id == NULL || name == NULL || id[0] == '\0') {
        return false;
    }

    if (product_db_find_by_id(id, &existing_product)) {
        return product_db_add_stock(id, 1, out_product);
    }

    return product_db_upsert_product(id, name, 1, out_product);
}


// FUNCION CLAVE -> LA MAS IMPORTANTE DE TODAS, ABEMUS FUNCION.
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

    if (!product_db_save_product_to_nvs(product)) {
    ESP_LOGW(TAG, "Stock decrementado en RAM, pero no se pudo guardar en NVS: %s", product->id);
    }

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

    bool removed = hash_table_remove(&s_product_table, id);

    if (removed) {
        if (!product_db_remove_product_from_nvs(id)) {
            ESP_LOGW(TAG, "Producto removido de RAM, pero no de NVS: %s", id);
        }
    }

    return removed;
}

uint32_t product_db_get_count(void)
{
    return (uint32_t)s_product_table.count;
}

