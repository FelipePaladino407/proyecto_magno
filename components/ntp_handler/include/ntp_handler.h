#ifndef NTP_HANDLER_H
#define NTP_HANDLER_H

#include <stdbool.h>
#include <time.h>

void init_time(void); // Inicializa la hora por internet usando NTP

bool get_timestamp(time_t *timestamp); // Guarda la hora actual en formato time_t y devuelve true si pudo hacerlo bien

#endif