#ifndef NVS_H
#define NVS_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "esp_err.h"

#define NVS_NAMESPACE   "storage"


esp_err_t nvs_storage_init(void);
esp_err_t nvs_storage_clear(void);

// Nada mas para saber si nvs esta listo. Sirve para no romper pruebas con product_db sin nvs.
bool nvs_storage_is_ready(void);

esp_err_t nvs_storage_set_str(const char *key, const char *value);
esp_err_t nvs_storage_get_str(const char *key, char *buf, size_t buf_size);

esp_err_t nvs_storage_set_int(const char *key, int32_t value);
esp_err_t nvs_storage_get_int(const char *key, int32_t *out_value);

esp_err_t nvs_storage_set_blob(const char *key, const void *data, size_t data_len);
esp_err_t nvs_storage_get_blob(const char *key, void *buf, size_t *buf_size);

#endif
