idf_component_register(
    SRCS
        "lcd_port.c"
    INCLUDE_DIRS "include"
    REQUIRES
        esp_lcd
        driver
        freertos
        lvgl
        esp_timer
        lcd_manager
)
