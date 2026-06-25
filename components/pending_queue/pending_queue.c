#include "pending_queue.h"

#include "esp_err.h"
#include "esp_log.h"
#include "nvs.h"

#include <stdint.h>
#include <string.h>

static const char *TAG = "PENDING_QUEUE";

static PendingMqttEvent s_buffer[PENDING_QUEUE_SIZE];
static int s_head = 0;
static int s_tail = 0;
static int s_count = 0;
static bool s_initialized = false;

static const char *NVS_KEY_BUFFER = "pend_buf";
static const char *NVS_KEY_HEAD = "pend_head";
static const char *NVS_KEY_TAIL = "pend_tail";
static const char *NVS_KEY_COUNT = "pend_count";

static void safe_copy(char *dest, const char *src, size_t dest_size)
{
    if (dest == NULL || dest_size == 0) {
        return;
    }

    if (src == NULL) {
        dest[0] = '\0';
        return;
    }

    strncpy(dest, src, dest_size - 1);
    dest[dest_size - 1] = '\0';
}

static bool queue_is_valid(void)
{
    return s_head >= 0 && s_head < PENDING_QUEUE_SIZE &&
           s_tail >= 0 && s_tail < PENDING_QUEUE_SIZE &&
           s_count >= 0 && s_count <= PENDING_QUEUE_SIZE;
}

static bool pending_queue_save(void)
{
    esp_err_t err;
    bool ok = true;

    err = nvs_storage_set_blob(NVS_KEY_BUFFER, s_buffer, sizeof(s_buffer));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "No se pudo guardar buffer pendiente en NVS: %s", esp_err_to_name(err));
        ok = false;
    }

    err = nvs_storage_set_int(NVS_KEY_HEAD, (int32_t)s_head);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "No se pudo guardar head pendiente en NVS: %s", esp_err_to_name(err));
        ok = false;
    }

    err = nvs_storage_set_int(NVS_KEY_TAIL, (int32_t)s_tail);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "No se pudo guardar tail pendiente en NVS: %s", esp_err_to_name(err));
        ok = false;
    }

    err = nvs_storage_set_int(NVS_KEY_COUNT, (int32_t)s_count);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "No se pudo guardar count pendiente en NVS: %s", esp_err_to_name(err));
        ok = false;
    }

    return ok;
}

static void pending_queue_reset(void)
{
    memset(s_buffer, 0, sizeof(s_buffer));
    s_head = 0;
    s_tail = 0;
    s_count = 0;
}

bool pending_queue_init(void)
{
    esp_err_t err = nvs_storage_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo inicializar NVS para cola pendiente: %s", esp_err_to_name(err));
        return false;
    }

    pending_queue_reset();

    size_t buffer_size = sizeof(s_buffer);
    err = nvs_storage_get_blob(NVS_KEY_BUFFER, s_buffer, &buffer_size);
    if (err != ESP_OK || buffer_size != sizeof(s_buffer)) {
        ESP_LOGW(TAG, "No habia cola pendiente valida en NVS, arranca vacia");
        pending_queue_reset();
        pending_queue_save();
        s_initialized = true;
        return true;
    }

    int32_t value = 0;
    if (nvs_storage_get_int(NVS_KEY_HEAD, &value) == ESP_OK) {
        s_head = (int)value;
    }

    if (nvs_storage_get_int(NVS_KEY_TAIL, &value) == ESP_OK) {
        s_tail = (int)value;
    }

    if (nvs_storage_get_int(NVS_KEY_COUNT, &value) == ESP_OK) {
        s_count = (int)value;
    }

    if (!queue_is_valid()) {
        ESP_LOGW(TAG, "Indices invalidos en cola pendiente. Se reinicia cola");
        pending_queue_reset();
        pending_queue_save();
    }

    s_initialized = true;

    ESP_LOGI(TAG,
             "Cola pendiente inicializada desde NVS: head=%d tail=%d count=%d",
             s_head,
             s_tail,
             s_count);

    return true;
}

bool pending_queue_push(Product product, time_t timestamp, const char *state, const char *topic)
{
    if (!s_initialized && !pending_queue_init()) {
        return false;
    }

    if (timestamp == 0 || state == NULL || topic == NULL || topic[0] == '\0') {
        ESP_LOGW(TAG, "Evento pendiente invalido, no se guarda");
        return false;
    }

    PendingMqttEvent *slot = &s_buffer[s_tail];
    memset(slot, 0, sizeof(*slot));

    slot->product = product;
    slot->timestamp = timestamp;
    safe_copy(slot->state, state, sizeof(slot->state));
    safe_copy(slot->topic, topic, sizeof(slot->topic));

    s_tail = (s_tail + 1) % PENDING_QUEUE_SIZE;

    if (s_count < PENDING_QUEUE_SIZE) {
        s_count++;
    } else {
        s_head = (s_head + 1) % PENDING_QUEUE_SIZE;
        ESP_LOGW(TAG, "Cola pendiente llena: se sobrescribio el evento mas viejo");
    }

    if (!pending_queue_save()) {
        return false;
    }

    ESP_LOGI(TAG,
             "Evento pendiente guardado -> topic=%s | id=%s | nombre=%s | stock=%lu | timestamp=%lld | estado=%s | count=%d",
             slot->topic,
             slot->product.id,
             slot->product.name,
             (unsigned long)slot->product.stock,
             (long long)slot->timestamp,
             slot->state,
             s_count);

    return true;
}

bool pending_queue_peek(PendingMqttEvent *out_event)
{
    if (!s_initialized && !pending_queue_init()) {
        return false;
    }

    if (out_event == NULL || s_count == 0) {
        return false;
    }

    *out_event = s_buffer[s_head];
    return true;
}

bool pending_queue_pop(void)
{
    if (!s_initialized && !pending_queue_init()) {
        return false;
    }

    if (s_count == 0) {
        return false;
    }

    memset(&s_buffer[s_head], 0, sizeof(s_buffer[s_head]));
    s_head = (s_head + 1) % PENDING_QUEUE_SIZE;
    s_count--;

    return pending_queue_save();
}

int pending_queue_count(void)
{
    if (!s_initialized && !pending_queue_init()) {
        return 0;
    }

    return s_count;
}

void pending_queue_print(void)
{
    if (!s_initialized && !pending_queue_init()) {
        return;
    }

    ESP_LOGI(TAG, "Eventos pendientes en NVS: %d", s_count);

    for (int i = 0; i < s_count; i++) {
        int index = (s_head + i) % PENDING_QUEUE_SIZE;
        PendingMqttEvent *event = &s_buffer[index];

        ESP_LOGI(TAG,
                 "[%d] topic=%s | id=%s | nombre=%s | stock=%lu | timestamp=%lld | estado=%s",
                 i,
                 event->topic,
                 event->product.id,
                 event->product.name,
                 (unsigned long)event->product.stock,
                 (long long)event->timestamp,
                 event->state);
    }
}
