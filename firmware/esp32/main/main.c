#include "esp_log.h"
#include "esp_system.h"
#include "esp_lcd_panel_ops.h"
#include "bsp/board.h"
#include "bsp/lvgl_port.h"
#include "lvgl.h"

static const char *TAG = "music-controller";

static void show_screen(void)
{
    lv_obj_t *screen = lv_scr_act();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x121c30), 0);

    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, "Music Controller");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 28);

    lv_obj_t *message = lv_label_create(screen);
    lv_label_set_text(message, "Now Playing\n\nWaiting for Music Assistant");
    lv_obj_set_style_text_color(message, lv_color_hex(0xb4c5dd), 0);
    lv_obj_set_style_text_align(message, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(message, LV_ALIGN_CENTER, 0, -18);

    lv_obj_t *button = lv_btn_create(screen);
    lv_obj_set_size(button, 220, 64);
    lv_obj_align(button, LV_ALIGN_BOTTOM_MID, 0, -36);
    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, "Touch ready");
    lv_obj_center(label);
}

void app_main(void)
{
    ESP_LOGI(TAG, "Waveshare ESP32-S3 Touch LCD 4.3 bring-up");
    ESP_LOGI(TAG, "One screen at a time: 800 x 480");
    esp_lcd_panel_handle_t panel = NULL;
    esp_lcd_touch_handle_t touch = NULL;

    ESP_ERROR_CHECK(waveshare_esp32_s3_rgb_lcd_init(&panel, &touch));
    ESP_ERROR_CHECK(lvgl_port_init(panel, touch));
    ESP_ERROR_CHECK(waveshare_rgb_lcd_bl_on());

    if (lvgl_port_lock(-1)) {
        show_screen();
        lvgl_port_unlock();
    }
    ESP_LOGI(TAG, "LCD and LVGL initialised; check display and touch");
}
