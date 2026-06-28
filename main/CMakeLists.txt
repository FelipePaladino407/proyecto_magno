idf_component_register(SRCS "proyecto_final.c"
                        PRIV_REQUIRES
                            wifi_manager
                            http_handler
                            core
                            logger
                            mqtt_handler
                            tests
                            pending_queue
                            nvs_flash
                        INCLUDE_DIRS ".")
