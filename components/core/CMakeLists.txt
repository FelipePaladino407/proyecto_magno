idf_component_register(
                SRCS "src/fsm.c" "src/ev_queue.c" "src/core.c"
                REQUIRES
                    product_db
                    shared
                    freertos
                    wifi_manager
                    rgb_led
                    led_strip
                    http_handler
                    core
                    logger
                    ntp_handler
                    mqtt_handler
                    input_handler
                    qr_handler
                    pending_queue
                    lcd_port
                    lcd_manager
                INCLUDE_DIRS "include"
                )
