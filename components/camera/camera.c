#include "camera.h"

#include "string.h"
#include "stdbool.h"
#include "esp_log.h"
#include "esp_camera.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "events.h"
#include "mode.h"
#include "storage.h"
#include "driver/gpio.h"

#include "camera_pinout.h"

#define TAG "Camera"

// config
#define CAMERA_TASK_STACK_SIZE      6144
#define CAMERA_TASK_PRIORITY        6
#define CAMERA_EVENT_QUEUE_LENGTH   5
#define CAMERA_FLASH_GPIO           4

// private types
typedef struct 
{
    bool flash_enabled;
    camera_capture_mode_t capture_mode;
} camera_state_t;

typedef void (*camera_handler_t)(void);

// private variables
static camera_state_t s_camera =
{
    .flash_enabled = false,
    .capture_mode  = CAMERA_CAPTURE_PHOTO
};

static TaskHandle_t s_camera_task = NULL;

static event_subscriber_t *s_subscriber = NULL;

static camera_config_t s_camera_config = 
{
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
    .pin_href  = CAM_PIN_HREF,
    .pin_pclk  = CAM_PIN_PCLK,

    .xclk_freq_hz = 20000000,

    .ledc_timer = LEDC_TIMER_0,
    .ledc_channel = LEDC_CHANNEL_0,

    .pixel_format = PIXFORMAT_JPEG,
    .frame_size = FRAMESIZE_VGA,
    .jpeg_quality = 12,
    .fb_count = 1,
    .fb_location = CAMERA_FB_IN_PSRAM,
    .grab_mode = CAMERA_GRAB_WHEN_EMPTY
};

// private function prototypes
static void camera_task(void *arg);

// event handlers
static void camera_handle_capture(void);

static void camera_handle_toggle_flash_mode(void);

static void camera_handle_toggle_capture_mode(void);

static void camera_handle_open_gallery(void);

// helpers
static void camera_set_flash_mode(bool enable);

static void camera_set_capture_mode(camera_capture_mode_t mode);

// esp_err_t camera_capture_photo(void);

static esp_err_t camera_capture_video(void);

static esp_err_t camera_save_photo(camera_fb_t *fb);

// dispatch table
static const camera_handler_t s_camera_handlers[CAMERA_EVENT_COUNT] = 
{
    [CAMERA_EVENT_CAPTURE] = camera_handle_capture,
    [CAMERA_EVENT_FLASH_TOGGLE] = camera_handle_toggle_flash_mode,
    [CAMERA_EVENT_TOGGLE_VIDEO] = camera_handle_toggle_capture_mode,
    [CAMERA_EVENT_OPEN_GALLERY] = camera_handle_open_gallery
};


// capture private functions (set, get, toggle)
static void camera_set_capture_mode(camera_capture_mode_t mode)
{
    if (mode >= CAMERA_CAPTURE_MODE_COUNT) return ;
    s_camera.capture_mode = mode;
    ESP_LOGI(TAG, "Capture mode: %s", mode == CAMERA_CAPTURE_PHOTO ? "PHOTO" : "VIDEO");
}

camera_capture_mode_t camera_get_capture_mode(void)
{
    return s_camera.capture_mode;
}

static void camera_handle_toggle_capture_mode(void)
{
    if(s_camera.capture_mode == CAMERA_CAPTURE_PHOTO)
    {
        camera_set_capture_mode(CAMERA_CAPTURE_VIDEO);
    } else {
        camera_set_capture_mode(CAMERA_CAPTURE_PHOTO);
    }
}

// flash private functions (set, get, toggle)
static void camera_set_flash_mode(bool enable)
{
    s_camera.flash_enabled = enable;
    gpio_set_level(CAMERA_FLASH_GPIO, enable);
    ESP_LOGI(TAG, "Flash %s", enable?"ON":"OFF");
}

bool camera_get_flash_mode(void)
{
    return s_camera.flash_enabled;
}

static void camera_handle_toggle_flash_mode(void)
{
    camera_set_flash_mode(!s_camera.flash_enabled);
}

// capture handler, execute capture when button pressed
static void camera_handle_capture(void)
{   
    esp_err_t ret;
    if(s_camera.capture_mode == CAMERA_CAPTURE_PHOTO)
    {
        ESP_LOGI(TAG,"Calling handler for capture photo");
        ret = camera_capture_photo();
    } else 
    {
        ESP_LOGI(TAG, "Calling handler for capture video");
        ret = camera_capture_video();
    }

    if(ret != ESP_OK) ESP_LOGW(TAG, "Capture failed");
}

// open gallery mode
static void camera_handle_open_gallery(void)
{
    ESP_LOGI(TAG,"Switch to Gallery mode");
    mode_set(APP_MODE_GALLERY);
}

// photo capture
static esp_err_t camera_save_photo(camera_fb_t *fb)
{   
    if(fb == NULL) return ESP_ERR_INVALID_ARG;
    return storage_save_jpeg(fb->buf, fb->len);
}

esp_err_t camera_capture_photo(void)
{
    ESP_LOGI(TAG,"Capturing photo...");
    camera_fb_t *fb = esp_camera_fb_get();

    if(fb == NULL)
    {
        ESP_LOGE(TAG, "Camera capture failed.");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Photo captured (%d bytes)", (unsigned)fb->len);

    esp_err_t ret = camera_save_photo(fb);

    esp_camera_fb_return(fb);
    if (ret != ESP_OK){
        ESP_LOGE(TAG, "Cannot save photo");
        return ret;
    }
    return ESP_OK;
}

// record video
static esp_err_t camera_capture_video(void)
{
    ESP_LOGI(TAG, "Video mode / (NOT IMPLEMENTED YET)");
    // to do later
    return ESP_OK;
}

// camera driver init
static esp_err_t camera_driver_init(void)
{
    esp_err_t ret = esp_camera_init(&s_camera_config);

    camera_fb_t *fb = esp_camera_fb_get();

    if (fb)
    {
        ESP_LOGI(TAG, "Discard first frame (%u bytes)", (unsigned)fb->len);
        esp_camera_fb_return(fb);
    }

    if(ret != ESP_OK){
        ESP_LOGE(TAG, "Camera init failed %s", esp_err_to_name(ret));
        return ret;
    }

    sensor_t *sensor = esp_camera_sensor_get();
    if(sensor == NULL) ESP_LOGW(TAG, "Cannot get sensor");
    else ESP_LOGI(TAG, "Camera sensor initialized");
    return ESP_OK;
}

static esp_err_t camera_flash_init(void)
{
    gpio_config_t io = 
    {
        .pin_bit_mask = 1ULL << CAMERA_FLASH_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };

    ESP_ERROR_CHECK(gpio_config(&io));
    gpio_set_level(CAMERA_FLASH_GPIO, 0);
    s_camera.flash_enabled = false;
    ESP_LOGI(TAG, "Flash initialized");
    return ESP_OK;
}

// camera task
static void camera_task(void *arg)
{
    event_t event;
    ESP_LOGI(TAG, "Camera tasks started");

    while(true)
    {
        if(events_receive(s_subscriber, 
                &event, portMAX_DELAY) != pdTRUE) continue;

        ESP_LOGI(TAG, "Receive channel=%d type=%d",
         event.channel, event.type.raw);

        if(event.channel != EVENT_CHANNEL_CAMERA) continue;

        if(event.type.camera >= CAMERA_EVENT_COUNT) continue;

        camera_handler_t handler = s_camera_handlers[event.type.camera];

        ESP_LOGI(TAG, "Dispatch handler");

        if(handler == NULL) continue; 
        
        handler();
    }

}

esp_err_t camera_init(void)
{
    ESP_ERROR_CHECK(camera_driver_init());
    ESP_ERROR_CHECK(camera_flash_init());

    s_subscriber = events_subscribe(EVENT_MASK_CAMERA, CAMERA_EVENT_QUEUE_LENGTH);

    if(s_subscriber == NULL) return ESP_FAIL;

    ESP_LOGI(TAG, "Camera initialized");
    return ESP_OK;
}

esp_err_t camera_start(void)
{
    if(s_camera_task != NULL) return ESP_OK;

    BaseType_t ret = xTaskCreate(
                        camera_task, 
                        "camera task",
                        CAMERA_TASK_STACK_SIZE,
                        NULL,
                        CAMERA_TASK_PRIORITY,
                        &s_camera_task);

    if(ret != pdPASS){
        ESP_LOGE(TAG, "Cannot create camera task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Camera task created");
    return ESP_OK;
}