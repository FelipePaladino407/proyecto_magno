#pragma once
#ifndef QR_HANDLER
#define QR_HANDLER

/*
 * Callback avisado cuando se decodifica un QR valido.
 * El qr_handler no sabe si el receptor es la FSM LCD o MQTT CAM.
 */
typedef void (*qr_detected_callback_t)(const char *id, const char *name);

void qr_handler_set_callback(qr_detected_callback_t callback);
void qr_handler_init(void);

#endif // !QR_HANDLER
