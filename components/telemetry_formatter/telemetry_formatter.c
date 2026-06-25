#include "telemetry_formatter.h"

#include "product_db.h"

#include <stdio.h>
#include <string.h>

static bool is_valid_buffer(char *out, size_t out_size)
{
    return out != NULL && out_size > 0;
}

bool telemetry_build_scan_payload(const char *device_id,
                                  Product product,
                                  const char *state,
                                  time_t timestamp,
                                  char *out,
                                  size_t out_size)
{
    if (!is_valid_buffer(out, out_size) || device_id == NULL || state == NULL) {
        return false;
    }

    int written = snprintf(out,
                           out_size,
                           "{\"device_id\":\"%s\",\"id\":\"%s\",\"producto\":\"%s\"," 
                           "\"stock\":%lu,\"timestamp\":%lld,\"estado\":\"%s\"}",
                           device_id,
                           product.id,
                           product.name,
                           (unsigned long)product.stock,
                           (long long)timestamp,
                           state);

    return written > 0 && (size_t)written < out_size;
}

bool telemetry_build_telemetry_payload(Product product,
                                       const char *state,
                                       time_t timestamp,
                                       char *out,
                                       size_t out_size)
{
    if (!is_valid_buffer(out, out_size) || state == NULL) {
        return false;
    }

    int written;

    if (strcmp(state, "ERROR") == 0) {
        written = snprintf(out,
                           out_size,
                           "{\"ts\":%lld,\"values\":{\"last_error\":\"%s\",\"last_estado\":\"ERROR\"}}",
                           (long long)timestamp * 1000LL,
                           product.name);
    } else {
        written = snprintf(out,
                           out_size,
                           "{\"ts\":%lld,\"values\":{\"%s\":%lu,\"last_product_id\":\"%s\"," 
                           "\"last_product_name\":\"%s\",\"last_estado\":\"%s\"}}",
                           (long long)timestamp * 1000LL,
                           product.name,
                           (unsigned long)product.stock,
                           product.id,
                           product.name,
                           state);
    }

    return written > 0 && (size_t)written < out_size;
}

bool telemetry_build_catalog_batch_payload(size_t start_index,
                                           size_t batch_size,
                                           time_t timestamp,
                                           char *out,
                                           size_t out_size,
                                           size_t *next_index)
{
    if (!is_valid_buffer(out, out_size) || batch_size == 0 || start_index >= CATALOGO_SIZE) {
        return false;
    }

    char values[640] = "";
    int len = 0;
    size_t i = start_index;

    for (size_t j = 0; j < batch_size && i < CATALOGO_SIZE; j++, i++) {
        Product product_db;
        uint32_t stock_actual = 0;

        if (product_db_find_by_id(catalogo_completo[i].id, &product_db)) {
            stock_actual = product_db.stock;
        }

        int written = snprintf(values + len,
                               sizeof(values) - (size_t)len,
                               "%s\"%s\":%lu",
                               (j == 0) ? "" : ",",
                               catalogo_completo[i].nombre,
                               (unsigned long)stock_actual);

        if (written < 0 || written >= (int)(sizeof(values) - (size_t)len)) {
            return false;
        }

        len += written;
    }

    int written = snprintf(out,
                           out_size,
                           "{\"ts\":%lld,\"values\":{%s}}",
                           (long long)timestamp * 1000LL,
                           values);

    if (next_index != NULL) {
        *next_index = i;
    }

    return written > 0 && (size_t)written < out_size;
}

size_t telemetry_catalog_size(void)
{
    return CATALOGO_SIZE;
}
