#ifndef PRODUCT_DB_H
#define PRODUCT_DB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "shared_types.h"

/**
 * @brief Inicializa product_db, inicializa NVS si hace falta y restaura datos persistidos.
 */
void product_db_init(void);

/**
 * @brief Carga un catalogo conservando el stock ya restaurado o existente.
 *
 * @param catalog Arreglo de productos de catalogo.
 * @param catalog_size Cantidad de elementos del arreglo.
 * @return Cantidad de productos cargados correctamente.
 */
uint32_t product_db_load_catalog(const ProductoCatalogo *catalog,
                                 size_t catalog_size);

/**
 * @brief Carga el catalogo compartido definido en shared_types.c.
 *
 * @return Cantidad de productos cargados correctamente.
 */
uint32_t product_db_load_default_catalog(void);

/**
 * @brief Inserta o actualiza un producto y persiste el cambio en NVS.
 *
 * @param id Identificador unico del producto.
 * @param name Nombre visible del producto.
 * @param stock Stock que debe quedar asociado.
 * @param out_product Producto resultante, opcional.
 * @return true si se actualizo y persistio correctamente; false si fallo.
 */
bool product_db_upsert_product(const char *id,
                               const char *name,
                               uint32_t stock,
                               Product *out_product);

/**
 * @brief Suma stock a un producto existente y persiste el nuevo valor en NVS.
 *
 * @param id Identificador del producto existente.
 * @param quantity Cantidad a sumar.
 * @param out_product Producto actualizado, opcional.
 * @return true si se actualizo y persistio correctamente; false si fallo.
 */
bool product_db_add_stock(const char *id,
                          uint32_t quantity,
                          Product *out_product);

/**
 * @brief Registra un escaneo como suma de una unidad o alta nueva con stock 1.
 *
 * @param id Identificador leido del QR.
 * @param name Nombre leido del QR.
 * @param out_product Producto resultante, opcional.
 * @return true si se registro y persistio correctamente; false si fallo.
 */
bool product_db_register_scan(const char *id,
                              const char *name,
                              Product *out_product);

/**
 * @brief Busca un producto por ID en la tabla RAM actual.
 *
 * @param id Identificador buscado.
 * @param out_product Producto encontrado.
 * @return true si existe; false si no existe o si los parametros son invalidos.
 */
bool product_db_find_by_id(const char *id,
                           Product *out_product);

/**
 * @brief Descuenta una unidad de stock y persiste el resultado en NVS.
 *
 * @param id Identificador del producto.
 * @param out_product Producto actualizado, opcional.
 * @return true si se desconto y persistio correctamente; false si fallo.
 */
bool product_db_decrement_stock(const char *id,
                                Product *out_product);

/**
 * @brief Elimina un producto de product_db y persiste el cambio en NVS.
 *
 * @param id Identificador del producto a eliminar.
 * @return true si se elimino y persistio correctamente; false si fallo.
 */
bool product_db_remove_product(const char *id);

/**
 * @brief Devuelve la cantidad de productos activos cargados en product_db.
 *
 * @return Cantidad de productos activos.
 */
uint32_t product_db_get_count(void);

#endif

