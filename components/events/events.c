#include "events.h"
#include "string.h"
#include "esp_log.h"
#include "freertos/semphr.h"

// config
#define TAG "Events"

#define EVENTS_MAX_SUBSCRIBERS 5

#define EVENTS_DEFAULT_QUEUE_SIZE 5

// internal subscriber
struct event_subscriber
{
    QueueHandle_t queue;
    uint32_t channel_mask;
};

// globals
static struct event_subscriber s_subscribers[EVENTS_MAX_SUBSCRIBERS];

static SemaphoreHandle_t s_mutex = NULL;

static bool s_initialized = false;

// private helpers
static inline bool events_channel_valid(event_channel_t channel)
{
    return channel < EVENT_CHANNEL_COUNT;
}

static inline uint32_t events_channel_to_mask(event_channel_t channel)
{
    return (1UL << channel);
}

static void events_lock(void)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
}

static void events_unlock(void)
{
    xSemaphoreGive(s_mutex);
}

static struct event_subscriber *events_find_free_slot(void)
{
    for(int i = 0; i < EVENTS_MAX_SUBSCRIBERS; i++){
        if(s_subscribers[i].queue == NULL)
        {
            return &s_subscribers[i];
        }
    }

    return NULL;
}

static bool events_validate_subscriber(
    const event_subscriber_t *subscriber
) {
    if(subscriber == NULL) return false;

    return (subscriber >= &s_subscribers[0]) && (
        subscriber < &s_subscribers[EVENTS_MAX_SUBSCRIBERS]);
}

bool events_is_initialized(void)
{
    return s_initialized;
}

esp_err_t events_init(void)
{
    if (s_initialized) return ESP_OK;

    memset(s_subscribers, 0, sizeof(s_subscribers));
    
    s_mutex = xSemaphoreCreateMutex();

    if(s_mutex == NULL)
    {
        ESP_LOGE(TAG, "Cannot create mutex");
        return ESP_ERR_NO_MEM;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "Event bus initialized");
    ESP_LOGI(TAG, "Maximum subscriber: %d", EVENTS_MAX_SUBSCRIBERS);
    ESP_LOGI(TAG, "Event system initialized");
    return ESP_OK;
}

event_subscriber_t *events_subscribe(
                        uint32_t channel_mask,
                        uint32_t queue_size)
{
    if(!s_initialized)
    {   
        ESP_LOGE(TAG, "Event bus not initialized");
        return NULL;
    }

    if(channel_mask == 0)
    {
        ESP_LOGE(TAG, "Invalid channel");
        return NULL;
    }

    if (queue_size == 0)
    {
        queue_size = EVENTS_DEFAULT_QUEUE_SIZE;
    }

    events_lock();

    struct event_subscriber *subscriber = events_find_free_slot();

    if(subscriber == NULL){
        events_unlock();
        ESP_LOGE(TAG, "Subscriber table full");
        return NULL;
    }

    subscriber->queue = xQueueCreate(queue_size, sizeof(event_t));

    if(subscriber->queue == NULL)
    {
        events_unlock();
        ESP_LOGE(TAG, "Cannot create subscriber queue");
        return NULL;
    }

    subscriber->channel_mask = channel_mask;
    ESP_LOGI(TAG, "Subscriber registered");
    ESP_LOGI(TAG, "Mask: 0x%08lx", (unsigned long)channel_mask);
    ESP_LOGI(TAG, "Queue size: %lu", (unsigned long)queue_size);
    
    events_unlock();

    return subscriber;
}

esp_err_t events_publish(const event_t *event)
{   
    ESP_LOGI(TAG, "Publish mask=%08lx", (unsigned long)(1UL << event->channel));
    if(!s_initialized){
        return ESP_ERR_INVALID_STATE;
    }

    if(event == NULL){
        return ESP_ERR_INVALID_ARG;
    }

    if(!events_channel_valid(event->channel))
    {
        ESP_LOGE(TAG, "Invalid channel");
        return ESP_ERR_INVALID_ARG;
    }

    uint32_t mask = events_channel_to_mask(event->channel);

    event_t copy = *event;

    copy.timestamp = xTaskGetTickCount();

    events_lock();

    for(int i = 0; i < EVENTS_MAX_SUBSCRIBERS; i++)
    {
        event_subscriber_t *subscriber = &s_subscribers[i];
        
        ESP_LOGI(TAG, "subscriber=%d mask=%08lx", i, (unsigned long)s_subscribers[i].channel_mask);

        if(subscriber->queue == NULL) continue;

        if((subscriber->channel_mask & mask) == 0) continue;

        if(xQueueSend(subscriber->queue, &copy, 0) != pdPASS)
        {
            ESP_LOGW(TAG, "Subscriber %d queue full", i);
        }
    }
    events_unlock();
    return ESP_OK;
}

esp_err_t events_receive(
    event_subscriber_t *subscriber,
    event_t *event,
    TickType_t timeout
) {
    if(!events_validate_subscriber(subscriber)) return pdFALSE;

    if(subscriber->queue == NULL) return pdFALSE;

    return xQueueReceive(subscriber->queue, event, timeout);
}

esp_err_t events_unsubscribe(
    event_subscriber_t *subscriber
) {
    if(!s_initialized) return ESP_ERR_INVALID_STATE;

    if(!events_validate_subscriber(subscriber)) return ESP_ERR_INVALID_ARG;

    events_lock();

    if(subscriber->queue != NULL)
    {
        vQueueDelete(subscriber->queue);
        subscriber->queue = NULL;
    }

    subscriber->channel_mask = 0;
    events_unlock();
    ESP_LOGI(TAG, "Subscriber removed");
    return ESP_OK;
}