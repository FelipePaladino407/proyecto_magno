idf_component_register(
                SRCS "src/fsm.c" "src/ev_queue.c" "src/app_logic.c" "src/app_logic_a.c" "src/app_logic_b.c"
                REQUIRES product_db shared freertos
                INCLUDE_DIRS "include"
                )
