#ifndef NTP_HANDLER_H
#define NTP_HANDLER_H

#include <stdbool.h>
#include <time.h>
#include "esp_err.h"

esp_err_t ntp_clock_init(void); // Inicializa la hora por internet usando NTP

bool get_timestamp(time_t *timestamp); // Guarda la hora actual en formato time_t y devuelve true si pudo hacerlo bien

#endif
