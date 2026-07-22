#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct{
    char ssid[32];
    char password[64];
} network_config_t;

esp_err_t network_init(const network_config_t *cfg);

#ifdef __cplusplus
}
#endif