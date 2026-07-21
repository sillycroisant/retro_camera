#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t sdcard_init(void);

esp_err_t sdcard_deinit(void);

esp_err_t sdcard_save_file(
    const char *filename,
    const uint8_t *data,
    size_t len);
    
#ifdef __cplustplus
}
#endif

