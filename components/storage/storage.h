#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct 
{   
    // for later, error check
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;

    // currently using
    uint32_t next_index;
    uint32_t image_count;
} storage_index_t;

/**
 * @brief Mount SD card and prepare photos folder
 */
esp_err_t storage_init(void);


/**
 * @brief Unmount SD card
 */
esp_err_t storage_deinit(void);


/** 
* @brief Save jpeg image into SD card
* @param data jpeg image pointe
* @param len  size of jpeg image
* @return ESP_OKE if success
*/
 esp_err_t storage_save_jpeg(
    const uint8_t *data,
    size_t len);


/**
 * @brief Directory path of the latest image
 */
const char *storage_latest_path(void);


/**
 * @brief Filename of latest image
 */
const char *storage_latest_filename(void);


/**
 * @brief Number of saved images in storage
 */
uint32_t storage_image_count(void);


/**
 * @brief Open image base on URI
 */

esp_err_t storage_open_uri(const char *uri, FILE **fp);

// Close storage
void storage_close(FILE *fp);

#ifdef __cplustplus
}
#endif

