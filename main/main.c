// #include <stdio.h>
// #include "sdkconfig.h"
// #include "esp_system.h"
// #include <sys/param.h>
// #include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_camera.h"
#include "esp_log.h"

#include "camera.h"
#include "storage.h"
#include "webserver.h"
#include "network.h"
#include "frontend.h"
#include "input.h"
#include "events.h"
#include "mode.h"

static const char *TAG = "Main";

static void camera_test_task(void *arg)
{
    while(1)
    {
        event_t event;

        memset(&event, 0, sizeof(event));

        event.channel = EVENT_CHANNEL_CAMERA;
        event.type.camera = CAMERA_EVENT_CAPTURE;

        ESP_LOGI("TEST", "=== Capture photo ===");

        esp_err_t ret = events_publish(&event);

        if(ret != ESP_OK){
            ESP_LOGE("TEST", "events_publish failed: %s", esp_err_to_name(ret));
        }

        vTaskDelay(pdMS_TO_TICKS(3000));

        // switch photo -> video
        memset(&event, 0, sizeof(event));
        event.channel = EVENT_CHANNEL_CAMERA;
        event.type.camera = CAMERA_EVENT_TOGGLE_VIDEO;

        ESP_LOGI(TAG, "=== switch to video===");
        events_publish(&event);

        memset(&event, 0, sizeof(event));

        event.channel = EVENT_CHANNEL_CAMERA;
        event.type.camera = CAMERA_EVENT_CAPTURE;

        ESP_LOGI("TEST", "=== Start record VIDEO ===");
        events_publish(&event);

        vTaskDelay(pdMS_TO_TICKS(5000));

        memset(&event, 0, sizeof(event));

        event.channel = EVENT_CHANNEL_CAMERA;
        event.type.camera = CAMERA_EVENT_CAPTURE;

        ESP_LOGI("TEST", "=== Stop recording VIDEO  ===");
        events_publish(&event);

    }
}

void app_main(void)
{

    events_init();

    mode_init();

    camera_init();

    storage_init();

    // input_init();

    camera_start();

    // input_start();
    
    ESP_LOGI(TAG, "System ready");
    
    BaseType_t ret = xTaskCreate(camera_test_task, "test", 4096, NULL, 3, NULL);

    if(ret != pdPASS) ESP_LOGE(TAG, "Cannot create test task");

    // while(1){
    //     vTaskDelay(pdMS_TO_TICKS(1000));
    // }
}
