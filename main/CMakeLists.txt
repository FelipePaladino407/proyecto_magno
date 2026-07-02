idf_component_register(SRCS 
  "proyecto_final.c"
                    PRIV_REQUIRES
                    wifi_manager
                    rgb_led
                    led_strip
                    http_handler
                    fsm
                    logger
                    ntp_handler
                    mqtt_handler
                    input_handler
                    unit_test
                    product_db
                    pending_queue
                    touchpad
                    lcd_port
                    lcd_manager
                    shared
                    
                    qr_handler
                    INCLUDE_DIRS ".")
