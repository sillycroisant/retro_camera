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
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define TAG "Display"

// Cấu hình chân GPIO đã đề xuất
#define LCD_PIN_SCLK     GPIO_NUM_14
#define LCD_PIN_MOSI     GPIO_NUM_21
#define LCD_PIN_CS       GPIO_NUM_45
#define LCD_PIN_DC       GPIO_NUM_48
#define LCD_PIN_RST      GPIO_NUM_47
#define LCD_PIN_BK_LIGHT GPIO_NUM_42

#define LCD_SPI_HOST     SPI2_HOST
#define LCD_CHUNK_LINES  40

static esp_lcd_panel_handle_t s_panel_handle = NULL;
static SemaphoreHandle_t s_display_mutex = NULL;

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
        .max_transfer_sz = LCD_H_RES * LCD_CHUNK_LINES * sizeof(uint16_t),
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

    // 4. Khởi tạo driver ST7789 (Tương thích ESP-IDF v6.0+)
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = LCD_PIN_RST,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle, &panel_config, &s_panel_handle));

    // 5. Cấu hình hiển thị màn hình
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(s_panel_handle, false));

    // Xoay màn hình ngang 320x240 (khớp chuẩn với ảnh QVGA của Camera)
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(s_panel_handle, true));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(s_panel_handle, false, true));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel_handle, true));

    // Xóa màn hình về màu đen và bật đèn nền
    display_clear(0x0000);
    gpio_set_level(LCD_PIN_BK_LIGHT, 1);

    // create mutex to protect display SPI
    if (s_display_mutex == NULL) {
        s_display_mutex = xSemaphoreCreateMutex();
    }

    ESP_LOGI(TAG, "ST7789 LCD Display initialized (320x240)");
    return ESP_OK;
}

esp_err_t display_clear(uint16_t color)
{
    if (s_panel_handle == NULL) return ESP_ERR_INVALID_STATE;
    static uint16_t s_line_buf[LCD_H_RES];
    for (int i = 0; i < LCD_H_RES; i++) s_line_buf[i] = color;
    for (int y = 0; y < LCD_V_RES; y++) esp_lcd_panel_draw_bitmap(s_panel_handle, 0, y, LCD_H_RES, y + 1, s_line_buf);
    return ESP_OK;
}


esp_err_t display_show_rgb565(const void *rgb565_buf, int x_start, int y_start, int width, int height)
{
    if (s_panel_handle == NULL || rgb565_buf == NULL) return ESP_ERR_INVALID_ARG;
    if(s_display_mutex) xSemaphoreTake(s_display_mutex, portMAX_DELAY);
    const uint8_t *src = (const uint8_t *)rgb565_buf;
    size_t line_bytes = width * sizeof(uint16_t);
    esp_err_t ret = ESP_OK;

    for (int y = 0; y < height; y += LCD_CHUNK_LINES) {
        int lines = (y + LCD_CHUNK_LINES <= height) ? LCD_CHUNK_LINES : (height - y);
        const uint8_t *chunk_ptr = src + (y * line_bytes);

        esp_err_t ret = esp_lcd_panel_draw_bitmap(
            s_panel_handle,
            x_start,
            y_start + y,
            x_start + width,
            y_start + y + lines,
            chunk_ptr
        );

        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Draw bitmap chunk failed at line %d: %s", y, esp_err_to_name(ret));
            break;
        }
    }

    if(s_display_mutex) xSemaphoreGive(s_display_mutex);
    return ret;
}


// Hàm đọc kích thước ảnh gốc từ file JPEG Header
static bool get_jpeg_resolution(const uint8_t *data, size_t len, uint16_t *width, uint16_t *height)
{
    if (len < 4 || data[0] != 0xFF || data[1] != 0xD8) return false;

    size_t i = 2;
    while (i < len - 8) {
        if (data[i] != 0xFF) {
            i++;
            continue;
        }
        uint8_t marker = data[i + 1];
        if (marker == 0xC0 || marker == 0xC1 || marker == 0xC2) { // SOF0, SOF1, SOF2
            *height = (data[i + 5] << 8) | data[i + 6];
            *width  = (data[i + 7] << 8) | data[i + 8];
            return true;
        }
        uint16_t length = (data[i + 2] << 8) | data[i + 3];
        i += 2 + length;
    }
    return false;
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
        ESP_LOGE(TAG, "Cannot allocate memory for JPG file (%u bytes)", (unsigned)file_size);
        return ESP_ERR_NO_MEM;
    }

    fread(jpg_buf, 1, file_size, fp);
    fclose(fp);

    // 1. Đọc kích thước gốc của ảnh JPEG
    uint16_t img_w = 0, img_h = 0;
    if (!get_jpeg_resolution(jpg_buf, file_size, &img_w, &img_h)) {
        free(jpg_buf);
        ESP_LOGE(TAG, "Invalid JPEG format: %s", file_path);
        return ESP_FAIL;
    }

    // 2. Tự động tính toán tỉ lệ Scale để vừa khít màn hình 320x240
    esp_jpeg_image_scale_t scale = JPEG_IMAGE_SCALE_0;
    uint16_t out_w = img_w;
    uint16_t out_h = img_h;

    if (img_w >= 1280 || img_h >= 960) {
        scale = JPEG_IMAGE_SCALE_1_4;
        out_w = img_w / 4;
        out_h = img_h / 4;
    } else if (img_w >= 640 || img_h >= 480) {
        scale = JPEG_IMAGE_SCALE_1_2;
        out_w = img_w / 2;
        out_h = img_h / 2;
    }

    ESP_LOGI(TAG, "Decoding JPEG: %s (%ux%u -> %ux%u)", file_path, img_w, img_h, out_w, out_h);

    // 3. Cấp phát đúng dung lượng buffer RGB565 cần thiết (Không bao giờ tràn)
    size_t rgb_size = (size_t)out_w * out_h * sizeof(uint16_t);
    uint8_t *rgb_buf = heap_caps_malloc(rgb_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (rgb_buf == NULL) {
        free(jpg_buf);
        ESP_LOGE(TAG, "Cannot allocate %u bytes for RGB buffer", (unsigned)rgb_size);
        return ESP_ERR_NO_MEM;
    }

    // 4. Giải nén JPEG sang RGB565 an toàn
    bool ok = jpg2rgb565(jpg_buf, file_size, rgb_buf, scale);
    free(jpg_buf);

    if (!ok) {
        free(rgb_buf);
        ESP_LOGE(TAG, "Failed to decode JPEG image");
        return ESP_FAIL;
    }

    // 5. Căn giữa và vẽ lên màn hình ST7789
    int x_start = (LCD_H_RES > out_w) ? (LCD_H_RES - out_w) / 2 : 0;
    int y_start = (LCD_V_RES > out_h) ? (LCD_V_RES - out_h) / 2 : 0;
    int draw_w = (out_w > LCD_H_RES) ? LCD_H_RES : out_w;
    int draw_h = (out_h > LCD_V_RES) ? LCD_V_RES : out_h;

    esp_err_t ret = display_show_rgb565(rgb_buf, x_start, y_start, draw_w, draw_h);
    
    // Giải phóng buffer sạch sẽ sau khi vẽ xong
    free(rgb_buf);
    ESP_LOGI(TAG, "Rendered image to LCD successfully: %s", file_path);
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