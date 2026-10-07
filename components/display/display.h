#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LCD_H_RES 320
#define LCD_V_RES 240
#define COLOR_BLACK 0x0000
#define COLOR_WHITE 0xFFFF

// OSD functions
void display_draw_pixel(uint16_t *buf, int buf_w, int buf_h, int x, int y, uint16_t color);

// void display_draw_char(uint16_t *buf, int buf_w, int buf_h, int x, int y, char c, uint16_t color, uint16_t bg, bool transparent);

void display_draw_string(uint16_t *buf, int buf_w, int buf_h, int x, int y, const char *str, uint16_t color, bool shadow);

void display_draw_osd_camera(uint16_t *buf, int buf_w, int buf_h, int img_w, int img_h, 
                                bool is_video_mode, bool is_recording, uint32_t record_sec, 
                                int fps, bool flash_on, uint32_t remaining_photos);

void display_draw_osd_gallery(uint16_t *buf, int buf_w, int buf_h, int img_w, int img_h,
                                uint32_t current_idx, uint32_t total_count);

/**
 * @brief Khởi tạo màn hình LCD ST7789 qua SPI DMA và bật đèn nền
 */
esp_err_t display_init(void);

/**
 * @brief Hiển thị mảng pixel RGB565 trực tiếp lên màn hình
 */
esp_err_t display_show_rgb565(const void *rgb565_buf, int x_start, int y_start, int width, int height);

/**
 * @brief Đọc và giải nén một file ảnh JPEG từ thẻ SD rồi vẽ lên LCD
 */
esp_err_t display_show_jpeg_file(const char *file_path);

/**
 * @brief Đọc và hiển thị bức ảnh chụp gần đây nhất từ thẻ SD
 */
esp_err_t display_show_latest_photo(void);

/**
 * @brief Xóa màn hình với một màu đơn sắc (RGB565)
 */
esp_err_t display_clear(uint16_t color);

#ifdef __cplusplus
}
#endif