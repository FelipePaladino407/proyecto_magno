idf_component_register(SRCS "fsm.c"
                      REQUIRES mqtt_handler shared 
                       INCLUDE_DIRS "include"
                     )
