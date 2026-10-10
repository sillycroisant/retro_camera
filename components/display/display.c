#include "display.h"
#include "font8x8.h"
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

// Cấu hình chân GPIO
// GND
// VCC
#define LCD_PIN_SCLK     GPIO_NUM_14
#define LCD_PIN_MOSI     GPIO_NUM_21
#define LCD_PIN_CS       GPIO_NUM_45
#define LCD_PIN_DC       GPIO_NUM_48
#define LCD_PIN_RST      GPIO_NUM_47
#define LCD_PIN_BK_LIGHT -1            // chân BK light mặc định 3v3

#define LCD_SPI_HOST     SPI2_HOST
#define LCD_CHUNK_LINES  120

static esp_lcd_panel_handle_t s_panel_handle = NULL;
static SemaphoreHandle_t s_display_mutex = NULL;

esp_err_t display_init(void)
{
    if (s_panel_handle != NULL) return ESP_OK;

    ESP_LOGI(TAG, "Initializing ST7789 LCD Display...");

    // 1. Khởi tạo SPI Bus
    spi_bus_config_t buscfg = {
        .sclk_io_num = LCD_PIN_SCLK,
        .mosi_io_num = LCD_PIN_MOSI,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_H_RES * LCD_V_RES * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));

    // 2. Cấu hình Panel IO SPI
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = LCD_PIN_DC,
        .cs_gpio_num = LCD_PIN_CS,
        .pclk_hz = 60 * 1000 * 1000, // Tần số SPI 60MHz
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_SPI_HOST, &io_config, &io_handle));

    // 3. Khởi tạo driver ST7789 (Tương thích ESP-IDF v6.0+)
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = LCD_PIN_RST,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle, &panel_config, &s_panel_handle));

    // 4. Cấu hình hiển thị màn hình
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(s_panel_handle, false));

    // Xoay màn hình ngang 320x240 (khớp chuẩn với ảnh QVGA của Camera)
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(s_panel_handle, true));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(s_panel_handle, false, true));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel_handle, true));

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

void display_draw_pixel(uint16_t *buf, int buf_w, int buf_h, int x, int y, uint16_t color){
    if(x >= 0 && x < buf_w && y >= 0 && y < buf_h){
        buf[y * buf_w + x] = color;
    }
}

void display_draw_char(uint16_t *buf, int buf_w, int buf_h, int x, int y, char c, uint16_t color)
{
    if (c < 32 || c > 126) c = '?';
    const uint8_t *glyph = font8x8_basic[(int)c - 32];

    for (int row = 0; row < 8; row++) {
        uint8_t line = glyph[row];
        for (int col = 0; col < 8; col++) {
            int px = x + col;
            int py = y + row;
            if (px >= 0 && px < buf_w && py >= 0 && py < buf_h) {
                if (line & (1 << col)) {
                    buf[py * buf_w + px] = color;
                }
            }
        }
    }
}

void display_draw_string(uint16_t *buf, int buf_w, int buf_h, int x, int y, const char *str, uint16_t color, bool shadow)
{
    if (str == NULL) return;

    if(shadow){
        // 1. draw text shadow
        int cx = x +1, cy = y +1;
        const char *p = str;
        while(*p){
            display_draw_char(buf, buf_w, buf_h, cx, cy, *p, COLOR_BLACK);
            cx += 8;
            p++;
        }
    }

    // 2. draw white text
    int cx = x;
    while(*str){
        display_draw_char(buf, buf_w, buf_h, cx, y, *str, color);
        cx += 8;
        str++;
    }
}

void display_draw_osd_camera(uint16_t *buf, int buf_w, int buf_h, 
                            int img_w, int img_h, 
                            bool is_video_mode, bool is_recording, uint32_t record_sec, 
                            int fps, bool flash_on, uint32_t remaining_photos,
                            int battery_pct, bool sd_ok)
{
    if(buf == NULL) return;

    // 1. Upper header: capture mode
    if (is_video_mode) {
        if (is_recording) {
            char rec_str[24];
            snprintf(rec_str, sizeof(rec_str), "REC %02lu:%02lu", (unsigned long)(record_sec / 60), (unsigned long)(record_sec % 60));
            display_draw_string(buf, buf_w, buf_h, 8, 8, rec_str, COLOR_WHITE, true);
        } else {
            display_draw_string(buf, buf_w, buf_h, 8, 8, "VIDEO", COLOR_WHITE, true);
        }
    } else {
        display_draw_string(buf, buf_w, buf_h, 8, 8, "PHOTO", COLOR_WHITE, true);
    }

    // 2. Middle header: remaining photos
    char rem_str[24];
    if (remaining_photos > 9999){
        snprintf(rem_str, sizeof(rem_str), "REM:9999+");    
    } else {
        snprintf(rem_str, sizeof(rem_str), "REM:%lu", (unsigned long)remaining_photos);
    }
    int rem_x = (buf_w - (int)strlen(rem_str) * 8) / 2;
    display_draw_string(buf, buf_w, buf_h, rem_x, 8, rem_str, COLOR_WHITE, true);

    // 3. Right header: pin + battery percentage
    char status_str[32];
    snprintf(status_str, sizeof(status_str), "%s BAT:%d%%", sd_ok ? "[SD]" : "[NO SD]", battery_pct);
    int status_x = buf_w - ((int)strlen(status_str)*8) - 8;
    display_draw_string(buf, buf_w, buf_h, status_x, 8, status_str, COLOR_WHITE, true);


    // Left Footer: image resolution + fps
    char res_str[32];
    if(is_video_mode){
        snprintf(res_str, sizeof(res_str), "%dx%d %dFPS", img_w, img_h, fps);
    } else {
        snprintf(res_str, sizeof(res_str), "%dx%d%s", img_w, img_h, flash_on?" [FL]": "");
    }
    display_draw_string(buf, buf_w, buf_h, 8, buf_h - 16, res_str, COLOR_WHITE, true);
}

void display_draw_osd_gallery(uint16_t *buf, int buf_w,
                                int img_w, int img_h,
                                int buf_h, uint32_t current_idx, uint32_t total_count)
{
    if (buf == NULL) return;

    // Left header
    display_draw_string(buf, buf_w, buf_h, 8, 8, "GALLERY", COLOR_WHITE, true);

    // Right header
    char idx_str[24];
    snprintf(idx_str, sizeof(idx_str), "%lu/%lu", (unsigned long)current_idx, (unsigned long)total_count);
    int idx_x = buf_w - (int)strlen(idx_str) * 8 - 8;
    display_draw_string(buf, buf_w, buf_h, idx_x, 8, idx_str, COLOR_WHITE, true);

    // Footer: Hướng dẫn nút
    char res_str[24];
    snprintf(res_str, sizeof(res_str), "%dx%d", img_h, img_w);
    display_draw_string(buf, buf_w, buf_h, 8, buf_h - 30, res_str, COLOR_WHITE, true);
    display_draw_string(buf, buf_w, buf_h, 8, buf_h - 16, "B1:DEL  B2:PREV  B3:NEXT  B4:CAM", COLOR_WHITE, true);
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

static void resize_rgb565_image(const uint16_t *src, int src_w, int src_h,
                               uint16_t *dst, int dst_w, int dst_h)
{
    for (int y = 0; y < dst_h; y++) {
        int src_y = (y * src_h) / dst_h;
        const uint16_t *src_row = &src[src_y * src_w];
        uint16_t *dst_row = &dst[y * dst_w];
        for (int x = 0; x < dst_w; x++) {
            int src_x = (x * src_w) / dst_w;
            dst_row[x] = src_row[src_x];
        }
    }
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
        return ESP_ERR_NO_MEM;
    }
    fread(jpg_buf, 1, file_size, fp);
    fclose(fp);

    // 1. Đọc kích thước gốc từ JPEG Header
    uint16_t img_w = 0, img_h = 0;
    if (!get_jpeg_resolution(jpg_buf, file_size, &img_w, &img_h)) {
        free(jpg_buf);
        ESP_LOGE(TAG, "Invalid JPEG format: %s", file_path);
        return ESP_FAIL;
    }
    
    // 2. Chọn Scale phần cứng phù hợp nhất để tiết kiệm RAM
    esp_jpeg_image_scale_t scale = JPEG_IMAGE_SCALE_0;
    uint16_t dec_w = img_w;
    uint16_t dec_h = img_h;
    if (img_w >= 1280 || img_h >= 960) {
        scale = JPEG_IMAGE_SCALE_1_4;
        dec_w = img_w / 4;
        dec_h = img_h / 4;
    } else if (img_w >= 640 || img_h >= 480) {
        scale = JPEG_IMAGE_SCALE_1_2;
        dec_w = img_w / 2;
        dec_h = img_h / 2;
    }
    // 3. Cấp phát buffer giải nén JPEG
    size_t dec_size = (size_t)dec_w * dec_h * sizeof(uint16_t);
    uint16_t *dec_buf = heap_caps_malloc(dec_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (dec_buf == NULL) {
        free(jpg_buf);
        return ESP_ERR_NO_MEM;
    }
    bool ok = jpg2rgb565(jpg_buf, file_size, (uint8_t *)dec_buf, scale);
    free(jpg_buf);
    if (!ok) {
        free(dec_buf);
        ESP_LOGE(TAG, "Failed to decode JPEG image");
        return ESP_FAIL;
    }
    // 4. Cấp phát buffer toàn màn hình chuẩn 320x240
    size_t full_size = LCD_H_RES * LCD_V_RES * sizeof(uint16_t);
    uint16_t *full_buf = heap_caps_malloc(full_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (full_buf == NULL) {
        free(dec_buf);
        return ESP_ERR_NO_MEM;
    }
    // Xóa nền đen toàn khung
    memset(full_buf, 0, full_size);
    // 5. Nếu ảnh khớp 320x240 thì copy trực tiếp, nếu khác thì Resize vừa khít 320x240
    if (dec_w == LCD_H_RES && dec_h == LCD_V_RES) {
        memcpy(full_buf, dec_buf, full_size);
    } else {
        resize_rgb565_image(dec_buf, dec_w, dec_h, full_buf, LCD_H_RES, LCD_V_RES);
    }
    free(dec_buf);
    // 6. Lấy index ảnh và vẽ OSD Gallery lên buffer 320x240
    uint32_t current_idx = 0;
    const char *pname = strrchr(file_path, '/');
    if (pname != NULL) {
        sscanf(pname + 1, "photo_%lu.jpg", &current_idx);
    }
    display_draw_osd_gallery(full_buf, LCD_H_RES, LCD_V_RES, img_w, img_h, current_idx, storage_image_count());
    // 7. Xuất ra LCD ST7789 với kích thước chuẩn 320x240
    esp_err_t ret = display_show_rgb565(full_buf, 0, 0, LCD_H_RES, LCD_V_RES);
    free(full_buf);
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

void display_show_splash_screen(const char *title, const char *subtitle, const char *author){
    if(s_panel_handle == NULL) return;

    size_t buf_size = LCD_H_RES * LCD_V_RES * sizeof(uint16_t);
    uint16_t *splash_buf = heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (splash_buf == NULL) return;
    // Nền đen
    memset(splash_buf, 0, buf_size);
    // Vẽ khung viền Retro ngoài cùng
    for (int x = 10; x < LCD_H_RES - 10; x++) {
        splash_buf[10 * LCD_H_RES + x] = COLOR_WHITE;
        splash_buf[(LCD_V_RES - 10) * LCD_H_RES + x] = COLOR_WHITE;
    }
    for (int y = 10; y < LCD_V_RES - 10; y++) {
        splash_buf[y * LCD_H_RES + 10] = COLOR_WHITE;
        splash_buf[y * LCD_H_RES + (LCD_H_RES - 10)] = COLOR_WHITE;
    }
    // Tiêu đề lớn ở giữa
    int title_x = (LCD_H_RES - (int)strlen(title) * 8) / 2;
    display_draw_string(splash_buf, LCD_H_RES, LCD_V_RES, title_x, 80, title, COLOR_WHITE, false);
    // Dòng phụ đề
    int sub_x = (LCD_H_RES - (int)strlen(subtitle) * 8) / 2;
    display_draw_string(splash_buf, LCD_H_RES, LCD_V_RES, sub_x, 110, subtitle, COLOR_WHITE, false);
    // Dấu ấn tác giả
    if (author != NULL) {
        int auth_x = (LCD_H_RES - (int)strlen(author) * 8) / 2;
        display_draw_string(splash_buf, LCD_H_RES, LCD_V_RES, auth_x, 150, author, COLOR_WHITE, false);
    }
    // Trạng thái tải
    int load_x = (LCD_H_RES - 18 * 8) / 2;
    display_draw_string(splash_buf, LCD_H_RES, LCD_V_RES, load_x, 190, "[ INITIALIZING... ]", COLOR_WHITE, false);
    display_show_rgb565(splash_buf, 0, 0, LCD_H_RES, LCD_V_RES);
    free(splash_buf);
}