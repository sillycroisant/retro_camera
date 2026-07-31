#ifndef INPUT_H
#define INPUT_H

#include "stdbool.h"

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t input_init(void);

esp_err_t input_start(void);

#ifdef __cplusplus
}
#endif
#endif