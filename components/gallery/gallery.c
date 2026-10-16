#include "gallery.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "mode.h"
#include "events.h"
#include "storage.h"
#include "display.h"

#define TAG "Gallery"

#define GALLERY_TASK_STACK_SIZE    4096
#define GALLERY_TASK_PRIORITY      5
#define GALLERY_EVENT_QUEUE_LENGTH 5

typedef void (*gallery_handler_t)(void);

static TaskHandle_t s_gallery_task = NULL;
static event_subscriber_t *s_subscriber = NULL;
static int32_t s_current_pos = -1;

// display current photo in gallery
void gallery_display_current_photo(void)
{
    if(mode_get() != APP_MODE_GALLERY) return;

    uint32_t total = storage_gallery_get_count();
    if(total == 0){
        ESP_LOGI(TAG, "No photos found on SD card");
        mode_set(APP_MODE_CAMERA);
        return;
    }

    if(s_current_pos < 0 || (uint32_t)s_current_pos >= total){
        s_current_pos = total - 1;
    }

    char path[128];
    char filename[128];
    if(storage_gallery_get_path(s_current_pos, path, sizeof(path)) == ESP_OK && storage_gallery_get_filename(s_current_pos, filename, sizeof(filename)) == ESP_OK){
        ESP_LOGI(TAG, "Gallery viewing [%s] (%lu/%lu)", filename, (long)(s_current_pos+1), (unsigned long)total);
        display_show_gallery_photo(path, filename, s_current_pos+1, total);
    }
}

// 1. Các hàm xử lý sự kiện trong Gallery
static void gallery_handle_open_camera(void)
{
    ESP_LOGI(TAG, ">>> Switching back to CAMERA mode...");
    s_current_pos = -1;
    mode_set(APP_MODE_CAMERA);
}

// cuộn xem ảnh tiếp theo trong gallery
static void gallery_handle_scroll_previous(void)
{
    ESP_LOGI(TAG, "Gallery: Scroll UP (Next item in gallery)");
    uint32_t total = storage_gallery_get_count();
    if(total == 0) return;

    s_current_pos--;
    if(s_current_pos < 0){
        s_current_pos = total - 1;
    }

    gallery_display_current_photo();
}

static void gallery_handle_scroll_next(void)
{
    ESP_LOGI(TAG, "Gallery: Scroll DOWN (Next item)");

    uint32_t total = storage_gallery_get_count();
    if(total == 0) return;

    s_current_pos++;
    if((uint32_t)s_current_pos >= total){
        s_current_pos = 0;
    }

    gallery_display_current_photo();
}

static void gallery_handle_delete_photo(void){
    uint32_t total = storage_gallery_get_count();
    if(total == 0 || s_current_pos < 0) return;


    ESP_LOGI(TAG, "Gallery: Deleting photo at position %ld..", (long)s_current_pos);
    esp_err_t ret = storage_gallery_delete(s_current_pos);

    if(ret == ESP_OK){
        total = storage_gallery_get_count();
        if(total == 0){
            ESP_LOGI(TAG, "All photos deleted, returning to Camera mode");
            mode_set(APP_MODE_CAMERA);
            return;
        }

        if((uint32_t)s_current_pos >= total){
            s_current_pos = total - 1;
        }
        gallery_display_current_photo();
        s_cached_rem_photos = storage_get_remaining_photos();
    } else {
        ESP_LOGE(TAG, "Failed to delete photo");
    }
}

// 2. Bảng Dispatch Table định tuyến Event
static const gallery_handler_t s_gallery_handlers[GALLERY_EVENT_COUNT] = 
{
    [GALLERY_EVENT_DELETE]       =  gallery_handle_delete_photo,
    [GALLERY_EVENT_SCROLL_FORWARD]   = gallery_handle_scroll_previous,
    [GALLERY_EVENT_SCROLL_BACK] = gallery_handle_scroll_next,
    [GALLERY_EVENT_OPEN_CAMERA] = gallery_handle_open_camera
};

// 3. Gallery Task
static void gallery_task(void *arg)
{
    event_t event;
    ESP_LOGI(TAG, "Gallery task started");

    while (true)
    {
        // Chờ nhận event từ kênh Gallery
        if (events_receive(s_subscriber, &event, portMAX_DELAY) != pdTRUE) continue;
        if (event.channel != EVENT_CHANNEL_GALLERY) continue;
        if (event.type.gallery >= GALLERY_EVENT_COUNT) continue;

        gallery_handler_t handler = s_gallery_handlers[event.type.gallery];
        if (handler != NULL) {
            handler();
        }
    }
}

// 4. Khởi tạo Gallery
esp_err_t gallery_init(void)
{
    if (s_subscriber != NULL) return ESP_OK;

    // Đăng ký nhận các event thuộc kênh GALLERY
    s_subscriber = events_subscribe(EVENT_MASK_GALLERY, GALLERY_EVENT_QUEUE_LENGTH);
    if (s_subscriber == NULL) {
        ESP_LOGE(TAG, "Failed to subscribe to Gallery events");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Gallery initialized successfully");
    return ESP_OK;
}

// 5. Bắt đầu Gallery Task
esp_err_t gallery_start(void)
{
    if (s_gallery_task != NULL) return ESP_OK;

    BaseType_t ret = xTaskCreatePinnedToCore(
        gallery_task,
        "gallery_task",
        GALLERY_TASK_STACK_SIZE,
        NULL,
        GALLERY_TASK_PRIORITY,
        &s_gallery_task,
        0 // pinned to core 0
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Cannot create gallery task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Gallery task created");
    return ESP_OK;
}