#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "esp_err.h"

#include "stdint.h"
#include "stdbool.h"

#ifdef __cplusplus
extern "C" {
#endif

// event channels
typedef enum
{
    EVENT_CHANNEL_CAMERA = 0,
    EVENT_CHANNEL_GALLERY,

    EVENT_CHANNEL_COUNT

} event_channel_t;

// camera events
typedef enum
{
    CAMERA_EVENT_CAPTURE = 0,
    CAMERA_EVENT_FLASH_TOGGLE,
    CAMERA_EVENT_TOGGLE_VIDEO,
    CAMERA_EVENT_OPEN_GALLERY,
    CAMERA_EVENT_COUNT

} camera_event_type_t;

// gallery events
typedef enum
{
    GALLERY_EVENT_DUMMY = 0,
    GALLERY_EVENT_SCROLL_UP,
    GALLERY_EVENT_SCROLL_DOWN,
    GALLERY_EVENT_OPEN_CAMERA,
    GALLERY_EVENT_COUNT
    
} gallery_event_type_t;

#define EVENT_MASK_CAMERA   (1UL << EVENT_CHANNEL_CAMERA)
#define EVENT_MASK_GALLERY  (1UL << EVENT_CHANNEL_GALLERY)

// event object
typedef struct
{
    event_channel_t channel;

    union
    {
        camera_event_type_t camera;

        gallery_event_type_t gallery;

        uint16_t raw;
    } type;
    
    uint32_t timestamp;

    uint32_t param;

    void *data;
    
} event_t;

// subscriber handle
typedef struct event_subscriber event_subscriber_t;

// event system
esp_err_t events_init(void);

esp_err_t events_deinit(void);

// subscribe/unsubscribe
event_subscriber_t *events_subscribe(uint32_t channel_mask, uint32_t queue_size);

esp_err_t events_unsubscribe(event_subscriber_t *subscriber);

// publish
esp_err_t events_publish(const event_t *event);

// receive
BaseType_t events_receive(
    event_subscriber_t *s_subscriber,
    event_t *event,
    TickType_t timeout);

bool events_is_initialized(void);
#ifdef __cplusplus
}
#endif