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

#define INPUT_QUEUE_SIZE        5
#define INPUT_TASK_STACK_SIZE   4096
#define INPUT_TASK_PRIORITY     5
#define INPUT_DEBOUNCE_MS       50

#define GPIO_BUTTON_ID_1 GPIO_NUM_16
#define GPIO_BUTTON_ID_2 GPIO_NUM_14
#define GPIO_BUTTON_ID_3 GPIO_NUM_15
#define GPIO_BUTTON_ID_4 GPIO_NUM_2

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

static QueueHandle_t s_button_queue = NULL;

static TaskHandle_t s_input_task = NULL;

static TickType_t s_last_tick[BUTTON_COUNT] = {0};

static volatile uint32_t s_isr_count = 0;

typedef struct
{
    gpio_num_t gpio;

    button_id_t button;

} button_gpio_map_t;

static const button_gpio_map_t s_buttons[BUTTON_COUNT] =
{
    {
        .gpio = GPIO_BUTTON_ID_1,
        .button = BUTTON_ID_1
    },
    {
        .gpio = GPIO_BUTTON_ID_2,
        .button = BUTTON_ID_2
    },
    {
        .gpio = GPIO_BUTTON_ID_3,
        .button = BUTTON_ID_3
    },
    {
        .gpio = GPIO_BUTTON_ID_4,
        .button = BUTTON_ID_4
    }
};

static bool input_debounce(button_id_t button)
{
    if(button >= BUTTON_COUNT) return false;

    TickType_t now = xTaskGetTickCount();

    if(now - s_last_tick[button] < pdMS_TO_TICKS(INPUT_DEBOUNCE_MS)) return false;

    s_last_tick[button] = now;

    return true;
}

static void IRAM_ATTR input_gpio_isr(void *arg)
{
    const button_gpio_map_t *button = (const button_gpio_map_t *)arg;

    s_isr_count ++;

    button_event_t event = { .button = button->button };

    BaseType_t hp_task_woken = pdFALSE;

    xQueueSendFromISR(
        s_button_queue,
        &event,
        &hp_task_woken);

    if (hp_task_woken)
    {
        portYIELD_FROM_ISR();
    }
}

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
                    break;

                case BUTTON_ID_2:
                    event->type.camera = CAMERA_EVENT_FLASH_TOGGLE;
                    break;
                    
                case BUTTON_ID_3:
                    event->type.camera = CAMERA_EVENT_TOGGLE_VIDEO;
                    break;

                case BUTTON_ID_4:
                    event->type.camera = CAMERA_EVENT_OPEN_GALLERY;
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
                    break;

                case BUTTON_ID_2:
                    event->type.gallery = GALLERY_EVENT_SCROLL_UP;
                    break;

                case BUTTON_ID_3:
                    event->type.gallery = GALLERY_EVENT_SCROLL_DOWN;
                    break;
                    
                case BUTTON_ID_4:
                    event->type.gallery = GALLERY_EVENT_OPEN_CAMERA;
                    break;

                default:
                    return false;
            }
            return true;

        default:
            return false;
    }
}

static void input_task(void *arg)
{
    button_event_t button_event;
    event_t event;

    ESP_LOGI(TAG, "Input task started");
    ESP_LOGI(TAG, "ISR count=%lu", (unsigned long)s_isr_count);

    while(1)
    {
        if(xQueueReceive(s_button_queue, &button_event, portMAX_DELAY) != pdTRUE) continue;
        ESP_LOGI(TAG, "Button %d pressed", button_event.button);
        if(!input_debounce(button_event.button)) continue;

        if(!input_translate(button_event.button, &event)) continue;
        ESP_LOGI(TAG, "Publish channel=%d type=%d", event.channel, event.type.raw);
        esp_err_t ret = events_publish(&event);

        if(ret != ESP_OK) ESP_LOGW(TAG, "Failed to publish event (%s)", esp_err_to_name(ret));
    }
}


esp_err_t input_init(void)
{
    if (s_button_queue != NULL) return ESP_OK;

    s_button_queue = xQueueCreate(INPUT_QUEUE_SIZE, sizeof(button_event_t));

    if (s_button_queue == NULL) return ESP_ERR_NO_MEM;

    gpio_config_t io =
    {
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE
    };

    for (int i = 0; i < BUTTON_COUNT; i++)
    {
        io.pin_bit_mask = 1ULL << s_buttons[i].gpio;

        ESP_ERROR_CHECK(gpio_config(&io));

        esp_err_t err = gpio_isr_handler_add(
                s_buttons[i].gpio, input_gpio_isr, (void *)&s_buttons[i]);

        printf("handler_add=%s\n", esp_err_to_name(err));
    }

    ESP_LOGI(TAG, "%d buttons initialized", BUTTON_COUNT);

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