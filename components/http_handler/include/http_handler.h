#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include "esp_err.h"

// Inicia el servidor HTTP y registra todos los endpoints.
esp_err_t http_handler_start(void);

// Detiene el servidor HTTP, si esta iniciado. 
esp_err_t http_handler_stop(void);

#endif
