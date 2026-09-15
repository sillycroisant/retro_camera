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
#include "gallery.h"
#include "display.h"

static const char *TAG = "Main";

void app_main(void)
{
    // // tạo semaphore, mutex và queue để quản lý events
    events_init();
    // // hỗ trợ thay đổi các chế độ trong camera
    mode_init();
    // // ktra và kết nối vs module camera
    camera_init();
    gallery_init();
    storage_init();
    display_init();
    input_init();
    
    camera_start();
    gallery_start();
    input_start();

    display_show_latest_photo();

    ESP_LOGI(TAG, "=== System ready ===");
    
    // BaseType_t ret = xTaskCreate(camera_test_task, "test", 4096, NULL, 3, NULL);

    // if(ret != pdPASS) ESP_LOGE(TAG, "Cannot create test task");
}
