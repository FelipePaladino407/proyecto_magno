#ifndef DEVICE_ROLE_H
#define DEVICE_ROLE_H

#include "sdkconfig.h"

#define DEVICE_ROLE_LCD 1
#define DEVICE_ROLE_CAM 2

/*
 * Rol de firmware.
 *
 * Default seguro: LCD, para no romper el flujo principal de la demo.
 * Se puede cambiar desde menuconfig:
 *   Proyecto Magno -> Rol de esta placa
 *
 * Tambien se puede forzar por compilacion definiendo DEVICE_ROLE.
 */
#ifndef DEVICE_ROLE
#  ifdef CONFIG_PROYECTO_DEVICE_ROLE_CAM
#    if CONFIG_PROYECTO_DEVICE_ROLE_CAM
#      define DEVICE_ROLE DEVICE_ROLE_CAM
#    else
#      define DEVICE_ROLE DEVICE_ROLE_LCD
#    endif
#  else
#    define DEVICE_ROLE DEVICE_ROLE_LCD
#  endif
#endif

#ifdef DEVICE_IS_LCD
#  undef DEVICE_IS_LCD
#endif
#ifdef DEVICE_IS_CAM
#  undef DEVICE_IS_CAM
#endif

#define DEVICE_IS_LCD (DEVICE_ROLE == DEVICE_ROLE_LCD)
#define DEVICE_IS_CAM (DEVICE_ROLE == DEVICE_ROLE_CAM)

#endif // DEVICE_ROLE_H
