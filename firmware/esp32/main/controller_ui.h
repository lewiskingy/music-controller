#pragma once
#include <stdbool.h>
#include "lvgl.h"

/* Geometry shared with docs/design/music-controller-design.html. */
enum { UI_WIDTH = 480, UI_HEIGHT = 800, UI_HEADER_HEIGHT = 64,
       UI_CONTENT_HEIGHT = 468, UI_DOCK_Y = 532, UI_DOCK_HEIGHT = 200,
       UI_NAV_Y = 732, UI_NAV_HEIGHT = 68 };
typedef enum { UI_PLAYING, UI_PLAYERS, UI_PROVIDERS } ui_view_t;
typedef enum { UI_PREVIOUS, UI_PLAY_STOP, UI_NEXT, UI_VOLUME_DOWN, UI_VOLUME_UP } ui_action_t;
typedef void (*ui_action_cb_t)(ui_action_t action);
typedef struct {
    const char *track, *artist, *player, *status;
    int volume;
    bool connected, available, playing, pending;
} ui_playback_t;
typedef struct {
    lv_obj_t *root, *now_content, *players_content, *providers_content;
    lv_obj_t *players_list, *providers_list, *provider_feedback, *save_button;
    lv_obj_t *heading, *player_text, *title, *artist, *status;
    lv_obj_t *mini_title, *mini_artist, *volume, *transport[3], *transport_text;
    lv_obj_t *volume_down, *volume_up, *navigation[3];
    ui_action_cb_t action_cb;
} controller_ui_t;
/* All methods run with the LVGL port lock held. No networking in this module. */
void controller_ui_create(controller_ui_t *ui, lv_obj_t *root, ui_action_cb_t action_cb,
                          lv_event_cb_t players_cb, lv_event_cb_t providers_cb,
                          lv_event_cb_t back_cb, lv_event_cb_t save_cb);
void controller_ui_show(controller_ui_t *ui, ui_view_t view);
void controller_ui_update(controller_ui_t *ui, const ui_playback_t *state);
void controller_ui_style_row(lv_obj_t *row, bool selected, bool available);
