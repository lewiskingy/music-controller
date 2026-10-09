#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_websocket_client.h"
#include "esp_crt_bundle.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "cJSON.h"
#include "esp_lcd_panel_ops.h"
#include "bsp/board.h"
#include "bsp/lvgl_port.h"
#include "lvgl.h"

static char ssid[33], password[65], token[129], url[192], preferred[129];
static char player_id[129], player_name[96] = "No player", track[160] = "Waiting for Music Assistant",
            artist[160] = "", state[96] = "Starting...";
#define MAX_PLAYERS 20
#define MAX_PROVIDERS 16
#define PROVIDER_SELECTION_SIZE 2048
typedef struct {
    char id[129];
    char name[96];
    char provider[64];
    int volume;
    bool playing;
    bool paused;
    bool available;
} player_info_t;
static player_info_t players[MAX_PLAYERS];
static size_t player_count;
typedef struct { char id[96]; char name[96]; bool selected; } music_provider_t;
static music_provider_t music_providers[MAX_PROVIDERS];
static size_t music_provider_count;
static char selected_provider_ids[PROVIDER_SELECTION_SIZE];
static bool provider_selection_saved;
static volatile bool providers_dirty = true;
static volatile bool provider_fetch_pending;
static SemaphoreHandle_t state_mutex;
static QueueHandle_t action_queue;
typedef enum { VIEW_NOW_PLAYING, VIEW_PLAYERS, VIEW_PROVIDERS } view_t;
typedef enum { ACT_SELECT, ACT_PLAY_PAUSE, ACT_STOP, ACT_PREVIOUS, ACT_NEXT, ACT_VOLUME_DOWN, ACT_VOLUME_UP, ACT_PROVIDER_TOGGLE } action_type_t;
typedef struct { action_type_t type; char player_id[129]; bool selected; } action_t;
static view_t active_view = VIEW_NOW_PLAYING;
static lv_obj_t *now_screen, *players_screen, *providers_screen;
static lv_obj_t *title_label, *artist_label, *player_label, *state_label, *volume_label, *play_button, *play_text;
static lv_obj_t *players_list, *providers_list;
static volatile bool players_dirty = true;
static int current_volume = 0;
static bool current_playing, current_paused, current_available;
static bool current_sonos;
static esp_websocket_client_handle_t socket_handle;
static bool ready;
static volatile bool ip_ready;
static unsigned request_id;
static const char *TAG = "controller";
static volatile bool screen_dirty = true;
static volatile bool refresh_pending;
static volatile bool ws_connected;
#define WS_MAX_MESSAGE (128 * 1024)
static char *ws_buffer;
static size_t ws_expected, ws_received;
static void reset_ws_message(void) {
    free(ws_buffer);
    ws_buffer = NULL;
    ws_expected = ws_received = 0;
}

static void build_players_view(void);
static void build_providers_view(void);
static void update_now_playing(void);
static void render(void) {
    if (!now_screen || !lvgl_port_lock(pdMS_TO_TICKS(250))) return;
    if (state_mutex && xSemaphoreTake(state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        update_now_playing();
        if (players_dirty) { build_players_view(); players_dirty = false; }
        if (providers_dirty) { build_providers_view(); providers_dirty = false; }
        xSemaphoreGive(state_mutex);
    }
    lvgl_port_unlock();
}
static void status(const char *s) {
    snprintf(state, sizeof(state), "%s", s);
    ESP_LOGI(TAG, "STATE: %s", state);
    screen_dirty = true;
}
static void field(char *out, size_t cap, const cJSON *obj, const char *key) {
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsString(v)) snprintf(out, cap, "%s", v->valuestring);
}
static void request(const char *command, char type) {
    if (!ready || !esp_websocket_client_is_connected(socket_handle)) return;
    char payload[180];
    snprintf(payload, sizeof(payload),
             "{\"message_id\":\"%c%u\",\"command\":\"%s\",\"args\":{}}",
             type, ++request_id, command);
    ESP_LOGI(TAG, "MA request %c: %s", type, command);
    int sent = esp_websocket_client_send_text(socket_handle, payload, strlen(payload), pdMS_TO_TICKS(1000));
    if (sent < 0) ESP_LOGE(TAG, "Failed to send MA request %c", type);
}

static void command_with_args(const char *command, const char *id, int volume, bool with_volume) {
    if (!ready || !esp_websocket_client_is_connected(socket_handle) || !id[0]) return;
    cJSON *message = cJSON_CreateObject();
    cJSON *args = cJSON_CreateObject();
    if (!message || !args) { cJSON_Delete(message); cJSON_Delete(args); return; }
    char message_id[24];
    snprintf(message_id, sizeof(message_id), "c%u", ++request_id);
    cJSON_AddStringToObject(message, "message_id", message_id);
    cJSON_AddStringToObject(message, "command", command);
    cJSON_AddStringToObject(args, "player_id", id);
    if (with_volume) cJSON_AddNumberToObject(args, "volume_level", volume);
    cJSON_AddItemToObject(message, "args", args);
    char *payload = cJSON_PrintUnformatted(message);
    if (payload) {
        ESP_LOGI(TAG, "Sending player command: %s", command);
        esp_websocket_client_send_text(socket_handle, payload, strlen(payload), pdMS_TO_TICKS(1000));
        free(payload);
    }
    cJSON_Delete(message);
}
static void save_preferred_player(const char *id) {
    nvs_handle_t n;
    if (nvs_open("controller", NVS_READWRITE, &n) != ESP_OK) {
        status("Unable to save player");
        return;
    }
    esp_err_t err = nvs_set_str(n, "player_id", id);
    if (err == ESP_OK) err = nvs_commit(n);
    nvs_close(n);
    if (err != ESP_OK) status("Unable to save player");
    else ESP_LOGI(TAG, "Player selection saved to NVS");
}
static void process_action(const action_t *action) {
    if (action->type == ACT_PROVIDER_TOGGLE) {
        if (xSemaphoreTake(state_mutex, pdMS_TO_TICKS(250)) != pdTRUE) return;
        bool found = false;
        for (size_t i = 0; i < music_provider_count; ++i) {
            if (!strcmp(music_providers[i].id, action->player_id)) {
                music_providers[i].selected = action->selected;
                found = true;
                break;
            }
        }
        xSemaphoreGive(state_mutex);
        if (found) save_music_providers();
        return;
    }
    if (action->type == ACT_SELECT) {
        if (xSemaphoreTake(state_mutex, pdMS_TO_TICKS(250)) != pdTRUE) return;
        bool found = false;
        for (size_t i = 0; i < player_count; ++i)
            if (!strcmp(players[i].id, action->player_id)) found = true;
        if (found) {
            snprintf(preferred, sizeof(preferred), "%s", action->player_id);
            snprintf(player_id, sizeof(player_id), "%s", action->player_id);
            snprintf(track, sizeof(track), "Loading player...");
            artist[0] = 0;
            players_dirty = true;
            screen_dirty = true;
        }
        xSemaphoreGive(state_mutex);
        if (found) { save_preferred_player(action->player_id); refresh_pending = true; }
        return;
    }
    if (!ready || !current_available || !player_id[0]) {
        status("Player unavailable");
        return;
    }
    const char *command = NULL;
    int volume = current_volume;
    bool volume_command = false;
    switch (action->type) {
        case ACT_PLAY_PAUSE:
            if (current_playing && current_sonos) {
                status("Use Stop on Sonos");
                return;
            }
            command = current_playing ? "players/cmd/pause" : "players/cmd/play";
            break;
        case ACT_STOP: command = "players/cmd/stop"; break;
        case ACT_PREVIOUS: command = "players/cmd/previous"; break;
        case ACT_NEXT: command = "players/cmd/next"; break;
        case ACT_VOLUME_DOWN:
            volume = volume <= 0 ? 0 : ((volume - 1) / 5) * 5;
            command = "players/cmd/volume_set"; volume_command = true; break;
        case ACT_VOLUME_UP:
            volume = volume >= 100 ? 100 : ((volume / 5) + 1) * 5;
            command = "players/cmd/volume_set"; volume_command = true; break;
        default: return;
    }
    command_with_args(command, player_id, volume, volume_command);
    refresh_pending = true;
}

static void refresh(void) {
    request("players/all", 'p');
    request("player_queues/all", 'q');
}
static void players_received(const cJSON *array) {
    if (!cJSON_IsArray(array) || !state_mutex) return;
    if (xSemaphoreTake(state_mutex, pdMS_TO_TICKS(250)) != pdTRUE) return;
    player_count = 0;
    const cJSON *p;
    cJSON_ArrayForEach(p, array) {
        if (player_count >= MAX_PLAYERS) break;
        const cJSON *id = cJSON_GetObjectItemCaseSensitive(p, "player_id");
        if (!cJSON_IsString(id) || !id->valuestring[0]) continue;
        player_info_t *info = &players[player_count++];
        memset(info, 0, sizeof(*info));
        field(info->id, sizeof(info->id), p, "player_id");
        field(info->name, sizeof(info->name), p, "name");
        field(info->provider, sizeof(info->provider), p, "provider");
        const cJSON *volume = cJSON_GetObjectItemCaseSensitive(p, "volume_level");
        info->volume = cJSON_IsNumber(volume) ? volume->valueint : 0;
        const cJSON *play_state = cJSON_GetObjectItemCaseSensitive(p, "state");
        info->playing = cJSON_IsString(play_state) && strcmp(play_state->valuestring, "playing") == 0;
        info->paused = cJSON_IsString(play_state) && strcmp(play_state->valuestring, "paused") == 0;
        const cJSON *available = cJSON_GetObjectItemCaseSensitive(p, "available");
        info->available = !cJSON_IsFalse(available);
    }
    const player_info_t *chosen = NULL;
    for (size_t i = 0; i < player_count; ++i) {
        if (!strcmp(players[i].id, preferred)) { chosen = &players[i]; break; }
    }
    if (!chosen && player_count) chosen = &players[0];
    bool selection_changed = false;
    if (chosen) {
        bool changed = strcmp(player_id, chosen->id) != 0;
        selection_changed = changed;
        snprintf(player_id, sizeof(player_id), "%s", chosen->id);
        snprintf(player_name, sizeof(player_name), "%s", chosen->name);
        current_volume = chosen->volume;
        current_playing = chosen->playing;
        current_paused = chosen->paused;
        current_available = chosen->available;
        current_sonos = strstr(chosen->provider, "sonos") != NULL ||
                        strstr(chosen->provider, "SONOS") != NULL;
        if (changed) { snprintf(track, sizeof(track), "Loading player..."); artist[0] = 0; }
    } else {
        player_id[0] = 0;
        snprintf(player_name, sizeof(player_name), "No players");
        current_available = false;
    }
    players_dirty = true;
    screen_dirty = true;
    xSemaphoreGive(state_mutex);
    if (selection_changed) refresh_pending = true;
}

static bool provider_is_selected(const char *id) {
    if (!provider_selection_saved) return true;
    char needle[100];
    snprintf(needle, sizeof(needle), "|%s|", id);
    return strstr(selected_provider_ids, needle) != NULL;
}
static void save_music_providers(void) {
    char value[PROVIDER_SELECTION_SIZE] = "";
    for (size_t i = 0; i < music_provider_count; ++i) {
        if (!music_providers[i].selected) continue;
        size_t remaining = sizeof(value) - strlen(value);
        if (remaining <= strlen(music_providers[i].id) + 2) {
            status("Provider selection too large");
            return;
        }
        strcat(value, "|");
        strcat(value, music_providers[i].id);
        strcat(value, "|");
    }
    nvs_handle_t n;
    if (nvs_open("controller", NVS_READWRITE, &n) != ESP_OK) {
        status("Cannot save providers");
        return;
    }
    esp_err_t err = nvs_set_str(n, "music_sources", value);
    if (err == ESP_OK) err = nvs_commit(n);
    nvs_close(n);
    if (err != ESP_OK) { status("Cannot save providers"); return; }
    snprintf(selected_provider_ids, sizeof(selected_provider_ids), "%s", value);
    provider_selection_saved = true;
    ESP_LOGI(TAG, "Music provider selection saved (%u bytes)", (unsigned)strlen(value));
}
static void providers_received(const cJSON *array) {
    if (!cJSON_IsArray(array) || !state_mutex) {
        status("Provider response unavailable");
        return;
    }
    if (xSemaphoreTake(state_mutex, pdMS_TO_TICKS(250)) != pdTRUE) return;
    music_provider_count = 0;
    const cJSON *item;
    cJSON_ArrayForEach(item, array) {
        if (music_provider_count >= MAX_PROVIDERS) break;
        const cJSON *type = cJSON_GetObjectItemCaseSensitive(item, "type");
        if (!cJSON_IsString(type) || strcmp(type->valuestring, "music")) continue;
        const cJSON *available = cJSON_GetObjectItemCaseSensitive(item, "available");
        if (cJSON_IsFalse(available)) continue;
        music_provider_t *provider = &music_providers[music_provider_count];
        memset(provider, 0, sizeof(*provider));
        field(provider->id, sizeof(provider->id), item, "instance_id");
        if (!provider->id[0]) continue;
        field(provider->name, sizeof(provider->name), item, "name");
        if (!provider->name[0]) snprintf(provider->name, sizeof(provider->name), "%s", provider->id);
        provider->selected = provider_is_selected(provider->id);
        music_provider_count++;
    }
    providers_dirty = true;
    xSemaphoreGive(state_mutex);
    ESP_LOGI(TAG, "Music providers discovered: %u", (unsigned)music_provider_count);
}

static void queues_received(const cJSON *array) {
    if (!cJSON_IsArray(array) || !state_mutex) return;
    if (xSemaphoreTake(state_mutex, pdMS_TO_TICKS(250)) != pdTRUE) return;
    const cJSON *queue;
    cJSON_ArrayForEach(queue, array) {
        const cJSON *id = cJSON_GetObjectItemCaseSensitive(queue, "queue_id");
        if (!cJSON_IsString(id) || strcmp(id->valuestring, player_id)) continue;
        const cJSON *item = cJSON_GetObjectItemCaseSensitive(queue, "current_item");
        const cJSON *media = cJSON_GetObjectItemCaseSensitive(item, "media_item");
        if (!cJSON_IsObject(media)) media = item;
        snprintf(track, sizeof(track), "Nothing playing");
        artist[0] = 0;
        if (cJSON_IsObject(media)) {
            field(track, sizeof(track), media, "name");
            const cJSON *artists = cJSON_GetObjectItemCaseSensitive(media, "artists");
            const cJSON *first = cJSON_GetArrayItem(artists, 0);
            if (first) field(artist, sizeof(artist), first, "name");
        }
        screen_dirty = true;
        xSemaphoreGive(state_mutex);
        return;
    }
    xSemaphoreGive(state_mutex);
}
static void handle_message(const char *payload, size_t len) {
    cJSON *msg = cJSON_ParseWithLength(payload, len);
    if (!msg) { ESP_LOGW(TAG, "Invalid JSON response (%u bytes)", (unsigned)len); return; }
    const cJSON *event = cJSON_GetObjectItemCaseSensitive(msg, "event");
    if (cJSON_IsString(event)) {
        ESP_LOGI(TAG, "Gateway event: %s", event->valuestring);
        if (!strcmp(event->valuestring, "gateway/ready")) {
            ready = true; status("Connected"); refresh_pending = true;
            provider_fetch_pending = true;
        } else if (!strcmp(event->valuestring, "gateway/error")) {
            ready = false; status("Gateway authentication failed");
        } else if (strstr(event->valuestring, "player") || strstr(event->valuestring, "queue")) {
            refresh_pending = true;
        }
    }
    const cJSON *mid = cJSON_GetObjectItemCaseSensitive(msg, "message_id");
    const cJSON *result = cJSON_GetObjectItemCaseSensitive(msg, "result");
    const cJSON *error = cJSON_GetObjectItemCaseSensitive(msg, "error");
    if (cJSON_IsString(mid)) {
        if (error && !cJSON_IsNull(error) && !cJSON_IsFalse(error)) {
            ESP_LOGE(TAG, "MA response %c returned error", mid->valuestring[0]);
            status("Music Assistant API error");
        } else if (cJSON_IsArray(result)) {
            ESP_LOGI(TAG, "MA response %c: %d items", mid->valuestring[0], cJSON_GetArraySize(result));
            if (mid->valuestring[0] == 'm') providers_received(result);
            else if (mid->valuestring[0] == 'p') players_received(result);
            else if (mid->valuestring[0] == 'q') queues_received(result);
        } else ESP_LOGW(TAG, "MA response %c: unexpected shape", mid->valuestring[0]);
    }
    cJSON_Delete(msg);
}
static void ws_event(void *arg, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_websocket_event_data_t *ev = event_data;
    if (event_id == WEBSOCKET_EVENT_DISCONNECTED) {
        reset_ws_message();
        ws_connected = false; ready = false;
        status("Gateway reconnecting...");
        ESP_LOGW(TAG, "WebSocket disconnected");
    } else if (event_id == WEBSOCKET_EVENT_CONNECTED) {
        reset_ws_message();
        ws_connected = true;
        ESP_LOGI(TAG, "WebSocket handshake completed");
        status("Authenticating...");
    } else if (event_id == WEBSOCKET_EVENT_ERROR) {
        reset_ws_message();
        ESP_LOGE(TAG, "WebSocket error; inspect transport_ws status");
        status("WebSocket error");
    } else if (event_id == WEBSOCKET_EVENT_DATA && ev) {
        ESP_LOGI(TAG, "WS frame: opcode=%d offset=%d length=%d total=%d",
                 ev->op_code, ev->payload_offset, ev->data_len, ev->payload_len);
        if (ev->payload_offset == 0) {
            reset_ws_message();
            if (ev->op_code != 1 || ev->payload_len <= 0 || ev->payload_len > WS_MAX_MESSAGE) {
                ESP_LOGW(TAG, "Dropping unsupported or oversized WS message");
                return;
            }
            ws_expected = ev->payload_len;
            ws_buffer = heap_caps_malloc(ws_expected + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!ws_buffer) {
                ESP_LOGE(TAG, "PSRAM allocation failed for WS response");
                reset_ws_message();
                return;
            }
        }
        if (!ws_buffer || ev->data_len < 0 || ev->payload_offset < 0 ||
            (size_t)ev->payload_offset != ws_received ||
            (size_t)ev->data_len > ws_expected - ws_received) {
            ESP_LOGW(TAG, "Invalid or out-of-order WS fragment; discarding message");
            reset_ws_message();
            return;
        }
        memcpy(ws_buffer + ws_received, ev->data_ptr, ev->data_len);
        ws_received += ev->data_len;
        if (ws_received == ws_expected) {
            ws_buffer[ws_received] = 0;
            ESP_LOGI(TAG, "WS message assembled: %u bytes", (unsigned)ws_received);
            handle_message(ws_buffer, ws_received);
            reset_ws_message();
        }
    }
}
static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "Wi-Fi starting association");
        esp_wifi_connect();
    }
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *info = data;
        ESP_LOGW(TAG, "Wi-Fi disconnect reason=%d", info ? info->reason : -1);
        ready = false; ip_ready = false; status("Wi-Fi reconnecting"); esp_wifi_connect();
    }
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ip = data;
        ESP_LOGI(TAG, "DHCP address " IPSTR, IP2STR(&ip->ip_info.ip));
        ip_ready = true;
        status("Wi-Fi connected; syncing clock");
    }
}
static bool load_config(void) {
    nvs_handle_t n;
    if (nvs_open("controller", NVS_READONLY, &n) != ESP_OK) return false;
    size_t len;
#define READ(k,v) do {len=sizeof(v);if(nvs_get_str(n,k,v,&len)!=ESP_OK){nvs_close(n);return false;}}while(0)
    READ("wifi_ssid",ssid); READ("wifi_pass",password);
    READ("device_token",token); READ("gateway_url",url);
#undef READ
    len=sizeof(preferred);
    nvs_get_str(n,"player_id",preferred,&len);
    len = sizeof(selected_provider_ids);
    if (nvs_get_str(n, "music_sources", selected_provider_ids, &len) == ESP_OK)
        provider_selection_saved = true;
    nvs_close(n);
    return ssid[0] && token[0] && !strncmp(url,"wss://",6);
}
static void enqueue(action_type_t type, const char *id) {
    if (!action_queue) return;
    action_t action = {.type = type};
    if (id) snprintf(action.player_id, sizeof(action.player_id), "%s", id);
    if (xQueueSend(action_queue, &action, 0) != pdTRUE) ESP_LOGW(TAG, "Action queue full");
}
static void control_clicked(lv_event_t *event) {
    action_type_t type = (action_type_t)(intptr_t)lv_event_get_user_data(event);
    enqueue(type, NULL);
}
static void player_clicked(lv_event_t *event) {
    const char *id = lv_event_get_user_data(event);
    if (!id) return;
    enqueue(ACT_SELECT, id);
    active_view = VIEW_NOW_PLAYING;
    lv_scr_load(now_screen);
}
static void open_players(lv_event_t *event) {
    active_view = VIEW_PLAYERS;
    lv_scr_load(players_screen);
}
static void back_clicked(lv_event_t *event) {
    active_view = VIEW_NOW_PLAYING;
    lv_scr_load(now_screen);
}
static lv_obj_t *make_button(lv_obj_t *parent, const char *label,
                             lv_coord_t x, lv_coord_t y, lv_coord_t w,
                             action_type_t action) {
    lv_obj_t *button = lv_btn_create(parent);
    lv_obj_set_size(button, w, 60);
    lv_obj_align(button, LV_ALIGN_TOP_LEFT, x, y);
    lv_obj_add_event_cb(button, control_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)action);
    lv_obj_t *text = lv_label_create(button);
    lv_label_set_text(text, label);
    lv_obj_center(text);
    return button;
}
static void update_now_playing(void) {
    lv_label_set_text(title_label, track);
    lv_label_set_text(artist_label, artist);
    lv_label_set_text(player_label, player_name);
    lv_label_set_text(state_label, state);
    char volume_text[40];
    snprintf(volume_text, sizeof(volume_text), "Volume %d%%", current_volume);
    lv_label_set_text(volume_label, volume_text);
    lv_label_set_text(play_text, current_playing ? "Pause" : "Play");
    // Sonos pause currently has a known queue-hopping issue. Use Stop instead.
    if (!ready || !current_available || (current_sonos && current_playing))
        lv_obj_add_state(play_button, LV_STATE_DISABLED);
    else lv_obj_clear_state(play_button, LV_STATE_DISABLED);
}
static void build_players_view(void) {
    if (!players_list) return;
    lv_obj_clean(players_list);
    if (!player_count) {
        lv_obj_t *label = lv_label_create(players_list);
        lv_label_set_text(label, "No players available");
        return;
    }
    for (size_t i = 0; i < player_count; ++i) {
        lv_obj_t *button = lv_list_add_btn(players_list, NULL, players[i].name);
        lv_obj_set_height(button, 56);
        if (!strcmp(players[i].id, player_id))
            lv_obj_set_style_bg_color(button, lv_color_hex(0x315f91), 0);
        lv_obj_add_event_cb(button, player_clicked, LV_EVENT_CLICKED, players[i].id);
    }
}

static void provider_toggled(lv_event_t *event) {
    lv_obj_t *checkbox = lv_event_get_target(event);
    const char *id = lv_event_get_user_data(event);
    if (!id) return;
    action_t action = {.type = ACT_PROVIDER_TOGGLE};
    snprintf(action.player_id, sizeof(action.player_id), "%s", id);
    action.selected = lv_obj_has_state(checkbox, LV_STATE_CHECKED);
    if (xQueueSend(action_queue, &action, 0) != pdTRUE)
        ESP_LOGW(TAG, "Provider selection queue full");
}
static void open_providers(lv_event_t *event) {
    active_view = VIEW_PROVIDERS;
    lv_scr_load(providers_screen);
}
static void build_providers_view(void) {
    if (!providers_list) return;
    lv_obj_clean(providers_list);
    if (!music_provider_count) {
        lv_obj_t *label = lv_label_create(providers_list);
        lv_label_set_text(label, "No music providers available");
        return;
    }
    for (size_t i = 0; i < music_provider_count; ++i) {
        lv_obj_t *checkbox = lv_checkbox_create(providers_list);
        lv_checkbox_set_text(checkbox, music_providers[i].name);
        lv_obj_set_width(checkbox, 690);
        lv_obj_set_height(checkbox, 58);
        if (music_providers[i].selected) lv_obj_add_state(checkbox, LV_STATE_CHECKED);
        lv_obj_add_event_cb(checkbox, provider_toggled, LV_EVENT_VALUE_CHANGED, music_providers[i].id);
    }
}

static void init_screen(void) {
    now_screen = lv_scr_act();
    lv_obj_set_style_bg_color(now_screen, lv_color_hex(0x121c30), 0);
    lv_obj_set_style_bg_opa(now_screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(now_screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *heading = lv_label_create(now_screen);
    lv_label_set_text(heading, "NOW PLAYING");
    lv_obj_set_style_text_color(heading, lv_color_hex(0xa8c3e3), 0);
    lv_obj_align(heading, LV_ALIGN_TOP_MID, 0, 82);

    lv_obj_t *player_button = lv_btn_create(now_screen);
    lv_obj_set_size(player_button, 260, 58);
    lv_obj_align(player_button, LV_ALIGN_TOP_RIGHT, -18, 8);
    lv_obj_add_event_cb(player_button, open_players, LV_EVENT_CLICKED, NULL);
    player_label = lv_label_create(player_button);
    lv_label_set_text(player_label, player_name);
    lv_label_set_long_mode(player_label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(player_label, 225);
    lv_obj_center(player_label);

    title_label = lv_label_create(now_screen);
    lv_obj_set_width(title_label, 750);
    lv_label_set_long_mode(title_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(title_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title_label, lv_color_white(), 0);
    lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 136);

    artist_label = lv_label_create(now_screen);
    lv_obj_set_width(artist_label, 740);
    lv_obj_set_style_text_align(artist_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(artist_label, lv_color_hex(0xc0d1e8), 0);
    lv_obj_align(artist_label, LV_ALIGN_TOP_MID, 0, 181);

    lv_obj_t *providers_button = lv_btn_create(now_screen);
    lv_obj_set_size(providers_button, 170, 58);
    lv_obj_align(providers_button, LV_ALIGN_TOP_LEFT, 18, 8);
    lv_obj_add_event_cb(providers_button, open_providers, LV_EVENT_CLICKED, NULL);
    lv_obj_t *providers_text = lv_label_create(providers_button);
    lv_label_set_text(providers_text, "Providers");
    lv_obj_center(providers_text);
    make_button(now_screen, "Prev", 116, 222, 125, ACT_PREVIOUS);
    play_button = make_button(now_screen, "Play", 250, 222, 125, ACT_PLAY_PAUSE);
    play_text = lv_obj_get_child(play_button, 0);
    make_button(now_screen, "Next", 384, 222, 125, ACT_NEXT);
    make_button(now_screen, "Stop", 518, 222, 125, ACT_STOP);

    make_button(now_screen, "-", 205, 313, 90, ACT_VOLUME_DOWN);
    volume_label = lv_label_create(now_screen);
    lv_obj_set_width(volume_label, 190);
    lv_obj_set_style_text_align(volume_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(volume_label, LV_ALIGN_TOP_MID, 0, 330);
    make_button(now_screen, "+", 505, 313, 90, ACT_VOLUME_UP);

    state_label = lv_label_create(now_screen);
    lv_obj_set_style_text_color(state_label, lv_color_hex(0x8fe3a0), 0);
    lv_obj_align(state_label, LV_ALIGN_BOTTOM_MID, 0, -20);

    players_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(players_screen, lv_color_hex(0x121c30), 0);
    lv_obj_set_style_bg_opa(players_screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(players_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *players_title = lv_label_create(players_screen);
    lv_label_set_text(players_title, "SELECT PLAYER");
    lv_obj_set_style_text_color(players_title, lv_color_white(), 0);
    lv_obj_set_style_text_font(players_title, &lv_font_montserrat_24, 0);
    lv_obj_align(players_title, LV_ALIGN_TOP_LEFT, 20, 24);
    lv_obj_t *back = lv_btn_create(players_screen);
    lv_obj_set_size(back, 190, 58);
    lv_obj_align(back, LV_ALIGN_TOP_RIGHT, -18, 10);
    lv_obj_add_event_cb(back, back_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back_text = lv_label_create(back);
    lv_label_set_text(back_text, "Back");
    lv_obj_center(back_text);
    players_list = lv_list_create(players_screen);
    lv_obj_set_size(players_list, 760, 365);
    lv_obj_align(players_list, LV_ALIGN_BOTTOM_MID, 0, -10);
    providers_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(providers_screen, lv_color_hex(0x121c30), 0);
    lv_obj_set_style_bg_opa(providers_screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(providers_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *provider_title = lv_label_create(providers_screen);
    lv_label_set_text(provider_title, "MUSIC PROVIDERS");
    lv_obj_set_style_text_color(provider_title, lv_color_white(), 0);
    lv_obj_set_style_text_font(provider_title, &lv_font_montserrat_24, 0);
    lv_obj_align(provider_title, LV_ALIGN_TOP_LEFT, 20, 24);
    lv_obj_t *provider_back = lv_btn_create(providers_screen);
    lv_obj_set_size(provider_back, 190, 58);
    lv_obj_align(provider_back, LV_ALIGN_TOP_RIGHT, -18, 10);
    lv_obj_add_event_cb(provider_back, back_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *provider_back_text = lv_label_create(provider_back);
    lv_label_set_text(provider_back_text, "Back");
    lv_obj_center(provider_back_text);
    lv_obj_t *hint = lv_label_create(providers_screen);
    lv_label_set_text(hint, "Choose sources for future Browse and Search");
    lv_obj_set_style_text_color(hint, lv_color_hex(0xa8c3e3), 0);
    lv_obj_align(hint, LV_ALIGN_TOP_LEFT, 20, 86);
    providers_list = lv_obj_create(providers_screen);
    lv_obj_set_size(providers_list, 760, 330);
    lv_obj_align(providers_list, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_set_flex_flow(providers_list, LV_FLEX_FLOW_COLUMN);
    update_now_playing();
}
void app_main(void) {
    esp_lcd_panel_handle_t panel = NULL;
    esp_lcd_touch_handle_t touch = NULL;
    ESP_ERROR_CHECK(waveshare_esp32_s3_rgb_lcd_init(&panel, &touch));
    ESP_ERROR_CHECK(lvgl_port_init(panel, touch));
    ESP_ERROR_CHECK(waveshare_rgb_lcd_bl_on());
    ESP_LOGI(TAG, "Creating diagnostic LVGL screen");
    if (lvgl_port_lock(pdMS_TO_TICKS(2000))) {
        init_screen();
        lv_obj_invalidate(lv_scr_act());
        lvgl_port_unlock();
        ESP_LOGI(TAG, "Diagnostic screen created");
    } else ESP_LOGE(TAG, "Failed to acquire LVGL lock");
    state_mutex = xSemaphoreCreateMutex();
    action_queue = xQueueCreate(12, sizeof(action_t));
    ESP_ERROR_CHECK(state_mutex && action_queue ? ESP_OK : ESP_ERR_NO_MEM);
    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) { status("NVS initialisation failed"); return; }
    if (!load_config()) { status("Provision device NVS first"); return; }
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event, NULL));
    wifi_config_t cfg = {0};
    memcpy(cfg.sta.ssid, ssid, strlen(ssid));
    memcpy(cfg.sta.password, password, strlen(password));
    cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));
    esp_websocket_client_config_t ws_cfg = {
        .uri = url, .crt_bundle_attach = esp_crt_bundle_attach,
        .reconnect_timeout_ms = 3000,
    };
    char headers[170];
    snprintf(headers, sizeof(headers), "X-Device-Token: %s\r\n", token);
    ws_cfg.headers = headers;
    socket_handle = esp_websocket_client_init(&ws_cfg);
    ESP_ERROR_CHECK(esp_websocket_register_events(socket_handle, WEBSOCKET_EVENT_ANY, ws_event, NULL));
    esp_sntp_config_t time_config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    ESP_ERROR_CHECK(esp_netif_sntp_init(&time_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI("controller", "LCD ready; waiting for Wi-Fi and authenticated gateway");
    bool started = false;
    int64_t last_refresh = 0;
    int64_t last_heartbeat = 0;
    while (true) {
        if (ip_ready && !started) {
            if (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(15000)) == ESP_OK) {
                status("Clock synced; connecting...");
                ESP_LOGI(TAG, "Starting WSS connection");
                esp_websocket_client_start(socket_handle);
                started = true;
            } else {
                status("Waiting for network time...");
            }
        }
        if (ready && provider_fetch_pending) {
            provider_fetch_pending = false;
            request("providers", 'm');
        }
        action_t action;
        while (xQueueReceive(action_queue, &action, 0) == pdTRUE) process_action(&action);
        if (screen_dirty || players_dirty || providers_dirty) { render(); screen_dirty = false; }
        int64_t now = esp_timer_get_time();
        if (ready && (refresh_pending || now - last_refresh >= 30000000LL)) {
            refresh_pending = false;
            last_refresh = now;
            refresh();
        }
        if (now - last_heartbeat >= 5000000LL) {
            ESP_LOGI(TAG, "Heartbeat: ip=%d ws=%d gateway=%d view=%d",
                     (int)ip_ready, (int)ws_connected, (int)ready, (int)active_view);
            last_heartbeat = now;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
