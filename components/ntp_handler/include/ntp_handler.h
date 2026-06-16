#ifndef NTP_HANDLER_H
#define NTP_HANDLER_H

#include <stdbool.h>
#include <stddef.h>

void init_time(void);  // Inicializa la hora por internet usando NTP y configura la zona horaria de Uruguay

bool get_fecha_hora(char *fecha_hora, size_t max_len); // Guarda la fecha y hora actual en el buffer recibido y devuelve true si pudo hacerlo bien

#endif