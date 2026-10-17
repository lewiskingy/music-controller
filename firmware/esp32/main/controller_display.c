#include "controller_display.h"
#include "display_buffer.h"
#include "sdkconfig.h"
#include "esp_attr.h"

#if CONFIG_LCD_RGB_BOUNCE_BUFFER_HEIGHT != 20 || !CONFIG_ESP32S3_DATA_CACHE_LINE_64B || !CONFIG_SPIRAM_FETCH_INSTRUCTIONS || !CONFIG_SPIRAM_RODATA || !CONFIG_COMPILER_OPTIMIZATION_PERF
#error "Music Controller requires RGB scanout stability settings; regenerate sdkconfig from defaults"
#endif
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "bsp/lvgl_port.h"
#include "lvgl.h"

#define PORTRAIT_WIDTH 480
#define PORTRAIT_HEIGHT 800
#define PANEL_PCLK_HZ 12000000
static const char *TAG = "controller-display";
static esp_lcd_panel_handle_t rgb_panel;
static SemaphoreHandle_t frame_events;
static uint16_t *panel_buffers[2];
static unsigned next_buffer;
static display_damage_t damage;
static unsigned presented_frames;
static int64_t longest_flush_us;

static bool IRAM_ATTR frame_complete(esp_lcd_panel_handle_t panel,
                                     const esp_lcd_rgb_panel_event_data_t *event,
                                     void *context) {
    (void)panel; (void)event; (void)context;
    BaseType_t wake = pdFALSE;
    xSemaphoreGiveFromISR(frame_events, &wake);
    return wake == pdTRUE;
}

static void present_and_retire(uint16_t *pixels) {
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(rgb_panel, 0, 0,
                    PORTRAIT_HEIGHT, PORTRAIT_WIDTH, pixels));
    /* Drain AFTER submission, not before. A notification from the previous
     * frame may race submission. IDF 5.4's bounce callback has no framebuffer
     * identity and can already be in flight on the other core. Two subsequent
     * completions guarantee the old framebuffer is no longer being copied.
     * This deliberately trades up to two panel periods for safe retirement. */
    while (xSemaphoreTake(frame_events, 0) == pdTRUE) {}
    for (unsigned i = 0; i < DISPLAY_RETIRE_EVENTS; ++i)
        ESP_ERROR_CHECK(xSemaphoreTake(frame_events, pdMS_TO_TICKS(1000)) == pdTRUE
                        ? ESP_OK : ESP_ERR_TIMEOUT);
}

static void flush_frame(lv_disp_drv_t *driver, const lv_area_t *area, lv_color_t *pixels) {
    display_damage_add(&damage, (display_rect_t){area->x1, area->y1, area->x2, area->y2},
                       PORTRAIT_WIDTH, PORTRAIT_HEIGHT);
    if (lv_disp_flush_is_last(driver)) {
        int64_t started = esp_timer_get_time();
        uint16_t *next = panel_buffers[next_buffer];
        uint16_t *old = panel_buffers[next_buffer ^ 1];
        for (unsigned i = 0; i < damage.count; ++i)
            display_rotate_90((const uint16_t *)pixels, next, damage.areas[i],
                              PORTRAIT_WIDTH, PORTRAIT_HEIGHT);
        present_and_retire(next);
        for (unsigned i = 0; i < damage.count; ++i)
            display_sync_90(next, old, damage.areas[i], PORTRAIT_WIDTH, PORTRAIT_HEIGHT);
        next_buffer ^= 1;
        damage.count = 0;
        ++presented_frames;
        int64_t elapsed = esp_timer_get_time()-started;
        if (elapsed > longest_flush_us) longest_flush_us = elapsed;
    }
    lv_disp_flush_ready(driver);
}

esp_err_t controller_display_init(esp_lcd_panel_handle_t panel) {
    frame_events = xSemaphoreCreateCounting(DISPLAY_RETIRE_EVENTS, 0);
    if (!frame_events) return ESP_ERR_NO_MEM;
    if (!lvgl_port_lock(2000)) return ESP_ERR_TIMEOUT;
    rgb_panel = panel;
    lv_disp_t *display = lv_disp_get_default();
    lv_disp_drv_t *driver = display ? display->driver : NULL;
    /* Only the pinned LVGL8/BSP rotated direct-mode layout is supported. The
     * third RGB allocation is LVGL's full logical canvas, never a panel front. */
    void *buffers[3];
    ESP_ERROR_CHECK(esp_lcd_rgb_panel_get_frame_buffer(panel, 3,
                    &buffers[0], &buffers[1], &buffers[2]));
    panel_buffers[0] = buffers[0]; panel_buffers[1] = buffers[1];
    void *canvas = buffers[2];
    ESP_ERROR_CHECK(driver && LV_COLOR_DEPTH == 16 && driver->direct_mode &&
                    !driver->draw_buf->buf2 && driver->draw_buf->buf1 == canvas &&
                    driver->hor_res == PORTRAIT_WIDTH && driver->ver_res == PORTRAIT_HEIGHT
                    ? ESP_OK : ESP_ERR_INVALID_STATE);
    esp_lcd_rgb_panel_event_callbacks_t callbacks = {.on_frame_buf_complete = frame_complete};
    ESP_ERROR_CHECK(esp_lcd_rgb_panel_register_event_callbacks(panel, &callbacks, NULL));
    ESP_ERROR_CHECK(esp_lcd_rgb_panel_set_pclk(panel, PANEL_PCLK_HZ));
    /* Establish a known front even if the BSP drew its default screen before
     * we acquired its lock. No application widgets or visible startup page yet. */
    present_and_retire(panel_buffers[0]);
    next_buffer = 1;
    driver->flush_cb = flush_frame;
    driver->full_refresh = 0;
    lv_obj_invalidate(lv_scr_act());
    lvgl_port_unlock();
    ESP_LOGI(TAG,"Buffered portrait scanout: pclk=%d bounce_lines=%d retire_events=%d",
             PANEL_PCLK_HZ, CONFIG_LCD_RGB_BOUNCE_BUFFER_HEIGHT, DISPLAY_RETIRE_EVENTS);
    return ESP_OK;
}

void controller_display_log(void) {
    if (!lvgl_port_lock(10)) return;
    ESP_LOGI(TAG,"Presentation: frames=%u longest_flush_ms=%u",
             presented_frames, (unsigned)(longest_flush_us/1000));
    lvgl_port_unlock();
}
