// SPDX-License-Identifier: Apache-2.0
#include "esp_heap_caps.h"
void *companion_quirc_malloc(size_t size) {return heap_caps_malloc(size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);}
void *companion_quirc_calloc(size_t count,size_t size) {return heap_caps_calloc(count,size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);}
