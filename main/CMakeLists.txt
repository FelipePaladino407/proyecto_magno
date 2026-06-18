idf_component_register(SRCS 
  "proyecto_final.c"
    "fsm_transition_test.c"
                    PRIV_REQUIRES fsm logger ntp_handler mqtt_handler input_handler
                    
                    INCLUDE_DIRS ".")
