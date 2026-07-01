#pragma once
#include "esp_camera.h"

#define CAM_PIN_XCLK 1
#define CAM_PIN_SIOD 8
#define CAM_PIN_SIOC 7

#define CAM_PIN_D0 46
#define CAM_PIN_D1 45
#define CAM_PIN_D2 41
#define CAM_PIN_D3 39
#define CAM_PIN_D4 40
#define CAM_PIN_D5 38
#define CAM_PIN_D6 42
#define CAM_PIN_D7 21

#define CAM_PIN_VSYNC 2
#define CAM_PIN_HREF 3
#define CAM_PIN_PCLK 33

static const camera_config_t QR_CAMERA_CONFIG = {
    .pin_pwdn = -1,
    .pin_reset = -1,
    .pin_xclk = CAM_PIN_XCLK,
    .pin_sccb_sda = CAM_PIN_SIOD,
    .pin_sccb_scl = CAM_PIN_SIOC,

    .pin_d0 = CAM_PIN_D0,
    .pin_d1 = CAM_PIN_D1,
    .pin_d2 = CAM_PIN_D2,
    .pin_d3 = CAM_PIN_D3,
    .pin_d4 = CAM_PIN_D4,
    .pin_d5 = CAM_PIN_D5,
    .pin_d6 = CAM_PIN_D6,
    .pin_d7 = CAM_PIN_D7,

    .pin_vsync = CAM_PIN_VSYNC,
    .pin_href = CAM_PIN_HREF,
    .pin_pclk = CAM_PIN_PCLK,

    .xclk_freq_hz = 10000000,
    .ledc_timer = LEDC_TIMER_0,
    .ledc_channel = LEDC_CHANNEL_0,

    .pixel_format = PIXFORMAT_GRAYSCALE,
    .frame_size = FRAMESIZE_QQVGA,

    .fb_count = 1,
    .fb_location = CAMERA_FB_IN_PSRAM,
    .grab_mode = CAMERA_GRAB_LATEST,
    .sccb_i2c_port = 1,
};
