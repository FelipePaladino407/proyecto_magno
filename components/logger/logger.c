#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "logger.h"

static const char *TAG = "LOGGER";

static HistoryEntry buffer[LOGGER_SIZE];

// head marca donde está el evento más viejo guardado
// tail donde se va a guardar el proximo evento
// y count guarda cuantos eventos hay actualmente en el buffer

static int head = 0;
static int tail = 0;
static int count = 0;

void logger_init(void)
{
    head = 0;
    tail = 0;
    count = 0;

    memset(buffer, 0, sizeof(buffer));

    ESP_LOGI(TAG, "Logger inicializado");
}

bool logger_push(Product producto, const uint32_t timestamp, const char state[16])
{
    if (timestamp == 0 || state == NULL) {
        return false;
    }

    buffer[tail].product = producto;
    buffer[tail].timestamp = timestamp;
    strncpy(buffer[tail].state, state, sizeof(buffer[tail].state) - 1);
    // Tambien se copia el estado del evento: OK, MANUAL o ERROR.

    tail = (tail + 1) % LOGGER_SIZE; // Esto hace que el buffer sea circular: cuando tail llega al final vuelve a 0, en los otros casos el resto es tail+1

    if (count < LOGGER_SIZE) {
        count++; // Cuenta cuantos eventos hay guardados, hasta el maximo de LOGGER_SIZE
    } else { // Cuando count = LOGGER_SIZE, el buffer ya está lleno, entonces al guardar un nuevo evento se sobrescribe el mas viejo, que es el que marca head. Por eso se avanza head tambien
        head = (head + 1) % LOGGER_SIZE; // Vuelve a 0 igual que tail
        ESP_LOGW(TAG, "Logger lleno: se sobrescribió el evento más viejo");
    }

    ESP_LOGI(TAG,
             "Guardado en logger: %s | %s | %lu | %s | %s",
             producto.id,
             producto.name,
             (unsigned long)producto.stock,
             timestamp,
             state);

    return true;
}

int logger_count(void)
{
    return count;
}

void logger_print(void)
{
    ESP_LOGI(TAG, "Eventos en logger: %d", count);

    for (int i = 0; i < count; i++) {
        int index = (head + i) % LOGGER_SIZE;

        ESP_LOGI(TAG,
                 "[%d] %s | %s | %lu | %s | %s",
                 i,
                 buffer[index].product.id,
                 buffer[index].product.name,
                 (unsigned long)buffer[index].product.stock,
                 buffer[index].timestamp,
                 buffer[index].state);
    }
}

// Simplificación de la referencia: https://embeddedartistry.com/blog/2017/05/17/creating-a-circular-buffer-in-c-and-c/
