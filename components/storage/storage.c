// include libraries and inits
#include "storage.h"

#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

#include "esp_log.h"
#include "esp_vfs_fat.h"

#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"

#define STORAGE_ROOT    "/sdcard"
#define PHOTO_DIRECTORY "/sdcard/photos"
#define INDEX_FILE      "/sdcard/index.dat"
#define STORAGE_INDEX_MAGIC NULL
#define STORAGE_INDEX_VERSION 1
#define STORAGE_MOUNT_POINT "/sdcard"
#define STORAGE_WWW_DIR "/www"

static const char *TAG = "[Storage]";

static sdmmc_card_t *card = NULL;

static uint32_t current_index = 1;
static uint32_t image_count = 0;

static char latest_path[128] = "";
static char latest_filename[32] = "";

static storage_index_t g_index;

//functions
static void storage_scan_directory(void)
{
    DIR *dir = opendir(PHOTO_DIRECTORY);

    if (dir == NULL){
        ESP_LOGW(TAG, "Cannot open photos directory.");
        current_index = 1;
        image_count = 0;
        return;
    }

    struct dirent *entry;
    uint32_t max_index = 0;

    while((entry = readdir(dir)) != NULL)
    {
        uint32_t index;

        if(sscanf(entry->d_name, "photo_%lu.jpg", &index) == 1)
        {
            image_count++;

            if(index > max_index) {
                max_index = index; 
            }
        }
    }
    
    closedir(dir);
    current_index = max_index + 1;
    ESP_LOGI(TAG, "Found %lu photos", (unsigned long)image_count);
}


static bool storage_load_index(void)
{
    FILE *fp = fopen(INDEX_FILE, "rb");

    if (fp == NULL){
        return false;
    }

    size_t n = fread(
        &g_index,
        sizeof(g_index),
        1, fp
    );

    fclose(fp);
    return (n == 1);
}

// explain for me what is this function do in 1 sentence.
static void storage_save_index(void)
{
    FILE *fp = fopen(INDEX_FILE, "wb");

    if (fp == NULL) {
        ESP_LOGE(TAG, "Cannot write index.dat");
        return;
    }

    fwrite(
        &g_index,
        sizeof(g_index),
        1, fp
    );

    fclose(fp);
}


esp_err_t storage_init(void){
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };

    ESP_LOGI(TAG, "Mounting SD card...");

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();

    // AI Thinker esp32-cam
    // using 1but mode for stablity
    slot_config.width = 1;

    esp_err_t ret = esp_vfs_fat_sdmmc_mount(
        "/sdcard",
        &host,
        &slot_config,
        &mount_config,
        &card);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount SD card (%s)", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "SD card mounted.");
    sdmmc_card_print_info(stdout, card);
    
    // check photos folder and create if not exist yet
    struct stat st;

    if(stat(PHOTO_DIRECTORY, &st) != 0)
    {
        mkdir(PHOTO_DIRECTORY, 0775);
        ESP_LOGI(TAG, "Created /photos.");
    }

    //
    if (storage_load_index()){
        current_index = g_index.next_index;
        image_count = g_index.image_count;

        ESP_LOGI(TAG, "Index loaded (%lu)", (unsigned long)current_index);
    } else {

        ESP_LOGW(TAG, "index.dat file missing");

        storage_scan_directory();
        
        g_index.next_index = current_index;
        g_index.image_count = image_count;

        storage_save_index();
    }

    return ESP_OK;
}


esp_err_t storage_save_jpeg(
    const uint8_t *jpeg,
    size_t len
) {
    snprintf(
        latest_filename,
        sizeof(latest_filename),
        "photo_%06lu.jpg",
        (unsigned long)current_index        
    );

    snprintf(
        latest_path,
        sizeof(latest_path),
        "%s/%s",
        PHOTO_DIRECTORY,
        latest_filename
    );

    FILE *fp = fopen(latest_path, "wb");

    if (fp == NULL) {
        ESP_LOGE(TAG, "Cannot create image file");
        return ESP_FAIL;
    }

    fwrite(jpeg, 1, len, fp);
    fclose(fp);
    ESP_LOGI(TAG, "Saved %s (%u bytes)", latest_filename, (unsigned)len);

    current_index++;
    image_count++;

    g_index.next_index = current_index;
    g_index.image_count = image_count;

    storage_save_index();
    
    return ESP_OK;
}


const char *storage_latest_path(void){
    return latest_path;
}


const char *storage_latest_filename(void){
    return latest_filename;
}


uint32_t storage_image_count(void){
    return image_count;
}


esp_err_t storage_deinit(void){
    return esp_vfs_fat_sdcard_unmount(STORAGE_ROOT, card);
}


esp_err_t sdcard_save_file(
    const char *filename,
    const uint8_t *data,
    size_t len
) {
    char path[128];

    snprintf(path,
            sizeof(path),
            "/sdcard/%s",
            filename);

    FILE *fp = fopen(path, "wb");

    if (fp == NULL){
        ESP_LOGE(TAG, "Cannot open %s", path);
        return ESP_FAIL;
    }

    fwrite(data, 1, len, fp);
    fclose(fp);

    ESP_LOGI(TAG, "Saved %s (%u bytes)", path, (unsigned)len);
    return ESP_OK;
}

static esp_err_t uri_to_path(
    const char *uri,
    char *path,
    size_t path_size
)
{
    if (uri == NULL || path == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if(strncmp(uri, "/photos/", 8) == 0) {
        snprintf(path, path_size, "/sdcard%s", uri);
    } else { // static for webserver frontend
        snprintf(path, path_size, 
                "/sdcard/www%s", uri);
    }
    
    return ESP_OK;
}

esp_err_t storage_open_uri(
    const char *uri, FILE **fp
) {
    if(uri == NULL || fp == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    char path[128];

    esp_err_t err = uri_to_path(uri, path, sizeof(path));

    if(err != ESP_OK)
    {
        return err;
    }

    ESP_LOGI(TAG, "Open URI: %s", uri);
    ESP_LOGI(TAG, "Mapped path: %s", path);

    *fp = fopen(path, "rb");
    
    // debuf html
    char test[256] = {0};

    FILE *dbg = fopen("/sdcard/www/index.html", "rb");
    fread(test, 1, sizeof(test)-1, dbg);
    fclose(dbg);

    ESP_LOGI(TAG, "index.html:\n%s", test);
    // end debugging
    
    if(*fp == NULL){
        ESP_LOGE(TAG, "Cannot open file");
        return ESP_ERR_NOT_FOUND;
    }
    return ESP_OK;
}

void storage_close(FILE *fp)
{
    if (fp != NULL){
        fclose(fp);
    }
}