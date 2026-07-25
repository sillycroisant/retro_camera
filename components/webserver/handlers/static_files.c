#include "static_files.h"

#include "storage.h"
#include "esp_log.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "StaticFiles";

static const char *get_content_type(const char *uri) {

    const char *ext = strrchr(uri, '.');

    if(ext == NULL) return "application/octet-stream";
    
    if(strcmp(ext, ".html") == 0) return "text/html";
    if(strcmp(ext, ".css") == 0)  return "text/css";
    if(strcmp(ext, ".js") == 0)   return "application/javascript";
    if(strcmp(ext, ".jpg") == 0)  return "image/jpeg";
    if(strcmp(ext, ".jpeg") == 0) return "image/jpeg";
    if(strcmp(ext, ".png") == 0)  return "image/png";
    if(strcmp(ext, ".ico") == 0)  return "image/x-icon";

    return "application/octet-stream";
}

static esp_err_t static_get_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "GET %s", req->uri);

    FILE *fp = NULL;

    esp_err_t err  = storage_open_uri(req->uri, &fp);

    if (err != ESP_OK){
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, get_content_type(req->uri));
    uint8_t buffer[4096];

    while(1) {
        size_t bytes = fread(buffer, 1, sizeof(buffer), fp);
        if (bytes == 0) {
            break;
        }

        err = httpd_resp_send_chunk(req, (const char *)buffer, bytes);

        if (err != ESP_OK) {
            storage_close(fp);
            return err;
        }
    }

    storage_close(fp);

    httpd_resp_send_chunk(req, NULL, 0);

    return ESP_OK;
}


static const httpd_uri_t photo_uri = 
{
    .uri = "/photos/*",
    .method = HTTP_GET,
    .handler = static_get_handler,
    .user_ctx = NULL
};

static const httpd_uri_t static_uri = 
{
    .uri = "/static/*",
    .method = HTTP_GET,
    .handler = static_get_handler,
    .user_ctx = NULL
};

esp_err_t static_files_register(httpd_handle_t server){

    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &photo_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &static_uri));

    ESP_LOGI(TAG, "Static file handlers registered");

    return ESP_OK;
}
