// include libraries and inits
#include "storage.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

#include "esp_log.h"
#include "esp_vfs_fat.h"

#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"
#include "esp_heap_caps.h"

#define STORAGE_ROOT    "/sdcard"
#define PHOTO_DIRECTORY "/sdcard/photos"
#define VIDEO_DIRECTORY "/sdcard/videos"
#define INDEX_FILE      "/sdcard/index.dat"
#define STORAGE_INDEX_MAGIC NULL
#define STORAGE_INDEX_VERSION 1
#define STORAGE_MOUNT_POINT "/sdcard"
#define STORAGE_WWW_DIR "/www"

#define SDMMC_PIN_CLK GPIO_NUM_39
#define SDMMC_PIN_CMD GPIO_NUM_38
#define SDMMC_PIN_D0  GPIO_NUM_40

static const char *TAG = "[Storage]";

static sdmmc_card_t *card = NULL;

static uint32_t current_index = 1;
static uint32_t image_count = 0;

static char latest_path[128] = "";
static char latest_filename[32] = "";

static storage_index_t g_index;

typedef struct __attribute__((packed))
{
    uint32_t offset;
    uint32_t size;

} avi_index_entry_t;

struct storage_video
{
    FILE *file;

    uint32_t width;
    uint32_t height;
    uint32_t fps;

    uint32_t frame_count;

    long movi_list_offset;
    long movi_data_offset;
    long avih_frames_offset;
    long strh_frames_offset;

    avi_index_entry_t *index;
    size_t index_count;
    size_t index_capacity;

    char path[64];
};

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

    slot_config.width = 1;
    slot_config.clk = SDMMC_PIN_CLK;
    slot_config.cmd = SDMMC_PIN_CMD;
    slot_config.d0  = SDMMC_PIN_D0;

    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    esp_err_t ret = esp_vfs_fat_sdmmc_mount(
        STORAGE_ROOT,
        &host,
        &slot_config,
        &mount_config,
        &card
    );

    if(ret != ESP_OK){
        ESP_LOGE(TAG, "Failed to mount SD card (%s)", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "SD card initialized successfully!");

    sdmmc_card_print_info(stdout, card);

    ESP_LOGI(TAG, "esp_vfs_fat_sdmmc_mount() returned: %s (0x%x)", esp_err_to_name(ret), ret);
    
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

    ESP_LOGI(TAG, "Free heap: %u", (unsigned)esp_get_free_heap_size());

    ESP_LOGI(TAG, "Largest block: %u",
            (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));

    FILE *fp = fopen(latest_path, "wb");

    if (fp == NULL) {
        ESP_LOGE(TAG, "Cannot create image file");
        ESP_LOGE(TAG,"errno=%d (%s)", errno, strerror(errno));

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

    // ESP_LOGI(TAG, "Open URI: %s", uri);
    ESP_LOGI(TAG, "Mapped path: %s", path);

    *fp = fopen(path, "rb");
    
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

// helper for avi header
static bool storage_write_u32(FILE *file, uint32_t value)
{
    return fwrite(&value, sizeof(value), 1, file) == 1;
}

static bool storage_write_fourcc(FILE *file, const char *fourcc)
{
    return fwrite(fourcc, 1, 4, file) == 4;
}

static esp_err_t storage_video_write_header(storage_video_t *video)
{
    FILE *f = video->file;

    uint32_t microseconds_per_frame = 1000000UL / video->fps;
    
    // riff
    storage_write_fourcc(f, "RIFF");

    uint32_t riff_size_placeholder = 0;
    storage_write_u32(f, riff_size_placeholder);

    storage_write_fourcc(f, "AVI ");

    // list hdrl
    storage_write_fourcc(f, "LIST");
    uint32_t hdrl_size = 192;
    storage_write_u32(f, hdrl_size);
    storage_write_fourcc(f, "hdrl");

    // avih
    storage_write_fourcc(f, "avih");
    uint32_t avih_size = 56;
    storage_write_u32(f, avih_size);

    uint32_t avih[14] = 
    {
        microseconds_per_frame,     // dwMicroSecPerFrame
        0,                          // dwMaxBytesPerSec
        0,                          // dwPaddingGranularity
        0x10,                       // dwFlags = AVIF_HASINDEX
        0,                          // dwTotalFrames
        0,                          // dwInitialFrames
        1,                          // dwStreams
        0,                          // dwSuggestedBufferSize
        video->width,               // dwWidth
        video->height,              // dwHeight
        0, 0, 0, 0                  // reserved
    };

    if( fwrite(avih, sizeof(avih), 1, f) != 1) return ESP_FAIL;

    // list strl
    storage_write_fourcc(f, "LIST");
    uint32_t strl_size = 116;
    storage_write_u32(f, strl_size);
    storage_write_fourcc(f, "strl");

    // strh
    storage_write_fourcc(f,"strh");
    uint32_t strh_size = 56;
    storage_write_u32(f, strh_size);
    storage_write_fourcc(f, "vids");
    storage_write_fourcc(f,"MJPG");

    uint32_t strh[10]=
    {
        0,                          // dwFlags
        0,                          // wPriority + wLanguage
        0,                          // dwInitialFrames
        1,                          // dwScale
        video->fps,                 // dwRate
        0,                          // dwStart
        0,                          // dwLength
        0,                          // dwSuggestedBufferSize
        0xFFFFFFFF,                 // dwQuality
        0                           // dwSampleSize
    };

    if(fwrite(strh, sizeof(strh), 1, f) != 1) return ESP_FAIL;

    uint16_t frame_left = 0;
    uint16_t frame_top = 0;
    uint16_t frame_right = video->width;
    uint16_t frame_bottom = video->height;

    if(fwrite(&frame_left, sizeof(frame_left), 1, f) != 1) return ESP_FAIL;
    if(fwrite(&frame_top, sizeof(frame_top), 1, f) != 1) return ESP_FAIL;
    if(fwrite(&frame_right, sizeof(frame_right), 1, f) != 1) return ESP_FAIL;
    if(fwrite(&frame_bottom, sizeof(frame_bottom), 1, f) != 1) return ESP_FAIL;

    // strf - bitmap info header
    storage_write_fourcc(f, "strf");
    uint32_t strf_size = 40;
    storage_write_u32(f, strf_size);

    uint32_t bitmap_size = 40;
    int32_t bitmap_width = video->width;
    int32_t bitmap_height = video->height;

    uint16_t planes = 1;
    uint16_t bit_count = 24;

    uint32_t compression = 0x47504A4D; // "MJPG" little endian

    uint32_t image_size = 0;
    int32_t xppm = 0;
    int32_t yppm = 0;
    uint32_t colors_used = 0;
    uint32_t colors_important = 0;

    fwrite(&bitmap_size, sizeof(bitmap_size), 1, f);
    fwrite(&bitmap_width, sizeof(bitmap_width), 1, f);
    fwrite(&bitmap_height, sizeof(bitmap_height), 1, f);
    fwrite(&planes, sizeof(planes), 1, f);
    fwrite(&bit_count, sizeof(bit_count), 1, f);
    fwrite(&compression, sizeof(compression), 1, f);
    fwrite(&image_size, sizeof(image_size), 1, f);
    fwrite(&xppm, sizeof(xppm), 1, f);
    fwrite(&yppm, sizeof(yppm), 1, f);
    fwrite(&colors_used, sizeof(colors_used), 1, f);
    fwrite(&colors_important, sizeof(colors_important), 1, f);


    // LIST movi
    video->movi_list_offset = ftell(f);
    storage_write_fourcc(f, "LIST");
    uint32_t movi_size_placeholder = 0;
    storage_write_u32(f, movi_size_placeholder);
    storage_write_fourcc(f, "movi");
    video->movi_data_offset = ftell(f);

    return ESP_OK;
}

static esp_err_t storage_video_reserve_index(storage_video_t *video)
{
    if(video->index_count < video->index_capacity) return ESP_OK;

    size_t new_capacity = video->index_capacity == 0 ? 256 : video->index_capacity * 2;

    avi_index_entry_t *new_index = realloc(video->index, new_capacity * sizeof(avi_index_entry_t));

    if(new_index == NULL){
        ESP_LOGE(TAG, "Cannot allocate AVI index");
        return ESP_ERR_NO_MEM;
    }

    video->index = new_index;
    video->index_capacity = new_capacity;

    return ESP_OK;
}


storage_video_t *storage_video_create(
    uint32_t width,
    uint32_t height,
    uint32_t fps
) 
{    
    if(width == 0 || height == 0 || fps == 0) return NULL;

    struct stat st;
    // create video directory if needed
    if(stat(VIDEO_DIRECTORY, &st) != 0)
    {
        if(mkdir(VIDEO_DIRECTORY, 0775) != 0){
            ESP_LOGE(TAG, "Cannot create %s", VIDEO_DIRECTORY);
            return NULL;
        }
    }

    storage_video_t *video = calloc(1, sizeof(storage_video_t));

    if(video == NULL) {
        ESP_LOGI(TAG, "Cannot allocate video context");
        return NULL;
    }
    video->width = width;
    video->height = height;
    video->fps = fps;

    // find unused filename
    bool found = false;
    for(uint32_t i = 0; i < 10000; i++){
        snprintf(
            video->path,
            sizeof(video->path),
            VIDEO_DIRECTORY "/video_%06lu.avi",
            (unsigned long)i
        );

        if (stat(video->path, &st) != 0)
        {
            if (errno == ENOENT)
            {
                found = true;
                break;
            }
        }
    }

    if(!found){
        ESP_LOGE(TAG, "Cannot find unused video filename");
        free(video);
        return NULL;
    }

    video->file = fopen(video->path, "wb");

    if(video->file == NULL){
        ESP_LOGE(TAG, "Cannot create %s, errno=%d", video->path, errno);
        free(video);
        return NULL;
    }

    esp_err_t ret = storage_video_write_header(video);
    if(ret != ESP_OK){
        fclose(video->file);
        remove(video->path);
        free(video);
        return NULL;
    }

    ESP_LOGI(TAG, "Video created: %s (%lux%lu @ %lu FPS)",
        video->path,
        (unsigned long)video->width,
        (unsigned long)video->height,
        (unsigned long)video->fps);

    return video;
}

esp_err_t storage_video_write_frame(
    storage_video_t *video,
    const uint8_t *data,
    size_t len)
{
    if (video == NULL || video->file == NULL) return ESP_ERR_INVALID_STATE;

    if (data == NULL || len == 0) return ESP_ERR_INVALID_ARG;

    if (len > UINT32_MAX) return ESP_ERR_INVALID_SIZE;

    esp_err_t ret = storage_video_reserve_index(video);

    if (ret != ESP_OK) return ret;

    FILE *f = video->file;

    long frame_position = ftell(f);

    if (frame_position < 0) return ESP_FAIL;

    uint32_t offset = (uint32_t)(frame_position - video->movi_data_offset);

    // MJPEG video frame
    if (!storage_write_fourcc(f, "00dc")) return ESP_FAIL;

    uint32_t frame_size = (uint32_t)len;

    if (!storage_write_u32(f, frame_size)) return ESP_FAIL;

    if (fwrite(data, 1, len, f) != len) return ESP_FAIL;

    /*
     * AVI chunks must be WORD aligned.
     */

    if (len & 1)
    {
        uint8_t padding = 0;

        if (fwrite(&padding, 1, 1, f) != 1) return ESP_FAIL;
    }

    video->index[video->index_count].offset = offset;
    video->index[video->index_count].size = frame_size;

    video->index_count++;
    video->frame_count++;

    return ESP_OK;
}

esp_err_t storage_video_close(storage_video_t *video)
{
    if (video == NULL)
        return ESP_ERR_INVALID_ARG;

    if (video->file == NULL)
    {
        free(video->index);
        free(video);
        return ESP_OK;
    }

    FILE *f = video->file;

    // End of movi
    long movi_end = ftell(f);

    if (movi_end < 0)
    {
        fclose(f);
        free(video->index);
        free(video);
        return ESP_FAIL;
    }

    // Write idx1

    storage_write_fourcc(f, "idx1");

    uint32_t index_size =
        video->frame_count * 16;

    storage_write_u32(f, index_size);

    for (uint32_t i = 0; i < video->frame_count; i++)
    {
        storage_write_fourcc(f, "00dc");

        uint32_t flags = 0x10;

        storage_write_u32(f, flags);

        storage_write_u32(f, video->index[i].offset);

        storage_write_u32(f, video->index[i].size);
    }

    // Final file size
    long file_end = ftell(f);

    if (file_end < 0)
    {
        fclose(f);
        free(video->index);
        free(video);
        return ESP_FAIL;
    }

    // RIFF size = file size - 8
    uint32_t riff_size = (uint32_t)(file_end - 8);

    // movi LIST size
    // Includes "movi" + all chunks.
    uint32_t movi_size = 
        (uint32_t)(movi_end - (video->movi_list_offset + 8));


    // Patch RIFF size
    fseek(f, 4, SEEK_SET);
    fwrite(&riff_size, sizeof(riff_size), 1, f);


    // Patch total frame count in avih
    // Offset 48
    fseek(f, 48, SEEK_SET);

    fwrite(&video->frame_count, sizeof(video->frame_count), 1, f);


    // Patch stream frame count
    // Offset 140
    fseek(f, 140, SEEK_SET);
    fwrite(&video->frame_count, sizeof(video->frame_count), 1, f);


    // Patch movi size
    fseek(f, video->movi_list_offset + 4, SEEK_SET);

    fwrite(&movi_size, sizeof(movi_size), 1, f);

    fflush(f);
    fclose(f);

    ESP_LOGI(TAG, "Video saved: %s (%lu frames)",
        video->path, (unsigned long)video->frame_count);

    free(video->index);
    free(video);

    return ESP_OK;
}

esp_err_t storage_video_abort(storage_video_t *video)
{
    if (video == NULL)
        return ESP_ERR_INVALID_ARG;

    if (video->file != NULL)
    {
        fclose(video->file);
        video->file = NULL;
    }

    if (video->path[0] != '\0')
    {
        remove(video->path);
    }

    free(video->index);
    free(video);

    ESP_LOGW(TAG, "Video recording aborted");

    return ESP_OK;
}