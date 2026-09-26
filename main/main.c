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
    events_init();
    mode_init();

    camera_init();
    gallery_init();
    storage_init();
    display_init();
    input_init();
    
    camera_start();
    gallery_start();
    input_start();

    ESP_LOGI(TAG, "=== System ready ===");
}
