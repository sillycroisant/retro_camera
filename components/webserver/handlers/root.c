#include "root.h"
#include "esp_log.h"

static const char *TAG = "ROOT";

// GET
static esp_err_t root_get_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "GET /");

    httpd_resp_set_status(req, "302 Found");

    httpd_resp_set_hdr(req, "Location", "/index.html");
    
    httpd_resp_send(req, NULL, 0);

    return ESP_OK;
};

// URI
static const httpd_uri_t root_uri = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = root_get_handler,
    .user_ctx = NULL
};

esp_err_t root_register(httpd_handle_t server)
{
    if(server == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Register /");

    return httpd_register_uri_handler(server, &root_uri);
}