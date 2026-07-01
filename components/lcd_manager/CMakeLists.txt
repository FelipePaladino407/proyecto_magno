idf_component_register(
    SRCS "lcd_manager.c"
    INCLUDE_DIRS "include"
    REQUIRES lcd_port lvgl shared
)
