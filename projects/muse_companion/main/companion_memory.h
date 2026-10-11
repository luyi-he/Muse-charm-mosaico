// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "esp_heap_caps.h"
/* The complete CJK font stays in flash on an 8 MB CoreS3. Unlike S31,
 * CoreS3 cannot copy the full application into PSRAM to keep external task
 * stacks accessible during NVS writes, so these stacks remain internal. */
#ifdef COMPANION_CORES3
#define COMPANION_TASK_STACK_CAPS (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)
#else
#define COMPANION_TASK_STACK_CAPS (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#endif
