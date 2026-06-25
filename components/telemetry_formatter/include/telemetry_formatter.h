#ifndef TELEMETRY_FORMATTER_H
#define TELEMETRY_FORMATTER_H

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#include "shared_types.h"

/* Payload de scan usado para comunicacion CAM -> LCD. */
bool telemetry_build_scan_payload(const char *device_id,
                                  Product product,
                                  const char *state,
                                  time_t timestamp,
                                  char *out,
                                  size_t out_size);

/* Payload compatible con ThingsBoard Integration para producto OK/MANUAL o ERROR. */
bool telemetry_build_telemetry_payload(Product product,
                                       const char *state,
                                       time_t timestamp,
                                       char *out,
                                       size_t out_size);

/* Payload por tandas para publicar el catalogo inicial con stock actual. */
bool telemetry_build_catalog_batch_payload(size_t start_index,
                                           size_t batch_size,
                                           time_t timestamp,
                                           char *out,
                                           size_t out_size,
                                           size_t *next_index);

size_t telemetry_catalog_size(void);

#endif // TELEMETRY_FORMATTER_H
