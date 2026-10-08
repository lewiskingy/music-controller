#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
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
static lv_obj_t *title_label, *artist_label, *player_label, *state_label;
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

static void render(void) {
    if (!title_label || !lvgl_port_lock(pdMS_TO_TICKS(250))) return;
    lv_label_set_text(title_label, track);
    lv_label_set_text(artist_label, artist);
    lv_label_set_text(player_label, player_name);
    lv_label_set_text(state_label, state);
    lvgl_port_unlock();
}
static void status(const char *s) { snprintf(state, sizeof(state), "%s", s); ESP_LOGI(TAG, "STATE: %s", state); screen_dirty = true; }
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
    if (!cJSON_IsArray(array)) return;
    const cJSON *chosen = NULL, *p;
    cJSON_ArrayForEach(p, array) {
        const cJSON *id = cJSON_GetObjectItemCaseSensitive(p, "player_id");
        if (!cJSON_IsString(id)) continue;
        if (!chosen) chosen = p;
        if (preferred[0] && !strcmp(preferred, id->valuestring)) { chosen = p; break; }
    }
    if (!chosen) { status("No players found"); return; }
    field(player_id, sizeof(player_id), chosen, "player_id");
    field(player_name, sizeof(player_name), chosen, "name");
    screen_dirty = true;
    refresh_pending = true;
}
static void queues_received(const cJSON *array) {
    if (!cJSON_IsArray(array)) return;
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
        return;
    }
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
static void init_screen(void) {
    lv_obj_t *root = lv_scr_act();
    lv_obj_set_style_bg_color(root, lv_color_hex(0x121c30), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_t *heading = lv_label_create(root);
    lv_label_set_text(heading, "Music Controller");
    lv_obj_set_style_text_font(heading, &lv_font_montserrat_24, 0);
    lv_obj_align(heading, LV_ALIGN_TOP_MID, 0, 24);
    lv_obj_set_style_text_color(heading, lv_color_white(), 0);
    player_label = lv_label_create(root);
    lv_obj_align(player_label, LV_ALIGN_TOP_MID, 0, 85);
    lv_obj_set_style_text_color(player_label, lv_color_white(), 0);
    title_label = lv_label_create(root);
    lv_obj_set_width(title_label, 730);
    lv_label_set_long_mode(title_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(title_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_24, 0);
    lv_obj_align(title_label, LV_ALIGN_CENTER, 0, -25);
    lv_obj_set_style_text_color(title_label, lv_color_white(), 0);
    artist_label = lv_label_create(root);
    lv_obj_set_width(artist_label, 700);
    lv_obj_set_style_text_align(artist_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(artist_label, LV_ALIGN_CENTER, 0, 28);
    lv_obj_set_style_text_color(artist_label, lv_color_white(), 0);
    state_label = lv_label_create(root);
    lv_obj_align(state_label, LV_ALIGN_BOTTOM_MID, 0, -26);
    lv_obj_set_style_text_color(state_label, lv_color_hex(0x8fe3a0), 0);
    lv_label_set_text(title_label, track);
    lv_label_set_text(artist_label, artist);
    lv_label_set_text(player_label, player_name);
    lv_label_set_text(state_label, state);
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
