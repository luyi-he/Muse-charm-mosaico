// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_codec_dev.h"
typedef struct companion_tts_t *companion_tts_handle_t;
typedef void (*companion_tts_done_cb)(void *ctx,uint32_t generation,bool ok);
esp_err_t companion_tts_create(esp_codec_dev_handle_t speaker,
    companion_tts_done_cb done,void *ctx,companion_tts_handle_t *out);
void companion_tts_cancel(companion_tts_handle_t tts,uint32_t generation,bool muted);
esp_err_t companion_tts_submit(companion_tts_handle_t tts,uint32_t generation,
    const char *text,unsigned volume,unsigned speech_rate);
bool companion_tts_idle(companion_tts_handle_t tts);
/* Render-task safe: only the playback worker applies the codec volume. */
void companion_tts_set_volume(companion_tts_handle_t tts,unsigned volume);
