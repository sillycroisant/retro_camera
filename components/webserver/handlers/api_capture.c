#include "api_capture.h"

#include "esp_log.h"

static const char *TAG = "[API Capture]";

static esp_err_t capture_post_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "POST /api/capture");

    httpd_resp_set_type(req, "application/json");

    httpd_resp_sendstr(
        req,
        "{"
        "\"success\":true,"
        "\"message\":\"Capture API stub\""
        "}");

    return ESP_OK;
}

static const httpd_uri_t capture_uri =
{
    .uri = "/api/capture",
    .method = HTTP_POST,
    .handler = capture_post_handler,
    .user_ctx = NULL
};

esp_err_t api_capture_register(httpd_handle_t server)
{
    if(server == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    return httpd_register_uri_handler(
                server,
                &capture_uri);
}