idf_component_register(
    SRCS "proyecto_final.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES fsm wifi_manager rgb_led led_strip logger ntp_handler mqtt_handler input_handler nvs http_handler
)
