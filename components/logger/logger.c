#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "esp_err.h"
#include "esp_log.h"

#include "logger.h"
#include "nvs.h"

static const char *TAG = "LOGGER";

static HistoryEntry buffer[LOGGER_SIZE];

// head marca donde esta el evento mas viejo guardado
// tail donde se va a guardar el proximo evento
// y count guarda cuantos eventos hay actualmente en el buffer

static int head = 0;
static int tail = 0;
static int count = 0;

static const char nvs_key_buffer[] = "logger_data";
static const char nvs_key_head[] = "logger_head";
static const char nvs_key_tail[] = "logger_tail";
static const char nvs_key_count[] = "logger_count";

static void logger_save_to_nvs(void)
{
    esp_err_t err;

    err = nvs_storage_set_blob(nvs_key_buffer, buffer, sizeof(buffer)); // Guarda el buffer entero en NVS
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "No se pudo guardar el buffer en NVS");
    }

    err = nvs_storage_set_int(nvs_key_head, head); // Guarda head en NVS
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "No se pudo guardar head en NVS");
    }

    err = nvs_storage_set_int(nvs_key_tail, tail); // Guarda tail en NVS
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "No se pudo guardar tail en NVS");
    }

    err = nvs_storage_set_int(nvs_key_count, count); // Guarda count en NVS
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "No se pudo guardar count en NVS");
    }
}

void logger_init(void)
{
    nvs_storage_init(); // Inicializa el almacenamiento NVS para guardar los eventos de forma persistente

    head = 0;
    tail = 0;
    count = 0;

    memset(buffer, 0, sizeof(buffer));

    size_t buffer_size = sizeof(buffer);
    esp_err_t err = nvs_storage_get_blob(nvs_key_buffer, buffer, &buffer_size); // Carga los eventos guardados en el buffer desde el almacenamiento NVS

    if (err != ESP_OK || buffer_size != sizeof(buffer)) {
        memset(buffer, 0, sizeof(buffer));
        ESP_LOGW(TAG, "No habia buffer valido en NVS, se arranca vacio");
    }

    int valor = 0;

    if (nvs_storage_get_int(nvs_key_head, &valor) == ESP_OK) {
        head = valor;
    }

    if (nvs_storage_get_int(nvs_key_tail, &valor) == ESP_OK) {
        tail = valor;
    }

    if (nvs_storage_get_int(nvs_key_count, &valor) == ESP_OK) {
        count = valor;
    }

    // Se validan los indices por si NVS tenia datos corruptos o viejos
    if (head < 0 || head >= LOGGER_SIZE) {
        head = 0;
    }

    if (tail < 0 || tail >= LOGGER_SIZE) {
        tail = 0;
    }

    if (count < 0 || count > LOGGER_SIZE) {
        count = 0;
    }

    ESP_LOGI(TAG, "Logger inicializado desde NVS");
}

bool logger_push(Product product, time_t timestamp, const char *state)
{
    if (timestamp == 0 || state == NULL) {
        return false;
    }

    memset(&buffer[tail], 0, sizeof(buffer[tail]));

    buffer[tail].product = product;

    buffer[tail].timestamp = timestamp;

    strncpy(buffer[tail].state, state, sizeof(buffer[tail].state) - 1);
    buffer[tail].state[sizeof(buffer[tail].state) - 1] = '\0';

    tail = (tail + 1) % LOGGER_SIZE; // Esto hace que el buffer sea circular: cuando tail llega al final vuelve a 0, en los otros casos el resto es tail+1

    if (count < LOGGER_SIZE) {
        count++; // Cuenta cuantos eventos hay guardados, hasta el maximo de LOGGER_SIZE
    } else { // Cuando count = LOGGER_SIZE, el buffer ya esta lleno, entonces al guardar un nuevo evento se sobrescribe el mas viejo, que es el que marca head. Por eso se avanza head tambien
        head = (head + 1) % LOGGER_SIZE; // Vuelve a 0 igual que tail
        ESP_LOGW(TAG, "Logger lleno: se sobrescribio el evento mas viejo");
    }

    logger_save_to_nvs(); // Cada vez que se guarda un evento, tambien se actualiza NVS

    ESP_LOGI(TAG,
             "Guardado en logger: %s | %s | %lu | %lld | %s",
             product.id,
             product.name,
             (unsigned long)product.stock,
             (long long)timestamp,
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
                 "[%d] %s | %s | %lu | %lld | %s",
                 i,
                 buffer[index].product.id,
                 buffer[index].product.name,
                 (unsigned long)buffer[index].product.stock,
                 (long long)buffer[index].timestamp,
                 buffer[index].state);
    }
}

// Simplificacion de la referencia: https://embeddedartistry.com/blog/2017/05/17/creating-a-circular-buffer-in-c-and-c/

