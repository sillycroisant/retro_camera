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

// get config base on esp32 board
#define BOARD_ESP32CAM_AITHINKER 1

// pin out for esp32-cam boards
#include "camera_pinout.h"

// include auto focus mode if camera is supported (for OV5460)s
#if defined(CONFIG_CAMERA_AF_SUPPORT) && CONFIG_CAMERA_AF_SUPPORT
#include "esp_camera_af.h"
#endif

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

    // Init wifi connection
    // network_config_t nw_cfg = {
    //     .ssid = "retro camera",
    //     .password = "123456789"
    // };

    // network_init(&nw_cfg);
    
    while(1){
        camera_capture_photo();
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}
