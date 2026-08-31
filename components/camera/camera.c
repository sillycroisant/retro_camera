#include "camera.h"

#include "string.h"
#include "stdbool.h"
#include "esp_log.h"
#include "esp_camera.h"
#include "img_converters.h"

#include "stdio.h"
#include "sys/stat.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

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
#define CAMERA_FLASH_DELAY_MS       80
#define VIDEO_DIRECTORY             "/sdcard/videos"
#define CAMERA_VIDEO_FPS            10

// private types
typedef struct 
{
    bool flash_enabled;
    camera_capture_mode_t capture_mode;

    bool recording;
    storage_video_t *video;

} camera_state_t;

typedef void (*camera_handler_t)(void);

// private variables
static camera_state_t s_camera =
{
    .flash_enabled = false,
    .capture_mode  = CAMERA_CAPTURE_PHOTO,
    .recording = false,
    .video = NULL
};

static TaskHandle_t s_camera_task = NULL;

static TaskHandle_t s_video_taks = NULL;

static SemaphoreHandle_t s_video_mutex = NULL;

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

    .pixel_format = PIXFORMAT_RGB565,
    .frame_size = FRAMESIZE_VGA,
    .jpeg_quality = 10,
    // esp32s3 có 8mb octal psram, nên dùng 2 framebuffer
    .fb_count = 2,
    .fb_location = CAMERA_FB_IN_PSRAM,
    .grab_mode = CAMERA_GRAB_LATEST
};

// private function prototypes
static void camera_task(void *arg);

static void video_task(void *arg);

// event handlers
static void camera_handle_capture(void);

static void camera_handle_toggle_flash_mode(void);

static void camera_handle_toggle_capture_mode(void);

static void camera_handle_open_gallery(void);

// helpers
static void camera_set_flash_mode(bool enable);

static void camera_set_capture_mode(camera_capture_mode_t mode);

static void camera_capture_photo(void);

static void camera_capture_video(void);

static esp_err_t camera_save_photo(camera_fb_t *fb);

static bool camera_is_recording(void);

static esp_err_t camera_start_video(void);

static esp_err_t camera_stop_video(void);

static esp_err_t camera_record_frame(void);

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
    if(camera_is_recording()){
        ESP_LOGW(TAG, "Cannot change capture mode while recording");
        return;
    }

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
    if(s_camera.capture_mode == CAMERA_CAPTURE_PHOTO)
    {
        ESP_LOGI(TAG,"Calling handler for capture photo");
        camera_capture_photo();
    } else 
    {
        ESP_LOGI(TAG, "Calling handler for capture video");
        camera_capture_video();
    }
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

static void camera_capture_photo(void)
{
    ESP_LOGI(TAG,"Capturing photo... (RGB565 -> JPG) ...");

    if(s_camera.flash_enabled){
        gpio_set_level(CAMERA_FLASH_GPIO, 1);
        vTaskDelay(pdMS_TO_TICKS(CAMERA_FLASH_DELAY_MS));
    }

    camera_fb_t *fb = esp_camera_fb_get();

    gpio_set_level(CAMERA_FLASH_GPIO, 0);

    if(fb == NULL)
    {
        ESP_LOGE(TAG, "Camera capture failed."); return ;
    }

    uint8_t *jpg_buf = NULL;
    size_t jpg_len = 0;
    bool converted = frame2jpg(fb, 80, &jpg_buf, &jpg_len);

    esp_camera_fb_return(fb);
    
    if(!converted || jpg_buf == NULL){
        ESP_LOGE(TAG, "JPEG compression failed");
        return ;
    }
    
    ESP_LOGI(TAG, "Compressed to JPEG: %u bytes", (unsigned)jpg_len);

    esp_err_t ret = storage_save_jpeg(jpg_buf, jpg_len);
    free(jpg_buf);

    if (ret != ESP_OK){
        ESP_LOGE(TAG, "Cannot save photo to sdcard");
    } else {
        ESP_LOGI(TAG, "Photo saved successfully");
    }
} 

// RECORD VIDEO
static bool camera_is_recording(void)
{
    bool recording;

    xSemaphoreTake(s_video_mutex, portMAX_DELAY);

    recording = s_camera.recording;
    xSemaphoreGive(s_video_mutex);
    return recording;
}

static esp_err_t camera_start_video(void){
    xSemaphoreTake(s_video_mutex, portMAX_DELAY);

   if(s_camera.recording){
        xSemaphoreGive(s_video_mutex);
        return ESP_OK;
   }

   s_camera.recording = true;
   s_camera.video = NULL;

   xSemaphoreGive(s_video_mutex);
    
   ESP_LOGI(TAG, "Video recording started");

    return ESP_OK;

}

static esp_err_t camera_stop_video(void)
{
    storage_video_t *video = NULL;

    xSemaphoreTake(s_video_mutex, portMAX_DELAY);

    if(!s_camera.recording){
        xSemaphoreGive(s_video_mutex);
        return ESP_OK;
    }

    // stop accepting new frames
    s_camera.recording = false;
    video = s_camera.video;
    s_camera.video = NULL;

    xSemaphoreGive(s_video_mutex);

    // finalize avi
    if(video != NULL){
        esp_err_t ret = storage_video_close(video);
        if(ret != ESP_OK){
            ESP_LOGE(TAG, "Cannot finalize video");
            return ret;
        }
    }
    ESP_LOGI(TAG, "Stopped recording video");
    return ESP_OK;
}

static esp_err_t camera_record_frame(void)
{
    // check recording state first
    xSemaphoreTake(s_video_mutex, portMAX_DELAY);

    if(!s_camera.recording){
        xSemaphoreGive(s_video_mutex);
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreGive(s_video_mutex);

    // capture frame
    camera_fb_t *fb = esp_camera_fb_get();

    if(fb == NULL){
        ESP_LOGE(TAG, "Failed to capture video frame");
        return ESP_FAIL;
    }
    esp_err_t ret = ESP_OK;

    // protect video context while creating/writing frames
    xSemaphoreTake(s_video_mutex, portMAX_DELAY);

    if(!s_camera.recording){
        xSemaphoreGive(s_video_mutex);
        esp_camera_fb_return(fb);
        return ESP_ERR_INVALID_STATE;
    }
   
    // first frame , create avi
    if(s_camera.video == NULL){
        s_camera.video = storage_video_create(fb->width, fb->height, CAMERA_VIDEO_FPS);
        if(s_camera.video == NULL){
            ESP_LOGE(TAG, "Cannot create video file");
            xSemaphoreGive(s_video_mutex);
            esp_camera_fb_return(fb);
            return ESP_FAIL;
        }
    }

    // write jpeg frame into avi
    ret = storage_video_write_frame(s_camera.video, fb->buf, fb->len);
    xSemaphoreGive(s_video_mutex);
    esp_camera_fb_return(fb);

    if(ret != ESP_OK){
        ESP_LOGE(TAG, "cannot write video frame: %s", esp_err_to_name(ret));
        return ret;
    }

    return ESP_OK;
}

static void camera_capture_video(void)
{
    if(camera_is_recording()){
        ESP_LOGI(TAG, "Stopping video recording..");
        if(camera_stop_video() != ESP_OK) ESP_LOGE(TAG, "Failed to stop video recording...");
    } else {
        ESP_LOGI(TAG, "Starting video recording..");
        if(camera_start_video() != ESP_OK) ESP_LOGE(TAG, "Failed to start video recording");
    }
}

// camera driver init
static esp_err_t camera_driver_init(void)
{
    esp_err_t ret = esp_camera_init(&s_camera_config);

    if(ret != ESP_OK){
        ESP_LOGE(TAG, "Camera init failed %s", esp_err_to_name(ret));
        return ret;
    }

    sensor_t *sensor = esp_camera_sensor_get();
    if(sensor == NULL) {
        ESP_LOGW(TAG, "Cannot get sensor");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Camera sensor initialized");

    // Phần này để cho vào menu setting các version sau...
    // cấu hình đặc biệt để đánh thức ov3660
    // sensor->set_vflip(sensor, 1);
    // sensor->set_hmirror(sensor, 0);
    // sensor->set_brightness(sensor, 1);
    // sensor->set_saturation(sensor, 0);   // Độ bão hòa màu (-2 đến 2)

    // sensor->set_framesize(sensor, FRAMESIZE_QVGA); // Đặt lại framesize
    // sensor->set_quality(sensor, 10);     // Chất lượng JPEG (10 - 63)
    
    ESP_LOGI(TAG, "Camera sensor ov3660 initialized & configured");

    // Đợi 200ms để cảm biến nạp cấu hình và ổn định luồng ảnh
    vTaskDelay(pdMS_TO_TICKS(200));
    // Đọc thử 2 frame đầu để xả buffer
    for (int i = 0; i < 2; i++) {
        camera_fb_t *fb = esp_camera_fb_get();
        if (fb) {
            ESP_LOGI(TAG, "Warmup frame %d received successfully (%u bytes)", i, (unsigned)fb->len);
            esp_camera_fb_return(fb);
        } else {
            ESP_LOGW(TAG, "Warmup frame %d timed out", i);
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }

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
    // default flash led turn off, but MODE ON
    gpio_set_level(CAMERA_FLASH_GPIO, 0);
    s_camera.flash_enabled = false;
    ESP_LOGI(TAG, "Flash initialized, mode OFF");
    return ESP_OK;
}

// camera task
static void camera_task(void *arg)
{
    event_t event;
    ESP_LOGI(TAG, "Camera tasks started");

    while(true)
    {
        if(events_receive(s_subscriber, &event, portMAX_DELAY) != pdTRUE) continue;

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

// video task
static void video_task(void *arg)
{
    ESP_LOGI(TAG, "Video task started");
    const TickType_t frame_period = pdMS_TO_TICKS(1000 / CAMERA_VIDEO_FPS);
    bool was_recording = false;
    TickType_t last_wake_time = xTaskGetTickCount();

    while(true)
    {   
        bool recording = camera_is_recording();

        // detect transist false -> true = start recordings
        if(recording && !was_recording){
            ESP_LOGI(TAG, "Video tasK: recording started");
            last_wake_time = xTaskGetTickCount();
        }

        // detect transist true -> false = stop recording
        if(!recording && was_recording){
            ESP_LOGI(TAG," Video task: recording stopped");
        }
        was_recording = recording;

        // not recording
        if(!recording){
            vTaskDelay(pdMS_TO_TICKS(10)); continue;
        }

        esp_err_t ret = camera_record_frame();
        if(ret != ESP_OK){
            ESP_LOGE(TAG,"Failed to record frame");
            camera_stop_video(); continue;
        }

        vTaskDelayUntil(&last_wake_time, frame_period);
    }
}

// initialization
esp_err_t camera_init(void)
{
    ESP_ERROR_CHECK(camera_driver_init());
    ESP_ERROR_CHECK(camera_flash_init());

    s_video_mutex = xSemaphoreCreateMutex();

    if(s_video_mutex == NULL){
        ESP_LOGE(TAG, "Cannot create video mutex");
        return ESP_ERR_NO_MEM;
    }

    s_subscriber = events_subscribe(EVENT_MASK_CAMERA, CAMERA_EVENT_QUEUE_LENGTH);

    if(s_subscriber == NULL) return ESP_FAIL;

    ESP_LOGI(TAG, "Camera initialized driver and flash.");
    return ESP_OK;
}

esp_err_t camera_start(void)
{
    if(s_camera_task == NULL){
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
    }

    if(s_video_taks == NULL){
        BaseType_t ret = xTaskCreate(
                            video_task,
                            "video task",
                            4096,
                            NULL,
                            CAMERA_TASK_PRIORITY,
                            &s_video_taks);
        if(ret != pdPASS){
            ESP_LOGE(TAG, "Cannot create video task");
            return ESP_FAIL;
        }
        ESP_LOGI(TAG, "Video task created");
    }

    return ESP_OK;

}