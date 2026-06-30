idf_component_register(
    SRCS "mqtt_handler.c"
    INCLUDE_DIRS "include"
    REQUIRES mqtt logger ntp_handler pending_queue product_db fsm shared freertos
)

