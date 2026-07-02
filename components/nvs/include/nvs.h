#ifndef NVS_H
#define NVS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define NVS_NAMESPACE "storage"

/**
 * @brief Inicializa NVS y abre el namespace compartido del proyecto.
 *
 * @return ESP_OK si NVS quedo listo para usarse.
 */
esp_err_t nvs_storage_init(void);

/**
 * @brief Indica si NVS ya esta inicializado y con handle abierto.
 *
 * @return true si NVS esta listo; false si todavia no se inicializo.
 */
bool nvs_storage_is_initialized(void);

/**
 * @brief Indica si un codigo de error representa una clave inexistente en NVS.
 *
 * Permite que los componentes de alto nivel no dependan directamente de los
 * codigos internos de nvs_flash.h.
 *
 * @param err Codigo de error a evaluar.
 * @return true si el error significa "clave no encontrada".
 */
bool nvs_storage_err_is_not_found(esp_err_t err);

/**
 * @brief Borra toda la particion NVS y la vuelve a inicializar.
 *
 * @return ESP_OK si se borro e inicializo correctamente.
 */
esp_err_t nvs_storage_clear(void);

/**
 * @brief Guarda una string en NVS.
 *
 * @param key Clave asociada al dato.
 * @param value String a almacenar.
 * @return ESP_OK si se guardo correctamente.
 */
esp_err_t nvs_storage_set_str(const char *key, const char *value);

/**
 * @brief Lee una string desde NVS.
 *
 * @param key Clave del dato.
 * @param buf Buffer de salida.
 * @param buf_size Tamanio del buffer de salida.
 * @return ESP_OK si se leyo correctamente; ESP_ERR_NOT_FOUND si no existe.
 */
esp_err_t nvs_storage_get_str(const char *key, char *buf, size_t buf_size);

/**
 * @brief Guarda un entero de 32 bits en NVS.
 *
 * @param key Clave asociada al dato.
 * @param value Valor a guardar.
 * @return ESP_OK si se guardo correctamente.
 */
esp_err_t nvs_storage_set_int(const char *key, int32_t value);

/**
 * @brief Lee un entero de 32 bits desde NVS.
 *
 * @param key Clave del dato.
 * @param out_value Variable de salida.
 * @return ESP_OK si se leyo correctamente; ESP_ERR_NOT_FOUND si no existe.
 */
esp_err_t nvs_storage_get_int(const char *key, int32_t *out_value);

/**
 * @brief Guarda un BLOB en NVS.
 *
 * @param key Clave asociada al dato.
 * @param data Datos a guardar.
 * @param data_len Tamanio de los datos.
 * @return ESP_OK si se guardo correctamente.
 */
esp_err_t nvs_storage_set_blob(const char *key, const void *data, size_t data_len);

/**
 * @brief Lee un BLOB desde NVS.
 *
 * @param key Clave del dato.
 * @param buf Buffer de salida.
 * @param buf_size Tamanio del buffer. Se actualiza con el tamanio real leido.
 * @return ESP_OK si se leyo correctamente; ESP_ERR_NOT_FOUND si no existe.
 */
esp_err_t nvs_storage_get_blob(const char *key, void *buf, size_t *buf_size);

#endif


