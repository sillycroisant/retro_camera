#include "handlers.h"

#include "root.h"
#include "api_status.h"
#include "api_capture.h"
#include "api_gallery.h"
#include "static_files.h"

#include "esp_log.h"

static const char *TAG = "[Handlers]";

esp_err_t handlers_register(httpd_handle_t server)
{
    if (server == NULL){
        return ESP_ERR_INVALID_ARG;
    };

    ESP_LOGI(TAG, "Registering handlers...");

    ESP_ERROR_CHECK(root_register(server));

    ESP_ERROR_CHECK(api_status_register(server));
    // 
    ESP_ERROR_CHECK(api_capture_register(server));
    
    ESP_ERROR_CHECK(api_gallery_register(server));

    // static register in the last
    ESP_ERROR_CHECK(static_files_register(server));

    ESP_LOGI(TAG, "ALL Handlers registered.");

    return ESP_OK;
}