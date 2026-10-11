// SPDX-License-Identifier: Apache-2.0
#include "companion_board.h"
#include "board_display.h"
#include "companion_runtime.h"
#include "companion_ui.h"
#include "esp_gsp_esp_lcd.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "ui_bundle.h"
extern const unsigned char font_start[] asm("_binary_NotoSansSC_Regular_ttf_start");
extern const unsigned char font_end[] asm("_binary_NotoSansSC_Regular_ttf_end");
static companion_ui_handle_t companion;
void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    esp_gsp_config_t config;
    ESP_ERROR_CHECK(ui_bundle_open(&config));
    config.ttf=font_start; config.ttf_size=font_end-font_start;
    esp_display_present_target_config_t display;
    ESP_ERROR_CHECK(board_display_init(&display));
    esp_lcd_touch_handle_t touch;
    ESP_ERROR_CHECK(board_touch_init(&touch));
    esp_gsp_esp_lcd_config_t lcd=ESP_GSP_ESP_LCD_CONFIG_INIT();
    lcd.display=display; lcd.touch=touch;
    lcd.touch_input_mode=ESP_GSP_TOUCH_INPUT_POLLING;
    lcd.task_stack_psram=false; lcd.task_stack_size_freetype=64*1024;
    /* Use full-width scan lines. Height must remain unaligned so GSP can
     * partition each raster into the 32-line DMA buffers. */
    lcd.render_alignment=(esp_display_present_render_alignment_t){320,1,320,1};
    esp_gsp_handle_t ui;
    ESP_ERROR_CHECK(esp_gsp_esp_lcd_start(&config,&lcd,&ui));
    ESP_ERROR_CHECK(companion_runtime_start());
    ESP_ERROR_CHECK(companion_ui_create(ui,&companion));
    ESP_LOGI("cores3","USB Codex ready; hold the bottom touchscreen talk button");
}
