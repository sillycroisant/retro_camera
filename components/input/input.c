#include "input.h"

#include "string.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "mode.h"
#include "events.h"

// config
#define TAG "Input"

#define INPUT_QUEUE_SIZE        5
#define INPUT_TASK_STACK_SIZE   4096
#define INPUT_TASK_PRIORITY     5
#define INPUT_DEBOUNCE_MS       100000

#define GPIO_BUTTON_ID_1 GPIO_NUM_0 // dùng nút boot có sẵn trên bo mạch
#define GPIO_BUTTON_ID_2 GPIO_NUM_3 
#define GPIO_BUTTON_ID_3 GPIO_NUM_42
#define GPIO_BUTTON_ID_4 GPIO_NUM_46

typedef enum
{
    BUTTON_ID_1 = 0,
    BUTTON_ID_2,
    BUTTON_ID_3,
    BUTTON_ID_4,

    BUTTON_COUNT
} button_id_t;

typedef struct
{
    button_id_t button;
} button_event_t;

typedef struct {
    gpio_num_t gpio;
    button_id_t button; 
} button_gpio_map_t;

static const button_gpio_map_t s_buttons[BUTTON_COUNT] =
{
    {   .gpio = GPIO_BUTTON_ID_1, .button = BUTTON_ID_1},
    {   .gpio = GPIO_BUTTON_ID_2, .button = BUTTON_ID_2},
    {   .gpio = GPIO_BUTTON_ID_3, .button = BUTTON_ID_3},
    {   .gpio = GPIO_BUTTON_ID_4, .button = BUTTON_ID_4}
};

static QueueHandle_t s_button_queue = NULL;
static TaskHandle_t s_input_task = NULL;
static TickType_t s_last_isr_time[BUTTON_COUNT] = {0};
static volatile uint32_t s_isr_count = 0;

// hàm phục vụ ngắt gpio isr (chạy trong IRAM)
static void IRAM_ATTR input_gpio_isr(void *arg)
{
    const button_gpio_map_t *btn_map = (const button_gpio_map_t *)arg;
    button_id_t btn = btn_map->button;
    if(btn >= BUTTON_COUNT) return;

    int64_t now = esp_timer_get_time();
    if((now - s_last_isr_time[btn]) < INPUT_DEBOUNCE_MS) return;
    s_last_isr_time[btn] = now;
    s_isr_count ++;

    button_event_t event = { .button = btn };
    BaseType_t hp_task_woken = pdFALSE;

    // only send 1 event into queue over 150ms period
    xQueueSendFromISR(s_button_queue, &event, &hp_task_woken);

    if (hp_task_woken){
        portYIELD_FROM_ISR();
    }
}

// ánh xạ nút bấm sang event của hệ thống dựa theo mode hiện tại
static bool input_translate(button_id_t button, event_t *event)
{
    if(event == NULL) return false;
    memset(event, 0, sizeof(event_t));

    switch (mode_get())
    {
        case APP_MODE_CAMERA:
            event->channel = EVENT_CHANNEL_CAMERA;

            switch (button)
            {
                case BUTTON_ID_1:
                    event->type.camera = CAMERA_EVENT_CAPTURE;
                    ESP_LOGI(TAG, "[Btn1 - GPIO 1] Event: CAPTURE/ RECORD");
                    break;

                case BUTTON_ID_2:
                    event->type.camera = CAMERA_EVENT_FLASH_TOGGLE;
                    ESP_LOGI(TAG, "[Btn2 - GPIO 2] Event: TOGGLE FLASH");
                    break;
                    
                case BUTTON_ID_3:
                    event->type.camera = CAMERA_EVENT_TOGGLE_VIDEO;
                    ESP_LOGI(TAG, "[Btn3 - GPIO 3] Event: TOGGLE PHOTO/VIDEO");
                    break;

                case BUTTON_ID_4:
                    event->type.camera = CAMERA_EVENT_OPEN_GALLERY;
                    ESP_LOGI(TAG, "[Btn4 - GPIO 41] Event: OPEN GALLERY");
                    break;

                default:
                    return false;
            }
            return true;

        case APP_MODE_GALLERY:
            event->channel = EVENT_CHANNEL_GALLERY;

            switch (button)
            {
                case BUTTON_ID_1:
                    event->type.gallery = GALLERY_EVENT_DUMMY;
                    ESP_LOGI(TAG, "[Btn 1 - GPIO 1] Event: DUMMY EVENT");
                    break;

                case BUTTON_ID_2:
                    event->type.gallery = GALLERY_EVENT_SCROLL_FORWARD;
                    ESP_LOGI(TAG, "[Btn 2 - GPIO 2] Event: SCROLL FORWARD");
                    break;

                case BUTTON_ID_3:
                    event->type.gallery = GALLERY_EVENT_SCROLL_BACK;
                    ESP_LOGI(TAG, "[Btn 3 - GPIO 3] Event: SCROLL BACK");
                    break;
                    
                case BUTTON_ID_4:
                    event->type.gallery = GALLERY_EVENT_OPEN_CAMERA;
                    ESP_LOGI(TAG, "[Btn 4 - GPIO 41] Event: BACK TO CAMERA");
                    break;

                default:
                    return false;
            }
            return true;

        default:
            return false;
    }
}

// tác vụ xử lý nút bấm
static void input_task(void *arg)
{
    button_event_t button_event;
    event_t event;

    ESP_LOGI(TAG, "Input task started successfully (Queue size:%d)", INPUT_QUEUE_SIZE);

    while(1)
    {
        // 1. chờ tín hiệu ngắt từ isr
        if(xQueueReceive(s_button_queue, &button_event, portMAX_DELAY) != pdTRUE) continue;

        button_id_t btn = button_event.button;
        if(btn >= BUTTON_COUNT) continue;
    
        // 2. ánh xạ chức năng nút bấm theo mode
        if(!input_translate(button_event.button, &event)) continue;
        esp_err_t ret = events_publish(&event);
        if(ret != ESP_OK) ESP_LOGW(TAG, "Failed to publish event (%s)", esp_err_to_name(ret));
    }
}

// khởi tạo gpio và interrupt
esp_err_t input_init(void)
{
    if (s_button_queue != NULL) return ESP_OK;

    s_button_queue = xQueueCreate(INPUT_QUEUE_SIZE, sizeof(button_event_t));
    if (s_button_queue == NULL){
        ESP_LOGE(TAG, "Failed to create button queue");
        return ESP_ERR_NO_MEM;
    }

    // khởi tại isr interrupt gpio
    esp_err_t err = gpio_install_isr_service(0);
    if(err != ESP_OK && err != ESP_ERR_INVALID_STATE){
        ESP_LOGE(TAG, "Failed to initialized gpio service: %s", esp_err_to_name(err));
        return err;
    }

    // cấu hình chế độ cho từng chân gpio
    gpio_config_t io =
    {
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE
    };

    for (int i = 0; i < BUTTON_COUNT; i++)
    {
        gpio_num_t pin = s_buttons[i].gpio;
        gpio_reset_pin(pin);
        io.pin_bit_mask = 1ULL << pin;
        ESP_ERROR_CHECK(gpio_config(&io));

        esp_err_t err = gpio_isr_handler_add(pin, input_gpio_isr, (void*)&s_buttons[i]);

        if(err != ESP_OK){
            ESP_LOGE(TAG, "Failed to add ISR handler for GPIO %d: %s", pin, esp_err_to_name(err));
            return err;
        }
        ESP_LOGI(TAG, "Button %d (GPIO %d) configured", i+1, pin);
    }

    ESP_LOGI(TAG, "All %d buttons initialized (Debounce: %d ms, Queue: %d)", BUTTON_COUNT, INPUT_DEBOUNCE_MS / 1000, INPUT_QUEUE_SIZE);
    return ESP_OK;
}

esp_err_t input_start(void)
{
    if(s_input_task != NULL) return ESP_OK;

    BaseType_t ret = xTaskCreatePinnedToCore(
        input_task, "input_task",
        INPUT_TASK_STACK_SIZE,
        NULL, INPUT_TASK_PRIORITY,
        &s_input_task, 0
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Cannot create input task");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Input task pinned to core 0");
    return ESP_OK;
}