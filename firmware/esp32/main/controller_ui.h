#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"

enum { UI_WIDTH = 480, UI_HEIGHT = 800, UI_HEADER_HEIGHT = 64,
       UI_CONTENT_WIDTH = 400, UI_CONTENT_HEIGHT = 532, UI_VOLUME_X = 400,
       UI_VOLUME_WIDTH = 80, UI_DOCK_Y = 596, UI_DOCK_HEIGHT = 136,
       UI_NAV_Y = 732, UI_NAV_HEIGHT = 68 };
typedef enum { UI_PLAYING, UI_PLAYERS, UI_PROVIDERS, UI_SETTINGS } ui_view_t;
typedef enum { UI_PREVIOUS, UI_PLAY_STOP, UI_NEXT, UI_VOLUME_DOWN, UI_VOLUME_UP,
               UI_VOLUME_SET, UI_SEEK } ui_action_t;
typedef struct {
    ui_action_t type;
    int value;
    char player_id[129], queue_id[129], item_id[129];
} ui_intent_t;
typedef void (*ui_action_cb_t)(const ui_intent_t *intent);
typedef struct {
    const char *track, *artist, *album, *player, *status;
    const char *art_id;
    const char *player_id, *queue_id, *item_id;
    int volume, position, duration;
    bool connected, available, playing, transport_pending, volume_pending, seek_pending;
    bool can_volume, can_seek, live;
} ui_playback_t;
typedef struct {
    lv_obj_t *root, *now_content, *players_content, *providers_content;
    lv_obj_t *players_list, *providers_list, *provider_feedback, *save_button;
    lv_obj_t *heading, *player_text, *title, *artist, *album, *status;
    lv_obj_t *mini_title, *mini_artist, *volume, *transport[3], *transport_text;
    lv_obj_t *volume_down, *volume_up, *volume_slider, *navigation[4];
    lv_obj_t *timeline, *elapsed, *duration;
    lv_obj_t *settings_button, *player_button, *settings_content, *sources_button, *volume_heading;
    lv_obj_t *palette_buttons[6], *mode_buttons[2];
    lv_obj_t *art_panel, *art_image, *art_placeholder[2], *thumbnail, *thumbnail_image;
    lv_img_dsc_t art_descriptor, thumbnail_descriptor;
    lv_color_t *art_pixels, *thumbnail_pixels;
    char art_id[65];
    bool art_loaded;
    unsigned palette;
    bool light;
    void (*theme_cb)(unsigned palette, bool light);
    ui_intent_t current, volume_gesture, seek_gesture;
    int track_duration;
    bool volume_dragging, seek_dragging, volume_valid, seek_valid;
    uint32_t last_volume_send;
    ui_action_cb_t action_cb;
} controller_ui_t;
/* All methods run with the LVGL port lock held; no network or NVS work. */
void controller_ui_create(controller_ui_t *ui, lv_obj_t *root, ui_action_cb_t action_cb,
                          lv_event_cb_t players_cb, lv_event_cb_t providers_cb,
                          lv_event_cb_t back_cb, lv_event_cb_t save_cb);
void controller_ui_show(controller_ui_t *ui, ui_view_t view);
void controller_ui_update(controller_ui_t *ui, const ui_playback_t *state);
void controller_ui_style_row(lv_obj_t *row, bool selected, bool available);

void controller_ui_set_theme(controller_ui_t *ui, unsigned palette, bool light);
void controller_ui_style_source(lv_obj_t *obj);

/* Exact MCAR 288x288 little-endian RGB565 packet; caller retains packet ownership. */
bool controller_ui_set_artwork(controller_ui_t *ui, const char *art_id, const unsigned char *packet, size_t length);
