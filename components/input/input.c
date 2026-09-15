#include "input.h"

#include "string.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "mode.h"
#include "events.h"

// config
#define TAG "Input"

#define INPUT_QUEUE_SIZE        1
#define INPUT_TASK_STACK_SIZE   4096
#define INPUT_TASK_PRIORITY     5
#define INPUT_DEBOUNCE_MS       250

#define GPIO_BUTTON_ID_1 GPIO_NUM_1
#define GPIO_BUTTON_ID_2 GPIO_NUM_2
#define GPIO_BUTTON_ID_3 GPIO_NUM_3
#define GPIO_BUTTON_ID_4 GPIO_NUM_41

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
    {
        .gpio = GPIO_BUTTON_ID_1, .button = BUTTON_ID_1
    },
    {
        .gpio = GPIO_BUTTON_ID_2, .button = BUTTON_ID_2
    },
    {
        .gpio = GPIO_BUTTON_ID_3, .button = BUTTON_ID_3
    },
    {
        .gpio = GPIO_BUTTON_ID_4, .button = BUTTON_ID_4
    }
};


static QueueHandle_t s_button_queue = NULL;
static TaskHandle_t s_input_task = NULL;
static TickType_t s_last_tick[BUTTON_COUNT] = {0};
static volatile uint32_t s_isr_count = 0;

// hàm chống rung phím (button debounce)
static bool input_debounce(button_id_t button)
{
    if(button >= BUTTON_COUNT) return false;

    TickType_t now = xTaskGetTickCount();

    if(now - s_last_tick[button] < pdMS_TO_TICKS(INPUT_DEBOUNCE_MS)) return false;

    s_last_tick[button] = now;
    return true;
}

// hàm phục vụ ngắt gpio isr (chạy trong IRAM)
static void IRAM_ATTR input_gpio_isr(void *arg)
{
    const button_gpio_map_t *button = (const button_gpio_map_t *)arg;
    s_isr_count ++;

    button_event_t event = { .button = button->button };
    BaseType_t hp_task_woken = pdFALSE;

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
                    event->type.gallery = GALLERY_EVENT_SCROLL_UP;
                    ESP_LOGI(TAG, "[Btn 2 - GPIO 2] Event: SCROLL UP");
                    break;

                case BUTTON_ID_3:
                    event->type.gallery = GALLERY_EVENT_SCROLL_DOWN;
                    ESP_LOGI(TAG, "[Btn 3 - GPIO 3] Event: SCROLL DOWN");
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

    ESP_LOGI(TAG, "Input task started successfully");
    ESP_LOGI(TAG, "ISR count=%lu", (unsigned long)s_isr_count);

    while(1)
    {
        // chờ tín hiệu ngắt từ isr
        if(xQueueReceive(s_button_queue, &button_event, portMAX_DELAY) != pdTRUE) continue;

        // debounce các tín hiệu ngắt
        if(!input_debounce(button_event.button)) continue;

        // ánh xạ chức năng nút bấm theo mode
        if(!input_translate(button_event.button, &event)) continue;
        
        // ESP_LOGI(TAG, "Publish channel=%d type=%d", event.channel, event.type.raw);
        esp_err_t ret = events_publish(&event);

        if(ret != ESP_OK) ESP_LOGW(TAG, "Failed to publish event (%s)", esp_err_to_name(ret));
    }
}

// khởi tạo gpio và interrupt
esp_err_t input_init(void)
{
    if (s_button_queue != NULL) return ESP_OK;

    // tạo hàng đợi nhận event từ isr
    s_button_queue = xQueueCreate(INPUT_QUEUE_SIZE, sizeof(button_event_t));
    if (s_button_queue == NULL) return ESP_ERR_NO_MEM;

    // khởi tại isr interrupt gpio
    esp_err_t err = gpio_install_isr_service(0);
    if(err != ESP_OK && err != ESP_ERR_INVALID_STATE){
        ESP_LOGE(TAG, "failed to initialized gpio service: %s", esp_err_to_name(err));
        return err;
    }

    // cấu hình chế độ cho từng chân gpio
    gpio_config_t io =
    {
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_POSEDGE
    };

    for (int i = 0; i < BUTTON_COUNT; i++)
    {
        gpio_num_t pin = s_buttons[i].gpio;
        io.pin_bit_mask = 1ULL << pin;

        ESP_ERROR_CHECK(gpio_config(&io));

        esp_err_t err = gpio_isr_handler_add(pin, input_gpio_isr, (void*)&s_buttons[i]);

        if(err != ESP_OK){
            ESP_LOGE(TAG, "Failed to add ISR handler for GPIO %d: %s", pin, esp_err_to_name(err));
            return err;
        }
        ESP_LOGI(TAG, "Button %d (GPIO %d) configured", i+1, pin);
    }

    ESP_LOGI(TAG, "All %d buttons initialized with interrupted!", BUTTON_COUNT);
    return ESP_OK;
}

esp_err_t input_start(void)
{
    if(s_input_task != NULL) return ESP_OK;

    BaseType_t ret = xTaskCreate(
        input_task,
        "input_task",
        INPUT_TASK_STACK_SIZE,
        NULL,
        INPUT_TASK_PRIORITY,
        &s_input_task
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Cannot create input task");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Input task created");

    return ESP_OK;
}