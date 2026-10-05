// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stddef.h>
#include <stdint.h>
/* In-place PCM16 LE conversion. 24kHz groups of three become two 16kHz
 * samples; unconsumed bytes are retained by the streaming owner. */
size_t companion_pcm_convert(uint8_t *pcm,size_t bytes,unsigned rate,size_t *consumed);
