// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdbool.h>
#include "esp_codec_dev.h"
typedef struct {
    const char *url, *token, *model, *voice, *text;
    unsigned speech_rate;
    esp_codec_dev_handle_t speaker;
    bool (*current)(void *ctx);
    unsigned (*volume)(void *ctx);
    void *ctx;
} companion_qwen_config_t;
/* Called by the TTS worker. WebSocket callback produces PCM; only this
 * calling worker drains it and accesses the speaker. Stops/joins producer
 * before freeing its buffers, including cancellation/error paths. */
bool companion_qwen_speak(const companion_qwen_config_t *config);
