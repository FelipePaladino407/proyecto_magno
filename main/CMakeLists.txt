idf_component_register(SRCS "proyecto_final.c"
                        PRIV_REQUIRES
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
                            tests
                            product_db
                            pending_queue
                        INCLUDE_DIRS ".")
