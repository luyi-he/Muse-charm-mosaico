// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "esp_err.h"
/* Iris service 0x7e11: method 1 saves TTS-only configuration, preserving
 * Muse token, then restarts; method 2 exposes model/voice only, never keys. */
esp_err_t companion_provision_start(void);
