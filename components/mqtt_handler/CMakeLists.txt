idf_component_register(
    SRCS "mqtt_handler.c"
    INCLUDE_DIRS "include"
    REQUIRES mqtt logger ntp_handler pending_queue telemetry_formatter core shared freertos core
)
