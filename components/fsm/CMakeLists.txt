idf_component_register(SRCS "fsm.c"
  REQUIRES product_db shared freertos 
                       INCLUDE_DIRS "include"
                     )
