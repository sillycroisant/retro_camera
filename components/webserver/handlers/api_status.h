#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
    #endif

esp_err_t api_status_register(httpd_handle_t server);

#ifdef __cplusplus
}
#endif