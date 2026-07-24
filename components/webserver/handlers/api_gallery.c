#include "api_gallery.h"

#include "esp_log.h"

static const char *TAG = "[API Gallery]";

static esp_err_t gallery_get_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "GET /api/gallery");

    httpd_resp_set_type(req, "application/json");

    httpd_resp_sendstr(
        req,
        "{"
        "\"photos\":[]"
        "}");

    return ESP_OK;
}

static const httpd_uri_t gallery_uri =
{
    .uri = "/api/gallery",
    .method = HTTP_GET,
    .handler = gallery_get_handler,
    .user_ctx = NULL
};

esp_err_t api_gallery_register(httpd_handle_t server)
{
    if(server == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    return httpd_register_uri_handler(
                server,
                &gallery_uri);
}