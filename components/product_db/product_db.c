#include "product_db.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "hash_table.h"
#include "nvs.h"

static const char *TAG = "PRODUCT_DB";

#define PRODUCT_DB_NVS_KEY "product_db"
#define PRODUCT_DB_SNAPSHOT_MAGIC 0x50444231u /* "PDB1" */
#define PRODUCT_DB_SNAPSHOT_VERSION 1u

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t table_size;
    HashTable table;
} ProductDbSnapshot;

static HashTable s_product_table;

/**
 * @brief Copia una cadena a un buffer de destino garantizando terminacion nula.
 *
 * @param dest Buffer de destino.
 * @param src Cadena origen. Si es NULL, se guarda una cadena vacia.
 * @param dest_size Tamanio total del buffer de destino.
 */
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

/**
 * @brief Verifica que los estados internos de la tabla sean coherentes.
 *
 * Valida que los slots tengan estados conocidos y que el contador de productos
 * coincida con la cantidad real de entradas ocupadas. Esto evita restaurar desde
 * NVS una tabla corrupta o con un formato inesperado.
 *
 * @param table Tabla a validar.
 * @return true si la tabla es consistente; false en caso contrario.
 */
static bool product_db_table_is_valid(const HashTable *table)
{
    if (table == NULL || table->count < 0 || table->count > HASH_TABLE_SIZE) {
        return false;
    }

    int occupied_count = 0;

    for (int i = 0; i < HASH_TABLE_SIZE; i++) {
        const HashEntry *entry = &table->entries[i];

        if (entry->status != ENTRY_EMPTY &&
            entry->status != ENTRY_OCCUPIED &&
            entry->status != ENTRY_DELETED) {
            return false;
        }

        if (entry->status == ENTRY_OCCUPIED) {
            if (entry->product.id[0] == '\0') {
                return false;
            }

            occupied_count++;
        }
    }

    return occupied_count == table->count;
}

/**
 * @brief Guarda una copia completa de la tabla de productos en NVS.
 *
 * Se persiste la HashTable completa como un BLOB versionado. De esta forma, al
 * reiniciar o volver a flashear la aplicacion sin hacer erase-flash, se puede
 * reconstruir la base local con los stocks ya modificados.
 *
 * @return true si el snapshot se guardo correctamente; false si NVS fallo.
 */
static bool product_db_save_snapshot(void)
{
    if (!nvs_storage_is_initialized()) {
        ESP_LOGW(TAG, "NVS no inicializado: product_db queda solo en RAM");
        return false;
    }

    ProductDbSnapshot snapshot = {
        .magic = PRODUCT_DB_SNAPSHOT_MAGIC,
        .version = PRODUCT_DB_SNAPSHOT_VERSION,
        .table_size = HASH_TABLE_SIZE,
        .table = s_product_table,
    };

    esp_err_t err = nvs_storage_set_blob(PRODUCT_DB_NVS_KEY,
                                         &snapshot,
                                         sizeof(snapshot));
    if (err != ESP_OK) {
        ESP_LOGE(TAG,
                 "No se pudo guardar product_db en NVS: %s",
                 esp_err_to_name(err));
        return false;
    }

    return true;
}

/**
 * @brief Restaura la tabla de productos desde NVS si existe un snapshot valido.
 *
 * Si no hay datos persistidos o el snapshot no coincide con la version actual,
 * la funcion deja la tabla RAM como estaba y permite que el catalogo se cargue
 * desde cero.
 *
 * @return true si se cargo una tabla valida desde NVS; false si no habia datos
 * persistidos o si no eran validos.
 */
static bool product_db_load_snapshot(void)
{
    if (!nvs_storage_is_initialized()) {
        ESP_LOGW(TAG, "NVS no inicializado: no se puede restaurar product_db");
        return false;
    }

    ProductDbSnapshot snapshot;
    memset(&snapshot, 0, sizeof(snapshot));

    size_t snapshot_size = sizeof(snapshot);
    esp_err_t err = nvs_storage_get_blob(PRODUCT_DB_NVS_KEY,
                                         &snapshot,
                                         &snapshot_size);

    if (err == ESP_ERR_NOT_FOUND) {
        ESP_LOGI(TAG, "No habia product_db persistida en NVS");
        return false;
    }

    if (err != ESP_OK) {
        ESP_LOGW(TAG,
                 "No se pudo leer product_db desde NVS: %s",
                 esp_err_to_name(err));
        return false;
    }

    if (snapshot_size != sizeof(snapshot) ||
        snapshot.magic != PRODUCT_DB_SNAPSHOT_MAGIC ||
        snapshot.version != PRODUCT_DB_SNAPSHOT_VERSION ||
        snapshot.table_size != HASH_TABLE_SIZE ||
        !product_db_table_is_valid(&snapshot.table)) {
        ESP_LOGW(TAG, "Snapshot product_db invalido o incompatible. Se ignora");
        return false;
    }

    s_product_table = snapshot.table;

    ESP_LOGI(TAG,
             "product_db restaurada desde NVS con %d productos",
             s_product_table.count);

    return true;
}

/**
 * @brief Inserta o actualiza un producto controlando si debe persistirse o no.
 *
 * Esta funcion interna permite reutilizar la logica de upsert tanto para cambios
 * individuales, que se guardan de inmediato, como para cargas masivas de catalogo,
 * donde se prefiere persistir una sola vez al final.
 *
 * @param id Identificador unico del producto.
 * @param name Nombre visible del producto.
 * @param stock Stock que debe quedar asociado al producto.
 * @param out_product Producto resultante, opcional.
 * @param persist_after_change Indica si debe guardarse en NVS al terminar.
 * @return true si la operacion fue exitosa; false si fallo la tabla o la
 * persistencia requerida.
 */
static bool product_db_upsert_product_internal(const char *id,
                                               const char *name,
                                               uint32_t stock,
                                               Product *out_product,
                                               bool persist_after_change)
{
    if (id == NULL || name == NULL || id[0] == '\0') {
        return false;
    }

    Product previous_product;
    Product *existing_product = hash_table_find(&s_product_table, id);
    bool existed_before = existing_product != NULL;

    if (existed_before) {
        previous_product = *existing_product;
    } else {
        memset(&previous_product, 0, sizeof(previous_product));
    }

    Product product;
    memset(&product, 0, sizeof(product));

    safe_copy(product.id, id, sizeof(product.id));
    safe_copy(product.name, name, sizeof(product.name));
    product.stock = stock;

    if (!hash_table_insert(&s_product_table, product)) {
        return false;
    }

    if (persist_after_change && !product_db_save_snapshot()) {
        if (existed_before) {
            (void)hash_table_insert(&s_product_table, previous_product);
        } else {
            (void)hash_table_remove(&s_product_table, product.id);
        }

        return false;
    }

    if (out_product != NULL) {
        *out_product = product;
    }

    return true;
}

/**
 * @brief Inicializa product_db, inicializa NVS si hace falta y restaura datos persistidos.
 *
 * Primero se limpia la tabla RAM, luego se asegura la disponibilidad de NVS y por
 * ultimo intenta restaurar el snapshot guardado. Si no hay snapshot, la tabla
 * queda vacia hasta que se cargue el catalogo por defecto.
 */
void product_db_init(void)
{
    hash_table_init(&s_product_table);

    esp_err_t err = nvs_storage_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG,
                 "No se pudo inicializar NVS para product_db: %s. Se usara solo RAM",
                 esp_err_to_name(err));
        return;
    }

    (void)product_db_load_snapshot();
}

/**
 * @brief Carga productos de catalogo conservando el stock que ya exista.
 *
 * Para cada item del catalogo, si el producto ya estaba en RAM o fue restaurado
 * desde NVS, se conserva su stock. Si no existia, se inserta con stock 0. Al
 * finalizar se guarda un unico snapshot para evitar escribir NVS muchas veces.
 *
 * @param catalog Arreglo de productos base del catalogo.
 * @param catalog_size Cantidad de elementos del arreglo.
 * @return Cantidad de productos insertados o actualizados correctamente.
 */
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
        if (product_db_upsert_product_internal(catalog[i].id,
                                               catalog[i].nombre,
                                               stock,
                                               &ignored,
                                               false)) {
            loaded++;
        }
    }

    if (loaded > 0 && !product_db_save_snapshot()) {
        ESP_LOGW(TAG, "Catalogo cargado en RAM, pero no se pudo persistir en NVS");
    }

    return loaded;
}

/**
 * @brief Carga el catalogo compartido definido en shared_types.c.
 *
 * Es un wrapper de conveniencia sobre product_db_load_catalog() para usar el
 * catalogo global del proyecto y conservar stocks persistidos.
 *
 * @return Cantidad de productos insertados o actualizados correctamente.
 */
uint32_t product_db_load_default_catalog(void)
{
    return product_db_load_catalog(catalogo_completo, CATALOGO_SIZE);
}

/**
 * @brief Inserta o actualiza un producto y guarda el cambio en NVS.
 *
 * Si el producto ya existe, se reemplazan nombre y stock. Si no existe, se crea.
 * La operacion solo devuelve true si tambien pudo persistirse el snapshot.
 *
 * @param id Identificador unico del producto.
 * @param name Nombre visible del producto.
 * @param stock Stock que debe quedar guardado.
 * @param out_product Producto resultante, opcional.
 * @return true si se actualizo RAM y NVS; false en caso contrario.
 */
bool product_db_upsert_product(const char *id,
                               const char *name,
                               uint32_t stock,
                               Product *out_product)
{
    return product_db_upsert_product_internal(id, name, stock, out_product, true);
}

/**
 * @brief Incrementa el stock de un producto existente y persiste el nuevo valor.
 *
 * La funcion falla si el ID no existe, si la cantidad es 0 o si el incremento
 * desborda uint32_t. Si NVS falla, se revierte el stock en RAM.
 *
 * @param id Identificador del producto existente.
 * @param quantity Cantidad a sumar.
 * @param out_product Producto actualizado, opcional.
 * @return true si el stock fue actualizado y guardado; false si fallo.
 */
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

    if (UINT32_MAX - product->stock < quantity) {
        ESP_LOGW(TAG, "No se puede sumar stock a %s: overflow", id);
        return false;
    }

    uint32_t previous_stock = product->stock;
    product->stock += quantity;

    if (!product_db_save_snapshot()) {
        product->stock = previous_stock;
        return false;
    }

    if (out_product != NULL) {
        *out_product = *product;
    }

    return true;
}

/**
 * @brief Registra un escaneo como alta nueva o suma de una unidad.
 *
 * Mantiene compatibilidad con codigo anterior: si el producto existe suma 1 al
 * stock; si no existe, lo crea con stock 1. En ambos casos persiste en NVS.
 *
 * @param id Identificador leido del QR.
 * @param name Nombre leido del QR.
 * @param out_product Producto creado o actualizado, opcional.
 * @return true si el escaneo se reflejo y persistio; false si fallo.
 */
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

/**
 * @brief Busca un producto por ID en la tabla RAM actual.
 *
 * La tabla RAM ya representa la version restaurada desde NVS mas los cambios de
 * la ejecucion actual, por lo que la busqueda no necesita leer NVS cada vez.
 *
 * @param id Identificador del producto buscado.
 * @param out_product Producto encontrado.
 * @return true si existe; false si no existe o los parametros son invalidos.
 */
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

/**
 * @brief Descuenta una unidad de stock y persiste el resultado en NVS.
 *
 * Falla si el producto no existe o si su stock ya es 0. Si el guardado en NVS
 * falla, se revierte el descuento en RAM para no dejar estados distintos.
 *
 * @param id Identificador del producto.
 * @param out_product Producto actualizado, opcional.
 * @return true si se desconto y persistio; false si fallo.
 */
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

    if (!product_db_save_snapshot()) {
        product->stock++;
        return false;
    }

    if (out_product != NULL) {
        *out_product = *product;
    }

    return true;
}

/**
 * @brief Elimina un producto de la tabla y guarda el cambio en NVS.
 *
 * Si el guardado persistente falla, se reinsertara el producto eliminado para
 * mantener sincronizadas la RAM y la version duradera.
 *
 * @param id Identificador del producto a eliminar.
 * @return true si se elimino y persistio; false si no existia o si fallo NVS.
 */
bool product_db_remove_product(const char *id)
{
    if (id == NULL) {
        return false;
    }

    Product previous_product;
    if (!product_db_find_by_id(id, &previous_product)) {
        return false;
    }

    if (!hash_table_remove(&s_product_table, id)) {
        return false;
    }

    if (!product_db_save_snapshot()) {
        (void)hash_table_insert(&s_product_table, previous_product);
        return false;
    }

    return true;
}

/**
 * @brief Devuelve la cantidad de productos activos en la tabla actual.
 *
 * @return Cantidad de entradas ocupadas en product_db.
 */
uint32_t product_db_get_count(void)
{
    return (uint32_t)s_product_table.count;
}

