idf_component_register(SRCS "touchpad.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_driver_touch_sens
                       REQUIRES freertos
                       REQUIRES fsm)