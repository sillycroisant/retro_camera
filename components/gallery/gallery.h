#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t gallery_init(void);

esp_err_t gallery_start(void);
#ifdef __cplusplus
}
#endif