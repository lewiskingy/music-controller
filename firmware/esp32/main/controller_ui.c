#include <stdio.h>
#include <string.h>
#include "controller_ui.h"

#define BG 0x101b19
#define SURFACE 0x162620
#define CARD 0x243a30
#define ACTIVE 0x273e30
#define TEXT 0xf0f5ed
#define MUTED 0xaabbb0
#define ACCENT 0xc1d7a5

static lv_obj_t *panel(lv_obj_t *parent, int x, int y, int w, int h, unsigned color) {
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}
static lv_obj_t *label(lv_obj_t *parent, const char *text, int x, int y, int w,
                       unsigned color, bool large) {
    lv_obj_t *obj = lv_label_create(parent);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_width(obj, w);
    lv_label_set_text(obj, text);
    lv_label_set_long_mode(obj, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    if (large) lv_obj_set_style_text_font(obj, &lv_font_montserrat_24, 0);
    return obj;
}
static lv_obj_t *button(lv_obj_t *parent, const char *text, int x, int y, int w, int h,
                        lv_event_cb_t cb, void *data) {
    lv_obj_t *obj = lv_btn_create(parent);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_bg_color(obj, lv_color_hex(CARD), 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(TEXT), 0);
    lv_obj_set_style_radius(obj, 12, 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(ACTIVE), LV_STATE_PRESSED);
    lv_obj_set_style_opa(obj, LV_OPA_40, LV_STATE_DISABLED);
    if (cb) lv_obj_add_event_cb(obj, cb, LV_EVENT_CLICKED, data);
    lv_obj_t *caption = lv_label_create(obj);
    lv_label_set_text(caption, text);
    lv_obj_center(caption);
    return obj;
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
    if (ui->action_cb) ui->action_cb(action);
}
void controller_ui_style_row(lv_obj_t *row, bool selected, bool available) {
    lv_obj_set_height(row, 72);
    lv_obj_set_style_bg_color(row, lv_color_hex(selected ? ACTIVE : SURFACE), 0);
    lv_obj_set_style_text_color(row, lv_color_hex(TEXT), 0);
    lv_obj_set_style_radius(row, 10, 0);
    if (!available) lv_obj_add_state(row, LV_STATE_DISABLED);
}
void controller_ui_create(controller_ui_t *ui, lv_obj_t *root, ui_action_cb_t action_cb,
                          lv_event_cb_t players_cb, lv_event_cb_t providers_cb,
                          lv_event_cb_t back_cb, lv_event_cb_t save_cb) {
    memset(ui, 0, sizeof(*ui));
    ui->root = root;
    ui->action_cb = action_cb;
    lv_obj_set_style_bg_color(root, lv_color_hex(BG), 0);
    lv_obj_set_style_text_color(root, lv_color_hex(TEXT), 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *header = panel(root, 0, 0, UI_WIDTH, UI_HEADER_HEIGHT, BG);
    ui->heading = label(header, "Now Playing", 24, 22, 180, TEXT, false);
    lv_obj_t *player_button = button(header, "", 216, 8, 240, 48, players_cb, NULL);
    ui->player_text = lv_obj_get_child(player_button, 0);
    lv_obj_set_width(ui->player_text, 210);
    lv_label_set_long_mode(ui->player_text, LV_LABEL_LONG_DOT);
    ui->now_content = panel(root, 0, UI_HEADER_HEIGHT, UI_WIDTH, UI_CONTENT_HEIGHT, BG);
    ui->players_content = panel(root, 0, UI_HEADER_HEIGHT, UI_WIDTH, UI_CONTENT_HEIGHT, BG);
    ui->providers_content = panel(root, 0, UI_HEADER_HEIGHT, UI_WIDTH, UI_CONTENT_HEIGHT, BG);
    lv_obj_t *art = panel(ui->now_content, 96, 20, 288, 288, ACTIVE);
    lv_obj_set_style_radius(art, 12, 0);
    lv_obj_t *music = label(art, LV_SYMBOL_AUDIO, 110, 100, 68, ACCENT, true);
    lv_obj_set_style_text_align(music, LV_TEXT_ALIGN_CENTER, 0);
    label(art, "Album artwork", 72, 220, 180, MUTED, false);
    ui->title = label(ui->now_content, "Nothing playing", 24, 326, 432, TEXT, true);
    lv_obj_set_height(ui->title, 58);
    lv_obj_set_style_text_align(ui->title, LV_TEXT_ALIGN_CENTER, 0);
    ui->artist = label(ui->now_content, "", 24, 391, 432, MUTED, false);
    lv_obj_set_style_text_align(ui->artist, LV_TEXT_ALIGN_CENTER, 0);
    ui->status = label(ui->now_content, "Connecting...", 24, 431, 432, MUTED, false);
    lv_obj_set_style_text_align(ui->status, LV_TEXT_ALIGN_CENTER, 0);
    label(ui->players_content, "Choose an output player", 24, 20, 432, MUTED, false);
    ui->players_list = lv_list_create(ui->players_content);
    lv_obj_set_pos(ui->players_list, 24, 60);
    lv_obj_set_size(ui->players_list, 432, 384);
    lv_obj_set_style_bg_color(ui->players_list, lv_color_hex(BG), 0);
    lv_obj_set_style_border_width(ui->players_list, 0, 0);
    label(ui->providers_content, "Choose sources for future Browse and Search", 24, 20, 432, MUTED, false);
    ui->providers_list = panel(ui->providers_content, 24, 60, 432, 284, BG);
    lv_obj_add_flag(ui->providers_list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(ui->providers_list, LV_FLEX_FLOW_COLUMN);
    ui->save_button = button(ui->providers_content, "Save", 312, 364, 144, 56, save_cb, NULL);
    ui->provider_feedback = label(ui->providers_content, "Tap checkboxes, then Save", 24, 428, 432, MUTED, false);
    lv_obj_t *dock = panel(root, 0, UI_DOCK_Y, UI_WIDTH, UI_DOCK_HEIGHT, SURFACE);
    lv_obj_t *thumbnail = panel(dock, 24, 12, 40, 40, ACTIVE);
    lv_obj_set_style_radius(thumbnail, 6, 0);
    ui->mini_title = label(dock, "Nothing playing", 76, 12, 380, TEXT, false);
    ui->mini_artist = label(dock, "", 76, 34, 380, MUTED, false);
    ui->transport[0] = button(dock, LV_SYMBOL_PREV, 108, 68, 64, 56, dock_clicked, ui);
    ui->transport[1] = button(dock, LV_SYMBOL_PLAY, 204, 60, 72, 72, dock_clicked, ui);
    ui->transport[2] = button(dock, LV_SYMBOL_NEXT, 308, 68, 64, 56, dock_clicked, ui);
    lv_obj_set_style_radius(ui->transport[1], 36, 0);
    lv_obj_set_style_bg_color(ui->transport[1], lv_color_hex(ACCENT), 0);
    lv_obj_set_style_text_color(ui->transport[1], lv_color_hex(BG), 0);
    ui->transport_text = lv_obj_get_child(ui->transport[1], 0);
    ui->volume_down = button(dock, "-", 24, 144, 56, 48, dock_clicked, ui);
    ui->volume = label(dock, "Volume 0%", 96, 157, 288, TEXT, false);
    lv_obj_set_style_text_align(ui->volume, LV_TEXT_ALIGN_CENTER, 0);
    ui->volume_up = button(dock, "+", 400, 144, 56, 48, dock_clicked, ui);
    lv_obj_t *nav = panel(root, 0, UI_NAV_Y, UI_WIDTH, UI_NAV_HEIGHT, BG);
    ui->navigation[0] = button(nav, "Playing", 12, 10, 144, 48, back_cb, NULL);
    ui->navigation[1] = button(nav, "Sources", 168, 10, 144, 48, providers_cb, NULL);
    ui->navigation[2] = button(nav, "Players", 324, 10, 144, 48, players_cb, NULL);
    controller_ui_show(ui, UI_PLAYING);
}
void controller_ui_show(controller_ui_t *ui, ui_view_t view) {
    lv_obj_t *panes[] = {ui->now_content, ui->players_content, ui->providers_content};
    for (unsigned i = 0; i < 3; ++i) {
        if (i == (unsigned)view) lv_obj_clear_flag(panes[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(panes[i], LV_OBJ_FLAG_HIDDEN);
    }
    const char *titles[] = {"Now Playing", "Players", "Providers"};
    lv_label_set_text(ui->heading, titles[view]);
    unsigned active = view == UI_PLAYING ? 0 : (view == UI_PROVIDERS ? 1 : 2);
    for (unsigned i = 0; i < 3; ++i)
        lv_obj_set_style_bg_color(ui->navigation[i], lv_color_hex(i == active ? ACTIVE : BG), 0);
}
static void enabled(lv_obj_t *obj, bool value) {
    if (value) lv_obj_clear_state(obj, LV_STATE_DISABLED);
    else lv_obj_add_state(obj, LV_STATE_DISABLED);
}
void controller_ui_update(controller_ui_t *ui, const ui_playback_t *state) {
    lv_label_set_text(ui->title, state->track);
    lv_label_set_text(ui->artist, state->artist);
    lv_label_set_text(ui->mini_title, state->track);
    char text[260];
    snprintf(text, sizeof(text), "%s%s%s", state->artist, state->artist[0] ? " - " : "", state->player);
    lv_label_set_text(ui->mini_artist, text);
    snprintf(text, sizeof(text), "%s%s", state->connected ? "" : "Offline - ", state->player);
    lv_label_set_text(ui->player_text, text);
    lv_label_set_text(ui->status, state->status);
    snprintf(text, sizeof(text), "Volume %d%%", state->volume);
    lv_label_set_text(ui->volume, text);
    lv_label_set_text(ui->transport_text, state->playing ? LV_SYMBOL_STOP : LV_SYMBOL_PLAY);
    bool can_control = state->connected && state->available && !state->pending;
    for (unsigned i = 0; i < 3; ++i) enabled(ui->transport[i], can_control);
    enabled(ui->volume_down, can_control && state->volume > 0);
    enabled(ui->volume_up, can_control && state->volume < 100);
    if (state->pending) lv_label_set_text(ui->volume, "Sending command...");
}
