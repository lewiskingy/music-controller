#pragma once
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#define CONFIG_LCD_RGB_BOUNCE_BUFFER_HEIGHT 20
#define CONFIG_ESP32S3_DATA_CACHE_LINE_64B 1
#define CONFIG_SPIRAM_FETCH_INSTRUCTIONS 1
#define CONFIG_SPIRAM_RODATA 1
#define CONFIG_COMPILER_OPTIMIZATION_PERF 1
#define IRAM_ATTR
#define ESP_OK 0
#define ESP_ERR_NO_MEM 1
#define ESP_ERR_TIMEOUT 2
#define ESP_ERR_INVALID_STATE 3
#define ESP_ERROR_CHECK(e) assert((e) == ESP_OK)
#define ESP_LOGI(tag, ...) ((void)(tag))
#define LV_COLOR_DEPTH 16
#define pdFALSE 0
#define pdTRUE 1
#define pdMS_TO_TICKS(ms) (ms)
typedef int esp_err_t;
typedef int BaseType_t;
typedef void *esp_lcd_panel_handle_t;
typedef struct { unsigned count, maximum; } test_semaphore_t;
typedef test_semaphore_t *SemaphoreHandle_t;
typedef struct { int unused; } esp_lcd_rgb_panel_event_data_t;
typedef struct {
    bool (*on_frame_buf_complete)(esp_lcd_panel_handle_t,
                                 const esp_lcd_rgb_panel_event_data_t *, void *);
} esp_lcd_rgb_panel_event_callbacks_t;
typedef struct { int x1,y1,x2,y2; } lv_area_t;
typedef uint16_t lv_color_t;
typedef struct { void *buf1, *buf2; } lv_disp_draw_buf_t;
typedef struct lv_disp_drv_t {
    lv_disp_draw_buf_t *draw_buf;
    bool direct_mode, full_refresh;
    int hor_res, ver_res;
    void (*flush_cb)(struct lv_disp_drv_t *, const lv_area_t *, lv_color_t *);
} lv_disp_drv_t;
typedef struct { lv_disp_drv_t *driver; } lv_disp_t;
SemaphoreHandle_t xSemaphoreCreateCounting(unsigned maximum, unsigned initial);
BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, unsigned timeout);
BaseType_t xSemaphoreGiveFromISR(SemaphoreHandle_t semaphore, BaseType_t *wake);
esp_err_t esp_lcd_panel_draw_bitmap(esp_lcd_panel_handle_t, int, int, int, int, const void *);
esp_err_t esp_lcd_rgb_panel_get_frame_buffer(esp_lcd_panel_handle_t, unsigned, ...);
esp_err_t esp_lcd_rgb_panel_register_event_callbacks(esp_lcd_panel_handle_t, const esp_lcd_rgb_panel_event_callbacks_t *, void *);
esp_err_t esp_lcd_rgb_panel_set_pclk(esp_lcd_panel_handle_t, unsigned);
int64_t esp_timer_get_time(void);
bool lvgl_port_lock(int);
void lvgl_port_unlock(void);
lv_disp_t *lv_disp_get_default(void);
void *lv_scr_act(void);
void lv_obj_invalidate(void *);
bool lv_disp_flush_is_last(lv_disp_drv_t *);
void lv_disp_flush_ready(lv_disp_drv_t *);
