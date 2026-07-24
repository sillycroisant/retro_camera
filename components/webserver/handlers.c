#include "handlers.h"

#include "esp_log.h"
#include "esp_http_server.h"

static const char *TAG = "[Handlers]";

static esp_err_t root_get_handler(httpd_req_t *req)
{
    const char response[] = "Retro Camera webserver running";

    httpd_resp_set_type(req, "text/plain");

    httpd_resp_send(
        req,
        response,
        HTTPD_RESP_USE_STRLEN);

    ESP_LOGI(TAG, "GET /");

    return ESP_OK;
}

static const httpd_uri_t root_uri = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = root_get_handler,
    .user_ctx = NULL
};

esp_err_t handlers_register(httpd_handle_t server)
{
    return httpd_register_uri_handler(
        server,
        &root_uri);
}