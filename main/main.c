#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_camera.h"
#include "esp_log.h"

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
    // quản lý events và mode
    // // tạo semaphore, mutex và queue để quản lý events
    events_init();
    // // hỗ trợ thay đổi các chế độ trong camera
    mode_init();

    // khởi tạo camera, gallery, storage và màn hình lcd
    camera_init();
    gallery_init();
    storage_init();
    display_init();
    input_init();
    
    // bắt đầu các task
    camera_start();
    gallery_start();
    input_start();

    ESP_LOGI(TAG, "=== System ready ===");

    display_show_latest_photo();

}
