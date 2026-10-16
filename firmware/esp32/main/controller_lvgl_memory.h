#ifndef CONTROLLER_LVGL_MEMORY_H
#define CONTROLLER_LVGL_MEMORY_H
#include <stdlib.h>
/* lv_conf_internal.h supplies defaults before lv_mem.c includes this adapter. */
#undef LV_MEM_CUSTOM_ALLOC
#undef LV_MEM_CUSTOM_REALLOC
#undef LV_MEM_CUSTOM_FREE
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#include "esp_log.h"
/* Keep widgets/styles and transient rendering buffers out of scarce internal RAM. */
static inline void *controller_lvgl_alloc(size_t size) {
    void *ptr=heap_caps_malloc(size,MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if(!ptr && size) {
        ESP_LOGE("lvgl-memory","PSRAM allocation failed: %u bytes",(unsigned)size);
        abort();
    }
    return ptr;
}
static inline void *controller_lvgl_realloc(void *ptr,size_t size) {
    void *next=heap_caps_realloc(ptr,size,MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if(!next && size) {
        ESP_LOGE("lvgl-memory","PSRAM reallocation failed: %u bytes",(unsigned)size);
        abort();
    }
    return next;
}
#define LV_MEM_CUSTOM_ALLOC controller_lvgl_alloc
#define LV_MEM_CUSTOM_REALLOC controller_lvgl_realloc
#define LV_MEM_CUSTOM_FREE heap_caps_free
#else
/* Native acceptance tests exercise LVGL's same custom allocator path. */
#define LV_MEM_CUSTOM_ALLOC malloc
#define LV_MEM_CUSTOM_REALLOC realloc
#define LV_MEM_CUSTOM_FREE free
#endif
#endif
