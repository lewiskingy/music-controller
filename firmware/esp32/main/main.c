#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
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
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_websocket_client.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "cJSON.h"
#include "esp_lcd_panel_ops.h"
#include "bsp/board.h"
#include "bsp/lvgl_port.h"
#include "lvgl.h"
#include "controller_ui.h"
#include "playback_policy.h"

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
    bool can_volume;
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
typedef enum { VIEW_NOW_PLAYING, VIEW_PLAYERS, VIEW_PROVIDERS, VIEW_QUEUE, VIEW_BROWSE } view_t;
typedef enum { ACT_SELECT, ACT_PLAY_STOP, ACT_PREVIOUS, ACT_NEXT, ACT_VOLUME_DOWN, ACT_VOLUME_UP, ACT_PROVIDER_TOGGLE, ACT_PROVIDER_SAVE, ACT_VOLUME_SET, ACT_SEEK, ACT_QUEUE_PLAY, ACT_BROWSE_PLAY } action_type_t;
typedef struct { action_type_t type; char player_id[129]; bool selected; bool playing; int volume; int position; char queue_id[129], item_id[129], media_uri[513]; } action_t;
static view_t active_view = VIEW_NOW_PLAYING;
static controller_ui_t ui;
static lv_obj_t *now_screen;
static lv_obj_t *players_list, *providers_list, *provider_save_status;
static volatile bool players_dirty = true;
static int current_volume = 0;
static bool current_playing, current_paused, current_available;
typedef struct { bool pending; char id[24]; int64_t started; } pending_command_t;
static pending_command_t commands[PLAYBACK_GROUPS];
static action_t deferred_volume;
static bool volume_deferred;
static bool current_can_volume, gateway_can_queue, gateway_can_browse, gateway_can_seek, queue_active, queue_live, queue_allow_seek;
static char current_queue_id[129], current_item_id[129], album[160];
static char current_art_id[65];
static bool gateway_can_artwork;
static unsigned char *art_result;
static char art_result_id[65];
static ui_queue_item_t queue_items[UI_QUEUE_PAGE_SIZE];
static unsigned queue_count;
static int queue_offset, queue_total, current_queue_index;
static bool queue_loading, queue_requested, queue_dirty=true;
static char queue_request_id[24], queue_request_player[129], queue_request_queue[129];
static char queue_message[96]="Open Queue to load tracks";
static int queue_request_offset;
static int64_t queue_request_started;
static void reset_queue(void) {
    queue_count=0; queue_offset=0; queue_total=0;
    queue_loading=false; queue_request_id[0]=0; queue_requested=true; queue_dirty=true;
    snprintf(queue_message,sizeof(queue_message),"Loading queue...");
}
enum { BROWSE_ROOT, BROWSE_SOURCE, BROWSE_ALBUMS, BROWSE_ARTISTS, BROWSE_PLAYLISTS,
       BROWSE_ARTIST_ALBUMS, BROWSE_ALBUM_TRACKS, BROWSE_PLAYLIST_TRACKS, BROWSE_FOLDERS };
static const char *browse_kind_names[]={"root","source","albums","artists","playlists","artist_albums","album_tracks","playlist_tracks","folders"};
typedef struct { int kind, offset; char id[129],provider[96],source[96],title[160],uri[513]; } browse_context_t;
static browse_context_t browse_stack[8];
static unsigned browse_depth, browse_generation, browse_count;
static ui_media_item_t browse_items[UI_QUEUE_PAGE_SIZE];
static bool browse_requested, browse_loading, browse_dirty=true, browse_more;
static char browse_request_id[24], browse_message[96]="Choose a music source";
static unsigned browse_request_generation;
static int64_t browse_request_started;
static void reset_browse(void) {
    browse_depth=0; browse_generation++; browse_count=0; browse_loading=false;
    browse_request_id[0]=0; browse_requested=false; browse_dirty=true; browse_more=false;
    memset(browse_stack,0,sizeof(browse_stack));
    snprintf(browse_message,sizeof(browse_message),"Choose a music source");
}
static void browse_changed(void) {
    browse_generation++; browse_count=0; browse_loading=false; browse_more=false;
    browse_request_id[0]=0; browse_requested=true; browse_dirty=true;
}
static int track_duration;
static double track_position, track_speed = 1.0;
static bool queue_playing;
static int64_t position_sample_time;
static int action_group(action_type_t type) {
    if (type == ACT_SEEK) return PLAYBACK_SEEK;
    if (type == ACT_VOLUME_SET || type == ACT_VOLUME_UP || type == ACT_VOLUME_DOWN) return PLAYBACK_VOLUME;
    return PLAYBACK_TRANSPORT;
}
static void clear_timeline(void) {
    current_queue_id[0] = current_item_id[0] = album[0] = 0;
    current_art_id[0] = 0;
    track_duration = 0; track_position = 0;
    queue_active = queue_live = queue_allow_seek = queue_playing = false;
}
static bool can_seek(void) {
    return playback_seek_allowed(track_duration, queue_live, queue_active, queue_allow_seek,
                                 gateway_can_seek, current_queue_id[0] && current_item_id[0]);
}
static char provider_feedback[96] = "Tap checkboxes, then Save";
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

static void save_music_providers(void);
static void providers_received(const cJSON *array);
static void build_players_view(void);
static void build_providers_view(void);
static void update_now_playing(void);
static void build_queue_view(void);
static void build_browse_view(void);
static bool render(void) {
    if (!now_screen || !lvgl_port_lock(250)) return false;
    bool rendered = false;
    if (state_mutex && xSemaphoreTake(state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        update_now_playing();
        if (players_dirty) { build_players_view(); players_dirty = false; }
        if (providers_dirty) { build_providers_view(); providers_dirty = false; }
        if (queue_dirty) { build_queue_view(); queue_dirty=false; }
        if (browse_dirty) { build_browse_view(); browse_dirty=false; }
        xSemaphoreGive(state_mutex);
        rendered = true;
    }
    lvgl_port_unlock();
    return rendered;
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

static bool command_with_args(const char *command, const char *id, int volume, bool with_volume, int group, int seek_position, const char *queue_item, const char *media_uri) {
    if (!ready || !esp_websocket_client_is_connected(socket_handle) || !id[0]) return false;
    cJSON *message = cJSON_CreateObject();
    cJSON *args = cJSON_CreateObject();
    if (!message || !args) { cJSON_Delete(message); cJSON_Delete(args); return false; }
    char message_id[24];
    snprintf(message_id, sizeof(message_id), "c%u", ++request_id);
    cJSON_AddStringToObject(message, "message_id", message_id);
    cJSON_AddStringToObject(message, "command", command);
    cJSON_AddStringToObject(args, media_uri || queue_item || group == PLAYBACK_SEEK ? "queue_id" : "player_id", id);
    if(queue_item) cJSON_AddStringToObject(args,"index",queue_item);
    if(media_uri) { cJSON_AddStringToObject(args,"media",media_uri); cJSON_AddStringToObject(args,"option","replace"); }
    if (group == PLAYBACK_SEEK) cJSON_AddNumberToObject(args, "position", seek_position);
    if (with_volume) cJSON_AddNumberToObject(args, "volume_level", volume);
    cJSON_AddItemToObject(message, "args", args);
    char *payload = cJSON_PrintUnformatted(message);
    bool sent = false;
    if (payload) {
        if (xSemaphoreTake(state_mutex, pdMS_TO_TICKS(250)) != pdTRUE) {
            free(payload); cJSON_Delete(message); return false;
        }
        snprintf(commands[group].id, sizeof(commands[group].id), "%s", message_id);
        xSemaphoreGive(state_mutex);
        ESP_LOGI(TAG, "Sending player command: %s", command);
        sent = esp_websocket_client_send_text(socket_handle, payload, strlen(payload), pdMS_TO_TICKS(1000)) == (int)strlen(payload);
        free(payload);
    }
    cJSON_Delete(message);
    return sent;
}
static void finish_command(int group) {
    if (xSemaphoreTake(state_mutex, pdMS_TO_TICKS(250)) != pdTRUE) return;
    commands[group].pending = false;
    commands[group].id[0] = 0;
    screen_dirty = true;
    xSemaphoreGive(state_mutex);
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
    if (action->type == ACT_PROVIDER_SAVE) {
        if (xSemaphoreTake(state_mutex, pdMS_TO_TICKS(250)) != pdTRUE) return;
        save_music_providers();
        reset_browse();
        screen_dirty = true;
        xSemaphoreGive(state_mutex);
        return;
    }
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
        if (found) ESP_LOGI(TAG, "Provider selection changed; awaiting Save");
        return;
    }
    if (action->type == ACT_SELECT) {
        if (xSemaphoreTake(state_mutex, pdMS_TO_TICKS(250)) != pdTRUE) return;
        bool found = false;
        for (size_t i = 0; i < player_count; ++i)
            if (!strcmp(players[i].id, action->player_id) && players[i].available) found = true;
        if (found) {
            snprintf(preferred, sizeof(preferred), "%s", action->player_id);
            snprintf(player_id, sizeof(player_id), "%s", action->player_id);
            snprintf(track, sizeof(track), "Loading player...");
            artist[0] = 0;
            clear_timeline();
            reset_queue();
            reset_browse();
            current_available = false;
            current_playing = false;
            snprintf(player_name, sizeof(player_name), "Loading player...");
            players_dirty = true;
            screen_dirty = true;
        }
        xSemaphoreGive(state_mutex);
        if (found) { save_preferred_player(action->player_id); refresh_pending = true; }
        return;
    }
    int group = action_group(action->type);
    if (xSemaphoreTake(state_mutex, pdMS_TO_TICKS(250)) != pdTRUE) {
        finish_command(group); return;
    }
    bool allowed = ready && current_available &&
        playback_target_matches(player_id, current_queue_id, current_item_id,
                                action->player_id, action->queue_id, action->item_id, group == PLAYBACK_SEEK);
    if(action->type==ACT_BROWSE_PLAY) allowed=allowed && gateway_can_browse && current_queue_id[0] &&
        !strcmp(current_queue_id,action->queue_id) && action->media_uri[0];
    if(action->type==ACT_QUEUE_PLAY) allowed=allowed && gateway_can_queue &&
        current_queue_id[0] && !strcmp(current_queue_id,action->queue_id) && action->item_id[0];
    if (group == PLAYBACK_VOLUME) allowed = allowed && current_can_volume;
    if (group == PLAYBACK_SEEK) allowed = allowed && can_seek() && !commands[PLAYBACK_TRANSPORT].pending;
    int seek_position = playback_seek_position(action->position, track_duration);
    xSemaphoreGive(state_mutex);
    if (!allowed) { status("Control target unavailable or changed"); finish_command(group); return; }
    const char *command = NULL;
    int volume = action->volume;
    bool volume_command = false;
    switch (action->type) {
        case ACT_PLAY_STOP: command = playback_toggle_command(action->playing); break;
        case ACT_PREVIOUS: command = "players/cmd/previous"; break;
        case ACT_NEXT: command = "players/cmd/next"; break;
        case ACT_VOLUME_DOWN:
            volume = playback_volume_step(volume, false);
            command = "players/cmd/volume_set"; volume_command = true; break;
        case ACT_VOLUME_UP:
            volume = playback_volume_step(volume, true);
            command = "players/cmd/volume_set"; volume_command = true; break;
        case ACT_VOLUME_SET:
            volume = volume < 0 ? 0 : (volume > 100 ? 100 : volume);
            command = "players/cmd/volume_set"; volume_command = true; break;
        case ACT_SEEK: command = "player_queues/seek"; break;
        case ACT_QUEUE_PLAY: command="player_queues/play_index"; break;
        case ACT_BROWSE_PLAY: command="player_queues/play_media"; break;
        default: finish_command(group); return;
    }
    const char *target = group == PLAYBACK_SEEK || action->type==ACT_QUEUE_PLAY || action->type==ACT_BROWSE_PLAY ? action->queue_id : action->player_id;
    if (!command_with_args(command, target, volume, volume_command, group, seek_position,
                           action->type==ACT_QUEUE_PLAY ? action->item_id : NULL,
                           action->type==ACT_BROWSE_PLAY ? action->media_uri : NULL)) {
        status("Unable to send command"); finish_command(group);
    }
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
        const cJSON *features = cJSON_GetObjectItemCaseSensitive(p, "supported_features");
        const cJSON *feature;
        cJSON_ArrayForEach(feature, features) {
            if (cJSON_IsString(feature) && !strcmp(feature->valuestring, "volume_set")) info->can_volume = true;
        }
        const cJSON *volume = cJSON_GetObjectItemCaseSensitive(p, "volume_level");
        info->volume = cJSON_IsNumber(volume) ? volume->valueint : 0;
        if (info->volume < 0) info->volume = 0;
        if (info->volume > 100) info->volume = 100;
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
        current_can_volume = chosen->can_volume;

        if (changed) { clear_timeline(); snprintf(track, sizeof(track), "Loading player..."); artist[0] = 0; }
    } else {
        player_id[0] = 0;
        snprintf(player_name, sizeof(player_name), "No players");
        clear_timeline();
        current_can_volume = false;
        current_available = false;
        current_playing = false;
        snprintf(track, sizeof(track), "Nothing playing");
        artist[0] = 0;
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
    snprintf(provider_feedback, sizeof(provider_feedback), "Unable to save providers; try again");
    char value[PROVIDER_SELECTION_SIZE] = "";
    for (size_t i = 0; i < music_provider_count; ++i) {
        if (!music_providers[i].selected) continue;
        size_t remaining = sizeof(value) - strlen(value);
        if (remaining <= strlen(music_providers[i].id) + 2) {
            status("Provider selection too large");
            return;
        }
        size_t used = strlen(value);
        int written = snprintf(value + used, sizeof(value) - used, "|%s|", music_providers[i].id);
        if (written < 0 || (size_t)written >= sizeof(value) - used) {
            status("Provider selection too large");
            return;
        }
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
    ESP_LOGI(TAG, "Provider selection committed to NVS");
    snprintf(selected_provider_ids, sizeof(selected_provider_ids), "%s", value);
    provider_selection_saved = true;
    ESP_LOGI(TAG, "Music provider selection saved (%u bytes)", (unsigned)strlen(value));
    snprintf(provider_feedback, sizeof(provider_feedback), "Selection saved");
    screen_dirty = true;
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
        if (!provider->name[0]) memcpy(provider->name, provider->id, strlen(provider->id) + 1);
        provider->selected = provider_is_selected(provider->id);
        music_provider_count++;
    }
    providers_dirty = true; browse_dirty=true;
    xSemaphoreGive(state_mutex);
    ESP_LOGI(TAG, "Music providers discovered: %u", (unsigned)music_provider_count);
}

static double number_or(const cJSON *obj, const char *key, double fallback) {
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsNumber(value) && isfinite(value->valuedouble) ? value->valuedouble : fallback;
}
static void queues_received(const cJSON *array) {
    if (!cJSON_IsArray(array) || !state_mutex) return;
    if (xSemaphoreTake(state_mutex, pdMS_TO_TICKS(250)) != pdTRUE) return;
    char previous_item[129]; snprintf(previous_item,sizeof(previous_item),"%s",current_item_id);
    int previous_total=queue_total;
    clear_timeline();
    const cJSON *queue;
    cJSON_ArrayForEach(queue, array) {
        const cJSON *id = cJSON_GetObjectItemCaseSensitive(queue, "queue_id");
        if (!cJSON_IsString(id) || strcmp(id->valuestring, player_id)) continue;
        field(current_queue_id, sizeof(current_queue_id), queue, "queue_id");
        double total=number_or(queue,"items",0);
        queue_total=total>=0 && total<=100000 ? (int)total : 0;
        double index=number_or(queue,"current_index",0);
        current_queue_index=index>=0 && index<100000 ? (int)index : 0;
        const cJSON *item = cJSON_GetObjectItemCaseSensitive(queue, "current_item");
        const cJSON *media = cJSON_GetObjectItemCaseSensitive(item, "media_item");
        if (!cJSON_IsObject(media)) media = item;
        field(current_item_id, sizeof(current_item_id), item, "queue_item_id");
        if(active_view==VIEW_QUEUE && (strcmp(previous_item,current_item_id) || previous_total!=queue_total)) queue_requested=true;
        if (current_item_id[0] && gateway_can_artwork) {
            field(current_art_id,sizeof(current_art_id),item,"controller_art_id");
            if (!current_art_id[0]) snprintf(current_art_id,sizeof(current_art_id),"generic");
        }
        queue_active = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(queue, "active"));
        const cJSON *queue_state = cJSON_GetObjectItemCaseSensitive(queue, "state");
        queue_playing = cJSON_IsString(queue_state) && !strcmp(queue_state->valuestring, "playing");
        double duration = number_or(item, "duration", number_or(media, "duration", 0));
        track_duration = duration > 0 && duration <= 604800 ? (int)duration : 0;
        const cJSON *type = cJSON_GetObjectItemCaseSensitive(media, "media_type");
        const cJSON *details = cJSON_GetObjectItemCaseSensitive(item, "streamdetails");
        if (!cJSON_IsString(type)) type = cJSON_GetObjectItemCaseSensitive(details, "media_type");
        queue_live = cJSON_IsString(type) && (!strcmp(type->valuestring, "radio") || !strcmp(type->valuestring, "audio_source"));
        queue_allow_seek = !cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(details, "allow_seek"));
        track_position = number_or(queue, "elapsed_time", 0);
        track_speed = number_or(queue, "playback_speed", 1.0);
        if (track_speed <= 0 || track_speed > 4) track_speed = 1.0;
        double updated = number_or(queue, "elapsed_time_last_updated", 0);
        double now = (double)time(NULL);
        if (queue_playing && updated > 0 && now >= updated && now - updated < 120)
            track_position += (now - updated) * track_speed;
        if (track_position < 0) track_position = 0;
        if (track_duration > 0 && track_position > track_duration) track_position = track_duration;
        position_sample_time = esp_timer_get_time();
        snprintf(track, sizeof(track), "Nothing playing");
        artist[0] = 0;
        if (cJSON_IsObject(media)) {
            field(track, sizeof(track), media, "name");
            const cJSON *artists = cJSON_GetObjectItemCaseSensitive(media, "artists");
            const cJSON *first = cJSON_GetArrayItem(artists, 0);
            if (first) field(artist, sizeof(artist), first, "name");
            field(album, sizeof(album), cJSON_GetObjectItemCaseSensitive(media, "album"), "name");
        }
        break;
    }
    screen_dirty = true;
    xSemaphoreGive(state_mutex);
}
static void request_queue_page(void) {
    if(xSemaphoreTake(state_mutex,pdMS_TO_TICKS(100))!=pdTRUE) return;
    if(!queue_requested || queue_loading) { xSemaphoreGive(state_mutex); return; }
    queue_requested=false;
    if(!ready || !current_available || !current_queue_id[0]) {
        queue_count=0; queue_total=0; queue_dirty=true;
        snprintf(queue_message,sizeof(queue_message),!ready?"Disconnected - reconnect to load":"No queue for this player");
        xSemaphoreGive(state_mutex); return;
    }
    queue_loading=true; queue_count=0; queue_dirty=true;
    snprintf(queue_message,sizeof(queue_message),"Loading queue...");
    snprintf(queue_request_id,sizeof(queue_request_id),"i%u",++request_id);
    snprintf(queue_request_player,sizeof(queue_request_player),"%s",player_id);
    snprintf(queue_request_queue,sizeof(queue_request_queue),"%s",current_queue_id);
    queue_request_offset=queue_offset; queue_request_started=esp_timer_get_time();
    cJSON *message=cJSON_CreateObject(), *args=cJSON_CreateObject();
    if(!message || !args) {
        cJSON_Delete(message); cJSON_Delete(args); queue_loading=false; queue_dirty=true;
        snprintf(queue_message,sizeof(queue_message),"Not enough memory - tap Refresh");
        xSemaphoreGive(state_mutex); return;
    }
    cJSON_AddStringToObject(message,"message_id",queue_request_id);
    cJSON_AddStringToObject(message,"command","player_queues/items");
    cJSON_AddStringToObject(args,"queue_id",queue_request_queue);
    cJSON_AddNumberToObject(args,"limit",UI_QUEUE_PAGE_SIZE);
    cJSON_AddNumberToObject(args,"offset",queue_offset);
    cJSON_AddItemToObject(message,"args",args);
    char *payload=cJSON_PrintUnformatted(message); cJSON_Delete(message);
    xSemaphoreGive(state_mutex);
    bool sent=payload && esp_websocket_client_send_text(socket_handle,payload,strlen(payload),pdMS_TO_TICKS(1000))==(int)strlen(payload);
    free(payload);
    if(!sent && xSemaphoreTake(state_mutex,pdMS_TO_TICKS(100))==pdTRUE) {
        queue_loading=false; queue_dirty=true;
        snprintf(queue_message,sizeof(queue_message),"Queue request failed - tap Refresh");
        xSemaphoreGive(state_mutex);
    }
    screen_dirty=true;
}
static void queue_page_received(const char *id, const cJSON *result, bool error) {
    if(xSemaphoreTake(state_mutex,pdMS_TO_TICKS(100))!=pdTRUE) return;
    bool matches=queue_loading && !strcmp(id,queue_request_id) &&
        !strcmp(player_id,queue_request_player) && !strcmp(current_queue_id,queue_request_queue) &&
        queue_offset==queue_request_offset;
    if(matches) {
        queue_loading=false; queue_count=0;
        if(error || !cJSON_IsArray(result)) snprintf(queue_message,sizeof(queue_message),"Could not load queue - tap Refresh");
        else {
            const cJSON *item;
            cJSON_ArrayForEach(item,result) {
                if(queue_count>=UI_QUEUE_PAGE_SIZE) break;
                ui_queue_item_t *row=&queue_items[queue_count++]; memset(row,0,sizeof(*row));
                field(row->id,sizeof(row->id),item,"queue_item_id");
                field(row->title,sizeof(row->title),item,"name");
                const cJSON *media=cJSON_GetObjectItemCaseSensitive(item,"media_item");
                if(!row->title[0]) field(row->title,sizeof(row->title),media,"name");
                if(!row->title[0]) snprintf(row->title,sizeof(row->title),"Unnamed track");
                field(row->artist,sizeof(row->artist),cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(media,"artists"),0),"name");
                double duration=number_or(item,"duration",number_or(media,"duration",0));
                row->duration=duration>0 && duration<=604800 ? (int)duration : 0;
                row->available=!cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(media,"available")) &&
                    !cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(item,"available"));
            }
            if(queue_total<queue_offset+(int)queue_count) queue_total=queue_offset+queue_count;
            snprintf(queue_message,sizeof(queue_message),queue_count?"Tap a track to play":"Queue is empty");
        }
        queue_dirty=true; screen_dirty=true;
    }
    xSemaphoreGive(state_mutex);
}
static void request_browse_page(void) {
    if(xSemaphoreTake(state_mutex,pdMS_TO_TICKS(100))!=pdTRUE) return;
    if(!browse_requested || browse_loading) { xSemaphoreGive(state_mutex); return; }
    browse_requested=false; browse_dirty=true;
    browse_context_t *context=&browse_stack[browse_depth];
    if(!browse_depth || context->kind==BROWSE_SOURCE) { xSemaphoreGive(state_mutex); return; }
    if(!ready || !gateway_can_browse) {
        snprintf(browse_message,sizeof(browse_message),!ready?"Reconnect to browse":"Gateway update required for Browse");
        xSemaphoreGive(state_mutex); return;
    }
    browse_loading=true; browse_request_started=esp_timer_get_time();
    browse_request_generation=browse_generation;
    snprintf(browse_request_id,sizeof(browse_request_id),"b%u",++request_id);
    snprintf(browse_message,sizeof(browse_message),"Loading music...");
    cJSON *message=cJSON_CreateObject(), *args=cJSON_CreateObject();
    if(!message || !args) {
        cJSON_Delete(message); cJSON_Delete(args); browse_loading=false;
        snprintf(browse_message,sizeof(browse_message),"Not enough memory - tap Refresh");
        xSemaphoreGive(state_mutex); return;
    }
    cJSON_AddStringToObject(message,"message_id",browse_request_id);
    cJSON_AddStringToObject(message,"command","controller/browse");
    cJSON_AddStringToObject(args,"kind",browse_kind_names[context->kind]);
    cJSON_AddStringToObject(args,"provider",context->provider);
    cJSON_AddStringToObject(args,"source",context->source);
    if(context->kind==BROWSE_FOLDERS) cJSON_AddStringToObject(args,"path",context->uri);
    else if(context->id[0]) cJSON_AddStringToObject(args,"parent_id",context->id);
    cJSON_AddNumberToObject(args,"offset",context->offset); cJSON_AddNumberToObject(args,"limit",UI_QUEUE_PAGE_SIZE);
    cJSON_AddItemToObject(message,"args",args);
    char *payload=cJSON_PrintUnformatted(message); cJSON_Delete(message); xSemaphoreGive(state_mutex);
    bool sent=payload && esp_websocket_client_send_text(socket_handle,payload,strlen(payload),pdMS_TO_TICKS(1000))==(int)strlen(payload);
    free(payload);
    if(!sent && xSemaphoreTake(state_mutex,pdMS_TO_TICKS(100))==pdTRUE) {
        browse_loading=false; browse_dirty=true;
        snprintf(browse_message,sizeof(browse_message),"Browse request failed - tap Refresh"); xSemaphoreGive(state_mutex);
    }
}
static void browse_page_received(const char *id,const cJSON *result,bool error) {
    if(xSemaphoreTake(state_mutex,pdMS_TO_TICKS(100))!=pdTRUE) return;
    if(browse_loading && !strcmp(id,browse_request_id) && browse_request_generation==browse_generation) {
        browse_loading=false; browse_count=0; browse_more=false;
        const cJSON *items=cJSON_GetObjectItemCaseSensitive(result,"items");
        if(error || !cJSON_IsArray(items)) snprintf(browse_message,sizeof(browse_message),"Could not browse - tap Refresh");
        else {
            const cJSON *item;
            cJSON_ArrayForEach(item,items) {
                if(browse_count>=UI_QUEUE_PAGE_SIZE) break;
                ui_media_item_t *row=&browse_items[browse_count++]; memset(row,0,sizeof(*row));
                field(row->id,sizeof(row->id),item,"id"); field(row->provider,sizeof(row->provider),item,"provider");
                field(row->uri,sizeof(row->uri),item,"uri"); field(row->title,sizeof(row->title),item,"name");
                field(row->subtitle,sizeof(row->subtitle),item,"subtitle");
                const cJSON *kind=cJSON_GetObjectItemCaseSensitive(item,"kind");
                row->kind=UI_MEDIA_TRACK;
                if(cJSON_IsString(kind)) {
                    if(!strcmp(kind->valuestring,"album")) row->kind=UI_MEDIA_ALBUM;
                    else if(!strcmp(kind->valuestring,"artist")) row->kind=UI_MEDIA_ARTIST;
                    else if(!strcmp(kind->valuestring,"playlist")) row->kind=UI_MEDIA_PLAYLIST;
                    else if(!strcmp(kind->valuestring,"folder")) row->kind=UI_MEDIA_FOLDER;
                }
                row->available=cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(item,"available"));
            }
            browse_more=cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(result,"has_more"));
            snprintf(browse_message,sizeof(browse_message),browse_count?"Choose an item":"No items in this source");
        }
        browse_dirty=true; screen_dirty=true;
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
            const cJSON *caps = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(msg, "data"), "capabilities");
            gateway_can_seek = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(caps, "seek"));
            gateway_can_queue=false; gateway_can_browse=false;
            ready = true; status("Connected"); refresh_pending = true;
            provider_fetch_pending = true;
        } else if (!strcmp(event->valuestring, "gateway/capabilities")) {
            gateway_can_seek = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(msg, "data"), "seek"));
            gateway_can_browse = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(msg, "data"), "browse"));
            browse_dirty=true;
            gateway_can_queue = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(msg, "data"), "queue_item_play"));
            gateway_can_artwork = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(msg, "data"), "artwork_rgb565"));
            screen_dirty = true;
        } else if (!strcmp(event->valuestring, "gateway/error")) {
            ready = false; status("Gateway authentication failed");
        } else if (strstr(event->valuestring, "player") || strstr(event->valuestring, "queue")) {
            refresh_pending = true;
            if(strstr(event->valuestring,"queue_items") && active_view==VIEW_QUEUE) queue_requested=true;
        }
    }
    const cJSON *mid = cJSON_GetObjectItemCaseSensitive(msg, "message_id");
    const cJSON *result = cJSON_GetObjectItemCaseSensitive(msg, "result");
    const cJSON *error = cJSON_GetObjectItemCaseSensitive(msg, "error");
    if (!error || cJSON_IsNull(error) || cJSON_IsFalse(error)) error = cJSON_GetObjectItemCaseSensitive(msg, "error_code");
    if(cJSON_IsString(mid) && mid->valuestring[0]=='b') {
        browse_page_received(mid->valuestring,result,error && !cJSON_IsNull(error) && !cJSON_IsFalse(error));
        cJSON_Delete(msg); return;
    }
    if(cJSON_IsString(mid) && mid->valuestring[0]=='i') {
        queue_page_received(mid->valuestring,result,error && !cJSON_IsNull(error) && !cJSON_IsFalse(error));
        cJSON_Delete(msg); return;
    }
    if (cJSON_IsString(mid)) {
        int reply_group = -1;
        if (xSemaphoreTake(state_mutex, pdMS_TO_TICKS(250)) == pdTRUE) {
            for (int i = 0; i < PLAYBACK_GROUPS; ++i)
                if (commands[i].pending && commands[i].id[0] && !strcmp(mid->valuestring, commands[i].id)) reply_group = i;
            xSemaphoreGive(state_mutex);
        }
        if (reply_group >= 0) { finish_command(reply_group); refresh_pending = true; }
        if (error && !cJSON_IsNull(error) && !cJSON_IsFalse(error)) {
            ESP_LOGE(TAG, "MA response %c returned error", mid->valuestring[0]);
            status("Music Assistant API error");
            if(reply_group==PLAYBACK_TRANSPORT && xSemaphoreTake(state_mutex,pdMS_TO_TICKS(100))==pdTRUE) {
                snprintf(queue_message,sizeof(queue_message),"Playback failed - try another track"); queue_dirty=true;
                snprintf(browse_message,sizeof(browse_message),"Playback failed - try another item"); browse_dirty=true;
                xSemaphoreGive(state_mutex);
            }
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
static void queue_action(action_t *action) {
    bool control = action->type != ACT_SELECT && action->type != ACT_PROVIDER_SAVE;
    int group = action_group(action->type);
    if (control && (!ready || !current_available || !player_id[0])) return;
    if (control && commands[group].pending) {
        /* Coalesce slider updates, including the final release, to the latest value. */
        if (action->type == ACT_VOLUME_SET) { deferred_volume = *action; volume_deferred = true; }
        return;
    }
    if (xQueueSend(action_queue, action, 0) == pdTRUE) {
        if (control) {
            commands[group].pending = true;
            commands[group].started = esp_timer_get_time();
        }
        if (action->type == ACT_PROVIDER_SAVE)
            snprintf(provider_feedback, sizeof(provider_feedback), "Saving selection...");
        screen_dirty = true;
    } else status("Action queue full; try again");
}
static void enqueue(action_type_t type, const char *id) {
    if (!action_queue || xSemaphoreTake(state_mutex, 0) != pdTRUE) return;
    action_t action = {.type = type, .playing = current_playing, .volume = current_volume};
    snprintf(action.player_id, sizeof(action.player_id), "%s", id ? id : player_id);
    queue_action(&action);
    xSemaphoreGive(state_mutex);
}
static void dock_action(const ui_intent_t *intent) {
    if(intent->type>=UI_BROWSE_OPEN) {
        if(xSemaphoreTake(state_mutex,0)!=pdTRUE) return;
        if(intent->type==UI_BROWSE_PLAY || intent->type==UI_BROWSE_PLAY_ALL) {
            if(intent->generation==browse_generation && intent->media_uri[0] &&
               !strcmp(intent->player_id,player_id) && !strcmp(intent->queue_id,current_queue_id)) {
                action_t action={.type=ACT_BROWSE_PLAY};
                snprintf(action.player_id,sizeof(action.player_id),"%s",intent->player_id);
                snprintf(action.queue_id,sizeof(action.queue_id),"%s",intent->queue_id);
                snprintf(action.media_uri,sizeof(action.media_uri),"%s",intent->media_uri);
                queue_action(&action);
            }
        } else if(intent->type==UI_BROWSE_OPEN) {
            active_view=VIEW_BROWSE; reset_browse(); browse_changed();
        } else if(intent->type==UI_BROWSE_BACK) {
            if(browse_depth) browse_depth--;
            browse_changed();
        } else if(intent->type==UI_BROWSE_SELECT && intent->generation==browse_generation &&
                  intent->value>=0 && intent->value<(int)browse_count && browse_depth<7) {
            ui_media_item_t *item=&browse_items[intent->value];
            if(item->available && !strcmp(item->id,intent->item_id)) {
                browse_context_t *parent=&browse_stack[browse_depth], *next=&browse_stack[browse_depth+1];
                memset(next,0,sizeof(*next));
                memmove(next->source,parent->source,sizeof(next->source));
                snprintf(next->provider,sizeof(next->provider),"%s",item->provider);
                snprintf(next->id,sizeof(next->id),"%s",item->id); snprintf(next->title,sizeof(next->title),"%s",item->title);
                snprintf(next->uri,sizeof(next->uri),"%s",item->uri);
                if(item->kind==UI_MEDIA_SOURCE) { next->kind=BROWSE_SOURCE; snprintf(next->source,sizeof(next->source),"%s",item->provider); }
                else if(item->kind==UI_MEDIA_CATEGORY) {
                    if(!strcmp(item->id,"albums")) next->kind=BROWSE_ALBUMS;
                    else if(!strcmp(item->id,"artists")) next->kind=BROWSE_ARTISTS;
                    else if(!strcmp(item->id,"playlists")) next->kind=BROWSE_PLAYLISTS;
                    else { next->kind=BROWSE_FOLDERS; snprintf(next->uri,sizeof(next->uri),"%s://",item->provider); }
                    next->id[0]=0;
                } else if(item->kind==UI_MEDIA_ALBUM) next->kind=BROWSE_ALBUM_TRACKS;
                else if(item->kind==UI_MEDIA_ARTIST) next->kind=BROWSE_ARTIST_ALBUMS;
                else if(item->kind==UI_MEDIA_PLAYLIST) next->kind=BROWSE_PLAYLIST_TRACKS;
                else next->kind=BROWSE_FOLDERS;
                browse_depth++; browse_changed();
            }
        } else if(intent->type==UI_BROWSE_SELECT && browse_depth>=7) {
            snprintf(browse_message,sizeof(browse_message),"Folder depth limit - use Back"); browse_dirty=true;
        } else if(!browse_loading) {
            browse_context_t *context=&browse_stack[browse_depth];
            if(intent->type==UI_BROWSE_PREVIOUS) context->offset=context->offset<20?0:context->offset-20;
            if(intent->type==UI_BROWSE_MORE && browse_more && context->offset<100000-20) context->offset+=20;
            browse_changed();
        }
        xSemaphoreGive(state_mutex); return;
    }
    if(intent->type>=UI_QUEUE_OPEN && intent->type!=UI_QUEUE_PLAY) {
        if(xSemaphoreTake(state_mutex,0)!=pdTRUE) return;
        if(intent->type==UI_QUEUE_OPEN) active_view=VIEW_QUEUE;
        if(!queue_loading) {
            if(intent->type==UI_QUEUE_OPEN) queue_offset=(current_queue_index/UI_QUEUE_PAGE_SIZE)*UI_QUEUE_PAGE_SIZE;
            if(intent->type==UI_QUEUE_BACK) queue_offset=queue_offset<UI_QUEUE_PAGE_SIZE?0:queue_offset-UI_QUEUE_PAGE_SIZE;
            if(intent->type==UI_QUEUE_MORE && queue_offset+UI_QUEUE_PAGE_SIZE<queue_total) queue_offset+=UI_QUEUE_PAGE_SIZE;
            queue_requested=true; queue_dirty=true; screen_dirty=true;
        }
        xSemaphoreGive(state_mutex); return;
    }
    static const action_type_t actions[] = {
        ACT_PREVIOUS, ACT_PLAY_STOP, ACT_NEXT, ACT_VOLUME_DOWN, ACT_VOLUME_UP, ACT_VOLUME_SET, ACT_SEEK,
        ACT_QUEUE_PLAY, ACT_QUEUE_PLAY, ACT_QUEUE_PLAY, ACT_QUEUE_PLAY, ACT_QUEUE_PLAY
    };
    if (xSemaphoreTake(state_mutex, 0) != pdTRUE) return;
    action_t action = {.type = actions[intent->type], .playing = current_playing,
                       .volume = intent->type == UI_VOLUME_SET ? intent->value : current_volume,
                       .position = intent->value};
    snprintf(action.player_id, sizeof(action.player_id), "%s", intent->player_id);
    snprintf(action.queue_id, sizeof(action.queue_id), "%s", intent->queue_id);
    snprintf(action.item_id, sizeof(action.item_id), "%s", intent->item_id);
    queue_action(&action);
    xSemaphoreGive(state_mutex);
}
static void player_clicked(lv_event_t *event) {
    const char *id = lv_event_get_user_data(event);
    if (!id) return;
    enqueue(ACT_SELECT, id);
    active_view = VIEW_NOW_PLAYING;
    controller_ui_show(&ui, UI_PLAYING);
}
static void open_players(lv_event_t *event) {
    active_view = VIEW_PLAYERS;
    controller_ui_show(&ui, UI_PLAYERS);
}
static void back_clicked(lv_event_t *event) {
    active_view = VIEW_NOW_PLAYING;
    controller_ui_show(&ui, UI_PLAYING);
}
static void update_now_playing(void) {
    double position = track_position;
    if (queue_playing && current_playing && ready)
        position += ((esp_timer_get_time() - position_sample_time) / 1000000.0) * track_speed;
    if (position < 0) position = 0;
    if (track_duration > 0 && position > track_duration) position = track_duration;
    if (position > 604800) position = 604800;
    ui_playback_t snapshot = {
        .track = track, .artist = artist, .album = album, .player = player_name, .status = state,
        .art_id = current_art_id,
        .player_id = player_id, .queue_id = current_queue_id, .item_id = current_item_id,
        .volume = current_volume, .position = (int)position, .duration = track_duration,
        .connected = ready, .available = current_available, .playing = current_playing,
        .transport_pending = commands[PLAYBACK_TRANSPORT].pending,
        .volume_pending = commands[PLAYBACK_VOLUME].pending,
        .seek_pending = commands[PLAYBACK_SEEK].pending,
        .can_volume = current_can_volume, .can_seek = can_seek(), .live = queue_live, .can_queue=gateway_can_queue, .can_browse=gateway_can_browse
    };
    controller_ui_update(&ui, &snapshot);
    if (art_result) {
        controller_ui_set_artwork(&ui,art_result_id,art_result,8+288*288*2);
        free(art_result); art_result=NULL;
    }
    lv_label_set_text(provider_save_status, provider_feedback);
}
static void build_queue_view(void) {
    controller_ui_queue(&ui,queue_items,queue_count,queue_offset,queue_total,current_item_id,
        !ready?"Disconnected - reconnect to load":(!gateway_can_queue && queue_count?"Gateway update required to play tracks":queue_message),queue_loading,
        ready && current_available && gateway_can_queue && !commands[PLAYBACK_TRANSPORT].pending);
}
static void build_browse_view(void) {
    browse_context_t *context=&browse_stack[browse_depth];
    if(!browse_depth) {
        browse_count=0;
        for(size_t i=0;i<music_provider_count && browse_count<UI_QUEUE_PAGE_SIZE;i++) if(music_providers[i].selected) {
            ui_media_item_t *row=&browse_items[browse_count++]; memset(row,0,sizeof(*row));
            row->kind=UI_MEDIA_SOURCE; row->available=ready && gateway_can_browse;
            snprintf(row->id,sizeof(row->id),"%s",music_providers[i].id);
            snprintf(row->provider,sizeof(row->provider),"%s",music_providers[i].id);
            snprintf(row->title,sizeof(row->title),"%s",music_providers[i].name);
            snprintf(row->subtitle,sizeof(row->subtitle),"Albums, artists, playlists and folders");
        }
        snprintf(browse_message,sizeof(browse_message),browse_count?"Choose a music source":"Select Music sources in Settings");
    } else if(context->kind==BROWSE_SOURCE) {
        const char *titles[]={"Albums","Artists","Playlists","Provider folders"};
        const char *ids[]={"albums","artists","playlists","folders"};
        browse_count=4;
        for(unsigned i=0;i<4;i++) {
            ui_media_item_t *row=&browse_items[i]; memset(row,0,sizeof(*row));
            row->kind=UI_MEDIA_CATEGORY; row->available=ready && gateway_can_browse;
            snprintf(row->id,sizeof(row->id),"%s",ids[i]); snprintf(row->title,sizeof(row->title),"%s",titles[i]);
            snprintf(row->provider,sizeof(row->provider),"%s",context->provider);
        }
        snprintf(browse_message,sizeof(browse_message),"Choose a category");
    }
    const char *uri=context->kind==BROWSE_ALBUM_TRACKS || context->kind==BROWSE_PLAYLIST_TRACKS ? context->uri : "";
    controller_ui_browse(&ui,browse_items,browse_count,browse_depth?context->title:"Music sources",
        !ready?"Disconnected - reconnect to browse":(!gateway_can_browse?"Gateway update required for Browse":browse_message),
        browse_loading,ready && current_available && gateway_can_browse && current_queue_id[0] && !commands[PLAYBACK_TRANSPORT].pending,
        browse_depth>0,context->offset>0,browse_more,uri,browse_generation);
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
        controller_ui_style_row(button, !strcmp(players[i].id, player_id), players[i].available);
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
    ESP_LOGI(TAG, "Provider checkbox changed: selected=%d", (int)action.selected);
    if (xQueueSend(action_queue, &action, 0) != pdTRUE)
        ESP_LOGW(TAG, "Provider selection queue full");
}
static void save_providers_clicked(lv_event_t *event) {
    enqueue(ACT_PROVIDER_SAVE, NULL);
}
static void open_providers(lv_event_t *event) {
    active_view = VIEW_PROVIDERS;
    controller_ui_show(&ui, UI_PROVIDERS);
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
        lv_obj_set_width(checkbox, 352);
        lv_obj_set_height(checkbox, 72);
        controller_ui_style_source(checkbox);
        if (music_providers[i].selected) lv_obj_add_state(checkbox, LV_STATE_CHECKED);
        lv_obj_add_event_cb(checkbox, provider_toggled, LV_EVENT_VALUE_CHANGED, music_providers[i].id);
    }
}

static esp_err_t art_http_event(esp_http_client_event_t *event) {
    if(event->event_id==HTTP_EVENT_ON_HEADER && event->header_key &&
       !strcasecmp(event->header_key,"X-Artwork-Fallback"))
        *(bool *)event->user_data = !strcmp(event->header_value,"1");
    return ESP_OK;
}
/* Dedicated worker: network and image transfer never hold LVGL or state locks. */
static void artwork_worker(void *unused) {
    (void)unused;
    char completed[65]="";
    bool last_fallback=false;
    int64_t last_completed=0;
    for(;;) {
        vTaskDelay(pdMS_TO_TICKS(500));
        char id[65], target_player[129], target_queue[129], target_item[129];
        if(xSemaphoreTake(state_mutex,pdMS_TO_TICKS(100))!=pdTRUE) continue;
        bool fetch=ready && gateway_can_artwork && current_art_id[0] && (strcmp(completed,current_art_id) || !ui.art_loaded ||
            (last_fallback && strcmp(current_art_id,"generic") && esp_timer_get_time()-last_completed>30000000LL));
        snprintf(id,sizeof(id),"%s",current_art_id);
        snprintf(target_player,sizeof(target_player),"%s",player_id);
        snprintf(target_queue,sizeof(target_queue),"%s",current_queue_id);
        snprintf(target_item,sizeof(target_item),"%s",current_item_id);
        xSemaphoreGive(state_mutex);
        if(!fetch) continue;
        char endpoint[320];
        const char *rest=NULL, *scheme=NULL;
        if(!strncmp(url,"ws://",5)) { rest=url+5; scheme="http://"; }
        else if(!strncmp(url,"wss://",6)) { rest=url+6; scheme="https://"; }
        else continue;
        snprintf(endpoint,sizeof(endpoint),"%s%s",scheme,rest);
        char *suffix=strrchr(endpoint,'/');
        if(!suffix || strcmp(suffix,"/ws")) continue;
        snprintf(suffix,sizeof(endpoint)-(suffix-endpoint),"/artwork/%s",id);
        enum { BYTES=8+288*288*2 };
        unsigned char *data=heap_caps_malloc(BYTES,MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if(!data) { vTaskDelay(pdMS_TO_TICKS(5000)); continue; }
        bool fallback=false;
        esp_http_client_config_t config={.event_handler=art_http_event,.user_data=&fallback,.url=endpoint,.timeout_ms=10000,.crt_bundle_attach=esp_crt_bundle_attach,
                                        .disable_auto_redirect=true};
        esp_http_client_handle_t client=esp_http_client_init(&config);
        int received=0;
        if(client) {
            esp_http_client_set_header(client,"X-Device-Token",token);
            if(esp_http_client_open(client,0)==ESP_OK && esp_http_client_fetch_headers(client)==BYTES &&
               esp_http_client_get_status_code(client)==200) {
                while(received<BYTES) {
                    int count=esp_http_client_read(client,(char *)data+received,BYTES-received);
                    if(count<=0) break;
                    received+=count;
                }
            }
            esp_http_client_close(client); esp_http_client_cleanup(client);
        }
        if(received==BYTES && !memcmp(data,"MCAR\x20\x01\x20\x01",8) &&
           xSemaphoreTake(state_mutex,pdMS_TO_TICKS(100))==pdTRUE) {
            if(ready && !strcmp(current_art_id,id) &&
               playback_target_matches(player_id,current_queue_id,current_item_id,target_player,target_queue,target_item,true)) {
                free(art_result); art_result=data; data=NULL;
                snprintf(art_result_id,sizeof(art_result_id),"%s",id);
                snprintf(completed,sizeof(completed),"%s",id);
                last_completed=esp_timer_get_time(); last_fallback=fallback; screen_dirty=true;
            }
            xSemaphoreGive(state_mutex);
        }
        free(data);
        if(received!=BYTES) vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

static void save_theme(unsigned palette, bool light) {
    nvs_handle_t n;
    if (nvs_open("controller", NVS_READWRITE, &n) != ESP_OK) return;
    esp_err_t err = nvs_set_u8(n, "ui_palette", palette);
    if (err == ESP_OK) err = nvs_set_u8(n, "ui_light", light);
    if (err == ESP_OK) err = nvs_commit(n);
    if (err != ESP_OK) ESP_LOGW(TAG, "Could not save appearance: %s", esp_err_to_name(err));
    nvs_close(n);
}

static void init_screen(void) {
    now_screen = lv_scr_act();
    controller_ui_create(&ui, now_screen, dock_action, open_players, open_providers,
                         back_clicked, save_providers_clicked);
    ui.theme_cb = save_theme;
    nvs_handle_t appearance;
    uint8_t palette = 0, light = 0;
    if (nvs_open("controller", NVS_READONLY, &appearance) == ESP_OK) {
        nvs_get_u8(appearance, "ui_palette", &palette);
        nvs_get_u8(appearance, "ui_light", &light);
        nvs_close(appearance);
    }
    controller_ui_set_theme(&ui, palette, light == 1);
    players_list = ui.players_list;
    providers_list = ui.providers_list;
    provider_save_status = ui.provider_feedback;
    update_now_playing();
}

void app_main(void) {
    ESP_LOGI(TAG, "Boot reset reason=%d", (int)esp_reset_reason());
    state_mutex = xSemaphoreCreateMutex();
    action_queue = xQueueCreate(12, sizeof(action_t));
    ESP_ERROR_CHECK(state_mutex && action_queue ? ESP_OK : ESP_ERR_NO_MEM);
    esp_lcd_panel_handle_t panel = NULL;
    esp_lcd_touch_handle_t touch = NULL;
    ESP_ERROR_CHECK(waveshare_esp32_s3_rgb_lcd_init(&panel, &touch));
    ESP_ERROR_CHECK(lvgl_port_init(panel, touch));
    ESP_ERROR_CHECK(waveshare_rgb_lcd_bl_on());
    esp_err_t err = nvs_flash_init();
    ESP_LOGI(TAG, "Creating diagnostic LVGL screen");
    if (lvgl_port_lock(2000)) {
        init_screen();
        lv_obj_invalidate(lv_scr_act());
        lvgl_port_unlock();
        ESP_LOGI(TAG, "Diagnostic screen created");
    } else ESP_LOGE(TAG, "Failed to acquire LVGL lock");
    if (err != ESP_OK) { status("NVS initialisation failed"); return; }
    if (!load_config()) { status("Provision device NVS first"); render(); return; }
    ESP_ERROR_CHECK(xTaskCreate(artwork_worker,"artwork",6144,NULL,3,NULL)==pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
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
    int64_t last_position_render = 0;
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
        if (xSemaphoreTake(state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            if (volume_deferred && !commands[PLAYBACK_VOLUME].pending) {
                action_t latest = deferred_volume;
                volume_deferred = false;
                if (ready && !strcmp(latest.player_id, player_id)) queue_action(&latest);
            }
            xSemaphoreGive(state_mutex);
        }
        action_t action;
        while (xQueueReceive(action_queue, &action, 0) == pdTRUE) process_action(&action);
        request_queue_page();
        request_browse_page();
        if(xSemaphoreTake(state_mutex,pdMS_TO_TICKS(100))==pdTRUE) {
            if(browse_loading && (!ready || esp_timer_get_time()-browse_request_started>10000000LL)) {
                browse_loading=false; browse_request_id[0]=0; browse_dirty=true;
                snprintf(browse_message,sizeof(browse_message),"Browse timed out - tap Refresh");
            }
            if(queue_loading && (!ready || esp_timer_get_time()-queue_request_started>10000000LL)) {
                queue_loading=false; queue_request_id[0]=0; queue_dirty=true; screen_dirty=true;
                snprintf(queue_message,sizeof(queue_message),"Queue request timed out - tap Refresh");
            }
            xSemaphoreGive(state_mutex);
        }
        if (screen_dirty || players_dirty || providers_dirty || queue_dirty || browse_dirty) {
            screen_dirty = false;
            if (!render()) screen_dirty = true;
        }
        int64_t now = esp_timer_get_time();
        for (int group = 0; group < PLAYBACK_GROUPS; ++group) {
            bool expired = false;
            if (xSemaphoreTake(state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                expired = commands[group].pending && (!ready || now - commands[group].started > 10000000LL);
                if (!ready) volume_deferred = false;
                if(expired && group==PLAYBACK_TRANSPORT) {
                    snprintf(queue_message,sizeof(queue_message),"Playback unconfirmed - tap Refresh"); queue_dirty=true;
                }
                xSemaphoreGive(state_mutex);
            }
            if (expired) { status("Command unconfirmed; refreshing"); finish_command(group); refresh_pending = true; }
        }
        if (now - last_position_render >= 1000000LL) {
            screen_dirty = true;
            last_position_render = now;
        }
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
