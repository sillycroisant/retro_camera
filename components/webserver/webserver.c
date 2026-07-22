#include "webserver.h"

#include "esp_http_server.h"
#include "esp_log.h"

static const char *TAG = "[WebServer]";
static httpd_handle_t server = NULL;


esp_err_t webserver_start(void){
    if (server != NULL){
        ESP_LOGW(TAG, "Server already started.");
        return ESP_OK;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    ESP_LOGI(TAG, "Starting HTTP server...");

    esp_err_t ret = httpd_start(&server, &config);

    if (ret != ESP_OK){
        ESP_LOGE(TAG, "Cannot start server");
        server = NULL;
        return ret;
    }

    ESP_LOGI(TAG, "HTTP server started");
    return ESP_OK;
}


esp_err_t webserver_stop(void){
    if(server == NULL){
        return ESP_OK;
    }

    esp_err_t ret = httpd_stop(server);

    server = NULL;
    return ret;
}