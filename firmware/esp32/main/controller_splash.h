#pragma once
#include <stdbool.h>
#include "lvgl.h"
/* LVGL task/lock only. The bundled image works before Wi-Fi/NVS startup. */
void controller_splash_show(bool loading);
/* NULL restores controls; otherwise cover them with an updating status caption. */
void controller_splash_status(const char *message);
void controller_splash_hide(void);
bool controller_splash_visible(void);

