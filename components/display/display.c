#include "display.h"

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_heap_caps.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"

#include "img_converters.h"
#include "storage.h"

#define TAG "Display"

// Cấu hình chân GPIO đã đề xuất
#define LCD_PIN_SCLK     GPIO_NUM_14
#define LCD_PIN_MOSI     GPIO_NUM_21
#define LCD_PIN_CS       GPIO_NUM_45
#define LCD_PIN_DC       GPIO_NUM_48
#define LCD_PIN_RST      GPIO_NUM_47
#define LCD_PIN_BK_LIGHT GPIO_NUM_42

#define LCD_SPI_HOST     SPI2_HOST

static esp_lcd_panel_handle_t s_panel_handle = NULL;

esp_err_t display_init(void)
{
    if (s_panel_handle != NULL) return ESP_OK;

    ESP_LOGI(TAG, "Initializing ST7789 LCD Display...");

    // 1. Cấu hình chân đèn nền (Backlight)
    gpio_config_t bk_gpio_config = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << LCD_PIN_BK_LIGHT
    };
    gpio_config(&bk_gpio_config);
    gpio_set_level(LCD_PIN_BK_LIGHT, 0); // Tắt đèn nền tạm thời lúc khởi tạo

    // 2. Khởi tạo SPI Bus
    spi_bus_config_t buscfg = {
        .sclk_io_num = LCD_PIN_SCLK,
        .mosi_io_num = LCD_PIN_MOSI,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_H_RES * LCD_V_RES * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));

    // 3. Cấu hình Panel IO SPI
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = LCD_PIN_DC,
        .cs_gpio_num = LCD_PIN_CS,
        .pclk_hz = 40 * 1000 * 1000, // Tần số SPI 40MHz
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_SPI_HOST, &io_config, &io_handle));

    // 4. Khởi tạo driver ST7789
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = LCD_PIN_RST,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle, &panel_config, &s_panel_handle));

    // 5. Cấu hình hiển thị màn hình
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(s_panel_handle, true));

    // Xoay màn hình ngang 320x240 (khớp chuẩn với ảnh QVGA của Camera)
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(s_panel_handle, true));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(s_panel_handle, false, true));
    
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel_handle, true));

    // Xóa màn hình về màu đen và bật đèn nền
    display_clear(0x0000);
    gpio_set_level(LCD_PIN_BK_LIGHT, 1);

    ESP_LOGI(TAG, "ST7789 LCD Display initialized (320x240)");
    return ESP_OK;
}

esp_err_t display_clear(uint16_t color)
{
    if (s_panel_handle == NULL) return ESP_ERR_INVALID_STATE;

    size_t line_pixels = LCD_H_RES;
    uint16_t *line_buf = heap_caps_malloc(line_pixels * sizeof(uint16_t), MALLOC_CAP_DMA);
    if (!line_buf) return ESP_ERR_NO_MEM;

    for (int i = 0; i < line_pixels; i++) {
        line_buf[i] = color;
    }

    for (int y = 0; y < LCD_V_RES; y++) {
        esp_lcd_panel_draw_bitmap(s_panel_handle, 0, y, LCD_H_RES, y + 1, line_buf);
    }

    free(line_buf);
    return ESP_OK;
}

esp_err_t display_show_rgb565(const void *rgb565_buf, int x_start, int y_start, int width, int height)
{
    if (s_panel_handle == NULL || rgb565_buf == NULL) return ESP_ERR_INVALID_ARG;
    return esp_lcd_panel_draw_bitmap(s_panel_handle, x_start, y_start, x_start + width, y_start + height, rgb565_buf);
}

esp_err_t display_show_jpeg_file(const char *file_path)
{
    if (s_panel_handle == NULL || file_path == NULL) return ESP_ERR_INVALID_ARG;

    FILE *fp = fopen(file_path, "rb");
    if (fp == NULL) {
        ESP_LOGE(TAG, "Cannot open image: %s", file_path);
        return ESP_ERR_NOT_FOUND;
    }

    fseek(fp, 0, SEEK_END);
    size_t file_size = ftell(fp);
    rewind(fp);

    uint8_t *jpg_buf = heap_caps_malloc(file_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (jpg_buf == NULL) {
        fclose(fp);
        ESP_LOGE(TAG, "Cannot allocate memory for JPG");
        return ESP_ERR_NO_MEM;
    }

    fread(jpg_buf, 1, file_size, fp);
    fclose(fp);

    // Cấp phát buffer RGB565 trong PSRAM để giải nén (320x240x2 = 153,600 bytes)
    size_t rgb_size = LCD_H_RES * LCD_V_RES * sizeof(uint16_t);
    uint8_t *rgb_buf = heap_caps_malloc(rgb_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (rgb_buf == NULL) {
        free(jpg_buf);
        return ESP_ERR_NO_MEM;
    }

    // Giải nén JPEG sang RGB565
    bool ok = jpg2rgb565(jpg_buf, file_size, rgb_buf, JPG_SCALE_NONE);
    free(jpg_buf);

    if (!ok) {
        free(rgb_buf);
        ESP_LOGE(TAG, "Failed to decode JPEG image");
        return ESP_FAIL;
    }

    // Vẽ toàn bộ frame lên LCD ST7789
    esp_err_t ret = esp_lcd_panel_draw_bitmap(s_panel_handle, 0, 0, LCD_H_RES, LCD_V_RES, rgb_buf);
    free(rgb_buf);

    ESP_LOGI(TAG, "Rendered image to LCD: %s", file_path);
    return ret;
}

esp_err_t display_show_latest_photo(void)
{
    const char *path = storage_latest_path();
    if (path == NULL || strlen(path) == 0) {
        ESP_LOGW(TAG, "No photo in storage to display");
        return ESP_ERR_NOT_FOUND;
    }

    return display_show_jpeg_file(path);
}