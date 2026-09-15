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

// 1. Các hàm xử lý sự kiện trong Gallery
static void gallery_handle_open_camera(void)
{
    ESP_LOGI(TAG, "Switching back to CAMERA mode...");
    mode_set(APP_MODE_CAMERA);
}

static void gallery_handle_scroll_up(void)
{
    ESP_LOGI(TAG, "Gallery: Scroll UP (Previous item)");
}

static void gallery_handle_scroll_down(void)
{
    ESP_LOGI(TAG, "Gallery: Scroll DOWN (Next item)");
}

// 2. Bảng Dispatch Table định tuyến Event
static const gallery_handler_t s_gallery_handlers[GALLERY_EVENT_COUNT] = 
{
    [GALLERY_EVENT_DUMMY]       = NULL,
    [GALLERY_EVENT_SCROLL_UP]   = gallery_handle_scroll_up,
    [GALLERY_EVENT_SCROLL_DOWN] = gallery_handle_scroll_down,
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
        if (events_receive(s_subscriber, &event, portMAX_DELAY) != pdTRUE) {
            continue;
        }

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

    BaseType_t ret = xTaskCreate(
        gallery_task,
        "gallery_task",
        GALLERY_TASK_STACK_SIZE,
        NULL,
        GALLERY_TASK_PRIORITY,
        &s_gallery_task
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Cannot create gallery task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Gallery task created");
    return ESP_OK;
}

void gallery_show_current(void){
    ESP_LOGI(TAG, "Gallery: Loading latest photo onto screen...");
    display_show_latest_photo();
}