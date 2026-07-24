#include "api_status.h"

#include "esp_log.h"

static const char *TAG = "[API_status]";

// GET /api/status
static esp_err_t status_get_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "GET /api/status");

    const char *json =
        "{"
        "\"status\":\"ok\""
        "}";

    httpd_resp_set_type(req, "application/json");

    httpd_resp_send(
        req,
        json,
        HTTPD_RESP_USE_STRLEN);

    return ESP_OK;
}

// URI
static const httpd_uri_t status_uri =
{
    .uri      = "/api/status",
    .method   = HTTP_GET,
    .handler  = status_get_handler,
    .user_ctx = NULL
};

// Register
esp_err_t api_status_register(httpd_handle_t server)
{
    if(server == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Register /api/status");

    return httpd_register_uri_handler(
                server,
                &status_uri);
}