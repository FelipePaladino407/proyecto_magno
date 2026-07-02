idf_component_register(
    SRCS "mqtt_handler.c"
    INCLUDE_DIRS "include"
    REQUIRES mqtt logger ntp_handler product_db pending_queue fsm shared freertos
)

# El rol de la placa se elige desde:
#   idf.py menuconfig -> Proyecto Magno -> Rol de esta placa
# Default: Placa LCD + Touch.
