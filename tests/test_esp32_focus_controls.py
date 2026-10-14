from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "firmware/esp32/main/main.c").read_text()
UI = (ROOT / "firmware/esp32/main/controller_ui.c").read_text()

def test_views_share_one_shell_and_dock():
    assert "controller_ui_create(&ui, now_screen" in SOURCE
    assert "controller_ui_show(&ui, UI_PLAYERS)" in SOURCE
    assert "controller_ui_show(&ui, UI_PLAYING)" in SOURCE
    assert "controller_ui_show(&ui, UI_PROVIDERS)" in SOURCE
    assert "lv_scr_load" not in SOURCE
    assert "LV_OBJ_FLAG_HIDDEN" in UI
    assert "UI_DOCK_Y, UI_WIDTH, UI_DOCK_HEIGHT" in UI

def test_persist_selection_and_avoid_secrets():
    assert 'nvs_set_str(n, "player_id", id)' in SOURCE
    assert "nvs_commit(n)" in SOURCE
    assert 'READ("device_token",token)' in SOURCE

def test_commands_capture_player_and_recover_pending():
    assert "playback_target_matches(player_id, current_queue_id, current_item_id" in SOURCE
    assert "action->player_id, action->queue_id, action->item_id" in SOURCE
    assert "command_with_args(command, target" in SOURCE
    assert "!strcmp(mid->valuestring, commands[i].id)" in SOURCE
    assert "now - commands[group].started > 10000000LL" in SOURCE
    assert "finish_command(group)" in SOURCE
    assert "players/cmd/pause" not in SOURCE

def test_side_volume_and_seek_timeline_use_separate_pending_states():
    assert "panel(root, UI_VOLUME_X, UI_HEADER_HEIGHT" in UI
    assert "UI_CONTENT_WIDTH, UI_CONTENT_HEIGHT" in UI
    assert "volume_gesture" in UI and "seek_gesture" in UI
    assert "LV_EVENT_RELEASED" in UI and "LV_EVENT_PRESS_LOST" in UI
    assert "lv_tick_elaps(ui->last_volume_send) >= 300" in UI
    assert "commands[PLAYBACK_SEEK].pending" in SOURCE
    assert "commands[PLAYBACK_VOLUME].pending" in SOURCE
    assert '"gateway/capabilities"' in SOURCE
    assert '"player_queues/seek"' in SOURCE
