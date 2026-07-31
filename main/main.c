#include <stdio.h>
#include "sdkconfig.h"

#include "esp_log.h"
#include "esp_system.h"
// #include "nvs_flash.h"
#include <sys/param.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_camera.h"

#include "esp_netif.h"
#include "esp_event.h"

// for sd card
// #include "driver/sdmmc_host.h"
// #include "esp_vfs_fat.h"
// #include "sdmmc_cmd.h"
#include "camera.h"

#include "storage.h"
#include "webserver.h"
#include "network.h"
#include "frontend.h"
#include "input.h"
#include "events.h"
#include "mode.h"

static const char *TAG = "Main.c";

static void test_task(void *arg)
{
    while(1)
    {
        vTaskDelay(pdMS_TO_TICKS(10000));

        event_t event = {
            .channel = EVENT_CHANNEL_CAMERA,
            .type.camera = CAMERA_EVENT_CAPTURE,
        };

        ESP_LOGI("TEST", "Publish capture");

        esp_err_t ret = events_publish(&event);

        if(ret != ESP_OK){
            ESP_LOGE("TEST", "events_publish failed: %s", esp_err_to_name(ret));
        }
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
    
    BaseType_t ret = xTaskCreate(test_task, "test", 4096, NULL, 3, NULL);

    if(ret != pdPASS) ESP_LOGE(TAG, "Cannot create test task");
    // while(1){
    //     vTaskDelay(pdMS_TO_TICKS(1000));
    // }
}
