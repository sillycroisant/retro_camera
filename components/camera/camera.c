#include "camera.h"

#include "string.h"
#include "stdbool.h"
#include "esp_log.h"
#include "esp_camera.h"
#include "img_converters.h"
#include "esp_timer.h"

#include "stdio.h"
#include "sys/stat.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include "events.h"
#include "storage.h"
#include "mode.h"
#include "driver/gpio.h"
#include "display.h"
#include "gallery.h"

#include "camera_pinout.h"

#define TAG "Camera"

// config
#define CAMERA_TASK_STACK_SIZE      6144
#define CAMERA_TASK_PRIORITY        6

#define LIVEVIEW_TASK_STACK_SIZE    4096
#define LIVEVIEW_TASK_PRIORITY      5

#define SAVE_TASK_STACK_SIZE        4096
#define SAVE_TASK_PRIORITY          4

#define CAMERA_EVENT_QUEUE_LENGTH   5

#define VIDEO_DIRECTORY             "/sdcard/videos"
#define CAMERA_VIDEO_FPS            10

// private types
typedef struct 
{
    camera_capture_mode_t capture_mode;
    bool recording;
    bool capturing;
    storage_video_t *video;
} camera_state_t;

typedef void (*camera_handler_t)(void);

// private variables
static camera_state_t s_camera =
{
    .capture_mode  = CAMERA_CAPTURE_PHOTO,
    .recording = false,
    .capturing = false,
    .video = NULL
};

// FPS
static int s_real_fps = 0;
static int s_fps_counter = 0;
static int64_t s_last_fps_calc_time = 0;
static int64_t s_record_start_time = 0;

// Buffer and cache
static uint16_t *s_live_lcd_buf = NULL;
static uint32_t s_cached_rem_photos = 0;
static int64_t s_last_rem_poll_time = 0;

// Task
static TaskHandle_t s_camera_task = NULL;
static TaskHandle_t s_video_task = NULL;
static TaskHandle_t s_liveview_task = NULL;
static TaskHandle_t s_save_task = NULL;

static QueueHandle_t s_save_queue = NULL;
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
    .frame_size = FRAMESIZE_QVGA,
    .jpeg_quality = 12,

    // esp32s3 có 8mb octal psram, nên dùng 2 framebuffer
    .fb_count = 2,
    .fb_location = CAMERA_FB_IN_PSRAM,
    .grab_mode = CAMERA_GRAB_LATEST
};

// private function prototypes
static void camera_task(void *arg);
static void video_task(void *arg);
static void liveview_task(void *arg);
static void photo_save_task(void *arg);

// event handlers for each buttons' input
static void camera_handle_capture(void);
// static void camera_handle_toggle_flash_mode(void);
// thay hàm trên bằng hàm handle tính năng khác
static void camera_handle_toggle_capture_mode(void);
static void camera_handle_open_gallery(void);

// helpers
static void camera_set_capture_mode(camera_capture_mode_t mode);
static void camera_capture_photo(void);
static void camera_capture_video(void);
static esp_err_t camera_start_video(void);
static esp_err_t camera_stop_video(void);
static esp_err_t camera_record_frame(void);
static bool camera_is_recording(void);

// dispatch table
static const camera_handler_t s_camera_handlers[CAMERA_EVENT_COUNT] = 
{
    [CAMERA_EVENT_CAPTURE]      = camera_handle_capture,
    [CAMERA_EVENT_FLASH_TOGGLE] = NULL,
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

// capture handler, execute capture when button pressed
static void camera_handle_capture(void)
{   
    if(s_camera.capture_mode == CAMERA_CAPTURE_PHOTO)
    {
        // ESP_LOGI(TAG,"Calling handler for capture photo");
        camera_capture_photo();
    } else {
        // ESP_LOGI(TAG, "Calling handler for capture video");
        camera_capture_video();
    }
}

static void downsample_to_lcd(const uint16_t *src, int src_w, int src_h, uint16_t *dst){
// Fast path: VGA 640x480 -> QVGA 320x240 (Reads 2 pixels per 32-bit cycle)
    if (src_w == 640 && src_h == 480) {
        for (int y = 0; y < 240; y++) {
            const uint32_t *src32 = (const uint32_t *)&src[(y * 2) * 640];
            uint16_t *dst16 = &dst[y * 320];
            for (int x = 0; x < 320; x += 2) {
                uint32_t p0_p1 = src32[x];     // Contains pixel (2x) and (2x+1)
                uint32_t p2_p3 = src32[x + 1]; // Contains pixel (2x+2) and (2x+3)
                dst16[x]     = (uint16_t)p0_p1;
                dst16[x + 1] = (uint16_t)p2_p3;
            }
        }
    } else {
        // General path (SVGA/HD): 16.16 Fixed-point stepping (NO division in loop)
        uint32_t x_ratio = ((uint32_t)src_w << 16) / 320;
        uint32_t y_ratio = ((uint32_t)src_h << 16) / 240;
        for (int y = 0; y < 240; y++) {
            int src_y = (int)((y * y_ratio) >> 16);
            const uint16_t *src_row = &src[src_y * src_w];
            uint16_t *dst_row = &dst[y * 320];
            uint32_t src_x_fp = 0;
            for (int x = 0; x < 320; x++) {
                dst_row[x] = src_row[src_x_fp >> 16];
                src_x_fp += x_ratio;
            }
        }
    }
}

// open gallery mode
static void camera_handle_open_gallery(void)
{
    ESP_LOGI(TAG,"Switch to Gallery mode");
    mode_set(APP_MODE_GALLERY);
    gallery_display_current_photo();}

// capture image using core 1..
static void camera_capture_photo(void)
{
    ESP_LOGI(TAG,"[Core 1] Capturing snapshot...");
    s_camera.capturing = true; // flag bận để live view task tạm dừng 1 nhịp

    camera_fb_t *fb = esp_camera_fb_get();

    if(fb == NULL)
    {
        ESP_LOGE(TAG, "Camera capture failed."); 
        s_camera.capturing = false;
        return;
    }

    // 1. HIỂN THỊ NGAY BỨC ẢNH VỪA CHỤP LÊN LCD ST7789
    if(fb->width == LCD_H_RES && fb->height == LCD_V_RES){
        display_show_rgb565(fb->buf, 0, 0, LCD_H_RES, LCD_V_RES);
    } else if (s_live_lcd_buf != NULL) {
        downsample_to_lcd((const uint16_t *)fb->buf, fb->width, fb->height, s_live_lcd_buf);
        display_show_rgb565(s_live_lcd_buf, 0, 0, LCD_H_RES, LCD_V_RES);
    }

    // đẩy frame buffer sang queue để core 0 nén jpeg và ghi thẻ nhớ ngầm
    if(xQueueSend(s_save_queue, &fb, 0) != pdPASS) {
        ESP_LOGW(TAG, "Save queue full, dropping snapshot");
        esp_camera_fb_return(fb);
    }
    // keep the captured img for a short duration
    vTaskDelay(pdMS_TO_TICKS(80));
    s_camera.capturing = false;
}

static void photo_save_task(void *arg){
    camera_fb_t *fb = NULL;
    ESP_LOGI(TAG, "Photo background saver task running on core 0");

    while(true){
        if(xQueueReceive(s_save_queue, &fb, portMAX_DELAY) != pdTRUE || fb == NULL) continue;

        ESP_LOGI(TAG, "[Core 0] Background JPEG compressing & SD writing...");
        uint8_t *jpg_buf = NULL;
        size_t jpg_len = 0;
        bool converted = frame2jpg(fb, 85, &jpg_buf, &jpg_len);

        esp_camera_fb_return(fb);

        if(!converted || jpg_buf == NULL){
            ESP_LOGE(TAG, "Background JPEG compression failed"); continue;
        }

        // writing into sdcard
        esp_err_t ret = storage_save_jpeg(jpg_buf, jpg_len);
        free(jpg_buf);

        if(ret != ESP_OK){
            ESP_LOGE(TAG, "Failed to save photo to SD card");
        } else {
            ESP_LOGI(TAG, "[Core 0] Photo saved to SD card successfully");
        }
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

static esp_err_t camera_start_video(void)
{
    xSemaphoreTake(s_video_mutex, portMAX_DELAY);
    if(s_camera.recording){
        xSemaphoreGive(s_video_mutex);
        return ESP_OK;
    }
    s_camera.recording = true;
    s_camera.video = NULL;
    s_record_start_time = esp_timer_get_time(); // start record camera timer
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
    // ktra trạng thái recording
    xSemaphoreTake(s_video_mutex, portMAX_DELAY);
    if(!s_camera.recording){
        xSemaphoreGive(s_video_mutex);
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreGive(s_video_mutex);

    // lấy frame rgb565
    camera_fb_t *fb = esp_camera_fb_get();
    if(fb == NULL){
        ESP_LOGE(TAG, "Failed to capture video frame");
        return ESP_FAIL;
    }

    // hiển thị frame đang quay lên màn hình live view
    // display_show_rgb565(fb->buf, 0, 0, fb->width, fb->height);

    // nén frame sang jpeg để ghi vào container avi
    uint8_t *jpg_buf = NULL;
    size_t jpg_len = 0;
    bool converted = frame2jpg(fb, 70, &jpg_buf, &jpg_len);
    
    uint32_t width = fb->width;
    uint32_t height = fb->height;
    esp_camera_fb_return(fb);

    if(!converted || jpg_buf == NULL){
        ESP_LOGI(TAG, "Video frame JPEG conversion failed");
        return ESP_FAIL;
    }

    // protect video context while creating/writing frames
    xSemaphoreTake(s_video_mutex, portMAX_DELAY);
    if(!s_camera.recording){
        xSemaphoreGive(s_video_mutex);
        free(jpg_buf);
        return ESP_ERR_INVALID_STATE;
    }
   
    // first frame , create avi
    if(s_camera.video == NULL){
        s_camera.video = storage_video_create(width, height, CAMERA_VIDEO_FPS);
        if(s_camera.video == NULL){
            ESP_LOGE(TAG, "Cannot create video file");
            xSemaphoreGive(s_video_mutex);
            free(jpg_buf);
            return ESP_FAIL;
        }
    }

    // write jpeg frame into avi
    esp_err_t ret = storage_video_write_frame(s_camera.video, jpg_buf, jpg_len);
    xSemaphoreGive(s_video_mutex);
    free(jpg_buf);

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

// camera driver init và kích hoạt ISP cho ov3660
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
    
    // CẤU HÌNH BỘ XỬ LÝ ẢNH ISP OV3660 CHO MÀU SẮC CHÂN THỰC, SẮC NÉT
    sensor->set_vflip(sensor, 1);          // Đảo ảnh đúng chiều
    sensor->set_hmirror(sensor, 0);
    
    sensor->set_whitebal(sensor, 1);       // Bật Cân bằng trắng tự động (Auto White Balance)
    sensor->set_awb_gain(sensor, 1);       // Tăng cường AWB
    sensor->set_wb_mode(sensor, 0);        // Chế độ AWB Auto
    
    sensor->set_exposure_ctrl(sensor, 1);  // Bật Phơi sáng tự động (Auto Exposure)
    sensor->set_aec2(sensor, 1);           // Kích hoạt thuật toán AEC2 nâng cao
    sensor->set_ae_level(sensor, 0);       // Mức bù sáng chuẩn
    
    sensor->set_gain_ctrl(sensor, 1);      // Tự động kiểm soát độ sáng khuếch đại (Auto Gain)
    sensor->set_gainceiling(sensor, (gainceiling_t)2);
    
    sensor->set_bpc(sensor, 1);            // Khử điểm ảnh chết màu đen
    sensor->set_wpc(sensor, 1);            // Khử điểm ảnh chết màu trắng
    sensor->set_raw_gma(sensor, 1);        // Bật Đường cong Gamma (tăng độ sâu màu)
    sensor->set_lenc(sensor, 1);           // Khử tối 4 góc ống kính (Lens Correction)
    sensor->set_dcw(sensor, 1);            // Khử nhiễu kỹ thuật số
    
    sensor->set_brightness(sensor, 1);     // Tăng nhẹ độ sáng
    sensor->set_contrast(sensor, 1);       // Tăng độ tương phản cho ảnh trong trẻo
    sensor->set_saturation(sensor, 1);     // Tăng độ đậm màu cho màu sắc tươi tắn
    ESP_LOGI(TAG, "Camera sensor OV3660 ISP configured");
    vTaskDelay(pdMS_TO_TICKS(150));

    // Xả 2 frame khởi động
    for (int i = 0; i < 2; i++) {
        camera_fb_t *fb = esp_camera_fb_get();
        if (fb) esp_camera_fb_return(fb);
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    ESP_LOGI(TAG, "Camera sensor initialized");
    return ESP_OK;
}

// camera task (run on core 1)
static void camera_task(void *arg)
{
    event_t event;
    ESP_LOGI(TAG, "Camera tasks started on Core 1");

    while(true)
    {
        if(events_receive(s_subscriber, &event, portMAX_DELAY) != pdTRUE) continue;
        if(event.channel != EVENT_CHANNEL_CAMERA) continue;
        if(event.type.camera >= CAMERA_EVENT_COUNT) continue;

        camera_handler_t handler = s_camera_handlers[event.type.camera];
        if(handler != NULL) handler();
    }
}

// liveview task (run on core 1)
static void liveview_task(void *arg){
    ESP_LOGI(TAG, "Real-time Liveview task started on Core 1");
    s_last_fps_calc_time = esp_timer_get_time();
    s_last_rem_poll_time = esp_timer_get_time();
    s_cached_rem_photos = storage_get_remaining_photos();

    while(true){
        // ktra mode hiện tại, nếu gallery hoặc chụp/ghi -> tạm dừng live
        if(mode_get() != APP_MODE_CAMERA || s_camera.capturing){
            // nếu đang quay video thì hiển thị thêm thông tin lên liveview để user biết, ko tạm dừng
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        // lấy frame từ camera
        camera_fb_t *fb = esp_camera_fb_get();
        if(fb != NULL){

            // 1. calculate fps every seconds
            int64_t now = esp_timer_get_time();
            s_fps_counter ++;
            if(now - s_last_fps_calc_time >= 1000000){
                s_real_fps = s_fps_counter;
                s_fps_counter = 0;
                s_last_fps_calc_time = now;
            }
            
            // 2. update remain photos storage every 10 seconds
            if(now - s_last_rem_poll_time >= 10000000){
                s_cached_rem_photos = storage_get_remaining_photos();
                s_last_rem_poll_time = now;
            }

            // 3. calculate video timer
            uint32_t record_sec = 0;
            bool is_video = (s_camera.capture_mode == CAMERA_CAPTURE_VIDEO);
            bool is_rec = s_camera.recording;
            if(is_rec && s_record_start_time > 0){
                record_sec = (uint32_t)((now - s_record_start_time)/1000000);
            }
            
            // 4. Resize camera frame to lcd 320x240
            if(fb->width == LCD_H_RES && fb->height == LCD_V_RES){
                memcpy(s_live_lcd_buf, fb->buf, LCD_H_RES * LCD_V_RES * sizeof(uint16_t));
            } else {
                downsample_to_lcd((const uint16_t *)fb->buf, fb->width, fb->height, s_live_lcd_buf);
            }

            // 5. draw OSD into frame buffer
            display_draw_osd_camera(s_live_lcd_buf, LCD_H_RES, LCD_V_RES, 
                                    fb->width, fb->height, is_video, is_rec,
                                    record_sec, s_real_fps, 1, s_cached_rem_photos, 80, 1);
            
            // chỗ này vẫn chưa điểu chỉnh ảnh theo độ phân giải ảnh // cẩn thận bị tràn khung ảnh -> core panic
            display_show_rgb565(s_live_lcd_buf, 0,0, LCD_H_RES, LCD_V_RES);
            esp_camera_fb_return(fb);
        }

        vTaskDelay(pdMS_TO_TICKS(10)); // delay để giữ fps ổn định và nhường cho cpu
    }
}

// video task (run on core 0)
static void video_task(void *arg)
{
    ESP_LOGI(TAG, "Video task started on Core 0");
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
            vTaskDelay(pdMS_TO_TICKS(20)); continue;
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

    // allocate a 320x240 buffer only when init
    if(s_live_lcd_buf == NULL){
        s_live_lcd_buf = heap_caps_malloc(LCD_H_RES * LCD_V_RES * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if(s_live_lcd_buf == NULL) {
            ESP_LOGE(TAG, "Cannot allocate static LCD buffer");
            return ESP_ERR_NO_MEM;
        }
    }

    s_video_mutex = xSemaphoreCreateMutex();
    if(s_video_mutex == NULL){
        ESP_LOGE(TAG, "Cannot create video mutex");
        return ESP_ERR_NO_MEM;
    }
    
    s_save_queue = xQueueCreate(2, sizeof(camera_fb_t *));
    if(s_save_queue == NULL){
        ESP_LOGE(TAG, "Cannot create save queue");
        return ESP_ERR_NO_MEM;
    }

    s_subscriber = events_subscribe(EVENT_MASK_CAMERA, CAMERA_EVENT_QUEUE_LENGTH);
    if(s_subscriber == NULL) return ESP_FAIL;

    ESP_LOGI(TAG, "Camera initialized driver successfully.");
    return ESP_OK;
}

esp_err_t camera_start(void)
{
    // Core 1
    if(s_camera_task == NULL){
        BaseType_t ret = xTaskCreatePinnedToCore(camera_task, 
                        "camera task", CAMERA_TASK_STACK_SIZE, NULL, 
                        CAMERA_TASK_PRIORITY, &s_camera_task, 1);

        if(ret != pdPASS) {
            ESP_LOGE(TAG, "Cannot create camera task"); 
            return ESP_FAIL;
        }
        ESP_LOGI(TAG, "Camera task created");
    }

    if(s_video_task == NULL){
        BaseType_t ret = xTaskCreatePinnedToCore(video_task,
                            "video task", CAMERA_TASK_STACK_SIZE, NULL, 
                            CAMERA_TASK_PRIORITY, &s_video_task, 1);

        if(ret != pdPASS) {
            ESP_LOGE(TAG, "Cannot create video task"); 
            return ESP_FAIL;
        }
        ESP_LOGI(TAG, "Video task created");
    }

    // Core 0
    if(s_liveview_task == NULL){
        BaseType_t ret = xTaskCreatePinnedToCore(liveview_task,
                            "liveview task", LIVEVIEW_TASK_STACK_SIZE, NULL,
                            LIVEVIEW_TASK_PRIORITY, &s_liveview_task, 0);
        if(ret != pdPASS) {
            ESP_LOGE(TAG,"Cannot create liveview task"); 
            return ESP_FAIL;
        }
        ESP_LOGI(TAG, "Liveview task created");
    }

    if(s_save_task == NULL){
        BaseType_t ret = xTaskCreatePinnedToCore(photo_save_task, 
                            "save photo task", SAVE_TASK_STACK_SIZE, NULL, 
                            SAVE_TASK_PRIORITY, &s_save_task, 0);

        if(ret != pdPASS) {
            ESP_LOGE(TAG, "Cannot create save task"); 
            return ESP_FAIL;
        }
    }

    ESP_LOGI(TAG, "Camera tasks pinned to core 0 and 1 successfully.");
    return ESP_OK;
}