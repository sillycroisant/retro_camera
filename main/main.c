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

void app_main(void)
{
    events_init();

    mode_init();

    camera_init();

    storage_init();

    input_init();

    camera_start();

    input_start();
    
    ESP_LOGI(TAG, "System ready");
    
    while(1){
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
