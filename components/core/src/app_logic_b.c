#include "app_logic.h"
#include "esp_log.h"
#include "ev_queue.h"
#include "product_db.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "APP_B";

static void on_link_scan_received(void) {
    Product pending;
    if (app_logic_get_active_product(&pending)) {
        ESP_LOGI(TAG, "Scan recibido de Board A -> ID=%s", pending.id);
    }
}
