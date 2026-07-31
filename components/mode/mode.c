#include "mode.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static app_mode_t s_mode = APP_MODE_CAMERA;

static SemaphoreHandle_t s_mutex = NULL;

esp_err_t mode_init(void)
{
    if (s_mutex != NULL)
    {
        return ESP_OK;
    }

    s_mutex = xSemaphoreCreateMutex();

    if (s_mutex == NULL)
    {
        return ESP_ERR_NO_MEM;
    }

    s_mode = APP_MODE_CAMERA;

    return ESP_OK;
}

app_mode_t mode_get(void)
{
    app_mode_t mode;

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    mode = s_mode;

    xSemaphoreGive(s_mutex);

    return mode;
}

void mode_set(app_mode_t mode)
{
    if (mode >= APP_MODE_COUNT)
    {
        return;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    s_mode = mode;

    xSemaphoreGive(s_mutex);
}