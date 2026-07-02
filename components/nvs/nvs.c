#include "nvs.h"
#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include <string.h>

static const char *TAG = "nvs";
static nvs_handle_t s_handle = 0;


bool nvs_storage_is_ready(void)
{
    return s_handle != 0;
}

/**
 * @brief Inicializa la partición NVS.
 *
 * Si la partición está corrupta se borra y se vuelve a inicializar.
 *
 * @return ESP_OK si la inicialización fue exitosa.
 */
esp_err_t nvs_storage_init(void) {
    esp_err_t err = nvs_flash_init();

    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
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
    }

    return err;
}

/**
 * @brief Borra completamente el contenido del NVS.
 *
 * Cierra el handler actual, elimina la partición y la
 * vuelve a inicializar.
 *
 * @return ESP_OK si la operación fue exitosa.
 */
esp_err_t nvs_storage_clear(void) {
    if (s_handle) {
        nvs_close(s_handle);
        s_handle = 0;
    }

    esp_err_t err = nvs_flash_erase();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_erase failed: %s", esp_err_to_name(err));
        return err;
    }

    return nvs_storage_init();
}

/**
 * @brief Guarda una string.
 *
 * @param key Clave asociada al dato.
 * @param value String a almacenar.
 * @return ESP_OK si se guardó correctamente.
 */
esp_err_t nvs_storage_set_str(const char *key, const char *value) {
    esp_err_t err = nvs_set_str(s_handle, key, value);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set_str('%s') failed: %s", key, esp_err_to_name(err));
        return err;
    }
    return nvs_commit(s_handle);
}

/**
 * @brief Obtiene una string almacenada.
 *
 * @param key Clave del dato.
 * @param buf Buffer de salida.
 * @param buf_size Tamaño del buffer.
 * @return ESP_OK si la lectura fue exitosa.
 */
esp_err_t nvs_storage_get_str(const char *key, char *buf, size_t buf_size) {
    esp_err_t err = nvs_get_str(s_handle, key, buf, &buf_size);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "get_str('%s') failed: %s", key, esp_err_to_name(err));
    }
    return err;
}

/**
 * @brief Guarda un entero de 32 bits.
 *
 * @param key Clave asociada al dato.
 * @param value Valor a almacenar.
 * @return ESP_OK si se guardó correctamente.
 */
esp_err_t nvs_storage_set_int(const char *key, int32_t value) {
    esp_err_t err = nvs_set_i32(s_handle, key, value);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set_int('%s') failed: %s", key, esp_err_to_name(err));
        return err;
    }
    return nvs_commit(s_handle);
}

/**
 * @brief Obtiene un entero de 32 bits.
 *
 * @param key Clave del dato.
 * @param out_value Variable de salida.
 * @return ESP_OK si la lectura fue exitosa.
 */
esp_err_t nvs_storage_get_int(const char *key, int32_t *out_value) {
    esp_err_t err = nvs_get_i32(s_handle, key, out_value);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "get_int('%s') failed: %s", key, esp_err_to_name(err));
    }
    return err;
}

/**
 * @brief Guarda un BLOB.
 *
 * @param key Clave asociada al dato.
 * @param data Puntero a los datos.
 * @param data_len Tamaño de los datos en bytes.
 * @return ESP_OK si se guardó correctamente.
 */
esp_err_t nvs_storage_set_blob(const char *key, const void *data, size_t data_len) {
    esp_err_t err = nvs_set_blob(s_handle, key, data, data_len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set_blob('%s') failed: %s", key, esp_err_to_name(err));
        return err;
    }
    return nvs_commit(s_handle);
}

/**
 * @brief Obtiene un BLOB.
 *
 * @param key Clave del dato.
 * @param buf Buffer de salida.
 * @param buf_size Tamaño del buffer (entrada/salida).
 * @return ESP_OK si la lectura fue exitosa.
 */
esp_err_t nvs_storage_get_blob(const char *key, void *buf, size_t *buf_size)
{
    esp_err_t err = nvs_get_blob(s_handle, key, buf, buf_size);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_ERR_NOT_FOUND;
    }

    if (err != ESP_OK && err != ESP_ERR_NVS_INVALID_LENGTH) {
        ESP_LOGE(TAG, "get_blob('%s') failed: %s", key, esp_err_to_name(err));
    }

    return err;
}
