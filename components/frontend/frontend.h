#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t frontend_init(void);

esp_err_t frontend_sync_to_sd(void);

#ifdef __cplusplus
}
#endif