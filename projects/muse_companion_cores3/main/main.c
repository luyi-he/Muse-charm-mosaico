// SPDX-License-Identifier: Apache-2.0
#include "companion_board.h"
#include "board_display.h"
#include "companion_credentials.h"
#include "companion_setup.h"
#include "companion_provision.h"
#include "companion_runtime.h"
#include "companion_ui.h"
#include "esp_gsp_esp_lcd.h"
#include "esp_iris.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ui_bundle.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>

extern const unsigned char font_start[] asm("_binary_NotoSansSC_Regular_ttf_start");
extern const unsigned char font_end[] asm("_binary_NotoSansSC_Regular_ttf_end");
static companion_ui_handle_t companion;

/* First-time setup has no cloud tasks. Exercise both codecs before asking
 * for credentials, then close the streams before a possible NVS write. */
static void audio_probe(void)
{
    esp_codec_dev_handle_t mic = bsp_audio_codec_microphone_init();
    esp_codec_dev_handle_t spk = bsp_audio_codec_speaker_init();
    if (!mic || !spk) {
        ESP_LOGE("cores3", "Audio probe: codec initialization failed");
        goto done;
    }
    esp_codec_dev_sample_info_t format = {.sample_rate = 16000, .bits_per_sample = 16, .channel = 1};
    if (esp_codec_dev_open(mic, &format) != ESP_CODEC_DEV_OK ||
        esp_codec_dev_open(spk, &format) != ESP_CODEC_DEV_OK) {
        ESP_LOGE("cores3", "Audio probe: stream initialization failed");
        goto done;
    }
    esp_codec_dev_set_in_gain(mic, 30.0f);
    esp_codec_dev_set_out_vol(spk, 60);
    int16_t samples[1024];
    int ret = esp_codec_dev_read(mic, samples, sizeof(samples));
    int peak = 0;
    if (ret == ESP_CODEC_DEV_OK) {
        for (unsigned i = 0; i < 1024; ++i) {
            int value = samples[i] < 0 ? -(int)samples[i] : samples[i];
            if (value > peak) peak = value;
        }
    }
    ESP_LOGI("cores3", "Audio probe: microphone read=%d peak=%d at 16000 Hz", ret, peak);
    for (unsigned block = 0; block < 5; ++block) {
        for (unsigned i = 0; i < 1024; ++i) {
            unsigned phase = block * 1024 + i;
            float gain = fminf(1.0f, (float)(phase < 2560 ? phase : 5119-phase) / 128.0f);
            samples[i] = (int16_t)(6000.0f * gain * sinf(2.0f * 3.14159265f * 880.0f * phase / 16000.0f));
        }
        ret = esp_codec_dev_write(spk, samples, sizeof(samples));
        if (ret != ESP_CODEC_DEV_OK) break;
    }
    /* Writes queue DMA data; allow the final samples to play before closing. */
    vTaskDelay(pdMS_TO_TICKS(150));
    ESP_LOGI("cores3", "Audio probe: speaker write=%d (short setup tone)", ret);
done:
    if (mic) { esp_codec_dev_close(mic); esp_codec_dev_delete(mic); }
    if (spk) { esp_codec_dev_close(spk); esp_codec_dev_delete(spk); }
}

void app_main(void)
{
    /* Do not erase NVS automatically when its layout differs. */
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(companion_credentials_init());

    esp_gsp_config_t config;
    ESP_ERROR_CHECK(ui_bundle_open(&config));
    config.ttf = font_start;
    config.ttf_size = font_end - font_start;
    esp_display_present_target_config_t display;
    ESP_ERROR_CHECK(board_display_init(&display));
    esp_lcd_touch_handle_t touch;
    ESP_ERROR_CHECK(board_touch_init(&touch));
    esp_gsp_esp_lcd_config_t lcd = ESP_GSP_ESP_LCD_CONFIG_INIT();
    lcd.display = display;
    lcd.touch = touch;
    lcd.task_stack_psram = false;
    lcd.task_stack_size_freetype = 64 * 1024;
    esp_gsp_handle_t ui;
    /* Reserve the large internal render stack before Wi-Fi fragments SRAM. */
    ESP_LOGI("cores3", "Before UI: internal free=%u largest=%u PSRAM free=%u",
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    ESP_ERROR_CHECK(esp_gsp_esp_lcd_start(&config, &lcd, &ui));
    ESP_ERROR_CHECK(companion_setup_start());
    /* Iris transports maintenance over TCP. Keep application diagnostics on
     * the CoreS3 USB console; Iris still owns and closes its private streams. */
    FILE *console_out = stdout;
    FILE *console_err = stderr;
    ESP_ERROR_CHECK(esp_iris_start());
    stdout = console_out;
    stderr = console_err;
    ESP_ERROR_CHECK(companion_provision_start());
    if (!companion_setup_active(NULL, NULL)) ESP_ERROR_CHECK(companion_runtime_start());
    ESP_ERROR_CHECK(companion_ui_create(ui, &companion));
    if (companion_setup_active(NULL, NULL)) audio_probe();
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_iris_mark_healthy());
    ESP_LOGI("cores3", "Muse companion ready at 320x240; hold the touchscreen talk button");
}
