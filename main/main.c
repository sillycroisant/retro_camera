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

// for http web server

// get config base on esp32 board
#define BOARD_ESP32CAM_AITHINKER 1

// pin out for esp32-cam boards
#include "camera_pinout.h"

// include auto focus mode if camera is supported (for OV5460)s
#if defined(CONFIG_CAMERA_AF_SUPPORT) && CONFIG_CAMERA_AF_SUPPORT
#include "esp_camera_af.h"
#endif

static const char *TAG = "ESP_CAM";

#if ESP_CAMERA_SUPPORTED
static camera_config_t camera_config = {
    .pin_pwdn = CAM_PIN_PWDN,
    .pin_reset = CAM_PIN_RESET,
    .pin_xclk = CAM_PIN_XCLK,
    .pin_sccb_sda = CAM_PIN_SIOD,
    .pin_sccb_scl = CAM_PIN_SIOC,

    .pin_d7 = CAM_PIN_D7,
    .pin_d6 = CAM_PIN_D6,
    .pin_d5 = CAM_PIN_D5,
    .pin_d4 = CAM_PIN_D4,
    .pin_d3 = CAM_PIN_D3,
    .pin_d2 = CAM_PIN_D2,
    .pin_d1 = CAM_PIN_D1,
    .pin_d0 = CAM_PIN_D0,
    .pin_vsync = CAM_PIN_VSYNC,
    .pin_href = CAM_PIN_HREF,
    .pin_pclk = CAM_PIN_PCLK,

    //XCLK 20MHz or 10MHz for OV2640 double FPS (Experimental)
    .xclk_freq_hz = 20000000,
    .ledc_timer = LEDC_TIMER_0,
    .ledc_channel = LEDC_CHANNEL_0,

    .pixel_format = PIXFORMAT_RGB565, //YUV422,GRAYSCALE,RGB565,JPEG
    .frame_size = FRAMESIZE_QVGA,    //QQVGA-UXGA, For ESP32, do not use sizes above QVGA when not JPEG. The performance of the ESP32-S series has improved a lot, but JPEG mode always gives better frame rates.

    .jpeg_quality = 12, //0-63, for OV series camera sensors, lower number means higher quality
    .fb_count = 1,       //When jpeg mode is used, if fb_count more than one, the driver will work in continuous mode.
    .fb_location = CAMERA_FB_IN_PSRAM,
    .grab_mode = CAMERA_GRAB_WHEN_EMPTY,
};

// initialize the camera
static esp_err_t init_camera(void){
    esp_err_t err = esp_camera_init(&camera_config);
    if(err != ESP_OK){
        // ESP_LOGE(TAG, "Camera init failed!");
        return err;
    }
    return ESP_OK;
}

#if defined(CONFIG_CAMERA_AF_SUPPORT) && CONFIG_CAMERA_AF_SUPPORT
static void init_autofocus(void)
{
    sensor_t *s = esp_camera_sensor_get();
    if (!s){
        ESP_LOGW(TAG, "AF: No sensor handle");
        return;
    }

    if(!esp_camera_af_is_supported(s)){
        ESP_LOGI(TAG, "AF: Not supported by this sensor");
        return;
    }

    esp_camera_af_config_t af_cfg = {
        .mode = ESP_CAM_AF_MODE_AUTO,
        .timeout_ms = CONFIG_CAMERA_AF_DEFAULT_TIMEOUT_MS,
    };

    esp_err_t ret = esp_camera_af_init(s, &af_cfg);
    if (ret != ESP_OK){
        ESP_LOGW(TAG, "AF init failed: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG,"AF initialized (AUTO mode)");
}

#endif

#endif

void app_main(void)
{
    // initialization
    #if ESP_CAMERA_SUPPORTED
        if (ESP_OK != init_camera()){
            return;
        }
    #endif

    #if defined(CONFIG_CAMERA_AF_SUPPORT) && CONFIG_CAMERA_AF_SUPPORT
        init_autofocus();
    #endif
    
    storage_init();
    // init network
    network_config_t nw_cfg = {
        .ssid = "retro camera",
        .password = "123456789"
    };

    ESP_ERROR_CHECK(network_init(&nw_cfg));

    
    while(1){
        ESP_LOGI(TAG, "Taking picture...");
        
        // camera frame buffer receive image from camera
        camera_fb_t *pic = esp_camera_fb_get();
        
        if (!pic){
            ESP_LOGE(TAG, "Camera capture failed");
            return;
        }

        // use pic->buf to accessthe image
        ESP_LOGI(TAG, "Picture taken! Its size was: %u bytes",pic->len);

        storage_save_jpeg(
            pic->buf,
            pic->len
        );

        esp_camera_fb_return(pic);

        vTaskDelay(100000/ portTICK_PERIOD_MS);
    }
}
