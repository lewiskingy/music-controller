#include <stdio.h>
#include <string.h>
#include "controller_ui.h"

/* Semantic colours are shared styles, so new and existing controls update together. */
enum { BG, SURFACE, CARD, ACTIVE, TEXT, MUTED, ACCENT, ROLE_COUNT };
static lv_style_t backgrounds[ROLE_COUNT], foregrounds[ROLE_COUNT];
static bool styles_ready;
static const char *palette_names[] = {"Green", "Blue", "Red", "Orange", "Purple", "Grey"};
static const unsigned palettes[6][2][ROLE_COUNT] = {
 {{0x101b19,0x162620,0x243a30,0x273e30,0xf0f5ed,0xaabbb0,0xc1d7a5},
  {0xf3f7f1,0xe4ece0,0xd7e4d0,0xc8ddbc,0x172517,0x4b6048,0x385d28}},
 {{0x101923,0x172634,0x243a50,0x294563,0xf0f5fb,0xaabacd,0xa5c9ef},
  {0xf1f6fb,0xe0eaf4,0xd0dfee,0xbcd6ee,0x172535,0x485e73,0x245b8e}},
 {{0x211315,0x301d20,0x492b30,0x563037,0xfbf1f1,0xc6afb2,0xefafb7},
  {0xfbf3f3,0xf2e2e3,0xebd2d5,0xe5bfc5,0x35171c,0x754b53,0x943747}},
 {{0x211810,0x302419,0x493525,0x563d29,0xfbf5ed,0xc6b6a5,0xefc295},
  {0xfbf6f0,0xf2e8db,0xebddca,0xe5cfb1,0x352517,0x72583c,0x87501b}},
 {{0x1b1423,0x281e34,0x3c2c4d,0x473359,0xf8f1fb,0xbcafca,0xd5b0ed},
  {0xf8f3fb,0xeee2f4,0xe4d3ed,0xdac1e6,0x2c1938,0x654d77,0x71428f}},
 {{0x171717,0x242424,0x353535,0x414141,0xf4f4f4,0xb7b7b7,0xd4d4d4},
  {0xf6f6f6,0xe7e7e7,0xd8d8d8,0xc8c8c8,0x202020,0x585858,0x444444}}
};
static void bg(lv_obj_t *obj, unsigned role, lv_style_selector_t selector) {
    lv_obj_add_style(obj, &backgrounds[role], selector);
}
static void fg(lv_obj_t *obj, unsigned role, lv_style_selector_t selector) {
    lv_obj_add_style(obj, &foregrounds[role], selector);
}
void controller_ui_set_theme(controller_ui_t *ui, unsigned palette, bool light) {
    if (palette >= 6) palette = 0;
    if (!styles_ready) {
        for (unsigned i=0;i<ROLE_COUNT;i++) {
            lv_style_init(&backgrounds[i]); lv_style_init(&foregrounds[i]);
        }
        styles_ready = true;
    }
    ui->palette = palette; ui->light = light;
    for (unsigned i=0;i<ROLE_COUNT;i++) {
        lv_style_set_bg_color(&backgrounds[i], lv_color_hex(palettes[palette][light][i]));
        lv_style_set_text_color(&foregrounds[i], lv_color_hex(palettes[palette][light][i]));
        lv_obj_report_style_change(&backgrounds[i]); lv_obj_report_style_change(&foregrounds[i]);
    }
    for (unsigned i=0;i<6;i++) if (ui->palette_buttons[i]) {
        if (i==palette) lv_obj_add_state(ui->palette_buttons[i],LV_STATE_CHECKED);
        else lv_obj_clear_state(ui->palette_buttons[i],LV_STATE_CHECKED);
    }
    for (unsigned i=0;i<2;i++) if (ui->mode_buttons[i]) {
        if (i==(unsigned)light) lv_obj_add_state(ui->mode_buttons[i],LV_STATE_CHECKED);
        else lv_obj_clear_state(ui->mode_buttons[i],LV_STATE_CHECKED);
    }
}
void controller_ui_style_source(lv_obj_t *obj) {
    fg(obj,TEXT,0); bg(obj,CARD,LV_PART_INDICATOR);
    bg(obj,ACCENT,LV_PART_INDICATOR | LV_STATE_CHECKED);
}
static void settings_clicked(lv_event_t *event) {
    controller_ui_t *ui = lv_event_get_user_data(event);
    controller_ui_show(ui, UI_SETTINGS);
}
static void theme_clicked(lv_event_t *event) {
    controller_ui_t *ui = lv_event_get_user_data(event);
    lv_obj_t *target = lv_event_get_target(event);
    unsigned palette=ui->palette; bool light=ui->light;
    for (unsigned i=0;i<6;i++) if(target==ui->palette_buttons[i]) palette=i;
    for (unsigned i=0;i<2;i++) if(target==ui->mode_buttons[i]) light=i;
    controller_ui_set_theme(ui,palette,light);
    if(ui->theme_cb) ui->theme_cb(palette,light);
}

static lv_obj_t *panel(lv_obj_t *parent, int x, int y, int w, int h, unsigned color) {
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, 0, 0);
    bg(obj, color, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}
static lv_obj_t *label(lv_obj_t *parent, const char *text, int x, int y, int w,
                       unsigned color, bool large) {
    lv_obj_t *obj = lv_label_create(parent);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_width(obj, w);
    lv_obj_set_height(obj, large ? 30 : 24);
    lv_label_set_text(obj, text);
    lv_label_set_long_mode(obj, LV_LABEL_LONG_DOT);
    fg(obj, color, 0);
    if (large) lv_obj_set_style_text_font(obj, &lv_font_montserrat_24, 0);
    return obj;
}
static lv_obj_t *button(lv_obj_t *parent, const char *text, int x, int y, int w, int h,
                        lv_event_cb_t cb, void *data) {
    lv_obj_t *obj = lv_btn_create(parent);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    bg(obj, CARD, 0);
    fg(obj, TEXT, 0);
    lv_obj_set_style_radius(obj, 12, 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
    bg(obj, ACTIVE, LV_STATE_PRESSED);
    bg(obj, CARD, LV_STATE_DISABLED);
    lv_obj_set_style_opa(obj, LV_OPA_40, LV_STATE_DISABLED);
    if (cb) lv_obj_add_event_cb(obj, cb, LV_EVENT_CLICKED, data);
    lv_obj_t *caption = lv_label_create(obj);
    lv_label_set_text(caption, text);
    lv_obj_center(caption);
    return obj;
}
static void send_intent(controller_ui_t *ui, ui_action_t type, int value, const ui_intent_t *target) {
    ui_intent_t intent = *target;
    intent.type = type;
    intent.value = value;
    if (ui->action_cb) ui->action_cb(&intent);
}
static void dock_clicked(lv_event_t *event) {
    controller_ui_t *ui = lv_event_get_user_data(event);
    lv_obj_t *target = lv_event_get_target(event);
    ui_action_t action;
    if (target == ui->transport[0]) action = UI_PREVIOUS;
    else if (target == ui->transport[1]) action = UI_PLAY_STOP;
    else if (target == ui->transport[2]) action = UI_NEXT;
    else if (target == ui->volume_down) action = UI_VOLUME_DOWN;
    else action = UI_VOLUME_UP;
    send_intent(ui, action, 0, &ui->current);
}
static void time_text(lv_obj_t *label_obj, int seconds) {
    char text[24];
    snprintf(text, sizeof(text), "%d:%02d", seconds / 60, seconds % 60);
    lv_label_set_text(label_obj, text);
}
static void slider_event(lv_event_t *event) {
    controller_ui_t *ui = lv_event_get_user_data(event);
    lv_obj_t *slider = lv_event_get_target(event);
    lv_event_code_t code = lv_event_get_code(event);
    bool volume = slider == ui->volume_slider;
    bool *dragging = volume ? &ui->volume_dragging : &ui->seek_dragging;
    bool *valid = volume ? &ui->volume_valid : &ui->seek_valid;
    ui_intent_t *gesture = volume ? &ui->volume_gesture : &ui->seek_gesture;
    if (code == LV_EVENT_PRESSED) {
        *gesture = ui->current;
        *dragging = *valid = true;
        ui->last_volume_send = lv_tick_get();
    } else if (code == LV_EVENT_VALUE_CHANGED && *dragging && *valid) {
        int value = lv_slider_get_value(slider);
        if (volume) {
            char text[24];
            snprintf(text, sizeof(text), "~%d%%", value);
            lv_label_set_text(ui->volume, text);
            if (lv_tick_elaps(ui->last_volume_send) >= 300) {
                send_intent(ui, UI_VOLUME_SET, value, gesture);
                ui->last_volume_send = lv_tick_get();
            }
        } else time_text(ui->elapsed, value);
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        if (*dragging && *valid && code == LV_EVENT_RELEASED)
            send_intent(ui, volume ? UI_VOLUME_SET : UI_SEEK, lv_slider_get_value(slider), gesture);
        *dragging = false;
        *valid = false;
    }
}
static lv_obj_t *slider(lv_obj_t *parent, int x, int y, int w, int h, controller_ui_t *ui) {
    lv_obj_t *obj = lv_slider_create(parent);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    bg(obj, CARD, LV_PART_MAIN);
    bg(obj, ACCENT, LV_PART_INDICATOR);
    bg(obj, ACCENT, LV_PART_KNOB);
    lv_obj_set_style_pad_all(obj, 8, LV_PART_KNOB);
    lv_obj_set_style_opa(obj, LV_OPA_40, LV_STATE_DISABLED);
    /* Extend hit targets without making the visible rails thick. */
    lv_obj_set_ext_click_area(obj, 21);
    lv_obj_add_event_cb(obj, slider_event, LV_EVENT_ALL, ui);
    return obj;
}
void controller_ui_style_row(lv_obj_t *row, bool selected, bool available) {
    lv_obj_set_height(row, 72);
    bg(row, selected ? ACTIVE : SURFACE, 0);
    fg(row, TEXT, 0);
    lv_obj_set_style_radius(row, 10, 0);
    if (!available) lv_obj_add_state(row, LV_STATE_DISABLED);
}
void controller_ui_create(controller_ui_t *ui, lv_obj_t *root, ui_action_cb_t action_cb,
                          lv_event_cb_t players_cb, lv_event_cb_t providers_cb,
                          lv_event_cb_t back_cb, lv_event_cb_t save_cb) {
    memset(ui, 0, sizeof(*ui));
    ui->root = root;
    ui->action_cb = action_cb;
    controller_ui_set_theme(ui,0,false);
    bg(root, BG, 0);
    fg(root, TEXT, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *header = panel(root, 0, 0, UI_WIDTH, UI_HEADER_HEIGHT, BG);
    ui->heading = label(header, "Now Playing", 68, 22, 142, TEXT, false);
    ui->settings_button = button(header, LV_SYMBOL_SETTINGS, 8, 8, 48, 48, settings_clicked, ui);
    ui->player_button = button(header, "", 224, 8, 248, 48, players_cb, NULL);
    lv_obj_t *player_button = ui->player_button;
    ui->player_text = lv_obj_get_child(player_button, 0);
    lv_obj_set_width(ui->player_text, 210);
    lv_label_set_long_mode(ui->player_text, LV_LABEL_LONG_DOT);
    ui->now_content = panel(root, 0, UI_HEADER_HEIGHT, UI_CONTENT_WIDTH, UI_CONTENT_HEIGHT, BG);
    ui->players_content = panel(root, 0, UI_HEADER_HEIGHT, UI_CONTENT_WIDTH, UI_CONTENT_HEIGHT, BG);
    ui->providers_content = panel(root, 0, UI_HEADER_HEIGHT, UI_CONTENT_WIDTH, UI_CONTENT_HEIGHT, BG);
    lv_obj_t *art = panel(ui->now_content, 56, 20, 288, 288, ACTIVE);
    lv_obj_set_style_radius(art, 12, 0);
    lv_obj_t *music = label(art, LV_SYMBOL_AUDIO, 110, 100, 68, ACCENT, true);
    lv_obj_set_style_text_align(music, LV_TEXT_ALIGN_CENTER, 0);
    label(art, "Album artwork", 72, 220, 180, MUTED, false);
    ui->title = label(ui->now_content, "Nothing playing", 24, 326, 352, TEXT, true);
    lv_obj_set_height(ui->title, 58);
    lv_obj_set_style_text_align(ui->title, LV_TEXT_ALIGN_CENTER, 0);
    ui->artist = label(ui->now_content, "", 24, 391, 352, MUTED, false);
    lv_obj_set_style_text_align(ui->artist, LV_TEXT_ALIGN_CENTER, 0);
    ui->album = label(ui->now_content, "", 24, 417, 352, MUTED, false);
    lv_obj_set_style_text_align(ui->album, LV_TEXT_ALIGN_CENTER, 0);
    ui->elapsed = label(ui->now_content, "0:00", 24, 456, 58, MUTED, false);
    ui->timeline = slider(ui->now_content, 103, 465, 194, 6, ui);
    ui->duration = label(ui->now_content, "--:--", 318, 456, 58, MUTED, false);
    ui->status = label(ui->now_content, "Connecting...", 24, 505, 352, MUTED, false);
    lv_obj_set_style_text_align(ui->status, LV_TEXT_ALIGN_CENTER, 0);
    label(ui->players_content, "Choose an output player", 24, 20, 352, MUTED, false);
    ui->players_list = lv_list_create(ui->players_content);
    lv_obj_set_pos(ui->players_list, 24, 60);
    lv_obj_set_size(ui->players_list, 352, 448);
    bg(ui->players_list, BG, 0);
    lv_obj_set_style_border_width(ui->players_list, 0, 0);
    label(ui->providers_content, "Choose music sources", 24, 20, 352, MUTED, false);
    ui->providers_list = panel(ui->providers_content, 24, 60, 352, 364, BG);
    lv_obj_add_flag(ui->providers_list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(ui->providers_list, LV_FLEX_FLOW_COLUMN);
    ui->save_button = button(ui->providers_content, "Save", 232, 434, 144, 56, save_cb, NULL);
    ui->provider_feedback = label(ui->providers_content, "Tap checkboxes, then Save", 24, 503, 352, MUTED, false);
    lv_obj_t *dock = panel(root, 0, UI_DOCK_Y, UI_WIDTH, UI_DOCK_HEIGHT, SURFACE);
    lv_obj_t *thumbnail = panel(dock, 24, 12, 40, 40, ACTIVE);
    lv_obj_set_style_radius(thumbnail, 6, 0);
    ui->mini_title = label(dock, "Nothing playing", 76, 12, 380, TEXT, false);
    ui->mini_artist = label(dock, "", 76, 34, 380, MUTED, false);
    ui->transport[0] = button(dock, LV_SYMBOL_PREV, 108, 68, 64, 56, dock_clicked, ui);
    ui->transport[1] = button(dock, LV_SYMBOL_PLAY, 204, 60, 72, 72, dock_clicked, ui);
    ui->transport[2] = button(dock, LV_SYMBOL_NEXT, 308, 68, 64, 56, dock_clicked, ui);
    lv_obj_set_style_radius(ui->transport[1], 36, 0);
    bg(ui->transport[1], ACCENT, 0);
    fg(ui->transport[1], BG, 0);
    ui->transport_text = lv_obj_get_child(ui->transport[1], 0);
    lv_obj_t *volume_dock = panel(root, UI_VOLUME_X, UI_HEADER_HEIGHT,
                                  UI_VOLUME_WIDTH, UI_CONTENT_HEIGHT, SURFACE);
    ui->volume_heading = label(volume_dock, "Vol", 8, 20, 64, MUTED, false);
    lv_obj_set_style_text_align(ui->volume_heading, LV_TEXT_ALIGN_CENTER, 0);
    ui->volume_up = button(volume_dock, "+", 12, 54, 56, 56, dock_clicked, ui);
    ui->volume_slider = slider(volume_dock, 37, 136, 6, 236, ui);
    lv_slider_set_range(ui->volume_slider, 0, 100);
    ui->volume = label(volume_dock, "0%", 8, 394, 64, TEXT, false);
    lv_obj_set_style_text_align(ui->volume, LV_TEXT_ALIGN_CENTER, 0);
    ui->volume_down = button(volume_dock, "-", 12, 432, 56, 56, dock_clicked, ui);
    lv_obj_t *nav = panel(root, 0, UI_NAV_Y, UI_WIDTH, UI_NAV_HEIGHT, BG);
    const char *tabs[] = {"Playing", "Queue", "Browse", "Search"};
    for (unsigned i=0;i<4;i++) {
        ui->navigation[i] = button(nav,tabs[i],8+118*i,10,110,48,i==0?back_cb:NULL,NULL);
        if(i) lv_obj_add_state(ui->navigation[i],LV_STATE_DISABLED);
    }
    ui->settings_content = panel(root,0,UI_HEADER_HEIGHT,UI_CONTENT_WIDTH,UI_CONTENT_HEIGHT,BG);
    label(ui->settings_content,"Colour palette",24,20,352,TEXT,true);
    for(unsigned i=0;i<6;i++) {
        ui->palette_buttons[i]=button(ui->settings_content,palette_names[i],24+(i%2)*180,
                                      68+(i/2)*68,172,56,theme_clicked,ui);
        bg(ui->palette_buttons[i],ACCENT,LV_STATE_CHECKED);
        fg(ui->palette_buttons[i],BG,LV_STATE_CHECKED);
    }
    label(ui->settings_content,"Appearance",24,278,352,TEXT,true);
    for(unsigned i=0;i<2;i++) {
        ui->mode_buttons[i]=button(ui->settings_content,i?"Light":"Dark",24+i*180,320,172,56,theme_clicked,ui);
        bg(ui->mode_buttons[i],ACCENT,LV_STATE_CHECKED);
        fg(ui->mode_buttons[i],BG,LV_STATE_CHECKED);
    }
    ui->sources_button=button(ui->settings_content,"Music sources",24,414,352,56,providers_cb,NULL);
    controller_ui_set_theme(ui,0,false);
    controller_ui_show(ui, UI_PLAYING);
}
void controller_ui_show(controller_ui_t *ui, ui_view_t view) {
    lv_obj_t *panes[] = {ui->now_content, ui->players_content, ui->providers_content, ui->settings_content};
    for (unsigned i = 0; i < 4; ++i) {
        if (i == (unsigned)view) lv_obj_clear_flag(panes[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(panes[i], LV_OBJ_FLAG_HIDDEN);
    }
    const char *titles[] = {"Now Playing", "Players", "Providers", "Settings"};
    lv_label_set_text(ui->heading, titles[view]);
    for (unsigned i = 0; i < 4; ++i) {
        lv_obj_remove_style(ui->navigation[i], &backgrounds[ACTIVE], 0);
        lv_obj_remove_style(ui->navigation[i], &backgrounds[BG], 0);
        bg(ui->navigation[i], i == 0 && view == UI_PLAYING ? ACTIVE : BG, 0);
    }
}
static void enabled(lv_obj_t *obj, bool value) {
    if (value) lv_obj_clear_state(obj, LV_STATE_DISABLED);
    else lv_obj_add_state(obj, LV_STATE_DISABLED);
}
void controller_ui_update(controller_ui_t *ui, const ui_playback_t *state) {
    bool player_changed = strcmp(ui->current.player_id, state->player_id) != 0;
    bool track_changed = player_changed || strcmp(ui->current.queue_id, state->queue_id) ||
                         strcmp(ui->current.item_id, state->item_id);
    if (player_changed || !state->connected || !state->available || !state->can_volume)
        ui->volume_valid = false;
    if (track_changed || !state->connected || !state->can_seek || state->transport_pending)
        ui->seek_valid = false;
    snprintf(ui->current.player_id, sizeof(ui->current.player_id), "%s", state->player_id);
    snprintf(ui->current.queue_id, sizeof(ui->current.queue_id), "%s", state->queue_id);
    snprintf(ui->current.item_id, sizeof(ui->current.item_id), "%s", state->item_id);
    ui->track_duration = state->duration;
    lv_label_set_text(ui->title, state->track);
    lv_label_set_text(ui->artist, state->artist);
    lv_label_set_text(ui->album, state->album);
    lv_label_set_text(ui->mini_title, state->track);
    char text[260];
    snprintf(text, sizeof(text), "%s%s%s", state->artist, state->artist[0] ? " - " : "", state->player);
    lv_label_set_text(ui->mini_artist, text);
    snprintf(text, sizeof(text), "%s%s", state->connected ? "" : "Offline - ", state->player);
    lv_label_set_text(ui->player_text, text);
    lv_label_set_text(ui->status, state->status);
    lv_label_set_text(ui->transport_text, state->playing ? LV_SYMBOL_STOP : LV_SYMBOL_PLAY);
    bool can_control = state->connected && state->available;
    for (unsigned i = 0; i < 3; ++i) enabled(ui->transport[i], can_control && !state->transport_pending);
    bool volume_enabled = can_control && state->can_volume;
    enabled(ui->volume_slider, volume_enabled);
    enabled(ui->volume_down, volume_enabled && !state->volume_pending && state->volume > 0);
    enabled(ui->volume_up, volume_enabled && !state->volume_pending && state->volume < 100);
    if (!ui->volume_dragging || !ui->volume_valid) {
        lv_slider_set_value(ui->volume_slider, state->volume, LV_ANIM_OFF);
        snprintf(text, sizeof(text), "%d%%%s", state->volume, state->volume_pending ? "*" : "");
        lv_label_set_text(ui->volume, text);
    }
    enabled(ui->timeline, can_control && state->can_seek && !state->seek_pending && !state->transport_pending);
    if (!ui->seek_dragging || !ui->seek_valid) {
        lv_slider_set_range(ui->timeline, 0, state->duration > 0 ? state->duration : 1);
        lv_slider_set_value(ui->timeline, state->position, LV_ANIM_OFF);
        time_text(ui->elapsed, state->position);
    }
    if (state->live) { lv_label_set_text(ui->elapsed, "Live"); lv_label_set_text(ui->duration, ""); }
    else if (state->duration > 0) time_text(ui->duration, state->duration);
    else lv_label_set_text(ui->duration, "--:--");
}
