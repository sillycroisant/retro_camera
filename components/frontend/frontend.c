#include "frontend.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "esp_log.h"

#include "storage.h"

#include "errno.h"

static const char *TAG = "Frontend";

extern const uint8_t index_html_start[]
asm("_binary_index_html_start");

extern const uint8_t index_html_end[]
asm("_binary_index_html_end");

esp_err_t frontend_init(void)
{
    size_t size = index_html_end - index_html_start;

    ESP_LOGI(TAG, "Embedded index.html size = %u", (unsigned)size);

    return ESP_OK;
}

// ktra xem file co ton tai chua
static bool file_exists(const char *path){
    struct stat st;

    return stat(path, &st) == 0;
}

// neu file ko ton tai thi se ghi vao the sd
static esp_err_t write_file(
    const char *path,
    const uint8_t *begin,
    const uint8_t *end
) {
    FILE *fp = fopen(path, "wb");

    if(fp == NULL){
        ESP_LOGE(TAG, "Cannot create %s", path);
        return ESP_FAIL;
    }

    size_t size = end - begin;
    size_t written = fwrite(begin, 1, size, fp);
    fclose(fp);

    if(written != size){
        ESP_LOGE(TAG, "Write failed.");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Created %s (%u bytes)", path, (unsigned)size);
    return ESP_OK;
}

// sync file tren project vs the nho board
esp_err_t frontend_sync_to_sd(void){
    const char *dir = "/sdcard/www";
    struct stat st;

    if(stat(dir, &st) != 0){
        if(mkdir(dir, 0775) != 0){
            ESP_LOGE(TAG, "Cannot create www");
            return ESP_FAIL;
        }
        ESP_LOGI(TAG, "Created /www");
    }

    const char *index_path = "/sdcard/www/index.html";

    if(file_exists(index_path)){
        ESP_LOGI(TAG, "index.html already exists");
        return ESP_OK;
    }

    ESP_LOGI(TAG,
            "Copy embedded index.html");

    return write_file(
                index_path,
                index_html_start,
                index_html_end);
}