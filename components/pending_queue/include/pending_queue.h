#ifndef PENDING_QUEUE_H
#define PENDING_QUEUE_H

#include <stdbool.h>
#include <time.h>

#include "shared_types.h"

#define PENDING_QUEUE_SIZE 20
#define PENDING_TOPIC_MAX_LEN 64

typedef struct {
    Product product;
    time_t timestamp;
    char state[16];
    char topic[PENDING_TOPIC_MAX_LEN];
} PendingMqttEvent;

bool pending_queue_init(void);
bool pending_queue_push(Product product, time_t timestamp, const char *state, const char *topic);
bool pending_queue_peek(PendingMqttEvent *out_event);
bool pending_queue_pop(void);
int pending_queue_count(void);
void pending_queue_print(void);

#endif // PENDING_QUEUE_H
