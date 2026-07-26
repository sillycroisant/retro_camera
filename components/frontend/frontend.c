#include "frontend.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "esp_log.h"

#include "storage.h"

#include "errno.h"

#define FRONTEND_ASSET_COUNT (sizeof(assets)/sizeof(assets[0]))

static const char *TAG = "Frontend";

// index for html
extern const uint8_t index_html_start[]
asm("_binary_index_html_start");

extern const uint8_t index_html_end[]
asm("_binary_index_html_end");

// index for css
extern const uint8_t style_css_start[]
asm("_binary_style_css_start");

extern const uint8_t style_css_end[]
asm("_binary_style_css_end");

// index for javascript
extern const uint8_t app_js_start[]
asm("_binary_app_js_start");

extern const uint8_t app_js_end[]
asm("_binary_app_js_end");

typedef struct
{
    const char *filename;
    const uint8_t *begin;
    const uint8_t *end;
} frontend_asset_t;

static const frontend_asset_t assets[] = {
    {
        "index.html", index_html_start, index_html_end
    },
    {
        "style.css", style_css_start, style_css_end
    },
    {
        "app.js", app_js_start, app_js_end
    }
};

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
static esp_err_t update_asset(
    const char *path,
    const uint8_t *begin,
    const uint8_t *end
) {
    
    ESP_LOGI(TAG, "Writing to: %s", path);
    
    FILE *fp = fopen(path, "wb");

    if(fp == NULL){
        ESP_LOGE(
            TAG, "Cannot create %s errno=%d (%s)", 
            path, errno, strerror(errno));

        return ESP_FAIL;
    }

    size_t size = end - begin;

    ESP_LOGI(TAG, "begin = %p", begin);
    ESP_LOGI(TAG, "end   = %p", end);
    ESP_LOGI(TAG, "size  = %u", (unsigned)size);

    size_t written = fwrite(begin, 1, size, fp);
    fclose(fp);

    if(written != size){
        ESP_LOGE(TAG, "Write failed.");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Created %s (%u bytes)", path, (unsigned)size);
    return ESP_OK;
}

static bool file_equals(
    const char *path,
    const uint8_t *begin, const uint8_t *end
) {
    FILE *fp = fopen(path, "rb");

    if(fp == NULL){
        return false;
    }

    size_t asset_size = end - begin;


    fseek(fp, 0, SEEK_END);
    size_t file_size = ftell(fp);
    rewind(fp);
    
    // debug here
    char text[512];
    size_t n = fread(text,1,sizeof(text)-1,fp);
    text[n]=0;
    ESP_LOGI(TAG,"===== SD File =====");
    printf("%s\n",text);
    rewind(fp);

    ESP_LOGI(TAG, "===== Embedded ====="); 
    printf("%.*s\n", (int)asset_size, (const char *)begin);

    
    ESP_LOGI(TAG, "Compare %s: asset=%u file=%u",
         path, (unsigned)asset_size,(unsigned)file_size);
    // debug end here

    rewind(fp);

    if(file_size != asset_size){
        fclose(fp);
        return false;
    }

    uint8_t buffer[512];
    size_t offset = 0;

    while (offset < asset_size){
        size_t remain = asset_size - offset;

        size_t chunk = remain > sizeof(buffer) ? sizeof(buffer) : remain;

        if(fread(buffer, 1, chunk, fp) != chunk)
        {
            fclose(fp);
            return false;
        }

        if(memcmp(buffer, begin+offset, chunk) != 0){
            ESP_LOGI(TAG, "File differs at offset %u", (unsigned)offset);
            fclose(fp);
            return false;
        }

        offset += chunk;
    }

    fclose(fp);
    return true;
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

    for (size_t i = 0; i < FRONTEND_ASSET_COUNT; i++)
    {
        char path[128];

        snprintf(path, sizeof(path), "/sdcard/www/%s", assets[i].filename);
        
        if(!file_exists(path))
        {
            ESP_LOGI(TAG, "%s not found -> create", assets[i].filename);

            ESP_ERROR_CHECK(update_asset(path, assets[i].begin, assets[i].end));
            continue;
        }

        if(file_equals(path, assets[i].begin, assets[i].end)) 
        {
            ESP_LOGI(TAG, "%s already up-to-date", assets[i].filename);
            continue;
        } else {
            ESP_LOGI(TAG, "%s changed -> update", assets[i].filename);
            ESP_ERROR_CHECK(update_asset(path, assets[i].begin, assets[i].end));
        }

    }

    return ESP_OK;
}