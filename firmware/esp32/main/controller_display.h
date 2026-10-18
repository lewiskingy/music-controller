#pragma once
#include "esp_err.h"
#include "esp_lcd_types.h"

/* Install once after BSP LVGL init and before constructing application widgets. */
esp_err_t controller_display_init(esp_lcd_panel_handle_t panel);
void controller_display_log(void);
