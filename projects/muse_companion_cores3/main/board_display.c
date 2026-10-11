// SPDX-License-Identifier: Apache-2.0
#include "companion_board.h"
#include "board_display.h"
#include "esp_check.h"
#include "esp_lcd_panel_ops.h"

esp_err_t board_display_init(esp_display_present_target_config_t *target)
{
    ESP_RETURN_ON_FALSE(target, ESP_ERR_INVALID_ARG, "cores3", "null display target");
    bsp_display_config_t config = {.max_transfer_sz = 320 * 240 * 2};
    esp_lcd_panel_handle_t panel = NULL;
    esp_lcd_panel_io_handle_t io = NULL;
    ESP_RETURN_ON_ERROR(bsp_display_new(&config, &panel, &io), "cores3", "LCD init");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(panel, true), "cores3", "LCD power");
    ESP_RETURN_ON_ERROR(bsp_display_brightness_init(), "cores3", "LCD backlight power");
    ESP_RETURN_ON_ERROR(bsp_display_backlight_on(), "cores3", "LCD backlight");
    *target = (esp_display_present_target_config_t){
        .hw = {.panel = panel, .io = io, .panel_type = ESP_DISPLAY_PRESENT_PANEL_IO,
            .input_pixel_format = ESP_DISPLAY_PRESENT_PIXEL_FORMAT_RGB565,
            .rotation = ESP_DISPLAY_PRESENT_ROTATE_0, .swap_bytes = true},
        .fb = {.mode = ESP_DISPLAY_PRESENT_MODE_AUTO},
        /* ESP32-S3 SPI uses internal DMA staging, unlike the S31 path. */
        .drawbuf = {.lines = 12, .in_psram = false, .buffers = 1},
    };
    return ESP_OK;
}

esp_err_t board_touch_init(esp_lcd_touch_handle_t *touch)
{
    ESP_RETURN_ON_FALSE(touch, ESP_ERR_INVALID_ARG, "cores3", "null touch target");
    const bsp_touch_config_t config = {0};
    /* Touch is the only talk/pairing control on CoreS3: fail visibly if absent. */
    return bsp_touch_new(&config, touch);
}
