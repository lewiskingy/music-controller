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
static SemaphoreHandle_t state_mutex;
static QueueHandle_t action_queue;
typedef enum { VIEW_NOW_PLAYING, VIEW_PLAYERS } view_t;
typedef enum { ACT_SELECT, ACT_PLAY_PAUSE, ACT_STOP, ACT_PREVIOUS, ACT_NEXT, ACT_VOLUME_DOWN, ACT_VOLUME_UP } action_type_t;
typedef struct { action_type_t type; char player_id[129]; } action_t;
static view_t active_view = VIEW_NOW_PLAYING;
static lv_obj_t *now_screen, *players_screen;
static lv_obj_t *title_label, *artist_label, *player_label, *state_label, *volume_label, *play_button, *play_text;
static lv_obj_t *players_list;
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
static unsigned long heartbeat_counter;
static void reset_ws_message(void) {
    free(ws_buffer);
    ws_buffer = NULL;
    ws_expected = ws_received = 0;
}

static void build_players_view(void);
static void update_now_playing(void);
static void render(void) {
    if (!now_screen || !lvgl_port_lock(pdMS_TO_TICKS(250))) return;
    if (state_mutex && xSemaphoreTake(state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        update_now_playing();
        if (players_dirty) { build_players_view(); players_dirty = false; }
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
    if (chosen) {
        bool changed = strcmp(player_id, chosen->id) != 0;
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
    refresh_pending = true;
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
            if (mid->valuestring[0] == 'p') players_received(result);
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
static void init_screen(void) {
    now_screen = lv_scr_act();
    lv_obj_set_style_bg_color(now_screen, lv_color_hex(0x121c30), 0);
    lv_obj_set_style_bg_opa(now_screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(now_screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *heading = lv_label_create(now_screen);
    lv_label_set_text(heading, "NOW PLAYING");
    lv_obj_set_style_text_color(heading, lv_color_hex(0xa8c3e3), 0);
    lv_obj_align(heading, LV_ALIGN_TOP_LEFT, 22, 16);

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
    lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 105);

    artist_label = lv_label_create(now_screen);
    lv_obj_set_width(artist_label, 740);
    lv_obj_set_style_text_align(artist_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(artist_label, lv_color_hex(0xc0d1e8), 0);
    lv_obj_align(artist_label, LV_ALIGN_TOP_MID, 0, 155);

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
        if (screen_dirty) { render(); screen_dirty = false; }
        if (ready && (refresh_pending || (++heartbeat_counter % 6 == 0))) { refresh_pending = false; refresh(); }
        ESP_LOGI(TAG, "Heartbeat: ip=%d ws=%d gateway=%d screen=%d", (int)ip_ready, (int)ws_connected, (int)ready, (int)(title_label != NULL));
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
