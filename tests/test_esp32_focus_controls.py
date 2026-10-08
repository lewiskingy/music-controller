from pathlib import Path

SOURCE = (Path(__file__).resolve().parents[1] / "firmware/esp32/main/main.c").read_text()


def test_single_focus_screens_and_navigation():
    assert "VIEW_NOW_PLAYING, VIEW_PLAYERS" in SOURCE
    assert "lv_scr_load(players_screen)" in SOURCE
    assert "lv_scr_load(now_screen)" in SOURCE
    assert "lv_obj_clear_flag(now_screen, LV_OBJ_FLAG_SCROLLABLE)" in SOURCE
    assert "lv_obj_clear_flag(players_screen, LV_OBJ_FLAG_SCROLLABLE)" in SOURCE


def test_persist_selection_and_avoid_secrets():
    assert 'nvs_set_str(n, "player_id", id)' in SOURCE
    assert "nvs_commit(n)" in SOURCE
    assert 'nvs_open("controller", NVS_READWRITE' in SOURCE
    assert 'READ("device_token",token)' in SOURCE


def test_transport_uses_existing_gateway_allowlist():
    for command in ("players/cmd/play", "players/cmd/pause", "players/cmd/stop",
                    "players/cmd/next", "players/cmd/previous", "players/cmd/volume_set"):
        assert command in SOURCE
    assert 'status("Use Stop on Sonos")' in SOURCE
    assert "xQueueSend(action_queue" in SOURCE
    assert "vTaskDelay(pdMS_TO_TICKS(100))" in SOURCE
