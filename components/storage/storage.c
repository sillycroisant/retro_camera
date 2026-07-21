#include "storage.h"

#include <stdio.h>

#include "esp_log.h"
#include "esp_vfs_fat.h"

#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"

static const char *TAG = "SDCard";

static sdmmc_card_t *card = NULL;

esp_err_t sdcard_init(void){
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
    return ESP_OK;
}


esp_err_t sdcard_deinit(void)
{
    return esp_vfs_fat_sdcard_unmount("/sdcard", card);
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

