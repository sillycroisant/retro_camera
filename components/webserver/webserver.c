#include "webserver.h"
#include "handlers.h"

#include "esp_http_server.h"
#include "esp_log.h"

static const char *TAG = "[WebServer]";

// context
typedef struct 
{
    httpd_handle_t handle;
} webserver_context_t;

static webserver_context_t g_server = {
    .handle = NULL
};

esp_err_t webserver_start(void)
{
    if (g_server.handle != NULL){
        ESP_LOGW(TAG, "Server already started.");
        return ESP_OK;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    ESP_LOGI(TAG, "Starting HTTP server...");

    ESP_ERROR_CHECK(httpd_start(&g_server.handle, &config));

    ESP_ERROR_CHECK(handlers_register(g_server.handle));

    ESP_LOGI(TAG, "HTTP server started");
    return ESP_OK;
}


esp_err_t webserver_stop(void){
    if(g_server.handle == NULL){
        return ESP_OK;
    }

    httpd_stop(g_server.handle);

    g_server.handle = NULL;
    
    return ESP_OK;
}