#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Inicia el servidor HTTP y registra todos los endpoints. */
esp_err_t web_server_start(void);

/* Detiene el servidor HTTP, si esta iniciado. */
esp_err_t web_server_stop(void);

#ifdef __cplusplus
}
#endif

#endif