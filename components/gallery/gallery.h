#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t gallery_init(void);

esp_err_t gallery_start(void);

void gallery_display_current_photo(void);

#ifdef __cplusplus
}
#endif