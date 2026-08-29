#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_camera.h"
#include "esp_log.h"

// #include "webserver.h"
// #include "network.h"
// #include "frontend.h"

#include "camera.h"
#include "storage.h"
#include "input.h"
#include "events.h"
#include "mode.h"

static const char *TAG = "Main";

static esp_err_t publish_camera_event(camera_event_type_t type)
{
    event_t event = {
        .channel = EVENT_CHANNEL_CAMERA,
        .type.camera = type,
    };

    return events_publish(&event);
}

static void camera_test_task(void *arg)
{
    while(true)
    {
        ESP_LOGI(TAG, "=== capture PHOTO ===");
        if(publish_camera_event(CAMERA_EVENT_CAPTURE) != ESP_OK){
            ESP_LOGE(TAG, "Failed to publish photo capture event");
        }

        vTaskDelay(pdMS_TO_TICKS(3000));

        ESP_LOGI(TAG,"=== switch photo -> video ===");
        if(publish_camera_event(CAMERA_EVENT_TOGGLE_VIDEO)!= ESP_OK){
            ESP_LOGE(TAG, "Failed tp toggle capture mode");
        }
        ESP_LOGI(TAG,"=== start VIDEO recording ===");
        if(publish_camera_event(CAMERA_EVENT_CAPTURE) != ESP_OK){
            ESP_LOGE(TAG, "Failed to start video");
        }

        vTaskDelay(pdMS_TO_TICKS(5000));
        ESP_LOGI(TAG, "=== Stop VIDEO recording ===");
        if (publish_camera_event(CAMERA_EVENT_CAPTURE) != ESP_OK){
            ESP_LOGE(TAG, "Failed to stop video");
        }

        ESP_LOGI(TAG, "=== Switch VIDEO -> PHOTO ===");
        if (publish_camera_event(CAMERA_EVENT_TOGGLE_VIDEO) != ESP_OK){
            ESP_LOGE(TAG, "Failed to toggle capture mode");
        }
    }
}

void app_main(void)
{
    // // tạo semaphore, mutex và queue để quản lý events
    // events_init();
    // // hỗ trợ thay đổi các chế độ trong camera
    // mode_init();
    // // ktra và kết nối vs module camera
    // // camera_init();
    // // ktra và kết nối vs thẻ nhớ
    esp_log_level_set("sdmmc_cmd", ESP_LOG_DEBUG);
    esp_log_level_set("sdmmc_common", ESP_LOG_DEBUG);
    esp_log_level_set("sdmmc_host", ESP_LOG_DEBUG);
    esp_log_level_set("diskio_sdmmc", ESP_LOG_DEBUG);
    esp_log_level_set("vfs_fat_sdmmc", ESP_LOG_DEBUG);

    storage_init();

    // input_init();
    
    // camera_start();

    // input_start();
    
    ESP_LOGI(TAG, "System ready");
    
    // BaseType_t ret = xTaskCreate(camera_test_task, "test", 4096, NULL, 3, NULL);

    // if(ret != pdPASS) ESP_LOGE(TAG, "Cannot create test task");
}
