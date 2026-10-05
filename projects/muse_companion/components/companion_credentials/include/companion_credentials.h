// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdbool.h>
#include "esp_err.h"
enum { COMPANION_MUSE_TOKEN_CAP=64, COMPANION_TTS_TOKEN_CAP=512,
       COMPANION_TTS_URL_CAP=256, COMPANION_TTS_NAME_CAP=128 };
typedef struct {
    const char *muse_token, *tts_token, *tts_url, *tts_model, *tts_voice;
    unsigned tts_sample_rate;
} companion_credentials_config_t;
/* App-lifetime immutable snapshot after init. Only the first-boot HTTP task
 * saves a replacement; it restarts the device before any service uses it. */
esp_err_t companion_credentials_init(void);
bool companion_credentials_ready(void);
bool companion_credentials_valid(const companion_credentials_config_t *config);
esp_err_t companion_credentials_save(const companion_credentials_config_t *config);
const char *companion_credentials_muse(void);
const char *companion_credentials_tts(void);
const char *companion_credentials_tts_url(void);
const char *companion_credentials_tts_model(void);
const char *companion_credentials_tts_voice(void);
unsigned companion_credentials_tts_sample_rate(void);
