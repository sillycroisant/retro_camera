#pragma once

#include "esp_http_server.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t root_register(httpd_handle_t server);

#ifdef __cplusplus
}
#endif