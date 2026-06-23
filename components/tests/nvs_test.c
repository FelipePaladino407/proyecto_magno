#include "esp_err.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <inttypes.h>
#include <string.h>

static const char *TAG = "NVS_TEST";

void nvs_test(void) {
    ESP_LOGI(TAG, "===== NVS TEST START =====");

    // Init
    if (nvs_storage_init() == ESP_OK)
        ESP_LOGI(TAG, "[PASS] Init");
    else {
        ESP_LOGE(TAG, "[FAIL] Init");
        return;
    }

    // Clear
    if (nvs_storage_clear() == ESP_OK)
        ESP_LOGI(TAG, "[PASS] Clear");
    else
        ESP_LOGE(TAG, "[FAIL] Clear");

    // String
    char str[32];
    if (nvs_storage_set_str("device", "ESP32S2") == ESP_OK &&
        nvs_storage_get_str("device", str, sizeof(str)) == ESP_OK && strcmp(str, "ESP32S2") == 0) {
        ESP_LOGI(TAG, "[PASS] String: %s", str);
    } else {
        ESP_LOGE(TAG, "[FAIL] String");
    }

    // Integer
    int32_t role = 0;
    if (nvs_storage_set_int("role", 1) == ESP_OK && nvs_storage_get_int("role", &role) == ESP_OK && role == 1) {
        ESP_LOGI(TAG, "[PASS] Integer: %" PRId32, role);
    } else {
        ESP_LOGE(TAG, "[FAIL] Integer");
    }

    // Blob
    uint8_t tx[] = {1, 2, 3, 4};
    uint8_t rx[4] = {0};
    size_t len = sizeof(rx);

    if (nvs_storage_set_blob("blob", tx, sizeof(tx)) == ESP_OK && nvs_storage_get_blob("blob", rx, &len) == ESP_OK &&
        len == sizeof(tx) && memcmp(tx, rx, sizeof(tx)) == 0) {
        ESP_LOGI(TAG, "[PASS] Blob");
    } else {
        ESP_LOGE(TAG, "[FAIL] Blob");
    }

    // Missing key
    if (nvs_storage_get_int("does_not_exist", &role) == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "[PASS] Missing key");
    } else {
        ESP_LOGE(TAG, "[FAIL] Missing key");
    }

    ESP_LOGI(TAG, "===== NVS TEST END =====");
}
