#ifndef LITTLE_FS_H
#define LITTLE_FS_H

#include "esp_err.h"

/**
 * @brief  Mount the LittleFS partition.
 *
 * Formats the partition automatically if it is not yet formatted.
 *
 * @return ESP_OK on success, or an esp_err_t error code.
 */
esp_err_t storage_init(void);

/**
 * @brief  Return true if the filesystem is currently mounted.
 */
bool storage_is_mounted(void);

#endif
