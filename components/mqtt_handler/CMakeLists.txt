
idf_component_register(
    SRCS "mqtt_handler.c"
    INCLUDE_DIRS "include"
    REQUIRES mqtt logger ntp_handler product_db pending_queue fsm shared freertos
)

# Para compilar este mismo handler como CAM, descomentar:
# target_compile_definitions(${COMPONENT_LIB} PRIVATE DEVICE_IS_LCD=0)

# Para compilarlo como LCD, dejar el default del .c o descomentar:
# target_compile_definitions(${COMPONENT_LIB} PRIVATE DEVICE_IS_LCD=1)
