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
static uint32_t s_current_photo_idx = 0;

// display current photo in gallery
void gallery_display_current_photo(void)
{
    if(mode_get() != APP_MODE_GALLERY) return;

    if(s_current_photo_idx == 0) s_current_photo_idx = storage_get_current_index();


    if(s_current_photo_idx == 0){
        ESP_LOGI(TAG, "No photos found on SD card");
        return;
    }

    char path[128];
    if(storage_get_path_by_index(s_current_photo_idx, path, sizeof(path)) == ESP_OK){
        ESP_LOGI(TAG, "Gallery viewing [%lu]: %s", (unsigned long)s_current_photo_idx, path);
        display_show_jpeg_file(path);
    } else {
        ESP_LOGW(TAG, "Photo index %lu not found", (unsigned long)s_current_photo_idx);
    }
}

// 1. Các hàm xử lý sự kiện trong Gallery
static void gallery_handle_open_camera(void)
{
    ESP_LOGI(TAG, ">>> Switching back to CAMERA mode...");
    mode_set(APP_MODE_CAMERA);
}

// cuộn xem ảnh tiếp theo trong gallery
static void gallery_handle_scroll_previous(void)
{
    ESP_LOGI(TAG, "Gallery: Scroll UP (Next item in gallery)");
    
    if(s_current_photo_idx > 1){
        s_current_photo_idx --;
        char path[128];

        // Bỏ qua các index file bị xóa hoặc không tồn tại
        while(s_current_photo_idx > 1 && storage_get_path_by_index(s_current_photo_idx, path, sizeof(path)) != ESP_OK){
            s_current_photo_idx--;
        }
    } else {
        // nếu bức ảnh hiện tại idx = 1 thì quay về bức ảnh mới nhất trong gallery
        s_current_photo_idx = storage_get_current_index();
        ESP_LOGI(TAG, "Already at the first photo, scroll back to lastest photo");
    }

    gallery_display_current_photo();
    ESP_LOGI(TAG, "CURRENT PHOTO INDEX = %lu", s_current_photo_idx);
}

static void gallery_handle_scroll_next(void)
{
    ESP_LOGI(TAG, "Gallery: Scroll DOWN (Next item)");

    uint32_t max_idx = storage_get_current_index();
    if (s_current_photo_idx < max_idx){
        s_current_photo_idx ++;
        // keep scroll next to look for a valid photo
        char path[128];
        while(s_current_photo_idx < max_idx && storage_get_path_by_index(s_current_photo_idx, path, sizeof(path)) != ESP_OK){
            s_current_photo_idx++;
        }
    } else {
        s_current_photo_idx = 1;
        ESP_LOGI(TAG, "Already at the latest photo, return back to the first one..");
    }
    gallery_display_current_photo();
    ESP_LOGI(TAG, "CURRENT PHOTO INDEX = %lu", s_current_photo_idx);
}

// 2. Bảng Dispatch Table định tuyến Event
static const gallery_handler_t s_gallery_handlers[GALLERY_EVENT_COUNT] = 
{
    [GALLERY_EVENT_DUMMY]       = NULL,
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