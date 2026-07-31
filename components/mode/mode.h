#ifndef MODE_H
#define MODE_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    APP_MODE_CAMERA = 0,

    APP_MODE_GALLERY,

    APP_MODE_COUNT

} app_mode_t;

esp_err_t mode_init(void);

app_mode_t mode_get(void);

void mode_set(app_mode_t mode);

#ifdef __cplusplus
}
#endif

#endif