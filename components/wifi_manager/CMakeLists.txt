idf_component_register(
    SRCS "wifi_manager.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_wifi esp_event esp_netif freertos fsm nvs rgb_led led_strip
)