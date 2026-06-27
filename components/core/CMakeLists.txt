idf_component_register(
                SRCS "src/fsm.c" "src/ev_queue.c" "src/core.c"
                REQUIRES product_db shared freertos
                INCLUDE_DIRS "include"
                )
