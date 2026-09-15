#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LCD_H_RES 320
#define LCD_V_RES 240

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