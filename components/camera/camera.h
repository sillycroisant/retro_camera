#ifndef CAMERA_H
#define CAMERA_H

#include "esp_err.h"
#include "stdbool.h"
#include "camera_pinout.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum 
{
    CAMERA_CAPTURE_PHOTO = 0,
    CAMERA_CAPTURE_VIDEO,
    CAMERA_CAPTURE_MODE_COUNT

} camera_capture_mode_t;

esp_err_t camera_init(void);

esp_err_t camera_start(void);

camera_capture_mode_t camera_get_capture_mode(void);

esp_err_t camera_capture_photo(void);

bool camera_flash_enabled(void);

#ifdef __cplusplus
}
#endif

#endif