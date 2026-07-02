#include "nvs.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"

static const char *TAG = "nvs";
static nvs_handle_t s_handle = 0;

/**
 * @brief Indica si el componente NVS ya tiene un handle abierto y utilizable.
 *
 * @return true si nvs_storage_init() ya abrio correctamente el namespace; false
 * en caso contrario.
 */
bool nvs_storage_is_initialized(void)
{
    return s_handle != 0;
}

/**
 * @brief Indica si un error corresponde a una clave inexistente en NVS.
 *
 * El wrapper normaliza las lecturas para devolver ESP_ERR_NOT_FOUND hacia el
 * resto del proyecto, pero esta funcion tambien reconoce ESP_ERR_NVS_NOT_FOUND
 * por compatibilidad con versiones anteriores del wrapper.
 *
 * @param err Codigo de error devuelto por NVS o por el wrapper.
 * @return true si el dato solicitado no existe.
 */
bool nvs_storage_err_is_not_found(esp_err_t err)
{
    return err == ESP_ERR_NOT_FOUND || err == ESP_ERR_NVS_NOT_FOUND;
}

/**
 * @brief Valida que exista un handle NVS abierto antes de leer o escribir.
 *
 * @param operation Nombre de la operacion que se esta intentando ejecutar.
 * @return ESP_OK si NVS esta listo; ESP_ERR_INVALID_STATE si falta inicializar.
 */
static esp_err_t nvs_storage_require_handle(const char *operation)
{
    if (s_handle != 0) {
        return ESP_OK;
    }

    ESP_LOGE(TAG,
             "%s requiere NVS inicializado. Llamar a nvs_storage_init() primero",
             operation != NULL ? operation : "operacion NVS");

    return ESP_ERR_INVALID_STATE;
}

/**
 * @brief Inicializa la particion NVS y abre el namespace compartido del proyecto.
 *
 * La funcion es idempotente: si NVS ya fue inicializado, devuelve ESP_OK sin
 * abrir otro handle. Si la particion esta llena, corrupta o tiene una version
 * incompatible, se borra y se inicializa nuevamente.
 *
 * @return ESP_OK si la inicializacion fue exitosa.
 */
esp_err_t nvs_storage_init(void)
{
    if (s_handle != 0) {
        return ESP_OK;
    }

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG,
                 "NVS requiere erase por estado %s",
                 esp_err_to_name(err));

        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_init failed: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &s_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        s_handle = 0;
    }

    return err;
}

/**
 * @brief Borra completamente el contenido de la particion NVS.
 *
 * Cierra el handle actual, elimina la particion NVS completa y la vuelve a
 * inicializar. Esto borra tambien product_db, pending_queue, loggers y
 * credenciales guardadas.
 *
 * @return ESP_OK si la operacion fue exitosa.
 */
esp_err_t nvs_storage_clear(void)
{
    if (s_handle != 0) {
        nvs_close(s_handle);
        s_handle = 0;

        esp_err_t deinit_err = nvs_flash_deinit();
        if (deinit_err != ESP_OK) {
            ESP_LOGW(TAG,
                     "nvs_flash_deinit antes de erase devolvio: %s",
                     esp_err_to_name(deinit_err));
        }
    }

    esp_err_t err = nvs_flash_erase();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_erase failed: %s", esp_err_to_name(err));
        return err;
    }

    return nvs_storage_init();
}

/**
 * @brief Guarda una string en NVS y confirma el cambio.
 *
 * @param key Clave asociada al dato.
 * @param value String a almacenar.
 * @return ESP_OK si se guardo correctamente.
 */
esp_err_t nvs_storage_set_str(const char *key, const char *value)
{
    if (key == NULL || value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = nvs_storage_require_handle("nvs_storage_set_str");
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_str(s_handle, key, value);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set_str('%s') failed: %s", key, esp_err_to_name(err));
        return err;
    }

    return nvs_commit(s_handle);
}

/**
 * @brief Obtiene una string almacenada en NVS.
 *
 * @param key Clave del dato.
 * @param buf Buffer de salida.
 * @param buf_size Tamanio del buffer.
 * @return ESP_OK si la lectura fue exitosa.
 */
esp_err_t nvs_storage_get_str(const char *key, char *buf, size_t buf_size)
{
    if (key == NULL || buf == NULL || buf_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = nvs_storage_require_handle("nvs_storage_get_str");
    if (err != ESP_OK) {
        return err;
    }

    size_t required_size = buf_size;
    err = nvs_get_str(s_handle, key, buf, &required_size);
    if (nvs_storage_err_is_not_found(err)) {
        return ESP_ERR_NOT_FOUND;
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "get_str('%s') failed: %s", key, esp_err_to_name(err));
    }

    return err;
}

/**
 * @brief Guarda un entero de 32 bits en NVS y confirma el cambio.
 *
 * @param key Clave asociada al dato.
 * @param value Valor a almacenar.
 * @return ESP_OK si se guardo correctamente.
 */
esp_err_t nvs_storage_set_int(const char *key, int32_t value)
{
    if (key == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = nvs_storage_require_handle("nvs_storage_set_int");
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_i32(s_handle, key, value);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set_int('%s') failed: %s", key, esp_err_to_name(err));
        return err;
    }

    return nvs_commit(s_handle);
}

/**
 * @brief Obtiene un entero de 32 bits almacenado en NVS.
 *
 * @param key Clave del dato.
 * @param out_value Variable de salida.
 * @return ESP_OK si la lectura fue exitosa.
 */
esp_err_t nvs_storage_get_int(const char *key, int32_t *out_value)
{
    if (key == NULL || out_value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = nvs_storage_require_handle("nvs_storage_get_int");
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_get_i32(s_handle, key, out_value);
    if (nvs_storage_err_is_not_found(err)) {
        return ESP_ERR_NOT_FOUND;
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "get_int('%s') failed: %s", key, esp_err_to_name(err));
    }

    return err;
}

/**
 * @brief Guarda un BLOB en NVS y confirma el cambio.
 *
 * @param key Clave asociada al dato.
 * @param data Puntero a los datos.
 * @param data_len Tamanio de los datos en bytes.
 * @return ESP_OK si se guardo correctamente.
 */
esp_err_t nvs_storage_set_blob(const char *key, const void *data, size_t data_len)
{
    if (key == NULL || (data == NULL && data_len > 0)) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = nvs_storage_require_handle("nvs_storage_set_blob");
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_blob(s_handle, key, data, data_len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set_blob('%s') failed: %s", key, esp_err_to_name(err));
        return err;
    }

    return nvs_commit(s_handle);
}

/**
 * @brief Obtiene un BLOB almacenado en NVS.
 *
 * @param key Clave del dato.
 * @param buf Buffer de salida.
 * @param buf_size Tamanio del buffer de salida. NVS actualiza este valor.
 * @return ESP_OK si la lectura fue exitosa.
 */
esp_err_t nvs_storage_get_blob(const char *key, void *buf, size_t *buf_size)
{
    if (key == NULL || buf_size == NULL || (buf == NULL && *buf_size > 0)) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = nvs_storage_require_handle("nvs_storage_get_blob");
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_get_blob(s_handle, key, buf, buf_size);
    if (nvs_storage_err_is_not_found(err)) {
        return ESP_ERR_NOT_FOUND;
    }

    if (err != ESP_OK && err != ESP_ERR_NVS_INVALID_LENGTH) {
        ESP_LOGE(TAG, "get_blob('%s') failed: %s", key, esp_err_to_name(err));
    }

    return err;
}

